// SynthyBoy - tests of the measurement statistics (bench/perf/perf_stats.h): percentile rule of tier_bench, summary, spread, run validity.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <string>
#include <vector>

#include "perf/perf_stats.h"
#include "test_runner.h"

using namespace synthyboy::perf;

namespace
{
RunHealth goodHealth()
{
    RunHealth h;
    h.runDelayShare = 0.001;
    h.khzBefore = 1608000.0;
    h.khzAfter = 1608000.0;
    h.khzMax = 1608000.0;
    h.tempMaxC = 69.6;
    h.maxMs = 5.0;
    h.budgetMs = 2.9025;
    h.majflt = 0;
    return h;
}
} // namespace

TEST (perf_stats)
{
    // 1.0 .. 1000.0 (fed in reverse: summarize sorts), budget 500.5
    std::vector<double> values;
    for (int i = 1000; i >= 1; --i)
        values.push_back (static_cast<double> (i));
    const Summary s = summarize (values, 500.5);
    CHECK_EQ (s.n, static_cast<size_t> (1000));
    CHECK_EQ (s.min, 1.0);
    CHECK_EQ (s.median, 501.0);   // sorted[1000 / 2]
    CHECK_EQ (s.p99, 990.0);      // sorted[floor (0.99 * 999) = 989]
    CHECK_EQ (s.p999, 999.0);     // sorted[floor (0.999 * 999) = 998]
    CHECK_EQ (s.max, 1000.0);
    CHECK_EQ (s.mean, 500.5);
    CHECK_EQ (s.over, static_cast<size_t> (500));

    // one value
    const Summary one = summarize (std::vector<double> { 7.0 }, 7.0);
    CHECK_EQ (one.n, static_cast<size_t> (1));
    CHECK_EQ (one.min, 7.0);
    CHECK_EQ (one.median, 7.0);
    CHECK_EQ (one.p99, 7.0);
    CHECK_EQ (one.p999, 7.0);
    CHECK_EQ (one.max, 7.0);
    CHECK_EQ (one.mean, 7.0);
    CHECK_EQ (one.over, static_cast<size_t> (0));

    // spread
    CHECK_NEAR (spreadRelative (std::vector<double> { 100.0, 102.0, 98.0 }), 0.04, 1.0e-12);

    // validity
    CHECK_EQ (invalidReason (goodHealth()), std::string());

    RunHealth h = goodHealth();
    h.runDelayShare = 0.003;
    CHECK_EQ (invalidReason (h), std::string ("run-delay"));

    h = goodHealth();
    h.khzAfter = 1500000.0;   // 1500000 / 1608000 = 0.9328 < 0.98
    CHECK_EQ (invalidReason (h), std::string ("freq"));

    h = goodHealth();
    h.majflt = 1;
    CHECK_EQ (invalidReason (h), std::string ("majflt"));

    h = goodHealth();
    h.tempMaxC = 81.0;
    CHECK_EQ (invalidReason (h), std::string ("temp"));

    h = goodHealth();
    h.maxMs = 40.0;   // 40 > 12 * 2.9025 = 34.83
    CHECK_EQ (invalidReason (h), std::string ("stall"));

    h = goodHealth();
    h.runDelayShare = 0.003;
    h.majflt = 1;
    CHECK_EQ (invalidReason (h), std::string ("run-delay,majflt"));

    h = goodHealth();
    h.khzBefore = 0.0;
    h.khzAfter = 0.0;
    h.khzMax = 0.0;
    CHECK_EQ (invalidReason (h), std::string());   // frequency unknown: no freq code
}
