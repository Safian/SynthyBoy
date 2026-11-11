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

namespace sbtest
{
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

struct State
{
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

#define CHECK(cond) sbtest::check (static_cast<bool> (cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) sbtest::check ((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) sbtest::check (std::fabs (static_cast<double> (a) - static_cast<double> (b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__)
