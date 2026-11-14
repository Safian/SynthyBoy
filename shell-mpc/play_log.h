// SynthyBoy - MPC shell (overlay build only): the debug log of the host's calls that the Play page depends on (docs/MPC.md "Play page", "Project save and reload").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Off unless the file /tmp/synthyboy-play-log exists (environment variable SYNTHYBOY_MPC_PLAY_LOG_FILE changes the path; it is looked at on every tick of the display thread, so creating or deleting the
// file switches the log on and off while the app runs). Then every call of effSetChunk, effGetChunk, effSetProgram and setParameter is appended as one line
//     <counter> <milliseconds since the first line> <what> <arguments> [-> what the plugin did]
// to /tmp/synthyboy-play-log.txt (the log file's own name plus ".txt"), together with the number of getParameter calls since the previous line. The file stops growing at 512 KB (the last line says so).
// A request file /tmp/synthyboy-play-dump (the log path plus nothing: "<log path>" is the switch, "<log path>-dump" the request) makes the display thread write the state of the instance to
// "<log path>-dump.txt" and delete the request. Nothing here runs in a build without the overlay, nothing of it is on the audio thread.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <sys/stat.h>

namespace synthyboy_mpc {

class PlayLog
{
public:
    static constexpr long kMaxBytes = 512 * 1024;

    static const std::string& path()
    {
        static const std::string p = [] { const char* e = std::getenv ("SYNTHYBOY_MPC_PLAY_LOG_FILE"); return std::string (e != nullptr && *e != 0 ? e : "/tmp/synthyboy-play-log"); }();
        return p;
    }
    static bool exists (const std::string& p) { struct stat st; return ::stat (p.c_str(), &st) == 0; }

    /** true while the switch file exists, as of the last refresh() (any thread, a relaxed atomic read: nothing here touches the file system on the audio thread). */
    static bool enabled() noexcept { return on().load (std::memory_order_relaxed); }
    /** Looks at the switch file (the display thread every tick, and a new instance once): the log goes on and off within a tick while the app runs. */
    static void refresh() noexcept { on().store (exists (path()), std::memory_order_relaxed); }

    /** FNV-1a of a chunk: the lines of a save and of the following load can be compared by eye. */
    static unsigned hash (const void* data, size_t size) noexcept
    {
        unsigned h = 2166136261u;
        for (size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<unsigned char> (static_cast<const char*> (data)[i])) * 16777619u;
        return h;
    }

    /** Counts a getParameter call (the next line reports the count). */
    static void countGet() noexcept { gets().fetch_add (1, std::memory_order_relaxed); }

    static void line (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)))
    {
        if (! enabled())
            return;
        char buf[512];
        va_list ap;
        va_start (ap, fmt);
        std::vsnprintf (buf, sizeof buf, fmt, ap);
        va_end (ap);
        std::lock_guard<std::mutex> g (mutex());
        State& s = state();
        if (s.full)
            return;
        const long long now = nowMs();
        if (s.first < 0)
            s.first = now;
        const std::string file = path() + ".txt";
        FILE* f = std::fopen (file.c_str(), "a");
        if (f == nullptr)
            return;
        std::fseek (f, 0, SEEK_END);
        if (std::ftell (f) > kMaxBytes)
        {
            std::fprintf (f, "-- log full (%ld bytes), nothing more is written --\n", kMaxBytes);
            s.full = true;
        }
        else
        {
            const long g2 = gets().exchange (0, std::memory_order_relaxed);
            if (g2 > 0)
                std::fprintf (f, "%lld %lld getParameter x%ld\n", ++s.counter, now - s.first, g2);
            std::fprintf (f, "%lld %lld %s\n", ++s.counter, now - s.first, buf);
        }
        std::fclose (f);
    }

    /** The dump request: true once when "<log path>-dump" exists (the file is deleted). Display thread. */
    static bool dumpRequested()
    {
        if (! enabled())
            return false;
        const std::string req = path() + "-dump";
        if (! exists (req))
            return false;
        std::remove (req.c_str());
        return true;
    }
    static std::string dumpPath() { return path() + "-dump.txt"; }

private:
    struct State { long long counter = 0; long long first = -1; bool full = false; };
    static State& state() { static State s; return s; }
    static std::mutex& mutex() { static std::mutex m; return m; }
    static std::atomic<long>& gets() { static std::atomic<long> g { 0 }; return g; }
    static std::atomic<bool>& on() { static std::atomic<bool> b { false }; return b; }
    static long long nowMs() noexcept { return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
};

} // namespace synthyboy_mpc
