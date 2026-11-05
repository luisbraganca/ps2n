// What the window draws over the game, with Dear ImGui (third_party/imgui):
// the pause menu, the controller settings modelled on PCSX2's (its Qt
// window's pages and DualShock 2 layout, its binding capture), and
// on-screen messages. Main thread only; SDL builds only.
#pragma once

#ifdef PS2N_HAVE_SDL2

#include "input/input.h"

#include <SDL.h>

#ifdef PS2N_HAVE_VULKAN
#include "gpu/vk.h"
#endif

#include <string>

namespace ps2::ui {

// SDL's renderer draws the overlay.
void init(SDL_Window* window, SDL_Renderer* renderer);
#ifdef PS2N_HAVE_VULKAN
// The window shows its pictures through Vulkan (video/present_vk.h), the
// overlay too.
void initVulkan(SDL_Window* window);
#endif
void shutdown();

// Every SDL event: the overlay's widgets and the binding capture read them.
void processEvent(const SDL_Event& ev);
// Once per host frame: lays out the overlay for what the host holds now.
// render() then draws it, after the game's picture.
void frame(const input::HostState& host);
void render();
#ifdef PS2N_HAVE_VULKAN
// With Vulkan: records the overlay into the window's command buffer, inside
// its rendering. The window's queue lock is held: ImGui uploads its textures
// with submissions of its own.
void render(VkCommandBuffer cmd);
#endif

// The pause menu or the settings are up: the game pauses and the input is the overlay's.
bool menuOpen();
void toggleMenu();
// Waiting for a key or button to bind: hotkeys are off meanwhile.
bool capturing();

// A message in the corner of the screen, as PCSX2's on-screen display.
void osd(const std::string& message, float seconds = 3.0f);
// The game is paused without the menu (the Toggle Pause hotkey).
void setPausedIndicator(bool on);

// What the menu asked the window to do.
enum class Action { None, ToggleFullscreen, Quit };
Action takeAction();

} // namespace ps2::ui

#endif
