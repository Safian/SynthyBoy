// SynthyBoy - own engine tests: the NEON loops of dsp/simd.h (addScaled, mulAddTo, mulRamp, rampLerp, halfSum, mulBy, addTo, clampTo, allAbsBelow) give, sample by sample, the bits of the scalar
// loops they stand for (written out in this file), for random data and for every special value (zeros of both signs, subnormals, infinities, NaNs, huge values) in every lane position and
// every length 0 .. 40 (the 4 lane loop and its tail). Compiled with the NEON forms on (also on Apple's arm64) and without fused multiply-add; header only, links nothing of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Float modes: AArch64 runs the default (IEEE) mode and the flush-to-zero mode of the MPC shell; 32 bit ARM, whose NEON always flushes subnormals, runs the flush-to-zero mode (and for the
// compare loops, which return no arithmetic result, the default mode without subnormal data). A NaN is compared as a NaN (the NEON of 32 bit ARM makes the default NaN).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "dsp/simd.h"
#include "fp_flush_mode.h"
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
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 13u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return 2.0f * uni() - 1.0f; }
};

bool sameValue (float a, float b) { return (a != a && b != b) || bitsOf (a) == bitsOf (b); }
bool sameValues (const std::vector<float>& a, const std::vector<float>& b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (! sameValue (a[i], b[i]))
            return false;
    return true;
}

const std::vector<float>& specials()
{
    static const std::vector<float> v = [] {
        std::vector<float> s;
        for (uint32_t u : { 0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00000u,
                            0xffc00000u, 0x7f800001u, 0x3f800000u, 0xbf800000u, 0x3f7fffffu, 0x3f800001u, 0x41800000u, 0xc1800000u, 0x4b000000u, 0x7e800000u })
            s.push_back (fromBits (u));
        for (float c : { 200.0f, 16.0f, 1.0e-18f, 0.5f })
            for (float sg : { 1.0f, -1.0f })
            {
                s.push_back (sg * c);
                s.push_back (std::nextafter (sg * c, 0.0f));
                s.push_back (std::nextafter (sg * c, sg * 1.0e30f));
            }
        return s;
    }();
    return v;
}

struct Mode
{
    bool fz, subnormals;
};
std::vector<Mode> modes (bool arithmetic)
{
#if defined(__aarch64__)
    (void) arithmetic;
    return { { false, true }, { true, true } };
#else
    if (arithmetic)
        return { { true, true } };
    return { { false, false }, { true, true } };
#endif
}

float pick (Rng& r, bool subnormals)
{
    float x;
    const uint32_t k = r.next() % 6u;
    if (k == 0)
        x = specials()[r.next() % specials().size()];
    else if (k == 1)
        x = fromBits (r.next());
    else
        x = r.bi() * (k == 2 ? 1.0f : (k == 3 ? 250.0f : 4.0f));
    if (! subnormals && x != 0.0f && std::fabs (x) < 1.17549435e-38f)
        x = 0.0f;
    return x;
}
std::vector<float> data (Rng& r, int n, bool subnormals)
{
    std::vector<float> v (static_cast<size_t> (n));
    for (float& x : v)
        x = pick (r, subnormals);
    return v;
}
/** Products and sums of normal numbers can be subnormal; where the mode does not allow subnormal results (32 bit ARM, default mode) the caller keeps the data in a range where they are not. */
} // namespace

