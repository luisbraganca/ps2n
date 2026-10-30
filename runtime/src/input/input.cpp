#include "input/input.h"

#include "settings.h"

#include "ps2n/util.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ps2::input {

namespace {

constexpr const char* kPadButtonNames[] = {
    "FaceSouth", "FaceEast", "FaceWest", "FaceNorth", "Back", "Guide", "Start", "LeftStick", "RightStick",
    "LeftShoulder", "RightShoulder", "DPadUp", "DPadDown", "DPadLeft", "DPadRight", "Misc1",
    "Paddle1", "Paddle2", "Paddle3", "Paddle4", "Touchpad",
};
static_assert(std::size(kPadButtonNames) == size_t(PadButton::Count));
// PCSX2's names before it moved to SDL 3 (Xbox positions), still read.
constexpr const char* kOldFaceNames[] = {"A", "B", "X", "Y"};
constexpr const char* kPadAxisNames[] = {"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};
static_assert(std::size(kPadAxisNames) == size_t(PadAxis::Count));
constexpr const char* kMotorNames[] = {"LargeMotor", "SmallMotor"};

struct Ds2Info {
    const char* key;
    const char* label;
};
constexpr Ds2Info kDs2[] = {
    {"Up", "D-Pad Up"}, {"Right", "D-Pad Right"}, {"Down", "D-Pad Down"}, {"Left", "D-Pad Left"},
    {"Triangle", "Triangle"}, {"Circle", "Circle"}, {"Cross", "Cross"}, {"Square", "Square"},
    {"Select", "Select"}, {"Start", "Start"}, {"L1", "L1 (Left Bumper)"}, {"L2", "L2 (Left Trigger)"},
    {"R1", "R1 (Right Bumper)"}, {"R2", "R2 (Right Trigger)"}, {"L3", "L3 (Left Stick Button)"},
    {"R3", "R3 (Right Stick Button)"}, {"Analog", "Analog Toggle"}, {"Pressure", "Apply Pressure"},
    {"LUp", "Left Stick Up"}, {"LRight", "Left Stick Right"}, {"LDown", "Left Stick Down"}, {"LLeft", "Left Stick Left"},
    {"RUp", "Right Stick Up"}, {"RRight", "Right Stick Right"}, {"RDown", "Right Stick Down"}, {"RLeft", "Right Stick Left"},
};
static_assert(std::size(kDs2) == size_t(Ds2::Count));
constexpr const char* kMotorLabels[] = {"Large (Low Frequency) Motor", "Small (High Frequency) Motor"};

struct HotkeyInfo {
    const char* key;
    const char* label;
    const char* category;
};
constexpr HotkeyInfo kHotkeys[] = {
    {"OpenPauseMenu", "Open Pause Menu", "Navigation"},
    {"ToggleFullscreen", "Toggle Fullscreen", "Navigation"},
    {"TogglePause", "Toggle Pause", "Speed"},
    {"ToggleFrameLimit", "Toggle Frame Limit", "Speed"},
    {"ToggleTurbo", "Toggle Turbo / Fast Forward", "Speed"},
    {"HoldTurbo", "Turbo / Fast Forward (Hold)", "Speed"},
    {"CycleInterlaceMode", "Cycle Deinterlace Mode", "Graphics"},
    {"Screenshot", "Save Screenshot", "Graphics"},
    {"ShutdownVM", "Quit the Game", "System"},
    {"Mute", "Toggle Mute", "Audio"},
    {"IncreaseVolume", "Increase Volume", "Audio"},
    {"DecreaseVolume", "Decrease Volume", "Audio"},
};
static_assert(std::size(kHotkeys) == size_t(Hotkey::Count));

bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

template <size_t N>
int indexOf(const char* const (&names)[N], std::string_view name) {
    for (size_t i = 0; i < N; ++i)
        if (sameText(names[i], name)) return static_cast<int>(i);
    return -1;
}

// "SDL-<n>/<input>": an input of a game controller, or Other when it is
// something this runtime does not read (a raw joystick's "JoyButton3").
HostKey parseSdlInput(int device, std::string_view input, std::string_view whole) {
    HostKey k;
    k.device = device;
    if (const int m = indexOf(kMotorNames, input); m >= 0) {
        k.kind = HostKey::Kind::Motor;
        k.index = m;
        return k;
    }
    if (const int b = indexOf(kPadButtonNames, input); b >= 0) {
        k.kind = HostKey::Kind::Button;
        k.index = b;
        return k;
    }
    if (const int b = indexOf(kOldFaceNames, input); b >= 0) {
        k.kind = HostKey::Kind::Button;
        k.index = b;
        return k;
    }
    std::string_view axis = input;
    if (ps2n::startsWith(axis, "Full")) {
        k.range = HostKey::Range::Full;
        axis.remove_prefix(4);
    } else if (!axis.empty() && (axis[0] == '+' || axis[0] == '-')) {
        k.range = axis[0] == '-' ? HostKey::Range::Negative : HostKey::Range::Positive;
        axis.remove_prefix(1);
    } else {
        axis = {};
    }
    if (!axis.empty() && axis.back() == '~') {
        k.invert = true;
        axis.remove_suffix(1);
    }
    if (const int a = indexOf(kPadAxisNames, axis); a >= 0) {
        k.kind = HostKey::Kind::Axis;
        k.index = a;
        return k;
    }
    HostKey other;
    other.kind = HostKey::Kind::Other;
    other.name = std::string(whole);
    return other;
}

std::optional<HostKey> parseKey(std::string_view text) {
    text = ps2n::trim(text);
    const size_t slash = text.find('/');
    if (slash == 0 || slash == std::string_view::npos || slash + 1 >= text.size()) return std::nullopt;
    const std::string_view source = text.substr(0, slash), input = text.substr(slash + 1);
    if (sameText(source, "Keyboard")) {
        HostKey k;
        k.kind = HostKey::Kind::Key;
        k.name = std::string(input);
        return k;
    }
    if (source.size() > 4 && sameText(source.substr(0, 4), "SDL-")) {
        const auto device = ps2n::parseInt(source.substr(4));
        if (device && *device >= 0 && *device < 256) return parseSdlInput(static_cast<int>(*device), input, text);
    }
    HostKey other;   // XInput-0/..., DInput-0/..., Pointer-0/...: kept as written
    other.kind = HostKey::Kind::Other;
    other.name = std::string(text);
    return other;
}

std::string axisText(const HostKey& k) {
    const char* range = k.range == HostKey::Range::Full ? "Full" : k.range == HostKey::Range::Negative ? "-" : "+";
    return std::string(range) + kPadAxisNames[k.index] + (k.invert ? "~" : "");
}

std::string formatKey(const HostKey& k) {
    switch (k.kind) {
    case HostKey::Kind::Key: return "Keyboard/" + k.name;
    case HostKey::Kind::Button: return ps2n::strprintf("SDL-%d/%s", k.device, kPadButtonNames[k.index]);
    case HostKey::Kind::Axis: return ps2n::strprintf("SDL-%d/", k.device) + axisText(k);
    case HostKey::Kind::Motor: return ps2n::strprintf("SDL-%d/%s", k.device, kMotorNames[k.index]);
    case HostKey::Kind::Other: return k.name;
    }
    return {};
}

std::string displayKey(const HostKey& k) {
    // SDL 2 names a controller's face buttons by the Xbox letters.
    static constexpr const char* kFace[] = {"A", "B", "X", "Y"};
    switch (k.kind) {
    case HostKey::Kind::Key: return k.name;
    case HostKey::Kind::Button:
        return ps2n::strprintf("SDL-%d %s", k.device, k.index < 4 ? kFace[k.index] : kPadButtonNames[k.index]);
    case HostKey::Kind::Axis: return ps2n::strprintf("SDL-%d ", k.device) + axisText(k);
    case HostKey::Kind::Motor: return ps2n::strprintf("SDL-%d %s", k.device, kMotorNames[k.index]);
    case HostKey::Kind::Other: return k.name;
    }
    return {};
}

uint8_t toByte(float v) { return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L)); }

