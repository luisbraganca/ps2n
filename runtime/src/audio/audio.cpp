#include "audio/audio.h"

#include "clock.h"
#include "internal.h"
#include "iop/spu2.h"
#include "log.h"

#include "ps2n/util.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#ifdef PS2N_HAVE_SDL2
#include "input/input.h"
#include "settings.h"

#include <SDL.h>
#endif

namespace ps2::audio {

namespace spu2 = iop::spu2;

namespace {

// ---- --dump-audio ----------------------------------------------------------------------------
FILE* g_wav = nullptr;
uint64_t g_wavFrames = 0, g_wavHeaderFrames = 0;

void putLe(uint8_t* p, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// The RIFF header for `frames` frames of 16-bit stereo at 48 kHz.
void writeWavHeader(uint64_t frames) {
    const auto data = static_cast<uint32_t>(std::min<uint64_t>(frames * 4, 0xFFFFFFFFu - 36));
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                     0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    putLe(h + 4, 36 + data, 4);
    putLe(h + 24, spu2::kSampleRate, 4);
    putLe(h + 28, spu2::kSampleRate * 4, 4);
    putLe(h + 40, data, 4);
    std::fseek(g_wav, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof h, g_wav);
    std::fseek(g_wav, 0, SEEK_END);
    g_wavHeaderFrames = frames;
}

void dump(const int16_t* lr, size_t frames) {
    std::vector<uint8_t> bytes(frames * 4);
    for (size_t i = 0; i < frames * 2; ++i) putLe(&bytes[i * 2], static_cast<uint16_t>(lr[i]), 2);
    std::fwrite(bytes.data(), 1, bytes.size(), g_wav);
    g_wavFrames += frames;
    // Once a second, so that a run cut short still leaves a playable file.
    if (g_wavFrames - g_wavHeaderFrames >= spu2::kSampleRate) writeWavHeader(g_wavFrames);
}

// ---- The device's ring buffer ------------------------------------------------------------------
// One writer (the guest thread) and one reader (SDL's audio thread).
constexpr uint32_t kRingFrames = 1u << 15;   // 0.68 s
std::vector<uint32_t> g_ring(kRingFrames);   // left | right << 16
std::atomic<uint64_t> g_written{0}, g_read{0};
std::atomic<bool> g_live{false};             // a device is reading
std::atomic<int> g_volume{100};
std::atomic<bool> g_muted{false};
bool g_mutedInSettings = false;

void push(const int16_t* lr, size_t frames) {
    if (!g_live.load(std::memory_order_acquire)) return;
    uint64_t w = g_written.load(std::memory_order_relaxed);
    const uint64_t r = g_read.load(std::memory_order_acquire);
    // What does not fit is dropped: the game ran ahead of the device.
    const size_t n = std::min<size_t>(frames, kRingFrames - static_cast<size_t>(w - r));
    for (size_t i = 0; i < n; ++i, ++w)
        g_ring[w & (kRingFrames - 1)] =
            static_cast<uint16_t>(lr[2 * i]) | (static_cast<uint32_t>(static_cast<uint16_t>(lr[2 * i + 1])) << 16);
    g_written.store(w, std::memory_order_release);
}

void sink(const int16_t* lr, size_t frames) {
    if (g_wav) dump(lr, frames);
    push(lr, frames);
}

#ifdef PS2N_HAVE_SDL2
SDL_AudioDeviceID g_device = 0;
std::atomic<int> g_fastForwardVolume{100};
uint32_t g_target = 2400;   // frames buffered before the device plays: BufferMS
bool g_playing = false;     // audio thread only

int16_t scale(int16_t s, int percent) { return static_cast<int16_t>(std::clamp(s * percent / 100, -0x8000, 0x7FFF)); }

void SDLCALL pull(void*, Uint8* stream, int len) {
    auto* out = reinterpret_cast<int16_t*>(stream);
    const size_t frames = static_cast<size_t>(len) / 4;
    uint64_t r = g_read.load(std::memory_order_relaxed);
    const uint64_t w = g_written.load(std::memory_order_acquire);
    uint64_t have = w - r;
    if (have > 2u * g_target) {   // after fast forward or a burst: keep the newest
        r = w - g_target;
        have = g_target;
    }
    // Play once the buffer holds its target, again after running dry, so that
    // a short gap is one gap and not a stutter.
    if (!g_playing && have >= g_target) g_playing = true;
    size_t n = 0;
    if (g_playing) {
        n = static_cast<size_t>(std::min<uint64_t>(frames, have));
        const int percent = g_muted ? 0 : clockNoThrottle() ? g_fastForwardVolume.load() : g_volume.load();
        for (size_t i = 0; i < n; ++i) {
            const uint32_t s = g_ring[(r + i) & (kRingFrames - 1)];
            out[2 * i] = scale(static_cast<int16_t>(s), percent);
            out[2 * i + 1] = scale(static_cast<int16_t>(s >> 16), percent);
        }
        if (n < frames) g_playing = false;
    }
    std::fill(out + 2 * n, out + 2 * frames, int16_t(0));
    g_read.store(r + n, std::memory_order_release);
}

int setting(const settings::Ini& ini, const char* key, int fallback, int lo, int hi) {
    return std::clamp(static_cast<int>(std::lround(ini.getFloat("SPU2/Output", key, static_cast<float>(fallback)))), lo, hi);
}
#endif

} // namespace

void init() {
    spu2::setSink(&sink);
    if (g_opt.dumpAudio.empty()) return;
    std::error_code ec;
    if (g_opt.dumpAudio.has_parent_path()) std::filesystem::create_directories(g_opt.dumpAudio.parent_path(), ec);
    g_wav = std::fopen(g_opt.dumpAudio.string().c_str(), "wb");
    if (!g_wav) {
        PS2_WARN("audio", "cannot write %s", g_opt.dumpAudio.string().c_str());
        return;
    }
    g_wavFrames = 0;
    writeWavHeader(0);
}

void shutdown() {
    if (!g_wav) return;
    writeWavHeader(g_wavFrames);
    std::fclose(g_wav);
    g_wav = nullptr;
    PS2_INFO("audio", "wrote %.1f s of sound to %s", static_cast<double>(g_wavFrames) / spu2::kSampleRate,
             g_opt.dumpAudio.string().c_str());
}

void openDevice() {
#ifdef PS2N_HAVE_SDL2
    const settings::Ini& ini = input::settingsFile();
    // PCSX2's default is Cubeb, which is not built in: anything but Null plays through SDL.
    if (ps2n::toLower(ini.getString("SPU2/Output", "Backend", "Cubeb")) == "null") {
        PS2_INFO("audio", "[SPU2/Output] Backend = Null: no sound");
        return;
    }
    g_volume = setting(ini, "StandardVolume", 100, 0, 200);
    g_fastForwardVolume = setting(ini, "FastForwardVolume", 100, 0, 200);
    g_mutedInSettings = ini.getBool("SPU2/Output", "OutputMuted", false);
    g_muted = g_mutedInSettings;
    const int bufferMs = setting(ini, "BufferMS", 50, 10, 500);
    const int latencyMs = setting(ini, "OutputLatencyMS", 20, 1, 500);
    const bool minimal = ini.getBool("SPU2/Output", "OutputLatencyMinimal", false);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        PS2_WARN("audio", "no sound: SDL cannot start its audio (%s)", SDL_GetError());
        return;
    }
    // The device's own buffer: OutputLatencyMS, in a power of two of frames.
    Uint16 frames = 256;
    while (!minimal && frames < 8192 && frames < latencyMs * 48) frames = static_cast<Uint16>(frames * 2);
    SDL_AudioSpec want{}, have{};
    want.freq = static_cast<int>(spu2::kSampleRate);
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = frames;
    want.callback = &pull;
    g_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // SDL converts to what the device takes
    if (!g_device) {
        PS2_WARN("audio", "no sound: no audio output device (%s)", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    g_target = static_cast<uint32_t>(bufferMs) * 48;
    g_playing = false;
    g_read = g_written.load();
    g_live = true;
    SDL_PauseAudioDevice(g_device, 0);
    PS2_INFO("audio", "sound through SDL (%s), %u-frame device buffer, %d ms buffered", SDL_GetCurrentAudioDriver(), have.samples,
             bufferMs);
#endif
}

void closeDevice() {
#ifdef PS2N_HAVE_SDL2
    if (!g_device) return;
    g_live = false;
    SDL_CloseAudioDevice(g_device);
    g_device = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
}

int volume() { return g_volume; }

bool setVolume(int percent) {
    if (g_mutedInSettings) return false;
    g_volume = std::clamp(percent, 0, 200);
    g_muted = false;   // as in PCSX2, changing the volume unmutes
    return true;
}

bool muted() { return g_muted; }

bool setMuted(bool on) {
    if (g_mutedInSettings) return false;
    g_muted = on;
    return true;
}

} // namespace ps2::audio
