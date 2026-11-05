#include "video/video.h"

#include "audio/audio.h"
#include "clock.h"
#include "hw/gs.h"
#include "hw/hw.h"
#include "input/input.h"
#include "internal.h"
#include "log.h"
#include "settings.h"

#include "ps2n/util.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "gpu/gpu.h"

#ifdef PS2N_HAVE_SDL2
#include "input/sdl.h"
#include "ui/overlay.h"

#include <SDL.h>
#endif

#if defined(PS2N_HAVE_SDL2) && defined(PS2N_HAVE_VULKAN)
#include "video/present_vk.h"
#define PS2N_VULKAN_WINDOW 1
#endif

namespace ps2::video {

namespace {

// ---- The picture's shape ------------------------------------------------------------------------

constexpr const char* kAspectNames[] = {"Stretch", "Auto 4:3/3:2", "4:3", "16:9", "10:7"};
constexpr const char* kFmvAspectNames[] = {"Off", "Auto 4:3/3:2", "4:3", "16:9", "10:7"};
static_assert(std::size(kAspectNames) == size_t(AspectRatio::Count));
static_assert(std::size(kFmvAspectNames) == size_t(FmvAspectRatio::Count));
constexpr const char* kBilinearNames[] = {"Off", "Bilinear (Smooth)", "Bilinear (Sharp)"};
static_assert(std::size(kBilinearNames) == size_t(BilinearMode::Count));
constexpr const char* kUpscaleNames[] = {"1x Native (PS2)", "2x Native (~720p)", "3x Native (~1080p)", "4x Native (~1440p/2K)",
                                         "5x Native (~1620p)", "6x Native (~2160p/4K)", "7x Native (~2520p)", "8x Native (~2880p/5K)"};
static_assert(std::size(kUpscaleNames) == size_t(kMaxUpscale));
constexpr const char* kSection = "EmuCore/GS";

// A patch's gsaspectratio, N:D with D not 0 as PCSX2 takes it; 0 otherwise.
double customRatio(const char* text) {
    if (!text) return 0.0;
    const std::string_view s(text);
    const size_t colon = s.find(':');
    if (colon == std::string_view::npos) return 0.0;
    const auto n = ps2n::parseInt(s.substr(0, colon)), d = ps2n::parseInt(s.substr(colon + 1));
    return n && d && *n > 0 && *d > 0 ? static_cast<double>(*n) / static_cast<double>(*d) : 0.0;
}

// What an aspect ratio setting shows: 0 fills the window. FmvAspectRatio's
// values, after Off, are AspectRatio's from Auto on. Under Auto, the game's
// patches have their say, as PCSX2 lets them.
double ratioOf(AspectRatio r, bool progressive, const char* gameAspect) {
    switch (r) {
    case AspectRatio::Stretch: return 0.0;
    case AspectRatio::Auto:
        if (const double custom = customRatio(gameAspect); custom > 0.0) return custom;
        return progressive ? 3.0 / 2.0 : 4.0 / 3.0;
    case AspectRatio::R4_3: return 4.0 / 3.0;
    case AspectRatio::R16_9: return 16.0 / 9.0;
    case AspectRatio::R10_7: return 10.0 / 7.0;
    case AspectRatio::Count: break;
    }
    return 4.0 / 3.0;
}

// PCSX2's way of telling a film: every seventh VDEC (its bit stream decoding)
// starts one, if none plays, at the next vsync; it ends at the first vsync
// 60,000,000 EE cycles (about a fifth of a second) after the last VDEC.
constexpr uint64_t kFilmIdleCycles = 60000000;
std::atomic<bool> g_film{false};
bool g_filmStarting = false;     // guest thread, from here on
unsigned g_vdecs = 0;
uint64_t g_lastFilmDecode = 0;

// The settings ([EmuCore/GS]), main thread: as saved (g_saved) and as this
// run uses them (the command line may say otherwise for the run).
DisplaySettings g_saved;
AspectRatio g_aspect = AspectRatio::Auto;
FmvAspectRatio g_fmvAspect = FmvAspectRatio::Off;
BilinearMode g_bilinear = BilinearMode::Smooth;
bool g_integerScaling = false;

// Saves one [EmuCore/GS] key in the settings file.
void saveSetting(std::string_view key, const std::string& value);

uint64_t g_frames = 0;
std::atomic<bool> g_windowOpen{false};     // the main thread shows frames
std::atomic<bool> g_windowClosed{false};

// The newest displayed frame, handed from the guest thread to the main thread:
// the software renderer's pixels, or a picture the Vulkan renderer drew.
struct GpuPicture {
    int slot = -1;
    uint64_t ready = 0;
    int width = 0, height = 0;
};
std::mutex g_frameMutex;
std::condition_variable g_frameReady;
gs::Frame g_pending;
GpuPicture g_pendingPicture;   // slot -1: the pending frame is g_pending
bool g_hasPending = false;

// Pausing: the main thread asks, the guest thread stops at its next vsync.
std::mutex g_pauseMutex;
std::condition_variable g_pauseChanged;
bool g_pauseWanted = false;

// Guest thread: waits here while paused. Time spent here does not count.
void pausePoint() {
    std::unique_lock<std::mutex> lock(g_pauseMutex);
    if (!g_pauseWanted) return;
    clockPause();
    g_pauseChanged.wait(lock, [] { return !g_pauseWanted || g_windowClosed; });
    clockResume();
}

void dumpPpm(const gs::Frame& f, uint64_t index) {
    std::error_code ec;
    std::filesystem::create_directories(g_opt.dumpFrames, ec);
    char name[64];
    std::snprintf(name, sizeof name, "frame_%06llu.ppm", static_cast<unsigned long long>(index));
    const auto path = g_opt.dumpFrames / name;
    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        PS2_WARN("video", "cannot write %s", path.string().c_str());
        return;
    }
    std::fprintf(fp, "P6\n%d %d\n255\n", f.width, f.height);
    for (uint32_t px : f.pixels) {
        const unsigned char rgb[3] = {static_cast<unsigned char>(px), static_cast<unsigned char>(px >> 8),
                                      static_cast<unsigned char>(px >> 16)};
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
}

#ifdef PS2N_HAVE_SDL2
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;   // null when the window shows its pictures through Vulkan
SDL_Texture* g_texture = nullptr;
std::atomic<bool> g_vulkanWindow{false};   // read by the GS thread too
gs::Frame g_shown;   // the picture on screen (its size; with SDL's renderer, its pixels for screenshots)

// Speed and pause, as the hotkeys left them.
bool g_userPaused = false, g_limiterOff = false, g_turbo = false, g_turboHeld = false;

void setPaused(bool on) {
    {
        const std::lock_guard<std::mutex> lock(g_pauseMutex);
        if (g_pauseWanted == on) return;
        g_pauseWanted = on;
    }
    g_pauseChanged.notify_all();
}

double currentAspect() { return pictureAspect(g_aspect, g_fmvAspect, g_film, false, g_game ? g_game->aspectRatio : nullptr); }

// Whatever the framebuffer's size, the PS2 drew for a television of the
// shape the aspect ratio settings say: the picture keeps it, centred, and the
// rest of the window is black. Integer scaling shrinks it to a whole multiple
// of the picture.
Rect pictureRect(int w, int h) {
    Rect r = fitPicture(w, h, currentAspect());
    if (g_integerScaling) r = integerRect(r, w, h, g_shown.height);
    return r;
}

void render() {
#ifdef PS2N_VULKAN_WINDOW
    if (g_vulkanWindow) {
        int w = 0, h = 0;
        vulkan::outputSize(w, h);
        // The game dims behind the menu.
        const vulkan::Look look{pictureRect(w, h), g_bilinear, ui::menuOpen() ? 0.5f : 1.0f};
        vulkan::draw(look, [](VkCommandBuffer cmd) { ui::render(cmd); });
        return;
    }
#endif
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    if (g_texture) {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(g_renderer, &w, &h);
        const Rect pr = pictureRect(w, h);
        const SDL_Rect r{pr.x, pr.y, pr.w, pr.h};
        SDL_RenderCopy(g_renderer, g_texture, nullptr, &r);
    }
    if (ui::menuOpen()) {   // the game dims behind the menu
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 128);
        SDL_RenderFillRect(g_renderer, nullptr);
    }
    ui::render();
    SDL_RenderPresent(g_renderer);
}

void show(gs::Frame&& f) {
#ifdef PS2N_VULKAN_WINDOW
    if (g_vulkanWindow) {
        vulkan::showFrame(f);
        g_shown = std::move(f);
        return;
    }
#endif
    if (!g_texture || f.width != g_shown.width || f.height != g_shown.height) {
        if (g_texture) SDL_DestroyTexture(g_texture);
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, f.width, f.height);
    }
    // SDL's renderer has no sharp bilinear: Sharp filters as Smooth does.
    if (g_texture) SDL_SetTextureScaleMode(g_texture, g_bilinear == BilinearMode::Off ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    SDL_UpdateTexture(g_texture, nullptr, f.pixels.data(), f.width * 4);
    g_shown = std::move(f);
}

// A picture the Vulkan renderer drew: only a Vulkan window shows those.
void showPicture(const GpuPicture& p) {
#ifdef PS2N_VULKAN_WINDOW
    vulkan::showPicture(p.slot, p.ready, p.width, p.height);
#endif
    g_shown.width = p.width;
    g_shown.height = p.height;
    g_shown.pixels.clear();
}

void toggleFullscreen() {
    const bool full = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(g_window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_ShowCursor(full ? SDL_ENABLE : SDL_DISABLE);
}

void applySpeed() { clockSetNoThrottle(g_opt.noThrottle || g_limiterOff || g_turbo || g_turboHeld); }

// The picture on screen, as a BMP in ./snaps (PCSX2 keeps its in "snaps" too).
void screenshot() {
    gs::Frame picture;   // the Vulkan window reads its picture back; SDL's keeps the pixels
#ifdef PS2N_VULKAN_WINDOW
    if (g_vulkanWindow) vulkan::shownPixels(picture);
    else
#endif
        picture = g_shown;
    if (picture.pixels.empty()) return;
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    std::error_code ec;
    const auto dir = std::filesystem::current_path(ec) / "snaps";
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / ps2n::strprintf("%s_%s.bmp", g_game ? g_game->name : "ps2n", stamp);
    SDL_Surface* rgba = SDL_CreateRGBSurfaceWithFormatFrom(picture.pixels.data(), picture.width, picture.height, 32,
                                                           picture.width * 4, SDL_PIXELFORMAT_ABGR8888);
    SDL_Surface* rgb = rgba ? SDL_ConvertSurfaceFormat(rgba, SDL_PIXELFORMAT_RGB24, 0) : nullptr;
    const std::u8string file = path.u8string();
    if (rgb && SDL_SaveBMP(rgb, reinterpret_cast<const char*>(file.c_str())) == 0) {
        ui::osd("Screenshot saved to " + path.string() + ".");
        PS2_INFO("video", "screenshot saved to %s", path.string().c_str());
    } else {
        PS2_WARN("video", "cannot save a screenshot to %s (%s)", path.string().c_str(), SDL_GetError());
    }
    SDL_FreeSurface(rgb);
    SDL_FreeSurface(rgba);
}

// The hotkeys act when released, as PCSX2's do; Hold Turbo lasts while held.
void hotkey(const input::HotkeyEvent& e) {
    using H = input::Hotkey;
    if (e.hotkey == H::HoldTurbo) {
        g_turboHeld = e.pressed;
        applySpeed();
        return;
    }
    if (e.pressed) return;
    switch (e.hotkey) {
    case H::OpenPauseMenu: ui::toggleMenu(); break;
    case H::ToggleFullscreen: toggleFullscreen(); break;
    case H::TogglePause:
        g_userPaused = !g_userPaused;
        ui::setPausedIndicator(g_userPaused);
        break;
    case H::ToggleFrameLimit:
        g_limiterOff = !g_limiterOff;
        ui::osd(g_limiterOff ? "Frame limiter disabled." : "Frame limiter enabled.");
        applySpeed();
        break;
    case H::ToggleTurbo:
        g_turbo = !g_turbo;
        ui::osd(g_turbo ? "Turbo enabled." : "Turbo disabled.");
        applySpeed();
        break;
    case H::CycleAspectRatio:   // for this run; the settings keep theirs
        g_aspect = AspectRatio((size_t(g_aspect) + 1) % size_t(AspectRatio::Count));
        ui::osd(std::string("Aspect ratio set to '") + aspectRatioName(g_aspect) + "'.");
        break;
    case H::CycleInterlaceMode: {
        const auto next = static_cast<gs::Deinterlace>((static_cast<int>(gs::deinterlace()) + 1) % static_cast<int>(gs::Deinterlace::Count));
        gs::setDeinterlace(next);
        ui::osd(std::string("Deinterlacing: ") + gs::deinterlaceName(next) + ".");
        break;
    }
    case H::Screenshot: screenshot(); break;
    case H::ShutdownVM: g_windowClosed = true; break;
    case H::Mute:   // PCSX2's messages
        if (!audio::setMuted(!audio::muted())) ui::osd("Volume: Muted in Settings");
        else if (audio::muted()) ui::osd("Volume: Muted");
        else ui::osd(ps2n::strprintf("Volume: Unmuted to %d%%", audio::volume()));
        break;
    case H::IncreaseVolume:
    case H::DecreaseVolume: {
        const int delta = e.hotkey == H::IncreaseVolume ? 5 : -5;
        const int v = std::clamp(audio::volume() + delta, 0, 200);
        if (!audio::setVolume(v)) ui::osd("Volume: Muted in Settings");
        else if (v > 0 && v < 200) ui::osd(ps2n::strprintf("Volume: %s to %d%%", delta < 0 ? "Decreased" : "Increased", v));
        else ui::osd(ps2n::strprintf("Volume: %s %d%% Reached", delta < 0 ? "Minimum" : "Maximum", v));
        break;
    }
    default: break;
    }
}

// The window opens before the game draws anything and stays black until its first frame.
bool openWindow() {
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");   // keep our Ctrl+C handling
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    input::sdl::setHints();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        PS2_WARN("video", "SDL_Init failed (%s); running headless", SDL_GetError());
        return false;
    }
    input::sdl::open();
    // The picture's shape (4:3 for Stretch), three quarters of the screen's height.
    SDL_Rect usable{0, 0, 1280, 960};
    SDL_GetDisplayUsableBounds(0, &usable);
    const int h = std::max(480, usable.h * 3 / 4);
    const double aspect = currentAspect() > 0.0 ? currentAspect() : 4.0 / 3.0;
    const int w = std::min(std::max(usable.w, 640), static_cast<int>(std::lround(h * aspect)));
    const std::string title = std::string(g_game ? g_game->name : "ps2n") + " (PS2Native)";
#ifdef PS2N_VULKAN_WINDOW
    // Through Vulkan where the graphics card is open and can show pictures:
    // the Vulkan renderer's pictures never leave the card. Else, and where
    // that fails, SDL's renderer as before (a window made for Vulkan cannot
    // have one, so it is made again).
    if (gpu::available() && gpu::status().canPresent) {
        g_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
        std::string why = g_window ? "" : SDL_GetError();
        if (g_window && vulkan::open(g_window, why)) {
            ui::initVulkan(g_window);
            g_vulkanWindow = true;
            PS2_DEBUG("video", "the window shows its pictures through Vulkan");
            return true;
        }
        PS2_WARN("video", "cannot show the pictures through Vulkan (%s); using SDL's renderer", why.c_str());
        vulkan::close();
        if (g_window) SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
#endif
    g_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE);
    g_renderer = g_window ? SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!g_renderer) {
        PS2_WARN("video", "cannot create an SDL window (%s); running headless", SDL_GetError());
        return false;
    }
    ui::init(g_window, g_renderer);
    return true;
}
#endif

