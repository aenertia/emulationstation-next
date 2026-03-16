#include "RxnmNetwork.h"
#include "Log.h"

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <cstdio>
#include <cstring>

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
    return system("which rxnm >/dev/null 2>&1") == 0;
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

            // Skip loopback, bridge, sit interfaces
            if (iface.type == "loopback" || iface.type == "bridge" || iface.type == "sit")
                continue;

            if (obj.HasMember("state") && obj["state"].IsString())
                iface.state = obj["state"].GetString();

            if (obj.HasMember("mac") && obj["mac"].IsString())
                iface.mac = obj["mac"].GetString();

            if (obj.HasMember("mtu") && obj["mtu"].IsInt())
                iface.mtu = obj["mtu"].GetInt();

            iface.connected = (iface.state == "connected" || iface.state == "routable");

            // IPv4
            if (obj.HasMember("ipv4") && obj["ipv4"].IsObject()) {
                const auto& v4 = obj["ipv4"];
                iface.ipv4Enabled = true;
                if (v4.HasMember("address") && v4["address"].IsString())
                    iface.ipv4Address = v4["address"].GetString();
                if (v4.HasMember("gateway") && v4["gateway"].IsString())
                    iface.ipv4Gateway = v4["gateway"].GetString();
            }

            // IPv6
            if (obj.HasMember("ipv6") && obj["ipv6"].IsObject()) {
                const auto& v6 = obj["ipv6"];
                iface.ipv6Enabled = true;
                if (v6.HasMember("address") && v6["address"].IsString())
                    iface.ipv6Address = v6["address"].GetString();
                if (v6.HasMember("gateway") && v6["gateway"].IsString())
                    iface.ipv6Gateway = v6["gateway"].GetString();
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

bool RxnmNetwork::connectWifi(const std::string& ssid, const std::string& password, bool hidden)
{
    std::string cmd = "wifi connect --ssid \"" + ssid + "\"";
    if (!password.empty())
        cmd += " --password \"" + password + "\"";
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

    if (doc.HasMember("status") && doc["status"].IsString())
        return std::string(doc["status"].GetString()) == "success";

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
