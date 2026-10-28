// libsd (the SPU2 driver library sound drivers import), high-level emulated
// without audio output. Sound drivers run; nothing is heard.
#pragma once

namespace ps2::iop::sound {

void init();   // defines the library; after kernel::init

} // namespace ps2::iop::sound
