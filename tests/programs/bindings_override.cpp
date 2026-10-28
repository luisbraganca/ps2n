// [functions] override target for bindings.s. The generated wrapper has already
// set cpu.pc to the return address; arguments are in $a0-$a3, the result goes
// to $v0.
#include "ps2/recomp.h"

using namespace ps2;
using namespace ps2::regs;

void triple_override() { setU32(V0, ru32(A0) * 100); }
