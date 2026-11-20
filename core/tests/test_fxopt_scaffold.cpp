// SynthyBoy - own engine tests: the helpers of the effect optimisation tests (docs/plans/reverb-convolve.md task T02).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>

#include "convolve.h"
#include "fx/fx_reverb.h"
#include "fxopt_util.h"
#include "synthyboy/fx/hyper_dimension.h"
#include "test_runner.h"

TEST (fxopt_scaffold_program_is_deterministic_and_in_range)
{
    const fxopt::Program a = fxopt::makeProgram (4410, 44100.0, 1);
    const fxopt::Program b = fxopt::makeProgram (4410, 44100.0, 1);
    CHECK (a.l == b.l);
    CHECK (a.r == b.r);
    bool finite = true;
    for (size_t i = 0; i < a.l.size(); ++i)
        finite = finite && std::isfinite (a.l[i]) && std::isfinite (a.r[i]);
    CHECK (finite);
    const float peak = fxopt::maxAbs (a.l.data(), a.l.size());
    CHECK (peak > 0.05f);
    CHECK (peak < 1.0f);
    const fxopt::Program c = fxopt::makeProgram (4410, 44100.0, 2);
    CHECK (a.l != c.l);
}

TEST (fxopt_scaffold_checksum_normalises_the_sign_of_zero)
{
    const float pz = +0.0f, nz = -0.0f, one = 1.0f;
    CHECK_EQ (fxopt::fnv1a (&pz, 1), fxopt::fnv1a (&nz, 1));
    CHECK (fxopt::fnv1a (&pz, 1) != fxopt::fnv1a (&one, 1));
    CHECK (fxopt::sameBits (&pz, &nz, 1));
    CHECK (!fxopt::sameBits (&pz, &one, 1));
}

TEST (fxopt_scaffold_the_effect_headers_are_reachable)
{
    synthyboy::own::Reverb reverb;
    synthyboy::fx::HyperDimension hyper;
    synthyboy::fx::Convolve convolve;
    (void) reverb;
    (void) hyper;
    CHECK_EQ (convolve.latency(), 0);
}
