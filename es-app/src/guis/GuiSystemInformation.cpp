#include "GuiSystemInformation.h"
#include "SystemConf.h"
#include "components/SwitchComponent.h"
#include "ThemeData.h"
#include "ApiSystem.h"
#include "views/UIModeController.h"
#include "utils/Platform.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include <cstdio>
#include <fstream>
#include <sstream>


GuiSystemInformation::GuiSystemInformation(Window* window) : GuiSettings(window, _("INFORMATION").c_str())
{
	auto theme = ThemeData::getMenuTheme();
	std::shared_ptr<Font> font = theme->Text.font;
	unsigned int color = theme->Text.color;

	bool warning = ApiSystem::getInstance()->isFreeSpaceLimit();

#if !defined(ROCKNIX)
	addGroup(_("INFORMATION"));

	addWithLabel(_("VERSION"), std::make_shared<TextComponent>(window, ApiSystem::getInstance()->getVersion(), font, color));
	addWithLabel(_("USER DISK USAGE"), std::make_shared<TextComponent>(window, ApiSystem::getInstance()->getFreeSpaceUserInfo(), font, warning ? 0xFF0000FF : color));
	addWithLabel(_("SYSTEM DISK USAGE"), std::make_shared<TextComponent>(window, ApiSystem::getInstance()->getFreeSpaceSystemInfo(), font, color));

	#ifndef WIN32
		std::string path = "/media";
		for (const auto & entry : Utils::FileSystem::getDirContent(path)) {
			if (entry != "/media/SHARE" && entry != "/media/BATOCERA") {
				addWithLabel(_("DISK USAGE") + " " + entry, std::make_shared<TextComponent>(window, ApiSystem::getInstance()->getFreeSpaceInfo(entry), font, warning ? 0xFF0000FF : color));
			}
		}
	#endif
#endif

	std::vector<std::string> infos = ApiSystem::getInstance()->getSystemInformations();
	if (infos.size() > 0)
	{
		addGroup(_("SYSTEM"));

		for (auto info : infos)
		{
			std::vector<std::string> tokens = Utils::String::split(info, ':');
			if (tokens.size() >= 2)
			{
				// concatenat the ending words
				std::string vname;
				for (unsigned int i = 1; i < tokens.size(); i++)
				{
					if (i > 1)
					{
						if (tokens.at(0).find("NETWORK IP ADDRESS"))
							vname += ":";
						else
							vname += " ";
					}
					vname += tokens.at(i);
				}

				addWithLabel(_(tokens.at(0).c_str()), std::make_shared<TextComponent>(window, vname, font, color));
			}
		}
	}

#ifdef ROCKNIX
	// Memory usage from /proc/meminfo
	{
		auto readMemInfo = [](const std::string& key) -> long {
			std::ifstream f("/proc/meminfo");
			std::string line;
			while (std::getline(f, line)) {
				if (line.find(key) == 0) {
					long val = 0;
					sscanf(line.c_str() + key.size(), " %ld", &val);
					return val; // kB
				}
			}
			return 0;
		};

		long totalKB = readMemInfo("MemTotal:");
		long availKB = readMemInfo("MemAvailable:");
		long swapTotalKB = readMemInfo("SwapTotal:");
		long swapFreeKB = readMemInfo("SwapFree:");

		addGroup(_("MEMORY"));

		long usedMB = (totalKB - availKB) / 1024;
		long totalMB = totalKB / 1024;
		std::string ramStr = std::to_string(usedMB) + " / " + std::to_string(totalMB) + " MB";
		addWithLabel(_("RAM USAGE"), std::make_shared<TextComponent>(window, ramStr, font, color));

		if (swapTotalKB > 0) {
			long swapUsedMB = (swapTotalKB - swapFreeKB) / 1024;
			long swapTotalMB = swapTotalKB / 1024;
			std::string swapStr = std::to_string(swapUsedMB) + " / " + std::to_string(swapTotalMB) + " MB";
			addWithLabel(_("SWAP USAGE"), std::make_shared<TextComponent>(window, swapStr, font, color));
		}

		// ZRAM stats
		std::ifstream zramStat("/sys/block/zram0/mm_stat");
		if (zramStat.good()) {
			long orig = 0, compr = 0;
			zramStat >> orig >> compr;
			if (compr > 0) {
				long origMB = orig / 1024 / 1024;
				long comprMB = compr / 1024 / 1024;
				std::string zramStr = std::to_string(comprMB) + " MB (" + std::to_string(origMB) + " MB data)";
				addWithLabel(_("ZRAM COMPRESSED"), std::make_shared<TextComponent>(window, zramStr, font, color));
			}
		}

		// KSM savings
		{
			std::ifstream ksmRun("/sys/kernel/mm/ksm/run");
			if (ksmRun.good()) {
				int running = 0;
				ksmRun >> running;
				if (running == 1) {
					std::ifstream ksmPages("/sys/kernel/mm/ksm/pages_sharing");
					if (ksmPages.good()) {
						long pages = 0;
						ksmPages >> pages;
						long savedMB = (pages * 4096) / 1024 / 1024;
						addWithLabel(_("KSM SAVINGS"), std::make_shared<TextComponent>(window,
							std::to_string(savedMB) + " MB", font, color));
					}
				}
			}
		}

		// Key VM tunables
		auto readSysctl = [](const std::string& path) -> std::string {
			std::ifstream f(path);
			std::string val;
			if (f.good()) std::getline(f, val);
			return val;
		};

		std::string swappiness = readSysctl("/proc/sys/vm/swappiness");
		if (!swappiness.empty())
			addWithLabel(_("SWAPPINESS"), std::make_shared<TextComponent>(window, swappiness, font, color));

		std::string compaction = readSysctl("/proc/sys/vm/compaction_proactiveness");
		if (!compaction.empty())
			addWithLabel(_("COMPACTION"), std::make_shared<TextComponent>(window, compaction, font, color));

		std::string maxMap = readSysctl("/proc/sys/vm/max_map_count");
		if (!maxMap.empty())
			addWithLabel(_("MAX MAP COUNT"), std::make_shared<TextComponent>(window, maxMap, font, color));
	}
#endif

	// Active storage profile
	if (Utils::FileSystem::exists("/usr/bin/profile-manager")) {
		std::string profileName = Utils::String::trim(
			Utils::Platform::GetShOutput(R"(/usr/bin/profile-manager)"));
		if (profileName.empty()) profileName = "Default";
		addWithLabel(_("STORAGE PROFILE"),
			std::make_shared<TextComponent>(window, profileName, font, color));
	}

	addGroup(_("VIDEO DRIVER"));
	for (auto info : Renderer::getDriverInformation())
		addWithLabel(_(info.first.c_str()), std::make_shared<TextComponent>(window, info.second, font, color));
}
