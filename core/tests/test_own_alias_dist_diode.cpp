// SynthyBoy - own engine tests: first-order ADAA of the distortion types 7 (Diode 1) and 8 (Diode 2) (docs/plans/distortion-antialiasing.md task E3).
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
// The ADAA of the plan 3.2 in double precision, followed by the DC blocker of type 7, for the whole input.
std::vector<double> reference (int type, float driveDb, const std::vector<float>& x)
{
    const double g = static_cast<double> (std::pow (10.0f, driveDb * 0.05f));
    auto F = [type] (double v) {
        if (type == 8)
            return std::fabs (v) - (1.0 - std::exp (-std::fabs (v)));
        return v >= 0.0 ? v + std::exp (-v) - 1.0 : -0.5 * v + 0.25 * std::exp (2.0 * v) - 0.25;
    };
    auto law = [type] (double v) {
        if (type == 8)
            return std::exp (std::min (v, 0.0)) - std::exp (-std::max (v, 0.0));
        return (1.0 - std::exp (-std::max (v, 0.0))) - 0.5 * (1.0 - std::exp (2.0 * std::min (v, 0.0)));
    };
    const double dcA = 1.0 - std::exp (-6.283185307179586 * 10.0 / aliasfx::kFs);
    std::vector<double> y (x.size());
    double vp = 0.0, dc = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double v = static_cast<double> (x[i]) * g;
        const double dv = v - vp;
        double out = std::fabs (dv) < 1.0 / 1024.0 ? law (0.5 * (v + vp)) : (F (v) - F (vp)) / dv;
        if (type == 7)
        {
            dc += dcA * (out - dc);
            out -= dc;
        }
        y[i] = out;
        vp = v;
    }
    return y;
}
} // namespace

TEST (own_alias_dist_diode_values_are_the_segment_averages)
{
    for (const bool fast : { false, true })
    {
        // Diode 1, 0 dB, 0 -> 1: v + e^-v - 1 = 0.367879, then the DC blocker (1 - 0.001424)
        CHECK_NEAR (aliasfx::run (aliasfx::params (7, 0.0f), fast, { 1.0f })[0], 0.36736, 2e-3);
        // Diode 2, no blocker
        CHECK_NEAR (aliasfx::run (aliasfx::params (8, 0.0f), fast, { 1.0f })[0], 0.367879, 5e-4);
    }
}

TEST (own_alias_dist_diode_lowers_the_aliasing_and_keeps_the_harmonics)
{
    for (const int type : { 7, 8 })
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

TEST (own_alias_dist_diode_is_block_size_independent_and_reset_is_complete)
{
    for (const int type : { 7, 8 })
        for (const bool fast : { false, true })
            CHECK (aliasfx::blockSplitAndReset (type, fast));
}

TEST (own_alias_dist_diode_matches_a_double_precision_reference)
{
    const std::vector<float> in = aliasfx::sine();
    for (const int type : { 7, 8 })
        for (const bool fast : { false, true })
        {
            const std::vector<float> y = aliasfx::run (aliasfx::params (type, 18.0f), fast, in);
            const std::vector<double> ref = reference (type, 18.0f, in);
            double worst = 0.0;
            for (size_t i = 0; i < y.size(); ++i)
                worst = std::max (worst, std::fabs (static_cast<double> (y[i]) - ref[i]));
            std::printf ("    type %d fast %d: worst difference to the double reference %.3g\n", type, fast ? 1 : 0, worst);
            CHECK (worst < (fast ? 6e-4 : 2e-4));
        }
}

TEST (own_alias_dist_diode_checksums_of_the_other_types)
{
    for (const int type : { 2, 3, 4, 5, 12, 13 })
        aliasfx::checksum (type);
}
