// SynthyBoy - own engine: wavetable oscillator with unison and warps (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "../dsp/util.h"
#include "morph.h"
#include "warp.h"
#include "wavetable_bank.h"

namespace synthyboy
{
namespace own
{
/** The effective (modulated) values of an oscillator at the end of a control period. */
struct OscTick
{
    const Wavetable* wt = nullptr;
    float semis = 69.0f;                  // pitch in MIDI semitones (fractional) without the unison offsets
    float frame = 0.0f, frameSpread = 0.0f; // 0 .. 1, -1 .. 1
    float level = 0.0f, pan = 0.0f, panRight = 0.0f; // amplitude (linear), -1 .. 1 (left lane, right lane of the pan)
    int voices = 1;
    float detuneSemis = 0.0f;             // offset of the outermost voice (range * detune)
    float curve = 0.0f;                   // detune curve (power scale parameter)
    int mode = 0;                         // unison mode
    int stack = 0, stackRepeat = 0;       // stack style
    float blendShape = 0.0f;
    float spread = 1.0f, blend = 1.0f;    // 0 .. 1
    int law = 0;                          // unison law: 0 Vital, 1 Serum (document 01, 9.6)
    int warp[2] = { 0, 0 };
    int warpSrc[2] = { -1, -1 };          // index in the cross list of the modulator of each slot (set by update ())
    float warpAmount[2] = { 0, 0 }, warpPhase[2] = { 0, 0 }, warpSpread[2] = { 0, 0 };
    float startPhase = 0.0f;              // only used when a note starts
    bool active = false;
    const float* remap = nullptr;         // the drawn remap curve of the oscillator for the warps Remap 1 .. 4 (257 points) or null (identity)
    int morphType = 0;                    // oscN.morph.type
    float morphAmount = 0.0f, morphSpread = 0.0f; // 0 .. 1, -1 .. 1
    bool oversample = false;              // run 4x oversampled even without a warp (ultra quality)
    bool smooth = false;                  // oscN.smooth_interp: blend the neighbouring frames (off: the frame steps, with a short cross-fade)
    bool lowCost = false;                 // tier low: a warp runs 2x oversampled with a 32 tap decimator instead of 4x with 128 taps, the spectral morph is rebuilt every 512 samples
};

/** True if every field of the two ticks is equal (floats compared as values: a NaN is never equal, which only costs the shortcut). */
inline bool sameOscTick (const OscTick& a, const OscTick& b) noexcept
{
    return a.wt == b.wt && a.semis == b.semis && a.frame == b.frame && a.frameSpread == b.frameSpread && a.level == b.level && a.pan == b.pan && a.panRight == b.panRight
        && a.voices == b.voices && a.detuneSemis == b.detuneSemis && a.curve == b.curve && a.mode == b.mode && a.stack == b.stack && a.stackRepeat == b.stackRepeat
        && a.blendShape == b.blendShape && a.spread == b.spread && a.blend == b.blend && a.law == b.law && a.warp[0] == b.warp[0] && a.warp[1] == b.warp[1]
        && a.warpSrc[0] == b.warpSrc[0] && a.warpSrc[1] == b.warpSrc[1] && a.warpAmount[0] == b.warpAmount[0] && a.warpAmount[1] == b.warpAmount[1]
        && a.warpPhase[0] == b.warpPhase[0] && a.warpPhase[1] == b.warpPhase[1] && a.warpSpread[0] == b.warpSpread[0] && a.warpSpread[1] == b.warpSpread[1]
        && a.startPhase == b.startPhase && a.active == b.active && a.remap == b.remap && a.morphType == b.morphType && a.morphAmount == b.morphAmount
        && a.morphSpread == b.morphSpread && a.oversample == b.oversample && a.smooth == b.smooth && a.lowCost == b.lowCost;
}

/** Hermite interpolation of the four samples x[-1], x[0], x[1], x[2] at x[0] + f. */
inline float hermite (const float* x, float f) noexcept
{
    const float c1 = 0.5f * (x[1] - x[-1]);
    const float c2 = x[-1] - 2.5f * x[0] + 2.0f * x[1] - 0.5f * x[2];
    const float c3 = 0.5f * (x[2] - x[-1]) + 1.5f * (x[0] - x[1]);
    return ((c3 * f + c2) * f + c1) * f + x[0];
}

class WtOsc
{
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kOversample = 4, kTaps = 128;
    static constexpr int kOversampleLow = 2, kTapsLow = 32;