TEST (simd_arithmetic_loops_equal_their_scalar_loops_on_special_values)
{
#if SB_NEON
    long long checks = 0, bad = 0;
    int modeNo = 0;
    for (const Mode& m : modes (true))
    {
        sbtest::FlushMode fm (m.fz);
        Rng r (3 + static_cast<uint32_t> (modeNo++));
        for (int round = 0; round < 6000; ++round)
        {
            const int n = round % 41;
            const std::vector<float> a = data (r, n, m.subnormals), g = data (r, n, m.subnormals), d0 = data (r, n, m.subnormals);
            const float gs = pick (r, m.subnormals), g0 = pick (r, m.subnormals), g1 = pick (r, m.subnormals);
            const float invP = r.uni() * 0.05f, t0 = static_cast<float> (r.next() % 128u);
            const int t0i = static_cast<int> (t0);
            const auto expect = [&] (bool ok) { ++checks; bad += ok ? 0 : 1; };

            std::vector<float> d = d0, e = d0;
            synthyboy::own::simd::addScaled (d.data(), a.data(), gs, n);
            for (int k = 0; k < n; ++k)
                e[static_cast<size_t> (k)] += a[static_cast<size_t> (k)] * gs;
            expect (sameValues (d, e));

            d = d0; e = d0;
            synthyboy::own::simd::mulAddTo (d.data(), a.data(), g.data(), n);
            for (int k = 0; k < n; ++k)
                e[static_cast<size_t> (k)] += a[static_cast<size_t> (k)] * g[static_cast<size_t> (k)];
            expect (sameValues (d, e));

            d = d0; e = d0;
            synthyboy::own::simd::mulRamp (d.data(), g.data(), n);
            synthyboy::own::simd::mulRampScalar (e.data(), g.data(), n);
            expect (sameValues (d, e));

            d = d0; e = d0;
            synthyboy::own::simd::mulBy (d.data(), g.data(), n);
            for (int k = 0; k < n; ++k)
                e[static_cast<size_t> (k)] *= g[static_cast<size_t> (k)];
            expect (sameValues (d, e));

            d = d0; e = d0;
            synthyboy::own::simd::addTo (d.data(), a.data(), n);
            for (int k = 0; k < n; ++k)
                e[static_cast<size_t> (k)] += a[static_cast<size_t> (k)];
            expect (sameValues (d, e));

            d = d0; e = d0;
            synthyboy::own::simd::halfSum (d.data(), a.data(), g.data(), n);
            for (int k = 0; k < n; ++k)
                e[static_cast<size_t> (k)] = 0.5f * (a[static_cast<size_t> (k)] + g[static_cast<size_t> (k)]);
            expect (sameValues (d, e));

            d = d0; e = d0;
            synthyboy::own::simd::rampLerp (d.data(), g0, g1, t0i, n, invP);
            for (int k = 0; k < n; ++k)
                e[static_cast<size_t> (k)] = g0 + (g1 - g0) * (static_cast<float> (t0i + k + 1) * invP);
            expect (sameValues (d, e));
        }
    }
    std::printf ("    %lld comparisons: %lld differ\n", checks, bad);
    CHECK_EQ (bad, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}

TEST (simd_clamp_and_runaway_test_equal_their_scalar_loops_on_special_values)
{
#if SB_NEON
    long long checks = 0, bad = 0;
    int modeNo = 0;
    for (const Mode& m : modes (false))
    {
        sbtest::FlushMode fm (m.fz);
        Rng r (17 + static_cast<uint32_t> (modeNo++));
        for (int round = 0; round < 8000; ++round)
        {
            const int n = round % 41;
            const std::vector<float> a0 = data (r, n, m.subnormals), b0 = data (r, n, m.subnormals);
            // clampTo (the limits the engine uses, and two more): std::clamp keeps a NaN, as the vector form does. A subnormal in flush-to-zero mode is NOT the same: std::clamp passes the
            // subnormal on (it only compares), vminq / vmaxq flush it to a zero of its sign (finding of this test, reported in docs/dsp-knowledge/vector-paths.md): the data of that mode have none.
            std::vector<float> c0 = a0;
            if (m.fz)
                for (float& x : c0)
                    if ((bitsOf (x) & 0x7f800000u) == 0u)   // a subnormal or a zero (tested on the bits: in this mode a float compare flushes the subnormal)
                        x = fromBits (bitsOf (x) & 0x80000000u);
            for (const float lim : { 16.0f, 1.0f, 200.0f })
            {
                std::vector<float> d = c0, e = c0;
                synthyboy::own::simd::clampTo (d.data(), -lim, lim, n);
                for (int k = 0; k < n; ++k)
                    e[static_cast<size_t> (k)] = std::clamp (e[static_cast<size_t> (k)], -lim, lim);
                ++checks;
                if (! sameValues (d, e))
                {
                    ++bad;
                    if (bad < 4)
                        for (int k = 0; k < n; ++k)
                            if (! sameValue (d[static_cast<size_t> (k)], e[static_cast<size_t> (k)]))
                                std::printf ("    clampTo (%g): in %08x vector %08x scalar %08x (fz %d)\n", static_cast<double> (lim), bitsOf (c0[static_cast<size_t> (k)]), bitsOf (d[static_cast<size_t> (k)]), bitsOf (e[static_cast<size_t> (k)]), m.fz ? 1 : 0);
                }
            }
            // allAbsBelow: the scalar loop of FilterUnit (a value that is not below c, a NaN included, makes it false)
            for (const float c : { 200.0f, 1.0e-18f, 16.0f })
            {
                bool expect = true;
                for (int k = 0; k < n; ++k)
                    if (! (std::fabs (a0[static_cast<size_t> (k)]) < c) || ! (std::fabs (b0[static_cast<size_t> (k)]) < c))
                        expect = false;
                ++checks;
                bad += synthyboy::own::simd::allAbsBelow (a0.data(), b0.data(), n, c) == expect ? 0 : 1;
            }
        }
    }
    std::printf ("    %lld comparisons: %lld differ\n", checks, bad);
    CHECK_EQ (bad, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}
