// C++ generation from an analysed program.
#pragma once

#include "analysis.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ps2uc {

struct GenerateResult {
    std::vector<std::filesystem::path> written;     // files whose content changed
    std::vector<std::filesystem::path> sources;     // every generated .cpp
    size_t functionFiles = 0;
};

// Number of recomp_funcs_NNN.cpp files the generator will produce.
int functionFileCount(const Program& prog, const Config& cfg);

GenerateResult generate(const Program& prog, const Config& cfg);

// The C++ text of one recompiled function (exposed for tests and `disasm --cpp`).
std::string emitFunction(const Program& prog, const Function& f, bool comments);

} // namespace ps2uc