// A stick axis, -1..1, as the pad reports it: 0..255 with 0x80 the centre.
uint8_t axisByte(float v) { return static_cast<uint8_t>(std::clamp(std::lround(127.5f + v * 127.5f), 0L, 255L)); }

Binding single(const std::string& text) { return *parseBinding(text); }

} // namespace

const char* padButtonName(PadButton b) { return kPadButtonNames[size_t(b)]; }
const char* padAxisName(PadAxis a) { return kPadAxisNames[size_t(a)]; }

std::optional<Binding> parseBinding(std::string_view text) {
    Binding b;
    for (const std::string& part : ps2n::split(text, '&')) {
        auto k = parseKey(part);
        if (!k) return std::nullopt;
        b.keys.push_back(std::move(*k));
    }
    if (b.keys.empty() || b.keys.size() > 4) return std::nullopt;
    return b;
}

std::string formatBinding(const Binding& b) {
    std::string out;
    for (const HostKey& k : b.keys) out += (out.empty() ? "" : " & ") + formatKey(k);
    return out;
}

std::string displayBinding(const Binding& b) {
    std::string out;
    for (const HostKey& k : b.keys) out += (out.empty() ? "" : " + ") + displayKey(k);
    return out;
}

float keyValue(const HostKey& k, const HostState& s) {
    if (k.kind == HostKey::Kind::Key) {
        for (const std::string& held : s.keys)
            if (sameText(held, k.name)) return 1.0f;
        return 0.0f;
    }
    if (k.kind != HostKey::Kind::Button && k.kind != HostKey::Kind::Axis) return 0.0f;
    for (const HostState::Controller& c : s.controllers) {
        if (c.device != k.device) continue;
        if (k.kind == HostKey::Kind::Button) return c.buttons[size_t(k.index)] ? 1.0f : 0.0f;
        const float a = c.axes[size_t(k.index)];
        float v = k.range == HostKey::Range::Full ? a * 0.5f + 0.5f : k.range == HostKey::Range::Negative ? -a : a;
        v = std::clamp(v, 0.0f, 1.0f);
        return k.invert ? 1.0f - v : v;
    }
    return 0.0f;
}