bool windowEnabled() { return g_windowOpen; }

void saveSetting(std::string_view key, const std::string& value) {
    input::settingsFile().set(kSection, key, value);
    input::save();
}

// "true", "1", "yes" (any case) and the like, as PCSX2 writes and reads booleans.
std::optional<bool> parseBool(std::string_view text) {
    const std::string t = ps2n::toLower(ps2n::trim(text));
    if (t == "true" || t == "1" || t == "yes" || t == "on") return true;
    if (t == "false" || t == "0" || t == "no" || t == "off") return false;
    return std::nullopt;
}

} // namespace

const char* rendererName(Renderer r) {
    switch (r) {
    case Renderer::Auto: return "Automatic (Default)";
    case Renderer::Software: return "Software";
    case Renderer::Vulkan: return "Vulkan";
    }
    return "?";
}

std::optional<Renderer> parseRendererOption(std::string_view text) {
    const std::string t = ps2n::toLower(ps2n::trim(text));
    if (t == "auto") return Renderer::Auto;
    if (t == "software") return Renderer::Software;
    if (t == "vulkan") return Renderer::Vulkan;
    return std::nullopt;
}

const char* bilinearModeName(BilinearMode m) { return m < BilinearMode::Count ? kBilinearNames[size_t(m)] : "?"; }
const char* upscaleName(int n) { return n >= 1 && n <= kMaxUpscale ? kUpscaleNames[n - 1] : "?"; }

