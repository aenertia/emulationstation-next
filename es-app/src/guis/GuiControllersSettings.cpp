#include "GuiControllersSettings.h"

#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"

#include "guis/GuiDetectDevice.h"
#include "guis/GuiBluetoothPair.h"
#include "ThreadedBluetooth.h"
#include "guis/GuiBluetoothDevices.h"
#include "guis/GuiKeyboardtopads.h"

#include "guis/GuiMsgBox.h"
#include "GuiLoading.h"
#ifdef ROCKNIX
#include "RxnmNetwork.h"
#endif
#include "InputManager.h"
#include "SystemConf.h"

#include "utils/Platform.h"
#include "utils/FileSystemUtil.h"

#include <algorithm>
#include <cctype>
#include <map>

#define gettext_controllers_settings				_("CONTROLLER SETTINGS")
#define gettext_controllers_and_bluetooth_settings  _("CONTROLLER & BLUETOOTH SETTINGS")

#define gettext_controllers_priority _("CONTROLLERS PRIORITY")
#define gettext_controllers_player_assigments _("PLAYER ASSIGNMENTS")

#define gettext_controllerid _("CONTROLLER #%i")
#define gettext_playerid _("P%i'S CONTROLLER")

// Windows build does not have bluetooth support, so affect the label for Windows
#if WIN32
#define controllers_settings_label		gettext_controllers_settings
#define controllers_group_label		gettext_controllers_priority
#else
#define controllers_settings_label		gettext_controllers_and_bluetooth_settings
#define controllers_group_label		gettext_controllers_player_assigments
#endif

struct hotkeyInputDefinition
{
	std::string code;
	std::string name;
};
struct hotkeyTargetDefinition
{
	std::string code;
	std::string name;
};

std::string GuiControllersSettings::getControllersSettingsLabel()
{
	return controllers_settings_label;
}

void GuiControllersSettings::openControllersSettings(Window* wnd, int autoSel)
{
	wnd->pushGui(new GuiControllersSettings(wnd, autoSel));
}

