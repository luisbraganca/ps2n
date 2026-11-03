// VIF0/VIF1: command streams that feed the vector units and (VIF1) the GS.
// A VIF code with the i bit stalls its VIF until the EE writes FBRST.STC; the
// words that arrive meanwhile wait in its FIFO.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::vif {

void init();
uint32_t regRead(uint32_t addr);
void regWrite(uint32_t addr, uint32_t value);
void fifoWrite(int unit, const uint64_t q[2]);
// DMA into the VIF: returns how many quadwords it took. Short of `count` when
// the VIF stalled: the rest of the quadword it stalled in waits in its FIFO.
size_t transfer(int unit, const uint64_t* qwords, size_t count);
// Upper 64 bits of a DMA tag with TTE set: two VIF codes.
void transferTagWords(int unit, const uint64_t q[2]);
bool stalled(int unit);   // stopped by an i bit (STAT.VIS) until FBRST.STC
// What the VU's XTOP / XITOP read.
uint32_t top(int unit);
uint32_t itop(int unit);

} // namespace ps2::vif
