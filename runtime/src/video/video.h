// Presents GS output: optional SDL2 window, optional PPM frame dumps.
//
// The guest runs on a worker thread. The window lives on the main thread
// (macOS requires that for windows and events), which runs hostLoop() while
// the guest runs: it shows the frames present() hands over, reads the host's
// input (input/), draws the overlay (ui/) and pauses the guest at its next
// vsync while the menu is open or the Toggle Pause hotkey says so.
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ps2::gs {
struct Frame;
}
namespace ps2::settings {
class Ini;
}

namespace ps2::video {

// Who draws the GS's pictures: PCSX2's Renderer setting, with its values.
// Automatic is Vulkan where the graphics card can, else Software.
enum class Renderer : int { Auto = -1, Software = 13, Vulkan = 14 };
const char* rendererName(Renderer r);   // PCSX2's labels: "Automatic (Default)", "Software", "Vulkan"
// --renderer's values: auto, software, vulkan.
std::optional<Renderer> parseRendererOption(std::string_view text);

// PCSX2's Bilinear Filtering of the picture in the window (linear_present_mode).
enum class BilinearMode : uint8_t { Off, Smooth, Sharp, Count };
const char* bilinearModeName(BilinearMode m);   // "Off", "Bilinear (Smooth)", "Bilinear (Sharp)"

constexpr int kMaxUpscale = 8;
// PCSX2's Internal Resolution labels: "1x Native (PS2)" ... "8x Native (~2880p/5K)".
const char* upscaleName(int n);

// The shape of the picture in the window: PCSX2's AspectRatio setting,
// [EmuCore/GS] in the settings file. Auto is 4:3 (3:2 for a progressive
// picture), unless the game's patches ask for another ([pnach], gsaspectratio).
enum class AspectRatio : uint8_t { Stretch, Auto, R4_3, R16_9, R10_7, Count };
// PCSX2's FMVAspectRatioSwitch: the shape while a film plays, which shows by
// the IPU decoding; Off keeps AspectRatio's.
enum class FmvAspectRatio : uint8_t { Off, Auto, R4_3, R16_9, R10_7, Count };
const char* aspectRatioName(AspectRatio r);          // as the settings file spells it: "Auto 4:3/3:2"
const char* fmvAspectRatioName(FmvAspectRatio r);
std::optional<AspectRatio> parseAspectRatio(std::string_view text);
std::optional<FmvAspectRatio> parseFmvAspectRatio(std::string_view text);

// Width over height of the picture on screen, or 0 to fill the window
// (Stretch). `gameAspect`: what the game's patches ask for ("16:9", any N:D),
// or null; it counts under Auto, as in PCSX2, the FMV setting's Auto too.
double pictureAspect(AspectRatio setting, FmvAspectRatio fmvSetting, bool filmPlaying, bool progressive,
                     const char* gameAspect);
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};
// The largest rectangle of that shape centred in a w x h window; the whole
// window for 0.
Rect fitPicture(int w, int h, double aspect);

// Guest thread: the IPU ran a VDEC, decoding a film's bit stream. As in
// PCSX2, every seventh starts a film at the next vsync, and a film ends at
// the first vsync a fifth of a second after the last VDEC.
void noteFilmDecode();
bool filmPlaying();   // any thread

// The display settings in the settings file, [EmuCore/GS] as PCSX2 names
// them: Renderer, upscale_multiplier, linear_present_mode, IntegerScaling,
// AspectRatio, FMVAspectRatioSwitch. A value that cannot be used becomes the
// default, or the nearest whole multiplier from 1 to 8, with a warning.
struct DisplaySettings {
    Renderer renderer = Renderer::Auto;
    int upscale = 1;
    BilinearMode bilinear = BilinearMode::Smooth;
    bool integerScaling = false;
    AspectRatio aspect = AspectRatio::Auto;
    FmvAspectRatio fmvAspect = FmvAspectRatio::Off;
};
DisplaySettings readDisplaySettings(const settings::Ini& ini, std::vector<std::string>* warnings);
void writeDisplaySettings(settings::Ini& ini, const DisplaySettings& s);

// Integer scaling: `fitted` (fitPicture's rectangle) shrunk to the largest
// whole multiple of the picture's height, keeping its shape, centred in the
// window; `fitted` itself when not even 1x fits.
Rect integerRect(const Rect& fitted, int windowW, int windowH, int pictureH);

// Main thread: the window's settings, as the runtime read them at start-up.
// The setters save them in the settings file.
AspectRatio aspectRatio();
void setAspectRatio(AspectRatio r);
FmvAspectRatio fmvAspectRatio();
void setFmvAspectRatio(FmvAspectRatio r);
BilinearMode bilinearMode();
void setBilinearMode(BilinearMode m);
bool integerScaling();
void setIntegerScaling(bool on);
// The saved choice, and what draws in this run (a --renderer or --upscale
// on the command line holds for the run, unless the settings page changes it).
Renderer rendererSetting();
void setRendererSetting(Renderer r);
int upscaleSetting();
void setUpscaleSetting(int n);

// What becomes of one vsync's picture.
struct Present {
    uint64_t index = 0;   // the vsync's number, which names its dump
    bool dump = false;
    bool show = false;
};

// The settings the runtime read (with the command line's over them).
void init(const DisplaySettings& settings = {});
Present onVsync();   // guest thread, every vsync: pauses there while asked to
// Any thread (the GS's, when it has one): dumps and shows a vsync's picture.
void present(gs::Frame&& frame, const Present& how);
// The Vulkan renderer's side: a picture it drew into `slot` of its pictures
// (gpu/picture.h), complete once its timeline reaches `ready`. Only a window
// that shows its pictures through Vulkan takes them; the renderer reads a
// picture back for --dump-frames itself and writes it with writeDump.
void presentPicture(int slot, uint64_t ready, int width, int height);
bool windowShowsGpuPictures();   // any thread
void writeDump(const gs::Frame& frame, uint64_t index);
// For the settings page: who draws now, where that is not what the settings
// say (no graphics card, a choice that applies at the next start); else empty.
std::string rendererNote();
// Main thread: shows frames and handles window events until `guestDone`.
// Returns at once when there is no window.
void hostLoop(const std::atomic<bool>& guestDone);
void shutdown();    // main thread, after the guest finished

} // namespace ps2::video
