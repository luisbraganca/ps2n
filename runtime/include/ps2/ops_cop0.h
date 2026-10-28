// COP0 (system control) instructions. Reads and writes of registers with side
// effects (Count, Compare, Status) go through the runtime.
#pragma once

#include "ps2/cpu.h"

#include <cstdint>

namespace ps2 {

uint32_t cop0Read(uint32_t reg);
void cop0Write(uint32_t reg, uint32_t value);
uint32_t perfCounterRead(uint32_t reg);
void perfCounterWrite(uint32_t reg, uint32_t value);
void tlbOp(const char* name);
bool cpcond0();   // BC0T/BC0F condition: all DMA channels named in D_PCR are idle

namespace ops {

inline void MFC0(uint32_t rt, uint32_t rd) { setS32(rt, static_cast<int32_t>(cop0Read(rd))); }
inline void MTC0(uint32_t rt, uint32_t rd) { cop0Write(rd, ru32(rt)); }
inline void MFPS(uint32_t rt, uint32_t) { setS32(rt, static_cast<int32_t>(cpu.pccr)); }
inline void MTPS(uint32_t rt, uint32_t) { cpu.pccr = ru32(rt); }
inline void MFPC(uint32_t rt, uint32_t reg) { setS32(rt, static_cast<int32_t>(perfCounterRead(reg))); }
inline void MTPC(uint32_t rt, uint32_t reg) { perfCounterWrite(reg, ru32(rt)); }
inline void EI() { cpu.cop0[COP0_STATUS] |= 0x00010000u; }
inline void DI() { cpu.cop0[COP0_STATUS] &= ~0x00010000u; }
inline void TLBR() { tlbOp("tlbr"); }
inline void TLBWI() { tlbOp("tlbwi"); }
inline void TLBWR() { tlbOp("tlbwr"); }
inline void TLBP() { tlbOp("tlbp"); }

} // namespace ops
} // namespace ps2
