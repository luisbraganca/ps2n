// C++ generation from an analysed program.
#pragma once

#include "analysis.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ps2n {

struct GenerateResult {
    std::vector<std::filesystem::path> written;     // files whose content changed
    std::vector<std::filesystem::path> sources;     // every generated .cpp
    size_t functionFiles = 0;
};

// Another executable of the same game ([input] programs). Its code goes to the
// folder `id` of the output, in namespace ps2game_<id>, and the boot program's
// table lists it so that LoadExecPS2 can switch to it.
struct ExtraProgram {
    const Program* prog = nullptr;
    const Config* cfg = nullptr;
    std::string id;
};

// An extra program's folder and namespace suffix: its ELF file name as a C
// identifier ("ACTION.UK" -> "ACTION_UK"), by the rule of CMake's MAKE_C_IDENTIFIER.
std::string programId(const std::filesystem::path& elf);

// Number of recomp_funcs_NNN.cpp files the generator will produce.
int functionFileCount(const Program& prog, const Config& cfg);

GenerateResult generate(const Program& prog, const Config& cfg, const std::vector<ExtraProgram>& extras = {});

// The C++ text of one recompiled function (exposed for tests and `disasm --cpp`).
std::string emitFunction(const Program& prog, const Function& f, bool comments);

} // namespace ps2n