float bindingValue(const Binding& b, const HostState& s) {
    if (b.keys.empty()) return 0.0f;
    float v = 1.0f;
    for (const HostKey& k : b.keys) v = std::min(v, keyValue(k, s));
    return v;
}

const char* ds2Key(Ds2 i) { return kDs2[size_t(i)].key; }
const char* ds2Label(Ds2 i) { return kDs2[size_t(i)].label; }
const char* motorKey(Motor m) { return kMotorNames[size_t(m)]; }
const char* motorLabel(Motor m) { return kMotorLabels[size_t(m)]; }

PadState evaluate(const PadConfig& cfg, const HostState& s) {
    PadState out;
    out.connected = cfg.connected;
    if (!cfg.connected) return out;
    // Each input: its strongest binding, after the input's own sensitivity and deadzone.
    std::array<float, size_t(Ds2::Count)> v{};
    for (size_t i = 0; i < v.size(); ++i) {
        for (const Binding& b : cfg.bindings[i]) {
            float x = std::clamp(bindingValue(b, s) * cfg.scale[i], 0.0f, 1.0f);
            if (x < cfg.deadzone[i]) x = 0.0f;
            v[i] = std::max(v[i], x);
        }
    }
    const PadTuning& t = cfg.tuning;

    // Buttons press at any value past the button deadzone. Their pressure
    // follows the value, scaled down while Apply Pressure is held, except
    // L2/R2's (usually analog triggers already).
    struct Btn {
        Ds2 in;
        uint16_t bit;
        int pressure;
    };
    static constexpr Btn kButtons[] = {
        {Ds2::Up, 0x0010, 2}, {Ds2::Right, 0x0020, 0}, {Ds2::Down, 0x0040, 3}, {Ds2::Left, 0x0080, 1},
        {Ds2::Triangle, 0x1000, 4}, {Ds2::Circle, 0x2000, 5}, {Ds2::Cross, 0x4000, 6}, {Ds2::Square, 0x8000, 7},
        {Ds2::Select, 0x0001, -1}, {Ds2::Start, 0x0008, -1}, {Ds2::L1, 0x0400, 8}, {Ds2::L2, 0x0100, 10},
        {Ds2::R1, 0x0800, 9}, {Ds2::R2, 0x0200, 11}, {Ds2::L3, 0x0002, -1}, {Ds2::R3, 0x0004, -1},
    };
    auto pressed = [&](Ds2 in) { return v[size_t(in)] >= t.buttonDeadzone ? v[size_t(in)] : 0.0f; };
    const bool modifier = pressed(Ds2::Pressure) > 0.0f;
    for (const Btn& b : kButtons) {
        const float x = pressed(b.in);
        if (x > 0.0f) out.buttons |= b.bit;
        if (b.pressure >= 0) {
            const bool trigger = b.in == Ds2::L2 || b.in == Ds2::R2;
            out.pressure[size_t(b.pressure)] = toByte(x * (modifier && !trigger ? t.pressureModifier : 1.0f));
        }
    }

    // Sticks: each direction scaled by the sensitivity (a key, at 1.0, gives
    // full tilt), optionally stretched from a round gate to the corners, then
    // a radial deadzone in that space reads as centred.
    auto stick = [&](Ds2 right, Ds2 left, Ds2 down, Ds2 up, int invert, uint8_t& ox, uint8_t& oy) {
        auto half = [&](Ds2 in) { return std::min(v[size_t(in)] * t.axisScale, 1.0f); };
        float x = half(right) - half(left);
        float y = half(down) - half(up);
        if (invert & 1) x = -x;
        if (invert & 2) y = -y;
        if (t.diagonalCorrection) {
            const float m = std::max(std::fabs(x), std::fabs(y));
            if (m > 0.0f) {
                const float k = std::min(std::sqrt(x * x + y * y), 1.0f) / m;
                x *= k;
                y *= k;
            }
        }
        if (t.deadzone > 0.0f && x * x + y * y < t.deadzone * t.deadzone) x = y = 0.0f;
        ox = axisByte(x);
        oy = axisByte(y);
    };
    stick(Ds2::LRight, Ds2::LLeft, Ds2::LDown, Ds2::LUp, t.invertL, out.lx, out.ly);
    stick(Ds2::RRight, Ds2::RLeft, Ds2::RDown, Ds2::RUp, t.invertR, out.rx, out.ry);
    out.analogHeld = pressed(Ds2::Analog) > 0.0f;
    return out;
}

