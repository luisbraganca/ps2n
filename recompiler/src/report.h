// Human-readable recompilation report (written next to the generated code).
#pragma once

#include "analysis.h"

#include <string>

namespace ps2uc {

std::string buildReport(const Program& prog, const Config& cfg);
// One-paragraph summary for the console.
std::string summarize(const Program& prog);

} // namespace ps2uc
