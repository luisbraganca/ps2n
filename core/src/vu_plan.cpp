// SPDX-License-Identifier: GPL-3.0-or-later
//
// See ps2n/vu_plan.h. This part is compiled into the runtime too, so it
// uses no C++ exceptions; the reader of capture files is vu_plan_read.cpp.

#include "ps2n/vu_plan.h"

#include <cstdio>
#include <cstring>

namespace ps2n::vu {
namespace {

struct StepInfo {
    const char* name;
    uint32_t operands;
};

constexpr StepInfo kSteps[] = {
#define PS2N_VU_STEP(name, operands) {#name, operands},
#include "ps2n/vu_steps.inc"
#undef PS2N_VU_STEP
};

constexpr size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

} // namespace

const char* stepName(StepOp op) {
    const size_t i = static_cast<size_t>(op);
    return i < kStepCount ? kSteps[i].name : "?";
}

uint32_t stepOperandCount(StepOp op) {
    const size_t i = static_cast<size_t>(op);
    return i < kStepCount ? kSteps[i].operands : 0;
}

StepOp stepByName(const char* name, size_t length) {
    for (size_t i = 0; i < kStepCount; ++i)
        if (std::strlen(kSteps[i].name) == length && std::memcmp(kSteps[i].name, name, length) == 0) return static_cast<StepOp>(i);
    return StepOp::Count;
}

PlanKey planKey(int vu, const Step* steps, size_t count) {
    // Two fingerprints that do not share their arithmetic, side by side.
    uint64_t a = 0xCBF29CE484222325ull ^ static_cast<uint64_t>(vu);
    uint64_t b = 0x9E3779B97F4A7C15ull + static_cast<uint64_t>(vu);
    const auto mix = [&](uint32_t v) {
        a = (a ^ v) * 0x100000001B3ull;
        a ^= a >> 29;
        b = (b + v) * 0xFF51AFD7ED558CCDull;
        b ^= b >> 32;
    };
    for (size_t i = 0; i < count; ++i) {
        const Step& s = steps[i];
        mix(static_cast<uint32_t>(s.op));
        const uint32_t n = stepOperandCount(s.op);
        for (uint32_t k = 0; k < n; ++k) mix(s.o[k]);
    }
    mix(static_cast<uint32_t>(count));
    return {a, b};
}

std::string planKeyText(const PlanKey& key) {
    char text[40];
    std::snprintf(text, sizeof text, "%016llx%016llx", static_cast<unsigned long long>(key.a), static_cast<unsigned long long>(key.b));
    return text;
}

std::string planText(int vu, const Step* steps, size_t count, uint32_t at, const uint64_t* words, size_t pairs) {
    std::string out;
    char text[96];
    std::snprintf(text, sizeof text, "\n[[block]]\nvu = %d\nat = 0x%04x\n", vu, at);
    out += text;
    out += "key = \"" + planKeyText(planKey(vu, steps, count)) + "\"\n";
    out += "words = [";
    for (size_t i = 0; i < pairs; ++i) {
        std::snprintf(text, sizeof text, "%s0x%08x, 0x%08x,", (i % 4) ? " " : "\n  ", static_cast<uint32_t>(words[i]),
                      static_cast<uint32_t>(words[i] >> 32));
        out += text;
    }
    out += "\n]\nsteps = [\n";
    for (size_t i = 0; i < count; ++i) {
        const Step& s = steps[i];
        out += "  \"";
        out += stepName(s.op);
        const uint32_t n = stepOperandCount(s.op);
        for (uint32_t k = 0; k < n; ++k) {
            if (s.o[k] < 10) std::snprintf(text, sizeof text, " %u", s.o[k]);
            else std::snprintf(text, sizeof text, " 0x%x", s.o[k]);
            out += text;
        }
        out += "\",\n";
    }
    out += "]\n";
    return out;
}

} // namespace ps2n::vu
