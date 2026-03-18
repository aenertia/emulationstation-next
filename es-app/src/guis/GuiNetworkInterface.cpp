#ifdef ROCKNIX

#include "GuiNetworkInterface.h"
#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"
#include "components/TextComponent.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiLoading.h"
#include "guis/GuiTextEditPopup.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "SystemConf.h"
#include "Settings.h"
#include "LocaleES.h"

GuiNetworkInterface::GuiNetworkInterface(Window* window, const std::string& ifaceName)
	: GuiSettings(window, (ifaceName + " " + _("SETTINGS")).c_str())
	, mIfaceName(ifaceName)
{
	auto theme = ThemeData::getMenuTheme();
	auto font = theme->Text.font;
	auto color = theme->Text.color;

	// Fetch initial state
	auto status = RxnmNetwork::getSystemStatus();
	RxnmNetwork::InterfaceInfo info;
	if (status.interfaces.count(ifaceName))
		info = status.interfaces[ifaceName];

	addGroup(_("STATUS"));

	mStateText = std::make_shared<TextComponent>(window,
		info.connected ? _("CONNECTED") : _("DISCONNECTED"), font, color);
	addWithLabel(_("STATE"), mStateText);

	mMacText = std::make_shared<TextComponent>(window,
		info.mac.empty() ? "N/A" : info.mac, font, color);
	addWithLabel(_("MAC ADDRESS"), mMacText);

	if (!info.driver.empty()) {
		auto driverText = std::make_shared<TextComponent>(window, info.driver, font, color);
		addWithLabel(_("DRIVER"), driverText);
	}

	// WiFi-specific info
	if (info.type == "wifi") {
		addGroup(_("WIFI"));

		mWifiSsidText = std::make_shared<TextComponent>(window,
			info.wifiSsid.empty() ? _("Not Connected") : info.wifiSsid, font, color);
		addWithLabel(_("SSID"), mWifiSsidText);

		mWifiRssiText = std::make_shared<TextComponent>(window,
			info.wifiRssi > -100 ? std::to_string(info.wifiRssi) + " dBm" : "N/A", font, color);
		addWithLabel(_("SIGNAL"), mWifiRssiText);

		if (info.wifiFrequency > 0) {
			std::string freqStr = std::to_string(info.wifiFrequency) + " MHz";
			if (info.wifiFrequency > 4900) freqStr += " (5 GHz)";
			else if (info.wifiFrequency > 2400) freqStr += " (2.4 GHz)";
			mWifiFreqText = std::make_shared<TextComponent>(window, freqStr, font, color);
		} else {
			mWifiFreqText = std::make_shared<TextComponent>(window, "N/A", font, color);
		}
		addWithLabel(_("FREQUENCY"), mWifiFreqText);

		if (!info.wifiBssid.empty()) {
			auto bssidText = std::make_shared<TextComponent>(window, info.wifiBssid, font, color);
			addWithLabel(_("BSSID"), bssidText);
		}

		// Forget current network
		if (!info.wifiSsid.empty()) {
			std::string ssid = info.wifiSsid;
			addEntry(_("FORGET THIS NETWORK"), false, [window, ssid] {
				window->pushGui(new GuiMsgBox(window,
					_("FORGET NETWORK") + " \"" + ssid + "\"?",
					_("YES"), [window, ssid] {
						window->pushGui(new GuiLoading<bool>(window, _("FORGETTING NETWORK..."),
							[ssid](auto gui) {
								RxnmNetwork::exec("wifi disconnect");
								RxnmNetwork::exec("wifi forget \"" + ssid + "\"");
								return RxnmNetwork::reload();
							},
							[window](bool success) {
								window->pushGui(new GuiMsgBox(window,
									success ? _("NETWORK FORGOTTEN") : _("FAILED")));
							}));
					},
					_("NO"), nullptr));
			});
		}

		// WiFi management - moved from main Network Settings page
		addGroup(_("WIFI MANAGEMENT"));

		addEntry(_("KNOWN NETWORKS"), true, [window] {
			window->pushGui(new GuiLoading<std::vector<RxnmNetwork::WifiNetwork>>(window,
				_("LOADING KNOWN NETWORKS..."),
				[](auto gui) { return RxnmNetwork::scanNetworks(); },
				[window](std::vector<RxnmNetwork::WifiNetwork> networks) {
					auto s2 = new GuiSettings(window, _("KNOWN NETWORKS"));
					for (auto& net : networks) {
						if (!net.known) continue;
						std::string label = net.ssid + "  (" + net.security + ")";
						if (net.connected) label += "  *";
						std::string ssid = net.ssid;
						s2->addEntry(label, true, [window, ssid, s2] {
							window->pushGui(new GuiMsgBox(window,
								_("FORGET NETWORK") + " \"" + ssid + "\"?",
								_("YES"), [window, ssid, s2] {
									window->pushGui(new GuiLoading<bool>(window,
										_("FORGETTING NETWORK..."),
										[ssid](auto gui) {
											RxnmNetwork::exec("wifi forget \"" + ssid + "\"");
											return RxnmNetwork::reload();
										},
										[window, s2](bool success) { delete s2; }));
								},
								_("NO"), nullptr));
						});
					}
					window->pushGui(s2);
				}));
		});

		addEntry(_("WIFI HOTSPOT"), true, [window] {
			auto s2 = new GuiSettings(window, _("WIFI HOTSPOT"));
			s2->addInputTextConfigRow(_("HOTSPOT SSID"), "wifi.ap.ssid", false);
			s2->addInputTextConfigRow(_("HOTSPOT PASSWORD"), "wifi.ap.key", true);

			s2->addEntry(_("START HOTSPOT"), false, [window] {
				std::string apSsid = SystemConf::getInstance()->get("wifi.ap.ssid");
				std::string apKey = SystemConf::getInstance()->get("wifi.ap.key");
				if (apSsid.empty()) { window->pushGui(new GuiMsgBox(window, _("PLEASE SET A HOTSPOT SSID"))); return; }
				window->pushGui(new GuiLoading<bool>(window, _("STARTING HOTSPOT..."),
					[apSsid, apKey](auto gui) {
						RxnmNetwork::exec("wifi ap start \"" + apSsid + "\" --password \"" + apKey + "\" --share");
						return RxnmNetwork::reload();
					},
					[window](bool ok) { window->pushGui(new GuiMsgBox(window, ok ? _("HOTSPOT STARTED") : _("HOTSPOT FAILED"))); }));
			});

			s2->addEntry(_("STOP HOTSPOT"), false, [window] {
				window->pushGui(new GuiLoading<bool>(window, _("STOPPING..."),
					[](auto gui) { RxnmNetwork::exec("wifi disconnect"); return RxnmNetwork::reload(); },
					[window](bool ok) { window->pushGui(new GuiMsgBox(window, _("HOTSPOT STOPPED"))); }));
			});
			window->pushGui(s2);
		});

		addEntry(_("CHECK INTERNET"), false, [window] {
			window->pushGui(new GuiLoading<bool>(window, _("CHECKING CONNECTIVITY..."),
				[](auto gui) { return RxnmNetwork::exec("system check internet"); },
				[window](bool ok) {
					window->pushGui(new GuiMsgBox(window, ok ? _("INTERNET: CONNECTED") : _("INTERNET: NOT CONNECTED")));
				}));
		});
	}

	addGroup(_("IPV4"));

	mIpv4Text = std::make_shared<TextComponent>(window,
		info.ipv4Address.empty() ? "N/A" : info.ipv4Address, font, color);
	addWithLabel(_("ADDRESS"), mIpv4Text);

	auto gw4 = std::make_shared<TextComponent>(window,
		info.ipv4Gateway.empty() ? "N/A" : info.ipv4Gateway, font, color);
	addWithLabel(_("GATEWAY"), gw4);

	addGroup(_("IPV6"));

	mIpv6Text = std::make_shared<TextComponent>(window,
		info.ipv6Address.empty() ? "N/A" : info.ipv6Address, font, color);
	addWithLabel(_("ADDRESS"), mIpv6Text);

	auto gw6 = std::make_shared<TextComponent>(window,
		info.ipv6Gateway.empty() ? "N/A" : info.ipv6Gateway, font, color);
	addWithLabel(_("GATEWAY"), gw6);

	addGroup(_("DETAILS"));

	auto mtuText = std::make_shared<TextComponent>(window,
		info.mtu > 0 ? std::to_string(info.mtu) : "N/A", font, color);
	addWithLabel(_("MTU"), mtuText);

	auto typeText = std::make_shared<TextComponent>(window,
		info.type.empty() ? "N/A" : info.type, font, color);
	addWithLabel(_("TYPE"), typeText);

	// IP Configuration (DHCP/Static)
	addGroup(_("IP CONFIGURATION"));

	std::string ifName = ifaceName;

	addEntry(_("SET DHCP"), false, [window, ifName] {
		window->pushGui(new GuiLoading<bool>(window, _("SETTING DHCP..."),
			[ifName](auto gui) { bool ok = RxnmNetwork::exec("interface " + ifName + " set dhcp"); RxnmNetwork::reload(); return ok; },
			[window](bool success) {
				window->pushGui(new GuiMsgBox(window,
					success ? _("DHCP CONFIGURED") : _("FAILED")));
			}));
	});

	addEntry(_("SET STATIC IP"), false, [window, ifName] {
		auto updateVal = [window, ifName](const std::string& ip) {
			if (ip.empty()) return;
			window->pushGui(new GuiLoading<bool>(window, _("SETTING STATIC IP..."),
				[ifName, ip](auto gui) { bool ok = RxnmNetwork::exec("interface " + ifName + " set static " + ip); RxnmNetwork::reload(); return ok; },
				[window](bool success) {
					window->pushGui(new GuiMsgBox(window,
						success ? _("STATIC IP CONFIGURED") : _("FAILED")));
				}));
		};
		if (Settings::getInstance()->getBool("UseOSK"))
			window->pushGui(new GuiTextEditPopupKeyboard(window, _("IP ADDRESS (CIDR)"), "", updateVal, false));
		else
			window->pushGui(new GuiTextEditPopup(window, _("IP ADDRESS (CIDR)"), "", updateVal, false));
	});

	addGroup(_("POWER MANAGEMENT"));

	auto nullifySwitch = std::make_shared<SwitchComponent>(window);
	bool initialNullify = info.isNullified;
	nullifySwitch->setState(initialNullify);
	addWithLabel(_("NULLIFY MODE"), nullifySwitch);
	addSaveFunc([nullifySwitch, ifName, initialNullify] {
		if (nullifySwitch->getState() != initialNullify)
			RxnmNetwork::exec(std::string("system nullify ") + (nullifySwitch->getState() ? "enable" : "disable") + " --interface " + ifName);
	});

	// Software Wake-on-LAN — enables remote wake via magic packets
	auto wolSwitch = std::make_shared<SwitchComponent>(window);
	std::string wolSetting = SystemConf::getInstance()->get("network." + ifName + ".soft_wol");
	wolSwitch->setState(wolSetting == "1");
	addWithLabel(_("SOFTWARE WAKE-ON-LAN"), wolSwitch);
	addSaveFunc([wolSwitch, ifName] {
		std::string val = wolSwitch->getState() ? "1" : "0";
		SystemConf::getInstance()->set("network." + ifName + ".soft_wol", val);
	});

	addGroup(_("PROFILES"));

	addEntry(_("SAVE PROFILE"), false, [window] {
		auto updateVal = [window](const std::string& name) {
			if (name.empty()) return;
			window->pushGui(new GuiLoading<bool>(window, _("SAVING PROFILE..."),
				[name](auto gui) { return RxnmNetwork::exec("profile save \"" + name + "\""); },
				[window](bool ok) { window->pushGui(new GuiMsgBox(window, ok ? _("PROFILE SAVED") : _("SAVE FAILED"))); }));
		};
		if (Settings::getInstance()->getBool("UseOSK"))
			window->pushGui(new GuiTextEditPopupKeyboard(window, _("PROFILE NAME"), "", updateVal, false));
		else
			window->pushGui(new GuiTextEditPopup(window, _("PROFILE NAME"), "", updateVal, false));
	});
}

