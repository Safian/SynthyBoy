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
    static void group (int w0, int w1, float ph0, float ph1, float* dst, const uint32_t* phs, const float* wbs, const float* cA, const float* cB, float fscale, int N, float a0, float a1)
    {
        WtOsc osc;
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
float scalarSample (uint32_t ph, int w0, int w1, float a0, float a1, float ph0, float ph1, const float* cA, const float* cB, float fscale, int N, float wB)
{
    const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
    float window = 1.0f, w2 = 1.0f, pp = p;
    if (w0 != 0)
        pp = warpPhase (w0, pp, a0, ph0, 0.0f, window, nullptr, true);
    if (w1 != 0)
    {
        pp = warpPhase (w1, pp, a1, ph1, 0.0f, w2, nullptr, true);
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
        y = warpSample (w0, y, a0, 0.0f, true);
    if (w1 != 0)
        y = warpSample (w1, y, a1, 0.0f, true);
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
                WtOscLaneTest::group (w0, w1, ph0, ph1, out, phs, wbs, cA, second ? cB : nullptr, static_cast<float> (N), N, a0, a1);
                for (int l = 0; l < 4; ++l)
                {
                    const float ref = scalarSample (phs[l], w0, w1, a0, a1, ph0, ph1, cA, second ? cB : nullptr, static_cast<float> (N), N, wbs[l]);
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
