// The runtime's commit, from a header the build writes
// (cmake/Ps2nVersion.cmake). Alone in this file, so a new commit recompiles
// nothing else.
#include "internal.h"

#include "ps2n_version.h"

namespace ps2 {

const char* buildCommit() { return PS2N_BUILD_COMMIT; }

} // namespace ps2
