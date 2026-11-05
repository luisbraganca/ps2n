// gpu.h for a runtime built without the Vulkan renderer (PS2N_WITH_VULKAN
// off): no device ever opens, and the software renderer draws.
#include "gpu/gpu.h"

namespace ps2::gpu {

namespace {
Status g_status;
}

bool init(const Options&) {
    g_status = Status{};
    g_status.reason = "this build has no Vulkan renderer (PS2N_WITH_VULKAN was off)";
    return false;
}

const Status& status() { return g_status; }
bool available() { return false; }
std::string describe() { return {}; }
void shutdown() {}
uint64_t validationMessages() { return 0; }

} // namespace ps2::gpu
