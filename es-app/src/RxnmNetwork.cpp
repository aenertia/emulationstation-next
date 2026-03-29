#ifdef ROCKNIX
#include "RxnmNetwork.h"
#include "Log.h"

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

// Shared utility: strip CIDR suffix from IP address string
std::string RxnmNetwork::stripCidr(const std::string& addr)
{
	auto pos = addr.find('/');
	return (pos != std::string::npos) ? addr.substr(0, pos) : addr;
}

// Build a JSON object string from category, action, and key-value params.
// Values are JSON-escaped (backslash, double-quote, control chars).
static std::string buildJson(const std::string& category, const std::string& action,
                             const std::map<std::string, std::string>& params)
{
	auto jsonEscape = [](const std::string& s) -> std::string {
		std::string out;
		out.reserve(s.size() + 8);
		for (char c : s) {
			switch (c) {
				case '\\': out += "\\\\"; break;
				case '"':  out += "\\\""; break;
				case '\n': out += "\\n";  break;
				case '\r': out += "\\r";  break;
				case '\t': out += "\\t";  break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						char hex[8];
						snprintf(hex, sizeof(hex), "\\u%04x", (unsigned)c);
						out += hex;
					} else {
						out += c;
					}
			}
		}
		return out;
	};

	std::string json = "{\"category\":\"" + jsonEscape(category) +
	                   "\",\"action\":\"" + jsonEscape(action) + "\"";
	json += ",\"format\":\"json\"";
	for (const auto& kv : params) {
		if (kv.first == "force" && kv.second == "true")
			json += ",\"force\":true";
		else if (kv.first == "share" && kv.second == "true")
			json += ",\"share\":true";
		else if (kv.first == "hidden" && kv.second == "true")
			json += ",\"hidden\":true";
		else
			json += ",\"" + jsonEscape(kv.first) + "\":\"" + jsonEscape(kv.second) + "\"";
	}
	json += "}";
	return json;
}

std::string RxnmNetwork::popen_read(const std::string& cmd)
{
	// Wrap with timeout to prevent indefinite hangs
	std::string wrapped = "timeout 5 " + cmd;
	FILE* pipe = popen(wrapped.c_str(), "r");
	if (!pipe) {
		LOG(LogError) << "RxnmNetwork: failed to run: " << cmd;
		return "";
	}

	std::string result;
	char buf[4096];
	while (fgets(buf, sizeof(buf), pipe))
		result += buf;

	int status = pclose(pipe);
	if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
		LOG(LogWarning) << "RxnmNetwork: command exited " << WEXITSTATUS(status) << ": " << cmd;
	else if (WIFSIGNALED(status))
		LOG(LogWarning) << "RxnmNetwork: command killed by signal " << WTERMSIG(status) << ": " << cmd;

	return result;
}

bool RxnmNetwork::exec(const std::string& category, const std::string& action,
                       const std::map<std::string, std::string>& params)
{
	return execJson(category, action, params);
}

