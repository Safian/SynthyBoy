// SynthyBoy - engine-vital tests: runs the ported unit tests of the Vital-derived DSP (tests/synthesis/**).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>

#include "unit_test_shim.h"

TEST (vital_dsp_unit_tests)
{
    CHECK (! UnitTest::registry().empty());
    for (UnitTest* test : UnitTest::registry())
    {
        const long failuresBefore = sbtest::state().failures;
        const long checksBefore = sbtest::state().checks;
        test->run();
        const long failures = sbtest::state().failures - failuresBefore;
        std::printf ("    %-48s %5ld check(s), %ld failed\n", test->getName().c_str(), sbtest::state().checks - checksBefore, failures);
    }
    sbtest::state().context = "";
}
