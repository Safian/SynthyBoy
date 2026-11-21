// SynthyBoy - benchmark: which hardware performance counters a machine lets the benchmarks read (bench/perf/perf_pmu.h). Prints the perf_event_paranoid level, for
// every event of both sets whether it opens alone, and what a group of each set counts in a 50 ms floating-point loop. The result line says per set: usable (opened,
// the counters ran the whole time), multiplexed (opened, but they shared counters: the numbers are scaled estimates) or unavailable.
// Linux only; on any other platform both sets are unavailable and the exit code is 0.
//
//   synthyboy_pmu_probe [--cpu N]
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
 #include <sched.h>
#endif

#include "perf/perf_io.h"
#include "perf/perf_pmu.h"

using namespace synthyboy::perf;

namespace
{
volatile double g_sink = 0.0;

/** About 50 ms of floating-point work and some memory traffic, so every counter has something to count. */
void spin50ms()
{
    std::vector<float> buffer (1 << 18, 1.0f);
    const auto start = std::chrono::steady_clock::now();
    double acc = 0.0;
    int round = 0;
    while (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count() < 50.0)
    {
        for (size_t i = 0; i < buffer.size(); i += 7)
        {
            buffer[i] = buffer[i] * 1.0001f + 0.5f;
            acc += std::sqrt (static_cast<double> (buffer[i]));
        }
        ++round;
    }
    g_sink = acc + round;
}

const char* setName (PmuSet set) { return set == PmuSet::cache ? "cache" : "compute"; }

/** Opens the group of the set, spins, reads, prints; returns usable / multiplexed / unavailable. */
const char* probeSet (PmuSet set)
{
    PmuReader reader;
    std::string why;
    if (! reader.open (set, why))
    {
        std::printf ("PMU-PROBE set=%s unavailable why=%s\n", setName (set), why.c_str());
        return "unavailable";
    }
    PmuSample before, after;
    if (! reader.read (before))
    {
        std::printf ("PMU-PROBE set=%s unavailable why=read failed\n", setName (set));
        return "unavailable";
    }
    spin50ms();
    if (! reader.read (after))
    {
        std::printf ("PMU-PROBE set=%s unavailable why=read failed\n", setName (set));
        return "unavailable";
    }
    const unsigned long long enabled = after.timeEnabled - before.timeEnabled;
    const unsigned long long running = after.timeRunning - before.timeRunning;
    std::printf ("PMU-PROBE set=%s", setName (set));
    for (int i = 0; i < reader.count(); ++i)
        std::printf (" %s=%.0f", reader.name (i), scaledCount (after.value[i] - before.value[i], enabled, running));
    std::printf (" enabled=%llu running=%llu\n", enabled, running);
    if (running == 0)
        return "unavailable";
    return running < enabled ? "multiplexed" : "usable";
}
} // namespace

int main (int argc, char** argv)
{
    int cpu = -1;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--cpu") == 0 && i + 1 < argc)
            cpu = std::atoi (argv[++i]);
        else
        {
            std::fprintf (stderr, "usage: synthyboy_pmu_probe [--cpu N]\n");
            return 2;
        }
    }
#if defined(__linux__)
    if (cpu >= 0 && cpu < CPU_SETSIZE)
    {
        cpu_set_t set;
        CPU_ZERO (&set);
        CPU_SET (cpu, &set);
        if (sched_setaffinity (0, sizeof (set), &set) != 0)
            std::printf ("PMU-PROBE pin cpu=%d refused\n", cpu);
    }
#else
    (void) cpu;
#endif

    std::printf ("PMU-PROBE paranoid=%d\n", parseParanoid (readTextFile ("/proc/sys/kernel/perf_event_paranoid")));
    for (PmuSet set : { PmuSet::compute, PmuSet::cache })
        for (const PmuEventSpec& spec : eventSet (set))
        {
            PmuReader one;
            std::string why;
            const bool ok = one.openEvents ({ spec }, why);
            std::printf ("PMU-PROBE event=%s status=%s errno=%d why=%s\n", spec.name, ok ? "ok" : "fail", ok ? 0 : one.lastErrno(), ok ? "" : why.c_str());
        }
    const char* cache = probeSet (PmuSet::cache);
    const char* compute = probeSet (PmuSet::compute);
    std::printf ("PMU-PROBE-RESULT cache=%s compute=%s\n", cache, compute);
    return 0;
}
