// SynthyBoy - own engine: the multiband compressor (document 06 section 6) with the look-ahead limiter and the peak compressor of Serum (document 13-new-keys 4.17, 4.48).
// Original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fx_dsp.h"

namespace synthyboy
{
namespace own
{
class Compressor
{
public:
    bool fast = false; // tier low: the per sample powers by dsp/fastmath.h (relative error about 1e-6)
    struct Band { float lowerDb = -40, upperDb = -20, lowerRatio = 0, upperRatio = 0.5f, gainDb = 0; };
    /** mode 0 Compress, 1 Limit (look-ahead brickwall at the upper threshold), 2 Serum (peak compressor). */
    struct Params { float mix = 1.0f, attack = 0.5f, release = 0.5f, crossLow = 120.0f, crossHigh = 2500.0f; int bands = 0, mode = 0; Band band[3]; };
    static constexpr float kLookAheadSeconds = 0.0015f;

    /** The look-ahead in samples (the latency of the limit mode). */
    static int lookAheadFor (double sr) noexcept { return std::max (1, static_cast<int> (std::lround (kLookAheadSeconds * sr))); }

    void prepare (double sr)
    {
        sampleRate = sr;
        lookAhead = lookAheadFor (sr);
        const int cap = lookAhead + 8;
        for (auto& b : lim)
            b.prepare (cap);
        for (int c = 0; c < 2; ++c)
            dry[c].prepare (cap);
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : lr) for (auto& b : c) b.reset();
        for (auto& b : lowAp) b.reset();
        for (auto& e : envHi) e = 0.0f;
        for (auto& e : envLo) e = 1.0f;
        for (auto& b : lim) b.reset();
        for (auto& d : dry) d.reset();
        for (auto& x : serumPeak) x = 0.0f;
        for (auto& x : serumGr) x = 0.0f;
    }
    void set (const Params& np) noexcept
    {
        p = np;
        // one split (two bands) for the Low / High band modes, two splits for Multiband
        const float f1 = p.bands == 2 ? p.crossHigh : p.crossLow, f2 = p.crossHigh;
        // designed once, for the left channel; the right channel copies the coefficients (the states stay apart)
        lr[0][0].design (Biquad::lowpass, f1, 0.7071f, 0.0f, sampleRate, 0.499f);
        lr[0][1].copyCoefficients (lr[0][0]);
        lr[0][2].design (Biquad::highpass, f1, 0.7071f, 0.0f, sampleRate, 0.499f);
        lr[0][3].copyCoefficients (lr[0][2]);
        lr[0][4].design (Biquad::lowpass, f2, 0.7071f, 0.0f, sampleRate, 0.499f);
        lr[0][5].copyCoefficients (lr[0][4]);
        lr[0][6].design (Biquad::highpass, f2, 0.7071f, 0.0f, sampleRate, 0.499f);
        lr[0][7].copyCoefficients (lr[0][6]);
        // the low band passes an all pass of the upper crossover so the three bands sum flat: one second order all pass with the poles of the crossover filters. It replaces
        // LP (f2)^2 + HP (f2)^2 (four biquads), which is this all pass for the exact Butterworth Q of 1 / sqrt (2); the Q of 0.7071 here makes the two differ by about -92 dB
        // (CPU review item 2, 2026-11-26; docs/MPC.md "Sound differences")
        lowAp[0].design (Biquad::allpass, f2, 0.7071f, 0.0f, sampleRate, 0.499f);
        for (int k = 0; k < 8; ++k)
            lr[1][k].copyCoefficients (lr[0][k]);
        lowAp[1].copyCoefficients (lowAp[0]);
    }
    /** Test only: run the limiter as it was before the CPU review item M1. */
    void setReferenceLimiter (bool on) noexcept { for (auto& b : lim) b.reference = on; }
    int latencySamples() const noexcept { return p.mode == 1 ? lookAhead : 0; }

    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        static const float kAtk[3] = { 2.8f, 1.4f, 0.7f }, kRel[3] = { 40.0f, 28.0f, 15.0f };
        int use[3] = { 0, 1, 2 };
        if (p.bands == 1) { use[0] = 0; use[1] = 1; use[2] = 1; }
        if (p.bands == 2) { use[0] = 1; use[1] = 2; use[2] = 2; }
        if (p.bands == 3) { use[0] = use[1] = use[2] = 1; }
        const bool limitMode = p.mode == 1, serumMode = p.mode == 2;
        const float expoA = std::exp (8.0f * p.attack - 4.0f), expoR = std::exp (8.0f * p.release - 4.0f);
        const float serumAtkMs = 0.1f * std::pow (10.0f, 4.0f * p.attack), serumRelMs = 0.1f * std::pow (10.0f, 4.0f * p.release);
        const float serumAtkTau = 0.37f * serumAtkMs * serumAtkMs / (serumAtkMs + 4.0f);
        const float serumAtkCo = 1.0f - std::exp (-1.0f / std::max (serumAtkTau * 0.001f * sr, 1.0e-3f)), serumRelCo = 1.0f - std::exp (-1.0f / std::max (serumRelMs * 0.001f * sr, 1.0f));
        const float peakDecay = std::exp (-1.0f / (0.001f * sr));
        // per band (the same for every sample of the call): the thresholds, the make-up gain, the ceiling and the time constants of the two envelopes
        struct BandConst { float thrUdb, thrLdb, thrU, thrL, makeup, ceilAmp, Na, Nr; };
        BandConst bc[3];
        for (int b = 0; b < 3; ++b)
        {
            const int tb = use[b];
            const Band& bd = p.band[tb];
            BandConst& k = bc[b];
            k.thrUdb = std::clamp (bd.upperDb, -120.0f, 12.0f);
            k.thrLdb = std::clamp (bd.lowerDb, -120.0f, 12.0f);
            k.thrU = std::pow (10.0f, k.thrUdb * 0.1f);
            k.thrL = std::pow (10.0f, k.thrLdb * 0.1f);
            k.makeup = std::pow (10.0f, bd.gainDb * 0.05f);
            k.ceilAmp = limitMode ? std::pow (10.0f, k.thrUdb * 0.05f) : 1.0f;
            k.Na = std::max (5.0f, kAtk[tb] * sr * 0.001f * expoA);
            k.Nr = std::max (5.0f, kRel[tb] * sr * 0.001f * expoR);
        }
        auto powG = [this] (float x, float y) noexcept { return fast ? (y == 0.0f ? 1.0f : fastmath::pow (x, y)) : std::pow (x, y); }; // tier low: exp2 / log2 forms
        if (limitMode)
            for (auto& b : lim)
                b.resumNext(); // the running sum of the limiter's box filter is made exactly once per call (at most one control period of samples between two exact sums)
        for (int i = 0; i < n; ++i)
        {
            const float in[2] = { l[i], r[i] };
            float sig[3][2], out[2] = { 0.0f, 0.0f };
            if (p.bands == 3)
            {
                for (int c = 0; c < 2; ++c)
                {
                    sig[0][c] = 0.0f;
                    sig[1][c] = in[c];
                    sig[2][c] = 0.0f;
                }
            }
            else
            {
                for (int c = 0; c < 2; ++c)
                {
                    const float x = in[c];
                    float lowS = lr[c][1].process (lr[c][0].process (x));
                    float restS = lr[c][3].process (lr[c][2].process (x));
                    float midS = 0.0f, highS = 0.0f;
                    if (p.bands == 0)
                    {
                        midS = lr[c][5].process (lr[c][4].process (restS));
                        highS = lr[c][7].process (lr[c][6].process (restS));
                        lowS = lowAp[c].process (lowS);
                    }
                    else midS = restS;
                    sig[0][c] = lowS; sig[1][c] = midS; sig[2][c] = highS;
                }
            }
            for (int b = 0; b < 3; ++b)
            {
                const bool skip = (p.bands == 1 && b == 2) || (p.bands == 2 && b == 2) || (p.bands == 3 && b != 1);
                if (skip)
                    continue;
                const int tb = use[b];
                const Band& bd = p.band[tb];
                const BandConst& k = bc[b];
                const float thrUdb = k.thrUdb, thrU = k.thrU, thrL = k.thrL;
                const float x2 = 0.5f * (sig[b][0] * sig[b][0] + sig[b][1] * sig[b][1]);
                const float Na = k.Na, Nr = k.Nr;
                // the lower (upward) envelope: the same in all the modes (skip pow when ratio is 0)
                float gLo = 1.0f;
                if (bd.lowerRatio != 0.0f)
                {
                    float& el = envLo[b];
                    el = std::min ((x2 + el * (x2 > el ? Na : Nr)) / ((x2 > el ? Na : Nr) + 1.0f), thrL);
                    el = flushDenormal (std::max (el, 1.0e-20f));
                    gLo = powG (thrL / el, std::clamp (bd.lowerRatio, -1.0f, 1.0f) * 0.5f);
                }
                float gHi = 1.0f;
                if (!limitMode && !serumMode)
                {
                    float& eh = envHi[b];
                    const float N = x2 > eh ? Na : Nr;
                    eh = std::max ((x2 + eh * N) / (N + 1.0f), thrU);
                    eh = flushDenormal (eh);
                    gHi = powG (thrU / eh, clamp01 (bd.upperRatio) * 0.5f);
                }
                else if (serumMode)
                {
                    // the peak of the stereo pair, held with a decay of 1 ms; a hard knee; the gain reduction in dB is smoothed (attack / release time constants)
                    const float inst = std::max (std::fabs (sig[b][0]), std::fabs (sig[b][1]));
                    float& pk = serumPeak[b];
                    pk = std::max (inst, pk * peakDecay);
                    pk = flushDenormal (pk);
                    const float levelDb = 20.0f * std::log10 (std::max (pk, 1.0e-9f));
                    const float target = clamp01 (bd.upperRatio) * std::max (levelDb - thrUdb, 0.0f);
                    float& gr = serumGr[b];
                    gr += (target - gr) * (target > gr ? serumAtkCo : serumRelCo);
                    gr = flushDenormal (gr);
                    gHi = fast ? fastmath::pow10 (-gr * 0.05f) : std::pow (10.0f, -gr * 0.05f);
                }
                const float gain = std::clamp (gHi * gLo, 0.0f, 32.0f);
                const float makeup = k.makeup;
                if (limitMode)
                {
                    // the look-ahead limiter at the ceiling thrU (the downward stage is replaced), stereo linked; the makeup comes after the limiter
                    const float ceilAmp = k.ceilAmp;
                    const float relN = k.Nr;
                    float lo[2];
                    lim[b].process (sig[b][0] * gLo, sig[b][1] * gLo, ceilAmp, 1.0f / relN, lookAhead, lo[0], lo[1]);
                    out[0] += lo[0] * makeup;
                    out[1] += lo[1] * makeup;
                }
                else
                {
                    out[0] += sig[b][0] * gain * makeup;
                    out[1] += sig[b][1] * gain * makeup;
                }
            }
            float d[2] = { p.bands == 3 ? in[0] : sig[0][0] + sig[1][0] + sig[2][0],
                           p.bands == 3 ? in[1] : sig[0][1] + sig[1][1] + sig[2][1] };
            if (limitMode)
                for (int c = 0; c < 2; ++c)
                {
                    dry[c].push (d[c]);
                    d[c] = dry[c].read (static_cast<float> (lookAhead));
                }
            l[i] = d[0] + (out[0] - d[0]) * p.mix;
            r[i] = d[1] + (out[1] - d[1]) * p.mix;
        }
    }

private:
    friend struct CompressorOptTest;
    /** The look-ahead brickwall limiter of one band: min filter of the required gain over the look-ahead, release, a box filter of the look-ahead length.
        The min filter is a monotone deque of the entries below 1 (an empty deque is the minimum 1.0f), the box filter a running sum in double that is made exactly again at least every
        kSumPeriod samples and while the window may hold a gain outside kMinGain .. 1. The box filter is not bit identical to the serial float sum it replaces (CPU review item 1, 2026-11-26): the
        gain differs by at most 1.4e-6 relative (measured, test_own_compressor_opt.cpp); the min filter, the release and the required gain stay bit identical, and nothing feeds the gain back. */
    struct Limiter
    {
        std::vector<float> xl, xr, req, g1;
        std::vector<float> dqVal;
        std::vector<uint32_t> dqTick;
        int pos = 0, cap = 8;
        float relGain = 1.0f, prevL = 0.0f, prevR = 0.0f, prev2L = 0.0f, prev2R = 0.0f;
        void prepare (int c)
        {
            cap = c;
            xl.assign (static_cast<size_t> (c), 0.0f);
            xr.assign (static_cast<size_t> (c), 0.0f);
            req.assign (static_cast<size_t> (c), 1.0f);
            g1.assign (static_cast<size_t> (c), 1.0f);
            dqVal.assign (static_cast<size_t> (c), 1.0f);
            dqTick.assign (static_cast<size_t> (c), 0u);
            reset();
        }
        void reset() noexcept
        {
            std::fill (xl.begin(), xl.end(), 0.0f);
            std::fill (xr.begin(), xr.end(), 0.0f);
            std::fill (req.begin(), req.end(), 1.0f);
            std::fill (g1.begin(), g1.end(), 1.0f);
            pos = 0;
            oddLeft = 0;
            relGain = 1.0f;
            prevL = prevR = prev2L = prev2R = 0.0f;
            dqValid = false;
            sumValid = false;
            sumOddLeft = 0;
            onesRun = cap; // the ring holds only ones
            lastGain = 1.0f;
        }
        /** True: the code as it was before the CPU review item M1 (a `%` per index), kept for the comparison test only. */
        bool reference = false;
        /** Samples for which the window may still hold a value the min filter does not treat like std::min (a NaN, an infinity, a subnormal or a negative zero). */
        int oddLeft = 0;
        /** The next sample sums the window again exactly (Compressor::process calls it at the start of every call: at most one control period of samples between two exact sums). */
        void resumNext() noexcept { sumValid = false; }
        /** The box filter output (the gain) of the last sample; the test compares it with the reference. */
        float lastGain = 1.0f;
        static constexpr int kSumPeriod = 32;
        static constexpr float kMinGain = 1.0f / 65536.0f; // a window that holds a smaller released gain is summed serially in float, as the reference does

