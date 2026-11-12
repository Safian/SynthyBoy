// SynthyBoy - own engine: the reverb with its six algorithms (docs/engine-spec/06-effects.md section 9). Original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "fx_dsp.h"

namespace synthyboy
{
namespace own
{
/** The equal power dry / wet crossfade of the effects. */
inline float mixLaw (float dry, float wet, float mix) noexcept
{
    const float a = clamp01 (mix) * (kPif * 0.5f);
    return dry * std::cos (a) + wet * std::sin (a);
}

/** Schroeder all-pass of document 06 9.1: `w = in - c z; store w; out = z + c w` with z read `delay` samples back. */
struct AllPassLine
{
    DelayLine line;
    void prepare (int maxSamples) { line.prepare (maxSamples); }
    void reset() noexcept { line.reset(); }
    float process (float x, float delay, float c) noexcept
    {
        const float z = line.readCubic (std::max (delay - 1.0f, 1.0f));
        const float v = x - c * z;
        line.push (flushDenormal (v));
        return z + c * v;
    }
};

/** The damping stage of the cores of 9.4: a zero delay one pole low pass of the stage input gives the high shelf `lp + A_h (x - lp)`, then the low shelf
    `y - lpl (1 - 10^(low_gain / 20))` with the same kind of low pass of y. */
struct DampStage
{
    TrapLp hs, ls;
    void reset() noexcept { hs.reset(); ls.reset(); }
    float process (float x, float aH, float aHs, float aL, float aLs) noexcept
    {
        const float lp = hs.process (x, aHs);
        const float y = lp + aH * (x - lp);
        return y - aL * ls.process (y, aLs);
    }
};

class Reverb
{
public:
    static constexpr int kLines = 16;
    struct Params
    {
        float mix = 0.25f, size = 0.5f, decay = 1.5f, predelay = 0.0f, lowCut = 20.0f, highCut = 8000.0f, highShelfDb = -1.0f, highShelfHz = 1400.0f,
              lowShelfDb = 0.0f, lowShelfHz = 100.0f, chorusAmount = 0.2f, chorusRate = 0.25f, width = 1.0f, diffA = 0.7f, diffB = 0.6f, erSize = 0.5f, feedback = 0.5f;
        int type = 0, room = 2;
    };

