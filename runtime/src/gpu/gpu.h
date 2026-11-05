// The graphics card, through Vulkan: one instance and one device, shared by
// the GS, which draws with it when the Vulkan renderer runs (gs.h), and by
// the window, which shows its pictures. The runtime opens it when it starts,
// unless the settings ask for the software renderer, and closes it at exit.
//
// Nothing here needs the Vulkan headers: a runtime built without the Vulkan
// renderer (PS2N_WITH_VULKAN off) has the same functions, and none opens.
#pragma once

#include <cstdint>
#include <string>

namespace ps2::gpu {

struct Options {
    bool window = false;   // a window will show the pictures: the instance can make surfaces
    bool debug = false;    // --gpu-debug: Vulkan's validation layer, when installed
};

struct Status {
    bool available = false;    // a device is open
    bool canPresent = false;   // it has swapchains: a window can show its pictures
    bool validation = false;   // the validation layer checks every call
    std::string reason;        // why no device opened, in one sentence
    std::string deviceName;    // "NVIDIA GeForce RTX 4070 SUPER"
    std::string driver;        // "NVIDIA 621.01"
    uint32_t apiVersion = 0;   // VK_MAKE_API_VERSION's packing
};

// Main thread, before any other call. Logs nothing but --gpu-debug's lines:
// the runtime says which renderer it chose once the log goes to Axiom too.
bool init(const Options& options);
const Status& status();
bool available();
// "NVIDIA GeForce RTX 4070 SUPER (NVIDIA 621.01, Vulkan 1.4)", or empty.
std::string describe();
// Main thread, after the GS and the window let go of it.
void shutdown();
// Errors and warnings the validation layer reported so far (the tests).
uint64_t validationMessages();

} // namespace ps2::gpu
