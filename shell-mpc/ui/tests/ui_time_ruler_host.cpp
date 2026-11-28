// SynthyBoy - MPC plugin-drawn layer: host test of time_ruler.h (the round time marks under a graph whose x axis is time, e.g. the envelope display).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../time_ruler.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace synthyboy_mpc::ui;

static int failures = 0;

static void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::printf ("FAIL  %s\n", what);
        ++failures;
    }
}

static bool near (double a, double b, double tol = 1e-3) { return std::fabs (a - b) <= tol; }

static void label (double ms, const char* expected)
{
    char buf[16];
    formatRulerTime (ms, buf, sizeof buf);
    char what[96];
    std::snprintf (what, sizeof what, "label of %g ms is \"%s\" (got \"%s\")", ms, expected, buf);
    check (std::strcmp (buf, expected) == 0, what);
}

int main()
{
    // the step: the smallest of 1, 2, 5 x 10^n milliseconds whose marks are at least minSpacingPx apart
    check (near (rulerStepMs (1000.0, 800.0f, 80.0f), 100.0), "1000 ms on 800 px, 80 px apart: step 100 ms");
    check (near (rulerStepMs (1050.0, 800.0f, 80.0f), 200.0), "1050 ms on 800 px, 80 px apart: 100 ms would be 76 px, so 200 ms");
    check (near (rulerStepMs (5.0, 400.0f, 100.0f), 2.0), "5 ms on 400 px, 100 px apart: step 2 ms");
    check (near (rulerStepMs (30000.0, 900.0f, 60.0f), 2000.0), "30 s on 900 px, 60 px apart: step 2 s");
    check (near (rulerStepMs (0.3, 300.0f, 100.0f), 0.1, 1e-6), "0.3 ms on 300 px, 100 px apart: step 0.1 ms");
    check (near (rulerStepMs (700.0, 1000.0f, 70.0f), 50.0), "700 ms on 1000 px, 70 px apart: step 50 ms");

    // the marks: 0, step, 2 step ... up to the total; x is linear in time
    {
        const TimeRuler r = makeTimeRuler (1000.0, 40.0f, 800.0f, 80.0f);
        check (r.count == 11, "1000 ms: 11 marks (0 .. 1000 in steps of 100)");
        check (near (r.stepMs, 100.0), "the ruler carries its step");
        check (r.count > 0 && near (r.ticks[0].x, 40.0) && near (r.ticks[0].ms, 0.0), "the first mark is at the left edge, time 0");
        check (r.count == 11 && near (r.ticks[10].x, 840.0) && near (r.ticks[10].ms, 1000.0), "the last mark is at the right edge, time 1000");
        check (r.count == 11 && near (r.ticks[3].x, 40.0 + 800.0 * 0.3) && near (r.ticks[3].ms, 300.0), "the mark of 300 ms is at 30 % of the width");
        check (r.count == 11 && std::strcmp (r.ticks[0].label, "0") == 0 && std::strcmp (r.ticks[5].label, "500 ms") == 0 && std::strcmp (r.ticks[10].label, "1 s") == 0, "labels 0, 500 ms, 1 s");
    }
    {
        const TimeRuler r = makeTimeRuler (1050.0, 0.0f, 800.0f, 80.0f);
        check (r.count == 6, "1050 ms: marks 0, 200 .. 1000 (6), none beyond the total");
        check (r.count == 6 && near (r.ticks[5].ms, 1000.0) && near (r.ticks[5].x, 800.0 * 1000.0 / 1050.0), "the last mark is at 1000 ms, left of the right edge");
    }
    {
        const TimeRuler r = makeTimeRuler (0.3, 0.0f, 300.0f, 100.0f);
        check (r.count == 4, "0.3 ms: marks 0, 0.1, 0.2, 0.3 (the last one must not be lost to rounding)");
        check (r.count == 4 && std::strcmp (r.ticks[3].label, "0.3 ms") == 0, "label 0.3 ms");
    }
    {
        const TimeRuler r = makeTimeRuler (30000.0, 0.0f, 900.0f, 60.0f);
        check (r.count == 16, "30 s: 16 marks in steps of 2 s");
        check (r.count == 16 && std::strcmp (r.ticks[1].label, "2 s") == 0 && std::strcmp (r.ticks[15].label, "30 s") == 0, "labels 2 s and 30 s");
    }

    // never more marks than the structure holds: the step grows instead
    {
        const TimeRuler r = makeTimeRuler (100000.0, 0.0f, 4000.0f, 1.0f);
        check (r.count <= TimeRuler::kMaxTicks && r.count >= 2, "a tiny spacing does not overflow the marks");
        bool even = true;
        for (int i = 1; i < r.count; ++i)
            even = even && near (r.ticks[i].ms - r.ticks[i - 1].ms, r.stepMs, 1e-6 * r.stepMs + 1e-9);
        check (even, "the marks are one step apart");
        const double m = r.stepMs / std::pow (10.0, std::floor (std::log10 (r.stepMs)));
        check (near (m, 1.0, 1e-6) || near (m, 2.0, 1e-6) || near (m, 5.0, 1e-6), "the grown step is still 1, 2 or 5 times a power of ten");
    }

    // nothing to draw
    check (makeTimeRuler (0.0, 0.0f, 800.0f, 80.0f).count == 0, "total 0: no marks");
    check (makeTimeRuler (-5.0, 0.0f, 800.0f, 80.0f).count == 0, "negative total: no marks");
    check (makeTimeRuler (1000.0, 0.0f, 0.0f, 80.0f).count == 0, "width 0: no marks");
    check (makeTimeRuler (1000.0, 0.0f, 800.0f, 0.0f).count >= 2, "spacing 0 is treated as 1 px: still marks, no division by zero");
    check (makeTimeRuler (1000.0, 0.0f, 50.0f, 80.0f).count == 1, "a width smaller than the spacing: only the mark at 0");

    // the labels
    label (0.0, "0");
    label (0.5, "0.5 ms");
    label (5.0, "5 ms");
    label (250.0, "250 ms");
    label (999.0, "999 ms");
    label (1000.0, "1 s");
    label (1500.0, "1.5 s");
    label (2250.0, "2.25 s");
    label (10000.0, "10 s");
    label (0.25, "0.25 ms");
    {
        char small[4];
        formatRulerTime (250.0, small, sizeof small);
        check (std::strlen (small) <= 3, "a short buffer is not overrun and stays terminated");
    }

    {
        char one[1] = { 'x' };
        formatRulerTime (0.0, one, sizeof one);
        check (one[0] == '\0', "a buffer of one byte gets only the terminator, also for 0");
        const TimeRuler r = makeTimeRuler (1e15, 0.0f, 100000.0f, 1.0f);
        check (r.count >= 2 && r.count <= TimeRuler::kMaxTicks, "a huge ratio of time to step does not overflow the count");
        check (near (rulerStepMs (100.00000000000001 * 8.0, 800.0f, 100.0f), 100.0), "an exact fit is not pushed to the next step by rounding");
    }

    if (failures == 0)
        std::printf ("ui_time_ruler_host: OK\n");
    return failures == 0 ? 0 : 1;
}
