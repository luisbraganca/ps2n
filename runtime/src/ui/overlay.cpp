#include "ui/overlay.h"

#ifdef PS2N_HAVE_SDL2

#include "input/sdl.h"
#include "internal.h"
#include "settings.h"

#include "ps2n/util.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <system_error>
#include <utility>

namespace ps2::ui {

namespace {

using input::Binding;
using input::Ds2;
using input::HostKey;
using input::Hotkey;
using input::Motor;
using input::PadConfig;

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
ImGuiStyle g_baseStyle;
float g_scale = 0.0f;

enum class Screen { None, PauseMenu, Settings };
enum class Page { Global, Port1, Port2, Hotkeys };
Screen g_screen = Screen::None;
Page g_page = Page::Port1;
bool g_settingsTab = false;      // a port page shows its Settings rather than its Bindings
bool g_focusMenu = false;        // the menu just opened: focus its first item
Action g_action = Action::None;
bool g_pausedIndicator = false;
bool g_escapeForPopup = false;   // that Escape closed a popup, not the menu
input::HostState g_host;         // what the host holds this frame

struct Message {
    std::string text;
    double until;
};
std::vector<Message> g_osd;

constexpr ImU32 kAccent = IM_COL32(66, 150, 250, 255);

// ---- Binding capture -----------------------------------------------------------------------
// As PCSX2's: press an input (several, for a chord) and the binding is made
// when one of them is released. Five seconds to start; a click cancels.

constexpr double kCaptureSeconds = 5.0;

struct Capture {
    bool active = false;
    bool hotkey = false;   // else a DualShock 2 input
    int port = 0;
    size_t index = 0;      // a Ds2 or a Hotkey
    bool append = false;   // add a binding rather than replace them all
    double started = 0.0;
    std::vector<HostKey> parts;
    std::vector<std::pair<int, int>> blocked;   // (device, axis) pushed already when the capture began
};
Capture g_capture;

std::vector<Binding>& bindingsOf(bool hotkey, int port, size_t index) {
    return hotkey ? input::hotkeys().bindings[index] : input::pad(port).bindings[index];
}

void startCapture(bool hotkey, int port, size_t index, bool append) {
    g_capture = Capture{};
    g_capture.active = true;
    g_capture.hotkey = hotkey;
    g_capture.port = port;
    g_capture.index = index;
    g_capture.append = append;
    g_capture.started = ImGui::GetTime();
    for (const auto& c : g_host.controllers)
        for (size_t a = 0; a < c.axes.size(); ++a)
            if (std::fabs(c.axes[a]) >= 0.5f) g_capture.blocked.emplace_back(c.device, static_cast<int>(a));
    // The controller binds now; it does not also steer the menu.
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
}

void endCapture() {
    g_capture.active = false;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
}

void commitCapture() {
    if (!g_capture.parts.empty()) {
        const Binding b{g_capture.parts};
        auto& list = bindingsOf(g_capture.hotkey, g_capture.port, g_capture.index);
        if (!g_capture.append) list = {b};
        else if (std::find(list.begin(), list.end(), b) == list.end()) list.push_back(b);
        input::save();
    }
    endCapture();
}

bool isPart(const HostKey& k) { return std::find(g_capture.parts.begin(), g_capture.parts.end(), k) != g_capture.parts.end(); }

void addPart(const HostKey& k) {
    if (g_capture.parts.size() < 4 && !isPart(k)) g_capture.parts.push_back(k);
}

HostKey keyboardKey(SDL_Keycode code) {
    HostKey k;
    k.kind = HostKey::Kind::Key;
    k.name = input::sdl::keyName(code);
    return k;
}

void captureEvent(const SDL_Event& ev) {
    switch (ev.type) {
    case SDL_KEYDOWN:
        if (!ev.key.repeat) {
            const HostKey k = keyboardKey(ev.key.keysym.sym);
            if (!k.name.empty()) addPart(k);
        }
        break;
    case SDL_KEYUP:
        if (isPart(keyboardKey(ev.key.keysym.sym))) commitCapture();
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        HostKey k;
        k.kind = HostKey::Kind::Button;
        k.device = input::sdl::deviceOf(ev.cbutton.which);
        k.index = ev.cbutton.button;
        if (k.device < 0 || k.index >= int(input::PadButton::Count)) break;
        if (ev.type == SDL_CONTROLLERBUTTONDOWN) addPart(k);
        else if (isPart(k)) commitCapture();
        break;
    }
    case SDL_CONTROLLERAXISMOTION: {
        const int device = input::sdl::deviceOf(ev.caxis.which), axis = ev.caxis.axis;
        if (device < 0 || axis >= int(input::PadAxis::Count)) break;
        const float v = ev.caxis.value < 0 ? ev.caxis.value / 32768.0f : ev.caxis.value / 32767.0f;
        // An axis already pushed when the capture began counts once it has come back.
        const auto blocked = std::find(g_capture.blocked.begin(), g_capture.blocked.end(), std::pair{device, axis});
        if (blocked != g_capture.blocked.end()) {
            if (std::fabs(v) < 0.5f) g_capture.blocked.erase(blocked);
            break;
        }
        const bool registered = std::any_of(g_capture.parts.begin(), g_capture.parts.end(), [&](const HostKey& p) {
            return p.kind == HostKey::Kind::Axis && p.device == device && p.index == axis;
        });
        if (registered && std::fabs(v) < 0.5f) {
            commitCapture();
        } else if (!registered && std::fabs(v) >= 0.5f) {
            HostKey k;
            k.kind = HostKey::Kind::Axis;
            k.device = device;
            k.index = axis;
            k.range = v < 0.0f ? HostKey::Range::Negative : HostKey::Range::Positive;
            addPart(k);
        }
        break;
    }
    case SDL_MOUSEBUTTONDOWN: endCapture(); break;
    default: break;
    }
}

int secondsLeft() { return std::max(0, static_cast<int>(std::ceil(kCaptureSeconds - (ImGui::GetTime() - g_capture.started)))); }

// ---- Popups the binding buttons open ----------------------------------------------------

struct EditRequest {
    bool open = false;   // OpenPopup at the settings window's level
    bool hotkey = false;
    int port = 0;
    size_t index = 0;
    int selected = -1;
};
EditRequest g_edit;

struct MotorRequest {
    bool open = false;
    int port = 0;
    Motor motor = Motor::Large;
};
MotorRequest g_motorPick;

// ---- Small pieces ----------------------------------------------------------------------------

std::string summary(const std::vector<Binding>& list) {
    if (list.empty()) return "";
    if (list.size() > 1) return ps2n::strprintf("%zu bindings", list.size());
    std::string s = input::displayBinding(list[0]);
    if (s.size() > 35) s = s.substr(0, 32) + "...";
    return s;
}

bool held(const std::vector<Binding>& list) {
    return std::any_of(list.begin(), list.end(), [](const Binding& b) { return input::bindingValue(b, g_host) > 0.0f; });
}

void bindingTooltip(const std::vector<Binding>& list) {
    if (!ImGui::BeginItemTooltip()) return;
    if (list.empty()) ImGui::TextDisabled("No binding");
    for (const Binding& b : list) ImGui::TextUnformatted(input::formatBinding(b).c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Left click to assign a new button\nShift + left click for additional bindings\nRight click to clear binding");
    ImGui::EndTooltip();
}

// One input's binding button. It lights while the input is held.
void bindingButton(const char* id, bool hotkey, int port, size_t index, float width) {
    auto& list = bindingsOf(hotkey, port, index);
    const bool mine = g_capture.active && !g_capture.append && g_capture.hotkey == hotkey && g_capture.port == port &&
                      g_capture.index == index;
    std::string text = mine ? ps2n::strprintf("Push Button/Axis... [%d]", secondsLeft()) : summary(list);
    if (mine && ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f > width)
        text = ps2n::strprintf("Push... [%d]", secondsLeft());   // a narrow button
    ImGui::PushID(id);
    const bool lit = !mine && held(list);
    if (lit) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    if (ImGui::Button((text + "###bind").c_str(), ImVec2(width, 0.0f)) && !g_capture.active) {
        if (ImGui::GetIO().KeyShift || list.size() > 1) {
            g_edit = EditRequest{true, hotkey, port, index, -1};
        } else {
            startCapture(hotkey, port, index, false);
        }
    }
    if (lit) ImGui::PopStyleColor();
    if (!g_capture.active && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        list.clear();
        input::save();
    }
    if (!g_capture.active) bindingTooltip(list);
    ImGui::PopID();
}

// An input with its caption above its button, `width` wide.
void bindingCell(const char* caption, int port, Ds2 in, float width) {
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", caption);
    bindingButton(input::ds2Key(in), false, port, size_t(in), width);
    ImGui::EndGroup();
}

void indent(float x) { ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x); }

struct Cell {
    const char* caption;
    Ds2 in;
    float width;
};
void bindingCell(const char* caption, int port, Ds2 in, float width);

// Inputs side by side, each where its width puts it, however long its caption.
void cellRow(int port, std::initializer_list<Cell> cells) {
    const float x0 = ImGui::GetCursorPosX(), sp = ImGui::GetStyle().ItemSpacing.x;
    float x = x0;
    for (const Cell& c : cells) {
        if (x != x0) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(x);
        }
        bindingCell(c.caption, port, c.in, c.width);
        x += c.width + sp;
    }
}

