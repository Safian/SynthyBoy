// SynthyBoy - own engine tests: the four lane form of the oversampled oscillator samples (WtOsc::warpedGroup4 and its parts) computes, lane by lane, what the scalar
// functions compute: the same bits. Compiled with the NEON forms on and without fused multiply-add (core/tests/CMakeLists.txt); links nothing of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "osc/wt_osc.h"
#include "test_runner.h"

namespace synthyboy
{
namespace own
{
#if SB_NEON
struct WtOscLaneTest
{
    static float32x4_t floor4 (float32x4_t x) { return WtOsc::ffloor4 (x); }
    static float32x4_t sin4 (float32x4_t x) { return WtOsc::sinCycles4 (x); }
    static bool laneType (int t) { return WtOsc::lanePhaseWarp (t); }
    static void group (bool fast, int w0, int w1, float ph0, float ph1, float* dst, const uint32_t* phs, const float* wbs, const float* cA, const float* cB, float fscale, int N, float a0, float a1)
    {
        WtOsc osc;
        osc.tick.fastMath = fast;
        osc.tick.warp[0] = w0;
        osc.tick.warp[1] = w1;
        osc.tick.warpPhase[0] = ph0;
        osc.tick.warpPhase[1] = ph1;
        osc.warpedGroup4 (dst, phs, wbs, cA, cB, fscale, N, a0, a1);
    }
};
#endif
} // namespace own
} // namespace synthyboy

using namespace synthyboy::own;