GuiControllersSettings::GuiControllersSettings(Window* wnd, int autoSel) : GuiSettings(wnd, controllers_settings_label.c_str())
{
	Window* window = mWindow;

	addGroup(_("SETTINGS"));

	// Provides a mechanism to disable automatic hotkey assignment
	bool HotKeysEnabled = SystemConf::getInstance()->getBool("system.autohotkeys");
	auto autohotkeys = std::make_shared<SwitchComponent>(mWindow);
	autohotkeys->setState(HotKeysEnabled);
	addWithLabel(_("AUTOCONFIGURE RETROARCH HOTKEYS"), autohotkeys);
	addSaveFunc([autohotkeys] {
		SystemConf::getInstance()->setBool("system.autohotkeys", autohotkeys->getState());
	});

	// CONTROLLER CONFIGURATION
	addEntry(_("CONTROLLER MAPPING"), false, [window, this]
	{
		window->pushGui(new GuiMsgBox(window,
			_("YOU ARE GOING TO MAP A CONTROLLER. MAP BASED ON THE BUTTON'S POSITION, "
				"NOT ITS PHYSICAL LABEL. IF YOU DO NOT HAVE A SPECIAL BUTTON FOR HOTKEY, "
				"USE THE SELECT BUTTON. SKIP ALL BUTTONS/STICKS YOU DO NOT HAVE BY "
				"HOLDING ANY BUTTON. PRESS THE SOUTH BUTTON TO CONFIRM WHEN DONE."),
			_("OK"), [window, this] { window->pushGui(new GuiDetectDevice(window, false, [window, this] { Window* parent = window; setSave(false); delete this; openControllersSettings(parent); })); },
			_("CANCEL"), nullptr,
			GuiMsgBoxIcon::ICON_INFORMATION));
	});

	const std::string rumblePath = ApiSystem::getInstance()->getRumblePath();
	if (!rumblePath.empty()) {
		auto rumble_enabled = std::make_shared<SwitchComponent>(mWindow);
		bool rumbleEnabled = SystemConf::getInstance()->get("rumble.enabled") == "1";
		rumble_enabled->setState(rumbleEnabled);
		addWithLabel(_("ENABLE RUMBLE"), rumble_enabled);
		rumble_enabled->setOnChangedCallback([rumble_enabled, rumblePath] {
			if (rumble_enabled->getState() == true) {
				Utils::Platform::runSystemCommand("echo 1 >" + rumblePath, "", nullptr);
			} else {
				Utils::Platform::runSystemCommand("echo 0 >" + rumblePath, "", nullptr);
			}
			SystemConf::getInstance()->set("rumble.enabled", rumble_enabled->getState() ? "1" : "0");
		});
	}

	bool sindenguns_menu = false;
	bool wiiguns_menu = false;
	bool steamdeckguns_menu = false;

	for (auto gun : InputManager::getInstance()->getGuns())
	{
		sindenguns_menu |= gun->needBorders();
		wiiguns_menu |= gun->name() == "Wii Gun calibrated";
		steamdeckguns_menu |= gun->name() == "Steam Gun";
	}

	for (auto joy : InputManager::getInstance()->getInputConfigs())
		wiiguns_menu |= joy->getDeviceName() == "Nintendo Wii Remote";

	for (auto mouse : InputManager::getInstance()->getMice()) // the Steam Mouse can be converted to a Steam Gun via options
		steamdeckguns_menu |= mouse == "Steam Mouse";

	if (sindenguns_menu)
		addEntry(_("SINDEN GUN SETTINGS"), true, [this] { openControllersSpecificSettings_sindengun(); });

	if (wiiguns_menu)
		addEntry(_("WIIMOTE GUN SETTINGS"), true, [this] { openControllersSpecificSettings_wiigun(); });

	if (steamdeckguns_menu)
		addEntry(_("STEAMDECK MOUSE/GUN SETTINGS"), true, [this] { openControllersSpecificSettings_steamdeckgun(); });

	if (ApiSystem::getInstance()->isScriptingSupported(ApiSystem::BLUETOOTH))
	{
		addGroup(_("BLUETOOTH"));

#if defined(BATOCERA) || defined(ROCKNIX)
		// Bluetooth enable
		bool baseBtEnabled = SystemConf::getInstance()->getBool("controllers.bluetooth.enabled");
		auto enable_bt = std::make_shared<SwitchComponent>(mWindow);
		enable_bt->setState(baseBtEnabled);
		addWithLabel(_("ENABLE BLUETOOTH"), enable_bt, autoSel == 2);
		enable_bt->setOnChangedCallback([this, window, enable_bt, baseBtEnabled]
		{
			bool btEnabled = enable_bt->getState();
			if (btEnabled != baseBtEnabled)
			{
				SystemConf::getInstance()->setBool("controllers.bluetooth.enabled", btEnabled);
				SystemConf::getInstance()->saveSystemConf();
				if (btEnabled)
					ApiSystem::getInstance()->enableBluetooth();
				else
					ApiSystem::getInstance()->disableBluetooth();

				Window* parent = window;
				delete this;
				openControllersSettings(parent, 2);
			}
		});

		addSaveFunc([enable_bt]
		{
			bool btEnabled = enable_bt->getState();
			if (btEnabled != SystemConf::getInstance()->getBool("controllers.bluetooth.enabled"))
			{
				SystemConf::getInstance()->setBool("controllers.bluetooth.enabled", btEnabled);
				SystemConf::getInstance()->saveSystemConf();
				if (btEnabled)
					ApiSystem::getInstance()->enableBluetooth();
				else
					ApiSystem::getInstance()->disableBluetooth();
			}
		});

		if (baseBtEnabled)
		{
#endif

		// PAIR A BLUETOOTH CONTROLLER
		addEntry(_("PAIR BLUETOOTH PADS AUTOMATICALLY"), false, [window] { ThreadedBluetooth::start(window); });

#if defined(BATOCERA) || defined(ROCKNIX) || defined(WIN32)
		// PAIR A BLUETOOTH CONTROLLER OR BT AUDIO DEVICE
		addEntry(_("PAIR A BLUETOOTH DEVICE MANUALLY"), false, [window]
		{
			if (ThreadedBluetooth::isRunning())
				window->pushGui(new GuiMsgBox(window, _("BLUETOOTH SCAN IS ALREADY RUNNING.")));
			else
				window->pushGui(new GuiBluetoothPair(window));
		});
#endif
		// FORGET BLUETOOTH CONTROLLERS OR BT AUDIO DEVICES
		addEntry(_("BLUETOOTH DEVICE LIST"), false, [window] { window->pushGui(new GuiBluetoothDevices(window)); });

#ifdef ROCKNIX
		if (RxnmNetwork::isAvailable()) {
			addEntry(_("BLUETOOTH TETHERING"), true, [window] {
				auto s2 = new GuiSettings(window, _("BLUETOOTH TETHERING"));
				s2->addEntry(_("ENABLE PAN CLIENT"), false, [window] {
					window->pushGui(new GuiLoading<bool>(window, _("ENABLING BT TETHERING..."),
						[](auto gui) { return RxnmNetwork::exec("bluetooth pan enable --mode client"); },
						[window](bool ok) { window->pushGui(new GuiMsgBox(window, ok ? _("BT TETHERING ENABLED") : _("FAILED"))); }));
				});
				s2->addEntry(_("DISABLE PAN"), false, [window] {
					window->pushGui(new GuiLoading<bool>(window, _("DISABLING..."),
						[](auto gui) { return RxnmNetwork::exec("bluetooth pan disable"); },
						[window](bool ok) { window->pushGui(new GuiMsgBox(window, _("BT TETHERING DISABLED"))); }));
				});
				window->pushGui(s2);
			});
		}
#endif

#if defined(BATOCERA) || defined(ROCKNIX)
		}
#endif
	}

	addGroup(_("DISPLAY OPTIONS"));

	// CONTROLLER NOTIFICATION
	addSwitch(_("SHOW CONTROLLER NOTIFICATIONS"), "ShowControllerNotifications", true);

	// CONTROLLER ACTIVITY
	auto activity = std::make_shared<SwitchComponent>(mWindow);
	activity->setState(Settings::getInstance()->getBool("ShowControllerActivity"));
	addWithLabel(_("SHOW CONTROLLER ACTIVITY"), activity, autoSel == 1);
	activity->setOnChangedCallback([this, window, activity]
	{
		if (Settings::getInstance()->setBool("ShowControllerActivity", activity->getState()))
		{
			Window* parent = window;
			delete this;
			openControllersSettings(parent, 1);
		}
	});

	if (Settings::getInstance()->getBool("ShowControllerActivity"))
		addSwitch(_("SHOW CONTROLLER BATTERY LEVEL"), "ShowControllerBattery", true);

	addSwitch(_("SHOW GUN NOTIFICATIONS"), "ShowGunsNotifications", true);	
	addSwitch(_("DRAW GUN CROSSHAIR"), "DrawGunCrosshair", true);

#ifdef ROCKNIX
	// CONTROLLER OUTPUT — expose device as gamepad to external hosts via rxjoy or legacy bridge
	bool hasRxjoy = Utils::FileSystem::exists("/usr/bin/rxjoy");
	bool hasGadgetController = Utils::FileSystem::exists("/usr/bin/gadget-controller");
	if (hasRxjoy || hasGadgetController)
	{
		addGroup(_("CONTROLLER OUTPUT"));

		// Output Mode
		auto outputMode = std::make_shared<OptionListComponent<std::string>>(mWindow, _("OUTPUT MODE"), false);
		std::string curMode = SystemConf::getInstance()->get("system.controller_output.mode");
		outputMode->add(_("DISABLED"), "disabled", curMode != "usb" && curMode != "bluetooth");
		outputMode->add(_("USB GADGET"), "usb", curMode == "usb");
		if (hasRxjoy)
			outputMode->add(_("BLUETOOTH"), "bluetooth", curMode == "bluetooth");
		addWithLabel(_("OUTPUT MODE"), outputMode);

		// Output Profile — rxjoy profiles or legacy type selector
		auto outputProfile = std::make_shared<OptionListComponent<std::string>>(mWindow, _("OUTPUT PROFILE"), false);
		std::string curProfile = SystemConf::getInstance()->get("system.controller_output.profile");
		if (hasRxjoy)
		{
			if (curProfile.empty()) curProfile = "dinput";

			// Console controllers
			outputProfile->add(_("GENERIC HID (DInput)"), "dinput", curProfile == "dinput");
			outputProfile->add(_("XBOX 360 (XInput)"), "xinput", curProfile == "xinput");
			outputProfile->add(_("XBOX ONE"), "xb-one", curProfile == "xb-one");
			outputProfile->add(_("XBOX ORIGINAL"), "xboxog", curProfile == "xboxog");
			outputProfile->add(_("PLAYSTATION 3"), "ps3", curProfile == "ps3");
			outputProfile->add(_("PLAYSTATION 4"), "ps4", curProfile == "ps4");
			outputProfile->add(_("PLAYSTATION 5"), "ps5", curProfile == "ps5");
			outputProfile->add(_("SWITCH PRO"), "switch", curProfile == "switch");
			outputProfile->add(_("PS CLASSIC"), "ps-classic", curProfile == "ps-classic");

			// Nintendo wireless
			outputProfile->add(_("WIIMOTE"), "wiimote", curProfile == "wiimote");
			outputProfile->add(_("WIIMOTE + NUNCHUK"), "wiimote-nunchuk", curProfile == "wiimote-nunchuk");
			outputProfile->add(_("WII CLASSIC"), "wii-classic", curProfile == "wii-classic");
			outputProfile->add(_("WII U PRO"), "wii-u-pro", curProfile == "wii-u-pro");

			// Adapters
			outputProfile->add(_("GAMECUBE ADAPTER"), "gc-adapter", curProfile == "gc-adapter");
			outputProfile->add(_("N64"), "n64", curProfile == "n64");

			// Specialty
			outputProfile->add(_("KEYBOARD"), "keyboard", curProfile == "keyboard");
			outputProfile->add(_("ARCADE STICK"), "arcade-stick", curProfile == "arcade-stick");
			outputProfile->add(_("DANCE PAD"), "dance-pad", curProfile == "dance-pad");
			outputProfile->add(_("FLIGHT STICK"), "flight-stick", curProfile == "flight-stick");

			// Rhythm instruments
			outputProfile->add(_("GH GUITAR"), "gh-guitar", curProfile == "gh-guitar");
			outputProfile->add(_("RB GUITAR"), "rb-guitar", curProfile == "rb-guitar");
			outputProfile->add(_("PC GUITAR"), "pc-guitar", curProfile == "pc-guitar");
			outputProfile->add(_("GH DRUMS"), "gh-drums", curProfile == "gh-drums");
			outputProfile->add(_("RB DRUMS"), "rb-drums", curProfile == "rb-drums");
			outputProfile->add(_("TURNTABLE"), "turntable", curProfile == "turntable");
		}
		else
		{
			// Legacy: basic type selector
			if (curProfile.empty()) curProfile = "xbox";
			outputProfile->add(_("XBOX SERIES"), "xbox", curProfile == "xbox");
			outputProfile->add(_("DUALSENSE"), "ds5", curProfile == "ds5");
			outputProfile->add(_("SWITCH PRO"), "switch", curProfile == "switch");
			outputProfile->add(_("GENERIC HID"), "generic", curProfile == "generic");
		}
		addWithLabel(_("OUTPUT PROFILE"), outputProfile);

		// Controller Audio (rxjoy only)
		std::shared_ptr<OptionListComponent<std::string>> outputAudio;
		if (hasRxjoy)
		{
			outputAudio = std::make_shared<OptionListComponent<std::string>>(mWindow, _("CONTROLLER AUDIO"), false);
			std::string curAudio = SystemConf::getInstance()->get("system.controller_output.audio");
			if (curAudio.empty()) curAudio = "off";
			outputAudio->add(_("OFF"), "off", curAudio == "off");
			outputAudio->add(_("USB (UAC2)"), "usb", curAudio == "usb");
			outputAudio->add(_("BLUETOOTH (HFP)"), "bluetooth", curAudio == "bluetooth");
			addWithLabel(_("CONTROLLER AUDIO"), outputAudio);
		}

		addSaveFunc([outputMode, outputProfile, outputAudio, hasRxjoy] {
			std::string mode = outputMode->getSelected();
			std::string profile = outputProfile->getSelected();
			std::string prevMode = SystemConf::getInstance()->get("system.controller_output.mode");
			std::string prevProfile = SystemConf::getInstance()->get("system.controller_output.profile");
			bool changed = (mode != prevMode) || (profile != prevProfile);

			SystemConf::getInstance()->set("system.controller_output.mode", mode);
			SystemConf::getInstance()->set("system.controller_output.profile", profile);

			if (hasRxjoy && outputAudio)
			{
				std::string audio = outputAudio->getSelected();
				if (audio != SystemConf::getInstance()->get("system.controller_output.audio"))
					changed = true;
				SystemConf::getInstance()->set("system.controller_output.audio", audio);
			}

			if (changed)
			{
				if (mode == "disabled")
					Utils::Platform::runSystemCommand("/usr/bin/usbgadget disabled 2>/dev/null", "", nullptr);
				else if (mode == "usb")
					Utils::Platform::runSystemCommand("/usr/bin/usbgadget controller 2>/dev/null", "", nullptr);
				else if (mode == "bluetooth" && hasRxjoy)
					Utils::Platform::runSystemCommand("systemctl start rxjoy@" + profile + " 2>/dev/null", "", nullptr);
			}
		});
	}

	// INPUTPLUMBER TARGET — change what internal controller appears as
	if (Utils::FileSystem::exists("/usr/bin/inputplumber"))
	{
		addGroup(_("INTERNAL CONTROLLER"));

		auto ipTarget = std::make_shared<OptionListComponent<std::string>>(mWindow, _("CONTROLLER MODE"), false);
		std::string curTarget = SystemConf::getInstance()->get("system.inputplumber.target");
		if (curTarget.empty()) curTarget = "xbox-series";
		ipTarget->add(_("XBOX SERIES"), "xbox-series", curTarget == "xbox-series");
		ipTarget->add(_("DUALSENSE"), "ds5", curTarget == "ds5");
		ipTarget->add(_("STEAM DECK"), "deck", curTarget == "deck");
		ipTarget->add(_("GENERIC GAMEPAD"), "gamepad", curTarget == "gamepad");
		addWithLabel(_("CONTROLLER MODE"), ipTarget);
		addSaveFunc([ipTarget] {
			std::string target = ipTarget->getSelected();
			std::string prev = SystemConf::getInstance()->get("system.inputplumber.target");
			if (target != prev) {
				SystemConf::getInstance()->set("system.inputplumber.target", target);
				// Switch InputPlumber target via DBus
				Utils::Platform::runSystemCommand(
					"for dev in $(busctl tree --list org.shadowblip.InputPlumber 2>/dev/null | grep CompositeDevice); do "
					"busctl call org.shadowblip.InputPlumber \"$dev\" "
					"org.shadowblip.Input.CompositeDevice SetTargetDevices as 1 \"" + target + "\" 2>/dev/null; done", "", nullptr);
			}
		});

		// Default Profile — loaded when not running an emulator
		// Scan system profiles + user dropins (user overrides system with same name)
		const std::string sysProfileDir = "/usr/share/inputplumber/profiles";
		const std::string userProfileDir = "/storage/.config/inputplumber/profiles";
		if (Utils::FileSystem::isDirectory(sysProfileDir))
		{
			auto ipProfile = std::make_shared<OptionListComponent<std::string>>(mWindow, _("DEFAULT PROFILE"), false);
			std::string curProfileName = SystemConf::getInstance()->get("system.inputplumber.default_profile");
			if (curProfileName.empty()) curProfileName = "default";

			// Collect profiles: stem → full path (user dir wins on conflict)
			std::map<std::string, std::string> profileMap;
			for (const auto& dir : { sysProfileDir, userProfileDir })
			{
				if (!Utils::FileSystem::isDirectory(dir))
					continue;
				for (const auto& path : Utils::FileSystem::getDirContent(dir, false, false))
				{
					std::string filename = Utils::FileSystem::getFileName(path);
					if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".yaml")
						continue;
					std::string stem = filename.substr(0, filename.size() - 5);
					profileMap[stem] = path;  // later dir (user) overwrites earlier (system)
				}
			}

			for (const auto& kv : profileMap)
			{
				std::string label = kv.first;
				std::transform(label.begin(), label.end(), label.begin(), ::toupper);
				std::replace(label.begin(), label.end(), '-', ' ');
				// Mark user-supplied profiles
				if (kv.second.find(userProfileDir) == 0)
					label += " *";
				ipProfile->add(label, kv.first, kv.first == curProfileName);
			}

			if (!ipProfile->hasSelection())
				ipProfile->selectFirstItem();

			addWithLabel(_("DEFAULT PROFILE"), ipProfile);
			addSaveFunc([ipProfile, sysProfileDir, userProfileDir] {
				std::string selected = ipProfile->getSelected();
				std::string prev = SystemConf::getInstance()->get("system.inputplumber.default_profile");
				if (selected != prev) {
					SystemConf::getInstance()->set("system.inputplumber.default_profile", selected);
					// Prefer user profile, fall back to system
					std::string fullPath = userProfileDir + "/" + selected + ".yaml";
					if (!Utils::FileSystem::exists(fullPath))
						fullPath = sysProfileDir + "/" + selected + ".yaml";
					Utils::Platform::runSystemCommand(
						"for dev in $(busctl tree --list org.shadowblip.InputPlumber 2>/dev/null | grep CompositeDevice); do "
						"busctl call org.shadowblip.InputPlumber \"$dev\" "
						"org.shadowblip.Input.CompositeDevice LoadProfilePath "
						"s \"" + fullPath + "\" 2>/dev/null; done", "", nullptr);
				}
			});
		}
	}

	// GLOBAL HOTKEYS — input_sense key bindings
	addGroup(_("SYSTEM HOTKEYS"));
	addEntry(_("CONFIGURE HOTKEYS"), true, [this] { openInputSenseHotkeys(); });
