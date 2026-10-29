// The IPU, the EE's MPEG-2 decoder: registers 0x10002000-0x10002030 and the
// FIFOs at 0x10007000 (out) and 0x10007010 (in). DMA channel 4 (toIPU) feeds
// the input FIFO and channel 3 (fromIPU) drains the output FIFO.
//
// Commands run as soon as their input is there. Each one is a series of steps
// (one macroblock, one matrix, one code) that read the bitstream through a
// cursor and commit only when they finish, so a step that runs out of data
// simply starts over when more arrives; the FIFO registers then show where the
// hardware would have stopped.
#pragma once

#include <cstdint>

namespace ps2::ipu {

void init();
uint32_t regRead(uint32_t addr);
void regWrite(uint32_t addr, uint32_t value);
void fifoRead(uint64_t out[2]);
void fifoWrite(const uint64_t q[2]);
// Moves data and runs commands as far as the FIFOs and DMA channels allow.
// The DMAC calls it when channel 3 or 4 starts or resumes.
void run();

} // namespace ps2::ipu
