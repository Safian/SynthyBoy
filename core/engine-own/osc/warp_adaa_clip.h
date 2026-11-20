// SynthyBoy - own engine: first-order ADAA kernels of the sample-domain oscillator warps, the clip family and Quantize (docs/plans/distortion-antialiasing.md sections 3.4 and 3.5).
// The kernel returns the average of the warp law over the straight line between the previous and the current input sample, (F (b) - F (a)) / (b - a), with F the antiderivative
// of the law in closed form, so the stair steps and the corners of the law do not alias. Pure functions of (state, input, amount): no allocation, no libm beyond floor and fabs, one divide.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "../dsp/util.h"
#include "warp.h"

namespace synthyboy
{
namespace own
{
namespace warpadaa
{
/** Below this distance of two arguments of the closed form the output is the law at their midpoint. */
constexpr float kEps = 1.0f / 1024.0f;

/** y = the last input of the warp, a = the amount (or the Quantize levels) that b and c were computed for, b = F (or its bounded part) at the last input, c = the constant a tanh law
    subtracts (warp_adaa_tanh.h; unused by the others). */
struct State
{
    float y = 0.0f, a = -1.0f, b = 0.0f, c = 0.0f;
};

namespace detail
{
/** The gain applied to the input before the law (1 for the laws that work in y itself). */
inline float clipGain (int type, float a) noexcept
{
    switch (type)
    {
        case kWarpDistHardClip: return 1.0f + 8.0f * a;
        case kWarpDistLinearFold: return 1.0f + 6.0f * a;
        case kWarpDistTape: return 1.0f + 3.0f * a;
        default: return 1.0f;
    }
}

/** The piecewise linear part of F at t (the argument of the closed form): only Hard Clip splits. */
inline float clipLin (int type, float t) noexcept
{
    return type == kWarpDistHardClip ? std::fabs (t) : 0.0f;
}

/** The bounded part of F at t: for Hard Clip F - |t|, otherwise the whole antiderivative. */
inline float clipBnd (int type, float t, float a) noexcept
{
    const float s = std::fabs (t);
    switch (type)
    {
        case kWarpDistHardClip: return s < 1.0f ? -0.5f + 0.5f * (1.0f - s) * (1.0f - s) : -0.5f;
        case kWarpDistRectify: return (1.0f - a) * 0.5f * t * t + a * (t * s - 0.6366f * t);
        case kWarpDistZeroSquare: return (1.0f - a) * 0.5f * t * t + a * s;
        case kWarpDistLinearFold:
        {
            const float u = (t + 1.0f) * 0.25f;
            const float w = u - ffloor (u);
            return w < 0.5f ? 4.0f * w * (2.0f * w - 1.0f) : -4.0f * (2.0f * w - 1.0f) * (w - 1.0f);
        }
        case kWarpDistTape: return s <= 1.5f ? t * t * (0.5f - t * t * (1.0f / 27.0f)) : 0.9375f + (s - 1.5f);
        default: return 0.0f;
    }
}

/** The warp law at t (the midpoint branch): the same functions as warpSample. */
inline float clipLaw (int type, float t, float a) noexcept
{
    switch (type)
    {
        case kWarpDistHardClip: return std::clamp (t, -1.0f, 1.0f);
        case kWarpDistRectify: return lerp (t, std::fabs (t) * 2.0f - 0.6366f, a);
        case kWarpDistZeroSquare: return lerp (t, t >= 0.0f ? 1.0f : -1.0f, a);
        case kWarpDistLinearFold:
        {
            const float u = t + 1.0f;
            const float v = u - 4.0f * ffloor (u * 0.25f);
            return v < 2.0f ? v - 1.0f : 3.0f - v;
        }
        case kWarpDistTape:
        {
            const float c = std::clamp (t, -1.5f, 1.5f);
            return c - c * c * c * (4.0f / 27.0f);
        }
        default: return t;
    }
}
} // namespace detail

/** One sample of Hard Clip, Rectify, Zero-Square, Linear Fold and Tape. Returns false (and leaves everything alone) for any other type. */
inline bool stepClip (int type, State& s, float y, float a, bool fast, float& out) noexcept
{
    (void) fast;
    switch (type)
    {
        case kWarpDistHardClip: case kWarpDistRectify: case kWarpDistZeroSquare: case kWarpDistLinearFold: case kWarpDistTape: break;
        default: return false;
    }
    const float gain = detail::clipGain (type, a);
    const float t = y * gain;
    if (!(std::fabs (t) < 1.0e6f)) // not finite or enormous: reset the pair and give the law of the clamped value
    {
        s.y = 0.0f;
        s.a = a;
        s.b = detail::clipBnd (type, 0.0f, a);
        out = detail::clipLaw (type, t != t ? 0.0f : (t > 0.0f ? 1.0e6f : -1.0e6f), a);
        return true;
    }
    const float tp = s.y * gain;
    if (a != s.a) // the amount changed: the stored value belongs to the old amount
    {
        s.b = detail::clipBnd (type, tp, a);
        s.a = a;
    }
    const float bt = detail::clipBnd (type, t, a);
    const float dt = t - tp;
    if (std::fabs (dt) < kEps)
        out = detail::clipLaw (type, 0.5f * (t + tp), a);
    else
        out = ((detail::clipLin (type, t) - detail::clipLin (type, tp)) + (bt - s.b)) / dt;
    s.y = y;
    s.b = bt;
    return true;
}

/** The antiderivative of the Quantize staircase round (L y) / L: F = k y / L - k^2 / (2 L^2), k = floor (L y + 0.5). */
inline float quantizeF (float y, float levels) noexcept
{
    const float k = ffloor (levels * y + 0.5f);
    return k * y / levels - k * k / (2.0f * levels * levels);
}

/** One sample of Quantize: levels = 2 + (1 - a)^2 * 62 (warp.h). */
inline float stepQuantize (State& s, float y, float levels) noexcept
{
    if (!(std::fabs (y) < 1.0e6f))
    {
        s.y = 0.0f;
        s.a = levels;
        s.b = 0.0f;
        return 0.0f;
    }
    if (levels != s.a)
    {
        s.b = quantizeF (s.y, levels);
        s.a = levels;
    }
    const float bt = quantizeF (y, levels);
    const float dy = y - s.y;
    float out;
    if (std::fabs (dy) < kEps)
        out = ffloor (levels * (0.5f * (y + s.y)) + 0.5f) / levels;
    else
        out = (bt - s.b) / dy;
    s.y = y;
    s.b = bt;
    return out;
}
} // namespace warpadaa
} // namespace own
} // namespace synthyboy