    /** Linear phase low pass of the 4x decimator (Kaiser window), computed once. */
    static const float* decimatorTaps() noexcept
    {
        static const struct Taps
        {
            float c[kTaps];
            Taps()
            {
                auto bessel0 = [] (double x)
                {
                    double sum = 1.0, term = 1.0;
                    for (int k = 1; k < 30; ++k)
                    {
                        term *= (x / (2.0 * k)) * (x / (2.0 * k));
                        sum += term;
                    }
                    return sum;
                };
                const double cutoff = 0.105, beta = 7.5; // in cycles per sample of the 4x signal
                double total = 0.0;
                double tmp[kTaps];
                for (int i = 0; i < kTaps; ++i)
                {
                    const double x = i - 0.5 * (kTaps - 1);
                    const double sinc = std::fabs (x) < 1.0e-9 ? 2.0 * cutoff : std::sin (2.0 * 3.14159265358979323846 * cutoff * x) / (3.14159265358979323846 * x);
                    const double r = 2.0 * i / (kTaps - 1) - 1.0;
                    tmp[i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
                    total += tmp[i];
                }
                for (int i = 0; i < kTaps; ++i)
                    c[i] = static_cast<float> (tmp[i] / total);
            }
        } taps;
        return taps.c;
    }

    /** The decimator of the low cost mode: 2x oversampling, 32 taps (the same Kaiser design, the band edge of the 4x one at 0.42 of the host rate). */
    static const float* decimatorTaps2() noexcept
    {
        static const struct Taps
        {
            float c[kTapsLow];
            Taps()
            {
                auto bessel0 = [] (double x)
                {
                    double sum = 1.0, term = 1.0;
                    for (int k = 1; k < 30; ++k)
                    {
                        term *= (x / (2.0 * k)) * (x / (2.0 * k));
                        sum += term;
                    }
                    return sum;
                };
                const double cutoff = 0.21, beta = 7.5; // in cycles per sample of the 2x signal
                double total = 0.0;
                double tmp[kTapsLow];
                for (int i = 0; i < kTapsLow; ++i)
                {
                    const double x = i - 0.5 * (kTapsLow - 1);
                    const double sinc = std::fabs (x) < 1.0e-9 ? 2.0 * cutoff : std::sin (2.0 * 3.14159265358979323846 * cutoff * x) / (3.14159265358979323846 * x);
                    const double r = 2.0 * i / (kTapsLow - 1) - 1.0;
                    tmp[i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
                    total += tmp[i];
                }
                for (int i = 0; i < kTapsLow; ++i)
                    c[i] = static_cast<float> (tmp[i] / total);
            }
        } taps;
        return taps.c;
    }

    void start (float startPhase, float randomPhase, Rng& rng) noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const float p = startPhase + randomPhase * rng.uni();
            phase[i] = static_cast<uint32_t> (static_cast<double> (p - std::floor (p)) * 4294967296.0);
        }
        havePrev = false;
        haveLast = false;
        morphAge = kMorphRebuild;
        for (auto& f : frameIdx)
            f = -1;
        for (auto& fs : filterState)
            for (auto& x : fs)
                x = 0.0f;
        for (auto& h : history)
            for (auto& x : h)
                x = 0.0f;
    }

