// SynthyBoy - measurement helper: the scheduling of a benchmark run (core pin, SCHED_FIFO, mlockall, the policy of the engine's helper threads, the
// scheduler's wait time of the measuring thread, the gap that keeps real-time throttling away). Linux only where the kernel is touched; elsewhere the
// functions report "not Linux: ignored". Header-only; the only engine dependency is the helper thread hook header.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#if defined(__linux__) && ! defined(_GNU_SOURCE)
 #define _GNU_SOURCE 1
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
 #include <pthread.h>
 #include <sched.h>
 #include <sys/mman.h>
 #include <unistd.h>
#endif

#include "perf/perf_io.h"
#include "synthyboy/helper_thread_hook.h"

namespace synthyboy::perf
{
/** "0-1,4,6-7" -> {0,1,4,6,7}. A trailing newline or blanks are ignored; an empty text or any malformed token gives {}. */
inline std::vector<int> parseCpuList (const std::string& text)
{
    std::string s = text;
    while (! s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    std::vector<int> out;
    if (s.empty())
        return out;
    size_t pos = 0;
    while (pos <= s.size())
    {
        size_t comma = s.find (',', pos);
        if (comma == std::string::npos)
            comma = s.size();
        const std::string token = s.substr (pos, comma - pos);
        if (token.empty())
            return {};
        const size_t dash = token.find ('-');
        auto number = [] (const std::string& t, int& v) {
            if (t.empty() || t.size() > 6)
                return false;
            v = 0;
            for (char c : t)
            {
                if (c < '0' || c > '9')
                    return false;
                v = v * 10 + (c - '0');
            }
            return true;
        };
        int lo = 0, hi = 0;
        if (dash == std::string::npos)
        {
            if (! number (token, lo))
                return {};
            hi = lo;
        }
        else if (! number (token.substr (0, dash), lo) || ! number (token.substr (dash + 1), hi) || hi < lo)
            return {};
        for (int c = lo; c <= hi; ++c)
            out.push_back (c);
        pos = comma + 1;
    }
    return out;
}

/** The online cpus without the measured one; when that leaves nothing, the online cpus. */
inline std::vector<int> helperCpus (const std::vector<int>& online, int measuredCore)
{
    std::vector<int> out;
    for (int c : online)
        if (c != measuredCore)
            out.push_back (c);
    return out.empty() ? online : out;
}

/** The three numbers of /proc/<pid>/schedstat: time on the cpu (ns), time waiting in a run queue (ns), number of slices. */
struct SchedStat
{
    unsigned long long runNs = 0, waitNs = 0, slices = 0;
    bool ok = false;
};

inline SchedStat parseSchedstat (const std::string& line)
{
    SchedStat s;
    unsigned long long a = 0, b = 0, c = 0;
    if (std::sscanf (line.c_str(), "%llu %llu %llu", &a, &b, &c) == 3)
    {
        s.runNs = a;
        s.waitNs = b;
        s.slices = c;
        s.ok = true;
    }
    return s;
}

/** wait / (run + wait); 0 when both are 0. */
inline double waitShare (const SchedStat& s) noexcept
{
    const double total = static_cast<double> (s.runNs) + static_cast<double> (s.waitNs);
    return total > 0.0 ? static_cast<double> (s.waitNs) / total : 0.0;
}

/** The shortest gap (ms) after every `every` blocks of blockMs so that the real-time duty stays below rtShare - margin: blockMs * every * (1 / (rtShare - margin) - 1). */
inline double minGapMs (double blockMs, int every, double rtShare, double margin) noexcept
{
    return blockMs * static_cast<double> (every) * (1.0 / (rtShare - margin) - 1.0);
}

/** After this many back-to-back blocks of a real-time run the bench sleeps a gap (outside the timed region). */
constexpr int kRtGapEvery = 64;

struct SchedRequest
{
    int core = -1;
    int fifoPriority = 0;
    bool mlockAll = false;
};

struct SchedResult
{
    bool affinityOk = false, fifoOk = false, mlockOk = false;
    int err = 0;
    std::string text;
};

#if defined(__linux__)
namespace detail
{
inline int& measuredCore() noexcept
{
    static int core = -1;
    return core;
}

inline void setAffinityTo (const std::vector<int>& cpus) noexcept
{
    cpu_set_t set;
    CPU_ZERO (&set);
    for (int c : cpus)
        if (c >= 0 && c < CPU_SETSIZE)
            CPU_SET (c, &set);
    (void) sched_setaffinity (0, sizeof (set), &set);
}

/** The start hook of the engine's helper threads (the pattern of shell-mpc/thread_policy.h): normal scheduling, off the measured core. */
inline void helperHook (const char* name)
{
    (void) pthread_setname_np (pthread_self(), name);
    sched_param sp {};
    sp.sched_priority = 0;
    (void) sched_setscheduler (0, SCHED_OTHER, &sp);
    setAffinityTo (helperCpus (parseCpuList (readTextFile ("/sys/devices/system/cpu/online")), measuredCore()));
}
} // namespace detail
#endif

/** Pin the calling thread, give it SCHED_FIFO, lock the memory (each only when requested). The text is the "rt:" line of the tools. */
inline SchedResult applySched (const SchedRequest& req)
{
    SchedResult r;
#if defined(__linux__)
    std::string affinity = "ok", policy = "ok", lock = "off";
    if (req.core >= 0)
    {
        cpu_set_t set;
        CPU_ZERO (&set);
        if (req.core < CPU_SETSIZE)
            CPU_SET (req.core, &set);
        if (req.core < CPU_SETSIZE && sched_setaffinity (0, sizeof (set), &set) == 0)
            r.affinityOk = true;
        else
        {
            affinity = "refused";
            r.err = errno;
        }
    }
    else
        affinity = "off";
    if (req.fifoPriority > 0)
    {
        sched_param sp {};
        sp.sched_priority = req.fifoPriority;
        if (sched_setscheduler (0, SCHED_FIFO, &sp) == 0)
            r.fifoOk = true;
        else
        {
            r.err = errno;
            policy = std::string ("refused (") + std::strerror (errno) + ")";
        }
    }
    else
        policy = "off";
    if (req.mlockAll)
    {
        if (mlockall (MCL_CURRENT | MCL_FUTURE) == 0)
        {
            r.mlockOk = true;
            lock = "ok";
        }
        else
        {
            r.err = errno;
            lock = "refused";
        }
    }
    r.text = "rt: requested FIFO " + std::to_string (req.fifoPriority) + " on cpu " + std::to_string (req.core) + ": affinity " + affinity + ", policy " + policy
             + ", mlockall " + lock;
#else
    (void) req;
    r.text = "rt: not Linux: ignored";
#endif
    return r;
}

/** Linux: engine helper threads (the effect builder, the effect job thread) run SCHED_OTHER, nice as inherited, on every online cpu but the measured one. Elsewhere nothing. */
inline void installHelperPolicyHook (int measuredCore)
{
#if defined(__linux__)
    detail::measuredCore() = measuredCore;
    synthyboy::setHelperThreadStartHook (&detail::helperHook);
#else
    (void) measuredCore;
#endif
}

/** The scheduler statistics of the calling thread (/proc/thread-self/schedstat); ok = false when the file is absent. */
inline SchedStat readThreadSchedstat()
{
#if defined(__linux__)
    return parseSchedstat (readTextFile ("/proc/thread-self/schedstat"));
#else
    return SchedStat {};
#endif
}

inline long readProcLong (const char* path)
{
    const std::string t = readTextFile (path);
    if (t.empty())
        return -1;
    char* end = nullptr;
    const long v = std::strtol (t.c_str(), &end, 10);
    return end == t.c_str() ? -1 : v;
}

/** /proc/sys/kernel/sched_rt_runtime_us and _period_us; -1 when absent (-1 is also the kernel's "unlimited" runtime). */
inline long readRtRuntimeUs() { return readProcLong ("/proc/sys/kernel/sched_rt_runtime_us"); }
inline long readRtPeriodUs() { return readProcLong ("/proc/sys/kernel/sched_rt_period_us"); }
} // namespace synthyboy::perf