DisplaySettings readDisplaySettings(const settings::Ini& ini, std::vector<std::string>* warnings) {
    DisplaySettings s;
    const auto warn = [&](std::string text) {
        if (warnings) warnings->push_back(std::move(text));
    };
    if (const auto v = ini.value(kSection, "Renderer")) {
        const auto n = ps2n::parseInt(ps2n::trim(*v));
        if (n && (*n == -1 || *n == 13 || *n == 14)) s.renderer = static_cast<Renderer>(*n);
        else warn("Renderer " + *v + " in the settings file is not a renderer PS2Native has (-1 Automatic, 13 Software, 14 Vulkan); using Automatic");
    }
    if (const auto v = ini.value(kSection, "upscale_multiplier")) {
        // PCSX2 keeps a float; the Vulkan renderer draws at whole multiples.
        char* end = nullptr;
        const std::string t = std::string(ps2n::trim(*v));
        const double f = std::strtod(t.c_str(), &end);
        if (t.empty() || !end || *end || !(f == f)) {
            warn("upscale_multiplier " + *v + " in the settings file is not a number; using 1");
        } else {
            const int n = std::clamp(static_cast<int>(std::lround(f)), 1, kMaxUpscale);
            if (static_cast<double>(n) != f)
                warn(ps2n::strprintf("upscale_multiplier %s in the settings file is not a whole number from 1 to %d; using %d", v->c_str(),
                                     kMaxUpscale, n));
            s.upscale = n;
        }
    }
    if (const auto v = ini.value(kSection, "linear_present_mode")) {
        const auto n = ps2n::parseInt(ps2n::trim(*v));
        if (n && *n >= 0 && *n < static_cast<int64_t>(BilinearMode::Count)) s.bilinear = static_cast<BilinearMode>(*n);
        else warn("linear_present_mode " + *v + " in the settings file is not 0, 1 or 2; using 1 (Bilinear (Smooth))");
    }
    if (const auto v = ini.value(kSection, "IntegerScaling")) {
        if (const auto b = parseBool(*v)) s.integerScaling = *b;
        else warn("IntegerScaling " + *v + " in the settings file is not true or false; using false");
    }
    const std::string a = ini.getString(kSection, "AspectRatio", aspectRatioName(AspectRatio::Auto));
    if (const auto r = parseAspectRatio(a)) s.aspect = *r;
    else warn("unknown AspectRatio '" + a + "' in the settings file; using Auto 4:3/3:2");
    const std::string f = ini.getString(kSection, "FMVAspectRatioSwitch", fmvAspectRatioName(FmvAspectRatio::Off));
    if (const auto r = parseFmvAspectRatio(f)) s.fmvAspect = *r;
    else warn("unknown FMVAspectRatioSwitch '" + f + "' in the settings file; using Off");
    return s;
}

