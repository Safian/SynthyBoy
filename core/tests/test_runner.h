// SynthyBoy - core tests: tiny test runner (no external framework).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TEST (name) { ... } registers a test case; CHECK / CHECK_EQ record a failure and continue.
// test_main.cpp provides main(): runs all registered cases (optionally those whose name contains argv[1]).
#pragma once

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <time.h>
#endif

// A build under a sanitizer is 5 to 20 times slower than a native one.
#if defined (__SANITIZE_THREAD__) || defined (__SANITIZE_ADDRESS__)
 #define SBTEST_SANITIZED 1
#elif defined (__has_feature)
 #if __has_feature (thread_sanitizer) || __has_feature (address_sanitizer)
  #define SBTEST_SANITIZED 1
 #endif
#endif
#ifndef SBTEST_SANITIZED
 #define SBTEST_SANITIZED 0
#endif

namespace sbtest
{
/** True when the run is much slower than a native one: built with a sanitizer, or SBTEST_INSTRUMENTED=1 in the environment (the CI jobs that run under QEMU user mode or
    ThreadSanitizer set it). Such a run keeps every correctness check but reduces the amount of work with scaled () and skips wall clock budgets; the native jobs run both in full. */
inline bool instrumented()
{
    static const bool value = [] {
        const char* env = std::getenv ("SBTEST_INSTRUMENTED");
        return SBTEST_SANITIZED != 0 || (env != nullptr && env[0] != '\0' && env[0] != '0');
    }();
    return value;
}

/** CPU time of the calling thread in milliseconds (the time this thread was running, not the time that passed: another process or another test thread taking the core does not
    count). A speed check of a test that runs in parallel with the whole suite uses this and the best of several repetitions, never the wall clock. Falls back to the wall clock
    where the platform has no per thread CPU clock. */
inline double threadCpuMs()
{
#ifdef _WIN32
    FILETIME creation, exitTime, kernel, user;
    if (GetThreadTimes (GetCurrentThread(), &creation, &exitTime, &kernel, &user))
    {
        auto ticks = [] (const FILETIME& f) { return static_cast<double> ((static_cast<unsigned long long> (f.dwHighDateTime) << 32) | f.dwLowDateTime); };   // 100 ns units
        return (ticks (kernel) + ticks (user)) / 1.0e4;
    }
#elif defined (CLOCK_THREAD_CPUTIME_ID)
    timespec t;
    if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t) == 0)
        return static_cast<double> (t.tv_sec) * 1.0e3 + static_cast<double> (t.tv_nsec) / 1.0e6;
#endif
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

/** Runs `fn` `repetitions` times and returns the smallest thread CPU time of one run in milliseconds (the best of several: a run that was slowed down, by a cache miss or a
    frequency step, does not decide). */
template <class Fn>
double bestCpuMs (int repetitions, Fn&& fn)
{
    double best = 1.0e300;
    for (int i = 0; i < (repetitions < 1 ? 1 : repetitions); ++i)
    {
        const double t0 = threadCpuMs();
        fn();
        const double ms = threadCpuMs() - t0;
        if (ms < best)
            best = ms;
    }
    return best;
}

/** The amount of work of a test: full natively, reduced (but never below 1) in an instrumented run. */
inline int scaled (int native, int instrumentedValue)
{
    return instrumented() ? (instrumentedValue < 1 ? 1 : instrumentedValue) : native;
}

/** setenv / unsetenv that also build with MSVC (the CRT has _putenv_s only). */
inline void setEnv (const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s (name, value);
#else
    setenv (name, value, 1);
#endif
}

inline void unsetEnv (const char* name)
{
#ifdef _WIN32
    _putenv_s (name, "");
#else
    unsetenv (name);
#endif
}


/** The quick / full split of the heavy test cases. Default (SB_TEST_FULL unset, empty or 0): the quick development run, in which a heavy case may use a smaller amount of work or skip a
    multi minute sweep, but only through skipInQuick () / quickScaled () below, which print a "[ SKIP-QUICK ]" line each and are counted in the summary of main (never silently).
    SB_TEST_FULL=1: every case runs exactly as it always did. The ctest entries "<name>_full" run the binaries that have such cases with SB_TEST_FULL=1 (label slow). */