void GuiNetworkInterface::update(int deltaTime)
{
	GuiSettings::update(deltaTime);

	mPollTimer += deltaTime;
	if (mPollTimer < POLL_INTERVAL_MS)
		return;
	mPollTimer = 0;

	// Check if we have a pending async result
	if (mPendingStatus.valid()) {
		if (mPendingStatus.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
			auto status = mPendingStatus.get();
			if (status.interfaces.count(mIfaceName))
				refreshFromStatus(status.interfaces[mIfaceName]);
		}
		return;
	}

	// Launch new async poll
	mPendingStatus = RxnmNetwork::getSystemStatusAsync();
}

void GuiNetworkInterface::refreshFromStatus(const RxnmNetwork::InterfaceInfo& info)
{
	if (mStateText)
		mStateText->setValue(info.connected ? _("CONNECTED") : _("DISCONNECTED"));
	if (mIpv4Text)
		mIpv4Text->setValue(info.ipv4Address.empty() ? "N/A" : info.ipv4Address);
	if (mIpv6Text)
		mIpv6Text->setValue(info.ipv6Address.empty() ? "N/A" : info.ipv6Address);
	if (mWifiSsidText)
		mWifiSsidText->setValue(info.wifiSsid.empty() ? _("Not Connected") : info.wifiSsid);
	if (mWifiRssiText)
		mWifiRssiText->setValue(info.wifiRssi > -100 ? std::to_string(info.wifiRssi) + " dBm" : "N/A");
	if (mWifiFreqText) {
		if (info.wifiFrequency > 0) {
			std::string freqStr = std::to_string(info.wifiFrequency) + " MHz";
			if (info.wifiFrequency > 4900) freqStr += " (5 GHz)";
			else if (info.wifiFrequency > 2400) freqStr += " (2.4 GHz)";
			mWifiFreqText->setValue(freqStr);
		} else {
			mWifiFreqText->setValue("N/A");
		}
	}
}

#endif // ROCKNIX
