// SynthyBoy - own engine tests: the CPU savings of the classic voice filters (docs/dsp-knowledge/filters.md, "Savings of 2026-11-26"). Every change that must not change a sample
// is compared with the code it replaced, bit for bit (the floats' bit patterns, not ==); the changes that may differ state their bound and are tested against it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include "dsp/fastmath.h"
#include "synthyboy/filters/filter.h"
#include "synthyboy/filters/filter_types.h"
#include "voice/classic_filters.h"
#include "voice/filter_unit.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy::own;

namespace
{
uint32_t bitsOf (float x)
{
    uint32_t u;
    std::memcpy (&u, &x, sizeof u);
    return u;
}

bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
}

/** Noise in -amp .. amp; with `special` a few values of every kind a recursion can meet (denormals, tiny, huge, signed zeros). */
void fillInput (sbtest::Rng& rng, std::vector<float>& v, float amp, bool special)
{
    static const float kSpecial[] = { 0.0f, -0.0f, 1.0e-30f, -1.0e-30f, 1.0e-39f, -1.0e-39f, 1.0e-18f, 1.0f, -1.0f, 8.0f, -8.0f, 1000.0f };
    for (auto& x : v)
    {
        x = static_cast<float> (rng.uniform() * 2.0 - 1.0) * amp;
        if (special && rng.u32() % 16u == 0u)
            x = kSpecial[rng.u32() % (sizeof kSpecial / sizeof kSpecial[0])];
    }
}

ClassicParams makeParams (int model, int style, bool fast, float cutoff, float res, float driveDb, float blendRaw)
{
    ClassicParams p;
    p.model = model;
    p.style = style;
    p.cutoffHz = cutoff;
    p.res = res;
    p.driveDb = driveDb;
    p.blendRaw = blendRaw;
    p.fast = fast;
    p.combOffset = 30.0f;
    return p;
}

int styleCount (int model)
{
    switch (model)
    {
        case 0: case 1: case 3: return 5;
        case 2: return 5;
        case 4: return 2;
        case 5: return 3;
        case 6: return 6;
        default: return 3;
    }
}
} // namespace

/** Item 1: the two channels of a filter with equal parameters share the setup of every piece. Against the separate passes the code had before (the left channel, adoptRamp,
    the right channel), every model and style, tier low and exact, parameters still / only the cutoff moving / everything moving, a block grid cut at odd places. */
TEST (own_filter_savings_twin_channels_are_bit_identical_to_separate_passes)
{
    long compared = 0;
    for (int model = 0; model <= 7; ++model)
        for (int style = 0; style < styleCount (model); ++style)
            for (int fastI = 0; fastI < 2; ++fastI)
                for (int motion = 0; motion < 3; ++motion)
                {
                    ClassicFilter ra, rb, ta, tb;
                    for (ClassicFilter* f : { &ra, &rb, &ta, &tb })
                        f->prepare (44100.0);
                    sbtest::Rng rng (0x7714ull + static_cast<uint64_t> (model * 1000 + style * 100 + fastI * 10 + motion));
                    ClassicParams prev = makeParams (model, style, fastI != 0, 900.0f, 0.4f, 6.0f, 0.7f);
                    const int cuts[3][3] = { { 32, 0, 0 }, { 5, 27, 0 }, { 8, 8, 16 } };
                    for (int tick = 0; tick < 60; ++tick)
                    {
                        ClassicParams cur = prev;
                        if (motion >= 1)
                            cur.cutoffHz = std::clamp (prev.cutoffHz * (0.8f + 0.5f * static_cast<float> (rng.uniform())), 60.0f, 18000.0f);
                        if (motion >= 2)
                        {
                            cur.res = clamp01 (prev.res + 0.2f * static_cast<float> (rng.uniform() - 0.5));
                            cur.driveDb = std::clamp (prev.driveDb + 4.0f * static_cast<float> (rng.uniform() - 0.5), 0.0f, 24.0f);
                            cur.blendRaw = std::clamp (prev.blendRaw + 0.3f * static_cast<float> (rng.uniform() - 0.5), 0.0f, 2.0f);
                        }
                        const int* cut = cuts[tick % 3];
                        int t0 = 0;
                        for (int part = 0; part < 3 && cut[part] > 0; ++part)
                        {
                            const int n = cut[part];
                            std::vector<float> inA (static_cast<size_t> (n)), inB (static_cast<size_t> (n)), oRA (static_cast<size_t> (n)), oRB (static_cast<size_t> (n)),
                                oTA (static_cast<size_t> (n)), oTB (static_cast<size_t> (n));
                            fillInput (rng, inA, 0.6f, tick % 7 == 0);
                            fillInput (rng, inB, 0.6f, tick % 7 == 0);
                            // the code before: the left channel, its ramp setups to the right one, the right channel
                            ra.process (inA.data(), oRA.data(), n, prev, cur, t0);
                            rb.adoptRamp (ra);
                            rb.process (inB.data(), oRB.data(), n, prev, cur, t0);
                            ClassicFilter::processTwin (ta, tb, inA.data(), oTA.data(), inB.data(), oTB.data(), n, prev, cur, t0, ClassicFilter::sameParams (prev, cur));
                            CHECK (sameBits (oRA, oTA));
                            CHECK (sameBits (oRB, oTB));
                            compared += 2 * n;
                            t0 += n;
                        }
                        prev = cur;
                    }
                }
    std::printf ("SAVINGS-TWIN compared %ld samples\n", compared);
}

namespace
{
FilterSettings settingsFor (int model, int style, bool fast, float cutoff, float cutoffR, float res, float drive)
{
    FilterSettings s;
    s.model = model;
    s.style = style;
    s.fast = fast;
    s.cutoffHz = cutoff;
    s.cutoffHzR = cutoffR;
    s.resonance = res;
    s.driveDb = drive;
    s.blend = 0.35f;
    s.mix = 1.0f;
    s.combOffset = 30.0f;
    return s;
}
} // namespace

/** The same at the level of the voice's FilterUnit: the classic models 0 .. 7 with the pan 0 (equal cutoffs, the twin pass) and a pan (different cutoffs), against the unit with
    `scalarOnly` (which keeps the separate passes). */