inline bool full()
{
    static const bool value = [] {
        const char* env = std::getenv ("SB_TEST_FULL");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    return value;
}

struct Case
{
    const char* name;
    void (*fn) ();
    Case* next;
};

/** Work that has to happen inside main(), not during static initialisation (starting JUCE's message manager before main() crashes on Windows: the CRT and the modules' own
    statics are not up yet, and the order between translation units is unspecified). start runs before the first test case, stop after the last one, in reverse order. */
struct Startup
{
    void (*start) ();
    void (*stop) ();
    Startup* next;
};

struct State
{
    Startup* startups = nullptr;
    Startup* startupsTail = nullptr;
    Case* head = nullptr;
    Case* tail = nullptr;
    long checks = 0;
    long failures = 0;
    int quickReductions = 0;   // heavy cases that ran reduced or not at all in the quick run (always 0 with SB_TEST_FULL=1)
    long quickSkippedChecks = 0; // the checks those reductions did not run, as declared by the test (measured with SB_TEST_FULL=1 against the quick run)
    const char* context = ""; // shown with every failure (e.g. the engine under test)
};

inline State& state()
{
    static State s;
    return s;
}

struct Registrar
{
    explicit Registrar (Case& c)
    {
        auto& s = state();
        if (s.tail != nullptr)
            s.tail->next = &c;
        else
            s.head = &c;
        s.tail = &c;
    }
};

struct StartupRegistrar
{
    StartupRegistrar (Startup& u, void (*start) (), void (*stop) ())
    {
        u.start = start;
        u.stop = stop;
        u.next = nullptr;
        auto& s = state();
        if (s.startupsTail != nullptr)
            s.startupsTail->next = &u;
        else
            s.startups = &u;
        s.startupsTail = &u;
    }
};

inline void runStartups()
{
    for (auto* u = state().startups; u != nullptr; u = u->next)
        if (u->start != nullptr)
            u->start();
}

inline void runShutdowns (Startup* u)
{
    if (u == nullptr)
        return;
    runShutdowns (u->next);
    if (u->stop != nullptr)
        u->stop();
}

/** Quick run only: true (after printing the "[ SKIP-QUICK ]" line) when the caller has to skip the heavy part; `skippedChecks` is the number of CHECKs that part would have recorded
    (declared by the test, verified against the full run). Always false with SB_TEST_FULL=1. */
inline bool skipInQuick (const char* what, long skippedChecks, const char* reason)
{
    if (full())
        return false;
    auto& s = state();
    ++s.quickReductions;
    s.quickSkippedChecks += skippedChecks;
    std::printf ("[ SKIP-QUICK ] %s: %s (%ld check(s) not run), run with SB_TEST_FULL=1\n", what, reason, skippedChecks);
    std::fflush (stdout);
    return true;
}

/** The amount of work of a heavy case: `fullValue` with SB_TEST_FULL=1, `quickValue` in the quick run (printed as a "[ SKIP-QUICK ]" line). `skippedChecks` is the number of CHECKs the
    quick run records fewer than the full run (0 when the number of checks does not depend on the amount of work). */
inline int quickScaled (const char* what, int fullValue, int quickValue, long skippedChecks = 0)
{
    if (full())
        return fullValue;
    auto& s = state();
    ++s.quickReductions;
    s.quickSkippedChecks += skippedChecks;
    std::printf ("[ SKIP-QUICK ] %s: reduced from %d to %d (%ld check(s) not run), run with SB_TEST_FULL=1\n", what, fullValue, quickValue, skippedChecks);
    std::fflush (stdout);
    return quickValue;
}

inline bool check (bool ok, const char* expr, const char* file, int line)
{
    ++state().checks;
    if (! ok)
    {
        ++state().failures;
        if (state().context[0] != '\0')
            std::printf ("    CHECK FAILED %s:%d: %s   [%s]\n", file, line, expr, state().context);
        else
            std::printf ("    CHECK FAILED %s:%d: %s\n", file, line, expr);
        std::fflush (stdout); // a crash right after a failed check must not lose the report
    }
    return ok;
}

/** Names the object under test for the failure messages of its scope. */
struct Context
{
    explicit Context (const char* name) : previous (state().context) { state().context = name; }
    ~Context() { state().context = previous; }
    const char* previous;
};
} // namespace sbtest

#define TEST(name)                                                              \
    static void name();                                                         \
    static sbtest::Case name##_case { #name, name, nullptr };                   \
    static sbtest::Registrar name##_registrar (name##_case);                    \
    static void name()

/** SBTEST_STARTUP (id, start, stop): start() runs at the beginning of main(), stop() after the last test case (either may be null). */
#define SBTEST_STARTUP(id, startFn, stopFn)                                                \
    static sbtest::Startup id##_startup {};                                                \
    static sbtest::StartupRegistrar id##_startupRegistrar (id##_startup, startFn, stopFn)

#define CHECK(cond) sbtest::check (static_cast<bool> (cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) sbtest::check ((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) sbtest::check (std::fabs (static_cast<double> (a) - static_cast<double> (b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__)
