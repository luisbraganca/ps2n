// EE DMA controller. Transfers complete immediately when a channel is
// started; completion is then signalled through D_STAT (and the DMAC
// interrupt) exactly as on hardware, so code that polls or waits on
// interrupts behaves.
#pragma once

#include <cstdint>

namespace ps2::dmac {

void init();
uint32_t regRead(uint32_t addr);
void regWrite(uint32_t addr, uint32_t value);
uint32_t enableRegRead(uint32_t addr);
void enableRegWrite(uint32_t addr, uint32_t value);

uint32_t pending();     // channels whose CIS and CIM bits are both set
void ack(int channel);
void setMask(int channel, bool enabled);
void raise(int channel);
bool idle();            // CPCOND0

} // namespace ps2::dmac
