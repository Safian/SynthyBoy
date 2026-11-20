// SynthyBoy - measurement helper: hardware performance counters of the calling thread through perf_event_open(2) (one group per set, one read per call, user space
// only). Where the kernel has no perf events, the paranoid level forbids them, or the platform is not Linux, open() fails with a text that says why; nothing
// else in the benchmarks depends on it. Header-only, no engine dependency.
// The event numbers are the architectural ARM events named in docs/release/CPU-REVIEW-MEMORY.md (L1D refill 0x03, L1D access 0x04, L2D access 0x16, L2D refill 0x17,
// branch mispredicted 0x10).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

#include "perf/perf_io.h"

namespace synthyboy::perf
{
enum class PmuSet { compute, cache };

/** type 0 = PERF_TYPE_HARDWARE, 4 = PERF_TYPE_RAW (perf_event_open(2)). */
struct PmuEventSpec
{
    const char* name;
    unsigned type;
    unsigned long long config;
};

/** compute: cycles, instructions, branch mispredictions. cache: cycles, L1D refill, L1D access, L2D refill, L2D access. At most as many events as the core has counters, so nothing is multiplexed. */
inline std::vector<PmuEventSpec> eventSet (PmuSet set)
{
    if (set == PmuSet::compute)
        return { { "cycles", 0, 0 }, { "instructions", 0, 1 }, { "br_mis_pred", 4, 0x10 } };
    return { { "cycles", 0, 0 }, { "l1d_refill", 4, 0x03 }, { "l1d_access", 4, 0x04 }, { "l2d_refill", 4, 0x17 }, { "l2d_access", 4, 0x16 } };
}

/** The text of /proc/sys/kernel/perf_event_paranoid: "2\n" -> 2, "-1" -> -1; not a number -> 99. */
inline int parseParanoid (const std::string& text)
{
    char* end = nullptr;
    const long v = std::strtol (text.c_str(), &end, 10);
    if (end == text.c_str())
        return 99;
    return static_cast<int> (v);
}

/** Why perf_event_open failed, in words (the errno of the call and the paranoid level). */
inline std::string explainOpenError (int err, int paranoid)
{
    if ((err == EACCES || err == EPERM) && paranoid >= 2)
        return "perf_event_paranoid is " + std::to_string (paranoid) + " (needs 1 or less, or root / CAP_PERFMON)";
    if (err == EACCES || err == EPERM)
        return "permission denied (perf_event_paranoid " + std::to_string (paranoid) + ")";
    if (err == ENOENT)
        return "event not supported by this PMU";
    if (err == ENOSYS)
        return "kernel without CONFIG_PERF_EVENTS";
    if (err == ENODEV)
        return "no PMU driver for this CPU";
    if (err == EOPNOTSUPP)
        return "not supported";
    return "errno " + std::to_string (err);
}

/** The count scaled to the time the counter was enabled: raw * enabled / running; running == 0 -> -1 (never ran). */
inline double scaledCount (unsigned long long raw, unsigned long long enabled, unsigned long long running) noexcept
{
    if (running == 0)
        return -1.0;
    return static_cast<double> (raw) * static_cast<double> (enabled) / static_cast<double> (running);
}

/** numerator / denominator; 0 when the denominator is 0 (ipc, miss rate). */
inline double ratio (double numerator, double denominator) noexcept { return denominator == 0.0 ? 0.0 : numerator / denominator; }

struct PmuSample
{
    unsigned long long value[8] = {}, timeEnabled = 0, timeRunning = 0;
};

/** One counter group on the calling thread. open() starts the counters; read() is one system call. Not for the audio thread (the benchmark's measuring thread is not it). */
class PmuReader
{
public:
    PmuReader() = default;
    PmuReader (const PmuReader&) = delete;
    PmuReader& operator= (const PmuReader&) = delete;
    ~PmuReader() { close(); }

    bool open (PmuSet set, std::string& why) { return openEvents (eventSet (set), why); }

    /** The events of any list (at most 8, the first is the group leader); after a failure lastErrno() is the errno of the call. */
    bool openEvents (const std::vector<PmuEventSpec>& specs, std::string& why)
    {
        close();
        lastErrno_ = 0;
#if defined(__linux__) && defined(__NR_perf_event_open)
        if (specs.empty() || specs.size() > 8)
        {
            why = "no events or too many";
            return false;
        }
        for (const PmuEventSpec& spec : specs)
        {
            perf_event_attr attr;
            std::memset (&attr, 0, sizeof (attr));
            attr.size = sizeof (attr);
            attr.type = spec.type;
            attr.config = spec.config;
            attr.disabled = n_ == 0 ? 1 : 0;   // the leader only
            attr.exclude_kernel = 1;
            attr.exclude_hv = 1;
            attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            const int leader = n_ == 0 ? -1 : fds_[0];
            const long fd = ::syscall (__NR_perf_event_open, &attr, 0 /* this thread */, -1 /* any cpu */, leader, PERF_FLAG_FD_CLOEXEC);
            if (fd < 0)
            {
                lastErrno_ = errno;
                why = explainOpenError (errno, parseParanoid (readTextFile ("/proc/sys/kernel/perf_event_paranoid")));
                close();
                return false;
            }
            fds_[n_] = static_cast<int> (fd);
            names_[n_] = spec.name;
            ++n_;
        }
        (void) ::ioctl (fds_[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        (void) ::ioctl (fds_[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        why.clear();
        return true;
#else
        (void) specs;
        why = "not Linux";
        return false;
#endif
    }

    void close()
    {
#if defined(__linux__)
        for (int i = 0; i < n_; ++i)
            if (fds_[i] >= 0)
                ::close (fds_[i]);
#endif
        for (int i = 0; i < 8; ++i)
            fds_[i] = -1;
        n_ = 0;
    }

    /** One read of the whole group. False when closed or the kernel returned less than expected. */
    bool read (PmuSample& out) noexcept
    {
#if defined(__linux__)
        if (n_ <= 0)
            return false;
        unsigned long long buf[3 + 8] = {};
        const ssize_t got = ::read (fds_[0], buf, sizeof (buf));
        if (got < static_cast<ssize_t> ((3 + static_cast<size_t> (n_)) * sizeof (unsigned long long)) || static_cast<int> (buf[0]) != n_)
            return false;
        out.timeEnabled = buf[1];
        out.timeRunning = buf[2];
        for (int i = 0; i < n_; ++i)
            out.value[i] = buf[3 + i];
        return true;
#else
        (void) out;
        return false;
#endif
    }

    int lastErrno() const noexcept { return lastErrno_; }
    int count() const noexcept { return n_; }
    const char* name (int i) const noexcept { return i >= 0 && i < n_ ? names_[i] : ""; }

private:
    int fds_[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    const char* names_[8] = {};
    int n_ = 0;
    int lastErrno_ = 0;
};
} // namespace synthyboy::perf
