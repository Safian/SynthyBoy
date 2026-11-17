// SynthyBoy - MPC shell: no exception may leave a tick of a thread of the plugin (robustness review F2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// An exception that leaves the function of a std::thread ends the process (std::terminate), and the process is the MPC application. Every tick of the display thread and of the overlay's service thread is run through a
// PollGuard: the exception is caught, counted, the reason is written to the status file of the shell (/tmp/synthyboy-mpc-status.txt, one line per feature, the last events of every feature), and after
// kPollFailureLimit failures IN A ROW the feature is switched off for the rest of the instance's life instead of failing again on every tick. A tick that succeeds clears the streak.
// What can throw under such a tick at all: std::bad_alloc from std::string / std::vector / make_shared (the copy of the content in lfoShapePoll, publishOverlayContent, the content browser), std::system_error from
// a std::mutex or a thread, and the host's own callback if it were a C++ one. The ticks of the display thread that only exchange atomics and call the host (the cursors, the overlay report, the Play proxies, the
// Q values, the dirty parameters) allocate nothing and cannot throw by themselves; they are wrapped as well, because a callback into the host and the code of later versions are not ours to promise about.
#pragma once

#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>

#include "safe_file.h"

namespace synthyboy_mpc {

constexpr int kPollFailureLimit = 5;

/** The status file of the shell: what the guarded ticks reported. Process wide; the display thread of every instance and the overlay service thread write to it. */
class PollStatus
{
public:
    static const char* defaultPath() noexcept { return "/tmp/synthyboy-mpc-status.txt"; }

    /** Tests give another path (and read the counters). */
    static void setPath (const char* path) noexcept
    {
        std::lock_guard<std::mutex> g (mutex());
        std::snprintf (pathBuf(), kPathBytes, "%s", path != nullptr ? path : "");
    }
    static long events() noexcept { std::lock_guard<std::mutex> g (mutex()); return count(); }

    /** One failed tick of `feature` of the instance `owner` (any pointer value: it only tells instances apart). Never throws. */
    static void report (const char* feature, const void* owner, int streak, bool switchedOff, const char* why) noexcept
    {
        try
        {
            std::lock_guard<std::mutex> g (mutex());
            ++count();
            Line& l = lines()[next()++ % kLines];
            std::snprintf (l.text, sizeof l.text, "%s (instance %p): %s %d time%s in a row%s: %s\n", feature != nullptr ? feature : "?", owner, "failed", streak, streak == 1 ? "" : "s",
                           switchedOff ? ", SWITCHED OFF for the rest of this instance" : "", why != nullptr ? why : "?");
            char all[kLines * sizeof (Line) + 64];
            size_t n = 0;
            n += static_cast<size_t> (std::snprintf (all + n, sizeof all - n, "events: %ld\n", count()));
            const size_t first = next() > kLines ? next() - kLines : 0;
            for (size_t i = first; i < next() && n < sizeof all; ++i)
                n += static_cast<size_t> (std::snprintf (all + n, sizeof all - n, "%s", lines()[i % kLines].text));
            if (n >= sizeof all)
                n = sizeof all - 1;
            const char* p = pathBuf()[0] != '\0' ? pathBuf() : defaultPath();
            safefile::writeSmall (p, all, n);
        }
        catch (...) {}
    }

private:
    static constexpr size_t kLines = 8, kPathBytes = 256;
    struct Line { char text[256]; };
    static std::mutex& mutex() { static std::mutex m; return m; }
    static Line* lines() { static Line l[kLines]; return l; }
    static size_t& next() { static size_t n = 0; return n; }
    static long& count() { static long c = 0; return c; }
    static char* pathBuf() { static char p[kPathBytes] = ""; return p; }
};

/** One feature's tick, guarded. `run (f)` calls f (); an exception ends the tick, is counted and reported; after kPollFailureLimit in a row the feature is off. Used by ONE thread. */
struct PollGuard
{
    const char* name = "";
    const void* owner = nullptr;
    int streak = 0;
    long failures = 0;
    bool off = false;

    PollGuard() = default;
    explicit PollGuard (const char* featureName, const void* instance = nullptr) noexcept : name (featureName), owner (instance) {}

    /** Returns false when the tick did not run (the feature is off) or failed. */
    template <class F>
    bool run (F&& tick) noexcept
    {
        if (off)
            return false;
        try
        {
            tick();
            streak = 0;
            return true;
        }
        catch (const std::exception& ex) { fail (ex.what()); }
        catch (...) { fail ("an exception that is not a std::exception"); }
        return false;
    }

private:
    void fail (const char* why) noexcept
    {
        ++streak;
        ++failures;
        if (streak >= kPollFailureLimit)
            off = true;
        PollStatus::report (name, owner, streak, off, why);
    }
};

} // namespace synthyboy_mpc
