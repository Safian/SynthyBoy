// SynthyBoy - own engine tests: the oscillator savings of docs/release/CPU-REVIEW-OSC.md (the second round, 2026-11-26). Each bit identical saving is compared against the path it
// replaced, on random input, bit for bit: the steady plain table read (renderPlain against its general form and the reference code), the held morph (no rebuild, one cycle read)
// and the other items below. The one saving that is not bit identical (a single cycle read where the cross-fade weight is 1) is tested against a stated bound. Compiled with the NEON
// forms on (also on Apple's arm64) and without fused multiply-add, with the morph and wavetable sources (core/tests/CMakeLists.txt); it links nothing else of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "osc/wt_osc.h"
#include "test_runner.h"

namespace synthyboy
{
namespace own
{
struct WtOscSavingsTest
{
    static void plain (bool steadyAllowed, float* L, float* R, int t0, int n, uint32_t& ph, const float* cA, const float* cB, float wB0, float wB1, float fscale, int N, float pg, float cgl,
                       float pgr, float cgr, float pinc, float cinc, float invP, float invN)
    {
        WtOsc::renderPlain (L, R, t0, n, ph, cA, cB, wB0, wB1, fscale, N, pg, cgl, pgr, cgr, pinc, cinc, invP, invN, steadyAllowed);
    }
    static void plainReference (float* L, float* R, int t0, int n, uint32_t& ph, const float* cA, const float* cB, float wB0, float wB1, float fscale, int N, float pg, float cgl,
                                float pgr, float cgr, float pinc, float cinc, float invP)
    {
        WtOsc::renderPlainReference (L, R, t0, n, ph, cA, cB, wB0, wB1, fscale, N, pg, cgl, pgr, cgr, pinc, cinc, invP);
    }
};
} // namespace own
} // namespace synthyboy

using namespace synthyboy::own;

namespace
{
struct TestRng
{
    uint64_t s;
    explicit TestRng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t> (s >> 16); }
    float uni() { return static_cast<float> (next() & 0xFFFFFF) / 16777216.0f; }
    float bi() { return uni() * 2.0f - 1.0f; }
    int range (int lo, int hi) { return lo + static_cast<int> (next() % static_cast<uint32_t> (hi - lo + 1)); }
    bool chance (int percent) { return static_cast<int> (next() % 100) < percent; }
};

bool sameBits (float a, float b) { return std::memcmp (&a, &b, 4) == 0; }
bool sameBitsArr (const float* a, const float* b, int n) { return std::memcmp (a, b, static_cast<size_t> (n) * 4) == 0; }
} // namespace

TEST (the_steady_plain_table_read_equals_the_general_form_and_the_reference)
{
    TestRng r (11);
    long long calls = 0, steadyCalls = 0, bad = 0, badRef = 0, badPhase = 0;
    for (int round = 0; round < 40000; ++round)
    {
        const int N = r.chance (50) ? 2048 : (1 << r.range (3, 11));
        std::vector<float> a (static_cast<size_t> (N) + 8), b (static_cast<size_t> (N) + 8);
        for (auto& x : a) x = r.bi();
        for (auto& x : b) x = r.bi();
        const float* cA = a.data() + 1;
        const float* cB = r.chance (50) ? b.data() + 1 : nullptr;
        const int n = r.chance (50) ? 32 : r.range (1, 32);
        const int t0 = r.chance (50) ? 0 : r.range (0, 128);
        const float invP = 1.0f / 128.0f;
        const bool pow2 = (n & (n - 1)) == 0;
        const float invN = pow2 ? 1.0f / static_cast<float> (n) : 0.0f;
        // the value of the increment and the gains: equal (steady) in most rounds, with the special values, or different
        auto pick = [&] (float scale) -> float
        {
            switch (r.range (0, 9))
            {
                case 0: return 0.0f;
                case 1: return -0.0f;
                case 2: return std::numeric_limits<float>::denorm_min() * static_cast<float> (r.range (1, 1000));
                case 3: return -scale * r.uni();
                default: return scale * r.uni();
            }
        };
        const bool allEqual = r.chance (60);
        float pinc = pick (0.3f), cinc = allEqual || r.chance (70) ? pinc : pick (0.3f);
        float pg = pick (1.0f), cgl = allEqual || r.chance (70) ? pg : pick (1.0f), pgr = pick (1.0f), cgr = allEqual || r.chance (70) ? pgr : pick (1.0f);
        if (r.chance (2)) pinc = cinc = std::numeric_limits<float>::infinity();
        if (r.chance (2)) pg = cgl = std::numeric_limits<float>::quiet_NaN();
        if (r.chance (1)) pinc = std::numeric_limits<float>::quiet_NaN(), cinc = pinc;
        const float wB0 = r.uni(), wB1 = r.chance (30) ? wB0 : r.uni();
        const float fscale = static_cast<float> (N);
        const uint32_t ph0 = r.next() << 16 ^ r.next();
        float L1[40], R1[40], L2[40], R2[40], L3[40], R3[40];
        for (int i = 0; i < 40; ++i)
            L1[i] = L2[i] = L3[i] = r.bi(), R1[i] = R2[i] = R3[i] = r.bi();
        uint32_t ph1 = ph0, ph2 = ph0, ph3 = ph0;
        WtOscSavingsTest::plain (true, L1, R1, t0, n, ph1, cA, cB, wB0, wB1, fscale, N, pg, cgl, pgr, cgr, pinc, cinc, invP, invN);
        WtOscSavingsTest::plain (false, L2, R2, t0, n, ph2, cA, cB, wB0, wB1, fscale, N, pg, cgl, pgr, cgr, pinc, cinc, invP, invN);
        WtOscSavingsTest::plainReference (L3, R3, t0, n, ph3, cA, cB, wB0, wB1, fscale, N, pg, cgl, pgr, cgr, pinc, cinc, invP);
        ++calls;
        steadyCalls += sameBits (pinc, cinc) && sameBits (pg, cgl) && sameBits (pgr, cgr) && std::isfinite (pinc) && std::isfinite (pg) && std::isfinite (pgr) ? 1 : 0;
        if (!sameBitsArr (L1, L2, 40) || !sameBitsArr (R1, R2, 40))
            ++bad;
        if (ph1 != ph2)
            ++badPhase;
        if (!sameBitsArr (L1, L3, 40) || !sameBitsArr (R1, R3, 40) || ph1 != ph3)
            ++badRef;
    }
    std::printf ("    %lld random chunks (%lld take the steady form): output differs from the general form in %lld, phase in %lld, from the reference in %lld\n", calls, steadyCalls, bad, badPhase,
                 badRef);
    CHECK (steadyCalls > 20000);
    CHECK_EQ (bad, 0);
    CHECK_EQ (badPhase, 0);
    CHECK_EQ (badRef, 0);
}