PadConfig defaultPad(int port) {
    PadConfig c;
    if (port == 0) mapKeyboard(c);
    else c.connected = false;
    return c;
}

void mapController(PadConfig& c, int device) {
    static constexpr std::pair<Ds2, const char*> kMap[] = {
        {Ds2::Up, "DPadUp"}, {Ds2::Right, "DPadRight"}, {Ds2::Down, "DPadDown"}, {Ds2::Left, "DPadLeft"},
        {Ds2::Triangle, "FaceNorth"}, {Ds2::Circle, "FaceEast"}, {Ds2::Cross, "FaceSouth"}, {Ds2::Square, "FaceWest"},
        {Ds2::Select, "Back"}, {Ds2::Start, "Start"}, {Ds2::L1, "LeftShoulder"}, {Ds2::L2, "+LeftTrigger"},
        {Ds2::R1, "RightShoulder"}, {Ds2::R2, "+RightTrigger"}, {Ds2::L3, "LeftStick"}, {Ds2::R3, "RightStick"},
        {Ds2::Analog, "Guide"},
        {Ds2::LUp, "-LeftY"}, {Ds2::LRight, "+LeftX"}, {Ds2::LDown, "+LeftY"}, {Ds2::LLeft, "-LeftX"},
        {Ds2::RUp, "-RightY"}, {Ds2::RRight, "+RightX"}, {Ds2::RDown, "+RightY"}, {Ds2::RLeft, "-RightX"},
    };
    const std::string prefix = ps2n::strprintf("SDL-%d/", device);
    for (const auto& [in, name] : kMap) c.bindings[size_t(in)] = {single(prefix + name)};
    c.motors[size_t(Motor::Large)] = {single(prefix + "LargeMotor")};
    c.motors[size_t(Motor::Small)] = {single(prefix + "SmallMotor")};
}

