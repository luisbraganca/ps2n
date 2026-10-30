// libsd, the library IOP sound drivers use to drive the SPU2 (spu2.h), as
// host code: its calls set the SPU2's registers as Sony's library does, its
// transfers complete with the library's interrupts, and the drivers'
// handlers run from those.
#pragma once

namespace ps2::iop::sound {

void init();   // defines the library; after kernel::init

} // namespace ps2::iop::sound
