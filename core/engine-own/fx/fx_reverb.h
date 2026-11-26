// SynthyBoy - own engine: the reverb with its six algorithms (docs/engine-spec/06-effects.md section 9). Original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

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

/** mixLaw () for a mix that stays the same over a block: the cosine and the sine once (the same values, the same arithmetic per sample). */
struct MixGains
{
    float dry, wet;
    explicit MixGains (float mix) noexcept
    {
        const float a = clamp01 (mix) * (kPif * 0.5f);
        dry = std::cos (a);
        wet = std::sin (a);
    }
    float apply (float d, float w) const noexcept { return d * dry + w * wet; }
};

/** Schroeder all-pass of document 06 9.1: `w = in - c z; store w; out = z + c w` with z read `delay` samples back. */
struct AllPassLine
{
    DelayLine line;
    void prepare (int maxSamples) { line.prepare (maxSamples); }
    void bind (float* buf, int pow2Size) noexcept { line.bind (buf, pow2Size); }
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

    Reverb() = default;
    // the delay lines point into `arena`: a copy would point into the arena of the other reverb (a move keeps the heap block)
    Reverb (const Reverb&) = delete;
    Reverb& operator= (const Reverb&) = delete;
    Reverb (Reverb&&) noexcept = default;
    Reverb& operator= (Reverb&&) noexcept = default;

    /** One block of storage holds every delay line: the two pre-delay lines (always used) and then, overlapping, the lines of the four mutually exclusive cores (activeGroup). */
    void prepare (double sr)
    {
        sampleRate = sr;
        scale = 1;
        while (static_cast<double> (scale) < sr / 44100.0)
            scale <<= 1;
        layoutLines (nullptr);   // measures: coreOffset and groupFloats
        const size_t most = std::max ({ groupFloats[0], groupFloats[1], groupFloats[2], groupFloats[3] });
        arena.assign (coreOffset + most, 0.0f);
        layoutLines (arena.data());
        activeGroup = -1;
        reset();
    }
    /** The heap bytes of the delay lines (one block) and the number of blocks. */
    size_t memoryBytes() const noexcept { return arena.size() * sizeof (float); }
    int bufferAllocCount() const noexcept { return arena.empty() ? 0 : 1; }

    void reset() noexcept
    {
        // the pre-delay lines, and the lines of the core that holds the arena (all of it while no core has run yet)
        pre[0].reset();
        pre[1].reset();
        const size_t from = coreOffset, count = activeGroup < 0 ? arena.size() - coreOffset : groupFloats[activeGroup];
        if (! arena.empty())
            std::fill (arena.begin() + static_cast<std::ptrdiff_t> (from), arena.begin() + static_cast<std::ptrdiff_t> (from + count), 0.0f);
        rewindLines();
        for (int b = 0; b < 2; ++b)
        {
            plDamp[b].reset(); plFeed[b] = 0.0f;
        }
        for (int c = 0; c < 2; ++c)
        {
            vLp[c] = 0.0f;
            for (auto& s : vDamp[c]) s.reset();
        }
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
    /** Advances the phase of the tail's modulation by n samples without processing (the module is skipped): the same steps as the sample loops take. */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        for (int s = 0; s < n; ++s)
        {
            phase += std::min (p.chorusRate, 16.0f) / sr;
            phase -= std::floor (phase);
        }
    }

