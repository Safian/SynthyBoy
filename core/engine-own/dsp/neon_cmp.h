// SynthyBoy - NEON float comparisons made on the bit patterns with integer instructions.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Why: GCC for 32 bit ARM does not emit the NEON float compares (vcgt.f32, vcge.f32, vceq.f32 ...) without -funsafe-math-optimizations, because NEON on ARMv7 flushes
// subnormal inputs to zero. Every vclt_f32 / vcgeq_f32 ... of the source becomes, per lane, a move to a core register, a VFP compare, a vmrs transfer of the flags, a
// conditional move and a store, and the mask is reloaded from the stack (CPU review F2, docs/release/CPU-REVIEW-FILTER-FX.md). The integer compares below are emitted
// as single NEON instructions on both MPC architectures.
//
// Exact semantics (tested on every special value and on random bit patterns in core/tests/test_neon_cmp.cpp):
//
// - The forms against a constant (absLt, absLe, geConst, leConst) take a constant c that is a NORMAL number (not zero, not subnormal, not NaN; +infinity is allowed
//   where noted). For every input x, NaN, both infinities, both zeros and the subnormals included, they give the mask of the IEEE comparison of the float x with c:
//   all ones where the comparison is true, 0 where it is false (a NaN compares false). Because c is normal, a subnormal x compares the same way as zero would, so the
//   result is also that of the comparison made in flush-to-zero mode (the MPC shell sets FPSCR.FZ; NEON on ARMv7 always flushes): the constant forms equal the float
//   compare of the scalar VFP code in both modes, bit for bit.
// - The general forms (lt, le, gt, ge, eq of two vectors) give the IEEE comparison: a NaN on either side gives false, -0 equals +0, a subnormal compares as its value.
//   In flush-to-zero mode the float compare treats a subnormal operand as zero, so the general forms equal the flush-to-zero compare wherever neither operand is
//   subnormal (use them only where that holds or where IEEE is what the scalar code computes).
#pragma once

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
 #include <arm_neon.h>
 #include <cstdint>
 #include <cstring>
 #define SYNTHYBOY_NEON_CMP 1