    /** Computes the per voice frequencies, levels and pans of the period that ends now (call once per control period). The layout follows
        docs/engine-spec/01-voice-management.md 9: voices come in left / right pairs (an odd count has a single centre voice), the pair i is detuned by
        P (t, curve) of the outermost range, sharp and flat alternate, amplitudes by the blend law with power normalisation, then the stereo spread. */
    void update (const OscTick& t, double sampleRate, Rng& rng) noexcept
    {
        prevCount = curCount;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            prevInc[i] = curInc[i];
            prevGainL[i] = curGainL[i];
            prevGainR[i] = curGainR[i];
        }
        prevFrame = curFrame;
        // the same inputs as the previous period (the usual case of a held note): the voices below would be computed again to the same values
        if (haveLast && lastRate == sampleRate && sameOscTick (t, lastT))
            return;
        lastT = t;
        lastRate = sampleRate;
        haveLast = true;
        tick = t;
        for (int s = 0; s < 2; ++s)
        {
            tick.warpSrc[s] = warpCrossSource (t.warp[s]);
            tick.warp[s] = warpFamily (t.warp[s]);
        }
        const int U = std::clamp (t.voices, 1, kMaxVoices);
        const bool odd = (U & 1) != 0;
        const int pairs = (U + 1) / 2;
        const double nyq = 0.5 * sampleRate;
        // blend law (Vital): amplitudes of the centre pair and of the others, normalised to unit power
        float ac = 1.0f, ad = 0.0f;
        if (U >= 3)
        {
            const float b = clamp01 (t.blend), s = std::clamp (t.blendShape, -1.0f, 1.0f);
            const float c0 = 1.0f - 0.6f * b, d0 = 0.6f * (1.0f - (1.0f - b) * (1.0f - b));
            const float c1 = c0 * (1.0f - 0.5f * s), d1 = d0 * (1.0f + 0.5f * s);
            const float norm = 1.0f / std::sqrt (std::max (c1 * c1 + static_cast<float> (pairs - 1) * d1 * d1, 1.0e-12f));
            ac = c1 * norm;
            ad = d1 * norm;
        }
        // stereo spread: own channel a, other channel b (equal power)
        const float w = clamp01 (t.spread);
        float sa = std::sin (kPif * 0.5f * (0.5f + 0.5f * w)), sb = std::sin (kPif * 0.5f * (1.5f + 0.5f * w));
        if (t.law == 1)
        {
            // the Serum law (document 01, 9.6): a measured gain per voice count, centre and outer gains on the blend axis, the own / other channel gains of the spread
            static const float kG[17] = { 1.0f, 1.0f, 0.3200f, 0.4384f, 0.4260f, 0.4139f, 0.4021f, 0.3907f, 0.3796f, 0.3689f, 0.3584f, 0.3482f, 0.3384f, 0.3288f, 0.3194f, 0.3104f, 0.3016f };
            static const float kCentre[11] = { 1.0000f, 0.9945f, 0.9778f, 0.9501f, 0.9112f, 0.8613f, 0.8003f, 0.7281f, 0.6449f, 0.5506f, 0.4451f };
            static const float kOuter[11] = { 0.0445f, 0.0573f, 0.0948f, 0.1543f, 0.2314f, 0.3200f, 0.4119f, 0.4975f, 0.5653f, 0.6018f, 0.5921f };
            static const float kSpreadAt[7] = { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f };
            static const float kOwn[7] = { 1.0000f, 1.1286f, 1.3315f, 1.6955f, 2.0925f, 2.3461f, 2.5217f };
            static const float kOther[7] = { 1.0000f, 0.8766f, 0.7010f, 0.4346f, 0.2011f, 0.0764f, 0.0000f };
            auto interp = [] (const float* tab, const float* xs, int n, float x)
            {
                int i = 0;
                while (i + 2 < n && x > xs[i + 1])
                    ++i;
                const float f = clamp01 ((x - xs[i]) / (xs[i + 1] - xs[i]));
                return tab[i] + (tab[i + 1] - tab[i]) * f;
            };
            static const float kBlendAt[11] = { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f };
            const float b = clamp01 (t.blend), w = clamp01 (t.spread);
            const float own = interp (kOwn, kSpreadAt, 7, w), other = interp (kOther, kSpreadAt, 7, w), total = own + other;
            const float g = kG[U];
            sa = own / total;
            sb = other / total;
            if (U == 1)
            {
                ac = 1.0f;
                ad = 0.0f;
            }
            else
            {
                ac = g * (U == 2 ? 1.0f : interp (kCentre, kBlendAt, 11, b)) * (odd ? 1.0f : total);
                ad = g * interp (kOuter, kBlendAt, 11, b) * total;
            }
        }
        // oscillator pan, constant power with 0 dB at the centre
        const float pan = std::clamp (t.pan, -1.0f, 1.0f);
        const float panRt = std::clamp (t.panRight, -1.0f, 1.0f);
        const float panL = 1.41421356f * std::sin (kPif * 0.25f * (1.0f - pan)), panR = 1.41421356f * std::sin (kPif * 0.25f * (1.0f + panRt));
        const int bump = odd ? 0 : 1;
        const float divisor = static_cast<float> (std::max (1, U - 1));
        int n = 0;
        auto addVoice = [&] (float semis, float amp, float gainL, float gainR, float framePos) noexcept
        {
            const double f = 440.0 * std::exp2 ((static_cast<double> (semis) - 69.0) / 12.0);
            curInc[n] = static_cast<float> (std::min (f, nyq) / sampleRate);
            curGainL[n] = amp * gainL * panL * t.level;
            curGainR[n] = amp * gainR * panR * t.level;
            curFrameOf[n] = std::clamp (t.frame + t.frameSpread * framePos, 0.0f, 1.0f);
            ++n;
        };
        for (int i = 0; i < pairs; ++i)
        {
            const float amp = i == 0 ? ac : ad;
            const float stack = stackRatioSemis (t.stack, i, t.stackRepeat);
            if (odd && i == 0)
            {
                addVoice (t.semis + stack, amp, sa + sb, sa + sb, 0.0f);
                continue;
            }
            float tt = (static_cast<float> (2 * i + bump)) / divisor;
            tt = std::clamp (tt, 0.0f, 1.0f);
            switch (t.mode)
            {
                case 1: tt = tt * tt * tt; break;
                case 2: tt = (std::exp (2.0f * tt) - 1.0f) / (std::exp (2.0f) - 1.0f); break;
                case 3: tt = 1.0f - (1.0f - tt) * (1.0f - tt) * (1.0f - tt); break;
                case 4:
                {
                    Rng r;
                    r.seed (static_cast<uint32_t> (i) * 7919u + 13u);
                    tt = r.uni();
                    break;
                }
                default: break;
            }
            const float semisOff = powerShape (tt, t.curve) * t.detuneSemis;
            const bool leftFlat = (i % 2) == 0;
            const float leftSemis = leftFlat ? -semisOff : semisOff;
            const float pos = divisor > 0.0f ? (static_cast<float> (2 * i + bump)) / divisor : 0.0f;
            addVoice (t.semis + stack + leftSemis, amp, sa, sb, -pos);   // the left channel voice
            addVoice (t.semis + stack - leftSemis, amp, sb, sa, pos);    // the right channel voice
        }
        curCount = n;
        for (int i = n; i < kMaxVoices; ++i)
        {
            curGainL[i] = curGainR[i] = 0.0f;
            curInc[i] = curInc[0];
        }
        curFrame = t.frame;
        if (!havePrev)
        {
            for (int i = 0; i < kMaxVoices; ++i)
            {
                prevInc[i] = curInc[i];
                prevGainL[i] = curGainL[i];
                prevGainR[i] = curGainR[i];
            }
            prevCount = curCount;
            havePrev = true;
        }
        (void) rng;
    }