        /** The old process (): the same arithmetic in the same order, kept as the reference and for a look-ahead that is not below the capacity. */
        void processReference (float inL, float inR, float ceilAmp, float relCoef, int la, float& outL, float& outR) noexcept
        {
            // the required gain of this sample: the peak of the stereo pair with an estimate between the samples
            const float mL = -0.125f * prev2L + 0.75f * prevL + 0.375f * inL, mR = -0.125f * prev2R + 0.75f * prevR + 0.375f * inR;
            const float pk = std::max ({ std::fabs (inL), std::fabs (inR), std::fabs (mL), std::fabs (mR) });
            prev2L = prevL; prev2R = prevR; prevL = inL; prevR = inR;
            const float r = pk > ceilAmp ? ceilAmp / pk : 1.0f;
            pos = (pos + 1) % cap;
            xl[static_cast<size_t> (pos)] = inL;
            xr[static_cast<size_t> (pos)] = inR;
            req[static_cast<size_t> (pos)] = r;
            // min over the look-ahead window (this sample and the la before it)
            float m = 1.0f;
            for (int k = 0; k <= la; ++k)
                m = std::min (m, req[static_cast<size_t> ((pos - k + cap * 4) % cap)]);
            relGain = std::min (m, relGain + (1.0f - relGain) * relCoef);
            g1[static_cast<size_t> (pos)] = relGain;
            float sum = 0.0f;
            for (int k = 0; k <= la; ++k)
                sum += g1[static_cast<size_t> ((pos - k + cap * 4) % cap)];
            const float gSm = sum / static_cast<float> (la + 1);
            lastGain = gSm;
            const size_t di = static_cast<size_t> ((pos - la + cap * 4) % cap);
            outL = xl[di] * gSm;
            outR = xr[di] * gSm;
        }

