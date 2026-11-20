// SynthyBoy - own engine tests: the ADAA kernels of the other sample-domain warps (docs/plans/distortion-antialiasing.md task W2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>

#include "alias_warp_support.h"
#include "osc/warp_adaa_misc.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_alias_warp_misc_kernels_equal_the_segment_average)
{
    const int types[] = { kWarpDistDiode2, kWarpDistStomp, kWarpDistSoftSat, kWarpDistSineFold, kWarpDistSineShaper };
    for (const int type : types)
    {
        const double err = aliaswarp::worstError (type, false, [type] (warpadaa::State& s, float y, float a) {
            float out = 0.0f;
            const bool ok = warpadaa::stepMisc (type, s, y, a, false, out);
            return ok ? out : 1.0e9f;
        });
        std::printf ("    warp %d: worst difference to the segment average %.3g\n", type, err);
        CHECK (err < 6.0e-4);
    }
}

TEST (own_alias_warp_misc_unit_values)
{
    float out = 0.0f;
    // Sine Fold, gain 1 (a = 0), (0 -> 1): the average of sin (pi v / 2) over [0, 1] is 2 / pi
    warpadaa::State s;
    CHECK (warpadaa::stepMisc (kWarpDistSineFold, s, 0.0f, 0.0f, false, out));
    CHECK (warpadaa::stepMisc (kWarpDistSineFold, s, 1.0f, 0.0f, false, out));
    CHECK_NEAR (out, 0.636620, 1e-5);
    // Diode 2, gain 1, (0 -> 1): |v| - ln (1 + |v|) = 1 - ln 2
    warpadaa::State d;
    CHECK (warpadaa::stepMisc (kWarpDistDiode2, d, 0.0f, 0.0f, false, out));
    CHECK (warpadaa::stepMisc (kWarpDistDiode2, d, 1.0f, 0.0f, false, out));
    CHECK_NEAR (out, 0.306853, 1e-5);
    CHECK (!warpadaa::stepMisc (kWarpDistHardClip, d, 1.0f, 0.0f, false, out));
}
