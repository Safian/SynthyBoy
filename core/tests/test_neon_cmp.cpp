// SynthyBoy - own engine tests: the integer NEON compares of dsp/neon_cmp.h give the masks of the float compares, for every input.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The constant forms are checked on all 2^32 bit patterns of x for every constant the engine uses them with (and a few more), against the scalar IEEE compare and
// against the same compare in flush-to-zero mode (a subnormal x taken as zero of its sign); the general forms on every pair of a list of special values (both zeros,
// subnormals, the normal boundaries, both infinities, NaNs of both signs and several payloads) and on random pairs of bit patterns.
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
/** x as the flush-to-zero compare sees it: a subnormal is a zero of its sign. */
float ftz (float x)
{
    const uint32_t u = bitsOf (x);
    return (u & 0x7f800000u) == 0u ? fromBits (u & 0x80000000u) : x;
}
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 7u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

std::vector<float> specialValues()
{
    std::vector<uint32_t> b = { 0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x00000002u, 0x007fffffu, 0x807fffffu, 0x00400000u, 0x00800000u, 0x80800000u,
                                0x00800001u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7f800001u, 0xff800001u, 0x7fc00000u, 0xffc00000u, 0x7fffffffu,
                                0xffffffffu, 0x7fa00000u, 0x3f800000u, 0xbf800000u, 0x3f7fffffu, 0x3f800001u, 0x40000000u, 0xc0000000u, 0x00000010u, 0x80000010u };
    std::vector<float> v;
    for (uint32_t u : b)
        v.push_back (fromBits (u));
    for (float c : { 1.0e-18f, 0.66f, 3.0f, 200.0f, 16.0f, 0.5f, 1.0f, 1.0e-30f, 1.0e30f })
        for (float s : { 1.0f, -1.0f })
        {
            const float x = s * c;
            v.push_back (x);
            v.push_back (std::nextafter (x, 0.0f));
            v.push_back (std::nextafter (x, s * INFINITY));
        }
    return v;
}
#if SYNTHYBOY_NEON_CMP
/** All 2^32 bit patterns of x through a vector compare against c and through the scalar compare (as it is, and with x flushed to zero). */
template <class Vec, class Ref>
void sweep (long long& badIeee, long long& badFtz, float c, Vec vec, Ref ref)
{
    uint32_t bits[4], lanes[4];
    for (uint64_t u = 0; u < (1ull << 32); u += 4)
    {
        for (int l = 0; l < 4; ++l)
            bits[l] = static_cast<uint32_t> (u + static_cast<uint64_t> (l));
        vst1q_u32 (lanes, vec (vreinterpretq_f32_u32 (vld1q_u32 (bits)), c));
        for (int l = 0; l < 4; ++l)
        {
            const float xf = fromBits (bits[l]);
            badIeee += lanes[l] != (ref (xf, c) ? 0xffffffffu : 0u) ? 1 : 0;
            badFtz += lanes[l] != (ref (ftz (xf), c) ? 0xffffffffu : 0u) ? 1 : 0;
        }
    }
}
#endif
} // namespace

