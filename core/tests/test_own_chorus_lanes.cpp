// SynthyBoy - own engine tests: the chorus with its four lane LFO refresh gives the samples of its scalar refresh (Chorus::scalarLoops, the --scalar-filters switch of
// the bench). Compiled with the NEON forms on (also on Apple's arm64) and without fused multiply-add, in the executable of test_own_filter_unit_lanes.cpp.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "fx/fx_effects.h"
#include "test_runner.h"

using namespace synthyboy::own;

namespace
{
struct CRng
{
    uint32_t s;
    explicit CRng (uint32_t seed) : s (seed * 2654435761u + 5u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return 2.0f * uni() - 1.0f; }
};
} // namespace

TEST (chorus_lfo_refresh_in_lanes_equals_the_scalar_refresh)
{
    long long bad = 0, total = 0;
    for (int run = 0; run < 60; ++run)
    {
        CRng r (static_cast<uint32_t> (run + 1));
        Chorus lane, ref;
        lane.prepare (run % 5 == 4 ? 48000.0 : 44100.0);
        ref.prepare (run % 5 == 4 ? 48000.0 : 44100.0);
        lane.fast = ref.fast = run % 7 != 6; // tier low (the table sine, the four lane form), and now and then the library sine (scalar in both)
        ref.scalarLoops = true;
        Chorus::Params p;
        for (int block = 0; block < 300; ++block)
        {
            if (block % 40 == 0)
            {
                static const float kRates[] = { 0.0f, 0.01f, 0.4f, 3.0f, 9.7f, 20.0f, 250.0f, 11025.0f };
                p.rate = (run % 3 == 0) ? kRates[r.next() % 8u] : 0.05f + 10.0f * r.uni();
                p.depth = run % 4 == 0 ? (r.uni() < 0.5f ? 0.0f : 1.0f) : r.uni();
                p.voices = 1 + static_cast<int> (r.next() % 4u);
                p.feedback = 1.9f * r.uni() - 0.95f;
                p.delay1 = 0.0005f + 0.03f * r.uni();
                p.delay2 = 0.0005f + 0.04f * r.uni();
                p.cutoff = 50.0f + 9000.0f * r.uni();
                p.spread = r.uni();
                p.mix = r.uni();
                lane.set (p);
                ref.set (p);
            }
            const int n = block % 9 == 0 ? 13 : 32;
            float l1[32], r1[32], l2[32], r2[32];
            const int kind = block % 6;
            for (int i = 0; i < n; ++i)
            {
                float a = r.bi(), b = r.bi();
                if (kind == 1) { a *= 8.0f; b *= 8.0f; }
                else if (kind == 2) { a = 0.0f; b = -0.0f; }
                else if (kind == 3) { uint32_t u = r.next() & 0x807fffffu; std::memcpy (&a, &u, 4); b = a; }
                if (run == 59 && block == 250 && i == 3)
                    a = std::numeric_limits<float>::quiet_NaN(); // the same way in both (the tap groups are the same code)
                l1[i] = l2[i] = a;
                r1[i] = r2[i] = b;
            }
            lane.process (l1, r1, n);
            ref.process (l2, r2, n);
            bad += (std::memcmp (l1, l2, static_cast<size_t> (n) * 4) != 0 || std::memcmp (r1, r2, static_cast<size_t> (n) * 4) != 0) ? 1 : 0;
            total += 2 * n;
        }
    }
    std::printf ("    %lld samples (60 runs: rates 0 to Nyquist, 1 to 4 voice pairs, loud, silent and subnormal input, one NaN): %lld blocks differ\n", total, bad);
    CHECK_EQ (bad, 0);
}

