#pragma once

#include <string>
#include <vector>
#include <map>
#include <future>

// rxnm network manager — thin JSON bridge
// Pattern: fetch status JSON → parse → display
//          exec("rxnm <cmd>") → reload() → re-fetch status
// All mutations go through exec() which calls rxnm CLI.

class RxnmNetwork {
public:
    struct InterfaceInfo {
        std::string name;
        std::string type;   // "wifi", "ethernet", "gadget", "wireguard"
        std::string state;
        std::string mac;
        std::string driver;
        int mtu = 0;
        bool connected = false;

        std::string ipv4Address;
        std::string ipv4Gateway;
        std::string ipv6Address;
        std::string ipv6Gateway;

        bool isNullified = false;

        // WiFi-specific
        std::string wifiSsid;
        std::string wifiBssid;
        int wifiRssi = -100;
        int wifiFrequency = 0;
    };

    struct SystemStatus {
        std::string hostname;
        std::string domain;
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

    struct BluetoothDevice {
        std::string mac;
        std::string name;
        bool connected = false;
        bool paired = false;
    };

    // Status (read-only)
    static SystemStatus getSystemStatus();
    static std::future<SystemStatus> getSystemStatusAsync();
    static std::vector<WifiNetwork> scanNetworks(const std::string& iface = "");
    static std::vector<BluetoothDevice> listBluetoothDevices();

    // Generic mutator — calls "rxnm <args> --format json", returns exit code == 0
    static bool exec(const std::string& args);

    // Reload networkd to pick up config changes
    static bool reload();

    // IP address helper
    static std::string getIpAddress();

    // Check if rxnm binary is available
    static bool isAvailable();

private:
    static std::string popen_read(const std::string& cmd);
};
