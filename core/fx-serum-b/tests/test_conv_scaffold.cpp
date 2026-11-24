// SynthyBoy - core/fx-serum-b tests: the helpers of the Convolve optimisation tests (docs/plans/reverb-convolve.md task T03).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>

#include "conv_util.h"
#include "test_runner.h"

TEST (conv_util_direct_convolve_matches_a_hand_computed_case)
{
    const std::vector<double> y = convutil::directConvolve ({ 1.0f, 0.0f, 0.0f, 2.0f }, { 0.5f, 0.25f }, 1);
    const double expected[6] = { 0.0, 0.5, 0.25, 0.0, 1.0, 0.5 };
    CHECK_EQ (y.size(), 6u);
    for (size_t i = 0; i < y.size() && i < 6; ++i)
        CHECK_EQ (y[i], expected[i]);
}

TEST (conv_util_impulses_are_deterministic)
{
    const std::vector<float> a = convutil::decayingNoise (4000, 800.0, 3), b = convutil::decayingNoise (4000, 800.0, 3), c = convutil::decayingNoise (4000, 800.0, 4);
    CHECK (a == b);
    CHECK (a != c);
    CHECK_NEAR (a[0], 1.0, 0.3);
    const std::vector<float> r = convutil::resonantImpulse (8000, 44100.0, 1);
    float peak = 0.0f;
    for (const float v : r)
        peak = std::max (peak, std::fabs (v));
    CHECK_NEAR (peak, 0.5, 1e-6);
}
