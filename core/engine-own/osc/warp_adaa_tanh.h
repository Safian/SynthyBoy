// SynthyBoy - own engine: first-order ADAA kernels of the tanh family of the oscillator warps (Tube, Soft Clip, Diode 1, Asym) (docs/plans/distortion-antialiasing.md section 3.5).
// The antiderivative of tanh is ln cosh; ln cosh (t) = |t| - ln 2 + L (|t|) with L (s) = log1p (exp (-2 s)), so the difference of two large values is taken on the linear part (exact)
// and on the bounded part (below 0.7) separately.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "../dsp/fastmath.h"
#include "warp_adaa_clip.h"

namespace synthyboy
{
namespace own
{
namespace warpadaa
{
namespace detail
{
/** ln cosh (t) - |t| = L (|t|) - ln 2 (bounded, between -0.7 and 0). */
inline float lncoshBnd (float t, bool fast) noexcept
{
    const float e = fast ? fastmath::exp (-2.0f * std::fabs (t)) : std::exp (-2.0f * std::fabs (t));
    const float l = fast ? 0.69314718f * fastmath::log2 (1.0f + e) : std::log1p (e);
    return l - 0.69314718f;
}

/** The slope of Diode 1 on the side of t (the law is tanh above zero and 0.4 tanh below). */
inline float tanhWeight (int type, float t) noexcept
{
    return type == kWarpDistDiode1 && !(t > 0.0f) ? 0.4f : 1.0f;
}
} // namespace detail

/** One sample of Tube, Soft Clip, Diode 1 or Asym. Returns false (and leaves everything alone) for any other type. */
inline bool stepTanh (int type, State& s, float y, float a, bool fast, float& out) noexcept
{
    float gain, off = 0.0f;
    switch (type)
    {
        case kWarpDistTube: gain = 1.0f + 6.0f * a; off = 0.2f * a; break;
        case kWarpDistSoftClip: gain = 1.0f + 8.0f * a; break;
        case kWarpDistDiode1: gain = 1.0f + 6.0f * a; break;
        case kWarpDistAsym: gain = 1.0f + 3.0f * a; off = 0.5f * a * gain; break;
        default: return false;
    }
    auto th = [fast] (float v) noexcept { return fast ? fastmath::tanh (v) : std::tanh (v); };
    auto bounded = [type, fast] (float t) noexcept { return detail::tanhWeight (type, t) * detail::lncoshBnd (t, fast); };
    const bool hasC = type == kWarpDistTube || type == kWarpDistAsym;
    const float t = y * gain + off;
    if (!(std::fabs (t) < 1.0e6f)) // not finite or enormous: reset the pair and give the law of the clamped value
    {
        s.y = 0.0f;
        s.a = a;
        s.c = hasC ? th (off) : 0.0f;
        s.b = bounded (off);
        const float tc = t != t ? 0.0f : (t > 0.0f ? 1.0e6f : -1.0e6f);
        out = detail::tanhWeight (type, tc) * th (tc) - s.c;
        return true;
    }
    const float tp = s.y * gain + off;
    if (a != s.a) // the amount changed: the stored values belong to the old amount
    {
        s.b = bounded (tp);
        s.c = hasC ? th (off) : 0.0f;
        s.a = a;
    }
    const float bt = bounded (t);
    const float dt = t - tp;
    if (std::fabs (dt) < kEps)
    {
        const float tm = 0.5f * (t + tp);
        out = detail::tanhWeight (type, tm) * th (tm) - s.c;
    }
    else
        out = ((detail::tanhWeight (type, t) * std::fabs (t) - detail::tanhWeight (type, tp) * std::fabs (tp)) + (bt - s.b)) / dt - s.c;
    s.y = y;
    s.b = bt;
    return true;
}
} // namespace warpadaa
} // namespace own
} // namespace synthyboy
