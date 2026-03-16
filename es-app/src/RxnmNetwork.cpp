#include "RxnmNetwork.h"
#include "Log.h"

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

// Strip CIDR prefix notation: "192.168.1.1/24" -> "192.168.1.1"
static std::string stripCidr(const std::string& addr) {
    auto pos = addr.find('/');
    return (pos != std::string::npos) ? addr.substr(0, pos) : addr;
}

// Shell-escape a string for safe inclusion in double-quoted args
static std::string shellEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`')
            out += '\\';
        out += c;
    }
    return out;
}

std::string RxnmNetwork::execRxnm(const std::string& args)
{
    std::string cmd = "rxnm " + args + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG(LogError) << "RxnmNetwork: failed to run: " << cmd;
        return "";
    }

    std::string result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        result += buf;

    pclose(pipe);
    return result;
}

bool RxnmNetwork::parseSuccess(const std::string& json)
{
    if (json.empty())
        return false;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return false;

    if (doc.HasMember("success") && doc["success"].IsBool())
        return doc["success"].GetBool();
    if (doc.HasMember("connected") && doc["connected"].IsBool())
        return doc["connected"].GetBool();

    return false;
}

bool RxnmNetwork::isAvailable()
{
    return access("/usr/bin/rxnm", X_OK) == 0;
}

RxnmNetwork::SystemStatus RxnmNetwork::getSystemStatus()
{
    SystemStatus status;
    std::string json = execRxnm("system status --json");
    if (json.empty())
        return status;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return status;

    if (doc.HasMember("hostname") && doc["hostname"].IsString())
        status.hostname = doc["hostname"].GetString();

    if (doc.HasMember("online") && doc["online"].IsBool())
        status.isOnline = doc["online"].GetBool();

    if (doc.HasMember("nullify") && doc["nullify"].IsBool())
        status.globalNullify = doc["nullify"].GetBool();

    if (doc.HasMember("interfaces") && doc["interfaces"].IsObject()) {
        for (auto it = doc["interfaces"].MemberBegin(); it != doc["interfaces"].MemberEnd(); ++it) {
            if (!it->value.IsObject())
                continue;

            InterfaceInfo iface;
            iface.name = it->name.GetString();

            const auto& obj = it->value;

            if (obj.HasMember("type") && obj["type"].IsString())
                iface.type = obj["type"].GetString();

            // Skip loopback, bridge, sit/tunnel interfaces (by type or name)
            if (iface.type == "loopback" || iface.type == "bridge" || iface.type == "sit"
                || iface.type == "tunnel" || iface.name == "sit0" || iface.name == "lo")
                continue;

            // Also skip "unknown" type interfaces (e.g. sit0 variants)
            if (iface.type == "unknown")
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
            // Prefer explicit "connected" field from rxnm if present
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

            // IPv4 array — rxnm returns ["addr/prefix", ...], use first non-link-local
            if (obj.HasMember("ipv4") && obj["ipv4"].IsArray()) {
                const auto& v4 = obj["ipv4"];
                iface.ipv4Enabled = (v4.Size() > 0);
                for (rapidjson::SizeType i = 0; i < v4.Size(); i++) {
                    if (!v4[i].IsString()) continue;
                    std::string addr = stripCidr(v4[i].GetString());
                    // Prefer non-link-local (169.254.x.x) address
                    if (addr.substr(0, 8) != "169.254.") {
                        iface.ipv4Address = addr;
                        break;
                    }
                    if (iface.ipv4Address.empty())
                        iface.ipv4Address = addr;
                }
            }
            // Fallback: use "ip" field if ipv4 array was empty
            if (iface.ipv4Address.empty() && obj.HasMember("ip") && obj["ip"].IsString())
                iface.ipv4Address = stripCidr(obj["ip"].GetString());

            // IPv6 array — rxnm returns ["addr/prefix", ...], use first global
            if (obj.HasMember("ipv6") && obj["ipv6"].IsArray()) {
                const auto& v6 = obj["ipv6"];
                iface.ipv6Enabled = (v6.Size() > 0);
                for (rapidjson::SizeType i = 0; i < v6.Size(); i++) {
                    if (!v6[i].IsString()) continue;
                    std::string addr = stripCidr(v6[i].GetString());
                    // Prefer global over link-local (fe80::)
                    if (addr.substr(0, 5) != "fe80:") {
                        iface.ipv6Address = addr;
                        break;
                    }
                    if (iface.ipv6Address.empty())
                        iface.ipv6Address = addr;
                }
            }

            // Gateway — top-level field per schema
            if (obj.HasMember("gateway") && obj["gateway"].IsString())
                iface.ipv4Gateway = stripCidr(obj["gateway"].GetString());

            // Routes fallback — extract default gateway from routes array ("gw" per schema)
            // Skip IPv6 gateways (contain ':') when populating ipv4Gateway
            if (iface.ipv4Gateway.empty() && obj.HasMember("routes") && obj["routes"].IsArray()) {
                const auto& routes = obj["routes"];
                for (rapidjson::SizeType i = 0; i < routes.Size(); i++) {
                    if (!routes[i].IsObject()) continue;
                    const auto& r = routes[i];
                    if (r.HasMember("dst") && r["dst"].IsString()) {
                        std::string dst = r["dst"].GetString();
                        if (dst == "default" || dst == "0.0.0.0/0") {
                            if (r.HasMember("gw") && r["gw"].IsString()) {
                                std::string gw = stripCidr(r["gw"].GetString());
                                if (gw.find(':') == std::string::npos) {
                                    iface.ipv4Gateway = gw;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // Extract IPv6 default gateway from routes if not yet set
            if (iface.ipv6Gateway.empty() && obj.HasMember("routes") && obj["routes"].IsArray()) {
                for (auto& r : obj["routes"].GetArray()) {
                    if (!r.IsObject()) continue;
                    if (r.HasMember("dst") && r["dst"].IsString()
                        && std::string(r["dst"].GetString()) == "default"
                        && r.HasMember("gw") && r["gw"].IsString()) {
                        std::string gw = stripCidr(r["gw"].GetString());
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
    std::string cmd = "wifi scan --json";
    if (!iface.empty())
        cmd += " --interface " + iface;

    std::string json = execRxnm(cmd);
    if (json.empty())
        return networks;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return networks;

    if (!doc.HasMember("results") || !doc["results"].IsArray())
        return networks;

    for (auto& item : doc["results"].GetArray()) {
        if (!item.IsObject())
            continue;

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

std::vector<RxnmNetwork::WifiNetwork> RxnmNetwork::listNetworks(const std::string& iface)
{
    std::vector<WifiNetwork> networks;
    std::string cmd = "wifi networks --json";
    if (!iface.empty())
        cmd += " --interface " + iface;

    std::string json = execRxnm(cmd);
    if (json.empty())
        return networks;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return networks;

    if (!doc.HasMember("results") || !doc["results"].IsArray())
        return networks;

    for (auto& item : doc["results"].GetArray()) {
        if (!item.IsObject())
            continue;

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

std::vector<RxnmNetwork::KnownNetwork> RxnmNetwork::getKnownNetworks()
{
    std::vector<KnownNetwork> networks;
    std::string json = execRxnm("wifi list --json");
    if (json.empty())
        return networks;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return networks;

    if (!doc.HasMember("networks") || !doc["networks"].IsArray())
        return networks;

    for (auto& item : doc["networks"].GetArray()) {
        if (!item.IsObject()) continue;
        KnownNetwork net;
        if (item.HasMember("ssid") && item["ssid"].IsString())
            net.ssid = item["ssid"].GetString();
        if (item.HasMember("security") && item["security"].IsString())
            net.security = item["security"].GetString();
        if (item.HasMember("last_connected") && item["last_connected"].IsString())
            net.lastConnected = item["last_connected"].GetString();
        if (!net.ssid.empty())
            networks.push_back(net);
    }

    return networks;
}

bool RxnmNetwork::forgetNetwork(const std::string& ssid)
{
    return parseSuccess(execRxnm("wifi forget \"" + shellEscape(ssid) + "\" --json"));
}

bool RxnmNetwork::enableWifi(const std::string& ssid, const std::string& password,
                             const std::string& country)
{
    // 1. Unblock WiFi radio
    system("rfkill unblock wifi");

    // 2. Set regulatory country code if provided
    if (!country.empty())
        setCountry(country);

    // 3. Connect to network (rxnm internally calls reconfigure_iface for DHCP)
    bool result = connectWifi(ssid, password);

    // 4. Ensure networkd picks up any config changes
    execRxnm("system reload --json");

    return result;
}

bool RxnmNetwork::disableWifi()
{
    disconnectWifi();
    execRxnm("system reload --json");
    system("rfkill block wifi");
    return true;
}

bool RxnmNetwork::connectWifi(const std::string& ssid, const std::string& password, bool hidden)
{
    std::string cmd = "wifi connect \"" + shellEscape(ssid) + "\" --password \"" + shellEscape(password) + "\"";
    if (hidden)
        cmd += " --hidden";
    cmd += " --json";

    return parseSuccess(execRxnm(cmd));
}

bool RxnmNetwork::disconnectWifi()
{
    std::string json = execRxnm("wifi disconnect --json");
    return !json.empty();
}

bool RxnmNetwork::setCountry(const std::string& code)
{
    return parseSuccess(execRxnm("wifi country " + code + " --json"));
}

bool RxnmNetwork::startAP(const std::string& ssid, const std::string& password, bool share)
{
    std::string cmd = "wifi ap start \"" + shellEscape(ssid) + "\" --password \"" + shellEscape(password) + "\"";
    if (share)
        cmd += " --share";
    cmd += " --json";
    return parseSuccess(execRxnm(cmd));
}

bool RxnmNetwork::stopAP()
{
    return parseSuccess(execRxnm("wifi disconnect --json"));
}

std::string RxnmNetwork::getIpAddress()
{
    SystemStatus status = getSystemStatus();
    for (auto& pair : status.interfaces) {
        if (pair.second.connected && !pair.second.ipv4Address.empty())
            return pair.second.ipv4Address;
    }
    return "NOT CONNECTED";
}

bool RxnmNetwork::checkInternet()
{
    std::string json = execRxnm("system check internet --json");
    if (json.empty())
        return false;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return false;

    if (doc.HasMember("connected") && doc["connected"].IsBool())
        return doc["connected"].GetBool();
    if (doc.HasMember("success") && doc["success"].IsBool())
        return doc["success"].GetBool();

    return false;
}

bool RxnmNetwork::setGlobalNullify(bool enable)
{
    std::string cmd = std::string("system nullify ") + (enable ? "enable" : "disable") + " --json";
    return parseSuccess(execRxnm(cmd));
}

bool RxnmNetwork::setInterfaceNullify(const std::string& iface, bool enable)
{
    std::string cmd = std::string("system nullify ") + (enable ? "enable" : "disable")
        + " --interface " + iface + " --json";
    return parseSuccess(execRxnm(cmd));
}

bool RxnmNetwork::setInterfaceDhcp(const std::string& iface)
{
    return parseSuccess(execRxnm("interface " + iface + " set dhcp --json"));
}

bool RxnmNetwork::setInterfaceStatic(const std::string& iface, const std::string& ip,
                                     const std::string& gateway, const std::string& dns)
{
    std::string cmd = "interface " + iface + " set static " + ip;
    if (!gateway.empty())
        cmd += " --gateway " + gateway;
    if (!dns.empty())
        cmd += " --dns " + dns;
    cmd += " --json";
    return parseSuccess(execRxnm(cmd));
}

std::vector<std::string> RxnmNetwork::listProfiles()
{
    std::vector<std::string> profiles;
    std::string json = execRxnm("profile list --json");
    if (json.empty())
        return profiles;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return profiles;

    if (!doc.HasMember("profiles") || !doc["profiles"].IsArray())
        return profiles;

    for (auto& item : doc["profiles"].GetArray()) {
        if (item.IsString())
            profiles.push_back(item.GetString());
    }

    return profiles;
}

bool RxnmNetwork::saveProfile(const std::string& name)
{
    return parseSuccess(execRxnm("profile save \"" + shellEscape(name) + "\" --json"));
}

bool RxnmNetwork::loadProfile(const std::string& name)
{
    return parseSuccess(execRxnm("profile load \"" + shellEscape(name) + "\" --json"));
}

bool RxnmNetwork::vpnConnect(const VpnConfig& cfg)
{
    std::string cmd = "vpn wireguard connect \"" + shellEscape(cfg.name) + "\"";
    if (!cfg.privateKey.empty())
        cmd += " --private-key \"" + shellEscape(cfg.privateKey) + "\"";
    if (!cfg.peerKey.empty())
        cmd += " --peer-key \"" + shellEscape(cfg.peerKey) + "\"";
    if (!cfg.endpoint.empty())
        cmd += " --endpoint \"" + shellEscape(cfg.endpoint) + "\"";
    if (!cfg.allowedIps.empty())
        cmd += " --allowed-ips \"" + shellEscape(cfg.allowedIps) + "\"";
    if (!cfg.address.empty())
        cmd += " --address \"" + shellEscape(cfg.address) + "\"";
    cmd += " --json";
    return parseSuccess(execRxnm(cmd));
}

bool RxnmNetwork::vpnDisconnect(const std::string& name)
{
    return parseSuccess(execRxnm("vpn wireguard disconnect \"" + shellEscape(name) + "\" --json"));
}
