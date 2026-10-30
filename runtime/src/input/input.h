// Controller input, modelled on PCSX2's (docs/input.md).
//
// Each DualShock 2 input is bound to host inputs named by strings:
// "Keyboard/K", "SDL-0/FaceSouth", "SDL-0/+LeftX" (one direction of an
// axis), "SDL-0/LargeMotor" for vibration. An input can have several
// bindings, and a binding can be a chord ("Keyboard/Alt & Keyboard/Return").
// The bindings and their tuning are the [Pad1] and [Pad2] sections of the
// settings file and the hotkeys its [Hotkeys] section, in PCSX2's format, so
// a PCSX2 user's sections carry over. Bindings this runtime cannot read
// (XInput, DInput, mouse, raw joysticks) are kept as written.
//
// This part knows nothing about SDL: the window code reports what the host
// holds (HostState) and gets back what each port shows the game.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ps2::settings {
class Ini;
}

namespace ps2::input {

// ---- Host inputs ------------------------------------------------------------------------

// A game controller's buttons and axes, in SDL 2's GameController order.
// The face buttons are named by position, as PCSX2 names them
// ("FaceSouth"; its older names "A", "B", "X", "Y" are read too).
enum class PadButton : uint8_t {
    FaceSouth, FaceEast, FaceWest, FaceNorth, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
    DPadUp, DPadDown, DPadLeft, DPadRight, Misc1, Paddle1, Paddle2, Paddle3, Paddle4, Touchpad, Count
};
enum class PadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };
const char* padButtonName(PadButton b);
const char* padAxisName(PadAxis a);

// One host input: a key, a controller button, one direction (or the whole
// range) of a controller axis, or a controller's motor (an output, for
// vibration bindings).
struct HostKey {
    enum class Kind : uint8_t { Key, Button, Axis, Motor, Other };
    enum class Range : uint8_t { Positive, Negative, Full };
    Kind kind = Kind::Key;
    int device = 0;                 // SDL-<device>
    std::string name;               // Key: the key's name ("K", "Return", "Alt"); Other: the text as written
    int index = 0;                  // Button: PadButton; Axis: PadAxis; Motor: 0 large, 1 small
    Range range = Range::Positive;  // Axis: '+', '-' or "Full" (-1..1 read as 0..1)
    bool invert = false;            // Axis: '~' suffix, read from the other end
    bool operator==(const HostKey&) const = default;
};

// One binding: a single host input, or a chord of up to four held together.
struct Binding {
    std::vector<HostKey> keys;
    bool operator==(const Binding&) const = default;
};

std::optional<Binding> parseBinding(std::string_view text);   // "SDL-0/+LeftX", "Keyboard/Alt & Keyboard/Return"
std::string formatBinding(const Binding& b);
std::string displayBinding(const Binding& b);                 // shorter, for buttons: "SDL-0 FaceSouth", "Alt + Return"

// What the host holds this moment.
struct HostState {
    struct Controller {
        int device = 0;   // SDL-<device>
        std::array<bool, size_t(PadButton::Count)> buttons{};
        std::array<float, size_t(PadAxis::Count)> axes{};   // sticks -1..1 (down and right positive), triggers 0..1
    };
    std::vector<std::string> keys;   // names of the keys held
    std::vector<Controller> controllers;
};

// How far one host input, or a binding, is pressed: 0..1. A chord is as
// pressed as its least pressed part.
float keyValue(const HostKey& k, const HostState& s);
float bindingValue(const Binding& b, const HostState& s);

// ---- The DualShock 2 ---------------------------------------------------------------------

// Its bindable inputs, in PCSX2's order; ds2Key is the settings key ("Cross").
enum class Ds2 : uint8_t {
    Up, Right, Down, Left, Triangle, Circle, Cross, Square, Select, Start, L1, L2, R1, R2, L3, R3,
    Analog, Pressure, LUp, LRight, LDown, LLeft, RUp, RRight, RDown, RLeft, Count
};
enum class Motor : uint8_t { Large, Small, Count };
const char* ds2Key(Ds2 i);        // "Cross"
const char* ds2Label(Ds2 i);      // "Cross", "Left Stick Up", "Apply Pressure"
const char* motorKey(Motor m);    // "LargeMotor"
const char* motorLabel(Motor m);  // "Large (Low Frequency) Motor"

// The per-port settings: PCSX2's keys, defaults and ranges.
struct PadTuning {
    int invertL = 0, invertR = 0;            // InvertL/InvertR: 1 left/right, 2 up/down, 3 both
    float deadzone = 0.0f;                   // Deadzone: radial stick deadzone, 0..1
    float axisScale = 1.33f;                 // AxisScale: stick sensitivity, 0.01..2
    bool diagonalCorrection = false;         // UseDiagonalScaleCorrection: a round stick reaches the corners
    float largeMotorScale = 1.0f;            // LargeMotorScale, 0..2
    float smallMotorScale = 1.0f;            // SmallMotorScale, 0..2
    float buttonDeadzone = 0.0f;             // ButtonDeadzone: buttons and triggers read as released below it
    float pressureModifier = 0.5f;           // PressureModifier: pressure while Apply Pressure is held, 0.01..1
};

