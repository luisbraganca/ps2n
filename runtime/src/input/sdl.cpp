#include "input/sdl.h"

#ifdef PS2N_HAVE_SDL2

#include "log.h"
#include "settings.h"

#include "ps2n/util.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace ps2::input::sdl {

namespace {

struct Opened {
    SDL_GameController* controller;
    SDL_JoystickID id;
    int device;
    bool rumble;
    uint16_t low = 0, high = 0;   // what the motors were last told
    uint32_t sentAt = 0;
};
std::vector<Opened> g_open;

int freeDevice(int wanted) {
    auto used = [](int d) { return std::any_of(g_open.begin(), g_open.end(), [&](const Opened& o) { return o.device == d; }); };
    if (wanted >= 0 && !used(wanted)) return wanted;
    int d = 0;
    while (used(d)) ++d;
    return d;
}

std::string deviceName(const Opened& o) {
    const char* name = SDL_GameControllerName(o.controller);
    return name ? name : "Game Controller";
}

void closeAll() {
    for (const Opened& o : g_open) SDL_GameControllerClose(o.controller);
    g_open.clear();
}

} // namespace

void setHints() {
    const settings::Ini& ini = settingsFile();
    // DualShock 4 / DualSense "enhanced mode": their rumble and lights, at the
    // cost of the controller's simple mode for other programs until it reconnects.
    const bool enhanced = ini.getBool("InputSources", "SDLControllerEnhancedMode", true);
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, enhanced ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, enhanced ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_PLAYER_LED, ini.getBool("InputSources", "SDLPS5PlayerLED", true) ? "1" : "0");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, ini.getBool("InputSources", "SDLRawInput", false) ? "1" : "0");
#endif
    // Controllers keep working while another window has the focus.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Extra controller mappings, as PCSX2 reads them: game_controller_db.txt
    // next to the settings file.
    const auto db = settingsPath().parent_path() / "game_controller_db.txt";
    std::error_code ec;
    if (std::filesystem::exists(db, ec)) {
        const std::u8string path = db.u8string();
        SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE, reinterpret_cast<const char*>(path.c_str()));
    }
    // [SDLHints]: any SDL hint, by name, over the ones above.
    for (const auto& [name, value] : ini.entries("SDLHints")) SDL_SetHint(name.c_str(), value.c_str());
}

void open() {
    // The subsystem reports every controller already connected as added.
    g_open.clear();
}

void close() {
    closeAll();
}

void restart() {
    closeAll();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    setHints();
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        PS2_WARN("input", "cannot restart SDL's game controllers (%s)", SDL_GetError());
}

std::string handleEvent(const SDL_Event& ev) {
    if (ev.type == SDL_CONTROLLERDEVICEADDED) {
        SDL_GameController* c = SDL_GameControllerOpen(ev.cdevice.which);
        if (!c) return {};
        const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
        if (deviceOf(id) >= 0) {   // already open (reported twice)
            SDL_GameControllerClose(c);
            return {};
        }
        Opened o{c, id, freeDevice(SDL_GameControllerGetPlayerIndex(c)), SDL_GameControllerHasRumble(c) == SDL_TRUE};
        g_open.push_back(o);
        applyLeds();
        const std::string name = deviceName(o);
        PS2_INFO("input", "controller SDL-%d connected: %s%s", o.device, name.c_str(), o.rumble ? " (with rumble)" : "");
        std::string message = ps2n::strprintf("Controller SDL-%d (%s) connected.", o.device, name.c_str());
        if (adoptFirstController(o.device)) message += " Mapped to port 1.";
        return message;
    }
    if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
        for (auto it = g_open.begin(); it != g_open.end(); ++it) {
            if (it->id != ev.cdevice.which) continue;
            const std::string name = deviceName(*it);
            const int device = it->device;
            SDL_GameControllerClose(it->controller);
            g_open.erase(it);
            PS2_INFO("input", "controller SDL-%d disconnected: %s", device, name.c_str());
            return ps2n::strprintf("Controller SDL-%d (%s) disconnected.", device, name.c_str());
        }
    }
    return {};
}