// Continues the line at its right edge, with room for `width`.
void alignRight(float width) {
    ImGui::SameLine();
    indent(std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
}

// A group box: a bordered child with its title on top.
bool beginBox(const char* title) {
    const bool open = ImGui::BeginChild(title, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted(title);
    return open;
}
void endBox() { ImGui::EndChild(); }

// Four inputs arranged like the d-pad they are: up, left and right, down.
void plusBox(const char* title, int port, const std::array<std::pair<const char*, Ds2>, 4>& in) {
    beginBox(title);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = (avail - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    indent((avail - w) / 2.0f);
    bindingCell(in[0].first, port, in[0].second, w);
    bindingCell(in[1].first, port, in[1].second, w);
    ImGui::SameLine();
    bindingCell(in[2].first, port, in[2].second, w);
    indent((avail - w) / 2.0f);
    bindingCell(in[3].first, port, in[3].second, w);
    endBox();
}

void motorCell(const char* title, int port, Motor m) {
    beginBox(title);
    const auto& list = input::pad(port).motors[size_t(m)];
    const std::string text = list.empty() ? "" : input::displayBinding(list[0]);
    ImGui::PushID(input::motorKey(m));
    if (ImGui::Button((text + "###motor").c_str(), ImVec2(-FLT_MIN, 0.0f)) && !g_capture.active) g_motorPick = {true, port, m};
    if (!g_capture.active && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        input::pad(port).motors[size_t(m)].clear();
        input::save();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(input::motorLabel(m));
        ImGui::TextDisabled("Left click to choose a controller motor\nRight click to clear");
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    endBox();
}

// The DualShock 2, drawn: it shows what the port reads from the host now.
void drawController(const PadConfig& cfg, float width) {
    const input::PadState st = input::evaluate(cfg, g_host);
    const float k = width / 400.0f, height = 250.0f * k;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto P = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
    const ImU32 body = IM_COL32(70, 72, 78, 255), part = IM_COL32(38, 40, 44, 255), lit = kAccent;
    auto on = [&](uint16_t bit) { return (st.buttons & bit) != 0; };
    auto col = [&](uint16_t bit) { return on(bit) ? lit : part; };

    dl->AddRectFilled(P(85, 6), P(150, 20), col(0x0100), 6.0f * k);     // L2
    dl->AddRectFilled(P(250, 6), P(315, 20), col(0x0200), 6.0f * k);    // R2
    dl->AddRectFilled(P(85, 24), P(150, 38), col(0x0400), 4.0f * k);    // L1
    dl->AddRectFilled(P(250, 24), P(315, 38), col(0x0800), 4.0f * k);   // R1
    dl->AddRectFilled(P(70, 40), P(330, 140), body, 45.0f * k);
    dl->AddCircleFilled(P(108, 150), 50.0f * k, body);
    dl->AddCircleFilled(P(292, 150), 50.0f * k, body);

    dl->AddRectFilled(P(107, 62), P(123, 80), col(0x0010), 2.0f * k);   // d-pad
    dl->AddRectFilled(P(107, 90), P(123, 108), col(0x0040), 2.0f * k);
    dl->AddRectFilled(P(88, 77), P(106, 93), col(0x0080), 2.0f * k);
    dl->AddRectFilled(P(124, 77), P(142, 93), col(0x0020), 2.0f * k);

    const float r = 10.5f * k, s = 5.5f * k, t = 1.6f * k;
    const ImU32 sym = IM_COL32(220, 220, 225, 255);
    ImVec2 c = P(285, 63);
    dl->AddCircleFilled(c, r, col(0x1000));
    dl->AddTriangle(ImVec2(c.x, c.y - s), ImVec2(c.x - s, c.y + s * 0.8f), ImVec2(c.x + s, c.y + s * 0.8f), sym, t);
    c = P(307, 85);
    dl->AddCircleFilled(c, r, col(0x2000));
    dl->AddCircle(c, s, sym, 0, t);
    c = P(285, 107);
    dl->AddCircleFilled(c, r, col(0x4000));
    dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), sym, t);
    dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), sym, t);
    c = P(263, 85);
    dl->AddCircleFilled(c, r, col(0x8000));
    dl->AddRect(ImVec2(c.x - s * 0.85f, c.y - s * 0.85f), ImVec2(c.x + s * 0.85f, c.y + s * 0.85f), sym, 0.0f, t);

    dl->AddRectFilled(P(166, 81), P(184, 89), col(0x0001), 3.0f * k);   // Select
    dl->AddTriangleFilled(P(216, 79), P(216, 91), P(232, 85), col(0x0008));   // Start
    dl->AddCircleFilled(P(200, 108), 6.0f * k, st.analogHeld ? lit : part);  // Analog

    auto stick = [&](float x, float y, uint8_t ax, uint8_t ay, uint16_t click) {
        dl->AddCircleFilled(P(x, y), 23.0f * k, part);
        const float dx = (ax - 127.5f) / 127.5f * 11.0f, dy = (ay - 127.5f) / 127.5f * 11.0f;
        const bool moved = std::fabs(dx) > 1.0f || std::fabs(dy) > 1.0f;
        dl->AddCircleFilled(P(x + dx, y + dy), 15.0f * k, on(click) ? lit : IM_COL32(95, 98, 106, 255));
        if (moved) dl->AddCircle(P(x + dx, y + dy), 15.0f * k, lit, 0, 2.0f * k);
    };
    stick(160, 132, st.lx, st.ly, 0x0002);
    stick(240, 132, st.rx, st.ry, 0x0004);
}