        /** True for a value that is not a normal number and not +0: the min filter (flush to zero, the sign of a zero) could differ from std::min on it. */
        static bool oddValue (float v) noexcept
        {
            uint32_t u;
            std::memcpy (&u, &v, 4);
            const uint32_t e = (u >> 23) & 0xffu;
            return (e == 0xffu || e == 0u) && u != 0u;
        }

        void process (float inL, float inR, float ceilAmp, float relCoef, int la, float& outL, float& outR) noexcept
        {
            // the old code wrapped with `% cap` (a library call on 32 bit ARM); for la >= cap it wrapped several times, which only the reference reproduces (never reached: cap = la + 8)
            if (reference || la >= cap || la < 0)
            {
                oddLeft = cap; // conservative if the fast path runs again later
                dqValid = false;
                sumValid = false;
                sumOddLeft = cap;
                onesRun = 0;
                processReference (inL, inR, ceilAmp, relCoef, la, outL, outR);
                return;
            }
            const float mL = -0.125f * prev2L + 0.75f * prevL + 0.375f * inL, mR = -0.125f * prev2R + 0.75f * prevR + 0.375f * inR;
            const float pk = std::max ({ std::fabs (inL), std::fabs (inR), std::fabs (mL), std::fabs (mR) });
            prev2L = prevL; prev2R = prevR; prevL = inL; prevR = inR;
            const float r = pk > ceilAmp ? ceilAmp / pk : 1.0f;
            pos = pos + 1 == cap ? 0 : pos + 1;
            ++tick;
            xl[static_cast<size_t> (pos)] = inL;
            xr[static_cast<size_t> (pos)] = inR;
            req[static_cast<size_t> (pos)] = r;
            if (oddValue (r))
                oddLeft = cap;
            const int W = la + 1;
            // the window (this sample and the la before it) is at most two contiguous runs of the ring: [lo1, pos] and, when it wraps, [cap - wrapped, cap - 1]
            const int lo1 = pos - la < 0 ? 0 : pos - la;
            const int wrapped = pos - la < 0 ? la - pos : 0;
            float m = 1.0f;
            if (oddLeft > 0)
            {
                --oddLeft;
                dqValid = false;
                for (int j = pos; j >= lo1; --j)
                    m = std::min (m, req[static_cast<size_t> (j)]);
                for (int j = cap - 1; j >= cap - wrapped; --j)
                    m = std::min (m, req[static_cast<size_t> (j)]);
            }
            else
            {
                // every value of the window is a normal number or +0: the minimum is the same bits in any order, so the deque of the entries below 1 gives it (1.0f when it is empty)
                if (!dqValid || dqLa != la)
                    rebuildDeque (la);
                else
                {
                    while (dqCount > 0 && tick - dqTick[static_cast<size_t> (dqHead)] >= static_cast<uint32_t> (W))
                        popFront();
                    if (r < 1.0f)
                        pushBack (r);
                }
                if (dqCount > 0)
                    m = dqVal[static_cast<size_t> (dqHead)];
            }
            relGain = std::min (m, relGain + (1.0f - relGain) * relCoef);
            const float g = relGain;
            const bool oddG = !(g >= kMinGain && g <= 1.0f); // outside the range where a running sum keeps its relative accuracy (a gain 96 dB down or more, negative, NaN, infinity)
            const int dLeave = pos - W < 0 ? pos - W + cap : pos - W; // the entry that leaves the window with this sample (read before the write: for la = cap - 1 it is the same cell)
            const float leaving = g1[static_cast<size_t> (dLeave)];
            g1[static_cast<size_t> (pos)] = g;
            onesRun = g == 1.0f ? std::min (onesRun + 1, cap) : 0;
            if (oddG)
                sumOddLeft = cap;
            float gSm;
            if (sumOddLeft > 0)
            {
                // a gain outside kMinGain .. 1 (tiny, negative, NaN, infinity) may be in the window: the serial float sum of the reference
                --sumOddLeft;
                sumValid = false;
                float sum = 0.0f;
                for (int j = pos; j >= lo1; --j)
                    sum += g1[static_cast<size_t> (j)];
                for (int j = cap - 1; j >= cap - wrapped; --j)
                    sum += g1[static_cast<size_t> (j)];
                gSm = sum / static_cast<float> (W);
            }
            else if (onesRun >= W)
            {
                // the whole window is exactly 1.0f: the sum and the gain are exact
                runSum = static_cast<double> (W);
                sumValid = true;
                sinceSum = 0;
                sumLa = la;
                gSm = 1.0f;
            }
            else
            {
                if (!sumValid || sumLa != la || sinceSum >= kSumPeriod)
                {
                    double s = 0.0;
                    for (int j = pos; j >= lo1; --j)
                        s += static_cast<double> (g1[static_cast<size_t> (j)]);
                    for (int j = cap - 1; j >= cap - wrapped; --j)
                        s += static_cast<double> (g1[static_cast<size_t> (j)]);
                    runSum = s;
                    sumValid = true;
                    sinceSum = 0;
                    sumLa = la;
                }
                else
                {
                    runSum += static_cast<double> (g) - static_cast<double> (leaving);
                    ++sinceSum;
                }
                gSm = static_cast<float> (runSum) / static_cast<float> (W);
            }
            lastGain = gSm;
            const int di = pos - la < 0 ? pos - la + cap : pos - la;
            outL = xl[static_cast<size_t> (di)] * gSm;
            outR = xr[static_cast<size_t> (di)] * gSm;
        }

