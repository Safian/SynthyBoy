// SynthyBoy - own engine: the totals of the section timing and the read access of the benchmark (engine.h, ownSection...).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "section_timing.h"

#include <ctime>

#include "synthyboy/engine.h"

namespace synthyboy
{
namespace own
{
namespace sections
{
Totals& totals() noexcept
{
    static Totals t;
    return t;
}

uint64_t nowNs() noexcept
{
    timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t> (ts.tv_sec) * 1000000000ull + static_cast<uint64_t> (ts.tv_nsec);
}
} // namespace sections
} // namespace own

bool ownSectionTimingAvailable() noexcept
{
#ifdef SB_SECTION_TIMING
    return true;
#else
    return false;
#endif
}

int ownSectionCount() noexcept { return own::sections::kCount; }
const char* ownSectionName (int index) noexcept { return own::sections::name (index); }

void ownSectionReset() noexcept { own::sections::totals() = own::sections::Totals(); }

void ownSectionRead (int index, double& totalMs, unsigned long long& calls) noexcept
{
    const auto& t = own::sections::totals();
    totalMs = index >= 0 && index < own::sections::kCount ? static_cast<double> (t.ns[index]) * 1.0e-6 : 0.0;
    calls = index >= 0 && index < own::sections::kCount ? t.calls[index] : 0;
}

double ownSectionClockCostNs() noexcept
{
    // the cost of one scope (two clock reads and the bookkeeping) as the engine pays it: a loop of empty scopes
    constexpr int kN = 20000;
    const uint64_t t0 = own::sections::nowNs();
    for (int i = 0; i < kN; ++i)
    {
        const uint64_t a = own::sections::nowNs();
        volatile uint64_t sink = a;
        (void) sink;
        const uint64_t b = own::sections::nowNs();
        volatile uint64_t sink2 = b;
        (void) sink2;
    }
    const uint64_t t1 = own::sections::nowNs();
    return static_cast<double> (t1 - t0) / kN / 2.0 * 2.0; // two reads per scope
}
} // namespace synthyboy
