// SynthyBoy - MPC shell: the scheduling of the shell's own helper threads (display thread, statistics writer).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A std::thread inherits scheduling policy, priority, nice value and CPU affinity of the thread that creates it. The plugin is created by the host's loader
// (VSTPluginMain), which may be a thread of any policy, and the host's audio threads may run SCHED_FIFO on a pinned core. A helper thread must never be one
// of those: it calls into the host (audioMasterAutomate takes the host's locks), and a real-time thread that holds a host lock the audio side waits for
// is exactly the stall this plugin must not cause. So every helper thread sets its own policy first thing: SCHED_OTHER, priority 0, the given nice value,
// all CPUs. Lowering from a real-time policy to SCHED_OTHER needs no privilege; lowering a nice value (a creator with nice > 0) does need CAP_SYS_NICE
// (without it the inherited nice value stays; policy and affinity are reset in any case). Each step is best effort.
// The thread also gets a name (visible in top -H and /proc/<pid>/task/<tid>/comm).
#pragma once

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace synthyboy_mpc
{
/** Call at the start of a helper thread, from that thread. `name`: at most 15 characters. Returns the scheduling policy the thread has afterwards (SCHED_OTHER when it worked). */
inline int useNormalScheduling (int nice, const char* name) noexcept
{
    (void) pthread_setname_np (pthread_self(), name);
    sched_param sp {};
    sp.sched_priority = 0;
    (void) sched_setscheduler (0, SCHED_OTHER, &sp); // pid 0 = the calling thread (Linux)
    (void) setpriority (PRIO_PROCESS, static_cast<id_t> (syscall (SYS_gettid)), nice);
    long cpus = sysconf (_SC_NPROCESSORS_CONF);
    if (cpus < 1) cpus = 1;
    if (cpus > CPU_SETSIZE) cpus = CPU_SETSIZE;
    cpu_set_t all;
    CPU_ZERO (&all);
    for (long c = 0; c < cpus; ++c)
        CPU_SET (static_cast<int> (c), &all);
    (void) sched_setaffinity (0, sizeof (all), &all); // the kernel keeps the cpuset of the cgroup; an inherited single-core pin of the audio thread goes
    return sched_getscheduler (0);
}
} // namespace synthyboy_mpc
#else
namespace synthyboy_mpc
{
inline int useNormalScheduling (int, const char*) noexcept { return 0; }
} // namespace synthyboy_mpc
#endif