void mapKeyboard(PadConfig& c) {
    static constexpr std::pair<Ds2, const char*> kKeys[] = {
        {Ds2::Up, "Up"}, {Ds2::Right, "Right"}, {Ds2::Down, "Down"}, {Ds2::Left, "Left"},
        {Ds2::Triangle, "I"}, {Ds2::Circle, "L"}, {Ds2::Cross, "K"}, {Ds2::Square, "J"},
        {Ds2::Select, "Backspace"}, {Ds2::Start, "Return"}, {Ds2::L1, "Q"}, {Ds2::L2, "1"},
        {Ds2::R1, "E"}, {Ds2::R2, "3"}, {Ds2::L3, "2"}, {Ds2::R3, "4"},
        {Ds2::LUp, "W"}, {Ds2::LRight, "D"}, {Ds2::LDown, "S"}, {Ds2::LLeft, "A"},
        {Ds2::RUp, "T"}, {Ds2::RRight, "H"}, {Ds2::RDown, "G"}, {Ds2::RLeft, "F"},
    };
    for (const auto& [in, key] : kKeys) c.bindings[size_t(in)] = {single(std::string("Keyboard/") + key)};
    // The keyboard has no Analog button or motors: those bindings go, as in PCSX2.
    c.bindings[size_t(Ds2::Analog)].clear();
    for (auto& m : c.motors) m.clear();
}

namespace {

std::string padSection(int port) { return ps2n::strprintf("Pad%d", port + 1); }

std::vector<Binding> readBindings(const settings::Ini& ini, const std::string& section, const std::string& key) {
    std::vector<Binding> out;
    for (const std::string& text : ini.values(section, key))
        if (auto b = parseBinding(text)) out.push_back(std::move(*b));
    return out;
}

void writeBindings(settings::Ini& ini, const std::string& section, const std::string& key, const std::vector<Binding>& bindings) {
    std::vector<std::string> texts;
    for (const Binding& b : bindings) texts.push_back(formatBinding(b));
    ini.setValues(section, key, texts);
}

void writeFloatUnless(settings::Ini& ini, const std::string& section, const std::string& key, float v, float fallback) {
    if (v == fallback) ini.setValues(section, key, {});
    else ini.setFloat(section, key, v);
}

} // namespace

PadConfig loadPad(const settings::Ini& ini, int port) {
    const std::string section = padSection(port);
    if (!ini.hasSection(section)) return defaultPad(port);
    PadConfig c;
    c.connected = !sameText(ini.getString(section, "Type", port == 0 ? "DualShock2" : "None"), "None");
    for (size_t i = 0; i < size_t(Ds2::Count); ++i) {
        const std::string key = kDs2[i].key;
        c.bindings[i] = readBindings(ini, section, key);
        c.scale[i] = std::clamp(ini.getFloat(section, key + "Scale", 1.0f), 0.01f, 2.0f);
        c.deadzone[i] = std::clamp(ini.getFloat(section, key + "Deadzone", 0.0f), 0.0f, 1.0f);
    }
    for (size_t m = 0; m < size_t(Motor::Count); ++m) c.motors[m] = readBindings(ini, section, kMotorNames[m]);
    PadTuning& t = c.tuning;
    const PadTuning d;
    t.invertL = std::clamp(static_cast<int>(ini.getFloat(section, "InvertL", 0.0f)), 0, 3);
    t.invertR = std::clamp(static_cast<int>(ini.getFloat(section, "InvertR", 0.0f)), 0, 3);
    t.deadzone = std::clamp(ini.getFloat(section, "Deadzone", d.deadzone), 0.0f, 1.0f);
    t.axisScale = std::clamp(ini.getFloat(section, "AxisScale", d.axisScale), 0.01f, 2.0f);
    t.diagonalCorrection = ini.getBool(section, "UseDiagonalScaleCorrection", d.diagonalCorrection);
    t.largeMotorScale = std::clamp(ini.getFloat(section, "LargeMotorScale", d.largeMotorScale), 0.0f, 2.0f);
    t.smallMotorScale = std::clamp(ini.getFloat(section, "SmallMotorScale", d.smallMotorScale), 0.0f, 2.0f);
    t.buttonDeadzone = std::clamp(ini.getFloat(section, "ButtonDeadzone", d.buttonDeadzone), 0.0f, 1.0f);
    t.pressureModifier = std::clamp(ini.getFloat(section, "PressureModifier", d.pressureModifier), 0.01f, 1.0f);
    return c;
}