#endif

#ifdef BATOCERA
	addGroup(_("BEHAVIOR"));

	addEntry(_("JOYSTICKS HOTKEYS"), true, [this] { openControllersHotkeys(); });

	auto restrictHotkeys = std::make_shared<SwitchComponent>(window);
	restrictHotkeys->setState(SystemConf::getInstance()->getBool("global.exithotkeyonly"));
	addWithLabel(_("RESTRICT JOYSTICKS HOTKEYS TO EXIT"), restrictHotkeys);
	addSaveFunc([restrictHotkeys] { SystemConf::getInstance()->setBool("global.exithotkeyonly", restrictHotkeys->getState()); });

	addEntry(_("GLOBAL HOTKEYS"), true, [this] { openGlobalHotkeys(); });
	addEntry(_("KEYBOARDTOPADS"), true, [this] { openKeyboardtopads(); });
#endif

	addGroup(controllers_group_label);

	// Here we go; for each player
	std::list<int> alreadyTaken = std::list<int>();

	// clear the current loaded inputs
	clearLoadedInput();

	std::vector<std::shared_ptr<OptionListComponent<InputConfigInfo*>>> options;

	auto configList = InputManager::getInstance()->getInputConfigs();

#if WIN32
	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		std::string label = Utils::String::format(gettext_controllerid.c_str(), player + 1);

		auto inputOptionList = std::make_shared<OptionListComponent<InputConfigInfo*> >(mWindow, label, false);
		inputOptionList->add(_("default"), nullptr, false);
		options.push_back(inputOptionList);

		// Checking if a setting has been saved, else setting to default
		std::string configuratedName = Settings::getInstance()->getString(Utils::String::format("INPUT P%iNAME", player + 1));
		std::string configuratedGuid = Settings::getInstance()->getString(Utils::String::format("INPUT P%iGUID", player + 1));
		std::string configuratedPath = Settings::getInstance()->getString(Utils::String::format("INPUT P%iPATH", player + 1));

		bool found = false;

		// Add configurated controller even if it's disconnected
		InputConfigInfo* defaultInputConfig = nullptr;

		if (!configuratedPath.empty())
		{
			InputConfigInfo* newInputConfig = new InputConfigInfo(configuratedName, configuratedGuid, configuratedPath);
			mLoadedInput.push_back(newInputConfig);

			auto it = std::find_if(configList.cbegin(), configList.cend(), [configuratedPath](InputConfig* x) { return x->getSortDevicePath() == configuratedPath; });
			if (it != configList.cend())
			{
				if (std::find(alreadyTaken.begin(), alreadyTaken.end(), (*it)->getDeviceId()) == alreadyTaken.end())
				{
					inputOptionList->addEx(configuratedName, configuratedPath, newInputConfig, true, false, false);
					alreadyTaken.push_back((*it)->getDeviceId());
					defaultInputConfig = newInputConfig;
				}
			}
			else
				inputOptionList->addEx(configuratedName + " (" + _("NOT CONNECTED") + ")", configuratedPath, newInputConfig, true, false, false);

			found = true;
		}

		// for each available and configured input
		for (auto config : configList)
		{
			if (defaultInputConfig != nullptr && defaultInputConfig->name == config->getDeviceName() && defaultInputConfig->guid == config->getDeviceGUIDString() && defaultInputConfig->path == config->getSortDevicePath())
				continue;

			std::string displayName = config->getDeviceName();

			bool foundFromConfig = !configuratedPath.empty() ? config->getSortDevicePath() == configuratedPath : configuratedName == config->getDeviceName() && configuratedGuid == config->getDeviceGUIDString();
			int deviceID = config->getDeviceId();

			InputConfigInfo* newInputConfig = new InputConfigInfo(config->getDeviceName(), config->getDeviceGUIDString(), config->getSortDevicePath());
			mLoadedInput.push_back(newInputConfig);

			if (foundFromConfig && std::find(alreadyTaken.begin(), alreadyTaken.end(), deviceID) == alreadyTaken.end() && !found)
			{
				found = true;
				alreadyTaken.push_back(deviceID);

				LOG(LogWarning) << "adding entry for player" << player << " (selected): " << config->getDeviceName() << "  " << config->getDeviceGUIDString() << "  " << config->getDevicePath();
				inputOptionList->addEx(displayName, config->getDevicePath(), newInputConfig, true, false, false);
			}
			else
			{
				LOG(LogInfo) << "adding entry for player" << player << " (not selected): " << config->getDeviceName() << "  " << config->getDeviceGUIDString() << "  " << config->getDevicePath();
				inputOptionList->addEx(displayName, config->getDevicePath(), newInputConfig, false, false, false);
			}
		}

		if (!inputOptionList->hasSelection())
			inputOptionList->selectFirstItem();

		// Populate controllers list
		addWithLabel(label, inputOptionList);
	}
