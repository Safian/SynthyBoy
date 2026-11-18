// SynthyBoy - own engine tests: the lane mix of a library player into the oscillator slot (LibOsc: d[k] += a[k] * lerp (g0, g1, (t0 + k + 1) * invP)) in NEON lanes (simd::rampLerp then
// simd::mulAddTo) gives, sample by sample, the bits of the scalar loop. Compiled with the NEON forms on and without fused multiply-add (core/tests/CMakeLists.txt); links nothing of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "dsp/simd.h"
#include "dsp/util.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_lane_mix_in_neon_lanes_is_the_scalar_mix)
{
    static_assert (SB_NEON == 1, "this test is built with the NEON forms on");
    uint32_t s = 4242u;
    const auto uni = [&] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float> (s >> 8) * (1.0f / 16777216.0f); };
    const float invP = 1.0f / static_cast<float> (kControlPeriod);
    int cases = 0;
    for (int rep = 0; rep < 2000; ++rep)
    {
        const int n = 1 + static_cast<int> (uni() * 32.0f) % 32;
        const int t0 = (rep % 4) * 32 * (1 + rep % 3);   // (any multiple of a control period in a block, and beyond)
        const float g0 = 4.0f * uni() - 2.0f, g1 = rep % 7 == 0 ? g0 : 4.0f * uni() - 2.0f;
        std::vector<float> a (32), d (32), ref (32), gk (32);
        for (int k = 0; k < 32; ++k)
        {
            a[static_cast<size_t> (k)] = 2.0f * uni() - 1.0f;
            d[static_cast<size_t> (k)] = ref[static_cast<size_t> (k)] = 2.0f * uni() - 1.0f;
        }
        for (int k = 0; k < n; ++k)
            ref[static_cast<size_t> (k)] += a[static_cast<size_t> (k)] * lerp (g0, g1, static_cast<float> (t0 + k + 1) * invP);
        simd::rampLerp (gk.data(), g0, g1, t0, n, invP);
        simd::mulAddTo (d.data(), a.data(), gk.data(), n);
        ++cases;
        CHECK (std::memcmp (d.data(), ref.data(), 32 * sizeof (float)) == 0);   // (the samples after n are untouched)
    }
    CHECK_EQ (cases, 2000);
}

/** simd::rampLerp with the lane offsets in a register (CPU-REVIEW-VOICE-MOD.md V2) against the form it replaced (simd::ramp4 per step) and the scalar loop: the same bits for every start, length
    and pair of ends (signed zeros, denormals, infinities and NaN among them). */
TEST (own_ramp_lerp_with_the_offsets_in_a_register_is_the_ramp4_form)
{
    static_assert (SB_NEON == 1, "this test is built with the NEON forms on");
    uint32_t s = 99u;
    const auto bits = [&] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
    const float special[] = { 0.0f, -0.0f, 1.0e-40f, -1.0e-40f, 1.0f, -1.0f, 3.4e38f, -3.4e38f, INFINITY, -INFINITY, NAN, 1.0e-30f };
    long long compared = 0;
    for (int rep = 0; rep < 200000; ++rep)
    {
        auto value = [&] () -> float
        {
            const uint32_t r = bits();
            if (r % 5 == 0)
                return special[r / 5 % (sizeof special / sizeof special[0])];
            float f;
            uint32_t u = bits();
            std::memcpy (&f, &u, 4);
            return (r % 5 == 1) ? f : static_cast<float> (static_cast<int32_t> (u >> 8) - (1 << 23)) * 1.0e-6f;
        };
        const float a = value(), b = value();
        const int n = 1 + static_cast<int> (bits() % 32u);
        const int t0 = rep % 3 == 0 ? static_cast<int> (bits() % 5000u) : (rep % 4) * 32;
        const float invP = rep % 2 == 0 ? 1.0f / static_cast<float> (kControlPeriod) : 1.0f / static_cast<float> (1 + bits() % 4096u);
        float got[32], old[32], scalar[32];
        simd::rampLerp (got, a, b, t0, n, invP);
        int k = 0;
        const float32x4_t va = vdupq_n_f32 (a), vb = vdupq_n_f32 (b);
        for (; k + 4 <= n; k += 4)
            vst1q_f32 (old + k, simd::lerp4 (va, vb, simd::ramp4 (t0, k, invP)));
        for (; k < n; ++k)
            old[k] = a + (b - a) * (static_cast<float> (t0 + k + 1) * invP);
        for (k = 0; k < n; ++k)
            scalar[k] = a + (b - a) * (static_cast<float> (t0 + k + 1) * invP);
        compared += n;
        CHECK (std::memcmp (got, old, static_cast<size_t> (n) * sizeof (float)) == 0);
        CHECK (std::memcmp (got, scalar, static_cast<size_t> (n) * sizeof (float)) == 0);
    }
    CHECK (compared > 1000000);
}
