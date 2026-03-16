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

            if (obj.HasMember("state") && obj["state"].IsString())
                iface.state = obj["state"].GetString();

            if (obj.HasMember("mac") && obj["mac"].IsString())
                iface.mac = obj["mac"].GetString();

            if (obj.HasMember("mtu") && obj["mtu"].IsInt())
                iface.mtu = obj["mtu"].GetInt();

            iface.connected = (iface.state == "connected" || iface.state == "routable");

            if (obj.HasMember("nullified") && obj["nullified"].IsBool())
                iface.isNullified = obj["nullified"].GetBool();

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
            if (iface.ipv4Gateway.empty() && obj.HasMember("routes") && obj["routes"].IsArray()) {
                const auto& routes = obj["routes"];
                for (rapidjson::SizeType i = 0; i < routes.Size(); i++) {
                    if (!routes[i].IsObject()) continue;
                    const auto& r = routes[i];
                    if (r.HasMember("dst") && r["dst"].IsString()) {
                        std::string dst = r["dst"].GetString();
                        if (dst == "default" || dst == "0.0.0.0/0") {
                            if (r.HasMember("gw") && r["gw"].IsString())
                                iface.ipv4Gateway = stripCidr(r["gw"].GetString());
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

bool RxnmNetwork::enableWifi(const std::string& ssid, const std::string& password,
                             const std::string& country)
{
    // 1. Unblock WiFi radio
    system("rfkill unblock wifi");

    // 2. Set regulatory country code if provided
    if (!country.empty())
        execRxnm("wifi country " + country + " --json");

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
    std::string cmd = "wifi connect \"" + ssid + "\" --password \"" + password + "\"";
    if (hidden)
        cmd += " --hidden";
    cmd += " --json";

    std::string json = execRxnm(cmd);
    if (json.empty())
        return false;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError())
        return false;

    // rxnm OutputResponse: {"success": bool, ...}
    // ActionResponse may also have: {"connected": bool, "ssid": "...", ...}
    if (doc.HasMember("success") && doc["success"].IsBool())
        return doc["success"].GetBool();
    if (doc.HasMember("connected") && doc["connected"].IsBool())
        return doc["connected"].GetBool();

    return false;
}

bool RxnmNetwork::disconnectWifi()
{
    std::string json = execRxnm("wifi disconnect --json");
    return !json.empty();
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

bool RxnmNetwork::setGlobalNullify(bool enable)
{
    std::string cmd = std::string("system nullify ") + (enable ? "enable" : "disable") + " --json";
    std::string json = execRxnm(cmd);
    return !json.empty();
}

bool RxnmNetwork::setInterfaceNullify(const std::string& iface, bool enable)
{
    std::string cmd = std::string("system nullify ") + (enable ? "enable" : "disable")
        + " --interface " + iface + " --json";
    std::string json = execRxnm(cmd);
    return !json.empty();
}
