// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's menu over a game played in its window (menu.h; docs/adr/0060).
//
// What the menu shows is drawn on the GS thread, with the ImGui PCSX2's
// renderer draws its own displays with, each of the menu's frames over the
// game's last picture (GSPresentCurrentFrame). What it holds of the game (the
// controls as the menu shows them, the graphics cards, what was last done) is
// put together on the CPU thread and sent to the GS thread as a whole
// (PostShown); what is chosen in it is sent back to the CPU thread (Do), which
// carries it out between two of the menu's frames (Tick), with the core's
// settings held. Only the page shown is the GS thread's own.
//
// Setting a control waits for a key or a controller's input as PCSX2's own
// fullscreen interface does (BeginInputBinding in pcsx2/ImGui/FullscreenUI.cpp
// of PCSX2 v2.9.198, Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+): an input
// hook takes what is pressed, and the binding is made when it is let go.
//
// Two players (docs/adr/0064): player 1's controller is the one in port 1,
// player 2's the one in port 2, which the runner puts in once player 2 has
// something to play with (main.cpp, ConnectSecondController); the settings
// call port 2 empty until then, and player 2's controls are a DualShock 2's
// all the same. An input, and a controller, play for one player at a time.

#include "menu.h"

#include "host_state.h"
#include "menu_sound.h"
#include "window.h"

#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Config.h"
#include "pcsx2/GS/GS.h"
#include "pcsx2/GS/Renderers/Common/GSDevice.h"
#include "pcsx2/Host.h"
#include "pcsx2/INISettingsInterface.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadTypes.h"
#include "pcsx2/SIO/Sio.h"
#include "pcsx2/VMManager.h"