TEST (own_filter_savings_filter_unit_twin_matches_the_scalar_only_passes)
{
    for (int model = 0; model <= 7; ++model)
        for (int style = 0; style < styleCount (model); ++style)
            for (int fastI = 0; fastI < 2; ++fastI)
                for (int pan = 0; pan < 2; ++pan)
                {
                    FilterUnit newer, older;
                    newer.prepare (44100.0);
                    older.prepare (44100.0);
                    sbtest::Rng rng (0x9a11ull + static_cast<uint64_t> (model * 977 + style * 61 + fastI * 7 + pan));
                    float cutoff = 1200.0f;
                    for (int tick = 0; tick < 50; ++tick)
                    {
                        cutoff = std::clamp (cutoff * (0.85f + 0.3f * static_cast<float> (rng.uniform())), 80.0f, 16000.0f);
                        FilterSettings s = settingsFor (model, style, fastI != 0, cutoff, pan != 0 ? cutoff * 1.3f : -1.0f, 0.5f * static_cast<float> (rng.uniform()), 3.0f);
                        s.scalarOnly = false;
                        newer.set (s);
                        s.scalarOnly = true;
                        older.set (s);
                        std::vector<float> inL (32), inR (32), a1 (32), a2 (32), b1 (32), b2 (32);
                        fillInput (rng, inL, 0.5f, false);
                        fillInput (rng, inR, 0.5f, false);
                        newer.process (inL.data(), inR.data(), a1.data(), a2.data(), 32, 0);
                        older.process (inL.data(), inR.data(), b1.data(), b2.data(), 32, 0);
                        CHECK (sameBits (a1, b1));
                        CHECK (sameBits (a2, b2));
                    }
                }
}

namespace
{
/** The classic branch of FilterUnit as it was before the sameParams booleans and the hoisted clean gain (the code of 391febd, with the separate channel passes): the
    reference of the item 7 test. */
struct ReferenceClassicUnit
{
    ClassicFilter cl[2];
    ClassicParams prevP, curP, prevPR, curPR;
    float prevMix = 1.0f, curMix = 1.0f, prevClean = 0.0f, curClean = 0.0f;

    void prepare (double sr)
    {
        cl[0].prepare (sr);
        cl[1].prepare (sr);
    }
    void set (const FilterSettings& s)
    {
        prevP = curP;
        prevPR = curPR;
        curP.model = s.model;
        curP.style = s.style;
        curP.cutoffHz = s.cutoffHz;
        curP.res = s.resonance;
        curP.driveDb = s.driveDb;
        curP.blendRaw = s.blend * 2.0f;
        curP.combOffset = s.combOffset;
        curP.fx = s.formantX;
        curP.fy = s.formantY;
        curP.fres = s.formantRes;
        curP.fspread = s.formantSpread;
        curP.ftranspose = s.formantTranspose;
        curP.fast = s.fast;
        curPR = curP;
        if (s.cutoffHzR > 0.0f)
            curPR.cutoffHz = s.cutoffHzR;
        cl[0].scalarLoops = cl[1].scalarLoops = s.scalarOnly;
        prevMix = curMix;
        curMix = clamp01 (s.mix);
        prevClean = curClean;
        curClean = (s.clean && s.model != 5 && s.model != 6) ? 1.0f : 0.0f;
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, int t0)
    {
        const float invP = 1.0f / static_cast<float> (kControlPeriod);
        float tmpL[kControlPeriod], tmpR[kControlPeriod];
        const bool cleaning = curClean > 0.5f || prevClean > 0.5f;
        const float* srcL = inL;
        const float* srcR = inR;
        if (cleaning)
        {
            for (int k = 0; k < n; ++k)
            {
                const float c = lerp (prevClean, curClean, static_cast<float> (t0 + k + 1) * invP);
                const float g = dbToGain (-24.0f * c);
                tmpL[k] = inL[k] * g;
                tmpR[k] = inR[k] * g;
            }
            srcL = tmpL;
            srcR = tmpR;
        }
        if (!ClassicFilter::processPair (cl[0], cl[1], srcL, outL, srcR, outR, n, prevP, curP, prevPR, curPR))
        {
            const bool sameLR = ClassicFilter::sameParams (prevP, prevPR) && ClassicFilter::sameParams (curP, curPR);
            if (sameLR && ClassicFilter::processRampPair (cl[0], cl[1], srcL, outL, srcR, outR, n, prevP, curP, t0))
            {
            }
            else
            {
                cl[0].process (srcL, outL, n, prevP, curP, t0);
                if (ClassicFilter::sameParams (prevP, prevPR) && ClassicFilter::sameParams (curP, curPR))
                    cl[1].adoptRamp (cl[0]);
                cl[1].process (srcR, outR, n, prevPR, curPR, t0);
            }
        }
        if (!cleaning && prevMix == 1.0f && curMix == 1.0f)
        {
            simd::clampTo (outL, -16.0f, 16.0f, n);
            simd::clampTo (outR, -16.0f, 16.0f, n);
            return;
        }
        for (int k = 0; k < n; ++k)
        {
            const float t = static_cast<float> (t0 + k + 1) * invP;
            const float c = cleaning ? dbToGain (24.0f * lerp (prevClean, curClean, t)) : 1.0f;
            const float m = lerp (prevMix, curMix, t);
            outL[k] = std::clamp (outL[k] * c, -16.0f, 16.0f);
            outR[k] = std::clamp (outR[k] * c, -16.0f, 16.0f);
            if (m < 1.0f)
            {
                outL[k] = inL[k] + (outL[k] - inL[k]) * m;
                outR[k] = inR[k] + (outR[k] - inR[k]) * m;
            }
        }
    }
};
} // namespace

/** Item 7: the parameter comparisons made once in FilterUnit::set () and the clean gain computed once for a block whose clean setting does not move, against the code before
    (ReferenceClassicUnit): models 0 .. 7, clean off / on / switching, the pan, parameters held and moving, mix 1 and below, tier low and exact. */
