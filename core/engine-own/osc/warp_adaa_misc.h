// SynthyBoy - own engine: first-order ADAA kernels of the other sample-domain oscillator warps (Diode 2, Stomp, Soft Sat, Sine Fold, Sine Shaper) (docs/plans/distortion-antialiasing.md
// section 3.5). These laws have no cheaper form in fastmath and the laws themselves already use the library functions, so the kernels use them as well.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "../dsp/util.h"
#include "warp_adaa_clip.h"

namespace synthyboy
{
namespace own
{
namespace warpadaa
{
namespace detail
{
inline float miscGain (int type, float a) noexcept
{
    switch (type)
    {
        case kWarpDistDiode2: return 1.0f + 6.0f * a;
        case kWarpDistStomp: return 1.0f + 10.0f * a;
        case kWarpDistSoftSat: return 1.0f + 6.0f * a;
        case kWarpDistSineFold: return 1.0f + 5.0f * a;
        default: return 1.0f;
    }
}

/** The antiderivative at t (the argument of the closed form; for Sine Shaper the input itself). */
inline float miscF (int type, float t, float a) noexcept
{
    switch (type)
    {
        case kWarpDistDiode2: { const float s = std::fabs (t); return s - std::log1p (s); }
        case kWarpDistStomp: return std::sqrt (1.0f + t * t);
        case kWarpDistSoftSat: return (2.0f / kPif) * (t * std::atan (t) - 0.5f * std::log1p (t * t));
        case kWarpDistSineShaper: return (1.0f - a) * 0.5f * t * t + a * (1.0f - std::cos (kTwoPif * t)) * (1.0f / kTwoPif);
        default: return 0.0f;
    }
}

/** The warp law at t (the midpoint branch): the same functions as warpSample. */
inline float miscLaw (int type, float t, float a) noexcept
{
    switch (type)
    {
        case kWarpDistDiode2: return t / (1.0f + std::fabs (t));
        case kWarpDistStomp: return t / std::sqrt (1.0f + t * t);
        case kWarpDistSoftSat: return std::atan (t) * (2.0f / kPif);
        case kWarpDistSineShaper: return lerp (t, std::sin (kTwoPif * t), a);
        default: return t;
    }
}
} // namespace detail

/** One sample of Diode 2, Stomp, Soft Sat, Sine Fold or Sine Shaper. Returns false (and leaves everything alone) for any other type. */
inline bool stepMisc (int type, State& s, float y, float a, bool fast, float& out) noexcept
{
    (void) fast;
    switch (type)
    {
        case kWarpDistDiode2: case kWarpDistStomp: case kWarpDistSoftSat: case kWarpDistSineFold: case kWarpDistSineShaper: break;
        default: return false;
    }
    const float gain = detail::miscGain (type, a);
    const float t = y * gain;
    if (!(std::fabs (t) < 1.0e6f)) // not finite or enormous: reset the pair and give the law of the clamped value
    {
        s.y = 0.0f;
        s.a = a;
        s.b = detail::miscF (type, 0.0f, a);
        const float tc = t != t ? 0.0f : (t > 0.0f ? 1.0e6f : -1.0e6f);
        out = type == kWarpDistSineFold ? std::sin (0.5f * kPif * tc) : detail::miscLaw (type, tc, a);
        return true;
    }
    const float tp = s.y * gain;
    const float dt = t - tp;
    if (type == kWarpDistSineFold)
    {
        // the closed form with no quotient: sin (pi m / 2) * sinc (dv / 4), m = the midpoint, sinc z = sin (pi z) / (pi z)
        const float m = 0.5f * (t + tp);
        const float z = 0.25f * dt;
        const float pz = kPif * z;
        const float sinc = std::fabs (z) < 0.01f ? 1.0f - pz * pz * (1.0f / 6.0f) : std::sin (pz) / pz;
        out = std::sin (0.5f * kPif * m) * sinc;
        s.y = y;
        return true;
    }
    if (a != s.a) // the amount changed: the stored value belongs to the old amount
    {
        s.b = detail::miscF (type, tp, a);
        s.a = a;
    }
    const float bt = detail::miscF (type, t, a);
    if (std::fabs (dt) < kEps)
        out = detail::miscLaw (type, 0.5f * (t + tp), a);
    else
        out = (bt - s.b) / dt;
    s.y = y;
    s.b = bt;
    return true;
}
} // namespace warpadaa
} // namespace own
} // namespace synthyboy
