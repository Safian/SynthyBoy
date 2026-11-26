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
    static bool held (const WtOsc& o, int g) { return o.morphHeld[g]; }
    /** sum over the voices of the larger gain of the period, and the largest sample of the cycle buffers (both of each group): the scale of the error bound of the single cycle read. */
    static void scale (WtOsc& o, float& gainSum, float& cyclePeak)
    {
        gainSum = 0.0f;
        for (int v = 0; v < std::max (o.curCount, o.prevCount); ++v)
            gainSum += std::max (std::max (std::fabs (o.curGainL[v]), std::fabs (o.prevGainL[v])), std::max (std::fabs (o.curGainR[v]), std::fabs (o.prevGainR[v])));
        cyclePeak = 0.0f;
        for (int g = 0; g < o.morphGroups; ++g)
            for (const float* c : { o.morphOld (g), o.morphNew (g) })
                for (int i = -1; i < WtOsc::kMorphStride - 1; ++i)
                    cyclePeak = std::max (cyclePeak, std::fabs (c[i]));
    }
    static int groups (const WtOsc& o) { return o.morphGroups; }
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

std::vector<std::unique_ptr<Wavetable>> makeTables (TestRng& r)
{
    std::vector<std::unique_ptr<Wavetable>> v;
    for (int t = 0; t < 3; ++t)
    {
        const int frames = t == 0 ? 1 : r.range (2, 6);
        std::vector<std::vector<float>> cc (static_cast<size_t> (frames), std::vector<float> (Wavetable::kCycle / 2 + 1, 0.0f)), ss = cc;
        for (int f = 0; f < frames; ++f)
        {
            const int top = r.range (3, 1023);
            for (int h = 1; h <= top; ++h)
            {
                const float a = (r.chance (30) ? 0.0f : 1.0f) / static_cast<float> (h);
                cc[static_cast<size_t> (f)][static_cast<size_t> (h)] = a * r.bi();
                ss[static_cast<size_t> (f)][static_cast<size_t> (h)] = a * r.bi();
            }
        }
        v.push_back (std::make_unique<Wavetable>());
        v.back()->build (cc, ss, true);
    }
    return v;
}
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

TEST (the_cross_fade_of_two_equal_values_is_that_value_for_the_formula_of_the_engine)
{
    // lerp (a, b, t) = a + (b - a) * t: for a == b the difference is +0 and a + 0 * t = a, so the cross-fade of two equal cycles is that cycle (only the sign of a zero can change: -0 + 0 = +0).
    // The other form, (1 - w) * a + w * a, is not used by WtOsc and rounds: counted below to document why the formula matters.
    TestRng r (5);
    long long lerpBad = 0, lerpSignOnly = 0, otherBad = 0;
    const long long total = 20000000;
    for (long long i = 0; i < total; ++i)
    {
        volatile float a = r.chance (5) ? 0.0f : (r.chance (5) ? -0.0f : r.bi() * (r.chance (10) ? 1.0e-30f : 8.0f));
        volatile float w = r.chance (10) ? 1.0f : (r.chance (10) ? 0.0f : r.uni());
        const float av = a, wv = w;
        const float y = lerp (av, av, wv);
        if (!sameBits (y, av))
        {
            if (y == av)
                ++lerpSignOnly;
            else
                ++lerpBad;
        }
        volatile float o = (1.0f - wv) * av + wv * av;
        if (static_cast<float> (o) != av)
            ++otherBad;
    }
    std::printf ("    %lld values: lerp (a, a, w) != a in %lld (sign of a zero only: %lld); the form (1 - w) a + w a differs from a in %lld\n", total, lerpBad, lerpSignOnly, otherBad);
    CHECK_EQ (lerpBad, 0);
    CHECK (otherBad > 0); // the form that is not used: it would not be exact (a -0 input giving +0 is the only difference of the form that is used, counted above and output neutral: see the held morph test)
}

