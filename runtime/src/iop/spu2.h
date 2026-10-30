// The SPU2, the IOP's sound processor: two cores of 24 voices each, which
// play ADPCM samples from 2 MB of sound memory with envelopes, pitch and
// volume sweeps, mixed with streamed PCM (AutoDMA) and reverb into one
// 48 kHz stereo output. Core 0's output feeds core 1, whose output is heard.
//
// It runs in emulated time: sync() mixes every sample due up to now, and an
// event keeps it going while nothing touches it. Its registers are the
// console's, at 0x1F900000 (IOP physical). The libsd stand-in (sound.h)
// drives them as Sony's library does, and IOP code may use them directly.
// Interrupts reach the IOP kernel as line 9 (an address the SPU2 touched)
// and lines 0x24/0x28 (a core's DMA finished; sound.cpp raises those).
#pragma once

#include <cstdint>
#include <cstddef>

namespace ps2::iop::spu2 {

constexpr uint32_t kRamBytes = 2u << 20;
constexpr uint32_t kSampleRate = 48000;
constexpr uint64_t kEeCyclesPerSample = 6144;   // 768 IOP cycles

void init();          // power-on: memory, registers and voices cleared
void onIopReboot();   // the IOP restarted: its DMA stops, the voices play on
void sync();          // mixes every sample due by now

// Registers, by IOP physical address (0x1F900000-0x1F9007FF).
uint16_t read16(uint32_t phys);
void write16(uint32_t phys, uint16_t value);

// Sound memory by byte address, as a core's DMA moves it: wraps at 2 MB and
// raises the interrupt when it passes a core's interrupt address.
void writeMemory(uint32_t addr, const uint8_t* src, uint32_t size);
void readMemory(uint32_t addr, uint8_t* dst, uint32_t size);
// Plain access for set-up and tests: no interrupt check.
uint8_t* ram();

// AutoDMA: a core's PCM input reads 512 bytes of left and 512 bytes of right
// samples from IOP memory for every 256 samples it plays. The transfer's
// last block taken calls the done hook (sound.cpp: the next half of a
// looping buffer, the transfer interrupt).
void startAutoDma(int core, uint32_t iopAddr, uint32_t size);
void stopAutoDma(int core);         // the input area is cleared, as writing ADMAS 0 does
bool autoDmaActive(int core);
// The IOP address it reads next: once done, the end of the transfer, as the
// DMA channel's address register shows it; 0 when stopped.
uint32_t autoDmaAddress(int core);
using AutoDmaDone = void (*)(int core);
void setAutoDmaDone(AutoDmaDone fn);

// Where the finished samples go: interleaved left/right, 48 kHz.
using Sink = void (*)(const int16_t* samples, size_t frames);
void setSink(Sink sink);

// Tests: mixes `samples` samples now, whatever the time.
void runSamples(uint32_t samples);
// One 16-byte ADPCM block (as eight words) to 28 samples; prev1 and prev2
// carry the prediction from block to block, as a voice's do.
void decodeAdpcm(const uint16_t block[8], int32_t& prev1, int32_t& prev2, int16_t samples[28]);

} // namespace ps2::iop::spu2
