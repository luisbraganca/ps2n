// EE DMA controller. Most transfers complete when a channel is started;
// completion is then signalled through D_STAT (and the DMAC interrupt), so
// code that polls or waits on interrupts behaves. The IPU's channels move data
// only as fast as the IPU's FIFOs take or give it. A VIF channel stops at an i
// bit and goes on when the stall ends. Reverse VIF1 DMA runs through the GS
// thread: its completion reaches the EE at a safepoint or a channel read.
#pragma once

#include <cstdint>

namespace ps2::dmac {

void init();
// Guest thread: publishes any reverse VIF1 results the GS thread has finished.
void pollReadbacks();
// At shutdown, before the GS thread stops: finishes every queued result.
void finishReadbacks();
uint32_t regRead(uint32_t addr);
void regWrite(uint32_t addr, uint32_t value);
uint32_t enableRegRead(uint32_t addr);
void enableRegWrite(uint32_t addr, uint32_t value);

uint32_t pending();     // channels whose CIS and CIM bits are both set
void ack(int channel);
void setMask(int channel, bool enabled);
void raise(int channel);
bool idle();            // CPCOND0
void resetInterrupts(); // clears every D_STAT status and mask bit

// Channel 4 (toIPU): up to `max` qwords for the IPU's input FIFO; returns how many.
uint32_t toIpu(uint64_t* dst, uint32_t max);
bool toIpuReady();      // channel 4 is running and has data to give
// Channel 3 (fromIPU): takes up to `count` qwords of IPU output; returns how many.
uint32_t fromIpu(const uint64_t* src, uint32_t count);
// VIF `unit` is no longer stalled: its channel goes on if it was waiting.
void vifReady(int unit);

} // namespace ps2::dmac