void writeDisplaySettings(settings::Ini& ini, const DisplaySettings& s) {
    ini.set(kSection, "Renderer", std::to_string(static_cast<int>(s.renderer)));
    ini.set(kSection, "upscale_multiplier", std::to_string(s.upscale));
    ini.set(kSection, "linear_present_mode", std::to_string(static_cast<int>(s.bilinear)));
    ini.set(kSection, "IntegerScaling", s.integerScaling ? "true" : "false");
    ini.set(kSection, "AspectRatio", aspectRatioName(s.aspect));
    ini.set(kSection, "FMVAspectRatioSwitch", fmvAspectRatioName(s.fmvAspect));
}

Rect integerRect(const Rect& fitted, int windowW, int windowH, int pictureH) {
    if (pictureH <= 0 || fitted.w <= 0 || fitted.h <= 0) return fitted;
    // Lines are what the console sends whole; across, the picture is
    // stretched to its shape anyway. So the height becomes a whole multiple of
    // the picture's, and the width follows the shape.
    const int k = fitted.h / pictureH;
    if (k < 1) return fitted;
    const int h = pictureH * k;
    const int w = static_cast<int>(std::lround(static_cast<double>(fitted.w) * h / fitted.h));
    return {(windowW - w) / 2, (windowH - h) / 2, w, h};
}

