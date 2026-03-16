#ifdef ROCKNIX

#include "GuiNetworkInterface.h"
#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"
#include "components/TextComponent.h"
#include "SystemConf.h"
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

	addGroup(_("POWER MANAGEMENT"));

	auto nullifySwitch = std::make_shared<SwitchComponent>(window);
	nullifySwitch->setState(info.isNullified);
	addWithLabel(_("NULLIFY MODE"), nullifySwitch);
	std::string ifName = ifaceName;
	addSaveFunc([nullifySwitch, ifName] {
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
}

#endif // ROCKNIX
