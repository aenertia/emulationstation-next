#pragma once

#ifdef ROCKNIX

#include "GuiSettings.h"
#include "RxnmNetwork.h"
#include "components/TextComponent.h"
#include <future>

// Per-interface network settings with live-updating state/IP fields
class GuiNetworkInterface : public GuiSettings
{
public:
	GuiNetworkInterface(Window* window, const std::string& ifaceName);
	void update(int deltaTime) override;

private:
	void refreshFromStatus(const RxnmNetwork::InterfaceInfo& info);

	std::string mIfaceName;

	// Live-updating text components
	std::shared_ptr<TextComponent> mStateText;
	std::shared_ptr<TextComponent> mIpv4Text;
	std::shared_ptr<TextComponent> mIpv6Text;
	std::shared_ptr<TextComponent> mMacText;

	// WiFi live-updating fields
	std::shared_ptr<TextComponent> mWifiSsidText;
	std::shared_ptr<TextComponent> mWifiRssiText;
	std::shared_ptr<TextComponent> mWifiFreqText;

	// Async polling
	std::future<RxnmNetwork::SystemStatus> mPendingStatus;
	int mPollTimer = 0;
	static const int POLL_INTERVAL_MS = 2000;
};

#endif // ROCKNIX
