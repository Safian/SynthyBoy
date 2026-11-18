// SynthyBoy - core/filters-serum: two floats side by side (the left and the right filter of a voice), for the pair kernels of filter.cpp.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every operation is, in each lane, the single IEEE operation the scalar code of filter_dsp.h does: separate multiply and add (no fused form), a true division, and
// minimum / maximum / sanitize / softClip written with the same comparisons as their scalar functions. So a kernel written with a lane type computes, per lane, the
// samples of the scalar path to the last bit, as long as the scalar path is compiled without contraction of a * b + c (filter.cpp sees to that where the kernels are
// used). ScalarLanes is the reference form (two floats, any platform); NeonLanes is the same on a 64 bit NEON register: 32 bit ARM has no vector division, so there
// the two quotients are made one after the other (still the true division), AArch64 divides both at once.
#pragma once

#include <cmath>

#include "filter_dsp.h"

#if (((defined(__arm__) || (defined(__aarch64__) && defined(__linux__))) && defined(__ARM_NEON)) || defined(SYNTHYBOY_FILTER_TEST_NEON)) && !defined(SB_NO_NEON)
 #include <arm_neon.h>
 #include "../../engine-own/dsp/neon_cmp.h"
 #define SYNTHYBOY_FILTER_NEON 1
#else
 #define SYNTHYBOY_FILTER_NEON 0
#endif

namespace synthyboy::filters
{
struct ScalarLanes
{
    float v[2];

    static ScalarLanes set (float a, float b) noexcept { return { { a, b } }; }
    static ScalarLanes all (float a) noexcept { return { { a, a } }; }
    float get (int lane) const noexcept { return v[lane]; }

    friend ScalarLanes operator+ (ScalarLanes a, ScalarLanes b) noexcept { return { { a.v[0] + b.v[0], a.v[1] + b.v[1] } }; }
    friend ScalarLanes operator- (ScalarLanes a, ScalarLanes b) noexcept { return { { a.v[0] - b.v[0], a.v[1] - b.v[1] } }; }
    friend ScalarLanes operator* (ScalarLanes a, ScalarLanes b) noexcept { return { { a.v[0] * b.v[0], a.v[1] * b.v[1] } }; }
    friend ScalarLanes operator/ (ScalarLanes a, ScalarLanes b) noexcept { return { { a.v[0] / b.v[0], a.v[1] / b.v[1] } }; }

    /** sanitize () of filter_dsp.h per lane. */
    static ScalarLanes sanitized (ScalarLanes a) noexcept { return { { sanitize (a.v[0]), sanitize (a.v[1]) } }; }
    /** softClip () of filter_dsp.h per lane. */
    static ScalarLanes softClipped (ScalarLanes a) noexcept { return { { softClip (a.v[0]), softClip (a.v[1]) } }; }
};

#if SYNTHYBOY_FILTER_NEON
struct NeonLanes
{
    float32x2_t v;

    static NeonLanes set (float a, float b) noexcept
    {
        // the lanes written as the S halves of the D register (GCC's vector subscript): on 32 bit ARM a float t[2] went through the stack, two stores and a 64 bit load
        float32x2_t r = vdup_n_f32 (a);
        r[1] = b;
        return { r };
    }
    static NeonLanes all (float a) noexcept { return { vdup_n_f32 (a) }; }
    float get (int lane) const noexcept { return lane == 0 ? vget_lane_f32 (v, 0) : vget_lane_f32 (v, 1); }

    friend NeonLanes operator+ (NeonLanes a, NeonLanes b) noexcept { return { vadd_f32 (a.v, b.v) }; }
    friend NeonLanes operator- (NeonLanes a, NeonLanes b) noexcept { return { vsub_f32 (a.v, b.v) }; }
    friend NeonLanes operator* (NeonLanes a, NeonLanes b) noexcept { return { vmul_f32 (a.v, b.v) }; }
    friend NeonLanes operator/ (NeonLanes a, NeonLanes b) noexcept
    {
 #if defined(__aarch64__)
        return { vdiv_f32 (a.v, b.v) };
 #else
        // no vector division on 32 bit ARM: two true divisions, written straight into the S halves of the result (no round trip through the stack)
        float32x2_t r = a.v;
        r[0] = a.v[0] / b.v[0];
        r[1] = a.v[1] / b.v[1];
        return { r };
 #endif
    }

    /** sanitize (): 0 for a NaN, an infinity and a magnitude below 1e-18, else the value. On the bit patterns (dsp/neon_cmp.h): the value is kept where its magnitude
        bits m satisfy bits (1e-18) <= m < bits (infinity), which is |x| >= 1e-18 and |x| < infinity of the scalar function (a NaN has m above the bits of infinity);
        one subtraction makes the range a single unsigned compare. A cleared lane is +0, as the scalar 0.0f. */
    static NeonLanes sanitized (NeonLanes a) noexcept
    {
        const uint32x2_t b = vreinterpret_u32_f32 (a.v);
        const uint32_t lo = neoncmp::bitsOf (1e-18f);
        const uint32x2_t m = vand_u32 (b, vdup_n_u32 (neoncmp::kAbsMask));
        const uint32x2_t keep = vclt_u32 (vsub_u32 (m, vdup_n_u32 (lo)), vdup_n_u32 (neoncmp::kInfBits - lo));
        return { vreinterpret_f32_u32 (vand_u32 (b, keep)) };
    }
    /** softClip (): -1 at and below -3, 1 at and above 3, else x * (27 + x * x) / (27 + 9 * (x * x)), with the scalar function's order of operations. The two tests
        are the integer forms of x >= 3 and x <= -3 (dsp/neon_cmp.h: the masks of the float compares for every x, a NaN false in both). */
    static NeonLanes softClipped (NeonLanes a) noexcept
    {
        const float32x2_t x = a.v;
        const float32x2_t x2 = vmul_f32 (x, x);
        const NeonLanes num { vmul_f32 (x, vadd_f32 (vdup_n_f32 (27.0f), x2)) };
        const NeonLanes den { vadd_f32 (vdup_n_f32 (27.0f), vmul_f32 (vdup_n_f32 (9.0f), x2)) };
        float32x2_t r = (num / den).v;
        r = vbsl_f32 (neoncmp::geConst (x, 3.0f), vdup_n_f32 (1.0f), r);
        r = vbsl_f32 (neoncmp::leConst (x, -3.0f), vdup_n_f32 (-1.0f), r); // the scalar function tests this one first: it wins where both could hold (never)
        return { r };
    }
};
#endif
} // namespace synthyboy::filters
