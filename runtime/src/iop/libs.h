// IOP kernel libraries that are plain functions over IOP memory (sysclib,
// stdio, ioman, iomanX, heaplib, timrman, and hardware set-up libraries).
#pragma once

namespace ps2::iop::libs {

void init();   // defines the libraries; after kernel::init

} // namespace ps2::iop::libs
