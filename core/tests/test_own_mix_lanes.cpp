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
