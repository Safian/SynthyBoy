// SynthyBoy - MPC overlay: the cores the overlay stays off (Linux): reads the online and the isolated cores and the names and affinities of the application's own threads
// (/proc/self/task/<tid>/status) and applies discovery.h's planCpus to the calling thread (and, by inheritance or sched_setaffinity, to the GPU helper).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#if defined(__linux__)
#include <cstdio>
#include <string>
#include <vector>

#include <dirent.h>
#include <sched.h>
#include <unistd.h>

#include "discovery.h"

namespace synthyboy_mpc { namespace overlay {

inline std::string readSmallFile (const std::string& path)
{
    std::string s;
    if (FILE* f = std::fopen (path.c_str(), "r"))
    {
        char buf[512];
        size_t n;
        while ((n = std::fread (buf, 1, sizeof buf, f)) > 0 && s.size() < 8192)
            s.append (buf, n);
        std::fclose (f);
    }
    return s;
}

/** The name and the affinity of every thread of this process. */
inline std::vector<ThreadInfo> readOwnThreads()
{
    std::vector<ThreadInfo> out;
    if (DIR* d = opendir ("/proc/self/task"))
    {
        while (dirent* e = readdir (d))
        {
            if (e->d_name[0] < '0' || e->d_name[0] > '9')
                continue;
            const std::string text = readSmallFile (std::string ("/proc/self/task/") + e->d_name + "/status");
            ThreadInfo t;
            const size_t n = text.find ("Name:");
            if (n != std::string::npos)
            {
                size_t b = n + 5;
                while (b < text.size() && (text[b] == ' ' || text[b] == '\t'))
                    ++b;
                const size_t eol = text.find ('\n', b);
                t.name = text.substr (b, eol == std::string::npos ? std::string::npos : eol - b);
            }
            const size_t c = text.find ("Cpus_allowed_list:");
            if (c != std::string::npos)
            {
                size_t b = c + 18;
                while (b < text.size() && (text[b] == ' ' || text[b] == '\t'))
                    ++b;
                const size_t eol = text.find ('\n', b);
                t.cpus = parseCpuList (text.substr (b, eol == std::string::npos ? std::string::npos : eol - b));
            }
            out.push_back (t);
        }
        closedir (d);
    }
    return out;
}

struct CpuState
{
    std::vector<int> online, isolated;
    CpuPlan plan;
};

/** Reads the system and the threads and makes the plan. */
inline CpuState planOwnCpus()
{
    CpuState s;
    s.online = parseCpuList (readSmallFile ("/sys/devices/system/cpu/online"));
    s.isolated = parseCpuList (readSmallFile ("/sys/devices/system/cpu/isolated"));
    if (s.online.empty())
    {
        const long n = sysconf (_SC_NPROCESSORS_ONLN);
        for (long i = 0; i < n; ++i)
            s.online.push_back (static_cast<int> (i));
    }
    s.plan = planCpus (s.online, s.isolated, readOwnThreads());
    return s;
}

inline bool setAffinity (pid_t pid, const std::vector<int>& cpus)
{
    if (cpus.empty())
        return false;
    cpu_set_t set;
    CPU_ZERO (&set);
    for (int c : cpus)
        if (c >= 0 && c < CPU_SETSIZE)
            CPU_SET (c, &set);
    return sched_setaffinity (pid, sizeof set, &set) == 0;
}

/** The affinity of a thread or process as the kernel reports it ("0-3"). */
inline std::string affinityOf (pid_t pid)
{
    cpu_set_t set;
    if (sched_getaffinity (pid, sizeof set, &set) != 0)
        return "unknown";
    std::vector<int> v;
    for (int c = 0; c < CPU_SETSIZE; ++c)
        if (CPU_ISSET (c, &set))
            v.push_back (c);
    return formatCpuList (v);
}

}} // namespace synthyboy_mpc::overlay
#endif // __linux__
