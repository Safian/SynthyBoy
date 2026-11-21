// SynthyBoy - own engine tests: the warp helpers of the anti-aliasing (docs/plans/distortion-antialiasing.md task O1): the sample-domain class, the discontinuity events of the phase
// maps and the band limit factor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdint>

#include "osc/warp.h"
#include "synthyboy/aa_switch.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_alias_warp_events_sample_domain_class)
{
    CHECK (warpIsSampleDomain (kWarpDistHardClip));
    CHECK (warpIsSampleDomain (kWarpDistTube));
    CHECK (warpIsSampleDomain (kWarpDistSoftSat));
    CHECK (warpIsSampleDomain (kWarpQuantize));
    CHECK (!warpIsSampleDomain (kWarpRmA));
    CHECK (!warpIsSampleDomain (kWarpAmA));
    CHECK (!warpIsSampleDomain (kWarpSync));
    CHECK (!warpIsSampleDomain (kWarpNone));
}

TEST (own_alias_warp_events_of_the_phase_maps)
{
    WarpEvent e[2];
    CHECK_EQ (warpEvents (kWarpSync, 0.35f, e), 1);
    CHECK_EQ (e[0].phase, 0u);
    CHECK_EQ (e[0].kind, 1);
    CHECK_EQ (warpEvents (kWarpSyncVar, 0.35f, e), 1);

    CHECK_EQ (warpEvents (kWarpFlip, 0.5f, e), 2);
    CHECK_EQ (e[0].phase, 0u);
    CHECK_EQ (e[0].kind, 1);
    CHECK_EQ (e[1].phase, 2147483648u);
    CHECK_EQ (e[1].kind, 2);

    CHECK_EQ (warpEvents (kWarpMirror, 0.5f, e), 2);
    CHECK_EQ (e[0].kind, 2);
    CHECK_EQ (e[1].kind, 2);

    CHECK_EQ (warpEvents (kWarpPulse, 0.5f, e), 2);
    CHECK (e[0].phase >= 2791728741u && e[0].phase <= 2791728743u); // (1 - 0.7 * 0.5) * 2^32
    CHECK_EQ (e[0].kind, 2);
    CHECK_EQ (e[1].phase, 0u); // the slope rises again at the wrap
    CHECK_EQ (e[1].kind, 2);

    CHECK_EQ (warpEvents (kWarpFormant, 0.5f, e), 0);
    CHECK_EQ (warpEvents (kWarpBend, 0.5f, e), 0);
    CHECK_EQ (warpEvents (kWarpSqueeze, 0.5f, e), 0);
    CHECK_EQ (warpEvents (kWarpFmA, 0.5f, e), 0);
}

TEST (own_alias_warp_events_band_limit_factor)
{
    synthyboy::aa::setOff (0);
    CHECK_NEAR (warpBandLimitFactor (kWarpSync, 0, false), 3.2, 1e-6);
    CHECK_NEAR (warpBandLimitFactor (kWarpDistHardClip, 0, false), 2.0, 1e-6);
    CHECK_NEAR (warpBandLimitFactor (kWarpQuantize, kWarpSync, false), 2.0, 1e-6);
    CHECK_NEAR (warpBandLimitFactor (kWarpDistHardClip, 0, true), 1.0, 1e-6);
    CHECK_NEAR (warpBandLimitFactor (0, 0, false), 3.2, 1e-6);
    synthyboy::aa::setOff (synthyboy::aa::kBandLimit);
    CHECK_NEAR (warpBandLimitFactor (kWarpDistHardClip, 0, false), 3.2, 1e-6);
    synthyboy::aa::setOff (0);
}