    void prepare (double sr)
    {
        sampleRate = sr;
        scale = 1;
        while (static_cast<double> (scale) < sr / 44100.0)
            scale <<= 1;
        for (int k = 0; k < kLines; ++k)
        {
            line[k].prepare (static_cast<int> (sr / 44100.0 * 2.0 * 11400.0) + 4096);
            apLine[k].prepare (1100 * scale);
        }
        for (int k = 0; k < 8; ++k)
        {
            net[k].prepare (static_cast<int> (sr * 0.5) + 64);
            netAp[k].prepare (static_cast<int> (sr * 0.02) + 64);
        }
        for (auto& a : inAp)
            a.prepare (static_cast<int> (sr * 0.02) + 64);
        const float pr = static_cast<float> (sr / 29761.0) * 2.83f * 1.2f;
        for (int b = 0; b < 2; ++b)
        {
            plModAp[b].prepare (static_cast<int> (1000.0f * pr) + 64);
            plDel1[b].prepare (static_cast<int> (4500.0f * pr) + 64);
            plAp2[b].prepare (static_cast<int> (2700.0f * pr) + 64);
            plDel2[b].prepare (static_cast<int> (3800.0f * pr) + 64);
        }
        for (auto& a : plIn)
            a.prepare (static_cast<int> (400.0f * pr) + 64);
        const int vmax = static_cast<int> (1700.0 * sr / 44100.0 * 2.83 * 1.2) + 128;
        for (int c = 0; c < 2; ++c)
        {
            vInAp[c].prepare (static_cast<int> (200.0 * sr / 44100.0 * 2.83) + 64);
            for (int k = 0; k < 8; ++k)
                vComb[c][k].prepare (vmax);
            for (int k = 0; k < 4; ++k)
                vAp[c][k].prepare (static_cast<int> (600.0 * sr / 44100.0 * 2.83) + 64);
        }
        erLine.prepare (static_cast<int> (sr * 0.17) + 64);
        pre[0].prepare (static_cast<int> (sr * 0.32));
        pre[1].prepare (static_cast<int> (sr * 0.32));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (auto& d : apLine) d.reset();
        for (auto& d : pre) d.reset();
        for (auto& d : net) d.reset();
        for (auto& d : netAp) d.reset();
        for (auto& d : inAp) d.reset();
        for (auto& d : plIn) d.reset();
        for (int b = 0; b < 2; ++b)
        {
            plModAp[b].reset(); plDel1[b].reset(); plAp2[b].reset(); plDel2[b].reset(); plDamp[b].reset(); plFeed[b] = 0.0f;
        }
        for (int c = 0; c < 2; ++c)
        {
            vInAp[c].reset();
            for (auto& d : vComb[c]) d.reset();
            for (auto& d : vAp[c]) d.reset();
            vLp[c] = 0.0f;
            for (auto& s : vDamp[c]) s.reset();
        }
        erLine.reset();
        for (auto& x : hsLp) x = 0.0f;
        for (auto& x : lsLp) x = 0.0f;
        for (auto& x : inLp) x = 0.0f;
        for (auto& s : netDamp) s.reset();
        for (int k = 0; k < 8; ++k)
            netLen[k] = -1.0f;
        phase = 0.0f;
        pdCur = -1.0f;
    }
    void set (const Params& np) noexcept { p = np; }

    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float pdTarget = std::clamp (p.predelay, 0.0f, 0.3f) * sr;
        if (pdCur < 0.0f)
            pdCur = pdTarget;
        // the read position moves a few percent of the error per chunk
        pdCur += (pdTarget - pdCur) * 0.04f;
        const bool useWidth = std::fabs (p.width - 1.0f) > 1.0e-4f;
        const int type = std::clamp (p.type, 0, 5);
        const float aHs = TrapLp::coefficient (p.highShelfHz, sampleRate), aLs = TrapLp::coefficient (p.lowShelfHz, sampleRate);
        coefs.cHi = OnePole::coefficient (p.highCut, sampleRate);
        coefs.cLo = OnePole::coefficient (p.lowCut, sampleRate);
        if (type == 0)
            prepareFdn();
        else
            prepareCore (type, aHs, aLs);
        const float pd = pdCur;
        for (int i = 0; i < n; ++i)
        {
            float in[2] = { l[i], r[i] }, bp[2];
            for (int c = 0; c < 2; ++c)
            {
                inLp[c] += coefs.cHi * (in[c] - inLp[c]);
                inLp[c + 2] += coefs.cLo * (in[c] - inLp[c + 2]);
                inLp[c] = flushDenormal (inLp[c]);
                inLp[c + 2] = flushDenormal (inLp[c + 2]);
                bp[c] = inLp[c + 2] - inLp[c]; // the polarity inverted band pass
            }
            float wl, wr;
            if (type == 0)
                fdnSample (bp[0] * 0.25f, bp[1] * 0.25f, wl, wr);
            else
            {
                const float mono = 0.5f * (bp[0] + bp[1]) * kCoreIn;
                coreSample (type, mono, wl, wr);
            }
            float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr);
            if (useWidth)
                sd *= p.width;
            const float xl = m + sd, xr = m - sd;
            pre[0].push (xl);
            pre[1].push (xr);
            const float ol = pd > 0.5f ? pre[0].readCubic (pd) : xl, orr = pd > 0.5f ? pre[1].readCubic (pd) : xr;
            l[i] = mixLaw (in[0], ol, p.mix);
            r[i] = mixLaw (in[1], orr, p.mix);
        }
    }

private:
    static constexpr float kLn1000 = 6.907755279f;
    // ---------------------------------------------------------------- type 0: the 16 line network
    void prepareFdn() noexcept
    {
        static const float kD[kLines] = { 6753.2f, 9278.4f, 7704.5f, 11328.5f, 9701.12f, 5512.5f, 8480.45f, 5638.65f,
                                          3120.73f, 3429.5f, 3626.37f, 7713.52f, 4521.54f, 6518.97f, 5265.56f, 5630.25f };
        const float rate = static_cast<float> (sampleRate) / 44100.0f;
        const float sizeMult = std::exp2 (4.0f * p.size - 3.0f);
        const float T = std::clamp (p.decay, 0.1f, 100.0f);
        for (int k = 0; k < kLines; ++k)
        {
            fdnLen[k] = sizeMult * kD[k] * rate;
            fdnGain[k] = std::pow (0.001f, sizeMult * kD[k] / (T * 44100.0f));
        }
        fdnAHs = std::pow (10.0f, std::clamp (p.highShelfDb, -24.0f, 0.0f) * 0.05f);
        fdnALs = 1.0f - std::pow (10.0f, std::clamp (p.lowShelfDb, -24.0f, 0.0f) * 0.05f);
        fdnCHs = OnePole::coefficient (p.highShelfHz, sampleRate);
        fdnCLs = OnePole::coefficient (p.lowShelfHz, sampleRate);
        fdnDepth = p.chorusAmount * p.chorusAmount * 2500.0f * rate;
    }
    void fdnSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        static const float kA[kLines] = { 1001, 799, 933, 876, 895, 807, 907, 853, 957, 1019, 711, 567, 833, 779, 663, 997 };
        const float sr = static_cast<float> (sampleRate);
        phase += std::min (p.chorusRate, 16.0f) / sr;
        phase -= std::floor (phase);
        float v[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            const int group = k / 4, lane = k % 4;
            const float ph = kTwoPif * (phase + static_cast<float> (lane) / 16.0f);
            const float mod = fdnDepth * (group == 0 ? std::cos (ph) : (group == 1 ? -std::cos (ph) : (group == 2 ? std::sin (ph) : -std::sin (ph))));
            const float lenK = std::max (fdnLen[k] + mod, 32.0f);
            const float rd = line[k].readLagrange4 (std::max (lenK - 1.0f, 1.0f));
            const float aRead = apLine[k].line.readCubic (std::max (kA[k] * static_cast<float> (scale) - 1.0f, 1.0f));
            const float w = rd - 0.6f * aRead;
            apLine[k].line.push (flushDenormal ((k < 8 ? inL : inR) + w));
            v[k] = aRead + 0.6f * w;
        }
        float S = 0.0f, C[4] = {}, R[4] = {};
        for (int g = 0; g < 4; ++g)
            for (int j = 0; j < 4; ++j)
            {
                S += v[g * 4 + j];
                C[j] += v[g * 4 + j];
                R[g] += v[g * 4 + j];
            }
        float side[2] = { 0.0f, 0.0f };
        for (int k = 0; k < kLines; ++k)
        {
            const int g = k / 4, j = k % 4;
            float y = v[k] + 0.25f * S - 0.5f * C[j] - 0.5f * R[g];
            hsLp[k] += fdnCHs * (y - hsLp[k]);
            y = hsLp[k] + fdnAHs * (y - hsLp[k]);
            lsLp[k] += fdnCLs * (y - lsLp[k]);
            y -= fdnALs * lsLp[k];
            hsLp[k] = flushDenormal (hsLp[k]);
            lsLp[k] = flushDenormal (lsLp[k]);
            side[k < 8 ? 0 : 1] += y; // the output taps the line BEFORE the decay gain (document 06, 9.1): the level does not depend on decay or size
            line[k].push (flushDenormal (y * fdnGain[k]));
        }
        outL = side[0] * kOutFdn;
        outR = side[1] * kOutFdn;
    }

    // ---------------------------------------------------------------- types 1 .. 5
    struct CoreCfg
    {
        float sizeScale = 1.0f, T = 1.0f, aH = 1.0f, aL = 0.0f, aHs = 0.5f, aLs = 0.5f, erScale = 1.0f, erLevel = 0.3f, outScale = 1.0f;
        float apIn = 0.5f, apLine = 0.3f, depth = 0.0f;
    };
    static float gainFor (float roundTrip, float T) noexcept { return std::min (std::exp (-kLn1000 * roundTrip / T), 0.9996f); }

    void prepareCore (int type, float aHsC, float aLsC) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        CoreCfg& c = cfg;
        c.sizeScale = std::exp2 ((p.size - 0.5f) * 3.0f);
        float T = std::max (std::clamp (p.decay, 0.1f, 100.0f), 0.05f);
        if (type == 4 || type == 5)
            T = std::max (T * std::exp2 ((p.feedback - 0.5f) * 4.0f), 0.05f);
        c.T = T;
        const float ahRaw = std::pow (10.0f, std::clamp (p.highShelfDb, -24.0f, 0.0f) * 0.05f);
        static const float kRoomK[5] = { 1.45f, 0.40f, 1.0f, 1.0f, 0.80f };
        float ah = ahRaw;
        if (type == 5)
            ah = 1.0f - (1.0f - ahRaw) * 0.35f;
        else if (type == 4)
            ah = 1.0f - (1.0f - ahRaw) * kRoomK[std::clamp (p.room, 0, 4)];
        c.aH = ah;
        c.aL = 1.0f - std::pow (10.0f, std::clamp (p.lowShelfDb, -24.0f, 0.0f) * 0.05f);
        c.aHs = aHsC;
        c.aLs = aLsC;
        c.erScale = std::exp2 ((p.erSize - 0.5f) * 3.0f);
        static const float kEr[6] = { 0.30f, 0.30f, 0.45f, 0.25f, 0.0f, 0.25f }; // by type (Nitrous by room below)
        static const float kErRoom[5] = { 0.15f, 0.60f, 0.40f, 0.40f, 0.45f };
        c.erLevel = type == 4 ? kErRoom[std::clamp (p.room, 0, 4)] : kEr[type];
        const float ca = clamp01 (p.chorusAmount);
        const float chorus = ca * ca; // the stored value of the squared knob
        if (type == 2 || type == 4 || type == 5)
        {
            c.apIn = (type == 5 ? 0.55f : 0.30f) + 0.40f * p.diffA;
            c.apLine = 0.2f + 0.5f * p.diffB;
            const float depthMs = type == 2 ? 1.2f : (type == 4 ? 0.6f : 2.4f);
            c.depth = chorus * depthMs * 0.001f * sr;
            c.outScale = type == 2 ? kOutHall : (type == 4 ? kOutNitrous : kOutBasin);
            coreNetTargets (type);
        }
        else if (type == 1)
        {
            c.depth = chorus * 16.0f * sr / 29761.0f;
        }
        else
        {
            c.depth = chorus * 0.8f * 0.001f * sr;
        }
    }

    void coreNetTargets (int type) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        static const float kHall[8] = { 41.7f, 53.9f, 61.3f, 71.1f, 79.7f, 89.9f, 101.3f, 113.9f };
        static const float kBasin[8] = { 17.3f, 21.1f, 23.9f, 27.7f, 31.3f, 34.9f, 39.1f, 43.7f };
        static const float kApHall[8] = { 4.3f, 5.1f, 6.7f, 7.9f, 9.1f, 10.9f, 12.7f, 14.3f };
        static const float kApBasin[8] = { 2.3f, 3.1f, 3.7f, 4.3f, 5.3f, 6.1f, 7.1f, 8.3f };
        static const float kInHall[4] = { 5.3f, 7.9f, 11.3f, 13.7f };
        static const float kInBasin[4] = { 2.9f, 4.1f, 5.7f, 7.3f };
        static const float kRooms[5][3] = { { 24, 32, 18 }, { 6.3f, 4.1f, 3.0f }, { 9.0f, 5.5f, 3.5f }, { 7.5f, 6.5f, 3.8f }, { 5, 5, 5 } };
        float base[8];
        for (int k = 0; k < 8; ++k)
            apDelay[k] = (type == 5 ? kApBasin[k] : (type == 2 ? kApHall[k] : 0.5f * kApHall[k])) * 0.001f * sr;
        for (int k = 0; k < 4; ++k)
            inDelay[k] = (type == 5 ? kInBasin[k] : kInHall[k]) * 0.001f * sr;
        if (type == 4)
        {
            const float* rm = kRooms[std::clamp (p.room, 0, 4)];
            const float a = rm[0], b = rm[1], c = rm[2];
            const float path[8] = { 2 * a, 2 * b, 2 * c, 2 * std::sqrt (a * a + b * b), 2 * std::sqrt (a * a + c * c), 2 * std::sqrt (b * b + c * c),
                                    2 * std::sqrt (a * a + b * b + c * c), a + b + c };
            for (int k = 0; k < 8; ++k)
                base[k] = path[k] / 343.0f * (1.0f + 0.0113f * static_cast<float> (k));
        }
        else
            for (int k = 0; k < 8; ++k)
                base[k] = (type == 5 ? kBasin[k] : kHall[k]) * 0.001f;
        for (int k = 0; k < 8; ++k)
        {
            netTarget[k] = std::clamp (base[k] * cfg.sizeScale * sr, 4.0f, 0.5f * sr);
            if (netLen[k] < 0.0f)
                netLen[k] = netTarget[k];
            netGain[k] = gainFor ((netTarget[k] + apDelay[k]) / sr, cfg.T);
        }
    }

    void coreSample (int type, float mono, float& outL, float& outR) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        // early reflections of every type
        erLine.push (mono);
        static const float kL[6] = { 7.3f, 13.1f, 19.7f, 29.3f, 37.9f, 47.1f }, kR[6] = { 9.1f, 15.7f, 23.3f, 31.9f, 41.3f, 53.3f };
        static const float kG[6] = { 0.8f, -0.7f, 0.6f, -0.5f, 0.4f, -0.35f };
        float erL = 0.0f, erR = 0.0f;
        for (int t = 0; t < 6; ++t)
        {
            erL += kG[t] * erLine.readCubic (std::max (kL[t] * 0.001f * sr * cfg.erScale, 1.0f));
            erR -= kG[t] * erLine.readCubic (std::max (kR[t] * 0.001f * sr * cfg.erScale, 1.0f));
        }
        const float er = 0.5f * cfg.erLevel;
        phase += std::min(p.chorusRate, 16.0f) / sr;
        phase -= std::floor (phase);
        float wl = 0.0f, wr = 0.0f;
        if (type == 2 || type == 4 || type == 5)
            netSample (type, mono, wl, wr);
        else if (type == 1)
            plateSample (mono, wl, wr);
        else
            vintageSample (mono, wl, wr);
        outL = wl + er * erL;
        outR = wr + er * erR;
    }

    void netSample (int type, float mono, float& outL, float& outR) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        float x = mono;
        for (int i = 0; i < 4; ++i)
            x = inAp[i].process (x, inDelay[i], cfg.apIn);
        static const float kA[8] = { 1, 1, -1, -1, 1, 1, -1, -1 }, kB[8] = { 1, -1, 1, -1, -1, 1, -1, 1 };
        float y[8];
        for (int k = 0; k < 8; ++k)
        {
            netLen[k] += (netTarget[k] - netLen[k]) * 0.002f;
            const float d = netLen[k] + cfg.depth * std::sin (kTwoPif * (phase + static_cast<float> (k) / 8.0f));
            y[k] = net[k].readCubic (std::clamp (d - 1.0f, 2.0f, 0.5f * sr));
        }
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < 8; ++k)
        {
            l += kA[k] * y[k];
            r += kB[k] * y[k];
        }
        // the Walsh Hadamard feedback (orthonormal)
        float h[8];
        for (int i = 0; i < 8; ++i)
        {
            float s = 0.0f;
            for (int j = 0; j < 8; ++j)
                s += (__builtin_popcount (static_cast<unsigned> (i & j)) & 1 ? -y[j] : y[j]);
            h[i] = s * 0.35355339f;
        }
        for (int k = 0; k < 8; ++k)
        {
            const float sigma = k < 4 ? 1.0f : -1.0f, ck = sigma * ((k & 1) ? -1.0f : 1.0f) * 0.35355339f;
            float v = h[k] + ck * x;
            v = netAp[k].process (v, apDelay[k], cfg.apLine);
            v = netDamp[k].process (v, cfg.aH, cfg.aHs, cfg.aL, cfg.aLs);
            net[k].push (flushDenormal (v * netGain[k]));
        }
        outL = 0.5f * l * cfg.outScale;
        outR = 0.5f * r * cfg.outScale;
        (void) type;
    }

    void plateSample (float mono, float& outL, float& outR) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float sc = std::max (sr / 29761.0f * cfg.sizeScale, 0.0f);
        auto S = [&] (float v) { return std::max (v * sc, 2.0f); };
        float x = 0.5f * mono;
        static const float kInD[4] = { 142, 107, 379, 277 };
        for (int i = 0; i < 4; ++i)
            x = plIn[i].process (x, std::max (kInD[i] * sc, 2.0f), i < 2 ? 0.45f + 0.40f * p.diffA : 0.35f + 0.40f * p.diffA);
        static const float kMod[2] = { 672, 908 }, kD1[2] = { 4453, 4217 }, kAp2[2] = { 1800, 2656 }, kD2[2] = { 3720, 3163 };
        const float p0 = (672.0f + 4453.0f + 1800.0f + 3720.0f) / 29761.0f * cfg.sizeScale, p1 = (908.0f + 4217.0f + 2656.0f + 3163.0f) / 29761.0f * cfg.sizeScale;
        const float g = gainFor (0.5f * (p0 + p1), cfg.T);
        float outBranch[2];
        const float apMod = -(0.4f + 0.45f * p.diffB), apFix = 0.75f * (0.4f + 0.45f * p.diffB);
        for (int b = 0; b < 2; ++b)
        {
            const float in = x + plFeed[1 - b];
            const float modD = S (kMod[b]) + cfg.depth * std::sin (kTwoPif * (phase + 0.5f * static_cast<float> (b)));
            float v = plModAp[b].process (in, modD, apMod);
            plDel1[b].push (flushDenormal (v));
            v = plDel1[b].readCubic (std::max (S (kD1[b]) - 1.0f, 1.0f));
            v = plDamp[b].process (v, cfg.aH, cfg.aHs, cfg.aL, cfg.aLs) * g;
            v = plAp2[b].process (v, S (kAp2[b]), apFix);
            plDel2[b].push (flushDenormal (v));
            outBranch[b] = plDel2[b].readCubic (std::max (S (kD2[b]) - 1.0f, 1.0f));
        }
        plFeed[0] = outBranch[0];
        plFeed[1] = outBranch[1];
        auto tapDel = [&] (DelayLine& d, float pos) { return d.readCubic (std::max (pos * sc, 1.0f)); };
        auto tapAp = [&] (AllPassLine& a, float pos) { return a.line.readCubic (std::max (pos * sc, 1.0f)); };
        const float l = tapDel (plDel1[0], 266) + tapDel (plDel1[0], 2974) - tapAp (plModAp[0], 606.5f) + tapDel (plDel2[0], 1996)
                      - tapDel (plDel1[1], 1990) - tapAp (plAp2[1], 187) - tapDel (plDel2[1], 1066);
        const float r = tapDel (plDel1[1], 353) + tapDel (plDel1[1], 3627) - tapAp (plAp2[1], 1228) + tapDel (plDel2[1], 2673)
                      - tapDel (plDel1[0], 2111) - tapAp (plAp2[0], 335) - tapDel (plDel2[0], 121);
        outL = l * 0.75f * kOutPlate;
        outR = r * 0.75f * kOutPlate;
    }

    void vintageSample (float mono, float& outL, float& outR) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        static const int kC[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int kAp[4] = { 556, 441, 341, 225 };
        const float ratio = sr / 44100.0f * cfg.sizeScale;
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            float x = vInAp[c].process (mono, 142.0f * ratio, 0.3f + 0.4f * p.diffA);
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                const float len = (static_cast<float> (kC[k]) + (c == 1 ? 23.0f : 0.0f)) * ratio;
                const float mod = cfg.depth * std::sin (kTwoPif * (phase + static_cast<float> (k) / 8.0f + 0.5f * static_cast<float> (c)));
                const float rd = vComb[c][k].readCubic (std::max (len + mod - 1.0f, 2.0f));
                const float gk = gainFor (len / sr, cfg.T);
                const float damped = vDamp[c][k].process (rd, cfg.aH, cfg.aHs, cfg.aL, cfg.aLs);
                vComb[c][k].push (flushDenormal (x + damped * gk));
                sum += rd;
            }
            for (int k = 0; k < 4; ++k)
                sum = vAp[c][k].process (sum, static_cast<float> (kAp[k]) * ratio, 0.3f + 0.4f * p.diffB);
            vLp[c] += OnePole::coefficient (7000.0f, sampleRate) * (sum - vLp[c]);
            vLp[c] = flushDenormal (vLp[c]);
            out[c] = vLp[c] * 0.14f * kOutVintage;
        }
        outL = out[0];
        outR = out[1];
    }

