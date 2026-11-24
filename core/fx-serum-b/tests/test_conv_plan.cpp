// SynthyBoy - core/fx-serum-b tests: the partition plan of the non-uniform convolution (docs/plans/reverb-convolve.md task C2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>

#include "conv_plan.h"
#include "test_runner.h"

using namespace synthyboy::fx;

namespace
{
bool planIs (int frames, int n1, int n2, int n3)
{
    const ConvPlan p = planFor (frames);
    return p.n[0] == n1 && p.n[1] == n2 && p.n[2] == n3;
}
} // namespace

TEST (conv_plan_table)
{
    CHECK (planIs (1, 0, 0, 0));
    CHECK (planIs (128, 0, 0, 0));
    CHECK (planIs (129, 1, 0, 0));
    CHECK (planIs (3969, 31, 0, 0));
    CHECK (planIs (5120, 39, 0, 0));
    CHECK (planIs (5121, 23, 3, 0));
    CHECK (planIs (30870, 23, 28, 0));
    CHECK (planIs (45056, 23, 41, 0));
    CHECK (planIs (45057, 23, 21, 3));
    CHECK (planIs (63945, 23, 21, 5));
    CHECK (planIs (524288, 23, 21, 61));
    CHECK (planIs (600000, 23, 21, 61));
}

TEST (conv_plan_invariants)
{
    for (int s = 0; s < 3; ++s)
    {
        const ConvStageSpec& st = kConvStages[s];
        CHECK_EQ (st.ticks, st.block / 128);
        CHECK (st.stagger <= st.ticks - 1);
        if (s == 0)
            CHECK (st.offset == st.block && st.block == 128);
        else
        {
            CHECK_EQ (st.offset, 3 * st.block);
            CHECK (st.ticks - 2 >= 1);
        }
    }
    int bad = 0;
    for (int f = 1; f <= kConvMaxFrames; ++f)
    {
        const ConvPlan p = planFor (f);
        const int n1 = p.n[0], n2 = p.n[1], n3 = p.n[2];
        if (n1 > 39 || n2 > 41 || n3 > 61)
            ++bad;
        else if (f <= 128)
        {
            if (n1 != 0 || n2 != 0 || n3 != 0)
                ++bad;
        }
        else if (n2 == 0 && n3 == 0)
        {
            if (128 + 128 * n1 < f)
                ++bad;
        }
        else if (n3 == 0)
        {
            if (n1 != 23 || 3072 + 1024 * n2 < f)
                ++bad;
        }
        else if (n1 != 23 || n2 != 21 || 24576 + 8192 * n3 < f)
            ++bad;
    }
    CHECK_EQ (bad, 0);
}

TEST (conv_plan_thresholds_are_the_break_even_points)
{
    // the cost of the plan of F is not larger (within 2 %: the thresholds are rounded) than the cost of the plan with a threshold moved by one partition of the stage below it
    int bad = 0;
    for (int f = 129; f <= kConvMaxFrames; f += 7)
    {
        const double c = planCost (planFor (f));
        for (const int d2 : { -128, 128 })
            if (c > planCost (planWith (f, kConvLevel2Min + d2, kConvLevel3Min)) * 1.02)
                ++bad;
        for (const int d3 : { -1024, 1024 })
            if (c > planCost (planWith (f, kConvLevel2Min, kConvLevel3Min + d3)) * 1.02)
                ++bad;
    }
    std::printf ("    plan cost of 63945 frames: %.0f flops per sample (model 1959)\n", planCost (planFor (63945)));
    CHECK_EQ (bad, 0);
}

TEST (conv_plan_bytes)
{
    CHECK_EQ (irSpectraBytes (planFor (524288)), 8388240u);
    CHECK_EQ (fdlBytes (capacityFor (524288)), 8537792u);
    CHECK_EQ (irSpectraBytes (planFor (63945)), 1047312u);
    CHECK_EQ (fdlBytes (capacityFor (63945)), 1721216u);
    const IrCapacity cap = capacityFor (63945);
    CHECK (cap.n[0] == 23 && cap.n[1] == 21 && cap.n[2] == 9);
}
