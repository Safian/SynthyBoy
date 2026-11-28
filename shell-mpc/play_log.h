// SynthyBoy - MPC shell (overlay build only): the debug log of the host's calls that the Play page depends on (docs/MPC.md "Play page", "Project save and reload").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The debug log is gone with the debug hooks (owner 2026-11-28): every function below is an empty inline one, so the calls stay harmless.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>



namespace synthyboy_mpc {


class PlayLog
{
public:
    static bool enabled() noexcept { return false; }
    static void refresh() noexcept {}
    static unsigned hash (const void*, size_t) noexcept { return 0; }
    static void countGet() noexcept {}
    static void line (const char*, ...) noexcept __attribute__ ((format (printf, 1, 2))) {}
    static void note (bool, int, int, int, int) noexcept {}
    static void drain() noexcept {}
    static void offerChunk (bool, const void*, size_t) noexcept {}
    static std::string dumpPath() { return std::string(); }
    static std::string path() { return std::string(); }
};


} // namespace synthyboy_mpc
