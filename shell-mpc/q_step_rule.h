// SynthyBoy - MPC shell: which parameters a Q proxy drives RELATIVELY (in steps of 6 host increments, q_router.h) and which stay absolute.
// The rule (owner's wish: a toggle, a list and a small integer stepper are STEPPED CONTROLS, a fine-grained quantised amount is a knob):
//   - every parameter whose scale is `stepped` (toggles, lists, enumerations of any length: a list of 1249 destinations cannot be reached with an absolute knob of 128 clicks);
//   - an integer stepper (interval 1, whole numbers) of at most kMaxIntegerSteps values: voices, polyphony, bend range, octave, length, unison;
//   - NOT a quantised value with more values than that (grains per second 1 .. 64, a percentage in steps of 1): those are amounts, the knob stays absolute.
// The list of the parameters of the second kind is pinned by shell-mpc/tests/q_step_rule_host.cpp, so a new parameter does not change its behaviour unnoticed.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>

#include "synthyboy/params.h"

namespace synthyboy_mpc
{
constexpr int kMaxIntegerSteps = 32;

/** The number of values of a stepped control (a toggle 2, a list its entries, an integer stepper its range), 0 for a parameter that stays an absolute knob. */
inline int stepCountOf (const synthyboy::ParamInfo& p) noexcept
{
    if (p.scale == synthyboy::Scale::stepped)
        return static_cast<int> (std::lround (p.max - p.min)) + 1;
    if (p.interval == 1.0f && p.max > p.min && std::floor (p.min) == p.min && std::floor (p.max) == p.max)
    {
        const int n = static_cast<int> (std::lround (p.max - p.min)) + 1;
        return n <= kMaxIntegerSteps ? n : 0;
    }
    return 0;
}
} // namespace synthyboy_mpc
