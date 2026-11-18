// SynthyBoy - own engine tests: the NEON inverse transform of the spectral morph cycle (morph.cpp inverseFft) gives the very bits of its scalar loops, for every morph type, both transform
// sizes (the full one and the half size one of tier low) and a range of amounts and band limits. Compiled with the NEON form switched on (also on Apple's arm64, where the engine runs the
// scalar code) and without fused multiply-add, together with the morph and the wavetable sources it needs (core/tests/CMakeLists.txt): it links nothing else of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "osc/morph.h"
#include "test_runner.h"

using namespace synthyboy::own;

TEST (own_morph_neon_transform_equals_the_scalar_transform)
{
    constexpr int kCyc = Wavetable::kCycle;
    std::vector<std::vector<float>> cc (2, std::vector<float> (kCyc / 2 + 1, 0.0f)), ss = cc;
    for (int f = 0; f < 2; ++f)
        for (int h = 1; h <= 1023; ++h)
        {
            ss[static_cast<size_t> (f)][static_cast<size_t> (h)] = 1.0f / static_cast<float> (h) * (f != 0 ? 0.7f : 1.0f);
            cc[static_cast<size_t> (f)][static_cast<size_t> (h)] = 0.3f / static_cast<float> (h) * static_cast<float> ((h * 7 + f) % 5 - 2);
        }
    Wavetable wt;
    wt.build (cc, ss, false);
    static MorphScratch scratch;
    std::vector<float> a (static_cast<size_t> (kCyc) + 4), b (static_cast<size_t> (kCyc) + 4);
    int builds = 0, differ = 0;
    for (int type = 0; type < 12; ++type)
        for (float m : { 0.0f, 0.17f, 0.5f, 0.83f, 1.0f })
            for (int K : { 40, 300, 1000 })
                for (int fast = 0; fast < 2; ++fast)
                {
                    buildMorphCycle (wt, 0.4f, true, type, m, K, 0.004f, 44100.0, a.data() + 1, scratch, fast != 0, false);
                    buildMorphCycle (wt, 0.4f, true, type, m, K, 0.004f, 44100.0, b.data() + 1, scratch, fast != 0, true);
                    ++builds;
                    differ += std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) != 0 ? 1 : 0;
                }
    CHECK_EQ (builds, 360);
    CHECK_EQ (differ, 0);
}

namespace
{
/** A small deterministic generator for the randomised patch sets of the tests. */
struct TestRng
{
    uint64_t s;
    explicit TestRng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t> (s >> 16); }
    float uni() { return static_cast<float> (next() & 0xFFFFFF) / 16777216.0f; }
    int range (int lo, int hi) { return lo + static_cast<int> (next() % static_cast<uint32_t> (hi - lo + 1)); }
};

Wavetable randomTable (TestRng& r, int frames)
{
    constexpr int kCyc = Wavetable::kCycle;
    std::vector<std::vector<float>> cc (static_cast<size_t> (frames), std::vector<float> (kCyc / 2 + 1, 0.0f)), ss = cc;
    for (int f = 0; f < frames; ++f)
    {
        const int top = r.range (1, 1023);
        for (int h = 1; h <= top; ++h)
        {
            const float a = (r.next() % 3 == 0 ? 0.0f : 1.0f) / static_cast<float> (h);
            cc[static_cast<size_t> (f)][static_cast<size_t> (h)] = a * (r.uni() * 2.0f - 1.0f);
            ss[static_cast<size_t> (f)][static_cast<size_t> (h)] = a * (r.uni() * 2.0f - 1.0f);
        }
        cc[static_cast<size_t> (f)][0] = r.uni() * 0.2f - 0.1f;
    }
    Wavetable wt;
    wt.build (cc, ss, false);
    return wt;
}
} // namespace

// The morph build skips the pre-twiddle and the clearing of the bins outside 0 .. K (half size transform): the cycle is the very bits of the reference build, also when the scratch holds
// stale values from earlier builds (here: poisoned with random numbers and NaNs).
TEST (own_morph_build_skipping_zero_bins_equals_the_reference_build)
{
    constexpr int kCyc = Wavetable::kCycle;
    static MorphScratch fresh, ref;
    TestRng r (77);
    std::vector<float> a (static_cast<size_t> (kCyc) + 4), b (static_cast<size_t> (kCyc) + 4);
    const int kSpecial[10] = { 0, 1, 2, 20, 337, 511, 512, 513, 1022, 1023 };
    int builds = 0, differ = 0;
    double usNew = 0.0, usRef = 0.0;
    for (int t = 0; t < 6; ++t)
    {
        const Wavetable wt = randomTable (r, r.range (1, 5));
        for (int i = 0; i < 400; ++i)
        {
            const int type = r.range (0, 11);
            const int K = r.next() % 5 == 0 ? kSpecial[r.next() % 10] : r.range (0, 1023);
            const float m = r.next() % 6 == 0 ? (r.next() % 2 ? 0.0f : 1.0f) : r.uni();
            const float framePos = r.uni() * static_cast<float> (std::max (wt.numFrames() - 1, 0));
            const float inc = 0.0005f + r.uni() * 0.4f;
            const bool fast = (r.next() & 1) != 0, scalar = (r.next() & 1) != 0, interp = (r.next() & 1) != 0;
            for (auto& x : fresh.re) x = r.next() % 7 == 0 ? std::numeric_limits<float>::quiet_NaN() : r.uni() * 1.0e6f - 5.0e5f;
            for (auto& x : fresh.im) x = r.uni() * 1.0e6f - 5.0e5f;
            for (auto& x : a) x = 1.0f;
            for (auto& x : b) x = 2.0f;
            const auto t0 = std::chrono::steady_clock::now();
            buildMorphCycle (wt, framePos, interp, type, m, K, inc, 44100.0, a.data() + 1, fresh, fast, scalar, false);
            const auto t1 = std::chrono::steady_clock::now();
            buildMorphCycle (wt, framePos, interp, type, m, K, inc, 44100.0, b.data() + 1, ref, fast, scalar, true);
            const auto t2 = std::chrono::steady_clock::now();
            usNew += std::chrono::duration<double, std::micro> (t1 - t0).count();
            usRef += std::chrono::duration<double, std::micro> (t2 - t1).count();
            ++builds;
            differ += std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) != 0 ? 1 : 0;
        }
    }
    std::printf ("morph build timing over %d random builds: new %.2f us, reference %.2f us per build\n", builds, usNew / builds, usRef / builds);
    CHECK_EQ (builds, 2400);
    CHECK_EQ (differ, 0);
    // timing of the build of the half size transform at the band limits of the bench chord (keys 36 .. 85 on the Key 61), Harmonic Stretch, the same cycle again and again (warm)
    const Wavetable wt = randomTable (r, 3);
    for (int mode = 0; mode < 2; ++mode)
    {
        double us = 0.0;
        for (int K : { 337, 225, 150, 100, 67, 45, 30, 20 })
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 300; ++i)
                buildMorphCycle (wt, 0.5f, true, kMorphHarmonicStretch, 0.3f + 0.001f * static_cast<float> (i), K, 0.01f, 44100.0, a.data() + 1, mode == 0 ? fresh : ref, true, false, mode == 1);
            us += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 300.0;
        }
        std::printf ("morph build timing, bench chord band limits, mean of 8 notes: %s %.2f us per build\n", mode == 0 ? "new" : "reference", us / 8.0);
    }
}
