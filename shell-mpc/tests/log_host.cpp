// The developer log (shell-mpc/kit/log.h) in a dev build (MPCKIT_DEV_LOG 1): off until the control file switches a category on, the control file is re-read, the ring counts what it drops, the file is capped and
// rotated, lines carry category and level, and the writer thread does the whole thing by itself. The compiled-out mode is log_off_host.cpp, the audio-thread rule log_rt_host.cpp.
// Usage: log_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#define MPCKIT_DEV_LOG 1
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>

#include "kit/log.h"

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } }
static std::string slurp (const std::string& p)
{
    std::string s;
    if (FILE* f = std::fopen (p.c_str(), "rb")) { char b[4096]; size_t n; while ((n = std::fread (b, 1, sizeof b, f)) > 0) s.append (b, n); std::fclose (f); }
    return s;
}
static void put (const std::string& p, const std::string& text) { if (FILE* f = std::fopen (p.c_str(), "wb")) { std::fwrite (text.data(), 1, text.size(), f); std::fclose (f); } }
static long sizeOf (const std::string& p) { FILE* f = std::fopen (p.c_str(), "rb"); if (! f) return -1; std::fseek (f, 0, SEEK_END); const long n = std::ftell (f); std::fclose (f); return n; }

int main()
{
    using namespace mpckit;
    using namespace mpckit::devlog;
    const std::string base = "/tmp/sb-log-host-" + std::to_string (static_cast<long> (getpid()));
    const std::string logPath = base + ".log", confPath = base + ".conf";
    std::remove (logPath.c_str()); std::remove ((logPath + ".1").c_str()); std::remove (confPath.c_str());

    Logger lg;   // an own instance: the test does not depend on the process wide one
    Settings s;
    s.logPath = logPath; s.confPath = confPath; s.capBytes = 4096; s.thread = false;
    lg.start (s);

    // no control file: everything is off, and the arguments of a switched-off MPCKIT_LOG are not evaluated
    check (! lg.enabled ("startup", Level::error), "no control file: even errors are off");
    int evaluated = 0;
    MPCKIT_LOG ("startup", info, "never %d", ++evaluated);
    check (evaluated == 0, "a switched-off log call does not evaluate its arguments");

    // the control file: all=info, q=trace, startup=off
    put (confPath, "# comment\nall = info\nq=trace\nstartup=off\nbogus line\nx=loud\n");
    lg.refreshConfig();
    check (lg.enabled ("other", Level::info) && ! lg.enabled ("other", Level::debug), "all=info: info yes, debug no");
    check (lg.enabled ("q", Level::trace), "q=trace");
    check (! lg.enabled ("startup", Level::error), "startup=off");
    check (lg.enabled ("x", Level::info), "a line with an unknown level is ignored (x follows all)");
    put (confPath, "startup=debug\n");
    lg.refreshConfig();
    check (lg.enabled ("startup", Level::debug) && ! lg.enabled ("other", Level::error) && ! lg.enabled ("q", Level::error), "the file is re-read: startup on, all and q back to off");
    put (confPath, "all=info\n");
    lg.refreshConfig();

    // lines reach the file with category, level and text
    lg.writef ("net", Level::info, "hello %d", 1);
    lg.drainNow();
    check (slurp (logPath).find ("INF") != std::string::npos && slurp (logPath).find ("net hello 1") != std::string::npos, "a line reaches the file with level, category and text");
    std::remove (confPath.c_str());
    lg.refreshConfig();
    check (! lg.enabled ("net", Level::info), "the control file deleted: off again");
    lg.stop();

    // the writer thread, the real macro and the singleton: a record written, the file read back
    std::remove (logPath.c_str());
    {
        Settings t;
        t.logPath = logPath; t.confPath = confPath; t.capBytes = 1 << 20; t.thread = true;
        put (confPath, "all=info\n");
        Logger& g = logger();
        g.start (t);
        check (g.enabled ("thread", Level::info), "start reads the control file at once");
        MPCKIT_LOG ("thread", info, "from the macro %d %s", 7, "text");
        { MPCKIT_SCOPE ("thread", "a scope"); }
        bool found = false;
        for (int i = 0; i < 100 && ! found; ++i)   // the writer drains every 50 ms
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
            const std::string text = slurp (logPath);
            found = text.find ("INF") != std::string::npos && text.find ("thread from the macro 7 text") != std::string::npos && text.find ("a scope:") != std::string::npos;
        }
        check (found, "the writer thread writes the macro's line and the scope's line (level, category, text)");
        // the file switches a category on while it runs (about once a second)
        put (confPath, "all=off\nlive=info\n");
        bool on = false;
        for (int i = 0; i < 100 && ! on; ++i)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
            on = g.enabled ("live", Level::info) && ! g.enabled ("thread", Level::info);
        }
        check (on, "the control file is re-read by the writer thread within about a second");
        g.stop();
    }

    // a full ring drops and counts; the writer says so
    {
        std::remove (logPath.c_str());
        put (confPath, "all=info\n");
        Logger lg3;
        Settings t;
        t.logPath = logPath; t.confPath = confPath; t.capBytes = 1 << 20; t.thread = false;
        lg3.start (t);
        const size_t n = devlog::kRingSlots + 100;
        for (size_t i = 0; i < n; ++i)
            lg3.writef ("flood", Level::info, "line %d", static_cast<int> (i));
        lg3.drainNow();
        check (lg3.droppedTotal() >= 100, "records beyond the ring are dropped and counted");
        check (slurp (logPath).find ("records dropped") != std::string::npos, "the file says how many were dropped");
        lg3.stop();
    }

    // the cap: the file never grows past it, the old one is kept as .1
    {
        std::remove (logPath.c_str()); std::remove ((logPath + ".1").c_str());
        Logger lg4;
        Settings t;
        t.logPath = logPath; t.confPath = confPath; t.capBytes = 4096; t.thread = false;
        lg4.start (t);
        for (int round = 0; round < 60; ++round)
        {
            for (int i = 0; i < 10; ++i)
                lg4.writef ("cap", Level::info, "round %d line %d padding padding padding", round, i);
            lg4.drainNow();
        }
        check (sizeOf (logPath) >= 0 && sizeOf (logPath) <= 4096, "the log file stays within its cap");
        check (sizeOf (logPath + ".1") > 0 && sizeOf (logPath + ".1") <= 4096, "the previous file is kept as .1, also within the cap");
        lg4.stop();
    }

    std::remove (logPath.c_str()); std::remove ((logPath + ".1").c_str()); std::remove (confPath.c_str());
    if (fails == 0)
        std::printf ("log_host: all passed\n");
    return fails == 0 ? 0 : 1;
}