const char* aspectRatioName(AspectRatio r) { return r < AspectRatio::Count ? kAspectNames[size_t(r)] : "?"; }
const char* fmvAspectRatioName(FmvAspectRatio r) { return r < FmvAspectRatio::Count ? kFmvAspectNames[size_t(r)] : "?"; }

std::optional<AspectRatio> parseAspectRatio(std::string_view text) {
    for (size_t i = 0; i < std::size(kAspectNames); ++i)
        if (ps2n::toLower(ps2n::trim(text)) == ps2n::toLower(kAspectNames[i])) return AspectRatio(i);
    return std::nullopt;
}

std::optional<FmvAspectRatio> parseFmvAspectRatio(std::string_view text) {
    for (size_t i = 0; i < std::size(kFmvAspectNames); ++i)
        if (ps2n::toLower(ps2n::trim(text)) == ps2n::toLower(kFmvAspectNames[i])) return FmvAspectRatio(i);
    return std::nullopt;
}

double pictureAspect(AspectRatio setting, FmvAspectRatio fmvSetting, bool filmPlaying, bool progressive, const char* gameAspect) {
    if (filmPlaying && fmvSetting != FmvAspectRatio::Off && fmvSetting < FmvAspectRatio::Count)
        setting = AspectRatio(size_t(fmvSetting));
    return ratioOf(setting, progressive, gameAspect);
}