TEST (chorus_lane_floor_sine_and_refresh_equal_the_scalar_functions)
{
#if SB_NEON
    // ffloorFour and sinCyclesFour against ffloor () and fastmath::sinCycles () on special values (zeros, subnormals, integers and their neighbours, 2^22 and
    // beyond, infinities, NaN) and random bit patterns; refreshFour against the scalar refresh of four taps
    auto same = [] (float a, float b) { return std::memcmp (&a, &b, 4) == 0 || (std::isnan (a) && std::isnan (b)); };
    std::vector<float> vals = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 1.75f, 2.0f, 4194304.0f, -4194304.0f, 4194303.5f, 8388608.0f, -8388608.0f, 1.0e30f, -1.0e30f, INFINITY, -INFINITY,
                                std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(),
                                std::numeric_limits<float>::min(), -std::numeric_limits<float>::min() };
    for (float x : { 1.0f, 2.0f, 3.0f, -1.0f, -2.0f, 0.25f, 0.75f })
    {
        vals.push_back (std::nextafter (x, 0.0f));
        vals.push_back (std::nextafter (x, 100.0f * x));
    }
    CRng r (77);
    for (int i = 0; i < 3000000; ++i)
    {
        const uint32_t u = r.next();
        float x;
        if (i % 3 == 0) { std::memcpy (&x, &u, 4); }
        else x = (i % 3 == 1) ? 4.0f * r.uni() - 1.0f : 3.0e-38f * r.bi(); // the chorus range, and values near the subnormals
        vals.push_back (x);
    }
    CHECK_EQ (vals.size() % 4, 0u); // Every generated input must be covered by the four-lane loop.
    long long badFloor = 0, badSin = 0;
    for (size_t k = 0; k + 4 <= vals.size(); k += 4)
    {
        const float32x4_t x = vld1q_f32 (vals.data() + k);
        float f[4], s[4];
        vst1q_f32 (f, Chorus::ffloorFour (x));
        for (int l = 0; l < 4; ++l)
            badFloor += same (f[l], ffloor (vals[k + static_cast<size_t> (l)])) ? 0 : 1;
        vst1q_f32 (s, Chorus::sinCyclesFour (x));
        for (int l = 0; l < 4; ++l)
            badSin += same (s[l], fastmath::sinCycles (vals[k + static_cast<size_t> (l)])) ? 0 : 1;
    }
    // refreshFour against the loop of Chorus::process () for one voice pair
    long long badRefresh = 0;
    for (int i = 0; i < 400000; ++i)
    {
        const float phase = i % 50 == 0 ? 0.0f : (i % 50 == 1 ? std::nextafter (1.0f, 0.0f) : r.uni()), phOff = 0.25f * r.uni(), avg = 0.02f * r.uni(), t = r.uni();
        const float baseOf[2] = { 0.03f * r.uni(), 0.03f * r.uni() }, depth = i % 7 == 0 ? 1.0f : r.uni(), sr = i % 2 == 0 ? 44100.0f : 48000.0f;
        float lane[4];
        Chorus::refreshFour (lane, phase, phOff, baseOf, avg, t, depth, sr);
        for (int vi = 0; vi < 2; ++vi)
            for (int c = 0; c < 2; ++c)
            {
                float ph = phase + (c == 1 ? 0.25f : 0.0f) + (vi == 1 ? 0.5f : 0.0f) + phOff;
                ph -= ffloor (ph);
                const float mod = 1.0f + 0.5f * fastmath::sinCycles (ph);
                const float delay = mod * 0.03f * depth + lerp (baseOf[vi], avg, t);
                const float want = std::clamp (delay * sr, 3.0f, 0.08f * sr);
                badRefresh += same (lane[vi * 2 + c], want) ? 0 : 1;
            }
    }
    std::printf ("    %zu values: ffloorFour differs in %lld, sinCyclesFour in %lld; 400000 refreshes of four taps: %lld taps differ\n", vals.size(), badFloor, badSin, badRefresh);
    CHECK_EQ (badFloor, 0);
    CHECK_EQ (badSin, 0);
    CHECK_EQ (badRefresh, 0);
#else
    std::printf ("    no NEON in this build\n");
    CHECK (true);
#endif
}
