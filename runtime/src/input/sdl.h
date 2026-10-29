// The SDL side of controller input: game controllers, numbered SDL-0,
// SDL-1, ... as PCSX2 numbers them (the controller's player index when it
// has a free one, else the lowest free number); keyboard key names; rumble;
// and the SDL options of the settings file ([InputSources], [SDLExtra],
// [SDLHints], as PCSX2 has them). Main thread only; SDL builds only.
#pragma once

#ifdef PS2N_HAVE_SDL2

#include "input/input.h"

#include <SDL.h>

#include <string>
#include <vector>

namespace ps2::input::sdl {

// Before SDL_Init: the hints the settings ask for.
void setHints();
// SDL's game controller subsystem started or stopped.
void open();
void close();
// Stops and restarts the subsystem, so that changed hints take effect.
void restart();

// Controllers coming and going. Returns a message for the screen, or "".
std::string handleEvent(const SDL_Event& ev);
HostState poll();
// Drives every controller's motors; controllers not listed stop.
void applyRumble(const std::vector<Rumble>& rumble);
// The controllers' LEDs, from [SDLExtra] Player<N>LED.
void applyLeds();

struct Device {
    int device;          // SDL-<device>
    std::string name;
    bool rumble;
};
std::vector<Device> devices();
int deviceOf(SDL_JoystickID id);   // -1: not an open controller

// PCSX2's name for a key ("K", "Return", "Shift", "Numpad5"), or "" for none.
std::string keyName(SDL_Keycode key);

} // namespace ps2::input::sdl

#endif
