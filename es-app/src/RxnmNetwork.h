#pragma once

#include <string>
#include <vector>
#include <map>
#include <future>

// rxnm network manager C++ wrapper
// Calls rxnm CLI with JSON output and parses responses via rapidjson.
// All methods are static — no instance state.

class RxnmNetwork {
public:
    struct InterfaceInfo {
        std::string name;
        std::string type;   // "wifi", "ethernet", "gadget", "wireguard", "loopback"
        std::string state;  // "connected", "disconnected", "connecting", etc.
        std::string mac;
        std::string driver;
        int mtu = 0;
        bool connected = false;

        bool ipv4Enabled = false;
        std::string ipv4Address;
        std::string ipv4Gateway;

        bool ipv6Enabled = false;
        std::string ipv6Address;
        std::string ipv6Gateway;

        bool isNullified = false;

        // WiFi-specific (populated when type == "wifi")
        std::string wifiSsid;
        std::string wifiBssid;
        int wifiRssi = -100;
        int wifiFrequency = 0;
    };

    struct SystemStatus {
        std::string hostname;
        bool isOnline = false;
        bool globalNullify = false;
        std::map<std::string, InterfaceInfo> interfaces;
    };

    struct WifiNetwork {
        std::string ssid;
        std::string security;
        int strengthPct = 0;
        bool connected = false;
        bool known = false;
    };

    struct KnownNetwork {
        std::string ssid;
        std::string security;
        std::string lastConnected;
    };

    // System-wide status (rxnm system status --json)
    static SystemStatus getSystemStatus();
    static std::future<SystemStatus> getSystemStatusAsync();

    // WiFi operations — full lifecycle
    static bool enableWifi(const std::string& ssid, const std::string& password,
                           const std::string& country = "");
    static bool disableWifi();

    // WiFi operations — individual
    static std::vector<WifiNetwork> scanNetworks(const std::string& iface = "");
    static std::vector<WifiNetwork> listNetworks(const std::string& iface = "");
    static bool connectWifi(const std::string& ssid, const std::string& password, bool hidden = false);
    static bool disconnectWifi();

    // Known networks management
    static std::vector<KnownNetwork> getKnownNetworks();
    static bool forgetNetwork(const std::string& ssid);

    // WiFi AP / Hotspot
    static bool startAP(const std::string& ssid, const std::string& password, bool share = false);
    static bool stopAP();

    // Country code
    static bool setCountry(const std::string& code);

    // IP address helper (returns first connected interface's IPv4)
    static std::string getIpAddress();

    // Internet connectivity check
    static bool checkInternet();

    // Power management (Nullify Mode)
    static bool setGlobalNullify(bool enable);
    static bool setInterfaceNullify(const std::string& iface, bool enable);

    // Interface IP configuration
    static bool setInterfaceDhcp(const std::string& iface);
    static bool setInterfaceStatic(const std::string& iface, const std::string& ip,
                                   const std::string& gateway = "", const std::string& dns = "");

    // Profiles
    static std::vector<std::string> listProfiles();
    static bool saveProfile(const std::string& name);
    static bool loadProfile(const std::string& name);

    // VPN (WireGuard)
    struct VpnConfig {
        std::string name;
        std::string privateKey;
        std::string peerKey;
        std::string endpoint;
        std::string allowedIps;
        std::string address;
    };
    static bool vpnConnect(const VpnConfig& cfg);
    static bool vpnDisconnect(const std::string& name);

    // Check if rxnm binary is available
    static bool isAvailable();

private:
    // Execute rxnm command and capture JSON stdout
    static std::string execRxnm(const std::string& args);

    // Parse {"success": bool} response pattern
    static bool parseSuccess(const std::string& json);
};
