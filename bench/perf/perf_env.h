// SynthyBoy - measurement helper: what the machine reports around a benchmark run (temperature, resident memory, page faults, per-thread cpu time, cache sizes)
// and the eviction of the caches between blocks. The parsers are pure functions (tested on any host); the readers return "absent" values where the system file
// does not exist. Header-only, no engine dependency.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

#if ! defined(_WIN32)
 #include <dirent.h>
 #include <sys/resource.h>
#endif

#include "perf/perf_io.h"

namespace synthyboy::perf
{
/** "68500\n" (millidegrees, /sys/class/thermal/thermal_zone*\/temp) -> 68.5. False when the text is not a number. */
inline bool parseMilliC (const std::string& text, double& celsius)
{
    char* end = nullptr;
    const double v = std::strtod (text.c_str(), &end);
    if (end == text.c_str())
        return false;
    while (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')
        ++end;
    if (*end != '\0')
        return false;
    celsius = v / 1000.0;
    return true;
}

/** The value in kB of a line "<key>:   <n> kB" of the text of /proc/self/status; -1 when the key is absent. */
inline long parseStatusKb (const std::string& text, const std::string& key)
{
    const std::string needle = key + ":";
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find ('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        if (text.compare (pos, needle.size(), needle) == 0)
        {
            const std::string rest = text.substr (pos + needle.size(), eol - pos - needle.size());
            char* end = nullptr;
            const long v = std::strtol (rest.c_str(), &end, 10);
            return end == rest.c_str() ? -1L : v;
        }
        pos = eol + 1;
    }
    return -1L;
}

/** A cache size of /sys/devices/system/cpu/cpuN/cache/indexM/size: "32K" 32768, "2M" 2097152, "1G"; a plain number is bytes; anything else -1. */
inline long parseCacheBytes (const std::string& text)
{
    char* end = nullptr;
    const long v = std::strtol (text.c_str(), &end, 10);
    if (end == text.c_str() || v < 0)
        return -1L;
    long unit = 1;
    if (*end == 'K' || *end == 'k')
        unit = 1024L, ++end;
    else if (*end == 'M' || *end == 'm')
        unit = 1024L * 1024L, ++end;
    else if (*end == 'G' || *end == 'g')
        unit = 1024L * 1024L * 1024L, ++end;
    while (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')
        ++end;
    return *end == '\0' ? v * unit : -1L;
}

/** The size of the buffer that evicts the caches: twice the largest level; without any size 4 MiB. */
inline size_t evictBytes (const std::vector<long>& levelSizes)
{
    long best = 0;
    for (long s : levelSizes)
        best = std::max (best, s);
    return best > 0 ? static_cast<size_t> (best) * 2 : static_cast<size_t> (4194304);
}

struct Rusage
{
    long minflt = 0, majflt = 0, nvcsw = 0, nivcsw = 0;
};

/** The page faults and context switches of the calling thread (RUSAGE_THREAD on Linux, else of the process). */
inline Rusage readRusage()
{
    Rusage r;
#if ! defined(_WIN32)
 #if defined(__linux__) && defined(RUSAGE_THREAD)
    const int who = RUSAGE_THREAD;
 #else
    const int who = RUSAGE_SELF;
 #endif
    struct rusage u;
    std::memset (&u, 0, sizeof (u));
    if (getrusage (who, &u) == 0)
    {
        r.minflt = static_cast<long> (u.ru_minflt);
        r.majflt = static_cast<long> (u.ru_majflt);
        r.nvcsw = static_cast<long> (u.ru_nvcsw);
        r.nivcsw = static_cast<long> (u.ru_nivcsw);
    }
#endif
    return r;
}

/** The highest of /sys/class/thermal/thermal_zone*\/temp in degrees C; NaN when there is none. */
inline double maxThermalC()
{
    double best = std::numeric_limits<double>::quiet_NaN();
#if ! defined(_WIN32)
    if (DIR* d = opendir ("/sys/class/thermal"))
    {
        while (const dirent* e = readdir (d))
        {
            if (std::strncmp (e->d_name, "thermal_zone", 12) != 0)
                continue;
            double c = 0.0;
            if (parseMilliC (readTextFile (std::string ("/sys/class/thermal/") + e->d_name + "/temp"), c) && (std::isnan (best) || c > best))
                best = c;
        }
        closedir (d);
    }
#endif
    return best;
}

/** VmRSS of /proc/self/status in kB; -1 when absent. */
inline long readRssKb() { return parseStatusKb (readTextFile ("/proc/self/status"), "VmRSS"); }

/** The cpu time of the calling thread in ms (CLOCK_THREAD_CPUTIME_ID; the process cpu time where that clock does not exist). */
inline double threadCpuMs()
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts {};
    if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
        return static_cast<double> (ts.tv_sec) * 1000.0 + static_cast<double> (ts.tv_nsec) / 1.0e6;
#endif
    return 1000.0 * static_cast<double> (std::clock()) / static_cast<double> (CLOCKS_PER_SEC);
}

/** The sizes (bytes) of the caches of a cpu that count for eviction: level 2 and up, and every Data / Unified cache (/sys/devices/system/cpu/cpuN/cache/index0..7). */
inline std::vector<long> cacheLevelBytes (int cpu)
{
    std::vector<long> out;
    for (int i = 0; i < 8; ++i)
    {
        const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string (cpu) + "/cache/index" + std::to_string (i) + "/";
        const std::string sizeText = readTextFile (base + "size");
        if (sizeText.empty())
            continue;
        const long bytes = parseCacheBytes (sizeText);
        if (bytes <= 0)
            continue;
        const int level = std::atoi (readTextFile (base + "level").c_str());
        const std::string type = readTextFile (base + "type");
        if (level >= 2 || type.compare (0, 4, "Data") == 0 || type.compare (0, 7, "Unified") == 0)
            out.push_back (bytes);
    }
    return out;
}

/** A read-modify-write of every 64th byte: the working set of the engine is pushed out of the caches. */
inline void evictCaches (std::vector<unsigned char>& buffer)
{
    for (size_t i = 0; i < buffer.size(); i += 64)
        buffer[i] = static_cast<unsigned char> (buffer[i] + 1);
}
} // namespace synthyboy::perf