TEST (own_filter_savings_unit_booleans_and_steady_clean_match_the_reference)
{
    for (int model = 0; model <= 7; ++model)
        for (int fastI = 0; fastI < 2; ++fastI)
            for (int variant = 0; variant < 6; ++variant)
            {
                const int style = variant % styleCount (model);
                FilterUnit unit;
                ReferenceClassicUnit ref;
                unit.prepare (44100.0);
                ref.prepare (44100.0);
                sbtest::Rng rng (0xc1ea0ull + static_cast<uint64_t> (model * 131 + fastI * 17 + variant));
                float cutoff = 900.0f;
                for (int tick = 0; tick < 80; ++tick)
                {
                    // the parameters stay put for a stretch of ticks, then move; the clean setting flips now and then; the pan on and off
                    if (tick % 10 >= 4)
                        cutoff = std::clamp (cutoff * (0.9f + 0.2f * static_cast<float> (rng.uniform())), 80.0f, 15000.0f);
                    FilterSettings s = settingsFor (model, style, fastI != 0, cutoff, (variant & 1) != 0 && tick % 20 >= 10 ? cutoff * 1.25f : -1.0f, 0.3f, 4.0f);
                    s.clean = variant >= 2 && (tick / 7) % 2 == 1;
                    s.mix = variant >= 4 && tick % 5 == 0 ? 0.6f : 1.0f;
                    unit.set (s);
                    ref.set (s);
                    const int cuts[2][2] = { { 32, 0 }, { 13, 19 } };
                    int t0 = 0;
                    for (int part = 0; part < 2 && cuts[tick % 2][part] > 0; ++part)
                    {
                        const int n = cuts[tick % 2][part];
                        std::vector<float> inL (static_cast<size_t> (n)), inR (static_cast<size_t> (n)), a1 (static_cast<size_t> (n)), a2 (static_cast<size_t> (n)),
                            b1 (static_cast<size_t> (n)), b2 (static_cast<size_t> (n));
                        fillInput (rng, inL, 0.5f, tick % 9 == 0);
                        fillInput (rng, inR, 0.5f, tick % 9 == 0);
                        unit.process (inL.data(), inR.data(), a1.data(), a2.data(), n, t0);
                        ref.process (inL.data(), inR.data(), b1.data(), b2.data(), n, t0);
                        CHECK (sameBits (a1, b1));
                        CHECK (sameBits (a2, b2));
                        t0 += n;
                    }
                }
            }
}

namespace
{
/** The Serum branch of FilterUnit as it was (the collection's two filters with the clean gain computed per sample): the reference of the Serum clean test. */
struct ReferenceSerumUnit
{
    synthyboy::filters::Filter left, right;
    float prevClean = 0.0f, curClean = 0.0f;
    void prepare (double sr)
    {
        left.prepare (sr);
        right.prepare (sr);
    }
    void set (const FilterSettings& s)
    {
        prevClean = curClean;
        curClean = (s.clean && s.model != 5 && s.model != 6) ? 1.0f : 0.0f;
        float morph = -1.0f;
        const int type = FilterUnit::typeFor (s.model, s.style, s.blend, morph);
        if (type != left.getType())
        {
            left.setType (type);
            right.setType (type);
        }
        synthyboy::filters::FilterParams p;
        p.cutoff = std::clamp (s.cutoffHz, 20.0f, 20000.0f);
        p.resonance = clamp01 (s.resonance);
        p.drive = clamp01 (s.drive);
        p.var = clamp01 (morph >= 0.0f ? morph : s.var);
        p.blend = clamp01 (s.blend);
        p.mix = clamp01 (s.mix);
        p.jump = s.noSmoothing;
        p.coefHold = s.fast ? 4 : 1;
        left.setParams (p);
        right.setParams (p);
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, int t0)
    {
        const bool cleaning = curClean > 0.5f || prevClean > 0.5f;
        const float invP = 1.0f / static_cast<float> (kControlPeriod);
        float tmpL[kControlPeriod], tmpR[kControlPeriod];
        if (cleaning)
        {
            for (int k = 0; k < n; ++k)
            {
                const float g = dbToGain (-24.0f * lerp (prevClean, curClean, static_cast<float> (t0 + k + 1) * invP));
                tmpL[k] = inL[k] * g;
                tmpR[k] = inR[k] * g;
            }
            if (!synthyboy::filters::Filter::processPair (left, right, tmpL, outL, tmpR, outR, n))
            {
                left.process (tmpL, outL, n);
                right.process (tmpR, outR, n);
            }
            for (int k = 0; k < n; ++k)
            {
                const float g = dbToGain (24.0f * lerp (prevClean, curClean, static_cast<float> (t0 + k + 1) * invP));
                outL[k] *= g;
                outR[k] *= g;
            }
        }
        else if (!synthyboy::filters::Filter::processPair (left, right, inL, outL, inR, outR, n))
        {
            left.process (inL, outL, n);
            right.process (inR, outR, n);
        }
        // the limiter after the filter: no runaway in these tests, the output is only limited (the unit's vector form of std::clamp)
        simd::clampTo (outL, -16.0f, 16.0f, n);
        simd::clampTo (outR, -16.0f, 16.0f, n);
    }
};
} // namespace

/** Item 7 for the Serum models (the clean gain around the collection's filter): the unit against ReferenceSerumUnit with the per sample dbToGain loops. */
TEST (own_filter_savings_serum_clean_steady_matches_the_per_sample_gain)
{
    const int models[] = { 8, 9, 12, 20, 26 };
    for (int model : models)
        for (int fastI = 0; fastI < 2; ++fastI)
        {
            FilterUnit unit;
            ReferenceSerumUnit ref;
            unit.prepare (44100.0);
            ref.prepare (44100.0);
            sbtest::Rng rng (0x5e70ull + static_cast<uint64_t> (model * 3 + fastI));
            for (int tick = 0; tick < 60; ++tick)
            {
                FilterSettings s = settingsFor (model, 0, fastI != 0, 800.0f + 40.0f * static_cast<float> (tick), -1.0f, 0.3f, 3.0f);
                s.clean = (tick / 8) % 2 == 1;
                s.drive = 0.3f;
                unit.set (s);
                ref.set (s);
                std::vector<float> inL (32), inR (32), a1 (32), a2 (32), b1 (32), b2 (32);
                fillInput (rng, inL, 0.4f, false);
                fillInput (rng, inR, 0.4f, false);
                unit.process (inL.data(), inR.data(), a1.data(), a2.data(), 32, 0);
                ref.process (inL.data(), inR.data(), b1.data(), b2.data(), 32, 0);
                CHECK (sameBits (a1, b1));
                CHECK (sameBits (a2, b2));
            }
        }
}

// ---- Item 3: the compares on the bit pattern, against the float forms they replaced, over all 2^32 patterns ----