#include "fmt/format.h"
#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ps2n::host::menu
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// How long setting a control waits for something to be pressed.
		constexpr auto kCaptureTime = std::chrono::seconds(5);

		// The players, whose controllers are in ports 1 and 2; a session writes
		// down both (host/session_record.h, docs/adr/0064).
		constexpr u32 kPlayers = 2;

		// ---- what the menu shows (the CPU thread makes it, the GS thread draws it) ----

		// One of the controller's inputs: its name, and what it is on the
		// keyboard and on controllers, as they are shown.
		struct Row
		{
			std::string name; // the binding's own name in the settings ("Cross")
			std::string label; // as shown ("Cross")
			InputBindingInfo::Type type = InputBindingInfo::Type::Button;
			std::string keys; // the keyboard's, shown
			std::string pad; // controllers', shown
		};

		struct Device
		{
			std::string id; // "SDL-0"
			std::string name;
			int player = -1; // the player it plays for (0 or 1), or -1
		};

		struct Shown
		{
			std::string title;
			u32 player = 0; // whose controls the Controls page shows
			bool port2 = false; // whether port 2 has its controller (player 2's)
			std::vector<Row> rows;
			std::string playing; // what the player's controller is played with
			std::vector<Device> controllers; // connected
			int capturing = -1; // the row being set, or -1
			Clock::time_point capture_until;
			bool own = false; // the player's own controls, kept in the game's folder
			std::vector<std::string> adapters; // the graphics cards for the game's renderer
			bool adapters_read = false;
			std::string message; // what was last done, said
		};

		// ---- the CPU thread's ----

		std::string s_controls_path; // the controls file (LoadControls)
		bool s_controls_own = false;
		u32 s_player = 0; // whose controls the Controls page shows and sets: 0 player 1, 1 player 2
		std::atomic<bool> s_requested{false};
		std::atomic<bool> s_open{false}; // read by the window's thread too (IsOpen)
		bool s_quit = false;
		bool s_reboot = false;
		bool s_ask_quit = false; // the close button asked for the menu: it opens on Quit's question
		std::string s_title;
		std::string s_message;
		std::vector<std::string> s_adapters;
		bool s_adapters_read = false;
		float s_pointer_x = -1.0f, s_pointer_y = -1.0f;

		// The control being set: the row, the inputs held so far, until when it
		// waits. The input hook (OnCaptureEvent) runs on the CPU thread too, from
		// InputManager::InvokeEvents.
		struct Capture
		{
			int row = -1;
			std::vector<InputBindingKey> keys;
			Clock::time_point until;
			bool done = false; // what was held has been let go: the binding is made at the next Tick
		};
		Capture s_capture;

		// What the GS thread asks the CPU thread to do.
		std::mutex s_actions_mutex;
		std::vector<std::function<void()>> s_actions;

		// ---- the GS thread's ----

		enum class Page
		{
			Main,
			Quit,
			Reboot,
			Controls,
			GraphicsCard,
		};
		Shown s_shown;
		Page s_page = Page::Main;
		// Quit's question came from the window's close button: Back goes back to
		// the game, not to the first page.
		bool s_from_close = false;
		std::atomic<bool> s_asking_quit{false}; // read by the window's thread (IsAskingQuit)
		// Each page is a window of its own, which ImGui gives the keyboard's and
		// controllers' focus when it appears, on its first item unless another
		// says it is the default (SetItemDefaultFocus): on the first page, the
		// item of the page come back from. The focus is shown at once.
		int s_main_default = 0; // Resume, Controls, Graphics card, Reboot, Quit
		bool s_show_focus = false;
		bool s_adapters_asked = false;

		// The item the pointer is over and the one the keys or a controller have
		// reached, last frame and this one: a tick sounds when either moves to
		// another (Pointed). Not for a few frames after a page has come, or an
		// item has been chosen, which move them by themselves.
		ImGuiID s_hovered = 0, s_focused = 0, s_hovered_now = 0, s_focused_now = 0;
		int s_quiet_frames = 0;

		using sound::Sound;

		// ---- settings -----------------------------------------------------------------

		SettingsInterface& Base()
		{
			return *Host::Internal::GetBaseSettingsLayer();
		}

		// The settings' section of a player's controller: "Pad1", "Pad2".
		std::string Section(u32 port)
		{
			return Pad::GetConfigSection(port);
		}

		// What a player's controller is: the type the settings give its port (a
		// DualShock 2 in port 1); player 2's a DualShock 2, the one put into port
		// 2 once player 2 has something to play with, before it is there too
		// (the settings call port 2 empty until then).
		const Pad::ControllerInfo* PadInfo(const SettingsInterface& si, u32 port)
		{
			const std::string section = Section(port);
			const Pad::ControllerInfo* info = Pad::GetConfigControllerType(si, section.c_str(), port);
			if (port == 1 && (!info || info->type == Pad::ControllerType::NotConnected))
				return Pad::GetControllerInfo(Pad::ControllerType::DualShock2);
			return info;
		}

		// The device a binding is on ("Keyboard", "SDL-0"), from its first part.
		std::string_view DeviceOf(std::string_view binding)
		{
			const size_t slash = binding.find('/');
			return StringUtil::StripWhitespace(slash == std::string_view::npos ? binding : binding.substr(0, slash));
		}

		bool IsKeyboard(std::string_view binding)
		{
			return DeviceOf(binding) == "Keyboard";
		}

		// A controller, not the keyboard or the pointer, which are always there.
		bool IsController(std::string_view device)
		{
			return device != "Keyboard" && device != "Mouse" && !device.starts_with("Pointer");
		}

		bool IsConnected(std::string_view device, const Devices& connected)
		{
			return !IsController(device) ||
				   std::any_of(connected.begin(), connected.end(), [device](const auto& d) { return d.first == device; });
		}

		// Each of a player's inputs, the motors left out, with its bindings.
		template <typename F>
		void ForEachInput(const SettingsInterface& si, u32 port, F&& f)
		{
			const Pad::ControllerInfo* info = PadInfo(si, port);
			if (!info)
				return;
			const std::string section = Section(port);
			for (const InputBindingInfo& bi : info->bindings)
			{
				if (bi.bind_type != InputBindingInfo::Type::Motor)
					f(bi, si.GetStringList(section.c_str(), bi.name));
			}
		}

		// Whether a player's controls have any input at all.
		bool HasAnyInput(const SettingsInterface& si, u32 port)
		{
			bool found = false;
			ForEachInput(si, port, [&](const InputBindingInfo&, const std::vector<std::string>& bindings) { found = found || !bindings.empty(); });
			return found;
		}

		// Whether a player's controls have an input on `device`.
		bool Names(const SettingsInterface& si, u32 port, std::string_view device)
		{
			bool found = false;
			ForEachInput(si, port, [&](const InputBindingInfo&, const std::vector<std::string>& bindings) {
				found = found || std::any_of(bindings.begin(), bindings.end(), [device](const std::string& b) { return DeviceOf(b) == device; });
			});
			return found;
		}

		// Whether a player plays with a controller: one their controls have an
		// input on, other than `except`; only one that is connected where
		// `connected` is given.
		bool PlaysWithController(const SettingsInterface& si, u32 port, const Devices* connected, std::string_view except = {})
		{
			bool found = false;
			ForEachInput(si, port, [&](const InputBindingInfo&, const std::vector<std::string>& bindings) {
				for (const std::string& binding : bindings)
				{
					const std::string_view device = DeviceOf(binding);
					if (IsController(device) && device != except && (!connected || IsConnected(device, *connected)))
						found = true;
				}
			});
			return found;
		}

		// Every input of a player's controls, its motors among them, gone.
		void ClearPlayer(SettingsInterface& si, u32 port)
		{
			const Pad::ControllerInfo* info = PadInfo(si, port);
			if (!info)
				return;
			const std::string section = Section(port);
			for (const InputBindingInfo& bi : info->bindings)
				si.DeleteValue(section.c_str(), bi.name);
		}

		// Player 2's controls copied as a DualShock 2's, whatever the settings
		// say port 2 holds (Pad::CopyConfiguration copies the inputs of the type
		// they say only).
		void CopyPlayer2(SettingsInterface& to, const SettingsInterface& from)
		{
			const std::string section = Section(1);
			for (const InputBindingInfo& bi : Pad::GetControllerInfo(Pad::ControllerType::DualShock2)->bindings)
				to.CopyStringListValue(from, section.c_str(), bi.name);
		}

		// An input plays for one player: `binding` given to `port`'s, taken from
		// the other's.
		void TakeFromOther(SettingsInterface& si, u32 port, const std::string& binding)
		{
			const u32 other = (port == 0) ? 1 : 0;
			const std::string section = Section(other);
			ForEachInput(si, other, [&](const InputBindingInfo& bi, std::vector<std::string> bindings) {
				if (std::erase(bindings, binding) == 0)
					return;
				if (bindings.empty())
					si.DeleteValue(section.c_str(), bi.name);
				else
					si.SetStringList(section.c_str(), bi.name, bindings);
			});
		}

		// A controller plays for one player: `device`'s inputs and motors taken
		// from `port`'s.
		void TakeController(SettingsInterface& si, u32 port, std::string_view device)
		{
			const std::string section = Section(port);
			ForEachInput(si, port, [&](const InputBindingInfo& bi, std::vector<std::string> bindings) {
				if (std::erase_if(bindings, [device](const std::string& b) { return DeviceOf(b) == device; }) == 0)
					return;
				if (bindings.empty())
					si.DeleteValue(section.c_str(), bi.name);
				else
					si.SetStringList(section.c_str(), bi.name, bindings);
			});
			for (const char* motor : {"LargeMotor", "SmallMotor"})
			{
				if (DeviceOf(si.GetStringValue(section.c_str(), motor)) == device)
					si.DeleteValue(section.c_str(), motor);
			}
		}

		// What a binding looks like: a key's name, a controller's button as its
		// glyph (PCSX2's icon fonts, which the menu's font carries).
		std::string Pretty(const std::string& binding)
		{
			SmallString text;
			text.assign(binding);
			InputManager::PrettifyInputBinding(text, true);
			return std::string(text.view());
		}

		// A device's mapping of the controller's generic inputs ("SDL-0/FaceSouth"
		// for Cross), as PCSX2 makes it.
		const std::string* Mapped(const InputManager::GenericInputBindingMapping& mapping, GenericInputBinding generic)
		{
			for (const auto& [which, binding] : mapping)
			{
				if (which == generic)
					return &binding;
			}
			return nullptr;
		}

		// The vibration of the port given to `device`, where it has motors.
		void SetMotors(SettingsInterface& si, const Pad::ControllerInfo& info, u32 port, std::string_view device)
		{
			if (info.vibration_caps != Pad::VibrationCapabilities::LargeSmallMotors)
				return;
			const std::string section = Section(port);
			const InputManager::GenericInputBindingMapping mapping = InputManager::GetGenericBindingMapping(device);
			const std::string* large = Mapped(mapping, GenericInputBinding::LargeMotor);
			const std::string* small = Mapped(mapping, GenericInputBinding::SmallMotor);
			if (large)
				si.SetStringValue(section.c_str(), "LargeMotor", large->c_str());
			if (small)
				si.SetStringValue(section.c_str(), "SmallMotor", small->c_str());
		}

		// ---- what the menu shows, put together and sent ----------------------------

		Shown MakeShown()
		{
			Shown shown;
			shown.title = s_title;
			shown.player = s_player;
			shown.port2 = ps2n::host::SecondControllerConnected();
			shown.capturing = s_capture.row;
			shown.capture_until = s_capture.until;
			shown.own = s_controls_own;
			shown.adapters = s_adapters;
			shown.adapters_read = s_adapters_read;
			shown.message = s_message;

			const std::vector<std::pair<std::string, std::string>> devices = InputManager::EnumerateDevices();
			const auto name_of = [&devices](std::string_view id) -> std::string {
				for (const auto& [device, name] : devices)
				{
					if (device == id)
						return name;
				}
				return fmt::format("{} (not connected)", id);
			};
			for (const auto& [device, name] : devices)
			{
				if (IsController(device))
					shown.controllers.push_back({device, name});
			}

			// Each input's bindings, and the devices the controls are on, in their
			// order; and which player each controller connected plays for.
			std::vector<std::pair<Row, std::vector<std::string>>> inputs;
			std::vector<std::string> used;
			{
				auto lock = Host::GetSettingsLock();
				SettingsInterface& si = Base();
				for (Device& device : shown.controllers)
				{
					for (u32 port = 0; port < kPlayers && device.player < 0; port++)
					{
						if (Names(si, port, device.id))
							device.player = static_cast<int>(port);
					}
				}
				const std::string section = Section(s_player);
				if (const Pad::ControllerInfo* info = PadInfo(si, s_player))
				{
					for (const InputBindingInfo& bi : info->bindings)
					{
						if (bi.bind_type == InputBindingInfo::Type::Motor)
							continue;
						Row row;
						row.name = bi.name;
						row.label = bi.display_name;
						row.type = bi.bind_type;
						std::vector<std::string> bindings = si.GetStringList(section.c_str(), bi.name);
						for (const std::string& binding : bindings)
						{
							const std::string device(DeviceOf(binding));
							if (std::find(used.begin(), used.end(), device) == used.end())
								used.push_back(device);
						}
						inputs.emplace_back(std::move(row), std::move(bindings));
					}
				}
			}

			// As shown: without the device where the column says it (the keyboard),
			// or where the controls are on one controller only.
			const size_t controllers_used = static_cast<size_t>(std::count_if(used.begin(), used.end(), [](const std::string& d) { return d != "Keyboard"; }));
			for (auto& [row, bindings] : inputs)
			{
				for (const std::string& binding : bindings)
				{
					std::string text = Pretty(binding);
					const std::string device(DeviceOf(binding));
					if ((device == "Keyboard" || controllers_used == 1) && text.starts_with(device + " "))
						text.erase(0, device.size() + 1);
					std::string& column = IsKeyboard(binding) ? row.keys : row.pad;
					if (!column.empty())
						column += "  ";
					column += text;
				}
				shown.rows.push_back(std::move(row));
			}

			for (size_t i = 0; i < used.size(); i++)
			{
				if (i > 0)
					shown.playing += (i + 1 == used.size()) ? " and " : ", ";
				shown.playing += (used[i] == "Keyboard") ? std::string("the keyboard") : name_of(used[i]);
			}
			return shown;
		}

		// The GS thread draws what the CPU thread has now. `fresh`: the menu has
		// just opened, at its first page, or at Quit's question where the
		// window's close button asked for it (`at_quit`).
		void PostShown(bool fresh = false, bool at_quit = false)
		{
			MTGS::RunOnGSThread([shown = MakeShown(), fresh, at_quit]() mutable {
				s_shown = std::move(shown);
				if (fresh)
				{
					s_page = at_quit ? Page::Quit : Page::Main;
					s_from_close = at_quit;
					s_main_default = 0;
					s_show_focus = true;
					s_adapters_asked = false;
					s_hovered = s_focused = s_hovered_now = s_focused_now = 0;
					s_quiet_frames = 3;
					if (ImGui::GetCurrentContext())
					{
						ImGui::GetIO().ClearInputKeys();
						ImGui::GetIO().ClearInputMouse();
					}
				}
			});
		}

		// ---- what is chosen in the menu, on the CPU thread --------------------------

		void Do(std::function<void()> action)
		{
			std::lock_guard lock(s_actions_mutex);
			s_actions.push_back(std::move(action));
		}

		void RunActions()
		{
			std::vector<std::function<void()>> actions;
			{
				std::lock_guard lock(s_actions_mutex);
				actions.swap(s_actions);
			}
			for (const std::function<void()>& action : actions)
				action();
		}

		// Quit or Reboot confirmed: the menu closes and the runner does it (main.cpp).
		// Not while the game is saving to its memory card, or has just saved: PCSX2
		// counts a card busy for 300 frames after each write (MemcardBusy), refuses
		// a reset then (VMManager::RequestReset), and warns before shutting down
		// that the card could be corrupted. The game being held while the menu is
		// open, that count only runs down in the game: back to it first.
		void Leave(bool reboot)
		{
			if (MemcardBusy::IsBusy())
			{
				s_message = "The game is saving to its memory card, or has just saved: go back to the game for a few seconds, then try again.";
				sound::Play(Sound::Back);
				PostShown();
				return;
			}
			(reboot ? s_reboot : s_quit) = true;
			s_open = false;
		}

		// The controls as they are now kept in the game's folder, in PCSX2's own
		// format for an input profile.
		void SaveControls()
		{
			if (s_controls_path.empty())
				return;
			FileSystem::EnsureDirectoryExists(std::string(Path::GetDirectory(s_controls_path)).c_str(), true);
			INISettingsInterface ini(s_controls_path);
			{
				auto lock = Host::GetSettingsLock();
				Pad::CopyConfiguration(&ini, Base(), true, true, false);
				// Player 2's, whether or not port 2 has its controller yet; port 2
				// named a DualShock 2's where player 2 has any (the runner puts it in
				// once the game runs: LoadControls).
				CopyPlayer2(ini, Base());
				ini.SetStringValue(Section(1).c_str(), "Type",
					Pad::GetControllerInfo(HasAnyInput(Base(), 1) ? Pad::ControllerType::DualShock2 : Pad::ControllerType::NotConnected)->name);
			}
			if (ini.Save())
			{
				s_controls_own = true;
				s_message = "Kept in the game's folder: the game starts with these controls.";
			}
			else
			{
				s_message = fmt::format("The controls could not be written to {}: they hold until the game ends.", s_controls_path);
			}
		}

		// Controls changed: the core takes them at once, and they are kept. Where
		// player 2 now has something to play with, a controller is put into port
		// 2 for them (between two frames: the machine is held between two while
		// the menu is open).
		void Changed()
		{
			const bool before = ps2n::host::SecondControllerConnected();
			ps2n::host::ConnectSecondControllerIfPlayed();
			VMManager::ReloadInputBindings(true);
			SaveControls();
			if (!before && ps2n::host::SecondControllerConnected())
				s_message += " Player 2's controller is connected to the game from now on.";
			PostShown();
		}

		const InputBindingInfo* RowInfo(const SettingsInterface& si, int row)
		{
			const Pad::ControllerInfo* info = PadInfo(si, s_player);
			if (!info || row < 0)
				return nullptr;
			int index = 0;
			for (const InputBindingInfo& bi : info->bindings)
			{
				if (bi.bind_type == InputBindingInfo::Type::Motor)
					continue;
				if (index++ == row)
					return &bi;
			}
			return nullptr;
		}

		// `binding` in the place of what the row had of its kind (the keyboard's,
		// or controllers'), for the player shown; taken from the other player,
		// where it was one of theirs.
		void SetBinding(int row, const std::string& binding)
		{
			{
				auto lock = Host::GetSettingsLock();
				SettingsInterface& si = Base();
				const InputBindingInfo* bi = RowInfo(si, row);
				const Pad::ControllerInfo* info = PadInfo(si, s_player);
				if (!bi || !info)
					return;
				const std::string section = Section(s_player);
				const bool keyboard = IsKeyboard(binding);
				std::vector<std::string> list = si.GetStringList(section.c_str(), bi->name);
				std::erase_if(list, [keyboard](const std::string& b) { return IsKeyboard(b) == keyboard; });
				list.push_back(binding);
				si.SetStringList(section.c_str(), bi->name, list);
				TakeFromOther(si, s_player, binding);

				// A controller given a control by hand also vibrates, where nothing
				// does yet.
				if (!keyboard && si.GetStringValue(section.c_str(), "LargeMotor").empty() &&
					si.GetStringValue(section.c_str(), "SmallMotor").empty())
				{
					SetMotors(si, *info, s_player, DeviceOf(binding));
				}
			}
			Changed();
		}

		void ClearBinding(int row, bool keyboard)
		{
			{
				auto lock = Host::GetSettingsLock();
				SettingsInterface& si = Base();
				const InputBindingInfo* bi = RowInfo(si, row);
				if (!bi)
					return;
				const std::string section = Section(s_player);
				std::vector<std::string> list = si.GetStringList(section.c_str(), bi->name);
				const size_t before = list.size();
				std::erase_if(list, [keyboard](const std::string& b) { return IsKeyboard(b) == keyboard; });
				if (list.size() == before)
					return;
				if (list.empty())
					si.DeleteValue(section.c_str(), bi->name);
				else
					si.SetStringList(section.c_str(), bi->name, list);
			}
			Changed();
		}

		// Gives `device` the controller of `port`'s player beside what is there:
		// the mapping PCSX2 would make for it automatically, its motors for the
		// vibration. With the core's settings held. Returns the inputs mapped.
		u32 AddController(SettingsInterface& si, u32 port, std::string_view device)
		{
			const Pad::ControllerInfo* info = PadInfo(si, port);
			if (!info)
				return 0;
			const std::string section = Section(port);
			const InputManager::GenericInputBindingMapping mapping = InputManager::GetGenericBindingMapping(device);
			u32 mapped = 0;
			for (const InputBindingInfo& bi : info->bindings)
			{
				if (bi.generic_mapping == GenericInputBinding::Unknown || bi.bind_type == InputBindingInfo::Type::Motor)
					continue;
				if (const std::string* binding = Mapped(mapping, bi.generic_mapping))
				{
					si.AddToStringList(section.c_str(), bi.name, binding->c_str());
					mapped++;
				}
			}
			if (mapped > 0)
				SetMotors(si, *info, port, device);
			return mapped;
		}

		// The controller `device` plays the shown player's: its inputs are the
		// mapping PCSX2 makes for it, in the place of other controllers'; the
		// keys stay. Taken from the other player, where it was theirs.
		void UseController(const std::string& device)
		{
			{
				auto lock = Host::GetSettingsLock();
				SettingsInterface& si = Base();
				const Pad::ControllerInfo* info = PadInfo(si, s_player);
				if (!info)
					return;
				TakeController(si, (s_player == 0) ? 1 : 0, device);
				const std::string section = Section(s_player);
				const InputManager::GenericInputBindingMapping mapping = InputManager::GetGenericBindingMapping(device);
				for (const InputBindingInfo& bi : info->bindings)
				{
					if (bi.bind_type == InputBindingInfo::Type::Motor)
						continue;
					std::vector<std::string> list = si.GetStringList(section.c_str(), bi.name);
					std::erase_if(list, [](const std::string& b) { return !IsKeyboard(b); });
					if (const std::string* mapped = Mapped(mapping, bi.generic_mapping); mapped && bi.generic_mapping != GenericInputBinding::Unknown)
						list.push_back(*mapped);
					if (list.empty())
						si.DeleteValue(section.c_str(), bi.name);
					else
						si.SetStringList(section.c_str(), bi.name, list);
				}
				si.DeleteValue(section.c_str(), "LargeMotor");
				si.DeleteValue(section.c_str(), "SmallMotor");
				SetMotors(si, *info, s_player, device);
			}
			Changed();
		}

		// PCSX2's keys again for player 1, nothing for player 2, and the
		// controllers connected given out again as when they connected: the
		// first to player 1, beside the keys, the next to player 2 (as when the
		// game had no controls of its own); the file goes. A controller in port 2
		// stays there, as it does until the game ends.
		void DefaultControls()
		{
			MemorySettingsInterface defaults;
			Pad::SetDefaultControllerConfig(defaults);
			{
				auto lock = Host::GetSettingsLock();
				ClearPlayer(Base(), 1);
				Pad::CopyConfiguration(&Base(), defaults, true, true, false);
				if (ps2n::host::SecondControllerConnected())
					Base().SetStringValue(Section(1).c_str(), "Type", Pad::GetControllerInfo(Pad::ControllerType::DualShock2)->name);
			}
			s_controls_own = false;
			Devices devices = InputManager::EnumerateDevices();
			std::stable_sort(devices.begin(), devices.end(), [](const auto& a, const auto& b) {
				return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
			});
			for (const auto& [device, name] : devices)
			{
				if (!IsController(device))
					continue;
				auto lock = Host::GetSettingsLock();
				GiveController(Base(), device, devices);
			}
			ps2n::host::ConnectSecondControllerIfPlayed();
			VMManager::ReloadInputBindings(true);
			if (!s_controls_path.empty() && FileSystem::FileExists(s_controls_path.c_str()))
				FileSystem::DeleteFilePath(s_controls_path.c_str());
			s_message = "The default controls: the game starts with them.";
			PostShown();
		}

		// The Controls page shows `player`'s controls (0 or 1), and sets them.
		void SelectPlayer(u32 player)
		{
			if (player >= kPlayers || player == s_player)
				return;
			s_player = player;
			s_message.clear();
			PostShown();
		}

		// ---- setting a control: the input hook --------------------------------------

		InputInterceptHook::CallbackResult OnCaptureEvent(InputBindingKey key, float value)
		{
			// The pointer is the menu's.
			if (key.source_type == InputSourceType::Pointer)
				return InputInterceptHook::CallbackResult::ContinueProcessingEvent;
			if (s_capture.done)
				return InputInterceptHook::CallbackResult::StopProcessingEvent;

			const float magnitude = std::abs(value);
			for (const InputBindingKey& held : s_capture.keys)
			{
				if (held.MaskDirection() == key.MaskDirection())
				{
					// Let go: what was held is the binding.
					if (magnitude < 0.5f)
					{
						s_capture.done = true;
						return InputInterceptHook::CallbackResult::RemoveHookAndStopProcessingEvent;
					}
					return InputInterceptHook::CallbackResult::StopProcessingEvent;
				}
			}

			// What was already held when the wait began comes up without having
			// gone down here: on to the menu, which saw it go down (the Enter or
			// the Cross that chose the control), or it would stay held there.
			if (magnitude < 0.5f)
				return InputInterceptHook::CallbackResult::ContinueProcessingEvent;

			// Pressed far enough: held, and more may be pressed with it (a chord).
			if (s_capture.keys.size() < 4)
			{
				InputBindingKey held = key;
				held.modifier = (value < 0.0f) ? InputModifier::Negate : InputModifier::None;
				held.invert = 0;
				s_capture.keys.push_back(held);
			}
			return InputInterceptHook::CallbackResult::StopProcessingEvent;
		}

		void BeginCapture(int row)
		{
			if (s_capture.row >= 0)
				return;
			s_capture = {};
			s_capture.row = row;
			s_capture.until = Clock::now() + kCaptureTime;
			s_message.clear();
			InputManager::SetHook([](InputBindingKey key, float value) { return OnCaptureEvent(key, value); });
			PostShown();
		}

		// Given up (Esc), or nothing pressed in time.
		void EndCapture(std::string message)
		{
			InputManager::RemoveHook();
			s_capture = {};
			s_message = std::move(message);
			sound::Play(Sound::Back);
			PostShown();
		}

		void FinishCapture()
		{
			const int row = s_capture.row;
			std::string binding;
			{
				auto lock = Host::GetSettingsLock();
				if (const InputBindingInfo* bi = RowInfo(Base(), row))
					binding = InputManager::ConvertInputBindingKeysToString(bi->bind_type, s_capture.keys.data(), s_capture.keys.size());
			}
			InputManager::RemoveHook();
			s_capture = {};
			if (binding.empty())
			{
				s_message = "That cannot be a control.";
				sound::Play(Sound::Back);
				PostShown();
				return;
			}
			sound::Play(Sound::Choose);
			SetBinding(row, binding);
		}

		// ---- the graphics card -----------------------------------------------------

		GSRendererType RendererFor(RenderAPI api)
		{
			switch (api)
			{
				case RenderAPI::D3D11: return GSRendererType::DX11;
				case RenderAPI::D3D12: return GSRendererType::DX12;
				case RenderAPI::Vulkan: return GSRendererType::VK;
				case RenderAPI::OpenGL: return GSRendererType::OGL;
				case RenderAPI::Metal: return GSRendererType::Metal;
				default: return GSRendererType::Null;
			}
		}

		// What draws the game now, asked of the GS thread.
		std::string CardInUse(RenderAPI* api = nullptr)
		{
			std::string name;
			RenderAPI in_use = RenderAPI::None;
			MTGS::RunOnGSThread([&name, &in_use]() {
				if (g_gs_device)
				{
					name = g_gs_device->GetName();
					in_use = g_gs_device->GetRenderAPI();
				}
			});
			MTGS::WaitGS(false, false, false);
			if (api)
				*api = in_use;
			return name;
		}

		// The cards PCSX2 offers for the renderer the game is drawn with.
		void ReadAdapters()
		{
			RenderAPI api = RenderAPI::None;
			CardInUse(&api);
			s_adapters.clear();
			for (const GSAdapterInfo& adapter : GSGetAdapterInfo(RendererFor(api)))
			{
				// Windows' software renderer, which Direct3D lists with the cards,
				// is not a graphics card: a game on it would crawl.
				if (adapter.name != "Microsoft Basic Render Driver")
					s_adapters.push_back(adapter.name);
			}
			s_adapters_read = true;
			PostShown();
		}

		// The card in the settings the core holds: the game's own, as the
		// wizard's graphics page keeps it, where the game has them.
		void SetCardSetting(const std::string& name)
		{
			auto lock = Host::GetSettingsLock();
			SettingsInterface* game = Host::Internal::GetGameSettingsLayer();
			(game ? *game : Base()).SetStringValue("EmuCore/GS", "Adapter", name.c_str());
		}

		// And in the game's own settings file, for its next start.
		bool KeepCardSetting(const std::string& name)
		{
			std::string path;
			{
				auto lock = Host::GetSettingsLock();
				// The core's game settings are an INI file's (VMManager::UpdateGameSettingsLayer).
				if (SettingsInterface* game = Host::Internal::GetGameSettingsLayer())
					path = static_cast<INISettingsInterface*>(game)->GetFileName();
			}
			if (path.empty() && VMManager::GetDiscCRC() != 0)
				path = VMManager::GetGameSettingsPath(VMManager::GetDiscSerial(), VMManager::GetDiscCRC());
			if (path.empty())
				return false;
			INISettingsInterface ini(path);
			if (FileSystem::FileExists(path.c_str()) && !ini.Load())
				return false;
			ini.SetStringValue("EmuCore/GS", "Adapter", name.c_str());
			return ini.Save();
		}

		// The game drawn by `name` from now on: PCSX2 makes its renderer again on
		// it, as when its setting changes while a game runs (GSUpdateConfig),
		// and goes back to the card before where that card cannot.
		void ChooseCard(const std::string& name)
		{
			const std::string before = EmuConfig.GS.Adapter;
			SetCardSetting(name);
			EmuConfig.GS.Adapter = name;
			MTGS::ApplySettings();
			const std::string now = CardInUse();
			if (now == name)
			{
				s_message = KeepCardSetting(name) ?
								fmt::format("The game is drawn by {} from now on.", name) :
								fmt::format("The game is drawn by {}, until it ends: its settings file could not be written.", name);
			}
			else
			{
				SetCardSetting(before);
				EmuConfig.GS.Adapter = before;
				s_message = fmt::format("{} could not draw the game: {} draws it, as before.", name, now);
			}
			PostShown();
		}

		// ---- drawing, on the GS thread ----------------------------------------------

		// The brand's colours (assets/branding/README.md), as the notice has them:
		// navy (0A1022) behind, at various strengths below, the emblem's blue.
		constexpr ImU32 kBlue = IM_COL32(0x00, 0x60, 0xFC, 0xFF);
		constexpr ImU32 kWhite = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
		constexpr ImU32 kDim = IM_COL32(0xB4, 0xBC, 0xCC, 0xFF);

		// The menu's measures, in pixels at a picture 720 high, and its scale.
		float s_scale = 1.0f;
		float Px(float pixels)
		{
			return std::round(pixels * s_scale);
		}

		void Text(float size, ImU32 colour, std::string_view text, bool wrap = false)
		{
			ImGui::PushFont(ImGuiManager::GetStandardFont(), Px(size));
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			if (wrap)
				ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(text.data(), text.data() + text.size());
			if (wrap)
				ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}


		// The item just drawn, pointed at or reached; chosen, with the sound
		// that says so (none where the menu closes: Close() sounds then).
		void Pointed(bool pressed, std::optional<Sound> sound)
		{
			const ImGuiID id = ImGui::GetItemID();
			if (ImGui::IsItemHovered())
				s_hovered_now = id;
			if (ImGui::IsItemFocused())
				s_focused_now = id;
			if (pressed)
			{
				s_quiet_frames = std::max(s_quiet_frames, 2);
				if (sound)
					sound::Play(*sound);
			}
		}

		// A line of the menu that can be chosen: its label on the left, what it
		// is on the right. True when chosen (a click, Enter, a controller's
		// Cross). `width` 0: the rest of the line.
		bool Item(const char* id, std::string_view label, std::string_view value = {}, float width = 0.0f, bool chosen = false,
			std::optional<Sound> sound = Sound::Choose)
		{
			const float height = Px(44.0f);
			if (width <= 0.0f)
				width = ImGui::GetContentRegionAvail().x;
			const ImVec2 at = ImGui::GetCursorScreenPos();
			// (An invisible button is reached with the keys and a controller only
			// when asked: ImGui leaves it out of navigation otherwise.)
			const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_EnableNav);
			Pointed(pressed, sound);
			const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 end(at.x + width, at.y + height);
			if (hot || chosen)
				draw->AddRectFilled(at, end, hot ? IM_COL32(0x00, 0x60, 0xFC, 0x66) : IM_COL32(0xFF, 0xFF, 0xFF, 0x14), Px(4.0f));
			if (hot)
				draw->AddRectFilled(at, ImVec2(at.x + Px(4.0f), end.y), kBlue);

			ImFont* font = ImGuiManager::GetStandardFont();
			const float size = Px(22.0f);
			const float y = at.y + (height - size) * 0.5f;
			draw->PushClipRect(at, end, true);
			draw->AddText(font, size, ImVec2(at.x + Px(16.0f), y), kWhite, label.data(), label.data() + label.size());
			if (!value.empty())
			{
				const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.0f, value.data(), value.data() + value.size());
				draw->AddText(font, size, ImVec2(end.x - Px(16.0f) - measured.x, y), kDim, value.data(), value.data() + value.size());
			}
			draw->PopClipRect();
			return pressed;
		}

		// A cell of the controls' table: what an input is on one side. True when
		// chosen; `clear` when Delete, a right click or a controller's Square
		// asks for it to be emptied.
		bool Cell(const char* id, std::string_view text, float width, bool waiting, bool* clear)
		{
			const float height = Px(38.0f);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height),
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_EnableNav);
			const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
			const bool right = pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
			*clear = right || (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
															  ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
															  ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)));
			// Chosen, it waits for a key: the rising notes. Cleared: the falling
			// ones. Nothing while a control is being set, which takes no choice.
			const bool idle = s_shown.capturing < 0;
			Pointed(pressed || *clear, idle ? std::optional<Sound>(*clear ? Sound::Back : Sound::Choose) : std::nullopt);
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 end(at.x + width, at.y + height);
			draw->AddRectFilled(at, end, waiting ? IM_COL32(0x00, 0x60, 0xFC, 0xAA) : hot ? IM_COL32(0x00, 0x60, 0xFC, 0x66) : IM_COL32(0xFF, 0xFF, 0xFF, 0x10), Px(4.0f));
			if (hot)
				draw->AddRect(at, end, kBlue, Px(4.0f), Px(2.0f));
			ImFont* font = ImGuiManager::GetStandardFont();
			const float size = Px(20.0f);
			const std::string_view shown = text.empty() ? std::string_view("-") : text;
			draw->PushClipRect(at, end, true);
			draw->AddText(font, size, ImVec2(at.x + Px(10.0f), at.y + (height - size) * 0.5f), text.empty() ? kDim : kWhite,
				shown.data(), shown.data() + shown.size());
			draw->PopClipRect();
			return pressed && !right;
		}

		// The panel a page is in, in the middle of the picture: navy, with the
		// emblem's blue down its side, as the notice is.
		bool BeginPanel(const char* id, float width, float height)
		{
			const ImGuiIO& io = ImGui::GetIO();
			width = std::min(width, io.DisplaySize.x - Px(32.0f));
			ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (height > 0.0f)
				ImGui::SetNextWindowSize(ImVec2(width, std::min(height, io.DisplaySize.y - Px(32.0f))), ImGuiCond_Always);
			else
				ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, io.DisplaySize.y - Px(32.0f)));
			// The page's panel is the only window: ImGui focuses it when it
			// appears; when a click beside it has left no window focused, it takes
			// the focus back (ImGui gives it back what it had reached). Not
			// otherwise: the focus may be in the controls' table, a window of its
			// own inside it.
			if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
				ImGui::SetNextWindowFocus();
			ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0x0A, 0x10, 0x22, 0xF5));
			ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0x00, 0x60, 0xFC, 0x80));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(0x00, 0x60, 0xFC, 0x80));
			ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(0xFF, 0xFF, 0xFF, 0x30));
			ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0)); // the items show their focus themselves
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(28.0f), Px(24.0f)));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(8.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, Px(1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(8.0f), Px(6.0f)));
			ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, Px(10.0f));
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
										   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
										   (height > 0.0f ? 0 : ImGuiWindowFlags_AlwaysAutoResize);
			const bool shown = ImGui::Begin(id, nullptr, flags);
			if (s_show_focus)
			{
				ImGui::SetNavCursorVisible(true);
				s_show_focus = false;
			}
			if (shown)
			{
				const ImVec2 at = ImGui::GetWindowPos();
				const ImVec2 size = ImGui::GetWindowSize();
				ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + Px(5.0f), at.y + size.y), kBlue, Px(8.0f), ImDrawFlags_RoundLeft);
			}
			return shown;
		}

		void EndPanel()
		{
			ImGui::End();
			ImGui::PopStyleVar(5);
			ImGui::PopStyleColor(7);
		}

		void Gap(float pixels)
		{
			ImGui::Dummy(ImVec2(0.0f, Px(pixels)));
		}

		void Hint(std::string_view text)
		{
			Gap(6.0f);
			Text(16.0f, kDim, text, true);
		}

		bool s_page_changed = false;

		// Another page: its first item takes the focus, and what was said on
		// the page before goes.
		void GoTo(Page page)
		{
			if (page == Page::Controls)
				s_main_default = 1;
			else if (page == Page::GraphicsCard)
				s_main_default = 2;
			else if (page == Page::Reboot)
				s_main_default = 3;
			else if (page == Page::Quit)
				s_main_default = 4;
			s_page = page;
			s_from_close = false;
			s_page_changed = true;
			s_show_focus = true;
			s_quiet_frames = 4;
			s_shown.message.clear();
			Do([]() { s_message.clear(); });
		}

		// Back a page, or out of the menu from its first, or from Quit's question
		// where the window's close button asked it (Close() sounds then).
		void Back()
		{
			if (s_page == Page::Main || (s_page == Page::Quit && s_from_close))
			{
				Do([]() { s_open = false; });
				return;
			}
			sound::Play(Sound::Back);
			GoTo(Page::Main);
		}

		void DrawMain()
		{
			if (BeginPanel("##ps2n-menu", Px(560.0f), 0.0f))
			{
				Text(30.0f, kWhite, s_shown.title, true);
				Text(18.0f, kDim, "Paused");
				Gap(14.0f);
				const auto default_here = [](int item) {
					if (s_main_default == item)
						ImGui::SetItemDefaultFocus();
				};
				if (Item("##resume", "Resume", {}, 0.0f, false, std::nullopt))
					Do([]() { s_open = false; });
				default_here(0);
				if (Item("##controls", "Controls"))
					GoTo(Page::Controls);
				default_here(1);
				if (Item("##card", "Graphics card"))
					GoTo(Page::GraphicsCard);
				default_here(2);
				if (Item("##reboot", "Reboot"))
					GoTo(Page::Reboot);
				default_here(3);
				if (Item("##quit", "Quit"))
					GoTo(Page::Quit);
				default_here(4);
				Hint("Esc: back to the game");
			}
			EndPanel();
		}

		// Quit's question, or Reboot's: what was not saved in the game is lost.
		// Back is reached first: a choice made by mistake loses nothing.
		void DrawLeave(bool reboot)
		{
			if (BeginPanel(reboot ? "##ps2n-reboot" : "##ps2n-quit", Px(560.0f), 0.0f))
			{
				Text(28.0f, kWhite, fmt::format("{} {}?", reboot ? "Reboot" : "Quit", s_shown.title), true);
				Gap(4.0f);
				Text(20.0f, kDim,
					reboot ? "The game starts again from the beginning. Anything you have not saved in it will be lost." :
							 "Anything you have not saved in the game will be lost.",
					true);
				if (!s_shown.message.empty())
				{
					Gap(4.0f);
					Text(18.0f, kWhite, s_shown.message, true);
				}
				Gap(14.0f);
				if (Item(reboot ? "##reboot-yes" : "##quit-yes", reboot ? "Reboot" : "Quit", {}, 0.0f, false, std::nullopt))
					Do([reboot]() { Leave(reboot); });
				if (Item(reboot ? "##reboot-no" : "##quit-no", "Back", {}, 0.0f, false, std::nullopt))
					Back();
				ImGui::SetItemDefaultFocus();
				Hint(s_from_close ? "Esc: back to the game" : "Esc: back");
			}
			EndPanel();
		}

		void DrawControls()
		{
			const ImGuiIO& io = ImGui::GetIO();
			if (BeginPanel("##ps2n-controls", Px(880.0f), io.DisplaySize.y - Px(64.0f)))
			{
				const bool waiting = s_shown.capturing >= 0;
				const u32 player = s_shown.player;
				Text(28.0f, kWhite, "Controls");
				Gap(4.0f);

				// The players: the one whose controls are shown, marked.
				const float tab = std::floor((ImGui::GetContentRegionAvail().x - Px(8.0f)) * 0.5f);
				for (u32 p = 0; p < kPlayers; p++)
				{
					if (p > 0)
						ImGui::SameLine(0.0f, Px(8.0f));
					const bool this_one = (p == player);
					if (Item(fmt::format("##player-{}", p + 1).c_str(), fmt::format("Player {}", p + 1), {}, tab, this_one,
							(waiting || this_one) ? std::nullopt : std::optional<Sound>(Sound::Choose)) &&
						!waiting && !this_one)
						Do([p]() { SelectPlayer(p); });
				}
				Gap(4.0f);
				std::string said;
				if (player == 1 && !s_shown.port2)
				{
					said = s_shown.playing.empty() ?
							   std::string("Player 2 has nothing to play with yet. Give them a controller below, or keys: the game "
										   "sees a second controller from then on.") :
							   fmt::format("Player 2 plays with {}: the game sees a second controller once one of them is connected.",
								   s_shown.playing);
				}
				else
				{
					said = s_shown.playing.empty() ? fmt::format("Nothing plays player {}'s controller.", player + 1) :
													 fmt::format("Player {} plays with {}.", player + 1, s_shown.playing);
				}
				Text(18.0f, kDim, said, true);
				Gap(8.0f);

				// The controllers connected: one chosen plays the shown player's, with
				// the mapping PCSX2 makes for it.
				if (s_shown.controllers.empty())
					Text(18.0f, kDim, "No controller is connected.");
				for (const Device& device : s_shown.controllers)
				{
					const std::string whose = (device.player < 0) ? device.id : fmt::format("Player {}'s", device.player + 1);
					if (Item(fmt::format("##use-{}", device.id).c_str(), fmt::format("Play with {}", device.name), whose, 0.0f,
							device.player == static_cast<int>(player), waiting ? std::nullopt : std::optional<Sound>(Sound::Choose)) &&
						!waiting)
						Do([id = device.id]() { UseController(id); });
				}
				Gap(6.0f);

				// The table: each input, and what it is on the keyboard and on a
				// controller.
				const float width = ImGui::GetContentRegionAvail().x;
				const float label_width = std::floor(width * 0.36f);
				const float cell_width = std::floor((width - label_width - Px(24.0f)) * 0.5f);
				ImGui::PushFont(ImGuiManager::GetStandardFont(), Px(17.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, kDim);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label_width + Px(8.0f));
				ImGui::TextUnformatted("Keyboard");
				ImGui::SameLine(label_width + Px(8.0f) + cell_width + Px(8.0f) + ImGui::GetStyle().WindowPadding.x);
				ImGui::TextUnformatted("Controller");
				ImGui::PopStyleColor();
				ImGui::PopFont();

				// Under the table: what was last done (a line), the two items, the hint
				// (two lines).
				const float footer = Px(4.0f) + Px(24.0f) + Px(44.0f) + Px(6.0f) + Px(6.0f) + Px(44.0f) + Px(8.0f);
				const float rows_height = std::max(Px(120.0f), ImGui::GetContentRegionAvail().y - footer);
				if (ImGui::BeginChild("##rows", ImVec2(0.0f, rows_height), ImGuiChildFlags_NavFlattened, 0))
				{
					for (size_t i = 0; i < s_shown.rows.size(); i++)
					{
						const Row& row = s_shown.rows[i];
						const bool this_one = waiting && s_shown.capturing == static_cast<int>(i);
						ImGui::PushID(static_cast<int>(i));
						const ImVec2 at = ImGui::GetCursorScreenPos();
						ImGui::GetWindowDrawList()->AddText(ImGuiManager::GetStandardFont(), Px(20.0f),
							ImVec2(at.x + Px(4.0f), at.y + (Px(38.0f) - Px(20.0f)) * 0.5f), kWhite, row.label.c_str());
						ImGui::SetCursorScreenPos(ImVec2(at.x + label_width, at.y));
						const std::string countdown = this_one ?
														  fmt::format("Press... {}", std::max<long long>(1, std::chrono::ceil<std::chrono::seconds>(s_shown.capture_until - Clock::now()).count())) :
														  std::string();
						bool clear = false;
						if (Cell("##keys", this_one ? std::string_view(countdown) : std::string_view(row.keys), cell_width, this_one, &clear) && !waiting)
							Do([i]() { BeginCapture(static_cast<int>(i)); });
						else if (clear && !waiting)
							Do([i]() { ClearBinding(static_cast<int>(i), true); });
						ImGui::SameLine(0.0f, Px(8.0f));
						clear = false;
						if (Cell("##pad", this_one ? std::string_view(countdown) : std::string_view(row.pad), cell_width, this_one, &clear) && !waiting)
							Do([i]() { BeginCapture(static_cast<int>(i)); });
						else if (clear && !waiting)
							Do([i]() { ClearBinding(static_cast<int>(i), false); });
						ImGui::PopID();
					}
				}
				ImGui::EndChild();

				Gap(4.0f);
				Text(17.0f, kDim, s_shown.message.empty() ? std::string_view(" ") : std::string_view(s_shown.message), true);
				const float half = std::floor((ImGui::GetContentRegionAvail().x - Px(8.0f)) * 0.5f);
				if (Item("##defaults", "Default controls", {}, half, false, waiting ? std::nullopt : std::optional<Sound>(Sound::Choose)) && !waiting)
					Do([]() { DefaultControls(); });
				ImGui::SameLine(0.0f, Px(8.0f));
				if (Item("##back", "Back", {}, half, false, std::nullopt) && !waiting)
					Back();
				Hint(waiting ? fmt::format("Press a key, or a controller's button or stick, for player {}, and let go. Esc: leave it as "
										   "it is.",
								   player + 1) :
							   std::string("Enter, Cross or a click: set it. Delete, Square or a right click: clear it. L1 or R1: the "
										   "other player. Esc or Circle: back."));
			}
			EndPanel();
		}

		void DrawGraphicsCard()
		{
			if (!s_adapters_asked)
			{
				s_adapters_asked = true;
				Do([]() { ReadAdapters(); });
			}
			const std::string in_use = g_gs_device ? g_gs_device->GetName() : std::string();
			const char* api = g_gs_device ? GSDevice::RenderAPIToString(g_gs_device->GetRenderAPI()) : "";
			if (BeginPanel("##ps2n-card", Px(640.0f), 0.0f))
			{
				Text(28.0f, kWhite, "Graphics card");
				Text(18.0f, kDim, fmt::format("The game is drawn by {}, with {}.", in_use, api), true);
				Gap(10.0f);
				if (!s_shown.adapters_read)
					Text(20.0f, kDim, "Looking for this computer's graphics cards...");
				for (const std::string& adapter : s_shown.adapters)
				{
					const bool current = (adapter == in_use);
					if (Item(fmt::format("##card-{}", adapter).c_str(), adapter, current ? "In use" : "", 0.0f, current,
							current ? std::nullopt : std::optional<Sound>(Sound::Choose)) &&
						!current)
						Do([adapter]() { ChooseCard(adapter); });
				}
				if (s_shown.adapters_read && s_shown.adapters.size() <= 1)
					Text(18.0f, kDim, "It is the only one PCSX2 offers for this renderer.", true);
				Gap(6.0f);
				if (!s_shown.message.empty())
					Text(17.0f, kDim, s_shown.message, true);
				if (Item("##card-back", "Back", {}, 0.0f, false, std::nullopt))
					Back();
				Hint("A card chosen draws the game at once, and when it next starts.");
			}
			EndPanel();
		}

		void Draw()
		{
			if (!ImGui::GetCurrentContext() || !ImGuiManager::GetStandardFont())
				return;
			const ImGuiIO& io = ImGui::GetIO();
			s_scale = std::clamp(std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f), 0.6f, 4.0f);

			// The game's picture dimmed behind the menu.
			ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0x0A, 0x10, 0x22, 0xB4));

			// A controller's Circle goes back, as Esc does (the keyboard's Esc
			// comes through OnKey), except while a control is being set.
			if (s_shown.capturing < 0 && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
				Back();

			// On the Controls page its L1 and R1 show the other player's.
			if (s_page == Page::Controls && s_shown.capturing < 0 &&
				(ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)))
			{
				const u32 other = (s_shown.player == 0) ? 1 : 0;
				sound::Play(Sound::Point);
				Do([other]() { SelectPlayer(other); });
			}

			switch (s_page)
			{
				case Page::Main: DrawMain(); break;
				case Page::Quit: DrawLeave(false); break;
				case Page::Reboot: DrawLeave(true); break;
				case Page::Controls: DrawControls(); break;
				case Page::GraphicsCard: DrawGraphicsCard(); break;
			}
			s_asking_quit.store(s_page == Page::Quit);

			// The pointer, the keys or a controller have reached another item.
			const bool moved = (s_hovered_now != 0 && s_hovered_now != s_hovered) || (s_focused_now != 0 && s_focused_now != s_focused);
			if (moved && s_quiet_frames == 0)
				sound::Play(Sound::Point);
			s_hovered = s_hovered_now;
			s_focused = s_page_changed ? 0 : s_focused_now; // the new page's items are not focused yet
			s_hovered_now = s_focused_now = 0;
			s_page_changed = false;
			if (s_quiet_frames > 0)
				s_quiet_frames--;
		}
	} // namespace

	std::optional<Given> GiveController(SettingsInterface& si, std::string_view device, const Devices& connected)
	{
		if (!IsController(device))
			return std::nullopt;
		// Already a player's: theirs.
		for (u32 port = 0; port < kPlayers; port++)
		{
			if (Names(si, port, device))
				return std::nullopt;
		}
		// Player 1's, beside the keys, where the controls are the defaults and
		// player 1 has no controller connected; player 2's, where player 1 plays
		// with another that is, and player 2 with none.
		std::optional<u32> port;
		if (!s_controls_own && !PlaysWithController(si, 0, &connected, device))
			port = 0;
		else if (PlaysWithController(si, 0, &connected, device) && !PlaysWithController(si, 1, nullptr))
			port = 1;
		if (!port)
			return std::nullopt;
		const u32 inputs = AddController(si, *port, device);
		if (inputs == 0)
			return std::nullopt;
		return Given{*port, inputs};
	}

	bool PlaysWithSomething(const SettingsInterface& si, u32 port, const Devices& connected)
	{
		bool found = false;
		ForEachInput(si, port, [&](const InputBindingInfo&, const std::vector<std::string>& bindings) {
			found = found ||
					std::any_of(bindings.begin(), bindings.end(), [&connected](const std::string& b) { return IsConnected(DeviceOf(b), connected); });
		});
		return found;
	}

	void Refresh()
	{
		if (s_open)
			PostShown();
	}

	bool LoadControls(SettingsInterface& si, const std::string& data_root)
	{
		s_controls_path = Path::Combine(Path::Combine(data_root, "inputprofiles"), "controls.ini");
		s_controls_own = false;
		if (!FileSystem::FileExists(s_controls_path.c_str()))
			return false;
		INISettingsInterface ini(s_controls_path);
		if (!ini.Load())
			return false;
		Pad::CopyConfiguration(&si, ini, true, true, false);
		// Player 2's whatever port 2 is said to hold (the runner has it empty
		// until a controller is put in for them: main.cpp).
		CopyPlayer2(si, ini);
		s_controls_own = true;
		return true;
	}

	bool ControlsAreOwn()
	{
		return s_controls_own;
	}

	bool OnKey(u32 code, bool pressed)
	{
		const bool escape = (code == window::KeyEscape);
		if (!s_open)
		{
			// Esc is the menu's, never one of the controller's buttons.
			if (escape && pressed)
				s_requested.store(true);
			return escape;
		}

		const InputBindingKey key = InputManager::MakeHostKeyboardKey(code);
		if (s_capture.row >= 0 && !s_capture.done)
		{
			if (escape)
			{
				if (pressed)
					EndCapture("Left as it was.");
				return true;
			}
			// To the input hook, which takes it.
			InputManager::InvokeEvents(key, pressed ? 1.0f : 0.0f);
			return true;
		}

		if (escape)
		{
			if (pressed)
				MTGS::RunOnGSThread([]() { Back(); });
			return true;
		}
		// To the menu's ImGui, never to the controller.
		ImGuiManager::ProcessHostKeyEvent(key, pressed ? 1.0f : 0.0f);
		return true;
	}

	void OnPointerMoved(float x, float y)
	{
		s_pointer_x = x;
		s_pointer_y = y;
		if (s_open)
			ImGuiManager::UpdateMousePosition(x, y);
	}

	void OnPointerButton(u32 button, bool pressed)
	{
		if (s_open)
			ImGuiManager::ProcessPointerButtonEvent(InputManager::MakePointerButtonKey(0, button), pressed ? 1.0f : 0.0f);
	}

	void OnPointerHidden()
	{
		s_pointer_x = -1.0f;
		s_pointer_y = -1.0f;
		if (s_open)
			ImGuiManager::UpdateMousePosition(-FLT_MAX, -FLT_MAX);
	}

	void OnWheel(float dx, float dy)
	{
		if (!s_open)
			return;
		if (dy != 0.0f)
			ImGuiManager::ProcessPointerAxisEvent(InputManager::MakePointerAxisKey(0, InputPointerAxis::WheelY), dy);
		if (dx != 0.0f)
			ImGuiManager::ProcessPointerAxisEvent(InputManager::MakePointerAxisKey(0, InputPointerAxis::WheelX), dx);
	}

	void OnFocusLost()
	{
		if (!s_open)
			return;
		MTGS::RunOnGSThread([]() {
			if (ImGui::GetCurrentContext())
			{
				ImGui::GetIO().ClearInputKeys();
				ImGui::GetIO().ClearInputMouse();
			}
		});
	}

	bool Requested()
	{
		return s_requested.load();
	}

	void AskQuit()
	{
		if (s_open)
		{
			// Open already: to Quit's question, from where Back goes back to the game.
			// A control waiting for its key is left as it was: the keys are the
			// question's now.
			if (s_capture.row >= 0)
			{
				InputManager::RemoveHook();
				s_capture = {};
			}
			s_message.clear();
			MTGS::RunOnGSThread([]() {
				GoTo(Page::Quit);
				s_from_close = true;
			});
			return;
		}
		s_ask_quit = true;
		s_requested.store(true);
	}

	void ConfirmQuit()
	{
		if (s_open)
			Leave(false);
	}

	bool IsOpen()
	{
		return s_open.load();
	}

	bool IsAskingQuit()
	{
		return s_open.load() && s_asking_quit.load();
	}

	void Open(const std::string& title)
	{
		s_requested.store(false);
		s_open = true;
		s_quit = false;
		s_reboot = false;
		s_title = title;
		s_message.clear();
		s_player = 0;
		{
			std::lock_guard lock(s_actions_mutex);
			s_actions.clear();
		}
		PostShown(true, s_ask_quit);
		s_ask_quit = false;
		if (s_pointer_x >= 0.0f)
			ImGuiManager::UpdateMousePosition(s_pointer_x, s_pointer_y);
		sound::Play(Sound::Open);
	}

	bool Tick()
	{
		RunActions();
		if (s_capture.row >= 0)
		{
			if (s_capture.done)
				FinishCapture();
			else if (Clock::now() >= s_capture.until)
				EndCapture("Nothing was pressed: left as it was.");
		}
		if (!s_open)
			return false;

		// The menu's frame, over the game's last picture. The one before has been
		// shown first, so that frames do not pile up behind a slow present.
		MTGS::WaitGS(false, false, false);
		MTGS::RunOnGSThread([]() {
			Draw();
			GSPresentCurrentFrame();
		});
		return true;
	}

	bool QuitChosen()
	{
		return s_quit;
	}

	bool RebootChosen()
	{
		return s_reboot;
	}

	void Close()
	{
		if (s_capture.row >= 0)
		{
			InputManager::RemoveHook();
			s_capture = {};
		}
		s_open = false;
		s_asking_quit.store(false);
		s_ask_quit = false;
		s_requested.store(false);
		{
			std::lock_guard lock(s_actions_mutex);
			s_actions.clear();
		}
		sound::Play(Sound::Close);
		MTGS::RunOnGSThread([]() {
			s_page = Page::Main;
			if (ImGui::GetCurrentContext())
			{
				ImGui::GetIO().ClearInputKeys();
				ImGui::GetIO().ClearInputMouse();
			}
		});
	}
} // namespace ps2n::host::menu
