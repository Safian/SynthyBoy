// SynthyBoy - own engine tests: first-order ADAA of the distortion types 0 (Soft Clip), 6 (Tube), 9 (Zero-Square) and 10 (Asym) (docs/plans/distortion-antialiasing.md task E2).
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
double lncosh (double t)
{
    const double s = std::fabs (t);
    return s - std::log (2.0) + std::log1p (std::exp (-2.0 * s));
}

// The ADAA of the plan 3.2 in double precision, followed by the DC blocker of the asymmetric types, for the whole input.
std::vector<double> reference (int type, float driveDb, const std::vector<float>& x)
{
    const double g = static_cast<double> (std::pow (10.0f, driveDb * 0.05f));
    const double ge = type == 9 ? 32.0 * g : g;
    const double off = type == 6 ? 0.3 : 0.0;
    const double sub = type == 6 ? std::tanh (0.3) : 0.0;
    auto F = [type] (double t) {
        const double s = std::fabs (t);
        if (type == 10 && t < 0.0)
            return s >= 0.5 ? 0.5 * s - 0.125 : 0.5 * t * t;
        return lncosh (t);
    };
    auto law = [type, sub] (double t) {
        if (type == 10)
            return std::tanh (std::max (t, 0.0)) + std::max (std::min (t, 0.0), -0.5);
        return std::tanh (t) - sub;
    };
    const double dcA = 1.0 - std::exp (-6.283185307179586 * 10.0 / aliasfx::kFs);
    std::vector<double> y (x.size());
    double tp = off, dc = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (x[i]) * ge + off;
        const double dt = t - tp;
        double v = std::fabs (dt) < 1.0 / 1024.0 ? law (0.5 * (t + tp)) : (F (t) - F (tp)) / dt - sub;
        if (type == 6 || type == 10)
        {
            dc += dcA * (v - dc);
            v -= dc;
        }
        y[i] = v;
        tp = t;
    }
    return y;
}
} // namespace

TEST (own_alias_dist_tanh_values_are_the_segment_averages)
{
    for (const bool fast : { false, true })
    {
        const double tol = fast ? 5e-4 : 1e-4;
        // Soft Clip, 0 dB, 0 -> 1: ln cosh 1
        CHECK_NEAR (aliasfx::run (aliasfx::params (0, 0.0f), fast, { 1.0f })[0], 0.433781, tol);
        // Zero-Square, first sample 0.01: u = 0.32, ln cosh (0.32) / 0.32 = 0.050349 / 0.32 = 0.157342 (the plan has 0.050264 / 0.157075, an arithmetic slip)
        CHECK_NEAR (aliasfx::run (aliasfx::params (9, 0.0f), fast, { 0.01f })[0], 0.157342, tol);
        // Tube, 1.0: (ln cosh 1.3 - ln cosh 0.3) - tanh 0.3, then the DC blocker (1 - 0.001424)
        CHECK_NEAR (aliasfx::run (aliasfx::params (6, 0.0f), fast, { 1.0f })[0], 0.34235, 2e-3);
        // Asym, 1.0: ln cosh 1, then the DC blocker
        CHECK_NEAR (aliasfx::run (aliasfx::params (10, 0.0f), fast, { 1.0f })[0], 0.43316, 2e-3);
    }
}

TEST (own_alias_dist_tanh_lowers_the_aliasing_and_keeps_the_harmonics)
{
    struct Case { int type; float drive; double gain; };
    // The plan's acceptance criterion for ADAA is >= 3.0 dB (docs/plans/distortion-antialiasing.md risk 1 / section 5). Type 9 (Zero-Square) measures 5.91 dB;
    // the plan's 10.1 dB model was optimistic (the kernel matches the double-precision reference in this file).
    const Case cases[] = { { 0, 18.0f, 3.0 }, { 6, 18.0f, 3.0 }, { 10, 18.0f, 3.0 }, { 9, 0.0f, 3.0 } };
    for (const Case& c : cases)
        for (const bool fast : { false, true })
        {
            const aliasm::Result off = aliasfx::measure (c.type, c.drive, fast, false);
            const aliasm::Result on = aliasfx::measure (c.type, c.drive, fast, true);
            std::printf ("    DIST-AA type=%d fast=%d alias off %.2f dB on %.2f dB (difference %.2f), harm off %.2f on %.2f\n", c.type, fast ? 1 : 0, off.aliasDb, on.aliasDb,
                         on.aliasDb - off.aliasDb, off.harmDb, on.harmDb);
            CHECK (on.aliasDb <= off.aliasDb - c.gain);
            CHECK (std::fabs (on.harmDb - off.harmDb) < 1.0);
        }
}

TEST (own_alias_dist_tanh_is_block_size_independent_and_reset_is_complete)
{
    for (const int type : { 0, 6, 9, 10 })
        for (const bool fast : { false, true })
            CHECK (aliasfx::blockSplitAndReset (type, fast));
}

TEST (own_alias_dist_tanh_matches_a_double_precision_reference)
{
    const std::vector<float> in = aliasfx::sine();
    struct Case { int type; float drive; };
    const Case cases[] = { { 0, 18.0f }, { 6, 18.0f }, { 9, 0.0f }, { 10, 18.0f } };
    for (const Case& c : cases)
        for (const bool fast : { false, true })
        {
            const std::vector<float> y = aliasfx::run (aliasfx::params (c.type, c.drive), fast, in);
            const std::vector<double> ref = reference (c.type, c.drive, in);
            double worst = 0.0;
            for (size_t i = 0; i < y.size(); ++i)
                worst = std::max (worst, std::fabs (static_cast<double> (y[i]) - ref[i]));
            std::printf ("    type %d fast %d: worst difference to the double reference %.3g\n", c.type, fast ? 1 : 0, worst);
            CHECK (worst < (fast ? 6e-4 : 2e-4));
        }
}

TEST (own_alias_dist_tanh_checksums_of_the_other_types)
{
    for (const int type : { 2, 3, 4, 5, 7, 8, 12, 13 })
        aliasfx::checksum (type);
}