void savePad(settings::Ini& ini, int port, const PadConfig& c) {
    const std::string section = padSection(port);
    ini.set(section, "Type", c.connected ? "DualShock2" : "None");
    const PadTuning& t = c.tuning;
    ini.set(section, "InvertL", std::to_string(t.invertL));
    ini.set(section, "InvertR", std::to_string(t.invertR));
    ini.setFloat(section, "Deadzone", t.deadzone);
    ini.setFloat(section, "AxisScale", t.axisScale);
    ini.setBool(section, "UseDiagonalScaleCorrection", t.diagonalCorrection);
    ini.setFloat(section, "LargeMotorScale", t.largeMotorScale);
    ini.setFloat(section, "SmallMotorScale", t.smallMotorScale);
    ini.setFloat(section, "ButtonDeadzone", t.buttonDeadzone);
    ini.setFloat(section, "PressureModifier", t.pressureModifier);
    for (size_t i = 0; i < size_t(Ds2::Count); ++i) {
        const std::string key = kDs2[i].key;
        writeBindings(ini, section, key, c.bindings[i]);
        writeFloatUnless(ini, section, key + "Scale", c.scale[i], 1.0f);
        writeFloatUnless(ini, section, key + "Deadzone", c.deadzone[i], 0.0f);
    }
    for (size_t m = 0; m < size_t(Motor::Count); ++m) writeBindings(ini, section, kMotorNames[m], c.motors[m]);
}

const char* hotkeyKey(Hotkey h) { return kHotkeys[size_t(h)].key; }
const char* hotkeyLabel(Hotkey h) { return kHotkeys[size_t(h)].label; }
const char* hotkeyCategory(Hotkey h) { return kHotkeys[size_t(h)].category; }

HotkeyConfig defaultHotkeys() {
    HotkeyConfig c;
    auto add = [&](Hotkey h, const char* text) { c.bindings[size_t(h)].push_back(single(text)); };
    add(Hotkey::OpenPauseMenu, "Keyboard/Escape");
    add(Hotkey::ToggleFullscreen, "Keyboard/Alt & Keyboard/Return");
    add(Hotkey::ToggleFullscreen, "Keyboard/F11");   // not one of PCSX2's
    add(Hotkey::TogglePause, "Keyboard/Space");
    add(Hotkey::ToggleFrameLimit, "Keyboard/F4");
    add(Hotkey::ToggleTurbo, "Keyboard/Tab");
    add(Hotkey::HoldTurbo, "Keyboard/Period");
    add(Hotkey::CycleInterlaceMode, "Keyboard/F5");
    add(Hotkey::Screenshot, "Keyboard/F8");
    return c;
}

HotkeyConfig loadHotkeys(const settings::Ini& ini) {
    if (!ini.hasSection("Hotkeys")) return defaultHotkeys();
    HotkeyConfig c;
    for (size_t h = 0; h < size_t(Hotkey::Count); ++h) c.bindings[h] = readBindings(ini, "Hotkeys", kHotkeys[h].key);
    return c;
}

void saveHotkeys(settings::Ini& ini, const HotkeyConfig& c) {
    for (size_t h = 0; h < size_t(Hotkey::Count); ++h) writeBindings(ini, "Hotkeys", kHotkeys[h].key, c.bindings[h]);
}

} // namespace ps2::input
