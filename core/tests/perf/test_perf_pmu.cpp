// SynthyBoy - tests of the PMU counter helpers of the benchmarks (bench/perf/perf_pmu.h): event sets, scaling, ratios, error texts. The counters themselves are
// not opened here (a CI machine may not allow it).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cerrno>
#include <string>
#include <vector>

#include "perf/perf_pmu.h"
#include "test_runner.h"

using namespace synthyboy::perf;

TEST (perf_pmu)
{
    // the events of docs/release/CPU-REVIEW-MEMORY.md:344
    const std::vector<PmuEventSpec> cache = eventSet (PmuSet::cache);
    CHECK_EQ (cache.size(), static_cast<size_t> (5));
    if (cache.size() == 5)
    {
        const char* names[5] = { "cycles", "l1d_refill", "l1d_access", "l2d_refill", "l2d_access" };
        const unsigned long long configs[5] = { 0, 0x03, 0x04, 0x17, 0x16 };
        for (int i = 0; i < 5; ++i)
        {
            CHECK_EQ (std::string (cache[static_cast<size_t> (i)].name), std::string (names[i]));
            CHECK_EQ (cache[static_cast<size_t> (i)].config, configs[i]);
        }
    }
    const std::vector<PmuEventSpec> compute = eventSet (PmuSet::compute);
    CHECK_EQ (compute.size(), static_cast<size_t> (3));
    if (compute.size() == 3)
    {
        const char* names[3] = { "cycles", "instructions", "br_mis_pred" };
        const unsigned long long configs[3] = { 0, 1, 0x10 };
        for (int i = 0; i < 3; ++i)
        {
            CHECK_EQ (std::string (compute[static_cast<size_t> (i)].name), std::string (names[i]));
            CHECK_EQ (compute[static_cast<size_t> (i)].config, configs[i]);
        }
    }

    CHECK_EQ (parseParanoid ("2\n"), 2);
    CHECK_EQ (parseParanoid ("-1"), -1);
    CHECK_EQ (parseParanoid ("x"), 99);

    CHECK_EQ (scaledCount (1000, 200, 100), 2000.0);
    CHECK_EQ (scaledCount (1000, 100, 100), 1000.0);
    CHECK_EQ (scaledCount (1000, 100, 0), -1.0);

    CHECK_EQ (ratio (1.2e6, 2.0e6), 0.6);
    CHECK_EQ (ratio (250, 5000), 0.05);
    CHECK_EQ (ratio (1, 0), 0.0);

    CHECK (explainOpenError (EACCES, 2).find ("perf_event_paranoid") != std::string::npos);
    CHECK (explainOpenError (ENOSYS, 2).find ("CONFIG_PERF_EVENTS") != std::string::npos);
    CHECK (explainOpenError (ENOENT, 2).find ("not supported") != std::string::npos);
}