bool RxnmNetwork::execJson(const std::string& category, const std::string& action,
                           const std::map<std::string, std::string>& params)
{
	std::string json = buildJson(category, action, params);

	// Write JSON directly to rxnm stdin via popen("w") — no shell interpolation
	FILE* pipe = popen("rxnm --stdin --format json 2>/dev/null", "w");
	if (!pipe) {
		LOG(LogError) << "RxnmNetwork::execJson: failed to start rxnm";
		return false;
	}

	fwrite(json.c_str(), 1, json.size(), pipe);

	int status = pclose(pipe);
	if (WIFSIGNALED(status)) {
		LOG(LogWarning) << "RxnmNetwork::execJson: rxnm killed by signal " << WTERMSIG(status);
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool RxnmNetwork::reload()
{
	return exec("system", "reload");
}

bool RxnmNetwork::isAvailable()
{
	return access("/usr/bin/rxnm", X_OK) == 0;
}

RxnmNetwork::SystemStatus RxnmNetwork::getSystemStatus()
{
	SystemStatus status;
	std::string json = popen_read("rxnm system status --format json 2>/dev/null");
	if (json.empty())
		return status;

	rapidjson::Document doc;
	doc.Parse(json.c_str());
	if (doc.HasParseError() || !doc.IsObject())
		return status;

	if (doc.HasMember("hostname") && doc["hostname"].IsString())
		status.hostname = doc["hostname"].GetString();
	if (doc.HasMember("domain") && doc["domain"].IsString())
		status.domain = doc["domain"].GetString();

	if (doc.HasMember("interfaces") && doc["interfaces"].IsObject()) {
		for (auto it = doc["interfaces"].MemberBegin(); it != doc["interfaces"].MemberEnd(); ++it) {
			if (!it->value.IsObject()) continue;

			InterfaceInfo iface;
			iface.name = it->name.GetString();
			const auto& obj = it->value;

			if (obj.HasMember("type") && obj["type"].IsString())
				iface.type = obj["type"].GetString();

			// Filter noise interfaces
			if (iface.type == "loopback" || iface.type == "sit" || iface.type == "unknown"
			    || iface.name == "sit0" || iface.name == "lo")
				continue;

			// Verify interface still exists on the system
			std::string sysPath = "/sys/class/net/" + iface.name;
			if (access(sysPath.c_str(), F_OK) != 0)
				continue;

			if (obj.HasMember("state") && obj["state"].IsString())
				iface.state = obj["state"].GetString();
			if (obj.HasMember("mac") && obj["mac"].IsString())
				iface.mac = obj["mac"].GetString();
			if (obj.HasMember("driver") && obj["driver"].IsString())
				iface.driver = obj["driver"].GetString();
			if (obj.HasMember("mtu") && obj["mtu"].IsInt())
				iface.mtu = obj["mtu"].GetInt();

			iface.connected = (iface.state == "connected" || iface.state == "routable");
			if (obj.HasMember("connected") && obj["connected"].IsBool())
				iface.connected = obj["connected"].GetBool();

			if (obj.HasMember("nullified") && obj["nullified"].IsBool())
				iface.isNullified = obj["nullified"].GetBool();

			// WiFi sub-object
			if (obj.HasMember("wifi") && obj["wifi"].IsObject()) {
				const auto& wifi = obj["wifi"];
				if (wifi.HasMember("ssid") && wifi["ssid"].IsString())
					iface.wifiSsid = wifi["ssid"].GetString();
				if (wifi.HasMember("bssid") && wifi["bssid"].IsString())
					iface.wifiBssid = wifi["bssid"].GetString();
				if (wifi.HasMember("rssi") && wifi["rssi"].IsInt())
					iface.wifiRssi = wifi["rssi"].GetInt();
				if (wifi.HasMember("frequency") && wifi["frequency"].IsInt())
					iface.wifiFrequency = wifi["frequency"].GetInt();
			}

			// IPv4 — handle both string "addr/prefix" and string array formats
			if (obj.HasMember("ip") && obj["ip"].IsString())
				iface.ipv4Address = stripCidr(obj["ip"].GetString());
			if (obj.HasMember("ipv4") && obj["ipv4"].IsArray()) {
				for (const auto& v : obj["ipv4"].GetArray()) {
					if (!v.IsString()) continue;
					std::string addr = stripCidr(v.GetString());
					if (addr.rfind("169.254.", 0) != 0) {
						iface.ipv4Address = addr;
						break;
					}
					if (iface.ipv4Address.empty())
						iface.ipv4Address = addr;
				}
			}

			// IPv6
			if (obj.HasMember("ipv6") && obj["ipv6"].IsArray()) {
				for (const auto& v : obj["ipv6"].GetArray()) {
					if (!v.IsString()) continue;
					std::string addr = stripCidr(v.GetString());
					if (addr.rfind("fe80:", 0) != 0) {
						iface.ipv6Address = addr;
						break;
					}
					if (iface.ipv6Address.empty())
						iface.ipv6Address = addr;
				}
			}

			// Gateway
			if (obj.HasMember("gateway") && obj["gateway"].IsString())
				iface.ipv4Gateway = obj["gateway"].GetString();
			if (iface.ipv4Gateway.empty() && obj.HasMember("routes") && obj["routes"].IsArray()) {
				for (const auto& r : obj["routes"].GetArray()) {
					if (!r.IsObject()) continue;
					if (r.HasMember("dst") && r["dst"].IsString()
					    && (std::string(r["dst"].GetString()) == "default")
					    && r.HasMember("gw") && r["gw"].IsString()) {
						std::string gw = r["gw"].GetString();
						if (gw.find(':') == std::string::npos) {
							iface.ipv4Gateway = gw;
							break;
						}
					}
				}
			}
			if (iface.ipv6Gateway.empty() && obj.HasMember("routes") && obj["routes"].IsArray()) {
				for (const auto& r : obj["routes"].GetArray()) {
					if (!r.IsObject()) continue;
					if (r.HasMember("dst") && r["dst"].IsString()
					    && (std::string(r["dst"].GetString()) == "default")
					    && r.HasMember("gw") && r["gw"].IsString()) {
						std::string gw = r["gw"].GetString();
						if (gw.find(':') != std::string::npos) {
							iface.ipv6Gateway = gw;
							break;
						}
					}
				}
			}

			status.interfaces[iface.name] = iface;
		}
	}

	return status;
}

std::future<RxnmNetwork::SystemStatus> RxnmNetwork::getSystemStatusAsync()
{
	return std::async(std::launch::async, getSystemStatus);
}

std::vector<RxnmNetwork::WifiNetwork> RxnmNetwork::scanNetworks(const std::string& iface)
{
	std::vector<WifiNetwork> networks;
	std::string cmd = "rxnm wifi scan --format json";
	if (!iface.empty())
		cmd += " --interface " + iface;
	cmd += " 2>/dev/null";

	std::string json = popen_read(cmd);
	if (json.empty()) return networks;

	rapidjson::Document doc;
	doc.Parse(json.c_str());
	if (doc.HasParseError() || !doc.IsObject()) return networks;
	if (!doc.HasMember("results") || !doc["results"].IsArray()) return networks;

	for (const auto& item : doc["results"].GetArray()) {
		if (!item.IsObject()) continue;
		WifiNetwork net;
		if (item.HasMember("ssid") && item["ssid"].IsString())
			net.ssid = item["ssid"].GetString();
		if (item.HasMember("security") && item["security"].IsString())
			net.security = item["security"].GetString();
		if (item.HasMember("strength_pct") && item["strength_pct"].IsInt())
			net.strengthPct = item["strength_pct"].GetInt();
		if (item.HasMember("connected") && item["connected"].IsBool())
			net.connected = item["connected"].GetBool();
		if (item.HasMember("known") && item["known"].IsBool())
			net.known = item["known"].GetBool();
		if (!net.ssid.empty())
			networks.push_back(net);
	}

	return networks;
}

std::vector<RxnmNetwork::BluetoothDevice> RxnmNetwork::listBluetoothDevices()
{
	std::vector<BluetoothDevice> devices;
	std::string json = popen_read("rxnm bluetooth list --format json 2>/dev/null");
	if (json.empty()) return devices;

	rapidjson::Document doc;
	doc.Parse(json.c_str());
	if (doc.HasParseError() || !doc.IsObject()) return devices;
	if (!doc.HasMember("devices") || !doc["devices"].IsArray()) return devices;

	for (const auto& item : doc["devices"].GetArray()) {
		if (!item.IsObject()) continue;
		BluetoothDevice dev;
		if (item.HasMember("mac") && item["mac"].IsString())
			dev.mac = item["mac"].GetString();
		if (item.HasMember("name") && item["name"].IsString())
			dev.name = item["name"].GetString();
		if (item.HasMember("icon") && item["icon"].IsString())
			dev.icon = item["icon"].GetString();
		if (item.HasMember("connected") && item["connected"].IsBool())
			dev.connected = item["connected"].GetBool();
		if (item.HasMember("paired") && item["paired"].IsBool())
			dev.paired = item["paired"].GetBool();
		if (!dev.mac.empty())
			devices.push_back(dev);
	}

	return devices;
}

std::string RxnmNetwork::getIpAddress()
{
	SystemStatus status = getSystemStatus();
	for (const auto& pair : status.interfaces) {
		if (pair.second.connected && !pair.second.ipv4Address.empty())
			return pair.second.ipv4Address;
	}
	return "NOT CONNECTED";
}

#endif // ROCKNIX
