// Host sound: where the SPU2's output (iop/spu2.h) goes. In the window (SDL
// builds) it plays through the default output device, with the volume,
// mute and buffering ps2n.ini sets in [SPU2/Output] (PCSX2's keys). With
// --dump-audio it is also written to a WAV file, as the SPU2 made it.
//
// The SPU2 mixes on the guest thread in emulated time, which in real time
// keeps pace with the device: a ring buffer between them absorbs the guest's
// bursts, drops what runs ahead (fast forward) and plays silence when the
// guest falls behind.
#pragma once

namespace ps2::audio {

void init();          // guest start-up: takes the SPU2's output; opens --dump-audio
void shutdown();      // after the guest finished: completes the WAV file

// Main thread: the output device, once the window is up and when it closes.
void openDevice();
void closeDevice();

// Main thread: the volume the hotkeys change (PCSX2's percent, 0-200), and
// mute. False when ps2n.ini mutes the output, which they cannot override.
int volume();
bool setVolume(int percent);
bool muted();
bool setMuted(bool on);

} // namespace ps2::audio