namespace
{
struct HeldRun
{
    long long chunks = 0, mismatches = 0, heldPeriods = 0, loud = 0;
};

/** One oscillator pair (reference, optimised) on a mostly static patch: `holdPeriods` periods of the same tick, then a change of the morph amount, a held stretch again, a pitch change and a new note. */
HeldRun runHeld (const std::vector<std::unique_ptr<Wavetable>>& tables, int morphType, bool low, int voices, bool share, float spread, int warp, int tableIdx, double rate, int seed)
{
    HeldRun out;
    TestRng r (static_cast<uint64_t> (seed));
    WtOsc ref, opt;
    ref.setReference (true);
    opt.setLowMorphSingleCycle (false); // the bit identical savings only (the single cycle read of tier low has its own bound test)
    ref.prepareMorph();
    opt.prepareMorph();
    MorphCache cacheRef, cacheOpt;
    cacheRef.prepare();
    cacheOpt.prepare();
    Rng rngRef, rngOpt;
    OscTick t;
    t.active = true;
    t.wt = tables[static_cast<size_t> (tableIdx)].get();
    t.semis = 52.0f + static_cast<float> (seed % 24);
    t.frame = 0.4f;
    t.level = 0.7f;
    t.voices = voices;
    t.detuneSemis = 0.3f;
    t.spread = 0.6f;
    t.morphType = morphType;
    t.morphAmount = 0.35f;
    t.morphSpread = spread;
    t.lowMorph = low;
    t.shareMorph = share;
    t.lowWarp = low;
    t.fastMath = low;
    t.warp[0] = warp;
    t.warpAmount[0] = 0.4f;
    ref.start (0.0f, 0.5f, rngRef);
    opt.start (0.0f, 0.5f, rngOpt);
    bool first = true;
    for (int per = 0; per < 400; ++per)
    {
        if (per == 140)
            t.morphAmount = 0.6f;     // the morph moves: a real build, then held again
        if (per == 260)
            t.semis += 7.0f;           // the band limit (and Vocode's increment) changes
        if (per == 330)
        {
            ref.start (0.0f, 0.5f, rngRef); // a new note on the same oscillator
            opt.start (0.0f, 0.5f, rngOpt);
            first = true;
        }
        ref.update (t, rate, rngRef);
        ref.updateMorph (rate, first, &cacheRef);
        opt.update (t, rate, rngOpt);
        opt.updateMorph (rate, first, &cacheOpt);
        first = false;
        for (int g = 0; g < WtOscSavingsTest::groups (opt); ++g)
            out.heldPeriods += WtOscSavingsTest::held (opt, g) ? 1 : 0;
        float cross[6][kControlPeriod];
        for (auto& c : cross)
            for (float& x : c)
                x = r.bi();
        const float* cr[6] = { cross[0], cross[1], cross[2], cross[3], cross[4], cross[5] };
        float Lr[kControlPeriod], Rr[kControlPeriod], Lo[kControlPeriod], Ro[kControlPeriod];
        ref.render (0, kControlPeriod, Lr, Rr, cr, rate);
        opt.render (0, kControlPeriod, Lo, Ro, cr, rate);
        ++out.chunks;
        if (!sameBitsArr (Lr, Lo, kControlPeriod) || !sameBitsArr (Rr, Ro, kControlPeriod))
            ++out.mismatches;
        for (int i = 0; i < kControlPeriod; ++i)
            if (Lr[i] != 0.0f)
            {
                ++out.loud;
                break;
            }
    }
    return out;
}
} // namespace

TEST (a_held_morph_is_not_rebuilt_and_gives_the_bits_of_the_rebuilding_reference)
{
    TestRng r (21);
    const auto tables = makeTables (r);
    long long chunks = 0, bad = 0, held = 0, loud = 0;
    int configs = 0;
    const int warps[] = { 0, kWarpSync, kWarpFmA, kWarpDistTube, kWarpQuantize };
    for (int type = 1; type <= 11; ++type)
        for (int low = 0; low < 2; ++low)
            for (int vc = 0; vc < 3; ++vc)
            {
                const int voices = vc == 0 ? 1 : (vc == 1 ? 5 : 16);
                const float spread = vc == 2 ? 0.5f : 0.0f;
                const int warp = warps[(type + low + vc) % 5];
                const HeldRun h = runHeld (tables, type, low != 0, voices, low != 0 && vc == 1, spread, warp, type % 3, low != 0 ? 44100.0 : 48000.0, type * 7 + vc + low * 3);
                chunks += h.chunks;
                bad += h.mismatches;
                held += h.heldPeriods;
                loud += h.loud;
                ++configs;
            }
    std::printf ("    %d configurations, %lld periods (%lld loud), %lld held group periods (no rebuild), %lld periods with output bits that differ from the rebuilding reference\n", configs, chunks, loud, held, bad);
    CHECK (held > 5000);
    CHECK (loud > chunks / 2);
    CHECK_EQ (bad, 0);
}

TEST (the_morph_cache_key_carries_the_phase_increment_only_for_vocode)
{
    // buildMorphCycle () reads phaseInc only in Vocode (morph.cpp), so the other types store their cycle with phaseInc 0 and a note of another pitch with the same band limit hits it.
    TestRng r (31);
    const auto tables = makeTables (r);
    int hitsOther = 0, hitsVocode = 0, builtOther = 0;
    for (int type = 1; type <= 11; ++type)
    {
        WtOsc osc;
        osc.prepareMorph();
        MorphCache cache;
        cache.prepare();
        Rng rng;
        OscTick t;
        t.active = true;
        t.wt = tables[1].get();
        t.semis = 60.0f;
        t.frame = 0.3f;
        t.level = 0.7f;
        t.morphType = type;
        t.morphAmount = 0.45f;
        osc.start (0.0f, 0.0f, rng);
        osc.update (t, 44100.0, rng);
        osc.updateMorph (44100.0, true, &cache);
        std::vector<float> buf (MorphCache::kStride);
        int hits = 0;
        for (int K = 1; K < Wavetable::kHarmonics; ++K)
        {
            MorphCache::Key key;
            key.wt = t.wt;
            key.framePos = t.frame * static_cast<float> (t.wt->numFrames() - 1);
            key.m = t.morphAmount;
            key.phaseInc = 0.0f;
            key.type = type;
            key.maxHarmonic = K;
            key.hostRate = 44100.0;
            key.fast = false;
            hits += cache.fetch (key, buf.data()) ? 1 : 0;
        }
        if (type == kMorphVocode)
            hitsVocode += hits;
        else
        {
            hitsOther += hits;
            builtOther += hits == 1 ? 1 : 0;
        }
    }
    std::printf ("    phaseInc 0 finds the stored cycle for %d of 10 non Vocode types (%d hits), for Vocode %d hits\n", builtOther, hitsOther, hitsVocode);
    CHECK_EQ (builtOther, 10);
    CHECK_EQ (hitsVocode, 0);
}