#else
	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		std::string confName = Utils::String::format("INPUT P%iNAME", player + 1);
		std::string confGuid = Utils::String::format("INPUT P%iGUID", player + 1);
		std::string confPath = Utils::String::format("INPUT P%iPATH", player + 1);

		std::string label = Utils::String::format(gettext_playerid.c_str(), player + 1);

		auto inputOptionList = std::make_shared<OptionListComponent<InputConfigInfo*> >(mWindow, label, false);
		inputOptionList->add(_("default"), nullptr, false);
		options.push_back(inputOptionList);

		// Checking if a setting has been saved, else setting to default
		std::string configuratedName = Settings::getInstance()->getString(confName);
		std::string configuratedGuid = Settings::getInstance()->getString(confGuid);
		std::string configuratedPath = Settings::getInstance()->getString(confPath);

		bool found = false;

		// For each available and configured input
		for (auto config : configList)
		{
			std::string displayName = "#" + std::to_string(config->getDeviceIndex()) + " " + config->getDeviceName();
			bool foundFromConfig = !configuratedPath.empty() ? config->getSortDevicePath() == configuratedPath : configuratedName == config->getDeviceName() && configuratedGuid == config->getDeviceGUIDString();

			int deviceID = config->getDeviceId();

			InputConfigInfo* newInputConfig = new InputConfigInfo(config->getDeviceName(), config->getDeviceGUIDString(), config->getSortDevicePath());
			mLoadedInput.push_back(newInputConfig);

			if (foundFromConfig && std::find(alreadyTaken.begin(), alreadyTaken.end(), deviceID) == alreadyTaken.end() && !found)
			{
				found = true;
				alreadyTaken.push_back(deviceID);

				LOG(LogWarning) << "adding entry for player" << player << " (selected): " << config->getDeviceName() << "  " << config->getDeviceGUIDString() << "  " << config->getDevicePath();
				inputOptionList->add(displayName, newInputConfig, true);
			}
			else
			{
				LOG(LogInfo) << "adding entry for player" << player << " (not selected): " << config->getDeviceName() << "  " << config->getDeviceGUIDString() << "  " << config->getDevicePath();
				inputOptionList->add(displayName, newInputConfig, false);
			}
		}

		if (!inputOptionList->hasSelection())
			inputOptionList->selectFirstItem();

		// Populate controllers list
		addWithLabel(label, inputOptionList);
	}
#endif

	addSaveFunc([this, options, window]
	{
		bool changed = false;

		for (int player = 0; player < MAX_PLAYERS; player++)
		{
			std::string confName = Utils::String::format("INPUT P%iNAME", player + 1);
			std::string confGuid = Utils::String::format("INPUT P%iGUID", player + 1);
			std::string confPath = Utils::String::format("INPUT P%iPATH", player + 1);

			auto input = options.at(player);

			InputConfigInfo* selected = input->getSelected();
			if (selected == nullptr)
			{
				changed |= Settings::getInstance()->setString(confName, "DEFAULT");
				changed |= Settings::getInstance()->setString(confGuid, "");
				changed |= Settings::getInstance()->setString(confPath, "");
			}
			else if (input->changed())
			{
				LOG(LogInfo) << "Found the selected controller : " << input->getSelectedName() << ", " << selected->guid << ", " << selected->path;

				changed |= Settings::getInstance()->setString(confName, selected->name);
				changed |= Settings::getInstance()->setString(confGuid, selected->guid);
				changed |= Settings::getInstance()->setString(confPath, selected->path);
			}
		}

		if (changed)
			Settings::getInstance()->saveFile();

		// this is dependant of this configuration, thus update it
		InputManager::getInstance()->computeLastKnownPlayersDeviceIndexes();
	});
}

