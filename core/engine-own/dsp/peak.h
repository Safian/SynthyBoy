// SynthyBoy - own engine: the peak of a block of samples, in NEON lanes with the exact result of the scalar loops they replace (CPU review FILTER-FX F10, PROFILE S3 / H1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The scalar loops (kept here as peakAbsScalar / peakAbsPairScalar, the reference of core/tests/test_own_peak.cpp) are
//     m = 0;  m = std::max (m, std::fabs (x[k]))                                    one channel
//     m = std::max (m, std::max (std::fabs (l[k]), std::fabs (r[k])))               two channels
// and std::max (a, b) is (a < b) ? b : a, so a NaN never enters the result: a NaN magnitude compares false and is passed over; in the two channel form a NaN in l drops the
// whole sample (the inner max returns |l|, the NaN), a NaN in r alone leaves |l|. An all NaN block gives 0. The vector form masks the NaN lanes to zero with an integer
// compare on the magnitude bits, then takes vmaxq_f32 of values that are never NaN: the maximum of the same set of numbers, the same float. A subnormal magnitude is
// treated as the float compare of the build treats it (zero where the FPSCR flushes, as on the MPC shell and always on ARMv7 NEON; itself where it does not), because the
// vector max is a float max and the scalar compare is the float compare of the same mode. Not in simd.h on purpose: that header switches the contraction of the whole
// translation unit off on ARM, and the effect code must keep compiling as it did.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#if ((((defined(__arm__) || (defined(__aarch64__) && defined(__linux__)))) && defined(__ARM_NEON)) || defined(SB_TEST_NEON)) && !defined(SB_NO_NEON)
 #include <arm_neon.h>
 #define SB_PEAK_NEON 1
#else
 #define SB_PEAK_NEON 0
#endif

namespace synthyboy
{
namespace own
{
namespace peak
{
/** The old loop: the largest magnitude of n samples, NaN passed over. */
inline float peakAbsScalar (const float* x, int n) noexcept
{
    float m = 0.0f;
    for (int k = 0; k < n; ++k)
        m = std::max (m, std::fabs (x[k]));
    return m;
}
/** The old two channel loop. */
inline float peakAbsPairScalar (const float* l, const float* r, int n) noexcept
{
    float m = 0.0f;
    for (int k = 0; k < n; ++k)
        m = std::max (m, std::max (std::fabs (l[k]), std::fabs (r[k])));
    return m;
}

#if SB_PEAK_NEON
/** |x| as a float vector with a NaN lane turned into +0; keep gets all ones where x is not a NaN. */
inline float32x4_t magnitude (float32x4_t x, uint32x4_t& keep) noexcept
{
    const uint32x4_t u = vandq_u32 (vreinterpretq_u32_f32 (x), vdupq_n_u32 (0x7fffffffu));
    keep = vcleq_u32 (u, vdupq_n_u32 (0x7f800000u));
    return vreinterpretq_f32_u32 (vandq_u32 (u, keep));
}
inline float horizontalMax (float32x4_t a) noexcept
{
    const float32x2_t h = vpmax_f32 (vget_low_f32 (a), vget_high_f32 (a));
    return vget_lane_f32 (vpmax_f32 (h, h), 0);
}
#endif

/** The largest magnitude of n samples (the loop of peakAbsScalar). */
inline float peakAbs (const float* x, int n) noexcept
{
    int k = 0;
    float m = 0.0f;
#if SB_PEAK_NEON
    if (n >= 4)
    {
        float32x4_t acc = vdupq_n_f32 (0.0f);
        for (; k + 4 <= n; k += 4)
        {
            uint32x4_t keep;
            acc = vmaxq_f32 (acc, magnitude (vld1q_f32 (x + k), keep));
        }
        m = horizontalMax (acc);
    }
#endif
    for (; k < n; ++k)
        m = std::max (m, std::fabs (x[k]));
    return m;
}

/** The largest magnitude over both channels (the loop of peakAbsPairScalar). */
inline float peakAbsPair (const float* l, const float* r, int n) noexcept
{
    int k = 0;
    float m = 0.0f;
#if SB_PEAK_NEON
    if (n >= 4)
    {
        float32x4_t acc = vdupq_n_f32 (0.0f);
        for (; k + 4 <= n; k += 4)
        {
            uint32x4_t keepL, keepR;
            const float32x4_t a = magnitude (vld1q_f32 (l + k), keepL), b = magnitude (vld1q_f32 (r + k), keepR);
            // a NaN in l drops the sample (std::max (|l|, |r|) returns the NaN, which the outer max passes over)
            acc = vmaxq_f32 (acc, vreinterpretq_f32_u32 (vandq_u32 (vreinterpretq_u32_f32 (vmaxq_f32 (a, b)), keepL)));
        }
        m = horizontalMax (acc);
    }
#endif
    for (; k < n; ++k)
        m = std::max (m, std::max (std::fabs (l[k]), std::fabs (r[k])));
    return m;
}
} // namespace peak
} // namespace own
} // namespace synthyboy