    void process (float* l, float* r, int n) noexcept
    {
        const MixGains mg (p.mix);
        const float sr = static_cast<float> (sampleRate);
        const float pdTarget = std::clamp (p.predelay, 0.0f, 0.3f) * sr;
        if (pdCur < 0.0f)
            pdCur = pdTarget;
        // the read position moves a few percent of the error per chunk
        pdCur += (pdTarget - pdCur) * 0.04f;
        const bool useWidth = std::fabs (p.width - 1.0f) > 1.0e-4f;
        const int type = std::clamp (p.type, 0, 5);
        if (groupOf (type) != activeGroup)
            enterGroup (groupOf (type));
        // what a call needs of libm depends on the settings and the rate alone: made again only when one of them changed (the exact bits). The two shelf coefficients (a tan each) are
        // the cores' (types 1 .. 5) and are not made for the 16 line network; the input band pass (two exp) and the network's lengths, gains and shelves (17 pow / exp2, 4 exp) are kept
        float aHs = 0.0f, aLs = 0.0f;
        if (type != 0)
        {
            if (!shelfValid || !sameBits (p.highShelfHz, shelfKey[0]) || !sameBits (p.lowShelfHz, shelfKey[1]) || shelfRate != sampleRate)
            {
                shelfCoef[0] = TrapLp::coefficient (p.highShelfHz, sampleRate);
                shelfCoef[1] = TrapLp::coefficient (p.lowShelfHz, sampleRate);
                shelfKey[0] = p.highShelfHz;
                shelfKey[1] = p.lowShelfHz;
                shelfRate = sampleRate;
                shelfValid = true;
            }
            aHs = shelfCoef[0];
            aLs = shelfCoef[1];
        }
        if (!cutValid || !sameBits (p.highCut, cutKey[0]) || !sameBits (p.lowCut, cutKey[1]) || cutRate != sampleRate)
        {
            coefs.cHi = OnePole::coefficient (p.highCut, sampleRate);
            coefs.cLo = OnePole::coefficient (p.lowCut, sampleRate);
            cutKey[0] = p.highCut;
            cutKey[1] = p.lowCut;
            cutRate = sampleRate;
            cutValid = true;
        }
        if (type == 0)
        {
            const float key[7] = { p.size, p.decay, p.highShelfDb, p.lowShelfDb, p.highShelfHz, p.lowShelfHz, p.chorusAmount };
            if (!fdnValid || std::memcmp (key, fdnKey, sizeof key) != 0 || fdnRate != sampleRate)
            {
                prepareFdn();
                std::memcpy (fdnKey, key, sizeof key);
                fdnRate = sampleRate;
                fdnValid = true;
            }
        }
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
            l[i] = mg.apply (in[0], ol);
            r[i] = mg.apply (in[1], orr);
        }
    }