Rect fitPicture(int w, int h, double aspect) {
    if (aspect <= 0.0 || w <= 0 || h <= 0) return {0, 0, std::max(w, 0), std::max(h, 0)};
    int pw = w, ph = static_cast<int>(std::lround(w / aspect));
    if (ph > h) {
        ph = h;
        pw = std::min(w, static_cast<int>(std::lround(h * aspect)));
    }
    return {(w - pw) / 2, (h - ph) / 2, pw, ph};
}

void noteFilmDecode() {
    g_lastFilmDecode = nowCycles();
    if (++g_vdecs < 7) return;
    g_vdecs = 0;
    if (!g_film.load(std::memory_order_relaxed)) g_filmStarting = true;
}

bool filmPlaying() { return g_film; }

AspectRatio aspectRatio() { return g_aspect; }
FmvAspectRatio fmvAspectRatio() { return g_fmvAspect; }

void setAspectRatio(AspectRatio r) {
    g_aspect = g_saved.aspect = r;
    saveSetting("AspectRatio", aspectRatioName(r));
}

void setFmvAspectRatio(FmvAspectRatio r) {
    g_fmvAspect = g_saved.fmvAspect = r;
    saveSetting("FMVAspectRatioSwitch", fmvAspectRatioName(r));
}

BilinearMode bilinearMode() { return g_bilinear; }