    /** Semitone offset of the stack style (pair index based, document 01, 9.3). */
    static float stackRatioSemis (int style, int pair, int repeat) noexcept
    {
        static const float kPower[8] = { 0.0f, 7.0f, 12.0f, 19.0f, 24.0f, 0.0f, 7.0f, 12.0f };
        const int i = pair;
        auto ratioToSemis = [] (float ratio) { return 12.0f * std::log2 (ratio); };
        switch (style)
        {
            case 1: return i == 0 ? -12.0f : 0.0f;                     // Center Drop 12
            case 2: return i == 0 ? -24.0f : 0.0f;                     // Center Drop 24
            case 3: return 12.0f * static_cast<float> ((repeat >= 0 ? (i % (repeat + 2)) : (i % 2)));   // Octaves (n = repeat + 1 octaves)
            case 4: return ratioToSemis (static_cast<float> (1 + (i % 3)));                             // 2 Octaves: 1x, 2x, 3x
            case 5: { const int sstep = i % (2 * (repeat + 1) + 1); return 12.0f * static_cast<float> (sstep / 2) + ((sstep & 1) ? 7.0f : 0.0f); } // Power Chord
            case 6: return kPower[i % 8];                              // Power Chord x2: 1 1.4983 2 2.9966 4
            case 7: { static const float m[4] = { 0.0f, 4.0f, 7.0f, 12.0f }; return m[i % 4]; }      // Major
            case 8: { static const float m[4] = { 0.0f, 3.0f, 7.0f, 12.0f }; return m[i % 4]; }      // Minor
            case 9: return ratioToSemis (static_cast<float> (1 + (i % 8)));                          // Harmonic series
            case 10: return ratioToSemis (static_cast<float> (1 + 2 * (i % 8)));                     // Odd harmonics
            default: return 0.0f;
        }
    }

