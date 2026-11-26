// SynthyBoy - own engine tests: the integer NEON compares that replaced the raw float compares of the wavetable oscillator (osc/wt_osc.h) and of the spectral kernels
// (core/spectral/src/kernels.cpp) give, for every input, the masks of the float compares they replaced (dsp/neon_cmp.h: ltZero, belowTruncation, nonNegNormalOrZero,
// gtNonNeg, geNonNeg, and the constant forms with the constants of those sites).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Single operand compares: all 2^32 bit patterns with SB_TEST_FULL=1 (the ctest entry `..._full`, label slow); the quick run takes every 257th pattern plus every binade
// boundary (both signs, mantissas 0, 1, 2, 2^22 - 1, 2^22, 2^22 + 1, 2^23 - 2, 2^23 - 1), 2048 neighbours either side of every constant of the sites, and the NaN payloads. The
// reference is the scalar C compare in the default (IEEE) float mode; the vector forms are integer instructions, so they do not depend on the mode.
// Two operand compares (gtNonNeg, geNonNeg): every pair of a list of boundary values of their domain, and random pairs of the domain.
// Built for 32 bit and 64 bit ARM (core/tests/CMakeLists.txt) and run under qemu-user where there is no device.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "dsp/neon_cmp.h"
#include "test_runner.h"

namespace
{
float fromBits (uint32_t u)
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}
uint32_t bitsOf (float f)
{
    uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 7u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

#if SYNTHYBOY_NEON_CMP
using namespace synthyboy::neoncmp;

constexpr float kTwoPiF = 6.28318530717958647692f; // the constants of core/spectral/src/kernels.cpp
constexpr float kPiF = 3.14159265f;

/** vcvtq_s32_f32 in C: the truncation, saturating at the ends of int32, NaN gives 0 (the behaviour of the NEON conversion on both architectures). */
int32_t truncSat (float x)
{
    if (x != x)
        return 0;
    if (x >= 2147483648.0f)
        return std::numeric_limits<int32_t>::max();
    if (x <= -2147483648.0f)
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t> (x);
}
bool plainRef (uint32_t u) { return u == 0u || (u >= 0x00800000u && u <= 0x7f800000u); }

struct Tally
{
    long long patterns = 0, ltZero = 0, belowTrunc = 0, gtHundred = 0, geTwoPi = 0, gtPi = 0, absLtInf = 0, domain = 0;
};

/** Four bit patterns through every single operand form of the sites, against the scalar compares. */
void group (const uint32_t b[4], Tally& t)
{
    const float32x4_t x = vreinterpretq_f32_u32 (vld1q_u32 (b));
    uint32_t m[7][4];
    vst1q_u32 (m[0], ltZero (x));                                              // wt_osc.h sinCycles4 (w < 0), kernels.cpp sincosN (a < 0), wrapN (r < 0, x < 0)
    vst1q_u32 (m[1], belowTruncation (x, vcvtq_s32_f32 (x)));                  // wt_osc.h ffloor4
    vst1q_u32 (m[2], gtConst (vabsq_f32 (x), 100.0f));                         // kernels.cpp wrapN: |x| > 100
    vst1q_u32 (m[3], geConst (x, kTwoPiF));                                    // wrapN: r >= 2 pi
    vst1q_u32 (m[4], gtConst (x, kPiF));                                       // wrapN: r > pi
    vst1q_u32 (m[5], absLt (x, std::numeric_limits<float>::infinity()));       // gainClamp: finite
    vst1q_u32 (m[6], nonNegNormalOrZero (x));                                  // the domain of gtNonNeg / geNonNeg
    for (int l = 0; l < 4; ++l)
    {
        const float xf = fromBits (b[l]);
        const uint32_t T = 0xffffffffu;
        t.ltZero += m[0][l] != (xf < 0.0f ? T : 0u) ? 1 : 0;
        t.belowTrunc += m[1][l] != (xf < static_cast<float> (truncSat (xf)) ? T : 0u) ? 1 : 0;
        t.gtHundred += m[2][l] != (std::fabs (xf) > 100.0f ? T : 0u) ? 1 : 0;
        t.geTwoPi += m[3][l] != (xf >= kTwoPiF ? T : 0u) ? 1 : 0;
        t.gtPi += m[4][l] != (xf > kPiF ? T : 0u) ? 1 : 0;
        t.absLtInf += m[5][l] != (std::fabs (xf) < std::numeric_limits<float>::infinity() ? T : 0u) ? 1 : 0;
        t.domain += m[6][l] != (plainRef (b[l]) ? T : 0u) ? 1 : 0;
    }
    t.patterns += 4;
}

/** The patterns of the quick run that are not every 257th: binade boundaries, the neighbourhoods of the constants, the NaNs. */
std::vector<uint32_t> boundaryPatterns()
{
    std::vector<uint32_t> v;
    for (uint32_t sign = 0; sign < 2; ++sign)
        for (uint32_t e = 0; e < 256; ++e)
            for (uint32_t mant : { 0u, 1u, 2u, 0x3fffffu, 0x400000u, 0x400001u, 0x7ffffeu, 0x7fffffu })
                v.push_back ((sign << 31) | (e << 23) | mant);
    for (float c : { 0.0f, 1.0f, 100.0f, kTwoPiF, kPiF, 4194304.0f, 8388608.0f, 2147483648.0f, 1.0e-18f, 0.5f, 16.0f, 1.17549435e-38f, std::numeric_limits<float>::infinity() })
        for (uint32_t sign = 0; sign < 2; ++sign)
        {
            const uint32_t u = bitsOf (c);
            for (int d = -2048; d <= 2048; ++d)
                v.push_back ((sign << 31) | ((u + static_cast<uint32_t> (d)) & 0x7fffffffu));
        }
    for (uint32_t sign = 0; sign < 2; ++sign)
        for (uint32_t p = 1; p < 512; ++p)
            v.push_back ((sign << 31) | 0x7f800000u | p);
    return v;
}
#endif
} // namespace