void GuiControllersSettings::openControllersHotkeys()
{
	GuiSettings* s = new GuiSettings(mWindow, _("JOYSTICKS HOTKEYS"));

	std::vector<hotkeyInputDefinition> keys_labels = 
	{
	  { "b",        _("SOUTH") },
	  { "a",        _("EAST") },
	  { "x",        _("NORTH") },
	  { "y",        _("WEST") },
	  { "start",    _("START") },
	  { "select",   _("SELECT") },
	  { "up",       _("D-PAD UP") },
	  { "down",     _("D-PAD DOWN") },
	  { "left",     _("D-PAD LEFT") },
	  { "right",    _("D-PAD RIGHT") },
	  { "pageup",   _("LEFT SHOULDER") },
	  { "pagedown", _("RIGHT SHOULDER") },
	  { "l2",       _("LEFT TRIGGER") },
	  { "r2",       _("RIGHT TRIGGER") },
	  { "l3",       _("LEFT STICK PRESS") },
	  { "r3",       _("RIGHT STICK PRESS") }
	};

	std::vector<hotkeyTargetDefinition> targets_labels = 
	{
		{ "bezels",           _("OVERLAYS") },
		{ "brightness-cycle", _("BRIGHTNESS CYCLE") },
		{ "controlcenter",    _("CONTROL CENTER") },
		{ "exit",             _("EXIT") },
		{ "pause",            _("PAUSE") },
		{ "menu",             _("MENU") },
		{ "files",            _("FILES") },
		{ "coin",             _("COIN") },
		{ "fastforward",      _("FAST FORWARD") },
		{ "next_disk",        _("NEXT DISK") },
		{ "next_slot",        _("NEXT SLOT") },
		{ "previous_slot",    _("PREVIOUS SLOT") },
		{ "reset",            _("RESET") },
		{ "save_state",       _("SAVE STATE") },
		{ "restore_state",    _("RESTORE STATE") },
		{ "rewind",           _("REWIND") },
		{ "screen_layout",    _("SCREEN LAYOUT") },
		{ "screenshot",       _("SCREENSHOT") },
		{ "swap_screen",      _("SWAP SCREEN") },
		{ "translation",      _("TRANSLATION") },
		{ "volumedown",       _("VOLUME DOWN") },
		{ "volumeup",         _("VOLUME UP") },
		{ "volumemute",       _("VOLUME MUTE") },
		{ "recording-start",  _("START RECORDING") },
		{ "recording-stop",   _("STOP RECORDING") },
		{ "onscreen-keyboard",_("KEYBOARD TOGGLE") },
	};


	std::vector<Hotkey> hotkeys = ApiSystem::getInstance()->getJoysticksHotkeys();
	std::vector<std::string> hotkeys_values = ApiSystem::getInstance()->getJoysticksHotkeysValues();
	std::vector<std::shared_ptr<OptionListComponent<std::string>>> btns_elts;

	for (unsigned int i = 0; i < hotkeys.size(); i++) 
	{
		// find the label
		std::string label = _("HOTKEY") + " + " + hotkeys[i].button;
		for (unsigned int x = 0; x < keys_labels.size(); x++) 
		{
			if (keys_labels[x].code == hotkeys[i].button) 
			{
				label = _("HOTKEY") + " + " + keys_labels[x].name;
				break;
			}
		}

		auto btn = std::make_shared< OptionListComponent<std::string> >(mWindow, label, false);

		// build the default label
		std::string default_label = hotkeys[i].default_action;
		for (unsigned int x = 0; x < targets_labels.size(); x++)
		{
			if (targets_labels[x].code == hotkeys[i].default_action)
			{
				default_label = targets_labels[x].name;
				break;
			}
		}

		if (hotkeys[i].default_action == "")
			default_label = _("NONE");
		
		btn->add(_("DEFAULT") + " (" + default_label + ")", "default", hotkeys[i].default_action == hotkeys[i].action);
		btn->add(_("NONE"), "none", hotkeys[i].default_action != "" && hotkeys[i].action == "");

		// add known hotkeys
		for (unsigned int v = 0; v < hotkeys_values.size(); v++) 
		{
			// find the label
			std::string xlabel = hotkeys_values[v];
			for (unsigned int x = 0; x < targets_labels.size(); x++)
			{
				if (targets_labels[x].code == hotkeys_values[v]) 
				{
					xlabel = targets_labels[x].name;
					break;
				}
			}
			btn->add(xlabel, hotkeys_values[v], hotkeys[i].action != "" && hotkeys[i].default_action != hotkeys[i].action && hotkeys[i].action == hotkeys_values[v]);
		}

		s->addWithLabel(label, btn);
		btns_elts.push_back(btn);
	}

	s->addSaveFunc([hotkeys, btns_elts] 
		{
		std::vector<Hotkey> vals;
		for (unsigned int h = 0; h < hotkeys.size(); h++) 
		{
			Hotkey k;
			k.button = hotkeys[h].button;
			k.action = btns_elts[h]->getSelected();
			vals.push_back(k);
		}
		ApiSystem::getInstance()->setJoysticksHotkeys(vals);

#ifdef BATOCERA
		// change the control center key for es too
		for (unsigned int h = 0; h < hotkeys.size(); h++) {
			if (btns_elts[h]->getSelected() == "controlcenter") {
				Settings::getInstance()->setString("HOTKEY_CONTROLCENTER", hotkeys[h].button);
				Settings::getInstance()->saveFile();
				break;
			}
		}
		//
#endif
		});

	mWindow->pushGui(s);
}

void GuiControllersSettings::openGlobalHotkeys()
{
	GuiSettings* s = new GuiSettings(mWindow, _("GLOBAL HOTKEYS"));
	initializeGlobalHotkeys(mWindow, s);

	mWindow->pushGui(s);
}

void GuiControllersSettings::openKeyboardtopads()
{
	GuiSettings* s = new GuiSettings(mWindow, _("KEYBOARDTOPADS"), true);
	Window* window = mWindow;

	std::vector<Keyboardtopad> keyboardtopads = ApiSystem::getInstance()->getKeyboardtopads();

	if (keyboardtopads.size() == 0)
		s->addEntry(_("NO DEVICE FOUND (JAMMASD, IPAC, ...)"), false);
	else
	{
		for (unsigned int i = 0; i < keyboardtopads.size(); i++)
			s->addEntry(keyboardtopads[i].name, false, [this, keyboardtopads, i] { mWindow->pushGui(new GuiKeyboardtopads(mWindow, keyboardtopads[i])); });		
	}

	mWindow->pushGui(s);
}

