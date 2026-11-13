// SynthyBoy - own engine: NEON versions of the hot loops. Every vector function does, in each lane, the same operations in the same order as the scalar function
// next to it (separate multiply and add: engine-own is compiled with -ffp-contract=off, no vfma, no reciprocal estimates), so the samples are identical to the last
// bit; the scalar code is the fallback where there is no NEON (SB_NO_NEON forces it, the tests compare the two).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

#if (defined(__ARM_NEON) || defined(__aarch64__)) && !defined(SB_NO_NEON)
 #include <arm_neon.h>
 #define SB_NEON 1
#else
 #define SB_NEON 0
#endif

namespace synthyboy
{
namespace own
{
namespace simd
{
/** d[k] += a[k] * g */
inline void addScaled (float* d, const float* a, float g, int n) noexcept
{
    int k = 0;
#if SB_NEON
    const float32x4_t vg = vdupq_n_f32 (g);
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (d + k, vaddq_f32 (vld1q_f32 (d + k), vmulq_f32 (vld1q_f32 (a + k), vg)));
#endif
    for (; k < n; ++k)
        d[k] += a[k] * g;
}

/** t[k] = (t0 + k + 1) * invP for k = 0 .. 3 starting at k0, as the scalar loops write it: float (int) * invP */
#if SB_NEON
inline float32x4_t ramp4 (int t0, int k0, float invP) noexcept
{
    const int32_t base = t0 + k0 + 1;
    const int32_t idx[4] = { base, base + 1, base + 2, base + 3 };
    return vmulq_f32 (vcvtq_f32_s32 (vld1q_s32 (idx)), vdupq_n_f32 (invP));
}
/** a + (b - a) * t */
inline float32x4_t lerp4 (float32x4_t a, float32x4_t b, float32x4_t t) noexcept
{
    return vaddq_f32 (a, vmulq_f32 (vsubq_f32 (b, a), t));
}
#endif

/** out[k] = a + (b - a) * ((t0 + k + 1) * invP) */
inline void rampLerp (float* out, float a, float b, int t0, int n, float invP) noexcept
{
    int k = 0;
#if SB_NEON
    const float32x4_t va = vdupq_n_f32 (a), vb = vdupq_n_f32 (b);
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (out + k, lerp4 (va, vb, ramp4 (t0, k, invP)));
#endif
    for (; k < n; ++k)
        out[k] = a + (b - a) * (static_cast<float> (t0 + k + 1) * invP);
}
/** out[k] = 0.5f * (a[k] + b[k]) */
inline void halfSum (float* out, const float* a, const float* b, int n) noexcept
{
    int k = 0;
#if SB_NEON
    const float32x4_t h = vdupq_n_f32 (0.5f);
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (out + k, vmulq_f32 (h, vaddq_f32 (vld1q_f32 (a + k), vld1q_f32 (b + k))));
#endif
    for (; k < n; ++k)
        out[k] = 0.5f * (a[k] + b[k]);
}

/** a[k] *= g[k] */
inline void mulBy (float* a, const float* g, int n) noexcept
{
    int k = 0;
#if SB_NEON
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (a + k, vmulq_f32 (vld1q_f32 (a + k), vld1q_f32 (g + k)));
#endif
    for (; k < n; ++k)
        a[k] *= g[k];
}

/** a[k] += b[k] */
inline void addTo (float* a, const float* b, int n) noexcept
{
    int k = 0;
#if SB_NEON
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (a + k, vaddq_f32 (vld1q_f32 (a + k), vld1q_f32 (b + k)));
#endif
    for (; k < n; ++k)
        a[k] += b[k];
}
/** a[k] = clamp (a[k], lo, hi) */
inline void clampTo (float* a, float lo, float hi, int n) noexcept
{
    int k = 0;
#if SB_NEON
    const float32x4_t vlo = vdupq_n_f32 (lo), vhi = vdupq_n_f32 (hi);
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (a + k, vminq_f32 (vmaxq_f32 (vld1q_f32 (a + k), vlo), vhi));
#endif
    for (; k < n; ++k)
        a[k] = std::clamp (a[k], lo, hi);
}
} // namespace simd
} // namespace own
} // namespace synthyboy