TEST (tier_low_reads_one_morph_cycle_where_the_weight_is_one_within_3_ulp_of_the_samples)
{
    // Not bit identical: yA + (yB - yA) * 1 = yB up to the rounding of the difference and of the sum, |error| <= 2^-24 (|yB - yA| + |yB|) <= 3 * 2^-24 max (|yA|, |yB|) per voice sample. The
    // Hermite read of a cycle can overshoot its largest sample, so the bound is taken with a factor 1.5 on the largest cycle sample, and summed over the voices at their gains.
    TestRng r (41);
    const auto tables = makeTables (r);
    const float unit = 1.0f / 16777216.0f; // 2^-24
    double worst = 0.0;
    long long chunks = 0, differing = 0, over = 0;
    for (int cfg = 0; cfg < 48; ++cfg)
    {
        WtOsc on, off;
        on.prepareMorph();
        off.prepareMorph();
        off.setLowMorphSingleCycle (false);
        MorphCache cOn, cOff;
        cOn.prepare();
        cOff.prepare();
        Rng rOn, rOff;
        OscTick t;
        t.active = true;
        t.wt = tables[static_cast<size_t> (1 + cfg % 2)].get();
        t.semis = 40.0f + static_cast<float> ((cfg * 7) % 50);
        t.frame = 0.2f + 0.01f * static_cast<float> (cfg % 10);
        t.level = 0.8f;
        t.voices = cfg % 3 == 0 ? 1 : (cfg % 3 == 1 ? 4 : 8);
        t.detuneSemis = 0.2f;
        t.spread = 0.5f;
        t.morphType = 1 + cfg % 11;
        t.lowMorph = true;
        t.lowWarp = true;
        t.fastMath = true;
        t.shareMorph = cfg % 4 < 2;
        t.morphSpread = cfg % 4 == 3 ? 0.4f : 0.0f;
        if (cfg % 6 == 5)
        {
            t.warp[0] = kWarpFmA; // a warped path (cB == nullptr then also reaches the oversampled readers)
            t.warpAmount[0] = 0.3f;
        }
        else if (cfg % 6 == 4)
        {
            t.warp[0] = kWarpSync;
            t.warpAmount[0] = 0.3f;
        }
        on.start (0.0f, 0.5f, rOn);
        off.start (0.0f, 0.5f, rOff);
        bool first = true;
        for (int per = 0; per < 200; ++per)
        {
            t.morphAmount = 0.1f + 0.004f * static_cast<float> (per % 200); // moves every period: a real build every 16 periods, no held stretch
            on.update (t, 44100.0, rOn);
            on.updateMorph (44100.0, first, &cOn);
            off.update (t, 44100.0, rOff);
            off.updateMorph (44100.0, first, &cOff);
            first = false;
            float cross[6][kControlPeriod];
            for (auto& c : cross)
                for (float& x : c)
                    x = r.bi();
            const float* cr[6] = { cross[0], cross[1], cross[2], cross[3], cross[4], cross[5] };
            float gainSum, peak;
            WtOscSavingsTest::scale (on, gainSum, peak);
            const double bound = 3.0 * unit * 1.5 * peak * gainSum;
            for (int t0 = 0; t0 < kControlPeriod; t0 += 8)
            {
                float Lo[8], Ro[8], Lf[8], Rf[8];
                on.render (t0, 8, Lo, Ro, cr, 44100.0);
                off.render (t0, 8, Lf, Rf, cr, 44100.0);
                ++chunks;
                bool diff = false;
                for (int i = 0; i < 8; ++i)
                {
                    const double dl = std::fabs (static_cast<double> (Lo[i]) - Lf[i]), dr = std::fabs (static_cast<double> (Ro[i]) - Rf[i]);
                    diff = diff || dl > 0.0 || dr > 0.0;
                    if (bound > 0.0)
                        worst = std::max (worst, std::max (dl, dr) / bound);
                    if (std::max (dl, dr) > bound)
                        ++over;
                }
                differing += diff ? 1 : 0;
            }
        }
    }
    std::printf ("    %lld chunks of 8 samples, %lld differ from the two cycle read; the largest error is %.3f of the bound 3 * 2^-24 * 1.5 * (cycle peak) * (sum of the voice gains); %lld samples over it\n", chunks, differing, worst,
                 over);
    CHECK (differing > chunks / 10); // the single cycle read does happen
    CHECK_EQ (over, 0);
}