void GuiControllersSettings::initializeGlobalHotkeys(Window* window, GuiSettings* s)
{
	std::vector<std::shared_ptr<OptionListComponent<std::string>>> elts;

	std::vector<hotkeyTargetDefinition> targets_labels =
	{
		{ "bezels",           _("OVERLAYS") },
		{ "brightness-cycle", _("BRIGHTNESS CYCLE") },
		{ "controlcenter",    _("CONTROL CENTER") },
		{ "exit",             _("EXIT") },
		{ "pause",            _("PAUSE") },
		{ "menu",             _("MENU") },
		{ "files",            _("FILES") },
		{ "coin",             _("COIN") },
		{ "fastforward",      _("FAST FORWARD") },
		{ "next_disk",        _("NEXT DISK") },
		{ "next_slot",        _("NEXT SLOT") },
		{ "previous_slot",    _("PREVIOUS SLOT") },
		{ "reset",            _("RESET") },
		{ "save_state",       _("SAVE STATE") },
		{ "restore_state",    _("RESTORE STATE") },
		{ "rewind",           _("REWIND") },
		{ "screen_layout",    _("SCREEN LAYOUT") },
		{ "screenshot",       _("SCREENSHOT") },
		{ "swap_screen",      _("SWAP SCREEN") },
		{ "translation",      _("TRANSLATION") },
		{ "volumedown",       _("VOLUME DOWN") },
		{ "volumeup",         _("VOLUME UP") },
		{ "volumemute",       _("VOLUME MUTE") },
		{ "recording-start",  _("START RECORDING") },
		{ "recording-stop",   _("STOP RECORDING") },
		{ "onscreen-keyboard",_("KEYBOARD TOGGLE") },
	};

	s->save(); // save the current step to avoid loosing information, will do nothing the first time while there is no save function
	s->clear();
	s->addEntry(_("DECLARE A NEW GLOBAL HOTKEY"), false, [this, window, s] { declareGlobalHotkey(window, s); });

	std::vector<GlobalHotkey> hotkeys = ApiSystem::getInstance()->getGlobalHotkeys();
	std::vector<std::string> global_hotkeys_values = ApiSystem::getInstance()->getGlobalHotkeysValues();
	std::string current_device = "";

	for (unsigned int i = 0; i < hotkeys.size(); i++) 
	{
		if (current_device != hotkeys[i].device_fancy_name) 
		{
			current_device = hotkeys[i].device_fancy_name;
			s->addGroup(current_device);
		}

		auto elt = std::make_shared< OptionListComponent<std::string> >(mWindow, hotkeys[i].key, false);
		s->addWithLabel(hotkeys[i].key, elt);
		elts.push_back(elt);

		elt->add(_("NONE"), "none", hotkeys[i].action == "");

		// add known hotkeys
		for (unsigned int v = 0; v < global_hotkeys_values.size(); v++) 
		{
			// find the label
			std::string xlabel = global_hotkeys_values[v];
			for (unsigned int x = 0; x < targets_labels.size(); x++) 
			{
				if (targets_labels[x].code == global_hotkeys_values[v]) 
				{
					xlabel = targets_labels[x].name;
					break;
				}
			}
			elt->add(xlabel, global_hotkeys_values[v], hotkeys[i].action == global_hotkeys_values[v]);
		}
	}

	// reset the save function
	s->clearSaveFuncs();
	s->addSaveFunc([hotkeys, elts]
		{
			for (unsigned int h = 0; h < hotkeys.size(); h++)
			{
				if (elts[h]->getSelected() == "none") // save save time, we do cleaning of none values
					ApiSystem::getInstance()->removeGlobalHotkey(hotkeys[h].device_config, hotkeys[h].key);
				else if (elts[h]->changed())
					ApiSystem::getInstance()->setGlobalHotkey(hotkeys[h].device_config, hotkeys[h].key, elts[h]->getSelected());
			}
		});
}

void GuiControllersSettings::declareGlobalHotkey(Window* window, GuiSettings* s)
{
	window->pushGui(new GuiLoading<int>(window, _("YOU'VE 4 SECONDS TO PRESS EXACTLY 2 TIMES THE GLOBAL HOTKEY"), [this, window](auto gui)
		{
			std::vector<GlobalHotkey> res = ApiSystem::getInstance()->detectGlobalHotkeys();
			if (res.size() == 1)
				ApiSystem::getInstance()->setGlobalHotkey(res[0].device_config, res[0].key, "none");
			
			return res.size();
		}, [this, window, s](int ret)
		{
			if (ret == 1)
				initializeGlobalHotkeys(window, s);			
			else 
			{
				if (ret == 0)
					window->pushGui(new GuiMsgBox(window, _("NO GLOBAL HOTKEY DETECTED")));								
				else if (ret > 1)
					window->pushGui(new GuiMsgBox(window, _("MORE THAN ONE GLOBAL HOTKEY DETECTED")));				
			}
		}));
}

