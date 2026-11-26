// SynthyBoy - own engine tests: the CPU savings of the classic voice filters (docs/dsp-knowledge/filters.md, "Savings of 2026-11-26"). Every change that must not change a sample
// is compared with the code it replaced, bit for bit (the floats' bit patterns, not ==); the changes that may differ state their bound and are tested against it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