void setBilinearMode(BilinearMode m) {
    g_bilinear = g_saved.bilinear = m;
    saveSetting("linear_present_mode", std::to_string(static_cast<int>(m)));
}

bool integerScaling() { return g_integerScaling; }

void setIntegerScaling(bool on) {
    g_integerScaling = g_saved.integerScaling = on;
    saveSetting("IntegerScaling", on ? "true" : "false");
}

Renderer rendererSetting() { return g_saved.renderer; }

void setRendererSetting(Renderer r) {
    g_saved.renderer = r;
    saveSetting("Renderer", std::to_string(static_cast<int>(r)));
}

int upscaleSetting() { return g_saved.upscale; }

void setUpscaleSetting(int n) {
    g_saved.upscale = std::clamp(n, 1, kMaxUpscale);
    saveSetting("upscale_multiplier", std::to_string(g_saved.upscale));
    gs::requestScale(g_saved.upscale);   // the card draws at it from the next vsync
}

std::string rendererNote() {
    if (g_saved.renderer != Renderer::Software && !gpu::available()) {
        const std::string why = gpu::status().reason;
        return "Vulkan cannot draw here" + (why.empty() ? std::string() : ": " + why) + ". The software renderer draws.";
    }
    if ((g_saved.renderer == Renderer::Software) == gs::vulkanDraws()) return "The renderer changes when the game starts again.";
    if (!gs::vulkanDraws()) return "The software renderer draws at the console's resolution.";
    return {};
}

void init(const DisplaySettings& settings) {
    g_saved = settings;
    g_aspect = settings.aspect;
    g_fmvAspect = settings.fmvAspect;
    g_bilinear = settings.bilinear;
    g_integerScaling = settings.integerScaling;
    g_frames = 0;
    g_film = false;
    g_filmStarting = false;
    g_vdecs = 0;
}

Present onVsync() {
    ++g_frames;
    if (std::exchange(g_filmStarting, false)) {
        g_film = true;
        PS2_DEBUG("video", "a film plays (the IPU decodes its bit stream)");
    } else if (g_film.load(std::memory_order_relaxed) && nowCycles() - g_lastFilmDecode > kFilmIdleCycles) {
        g_film = false;
        PS2_DEBUG("video", "the film ended (the IPU has decoded nothing for a while)");
    }
    pausePoint();
    if (g_windowClosed.exchange(false)) {
        PS2_INFO("video", "window closed");
        requestExit(0);
        return {};
    }
    Present p;
    p.index = g_frames;
    p.dump = !g_opt.dumpFrames.empty() && g_opt.dumpEvery && (g_frames % g_opt.dumpEvery) == 0;
    p.show = windowEnabled();
    return p;
}

