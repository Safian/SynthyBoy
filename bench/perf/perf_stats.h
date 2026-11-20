// SynthyBoy - measurement helper: the statistics of a benchmark run (percentile rule of bench/tier_bench.cpp, summary, spread between passes) and the
// validity of a run (a run disturbed by the scheduler, the frequency governor, page faults or heat is marked INVALID, never averaged in).
// Header-only, no engine dependency.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace synthyboy::perf
{
/** sorted[size_t (q * double (n - 1))], the percentile rule of tier_bench.cpp. 0 for an empty vector. */
inline double percentileSorted (const std::vector<double>& sorted, double q) noexcept
{
    if (sorted.empty())
        return 0.0;
    return sorted[static_cast<size_t> (q * static_cast<double> (sorted.size() - 1))];
}

/** sorted[n / 2], the median rule of tier_bench.cpp. 0 for an empty vector. */
inline double medianSorted (const std::vector<double>& sorted) noexcept
{
    if (sorted.empty())
        return 0.0;
    return sorted[sorted.size() / 2];
}

struct Summary
{
    size_t n = 0;
    double min = 0, median = 0, p99 = 0, p999 = 0, max = 0, mean = 0;
    size_t over = 0;
};

/** Statistics of the values; over = the number of values greater than budget. */
inline Summary summarize (std::vector<double> values, double budget)
{
    Summary s;
    s.n = values.size();
    if (values.empty())
        return s;
    std::sort (values.begin(), values.end());
    s.min = values.front();
    s.max = values.back();
    s.median = medianSorted (values);
    s.p99 = percentileSorted (values, 0.99);
    s.p999 = percentileSorted (values, 0.999);
    double sum = 0.0;
    for (double v : values)
    {
        sum += v;
        if (v > budget)
            ++s.over;
    }
    s.mean = sum / static_cast<double> (values.size());
    return s;
}

/** (max - min) / median of the values of several passes; 0 when there are none or the median is 0. */
inline double spreadRelative (std::vector<double> passes)
{
    if (passes.empty())
        return 0.0;
    std::sort (passes.begin(), passes.end());
    const double med = medianSorted (passes);
    if (med == 0.0)
        return 0.0;
    return (passes.back() - passes.front()) / med;
}

struct RunHealth
{
    double runDelayShare = 0, khzBefore = 0, khzAfter = 0, khzMax = 0, tempMaxC = 0, maxMs = 0, budgetMs = 0;
    long majflt = 0;
};

struct HealthLimits
{
    double runDelayShare = 0.002, freqShare = 0.98, tempMaxC = 80.0, stallBlocks = 12.0;
    long majflt = 0;
};

/** "" when the run is valid, else the codes stall, run-delay, freq, majflt, temp (in this order) joined by ','. */
inline std::string invalidReason (const RunHealth& h, const HealthLimits& limits = {})
{
    std::string out;
    auto add = [&out] (const char* code) {
        if (! out.empty())
            out += ',';
        out += code;
    };
    if (h.maxMs > limits.stallBlocks * h.budgetMs)
        add ("stall");
    if (h.runDelayShare > limits.runDelayShare)
        add ("run-delay");
    if (h.khzBefore > 0.0 && h.khzAfter > 0.0 && std::min (h.khzBefore, h.khzAfter) < limits.freqShare * h.khzMax)
        add ("freq");
    if (h.majflt > limits.majflt)
        add ("majflt");
    if (h.tempMaxC > limits.tempMaxC)
        add ("temp");
    return out;
}
} // namespace synthyboy::perf
