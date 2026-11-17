// SPDX-License-Identifier: GPL-3.0-or-later
//
// A block of micro code as it is carried out: its plan.
//
// PCSX2's VU recompiler (microVU) decides everything about a block but the
// values when it compiles it: from the pipeline state the block is entered
// with (its first pass, ps2n/vu_mvu.h), and from the host registers its
// allocator gives each operand as its second pass goes (which shows in a few
// results: the high half of VI registers, which of two NaNs an addition
// gives). PS2Native's VU interpreter makes the same decisions once, when it
// first meets the block (runtime/vu_interp.cpp), and writes them down as a
// plan: a list of steps, each a small operation with everything decided
// already in its operands. The interpreter carries a block out by its plan,
// step by step. The translator (ps2n-recomp vu) turns a plan into a C++
// function that calls the same code for each step with the operands as
// constants (runtime/include/ps2n/vu_ops.h), so that a block is carried out
// the same way compiled as interpreted
// (docs/adr/0022-vu-micro-programs-compiled-as-step-g11.md).
//
// A plan says nothing of where its block sits: the addresses a step hands
// on are distances from the block's start, which whoever runs the plan
// knows. So one plan, and one compiled function, serve the same code
// wherever a game writes it. And it says nothing of how the block ends
// (where it branches to, the cycles it takes): the record of programs and
// blocks keeps that for each block, and sees to it after the plan has run.
//
// Compiled code is found by a plan's key, a fingerprint of its steps: a
// block whose plan a build has compiled runs compiled, any other is
// interpreted and, if the run is asked to, written to a capture file for the
// next build (planText, readPlans).
#pragma once

#include "ps2n/vu_step.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps2n::vu {

const char* stepName(StepOp op);
uint32_t stepOperandCount(StepOp op);
// The step of that name, or StepOp::Count.
StepOp stepByName(const char* name, size_t length);

// The key of a plan (PlanKey, in ps2n/vu_step.h).
PlanKey planKey(int vu, const Step* steps, size_t count);

// The key as 32 hexadecimal digits.
std::string planKeyText(const PlanKey& key);

// One plan as a block of a capture file. `at` (where the block was first
// met) and `words` (its pairs as they were then, lower word then upper) are
// for whoever reads the file; the plan is the steps.
std::string planText(int vu, const Step* steps, size_t count, uint32_t at, const uint64_t* words, size_t pairs);

} // namespace ps2n::vu