private:
    static constexpr float kLn1000 = 6.907755279f;
    // ---------------------------------------------------------------- the storage
    /** The core group of a type: 0 the 16 line network (type 0), 1 the plate, 2 the 8 line networks (Hall, Nitrous, Basin), 3 the vintage. */
    static int groupOf (int type) noexcept { return type == 0 ? 0 : (type == 1 ? 1 : (type == 3 ? 3 : 2)); }

    /** Gives every line its place in the arena (base null: nothing is bound, the sizes are measured). The lengths are the ones a line allocated for itself when each line had its own block. */
    void layoutLines (float* base) noexcept
    {
        const double sr = sampleRate;
        size_t cur = 0;
        const auto place = [&] (DelayLine& d, int maxSamples)
        {
            const int n = DelayLine::storageSize (maxSamples);
            if (base != nullptr)
                d.bind (base + cur, n);
            cur += static_cast<size_t> (n);
        };
        const int preMax = static_cast<int> (sr * 0.32);
        place (pre[0], preMax);
        place (pre[1], preMax);
        coreOffset = cur;

        // group 0: the 16 line network (no early reflections)
        cur = coreOffset;
        for (int k = 0; k < kLines; ++k)
        {
            place (line[k], static_cast<int> (sr / 44100.0 * 2.0 * 11400.0) + 4096);
            place (apLine[k].line, 1100 * scale);
        }
        groupFloats[0] = cur - coreOffset;

        // group 1: the plate
        cur = coreOffset;
        place (erLine, static_cast<int> (sr * 0.17) + 64);
        const float pr = static_cast<float> (sr / 29761.0) * 2.83f * 1.2f;
        for (int b = 0; b < 2; ++b)
        {
            place (plModAp[b].line, static_cast<int> (1000.0f * pr) + 64);
            place (plDel1[b], static_cast<int> (4500.0f * pr) + 64);
            place (plAp2[b].line, static_cast<int> (2700.0f * pr) + 64);
            place (plDel2[b], static_cast<int> (3800.0f * pr) + 64);
        }
        for (auto& a : plIn)
            place (a.line, static_cast<int> (400.0f * pr) + 64);
        groupFloats[1] = cur - coreOffset;

        // group 2: the 8 line networks (Hall, Nitrous, Basin)
        cur = coreOffset;
        place (erLine, static_cast<int> (sr * 0.17) + 64);
        for (int k = 0; k < 8; ++k)
        {
            place (net[k], static_cast<int> (sr * 0.5) + 64);
            place (netAp[k].line, static_cast<int> (sr * 0.02) + 64);
        }
        for (auto& a : inAp)
            place (a.line, static_cast<int> (sr * 0.02) + 64);
        groupFloats[2] = cur - coreOffset;

        // group 3: the vintage
        cur = coreOffset;
        place (erLine, static_cast<int> (sr * 0.17) + 64);
        const int vmax = static_cast<int> (1700.0 * sr / 44100.0 * 2.83 * 1.2) + 128;
        for (int c = 0; c < 2; ++c)
        {
            place (vInAp[c].line, static_cast<int> (200.0 * sr / 44100.0 * 2.83) + 64);
            for (int k = 0; k < 8; ++k)
                place (vComb[c][k], vmax);
            for (int k = 0; k < 4; ++k)
                place (vAp[c][k].line, static_cast<int> (600.0 * sr / 44100.0 * 2.83) + 64);
        }
        groupFloats[3] = cur - coreOffset;
    }

    /** The write position of every line of the cores back to 0 (the pre-delay lines are reset by reset ()). */
    void rewindLines() noexcept
    {
        for (auto& d : line) d.rewind();
        for (auto& d : apLine) d.line.rewind();
        for (auto& d : net) d.rewind();
        for (auto& d : netAp) d.line.rewind();
        for (auto& d : inAp) d.line.rewind();
        for (auto& d : plIn) d.line.rewind();
        for (int b = 0; b < 2; ++b)
        {
            plModAp[b].line.rewind(); plDel1[b].rewind(); plAp2[b].line.rewind(); plDel2[b].rewind();
        }
        for (int c = 0; c < 2; ++c)
        {
            vInAp[c].line.rewind();
            for (auto& d : vComb[c]) d.rewind();
            for (auto& d : vAp[c]) d.line.rewind();
        }
        erLine.rewind();
    }

    /** Another core takes the arena over (the type of the reverb changed to another group): it starts from silence, whatever the core before it left in the lines and in its filters. */
    void enterGroup (int g) noexcept
    {
        if (! arena.empty())
            std::fill (arena.begin() + static_cast<std::ptrdiff_t> (coreOffset), arena.begin() + static_cast<std::ptrdiff_t> (coreOffset + groupFloats[g]), 0.0f);
        rewindLines();
        switch (g)
        {
            case 0:
                for (auto& x : hsLp) x = 0.0f;
                for (auto& x : lsLp) x = 0.0f;
                break;
            case 1:
                for (int b = 0; b < 2; ++b)
                {
                    plDamp[b].reset();
                    plFeed[b] = 0.0f;
                }
                break;
            case 2:
                for (auto& s : netDamp) s.reset();
                for (int k = 0; k < 8; ++k)
                    netLen[k] = -1.0f;
                break;
            default:
                for (int c = 0; c < 2; ++c)
                {
                    vLp[c] = 0.0f;
                    for (auto& s : vDamp[c]) s.reset();
                }
                break;
        }
        activeGroup = g;
    }
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
        // the four groups share the four lane phases: group 0 and 1 take the cosine (group 1 negated), group 2 and 3 the sine (group 3 negated), of the same argument: 4 cosines and
        // 4 sines per sample instead of 16 calls (the same values: bit identical)
        float lcos[4], lsin[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            const float ph = kTwoPif * (phase + static_cast<float> (lane) / 16.0f);
            lcos[lane] = std::cos (ph);
            lsin[lane] = std::sin (ph);
        }
        for (int k = 0; k < kLines; ++k)
        {
            const int group = k / 4, lane = k % 4;
            const float mod = fdnDepth * (group == 0 ? lcos[lane] : (group == 1 ? -lcos[lane] : (group == 2 ? lsin[lane] : -lsin[lane])));
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
        // type 3 (vintage): everything that depends only on the parameters and the sample rate, once per call of process ()
        float vRatio = 1.0f, vInDelay = 0.0f, vInC = 0.3f, vApC = 0.3f, vLpCoef = 0.0f, vApDelay[4] = {}, vLen[2][8] = {}, vGain[2][8] = {};
    };
    static constexpr int kC[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }; // the comb lengths of the vintage type (samples at 44.1 kHz)
    static constexpr int kAp[4] = { 556, 441, 341, 225 };                             // its all-pass lengths
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
            c.vRatio = sr / 44100.0f * c.sizeScale;
            c.vInDelay = 142.0f * c.vRatio;
            c.vInC = 0.3f + 0.4f * p.diffA;
            c.vApC = 0.3f + 0.4f * p.diffB;
            c.vLpCoef = OnePole::coefficient (7000.0f, sampleRate);
            for (int k = 0; k < 4; ++k)
                c.vApDelay[k] = static_cast<float> (kAp[k]) * c.vRatio;
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 8; ++k)
                {
                    c.vLen[ch][k] = (static_cast<float> (kC[k]) + (ch == 1 ? 23.0f : 0.0f)) * c.vRatio;
                    c.vGain[ch][k] = gainFor (c.vLen[ch][k] / sr, c.T);
                }
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
                s += (((((i & j) & 1) + (((i & j) >> 1) & 1) + (((i & j) >> 2) & 1)) & 1) ? -y[j] : y[j]);
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
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            float x = vInAp[c].process (mono, cfg.vInDelay, cfg.vInC);
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                const float len = cfg.vLen[c][k];
                const float mod = cfg.depth * std::sin (kTwoPif * (phase + static_cast<float> (k) / 8.0f + 0.5f * static_cast<float> (c)));
                const float rd = vComb[c][k].readCubic (std::max (len + mod - 1.0f, 2.0f));
                const float damped = vDamp[c][k].process (rd, cfg.aH, cfg.aHs, cfg.aL, cfg.aLs);
                vComb[c][k].push (flushDenormal (x + damped * cfg.vGain[c][k]));
                sum += rd;
            }
            for (int k = 0; k < 4; ++k)
                sum = vAp[c][k].process (sum, cfg.vApDelay[k], cfg.vApC);
            vLp[c] += cfg.vLpCoef * (sum - vLp[c]);
            vLp[c] = flushDenormal (vLp[c]);
            out[c] = vLp[c] * 0.14f * kOutVintage;
        }
        outL = out[0];
        outR = out[1];
    }

