// EE-side hardware: register file routing, INTC, timers, DMAC, GS, VIF, SIO.
#pragma once

#include <cstdint>

namespace ps2::hw {

enum IntcCause : int {
    INTC_GS = 0, INTC_SBUS = 1, INTC_VBLANK_START = 2, INTC_VBLANK_END = 3, INTC_VIF0 = 4, INTC_VIF1 = 5,
    INTC_VU0 = 6, INTC_VU1 = 7, INTC_IPU = 8, INTC_TIM0 = 9, INTC_TIM1 = 10, INTC_TIM2 = 11, INTC_TIM3 = 12,
    INTC_SFIFO = 13, INTC_VU0WD = 14,
};

enum DmaChannel : int {
    DMA_VIF0 = 0, DMA_VIF1 = 1, DMA_GIF = 2, DMA_IPU_FROM = 3, DMA_IPU_TO = 4, DMA_SIF0 = 5, DMA_SIF1 = 6,
    DMA_SIF2 = 7, DMA_SPR_FROM = 8, DMA_SPR_TO = 9,
};

void init();

// Register access for physical addresses 0x10000000-0x1000FFFF (EE registers)
// and 0x12000000-0x12001FFF (GS privileged registers).
uint32_t read32(uint32_t phys);
void write32(uint32_t phys, uint32_t value);
uint64_t read64(uint32_t phys);
void write64(uint32_t phys, uint64_t value);
void read128(uint32_t phys, uint64_t out[2]);
void write128(uint32_t phys, const uint64_t value[2]);
uint8_t read8(uint32_t phys);
void write8(uint32_t phys, uint8_t value);
uint16_t read16(uint32_t phys);
void write16(uint32_t phys, uint16_t value);

// INTC
void raiseIntc(int cause);
uint32_t intcPending();       // I_STAT & I_MASK
void ackIntc(int cause);
void setIntcMask(int cause, bool enabled);
// Clears the INTC and DMAC interrupt status and masks (a new program starts).
void resetInterrupts();

// DMAC
uint32_t dmacPending();       // channels with CIS and CIM set
void ackDmac(int channel);
void setDmacMask(int channel, bool enabled);
void dmacRaise(int channel);  // marks a channel's transfer complete
bool dmacIdle();              // for BC0T/BC0F

// Timers / vsync
void startVsync();
bool isPal();
void setVideoMode(bool interlace, int mode, bool field);
uint64_t vsyncCount();
uint64_t hsyncCycles();       // EE cycles per scanline

} // namespace ps2::hw
