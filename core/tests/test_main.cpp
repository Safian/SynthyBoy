// SynthyBoy - core tests: main().
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>

#include "test_runner.h"

int main (int argc, char** argv)
{
    auto& s = sbtest::state();
    int run = 0;
    int failedCases = 0;
    sbtest::runStartups();
    const auto t0 = std::chrono::steady_clock::now();

    for (auto* c = s.head; c != nullptr; c = c->next)
    {
        if (argc > 1 && std::strstr (c->name, argv[1]) == nullptr)
            continue;

        const long before = s.failures;
        const auto caseStart = std::chrono::steady_clock::now();
        std::printf ("[ RUN  ] %s\n", c->name);
        std::fflush (stdout);
        try
        {
            c->fn();
        }
        catch (const std::exception& e)
        {
            // reported as a failure of the case: an exception out of main() is only an opaque abort (0xc0000409 on Windows) and drops the buffered output
            ++s.failures;
            std::printf ("    EXCEPTION in %s: %s\n", c->name, e.what());
        }
        catch (...)
        {
            ++s.failures;
            std::printf ("    EXCEPTION in %s: unknown\n", c->name);
        }
        ++run;
        const double caseSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - caseStart).count();
        if (s.failures != before)
        {
            ++failedCases;
            std::printf ("[ FAIL ] %s (%.2f s)\n", c->name, caseSeconds);
        }
        else
        {
            std::printf ("[  OK  ] %s (%.2f s)\n", c->name, caseSeconds);
        }
        std::fflush (stdout); // a crash of a later case (or of the process exit) must not take the output of this one with it (stdout is a pipe under ctest)
    }

    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("%d test case(s), %ld check(s), %d failed case(s), %.2f s\n", run, s.checks, failedCases, seconds);
    std::fflush (stdout);
    sbtest::runShutdowns (s.startups);
    return (failedCases != 0 || run == 0) ? 1 : 0;
}