namespace
{
float floatFrom (uint32_t u)
{
    float f;
    std::memcpy (&f, &u, sizeof f);
    return f;
}
bool isNanBits (uint32_t u)
{
    return (u & 0x7fffffffu) > 0x7f800000u;
}

// the code before
float oldFlushDenormal (float v)
{
    return std::fabs (v) < 1.0e-18f ? 0.0f : v;
}
float oldFastTanh (float x)
{
    if (x >= 4.972f)
        return 1.0f;
    if (x <= -4.972f)
        return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}
float oldHardtanh (const ClassicFilter& f, float v)
{
    const float a = std::fabs (v);
    return a <= 0.66f ? v : std::copysign (0.66f + 0.34f * f.th ((a - 0.66f) / 0.34f), v);
}

struct SweepResult
{
    uint64_t flushDiff = 0, tanhDiff = 0, tanhNanPayload = 0, kneeDiff = 0, hardDiff = 0, hardChecked = 0;
};

/** Every bit pattern u in [from, to) through the old and the new forms. A result differs when its bits differ; two NaNs of another payload are counted apart (tanhNanPayload):
    both forms are the same expression on a NaN, so the payload follows the hardware's rule for it. */
SweepResult sweep (uint64_t from, uint64_t to, bool hardtanhExact)
{
    SweepResult r;
    ClassicFilter fast, exact;
    fast.fastMath = true;
    exact.fastMath = false;
    for (uint64_t u = from; u < to; ++u)
    {
        const uint32_t b = static_cast<uint32_t> (u);
        const float x = floatFrom (b);
        if (bitsOf (flushDenormal (x)) != bitsOf (oldFlushDenormal (x)))
            ++r.flushDiff;
        const float tn = fastmath::tanh (x), tOld = oldFastTanh (x);
        if (bitsOf (tn) != bitsOf (tOld))
        {
            if (isNanBits (bitsOf (tn)) && isNanBits (bitsOf (tOld)))
                ++r.tanhNanPayload;
            else
                ++r.tanhDiff;
        }
        // the knee test of hardtanh: |v| <= 0.66f
        const bool knee = (b & 0x7fffffffu) <= 0x3f28f5c3u;
        if (knee != (std::fabs (x) <= 0.66f))
            ++r.kneeDiff;
        // hardtanh as a whole against its old form (the knee test above covers every pattern; here the whole function on a stride of the patterns: the tier low tanh on every 16th, the
        // libm tanh on every 97th, a library call)
        if (b % 16u == 0u)
        {
            const float h = fast.hardtanh (x), o = oldHardtanh (fast, x);
            ++r.hardChecked;
            if (bitsOf (h) != bitsOf (o) && !(isNanBits (bitsOf (h)) && isNanBits (bitsOf (o))))
                ++r.hardDiff;
        }
        if (hardtanhExact && b % 97u == 0u)
        {
            const float h = exact.hardtanh (x), o = oldHardtanh (exact, x);
            ++r.hardChecked;
            if (bitsOf (h) != bitsOf (o) && !(isNanBits (bitsOf (h)) && isNanBits (bitsOf (o))))
                ++r.hardDiff;
        }
    }
    return r;
}

SweepResult sweepAll (bool hardtanhExact)
{
    const unsigned threads = std::clamp (std::thread::hardware_concurrency(), 1u, 16u);
    std::vector<SweepResult> results (threads);
    std::vector<std::thread> pool;
    const uint64_t total = 1ull << 32;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back ([&, t]
                           {
                               results[t] = sweep (total * t / threads, total * (t + 1) / threads, hardtanhExact);
                           });
    for (auto& th : pool)
        th.join();
    SweepResult sum;
    for (const auto& r : results)
    {
        sum.flushDiff += r.flushDiff;
        sum.tanhDiff += r.tanhDiff;
        sum.tanhNanPayload += r.tanhNanPayload;
        sum.kneeDiff += r.kneeDiff;
        sum.hardDiff += r.hardDiff;
        sum.hardChecked += r.hardChecked;
    }
    return sum;
}
} // namespace

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

TEST (own_filter_savings_integer_compares_match_the_float_forms_on_denormals_with_flush_to_zero)
{
#if defined(__SSE__) || defined(__x86_64__)
    // the engine runs with FZ set on the devices: the denormal patterns (and the smallest normals) through the old and the new forms with FTZ / DAZ on (x86 MXCSR here)
    const unsigned oldCsr = _mm_getcsr();
    _mm_setcsr (oldCsr | 0x8040u); // FTZ | DAZ
    const SweepResult pos = sweep (0u, 0x01000000u, true), neg = sweep (0x80000000ull, 0x81000000ull, true);
    _mm_setcsr (oldCsr);
    CHECK (pos.flushDiff + neg.flushDiff == 0);
    CHECK (pos.tanhDiff + neg.tanhDiff == 0);
    CHECK (pos.kneeDiff + neg.kneeDiff == 0);
    CHECK (pos.hardDiff + neg.hardDiff == 0);
#else
    std::printf ("SAVINGS-SWEEP flush-to-zero sweep skipped: no x86 control register here (the ARM FZ bit is covered by the device and arm_denormal_check)\n");
#endif
}

TEST (own_filter_savings_integer_compares_match_the_float_forms_on_all_bit_patterns)
{
    // the constants the integer forms carry
    CHECK (bitsOf (1.0e-18f) == 0x219392efu);
    CHECK (bitsOf (4.972f) == 0x409f1aa0u);
    CHECK (bitsOf (0.66f) == 0x3f28f5c3u);
    CHECK (std::isinf (floatFrom (0x7f800000u)) && !std::isinf (floatFrom (0x7f7fffffu)));
    const SweepResult r = sweepAll (true);
    std::printf ("SAVINGS-SWEEP 2^32 patterns: flushDenormal differences %llu, tanh clamp differences %llu (NaN payload only: %llu), knee test differences %llu, hardtanh differences %llu of %llu\n",
                 static_cast<unsigned long long> (r.flushDiff), static_cast<unsigned long long> (r.tanhDiff), static_cast<unsigned long long> (r.tanhNanPayload),
                 static_cast<unsigned long long> (r.kneeDiff), static_cast<unsigned long long> (r.hardDiff), static_cast<unsigned long long> (r.hardChecked));
    CHECK (r.flushDiff == 0);
    CHECK (r.tanhDiff == 0);
    CHECK (r.tanhNanPayload == 0);
    CHECK (r.kneeDiff == 0);
    CHECK (r.hardDiff == 0);
}

// ---- Item 4: the dead terms of the Diode loop (w = 1) ----

namespace
{
/** The classic Diode as it was before the dead terms went (the setup of remember () and diode () and the sample loop of 391febd), in a struct of its own: the reference for the
    item 4 test. Everything but the four right hand side lines is the code of ClassicFilter. */
struct ReferenceDiode
{
    double sr = 44100.0;
    float dv[4] = {}, dHp[2] = {}, dFb = 0.0f, dU = 0.0f;
    bool quick = false;