HostState poll() {
    HostState s;
    int count = 0;
    const Uint8* keys = SDL_GetKeyboardState(&count);
    for (int sc = 0; sc < count; ++sc) {
        if (!keys[sc]) continue;
        std::string name = keyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc)));
        if (!name.empty() && std::find(s.keys.begin(), s.keys.end(), name) == s.keys.end()) s.keys.push_back(std::move(name));
    }
    for (const Opened& o : g_open) {
        HostState::Controller c;
        c.device = o.device;
        for (size_t b = 0; b < c.buttons.size(); ++b)
            c.buttons[b] = SDL_GameControllerGetButton(o.controller, static_cast<SDL_GameControllerButton>(b)) != 0;
        for (size_t a = 0; a < c.axes.size(); ++a) {
            const Sint16 v = SDL_GameControllerGetAxis(o.controller, static_cast<SDL_GameControllerAxis>(a));
            c.axes[a] = v < 0 ? v / 32768.0f : v / 32767.0f;
        }
        s.controllers.push_back(c);
    }
    return s;
}

void applyRumble(const std::vector<Rumble>& rumble) {
    const uint32_t now = SDL_GetTicks();
    for (Opened& o : g_open) {
        if (!o.rumble) continue;
        uint16_t low = 0, high = 0;
        for (const Rumble& r : rumble) {
            if (r.device != o.device) continue;
            low = static_cast<uint16_t>(std::clamp(r.large, 0.0f, 1.0f) * 65535.0f);
            high = static_cast<uint16_t>(std::clamp(r.small, 0.0f, 1.0f) * 65535.0f);
        }
        // Sent when it changes, and again every half second while it runs
        // (each request lasts a second, so a stalled game does not leave it buzzing).
        const bool running = low || high;
        if (low == o.low && high == o.high && (!running || now - o.sentAt < 500)) continue;
        SDL_GameControllerRumble(o.controller, low, high, running ? 1000 : 0);
        o.low = low;
        o.high = high;
        o.sentAt = now;
    }
}

void applyLeds() {
    static constexpr const char* kDefaults[4] = {"000080", "800000", "008000", "808000"};
    const settings::Ini& ini = settingsFile();
    for (const Opened& o : g_open) {
        if (o.device > 3 || !SDL_GameControllerHasLED(o.controller)) continue;
        const std::string hex = ini.getString("SDLExtra", ps2n::strprintf("Player%dLED", o.device), kDefaults[o.device]);
        const unsigned long rgb = std::strtoul(hex.c_str(), nullptr, 16);
        SDL_GameControllerSetLED(o.controller, static_cast<Uint8>(rgb >> 16), static_cast<Uint8>(rgb >> 8), static_cast<Uint8>(rgb));
    }
}

std::vector<Device> devices() {
    std::vector<Device> out;
    for (const Opened& o : g_open) out.push_back({o.device, deviceName(o), o.rumble});
    std::sort(out.begin(), out.end(), [](const Device& a, const Device& b) { return a.device < b.device; });
    return out;
}

int deviceOf(SDL_JoystickID id) {
    for (const Opened& o : g_open)
        if (o.id == id) return o.device;
    return -1;
}

