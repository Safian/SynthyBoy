// SynthyBoy - own engine: the last stage of the engine's block, per sample: master volume (smoothed), the stereo routing, the Direct sum and the safety limit (CPU review PROFILE H1, VOICE-MOD V6 remnants).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// stageReference is the loop as it was in OwnEngine::process (kept as the reference of core/tests/test_own_master_stage.cpp). stage gives the same samples to the last bit:
//  - the volume smoother runs sample by sample only while it ramps (20 ms after a change); once it has settled, next () returns the target and changes nothing, so the target is used as it is;
//  - the stereo routing, a test of a value that does not change inside the block, is decided once and not per sample (the same arithmetic where it is on, none where it is off);
//  - on NEON, four samples at a time: the Direct gain lerp (simd::lerp4 of the same ramp), the routing, the sum and the gain as separate multiplies and adds, and the safety limit as an integer test: the
//    group is only taken by the vector form when no lane is above 1.8 in magnitude (the identity there, a NaN lane becoming +0); any other group runs through the scalar limit.
#pragma once

#include <cmath>
#include <cstdint>

#include "simd.h"
#include "util.h"

namespace synthyboy
{
namespace own
{
namespace master
{
/** The output safety stage: the identity up to +-1.8 (a loud patch with a boosting warp or filter passes unchanged, like the headroom of engine-vital), above it a smooth knee that
    approaches +-2 (any patch stays bounded). A knee that starts at 1 distorts a signal with peaks of 1.5 and aliases the distortion (a high-pass warp measured -58 dB). */
inline float outputLimit (float y) noexcept
{
    const float a = std::fabs (y);
    if (!(a > 1.8f))
        return y == y ? y : 0.0f;
    return std::copysign (1.8f + 0.2f * std::tanh ((a - 1.8f) * 5.0f), y);
}

struct Params
{
    float routeCos = 1.0f, routeSin = 0.0f;   // the stereo routing (master.stereo_routing)
    float directPrev = 1.0f, directNow = 1.0f; // the Direct gain at the start and the end of the control period
    int controlPos = 0;                        // the position of the first sample in the control period
};

/** The loop of OwnEngine::process before this file existed: n samples of left / right (the main sum), directL / directR (the Direct sum). */
inline void stageReference (float* left, float* right, const float* directL, const float* directR, int n, const Params& p, LinearSmoother& gain) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float g = gain.next();
        if (p.routeSin != 0.0f)
        {
            const float l0 = left[i], r0 = right[i];
            left[i] = l0 * p.routeCos + r0 * p.routeSin;
            right[i] = r0 * p.routeCos + l0 * p.routeSin;
        }
        const float dg = lerp (p.directPrev, p.directNow, static_cast<float> (p.controlPos + i + 1) * (1.0f / static_cast<float> (kControlPeriod)));
        left[i] = outputLimit ((left[i] + directL[i] * dg) * g);
        right[i] = outputLimit ((right[i] + directR[i] * dg) * g);
    }
}

namespace detail
{
template <bool Route>
inline void sampleOne (float* left, float* right, const float* directL, const float* directR, int i, float g, const Params& p) noexcept
{
    if (Route)
    {
        const float l0 = left[i], r0 = right[i];
        left[i] = l0 * p.routeCos + r0 * p.routeSin;
        right[i] = r0 * p.routeCos + l0 * p.routeSin;
    }
    const float dg = lerp (p.directPrev, p.directNow, static_cast<float> (p.controlPos + i + 1) * (1.0f / static_cast<float> (kControlPeriod)));
    left[i] = outputLimit ((left[i] + directL[i] * dg) * g);
    right[i] = outputLimit ((right[i] + directR[i] * dg) * g);
}

#if SB_NEON
/** The safety limit of four samples: y itself where no lane is above 1.8 in magnitude (a NaN lane gives +0), else the scalar limit of each lane. */
inline float32x4_t limit4 (float32x4_t y) noexcept
{
    const uint32x4_t u = vandq_u32 (vreinterpretq_u32_f32 (y), vdupq_n_u32 (0x7fffffffu));
    const uint32x4_t nan = vcgtq_u32 (u, vdupq_n_u32 (0x7f800000u));
    const uint32x4_t big = vandq_u32 (vcgtq_u32 (u, vdupq_n_u32 (neoncmp::bitsOf (1.8f))), vmvnq_u32 (nan));   // a > 1.8f, a NaN is not
    const uint32x2_t any = vorr_u32 (vget_low_u32 (big), vget_high_u32 (big));
    if ((vget_lane_u32 (any, 0) | vget_lane_u32 (any, 1)) != 0u)
    {
        float t[4];
        vst1q_f32 (t, y);
        for (float& v : t)
            v = outputLimit (v);
        return vld1q_f32 (t);
    }
    return vreinterpretq_f32_u32 (vbicq_u32 (vreinterpretq_u32_f32 (y), nan));
}

template <bool Route>
inline int lanes (float* left, float* right, const float* directL, const float* directR, int i, int n, float g, const Params& p) noexcept
{
    const float32x4_t vg = vdupq_n_f32 (g), vc = vdupq_n_f32 (p.routeCos), vs = vdupq_n_f32 (p.routeSin), vp = vdupq_n_f32 (p.directPrev), vn = vdupq_n_f32 (p.directNow);
    const float invP = 1.0f / static_cast<float> (kControlPeriod);
    for (; i + 4 <= n; i += 4)
    {
        float32x4_t l = vld1q_f32 (left + i), r = vld1q_f32 (right + i);
        if (Route)
        {
            const float32x4_t l1 = vaddq_f32 (vmulq_f32 (l, vc), vmulq_f32 (r, vs));
            const float32x4_t r1 = vaddq_f32 (vmulq_f32 (r, vc), vmulq_f32 (l, vs));
            l = l1;
            r = r1;
        }
        const float32x4_t dg = simd::lerp4 (vp, vn, simd::ramp4 (p.controlPos, i, invP));
        const float32x4_t yl = vmulq_f32 (vaddq_f32 (l, vmulq_f32 (vld1q_f32 (directL + i), dg)), vg);
        const float32x4_t yr = vmulq_f32 (vaddq_f32 (r, vmulq_f32 (vld1q_f32 (directR + i), dg)), vg);
        vst1q_f32 (left + i, limit4 (yl));
        vst1q_f32 (right + i, limit4 (yr));
    }
    return i;
}
#endif

template <bool Route>
inline void run (float* left, float* right, const float* directL, const float* directR, int n, const Params& p, LinearSmoother& gain, bool useLanes) noexcept
{
    int i = 0;
    for (; i < n && ! gain.settled(); ++i)   // the ramp: one smoother step per sample, as before
        sampleOne<Route> (left, right, directL, directR, i, gain.next(), p);
    const float g = gain.targetValue();       // settled: next () would return this and change nothing
#if SB_NEON
    if (useLanes)
        i = lanes<Route> (left, right, directL, directR, i, n, g, p);
#else
    (void) useLanes;
#endif
    for (; i < n; ++i)
        sampleOne<Route> (left, right, directL, directR, i, g, p);
}
} // namespace detail

/** The same samples as stageReference. useLanes false: the scalar form with the hoists only (the NEON form is a build setting; the flag is for the tests). */
inline void stage (float* left, float* right, const float* directL, const float* directR, int n, const Params& p, LinearSmoother& gain, bool useLanes = true) noexcept
{
    if (p.routeSin != 0.0f)
        detail::run<true> (left, right, directL, directR, n, p, gain, useLanes);
    else
        detail::run<false> (left, right, directL, directR, n, p, gain, useLanes);
}
} // namespace master
} // namespace own
} // namespace synthyboy
