// IOP modules: loading IRX images into IOP memory (relocation and import
// linking, as loadcore does), starting them on a loader thread (as modload
// does), and the modules the HLE kernel provides itself instead of running
// their code: the kernel's own, and drivers for hardware it does not emulate
// at the register level (SIO2, CD/DVD, SPU2), which host services replace.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ps2::iop::modules {

void init();

// Called when a load finishes: the module id (or a negative KE_ error) and
// the value its start function returned. May run before load() returns.
using Done = std::function<void(int id, int startResult)>;

// Loads and starts a module by PS2 path (rom0:, host0:, cdrom0:, ...).
void load(const std::string& path, const std::string& args, Done done);
// The same for an image already in IOP memory (LoadModuleBuffer).
void loadFromMemory(uint32_t addr, const std::string& args, Done done);

// Id of the loaded module with this name, or KE_UNKNOWN_MODULE.
int searchByName(const std::string& name);

// True when the HLE kernel provides the module with this name (file name or
// .iopmod name, any case).
bool providedByHle(const std::string& name);

} // namespace ps2::iop::modules