std::string keyName(SDL_Keycode k) {
    // Qt's key names, which PCSX2 writes (Qt::Key_Return is "Return"):
    // letters upper case, no left/right modifiers, the keypad as "Numpad...".
    if (k >= SDLK_a && k <= SDLK_z) return std::string(1, static_cast<char>('A' + (k - SDLK_a)));
    if (k >= SDLK_0 && k <= SDLK_9) return std::string(1, static_cast<char>('0' + (k - SDLK_0)));
    if (k >= SDLK_F1 && k <= SDLK_F12) return "F" + std::to_string(1 + (k - SDLK_F1));
    if (k >= SDLK_F13 && k <= SDLK_F24) return "F" + std::to_string(13 + (k - SDLK_F13));
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return "Numpad" + std::to_string(1 + (k - SDLK_KP_1));
    switch (k) {
    case SDLK_RETURN: return "Return";
    case SDLK_ESCAPE: return "Escape";
    case SDLK_BACKSPACE: return "Backspace";
    case SDLK_TAB: return "Tab";
    case SDLK_SPACE: return "Space";
    case SDLK_LSHIFT: case SDLK_RSHIFT: return "Shift";
    case SDLK_LCTRL: case SDLK_RCTRL: return "Control";
    case SDLK_LALT: case SDLK_RALT: return "Alt";
    case SDLK_LGUI: case SDLK_RGUI: return "Meta";
    case SDLK_APPLICATION: return "Menu";
    case SDLK_MINUS: return "Minus";
    case SDLK_EQUALS: return "Equal";
    case SDLK_LEFTBRACKET: return "BracketLeft";
    case SDLK_RIGHTBRACKET: return "BracketRight";
    case SDLK_BACKSLASH: return "Backslash";
    case SDLK_SEMICOLON: return "Semicolon";
    case SDLK_QUOTE: return "Apostrophe";
    case SDLK_BACKQUOTE: return "QuoteLeft";
    case SDLK_COMMA: return "Comma";
    case SDLK_PERIOD: return "Period";
    case SDLK_SLASH: return "Slash";
    case SDLK_LESS: return "Less";
    case SDLK_PLUS: return "Plus";
    case SDLK_ASTERISK: return "Asterisk";
    case SDLK_KP_0: return "Numpad0";
    case SDLK_KP_PERIOD: return "NumpadPeriod";
    case SDLK_KP_PLUS: return "NumpadPlus";
    case SDLK_KP_MINUS: return "NumpadMinus";
    case SDLK_KP_MULTIPLY: return "NumpadAsterisk";
    case SDLK_KP_DIVIDE: return "NumpadSlash";
    case SDLK_KP_ENTER: return "NumpadEnter";
    case SDLK_INSERT: return "Insert";
    case SDLK_DELETE: return "Delete";
    case SDLK_HOME: return "Home";
    case SDLK_END: return "End";
    case SDLK_PAGEUP: return "PageUp";
    case SDLK_PAGEDOWN: return "PageDown";
    case SDLK_LEFT: return "Left";
    case SDLK_UP: return "Up";
    case SDLK_RIGHT: return "Right";
    case SDLK_DOWN: return "Down";
    case SDLK_CAPSLOCK: return "CapsLock";
    case SDLK_NUMLOCKCLEAR: return "NumLock";
    case SDLK_SCROLLLOCK: return "ScrollLock";
    case SDLK_PRINTSCREEN: return "Print";
    case SDLK_PAUSE: return "Pause";
    default: break;
    }
    // Latin-1 letters and signs of other layouts, by Qt's names for them.
    static constexpr std::pair<SDL_Keycode, const char*> kLatin1[] = {
        {0xE7, "Ccedilla"}, {0xBA, "masculine"}, {0xAA, "ordfeminine"}, {0xAB, "guillemotleft"},
        {0xBB, "guillemotright"}, {0xB4, "acute"}, {0xA8, "diaeresis"}, {0xE4, "Adiaeresis"},
        {0xF6, "Odiaeresis"}, {0xFC, "Udiaeresis"}, {0xDF, "ssharp"}, {0xE0, "Agrave"}, {0xE8, "Egrave"},
        {0xE9, "Eacute"}, {0xF1, "Ntilde"}, {0xF2, "Ograve"}, {0xF9, "Ugrave"}, {0xEC, "Igrave"},
        {0xA7, "section"}, {0xB0, "degree"}, {0x7E, "AsciiTilde"}, {0x5E, "AsciiCircum"},
    };
    for (const auto& [code, name] : kLatin1)
        if (code == k) return name;
    return {};
}

} // namespace ps2::input::sdl

#endif
