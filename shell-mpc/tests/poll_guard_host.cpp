// The poll guard (shell-mpc/poll_guard.h, review finding F2): no exception leaves a tick, failures are counted and written to the status file, five in a row switch the feature off, a good tick clears the streak.
// Usage: poll_guard_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

#include "poll_guard.h"

using namespace synthyboy_mpc;

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } }
static std::string slurp (const std::string& p)
{
    std::string s;
    if (FILE* f = std::fopen (p.c_str(), "rb")) { char b[1024]; size_t n; while ((n = std::fread (b, 1, sizeof b, f)) > 0) s.append (b, n); std::fclose (f); }
    return s;
}

int main()
{
    const std::string path = "/tmp/sb-pollguard-" + std::to_string (static_cast<long> (getpid()));
    PollStatus::setPath (path.c_str());

    PollGuard g ("test feature", &fails);
    int ran = 0;
    check (g.run ([&] { ++ran; }) && ran == 1 && g.streak == 0, "a good tick runs");
    check (! g.run ([&] { throw std::bad_alloc(); }) && g.streak == 1, "bad_alloc is caught");
    check (! g.run ([&] { throw std::runtime_error ("reason A"); }) && g.streak == 2, "runtime_error is caught");
    check (! g.run ([&] { throw 42; }) && g.streak == 3, "a non-exception throw is caught");
    check (g.run ([&] { ++ran; }) && g.streak == 0 && ! g.off, "a good tick clears the streak");
    for (int i = 0; i < kPollFailureLimit - 1; ++i)
        g.run ([&] { throw std::runtime_error ("reason B"); });
    check (! g.off, "one short of the limit the feature is on");
    g.run ([&] { throw std::runtime_error ("reason B"); });
    check (g.off, "the limit switches the feature off");
    const int before = ran;
    check (! g.run ([&] { ++ran; }) && ran == before, "an off feature is not run again");
    const std::string text = slurp (path);
    check (text.find ("test feature") != std::string::npos && text.find ("reason B") != std::string::npos && text.find ("SWITCHED OFF") != std::string::npos, "the status file names feature, reason and the switch off");
    check (PollStatus::events() >= 8, "events are counted");

    // a thread whose tick throws does not end the process
    PollGuard tg ("thread feature", nullptr);
    std::thread t ([&] { for (int i = 0; i < 20; ++i) tg.run ([] { throw std::length_error ("in a thread"); }); });
    t.join();
    check (tg.off && tg.failures == kPollFailureLimit, "a throwing tick in a thread is guarded");

    ::unlink (path.c_str());
    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