    float th (float x) const
    {
        return quick ? fastmath::tanh (x) : std::tanh (x);
    }
    void block (const float* in, float* out, int n, const ClassicParams& p)
    {
        const float f = std::clamp (p.cutoffHz, 8.0f, static_cast<float> (sr) * 0.45f);
        const float d = std::pow (10.0f, p.driveDb * 0.05f);
        const float r = clamp01 (p.res);
        constexpr float kDiodeCorner = SB_DIODE_CORNER, kDiodeGain = SB_DIODE_GAIN, kDiodePre = 100.0f, kDiodeFb = SB_DIODE_FB, kDiodeW = 1.0f;
        (void) kDiodePre;
        const float k = 0.7f + 16.3f * r * r * r;
        const float dIn = (0.5f * k + 1.0f) * d;
        const float post = 1.0f / std::sqrt (d);
        const float fc = std::clamp (f * kDiodeCorner, 5.0f, static_cast<float> (sr) * 0.45f);
        const float g = std::tan (kPif * std::min (fc / static_cast<float> (sr), 0.499f));
        const float brawS = clamp01 (p.blendRaw * 0.5f);
        const bool style12S = p.style == 0;
        const float hpHz = f * std::exp2 (style12S ? -1.0f : -9.0f + 8.0f * brawS);
        const float amount = style12S ? brawS * brawS * 0.25f : 1.0f;
        const float gh = std::tan (kPif * std::clamp (hpHz, 1.0f, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr));
        const float gf = std::tan (kPif * 20.0f / static_cast<float> (sr));
        const float wS = kDiodeW, upS = 0.5f * wS, loS = 1.0f - 0.5f * wS;
        const float bb = 1.0f + g;
        float cp[4];
        cp[0] = -g * upS / bb;
        for (int j = 1; j < 4; ++j)
            cp[j] = (j < 3 ? -g * upS : 0.0f) / (bb + g * loS * cp[j - 1]);
        const float invBb = 1.0f / bb, hpA = gh / (1.0f + gh), hfA = gf / (1.0f + gf);
        float invD[4];
        invD[0] = 0.0f;
        for (int j = 1; j < 4; ++j)
            invD[j] = 1.0f / (bb + g * loS * cp[j - 1]);
        const float w = kDiodeW, up = 0.5f * w, lo = 1.0f - 0.5f * w;
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            float h = x;
            for (int j = 0; j < 2; ++j)
            {
                const float v = quick ? (h - dHp[j]) * hpA : (h - dHp[j]) * gh / (1.0f + gh);
                const float lp = v + dHp[j];
                dHp[j] = flushDenormal (lp + v);
                h = h - lp;
            }
            const float xin = lerp (x, h, amount);
            const float vf = quick ? (dv[3] - dFb) * hfA : (dv[3] - dFb) * gf / (1.0f + gf);
            const float lpf = vf + dFb;
            dFb = flushDenormal (lpf + vf);
            const float fb = dv[3] - lpf;
            const float xp = xin; // (kDiodePre = 100 is above 50: the optional extra pole is off)
            const float u = th (0.5f * (dIn * xp - k * kDiodeFb * fb)) * kDiodeGain;
            const float s0 = ClassicFilter::sat (dv[0]), s1 = ClassicFilter::sat (dv[1]), s2 = ClassicFilter::sat (dv[2]), s3 = ClassicFilter::sat (dv[3]);
            const float in0 = up * (dU + s1) + (1.0f - w) * dU, in1 = up * (s0 + s2) + (1.0f - w) * s0, in2 = up * (s1 + s3) + (1.0f - w) * s1,
                        in3 = up * 0.0f + lo * s2;
            float rhs[4] = { (1.0f - g) * dv[0] + g * in0 + g * lo * u, (1.0f - g) * dv[1] + g * in1, (1.0f - g) * dv[2] + g * in2, (1.0f - g) * dv[3] + g * in3 };
            dU = u;
            float dp[4];
            dp[0] = quick ? rhs[0] * invBb : rhs[0] / bb;
            for (int j = 1; j < 4; ++j)
                dp[j] = quick ? (rhs[j] + g * lo * dp[j - 1]) * invD[j] : (rhs[j] + g * lo * dp[j - 1]) / (bb + g * lo * cp[j - 1]);
            float v[4];
            v[3] = dp[3];
            for (int j = 2; j >= 0; --j)
                v[j] = dp[j] - cp[j] * v[j + 1];
            for (int j = 0; j < 4; ++j)
                dv[j] = flushDenormal (v[j]);
            out[i] = dv[3] * post;
        }
    }
};
} // namespace

/** The right hand side without the zero terms against the form with them: every finite output compares equal as a float (only the sign of an exact zero may differ), over noise, the
    specials of a recursion (denormals, signed zeros, huge values) and both tiers. Non-finite outputs are counted and printed: they are where the old zero terms made a NaN of an
    infinite state. */
TEST (own_filter_savings_diode_dead_terms_are_equal_as_floats)
{
    long samples = 0, zeroSign = 0, nonFinite = 0;
    const float cutoffs[] = { 80.0f, 420.0f, 3100.0f, 12000.0f };
    const float resonances[] = { 0.0f, 0.2f, 0.45f, 1.0f };
    for (int fastI = 0; fastI < 2; ++fastI)
        for (int style = 0; style < 2; ++style)
            for (float cutoff : cutoffs)
                for (float res : resonances)
                    for (int special = 0; special < 3; ++special) // 2: an infinity and a NaN in the input
                    {
                        ClassicFilter now;
                        now.prepare (44100.0);
                        ReferenceDiode ref;
                        ref.quick = fastI != 0;
                        ClassicParams p = makeParams (4, style, fastI != 0, cutoff, res, 20.0f, special != 0 ? 0.6f : 0.0f);
                        sbtest::Rng rng (0xd1ede0ull + static_cast<uint64_t> (fastI * 5000 + style * 1000 + static_cast<int> (cutoff) + static_cast<int> (res * 100) + special));
                        for (int blockNo = 0; blockNo < 40; ++blockNo)
                        {
                            std::vector<float> in (32), a (32), b (32);
                            // loud noise (the saturators and the loop clip), quiet noise, and with `special` the values of a recursion's edge cases
                            fillInput (rng, in, blockNo % 3 == 0 ? 8.0f : (blockNo % 3 == 1 ? 0.3f : 1.0e-3f), special != 0);
                            if (special == 2 && blockNo == 7)
                            {
                                in[3] = std::numeric_limits<float>::infinity();
                                in[20] = std::numeric_limits<float>::quiet_NaN();
                            }
                            now.block (in.data(), a.data(), 32, p);
                            ref.block (in.data(), b.data(), 32, p);
                            for (size_t i = 0; i < 32; ++i)
                            {
                                ++samples;
                                if (std::isfinite (a[i]) && std::isfinite (b[i]))
                                {
                                    CHECK (a[i] == b[i]);
                                    if (bitsOf (a[i]) != bitsOf (b[i]))
                                        ++zeroSign;
                                }
                                else
                                {
                                    ++nonFinite;
                                    CHECK (!std::isfinite (b[i]) || std::isfinite (a[i])); // (the old zero terms make a NaN of an infinite state; the new form is never the less finite one)
                                }
                            }
                        }
                    }
    std::printf ("SAVINGS-DIODE %ld samples: equal as floats, sign of a zero differs in %ld, a non-finite output in one of the two in %ld\n", samples, zeroSign, nonFinite);
}