struct PadConfig {
    bool connected = true;   // Type = DualShock2; "None" is no controller
    std::array<std::vector<Binding>, size_t(Ds2::Count)> bindings;
    // Each input's own sensitivity and deadzone (<Key>Scale, <Key>Deadzone),
    // applied to each of its bindings before the pad's settings.
    std::array<float, size_t(Ds2::Count)> scale;
    std::array<float, size_t(Ds2::Count)> deadzone{};
    std::array<std::vector<Binding>, size_t(Motor::Count)> motors;
    PadTuning tuning;
    PadConfig() { scale.fill(1.0f); }
};

// What one port shows the game this frame.
struct PadState {
    bool connected = false;
    uint16_t buttons = 0;                   // libpad order, pressed = 1
    uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    // Pressure, libpad order: right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2.
    std::array<uint8_t, 12> pressure{};
    bool analogHeld = false;                // the Analog button (toggles analog mode when the game allows)
};

PadState evaluate(const PadConfig& cfg, const HostState& s);

// The defaults: PCSX2's keyboard layout on port 1, no controller on port 2.
PadConfig defaultPad(int port);
// Automatic Mapping, as PCSX2's: every input a controller has a counterpart
// for gets exactly that one binding (by position: the bottom face button is
// Cross); Apply Pressure and the settings stay. mapKeyboard binds the
// keyboard layout the same way.
void mapController(PadConfig& cfg, int device);
void mapKeyboard(PadConfig& cfg);

// Reads and writes [Pad1]/[Pad2] (port 0/1). A port without a section gets the defaults.
PadConfig loadPad(const settings::Ini& ini, int port);
void savePad(settings::Ini& ini, int port, const PadConfig& cfg);

// ---- Hotkeys -----------------------------------------------------------------------------

// As in PCSX2, a hotkey acts when it is released, except HoldTurbo, which
// lasts while it is held.
enum class Hotkey : uint8_t {
    OpenPauseMenu, ToggleFullscreen, TogglePause, ToggleFrameLimit, ToggleTurbo, HoldTurbo, CycleInterlaceMode,
    Screenshot, ShutdownVM, Mute, IncreaseVolume, DecreaseVolume, Count
};
const char* hotkeyKey(Hotkey h);       // "ToggleFullscreen"
const char* hotkeyLabel(Hotkey h);     // "Toggle Fullscreen"
const char* hotkeyCategory(Hotkey h);  // "Navigation", "Speed", "System", "Graphics", "Audio"

struct HotkeyConfig {
    std::array<std::vector<Binding>, size_t(Hotkey::Count)> bindings;
};
HotkeyConfig defaultHotkeys();
HotkeyConfig loadHotkeys(const settings::Ini& ini);
void saveHotkeys(settings::Ini& ini, const HotkeyConfig& cfg);

// ---- The running game (input/manager.cpp) -------------------------------------------------

// Start-up, before the guest runs: reads the settings file (see settings.h).
void init(const std::filesystem::path& settingsFile);
const std::filesystem::path& settingsPath();
settings::Ini& settingsFile();   // main thread: the whole file, for the other sections

struct HotkeyEvent {
    Hotkey hotkey;
    bool pressed;   // false: released
};
// Main thread: what the host holds now. Recomputes both ports (nothing held
// when `toGame` is false: the settings overlay has the input) and returns
// the hotkeys pressed or released since the last call. Keys taken by a
// chord that is held (Alt in Alt+Return) do not reach the pads meanwhile.
std::vector<HotkeyEvent> update(const HostState& s, bool toGame, bool hotkeysOn);
// Main thread: the keys and buttons held now reach no binding until they
// are released (the Return that closed the menu does not press Start).
void holdBack(const HostState& s);
// Main thread, a controller connected. On a first run (no settings file
// yet) the first one is mapped automatically to port 1, beside the
// keyboard, as PCSX2's setup wizard offers, and the result is saved. True
// if it was.
bool adoptFirstController(int device);
// Guest thread (the pad service): what port `port` shows the game.
PadState padState(int port);
// Guest thread: the game's vibration request for `port`. A DualShock 2's
// small motor is on or off, its large one has a speed, 0..255.
void setVibration(int port, bool small, uint8_t large);

// Main thread: how hard each controller bound to a port's motors should
// rumble now, 0..1, after the ports' motor scales. Silent while paused.
struct Rumble {
    int device;
    float large, small;
};
std::vector<Rumble> rumble();

// Main thread: the configuration the settings overlay edits; save() writes
// the settings file.
PadConfig& pad(int port);
HotkeyConfig& hotkeys();
bool save();

} // namespace ps2::input