public:
    // output scale calibration of the cores (tuned against the level vectors of document 06, 9.5)
    static inline float kCoreIn = 0.922f; // the mono input of the cores of 9.4 (calibrated on the live level of engine-vital, see QUESTIONS.md row 14)
    static inline float kOutFdn = 1.17f, kOutHall = 1.546f, kOutNitrous = 1.5f, kOutBasin = 1.1f, kOutPlate = 1.02f, kOutVintage = 1.08f;

private:
    Params p;
    CoreCfg cfg;
    struct { float cHi = 0.1f, cLo = 0.001f; } coefs;
    double sampleRate = 48000.0;
    int scale = 2;
    DelayLine line[kLines], pre[2];
    AllPassLine apLine[kLines];
    float hsLp[kLines] = {}, lsLp[kLines] = {}, inLp[4] = {};
    float fdnLen[kLines] = {}, fdnGain[kLines] = {}, fdnAHs = 1.0f, fdnALs = 0.0f, fdnCHs = 0.1f, fdnCLs = 0.01f, fdnDepth = 0.0f;
    float phase = 0.0f, pdCur = -1.0f;
    // network cores
    DelayLine net[8], erLine;
    AllPassLine netAp[8], inAp[4];
    DampStage netDamp[8];
    float netLen[8] = {}, netTarget[8] = {}, netGain[8] = {}, apDelay[8] = {}, inDelay[4] = {};
    // plate
    AllPassLine plIn[4], plModAp[2], plAp2[2];
    DelayLine plDel1[2], plDel2[2];
    DampStage plDamp[2];
    float plFeed[2] = {};
    // vintage
    AllPassLine vInAp[2], vAp[2][4];
    DelayLine vComb[2][8];
    DampStage vDamp[2][8];
    float vLp[2] = {};
};
} // namespace own
} // namespace synthyboy
