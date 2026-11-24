// The play log of the debug-hooks build (shell-mpc/play_log.h, review finding F8): line () formats into a ring and never touches a file, drain () writes (from one thread), many threads can log at once, the file stops at its cap.
// Built with -DSYNTHYBOY_MPC_DEBUG_HOOKS=1. Usage: play_log_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#define SYNTHYBOY_MPC_DEBUG_HOOKS 1
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "play_log.h"

using synthyboy_mpc::PlayLog;

static int fails = 0;
static void check (bool ok, const std::string& what) { if (! ok) { std::printf ("FAIL %s\n", what.c_str()); ++fails; } }
static std::string slurp (const std::string& p)
{
    std::string s;
    if (FILE* f = std::fopen (p.c_str(), "rb")) { char b[65536]; size_t n; while ((n = std::fread (b, 1, sizeof b, f)) > 0) s.append (b, n); std::fclose (f); }
    return s;
}
static size_t count (const std::string& s, const std::string& what) { size_t n = 0; for (size_t at = s.find (what); at != std::string::npos; at = s.find (what, at + 1)) ++n; return n; }

int main()
{
    const std::string base = "/tmp/sb-playlog-" + std::to_string (static_cast<long> (getpid()));
    setenv ("SYNTHYBOY_MPC_PLAY_LOG_FILE", base.c_str(), 1);
    const std::string txt = base + ".txt";
    std::remove (txt.c_str());

    PlayLog::line ("not logged: the switch is off");
    PlayLog::drain();
    check (slurp (txt).empty(), "switch off: nothing is written");

    if (FILE* f = std::fopen (base.c_str(), "w")) std::fclose (f);
    PlayLog::refresh();
    check (PlayLog::enabled(), "the switch file turns the log on");
    for (int i = 0; i < 100; ++i)
        PlayLog::line ("setParameter %d (real) %.9g -> written", i, 0.25 * i);
    check (slurp (txt).empty(), "line () writes no file (the calling thread does no I/O)");
    PlayLog::drain();
    const std::string a = slurp (txt);
    check (count (a, "setParameter") == 100 && a.find ("setParameter 99 (real) 24.75 -> written") != std::string::npos, "drain () writes the 100 lines in order");

    // note logging (debug hooks build): note_on and note_off format, dropped count
    PlayLog::note (true, 1, 60, 40, 40);
    PlayLog::note (true, 1, 62, 127, 127);
    PlayLog::note (false, 1, 60, 0, 0);
    PlayLog::drain();
    const std::string noteLog = slurp (txt);
    check (noteLog.find ("note_on channel 1 note 60 vel 40 engine 40 dropped 0") != std::string::npos, "PlayLog::note formats velocity 40");
    check (noteLog.find ("note_on channel 1 note 62 vel 127 engine 127 dropped 0") != std::string::npos, "PlayLog::note formats velocity 127");
    check (noteLog.find ("note_off channel 1 note 60 vel 0 engine 0 dropped 0") != std::string::npos, "PlayLog::note formats note_off");

    // eight threads at once: every line is there once or counted as lost
    std::vector<std::thread> ts;
    std::atomic<bool> go { false };
    for (int t = 0; t < 8; ++t)
        ts.emplace_back ([&, t] { while (! go.load()) {} for (int i = 0; i < 400; ++i) PlayLog::line ("thread %d line %d", t, i); });
    go.store (true);
    for (auto& t : ts) t.join();
    PlayLog::drain();
    const std::string b = slurp (txt);
    size_t got = 0;
    for (int t = 0; t < 8; ++t) got += count (b, "thread " + std::to_string (t) + " line ");
    check (got == 3200, "eight threads: 3200 lines (the ring holds 4096)");

    // a flood bigger than the ring: the extra lines are dropped and said so, nothing blocks
    for (int i = 0; i < 20000; ++i)
        PlayLog::line ("flood %d", i);
    PlayLog::drain();
    check (slurp (txt).find ("lines lost") != std::string::npos, "a flood past the ring is reported as lost lines");

    // the cap
    for (int round = 0; round < 200; ++round)
    {
        for (int i = 0; i < 3000; ++i)
            PlayLog::line ("fill %d %d 0123456789012345678901234567890123456789", round, i);
        PlayLog::drain();
    }
    const std::string full = slurp (txt);
    check (full.size() < 600 * 1024 && full.find ("log full") != std::string::npos, "the file stops at its cap (" + std::to_string (full.size()) + " bytes)");

    // the chunk copies go through the display thread too
    PlayLog::offerChunk (true, "{\"a\":1}", 7);
    check (slurp (base + "-chunk-get.json").empty(), "offerChunk writes no file");
    PlayLog::drain();
    check (slurp (base + "-chunk-get.json") == "{\"a\":1}", "drain () writes the chunk");

    std::remove (txt.c_str()); std::remove (base.c_str()); std::remove ((base + "-chunk-get.json").c_str());
    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