        // the monotone deque (a ring of cap entries, the sample number and the value; the values rise from the front to the back) and the running sum
        uint32_t tick = 0;
        int dqHead = 0, dqCount = 0, dqLa = -1;
        bool dqValid = false;
        double runSum = 0.0;
        bool sumValid = false;
        int sumLa = -1, sinceSum = 0, sumOddLeft = 0, onesRun = 0;
        void popFront() noexcept { dqHead = dqHead + 1 == cap ? 0 : dqHead + 1; --dqCount; }
        void pushBack (float v) noexcept
        {
            while (dqCount > 0)
            {
                const int back = dqHead + dqCount - 1 >= cap ? dqHead + dqCount - 1 - cap : dqHead + dqCount - 1;
                if (dqVal[static_cast<size_t> (back)] < v)
                    break;
                --dqCount;
            }
            const int at = dqHead + dqCount >= cap ? dqHead + dqCount - cap : dqHead + dqCount;
            dqVal[static_cast<size_t> (at)] = v;
            dqTick[static_cast<size_t> (at)] = tick;
            ++dqCount;
        }
        /** The deque of the window that ends at the current sample, oldest entry first (every value of the window is a normal number or +0). */
        void rebuildDeque (int la) noexcept
        {
            dqHead = 0;
            dqCount = 0;
            const uint32_t now = tick;
            for (int age = la; age >= 0; --age)
            {
                const int j = pos - age < 0 ? pos - age + cap : pos - age;
                const float v = req[static_cast<size_t> (j)];
                if (v < 1.0f)
                {
                    tick = now - static_cast<uint32_t> (age);
                    pushBack (v);
                }
            }
            tick = now;
            dqValid = true;
            dqLa = la;
        }
    };

    Params p;
    double sampleRate = 48000.0;
    int lookAhead = 72;
    Biquad lr[2][8], lowAp[2];
    float envHi[3] = {}, envLo[3] = { 1.0f, 1.0f, 1.0f }, serumPeak[3] = {}, serumGr[3] = {};
    Limiter lim[3];
    DelayLine dry[2];
};
} // namespace own
} // namespace synthyboy
