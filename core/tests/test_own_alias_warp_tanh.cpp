// SynthyBoy - own engine tests: the ADAA kernels of the tanh family warps (docs/plans/distortion-antialiasing.md task W1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>

#include "alias_warp_support.h"
#include "osc/warp_adaa_tanh.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_alias_warp_tanh_kernels_equal_the_segment_average)
{
    const int types[] = { kWarpDistTube, kWarpDistSoftClip, kWarpDistDiode1, kWarpDistAsym };
    for (const int type : types)
        for (const bool fast : { false, true })
        {
            const double err = aliaswarp::worstError (type, fast, [type, fast] (warpadaa::State& s, float y, float a) {
                float out = 0.0f;
                const bool ok = warpadaa::stepTanh (type, s, y, a, fast, out);
                return ok ? out : 1.0e9f;
            });
            std::printf ("    warp %d fast=%d: worst difference to the segment average %.3g\n", type, fast ? 1 : 0, err);
            CHECK (err < (fast ? 1.0e-3 : 3.0e-4));
        }
}

TEST (own_alias_warp_tanh_soft_clip_unit_value)
{
    // gain 1 (a = 0), (0 -> 1): the average of tanh over [0, 1] is ln (cosh 1) = 0.433781
    warpadaa::State s;
    float out = 0.0f;
    CHECK (warpadaa::stepTanh (kWarpDistSoftClip, s, 0.0f, 0.0f, false, out));
    CHECK (warpadaa::stepTanh (kWarpDistSoftClip, s, 1.0f, 0.0f, false, out));
    CHECK_NEAR (out, 0.433781, 1e-5);
    CHECK (!warpadaa::stepTanh (kWarpDistHardClip, s, 1.0f, 0.0f, false, out));
}
