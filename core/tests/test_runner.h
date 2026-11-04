// SynthyBoy - core tests: tiny test runner (no external framework).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TEST (name) { ... } registers a test case; CHECK / CHECK_EQ record a failure and continue.
// test_main.cpp provides main(): runs all registered cases (optionally those whose name contains argv[1]).
#pragma once

#include <cmath>
#include <cstdio>

namespace sbtest
{
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
        std::printf ("    CHECK FAILED %s:%d: %s\n", file, line, expr);
    }
    return ok;
}
} // namespace sbtest

#define TEST(name)                                                              \
    static void name();                                                         \
    static sbtest::Case name##_case { #name, name, nullptr };                   \
    static sbtest::Registrar name##_registrar (name##_case);                    \
    static void name()

#define CHECK(cond) sbtest::check (static_cast<bool> (cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) sbtest::check ((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) sbtest::check (std::fabs (static_cast<double> (a) - static_cast<double> (b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__)