TEST (the_integer_compares_of_the_wavetable_and_spectral_sites_equal_the_float_compares_on_every_bit_pattern)
{
#if SYNTHYBOY_NEON_CMP
    Tally t;
    // quick: every 257th pattern (coprime to every power of two) and the boundary patterns; SB_TEST_FULL=1: all 2^32
    const uint32_t stride = static_cast<uint32_t> (sbtest::quickScaled ("compare sites: 2^32 pattern sweep (stride)", 1, 257, 0));
    if (stride == 1)
    {
        uint32_t b[4];
        for (uint64_t u = 0; u < (1ull << 32); u += 4)
        {
            for (int l = 0; l < 4; ++l)
                b[l] = static_cast<uint32_t> (u + static_cast<uint64_t> (l));
            group (b, t);
        }
    }
    else
    {
        std::vector<uint32_t> v = boundaryPatterns();
        for (uint64_t u = 0; u < (1ull << 32); u += stride)
            v.push_back (static_cast<uint32_t> (u));
        while (v.size() % 4 != 0)
            v.push_back (0u);
        for (size_t i = 0; i < v.size(); i += 4)
            group (&v[i], t);
    }
    std::printf ("    %lld patterns: ltZero differs in %lld, belowTruncation %lld, |x| > 100 %lld, x >= 2 pi %lld, x > pi %lld, |x| < inf %lld, domain test %lld\n", t.patterns, t.ltZero,
                 t.belowTrunc, t.gtHundred, t.geTwoPi, t.gtPi, t.absLtInf, t.domain);
    CHECK_EQ (t.ltZero, 0);
    CHECK_EQ (t.belowTrunc, 0);
    CHECK_EQ (t.gtHundred, 0);
    CHECK_EQ (t.geTwoPi, 0);
    CHECK_EQ (t.gtPi, 0);
    CHECK_EQ (t.absLtInf, 0);
    CHECK_EQ (t.domain, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}

TEST (the_integer_compare_of_two_non_negative_magnitudes_equals_the_float_compare_and_the_domain_test_rejects_the_rest)
{
#if SYNTHYBOY_NEON_CMP
    // the boundary values of the domain: +0, every normal binade at a few mantissas, +infinity
    std::vector<float> d = { 0.0f, std::numeric_limits<float>::infinity() };
    for (uint32_t e = 1; e < 255; ++e)
        for (uint32_t mant : { 0u, 1u, 0x3fffffu, 0x400000u, 0x7ffffeu, 0x7fffffu })
            d.push_back (fromBits ((e << 23) | mant));
    while (d.size() % 4 != 0)
        d.push_back (0.0f);
    long long bad = 0, pairs = 0;
    const auto pair4 = [&] (const float a[4], const float bb[4])
    {
        uint32_t g[4], ge[4];
        vst1q_u32 (g, gtNonNeg (vld1q_f32 (a), vld1q_f32 (bb)));
        vst1q_u32 (ge, geNonNeg (vld1q_f32 (a), vld1q_f32 (bb)));
        for (int l = 0; l < 4; ++l)
        {
            bad += g[l] != (a[l] > bb[l] ? 0xffffffffu : 0u) ? 1 : 0;
            bad += ge[l] != (a[l] >= bb[l] ? 0xffffffffu : 0u) ? 1 : 0;
        }
        pairs += 4;
    };
    for (float a : d)
        for (size_t j = 0; j + 4 <= d.size(); j += 4)
        {
            const float av[4] = { a, a, a, a };
            pair4 (av, &d[j]);
        }
    Rng r (21);
    const int rounds = sbtest::quickScaled ("compare sites: random magnitude pairs", 5000000, 500000, 0);
    for (int i = 0; i < rounds; ++i)
    {
        float av[4], bv[4];
        for (int l = 0; l < 4; ++l)
        {
            // normal magnitudes, with exact ties and neighbours (the compares of a tie and of one ulp)
            const uint32_t u = 0x00800000u + r.next() % (0x7f800000u - 0x00800000u + 1u);
            av[l] = fromBits (u);
            const uint32_t k = r.next() % 4u;
            bv[l] = k == 0 ? av[l] : (k == 1 ? fromBits (u + 1u > 0x7f800000u ? u : u + 1u) : (k == 2 ? fromBits (u - 1u < 0x00800000u ? u : u - 1u) : fromBits (0x00800000u + r.next() % (0x7f800000u - 0x00800000u + 1u))));
        }
        pair4 (av, bv);
    }
    std::printf ("    %lld ordered pairs of magnitudes: gtNonNeg / geNonNeg differ from the float compares in %lld\n", pairs, bad);
    CHECK_EQ (bad, 0);
    // outside the domain the forms are not the float compares (that is why the callers test the domain): the domain test must say so for every one of these
    for (float x : { -0.0f, -1.0f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), fromBits (0xffc00000u), fromBits (1u), fromBits (0x007fffffu) })
    {
        uint32_t m[4];
        vst1q_u32 (m, nonNegNormalOrZero (vdupq_n_f32 (x)));
        CHECK_EQ (m[0], 0u);
    }
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}
