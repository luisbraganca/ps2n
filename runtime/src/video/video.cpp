#include "video/video.h"

#include "hw/gs.h"
#include "hw/hw.h"
#include "internal.h"
#include "log.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifdef PS2N_HAVE_SDL2
#include <SDL.h>
#endif

namespace ps2::video {

namespace {

uint64_t g_frames = 0;
std::atomic<bool> g_windowClosed{false};

// Controller state, written by the main thread, read by the guest thread.
std::mutex g_padMutex;
std::array<HostPad, 2> g_pads;

// The newest displayed frame, handed from the guest thread to the main thread.
std::mutex g_frameMutex;
std::condition_variable g_frameReady;
gs::Frame g_pending;
bool g_hasPending = false;

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
int g_texW = 0, g_texH = 0;
bool g_sdlFailed = false;

std::vector<SDL_GameController*> g_controllers;

uint8_t axisByte(SDL_GameController* c, SDL_GameControllerAxis a) {
    return static_cast<uint8_t>((SDL_GameControllerGetAxis(c, a) + 32768) >> 8);
}

HostPad readController(SDL_GameController* c) {
    HostPad p;
    p.connected = true;
    static const std::pair<SDL_GameControllerButton, uint16_t> map[] = {
        {SDL_CONTROLLER_BUTTON_BACK, 0x0001}, {SDL_CONTROLLER_BUTTON_LEFTSTICK, 0x0002},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0x0004}, {SDL_CONTROLLER_BUTTON_START, 0x0008},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, 0x0010}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0x0020},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0x0040}, {SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0x0080},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0x0400}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0x0800},
        {SDL_CONTROLLER_BUTTON_Y, 0x1000}, {SDL_CONTROLLER_BUTTON_B, 0x2000},
        {SDL_CONTROLLER_BUTTON_A, 0x4000}, {SDL_CONTROLLER_BUTTON_X, 0x8000},
    };
    for (const auto& [b, bit] : map)
        if (SDL_GameControllerGetButton(c, b)) p.buttons |= bit;
    p.lx = axisByte(c, SDL_CONTROLLER_AXIS_LEFTX);
    p.ly = axisByte(c, SDL_CONTROLLER_AXIS_LEFTY);
    p.rx = axisByte(c, SDL_CONTROLLER_AXIS_RIGHTX);
    p.ry = axisByte(c, SDL_CONTROLLER_AXIS_RIGHTY);
    p.l2 = static_cast<uint8_t>(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
    p.r2 = static_cast<uint8_t>(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
    if (p.l2 > 0x40) p.buttons |= 0x0100;
    if (p.r2 > 0x40) p.buttons |= 0x0200;
    return p;
}

void pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) g_windowClosed = true;
        if (ev.type == SDL_CONTROLLERDEVICEADDED) {
            if (SDL_GameController* c = SDL_GameControllerOpen(ev.cdevice.which)) {
                g_controllers.push_back(c);
                PS2_INFO("video", "game controller \"%s\" is port %zu", SDL_GameControllerName(c), g_controllers.size());
            }
        }
        if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
            for (auto it = g_controllers.begin(); it != g_controllers.end(); ++it) {
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it)) == ev.cdevice.which) {
                    SDL_GameControllerClose(*it);
                    g_controllers.erase(it);
                    break;
                }
            }
        }
    }
    // Keyboard, on port 0 (PS2 pad bit order).
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    uint16_t keys = 0;
    auto set = [&](int bit, SDL_Scancode sc) { if (k[sc]) keys |= static_cast<uint16_t>(1u << bit); };
    set(0, SDL_SCANCODE_BACKSPACE);   // SELECT
    set(3, SDL_SCANCODE_RETURN);      // START
    set(4, SDL_SCANCODE_UP);
    set(5, SDL_SCANCODE_RIGHT);
    set(6, SDL_SCANCODE_DOWN);
    set(7, SDL_SCANCODE_LEFT);
    set(8, SDL_SCANCODE_1);           // L2
    set(9, SDL_SCANCODE_3);           // R2
    set(10, SDL_SCANCODE_Q);          // L1
    set(11, SDL_SCANCODE_E);          // R1
    set(12, SDL_SCANCODE_W);          // TRIANGLE
    set(13, SDL_SCANCODE_D);          // CIRCLE
    set(14, SDL_SCANCODE_S);          // CROSS
    set(15, SDL_SCANCODE_A);          // SQUARE
    std::array<HostPad, 2> pads;
    pads[0].connected = true;
    for (size_t i = 0; i < pads.size() && i < g_controllers.size(); ++i) pads[i] = readController(g_controllers[i]);
    pads[0].buttons |= keys;
    const std::lock_guard<std::mutex> lock(g_padMutex);
    g_pads = pads;
}

void present(const gs::Frame& f) {
    if (g_sdlFailed) return;
    if (!g_window) {
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");   // keep our Ctrl+C handling
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            PS2_WARN("video", "SDL_Init failed (%s); running headless", SDL_GetError());
            g_sdlFailed = true;
            return;
        }
        const std::string title = std::string(g_game ? g_game->name : "ps2n") + " (PS2Native)";
        g_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 448,
                                    SDL_WINDOW_RESIZABLE);
        g_renderer = g_window ? SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
        if (!g_renderer) {
            PS2_WARN("video", "cannot create an SDL window (%s); running headless", SDL_GetError());
            g_sdlFailed = true;
            return;
        }
    }
    if (f.width != g_texW || f.height != g_texH) {
        if (g_texture) SDL_DestroyTexture(g_texture);
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, f.width, f.height);
        g_texW = f.width;
        g_texH = f.height;
    }
    SDL_UpdateTexture(g_texture, nullptr, f.pixels.data(), f.width * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}
#endif

bool windowEnabled() {
#ifdef PS2N_HAVE_SDL2
    return g_opt.window;
#else
    return false;
#endif
}

} // namespace

void init() {
    g_frames = 0;
    const std::lock_guard<std::mutex> lock(g_padMutex);
    g_pads = {};
    g_pads[0].connected = true;   // a controller in the first port, nothing pressed, until the host says otherwise
}

void onVsync() {
    ++g_frames;
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
    while (!guestDone) {
        gs::Frame f;
        bool have = false;
        {
            std::unique_lock<std::mutex> lock(g_frameMutex);
            // The timeout keeps window events flowing while the guest draws nothing.
            g_frameReady.wait_for(lock, std::chrono::milliseconds(16), [] { return g_hasPending; });
            if (g_hasPending) {
                f = std::move(g_pending);
                g_hasPending = false;
                have = true;
            }
        }
        if (have) present(f);
        if (g_window) pumpEvents();
    }
#endif
}

void shutdown() {
#ifdef PS2N_HAVE_SDL2
    if (g_texture) SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    if (g_window) SDL_Quit();
    g_texture = nullptr;
    g_renderer = nullptr;
    g_window = nullptr;
#endif
}

HostPad hostPad(int port) {
    if (port < 0 || port > 1) return HostPad{};
    const std::lock_guard<std::mutex> lock(g_padMutex);
    return g_pads[static_cast<size_t>(port)];
}

} // namespace ps2::video
