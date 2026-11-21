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
constexpr uint32_t kConverted = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11);

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

/** The periodic integral of the Linear Fold triangle (0 .. 0.5), w = fract (v / 4 + 3/4): Fp (w) = 4 w (1 - 2 w) below 1/2, 4 (2 w - 1) (w - 1) above. */
inline float foldIntegral (float v) noexcept
{
    const float t = 0.25f * v + 0.75f;
    const float w = t - ffloor (t);
    return w < 0.5f ? 4.0f * w * (1.0f - 2.0f * w) : 4.0f * (2.0f * w - 1.0f) * (w - 1.0f);
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
        case 2: return foldIntegral (t);
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
        case 6: return th (t) - th (0.3f); // the same function twice, so that the law is exactly 0 at x = 0 (the float constant kTanh03 is one ulp off std::tanh (0.3f))
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
        case 2: { const float u = 0.25f * t + 0.75f; return std::fabs (-4.0f * (u - ffloor (u)) + 2.0f) - 1.0f; }
        case 3: return fast ? fastmath::sinCycles (0.25f * t) : std::sin (0.5f * kPif * t);
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
    if (type == 3) // Sine Fold: the closed form has no quotient, sin (pi m / 2) * sinc (dv / 4) with m the midpoint, dv the difference (the cosine identity of the plan 3.2)
    {
        const float z = 0.25f * (t - tp);
        const float pz = kPif * z, pz2 = pz * pz;
        const float sinc = std::fabs (z) < (fast ? 0.05f : 0.01f) ? 1.0f - pz2 * (1.0f / 6.0f - pz2 * (1.0f / 120.0f)) : (fast ? fastmath::sinCycles (0.5f * z) : std::sin (pz)) / pz;
        const float ys = (fast ? fastmath::sinCycles (0.125f * (t + tp)) : std::sin (0.25f * kPif * (t + tp))) * sinc;
        s.x = flushDenormal (x);
        s.g = ge;
        return ys;
    }
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

/** The 512-segment interpolation of a transfer curve (y == nullptr: identity clamp (u, 0, 1)). */
inline float shaperC (const float* y, float u) noexcept
{
    if (u <= 0.0f) return y ? y[0] : 0.0f;
    if (u >= 1.0f) return y ? y[512] : 1.0f;
    if (!y) return u;
    const float s = 512.0f * u;
    const int i = std::min (static_cast<int> (s), 511);
    const float t = s - static_cast<float> (i);
    return y[i] + (y[i + 1] - y[i]) * t;
}

/** The 512-segment interpolation of the curve's cumulative integral (null pair: identity u^2 / 2 for u in [0, 1]). */
inline float shaperI (const float* y, const float* I, float u) noexcept
{
    if (!y || !I)
    {
        if (u <= 0.0f) return 0.0f;
        if (u >= 1.0f) return u - 0.5f;
        return 0.5f * u * u;
    }
    if (u <= 0.0f) return y[0] * u;
    if (u >= 1.0f) return I[512] + y[512] * (u - 1.0f);
    constexpr float h = 1.0f / 512.0f;
    const float s = 512.0f * u;
    const int i = std::min (static_cast<int> (s), 511);
    const float t = s - static_cast<float> (i);
    return I[i] + h * (y[i] * t + 0.5f * (y[i + 1] - y[i]) * t * t);
}

/** One sample of the X-Shaper (types 12 and 13): x = raw input, m = morph in [0, 1], curves A and B with their cumulative integrals. */
inline float stepShaper (int type, State& s, float x, float m, const float* yA, const float* IA, const float* yB, const float* IB) noexcept
{
    auto evalNaive = [type, m, yA, yB] (float v) noexcept -> float
    {
        if (type == 13)
        {
            const float u = 0.5f * (v + 1.0f);
            const float c = (1.0f - m) * shaperC (yA, u) + m * shaperC (yB, u);
            return 2.0f * c - 1.0f;
        }
        else
        {
            if (v == 0.0f)
                return 0.0f;
            const float u = std::min (std::fabs (v), 1.0f);
            const float c = (1.0f - m) * shaperC (yA, u) + m * shaperC (yB, u);
            return v > 0.0f ? c : -c;
        }
    };

    auto evalF = [type, m, yA, IA, yB, IB] (float v) noexcept -> float
    {
        if (type == 13)
        {
            const float u = 0.5f * (v + 1.0f);
            const float C = (1.0f - m) * shaperI (yA, IA, u) + m * shaperI (yB, IB, u);
            return 4.0f * C - 2.0f * u;
        }
        else
        {
            const float u = std::fabs (v);
            return (1.0f - m) * shaperI (yA, IA, u) + m * shaperI (yB, IB, u);
        }
    };

    if (!(std::fabs (x) < 1.0e6f)) // too large or not finite: reset the pair and return the naive law
    {
        const float c = x != x ? 0.0f : (x > 0.0f ? 1.0e6f : -1.0e6f);
        s.x = 0.0f;
        s.g = m;
        s.b = evalF (0.0f);
        return evalNaive (c);
    }
    if (s.g != m) // morph changed: recompute stored integral at previous x
    {
        s.b = evalF (s.x);
        s.g = m;
    }
    const float Fx = evalF (x);
    const float dx = x - s.x;
    float y;
    if (std::fabs (dx) < kEps)
        y = evalNaive (0.5f * (x + s.x));
    else
        y = (Fx - s.b) / dx;
    s.x = flushDenormal (x);
    s.b = flushDenormal (Fx);
    return y;
}
} // namespace adaa
} // namespace own
} // namespace synthyboy