namespace
{
struct TestRng
{
    uint32_t s;
    explicit TestRng (uint32_t seed) : s (seed * 2654435761u + 99u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
};

bool sameBits (float a, float b) { return std::memcmp (&a, &b, 4) == 0; }

#if SB_NEON
/** sampleAt () of WtOsc::render for two phase warps, no cross signal, no filter stage, the fast forms: the scalar functions of the engine in its order. */
float scalarSample (bool fast, uint32_t ph, int w0, int w1, float a0, float a1, float ph0, float ph1, const float* cA, const float* cB, float fscale, int N, float wB)
{
    const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
    float window = 1.0f, w2 = 1.0f, pp = p;
    if (w0 != 0)
        pp = warpPhase (w0, pp, a0, ph0, 0.0f, window, nullptr, fast);
    if (w1 != 0)
    {
        pp = warpPhase (w1, pp, a1, ph1, 0.0f, w2, nullptr, fast);
        window *= w2;
    }
    float q = pp;
    q -= ffloor (q);
    const float pos = q * fscale;
    const int idx = std::clamp (static_cast<int> (pos), 0, N);
    const float fr = pos - static_cast<float> (idx);
    float r = hermite (cA + idx, fr);
    if (cB != nullptr)
        r = lerp (r, hermite (cB + idx, fr), wB);
    float y = r;
    y *= window;
    if (w0 != 0)
        y = warpSample (w0, y, a0, 0.0f, fast);
    if (w1 != 0)
        y = warpSample (w1, y, a1, 0.0f, fast);
    return y;
}
#endif
} // namespace

TEST (the_parts_of_the_lane_form_equal_the_scalar_functions)
{
#if SB_NEON
    TestRng r (3);
    long long badFloor = 0, badSin = 0;
    for (int i = 0; i < 400000; ++i)
    {
        float x[4], f[4], s[4];
        for (float& v : x)
        {
            const int kind = static_cast<int> (r.next() % 6u);
            v = kind == 0 ? r.uni() : (kind == 1 ? 14.0f * r.uni() - 1.0f : (kind == 2 ? std::floor (12.0f * r.uni()) : (kind == 3 ? 0.0f : (kind == 4 ? -r.uni() : 1.0f - 1.0e-7f * r.uni()))));
        }
        vst1q_f32 (f, WtOscLaneTest::floor4 (vld1q_f32 (x)));
        vst1q_f32 (s, WtOscLaneTest::sin4 (vld1q_f32 (x)));
        for (int l = 0; l < 4; ++l)
        {
            badFloor += sameBits (f[l], ffloor (x[l])) ? 0 : 1;
            badSin += sameBits (s[l], fastmath::sinCycles (x[l])) ? 0 : 1;
        }
    }
    std::printf ("    1.6 million values each: ffloor4 differs in %lld, sinCycles4 in %lld\n", badFloor, badSin);
    CHECK_EQ (badFloor, 0);
    CHECK_EQ (badSin, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}

/** ffloor4 () and sinCycles4 () (the lane forms; their compares are the integer forms of dsp/neon_cmp.h) against ffloor () and fastmath::sinCycles () over their whole domain: every normal float of
    either sign with |x| < 2^22 (ffloor4) or |c| < 2^23 (sinCycles4, the table sine), with SB_TEST_FULL=1 every 1st / 13th such pattern, in the quick run every 4099th, and in both the binade
    boundaries, the integers and their neighbours. (A subnormal phase is not in the domain: documented at the functions.) */
TEST (the_lane_floor_and_sine_equal_the_scalar_functions_over_their_whole_domain)
{
#if SB_NEON
    const uint32_t strideFloor = static_cast<uint32_t> (sbtest::quickScaled ("lane floor: sweep of the domain (stride)", 1, 4099));
    const uint32_t strideSin = static_cast<uint32_t> (sbtest::quickScaled ("lane sine: sweep of the domain (stride)", 13, 4099));
    const auto fromBits = [] (uint32_t u) { float f; std::memcpy (&f, &u, 4); return f; };
    std::vector<uint32_t> boundary;
    for (uint32_t sign = 0; sign < 2; ++sign)
    {
        for (uint32_t e = 1; e < 151; ++e)
            for (uint32_t mant : { 0u, 1u, 2u, 0x3fffffu, 0x400000u, 0x400001u, 0x7ffffeu, 0x7fffffu })
                boundary.push_back ((sign << 31) | (e << 23) | mant);
        for (uint32_t k = 0; k < 40; ++k)   // the integers 0 .. 39 and their neighbours
            for (int d = -3; d <= 3; ++d)
            {
                const uint32_t u = [&] { const float f = static_cast<float> (k); uint32_t b; std::memcpy (&b, &f, 4); return b; }();
                boundary.push_back ((sign << 31) | ((u + static_cast<uint32_t> (d)) & 0x7fffffffu));
            }
    }
    long long badFloor = 0, badSin = 0, valuesFloor = 0, valuesSin = 0;
    // four patterns at a time through the lane form and the scalar function (the sweep is streamed: all 2^31 patterns would not fit in memory)
    const auto group = [&] (bool sine, const uint32_t b[4])
    {
        float x[4], out[4];
        for (int l = 0; l < 4; ++l)
            x[l] = fromBits (b[l]);
        vst1q_f32 (out, sine ? WtOscLaneTest::sin4 (vld1q_f32 (x)) : WtOscLaneTest::floor4 (vld1q_f32 (x)));
        for (int l = 0; l < 4; ++l)
        {
            // ffloor4 (-0) is +0 where ffloor (-0) is -0 (the truncation makes the zero): a documented difference of the domain, a phase is never -0 (see ffloor4); every other input is bit for bit
            const bool ok = sameBits (out[l], sine ? fastmath::sinCycles (x[l]) : ffloor (x[l])) || (! sine && x[l] == 0.0f && out[l] == 0.0f);
            if (! ok && (sine ? badSin : badFloor) < 4)
            {
                uint32_t xb, ob;
                std::memcpy (&xb, &x[l], 4);
                std::memcpy (&ob, &out[l], 4);
                std::printf ("    %s: input %08x lane form %08x\n", sine ? "sinCycles4" : "ffloor4", xb, ob);
            }
            (sine ? badSin : badFloor) += ok ? 0 : 1;
            ++(sine ? valuesSin : valuesFloor);
        }
    };
    const auto run = [&] (bool sine, uint32_t limit, uint32_t stride)
    {
        uint32_t b[4];
        int n = 0;
        const auto push = [&] (uint32_t u)
        {
            b[n++] = u;
            if (n == 4)
            {
                group (sine, b);
                n = 0;
            }
        };
        for (uint64_t m = 0x00800000u; m < limit; m += stride)
            for (uint32_t sign = 0; sign < 2; ++sign)
                push ((sign << 31) | static_cast<uint32_t> (m));
        for (uint32_t bd : boundary)
            if ((bd & 0x7fffffffu) >= 0x00800000u && (bd & 0x7fffffffu) < limit)
                push (bd);
        push (0u);
        push (0x80000000u);   // +0 and -0
        while (n != 0)
            push (0u);
    };
    run (false, 0x4a800000u, strideFloor);
    run (true, 0x4b000000u, strideSin);
    std::printf ("    ffloor4: %lld values, %lld differ; sinCycles4: %lld values, %lld differ\n", valuesFloor, badFloor, valuesSin, badSin);
    CHECK_EQ (badFloor, 0);
    CHECK_EQ (badSin, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}

TEST (four_warped_samples_at_once_equal_four_scalar_samples)
{
#if SB_NEON
    // two cycles of 2048 samples with the guard samples the Hermite read needs (index -1 .. N + 2)
    const int N = 2048;
    std::vector<float> a (static_cast<size_t> (N) + 8), b (static_cast<size_t> (N) + 8);
    TestRng r (11);
    for (int i = 0; i < N + 8; ++i)
    {
        a[static_cast<size_t> (i)] = static_cast<float> (std::sin (6.283185307179586 * (i - 1) / N) + 0.3 * std::sin (6.283185307179586 * 7 * (i - 1) / N));
        b[static_cast<size_t> (i)] = 2.0f * r.uni() - 1.0f;
    }
    const float* cA = a.data() + 1;
    const float* cB = b.data() + 1;
    static const int kTypes[] = { kWarpNone, kWarpSync, kWarpSyncVar, kWarpFormant, kWarpBend, kWarpBendBoth, kWarpBendPlus, kWarpBendMinus };
    for (const int t : kTypes)
        CHECK (WtOscLaneTest::laneType (t));
    CHECK (! WtOscLaneTest::laneType (kWarpQuantize));
    CHECK (! WtOscLaneTest::laneType (kWarpFilterLpf));
    CHECK (! WtOscLaneTest::laneType (kWarpFmA));
    long long bad = 0, total = 0;
    for (const int w0 : kTypes)
        for (const int w1 : kTypes)
        {
            if (w0 == kWarpNone && w1 == kWarpNone)
                continue;
            for (int rep = 0; rep < 1500; ++rep)
            {
                const bool fast = rep % 4 != 3; // the table sine of the Math group, and every fourth time the library sine and cosine
                const float a0 = rep % 7 == 0 ? 0.0f : (rep % 11 == 0 ? 1.0f : r.uni()), a1 = rep % 5 == 0 ? 1.0f : r.uni();
                const float ph0 = rep % 3 == 0 ? 0.5f : 2.0f * r.uni() - 0.5f, ph1 = r.uni();
                const bool second = rep % 2 == 0;
                uint32_t phs[4];
                float wbs[4], out[4];
                uint32_t ph = rep % 13 == 0 ? 0u : r.next();
                const uint32_t inc = rep % 17 == 0 ? 0u : (r.next() >> 6);
                for (int l = 0; l < 4; ++l)
                {
                    phs[l] = ph;
                    wbs[l] = r.uni();
                    ph += inc;
                }
                if (rep % 19 == 0)
                    phs[1] = 0xffffffffu, phs[2] = 0x000000ffu, phs[3] = 0x80000000u;
                WtOscLaneTest::group (fast, w0, w1, ph0, ph1, out, phs, wbs, cA, second ? cB : nullptr, static_cast<float> (N), N, a0, a1);
                for (int l = 0; l < 4; ++l)
                {
                    const float ref = scalarSample (fast, phs[l], w0, w1, a0, a1, ph0, ph1, cA, second ? cB : nullptr, static_cast<float> (N), N, wbs[l]);
                    bad += sameBits (out[l], ref) ? 0 : 1;
                    ++total;
                }
            }
        }
    std::printf ("    %lld samples over the 63 pairs of phase warps: %lld differ\n", total, bad);
    CHECK_EQ (bad, 0);
#else
    CHECK (true);
#endif
}

// ---- the Digital filter model's pair loop: the NEON form against the scalar loop --------------------------------------------------------------------------------
#include "voice/classic_filters.h"

TEST (digital_filter_pair_loop_in_lanes_equals_the_scalar_loop)
{
#if SB_NEON
    TestRng r (21);
    long long bad = 0, total = 0;
    for (int run = 0; run < 400; ++run)
    {
        ClassicFilter refA, refB, laneA, laneB;
        for (ClassicFilter* f : { &refA, &refB, &laneA, &laneB })
        {
            f->prepare (44100.0);
            f->reset();
            f->fastMath = run % 3 != 0; // the limiter's tanh in its fast and its library form
        }
        for (int block = 0; block < 120; ++block)
        {
            // setups as the model makes them: g = tan (pi f / sr), damping, drive gains, section weights; the two channels alike or not
            auto setup = [&] ()
            {
                ClassicFilter::SvfSetup z;
                z.g = std::tan (3.14159265f * (0.0004f + 0.44f * r.uni() * r.uni()));
                z.R = 0.03f + 2.0f * r.uni();
                z.R1 = r.uni() < 0.7f ? 1.0f : 0.5f + r.uni();
                z.dEff = 0.2f + 6.0f * r.uni();
                z.post = 0.3f + 1.5f * r.uni();
                z.wa = { 2.0f * r.uni() - 1.0f, 2.0f * r.uni() - 1.0f, 2.0f * r.uni() - 1.0f };
                z.wb = { 2.0f * r.uni() - 1.0f, 2.0f * r.uni() - 1.0f, 2.0f * r.uni() - 1.0f };
                return z;
            };
            ClassicFilter::SvfSetup za = setup(), zb = block % 2 == 0 ? za : setup();
            za.two = zb.two = (run % 2) == 0;
            const int n = block % 7 == 0 ? 5 : 8;
            float inA[8], inB[8], oA[8], oB[8], pA[8], pB[8];
            const int kind = block % 6;
            for (int i = 0; i < n; ++i)
            {
                const float gain = kind == 0 ? 3.0f : (kind == 1 ? 0.0f : (kind == 2 ? 1.0e-19f : (kind == 3 ? 0.02f : 0.6f)));
                inA[i] = gain * (2.0f * r.uni() - 1.0f);
                inB[i] = gain * (2.0f * r.uni() - 1.0f);
            }
            if (run == 399 && block == 60)
                inA[2] = std::nanf (""); // a NaN goes the same way in both forms (and stays in the filter in both)
            ClassicFilter::svfPairLoopScalar (refA, refB, za, zb, inA, oA, inB, oB, n, 3);
            ClassicFilter::svfPairLoop (laneA, laneB, za, zb, inA, pA, inB, pB, n, 3);
            bad += std::memcmp (oA, pA, static_cast<size_t> (n) * 4) == 0 && std::memcmp (oB, pB, static_cast<size_t> (n) * 4) == 0 ? 0 : 1;
            total += 2 * n;
        }
    }
    std::printf ("    %lld samples in 48000 blocks (one and two sections, loud, silent, tiny and NaN input): %lld blocks differ\n", total, bad);
    CHECK_EQ (bad, 0);
#else
    CHECK (true);
#endif
}

TEST (formant_model_with_its_four_resonators_in_lanes_equals_the_scalar_loop)
{
#if SB_NEON
    TestRng r (5);
    long long bad = 0, total = 0;
    for (int run = 0; run < 300; ++run)
    {
        ClassicFilter ref, lane;
        ref.prepare (44100.0);
        lane.prepare (44100.0);
        ref.reset();
        lane.reset();
        ref.scalarLoops = true;
        ClassicParams p;
        p.model = 5;
        p.style = run % 3;
        for (int block = 0; block < 100; ++block)
        {
            const ClassicParams before = p;
            if (block % 9 == 0)
            {
                p.fx = r.uni();
                p.fy = r.uni();
                p.fres = r.uni();
                p.fspread = 2.0f * r.uni() - 1.0f;
                p.ftranspose = 24.0f * r.uni() - 12.0f;
                p.cutoffHz = 200.0f + 4000.0f * r.uni();
            }
            const int n = block % 5 == 0 ? 7 : 32;
            float in[32], a[32], b[32];
            const int kind = block % 6;
            for (int i = 0; i < n; ++i)
                in[i] = (kind == 0 ? 2.0f : (kind == 1 ? 0.0f : (kind == 2 ? 1.0e-19f : 0.5f))) * (2.0f * r.uni() - 1.0f);
            if (run == 299 && block == 50)
                in[3] = std::nanf ("");
            ref.process (in, a, n, before, p, 0);
            lane.process (in, b, n, before, p, 0);
            bad += std::memcmp (a, b, static_cast<size_t> (n) * 4) == 0 ? 0 : 1;
            total += n;
        }
    }
    std::printf ("    %lld samples in 30000 blocks (three styles, moving vowels, loud, silent, tiny and NaN input): %lld blocks differ\n", total, bad);
    CHECK_EQ (bad, 0);
#else
    CHECK (true);
#endif
}