TEST (neon_constant_compares_equal_the_float_compares_on_every_bit_pattern)
{
#if SYNTHYBOY_NEON_CMP
    using namespace synthyboy::neoncmp;
    struct Case { const char* name; int kind; float c; };
    // kind: 0 |x| < c, 1 |x| <= c, 2 x >= c, 3 x <= c
    const Case cases[] = { { "|x| < 1e-18", 0, 1.0e-18f }, { "|x| < inf", 0, INFINITY }, { "|x| < 200", 0, 200.0f }, { "|x| <= 0.66", 1, 0.66f },
                           { "x >= 3", 2, 3.0f }, { "x >= inf", 2, INFINITY }, { "x <= -3", 3, -3.0f }, { "x <= -inf", 3, -INFINITY } };
    for (const Case& cs : cases)
    {
        long long badIeee = 0, badFtz = 0;
        switch (cs.kind)
        {
            case 0: sweep (badIeee, badFtz, cs.c, [] (float32x4_t x, float c) { return absLt (x, c); }, [] (float x, float c) { return std::fabs (x) < c; }); break;
            case 1: sweep (badIeee, badFtz, cs.c, [] (float32x4_t x, float c) { return absLe (x, c); }, [] (float x, float c) { return std::fabs (x) <= c; }); break;
            case 2: sweep (badIeee, badFtz, cs.c, [] (float32x4_t x, float c) { return geConst (x, c); }, [] (float x, float c) { return x >= c; }); break;
            default: sweep (badIeee, badFtz, cs.c, [] (float32x4_t x, float c) { return leConst (x, c); }, [] (float x, float c) { return x <= c; }); break;
        }
        std::printf ("    %-12s all 2^32 inputs: %lld differ from the IEEE compare, %lld from the flush-to-zero compare\n", cs.name, badIeee, badFtz);
        CHECK_EQ (badIeee, 0);
        CHECK_EQ (badFtz, 0);
    }
    // the two lane forms are the same instructions on a D register: a sample of patterns
    Rng r (5);
    long long bad2 = 0;
    for (int i = 0; i < 2000000; ++i)
    {
        const uint32_t b[2] = { r.next(), i % 3 == 0 ? (r.next() & 0x807fffffu) : r.next() };
        const float32x2_t x = vreinterpret_f32_u32 (vld1_u32 (b));
        uint32_t o[4][2];
        vst1_u32 (o[0], absLt (x, 1.0e-18f));
        vst1_u32 (o[1], absLe (x, 0.66f));
        vst1_u32 (o[2], geConst (x, 3.0f));
        vst1_u32 (o[3], leConst (x, -3.0f));
        for (int l = 0; l < 2; ++l)
        {
            const float xf = fromBits (b[l]);
            bad2 += o[0][l] != (std::fabs (xf) < 1.0e-18f ? 0xffffffffu : 0u) ? 1 : 0;
            bad2 += o[1][l] != (std::fabs (xf) <= 0.66f ? 0xffffffffu : 0u) ? 1 : 0;
            bad2 += o[2][l] != (xf >= 3.0f ? 0xffffffffu : 0u) ? 1 : 0;
            bad2 += o[3][l] != (xf <= -3.0f ? 0xffffffffu : 0u) ? 1 : 0;
        }
    }
    std::printf ("    two lane forms, 4 million random patterns x 4 compares: %lld differ\n", bad2);
    CHECK_EQ (bad2, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}

TEST (neon_general_compares_equal_the_ieee_compares)
{
#if SYNTHYBOY_NEON_CMP
    using namespace synthyboy::neoncmp;
    long long bad = 0, total = 0;
    auto check = [&] (float a, float b)
    {
        const float av[4] = { a, b, a, -a }, bv[4] = { b, a, a, b };
        uint32_t m[5][4];
        const float32x4_t va = vld1q_f32 (av), vb = vld1q_f32 (bv);
        vst1q_u32 (m[0], lt (va, vb));
        vst1q_u32 (m[1], le (va, vb));
        vst1q_u32 (m[2], gt (va, vb));
        vst1q_u32 (m[3], ge (va, vb));
        vst1q_u32 (m[4], eq (va, vb));
        uint32_t d[5][2];
        const float32x2_t da = vld1_f32 (av), db = vld1_f32 (bv);
        vst1_u32 (d[0], lt (da, db));
        vst1_u32 (d[1], le (da, db));
        vst1_u32 (d[2], gt (da, db));
        vst1_u32 (d[3], ge (da, db));
        vst1_u32 (d[4], eq (da, db));
        for (int l = 0; l < 4; ++l)
        {
            const float x = av[l], y = bv[l];
            const bool want[5] = { x < y, x <= y, x > y, x >= y, x == y };
            for (int k = 0; k < 5; ++k)
            {
                bad += m[k][l] != (want[k] ? 0xffffffffu : 0u) ? 1 : 0;
                if (l < 2)
                    bad += d[k][l] != (want[k] ? 0xffffffffu : 0u) ? 1 : 0;
                ++total;
            }
        }
    };
    const std::vector<float> sv = specialValues();
    for (float a : sv)
        for (float b : sv)
            check (a, b);
    Rng r (11);
    for (int i = 0; i < 3000000; ++i)
    {
        const uint32_t ua = r.next();
        uint32_t ub = r.next();
        if (i % 4 == 1) ub = ua ^ (r.next() & 0xffu);           // neighbours: the same exponent and sign
        else if (i % 4 == 2) ub = ua ^ 0x80000000u;              // the same magnitude, the other sign
        else if (i % 4 == 3) ub = (ua & 0x807fffffu);            // a subnormal or a zero against anything
        check (fromBits (ua), fromBits (ub));
    }
    std::printf ("    %lld compares (special value pairs and random patterns, 2 and 4 lanes): %lld differ\n", total, bad);
    CHECK_EQ (bad, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}

TEST (neon_flush_of_tiny_values_equals_flush_denormal)
{
#if SYNTHYBOY_NEON_CMP
    using namespace synthyboy::neoncmp;
    long long bad = 0;
    uint32_t in[4], out[4];
    for (uint64_t u = 0; u < (1ull << 32); u += 4)
    {
        for (int l = 0; l < 4; ++l)
            in[l] = static_cast<uint32_t> (u + static_cast<uint64_t> (l));
        vst1q_u32 (out, vreinterpretq_u32_f32 (flushTiny (vreinterpretq_f32_u32 (vld1q_u32 (in)))));
        for (int l = 0; l < 4; ++l)
        {
            const float x = fromBits (in[l]);
            const float want = std::fabs (x) < 1.0e-18f ? 0.0f : x; // flushDenormal (dsp/util.h)
            bad += std::memcmp (&want, &out[l], 4) != 0 ? 1 : 0;
        }
    }
    std::printf ("    all 2^32 inputs: %lld differ from flushDenormal\n", bad);
    CHECK_EQ (bad, 0);
#else
    CHECK (true);
#endif
}
