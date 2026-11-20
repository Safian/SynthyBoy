// SynthyBoy - core tests: shared helpers of the warp ADAA kernel tests (docs/plans/distortion-antialiasing.md tasks W0, W1, W2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "osc/warp.h"
#include "osc/warp_adaa_clip.h"

namespace aliaswarp
{
/** The exact average of warpSample over the segment y0 .. y1 (4096 midpoints, double accumulation). */
inline double segmentAverage (int type, float y0, float y1, float a, bool fast)
{
    constexpr int kSteps = 4096;
    double sum = 0.0;
    for (int j = 0; j < kSteps; ++j)
    {
        const double y = static_cast<double> (y0) + (static_cast<double> (j) + 0.5) / kSteps * (static_cast<double> (y1) - static_cast<double> (y0));
        sum += static_cast<double> (synthyboy::own::warpSample (type, static_cast<float> (y), a, 0.0f, fast));
    }
    return sum / kSteps;
}

/** The worst difference between a kernel (called as k (state, y, a) -> float, primed with y0) and the segment average over 2000 random pairs in [-1.2, 1.2] and a in {0.1, 0.5, 1.0}. */
template <class Kernel>
double worstError (int type, bool fast, Kernel kernel)
{
    uint32_t state = 123456789u;
    auto next = [&state] () {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state) * (2.4f / 4294967296.0f) - 1.2f;
    };
    const float amounts[3] = { 0.1f, 0.5f, 1.0f };
    double worst = 0.0;
    for (int i = 0; i < 2000; ++i)
    {
        const float y0 = next(), y1 = next(), a = amounts[i % 3];
        synthyboy::own::warpadaa::State s;
        (void) kernel (s, y0, a);
        const double got = static_cast<double> (kernel (s, y1, a));
        const double want = segmentAverage (type, y0, y1, a, fast);
        worst = std::fmax (worst, std::fabs (got - want));
    }
    return worst;
}
} // namespace aliaswarp
