// SynthyBoy - core tests: tiny test runner (no external framework).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TEST (name) { ... } registers a test case; CHECK / CHECK_EQ record a failure and continue.
// test_main.cpp provides main(): runs all registered cases (optionally those whose name contains argv[1]).
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>

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
