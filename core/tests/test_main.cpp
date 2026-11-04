// SynthyBoy - core tests: main().
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <chrono>
#include <cstdio>
#include <cstring>

#include "test_runner.h"

int main (int argc, char** argv)
{
    auto& s = sbtest::state();
    int run = 0;
    int failedCases = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (auto* c = s.head; c != nullptr; c = c->next)
    {
        if (argc > 1 && std::strstr (c->name, argv[1]) == nullptr)
            continue;

        const long before = s.failures;
        std::printf ("[ RUN  ] %s\n", c->name);
        std::fflush (stdout);
        c->fn();
        ++run;
        if (s.failures != before)
        {
            ++failedCases;
            std::printf ("[ FAIL ] %s\n", c->name);
        }
        else
        {
            std::printf ("[  OK  ] %s\n", c->name);
        }
    }

    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("%d test case(s), %ld check(s), %d failed case(s), %.2f s\n", run, s.checks, failedCases, seconds);
    return (failedCases != 0 || run == 0) ? 1 : 0;
}
