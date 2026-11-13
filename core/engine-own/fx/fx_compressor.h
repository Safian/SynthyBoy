// SynthyBoy - own engine: the multiband compressor (document 06 section 6) with the look-ahead limiter and the peak compressor of Serum (document 13-new-keys 4.17, 4.48).
// Original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <array>
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
        for (auto& c : ap) for (auto& b : c) b.reset();
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
        for (int c = 0; c < 2; ++c)
        {
            lr[c][0].design (Biquad::lowpass, f1, 0.7071f, 0.0f, sampleRate, 0.499f);
            lr[c][1].copyCoefficients (lr[c][0]);
            lr[c][2].design (Biquad::highpass, f1, 0.7071f, 0.0f, sampleRate, 0.499f);
            lr[c][3].copyCoefficients (lr[c][2]);
            lr[c][4].design (Biquad::lowpass, f2, 0.7071f, 0.0f, sampleRate, 0.499f);
            lr[c][5].copyCoefficients (lr[c][4]);
            lr[c][6].design (Biquad::highpass, f2, 0.7071f, 0.0f, sampleRate, 0.499f);
            lr[c][7].copyCoefficients (lr[c][6]);
            // the low band passes an all pass of the upper crossover (LP + HP of it) so the three bands sum flat
            ap[c][0].copyCoefficients (lr[c][4]);
            ap[c][1].copyCoefficients (lr[c][4]);
            ap[c][2].copyCoefficients (lr[c][6]);
            ap[c][3].copyCoefficients (lr[c][6]);
        }
    }
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
        for (int i = 0; i < n; ++i)
        {
            const float in[2] = { l[i], r[i] };
            float sig[3][2], out[2] = { 0.0f, 0.0f };
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
                    lowS = ap[c][1].process (ap[c][0].process (lowS)) + ap[c][3].process (ap[c][2].process (lowS));
                }
                else if (p.bands == 3) { lowS = 0.0f; midS = x; }
                else midS = restS;
                sig[0][c] = lowS; sig[1][c] = midS; sig[2][c] = highS;
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
                // the lower (upward) envelope: the same in all the modes
                float& el = envLo[b];
                el = std::min ((x2 + el * (x2 > el ? Na : Nr)) / ((x2 > el ? Na : Nr) + 1.0f), thrL);
                el = flushDenormal (std::max (el, 1.0e-20f));
                const float gLo = powG (thrL / el, std::clamp (bd.lowerRatio, -1.0f, 1.0f) * 0.5f);
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
            float d[2] = { in[0], in[1] };
            if (limitMode)
                for (int c = 0; c < 2; ++c)
                {
                    dry[c].push (in[c]);
                    d[c] = dry[c].read (static_cast<float> (lookAhead));
                }
            l[i] = d[0] + (out[0] - d[0]) * p.mix;
            r[i] = d[1] + (out[1] - d[1]) * p.mix;
        }
    }

private:
    /** The look-ahead brickwall limiter of one band: min filter of the required gain over the look-ahead, release, a box filter of the look-ahead length. */
    struct Limiter
    {
        std::vector<float> xl, xr, req, g1;
        int pos = 0, cap = 8;
        float relGain = 1.0f, prevL = 0.0f, prevR = 0.0f, prev2L = 0.0f, prev2R = 0.0f;
        void prepare (int c)
        {
            cap = c;
            xl.assign (static_cast<size_t> (c), 0.0f);
            xr.assign (static_cast<size_t> (c), 0.0f);
            req.assign (static_cast<size_t> (c), 1.0f);
            g1.assign (static_cast<size_t> (c), 1.0f);
            reset();
        }
        void reset() noexcept
        {
            std::fill (xl.begin(), xl.end(), 0.0f);
            std::fill (xr.begin(), xr.end(), 0.0f);
            std::fill (req.begin(), req.end(), 1.0f);
            std::fill (g1.begin(), g1.end(), 1.0f);
            pos = 0;
            relGain = 1.0f;
            prevL = prevR = prev2L = prev2R = 0.0f;
        }
        void process (float inL, float inR, float ceilAmp, float relCoef, int la, float& outL, float& outR) noexcept
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
            const size_t di = static_cast<size_t> ((pos - la + cap * 4) % cap);
            outL = xl[di] * gSm;
            outR = xr[di] * gSm;
        }
    };

    Params p;
    double sampleRate = 48000.0;
    int lookAhead = 72;
    Biquad lr[2][8], ap[2][4];
    float envHi[3] = {}, envLo[3] = { 1.0f, 1.0f, 1.0f }, serumPeak[3] = {}, serumGr[3] = {};
    Limiter lim[3];
    DelayLine dry[2];
};
} // namespace own
} // namespace synthyboy
