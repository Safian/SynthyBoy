// SynthyBoy - own engine tests: the optimised wavetable oscillator (WtOsc, docs/release/CPU-REVIEW-OSC.md) gives the very bits of the code it replaced. Two oscillators run in step on
// random patches: one with setReference (true) (the old code paths), one as shipped; after every call their outputs and their whole state are compared bit by bit. Compiled with the
// NEON forms on (also on Apple's arm64) and without fused multiply-add, with the morph and wavetable sources (core/tests/CMakeLists.txt); it links nothing else of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "dsp/noise_coeff.h"
#include "osc/wt_osc.h"
#include "test_runner.h"

namespace synthyboy
{
namespace own
{
struct WtOscOptTest
{
    static constexpr int kMorphStride = WtOsc::kMorphStride;
    /** True if every piece of state that the audio depends on is bit for bit the same (the caches of the optimisations are not state). */
    /** 0 if every piece of state that the audio depends on is bit for bit the same (the caches of the optimisations are not state), else the number of the first field that differs. */
    static int stateDiff (const WtOsc& a, const WtOsc& b)
    {
        auto eq = [] (const void* x, const void* y, size_t n) { return std::memcmp (x, y, n) == 0; };
        if (!eq (a.phase, b.phase, sizeof a.phase)) return 1;
        if (!eq (a.prevInc, b.prevInc, sizeof a.prevInc)) return 2;
        if (!eq (a.curInc, b.curInc, sizeof a.curInc)) return 3;
        // The gains are compared as values: the old shortcut of update () treats +0 and -0 of the tick as equal, so after a run of silent ticks (level +0 / -0) a gain can be -0 where
        // the optimised oscillator, which applies the last silent tick instead of skipping by value, has +0. That cannot reach the output: pg + (cg - pg) * t is +0 for every
        // sign of zero of pg and cg and equals cg * t for a non zero cg (and the render tests compare the output bit for bit).
        for (int i = 0; i < 16; ++i)
            if (a.prevGainL[i] != b.prevGainL[i] || a.prevGainR[i] != b.prevGainR[i] || a.curGainL[i] != b.curGainL[i] || a.curGainR[i] != b.curGainR[i])
                return 4;
        // (the frame of a lane beyond both counts is never read)
        for (int i = 0; i < std::max (a.curCount, a.prevCount); ++i)
            if (std::memcmp (&a.curFrameOf[i], &b.curFrameOf[i], 4) != 0)
                return 6;
        if (!eq (a.frameIdx, b.frameIdx, sizeof a.frameIdx)) return 7;
        if (!eq (a.filterState, b.filterState, sizeof a.filterState)) return 8;
        if (!eq (a.lastCross, b.lastCross, sizeof a.lastCross)) return 9;
        if (a.curCount != b.curCount || a.prevCount != b.prevCount) return 10;
        if (a.havePrev != b.havePrev) return 11;
        if (a.morphAge != b.morphAge) return 12;
        if (a.morphGroups != b.morphGroups) return 13;
        if (a.morphActive != b.morphActive) return 14;
        if (a.prevFrame != b.prevFrame || a.curFrame != b.curFrame) return 15;
        if (a.historyDirty != b.historyDirty) return 16;
        if (a.history.size() != b.history.size() || (!a.history.empty() && !eq (a.history.data(), b.history.data(), a.history.size() * sizeof (float)))) return 17;
        // the two cycles of each group by role (the optimised oscillator swaps the two buffers instead of copying)
        if (a.morphBuf.size() != b.morphBuf.size()) return 18;
        if (!a.morphBuf.empty())
            for (int g = 0; g < 2; ++g)
            {
                WtOsc& x = const_cast<WtOsc&> (a);
                WtOsc& y = const_cast<WtOsc&> (b);
                // a held morph (CPU-REVIEW-OSC.md, second round): the optimised oscillator leaves its old buffer stale and reads the new one alone, while the reference holds the same
                // cycle in both: the new cycle of the held one must equal both cycles of the other
                const bool xh = x.morphHeld[g], yh = y.morphHeld[g];
                if (xh || yh)
                {
                    WtOsc& h = xh ? x : y;
                    WtOsc& o = xh ? y : x;
                    if (!eq (h.morphNew (g) - 1, o.morphNew (g) - 1, kMorphStride * sizeof (float)) || (!(xh && yh) && !eq (h.morphNew (g) - 1, o.morphOld (g) - 1, kMorphStride * sizeof (float)))) return 19;
                }
                else if (!eq (x.morphOld (g) - 1, y.morphOld (g) - 1, kMorphStride * sizeof (float)) || !eq (x.morphNew (g) - 1, y.morphNew (g) - 1, kMorphStride * sizeof (float)))
                    return 19;
            }
        return 0;
    }
    static bool sameState (const WtOsc& a, const WtOsc& b)
    {
        const int d = stateDiff (a, b);
        if (d != 0)
            std::printf ("state differs in field %d\n", d);
        return d == 0;
    }
    static const OscTick& tickOf (const WtOsc& o) { return o.tick; }
    static bool pending (const WtOsc& o) { return o.pending; }
    static int morphAge (const WtOsc& o) { return o.morphAge; }
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

std::vector<std::unique_ptr<Wavetable>> makeTables (TestRng& r)
{
    std::vector<std::unique_ptr<Wavetable>> v;
    for (int t = 0; t < 4; ++t)
    {
        const int frames = t == 0 ? 1 : r.range(2, 6);
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

const int kWarps[] = { kWarpSync, kWarpFormant, kWarpQuantize, kWarpBend, kWarpSqueeze, kWarpPulse, kWarpFmA, kWarpFmB, kWarpRmA, kWarpRmB, kWarpSyncVar, kWarpBendPlus, kWarpBendMinus,
                       kWarpBendBoth, kWarpAsymPlus, kWarpAsymMinus, kWarpAsymBoth, kWarpFlip, kWarpMirror, kWarpRemap1, kWarpRemap2, kWarpRemap3, kWarpRemap4, kWarpOddEven,
                       kWarpFilterLpf, kWarpFilterHpf, kWarpDistTube, kWarpDistSoftClip, kWarpDistHardClip, kWarpDistLinearFold, kWarpDistSineFold, kWarpFmTzA, kWarpFmExpB, kWarpFmLinA,
                       kWarpPdA, kWarpPdSelf, kWarpAmA, kWarpAmB, 52, 55, 60, 63, 70, 75 };

/** The patch generator: a full random OscTick, and mutations of a few fields (so that the shortcuts and the caches of the oscillator see both hits and misses). */
struct Patcher
{
    TestRng& r;
    const std::vector<std::unique_ptr<Wavetable>>& tables;
    std::vector<float> remap;
    int warpPercent = 25, morphPercent = 25;
    Patcher (TestRng& rr, const std::vector<std::unique_ptr<Wavetable>>& t) : r (rr), tables (t)
    {
        remap.resize (257);
        for (int i = 0; i <= 256; ++i)
            remap[static_cast<size_t> (i)] = std::min (1.0f, std::max (0.0f, static_cast<float> (i) / 256.0f + 0.1f * r.bi()));
    }
    int pickWarp() { return r.chance (100 - warpPercent) ? 0 : kWarps[r.next() % (sizeof kWarps / sizeof kWarps[0])]; }
    void field (OscTick& t, int which)
    {
        switch (which)
        {
            case 0: t.wt = r.chance (3) ? nullptr : tables[r.next() % tables.size()].get(); break;
            case 1: t.semis = r.chance (50) ? static_cast<float> (r.range (20, 110)) : 20.0f + 90.0f * r.uni(); break;
            case 2: t.frame = r.chance (10) ? (r.chance (50) ? 0.0f : 1.0f) : r.uni(); break;
            case 3: t.frameSpread = r.chance (40) ? 0.0f : r.bi(); break;
            case 4: t.level = r.chance (8) ? 0.0f : r.uni(); break;
            case 5: t.pan = r.chance (40) ? 0.0f : r.bi(); break;
            case 6: t.panRight = r.chance (40) ? 0.0f : r.bi(); break;
            case 7: t.voices = r.chance (30) ? 1 : r.range (1, 16); break;
            case 8: t.detuneSemis = r.chance (20) ? 0.0f : r.uni() * 2.0f; break;
            case 9: t.curve = r.chance (40) ? 0.0f : r.bi() * 8.0f; break;
            case 10: t.mode = r.range (0, 4); break;
            case 11: t.stack = r.range (0, 10); t.stackRepeat = r.range (0, 4); break;
            case 12: t.blendShape = r.bi(); break;
            case 13: t.spread = r.chance (20) ? (r.chance (50) ? 0.0f : 1.0f) : r.uni(); break;
            case 14: t.blend = r.uni(); break;
            case 15: t.law = r.chance (30) ? 1 : 0; break;
            case 16: t.warp[0] = pickWarp(); t.warpAmount[0] = r.uni(); t.warpPhase[0] = r.bi() * 0.5f; break;
            case 17: t.warp[1] = r.chance (70) ? 0 : pickWarp(); t.warpAmount[1] = r.uni(); t.warpPhase[1] = r.bi() * 0.5f; break;
            case 18: t.warpSpread[0] = r.bi(); break;
            case 19: t.startPhase = r.uni(); break;
            case 20: t.remap = r.chance (50) ? remap.data() : nullptr; break;
            case 21: t.morphType = r.chance (100 - morphPercent) ? 0 : r.range (1, 11); break;
            case 22: t.morphAmount = r.uni(); break;
            case 23: t.morphSpread = r.chance (50) ? 0.0f : r.bi(); break;
            case 24: t.jump = r.chance (10); break;
            case 25: t.oversample = r.chance (10); break;
            case 26: t.smooth = r.chance (50); break;
            case 27: t.shareMorph = r.chance (50); break;
            case 28: t.lowWarp = r.chance (60); break;
            case 29: t.lowMorph = r.chance (60); break;
            case 30: t.fastMath = r.chance (60); break;
            case 31: t.scalarWarp = r.chance (10); break;
            default: break;
        }
    }
    static constexpr int kFields = 32;
    OscTick fresh()
    {
        OscTick t;
        for (int i = 0; i < kFields; ++i)
            field (t, i);
        t.active = true;
        if (t.wt == nullptr)
            t.wt = tables[0].get();
        return t;
    }
    void mutate (OscTick& t)
    {
        switch (r.range (0, 6))
        {
            case 0: break; // unchanged: the shortcut of update ()
            case 1: field (t, 2); field (t, 4); field (t, 5); field (t, 22); break; // frame, level, pan, morph amount move
            case 2: field (t, 1); break;
            case 3: field (t, r.range (0, 31)); break;
            case 4: for (int i = 0; i < kFields; ++i) if (r.chance (12)) field (t, i); break;
            case 5: field (t, 4); field (t, 6); break;
            default: // the sign of a zero: equal as values, different as bytes
                for (float* f : { &t.pan, &t.panRight, &t.level, &t.frameSpread, &t.detuneSemis, &t.curve, &t.blendShape, &t.spread, &t.frame })
                    if (*f == 0.0f || r.chance (10))
                        *f = r.chance (50) ? 0.0f : -0.0f;
                break;
        }
        if (t.wt == nullptr && r.chance (70))
            t.wt = tables[0].get();
    }
};

struct Run
{
    WtOsc ref, opt;
    MorphCache cacheRef, cacheOpt;
    Rng rngRef, rngOpt;
    float cross[6][32];
    int periods = 0, mismatches = 0, stateMismatches = 0, plain = 0, warped = 0, morphing = 0, nonZero = 0, fmOnly = 0, filtered = 0, deferredPeriods = 0;
    Run()
    {
        ref.setReference (true);
        opt.setLowMorphSingleCycle (false); // the one saving that is not bit identical (tier low, a single cycle read where the weight is 1): its bound is tested in test_own_wt_osc_savings.cpp
        ref.prepareMorph();
        opt.prepareMorph();
        cacheRef.prepare();
        cacheOpt.prepare();
    }
};

float peak (const float* x, int n) { float m = 0.0f; for (int i = 0; i < n; ++i) m = std::max (m, std::fabs (x[i])); return m; }
} // namespace

TEST (own_wt_osc_optimised_equals_the_reference_on_random_patches)
{
    int totalPeriods = 0, totalMismatch = 0, totalState = 0, plain = 0, warped = 0, morphing = 0, loud = 0, fmOnly = 0, filtered = 0, deferredTotal = 0;
    const char* seedsEnv = std::getenv ("SB_TEST_SEEDS"); // more seeds for a long run: SB_TEST_SEEDS=600
    const int seeds = seedsEnv != nullptr ? std::max (60, std::atoi (seedsEnv)) : 60;
    for (int seed = 1; seed <= seeds; ++seed)
    {
        TestRng r (static_cast<uint64_t> (seed));
        const auto tables = makeTables (r);
        Patcher P (r, tables);
        P.warpPercent = seed % 3 == 0 ? 80 : (seed % 3 == 1 ? 0 : 25);
        P.morphPercent = seed % 4 == 0 ? 80 : (seed % 4 == 1 ? 0 : 25);
        Run run;
        const double rate = r.chance (70) ? 44100.0 : (r.chance (50) ? 48000.0 : 96000.0);
        OscTick t = P.fresh();
        const int chunkMode = seed % 5; // 0: 32 sample chunks (the engine of tier low), others: split periods
        run.ref.start (t.startPhase, 0.5f, run.rngRef);
        run.opt.start (t.startPhase, 0.5f, run.rngOpt);
        bool first = true;
        int streak = 0;
        for (int per = 0; per < 140; ++per)
        {
            if (!first)
                P.mutate (t);
            for (auto& c : run.cross)
                for (float& x : c)
                    x = r.bi();
            const float* cr[6] = { run.cross[0], run.cross[1], run.cross[2], run.cross[3], run.cross[4], run.cross[5] };
            if (r.chance (15))
                cr[r.range (0, 5)] = nullptr;
            const bool anyCross = !r.chance (10);
            ++run.periods;
            if (!first && r.chance (3)) // a new note on the same oscillator
            {
                run.ref.start (t.startPhase, 0.5f, run.rngRef);
                run.opt.start (t.startPhase, 0.5f, run.rngOpt);
                first = true;
            }
            // the period as the voice drives it: silent streaks (level 0), off oscillators (update only, no render), and the deferral of the update of a silent period
            if (streak == 0 && r.chance (4))
                streak = r.range (1, 7);
            OscTick tp = t;
            const bool off = streak == 0 ? r.chance (3) : false;
            if (streak > 0 || off)
                tp.level = 0.0f;
            if (streak > 0)
                --streak;
            bool deferred = false;
            if (off)
            {
                run.ref.update (tp, rate, run.rngRef);
                deferred = run.opt.deferSilent (tp, rate, false, false);
                if (!deferred)
                    run.opt.update (tp, rate, run.rngOpt);
            }
            else
            {
                run.ref.update (tp, rate, run.rngRef);
                run.ref.updateMorph (rate, first, &run.cacheRef);
                deferred = run.opt.deferSilent (tp, rate, true, first);
                if (!deferred)
                {
                    run.opt.update (tp, rate, run.rngOpt);
                    run.opt.updateMorph (rate, first, &run.cacheOpt);
                }
                first = false;
            }
            run.deferredPeriods += deferred ? 1 : 0;
            if (!WtOscOptTest::pending (run.opt) && !WtOscOptTest::pending (run.ref) && !WtOscOptTest::sameState (run.ref, run.opt))
                ++run.stateMismatches;
            if (off)
                continue; // an oscillator that is off is not rendered
            int t0 = 0;
            while (t0 < kControlPeriod)
            {
                int n = chunkMode == 0 ? kControlPeriod : std::min (kControlPeriod - t0, r.range (1, 32));
                if (chunkMode == 1 && n > 7)
                    n = 7;
                float Lr[32], Rr[32], Lo[32], Ro[32];
                run.ref.render (t0, n, Lr, Rr, anyCross ? cr : nullptr, rate);
                run.opt.render (t0, n, Lo, Ro, anyCross ? cr : nullptr, rate);
                if (std::memcmp (Lr, Lo, static_cast<size_t> (n) * 4) != 0 || std::memcmp (Rr, Ro, static_cast<size_t> (n) * 4) != 0)
                    ++run.mismatches;
                if (peak (Lr, n) > 0.0f || peak (Rr, n) > 0.0f)
                    ++run.nonZero;
                if (!WtOscOptTest::pending (run.opt) && !WtOscOptTest::sameState (run.ref, run.opt))
                    ++run.stateMismatches;
                t0 += n;
            }
            const OscTick& tk = WtOscOptTest::tickOf (run.opt);
            const bool w = tk.oversample || warpNeedsOversampling (tk.warp[0]) || warpNeedsOversampling (tk.warp[1]);
            (w ? run.warped : (tk.morphType != 0 ? run.morphing : run.plain))++;
            const int w0 = tk.warp[0];
            if (tk.warp[1] == 0 && (w0 == kWarpFmA || w0 == kWarpFmB || w0 == kWarpFmTzA || w0 == kWarpFmTzB || w0 == kWarpFmLinA || w0 == kWarpFmLinB) && tk.level > 0.0f && tk.wt != nullptr)
                ++run.fmOnly;
            if ((w0 == kWarpFilterLpf || w0 == kWarpFilterHpf || tk.warp[1] == kWarpFilterLpf || tk.warp[1] == kWarpFilterHpf) && tk.level > 0.0f && tk.wt != nullptr)
                ++run.filtered;
        }
        totalPeriods += run.periods;
        totalMismatch += run.mismatches;
        totalState += run.stateMismatches;
        plain += run.plain;
        warped += run.warped;
        morphing += run.morphing;
        loud += run.nonZero;
        fmOnly += run.fmOnly;
        filtered += run.filtered;
        deferredTotal += run.deferredPeriods;
    }
    std::printf ("wt_osc opt: %d periods (plain %d, warped %d, morph %d, of them FM only %d, filter warps %d), %d silent periods put off, %d loud chunks, %d output mismatches, %d state mismatches\n", totalPeriods, plain, warped, morphing,
                 fmOnly, filtered, deferredTotal, loud, totalMismatch, totalState);
    CHECK_EQ (totalPeriods, seeds * 140);
    CHECK (plain > 500 && warped > 500 && morphing > 500 && loud > 2000 && fmOnly > 150 && filtered > 100 && deferredTotal > 400);
    CHECK_EQ (totalMismatch, 0);
    CHECK_EQ (totalState, 0);
}

namespace
{
/** One preset like Lorenz Garden's three oscillators (3, 2, 1 unison lanes) plus the sub, run for `blocks` blocks of 4 periods: the time of the three calls of the oscillator, in us per block. */
struct Timing { double update = 0.0, morph = 0.0, render = 0.0; };

Timing timeConfig (bool reference, int voices, int morphType, int warp, int blocks, bool modulate, bool zeroLevel = false)
{
    TestRng r (4242);
    const auto tables = makeTables (r);
    Patcher P (r, tables);
    WtOsc osc;
    osc.setReference (reference);
    osc.prepareMorph();
    MorphCache cache;
    cache.prepare();
    Rng rng;
    OscTick t;
    t.wt = tables[1].get();
    t.semis = 57.0f;
    t.voices = voices;
    t.detuneSemis = 0.3f;
    t.curve = 1.5f;
    t.mode = 1;
    t.spread = 0.8f;
    t.blend = 0.7f;
    t.level = 0.7f;
    t.frame = 0.4f;
    t.morphType = morphType;
    t.morphAmount = 0.3f;
    t.lowWarp = t.lowMorph = t.fastMath = true;
    t.warp[0] = warp;
    t.warpAmount[0] = 0.5f;
    t.active = true;
    osc.start (0.0f, 0.5f, rng);
    float L[32], R[32], cb[6][32];
    for (auto& c : cb)
        for (float& x : c)
            x = r.bi();
    const float* cr[6] = { cb[0], cb[1], cb[2], cb[3], cb[4], cb[5] };
    Timing tm;
    using clk = std::chrono::steady_clock;
    bool first = true;
    for (int b = 0; b < blocks; ++b)
        for (int per = 0; per < 4; ++per)
        {
            if (modulate)
            {
                t.frame = r.uni();
                t.level = 0.3f + 0.7f * r.uni();
                t.pan = r.bi();
                t.panRight = t.pan;
                t.morphAmount = r.uni();
            }
            if (zeroLevel && !first)
                t.level = 0.0f;
            const auto a0 = clk::now();
            const bool deferred = osc.deferSilent (t, 44100.0, true, first);
            if (!deferred)
                osc.update (t, 44100.0, rng);
            const auto a1 = clk::now();
            if (!deferred)
                osc.updateMorph (44100.0, first, &cache);
            first = false;
            const auto a2 = clk::now();
            osc.render (0, 32, L, R, cr, 44100.0);
            const auto a3 = clk::now();
            tm.update += std::chrono::duration<double, std::micro> (a1 - a0).count();
            tm.morph += std::chrono::duration<double, std::micro> (a2 - a1).count();
            tm.render += std::chrono::duration<double, std::micro> (a3 - a2).count();
        }
    tm.update /= blocks;
    tm.morph /= blocks;
    tm.render /= blocks;
    return tm;
}
} // namespace

TEST (own_wt_osc_timing_report)
{
    struct Cfg { const char* name; int voices, morph, warp; bool modulate, zero; };
    const Cfg cfgs[] = { { "3 lanes plain, frame/level/pan modulated", 3, 0, 0, true, false }, { "3 lanes plain, held", 3, 0, 0, false, false }, { "1 lane morph (Harmonic Stretch), modulated", 1, kMorphHarmonicStretch, 0, true, false },
                         { "3 lanes morph (Shepard), modulated", 3, kMorphShepard, 0, true, false }, { "2 lanes Sync warp, modulated", 2, 0, kWarpSync, true, false }, { "1 lane FM A warp, modulated", 1, 0, kWarpFmA, true, false }, { "1 lane Filter LPF warp, modulated", 1, 0, kWarpFilterLpf, true, false }, { "3 lanes plain, level 0 (silent)", 3, 0, 0, true, true }, { "3 lanes morph (Shepard), level 0 (silent)", 3, kMorphShepard, 0, true, true } };
    for (const Cfg& c : cfgs)
    {
        // warm up, then the median of 5 runs
        Timing best[2];
        for (int mode = 0; mode < 2; ++mode)
        {
            timeConfig (mode == 0, c.voices, c.morph, c.warp, 200, c.modulate, c.zero);
            Timing runs[5];
            for (auto& x : runs)
                x = timeConfig (mode == 0, c.voices, c.morph, c.warp, 1500, c.modulate, c.zero);
            for (int i = 0; i < 5; ++i)
                for (int j = i + 1; j < 5; ++j)
                    if (runs[j].update + runs[j].morph + runs[j].render < runs[i].update + runs[i].morph + runs[i].render)
                        std::swap (runs[i], runs[j]);
            best[mode] = runs[2];
        }
        std::printf ("timing %-46s us per block (4 periods)  reference: update %.3f morph %.3f render %.3f   new: update %.3f morph %.3f render %.3f\n", c.name, best[0].update, best[0].morph, best[0].render,
                     best[1].update, best[1].morph, best[1].render);
    }
    CHECK (true);
}

// The proof behind the single reciprocal per render (F8): for a power of two n, (k + 1) / n and (k + 1) * (1 / n) are the same float for every k (both are the exact integer scaled by
// 2^-m); for other n they can differ, which is why the oscillator divides there.
TEST (own_wt_osc_reciprocal_of_a_power_of_two_is_exact)
{
    int differPow2 = 0, differOther = 0;
    for (int n = 1; n <= 4096; ++n)
    {
        const float invN = 1.0f / static_cast<float> (n);
        const bool pow2 = (n & (n - 1)) == 0;
        for (int i = 1; i <= n; ++i)
        {
            const float d = static_cast<float> (i) / static_cast<float> (n), m = static_cast<float> (i) * invN;
            if (std::memcmp (&d, &m, 4) != 0)
                (pow2 ? differPow2 : differOther)++;
        }
    }
    CHECK_EQ (differPow2, 0);
    CHECK (differOther > 0);
}

// The noise coefficients kept between periods are the values of computing them again, for held and for moving inputs (bits compared).
TEST (own_noise_coefficients_cache_equals_computing_them)
{
    TestRng r (5);
    NoiseCoeffCache cache;
    int differ = 0, calls = 0;
    float pitch = 0.0f, filter = 0.0f;
    double rate = 44100.0;
    for (int i = 0; i < 20000; ++i)
    {
        if (r.chance (20))
            pitch = r.chance (30) ? 0.0f : (r.uni() * 80.0f - 40.0f);
        if (r.chance (20))
            filter = r.chance (30) ? 0.0f : r.bi();
        if (r.chance (3))
            rate = r.chance (50) ? 48000.0 : (r.chance (50) ? 96000.0 : 44100.0);
        const NoiseCoeffs& c = cache.get (pitch, filter, rate);
        const NoiseCoeffs e = computeNoiseCoeffs (pitch, filter, rate);
        differ += std::memcmp (&c, &e, sizeof e) != 0 ? 1 : 0;
        ++calls;
    }
    using clk = std::chrono::steady_clock;
    float acc = 0.0f;
    volatile float vp = 3.0f, vf = 0.2f;
    volatile double vr = 44100.0;
    const auto t0 = clk::now();
    for (int i = 0; i < 200000; ++i)
        acc += computeNoiseCoeffs (vp + static_cast<float> (i & 1), vf, vr).fC;
    const auto t1 = clk::now();
    for (int i = 0; i < 200000; ++i)
        acc += cache.get (vp, vf, vr).fC;
    const auto t2 = clk::now();
    std::printf ("noise coefficients: computed %.1f ns, cached %.1f ns per period (acc %g)\n", std::chrono::duration<double, std::nano> (t1 - t0).count() / 200000.0,
                 std::chrono::duration<double, std::nano> (t2 - t1).count() / 200000.0, static_cast<double> (acc));
    CHECK_EQ (calls, 20000);
    CHECK_EQ (differ, 0);
}

// A directed case of the early-out of a silent oscillator: the level is 0 for the first periods of a note (pitch, frame and pan keep moving, the morph age would reach the rebuild period in
// the middle of the run) and rises later. The silent periods are put off; the output is exactly zero in them, and from the first audible period on it is the very output of the oscillator
// that applied every update.
TEST (own_wt_osc_silent_oscillator_early_out_is_bit_identical_when_the_level_rises_later)
{
    TestRng r (31);
    const auto tables = makeTables (r);
    int mismatches = 0, silentPeriods = 0, deferredPeriods = 0, audibleChunks = 0;
    for (int morphType : { 0, static_cast<int> (kMorphShepard), static_cast<int> (kMorphHarmonicStretch) })
        for (int lead : { 1, 3, 17, 40 })      // silent periods before the level rises
            for (int lowMorph = 0; lowMorph < 2; ++lowMorph)
            {
                WtOsc ref, opt;
                ref.setReference (true);
                opt.setLowMorphSingleCycle (false); // bit identical comparison of the other savings (the single cycle read has its bound test in test_own_wt_osc_savings.cpp)
                ref.prepareMorph();
                opt.prepareMorph();
                MorphCache cr, co;
                cr.prepare();
                co.prepare();
                Rng a, b;
                ref.start (0.0f, 0.5f, a);
                opt.start (0.0f, 0.5f, b);
                OscTick t;
                t.wt = tables[2].get();
                t.voices = 3;
                t.detuneSemis = 0.2f;
                t.morphType = morphType;
                t.lowMorph = lowMorph != 0;
                t.active = true;
                bool first = true;
                for (int per = 0; per < lead + 80; ++per)
                {
                    t.semis = 55.0f + 0.05f * static_cast<float> (per);
                    t.frame = std::fmod (0.013f * static_cast<float> (per), 1.0f);
                    t.pan = 0.01f * static_cast<float> (per % 50) - 0.2f;
                    t.morphAmount = std::fmod (0.031f * static_cast<float> (per), 1.0f);
                    t.level = per < lead ? 0.0f : 0.6f + 0.001f * static_cast<float> (per);
                    ref.update (t, 44100.0, a);
                    ref.updateMorph (44100.0, first, &cr);
                    const bool d = opt.deferSilent (t, 44100.0, true, first);
                    if (!d)
                    {
                        opt.update (t, 44100.0, b);
                        opt.updateMorph (44100.0, first, &co);
                    }
                    first = false;
                    deferredPeriods += d ? 1 : 0;
                    silentPeriods += per < lead ? 1 : 0;
                    float Lr[32], Rr[32], Lo[32], Ro[32];
                    ref.render (0, 32, Lr, Rr, nullptr, 44100.0);
                    opt.render (0, 32, Lo, Ro, nullptr, 44100.0);
                    mismatches += (std::memcmp (Lr, Lo, sizeof Lr) != 0 || std::memcmp (Rr, Ro, sizeof Rr) != 0) ? 1 : 0;
                    if (per < lead)
                        for (int k = 0; k < 32; ++k)
                            mismatches += (Lo[k] != 0.0f || Ro[k] != 0.0f) ? 1 : 0;
                    else
                        audibleChunks += peak (Lo, 32) > 0.0f ? 1 : 0;
                    if (!WtOscOptTest::pending (opt) && WtOscOptTest::stateDiff (ref, opt) != 0)
                        ++mismatches;
                }
            }
    std::printf ("silent early-out: %d silent periods, %d put off, %d audible chunks afterwards, %d mismatches\n", silentPeriods, deferredPeriods, audibleChunks, mismatches);
    CHECK_EQ (mismatches, 0);
    CHECK (deferredPeriods > 0 && audibleChunks > 100);
    CHECK (deferredPeriods >= silentPeriods - 3 * 2 * 4 * 2); // all but the note start (and the first periods after it) are put off
}

// ---- round 13: the stagger of the morph rebuilds (docs/release/CPU-REVIEW-OSC.md F1, WtOsc::setMorphStagger) -------------------------------------------------------------------------------
namespace
{
/** One voice's oscillator with a morph, driven period by period as Voice::tick and Voice::render do; `builds` counts the morph cycle builds (a build sets the age back). */
struct StaggerVoice
{
    WtOsc osc;
    MorphCache cache;
    Rng rng;
    bool first = true;
    StaggerVoice() { osc.prepareMorph(); cache.prepare(); }
    void start (uint32_t n)
    {
        osc.start (0.0f, 0.0f, rng);
        osc.setMorphStagger (n);
        first = true;
    }
    /** One control period; true if it built a morph cycle. */
    bool period (const OscTick& t, float* L, float* R)
    {
        osc.update (t, 44100.0, rng);
        const int before = WtOscOptTest::morphAge (osc);
        osc.updateMorph (44100.0, first, &cache);
        const bool built = first || WtOscOptTest::morphAge (osc) < before;
        first = false;
        osc.render (0, 32, L, R, nullptr, 44100.0);
        return built;
    }
};

OscTick morphTick (const Wavetable* wt, int morphType, bool low, int voices)
{
    OscTick t;
    t.wt = wt;
    t.semis = 57.0f;
    t.voices = voices;
    t.detuneSemis = 0.2f;
    t.spread = 0.5f;
    t.blend = 0.7f;
    t.level = 0.7f;
    t.frame = 0.4f;
    t.morphType = morphType;
    t.morphAmount = 0.3f;
    t.lowMorph = t.lowWarp = t.fastMath = low;
    t.active = true;
    return t;
}
} // namespace

// The slot: the 8 evenly spaced periods k P / 8 in the order 0, 2, 4, 6, 1, 3, 5, 7 of the seed mod 8; any 8 consecutive seeds take all 8 of them.
TEST (own_wt_osc_morph_stagger_slots_are_evenly_spaced)
{
    const int expect16[8] = { 0, 4, 8, 12, 2, 6, 10, 14 };
    for (uint32_t n = 0; n < 32; ++n)
    {
        CHECK_EQ (WtOsc::morphStaggerSlot (n, 16), expect16[n & 7u]);
        CHECK_EQ (WtOsc::morphStaggerSlot (n, 8), expect16[n & 7u] / 2);
    }
    for (int P : { 8, 16 })
        for (uint32_t from = 0; from < 40; ++from)
        {
            int seen = 0;
            for (uint32_t n = from; n < from + 8u; ++n)
                seen |= 1 << WtOsc::morphStaggerSlot (n, P);
            int expect = 0;
            for (int k = 0; k < 8; ++k)
                expect |= 1 << (k * P / 8);
            CHECK_EQ (seen, expect);
        }
    CHECK_EQ (WtOsc::morphStaggerSeed (10, 0), 10u);
    CHECK_EQ (WtOsc::morphStaggerSeed (10, 1), 14u);
}

// (c) The schedule. The voices of a chord start in the same control period (any of the 4 periods of a 128 sample block); serials consecutive from any first serial; 8 voices x 1 morph
// oscillator (Stretched Glass Choir) and 4 voices x 2 (Shepard Ascent: oscillators 0 and 1), tier low (a build every 16 periods) and full (every 8). Counted: the builds in each of the 8 blocks
// after the block of the start (the start block holds the builds of the note start, which are not moved). Flat: max - min <= 1 in every case (the old schedule: all in one block of 4).
// Restarts and voice stealing (a voice restarted at a random period with a new serial, 2000 periods, 8 voices): the first build is in the period of the start and no voice ever goes longer
// than one rebuild period between two builds.
TEST (own_wt_osc_morph_stagger_spreads_a_chord_flat_and_never_skips_a_rebuild)
{
    TestRng r (777);
    const auto tables = makeTables (r);
    float L[32], R[32];
    int cases = 0, flat = 0, worstSpread = 0, oldWorstSpread = 0;
    for (int low = 0; low < 2; ++low)
        for (int layout = 0; layout < 2; ++layout)
            for (int stagger = 0; stagger < 2; ++stagger)
                for (uint32_t serial0 = 0; serial0 < 16; ++serial0)
                    for (int phase = 0; phase < 4; ++phase)
                    {
                        const int notes = layout == 0 ? 8 : 4, oscs = layout == 0 ? 1 : 2;
                        std::vector<std::unique_ptr<StaggerVoice>> v;
                        for (int i = 0; i < notes * oscs; ++i)
                            v.push_back (std::make_unique<StaggerVoice>());
                        const OscTick t = morphTick (tables[2].get(), layout == 0 ? kMorphHarmonicStretch : kMorphShepard, low != 0, 1);
                        int perBlock[10] = {};
                        for (int per = 0; per < 4 * 10; ++per)
                        {
                            const int rel = per - phase;   // the chord starts in period `phase` of block 0
                            for (int i = 0; i < notes * oscs; ++i)
                            {
                                if (rel < 0)
                                    continue;
                                if (rel == 0)
                                    v[static_cast<size_t> (i)]->start (stagger != 0 ? WtOsc::morphStaggerSeed (serial0 + static_cast<uint32_t> (i / oscs), i % oscs) : 0u);
                                if (v[static_cast<size_t> (i)]->period (t, L, R) && per >= 4)
                                    ++perBlock[per / 4];
                            }
                        }
                        int mx = 0, mn = 1 << 20;
                        for (int b = 1; b <= 8; ++b)
                        {
                            mx = std::max (mx, perBlock[b]);
                            mn = std::min (mn, perBlock[b]);
                        }
                        if (stagger != 0)
                        {
                            ++cases;
                            flat += mx - mn <= 1 ? 1 : 0;
                            worstSpread = std::max (worstSpread, mx - mn);
                        }
                        else
                            oldWorstSpread = std::max (oldWorstSpread, mx - mn);
                    }
    std::printf ("    chords started in one block: %d cases, %d flat (builds per block max - min <= 1, worst %d); without the stagger the worst is %d\n", cases, flat, worstSpread, oldWorstSpread);
    CHECK_EQ (cases, 2 * 2 * 16 * 4);
    CHECK_EQ (flat, cases);
    CHECK (oldWorstSpread >= 4);
    // a chord of 4 notes with one morph oscillator (not exact for every first serial: at most 2 builds apart in a block of 4 periods, tier low, against 4 without the stagger)
    int worst4 = 0;
    for (uint32_t serial0 = 0; serial0 < 16; ++serial0)
        for (int phase = 0; phase < 4; ++phase)
        {
            std::vector<std::unique_ptr<StaggerVoice>> v;
            for (int i = 0; i < 4; ++i)
                v.push_back (std::make_unique<StaggerVoice>());
            const OscTick t = morphTick (tables[2].get(), kMorphHarmonicStretch, true, 1);
            int perBlock[10] = {};
            for (int per = phase; per < 40; ++per)
                for (int i = 0; i < 4; ++i)
                {
                    if (per == phase)
                        v[static_cast<size_t> (i)]->start (WtOsc::morphStaggerSeed (serial0 + static_cast<uint32_t> (i), 0));
                    if (v[static_cast<size_t> (i)]->period (t, L, R) && per >= 4)
                        ++perBlock[per / 4];
                }
            int mx = 0, mn = 1 << 20;
            for (int b = 1; b <= 8; ++b)
            {
                mx = std::max (mx, perBlock[b]);
                mn = std::min (mn, perBlock[b]);
            }
            worst4 = std::max (worst4, mx - mn);
        }
    std::printf ("    a chord of 4 notes, one morph oscillator, tier low: builds per block max - min at most %d (every first serial and start period)\n", worst4);
    CHECK (worst4 <= 2);

    // restarts and stealing
    int longestGap[2] = {}, starts = 0, firstBuildAtStart = 0;
    for (int low = 0; low < 2; ++low)
    {
        const int P = low != 0 ? 16 : 8;
        std::vector<std::unique_ptr<StaggerVoice>> v;
        std::vector<int> last (8, -1);
        for (int i = 0; i < 8; ++i)
            v.push_back (std::make_unique<StaggerVoice>());
        OscTick t = morphTick (tables[3].get(), kMorphShepard, low != 0, 2);
        uint32_t serial = 5;
        for (int per = 0; per < 2000; ++per)
        {
            t.morphAmount = 0.5f + 0.4f * std::sin (0.01f * static_cast<float> (per));   // a moving morph
            for (int i = 0; i < 8; ++i)
            {
                const bool restart = per == 0 || r.chance (2);
                if (restart)
                {
                    v[static_cast<size_t> (i)]->start (WtOsc::morphStaggerSeed (serial++, i & 1));
                    ++starts;
                }
                const bool built = v[static_cast<size_t> (i)]->period (t, L, R);
                if (restart)
                {
                    firstBuildAtStart += built ? 1 : 0;
                    last[static_cast<size_t> (i)] = per;
                }
                else if (built)
                {
                    longestGap[low] = std::max (longestGap[low], per - last[static_cast<size_t> (i)]);
                    last[static_cast<size_t> (i)] = per;
                }
                else
                    CHECK (per - last[static_cast<size_t> (i)] < P);   // the build is due at most P periods after the last
            }
        }
    }
    std::printf ("    restarts: %d note starts, every one built in its first period: %s; longest gap between two builds of a voice: %d periods (low, P = 16), %d (full, P = 8)\n", starts,
                 firstBuildAtStart == starts ? "yes" : "no", longestGap[1], longestGap[0]);
    CHECK_EQ (firstBuildAtStart, starts);
    CHECK (longestGap[1] <= 16);
    CHECK (longestGap[0] <= 8);
}

// (a) A held static morph (nothing moves: the tick is the same every period) gives the very bits whatever the stagger: every build of it is the same cycle (the build is deterministic, a cache
// hit has the samples of a build), and between two equal cycles the cross-fade is exact (a + (b - a) w = a for b = a). Tier low and full, unison 1 and 3, morph spread 0 and 0.6, several morph types,
// stagger 0 (the old schedule) against every slot.
TEST (own_wt_osc_morph_stagger_keeps_a_held_static_morph_bit_identical)
{
    TestRng r (31);
    const auto tables = makeTables (r);
    int runs = 0, different = 0;
    for (int low = 0; low < 2; ++low)
        for (int voices : { 1, 3 })
            for (int type : { static_cast<int> (kMorphHarmonicStretch), static_cast<int> (kMorphShepard), 1, 2 })
                for (float spread : { 0.0f, 0.6f })
                    for (uint32_t n = 1; n < 16; n += 2)
                    {
                        OscTick t = morphTick (tables[2].get(), type, low != 0, voices);
                        t.morphSpread = spread;
                        StaggerVoice a, b;
                        a.start (0);
                        b.start (n);
                        std::vector<float> oa, ob;
                        float La[32], Ra[32], Lb[32], Rb[32];
                        for (int per = 0; per < 80; ++per)
                        {
                            a.period (t, La, Ra);
                            b.period (t, Lb, Rb);
                            oa.insert (oa.end(), La, La + 32);
                            oa.insert (oa.end(), Ra, Ra + 32);
                            ob.insert (ob.end(), Lb, Lb + 32);
                            ob.insert (ob.end(), Rb, Rb + 32);
                        }
                        ++runs;
                        if (std::memcmp (oa.data(), ob.data(), oa.size() * sizeof (float)) != 0)
                            ++different;
                    }
    std::printf ("    held static morph: %d runs, %d differ from the unstaggered oscillator\n", runs, different);
    CHECK_EQ (runs, 2 * 2 * 4 * 2 * 8);
    CHECK_EQ (different, 0);
}

// Mac timing of the per block cost: 8 voices of Stretched Glass Choir's morph (Harmonic Stretch, 1 lane, tier low, a moving morph amount), started in one block, 64 blocks of 4 periods; the
// longest block and the median block in us, without and with the stagger (the best of 5 runs).
TEST (own_wt_osc_morph_stagger_timing_of_the_worst_block)
{
    TestRng r (99);
    const auto tables = makeTables (r);
    float L[32], R[32];
    for (int layout = 0; layout < 2; ++layout)
    {
        double worst[2] = { 1.0e9, 1.0e9 }, median[2] = { 1.0e9, 1.0e9 };
        for (int run = 0; run < 5; ++run)
            for (int stagger = 0; stagger < 2; ++stagger)
            {
                const int notes = layout == 0 ? 8 : 4, oscs = layout == 0 ? 1 : 2;
                std::vector<std::unique_ptr<StaggerVoice>> v;
                for (int i = 0; i < notes * oscs; ++i)
                    v.push_back (std::make_unique<StaggerVoice>());
                OscTick t = morphTick (tables[2].get(), layout == 0 ? kMorphHarmonicStretch : kMorphShepard, true, 1);
                std::vector<double> blocks;
                for (int b = 0; b < 65; ++b)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int per = 0; per < 4; ++per)
                    {
                        t.morphAmount = 0.3f + 0.2f * std::sin (0.05f * static_cast<float> (b * 4 + per));
                        for (int i = 0; i < notes * oscs; ++i)
                        {
                            if (b == 0 && per == 0)
                                v[static_cast<size_t> (i)]->start (stagger != 0 ? WtOsc::morphStaggerSeed (40u + static_cast<uint32_t> (i / oscs), i % oscs) : 0u);
                            v[static_cast<size_t> (i)]->period (t, L, R);
                        }
                    }
                    if (b > 0)
                        blocks.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
                }
                std::sort (blocks.begin(), blocks.end());
                worst[stagger] = std::min (worst[stagger], blocks.back());
                median[stagger] = std::min (median[stagger], blocks[blocks.size() / 2]);
            }
        std::printf ("timing %s (tier low, moving morph), blocks after the start: longest %.1f us, median %.1f us without the stagger; longest %.1f us, median %.1f us with it (this Mac)\n",
                     layout == 0 ? "8 voices x Harmonic Stretch" : "4 voices x 2 Shepard", worst[0], median[0], worst[1], median[1]);
    }
    CHECK (true);
}

// Pattern copied from own_wt_osc_silent_oscillator_early_out_is_bit_identical_when_the_level_rises_later (lines 511-620)
// and makeTables / OscTick setup (lines 1-140).
TEST (own_wt_osc_smooth_frame_scan_ramps_per_sample)
{
    // 1. Build a 2-frame Wavetable:
    //    frame 0: sinCoeff[0][1] = +1.0f (+sin(2 pi x))
    //    frame 1: sinCoeff[1][1] = -1.0f (-sin(2 pi x))
    //    all other harmonics 0, normalizePeak = true.
    //    Linear blend w in [0, 1] gives y(p, w) = (1 - 2 w) * sin(2 pi p).
    std::vector<std::vector<float>> cc (2, std::vector<float> (Wavetable::kCycle / 2 + 1, 0.0f));
    std::vector<std::vector<float>> ss = cc;
    ss[0][1] = 1.0f;
    ss[1][1] = -1.0f;
    Wavetable wt;
    wt.build (cc, ss, true);

    // 2. Run two WtOsc instances:
    //    oscConst with frame = 0.0f constant.
    //    oscSweep whose frame steps linearly by df = 1.0f / 32.0f = 0.03125f per 32-sample period.
    //    sr = 48000.0, voices = 1, level = 1.0f, pan = 0.0f, smooth = true, semis = 0.0f (f = 8.1758 Hz).
    //    start (0.25f, 0.0f, rng) -> phase moves from 0.25 to 0.3045, keeping oscConst in [0.94, 1.00].
    WtOsc oscConst, oscSweep;
    Rng rngConst, rngSweep;
    oscConst.start (0.25f, 0.0f, rngConst);
    oscSweep.start (0.25f, 0.0f, rngSweep);

    OscTick tConst;
    tConst.wt = &wt;
    tConst.voices = 1;
    tConst.level = 1.0f;
    tConst.pan = 0.0f;
    tConst.panRight = 0.0f;
    tConst.smooth = true;
    tConst.semis = 0.0f;
    tConst.frame = 0.0f;
    tConst.active = true;

    OscTick tSweep = tConst;

    // Priming period -1 with jump = true, frame = 0.0f (so prevFrameOf = 0.0f)
    tConst.jump = true;
    tSweep.jump = true;
    oscConst.update (tConst, 48000.0, rngConst);
    oscSweep.update (tSweep, 48000.0, rngSweep);
    tConst.jump = false;
    tSweep.jump = false;

    float constL[320] = {}, constR[320] = {};
    float sweepL[320] = {}, sweepR[320] = {};

    for (int p = 0; p < 10; ++p)
    {
        tSweep.frame = static_cast<float> (p + 1) * 0.03125f;
        oscConst.update (tConst, 48000.0, rngConst);
        oscSweep.update (tSweep, 48000.0, rngSweep);

        oscConst.render (0, 32, constL + p * 32, constR + p * 32, nullptr, 48000.0);
        oscSweep.render (0, 32, sweepL + p * 32, sweepR + p * 32, nullptr, 48000.0);
    }

    // 3. Recover instantaneous blend weight w[k] = 0.5f * (1.0f - sweepL[k] / constL[k])
    float w[320];
    for (int k = 0; k < 320; ++k)
        w[k] = 0.5f * (1.0f - sweepL[k] / constL[k]);

    float maxStep = 0.0f;
    float minInnerStep = 1.0f;
    for (int k = 1; k < 320; ++k)
    {
        const float step = std::fabs (w[k] - w[k - 1]);
        maxStep = std::max (maxStep, step);
        if ((k % 32) != 0)
            minInnerStep = std::min (minInnerStep, step);
    }

    // BEFORE FIX:
    // inside each 32-sample period w[k] is flat (minInnerStep == 0.0f).
    // at each 32-sample boundary w jumps by df = 0.03125f (maxStep == 0.03125f).
    // AFTER FIX:
    // w ramps by 0.03125f / 32 = 0.0009765625f per sample (maxStep ≈ 0.000977f, minInnerStep ≈ 0.000977f).
    CHECK (maxStep < 0.0015f);
    CHECK (minInnerStep > 0.0005f);

    // 4. Split period (t0 = 0, n = 10 then t0 = 10, n = 22) on oscSweep
    tSweep.frame = 11.0f * 0.03125f;
    oscConst.update (tConst, 48000.0, rngConst);
    oscSweep.update (tSweep, 48000.0, rngSweep);

    float splitConstL[32] = {}, splitConstR[32] = {};
    float splitSweepL[32] = {}, splitSweepR[32] = {};

    oscConst.render (0, 10, splitConstL, splitConstR, nullptr, 48000.0);
    oscSweep.render (0, 10, splitSweepL, splitSweepR, nullptr, 48000.0);
    oscConst.render (10, 22, splitConstL + 10, splitConstR + 10, nullptr, 48000.0);
    oscSweep.render (10, 22, splitSweepL + 10, splitSweepR + 10, nullptr, 48000.0);

    float wSplit[32];
    for (int k = 0; k < 32; ++k)
        wSplit[k] = 0.5f * (1.0f - splitSweepL[k] / splitConstL[k]);

    float maxSplitStep = std::fabs (wSplit[0] - w[319]);
    for (int k = 1; k < 32; ++k)
        maxSplitStep = std::max (maxSplitStep, std::fabs (wSplit[k] - wSplit[k - 1]));

    CHECK (maxSplitStep < 0.0015f);

    // 5. Static-frame bit-identity checksum: 10 periods with constant frame = 0.37f
    WtOsc oscStatic;
    Rng rngStatic;
    oscStatic.start (0.25f, 0.0f, rngStatic);

    OscTick tStatic = tConst;
    tStatic.frame = 0.37f;
    tStatic.jump = true;

    float staticL[320] = {}, staticR[320] = {};
    for (int p = 0; p < 10; ++p)
    {
        if (p > 0)
            tStatic.jump = false;
        oscStatic.update (tStatic, 48000.0, rngStatic);
        oscStatic.render (0, 32, staticL + p * 32, staticR + p * 32, nullptr, 48000.0);
    }

    double staticSum = 0.0;
    for (int k = 0; k < 320; ++k)
        staticSum += static_cast<double> (staticL[k]);

    std::printf ("WTFRAME-CHECKSUM static sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                 staticSum,
                 static_cast<double> (staticL[312]), static_cast<double> (staticL[313]),
                 static_cast<double> (staticL[314]), static_cast<double> (staticL[315]),
                 static_cast<double> (staticL[316]), static_cast<double> (staticL[317]),
                 static_cast<double> (staticL[318]), static_cast<double> (staticL[319]));
}

TEST (own_wt_osc_external_shared_morph_scratch_and_tier_sized_history)
{
    TestRng r (42);
    const auto tables = makeTables (r);
    const Wavetable* wt = tables[1].get();

    MorphScratch sharedScratch;
    WtOsc shared0, shared1;
    shared0.prepareMorph (&sharedScratch, 4);
    shared1.prepareMorph (&sharedScratch, 4);

    WtOsc priv0, priv1;
    priv0.prepareMorph();
    priv1.prepareMorph();

    // Verify memory bounds for tier-sized history and external shared scratch
    CHECK (shared0.morphOwnedBytes() <= 35000);
    CHECK (shared1.morphOwnedBytes() <= 35000);
    CHECK_EQ (shared0.morphOwnedBytes(), static_cast<size_t> (34864));
    CHECK_EQ (shared1.morphOwnedBytes(), static_cast<size_t> (34864));
    CHECK (priv0.morphOwnedBytes() > 60000);
    CHECK (priv1.morphOwnedBytes() > 60000);

    // Verify clearState preserves scratch pointer and tier history
    shared0.clearState();
    CHECK_EQ (shared0.morphOwnedBytes(), static_cast<size_t> (34864));

    const int morphTypes[4] = { kMorphVocode, kMorphHarmonicStretch, kMorphSmear, kMorphPhaseDisperse };
    const char* const morphNames[4] = { "Vocode", "HarmonicStretch", "Smear", "PhaseDisperse" };

    for (int m = 0; m < 4; ++m)
    {
        const int mType = morphTypes[m];

        OscTick t0;
        t0.wt = wt;
        t0.semis = 48.0f; // C3
        t0.voices = 4;
        t0.detuneSemis = 0.25f;
        t0.spread = 0.5f;
        t0.blend = 0.7f;
        t0.level = 0.5f;
        t0.frame = 0.35f;
        t0.morphType = mType;
        t0.morphAmount = 0.6f;
        t0.warp[0] = kWarpSync;
        t0.warpAmount[0] = 0.45f;
        t0.active = true;

        OscTick t1 = t0;
        t1.semis = 55.0f; // G3
        t1.frame = 0.75f;
        t1.morphAmount = 0.4f;
        t1.warpAmount[0] = 0.35f;

        // Reset state for clean comparison
        shared0.clearState();
        shared1.clearState();
        priv0.clearState();
        priv1.clearState();

        Rng rngS0, rngS1, rngP0, rngP1;
        shared0.start (0.125f, 0.5f, rngS0);
        shared1.start (0.375f, 0.5f, rngS1);
        priv0.start (0.125f, 0.5f, rngP0);
        priv1.start (0.375f, 0.5f, rngP1);

        constexpr int kTotalSamples = 4096;
        constexpr int kChunk = 32;
        constexpr int kPeriods = kTotalSamples / kChunk;
        float chordSharedL[kTotalSamples] = {}, chordSharedR[kTotalSamples] = {};
        float chordPrivL[kTotalSamples] = {}, chordPrivR[kTotalSamples] = {};

        const double sampleRate = 44100.0;

        for (int p = 0; p < kPeriods; ++p)
        {
            const bool firstOfNote = (p == 0);

            // Render shared scratch instances
            shared0.update (t0, sampleRate, rngS0);
            shared0.updateMorph (sampleRate, firstOfNote, nullptr);
            float s0L[kChunk], s0R[kChunk];
            shared0.render (0, kChunk, s0L, s0R, nullptr, sampleRate);

            shared1.update (t1, sampleRate, rngS1);
            shared1.updateMorph (sampleRate, firstOfNote, nullptr);
            float s1L[kChunk], s1R[kChunk];
            shared1.render (0, kChunk, s1L, s1R, nullptr, sampleRate);

            // Render private scratch instances
            priv0.update (t0, sampleRate, rngP0);
            priv0.updateMorph (sampleRate, firstOfNote, nullptr);
            float p0L[kChunk], p0R[kChunk];
            priv0.render (0, kChunk, p0L, p0R, nullptr, sampleRate);

            priv1.update (t1, sampleRate, rngP1);
            priv1.updateMorph (sampleRate, firstOfNote, nullptr);
            float p1L[kChunk], p1R[kChunk];
            priv1.render (0, kChunk, p1L, p1R, nullptr, sampleRate);

            for (int i = 0; i < kChunk; ++i)
            {
                chordSharedL[p * kChunk + i] = s0L[i] + s1L[i];
                chordSharedR[p * kChunk + i] = s0R[i] + s1R[i];
                chordPrivL[p * kChunk + i] = p0L[i] + p1L[i];
                chordPrivR[p * kChunk + i] = p0R[i] + p1R[i];
            }
        }

        // Must be bit-identical between shared and legacy private scratch
        CHECK_EQ (std::memcmp (chordSharedL, chordPrivL, sizeof (chordSharedL)), 0);
        CHECK_EQ (std::memcmp (chordSharedR, chordPrivR, sizeof (chordSharedR)), 0);

        double sumAbs = 0.0;
        for (int i = 0; i < kTotalSamples; ++i)
            sumAbs += static_cast<double> (std::fabs (chordSharedL[i]));

        CHECK (sumAbs > 10.0); // signal is audible and non-zero

        std::printf ("WTOSC-SHARED-SCRATCH-CHECKSUM morph=%d name=%s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     mType, morphNames[m], sumAbs,
                     static_cast<double> (chordSharedL[kTotalSamples - 8]), static_cast<double> (chordSharedL[kTotalSamples - 7]),
                     static_cast<double> (chordSharedL[kTotalSamples - 6]), static_cast<double> (chordSharedL[kTotalSamples - 5]),
                     static_cast<double> (chordSharedL[kTotalSamples - 4]), static_cast<double> (chordSharedL[kTotalSamples - 3]),
                     static_cast<double> (chordSharedL[kTotalSamples - 2]), static_cast<double> (chordSharedL[kTotalSamples - 1]));
    }
}