public:
    // output scale calibration of the cores (tuned against the level vectors of document 06, 9.5)
    static inline float kCoreIn = 0.922f; // the mono input of the cores of 9.4 (calibrated on the live level of the former Vital engine, see QUESTIONS.md row 14)
    static inline float kOutFdn = 1.17f, kOutHall = 1.546f, kOutNitrous = 1.5f, kOutBasin = 1.1f, kOutPlate = 1.02f, kOutVintage = 1.08f;

private:
    Params p;
    CoreCfg cfg;
    struct { float cHi = 0.1f, cLo = 0.001f; } coefs;
    double sampleRate = 48000.0;
    int scale = 2;
    std::vector<float> arena;          // the storage of every delay line (see prepare ())
    size_t coreOffset = 0;             // the start of the lines of the cores: the two pre-delay lines come first
    size_t groupFloats[4] = {};        // the floats the lines of each core group use from coreOffset on
    int activeGroup = -1;              // the core group that holds the arena (-1: none yet)
    DelayLine line[kLines], pre[2];
    AllPassLine apLine[kLines];
    float hsLp[kLines] = {}, lsLp[kLines] = {}, inLp[4] = {};
    float fdnLen[kLines] = {}, fdnGain[kLines] = {}, fdnAHs = 1.0f, fdnALs = 0.0f, fdnCHs = 0.1f, fdnCLs = 0.01f, fdnDepth = 0.0f;
    // the settings the per call coefficients were made for (process ())
    static bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, 4) == 0; }
    float fdnKey[7] = {}, shelfKey[2] = {}, shelfCoef[2] = {}, cutKey[2] = {};
    double fdnRate = 0.0, shelfRate = 0.0, cutRate = 0.0;
    bool fdnValid = false, shelfValid = false, cutValid = false;
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
