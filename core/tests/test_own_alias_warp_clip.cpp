// SynthyBoy - own engine tests: the ADAA kernels of the clip family warps and Quantize (docs/plans/distortion-antialiasing.md task W0).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>

#include "alias_warp_support.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_alias_warp_clip_kernels_equal_the_segment_average)
{
    const int types[] = { kWarpDistHardClip, kWarpDistRectify, kWarpDistZeroSquare, kWarpDistLinearFold, kWarpDistTape };
    for (const int type : types)
    {
        const double err = aliaswarp::worstError (type, false, [type] (warpadaa::State& s, float y, float a) {
            float out = 0.0f;
            const bool ok = warpadaa::stepClip (type, s, y, a, false, out);
            return ok ? out : 1.0e9f;
        });
        std::printf ("    warp %d: worst difference to the segment average %.3g\n", type, err);
        CHECK (err < 5.0e-4);
    }
}

TEST (own_alias_warp_clip_refuses_other_types)
{
    warpadaa::State s;
    float out = 0.0f;
    CHECK (!warpadaa::stepClip (kWarpDistSoftClip, s, 0.5f, 0.5f, false, out));
    CHECK (!warpadaa::stepClip (kWarpSync, s, 0.5f, 0.5f, false, out));
}

TEST (own_alias_warp_quantize_kernel_equals_the_staircase_average)
{
    const double err = aliaswarp::worstError (kWarpQuantize, false, [] (warpadaa::State& s, float y, float a) {
        const float levels = 2.0f + (1.0f - a) * (1.0f - a) * 62.0f;
        return warpadaa::stepQuantize (s, y, levels);
    });
    std::printf ("    quantize: worst difference to the segment average %.3g\n", err);
    CHECK (err < 2.0e-3);

    // levels 10, from y = 0 to 0.07: the staircase is 0 on [0, 0.05) and 0.1 on [0.05, 0.07]: average 0.1 * 0.02 / 0.07 = 0.028571
    warpadaa::State s;
    (void) warpadaa::stepQuantize (s, 0.0f, 10.0f);
    CHECK_NEAR (warpadaa::stepQuantize (s, 0.07f, 10.0f), 0.028571, 1e-6);
}