void GuiControllersSettings::openInputSenseHotkeys()
{
	auto s = new GuiSettings(mWindow, _("SYSTEM HOTKEYS"));

	// Helper: create a button selector dropdown
	auto makeButtonList = [this](const std::string& title, const std::string& settingKey,
	                              const std::string& defaultVal) {
		auto list = std::make_shared<OptionListComponent<std::string>>(mWindow, title, false);
		std::string cur = SystemConf::getInstance()->get(settingKey);
		if (cur.empty()) cur = defaultVal;

		struct BtnDef { const char* label; const char* code; };
		BtnDef buttons[] = {
			{ "GUIDE",        "BTN_MODE" },
			{ "START",        "BTN_START" },
			{ "SELECT",       "BTN_SELECT" },
			{ "L1",           "BTN_TL" },
			{ "R1",           "BTN_TR" },
			{ "L2",           "BTN_TL2" },
			{ "R2",           "BTN_TR2" },
			{ "L3",           "BTN_THUMBL" },
			{ "R3",           "BTN_THUMBR" },
			{ "SOUTH (A/X)",  "BTN_SOUTH" },
			{ "EAST (B/O)",   "BTN_EAST" },
			{ "NORTH (X/T)",  "BTN_NORTH" },
			{ "WEST (Y/S)",   "BTN_WEST" },
		};
		for (const auto& b : buttons)
			list->add(_(b.label), b.code, cur == b.code);

		if (!list->hasSelection())
			list->selectFirstItem();
		return list;
	};

	// Helper: create an action selector dropdown
	auto makeActionList = [this](const std::string& title, const std::string& settingKey,
	                              const std::string& defaultVal) {
		auto list = std::make_shared<OptionListComponent<std::string>>(mWindow, title, false);
		std::string cur = SystemConf::getInstance()->get(settingKey);
		if (cur.empty()) cur = defaultVal;

		struct ActDef { const char* label; const char* code; };
		ActDef actions[] = {
			{ "BRIGHTNESS UP",    "brightness up" },
			{ "BRIGHTNESS DOWN",  "brightness down" },
			{ "VOLUME UP",        "volume up" },
			{ "VOLUME DOWN",      "volume down" },
			{ "LED CONTROL",      "ledcontrol" },
			{ "LED OFF",          "ledcontrol poweroff" },
			{ "WIFI ENABLE",      "wifictl enable" },
			{ "WIFI DISABLE",     "wifictl disable" },
		};
		for (const auto& a : actions)
			list->add(_(a.label), a.code, cur == a.code);

		if (!list->hasSelection())
			list->selectFirstItem();
		return list;
	};

	// --- FN Modifier Keys ---
	s->addGroup(_("MODIFIER KEYS"));

	auto fnA = makeButtonList(_("FN MODIFIER (A)"), "key.function.a", "BTN_MODE");
	s->addWithLabel(_("FN MODIFIER (A)"), fnA);

	auto fnB = makeButtonList(_("FN MODIFIER (B)"), "key.function.b", "BTN_START");
	s->addWithLabel(_("FN MODIFIER (B)"), fnB);

	// --- Kill Combo ---
	s->addGroup(_("KILL COMBO (FN + A + B + C)"));

	auto killA = makeButtonList(_("KILL BUTTON A"), "key.hotkey.a", "BTN_TL");
	s->addWithLabel(_("KILL BUTTON A"), killA);

	auto killB = makeButtonList(_("KILL BUTTON B"), "key.hotkey.b", "BTN_TR");
	s->addWithLabel(_("KILL BUTTON B"), killB);

	auto killC = makeButtonList(_("KILL BUTTON C"), "key.hotkey.c", "BTN_START");
	s->addWithLabel(_("KILL BUTTON C"), killC);

	// --- FN+A Actions (Vol Up/Down with FN held) ---
	s->addGroup(_("FN + VOLUME ACTIONS"));

	auto fnAUp = makeActionList(_("FN(A) + VOL UP"), "key.function.a.up", "brightness up");
	s->addWithLabel(_("FN(A) + VOL UP"), fnAUp);

	auto fnADown = makeActionList(_("FN(A) + VOL DOWN"), "key.function.a.down", "brightness down");
	s->addWithLabel(_("FN(A) + VOL DOWN"), fnADown);

	auto fnBUp = makeActionList(_("FN(B) + VOL UP"), "key.function.b.up", "ledcontrol");
	s->addWithLabel(_("FN(B) + VOL UP"), fnBUp);

	auto fnBDown = makeActionList(_("FN(B) + VOL DOWN"), "key.function.b.down", "ledcontrol poweroff");
	s->addWithLabel(_("FN(B) + VOL DOWN"), fnBDown);

	auto fnABUp = makeActionList(_("FN(A+B) + VOL UP"), "key.function.ab.up", "wifictl enable");
	s->addWithLabel(_("FN(A+B) + VOL UP"), fnABUp);

	auto fnABDown = makeActionList(_("FN(A+B) + VOL DOWN"), "key.function.ab.down", "wifictl disable");
	s->addWithLabel(_("FN(A+B) + VOL DOWN"), fnABDown);

	// Save all settings — requires input_sense restart to take effect
	s->addSaveFunc([fnA, fnB, killA, killB, killC, fnAUp, fnADown, fnBUp, fnBDown, fnABUp, fnABDown] {
		bool changed = false;
		auto sc = SystemConf::getInstance();

		auto save = [&](const std::string& key, std::shared_ptr<OptionListComponent<std::string>> list) {
			std::string val = list->getSelected();
			if (val != sc->get(key)) {
				sc->set(key, val);
				changed = true;
			}
		};

		save("key.function.a", fnA);
		save("key.function.b", fnB);
		save("key.hotkey.a", killA);
		save("key.hotkey.b", killB);
		save("key.hotkey.c", killC);
		save("key.function.a.up", fnAUp);
		save("key.function.a.down", fnADown);
		save("key.function.b.up", fnBUp);
		save("key.function.b.down", fnBDown);
		save("key.function.ab.up", fnABUp);
		save("key.function.ab.down", fnABDown);

		if (changed) {
			// Restart input_sense to pick up new bindings
			Utils::Platform::runSystemCommand("systemctl restart input 2>/dev/null", "", nullptr);
		}
	});

	mWindow->pushGui(s);
}

void GuiControllersSettings::openControllersSpecificSettings_sindengun()
{
	GuiSettings* s = new GuiSettings(mWindow, controllers_settings_label.c_str());

	std::string selectedBordersSize = SystemConf::getInstance()->get("controllers.guns.borderssize");
	auto borderssize_set = std::make_shared<OptionListComponent<std::string> >(mWindow, _("BORDER SIZE"), false);
	borderssize_set->add(_("AUTO"), "", "" == selectedBordersSize);
	borderssize_set->add(_("THIN"), "THIN", "THIN" == selectedBordersSize);
	borderssize_set->add(_("MEDIUM"), "MEDIUM", "MEDIUM" == selectedBordersSize);
	borderssize_set->add(_("BIG"), "BIG", "BIG" == selectedBordersSize);
	s->addOptionList(_("BORDER SIZE"), { { _("AUTO"), "auto" },{ _("THIN") , "thin" },{ _("MEDIUM"), "medium" },{ _("BIG"), "big" } }, "controllers.guns.borderssize", false);

	std::string selectedBordersMode = SystemConf::getInstance()->get("controllers.guns.bordersmode");
	auto bordersmode_set = std::make_shared<OptionListComponent<std::string> >(mWindow, _("BORDER MODE"), false);
	bordersmode_set->add(_("AUTO"), "", "" == selectedBordersMode);
	bordersmode_set->add(_("NORMAL"), "NORMAL", "NORMAL" == selectedBordersMode);
	bordersmode_set->add(_("IN GAME ONLY"), "INGAMEONLY", "INGAMEONLY" == selectedBordersMode);
	bordersmode_set->add(_("HIDDEN"), "HIDDEN", "HIDDEN" == selectedBordersMode);
	s->addOptionList(_("BORDER MODE"), { { _("AUTO"), "auto" },{ _("NORMAL") , "normal" },{ _("IN GAME ONLY"), "gameonly" },{ _("HIDDEN"), "hidden" } }, "controllers.guns.bordersmode", false);

	std::string selectedBordersColor = SystemConf::getInstance()->get("controllers.guns.borderscolor");
	auto borderscolor_set = std::make_shared<OptionListComponent<std::string> >(mWindow, _("BORDER COLOR"), false);
	borderscolor_set->add(_("AUTO"), "", "" == selectedBordersColor);
	borderscolor_set->add(_("WHITE"), "WHITE", "white" == selectedBordersColor);
	borderscolor_set->add(_("RED"), "RED", "red" == selectedBordersColor);
	borderscolor_set->add(_("GREEN"), "GREEN", "green" == selectedBordersColor);
	borderscolor_set->add(_("BLUE"), "BLUE", "blue" == selectedBordersColor);
	s->addOptionList(_("BORDER COLOR"), { { _("AUTO"), "auto" },{ _("WHITE") , "white" },{ _("RED") , "red" },{ _("GREEN"), "green" },{ _("BLUE"), "blue" } }, "controllers.guns.borderscolor", false);

#if BATOCERA
	std::string selectedBordersRatio = SystemConf::getInstance()->get("controllers.guns.bordersratio");
	auto bordersratio_set = std::make_shared<OptionListComponent<std::string> >(mWindow, _("BORDER RATIO"), false);
	bordersratio_set->add(_("AUTO"), "", "" == selectedBordersRatio);
	bordersratio_set->add("4:3", "4:3", "4:3" == selectedBordersRatio);
	s->addWithLabel(_("BORDER RATIO"), bordersratio_set);

	std::string selectedCameraContrast = SystemConf::getInstance()->get("controllers.guns.sinden.contrast");
	auto cameracontrast_set = std::make_shared<OptionListComponent<std::string> >(mWindow, _("CAMERA CONTRAST"), false);
	cameracontrast_set->add(_("AUTO"), "", "" == selectedCameraContrast);
	cameracontrast_set->add(_("Daytime/Bright Sunlight (40)"), "40", "40" == selectedCameraContrast);
	cameracontrast_set->add(_("Default (50)"), "50", "50" == selectedCameraContrast);
	cameracontrast_set->add(_("Dim Display/Evening (60)"), "60", "60" == selectedCameraContrast);
	s->addWithLabel(_("CAMERA CONTRAST"), cameracontrast_set);

	std::string selectedCameraBrightness = SystemConf::getInstance()->get("controllers.guns.sinden.brightness");
	auto camerabrightness_set = std::make_shared<OptionListComponent<std::string> >(mWindow, _("CAMERA BRIGHTNESS"), false);
	camerabrightness_set->add(_("AUTO"), "", "" == selectedCameraBrightness);
	camerabrightness_set->add(_("Daytime/Bright Sunlight (80)"), "80", "80" == selectedCameraBrightness);
	camerabrightness_set->add(_("Default (100)"), "100", "100" == selectedCameraBrightness);
	camerabrightness_set->add(_("Dim Display/Evening (120)"), "120", "120" == selectedCameraBrightness);
	s->addWithLabel(_("CAMERA BRIGHTNESS"), camerabrightness_set);

	std::string selectedCameraExposure = SystemConf::getInstance()->get("controllers.guns.sinden.exposure");
	auto cameraexposure_set = std::make_shared<OptionListComponent<std::string> >(mWindow, _("CAMERA EXPOSURE"), false);
	cameraexposure_set->add(_("AUTO"), "", "" == selectedCameraExposure);
	cameraexposure_set->add(_("Projector/CRT (-5)"), "-5", "-5" == selectedCameraExposure);
	cameraexposure_set->add(_("Projector/CRT (-6)"), "-6", "-6" == selectedCameraExposure);
	cameraexposure_set->add(_("Default (-7)"), "-7", "-7" == selectedCameraExposure);
	cameraexposure_set->add(_("Other (-8)"), "-8", "-8" == selectedCameraExposure);
	cameraexposure_set->add(_("Other (-9)"), "-9", "-9" == selectedCameraExposure);
	s->addWithLabel(_("CAMERA EXPOSURE"), cameraexposure_set);

	std::string baseMode = SystemConf::getInstance()->get("controllers.guns.recoil");
	auto sindenmode_choices = std::make_shared<OptionListComponent<std::string> >(mWindow, _("RECOIL"), false);
	sindenmode_choices->add(_("AUTO"), "auto", baseMode.empty() || baseMode == "auto");
	sindenmode_choices->add(_("DISABLED"), "disabled", baseMode == "disabled");
	sindenmode_choices->add(_("GUN"), "gun", baseMode == "gun");
	sindenmode_choices->add(_("MACHINE GUN"), "machinegun", baseMode == "machinegun");
	sindenmode_choices->add(_("QUIET GUN"), "gun-quiet", baseMode == "gun-quiet");
	sindenmode_choices->add(_("QUIET MACHINE GUN"), "machinegun-quiet", baseMode == "machinegun-quiet");
	s->addWithLabel(_("RECOIL"), sindenmode_choices);

	s->addSaveFunc([sindenmode_choices, bordersratio_set, cameracontrast_set, camerabrightness_set, cameraexposure_set] {
		if (sindenmode_choices->getSelected() != SystemConf::getInstance()->get("controllers.guns.recoil") ||
		        bordersratio_set->getSelected() != SystemConf::getInstance()->get("controllers.guns.bordersratio") ||
			cameracontrast_set->getSelected() != SystemConf::getInstance()->get("controllers.guns.sinden.contrast") ||
			camerabrightness_set->getSelected() != SystemConf::getInstance()->get("controllers.guns.sinden.brightness") ||
			cameraexposure_set->getSelected() != SystemConf::getInstance()->get("controllers.guns.sinden.exposure")
			) {
			SystemConf::getInstance()->set("controllers.guns.recoil", sindenmode_choices->getSelected());
			SystemConf::getInstance()->set("controllers.guns.bordersratio", bordersratio_set->getSelected());
			SystemConf::getInstance()->set("controllers.guns.sinden.contrast", cameracontrast_set->getSelected());
			SystemConf::getInstance()->set("controllers.guns.sinden.brightness", camerabrightness_set->getSelected());
			SystemConf::getInstance()->set("controllers.guns.sinden.exposure", cameraexposure_set->getSelected());
			SystemConf::getInstance()->saveSystemConf();
			ApiSystem::getInstance()->replugControllers_sindenguns();
		}
	});
#endif

	mWindow->pushGui(s);
}