namespace synthyboy
{
namespace neoncmp
{
constexpr uint32_t kAbsMask = 0x7fffffffu;
constexpr uint32_t kInfBits = 0x7f800000u;

inline uint32_t bitsOf (float c) noexcept
{
    uint32_t u;
    std::memcpy (&u, &c, 4);
    return u;
}

// ---- 2 lanes -------------------------------------------------------------------------------------------------------------------------------------------------

/** |x| < c (c > 0 normal, or +infinity): the magnitude bits are below those of c. A NaN has magnitude bits above 0x7f800000, so it gives false. */
inline uint32x2_t absLt (float32x2_t x, float c) noexcept
{
    return vclt_u32 (vand_u32 (vreinterpret_u32_f32 (x), vdup_n_u32 (kAbsMask)), vdup_n_u32 (bitsOf (c)));
}
/** |x| <= c (c > 0 normal, or +infinity). */
inline uint32x2_t absLe (float32x2_t x, float c) noexcept
{
    return vcle_u32 (vand_u32 (vreinterpret_u32_f32 (x), vdup_n_u32 (kAbsMask)), vdup_n_u32 (bitsOf (c)));
}
/** x >= c (c > 0 normal, or +infinity): the bits of x, unsigned, lie in [bits (c), bits (+infinity)]; one subtraction turns the range into a single unsigned compare. */
inline uint32x2_t geConst (float32x2_t x, float c) noexcept
{
    const uint32_t lo = bitsOf (c);
    return vcle_u32 (vsub_u32 (vreinterpret_u32_f32 (x), vdup_n_u32 (lo)), vdup_n_u32 (kInfBits - lo));
}
/** x <= c (c < 0 normal, or -infinity): the bits of x lie in [bits (c), bits (-infinity)]. */
inline uint32x2_t leConst (float32x2_t x, float c) noexcept
{
    const uint32_t lo = bitsOf (c);
    return vcle_u32 (vsub_u32 (vreinterpret_u32_f32 (x), vdup_n_u32 (lo)), vdup_n_u32 ((0x80000000u | kInfBits) - lo));
}
/** A key that orders the floats as signed integers: +m for a positive x, -m for a negative one (m = magnitude bits), so -0 and +0 both give 0. */
inline int32x2_t orderKey (float32x2_t x) noexcept
{
    const int32x2_t b = vreinterpret_s32_f32 (x);
    const int32x2_t s = vshr_n_s32 (b, 31);                                        // all ones for a negative x
    const int32x2_t m = vand_s32 (b, vdup_n_s32 (static_cast<int32_t> (kAbsMask)));
    return vsub_s32 (veor_s32 (m, s), s);                                          // m or -m
}
/** Neither lane is a NaN. */
inline uint32x2_t ordered (float32x2_t a, float32x2_t b) noexcept
{
    const uint32x2_t inf = vdup_n_u32 (kInfBits);
    return vand_u32 (vcle_u32 (vand_u32 (vreinterpret_u32_f32 (a), vdup_n_u32 (kAbsMask)), inf), vcle_u32 (vand_u32 (vreinterpret_u32_f32 (b), vdup_n_u32 (kAbsMask)), inf));
}
inline uint32x2_t lt (float32x2_t a, float32x2_t b) noexcept { return vand_u32 (vclt_s32 (orderKey (a), orderKey (b)), ordered (a, b)); }
inline uint32x2_t le (float32x2_t a, float32x2_t b) noexcept { return vand_u32 (vcle_s32 (orderKey (a), orderKey (b)), ordered (a, b)); }
inline uint32x2_t gt (float32x2_t a, float32x2_t b) noexcept { return lt (b, a); }
inline uint32x2_t ge (float32x2_t a, float32x2_t b) noexcept { return le (b, a); }
inline uint32x2_t eq (float32x2_t a, float32x2_t b) noexcept { return vand_u32 (vceq_s32 (orderKey (a), orderKey (b)), ordered (a, b)); }

// ---- 4 lanes -------------------------------------------------------------------------------------------------------------------------------------------------

inline uint32x4_t absLt (float32x4_t x, float c) noexcept
{
    return vcltq_u32 (vandq_u32 (vreinterpretq_u32_f32 (x), vdupq_n_u32 (kAbsMask)), vdupq_n_u32 (bitsOf (c)));
}
inline uint32x4_t absLe (float32x4_t x, float c) noexcept
{
    return vcleq_u32 (vandq_u32 (vreinterpretq_u32_f32 (x), vdupq_n_u32 (kAbsMask)), vdupq_n_u32 (bitsOf (c)));
}
inline uint32x4_t geConst (float32x4_t x, float c) noexcept
{
    const uint32_t lo = bitsOf (c);
    return vcleq_u32 (vsubq_u32 (vreinterpretq_u32_f32 (x), vdupq_n_u32 (lo)), vdupq_n_u32 (kInfBits - lo));
}
inline uint32x4_t leConst (float32x4_t x, float c) noexcept
{
    const uint32_t lo = bitsOf (c);
    return vcleq_u32 (vsubq_u32 (vreinterpretq_u32_f32 (x), vdupq_n_u32 (lo)), vdupq_n_u32 ((0x80000000u | kInfBits) - lo));
}
inline int32x4_t orderKey (float32x4_t x) noexcept
{
    const int32x4_t b = vreinterpretq_s32_f32 (x);
    const int32x4_t s = vshrq_n_s32 (b, 31);
    const int32x4_t m = vandq_s32 (b, vdupq_n_s32 (static_cast<int32_t> (kAbsMask)));
    return vsubq_s32 (veorq_s32 (m, s), s);
}
inline uint32x4_t ordered (float32x4_t a, float32x4_t b) noexcept
{
    const uint32x4_t inf = vdupq_n_u32 (kInfBits);
    return vandq_u32 (vcleq_u32 (vandq_u32 (vreinterpretq_u32_f32 (a), vdupq_n_u32 (kAbsMask)), inf), vcleq_u32 (vandq_u32 (vreinterpretq_u32_f32 (b), vdupq_n_u32 (kAbsMask)), inf));
}
inline uint32x4_t lt (float32x4_t a, float32x4_t b) noexcept { return vandq_u32 (vcltq_s32 (orderKey (a), orderKey (b)), ordered (a, b)); }
inline uint32x4_t le (float32x4_t a, float32x4_t b) noexcept { return vandq_u32 (vcleq_s32 (orderKey (a), orderKey (b)), ordered (a, b)); }
inline uint32x4_t gt (float32x4_t a, float32x4_t b) noexcept { return lt (b, a); }
inline uint32x4_t ge (float32x4_t a, float32x4_t b) noexcept { return le (b, a); }
inline uint32x4_t eq (float32x4_t a, float32x4_t b) noexcept { return vandq_u32 (vceqq_s32 (orderKey (a), orderKey (b)), ordered (a, b)); }

/** |x| < 1e-18 gives +0, else x: flushDenormal () / the `tiny` flush of the scalar code, lane by lane. */
inline float32x2_t flushTiny (float32x2_t x) noexcept
{
    return vreinterpret_f32_u32 (vbic_u32 (vreinterpret_u32_f32 (x), absLt (x, 1.0e-18f)));
}
inline float32x4_t flushTiny (float32x4_t x) noexcept
{
    return vreinterpretq_f32_u32 (vbicq_u32 (vreinterpretq_u32_f32 (x), absLt (x, 1.0e-18f)));
}
} // namespace neoncmp
} // namespace synthyboy
#else
 #define SYNTHYBOY_NEON_CMP 0
#endif
