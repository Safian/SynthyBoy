// SynthyBoy - own engine tests: first-order ADAA of the distortion types 1 (Hard Clip) and 11 (Rectify) (docs/plans/distortion-antialiasing.md task E1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "alias_dist_support.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
std::vector<float> runOnce (int type, float driveDb, const std::vector<float>& in)
{
    return aliasfx::run (aliasfx::params (type, driveDb), false, in);
}
} // namespace

TEST (own_alias_dist_clip_values_are_the_segment_averages)
{
    // type 1, 0 dB: F (s) = s^2 / 2 below 1, s - 0.5 above; (0 -> 0.5) averages to 0.25, (0.5 -> 2.0) to 1.375 / 1.5
    const std::vector<float> a = runOnce (1, 0.0f, { 0.5f, 2.0f });
    CHECK_NEAR (a[0], 0.25, 1e-5);
    CHECK_NEAR (a[1], 0.916667, 1e-5);
    // type 11, 0 dB: the first sample 0.25 minus the DC blocker (dcA = 0.001424)
    const std::vector<float> b = runOnce (11, 0.0f, { 0.5f, -0.5f });
    CHECK_NEAR (b[0], 0.24964, 2e-3);
}

TEST (own_alias_dist_clip_lowers_the_aliasing_and_keeps_the_harmonics)
{
    for (const int type : { 1, 11 })
        for (const bool fast : { false, true })
        {
            const aliasm::Result off = aliasfx::measure (type, 18.0f, fast, false);
            const aliasm::Result on = aliasfx::measure (type, 18.0f, fast, true);
            std::printf ("    DIST-AA type=%d fast=%d alias off %.2f dB on %.2f dB (difference %.2f), harm off %.2f on %.2f\n", type, fast ? 1 : 0, off.aliasDb, on.aliasDb,
                         on.aliasDb - off.aliasDb, off.harmDb, on.harmDb);
            CHECK (on.aliasDb <= off.aliasDb - 3.0);
            CHECK (std::fabs (on.harmDb - off.harmDb) < 1.0);
        }
}

TEST (own_alias_dist_clip_is_block_size_independent_and_reset_is_complete)
{
    for (const int type : { 1, 11 })
        for (const bool fast : { false, true })
            CHECK (aliasfx::blockSplitAndReset (type, fast));
}

TEST (own_alias_dist_clip_rebases_to_a_new_drive)
{
    // constant 0.3 at 0 dB, then +20 dB: the first sample after the step is clamp (0.3 * 10) = 1.0 (the state is rebased to the new gain)
    own::Distortion d;
    d.prepare (aliasfx::kFs);
    d.set (aliasfx::params (1, 0.0f));
    std::vector<float> l (1000, 0.3f), r (l);
    d.process (l.data(), r.data(), 1000);
    d.set (aliasfx::params (1, 20.0f));
    std::vector<float> l2 (4, 0.3f), r2 (l2);
    d.process (l2.data(), r2.data(), 4);
    CHECK_NEAR (l2[0], 1.0, 1e-6);
}

TEST (own_alias_dist_clip_small_signal_droop_is_the_two_tap_average)
{
    // type 1 at -30 dB is linear: y = (v[n] + v[n-1]) / 2, a gain of cos (pi f / fs). A sine on bin 929 of 4096 (10003.6 Hz), amplitude 0.5.
    const size_t n = 4096, warm = 4096, bin = 929;
    const double f = static_cast<double> (bin) * aliasfx::kFs / static_cast<double> (n);
    std::vector<float> in (warm + n);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float> (0.5 * std::sin (6.283185307179586 * static_cast<double> ((bin * i) % n) / static_cast<double> (n)));
    const std::vector<float> y = runOnce (1, -30.0f, in);
    double sc = 0.0, ss = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = 6.283185307179586 * static_cast<double> ((bin * i) % n) / static_cast<double> (n);
        sc += static_cast<double> (y[warm + i]) * std::cos (ph);
        ss += static_cast<double> (y[warm + i]) * std::sin (ph);
    }
    const double amp = 2.0 / static_cast<double> (n) * std::sqrt (sc * sc + ss * ss);
    const double gainDb = 20.0 * std::log10 (amp / (0.5 * std::pow (10.0, -30.0 / 20.0)));
    const double expectedDb = 20.0 * std::log10 (std::cos (3.141592653589793 * f / aliasfx::kFs));
    std::printf ("DIST-AA-DROOP type=1 f=%.1f gain=%.3f dB expected=%.3f dB\n", f, gainDb, expectedDb);
    CHECK_NEAR (gainDb, expectedDb, 0.1);
}

TEST (own_alias_dist_clip_checksums_of_the_other_types)
{
    for (const int type : { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13 })
        aliasfx::checksum (type);
}
