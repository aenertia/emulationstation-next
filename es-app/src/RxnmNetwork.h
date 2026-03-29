#pragma once

#include <string>
#include <vector>
#include <map>
#include <future>

// rxnm network manager -- thin JSON bridge
// Pattern: fetch status JSON -> parse -> display
//          exec("category", "action", params) -> reload() -> re-fetch status
// All mutations go through execJson() which pipes JSON to rxnm stdin.

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
		std::string icon;   // BlueZ icon: "input-gaming", "audio-headphones", etc.
		bool connected = false;
		bool paired = false;
	};

	// Status (read-only)
	static SystemStatus getSystemStatus();
	static std::future<SystemStatus> getSystemStatusAsync();
	static std::vector<WifiNetwork> scanNetworks(const std::string& iface = "");
	static std::vector<BluetoothDevice> listBluetoothDevices();

	// Mutator -- builds JSON and pipes to rxnm stdin (no shell interpolation)
	static bool exec(const std::string& category, const std::string& action,
	                 const std::map<std::string, std::string>& params = {});

	// JSON-safe mutator -- pipes JSON input to rxnm via stdin
	static bool execJson(const std::string& category, const std::string& action,
	                     const std::map<std::string, std::string>& params = {});

	// Reload networkd to pick up config changes
	static bool reload();

	// IP address helper
	static std::string getIpAddress();

	// Check if rxnm binary is available
	static bool isAvailable();

	// Strip CIDR suffix from IP string (shared utility)
	static std::string stripCidr(const std::string& addr);

private:
	static std::string popen_read(const std::string& cmd);
};