void GuiControllersSettings::openControllersSpecificSettings_wiigun()
{
	GuiSettings* s = new GuiSettings(mWindow, controllers_settings_label.c_str());

	std::string baseMode = SystemConf::getInstance()->get("controllers.wiimote.mode");
	auto wiimode_choices = std::make_shared<OptionListComponent<std::string> >(mWindow, _("MODE"), false);
	wiimode_choices->add(_("AUTO"), "auto", baseMode.empty() || baseMode == "auto");
	wiimode_choices->add(_("GUN"), "gun", baseMode == "gun");
	wiimode_choices->add(_("JOYSTICK"), "joystick", baseMode == "joystick");
	s->addWithLabel(_("MODE"), wiimode_choices);
	s->addSaveFunc([wiimode_choices] {
		if (wiimode_choices->getSelected() != SystemConf::getInstance()->get("controllers.wiimote.mode")) {
			SystemConf::getInstance()->set("controllers.wiimote.mode", wiimode_choices->getSelected());
			SystemConf::getInstance()->saveSystemConf();
			ApiSystem::getInstance()->replugControllers_wiimotes();
		}
	});
	mWindow->pushGui(s);
}

void GuiControllersSettings::openControllersSpecificSettings_steamdeckgun()
{
	GuiSettings* s = new GuiSettings(mWindow, controllers_settings_label.c_str());

	std::string baseMode = SystemConf::getInstance()->get("controllers.steamdeckmouse.gun");
	auto mode_choices = std::make_shared<OptionListComponent<std::string> >(mWindow, _("MODE"), false);
	mode_choices->add(_("AUTO"), "auto", baseMode.empty() || baseMode == "auto");
	mode_choices->add(_("MOUSE ONLY"), "0", baseMode == "0");
	mode_choices->add(_("GUN"), "1", baseMode == "1");
	s->addWithLabel(_("MODE"), mode_choices);
	s->addSaveFunc([mode_choices] {
		if (mode_choices->getSelected() != SystemConf::getInstance()->get("controllers.steamdeckmouse.gun")) {
			SystemConf::getInstance()->set("controllers.steamdeckmouse.gun", mode_choices->getSelected());
			SystemConf::getInstance()->saveSystemConf();
			ApiSystem::getInstance()->replugControllers_steamdeckguns();
		}
	});

	std::string baseHand = SystemConf::getInstance()->get("controllers.steamdeckmouse.hand");
	auto hand_choices = std::make_shared<OptionListComponent<std::string> >(mWindow, _("HAND"), false);
	hand_choices->add(_("AUTO"), "auto", baseHand.empty() || baseHand == "auto");
	hand_choices->add(_("LEFT"), "left", baseHand == "left");
	hand_choices->add(_("RIGHT"), "right", baseHand == "right");
	s->addWithLabel(_("HAND"), hand_choices);
	s->addSaveFunc([hand_choices] {
		if (hand_choices->getSelected() != SystemConf::getInstance()->get("controllers.steamdeckmouse.hand")) {
			SystemConf::getInstance()->set("controllers.steamdeckmouse.hand", hand_choices->getSelected());
			SystemConf::getInstance()->saveSystemConf();
			ApiSystem::getInstance()->replugControllers_steamdeckguns();
		}
	});

	mWindow->pushGui(s);
}

void GuiControllersSettings::clearLoadedInput() 
{
	for (int i = 0; i < mLoadedInput.size(); i++) 
		delete mLoadedInput[i];

	mLoadedInput.clear();
}

GuiControllersSettings::~GuiControllersSettings() 
{
	clearLoadedInput();
}