void present(gs::Frame&& f, const Present& how) {
    if (how.dump) dumpPpm(f, how.index);
    if (how.show) {
        {
            const std::lock_guard<std::mutex> lock(g_frameMutex);
            g_pending = std::move(f);
            g_pendingPicture = GpuPicture{};
            g_hasPending = true;
        }
        g_frameReady.notify_one();
    }
}

void presentPicture(int slot, uint64_t ready, int width, int height) {
    {
        const std::lock_guard<std::mutex> lock(g_frameMutex);
        g_pendingPicture = GpuPicture{slot, ready, width, height};
        g_hasPending = true;
    }
    g_frameReady.notify_one();
}

void writeDump(const gs::Frame& f, uint64_t index) { dumpPpm(f, index); }

bool windowShowsGpuPictures() {
#ifdef PS2N_HAVE_SDL2
    return g_vulkanWindow;
#else
    return false;
#endif
}

void hostLoop(const std::atomic<bool>& guestDone) {
#ifndef PS2N_HAVE_SDL2
    (void)guestDone;
#else
    if (!g_opt.window) return;
    if (!openWindow()) return;
    audio::openDevice();   // sound plays where the picture shows
    g_windowOpen = true;
    bool menuWasOpen = false;
    while (!guestDone) {
        {
            // The newest picture, if the game made one. The wait also paces
            // the loop where the display's vsync does not.
            std::unique_lock<std::mutex> lock(g_frameMutex);
            g_frameReady.wait_for(lock, std::chrono::milliseconds(8), [] { return g_hasPending; });
            if (g_hasPending) {
                const GpuPicture picture = g_pendingPicture;
                gs::Frame f = std::move(g_pending);
                g_hasPending = false;
                lock.unlock();
                if (picture.slot >= 0) showPicture(picture);
                else show(std::move(f));
            }
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // Closing the window and controllers coming and going count
            // whatever the overlay is doing; the rest is the overlay's to use.
            if (ev.type == SDL_QUIT) g_windowClosed = true;
#ifdef PS2N_VULKAN_WINDOW
            if (g_vulkanWindow && ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) vulkan::resized();
#endif
            const std::string message = input::sdl::handleEvent(ev);
            if (!message.empty()) ui::osd(message);
            ui::processEvent(ev);
        }
        const input::HostState host = input::sdl::poll();
        // What was held when the menu closed (the Return that chose "Resume
        // Game") does not reach the game until it is released.
        const bool menuOpen = ui::menuOpen();
        if (menuWasOpen && !menuOpen) input::holdBack(host);
        menuWasOpen = menuOpen;
        for (const input::HotkeyEvent& e : input::update(host, !menuOpen, !ui::capturing())) hotkey(e);
        const ui::Action action = ui::takeAction();
        if (action == ui::Action::ToggleFullscreen) toggleFullscreen();
        else if (action == ui::Action::Quit) g_windowClosed = true;
        // Closing the window, or Ctrl+C, ends a paused game too.
        const bool paused = ui::menuOpen() || g_userPaused;
        setPaused(paused && !g_windowClosed && !g_interruptRequested);
        input::sdl::applyRumble(paused ? std::vector<input::Rumble>{} : input::rumble());
        ui::frame(host);
        render();
        if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) SDL_Delay(16);
    }
    setPaused(false);
    g_windowOpen = false;
#endif
}

void shutdown() {
#ifdef PS2N_HAVE_SDL2
    audio::closeDevice();
#ifdef PS2N_VULKAN_WINDOW
    if (g_vulkanWindow) vulkan::waitIdle();   // the last frame used the overlay's buffers
#endif
    ui::shutdown();
#ifdef PS2N_VULKAN_WINDOW
    vulkan::close();   // the surface goes before the window
#endif
    g_vulkanWindow = false;
    input::sdl::close();
    if (g_texture) SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    if (SDL_WasInit(0)) SDL_Quit();
    g_texture = nullptr;
    g_renderer = nullptr;
    g_window = nullptr;
    g_shown = {};
#endif
}

} // namespace ps2::video
