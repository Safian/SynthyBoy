// SynthyBoy - measurement helper: reading a small text file (/proc, /sys). Shared by perf_sched.h, perf_env.h and perf_pmu.h. Header-only, no engine dependency.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdio>
#include <string>

namespace synthyboy::perf
{
/** The whole content of a text file (at most 64 KiB); "" when it cannot be opened. Not for the audio thread. */
inline std::string readTextFile (const std::string& path)
{
    std::string out;
    std::FILE* f = std::fopen (path.c_str(), "r");
    if (f == nullptr)
        return out;
    char buf[4096];
    size_t n = 0;
    while (out.size() < 65536 && (n = std::fread (buf, 1, sizeof (buf), f)) > 0)
        out.append (buf, n);
    std::fclose (f);
    return out;
}
} // namespace synthyboy::perf
