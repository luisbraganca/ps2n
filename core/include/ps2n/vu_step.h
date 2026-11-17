// SPDX-License-Identifier: GPL-3.0-or-later
//
// The steps a block of micro code's plan is made of, and a plan's key (see
// ps2n/vu_plan.h, which has the rest): what compiled micro code needs of a
// plan, without the standard library, so that it can be compiled with
// PS2Native's own headers and nothing else (docs/phase4-proposal.md, step
// P4.2).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ps2n::vu {

enum class StepOp : uint16_t {
#define PS2N_VU_STEP(name, operands) name,
#include "ps2n/vu_steps.inc"
#undef PS2N_VU_STEP
    Count
};

inline constexpr uint32_t kStepOperands = 5;

// In the place of a host register: VI00, which reads as zero.
inline constexpr uint32_t kNone = 0xFF;

struct Step {
    StepOp op = StepOp::Count;
    uint32_t o[kStepOperands] = {};

    bool operator==(const Step&) const = default;
};

// A fingerprint of a plan: of which VU it is for and of its steps, each
// with the operands it has.
struct PlanKey {
    uint64_t a = 0, b = 0;

    bool operator==(const PlanKey&) const = default;
    bool operator<(const PlanKey& other) const { return a != other.a ? a < other.a : b < other.b; }
};

} // namespace ps2n::vu
