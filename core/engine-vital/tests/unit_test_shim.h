// SynthyBoy - engine-vital tests: a small stand-in for the parts of juce::UnitTest the ported Vital unit tests use
// (beginTest, expect, expectWithinAbsoluteError), mapped onto the core test runner (core/tests/test_runner.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "test_runner.h"

class UnitTest
{
public:
    UnitTest (const std::string& name, const std::string& category = std::string())
        : testName (category.empty() ? name : category + ": " + name)
    {
        registry().push_back (this);
    }

    virtual ~UnitTest() = default;
    virtual void runTest() = 0;

    /** All tests constructed so far (the ported test files construct one static instance each). */
    static std::vector<UnitTest*>& registry()
    {
        static std::vector<UnitTest*> tests;
        return tests;
    }

    /** Runs the test; every failed expect() is counted by the core test runner. */
    void run()
    {
        std::srand (1); // the processor tests use rand(): same inputs on every run
        subTest.clear();
        updateContext();
        runTest();
    }

    const std::string& getName() const { return testName; }

protected:
    void beginTest (const std::string& name)
    {
        subTest = name;
        updateContext();
    }

    void expect (bool condition, const std::string& message = std::string())
    {
        if (! condition && ! message.empty())
            std::printf ("    expect failed: %s\n", message.c_str());
        sbtest::check (condition, message.empty() ? "expect (condition)" : message.c_str(), __FILE__, __LINE__);
    }

    template <typename T>
    void expectWithinAbsoluteError (T actual, T expected, T maxAbsoluteError, const std::string& message = std::string())
    {
        const bool ok = std::fabs (static_cast<double> (actual) - static_cast<double> (expected)) <= static_cast<double> (maxAbsoluteError);
        if (! ok)
            std::printf ("    expectWithinAbsoluteError: %g is not within %g of %g %s\n", static_cast<double> (actual),
                         static_cast<double> (maxAbsoluteError), static_cast<double> (expected), message.c_str());
        sbtest::check (ok, "expectWithinAbsoluteError", __FILE__, __LINE__);
    }

private:
    void updateContext()
    {
        contextText = subTest.empty() ? testName : testName + " / " + subTest;
        sbtest::state().context = contextText.c_str();
    }

    std::string testName, subTest, contextText;
};

// The ported test files end with "SBVITAL_REGISTER_UNIT_TEST (Class, instance)" where upstream has "static Class instance;".
#define SBVITAL_REGISTER_UNIT_TEST(Class, instance) static Class instance;
