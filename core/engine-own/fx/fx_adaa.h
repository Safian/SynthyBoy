// SynthyBoy - own engine: first-order antiderivative anti-aliasing (ADAA) of the distortion laws (docs/plans/distortion-antialiasing.md sections 3.0 and 3.2).
// For a memoryless law f with the antiderivative F the output is the average of f over the straight line between two input samples, (F (a) - F (b)) / (a - b), and f of the
// midpoint when the two samples are closer than kEps. F is split into a piecewise linear part Lin (exact in float for neighbouring samples) and a bounded part Bnd, so that the
// difference of two large values does not lose precision. The formulas are derived in the plan from public texts (Parker, Zavalishin, Le Bivic 2016; Bilbao et al. 2017).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../dsp/fastmath.h"
#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
namespace adaa
{
/** Below this distance of two input samples the output is the law at their midpoint. */
constexpr float kEps = 1.0f / 1024.0f;

/** The types (bit n = distortion type n) that run through the ADAA. Types 4 and 5 are lo-fi effects on purpose and never join. */
constexpr uint32_t kConverted = (1u << 1) | (1u << 11);

/** x = the last raw input (before the drive), g = the gain b was computed for, b = the bounded part of F at x * g. */
struct State
{
    float x = 0.0f, g = -1.0f, b = 0.0f;
};

/** The piecewise linear part of the antiderivative at v. */
inline float lin (int type, float v) noexcept
{
    switch (type)
    {
        case 1: return std::fabs (v);
        case 11: return v;
        default: return 0.0f;
    }
}

/** The bounded part of the antiderivative at v: F = lin + bnd. */
inline float bnd (int type, float v, bool fast) noexcept
{
    (void) fast;
    const float s = std::fabs (v);
    switch (type)
    {
        case 1: return s < 1.0f ? -0.5f + 0.5f * (1.0f - s) * (1.0f - s) : -0.5f;
        case 11:
        {
            const float h = s < 1.0f ? 0.5f * s * s : s - 0.5f; // H (s), the integral of min (s, 1)
            return (v < 0.0f ? -h : h) - v;
        }
        default: return 0.0f;
    }
}

/** Today's law (docs/engine-spec/06-effects.md section 4) of a converted type. */
inline float naive (int type, float v, bool fast) noexcept
{
    (void) fast;
    switch (type)
    {
        case 1: return std::clamp (v, -1.0f, 1.0f);
        case 11: return std::fabs (std::clamp (v, -1.0f, 1.0f));
        default: return v;
    }
}

/** One sample of the converted type `type`: x = the raw input, g = the drive gain. The result has no DC blocker applied. */
inline float step (int type, State& s, float x, float g, bool fast) noexcept
{
    const float v = x * g;
    if (!(std::fabs (v) < 1.0e6f)) // too large or not finite: reset the pair and return the law of the clamped value
    {
        const float c = v != v ? 0.0f : (v > 0.0f ? 1.0e6f : -1.0e6f);
        s.x = 0.0f;
        s.g = g;
        s.b = bnd (type, 0.0f, fast);
        return naive (type, c, fast);
    }
    const float vp = s.x * g;
    if (g != s.g) // the gain changed: the stored bounded part belongs to the old gain
    {
        s.b = bnd (type, vp, fast);
        s.g = g;
    }
    const float bv = bnd (type, v, fast);
    const float dv = v - vp;
    float y;
    if (std::fabs (dv) < kEps)
        y = naive (type, 0.5f * (v + vp), fast);
    else
        y = ((lin (type, v) - lin (type, vp)) + (bv - s.b)) / dv;
    s.x = flushDenormal (x);
    s.b = flushDenormal (bv);
    return y;
}
} // namespace adaa
} // namespace own
} // namespace synthyboy
