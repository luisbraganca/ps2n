#include "video/video.h"

#include "audio/audio.h"
#include "clock.h"
#include "hw/gs.h"
#include "hw/hw.h"
#include "input/input.h"
#include "internal.h"
#include "log.h"

#include "ps2n/util.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifdef PS2N_HAVE_SDL2
#include "input/sdl.h"
#include "ui/overlay.h"

#include <SDL.h>
#endif

namespace ps2::video {

namespace {

uint64_t g_frames = 0;
std::atomic<bool> g_windowOpen{false};     // the main thread shows frames
std::atomic<bool> g_windowClosed{false};

// The newest displayed frame, handed from the guest thread to the main thread.
std::mutex g_frameMutex;
std::condition_variable g_frameReady;
gs::Frame g_pending;
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
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
gs::Frame g_shown;   // the picture on screen, for screenshots

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

// Whatever the framebuffer's size, the PS2 drew for a 4:3 television: the
// picture keeps that shape, centred, and the rest of the window is black.
SDL_Rect pictureRect() {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    int pw = w, ph = w * 3 / 4;
    if (ph > h) {
        ph = h;
        pw = h * 4 / 3;
    }
    return {(w - pw) / 2, (h - ph) / 2, pw, ph};
}

void render() {
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    if (g_texture) {
        const SDL_Rect r = pictureRect();
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
    if (!g_texture || f.width != g_shown.width || f.height != g_shown.height) {
        if (g_texture) SDL_DestroyTexture(g_texture);
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, f.width, f.height);
    }
    SDL_UpdateTexture(g_texture, nullptr, f.pixels.data(), f.width * 4);
    g_shown = std::move(f);
}

void toggleFullscreen() {
    const bool full = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(g_window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_ShowCursor(full ? SDL_ENABLE : SDL_DISABLE);
}

void applySpeed() { clockSetNoThrottle(g_opt.noThrottle || g_limiterOff || g_turbo || g_turboHeld); }

// The picture on screen, as a BMP in ./snaps (PCSX2 keeps its in "snaps" too).
void screenshot() {
    if (g_shown.pixels.empty()) return;
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    std::error_code ec;
    const auto dir = std::filesystem::current_path(ec) / "snaps";
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / ps2n::strprintf("%s_%s.bmp", g_game ? g_game->name : "ps2n", stamp);
    SDL_Surface* rgba = SDL_CreateRGBSurfaceWithFormatFrom(g_shown.pixels.data(), g_shown.width, g_shown.height, 32,
                                                           g_shown.width * 4, SDL_PIXELFORMAT_ABGR8888);
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
    // 4:3, three quarters of the screen's height.
    SDL_Rect usable{0, 0, 1280, 960};
    SDL_GetDisplayUsableBounds(0, &usable);
    const int h = std::max(480, usable.h * 3 / 4);
    const std::string title = std::string(g_game ? g_game->name : "ps2n") + " (PS2Native)";
    g_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, h * 4 / 3, h, SDL_WINDOW_RESIZABLE);
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

} // namespace

void init() { g_frames = 0; }

void onVsync() {
    ++g_frames;
    pausePoint();
    if (g_windowClosed.exchange(false)) {
        PS2_INFO("video", "window closed");
        requestExit(0);
        return;
    }
    const bool dump = !g_opt.dumpFrames.empty() && g_opt.dumpEvery && (g_frames % g_opt.dumpEvery) == 0;
    const bool show = windowEnabled();
    if (!dump && !show) return;
    gs::Frame f;
    if (!gs::currentFrame(f)) return;
    if (dump) dumpPpm(f, g_frames);
    if (show) {
        {
            const std::lock_guard<std::mutex> lock(g_frameMutex);
            g_pending = std::move(f);
            g_hasPending = true;
        }
        g_frameReady.notify_one();
    }
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
                gs::Frame f = std::move(g_pending);
                g_hasPending = false;
                lock.unlock();
                show(std::move(f));
            }
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // Closing the window and controllers coming and going count
            // whatever the overlay is doing; the rest is the overlay's to use.
            if (ev.type == SDL_QUIT) g_windowClosed = true;
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
    ui::shutdown();
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