// ---- Item 6: the reciprocal of the SVF denominator at tier low (not bit identical: a stated bound) ----

namespace synthyboy
{
namespace own
{
/** Test access to the private setup and loop of the state variable models (friend of ClassicFilter). */
struct ClassicFilterTestAccess
{
    static ClassicFilter::SvfSetup setup (const ClassicFilter& f, const ClassicParams& p) { return f.svfSetupFor (p); }
    static void run (ClassicFilter& f, const ClassicFilter::SvfSetup& z, const float* in, float* out, int n, int model, bool reciprocal)
    {
        f.fastMath = reciprocal; // svfRun () multiplies by the reciprocal denominators exactly when fastMath is set (the saturators are not reached at these levels)
        f.satLoop = false;
        f.svfRun (z, in, out, n, model, model == 3);
    }
};
} // namespace own
} // namespace synthyboy

/** The Digital (3) and Analog (0) state variable models, run by the exact division and by the reciprocal over the same setup and the same quiet noise (below the knee of the limiter
    and the saturators, so the only difference is the division): the largest difference and the RMS difference relative to the input RMS. The bound documented in docs/MPC.md is
    the one asserted here. */
TEST (own_filter_savings_svf_reciprocal_stays_within_the_stated_bound)
{
    double worstRelDb = -1000.0, worstAbs = 0.0;
    long different = 0, total = 0;
    const float cutoffs[] = { 60.0f, 420.0f, 1500.0f, 6000.0f, 14000.0f, 19000.0f };
    const float resonances[] = { 0.0f, 0.3f, 0.6f, 0.9f, 1.0f };
    for (int model : { 3, 0 })
        for (int style = 0; style < (model == 3 ? 5 : 2); ++style)
            for (float cutoff : cutoffs)
                for (float res : resonances)
                {
                    ClassicFilter exact, quick;
                    exact.prepare (44100.0);
                    quick.prepare (44100.0);
                    const ClassicParams p = makeParams (model, style, false, cutoff, res, 0.0f, 0.4f);
                    const ClassicFilter::SvfSetup z = ClassicFilterTestAccess::setup (exact, p);
                    sbtest::Rng rng (0x5f0ull + static_cast<uint64_t> (model * 100000 + style * 10000 + static_cast<int> (cutoff) + static_cast<int> (res * 10)));
                    double errSq = 0.0, refSq = 0.0, inSq = 0.0;
                    float maxAbs = 0.0f, peak = 0.0f;
                    for (int block = 0; block < 128; ++block)
                    {
                        std::vector<float> in (32), a (32), b (32);
                        fillInput (rng, in, 0.05f, false);
                        ClassicFilterTestAccess::run (exact, z, in.data(), a.data(), 32, model, false);
                        ClassicFilterTestAccess::run (quick, z, in.data(), b.data(), 32, model, true);
                        for (size_t i = 0; i < 32; ++i)
                        {
                            const double d = static_cast<double> (a[i]) - static_cast<double> (b[i]);
                            errSq += d * d;
                            refSq += static_cast<double> (a[i]) * static_cast<double> (a[i]);
                            inSq += static_cast<double> (in[i]) * static_cast<double> (in[i]);
                            maxAbs = std::max (maxAbs, static_cast<float> (std::fabs (d)));
                            peak = std::max (peak, std::fabs (a[i]));
                            different += a[i] != b[i] ? 1 : 0;
                            ++total;
                        }
                    }
                    CHECK (peak < 0.6f); // below the knee of hardtanh (0.66): the division is the only difference
                    if (refSq > 0.0)
                    {
                        // relative to the INPUT RMS (the filter is linear here, so this is level independent): the output of a 60 Hz resonant filter is small and made of large cancelling
                        // terms, so a difference of the order of the float rounding looks large against the output and small against what the filter was given
                        const double relDb = 10.0 * std::log10 (std::max (errSq, 1.0e-300) / inSq);
                        worstRelDb = std::max (worstRelDb, relDb);
                    }
                    worstAbs = std::max (worstAbs, static_cast<double> (maxAbs));
                }
    std::printf ("SAVINGS-SVF-RECIPROCAL %ld samples, %ld differ; worst RMS difference %.1f dB (re the input RMS), worst single difference %.3g (input peak 0.05)\n", total, different, worstRelDb, worstAbs);
    CHECK (worstRelDb < -100.0);
    CHECK (worstAbs < 1.0e-6);
}

/** The pair loop and the single channel loop of the state variable models give the same samples at both tiers (on x86 and wherever the pair loop is the scalar one; the NEON Digital loop
    keeps the true division at tier low until it is verified on ARM, so that case is left out of an SB_NEON build). */
TEST (own_filter_savings_svf_pair_loop_equals_the_single_channel_loop)
{
    for (int model : { 0, 1, 3 })
        for (int style = 0; style < 5; ++style)
            for (int fastI = 0; fastI < 2; ++fastI)
            {
#if SB_NEON
                if (model == 3 && fastI != 0)
                    continue;
#endif
                ClassicFilter pa, pb, sa, sb;
                for (ClassicFilter* f : { &pa, &pb, &sa, &sb })
                    f->prepare (44100.0);
                sbtest::Rng rng (0x9a1full + static_cast<uint64_t> (model * 100 + style * 10 + fastI));
                const ClassicParams p = makeParams (model, style, fastI != 0, 700.0f, 0.5f, 4.0f, 0.8f);
                const ClassicParams q = makeParams (model, style, fastI != 0, 2600.0f, 0.8f, 4.0f, 0.8f);
                for (int block = 0; block < 60; ++block)
                {
                    const ClassicParams& use = block % 2 == 0 ? p : q;
                    std::vector<float> inA (32), inB (32), a1 (32), a2 (32), b1 (32), b2 (32);
                    fillInput (rng, inA, 0.5f, block % 11 == 0);
                    fillInput (rng, inB, 0.5f, block % 11 == 0);
                    const bool did = ClassicFilter::processPairSteady (pa, pb, inA.data(), a1.data(), inB.data(), a2.data(), 32, use, use);
                    sa.block (inA.data(), b1.data(), 32, use);
                    sb.block (inB.data(), b2.data(), 32, use);
                    if (did)
                    {
                        CHECK (sameBits (a1, b1));
                        CHECK (sameBits (a2, b2));
                    }
                    else
                    {
                        // (the shelving mode has no pair loop: nothing to compare)
                        pa.block (inA.data(), a1.data(), 32, use);
                        pb.block (inB.data(), a2.data(), 32, use);
                    }
                }
            }
}

