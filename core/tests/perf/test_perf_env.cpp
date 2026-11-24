// SynthyBoy - tests of the environment readers of the benchmarks (bench/perf/perf_env.h): thermal, memory, cache sizes, cache eviction.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <string>
#include <vector>

#include "perf/perf_env.h"
#include "test_runner.h"

using namespace synthyboy::perf;

TEST (perf_env)
{
    double c = 0.0;
    CHECK (parseMilliC ("68500\n", c));
    CHECK_EQ (c, 68.5);
    CHECK (! parseMilliC ("x", c));

    // the figure of docs/MPC.md:148
    CHECK_EQ (parseStatusKb ("Name:\tx\nVmRSS:\t  948244 kB\nThreads: 3\n", "VmRSS"), 948244L);
    CHECK_EQ (parseStatusKb ("Name:\tx\nThreads: 3\n", "VmRSS"), -1L);

    CHECK_EQ (parseCacheBytes ("32K\n"), 32768L);
    CHECK_EQ (parseCacheBytes ("2M"), 2097152L);
    CHECK_EQ (parseCacheBytes ("1024K"), 1048576L);
    CHECK_EQ (parseCacheBytes ("zz"), -1L);

    CHECK_EQ (evictBytes (std::vector<long> { 32768, 1048576 }), static_cast<size_t> (2097152));
    CHECK_EQ (evictBytes (std::vector<long> {}), static_cast<size_t> (4194304));

    std::vector<unsigned char> buffer (4096, 0);
    evictCaches (buffer);
    CHECK_EQ (static_cast<int> (buffer[0]), 1);
    CHECK_EQ (static_cast<int> (buffer[64]), 1);
    CHECK_EQ (static_cast<int> (buffer[1]), 0);
    CHECK_EQ (static_cast<int> (buffer[4032]), 1);
}
