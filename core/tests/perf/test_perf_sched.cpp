// SynthyBoy - tests of the scheduling helpers of the benchmarks (bench/perf/perf_sched.h): cpu lists, helper core set, schedstat parsing, real-time gap.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <vector>

#include "perf/perf_sched.h"
#include "test_runner.h"

using namespace synthyboy::perf;

TEST (perf_sched)
{
    CHECK (parseCpuList ("2-3") == (std::vector<int> { 2, 3 }));
    CHECK (parseCpuList ("0-1,4,6-7") == (std::vector<int> { 0, 1, 4, 6, 7 }));
    CHECK (parseCpuList ("").empty());
    CHECK (parseCpuList ("\n").empty());
    CHECK (parseCpuList ("2-x").empty());

    CHECK (helperCpus ({ 0, 1, 2, 3 }, 3) == (std::vector<int> { 0, 1, 2 }));
    CHECK (helperCpus ({ 3 }, 3) == (std::vector<int> { 3 }));

    const SchedStat s = parseSchedstat ("9980000 20000 42\n");
    CHECK (s.ok);
    CHECK_EQ (s.runNs, 9980000ULL);
    CHECK_EQ (s.waitNs, 20000ULL);
    CHECK_EQ (s.slices, 42ULL);
    CHECK_EQ (waitShare (s), 0.002);   // 20000 / 10000000
    CHECK (! parseSchedstat ("garbage").ok);
    CHECK_EQ (waitShare (SchedStat {}), 0.0);

    // 64 blocks of 128 frames at 44.1 kHz = 185.76 ms; times (1 / 0.94 - 1) = 11.857 ms
    CHECK_NEAR (minGapMs (128.0 / 44100.0 * 1000.0, 64, 0.95, 0.01), 11.857, 0.01);
    CHECK_EQ (kRtGapEvery, 64);
}
