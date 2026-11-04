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
#include <string_view>

namespace ps2::gs {
struct Frame;
}

namespace ps2::video {

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

// Main thread: the window's settings, read from the settings file when the
// window opens. The setters save them there.
AspectRatio aspectRatio();
void setAspectRatio(AspectRatio r);
FmvAspectRatio fmvAspectRatio();
void setFmvAspectRatio(FmvAspectRatio r);

// What becomes of one vsync's picture.
struct Present {
    uint64_t index = 0;   // the vsync's number, which names its dump
    bool dump = false;
    bool show = false;
};

void init();
Present onVsync();   // guest thread, every vsync: pauses there while asked to
// Any thread (the GS's, when it has one): dumps and shows a vsync's picture.
void present(gs::Frame&& frame, const Present& how);
// Main thread: shows frames and handles window events until `guestDone`.
// Returns at once when there is no window.
void hostLoop(const std::atomic<bool>& guestDone);
void shutdown();    // main thread, after the guest finished

} // namespace ps2::video
