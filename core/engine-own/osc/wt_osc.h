// SynthyBoy - own engine: wavetable oscillator with unison and warps (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

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
};

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
    static constexpr int kOversample = 4, kTaps = 64;

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
                const double cutoff = 0.106, beta = 7.5; // in cycles per sample of the 4x signal (20.4 kHz at 48 kHz)
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

    void start (float startPhase, float randomPhase, Rng& rng) noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const float p = startPhase + randomPhase * rng.uni();
            phase[i] = static_cast<uint32_t> (static_cast<double> (p - std::floor (p)) * 4294967296.0);
        }
        havePrev = false;
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
        const float sa = std::sin (kPif * 0.5f * (0.5f + 0.5f * w)), sb = std::sin (kPif * 0.5f * (1.5f + 0.5f * w));
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
        auto mb = std::move (morphBuf);
        auto ms = std::move (morphScratch);
        *this = WtOsc();
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
        if (morphAge >= kMorphRebuild)
        {
            const Wavetable& wt = *tick.wt;
            const int pairs = (curCount + 1) / 2;
            const int groups = std::min (kMorphGroups, std::max (1, pairs));
            morphGroups = groups;
            const bool warped = tick.oversample || warpNeedsOversampling (tick.warp[0]) || warpNeedsOversampling (tick.warp[1]);
            const float nyq = static_cast<float> (warped ? 3.6 * sampleRate : 0.5 * sampleRate);
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
            const double rateI = sampleRate * (warped ? kOversample : 1);
            // band limit: the coarsest level that keeps the highest harmonic below Nyquist (of the rate the oscillator runs at)
            const int level = Wavetable::levelFor (static_cast<float> (warped ? 3.6 * sampleRate : nyq) / std::max (fmax, 1.0f));
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
            if (!warped)
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
                const float* taps = decimatorTaps();
                float* hist = history[v]; // the last kTaps - 1 samples of the 4x signal, then this period's
                float buf[kTaps - 1 + kOversample * kControlPeriod];
                std::copy (hist, hist + kTaps - 1, buf);
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const uint32_t inc4 = static_cast<uint32_t> (static_cast<double> (lerp (prevInc[v], curInc[v], t)) * (4294967296.0 / kOversample));
                    for (int j = 0; j < kOversample; ++j)
                    {
                        buf[kTaps - 1 + k * kOversample + j] = sampleAt (ph, k, static_cast<float> (j + 1) * (1.0f / kOversample),
                                                                         lerp (wB0, wB1, static_cast<float> (k + 1) / static_cast<float> (n)));
                        ph += inc4;
                    }
                }
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const float* x = buf + k * kOversample + 1; // the newest sample of the group is x[kTaps - 1]
                    float acc = 0.0f;
                    for (int i = 0; i < kTaps; ++i)
                        acc += taps[i] * x[kTaps - 1 - i];
                    L[k] += acc * lerp (prevGainL[v], curGainL[v], t);
                    R[k] += acc * lerp (prevGainR[v], curGainR[v], t);
                }
                std::copy (buf + n * kOversample, buf + n * kOversample + kTaps - 1, hist);
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
};
} // namespace own
} // namespace synthyboy