    /** Back to the state of a new oscillator (the morph buffers stay allocated). */
    void clearState() noexcept
    {
        // In place: the same state as `*this = WtOsc()`, without building a 9 KB temporary and copying it over the object.
        auto mb = std::move (morphBuf);
        auto ms = std::move (morphScratch);
        this->~WtOsc();
        ::new (static_cast<void*> (this)) WtOsc();
        morphBuf = std::move (mb);
        morphScratch = std::move (ms);
    }

    /** Prepares the buffers of the spectral morph (allocates; call from prepare ()). */
    void prepareMorph()
    {
        if (morphBuf.empty())
        {
            morphBuf.assign (static_cast<size_t> (kMorphGroups) * 2 * kMorphStride, 0.0f);
            morphScratch = std::make_unique<MorphScratch>();
        }
    }

    /** Called once per control period (after update ()): rebuilds the morphed cycles every kMorphRebuild samples. */
    void updateMorph (double sampleRate, bool firstOfNote) noexcept
    {
        morphActive = tick.morphType != kMorphNone && tick.wt != nullptr && !morphBuf.empty();
        if (!morphActive)
        {
            morphAge = 0;
            return;
        }
        if (firstOfNote)
            morphAge = kMorphRebuild; // build now, no cross-fade from stale data
        if (morphAge >= (tick.lowCost ? 2 * kMorphRebuild : kMorphRebuild))
        {
            const Wavetable& wt = *tick.wt;
            const int pairs = (curCount + 1) / 2;
            const int groups = std::min (kMorphGroups, std::max (1, pairs));
            morphGroups = groups;
            const bool warped = tick.oversample || warpNeedsOversampling (tick.warp[0]) || warpNeedsOversampling (tick.warp[1]);
            const float nyq = static_cast<float> (warped ? (tick.lowCost ? 1.0 : 3.2) * sampleRate : 0.5 * sampleRate);
            float fmax = 1.0f, incMax = 0.0f;
            for (int v = 0; v < curCount; ++v)
            {
                fmax = std::max (fmax, curInc[v] * static_cast<float> (sampleRate));
                incMax = std::max (incMax, curInc[v]);
            }
            const int K = std::min (Wavetable::kHarmonics - 1, static_cast<int> (nyq / fmax));
            for (int g = 0; g < groups; ++g)
            {
                float* a = morphBuf.data() + (static_cast<size_t> (g) * 2) * kMorphStride + 1;
                float* b = a + kMorphStride;
                if (!firstOfNote)
                    std::copy (b - 1, b - 1 + kMorphStride, a - 1); // the previous "new" buffer becomes the old one
                const float spreadShift = groups > 1 ? tick.morphSpread * 2.0f * static_cast<float> (g) / static_cast<float> (groups - 1) : 0.0f;
                const float framePos = tick.frame * static_cast<float> (std::max (wt.numFrames() - 1, 0));
                buildMorphCycle (wt, framePos, true, tick.morphType, clamp01 (tick.morphAmount + spreadShift), K, incMax, sampleRate, b, *morphScratch);
                if (firstOfNote)
                    std::copy (b - 1, b - 1 + kMorphStride, a - 1);
            }
            morphAge = 0;
        }
    }

