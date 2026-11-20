// SynthyBoy - own engine tests: the ADAA dispatcher of the oscillator warps (docs/plans/distortion-antialiasing.md task O2, the part that lives in warp_adaa.h).
// The part inside WtOsc::sampleAt (aliasDb on against off, the WARP-CHECKSUM lines) needs the edit of core/engine-own/osc/wt_osc.h and belongs to the owner of that file.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>

#include "osc/warp_adaa.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_alias_warp_adaa_dispatcher_covers_every_sample_domain_warp)
{
    // every sample-domain type is handled by one of the kernels: a step from 0 to 0.8 gives the segment average, which differs from the point law for a law with a slope
    for (int type = kWarpDistTube; type <= kWarpDistSoftSat; ++type)
    {
        CHECK (warpIsSampleDomain (type));
        warpadaa::State s;
        float out = 0.0f;
        const bool handled = warpadaa::stepClip (type, s, 0.0f, 0.5f, false, out) || warpadaa::stepTanh (type, s, 0.0f, 0.5f, false, out) || warpadaa::stepMisc (type, s, 0.0f, 0.5f, false, out);
        CHECK (handled);
    }
    CHECK (warpIsSampleDomain (kWarpQuantize));
}

TEST (own_alias_warp_adaa_dispatcher_equals_the_kernels_and_falls_back_to_the_law)
{
    warpadaa::State a, b;
    float out = 0.0f;
    (void) warpadaa::step (kWarpDistSoftClip, a, 0.1f, 0.5f, false);
    (void) warpadaa::stepTanh (kWarpDistSoftClip, b, 0.1f, 0.5f, false, out);
    const float viaDispatcher = warpadaa::step (kWarpDistSoftClip, a, 0.7f, 0.5f, false);
    (void) warpadaa::stepTanh (kWarpDistSoftClip, b, 0.7f, 0.5f, false, out);
    CHECK_EQ (viaDispatcher, out);

    // a type no kernel handles: the plain law
    warpadaa::State c;
    CHECK_EQ (warpadaa::step (kWarpSync, c, 0.3f, 0.5f, false), warpSample (kWarpSync, 0.3f, 0.5f, 0.0f, false));

    // Quantize with levels 2 + (1 - a)^2 * 62: a = 0.5 -> 17.5 levels
    warpadaa::State q, r;
    (void) warpadaa::step (kWarpQuantize, q, 0.0f, 0.5f, false);
    (void) warpadaa::stepQuantize (r, 0.0f, 17.5f);
    CHECK_EQ (warpadaa::step (kWarpQuantize, q, 0.4f, 0.5f, false), warpadaa::stepQuantize (r, 0.4f, 17.5f));
}
