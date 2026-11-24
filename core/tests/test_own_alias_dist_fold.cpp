// SynthyBoy - own engine tests: first-order ADAA of the distortion types 2 (Linear Fold) and 3 (Sine Fold) (docs/plans/distortion-antialiasing.md task E4).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "alias_dist_support.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
// The ADAA of the plan 3.2 in double precision for the whole input (types 2 and 3 have no DC blocker).
std::vector<double> reference (int type, float driveDb, const std::vector<float>& x)
{
    const double pi = 3.141592653589793;
    const double g = static_cast<double> (std::pow (10.0f, driveDb * 0.05f));
    auto Fp = [] (double v) {
        const double t = 0.25 * v + 0.75;
        const double w = t - std::floor (t);
        return w < 0.5 ? 4.0 * w * (1.0 - 2.0 * w) : 4.0 * (2.0 * w - 1.0) * (w - 1.0);
    };
    auto fold = [] (double v) {
        const double t = 0.25 * v + 0.75;
        return std::fabs (-4.0 * (t - std::floor (t)) + 2.0) - 1.0;
    };
    std::vector<double> y (x.size());
    double vp = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double v = static_cast<double> (x[i]) * g;
        const double dv = v - vp;
        if (type == 3)
        {
            const double z = dv / 4.0;
            const double sinc = std::fabs (z) < 1e-9 ? 1.0 : std::sin (pi * z) / (pi * z);
            y[i] = std::sin (pi * (v + vp) / 4.0) * sinc; // sin (pi m / 2) with m = (v + vp) / 2
        }
        else
            y[i] = std::fabs (dv) < 1.0 / 1024.0 ? fold (0.5 * (v + vp)) : (Fp (v) - Fp (vp)) / dv;
        vp = v;
    }
    return y;
}
} // namespace

TEST (own_alias_dist_fold_values_are_the_segment_averages)
{
    for (const bool fast : { false, true })
    {
        // Linear Fold, 0 dB, 0 -> 1: the law is f (v) = v there, average 0.5
        CHECK_NEAR (aliasfx::run (aliasfx::params (2, 0.0f), fast, { 1.0f })[0], 0.5, 1e-5);
        // Sine Fold, 0 -> 1: the average of sin (pi v / 2) is 2 / pi
        CHECK_NEAR (aliasfx::run (aliasfx::params (3, 0.0f), fast, { 1.0f })[0], 0.636620, fast ? 1e-4 : 1e-5);
    }
}

TEST (own_alias_dist_fold_lowers_the_aliasing_and_keeps_the_harmonics)
{
    for (const int type : { 2, 3 })
        for (const bool fast : { false, true })
        {
            const aliasm::Result off = aliasfx::measure (type, 24.0f, fast, false);
            const aliasm::Result on = aliasfx::measure (type, 24.0f, fast, true);
            std::printf ("    DIST-AA type=%d fast=%d alias off %.2f dB on %.2f dB (difference %.2f), harm off %.2f on %.2f\n", type, fast ? 1 : 0, off.aliasDb, on.aliasDb,
                         on.aliasDb - off.aliasDb, off.harmDb, on.harmDb);
            CHECK (on.aliasDb <= off.aliasDb - 3.0);
            CHECK (std::fabs (on.harmDb - off.harmDb) < 1.0);
        }
}

TEST (own_alias_dist_fold_is_block_size_independent_and_reset_is_complete)
{
    for (const int type : { 2, 3 })
        for (const bool fast : { false, true })
            CHECK (aliasfx::blockSplitAndReset (type, fast));
}

TEST (own_alias_dist_fold_matches_a_double_precision_reference)
{
    const std::vector<float> in = aliasfx::sine();
    for (const int type : { 2, 3 })
        for (const bool fast : { false, true })
        {
            const std::vector<float> y = aliasfx::run (aliasfx::params (type, 24.0f), fast, in);
            const std::vector<double> ref = reference (type, 24.0f, in);
            double worst = 0.0;
            for (size_t i = 0; i < y.size(); ++i)
                worst = std::max (worst, std::fabs (static_cast<double> (y[i]) - ref[i]));
            std::printf ("    type %d fast %d: worst difference to the double reference %.3g\n", type, fast ? 1 : 0, worst);
            CHECK (worst < 3e-4);
        }
}

TEST (own_alias_dist_fold_checksums_of_the_other_types)
{
    for (const int type : { 4, 5, 12, 13 })
        aliasfx::checksum (type);
}