std::string portTitle(int port) { return ps2n::strprintf("Controller Port %d", port + 1); }
const char* typeName(const PadConfig& cfg) { return cfg.connected ? "DualShock 2" : "Not Connected"; }

// ---- The DualShock 2 page: PCSX2's layout --------------------------------------------------

void drawBindings(int port) {
    PadConfig& cfg = input::pad(port);
    if (!ImGui::BeginTable("ds2", 3, ImGuiTableFlags_None)) return;
    ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("centre", ImGuiTableColumnFlags_WidthStretch, 1.45f);
    ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    const float sp = ImGui::GetStyle().ItemSpacing.x;

    ImGui::TableNextColumn();
    plusBox("D-Pad", port, {{{"Up", Ds2::Up}, {"Left", Ds2::Left}, {"Right", Ds2::Right}, {"Down", Ds2::Down}}});
    plusBox("Left Analog", port, {{{"Up", Ds2::LUp}, {"Left", Ds2::LLeft}, {"Right", Ds2::LRight}, {"Down", Ds2::LDown}}});
    motorCell("Large Motor", port, Motor::Large);

    ImGui::TableNextColumn();
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float half = (avail - sp) / 2.0f, quarter = (avail - 3.0f * sp) / 4.0f;
        const float side = (avail - 2.0f * sp) * 0.28f, middle = avail - 2.0f * sp - 2.0f * side;
        cellRow(port, {{"L2", Ds2::L2, half}, {"R2", Ds2::R2, half}});
        cellRow(port, {{"L1", Ds2::L1, quarter}, {"Select", Ds2::Select, quarter}, {"Start", Ds2::Start, quarter}, {"R1", Ds2::R1, quarter}});
        ImGui::Spacing();
        drawController(cfg, avail);
        ImGui::Spacing();
        cellRow(port, {{"L3", Ds2::L3, side}, {"Pressure Modifier", Ds2::Pressure, middle}, {"R3", Ds2::R3, side}});
        indent(side + sp);
        bindingCell("Analog", port, Ds2::Analog, middle);
    }

    ImGui::TableNextColumn();
    plusBox("Face Buttons", port,
            {{{"Triangle", Ds2::Triangle}, {"Square", Ds2::Square}, {"Circle", Ds2::Circle}, {"Cross", Ds2::Cross}}});
    plusBox("Right Analog", port, {{{"Up", Ds2::RUp}, {"Left", Ds2::RLeft}, {"Right", Ds2::RRight}, {"Down", Ds2::RDown}}});
    motorCell("Small Motor", port, Motor::Small);
    ImGui::EndTable();
}

