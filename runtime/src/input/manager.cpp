// The running game's input: the configuration loaded from the settings
// file, what each port shows the game, hotkeys and vibration.
//
// The main thread owns the configuration and turns host input into port
// states; the guest thread (the pad service) reads those and reports the
// game's vibration requests. One mutex covers what both threads touch.
#include "input/input.h"

#include "log.h"
#include "settings.h"

#include <algorithm>
#include <mutex>

namespace ps2::input {

namespace {

std::filesystem::path g_path;
settings::Ini g_ini;
bool g_firstRun = false;   // no settings file yet, and no controller adopted
std::array<PadConfig, 2> g_pads = {defaultPad(0), defaultPad(1)};
HotkeyConfig g_hotkeys = defaultHotkeys();
std::array<bool, size_t(Hotkey::Count)> g_hotkeyHeld{};
// Keys a held chord took (Alt and Return of Alt+Return): they reach no other
// binding until they are released.
std::vector<HostKey> g_consumed;

struct Vibration {
    bool small = false;
    uint8_t large = 0;
};

std::mutex g_mutex;
std::array<PadState, 2> g_states = {evaluate(g_pads[0], {}), evaluate(g_pads[1], {})};
std::array<Vibration, 2> g_vibration;

// `s` with `k` released.
void release(HostState& s, const HostKey& k) {
    if (k.kind == HostKey::Kind::Key) {
        std::erase_if(s.keys, [&](const std::string& held) { return keyValue(k, HostState{{held}, {}}) > 0.0f; });
        return;
    }
    for (HostState::Controller& c : s.controllers) {
        if (c.device != k.device) continue;
        if (k.kind == HostKey::Kind::Button) c.buttons[size_t(k.index)] = false;
        if (k.kind == HostKey::Kind::Axis) c.axes[size_t(k.index)] = 0.0f;
    }
}

bool held(const Binding& b, const HostState& s) { return bindingValue(b, s) > 0.0f; }

} // namespace

void init(const std::filesystem::path& file) {
    g_path = file;
    const bool found = g_ini.load(file);
    for (int p = 0; p < 2; ++p) g_pads[size_t(p)] = loadPad(g_ini, p);
    g_hotkeys = loadHotkeys(g_ini);
    g_hotkeyHeld = {};
    g_consumed.clear();
    g_firstRun = !found;
    if (found) PS2_INFO("input", "settings from %s", file.string().c_str());
    else PS2_INFO("input", "no settings file at %s yet: keyboard on port 1, as PCSX2 starts", file.string().c_str());
    const std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t p = 0; p < 2; ++p) g_states[p] = evaluate(g_pads[p], {});
    g_vibration = {};
}

const std::filesystem::path& settingsPath() { return g_path; }
settings::Ini& settingsFile() { return g_ini; }

std::vector<HotkeyEvent> update(const HostState& s, bool toGame, bool hotkeysOn) {
    // A chord, once held, takes its keys until each is released.
    std::erase_if(g_consumed, [&](const HostKey& k) { return keyValue(k, s) <= 0.0f; });
    for (const auto& bindings : g_hotkeys.bindings) {
        for (const Binding& b : bindings) {
            if (b.keys.size() < 2 || !held(b, s)) continue;
            for (const HostKey& k : b.keys)
                if (std::find(g_consumed.begin(), g_consumed.end(), k) == g_consumed.end()) g_consumed.push_back(k);
        }
    }
    HostState free = s;
    for (const HostKey& k : g_consumed) release(free, k);

    std::vector<HotkeyEvent> events;
    for (size_t h = 0; h < g_hotkeys.bindings.size(); ++h) {
        bool on = false;
        for (const Binding& b : g_hotkeys.bindings[h]) on = on || held(b, b.keys.size() > 1 ? s : free);
        if (!hotkeysOn) {
            g_hotkeyHeld[h] = false;   // forgotten silently: no release action
            continue;
        }
        if (on != g_hotkeyHeld[h]) {
            g_hotkeyHeld[h] = on;
            events.push_back({Hotkey(h), on});
        }
    }

    std::array<PadState, 2> states;
    for (size_t p = 0; p < 2; ++p) states[p] = evaluate(g_pads[p], toGame ? free : HostState{});
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_states = states;
    return events;
}

void holdBack(const HostState& s) {
    auto take = [](const HostKey& k) {
        if (std::find(g_consumed.begin(), g_consumed.end(), k) == g_consumed.end()) g_consumed.push_back(k);
    };
    for (const std::string& name : s.keys) {
        HostKey k;
        k.kind = HostKey::Kind::Key;
        k.name = name;
        take(k);
    }
    for (const HostState::Controller& c : s.controllers) {
        for (size_t b = 0; b < c.buttons.size(); ++b) {
            if (!c.buttons[b]) continue;
            HostKey k;
            k.kind = HostKey::Kind::Button;
            k.device = c.device;
            k.index = static_cast<int>(b);
            take(k);
        }
    }
}

bool adoptFirstController(int device) {
    if (!g_firstRun) return false;
    g_firstRun = false;
    PadConfig mapped;
    mapController(mapped, device);
    PadConfig& pad = g_pads[0];
    for (size_t i = 0; i < pad.bindings.size(); ++i)
        for (const Binding& b : mapped.bindings[i])
            if (std::find(pad.bindings[i].begin(), pad.bindings[i].end(), b) == pad.bindings[i].end()) pad.bindings[i].push_back(b);
    pad.motors = mapped.motors;
    save();
    PS2_INFO("input", "first run: controller SDL-%d mapped to port 1, beside the keyboard", device);
    return true;
}

PadState padState(int port) {
    if (port < 0 || port > 1) return {};
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_states[size_t(port)];
}

void setVibration(int port, bool small, uint8_t large) {
    if (port < 0 || port > 1) return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_vibration[size_t(port)] = {small, large};
}

std::vector<Rumble> rumble() {
    std::array<Vibration, 2> vib;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        vib = g_vibration;
    }
    std::vector<Rumble> out;
    for (size_t p = 0; p < 2; ++p) {
        const PadConfig& cfg = g_pads[p];
        if (!cfg.connected) continue;
        const float speed[2] = {std::min(vib[p].large / 255.0f * cfg.tuning.largeMotorScale, 1.0f),
                                vib[p].small ? std::min(cfg.tuning.smallMotorScale, 1.0f) : 0.0f};
        for (size_t m = 0; m < size_t(Motor::Count); ++m) {
            for (const Binding& b : cfg.motors[m]) {
                for (const HostKey& k : b.keys) {
                    if (k.kind != HostKey::Kind::Motor) continue;
                    auto it = std::find_if(out.begin(), out.end(), [&](const Rumble& r) { return r.device == k.device; });
                    if (it == out.end()) it = out.insert(out.end(), Rumble{k.device, 0.0f, 0.0f});
                    // Both of the pad's motors on one host motor: the stronger wins.
                    float& target = k.index == 0 ? it->large : it->small;
                    target = std::max(target, speed[m]);
                }
            }
        }
    }
    return out;
}

PadConfig& pad(int port) { return g_pads[size_t(std::clamp(port, 0, 1))]; }
HotkeyConfig& hotkeys() { return g_hotkeys; }

bool save() {
    for (int p = 0; p < 2; ++p) savePad(g_ini, p, g_pads[size_t(p)]);
    saveHotkeys(g_ini, g_hotkeys);
    if (g_ini.save(g_path)) return true;
    PS2_WARN("input", "cannot write the settings file %s", g_path.string().c_str());
    return false;
}

} // namespace ps2::input
