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
constexpr uint32_t kConverted = (1u << 0) | (1u << 1) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11);

/** x = the last raw input (before the drive), g = the gain b was computed for, b = the bounded part of F at x * g. */
struct State
{
    float x = 0.0f, g = -1.0f, b = 0.0f;
};

/** tanh (0.3), the constant the Tube law (type 6) subtracts. */
constexpr float kTanh03 = 0.29131261f;
constexpr float kLn2 = 0.69314718f;

/** ln cosh (t) - |t| = L (|t|) - ln 2 with L (s) = log1p (exp (-2 s)): bounded, between -0.7 and 0. */
inline float lncoshBnd (float t, bool fast) noexcept
{
    const float e = fast ? fastmath::exp (-2.0f * std::fabs (t)) : std::exp (-2.0f * std::fabs (t));
    const float l = fast ? kLn2 * fastmath::log2 (1.0f + e) : std::log1p (e);
    return l - kLn2;
}

/** E (s) = 1 - exp (-s), s >= 0. */
inline float oneMinusExp (float s, bool fast) noexcept
{
    return 1.0f - (fast ? fastmath::exp (-s) : std::exp (-s));
}

/** The piecewise linear part of the antiderivative at t, the argument of the closed form (t = v; type 9: 32 v; type 6: v + 0.3). */
inline float lin (int type, float t) noexcept
{
    switch (type)
    {
        case 0: case 6: case 9: case 1: return std::fabs (t);
        case 11: return t;
        case 10: case 7: return t >= 0.0f ? t : 0.5f * -t;
        case 8: return std::fabs (t);
        default: return 0.0f;
    }
}

/** The bounded part of the antiderivative at t: F = lin + bnd. */
inline float bnd (int type, float t, bool fast) noexcept
{
    const float s = std::fabs (t);
    switch (type)
    {
        case 0: case 6: case 9: return lncoshBnd (t, fast);
        case 1: return s < 1.0f ? -0.5f + 0.5f * (1.0f - s) * (1.0f - s) : -0.5f;
        case 11:
        {
            const float h = s < 1.0f ? 0.5f * s * s : s - 0.5f; // H (s), the integral of min (s, 1)
            return (t < 0.0f ? -h : h) - t;
        }
        case 10: return t >= 0.0f ? lncoshBnd (t, fast) : (s >= 0.5f ? -0.125f : 0.5f * t * t - 0.5f * s);
        case 7: return t >= 0.0f ? -oneMinusExp (s, fast) : -0.25f * oneMinusExp (2.0f * s, fast); // F = v + e^-v - 1 above zero, -v / 2 + e^2v / 4 - 1/4 below
        case 8: return -oneMinusExp (s, fast);
        default: return 0.0f;
    }
}

/** Today's law (docs/engine-spec/06-effects.md section 4) of a converted type at t (the argument of the closed form). */
inline float naive (int type, float t, bool fast) noexcept
{
    auto th = [fast] (float a) noexcept { return fast ? fastmath::tanh (a) : std::tanh (a); };
    switch (type)
    {
        case 0: case 9: return th (t);
        case 6: return th (t) - kTanh03;
        case 10: return th (std::max (t, 0.0f)) + std::max (std::min (t, 0.0f), -0.5f);
        case 7:
        {
            auto ex = [fast] (float a) noexcept { return fast ? fastmath::exp (a) : std::exp (a); };
            return (1.0f - ex (-std::max (t, 0.0f))) - 0.5f * (1.0f - ex (2.0f * std::min (t, 0.0f)));
        }
        case 8:
        {
            auto ex = [fast] (float a) noexcept { return fast ? fastmath::exp (a) : std::exp (a); };
            return ex (std::min (t, 0.0f)) - ex (-std::max (t, 0.0f));
        }
        case 1: return std::clamp (t, -1.0f, 1.0f);
        case 11: return std::fabs (std::clamp (t, -1.0f, 1.0f));
        default: return t;
    }
}

/** One sample of the converted type `type`: x = the raw input, g = the drive gain. The result has no DC blocker applied. */
inline float step (int type, State& s, float x, float g, bool fast) noexcept
{
    const float ge = type == 9 ? 32.0f * g : g; // Zero-Square is tanh (32 v)
    const float off = type == 6 ? 0.3f : 0.0f;  // Tube is tanh (v + 0.3) - tanh (0.3)
    const float sub = type == 6 ? kTanh03 : 0.0f;
    const float t = x * ge + off;
    if (!(std::fabs (t) < 1.0e6f)) // too large or not finite: reset the pair and return the law of the clamped value
    {
        const float c = t != t ? 0.0f : (t > 0.0f ? 1.0e6f : -1.0e6f);
        s.x = 0.0f;
        s.g = ge;
        s.b = bnd (type, off, fast);
        return naive (type, c, fast);
    }
    const float tp = s.x * ge + off;
    if (ge != s.g) // the gain changed: the stored bounded part belongs to the old gain
    {
        s.b = bnd (type, tp, fast);
        s.g = ge;
    }
    const float bt = bnd (type, t, fast);
    const float dt = t - tp;
    float y;
    if (std::fabs (dt) < kEps)
        y = naive (type, 0.5f * (t + tp), fast);
    else
        y = ((lin (type, t) - lin (type, tp)) + (bt - s.b)) / dt - sub;
    s.x = flushDenormal (x);
    s.b = flushDenormal (bt);
    return y;
}
} // namespace adaa
} // namespace own
} // namespace synthyboy