    /** Renders samples t0 .. t0 + n - 1 of the period into L and R (overwritten). `cross` are the signals of the other two oscillators (may be null). */
    void render (int t0, int n, float* L, float* R, const float* const* cross, double sampleRate) noexcept
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        if (tick.wt == nullptr || tick.level <= 0.0f)
            return;
        const Wavetable& wt = *tick.wt;
        const int frames = wt.numFrames();
        const double nyq = 0.5 * sampleRate;
        const int count = std::max (curCount, prevCount);
        const float invP = 1.0f / static_cast<float> (kControlPeriod);
        const float a0 = tick.warpAmount[0], a1 = tick.warpAmount[1];
        // a warp can create content far above the pitch: the oscillator then runs at four times the rate, with a band limited decimation
        const bool warped = tick.oversample || warpNeedsOversampling (tick.warp[0]) || warpNeedsOversampling (tick.warp[1]);
        const int cs0 = tick.warpSrc[0], cs1 = tick.warpSrc[1];
        const bool morphing = morphActive;
        const int morphPairs = std::max (1, (curCount + 1) / 2);
        for (int v = 0; v < count; ++v)
        {
            if (curGainL[v] == 0.0f && curGainR[v] == 0.0f && prevGainL[v] == 0.0f && prevGainR[v] == 0.0f)
            {
                // silent voice: advance the phase only
                phase[v] += static_cast<uint32_t> (static_cast<uint64_t> (static_cast<double> (curInc[v]) * 4294967296.0 * n));
                continue;
            }
            float bw = 1.0f;
            for (int w = 0; w < 2; ++w)
                bw = std::max (bw, warpBandwidthFactor (tick.warp[w], tick.warpAmount[w]));
            const float fmax = std::max (curInc[v], prevInc[v]) * static_cast<float> (sampleRate) * bw;
            const int os = tick.lowCost ? kOversampleLow : kOversample, nt = tick.lowCost ? kTapsLow : kTaps;
            const double rateI = sampleRate * (warped ? os : 1);
            // band limit: the coarsest level that keeps the highest harmonic below Nyquist (of the rate the oscillator runs at)
            const int level = Wavetable::levelFor (static_cast<float> (warped ? (tick.lowCost ? 1.0 : 3.2) * sampleRate : nyq) / std::max (fmax, 1.0f));
            int N = Wavetable::size (level);
            // two cycles and the weight of the second one at the start / end of this chunk
            const float* cA = nullptr;
            const float* cB = nullptr;
            float wB0 = 0.0f, wB1 = 0.0f;
            const float tEnd = static_cast<float> (t0 + n) * invP, tStart = static_cast<float> (t0) * invP;
            if (morphing)
            {
                const int g = std::min (morphGroups - 1, (v / 2) * morphGroups / morphPairs);
                cA = morphBuf.data() + (static_cast<size_t> (g) * 2) * kMorphStride + 1;
                cB = cA + kMorphStride;
                const float len = static_cast<float> (kMorphRebuild);
                wB0 = std::min (1.0f, (static_cast<float> (morphAge) + tStart * kControlPeriod) / len);
                wB1 = std::min (1.0f, (static_cast<float> (morphAge) + tEnd * kControlPeriod) / len);
                N = Wavetable::kCycle;
            }
            else
            {
                const float fpos = curFrameOf[v] * static_cast<float> (frames - 1);
                const int f0 = std::min (static_cast<int> (fpos), std::max (frames - 2, 0));
                if (tick.smooth || frames < 2)
                {
                    const float ff = frames > 1 ? fpos - static_cast<float> (f0) : 0.0f;
                    cA = wt.cycle (f0, level);
                    cB = frames > 1 && ff > 1.0e-4f ? wt.cycle (f0 + 1, level) : nullptr;
                    wB0 = wB1 = ff;
                }
                else
                {
                    // the frame steps (the integer part of the position); the change is cross-faded over the control period so that it does not click
                    const int fnew = std::min (static_cast<int> (fpos + 1.0e-3f), frames - 1); // the tolerance keeps 40 % of 256 frames on frame 102 (float rounding of the knob)
                    const int fold = std::clamp (frameIdx[v] < 0 ? fnew : frameIdx[v], 0, frames - 1);
                    cA = wt.cycle (fold, level);
                    cB = fnew != fold ? wt.cycle (fnew, level) : nullptr;
                    wB0 = tStart;
                    wB1 = tEnd;
                    if (t0 + n >= kControlPeriod)
                        frameIdx[v] = fnew;
                }
            }
            const float fscale = static_cast<float> (N);
            const float f0Hz = std::max (curInc[v], prevInc[v]) * static_cast<float> (sampleRate);
            const bool filt0 = tick.warp[0] == kWarpFilterLpf || tick.warp[0] == kWarpFilterHpf;
            const bool filt1 = tick.warp[1] == kWarpFilterLpf || tick.warp[1] == kWarpFilterHpf;
            float* fz = filterState[v];
            // the one pole filters of the Filter LPF / HPF warps: two cascaded poles at ratio * f0
            auto filterStage = [&] (int slot, int type, float amount, float y) noexcept
            {
                const float ratio = (type == kWarpFilterLpf ? 1.0f : 0.5f) * std::exp2 (7.0f * amount);
                const float x = std::min (kTwoPif * ratio * f0Hz / static_cast<float> (rateI), 8.0f);
                const float g = x / (1.0f + x);
                float* z = fz + slot * 2;
                if (type == kWarpFilterLpf)
                {
                    z[0] += g * (y - z[0]);
                    z[1] += g * (z[0] - z[1]);
                    return z[1];
                }
                const float h1 = y - (z[0] += g * (y - z[0]));
                return h1 - (z[1] += g * (h1 - z[1]));
            };
            // one sample of the oscillator at the (32 bit) phase `ph`; k selects the sample of the cross signals, wB is the weight of the second cycle
            auto sampleAt = [&] (uint32_t ph, int k, float fk, float wB) noexcept
            {
                const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
                float window = 1.0f, w2 = 1.0f, pp = p;
                // the other oscillator is interpolated between its samples (fk = position inside the sample k, 1 = at k)
                const float m0 = cs0 >= 0 && cross != nullptr && cross[cs0] ? lerp (k > 0 ? cross[cs0][k - 1] : lastCross[cs0], cross[cs0][k], fk) : 0.0f;
                const float m1 = cs1 >= 0 && cross != nullptr && cross[cs1] ? lerp (k > 0 ? cross[cs1][k - 1] : lastCross[cs1], cross[cs1][k], fk) : 0.0f;
                if (tick.warp[0] != 0)
                    pp = warpPhase (tick.warp[0], pp, a0, tick.warpPhase[0], m0, window, tick.remap);
                if (tick.warp[1] != 0)
                {
                    pp = warpPhase (tick.warp[1], pp, a1, tick.warpPhase[1], m1, w2, tick.remap);
                    window *= w2;
                }
                auto read = [&] (float q) noexcept
                {
                    q -= std::floor (q);
                    const float pos = q * fscale;
                    const int idx = std::clamp (static_cast<int> (pos), 0, N);
                    const float fr = pos - static_cast<float> (idx);
                    float r = hermite (cA + idx, fr);
                    if (cB != nullptr)
                        r = lerp (r, hermite (cB + idx, fr), wB);
                    return r;
                };
                float y = read (pp);
                // Odd/Even: the table read at the phase and half a cycle later, mixed as even / odd harmonics
                for (int sl = 0; sl < 2; ++sl)
                    if (tick.warp[sl] == kWarpOddEven)
                    {
                        const float am = sl == 0 ? a0 : a1;
                        const float y2 = read (pp + 0.5f);
                        const float ev = 0.5f * (y + y2), od = 0.5f * (y - y2);
                        y = std::min (1.0f, 2.0f * am) * ev + std::min (1.0f, 2.0f * (1.0f - am)) * od;
                        break;
                    }
                y *= window;
                if (tick.warp[0] != 0)
                    y = warpSample (tick.warp[0], y, a0, m0);
                if (tick.warp[1] != 0)
                    y = warpSample (tick.warp[1], y, a1, m1);
                if (filt0)
                    y = filterStage (0, tick.warp[0], a0, y);
                if (filt1)
                    y = filterStage (1, tick.warp[1], a1, y);
                return y;
            };
            uint32_t ph = phase[v];
            if (!warped && tick.warp[0] == 0 && tick.warp[1] == 0)
            {
                // the plain table read (no warp, no cross signal, no filter stage): the general sampleAt () below with its branches removed. p < 1, so the wrap of the
                // phase (q -= floor (q)) and the window factor 1 change nothing; the weight of the second cycle is only needed where there is one.
                const float pg = prevGainL[v], cgl = curGainL[v], pgr = prevGainR[v], cgr = curGainR[v];
                const float pinc = prevInc[v], cinc = curInc[v];
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
                    const float pos = p * fscale;
                    const int idx = std::clamp (static_cast<int> (pos), 0, N);
                    const float fr = pos - static_cast<float> (idx);
                    float y = hermite (cA + idx, fr);
                    if (cB != nullptr)
                        y = lerp (y, hermite (cB + idx, fr), lerp (wB0, wB1, static_cast<float> (k + 1) / static_cast<float> (n)));
                    L[k] += y * lerp (pg, cgl, t);
                    R[k] += y * lerp (pgr, cgr, t);
                    ph += static_cast<uint32_t> (static_cast<double> (lerp (pinc, cinc, t)) * 4294967296.0);
                }
            }
            else if (!warped)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const float y = sampleAt (ph, k, 1.0f, lerp (wB0, wB1, static_cast<float> (k + 1) / static_cast<float> (n)));
                    L[k] += y * lerp (prevGainL[v], curGainL[v], t);
                    R[k] += y * lerp (prevGainR[v], curGainR[v], t);
                    ph += static_cast<uint32_t> (static_cast<double> (lerp (prevInc[v], curInc[v], t)) * 4294967296.0);
                }
            }
            else
            {
                const float* taps = tick.lowCost ? decimatorTaps2() : decimatorTaps();
                float* hist = history[v]; // the last nt - 1 samples of the os x signal, then this period's
                float buf[kTaps - 1 + kOversample * kControlPeriod];
                std::copy (hist, hist + nt - 1, buf);
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const uint32_t incO = static_cast<uint32_t> (static_cast<double> (lerp (prevInc[v], curInc[v], t)) * (4294967296.0 / os));
                    for (int j = 0; j < os; ++j)
                    {
                        buf[nt - 1 + k * os + j] = sampleAt (ph, k, static_cast<float> (j + 1) * (1.0f / static_cast<float> (os)),
                                                             lerp (wB0, wB1, static_cast<float> (k + 1) / static_cast<float> (n)));
                        ph += incO;
                    }
                }
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const float* x = buf + k * os + 1; // the newest sample of the group is x[nt - 1]
                    float acc = 0.0f;
                    for (int i = 0; i < nt; ++i)
                        acc += taps[i] * x[nt - 1 - i];
                    L[k] += acc * lerp (prevGainL[v], curGainL[v], t);
                    R[k] += acc * lerp (prevGainR[v], curGainR[v], t);
                }
                std::copy (buf + n * os, buf + n * os + nt - 1, hist);
            }
            phase[v] = ph;
        }
        if (cross != nullptr)
            for (int c = 0; c < 6; ++c)
                if (cross[c] != nullptr)
                    lastCross[c] = cross[c][n - 1];
        // the morph age advances at the end of the period
        if (morphActive && t0 + n >= kControlPeriod)
            morphAge += kControlPeriod;
    }

    /** Phase of the first voice in cycles (for the "osc" sources and the phase legato). */
    float firstPhase() const noexcept { return static_cast<float> (phase[0] >> 8) * (1.0f / 16777216.0f); }
    void setPhase (float p) noexcept
    {
        for (auto& x : phase)
            x = static_cast<uint32_t> (static_cast<double> (p - std::floor (p)) * 4294967296.0);
    }

private:
    static constexpr int kMorphGroups = 2, kMorphStride = Wavetable::kCycle + 4, kMorphRebuild = 256;
    OscTick tick;
    std::vector<float> morphBuf;                 // [group][old, new][kMorphStride]
    std::unique_ptr<MorphScratch> morphScratch;
    int morphAge = 0, morphGroups = 1;
    bool morphActive = false;
    int frameIdx[kMaxVoices] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    float filterState[kMaxVoices][4] = {};
    uint32_t phase[kMaxVoices] = {};
    float history[kMaxVoices][kTaps - 1] = {};
    float prevInc[kMaxVoices] = {}, curInc[kMaxVoices] = {}, prevGainL[kMaxVoices] = {}, curGainL[kMaxVoices] = {}, prevGainR[kMaxVoices] = {},
          curGainR[kMaxVoices] = {}, curFrameOf[kMaxVoices] = {};
    float prevFrame = 0.0f, curFrame = 0.0f;
    float lastCross[6] = {};
    int curCount = 1, prevCount = 1;
    bool havePrev = false;
    bool haveLast = false;      // lastT / lastRate describe the inputs of the last full update ()
    OscTick lastT;
    double lastRate = 0.0;
};
} // namespace own
} // namespace synthyboy
