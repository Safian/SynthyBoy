// SynthyBoy - MPC overlay: timestamps of the page guard handshake (g132), for the latency of the overlay's reappearance after a page change. OFF by default: a line is written only while the file
// /tmp/synthyboy-overlay-guard-trace exists (one access() call per event; the events happen only around a guard plane-off, never at rest). The clock is CLOCK_MONOTONIC in ns, the same as the `time_ns`
// of the commit shim log (xl-commit-shim) and of gpoll, so the lines can be put on one time line (scripts/guard-trace-correlate.py).
// Line: `<time_ns> <who> <event> <a> <b>`; who = helper | plugin. Events (a, b):
//   helper  guard_flag_read        the helper read LayerBlock::guard_plane_off (the shim turned the plane off with a host commit)
//   helper  guard_sent   epoch     kMsgGuard sent to the plugin
//   helper  page_ok_recv epoch view  kMsgPageOk read
//   helper  first_present view     the first picture presented after the plane-off (the overlay is back)
//   plugin  guard_recv   epoch     kMsgGuard read by the overlay thread (takeGuardReport)
//   plugin  detect       view      a page detection run for a pending guard (view -1: no page found)
//   plugin  page_ok_sent epoch view  kMsgPageOk written
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace synthyboy_mpc { namespace overlay { namespace gpu {

inline uint64_t monotonicNs() noexcept
{
    timespec ts {};
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t> (ts.tv_sec) * 1000000000ull + static_cast<uint64_t> (ts.tv_nsec);
}

inline void guardTrace (const char* who, const char* event, long a = 0, long b = 0) noexcept
{
    if (::access ("/tmp/synthyboy-overlay-guard-trace", F_OK) != 0)
        return;
    if (FILE* f = std::fopen ("/tmp/synthyboy-overlay-guard-trace.log", "a"))
    {
        std::fprintf (f, "%llu %s %s %ld %ld\n", static_cast<unsigned long long> (monotonicNs()), who, event, a, b);
        std::fclose (f);
    }
}

}}}
