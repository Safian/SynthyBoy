// SynthyBoy - own engine tests: the optimised wavetable oscillator (WtOsc, docs/release/CPU-REVIEW-OSC.md) gives the very bits of the code it replaced. Two oscillators run in step on
// random patches: one with setReference (true) (the old code paths), one as shipped; after every call their outputs and their whole state are compared bit by bit. Compiled with the
// NEON forms on (also on Apple's arm64) and without fused multiply-add, with the morph and wavetable sources (core/tests/CMakeLists.txt); it links nothing else of the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "osc/wt_osc.h"
#include "test_runner.h"

namespace synthyboy
{
namespace own
{
struct WtOscOptTest
{
    /** True if every piece of state that the audio depends on is bit for bit the same (the caches of the optimisations are not state). */
    static bool sameState (const WtOsc& a, const WtOsc& b)
    {
        auto eq = [] (const void* x, const void* y, size_t n) { return std::memcmp (x, y, n) == 0; };
        bool ok = eq (a.phase, b.phase, sizeof a.phase) && eq (a.prevInc, b.prevInc, sizeof a.prevInc) && eq (a.curInc, b.curInc, sizeof a.curInc)
               && eq (a.prevGainL, b.prevGainL, sizeof a.prevGainL) && eq (a.curGainL, b.curGainL, sizeof a.curGainL) && eq (a.prevGainR, b.prevGainR, sizeof a.prevGainR)
               && eq (a.curGainR, b.curGainR, sizeof a.curGainR) && eq (a.curFrameOf, b.curFrameOf, sizeof a.curFrameOf) && eq (a.frameIdx, b.frameIdx, sizeof a.frameIdx)
               && eq (a.filterState, b.filterState, sizeof a.filterState) && eq (a.lastCross, b.lastCross, sizeof a.lastCross) && a.curCount == b.curCount && a.prevCount == b.prevCount
               && a.havePrev == b.havePrev && a.morphAge == b.morphAge && a.morphGroups == b.morphGroups && a.morphActive == b.morphActive && a.prevFrame == b.prevFrame
               && a.curFrame == b.curFrame && a.historyDirty == b.historyDirty;
        ok = ok && a.history.size() == b.history.size() && (a.history.empty() || eq (a.history.data(), b.history.data(), a.history.size() * sizeof (float)));
        ok = ok && a.morphBuf.size() == b.morphBuf.size() && (a.morphBuf.empty() || eq (a.morphBuf.data(), b.morphBuf.data(), a.morphBuf.size() * sizeof (float)));
        return ok;
    }
    static const OscTick& tickOf (const WtOsc& o) { return o.tick; }
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
    int periods = 0, mismatches = 0, stateMismatches = 0, plain = 0, warped = 0, morphing = 0, nonZero = 0;
    Run()
    {
        ref.setReference (true);
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
    int totalPeriods = 0, totalMismatch = 0, totalState = 0, plain = 0, warped = 0, morphing = 0, loud = 0;
    for (int seed = 1; seed <= 60; ++seed)
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
            run.ref.update (t, rate, run.rngRef);
            run.opt.update (t, rate, run.rngOpt);
            run.ref.updateMorph (rate, first, &run.cacheRef);
            run.opt.updateMorph (rate, first, &run.cacheOpt);
            first = false;
            if (!WtOscOptTest::sameState (run.ref, run.opt))
                ++run.stateMismatches;
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
                if (!WtOscOptTest::sameState (run.ref, run.opt))
                    ++run.stateMismatches;
                t0 += n;
            }
            ++run.periods;
            const OscTick& tk = WtOscOptTest::tickOf (run.opt);
            const bool w = tk.oversample || warpNeedsOversampling (tk.warp[0]) || warpNeedsOversampling (tk.warp[1]);
            (w ? run.warped : (tk.morphType != 0 ? run.morphing : run.plain))++;
        }
        totalPeriods += run.periods;
        totalMismatch += run.mismatches;
        totalState += run.stateMismatches;
        plain += run.plain;
        warped += run.warped;
        morphing += run.morphing;
        loud += run.nonZero;
    }
    std::printf ("wt_osc opt: %d periods (plain %d, warped %d, morph %d), %d loud chunks, %d output mismatches, %d state mismatches\n", totalPeriods, plain, warped, morphing, loud, totalMismatch,
                 totalState);
    CHECK_EQ (totalPeriods, 60 * 140);
    CHECK (plain > 500 && warped > 500 && morphing > 500 && loud > 2000);
    CHECK_EQ (totalMismatch, 0);
    CHECK_EQ (totalState, 0);
}

namespace
{
/** One preset like Lorenz Garden's three oscillators (3, 2, 1 unison lanes) plus the sub, run for `blocks` blocks of 4 periods: the time of the three calls of the oscillator, in us per block. */
struct Timing { double update = 0.0, morph = 0.0, render = 0.0; };

Timing timeConfig (bool reference, int voices, int morphType, int warp, int blocks, bool modulate, bool smooth = false)
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
    t.smooth = smooth;
    t.active = true;
    osc.start (0.0f, 0.5f, rng);
    float L[32], R[32];
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
            const auto a0 = clk::now();
            osc.update (t, 44100.0, rng);
            const auto a1 = clk::now();
            osc.updateMorph (44100.0, first, &cache);
            first = false;
            const auto a2 = clk::now();
            osc.render (0, 32, L, R, nullptr, 44100.0);
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
    struct Cfg { const char* name; int voices, morph, warp; bool modulate; };
    const Cfg cfgs[] = { { "3 lanes plain, frame/level/pan modulated", 3, 0, 0, true }, { "3 lanes plain, held", 3, 0, 0, false }, { "1 lane morph (Harmonic Stretch), modulated", 1, kMorphHarmonicStretch, 0, true },
                         { "3 lanes morph (Shepard), modulated", 3, kMorphShepard, 0, true }, { "2 lanes Sync warp, modulated", 2, 0, kWarpSync, true } };
    for (const Cfg& c : cfgs)
    {
        // warm up, then the median of 5 runs
        Timing best[2];
        for (int mode = 0; mode < 2; ++mode)
        {
            timeConfig (mode == 0, c.voices, c.morph, c.warp, 200, c.modulate);
            Timing runs[5];
            for (auto& x : runs)
                x = timeConfig (mode == 0, c.voices, c.morph, c.warp, 1500, c.modulate);
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
