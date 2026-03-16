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
								RxnmNetwork::disconnectWifi();
								return RxnmNetwork::forgetNetwork(ssid);
							},
							[window](bool success) {
								window->pushGui(new GuiMsgBox(window,
									success ? _("NETWORK FORGOTTEN") : _("FAILED")));
							}));
					},
					_("NO"), nullptr));
			});
		}
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
			[ifName](auto gui) { return RxnmNetwork::setInterfaceDhcp(ifName); },
			[window](bool success) {
				window->pushGui(new GuiMsgBox(window,
					success ? _("DHCP CONFIGURED") : _("FAILED")));
			}));
	});

	addEntry(_("SET STATIC IP"), false, [window, ifName] {
		auto updateVal = [window, ifName](const std::string& ip) {
			if (ip.empty()) return;
			window->pushGui(new GuiLoading<bool>(window, _("SETTING STATIC IP..."),
				[ifName, ip](auto gui) { return RxnmNetwork::setInterfaceStatic(ifName, ip); },
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
			RxnmNetwork::setInterfaceNullify(ifName, nullifySwitch->getState());
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
