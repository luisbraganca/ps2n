// The CD/DVD drive, high-level emulated over the disc layer (disc.h): the
// cdvdfsv RPC servers the EE's libcdvd calls, and the cdvdman library IOP
// modules import. Both are resident in the BIOS IOP image, so they exist from
// start-up.
#pragma once

namespace ps2::iop::cdvd {

void init();              // defines the cdvdman library and resets the drive; after kernel::init
void registerServers();   // the cdvdfsv RPC servers

} // namespace ps2::iop::cdvd