// ---- Item 5: the Serum filters' tangents and powers shared between the two sides of a stereo pair ----

/** Filter::processBoth (the right side reads and writes the left side's cache of tangents and powers) against two plain process () calls, every type of the collection, equal and
    different cutoffs on the two sides, coefficient hold 1 and 4, moving parameters, noise with the specials of a recursion. */
TEST (own_filter_savings_serum_process_both_matches_two_process_calls)
{
    using synthyboy::filters::Filter;
    using synthyboy::filters::FilterParams;
    long compared = 0;
    for (int type = 0; type < synthyboy::filters::kFilterTypeCount; ++type)
        for (int pan = 0; pan < 2; ++pan)
            for (int hold : { 1, 4 })
            {
                Filter a, b, c, d;
                for (Filter* f : { &a, &b, &c, &d })
                {
                    f->prepare (44100.0);
                    f->setType (type);
                }
                sbtest::Rng rng (0x5e70b0ull + static_cast<uint64_t> (type * 7 + pan * 3 + hold));
                for (int block = 0; block < 24; ++block)
                {
                    FilterParams p;
                    p.cutoff = 40.0f * std::pow (400.0f, static_cast<float> (rng.uniform()));
                    p.resonance = static_cast<float> (rng.uniform());
                    p.drive = static_cast<float> (rng.uniform());
                    p.var = static_cast<float> (rng.uniform());
                    p.blend = static_cast<float> (rng.uniform());
                    p.mix = block % 5 == 0 ? 0.6f : 1.0f;
                    p.coefHold = hold;
                    FilterParams q = p;
                    if (pan != 0)
                        q.cutoff = std::min (p.cutoff * 1.3f, 19000.0f);
                    a.setParams (p);
                    c.setParams (p);
                    b.setParams (q);
                    d.setParams (q);
                    std::vector<float> inL (32), inR (32), o1 (32), o2 (32), o3 (32), o4 (32);
                    fillInput (rng, inL, 0.5f, block % 7 == 0);
                    fillInput (rng, inR, 0.5f, block % 7 == 0);
                    Filter::processBoth (a, b, inL.data(), o1.data(), inR.data(), o2.data(), 32);
                    c.process (inL.data(), o3.data(), 32);
                    d.process (inR.data(), o4.data(), 32);
                    CHECK (sameBits (o1, o3));
                    CHECK (sameBits (o2, o4));
                    compared += 64;
                }
            }
    std::printf ("SAVINGS-SERUM-BOTH compared %ld samples\n", compared);
}

/** The pair kernel (its reference lanes, which run on every platform) takes lane 0's tangents for lane 1 when the arguments are bit-equal: against two plain process () calls, equal
    sides (pan 0), different cutoffs, and equal cutoffs with different var (the powers differ, the tangents of the cutoff do not). */
TEST (own_filter_savings_serum_pair_kernel_shared_tangents_match_two_process_calls)
{
    using synthyboy::filters::Filter;
    using synthyboy::filters::FilterParams;
    using synthyboy::filters::FilterType;
    const FilterType types[] = { FilterType::ScreamLP, FilterType::ScreamBP, FilterType::EMSLadder, FilterType::MGLadder, FilterType::LH, FilterType::LN, FilterType::PP, FilterType::NN,
                                 FilterType::CmbL, FilterType::FlgH, FilterType::PhsHL, FilterType::CmbHL, FilterType::RingMod, FilterType::RingModX2 };
    for (FilterType type : types)
        for (int mode = 0; mode < 3; ++mode)
            for (int hold : { 1, 4 })
            {
                Filter a, b, c, d;
                for (Filter* f : { &a, &b, &c, &d })
                {
                    f->prepare (44100.0);
                    f->setType (static_cast<int> (type));
                }
                sbtest::Rng rng (0x9a17ull + static_cast<uint64_t> (static_cast<int> (type) * 11 + mode * 3 + hold));
                for (int block = 0; block < 40; ++block)
                {
                    FilterParams p;
                    p.cutoff = 40.0f * std::pow (400.0f, static_cast<float> (rng.uniform()));
                    p.resonance = static_cast<float> (rng.uniform());
                    p.drive = static_cast<float> (rng.uniform());
                    p.var = static_cast<float> (rng.uniform());
                    p.blend = static_cast<float> (rng.uniform());
                    p.coefHold = hold;
                    FilterParams q = p;
                    if (mode == 1)
                        q.cutoff = std::min (p.cutoff * 1.3f, 19000.0f);
                    if (mode == 2)
                        q.var = 1.0f - p.var;
                    a.setParams (p);
                    c.setParams (p);
                    b.setParams (q);
                    d.setParams (q);
                    std::vector<float> inL (32), inR (32), o1 (32), o2 (32), o3 (32), o4 (32);
                    fillInput (rng, inL, 0.5f, block % 7 == 0);
                    fillInput (rng, inR, 0.5f, block % 7 == 0);
                    const bool did = Filter::processPairReference (a, b, inL.data(), o1.data(), inR.data(), o2.data(), 32);
                    c.process (inL.data(), o3.data(), 32);
                    d.process (inR.data(), o4.data(), 32);
                    CHECK (did);
                    CHECK (sameBits (o1, o3));
                    CHECK (sameBits (o2, o4));
                }
            }
}

// ---- Item 2: the Diode ramp at tier low (not bit identical: the 0.5 dB criterion of docs/plans/diode-ladder.md) ----

