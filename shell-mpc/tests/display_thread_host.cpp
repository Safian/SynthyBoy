// The kit's display thread (shell-mpc/kit/display_thread.h, migration step 11): the features run in the order they were added, once a tick; an exception is caught and counted, five in a row switch the
// feature off while the others go on; an unguarded step runs in its place; busy / idle periods; run () skips a tick while the host is not ready and ends on stop. No plugin, no host.
// Usage: display_thread_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "kit/display_thread.h"

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } }

int main()
{
    const std::string path = "/tmp/sb-displaythread-" + std::to_string (static_cast<long> (getpid()));
    synthyboy_mpc::PollStatus::setPath (path.c_str());
    using Clock = std::chrono::steady_clock;

    // ---- the order and the count
    {
        mpckit::DisplayThread dt (&fails, "test-display");
        std::vector<int> order;
        dt.add ("a", [&] { order.push_back (1); });
        dt.addUnguarded ([&] { order.push_back (2); });
        dt.add ("c", [&] { order.push_back (3); });
        dt.tick();
        dt.tick();
        check (order == std::vector<int> ({ 1, 2, 3, 1, 2, 3 }), "the features run in the order they were added, once a tick, the unguarded step in its place");
        check (dt.failures (0) == 0 && ! dt.off (0) && ! dt.off (2), "no failure, nothing off");
    }
    // ---- a failing feature: counted, five in a row switch it off, the others are not touched, a good tick clears the streak
    {
        mpckit::DisplayThread dt (&fails, "test-display");
        int bad = 0, good = 0, flaky = 0;
        bool flakyThrows = true;
        dt.add ("bad", [&] { ++bad; throw std::runtime_error ("boom"); });
        dt.add ("good", [&] { ++good; });
        dt.add ("flaky", [&] { ++flaky; if (flakyThrows) throw 7; });
        for (int i = 0; i < 4; ++i)
            dt.tick();
        flakyThrows = false;
        dt.tick();   // a good tick of the flaky one: its streak is cleared at 4, it would have been off at 5 in a row
        flakyThrows = true;
        for (int i = 0; i < 6; ++i)
            dt.tick();
        check (bad == 5 && dt.off (0) && dt.failures (0) == 5, "a feature that throws five times in a row is switched off and runs no more");
        check (good == 11, "the feature next to it ran on every tick, it never noticed");
        check (dt.failures (2) == 9 && dt.off (2) == true, "the flaky feature failed 4 times, was good once, failed 5 more times in a row and is off then");
    }
    // ---- run (): ready gating, busy and idle periods, stop
    {
        using ms = std::chrono::milliseconds;
        mpckit::DisplayThread dt (&fails, "test-display", { ms (5), ms (40) });
        std::atomic<bool> stop { false }, ready { false };
        std::atomic<int> ticks { 0 };
        dt.add ("count", [&] { ++ticks; });
        std::thread t ([&] { dt.run ([&] { return stop.load(); }, [&] { return ready.load(); }); });
        std::this_thread::sleep_for (ms (120));
        check (ticks.load() == 0, "while the host is not ready no tick runs");
        ready = true;
        std::this_thread::sleep_for (ms (300));
        const int idleTicks = ticks.load();
        check (idleTicks >= 4 && idleTicks <= 12, "idle: about one tick every 40 ms");
        dt.setBusy (true);
        std::this_thread::sleep_for (ms (300));
        const int busyTicks = ticks.load() - idleTicks;
        check (busyTicks > idleTicks * 2, "busy: the active period (5 ms) gives many more ticks");
        const auto before = Clock::now();
        stop = true;
        t.join();
        check (Clock::now() - before < ms (500), "stop is answered within a period");
    }
    // ---- the status file got the reports
    {
        FILE* f = std::fopen (path.c_str(), "rb");
        char b[2048] = {};
        const size_t n = f != nullptr ? std::fread (b, 1, sizeof b - 1, f) : 0;
        if (f != nullptr)
            std::fclose (f);
        check (n > 0 && std::string (b).find ("bad") != std::string::npos && std::string (b).find ("SWITCHED OFF") != std::string::npos, "the failures were written to the status file");
        ::unlink (path.c_str());
    }
    std::printf ("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
