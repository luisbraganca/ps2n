// IOP module (IRX) inspection: the .iopmod header plus the import and export
// tables that the IOP kernel's loadcore links at load time.
#pragma once

#include "ps2n/elf.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ps2n {

struct IrxImport {
    std::string library;   // e.g. "sysmem"
    uint16_t version = 0;
    uint32_t stubAddr = 0; // address of the "jr $ra; addiu $0,$0,N" stub
    uint32_t index = 0;    // export index in the library
    std::string name;      // known function name, or empty
};

struct IrxExportTable {
    std::string library;
    uint16_t version = 0;
    uint32_t tableAddr = 0;
    std::vector<uint32_t> functions;   // module-relative addresses
};

struct IrxInfo {
    std::string name;      // module name from .iopmod
    uint16_t version = 0;  // major << 8 | minor
    uint32_t moduleInfo = 0;
    uint32_t entry = 0;
    uint32_t gp = 0;
    uint32_t textSize = 0, dataSize = 0, bssSize = 0;
    std::vector<IrxImport> imports;
    std::vector<IrxExportTable> exports;
};

// Throws ps2n::Error if the file is not an IRX module.
IrxInfo parseIrx(const ElfFile& elf);

// Name of export `index` of an IOP library from the built-in table, or nullptr.
const char* irxKnownExportName(std::string_view library, uint32_t index);

} // namespace ps2n