// ---- The settings tab: one row per setting, with what it does ----------------------------

void settingRow(const char* label, const char* help) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", help);
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool percent(const char* id, float& v, float lo, float hi) {
    float pct = v * 100.0f;
    if (!ImGui::SliderFloat(id, &pct, lo * 100.0f, hi * 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) return false;
    v = std::round(pct) / 100.0f;
    return true;
}

void drawPadSettings(int port) {
    input::PadTuning& t = input::pad(port).tuning;
    bool changed = false;
    if (ImGui::BeginTable("settings", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        static constexpr const char* kInvert[] = {"Not Inverted", "Invert Left/Right", "Invert Up/Down", "Invert Left/Right + Up/Down"};
        settingRow("Invert Left Stick", "Turns the left stick's directions around.");
        changed |= ImGui::Combo("##invl", &t.invertL, kInvert, 4);
        settingRow("Invert Right Stick", "Turns the right stick's directions around.");
        changed |= ImGui::Combo("##invr", &t.invertR, kInvert, 4);
        settingRow("Analog Deadzone", "How far a stick can lean and still read as centred.");
        changed |= percent("##dz", t.deadzone, 0.0f, 1.0f);
        settingRow("Analog Sensitivity",
                   "Scales stick movement. Above 100%, a round stick reaches the corners of the square a DualShock 2 reports; "
                   "the default is 133%.");
        changed |= percent("##scale", t.axisScale, 0.01f, 2.0f);
        settingRow("Use Diagonal Scale Correction",
                   "Stretches a round stick's diagonals out to the square's corners. Use it with 100% sensitivity.");
        changed |= ImGui::Checkbox("##diag", &t.diagonalCorrection);
        settingRow("Large Motor Vibration Scale", "How strongly the game's vibration drives the large (low frequency) motor.");
        changed |= percent("##large", t.largeMotorScale, 0.0f, 2.0f);
        settingRow("Small Motor Vibration Scale", "How strongly the game's vibration drives the small (high frequency) motor.");
        changed |= percent("##small", t.smallMotorScale, 0.0f, 2.0f);
        settingRow("Button/Trigger Deadzone", "Buttons and triggers bound to analog inputs read as released below this.");
        changed |= percent("##bdz", t.buttonDeadzone, 0.0f, 1.0f);
        settingRow("Pressure Modifier Amount", "The pressure the buttons report while Apply Pressure is held.");
        changed |= percent("##pressure", t.pressureModifier, 0.01f, 1.0f);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Restore Default Settings")) {
        t = input::PadTuning{};
        changed = true;
    }
    if (changed) input::save();
}

// ---- Pages -------------------------------------------------------------------------------------

void drawPortPage(int port) {
    PadConfig& cfg = input::pad(port);
    ImGui::SeparatorText(portTitle(port).c_str());
    int type = cfg.connected ? 1 : 0;
    static constexpr const char* kTypes[] = {"Not Connected", "DualShock 2"};
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Controller Type");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    if (ImGui::Combo("##type", &type, kTypes, 2)) {
        cfg.connected = type == 1;
        input::save();
    }
    if (!cfg.connected) {
        ImGui::Spacing();
        ImGui::TextDisabled("No controller is plugged into this port.");
        return;
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
    auto tab = [](const char* label, bool settings) {
        const bool current = g_settingsTab == settings;
        if (current) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        if (ImGui::Button(label)) g_settingsTab = settings;
        if (current) ImGui::PopStyleColor();
    };
    tab("Bindings", false);
    ImGui::SameLine();
    tab("Settings", true);

    // Automatic Mapping and Clear Mapping, on the right.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float wMap = ImGui::CalcTextSize("Automatic Mapping").x + style.FramePadding.x * 2.0f;
    const float wClear = ImGui::CalcTextSize("Clear Mapping").x + style.FramePadding.x * 2.0f;
    alignRight(wMap + wClear + style.ItemSpacing.x);
    ImGui::BeginDisabled(g_settingsTab);
    if (ImGui::Button("Automatic Mapping")) ImGui::OpenPopup("automap");
    ImGui::SameLine();
    if (ImGui::Button("Clear Mapping")) ImGui::OpenPopup("Clear Mapping");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("automap")) {
        const auto devices = input::sdl::devices();
        for (const auto& d : devices) {
            if (ImGui::Selectable(ps2n::strprintf("SDL-%d: %s", d.device, d.name.c_str()).c_str())) {
                input::mapController(cfg, d.device);
                input::save();
                osd(ps2n::strprintf("Automatic mapping completed for %s.", portTitle(port).c_str()));
            }
        }
        if (ImGui::Selectable("Keyboard")) {
            input::mapKeyboard(cfg);
            input::save();
            osd(ps2n::strprintf("Automatic mapping completed for %s.", portTitle(port).c_str()));
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Clear Mapping", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Are you sure you want to clear all bindings for this controller?\nThis cannot be undone.");
        if (ImGui::Button("Clear")) {
            for (auto& b : cfg.bindings) b.clear();
            for (auto& m : cfg.motors) m.clear();
            input::save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    if (ImGui::BeginChild("tab", ImVec2(0.0f, 0.0f))) {
        if (g_settingsTab) drawPadSettings(port);
        else drawBindings(port);
    }
    ImGui::EndChild();
}

void drawHotkeysPage() {
    ImGui::SeparatorText("Hotkeys");
    static constexpr const char* kCategories[] = {"Navigation", "Speed", "System", "Graphics"};
    const float w = std::max(ImGui::GetFontSize() * 18.0f, ImGui::GetContentRegionAvail().x * 0.4f);
    for (const char* category : kCategories) {
        ImGui::Spacing();
        ImGui::SeparatorText(category);
        for (size_t h = 0; h < size_t(Hotkey::Count); ++h) {
            if (std::string_view(input::hotkeyCategory(Hotkey(h))) != category) continue;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(input::hotkeyLabel(Hotkey(h)));
            alignRight(w);
            bindingButton(input::hotkeyKey(Hotkey(h)), true, 0, h, w);
        }
    }
}

void drawGlobalPage() {
    settings::Ini& ini = input::settingsFile();
    ImGui::SeparatorText("SDL Input Source");
    bool restart = false;
    auto option = [&](const char* label, const char* key, bool fallback, const char* help) {
        bool v = ini.getBool("InputSources", key, fallback);
        if (ImGui::Checkbox(label, &v)) {
            ini.setBool("InputSources", key, v);
            restart = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        ImGui::SetItemTooltip("%s", help);
    };
    option("DualShock 4 / DualSense Enhanced Mode", "SDLControllerEnhancedMode", true,
           "Drives the rumble and lights of Sony's controllers. Other programs may need the controller reconnected afterwards.");
    option("DualSense Player LED", "SDLPS5PlayerLED", true, "Lights a DualSense's player LEDs for its number.");
#ifdef _WIN32
    option("SDL Raw Input", "SDLRawInput", false, "Reads controllers through Windows' raw input.");
#endif
    if (ImGui::Button("Controller LED Settings")) ImGui::OpenPopup("Controller LED Settings");
    if (ImGui::BeginPopupModal("Controller LED Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static constexpr const char* kDefaults[4] = {"000080", "800000", "008000", "808000"};
        for (int n = 0; n < 4; ++n) {
            const std::string key = ps2n::strprintf("Player%dLED", n);
            const unsigned long rgb = std::strtoul(ini.getString("SDLExtra", key, kDefaults[n]).c_str(), nullptr, 16);
            float c[3] = {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f};
            if (ImGui::ColorEdit3(ps2n::strprintf("SDL-%d", n).c_str(), c, ImGuiColorEditFlags_NoInputs)) {
                const auto byte = [](float f) { return static_cast<unsigned>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
                ini.set("SDLExtra", key, ps2n::strprintf("%02X%02X%02X", byte(c[0]), byte(c[1]), byte(c[2])));
                input::save();
                input::sdl::applyLeds();
            }
        }
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (restart) {
        input::save();
        // ImGui's own handles on the controllers go first: the restart closes them all.
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
        input::sdl::restart();
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoAll);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Detected Devices");
    ImGui::BulletText("Keyboard");
    for (const auto& d : input::sdl::devices())
        ImGui::BulletText("SDL-%d: %s%s", d.device, d.name.c_str(), d.rumble ? " (vibration)" : "");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Settings file: %s", input::settingsPath().string().c_str());
    ImGui::PopStyleColor();
}

void drawEditPopup() {
    if (g_edit.open) {
        ImGui::OpenPopup("Edit Bindings");
        g_edit.open = false;
    }
    if (!ImGui::BeginPopupModal("Edit Bindings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    auto& list = bindingsOf(g_edit.hotkey, g_edit.port, g_edit.index);
    const char* what = g_edit.hotkey ? input::hotkeyKey(Hotkey(g_edit.index)) : input::ds2Key(Ds2(g_edit.index));
    ImGui::Text("Bindings for %s %s", g_edit.hotkey ? "Hotkeys" : ps2n::strprintf("Pad%d", g_edit.port + 1).c_str(), what);
    if (ImGui::BeginListBox("##list", ImVec2(ImGui::GetFontSize() * 24.0f, ImGui::GetFontSize() * 7.0f))) {
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
            if (ImGui::Selectable(input::formatBinding(list[size_t(i)]).c_str(), g_edit.selected == i)) g_edit.selected = i;
        ImGui::EndListBox();
    }
    if (!g_edit.hotkey) {
        PadConfig& cfg = input::pad(g_edit.port);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        bool changed = percent("Sensitivity", cfg.scale[g_edit.index], 0.01f, 2.0f);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        changed |= percent("Deadzone", cfg.deadzone[g_edit.index], 0.0f, 1.0f);
        if (changed) input::save();
    }
    if (g_capture.active && g_capture.append) ImGui::Text("Push Button/Axis... [%d]", secondsLeft());
    else ImGui::TextDisabled(" ");
    ImGui::BeginDisabled(g_capture.active);
    if (ImGui::Button("Add Binding")) startCapture(g_edit.hotkey, g_edit.port, g_edit.index, true);
    ImGui::SameLine();
    if (ImGui::Button("Remove Binding") && g_edit.selected >= 0 && g_edit.selected < static_cast<int>(list.size())) {
        list.erase(list.begin() + g_edit.selected);
        g_edit.selected = -1;
        input::save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Bindings")) {
        list.clear();
        input::save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void drawMotorPopup() {
    if (g_motorPick.open) {
        ImGui::OpenPopup("Vibration Motor");
        g_motorPick.open = false;
    }
    if (!ImGui::BeginPopupModal("Vibration Motor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    auto& list = input::pad(g_motorPick.port).motors[size_t(g_motorPick.motor)];
    ImGui::Text("Select vibration motor for Pad%d/%s.", g_motorPick.port + 1, input::motorKey(g_motorPick.motor));
    bool any = false;
    for (const auto& d : input::sdl::devices()) {
        if (!d.rumble) continue;
        any = true;
        for (int m = 0; m < 2; ++m) {
            const std::string text = ps2n::strprintf("SDL-%d/%s (%s)", d.device, input::motorKey(Motor(m)), d.name.c_str());
            if (ImGui::Selectable(text.c_str())) {
                HostKey k;
                k.kind = HostKey::Kind::Motor;
                k.device = d.device;
                k.index = m;
                list = {Binding{{k}}};
                input::save();
                ImGui::CloseCurrentPopup();
            }
        }
    }
    if (!any) ImGui::TextDisabled("No devices with vibration motors were detected.");
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawSettings() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 size(vp->WorkSize.x * 0.96f, vp->WorkSize.y * 0.94f);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Controller Settings", nullptr, flags)) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float footer = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
        if (ImGui::BeginChild("pages", ImVec2(ImGui::GetFontSize() * 11.0f, -footer), ImGuiChildFlags_Borders)) {
            auto entry = [](const char* label, Page page) {
                if (ImGui::Selectable(label, g_page == page, 0, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 2.0f))) g_page = page;
            };
            entry("Global Settings", Page::Global);
            entry(ps2n::strprintf("Controller Port 1\n%s", typeName(input::pad(0))).c_str(), Page::Port1);
            entry(ps2n::strprintf("Controller Port 2\n%s", typeName(input::pad(1))).c_str(), Page::Port2);
            entry("Hotkeys", Page::Hotkeys);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("page", ImVec2(0.0f, -footer))) {
            switch (g_page) {
            case Page::Global: drawGlobalPage(); break;
            case Page::Port1: drawPortPage(0); break;
            case Page::Port2: drawPortPage(1); break;
            case Page::Hotkeys: drawHotkeysPage(); break;
            }
        }
        ImGui::EndChild();

        ImGui::BeginDisabled(g_capture.active);
        if (ImGui::Button("Restore Defaults")) ImGui::OpenPopup("Restore Defaults");
        alignRight(ImGui::CalcTextSize("Close").x + style.FramePadding.x * 2.0f);
        if (ImGui::Button("Close")) g_screen = Screen::PauseMenu;
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Restore Defaults", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Restore the default controller and hotkey configuration?\nAll bindings and settings will be lost.");
            if (ImGui::Button("Restore")) {
                input::pad(0) = input::defaultPad(0);
                input::pad(1) = input::defaultPad(1);
                input::hotkeys() = input::defaultHotkeys();
                input::save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        drawEditPopup();
        drawMotorPopup();
    }
    ImGui::End();
}

void drawPauseMenu() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Pause Menu", nullptr, flags)) {
        ImGui::TextDisabled("%s", g_game ? g_game->name : "PS2Native");
        ImGui::TextUnformatted("Paused");
        ImGui::Separator();
        const ImVec2 size(ImGui::GetFontSize() * 14.0f, 0.0f);
        if (g_focusMenu) {
            ImGui::SetKeyboardFocusHere();
            g_focusMenu = false;
        }
        if (ImGui::Button("Resume Game", size)) g_screen = Screen::None;
        if (ImGui::Button("Controller Settings", size)) g_screen = Screen::Settings;
        if (ImGui::Button("Toggle Fullscreen", size)) g_action = Action::ToggleFullscreen;
        if (ImGui::Button("Quit Game", size)) g_action = Action::Quit;
    }
    ImGui::End();
}

void drawOsd() {
    const double now = ImGui::GetTime();
    std::erase_if(g_osd, [&](const Message& m) { return m.until <= now; });
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float pad = ImGui::GetFontSize() * 0.4f;
    ImVec2 at(pad * 2.0f, pad * 2.0f);
    for (const Message& m : g_osd) {
        const float alpha = static_cast<float>(std::clamp((m.until - now) / 0.5, 0.0, 1.0));
        const ImVec2 ts = ImGui::CalcTextSize(m.text.c_str());
        dl->AddRectFilled(at, ImVec2(at.x + ts.x + pad * 2.0f, at.y + ts.y + pad * 2.0f), IM_COL32(0, 0, 0, int(170 * alpha)), pad);
        dl->AddText(ImVec2(at.x + pad, at.y + pad), IM_COL32(255, 255, 255, int(255 * alpha)), m.text.c_str());
        at.y += ts.y + pad * 3.0f;
    }
    if (g_pausedIndicator && g_screen == Screen::None) {
        const char* text = "Paused";
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const float x = ImGui::GetMainViewport()->WorkSize.x - ts.x - pad * 4.0f;
        dl->AddRectFilled(ImVec2(x, pad * 2.0f), ImVec2(x + ts.x + pad * 2.0f, pad * 4.0f + ts.y), IM_COL32(0, 0, 0, 170), pad);
        dl->AddText(ImVec2(x + pad, pad * 3.0f), IM_COL32(255, 255, 255, 255), text);
    }
}

// A system font where there is one (it reads better than ImGui's own).
void loadFont() {
    ImGuiIO& io = ImGui::GetIO();
    static constexpr const char* kFonts[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
    };
    for (const char* path : kFonts) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && io.Fonts->AddFontFromFileTTF(path)) return;
    }
    io.Fonts->AddFontDefaultVector();
}

// Sizes follow the window: text for a 720-line window at 18 pixels.
void scaleToWindow() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    const float scale = std::clamp(static_cast<float>(h) / 720.0f, 0.75f, 3.0f);
    if (std::fabs(scale - g_scale) < 0.01f) return;
    g_scale = scale;
    ImGuiStyle& style = ImGui::GetStyle();
    style = g_baseStyle;
    style.ScaleAllSizes(scale);
    style.FontSizeBase = 18.0f;
    style.FontScaleMain = scale;
}

} // namespace

void init(SDL_Window* window, SDL_Renderer* renderer) {
    g_window = window;
    g_renderer = renderer;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nothing of ImGui's own goes to disk
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    g_baseStyle = style;
    g_scale = 0.0f;
    loadFont();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoAll);
}

void shutdown() {
    if (!g_renderer) return;
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    g_renderer = nullptr;
    g_window = nullptr;
}

void processEvent(const SDL_Event& ev) {
    if (g_capture.active && ev.type != SDL_CONTROLLERDEVICEADDED && ev.type != SDL_CONTROLLERDEVICEREMOVED) {
        captureEvent(ev);
        // Releases and pointer motion still reach ImGui, so that it does not
        // think the click that started the capture is still held.
        if (ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEBUTTONUP || ev.type == SDL_KEYUP) ImGui_ImplSDL2_ProcessEvent(&ev);
        return;
    }
    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat)
        g_escapeForPopup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    ImGui_ImplSDL2_ProcessEvent(&ev);   // ImGui also keeps its own list of controllers
}

void frame(const input::HostState& host) {
    g_host = host;
    if (g_capture.active && ImGui::GetTime() - g_capture.started > kCaptureSeconds) endCapture();
    scaleToWindow();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (g_screen == Screen::PauseMenu) drawPauseMenu();
    else if (g_screen == Screen::Settings) drawSettings();
    drawOsd();
    ImGui::Render();
}

void render() { ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer); }

bool menuOpen() { return g_screen != Screen::None; }

void toggleMenu() {
    // The Escape that closed a popup does not also leave the menu.
    if (std::exchange(g_escapeForPopup, false)) return;
    if (g_screen == Screen::Settings) g_screen = Screen::PauseMenu;
    else if (g_screen == Screen::PauseMenu) g_screen = Screen::None;
    else {
        g_screen = Screen::PauseMenu;
        g_focusMenu = true;
    }
}

bool capturing() { return g_capture.active; }

void osd(const std::string& message, float seconds) {
    if (!ImGui::GetCurrentContext()) return;
    g_osd.push_back({message, ImGui::GetTime() + seconds});
    if (g_osd.size() > 6) g_osd.erase(g_osd.begin());
}

void setPausedIndicator(bool on) { g_pausedIndicator = on; }

Action takeAction() { return std::exchange(g_action, Action::None); }

} // namespace ps2::ui

#endif
