// SynthyBoy - own engine: NEON versions of the hot loops, used on 32 bit and 64 bit ARM (the MPC builds: arm32 for the Key 61, arm64 for the XL). Every vector function does, in
// each lane, the same operations in the same order as the scalar function next to it (separate multiply and add, no vfma, no reciprocal estimates), so the samples are identical
// to the last bit. On x86 the scalar code is used, with whatever contraction the compiler applies to it as always. SB_NO_NEON forces the scalar code on ARM too (the two are compared).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// 32 bit ARM, and AArch64 on Linux (the MPC XL). Not Apple's arm64 (macOS, iOS): there the scalar code and its compiler contraction stay as they were.
#if defined(__arm__) || (defined(__aarch64__) && defined(__linux__))
 #define SB_ARM_LINUX_OR_32 1
#else
 #define SB_ARM_LINUX_OR_32 0
#endif

#if (((SB_ARM_LINUX_OR_32) && defined(__ARM_NEON)) || defined(SB_TEST_NEON)) && !defined(SB_NO_NEON) // SB_TEST_NEON: the same intrinsics on a build that does not define __ARM_NEON
 #include <arm_neon.h>
 #include "neon_cmp.h"
 #define SB_NEON 1
#else
 #define SB_NEON 0
#endif

// On ARM the translation units that use these loops must not fuse a * b + c in their scalar code either (a build for a VFPv4 FPU, -mfpu=neon-vfpv4, would by
// default under GCC, and AArch64 always has the fused multiply-add): the pragma is honoured by GCC 12 (checked: the same expression gives vfma.f32 without it and the non
// fused vmla.f32 with it).
#if SB_ARM_LINUX_OR_32 && defined(__GNUC__) && !defined(__clang__)
 #pragma GCC optimize ("fp-contract=off")
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

/** d[k] += a[k] * g[k] */
inline void mulAddTo (float* d, const float* a, const float* g, int n) noexcept
{
    int k = 0;
#if SB_NEON
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (d + k, vaddq_f32 (vld1q_f32 (d + k), vmulq_f32 (vld1q_f32 (a + k), vld1q_f32 (g + k))));
#endif
    for (; k < n; ++k)
        d[k] += a[k] * g[k];
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
    // the lane offsets stay in a register for the whole loop: ramp4 () builds its four integers on the stack and loads them as one vector, a 128 bit load from two pending 64 bit
    // stores that cannot be forwarded (CPU-REVIEW-VOICE-MOD.md V2). The same integers t0 + k + 1 + lane, the same conversion and multiply as ramp4 ().
    static const int32_t kIota[4] = { 0, 1, 2, 3 };
    const int32x4_t iota = vld1q_s32 (kIota);
    const float32x4_t va = vdupq_n_f32 (a), vb = vdupq_n_f32 (b), vInvP = vdupq_n_f32 (invP);
    for (; k + 4 <= n; k += 4)
        vst1q_f32 (out + k, lerp4 (va, vb, vmulq_f32 (vcvtq_f32_s32 (vaddq_s32 (vdupq_n_s32 (t0 + k + 1), iota)), vInvP)));
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
/** Whether |a[k]| < c and |b[k]| < c for every k (c > 0 a normal number): the float compares of a scalar loop `!(std::fabs (x) < c)`, made on NEON with the integer
    compare of dsp/neon_cmp.h (the same result for every input: a NaN and an infinity are not below c). */
inline bool allAbsBelow (const float* a, const float* b, int n, float c) noexcept
{
    int k = 0;
#if SB_NEON
    uint32x4_t acc = vdupq_n_u32 (0xffffffffu);
    for (; k + 4 <= n; k += 4)
        acc = vandq_u32 (acc, vandq_u32 (neoncmp::absLt (vld1q_f32 (a + k), c), neoncmp::absLt (vld1q_f32 (b + k), c)));
    const uint32x2_t half = vand_u32 (vget_low_u32 (acc), vget_high_u32 (acc));
    if ((vget_lane_u32 (half, 0) & vget_lane_u32 (half, 1)) == 0u)
        return false;
#endif
    for (; k < n; ++k)
        if (!(std::fabs (a[k]) < c) || !(std::fabs (b[k]) < c))
            return false;
    return true;
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