namespace
{
struct DiodeCase
{
    const char* name;
    float cutoffHz, res, driveDb;
};
// the two shipped Diode presets (Diode Saturated Bass, Diode Bite Lead)
const DiodeCase kDiodePresets[2] = { { "Diode Saturated Bass (420 Hz, res 20, drive 20)", 420.0f, 0.20f, 20.0f }, { "Diode Bite Lead (3100 Hz, res 45, drive 20)", 3100.0f, 0.45f, 20.0f } };

/** The magnitude of the response of `ir` on a 20 Hz grid from 50 Hz to 0.45 sr, in dB re the DC sum (the measure of test_own_diode_probe). */
std::vector<double> responseDb (const std::vector<float>& ir, double sr)
{
    std::vector<double> db;
    double dc = 0.0;
    for (float v : ir)
        dc += v;
    const double dcAbs = std::max (std::fabs (dc), 1.0e-30);
    for (double hz = 50.0; hz <= 0.45 * sr; hz += 20.0)
    {
        double re = 0.0, im = 0.0;
        const double omega = 2.0 * 3.14159265358979323846 * hz / sr;
        for (size_t i = 0; i < ir.size(); ++i)
        {
            re += ir[i] * std::cos (omega * static_cast<double> (i));
            im -= ir[i] * std::sin (omega * static_cast<double> (i));
        }
        db.push_back (20.0 * std::log10 (std::max (std::sqrt (re * re + im * im), 1.0e-30) / dcAbs));
    }
    return db;
}

struct RampResult
{
    double peakDiffDb = 0.0;  // |peak (ramp) - peak (exact)|
    double bandDiffDb = 0.0;  // largest |ramp - exact| of the response over the bins within 30 dB of the exact peak
    double rmsDb = 0.0;       // RMS of the sample difference re the RMS of the exact response
};

/** The impulse response (1e-3 impulse at the start, /1e-3) of the Diode while the parameters move by `step` per 32 sample period (a triangle of 8 periods up, 8 down): the exact
    generic piece loop (every 8 sample piece its own setup, as before) against the ramp (the setup at the two ends of the period, interpolated). `what`: 0 cutoff in semitones, 1
    cutoff in semitones and the resonance +-0.02 per period, 2 the drive +-0.5 dB per period, 3 style 1 with the blend moving +-0.04 (of 0 .. 2) per period. */
RampResult rampAgainstExact (const DiodeCase& c, int style, float step, int what)
{
    ClassicFilter exact, ramp;
    exact.prepare (44100.0);
    ramp.prepare (44100.0);
    const int periods = 128;
    std::vector<float> irExact, irRamp;
    auto paramsAt = [&] (int m)
    {
        const int phase = m % 16;
        const float tri = static_cast<float> (phase < 8 ? phase : 16 - phase) - 4.0f; // -4 .. +4 steps around the base
        ClassicParams p = makeParams (4, style, true, c.cutoffHz, c.res, c.driveDb, 0.0f);
        if (what == 0 || what == 1)
            p.cutoffHz = c.cutoffHz * std::pow (2.0f, step * tri / 12.0f);
        if (what == 1)
            p.res = clamp01 (c.res + 0.02f * tri);
        if (what == 2)
            p.driveDb = c.driveDb + 0.5f * tri;
        if (what == 3)
        {
            p.cutoffHz = c.cutoffHz * std::pow (2.0f, step * tri / 12.0f);
            p.blendRaw = 0.8f + 0.04f * tri;
        }
        return p;
    };
    for (int m = 0; m < periods; ++m)
    {
        const ClassicParams a = paramsAt (m - 1), b = paramsAt (m);
        std::vector<float> in (32, 0.0f), oE (32), oR (32);
        if (m == 0)
            in[0] = 1.0e-3f;
        for (int i = 0; i < 32; i += 8)
        {
            const float t = static_cast<float> (i + 8) / static_cast<float> (kControlPeriod);
            exact.block (in.data() + i, oE.data() + i, 8, ClassicFilter::pieceParams (a, b, t));
        }
        ramp.process (in.data(), oR.data(), 32, a, b, 0);
        for (float v : oE)
            irExact.push_back (v / 1.0e-3f);
        for (float v : oR)
            irRamp.push_back (v / 1.0e-3f);
    }
    RampResult r;
    double errSq = 0.0, refSq = 0.0;
    for (size_t i = 0; i < irExact.size(); ++i)
    {
        const double d = static_cast<double> (irRamp[i]) - static_cast<double> (irExact[i]);
        errSq += d * d;
        refSq += static_cast<double> (irExact[i]) * static_cast<double> (irExact[i]);
    }
    r.rmsDb = 10.0 * std::log10 (std::max (errSq, 1.0e-300) / std::max (refSq, 1.0e-300));
    const std::vector<double> dbE = responseDb (irExact, 44100.0), dbR = responseDb (irRamp, 44100.0);
    double peakE = -1000.0, peakR = -1000.0;
    for (size_t i = 0; i < dbE.size(); ++i)
    {
        peakE = std::max (peakE, dbE[i]);
        peakR = std::max (peakR, dbR[i]);
    }
    r.peakDiffDb = std::fabs (peakR - peakE);
    for (size_t i = 0; i < dbE.size(); ++i)
        if (dbE[i] > peakE - 30.0)
            r.bandDiffDb = std::max (r.bandDiffDb, std::fabs (dbR[i] - dbE[i]));
    return r;
}
} // namespace

/** The two shipped Diode presets at tier low with the parameters moving every control period: the ramp (setup at the two ends of the period, interpolated per 8 sample piece) against
    the generic piece loop it replaced. The criterion is that of docs/plans/diode-ladder.md: the peak of the response within 0.5 dB (and, stricter, every bin within 30 dB of the peak). Asserted up to 4 semitones of cutoff per control period; the table also lists 8 and 12. */
TEST (own_filter_savings_diode_ramp_stays_within_half_a_db_of_the_piece_loop)
{
    const float steps[] = { 0.5f, 2.0f, 4.0f, 8.0f, 12.0f };
    double worstPeak = 0.0, worstBand = 0.0, worstRms = -1000.0;
    for (const DiodeCase& c : kDiodePresets)
        for (int style = 0; style < 2; ++style)
            for (int what = 0; what < 4; ++what)
            {
                if (what == 3 && style == 0)
                    continue;
                for (float step : steps)
                {
                    if ((what == 2) && step != steps[0])
                        continue; // (the drive moves by a fixed 0.5 dB per period: one line)
                    const RampResult r = rampAgainstExact (c, style, step, what);
                    std::printf ("SAVINGS-DIODE-RAMP %s style %d %s %.1f st/period: peak %.3f dB, band %.3f dB, rms %.1f dB\n", c.name, style,
                                 what == 0 ? "cutoff" : (what == 1 ? "cutoff+res" : (what == 2 ? "drive" : "cutoff+blend")), static_cast<double> (step), r.peakDiffDb, r.bandDiffDb, r.rmsDb);
                    if (step <= 4.0f)
                    {
                        worstPeak = std::max (worstPeak, r.peakDiffDb);
                        worstBand = std::max (worstBand, r.bandDiffDb);
                        worstRms = std::max (worstRms, r.rmsDb);
                    }
                }
            }
    std::printf ("SAVINGS-DIODE-RAMP up to 4 semitones per period: worst peak difference %.3f dB, worst band difference %.3f dB, worst RMS %.1f dB\n", worstPeak, worstBand, worstRms);
    CHECK (worstPeak < 0.5);
    CHECK (worstBand < 0.5);
}
