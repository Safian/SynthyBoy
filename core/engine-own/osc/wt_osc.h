// SynthyBoy - own engine: wavetable oscillator with unison and warps (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "../dsp/util.h"
#include "../dsp/simd.h"
#include "../dsp/section_timing.h"
#include "morph.h"
#include "warp.h"
#include "warp_adaa.h"
#include "wavetable_bank.h"
#include "synthyboy/aa_switch.h"

namespace synthyboy
{
namespace own
{
/** The effective (modulated) values of an oscillator at the end of a control period. */
struct OscTick
{
    // The fields are ordered so that the struct has no padding bytes (pointers, then the 4 byte fields, then the bools and explicit padding): a byte compare of two ticks then
    // compares exactly their fields (WtOsc::update ()).
    const Wavetable* wt = nullptr;
    const float* remap = nullptr;         // the drawn remap curve of the oscillator for the warps Remap 1 .. 4 (257 points) or null (identity)
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
    int morphType = 0;                    // oscN.morph.type
    float morphAmount = 0.0f, morphSpread = 0.0f; // 0 .. 1, -1 .. 1
    bool active = false;
    bool jump = false;                    // quality.no_smoothing: the period's values apply at once (no ramp from the previous period)
    bool oversample = false;              // run 4x oversampled even without a warp (ultra quality)
    bool smooth = false;                  // oscN.smooth_interp: blend the neighbouring frames (off: the frame steps, with a short cross-fade)
    bool shareMorph = false;              // tier low only: all unison voices share one morph cycle (no morph spread across them)
    bool lowWarp = false;                 // economy group Warp (tier low): a warp runs 2x oversampled with a 32 tap decimator instead of 4x with 128 taps
    bool lowMorph = false;                // economy group Morph (tier low): the spectral morph is rebuilt every 512 samples, from a transform of half the size
    bool fastMath = false;                // economy group Math (tier low): the warps use the cheaper forms of dsp/fastmath.h
    bool scalarWarp = false;              // measurements (setOwnSavingsOff, kOwnSavingVectorWarp): the oversampled samples one by one also where the four lane form exists
    bool padding[7] = {};                 // explicit: 9 bools + 7 = 16 bytes, so that the size is the sum of the fields on 32 and 64 bit
};
static_assert (sizeof (OscTick) == 2 * sizeof (void*) + 30 * 4 + 16, "OscTick must not have padding bytes (WtOsc::update compares ticks as bytes)");

/** True if every field of the two ticks is equal (floats compared as values: a NaN is never equal, which only costs the shortcut). */
inline bool sameOscTick (const OscTick& a, const OscTick& b) noexcept
{
    return a.wt == b.wt && a.semis == b.semis && a.frame == b.frame && a.frameSpread == b.frameSpread && a.level == b.level && a.pan == b.pan && a.panRight == b.panRight
        && a.voices == b.voices && a.detuneSemis == b.detuneSemis && a.curve == b.curve && a.mode == b.mode && a.stack == b.stack && a.stackRepeat == b.stackRepeat
        && a.blendShape == b.blendShape && a.spread == b.spread && a.blend == b.blend && a.law == b.law && a.warp[0] == b.warp[0] && a.warp[1] == b.warp[1]
        && a.warpSrc[0] == b.warpSrc[0] && a.warpSrc[1] == b.warpSrc[1] && a.warpAmount[0] == b.warpAmount[0] && a.warpAmount[1] == b.warpAmount[1]
        && a.warpPhase[0] == b.warpPhase[0] && a.warpPhase[1] == b.warpPhase[1] && a.warpSpread[0] == b.warpSpread[0] && a.warpSpread[1] == b.warpSpread[1]
        && a.startPhase == b.startPhase && a.active == b.active && a.remap == b.remap && a.morphType == b.morphType && a.morphAmount == b.morphAmount
        && a.morphSpread == b.morphSpread && a.oversample == b.oversample && a.smooth == b.smooth && a.lowWarp == b.lowWarp && a.lowMorph == b.lowMorph && a.fastMath == b.fastMath && a.scalarWarp == b.scalarWarp && a.shareMorph == b.shareMorph;
}

/** Hermite interpolation of the four samples x[-1], x[0], x[1], x[2] at x[0] + f. */
inline float hermite (const float* x, float f) noexcept
{
    const float c1 = 0.5f * (x[1] - x[-1]);
    const float c2 = x[-1] - 2.5f * x[0] + 2.0f * x[1] - 0.5f * x[2];
    const float c3 = 0.5f * (x[2] - x[-1]) + 1.5f * (x[0] - x[1]);
    return ((c3 * f + c2) * f + c1) * f + x[0];
}

/** Kaiser windowed sinc low pass of N taps, `cutoff` in cycles per sample of the oversampled signal, normalised to unity gain at DC. */
template <int N>
struct DecimatorTaps
{
    float c[N];
    DecimatorTaps (double cutoff, double beta)
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
        double total = 0.0;
        double tmp[N];
        for (int i = 0; i < N; ++i)
        {
            const double x = i - 0.5 * (N - 1);
            const double sinc = std::fabs (x) < 1.0e-9 ? 2.0 * cutoff : std::sin (2.0 * 3.14159265358979323846 * cutoff * x) / (3.14159265358979323846 * x);
            const double r = 2.0 * i / (N - 1) - 1.0;
            tmp[i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            total += tmp[i];
        }
        for (int i = 0; i < N; ++i)
            c[i] = static_cast<float> (tmp[i] / total);
    }
};

struct WtOscLaneTest; // core/tests/test_own_warp_lanes.cpp
struct WtOscOptTest;  // core/tests/test_own_wt_osc_opt.cpp
struct WtOscSavingsTest; // core/tests/test_own_wt_osc_savings.cpp

class WtOsc
{
    friend struct WtOscLaneTest;
    friend struct WtOscOptTest; // core/tests/test_own_wt_osc_opt.cpp
    friend struct WtOscSavingsTest; // core/tests/test_own_wt_osc_savings.cpp
    friend struct WtOscSharedDecimTest; // core/tests/test_own_wt_osc_shared_decim.cpp

public:
    static constexpr int kMaxVoices = 16;
    /** Tests only: run the code as it was before the optimisations of docs/release/CPU-REVIEW-OSC.md (the same output, slower). */
    void setReference (bool on) noexcept { reference_ = on; }
    /** Tests only: off = every unison voice of the oversampled (warped) path runs its own decimator, the code before the shared decimation (docs/dsp-knowledge/antialiasing.md). */
    void setSharedDecimator (bool on) noexcept { sharedDecimator_ = on; }
    /** The shared decimation of a warped render call pays from this many voices with a constant gain (two decimations replace one per voice). */
    static constexpr int kSharedDecimatorMin = 3;
    static constexpr int kOversample = 4, kTaps = 128;
    static constexpr int kOversampleLow = 2, kTapsLow = 32;
    static inline const DecimatorTaps<128> kDecimator4 { 0.105, 7.5 }; // cycles per sample of the 4x signal
    static inline const DecimatorTaps<32> kDecimator2 { 0.21, 7.5 };   // cycles per sample of the 2x signal
    /** The taps of the 2x decimator in reversed order, so that the dot product with the newest samples runs forwards through memory. */
    struct ReversedTaps
    {
        float c[kTapsLow];
        ReversedTaps()
        {
            for (int i = 0; i < kTapsLow; ++i)
                c[i] = kDecimator2.c[kTapsLow - 1 - i];
        }
    };
    static inline const ReversedTaps kDecimator2Rev {};
    /** The same for the 128 taps of the 4x decimator (used when the warp runs 4x and the Math economy group is on). */
    struct ReversedTaps4
    {
        float c[kTaps];
        ReversedTaps4()
        {
            for (int i = 0; i < kTaps; ++i)
                c[i] = kDecimator4.c[kTaps - 1 - i];
        }
    };
    static inline const ReversedTaps4 kDecimator4Rev {};
    /** sum of taps[i] * x[i] over the 32 taps of the low cost decimator, as four interleaved partial sums (lane j takes the taps 4k + j) added as (s0 + s1) + (s2 + s3): the
        NEON and the scalar version do the same additions in the same order. */
    static float dot32 (const float* x, const float* taps) noexcept { return dotLanes (x, taps, kTapsLow); }
    /** The same four partial sums over the 128 taps of the 4x decimator (the Math economy group with the 4x warp: rounding differs from the sequential sum of the
        medium / high tiers by about 1e-7 relative, as at 2x). */
    static float dot128 (const float* x, const float* taps) noexcept { return dotLanes (x, taps, kTaps); }
    /** sum of taps[i] * x[i] over n taps (a multiple of 4) as four interleaved partial sums added as (s0 + s1) + (s2 + s3). */
    static float dotLanes (const float* x, const float* taps, int n) noexcept
    {
#if SB_NEON
        float32x4_t acc = vmulq_f32 (vld1q_f32 (x), vld1q_f32 (taps));
        for (int k = 4; k < n; k += 4)
            acc = vaddq_f32 (acc, vmulq_f32 (vld1q_f32 (x + k), vld1q_f32 (taps + k)));
        return (vgetq_lane_f32 (acc, 0) + vgetq_lane_f32 (acc, 1)) + (vgetq_lane_f32 (acc, 2) + vgetq_lane_f32 (acc, 3));
#else
        float s0 = taps[0] * x[0], s1 = taps[1] * x[1], s2 = taps[2] * x[2], s3 = taps[3] * x[3];
        for (int k = 4; k < n; k += 4)
        {
            s0 += taps[k] * x[k];
            s1 += taps[k + 1] * x[k + 1];
            s2 += taps[k + 2] * x[k + 2];
            s3 += taps[k + 3] * x[k + 3];
        }
        return (s0 + s1) + (s2 + s3);
#endif
    }

    /** Linear phase low pass of the 4x decimator (Kaiser window), and the 32 tap one of the low cost mode (2x): built once at program start (namespace scope tables,
        so the render path has no initialisation guard). */
    static const float* decimatorTaps() noexcept { return kDecimator4.c; }
    static const float* decimatorTaps2() noexcept { return kDecimator2.c; }

    void start (float startPhase, float randomPhase, Rng& rng) noexcept
    {
        flushPending(); // the updates of the silent periods before the note come first, as they did
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const float p = startPhase + randomPhase * rng.uni();
            phase[i] = static_cast<uint32_t> (static_cast<double> (p - ffloor (p)) * 4294967296.0);
        }
        havePrev = false;
        haveLast = false;
        morphAge = kMorphRebuild;
        for (auto& k : morphKeyOk)
            k = false;
        for (auto& k : morphLastKeyOk)
            k = false;
        for (auto& f : frameIdx)
            f = -1;
        for (auto& fs : filterState)
            for (auto& x : fs)
                x = 0.0f;
        for (auto& wa : warpAa)
            wa[0] = wa[1] = warpadaa::State{};
        for (auto& bp : blepPend)
            bp = BlepPending{};
        if (historyDirty) // the decimator history is written only by the oversampled path: a note that never ran it starts with the zeros that are already there
        {
            std::fill (history.begin(), history.end(), 0.0f);
            historyDirty = false;
        }
    }

    /** Computes the per voice frequencies, levels and pans of the period that ends now (call once per control period). The layout follows
        docs/engine-spec/01-voice-management.md 9: voices come in left / right pairs (an odd count has a single centre voice), the pair i is detuned by
        P (t, curve) of the outermost range, sharp and flat alternate, amplitudes by the blend law with power normalisation, then the stereo spread. */
    void update (const OscTick& t, double sampleRate, Rng& rng) noexcept
    {
        flushPending();
        prevCount = curCount;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            prevInc[i] = curInc[i];
            prevGainL[i] = curGainL[i];
            prevGainR[i] = curGainR[i];
            prevFrameOf[i] = curFrameOf[i];
        }
        prevFrame = curFrame;
        // the same inputs as the previous period (the usual case of a held note): the voices below would be computed again to the same values
        // (equal bytes are the common case and cost one compare; ticks that differ in bytes only by the sign of a zero still take the field by field compare, as before)
        if (haveLast && lastRate == sampleRate && ((!reference_ && std::memcmp (&t, &lastT, sizeof (OscTick)) == 0) || sameOscTick (t, lastT)))
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
        // the two sines depend on the spread only, and the Serum law below overwrites them: they are computed (and kept for the next full update) only when they are used
        float sa = 0.0f, sb = 0.0f;
        if (reference_)
        {
            sa = std::sin (kPif * 0.5f * (0.5f + 0.5f * w));
            sb = std::sin (kPif * 0.5f * (1.5f + 0.5f * w));
        }
        else if (t.law != 1)
        {
            if (!(spreadOk && sameBits (spreadW, w)))
            {
                spreadSa = std::sin (kPif * 0.5f * (0.5f + 0.5f * w));
                spreadSb = std::sin (kPif * 0.5f * (1.5f + 0.5f * w));
                spreadW = w;
                spreadOk = true;
            }
            sa = spreadSa;
            sb = spreadSb;
        }
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
        float panL, panR;
        if (reference_)
        {
            panL = 1.41421356f * std::sin (kPif * 0.25f * (1.0f - pan));
            panR = 1.41421356f * std::sin (kPif * 0.25f * (1.0f + panRt));
        }
        else
        {
            if (!(panLOk && sameBits (panKeyL, pan)))
            {
                panLValue = 1.41421356f * std::sin (kPif * 0.25f * (1.0f - pan));
                panKeyL = pan;
                panLOk = true;
            }
            if (!(panROk && sameBits (panKeyR, panRt)))
            {
                panRValue = 1.41421356f * std::sin (kPif * 0.25f * (1.0f + panRt));
                panKeyR = panRt;
                panROk = true;
            }
            panL = panLValue;
            panR = panRValue;
        }
        const int bump = odd ? 0 : 1;
        const float divisor = static_cast<float> (std::max (1, U - 1));
        // the detune offset of each pair depends on the voice count, the mode, the curve and the range only (not on pitch, frame, level or pan)
        const bool offHit = !reference_ && offOk && offU == U && offMode == t.mode && sameBits (offCurve, t.curve) && sameBits (offRange, t.detuneSemis);
        if (!offHit)
        {
            offU = U;
            offMode = t.mode;
            offCurve = t.curve;
            offRange = t.detuneSemis;
            offOk = !reference_;
        }
        int n = 0;
        auto addVoice = [&] (float semis, float amp, float gainL, float gainR, float framePos, float w0, float w1) noexcept
        {
            double f;
            float inc;
            if (!reference_ && n < kMaxVoices && haveFreq[n] && freqSemis[n] == semis)
            {
                f = freqHz[n]; // the same pitch as this voice had in the previous full update: the same double
                if (!(incRate[n] == sampleRate)) // (the increment also depends on the rate; both are kept together)
                {
                    freqInc[n] = static_cast<float> (std::min (f, nyq) / sampleRate);
                    incRate[n] = sampleRate;
                }
                inc = freqInc[n];
            }
            else
            {
                f = 440.0 * std::exp2 ((static_cast<double> (semis) - 69.0) / 12.0);
                inc = static_cast<float> (std::min (f, nyq) / sampleRate);
                if (n < kMaxVoices)
                {
                    freqSemis[n] = semis;
                    freqHz[n] = f;
                    freqInc[n] = inc;
                    incRate[n] = sampleRate;
                    haveFreq[n] = true;
                }
            }
            curInc[n] = inc;
            curGainL[n] = amp * gainL * panL * t.level;
            curGainR[n] = amp * gainR * panR * t.level;
            curFrameOf[n] = std::clamp (t.frame + t.frameSpread * framePos, 0.0f, 1.0f);
            curWarpA[0][n] = w0;
            curWarpA[1][n] = w1;
            ++n;
        };
        for (int i = 0; i < pairs; ++i)
        {
            const float amp = i == 0 ? ac : ad;
            const float stack = stackRatioSemis (t.stack, i, t.stackRepeat);
            const float pairFrac = (pairs > 1) ? static_cast<float> (i) / static_cast<float> (pairs - 1) : 0.0f;
            const float w0 = std::clamp (t.warpAmount[0] + t.warpSpread[0] * pairFrac, 0.0f, 1.0f);
            const float w1 = std::clamp (t.warpAmount[1] + t.warpSpread[1] * pairFrac, 0.0f, 1.0f);
            if (odd && i == 0)
            {
                addVoice (t.semis + stack, amp, sa + sb, sa + sb, 0.0f, w0, w1);
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
            float semisOff;
            if (offHit)
                semisOff = offValue[i];
            else
            {
                semisOff = powerShape (tt, t.curve) * t.detuneSemis;
                offValue[i] = semisOff;
            }
            const bool leftFlat = (i % 2) == 0;
            const float leftSemis = leftFlat ? -semisOff : semisOff;
            const float pos = divisor > 0.0f ? (static_cast<float> (2 * i + bump)) / divisor : 0.0f;
            addVoice (t.semis + stack + leftSemis, amp, sa, sb, -pos, w0, w1);   // the left channel voice
            addVoice (t.semis + stack - leftSemis, amp, sb, sa, pos, w0, w1);    // the right channel voice
        }
        curCount = n;
        for (int i = n; i < kMaxVoices; ++i)
        {
            curGainL[i] = curGainR[i] = 0.0f;
            curInc[i] = curInc[0];
            curFrameOf[i] = curFrameOf[0];
            curWarpA[0][i] = curWarpA[0][0];
            curWarpA[1][i] = curWarpA[1][0];
        }
        curFrame = t.frame;
        if (t.jump)
            prevFrame = curFrame;
        if (!havePrev || t.jump)
        {
            for (int i = 0; i < kMaxVoices; ++i)
            {
                prevInc[i] = curInc[i];
                prevGainL[i] = curGainL[i];
                prevGainR[i] = curGainR[i];
                prevFrameOf[i] = curFrameOf[i];
            }
            prevCount = curCount;
            havePrev = true;
        }
        (void) rng;
    }

    /** The update of a silent period, put off. The caller (Voice::tick) calls this before update () / updateMorph () for an oscillator whose level of the period is 0 (or that is
        off); true = it was put off and the caller must not call them, false = the caller does as before.
        What a silent period does in the old code: render () returns at once for a level <= 0 (zeros, no phase, no morph age, no cross memory), so the output is exactly zero; the
        only things that change are what update () and updateMorph () compute from the tick, and there the last tick wins: the prev arrays of the next period are the cur arrays of
        the last silent tick, and cur is a function of the tick alone (the caches are keyed on their inputs), so replaying the LAST silent tick (update () and, for the oscillators
        that call it, updateMorph ()) before the next live update gives the very state the whole run of silent periods would have. The one side effect that a silent period has
        and that a later one does not overwrite is morphAge = 0 of an inactive morph, which is done here at once. A period that would build a morph cycle (a new note, or an age that
        reached the rebuild period) is not put off, so every build happens when it did and with its own tick. `withMorph`: the caller calls updateMorph () after update (). */
    bool deferSilent (const OscTick& t, double sampleRate, bool withMorph, bool firstOfNote) noexcept
    {
        if (reference_ || !(t.level <= 0.0f))
            return false;
        if (withMorph)
        {
            if (firstOfNote)
                return false;
            if (t.morphType != kMorphNone && t.wt != nullptr && !morphBuf.empty())
            {
                if (morphAge >= (t.lowMorph ? 2 * kMorphRebuild : kMorphRebuild))
                    return false;
            }
            else
                morphAge = 0; // updateMorph () of an inactive morph
        }
        pendingTick = t;
        pendingRate = sampleRate;
        pendingMorph = withMorph;
        pending = true;
        return true;
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
        // A held morph (updateMorph ()) leaves the old buffer stale: make it the new cycle again, the buffers are kept and the reset state reads both.
        if (!morphBuf.empty())
            for (int g = 0; g < kMorphGroups; ++g)
                if (morphHeld[g])
                    std::copy (morphNew (g) - 1, morphNew (g) - 1 + kMorphStride, morphOld (g) - 1);
        // In place: the same state as `*this = WtOsc()`, without building a 9 KB temporary and copying it over the object.
        auto mb = std::move (morphBuf);
        auto* const msPtr = morphScratchPtr;
        auto ms = std::move (ownedMorphScratch);
        auto hist = std::move (history);
        const int hv = historyVoices;
        const bool dirty = historyDirty;
        this->~WtOsc();
        ::new (static_cast<void*> (this)) WtOsc();
        morphBuf = std::move (mb);
        morphScratchPtr = msPtr;
        ownedMorphScratch = std::move (ms);
        history = std::move (hist);
        historyVoices = hv;
        historyDirty = dirty;
        if (historyDirty)
        {
            std::fill (history.begin(), history.end(), 0.0f);
            historyDirty = false;
        }
    }

    /** Prepares the buffers of the spectral morph (allocates; call from prepare ()). */
    void prepareMorph (MorphScratch* sharedScratch = nullptr, int maxUnison = kMaxVoices)
    {
        if (morphBuf.empty())
            morphBuf.assign (static_cast<size_t> (kMorphGroups) * 2 * kMorphStride, 0.0f);

        if (sharedScratch != nullptr)
        {
            morphScratchPtr = sharedScratch;
            ownedMorphScratch.reset();
        }
        else if (morphScratchPtr == nullptr)
        {
            ownedMorphScratch = std::make_unique<MorphScratch>();
            morphScratchPtr = ownedMorphScratch.get();
        }

        historyVoices = std::clamp (maxUnison, 1, kMaxVoices);
        const size_t targetHistorySize = static_cast<size_t> (historyVoices) * (kTaps - 1);
        if (history.size() != targetHistorySize)
        {
            history.assign (targetHistorySize, 0.0f);
            historyDirty = false;
        }
    }

    /** The heap memory owned by this oscillator for morphing and oversampling. */
    size_t morphOwnedBytes() const noexcept
    {
        return (morphBuf.size() + history.size()) * sizeof (float) + (ownedMorphScratch ? sizeof (MorphScratch) : 0);
    }

    /** The stagger of the morph rebuilds (round 13, docs/release/CPU-REVIEW-OSC.md F1): `n` = morphStaggerSeed (note serial, oscillator index) (Voice::tick, at the note start). The voices of a
        chord start in the same control period; without a stagger all of them rebuild their morph cycle in the same period (one block in four at tier low carried every build). The build of
        the first period of a note is unchanged; after it the morph age starts at `morphStaggerSlot (n, P) * kControlPeriod` instead of 0 (P = the rebuild period in control periods: 16 at
        tier low, 8 otherwise), so the voice's next build comes that many periods earlier and from then on every P periods as before. 0 (the default) is the old schedule. */
    void setMorphStagger (uint32_t n) noexcept { morphStagger = n; }
    /** The stagger seed of oscillator `osc` (0 .. 2) of the note with serial `serial` (the engine's note counter: the notes of a chord have consecutive serials): serial + 4 osc. The notes of a
        chord give consecutive seeds on each oscillator; a chord of 8 notes with one morph oscillator, or of 4 notes with two (oscillators 1 and 2: seeds s .. s + 7), gives 8 consecutive seeds. */
    static uint32_t morphStaggerSeed (uint32_t serial, int osc) noexcept { return serial + 4u * static_cast<uint32_t> (osc); }
    /** The slot (0 .. P - 1, P = 8 or 16) of seed `n`: one of the 8 evenly spaced periods k P / 8, in the order k = 0, 2, 4, 6, 1, 3, 5, 7 of n mod 8 (n mod 4 picks the quarter of the
        rebuild period, n / 4 mod 2 half a quarter later). Any 8 consecutive seeds take all 8 slots, so the 8 voices of a chord rebuild in every second period at tier low (every period
        otherwise): the same number of builds in every block of 2, 4 or 8 periods (64, 128, 256 samples), wherever the chord starts inside a block. 4 consecutive seeds that begin at a
        multiple of 4 take the four quarters; other runs are spread less evenly (4 notes from another seed: at most 2 builds in a block of 4 periods against 4 without the stagger). No
        mapping can be flat for every run length and every start (a block of 4 periods holds the same count at every alignment only if the slots are whole residue classes mod 4), so it
        is made exact for the chord of 8. */
    static int morphStaggerSlot (uint32_t n, int P) noexcept
    {
        static constexpr int kOrder[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };
        return kOrder[n & 7u] * (P / 8);
    }

    /** Called once per control period (after update ()): rebuilds the morphed cycles every kMorphRebuild samples (2 kMorphRebuild at tier low), staggered per voice (setMorphStagger ()). */
    void updateMorph (double sampleRate, bool firstOfNote, MorphCache* cache = nullptr) noexcept
    {
        morphActive = tick.morphType != kMorphNone && tick.wt != nullptr && !morphBuf.empty();
        if (!morphActive)
        {
            morphAge = 0;
            return;
        }
        const int rebuildEvery = tick.lowMorph ? 2 * kMorphRebuild : kMorphRebuild;
        if (firstOfNote)
            morphAge = rebuildEvery; // build now, no cross-fade from stale data
        if (morphAge >= rebuildEvery)
        {
            const Wavetable& wt = *tick.wt;
            const int pairs = (curCount + 1) / 2;
            const int groups = tick.lowMorph && tick.shareMorph ? 1 : std::min (kMorphGroups, std::max (1, pairs)); // tier low: the unison voices share one morph cycle (no morph spread across them)
            morphGroups = groups;
            const bool warped = tick.oversample || warpNeedsOversampling (tick.warp[0]) || warpNeedsOversampling (tick.warp[1]);
            const float nyq = static_cast<float> (warped ? (tick.lowWarp ? 1.0 : 3.2) * sampleRate : 0.5 * sampleRate);
            float fmax = 1.0f, incMax = 0.0f;
            for (int v = 0; v < curCount; ++v)
            {
                fmax = std::max (fmax, curInc[v] * static_cast<float> (sampleRate));
                incMax = std::max (incMax, curInc[v]);
            }
            const int K = std::min (Wavetable::kHarmonics - 1, static_cast<int> (nyq / fmax));
            for (int g = 0; g < groups; ++g)
            {
                const float spreadShift = groups > 1 ? tick.morphSpread * 2.0f * static_cast<float> (g) / static_cast<float> (groups - 1) : 0.0f;
                const float framePos = tick.frame * static_cast<float> (std::max (wt.numFrames() - 1, 0));
                MorphCache::Key key;
                key.wt = &wt;
                key.framePos = framePos;
                key.m = clamp01 (tick.morphAmount + spreadShift);
                key.phaseInc = tick.morphType == kMorphVocode ? incMax : 0.0f; // only Vocode reads the phase increment (buildMorphCycle): the other types share their cycles across pitches of the same band limit
                key.type = tick.morphType;
                key.maxHarmonic = K;
                key.hostRate = sampleRate;
                key.fast = tick.lowMorph;
                // A held morph (this group's last cycle was built from the very same arguments, and nothing has written its buffer since): buildMorphCycle () is a function of its arguments
                // alone, so the rebuild would give the cycle that is already in the new buffer, and the old buffer (the previous new one) would hold the same samples. Then the cross-fade
                // is lerp (y, y, w) = y + (y - y) * w = y exactly, so nothing is rebuilt and render () reads the one cycle (morphHeld).
                if (!reference_ && !firstOfNote && morphLastKeyOk[g] && sameMorphKey (morphLastKey[g], key))
                {
                    morphHeld[g] = true;
                    continue;
                }
                if (!firstOfNote)
                {
                    if (reference_)
                        std::copy (morphNew (g) - 1, morphNew (g) - 1 + kMorphStride, morphOld (g) - 1); // the previous "new" buffer becomes the old one
                    else
                        morphNewSlot[g] ^= 1; // the same without the copy: the two buffers swap roles, and the new one is written in full below
                }
                float* a = morphOld (g);
                float* b = morphNew (g);
                if (g > 0 && spreadShift == 0.0f)
                {
                    // no morph spread: every group asks for the same cycle, so the first one is copied (the same samples, built once)
                    const float* first = morphNew (0);
                    std::copy (first - 1, first - 1 + kMorphStride, b - 1);
                }
                else
                {
                    // a morph that moves (this build asks for another frame or amount than the last one of this oscillator) never asks for the same cycle again: only a cycle of a
                    // held morph is stored for the other voices of a chord (the cache only saves time: a hit has the samples of a build)
                    const bool moving = morphKeyOk[g] && (!sameBits (morphKeyFrame[g], framePos) || !sameBits (morphKeyM[g], key.m));
                    morphKeyOk[g] = true;
                    morphKeyFrame[g] = framePos;
                    morphKeyM[g] = key.m;
                    if (cache == nullptr || !cache->fetch (key, b - 1))
                    {
                        SB_SECTION (sections::oscMorphBuild);
                        if (morphScratchPtr != nullptr)
                            buildMorphCycle (wt, framePos, true, tick.morphType, key.m, K, incMax, sampleRate, b, *morphScratchPtr, tick.lowMorph, tick.scalarWarp, reference_);
                        if (cache != nullptr && (reference_ || !moving))
                            cache->store (key, b - 1);
                    }
                }
                if (reference_)
                {
                    if (firstOfNote)
                        std::copy (b - 1, b - 1 + kMorphStride, a - 1);
                }
                else
                {
                    morphLastKey[g] = key;
                    morphLastKeyOk[g] = true;
                    morphHeld[g] = firstOfNote; // a new note: after the first build the old and the new cycle are the same, so the old one is not written and the new one is read alone
                }
            }
            // after the build of the first period the old and the new cycle are the same (the weight of the cross-fade does not matter): the age starts at the voice's slot, so its next build
            // comes that many periods earlier, and the builds of a chord's voices fall in different periods
            morphAge = firstOfNote ? morphStaggerSlot (morphStagger, rebuildEvery / kControlPeriod) * kControlPeriod : 0;
        }
    }

    /** Renders samples t0 .. t0 + n - 1 of the period into L and R (overwritten). `cross` are the signals of the other two oscillators (may be null). */
    void render (int t0, int n, float* L, float* R, const float* const* cross, double sampleRate) noexcept
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        if (pending || tick.wt == nullptr || tick.level <= 0.0f) // pending: the last tick (not yet applied) is silent
            return;
        const bool aaOn = (synthyboy::aa::off() & synthyboy::aa::kWarpAdaa) == 0;
        const bool blepOn = (synthyboy::aa::off() & synthyboy::aa::kBlep) == 0;
        const bool blampOn = (synthyboy::aa::off() & synthyboy::aa::kBlamp) == 0;
        const Wavetable& wt = *tick.wt;
        const int frames = wt.numFrames();
        const double nyq = 0.5 * sampleRate;
        // a warp can create content far above the pitch: the oscillator then runs at four times the rate, with a band limited decimation
        const bool warped = tick.oversample || warpNeedsOversampling (tick.warp[0]) || warpNeedsOversampling (tick.warp[1]);
        // the decimator history is sized for the tier's unison count: the oversampled path never goes past it
        const int count = warped ? std::min (std::max (curCount, prevCount), historyVoices) : std::max (curCount, prevCount);
        const float invP = 1.0f / static_cast<float> (kControlPeriod);
        const int cs0 = tick.warpSrc[0], cs1 = tick.warpSrc[1];
        const bool morphing = morphActive;
        SB_SECTION (warped ? sections::oscWarp : (morphing ? sections::oscMorph : sections::oscTable));
        const int morphPairs = std::max (1, (curCount + 1) / 2);
        // (k + 1) / n of the weight of the second cycle: for a power of two n (the 32 sample chunks) the quotient is exactly (k + 1) * (1 / n) (both are exact scalings by 2^-m of an
        // integer below 2^24), so the divide is made once; for any other n the division stays
        const bool nPow2 = !reference_ && n > 0 && (n & (n - 1)) == 0;
        const float invN = 1.0f / static_cast<float> (n);
        auto stepFrac = [&] (int i) noexcept { return nPow2 ? static_cast<float> (i) * invN : static_cast<float> (i) / static_cast<float> (n); };
        // the same for every lane of this call
        float bw = 1.0f;
        for (int w = 0; w < 2; ++w)
            bw = std::max (bw, warpBandwidthFactor (tick.warp[w], std::clamp (tick.warpAmount[w] + std::max (0.0f, tick.warpSpread[w]), 0.0f, 1.0f)));
        const int os = tick.lowWarp ? kOversampleLow : kOversample, nt = tick.lowWarp ? kTapsLow : kTaps;
        const double rateI = sampleRate * (warped ? os : 1);
        // the decimator of the oversampled path: the 4x (or 2x) signal -> the output rate, for the window x of nt - 1 + os * n samples: acc of group k is the dot product with the taps
        const float* const taps = tick.lowWarp ? decimatorTaps2() : decimatorTaps();
        auto decimate = [&] (const float* x) noexcept
        {
            float acc = 0.0f;
            if (tick.lowWarp)
                acc = dot32 (x, kDecimator2Rev.c); // tier low: the same taps, four partial sums (rounding differs from the sequential sum by about 1e-7 relative)
            else if (tick.fastMath)
                acc = dot128 (x, kDecimator4Rev.c); // the 4x warp of a profile that keeps the Math economy group: the 128 taps in four partial sums, the same bound
            else
                for (int i = 0; i < nt; ++i)
                    acc += taps[i] * x[nt - 1 - i];
            return acc;
        };
        // Shared decimation: the decimator is linear and the same for every voice, so the voices whose gain does not change inside this call (previous gain == current gain in both
        // channels: out = g * dec (x)) are summed at the oversampled rate, weighted by their gains, and decimated once per channel instead of once per voice. A voice whose gain
        // ramps keeps its own decimator: the gain is applied after the decimation, and a gain ramp does not commute with the filter. The sum is formed over the whole window
        // (the per voice history and this period), so the per voice history stays as it was. Not bit identical to the per voice form: the float sums are in another order (about 1e-7 relative).
        auto steadyGain = [&] (int v) noexcept { return curGainL[v] == prevGainL[v] && curGainR[v] == prevGainR[v]; };
        bool shareDec = false;
        // only the 4x decimator with the sequential 128 tap sum (tiers medium / high / XL): the 32 tap 2x decimator and the four partial sum dot product of the Math economy group are
        // already cheap against the two accumulations (host A/B/A, no gain), so tier low keeps its bits
        if (warped && sharedDecimator_ && !tick.lowWarp && !tick.fastMath && !history.empty())
        {
            int steady = 0;
            for (int v = 0; v < count; ++v)
                if (steadyGain (v) && (curGainL[v] != 0.0f || curGainR[v] != 0.0f))
                    ++steady;
            shareDec = steady >= kSharedDecimatorMin;
        }
        constexpr int kWindowMax = kTaps - 1 + kOversample * kControlPeriod;
        float sumL[kWindowMax], sumR[kWindowMax]; // written by the first shared voice, read only after it
        bool sumInit = false;
        for (int v = 0; v < count; ++v)
        {
            const float a0 = curWarpA[0][v], a1 = curWarpA[1][v];
            if (curGainL[v] == 0.0f && curGainR[v] == 0.0f && prevGainL[v] == 0.0f && prevGainR[v] == 0.0f)
            {
                warpAa[v][0] = warpAa[v][1] = warpadaa::State{};
                blepPend[v] = BlepPending{};
                // silent voice: advance the phase only
                {
                    // the phase advance of n samples at the final increment, modulo 2^32, without a 64 bit conversion (a library call on ARM): the double product is exact
                    const double adv = static_cast<double> (curInc[v]) * 4294967296.0 * n;
                    const uint32_t wraps = static_cast<uint32_t> (adv * (1.0 / 4294967296.0));
                    phase[v] += static_cast<uint32_t> (adv - static_cast<double> (wraps) * 4294967296.0);
                }
                continue;
            }
            const float fmax = std::max (curInc[v], prevInc[v]) * static_cast<float> (sampleRate) * bw;
            // band limit: the coarsest level that keeps the highest harmonic below Nyquist (of the rate the oscillator runs at); levelFor () and size () are functions of this one
            // number, so the lane keeps the last answer for it
            const float ratio = static_cast<float> (warped ? (tick.lowWarp ? 1.0 : 3.2) * sampleRate : nyq) / std::max (fmax, 1.0f);
            int level, N;
            if (!reference_ && laneRatioOk[v] && sameBits (laneRatio[v], ratio))
            {
                level = laneLevel[v];
                N = laneSize[v];
            }
            else
            {
                level = Wavetable::levelFor (ratio);
                N = Wavetable::size (level);
                laneRatio[v] = ratio;
                laneLevel[v] = level;
                laneSize[v] = N;
                laneRatioOk[v] = true;
            }
            // two cycles and the weight of the second one at the start / end of this chunk
            const float* cA = nullptr;
            const float* cB = nullptr;
            float wB0 = 0.0f, wB1 = 0.0f;
            const float tEnd = static_cast<float> (t0 + n) * invP, tStart = static_cast<float> (t0) * invP;
            if (morphing)
            {
                const int g = std::min (morphGroups - 1, (v / 2) * morphGroups / morphPairs);
                if (!reference_ && morphHeld[g])
                {
                    cA = morphNew (g); // held: the old cycle is the new one, the cross-fade of two equal cycles is that cycle (see updateMorph ())
                    cB = nullptr;
                }
                else
                {
                    cA = morphOld (g);
                    cB = morphNew (g);
                    const float len = static_cast<float> (kMorphRebuild);
                    wB0 = std::min (1.0f, (static_cast<float> (morphAge) + tStart * kControlPeriod) / len);
                    wB1 = std::min (1.0f, (static_cast<float> (morphAge) + tEnd * kControlPeriod) / len);
                }
                N = Wavetable::kCycle;
            }
            else
            {
                if (frames < 2)
                {
                    cA = wt.cycle (0, level);
                    cB = nullptr;
                    wB0 = wB1 = 0.0f;
                }
                else if (tick.smooth)
                {
                    const float fpos = curFrameOf[v] * static_cast<float> (frames - 1);
                    const float pfpos = prevFrameOf[v] * static_cast<float> (frames - 1);
                    if (std::fabs (fpos - pfpos) >= 1.0f)
                    {
                        const int fold = std::clamp (static_cast<int> (std::round (pfpos)), 0, frames - 1);
                        const int fnew = std::clamp (static_cast<int> (std::round (fpos)), 0, frames - 1);
                        cA = wt.cycle (fold, level);
                        cB = fnew != fold ? wt.cycle (fnew, level) : nullptr;
                        wB0 = tStart;
                        wB1 = tEnd;
                    }
                    else
                    {
                        const int f0 = std::min (static_cast<int> (fpos), frames - 2);
                        const float ff1 = fpos - static_cast<float> (f0);
                        const float ff0 = std::clamp (pfpos - static_cast<float> (f0), 0.0f, 1.0f);
                        wB0 = lerp (ff0, ff1, tStart);
                        wB1 = lerp (ff0, ff1, tEnd);
                        cA = wt.cycle (f0, level);
                        cB = (wB0 > 1.0e-4f || wB1 > 1.0e-4f) ? wt.cycle (f0 + 1, level) : nullptr;
                    }
                }
                else
                {
                    const float fpos = curFrameOf[v] * static_cast<float> (frames - 1);
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
            // the pole gain of a filter slot: constant during this call (type, amount, f0Hz and the rate do not change inside it), so it is computed once per lane (the reference
            // path computes it per sample as before)
            auto filterGain = [&] (int type, float amount) noexcept
            {
                const float ratio = (type == kWarpFilterLpf ? 1.0f : 0.5f) * std::exp2 (7.0f * amount);
                const float x = std::min (kTwoPif * ratio * f0Hz / static_cast<float> (rateI), 8.0f);
                return x / (1.0f + x);
            };
            const float gain0 = filt0 && !reference_ ? filterGain (tick.warp[0], a0) : 0.0f, gain1 = filt1 && !reference_ ? filterGain (tick.warp[1], a1) : 0.0f;
            auto filterStage = [&] (int slot, int type, float amount, float y) noexcept
            {
                const float g = reference_ ? filterGain (type, amount) : (slot == 0 ? gain0 : gain1);
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
            auto sampleAt = [&] (uint32_t ph, int k, float fk, float wB, bool probe = false) noexcept
            {
                const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
                float window = 1.0f, w2 = 1.0f, pp = p;
                // the other oscillator is interpolated between its samples (fk = position inside the sample k, 1 = at k)
                const float m0 = cs0 >= 0 && cross != nullptr && cross[cs0] ? lerp (k > 0 ? cross[cs0][k - 1] : lastCross[cs0], cross[cs0][k], fk) : 0.0f;
                const float m1 = cs1 >= 0 && cross != nullptr && cross[cs1] ? lerp (k > 0 ? cross[cs1][k - 1] : lastCross[cs1], cross[cs1][k], fk) : 0.0f;
                if (tick.warp[0] != 0)
                    pp = warpPhase (tick.warp[0], pp, a0, tick.warpPhase[0], m0, window, tick.remap, tick.fastMath);
                if (tick.warp[1] != 0)
                {
                    pp = warpPhase (tick.warp[1], pp, a1, tick.warpPhase[1], m1, w2, tick.remap, tick.fastMath);
                    window *= w2;
                }
                auto read = [&] (float q) noexcept
                {
                    q -= ffloor (q);
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
                    y = (!probe && aaOn && warpIsSampleDomain (tick.warp[0]))
                            ? warpadaa::step (tick.warp[0], warpAa[v][0], y, a0, tick.fastMath)
                            : warpSample (tick.warp[0], y, a0, m0, tick.fastMath);
                if (tick.warp[1] != 0)
                    y = (!probe && aaOn && warpIsSampleDomain (tick.warp[1]))
                            ? warpadaa::step (tick.warp[1], warpAa[v][1], y, a1, tick.fastMath)
                            : warpSample (tick.warp[1], y, a1, m1, tick.fastMath);
                if (!probe && filt0)
                    y = filterStage (0, tick.warp[0], a0, y);
                if (!probe && filt1)
                    y = filterStage (1, tick.warp[1], a1, y);
                return y;
            };
            uint32_t ph = phase[v];
            if (!warped && tick.warp[0] == 0 && tick.warp[1] == 0)
            {
                // the plain table read (no warp, no cross signal, no filter stage): the general sampleAt () below with its branches removed. p < 1, so the wrap of the
                // phase (q -= floor (q)) and the window factor 1 change nothing; the weight of the second cycle is only needed where there is one.
                if (reference_)
                    renderPlainReference (L, R, t0, n, ph, cA, cB, wB0, wB1, fscale, N, prevGainL[v], curGainL[v], prevGainR[v], curGainR[v], prevInc[v], curInc[v], invP);
                else
                    renderPlain (L, R, t0, n, ph, cA, cB, wB0, wB1, fscale, N, prevGainL[v], curGainL[v], prevGainR[v], curGainR[v], prevInc[v], curInc[v], invP, nPow2 ? invN : 0.0f);
            }
            else if (!warped)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const float y = sampleAt (ph, k, 1.0f, lerp (wB0, wB1, stepFrac (k + 1)));
                    L[k] += y * lerp (prevGainL[v], curGainL[v], t);
                    R[k] += y * lerp (prevGainR[v], curGainR[v], t);
                    ph += static_cast<uint32_t> (lerp (prevInc[v], curInc[v], t) * 4294967296.0f);
                }
            }
            else
            {
                if (history.empty() || v >= historyVoices)
                    continue;
                float* hist = history.data() + static_cast<size_t> (v) * (kTaps - 1); // the last nt - 1 samples of the os x signal, then this period's
                float buf[kTaps - 1 + kOversample * kControlPeriod];
                std::copy (hist, hist + nt - 1, buf);
                // Sync and Sync Var alone (no second warp, no filter stage, no cross signal): the phase map is two operations, so the sample is read in place instead of through
                // the general sampleAt () (which calls warpPhase (), the table read and warpSample () as functions, each with a switch); the same arithmetic as that path
                const bool syncOnly = (tick.warp[0] == kWarpSync || tick.warp[0] == kWarpSyncVar) && tick.warp[1] == 0;
                const float syncMul = 1.0f + a0 * 11.0f, syncAdd = tick.warp[0] == kWarpSyncVar ? tick.warpPhase[0] : 0.0f;
                const bool syncVar = tick.warp[0] == kWarpSyncVar;
                auto syncSample = [&] (uint32_t phx, float wBk) noexcept
                {
                    const float p = static_cast<float> (phx >> 8) * (1.0f / 16777216.0f);
                    const float x = syncVar ? p * syncMul + syncAdd : p * syncMul;
                    float q = x - ffloor (x); // warpPhase (): frac
                    q -= ffloor (q);          // the read of the table wraps once more
                    const float pos = q * fscale;
                    const int idx = std::clamp (static_cast<int> (pos), 0, N);
                    const float fr = pos - static_cast<float> (idx);
                    float r = hermite (cA + idx, fr);
                    if (cB != nullptr)
                        r = lerp (r, hermite (cB + idx, fr), wBk);
                    return r;
                };
                // FM A / B, Thru-Zero and Linear alone (no second warp, no filter stage): the phase is p + m * a * 0.3, wrapped; no window, and the sample stage of these warps does
                // nothing. The same arithmetic as sampleAt () for this case, read in place; m is the other oscillator's signal interpolated as in sampleAt ()
                const bool fmOnly = !reference_ && tick.warp[1] == 0
                                    && (tick.warp[0] == kWarpFmA || tick.warp[0] == kWarpFmB || tick.warp[0] == kWarpFmTzA || tick.warp[0] == kWarpFmTzB || tick.warp[0] == kWarpFmLinA
                                        || tick.warp[0] == kWarpFmLinB);
                const float* fmCross = cs0 >= 0 && cross != nullptr ? cross[cs0] : nullptr;
                const float fmPrev = cs0 >= 0 ? lastCross[cs0] : 0.0f;
                auto fmSample = [&] (uint32_t phx, int k, float fk, float wBk) noexcept
                {
                    const float p = static_cast<float> (phx >> 8) * (1.0f / 16777216.0f);
                    const float m = fmCross != nullptr ? lerp (k > 0 ? fmCross[k - 1] : fmPrev, fmCross[k], fk) : 0.0f;
                    const float x = p + m * a0 * 0.3f;
                    float q = x - ffloor (x); // warpPhase (): frac
                    q -= ffloor (q);          // the read of the table wraps once more
                    const float pos = q * fscale;
                    const int idx = std::clamp (static_cast<int> (pos), 0, N);
                    const float fr = pos - static_cast<float> (idx);
                    float r = hermite (cA + idx, fr);
                    if (cB != nullptr)
                        r = lerp (r, hermite (cB + idx, fr), wBk);
                    return r;
                };
#if SB_NEON
                // Two warps that only move the phase (Sync, Formant, Bend: the warp presets), no filter stage: the oversampled samples are made four
                // at a time (warpedGroup4: the operations of sampleAt () lane by lane, the same values). The phases and the weights of the second cycle are laid out first,
                // by the integer steps of the scalar loop.
                const bool lanes = !tick.scalarWarp && !filt0 && !filt1 && lanePhaseWarp (tick.warp[0]) && lanePhaseWarp (tick.warp[1])
                                   && std::fabs (tick.warpPhase[0]) < 1000.0f && std::fabs (tick.warpPhase[1]) < 1000.0f;
#else
                const bool lanes = false;
#endif
                const uint32_t ph0 = ph;
                if (lanes)
                {
#if SB_NEON
                    uint32_t phs[kOversample * kControlPeriod + 4];
                    float wbs[kOversample * kControlPeriod + 4];
                    int total = 0;
                    for (int k = 0; k < n; ++k)
                    {
                        const float t = static_cast<float> (t0 + k + 1) * invP;
                        const uint32_t incO = static_cast<uint32_t> (lerp (prevInc[v], curInc[v], t) * (4294967296.0f / static_cast<float> (os)));
                        const float wBk = cB != nullptr ? lerp (wB0, wB1, stepFrac (k + 1)) : 0.0f;
                        for (int j = 0; j < os; ++j)
                        {
                            phs[total] = ph;
                            wbs[total] = wBk;
                            ++total;
                            ph += incO;
                        }
                    }
                    int s4 = 0;
                    for (; s4 + 4 <= total; s4 += 4)
                        warpedGroup4 (buf + nt - 1 + s4, phs + s4, wbs + s4, cA, cB, fscale, N, a0, a1);
                    for (; s4 < total; ++s4) // an odd count at 2x: the last two samples one by one
                        buf[nt - 1 + s4] = sampleAt (phs[s4], s4 / os, static_cast<float> (s4 % os + 1) * (1.0f / static_cast<float> (os)), wbs[s4]);
#endif
                }
                else
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const uint32_t incO = static_cast<uint32_t> (lerp (prevInc[v], curInc[v], t) * (4294967296.0f / static_cast<float> (os)));
                    const float wBk = cB != nullptr ? lerp (wB0, wB1, stepFrac (k + 1)) : 0.0f; // only a second cycle has a weight
                    for (int j = 0; j < os; ++j)
                    {
                        buf[nt - 1 + k * os + j] = syncOnly ? syncSample (ph, wBk)
                                                            : (fmOnly ? fmSample (ph, k, static_cast<float> (j + 1) * (1.0f / static_cast<float> (os)), wBk)
                                                                      : sampleAt (ph, k, static_cast<float> (j + 1) * (1.0f / static_cast<float> (os)), wBk));
                        ph += incO;
                    }
                }
                if (blepOn || blampOn)
                {
                    if (blepPend[v].J != 0.0f || blepPend[v].dS != 0.0f)
                    {
                        const float omD = 1.0f - blepPend[v].d;
                        if (blepPend[v].J != 0.0f)
                            buf[nt - 1] -= 0.5f * blepPend[v].J * omD * omD;
                        if (blepPend[v].dS != 0.0f)
                            buf[nt - 1] += blepPend[v].dS * omD * omD * omD * (1.0f / 6.0f);
                        blepPend[v] = BlepPending{};
                    }
                    WarpEvent events[2];
                    int numEvents = 0;
                    if (cs0 < 0 && cs1 < 0 && !filt0 && !filt1)
                    {
                        auto slotEvents = [] (int type, float amount, WarpEvent* out) noexcept -> int
                        {
                            if (amount == 0.0f && type != kWarpMirror)
                                return 0;
                            return warpEvents (type, amount, out);
                        };
                        if (tick.warp[0] != kWarpNone)
                        {
                            WarpEvent tmp[2];
                            if (slotEvents (tick.warp[1], a1, tmp) == 0)
                                numEvents = slotEvents (tick.warp[0], a0, events);
                        }
                        else
                        {
                            numEvents = slotEvents (tick.warp[1], a1, events);
                        }
                    }
                    bool anyActiveEvent = false;
                    for (int e = 0; e < numEvents; ++e)
                        if (blampOn || events[e].kind == 1)
                            anyActiveEvent = true;
                    if (anyActiveEvent)
                    {
                        constexpr uint32_t kDelta = 1u << 22; // 1/1024 cycle
                        const int totalOs = n * os;
                        uint32_t pCur = ph0;
                        for (int k = 0; k < n; ++k)
                        {
                            const float t = static_cast<float> (t0 + k + 1) * invP;
                            const uint32_t incO = static_cast<uint32_t> (lerp (prevInc[v], curInc[v], t) * (4294967296.0f / static_cast<float> (os)));
                            const float incCycles = static_cast<float> (incO) * (1.0f / 4294967296.0f);
                            const float wBk = cB != nullptr ? lerp (wB0, wB1, stepFrac (k + 1)) : 0.0f;
                            for (int j = 0; j < os; ++j)
                            {
                                const int s = k * os + j;
                                const uint32_t pNext = pCur + incO;
                                for (int e = 0; e < numEvents; ++e)
                                {
                                    const WarpEvent& ev = events[e];
                                    if (!blampOn && ev.kind != 1)
                                        continue;
                                    if (ev.phase != pCur && static_cast<uint32_t> (ev.phase - pCur) <= incO)
                                    {
                                        const float d = static_cast<float> (static_cast<uint32_t> (pNext - ev.phase)) / static_cast<float> (incO);
                                        const float fRight = sampleAt (ev.phase, 0, 1.0f, wBk, true);
                                        const float fLeft = (ev.kind == 1) ? sampleAt (ev.phase - 1u, 0, 1.0f, wBk, true) : fRight;
                                        float J = 0.0f;
                                        if (blepOn && ev.kind == 1)
                                        {
                                            J = fRight - fLeft;
                                            if (std::fabs (J) < 1.0e-6f)
                                                J = 0.0f;
                                        }
                                        float dS = 0.0f;
                                        if (blampOn)
                                        {
                                            const float fPlus = sampleAt (ev.phase + kDelta, 0, 1.0f, wBk, true);
                                            const float fMinus = sampleAt (ev.phase - kDelta, 0, 1.0f, wBk, true);
                                            const float dPlus = (fPlus - fRight) * 1024.0f;
                                            const float dMinus = (fLeft - fMinus) * 1024.0f;
                                            dS = incCycles * (dPlus - dMinus);
                                        }
                                        if (J != 0.0f)
                                            buf[nt - 1 + s] += 0.5f * J * d * d;
                                        if (dS != 0.0f)
                                            buf[nt - 1 + s] += dS * d * d * d * (1.0f / 6.0f);
                                        if (s + 1 < totalOs)
                                        {
                                            const float omD = 1.0f - d;
                                            if (J != 0.0f)
                                                buf[nt - 1 + s + 1] -= 0.5f * J * omD * omD;
                                            if (dS != 0.0f)
                                                buf[nt - 1 + s + 1] += dS * omD * omD * omD * (1.0f / 6.0f);
                                        }
                                        else if (J != 0.0f || dS != 0.0f)
                                        {
                                            blepPend[v].d = d;
                                            blepPend[v].J = J;
                                            blepPend[v].dS = dS;
                                        }
                                    }
                                }
                                pCur = pNext;
                            }
                        }
                    }
                }
                if (shareDec && steadyGain (v))
                {
                    const int len = nt - 1 + n * os;
                    const float gL = curGainL[v], gR = curGainR[v];
                    if (!sumInit)
                    {
                        for (int i = 0; i < len; ++i)
                        {
                            sumL[i] = buf[i] * gL;
                            sumR[i] = buf[i] * gR;
                        }
                        sumInit = true;
                    }
                    else
                        for (int i = 0; i < len; ++i)
                        {
                            sumL[i] += buf[i] * gL;
                            sumR[i] += buf[i] * gR;
                        }
                }
                else
                    for (int k = 0; k < n; ++k)
                    {
                        const float t = static_cast<float> (t0 + k + 1) * invP;
                        const float acc = decimate (buf + k * os + 1); // the newest sample of the group is x[nt - 1]
                        L[k] += acc * lerp (prevGainL[v], curGainL[v], t);
                        R[k] += acc * lerp (prevGainR[v], curGainR[v], t);
                    }
                std::copy (buf + n * os, buf + n * os + nt - 1, hist);
                historyDirty = true;
            }
            phase[v] = ph;
        }
        if (sumInit)
            for (int k = 0; k < n; ++k)
            {
                L[k] += decimate (sumL + k * os + 1);
                R[k] += decimate (sumR + k * os + 1);
            }
        if (cross != nullptr)
            for (int c = 0; c < 6; ++c)
                if (cross[c] != nullptr)
                    lastCross[c] = cross[c][n - 1];
        // the morph age advances at the end of the period
        if (morphActive && t0 + n >= kControlPeriod)
            morphAge += kControlPeriod;
    }

#if SB_NEON
    /** hermite () of four positions at once: base[lane] + idx[lane] is x[0] of the lane. The operations of the scalar function, lane by lane. */
    static float32x4_t hermite4 (const float* const base[4], int32x4_t idx, float32x4_t fr) noexcept
    {
        // rows: [x[-1], x[0], x[1], x[2]] of each lane's position, transposed into x[-1], x[0], x[1], x[2] vectors
        int32_t ii[4];
        vst1q_s32 (ii, idx);
        const float32x4_t r0 = vld1q_f32 (base[0] + ii[0] - 1), r1 = vld1q_f32 (base[1] + ii[1] - 1), r2 = vld1q_f32 (base[2] + ii[2] - 1), r3 = vld1q_f32 (base[3] + ii[3] - 1);
        const float32x4x2_t t01 = vtrnq_f32 (r0, r1), t23 = vtrnq_f32 (r2, r3);
        const float32x4_t xm1 = vcombine_f32 (vget_low_f32 (t01.val[0]), vget_low_f32 (t23.val[0]));
        const float32x4_t x0 = vcombine_f32 (vget_low_f32 (t01.val[1]), vget_low_f32 (t23.val[1]));
        const float32x4_t x1 = vcombine_f32 (vget_high_f32 (t01.val[0]), vget_high_f32 (t23.val[0]));
        const float32x4_t x2 = vcombine_f32 (vget_high_f32 (t01.val[1]), vget_high_f32 (t23.val[1]));
        const float32x4_t half = vdupq_n_f32 (0.5f);
        const float32x4_t c1 = vmulq_f32 (half, vsubq_f32 (x1, xm1));
        float32x4_t c2 = vsubq_f32 (xm1, vmulq_f32 (vdupq_n_f32 (2.5f), x0));
        c2 = vaddq_f32 (c2, vaddq_f32 (x1, x1)); // 2 x1: the sum is exactly the product
        c2 = vsubq_f32 (c2, vmulq_f32 (half, x2));
        const float32x4_t c3 = vaddq_f32 (vmulq_f32 (half, vsubq_f32 (x2, xm1)), vmulq_f32 (vdupq_n_f32 (1.5f), vsubq_f32 (x0, x1)));
        return vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (c3, fr), c2), fr), c1), fr), x0);
    }

    /** ffloor () of dsp/util.h for |x| < 4194304 (the caller sees to that): the truncation, minus one where it went up. */
    static float32x4_t ffloor4 (float32x4_t x) noexcept
    {
        const int32x4_t i = vcvtq_s32_f32 (x);
        const uint32x4_t less = vcltq_f32 (x, vcvtq_f32_s32 (i));
        return vcvtq_f32_s32 (vaddq_s32 (i, vreinterpretq_s32_u32 (less))); // a true lane is all ones: -1
    }
    /** fastmath::sinCycles () of four phases: the same truncation, wrap, table position and interpolation. */
    static float32x4_t sinCycles4 (float32x4_t c) noexcept
    {
        float32x4_t w = vsubq_f32 (c, vcvtq_f32_s32 (vcvtq_s32_f32 (c)));
        w = vbslq_f32 (vcltq_f32 (w, vdupq_n_f32 (0.0f)), vaddq_f32 (w, vdupq_n_f32 (1.0f)), w);
        const float32x4_t pos = vmulq_f32 (w, vdupq_n_f32 (1024.0f));
        const int32x4_t i = vminq_s32 (vcvtq_s32_f32 (pos), vdupq_n_s32 (1023));
        const float32x4_t fr = vsubq_f32 (pos, vcvtq_f32_s32 (i));
        int32_t ii[4];
        vst1q_s32 (ii, i);
        const float lo[4] = { fastmath::kSine.v[ii[0]], fastmath::kSine.v[ii[1]], fastmath::kSine.v[ii[2]], fastmath::kSine.v[ii[3]] };
        const float hi[4] = { fastmath::kSine.v[ii[0] + 1], fastmath::kSine.v[ii[1] + 1], fastmath::kSine.v[ii[2] + 1], fastmath::kSine.v[ii[3] + 1] };
        const float32x4_t a = vld1q_f32 (lo);
        return vaddq_f32 (a, vmulq_f32 (vsubq_f32 (vld1q_f32 (hi), a), fr));
    }
    /** The warps that only move the phase (and, Formant, set a window), for which warpPhase4 () has the four lane form. */
    static bool lanePhaseWarp (int type) noexcept
    {
        return type == kWarpNone || type == kWarpSync || type == kWarpSyncVar || type == kWarpFormant || type == kWarpBend || type == kWarpBendBoth || type == kWarpBendPlus
            || type == kWarpBendMinus;
    }
    /** warpPhase () of warp.h, four phases at once, for the types of lanePhaseWarp (): the same expressions in the same order. `fast`: the table sine of the Math
        economy group (sinCycles4); otherwise the library sine and cosine, called for each lane as the scalar function calls them. */
    static float32x4_t warpPhase4 (int type, float32x4_t p, float a, float ph, float32x4_t& window, bool fast) noexcept
    {
        const float32x4_t half = vdupq_n_f32 (0.5f), inv2pi = vdupq_n_f32 (1.0f / kTwoPif);
        window = vdupq_n_f32 (1.0f);
        auto frac4 = [] (float32x4_t x) noexcept { return vsubq_f32 (x, ffloor4 (x)); };
        auto sn4 = [fast] (float32x4_t c) noexcept
        {
            if (fast)
                return sinCycles4 (c);
            float t[4];
            vst1q_f32 (t, c);
            for (float& v : t)
                v = std::sin (kTwoPif * v);
            return vld1q_f32 (t);
        };
        auto cs4 = [fast] (float32x4_t c) noexcept
        {
            if (fast)
                return sinCycles4 (vaddq_f32 (c, vdupq_n_f32 (0.25f)));
            float t[4];
            vst1q_f32 (t, c);
            for (float& v : t)
                v = std::cos (kTwoPif * v);
            return vld1q_f32 (t);
        };
        switch (type)
        {
            case kWarpSync:    return frac4 (vmulq_f32 (p, vdupq_n_f32 (1.0f + a * 11.0f)));
            case kWarpSyncVar: return frac4 (vaddq_f32 (vmulq_f32 (p, vdupq_n_f32 (1.0f + a * 11.0f)), vdupq_n_f32 (ph)));
            case kWarpFormant:
                window = vsubq_f32 (half, vmulq_f32 (half, cs4 (p)));
                return frac4 (vsubq_f32 (vaddq_f32 (vmulq_f32 (p, vdupq_n_f32 (1.0f + a * 7.0f)), vdupq_n_f32 (ph)), half));
            case kWarpBend: case kWarpBendBoth:
                return vsubq_f32 (p, vmulq_f32 (vmulq_f32 (vdupq_n_f32 ((a - 0.5f) * 1.9f), sn4 (p)), inv2pi));
            case kWarpBendPlus:
                return vaddq_f32 (p, vmulq_f32 (vmulq_f32 (vdupq_n_f32 (a * 0.95f), sn4 (p)), inv2pi));
            case kWarpBendMinus:
                return vsubq_f32 (p, vmulq_f32 (vmulq_f32 (vdupq_n_f32 (a * 0.95f), sn4 (p)), inv2pi));
            default: return p;
        }
    }
    /** Four samples of the oversampled path at the phases phs[0 .. 3] (sampleAt () of render () for two phase warps, no cross signal, no filter stage): the phase
        through the two warps, the wrap, the table read (and the second cycle with the weights wbs), the window. */
    void warpedGroup4 (float* dst, const uint32_t* phs, const float* wbs, const float* cA, const float* cB, float fscale, int N, float a0, float a1) const noexcept
    {
        const float32x4_t p = vmulq_f32 (vcvtq_f32_u32 (vshrq_n_u32 (vld1q_u32 (phs), 8)), vdupq_n_f32 (1.0f / 16777216.0f));
        float32x4_t window = vdupq_n_f32 (1.0f), w2 = window, pp = p;
        if (tick.warp[0] != 0)
            pp = warpPhase4 (tick.warp[0], pp, a0, tick.warpPhase[0], window, tick.fastMath);
        if (tick.warp[1] != 0)
        {
            pp = warpPhase4 (tick.warp[1], pp, a1, tick.warpPhase[1], w2, tick.fastMath);
            window = vmulq_f32 (window, w2);
        }
        const float32x4_t q = vsubq_f32 (pp, ffloor4 (pp));
        const float32x4_t pos = vmulq_f32 (q, vdupq_n_f32 (fscale));
        const int32x4_t idx = vminq_s32 (vmaxq_s32 (vcvtq_s32_f32 (pos), vdupq_n_s32 (0)), vdupq_n_s32 (N));
        const float32x4_t fr = vsubq_f32 (pos, vcvtq_f32_s32 (idx));
        const float* const baseA[4] = { cA, cA, cA, cA };
        float32x4_t r = hermite4 (baseA, idx, fr);
        if (cB != nullptr)
        {
            const float* const baseB[4] = { cB, cB, cB, cB };
            r = vaddq_f32 (r, vmulq_f32 (vsubq_f32 (hermite4 (baseB, idx, fr), r), vld1q_f32 (wbs)));
        }
        vst1q_f32 (dst, vmulq_f32 (r, window));
    }
#endif

    /** The plain table read of one voice over a chunk (no warp): `n` samples from `ph`, added to L / R. The NEON version does four samples at a time with the operations
        of the scalar loop (the phases come from an integer prefix sum of the increments, kept in registers, the four table reads are four loads transposed into lanes).
        A steady voice (the increment and both gains of the previous and the current period are bit equal and finite: lerp (a, a, t) = a + 0 * t = a + 0) takes the steady
        form: the four phases are vph + {0, inc, 2 inc, 3 inc}, the gains are constants, and the ramp t and the increment ramp are not computed (bit identical: the same
        integer phase progression modulo 2^32 and the same gains). `allowSteady` false: the general form always (tests). */
    static void renderPlain (float* L, float* R, int t0, int n, uint32_t& ph, const float* cA, const float* cB, float wB0, float wB1, float fscale, int N, float pg, float cgl,
                             float pgr, float cgr, float pinc, float cinc, float invP, float invN, bool allowSteady = true) noexcept
    {
        // invN: 1 / n when n is a power of two (then (k + 1) / n = (k + 1) * invN exactly), else 0 and the weights are divided
        int k = 0;
#if SB_NEON
        const bool steady = allowSteady && sameBits (pinc, cinc) && sameBits (pg, cgl) && sameBits (pgr, cgr) && std::isfinite (pinc) && std::isfinite (pg) && std::isfinite (pgr);
        if (steady)
            k = renderPlainLanes<true> (L, R, t0, n, ph, cA, cB, wB0, wB1, fscale, N, pg, cgl, pgr, cgr, pinc, cinc, invP, invN);
        else
            k = renderPlainLanes<false> (L, R, t0, n, ph, cA, cB, wB0, wB1, fscale, N, pg, cgl, pgr, cgr, pinc, cinc, invP, invN);
#else
        (void) allowSteady;
#endif
        for (; k < n; ++k)
        {
            const float t = static_cast<float> (t0 + k + 1) * invP;
            const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
            const float pos = p * fscale;
            const int idx = std::clamp (static_cast<int> (pos), 0, N);
            const float fr = pos - static_cast<float> (idx);
            float y = hermite (cA + idx, fr);
            if (cB != nullptr)
                y = lerp (y, hermite (cB + idx, fr), lerp (wB0, wB1, invN > 0.0f ? static_cast<float> (k + 1) * invN : static_cast<float> (k + 1) / static_cast<float> (n)));
            L[k] += y * lerp (pg, cgl, t);
            R[k] += y * lerp (pgr, cgr, t);
            ph += static_cast<uint32_t> (lerp (pinc, cinc, t) * 4294967296.0f); // x * 2^32 is exact in float: the same integer as the double product
        }
    }
#if SB_NEON
    /** The four sample groups of renderPlain (): returns the number of samples done (a multiple of 4), `ph` is advanced past them. Steady: see renderPlain (). */
    template <bool Steady>
    static int renderPlainLanes (float* L, float* R, int t0, int n, uint32_t& ph, const float* cA, const float* cB, float wB0, float wB1, float fscale, int N, float pg, float cgl,
                                 float pgr, float cgr, float pinc, float cinc, float invP, float invN) noexcept
    {
        int k = 0;
        const float32x4_t vwb0 = vdupq_n_f32 (wB0), vdwb = vdupq_n_f32 (wB1 - wB0), vinvN = vdupq_n_f32 (invN);
        const int32_t kfirst[4] = { 1, 2, 3, 4 };
        int32x4_t vk = vld1q_s32 (kfirst); // k + j + 1
        const float32x4_t vinv = vdupq_n_f32 (1.0f / 16777216.0f), vscale = vdupq_n_f32 (fscale), vtwo32 = vdupq_n_f32 (4294967296.0f), vinvP = vdupq_n_f32 (invP);
        // lerp4 (a, b, t) = a + (b - a) * t with the difference formed once
        const float32x4_t vpinc = vdupq_n_f32 (pinc), vdinc = vsubq_f32 (vdupq_n_f32 (cinc), vpinc);
        const float32x4_t vpg = vdupq_n_f32 (pg), vdgl = vsubq_f32 (vdupq_n_f32 (cgl), vpg), vpgr = vdupq_n_f32 (pgr), vdgr = vsubq_f32 (vdupq_n_f32 (cgr), vpgr);
        const int32x4_t vzero = vdupq_n_s32 (0), vN = vdupq_n_s32 (N), vfour = vdupq_n_s32 (4);
        const uint32x4_t uzero = vdupq_n_u32 (0);
        const int32_t first[4] = { t0 + 1, t0 + 2, t0 + 3, t0 + 4 };
        int32x4_t vidx = vld1q_s32 (first); // t0 + k + 1 + lane (simd::ramp4 (t0, k, invP) = this times invP)
        uint32x4_t vph = vdupq_n_u32 (ph);
        // steady: the increment (vinc + vdinc * t with vdinc = 0 and t >= 0 is vinc + 0, the same vector operations here), its multiples for the four lanes and the gains
        const float32x4_t vzf = vdupq_n_f32 (0.0f);
        const uint32x4_t incS = vcvtq_u32_f32 (vmulq_f32 (vaddq_f32 (vpinc, vzf), vtwo32));
        const uint32_t incScalar = vgetq_lane_u32 (incS, 0);
        const uint32_t stepArr[4] = { 0u, incScalar, 2u * incScalar, 3u * incScalar };
        const uint32x4_t step4 = vld1q_u32 (stepArr);
        const uint32x4_t adv4 = vshlq_n_u32 (incS, 2);
        const float32x4_t gSl = vaddq_f32 (vpg, vzf), gSr = vaddq_f32 (vpgr, vzf);
        for (; k + 4 <= n; k += 4)
        {
            uint32x4_t phs;
            float32x4_t t = vzf;
            if (Steady)
            {
                phs = vaddq_u32 (vph, step4);
                vph = vaddq_u32 (vph, adv4);
            }
            else
            {
                t = vmulq_f32 (vcvtq_f32_s32 (vidx), vinvP);
                vidx = vaddq_s32 (vidx, vfour);
                // the increments of the four samples, the phase before each of them (an exclusive prefix sum modulo 2^32) and the phase after the last
                const uint32x4_t inc = vcvtq_u32_f32 (vmulq_f32 (vaddq_f32 (vpinc, vmulq_f32 (vdinc, t)), vtwo32));
                const uint32x4_t s1 = vaddq_u32 (inc, vextq_u32 (uzero, inc, 3));
                const uint32x4_t s2 = vaddq_u32 (s1, vextq_u32 (uzero, s1, 2));
                phs = vaddq_u32 (vph, vextq_u32 (uzero, s2, 3));
                vph = vaddq_u32 (vph, vdupq_lane_u32 (vget_high_u32 (s2), 1));
            }
            const float32x4_t p = vmulq_f32 (vcvtq_f32_u32 (vshrq_n_u32 (phs, 8)), vinv);
            const float32x4_t pos = vmulq_f32 (p, vscale);
            const int32x4_t idx = vminq_s32 (vmaxq_s32 (vcvtq_s32_f32 (pos), vzero), vN);
            const float32x4_t fr = vsubq_f32 (pos, vcvtq_f32_s32 (idx));
            const float* const bA[4] = { cA, cA, cA, cA };
            float32x4_t y = hermite4 (bA, idx, fr);
            if (cB != nullptr)
            {
                const float* const bB[4] = { cB, cB, cB, cB };
                const float32x4_t yB = hermite4 (bB, idx, fr);
                float32x4_t wkv;
                if (invN > 0.0f)
                    wkv = vaddq_f32 (vwb0, vmulq_f32 (vdwb, vmulq_f32 (vcvtq_f32_s32 (vk), vinvN))); // lerp (wB0, wB1, (k + j + 1) / n)
                else
                {
                    float wk[4];
                    for (int j = 0; j < 4; ++j)
                        wk[j] = lerp (wB0, wB1, static_cast<float> (k + j + 1) / static_cast<float> (n));
                    wkv = vld1q_f32 (wk);
                }
                y = simd::lerp4 (y, yB, wkv);
            }
            vk = vaddq_s32 (vk, vfour);
            const float32x4_t gl = Steady ? gSl : vaddq_f32 (vpg, vmulq_f32 (vdgl, t));
            const float32x4_t gr = Steady ? gSr : vaddq_f32 (vpgr, vmulq_f32 (vdgr, t));
            vst1q_f32 (L + k, vaddq_f32 (vld1q_f32 (L + k), vmulq_f32 (y, gl)));
            vst1q_f32 (R + k, vaddq_f32 (vld1q_f32 (R + k), vmulq_f32 (y, gr)));
        }
        ph = vgetq_lane_u32 (vph, 0);
        return k;
    }
#endif

    /** The plain table read as it was before the work of docs/release/CPU-REVIEW-OSC.md (kept for the tests): one voice over a chunk (no warp): `n` samples from `ph`, added to L / R. The NEON version does four samples at a time with the operations
        of the scalar loop (the phases come from an integer prefix sum of the increments, the four table reads are four loads transposed into lanes). */
    static void renderPlainReference (float* L, float* R, int t0, int n, uint32_t& ph, const float* cA, const float* cB, float wB0, float wB1, float fscale, int N, float pg, float cgl,
                             float pgr, float cgr, float pinc, float cinc, float invP) noexcept
    {
        int k = 0;
#if SB_NEON
        const float32x4_t vinv = vdupq_n_f32 (1.0f / 16777216.0f), vscale = vdupq_n_f32 (fscale), vtwo32 = vdupq_n_f32 (4294967296.0f);
        const float32x4_t vpinc = vdupq_n_f32 (pinc), vcinc = vdupq_n_f32 (cinc);
        const float32x4_t vpg = vdupq_n_f32 (pg), vcgl = vdupq_n_f32 (cgl), vpgr = vdupq_n_f32 (pgr), vcgr = vdupq_n_f32 (cgr);
        const int32x4_t vzero = vdupq_n_s32 (0), vN = vdupq_n_s32 (N);
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t t = simd::ramp4 (t0, k, invP);
            // the increments of the four samples and the phase before each of them
            uint32_t inc[4], phs[4];
            vst1q_u32 (inc, vcvtq_u32_f32 (vmulq_f32 (simd::lerp4 (vpinc, vcinc, t), vtwo32)));
            uint32_t acc = ph;
            for (int j = 0; j < 4; ++j)
            {
                phs[j] = acc;
                acc += inc[j];
            }
            ph = acc;
            const float32x4_t p = vmulq_f32 (vcvtq_f32_u32 (vshrq_n_u32 (vld1q_u32 (phs), 8)), vinv);
            const float32x4_t pos = vmulq_f32 (p, vscale);
            const int32x4_t idx = vminq_s32 (vmaxq_s32 (vcvtq_s32_f32 (pos), vzero), vN);
            const float32x4_t fr = vsubq_f32 (pos, vcvtq_f32_s32 (idx));
            const float* const bA[4] = { cA, cA, cA, cA };
            float32x4_t y = hermite4 (bA, idx, fr);
            if (cB != nullptr)
            {
                const float* const bB[4] = { cB, cB, cB, cB };
                const float32x4_t yB = hermite4 (bB, idx, fr);
                float wk[4];
                for (int j = 0; j < 4; ++j)
                    wk[j] = lerp (wB0, wB1, static_cast<float> (k + j + 1) / static_cast<float> (n));
                y = simd::lerp4 (y, yB, vld1q_f32 (wk));
            }
            vst1q_f32 (L + k, vaddq_f32 (vld1q_f32 (L + k), vmulq_f32 (y, simd::lerp4 (vpg, vcgl, t))));
            vst1q_f32 (R + k, vaddq_f32 (vld1q_f32 (R + k), vmulq_f32 (y, simd::lerp4 (vpgr, vcgr, t))));
        }
#endif
        for (; k < n; ++k)
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
            ph += static_cast<uint32_t> (lerp (pinc, cinc, t) * 4294967296.0f); // x * 2^32 is exact in float: the same integer as the double product
        }
    }

    /** Phase of the first voice in cycles (for the "osc" sources and the phase legato). */
    float firstPhase() const noexcept { return static_cast<float> (phase[0] >> 8) * (1.0f / 16777216.0f); }
    /** The 32 bit phases of all the voices (the phase memory and the phase legato carry them from one note to the next). */
    void getPhases (uint32_t* out) const noexcept { for (int i = 0; i < kMaxVoices; ++i) out[i] = phase[i]; }
    void setPhases (const uint32_t* in) noexcept { for (int i = 0; i < kMaxVoices; ++i) phase[i] = in[i]; }
    void setPhase (float p) noexcept
    {
        for (auto& x : phase)
            x = static_cast<uint32_t> (static_cast<double> (p - ffloor (p)) * 4294967296.0);
    }

private:
    static constexpr int kMorphGroups = 2, kMorphStride = Wavetable::kCycle + 4, kMorphRebuild = 256;
    OscTick tick;
    std::vector<float> morphBuf;                 // [group][two buffers][kMorphStride]: the old and the new cycle of a group, by morphNewSlot (the roles swap at a rebuild)
    int morphNewSlot[kMorphGroups] = { 1, 1 };
    bool morphKeyOk[kMorphGroups] = { false, false };
    MorphCache::Key morphLastKey[kMorphGroups] = {};  // the arguments the new cycle of each group was built from (valid: morphLastKeyOk), for the held morph skip of updateMorph ()
    bool morphLastKeyOk[kMorphGroups] = { false, false };
    bool morphHeld[kMorphGroups] = { false, false };  // the old and the new cycle of the group are the same samples: the old buffer is stale and render () reads the new one alone
    static bool sameMorphKey (const MorphCache::Key& a, const MorphCache::Key& b) noexcept
    {
        return a.wt == b.wt && sameBits (a.framePos, b.framePos) && sameBits (a.m, b.m) && sameBits (a.phaseInc, b.phaseInc) && a.type == b.type && a.maxHarmonic == b.maxHarmonic && a.hostRate == b.hostRate && a.fast == b.fast;
    }
    float morphKeyFrame[kMorphGroups] = {}, morphKeyM[kMorphGroups] = {};
    float* morphOld (int g) noexcept { return morphBuf.data() + (static_cast<size_t> (g) * 2 + static_cast<size_t> (morphNewSlot[g] ^ 1)) * kMorphStride + 1; }
    float* morphNew (int g) noexcept { return morphBuf.data() + (static_cast<size_t> (g) * 2 + static_cast<size_t> (morphNewSlot[g])) * kMorphStride + 1; }
    MorphScratch* morphScratchPtr = nullptr;
    std::unique_ptr<MorphScratch> ownedMorphScratch;
    int historyVoices = 0;
    bool sharedDecimator_ = true;                // setSharedDecimator (): tests only
    int morphAge = 0, morphGroups = 1;
    uint32_t morphStagger = 0;                   // setMorphStagger ()
    bool morphActive = false;
    int frameIdx[kMaxVoices] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    float filterState[kMaxVoices][4] = {};
    warpadaa::State warpAa[kMaxVoices][2] = {};
    struct BlepPending { float d = 0.0f, J = 0.0f, dS = 0.0f; };
    BlepPending blepPend[kMaxVoices] = {};
    uint32_t phase[kMaxVoices] = {};
    std::vector<float> history;                  // [voice][kTaps - 1]: the oversampled path's last samples (allocated in prepareMorph ())
    bool historyDirty = false;
    float prevInc[kMaxVoices] = {}, curInc[kMaxVoices] = {}, prevGainL[kMaxVoices] = {}, curGainL[kMaxVoices] = {}, prevGainR[kMaxVoices] = {},
          curGainR[kMaxVoices] = {}, prevFrameOf[kMaxVoices] = {}, curFrameOf[kMaxVoices] = {};
    float curWarpA[2][kMaxVoices] = {};
    float prevFrame = 0.0f, curFrame = 0.0f;
    float lastCross[6] = {};
    int curCount = 1, prevCount = 1;
    bool havePrev = false;
    // a silent tick that update () has not yet applied (see deferSilent ())
    bool pending = false, pendingMorph = false;
    double pendingRate = 0.0;
    OscTick pendingTick;
    void flushPending() noexcept
    {
        if (!pending)
            return;
        pending = false;
        const OscTick t = pendingTick;
        Rng unused;
        update (t, pendingRate, unused);
        if (pendingMorph)
            updateMorph (pendingRate, false, nullptr);
    }
    // the results of the sines and of the detune curve of the last full update (keyed on their inputs: the same values as computing them again); see update ()
    bool reference_ = false;
    static bool sameBits (float a, float b) noexcept { uint32_t x, y; std::memcpy (&x, &a, 4); std::memcpy (&y, &b, 4); return x == y; }
    bool spreadOk = false, panLOk = false, panROk = false, offOk = false;
    float spreadW = 0.0f, spreadSa = 0.0f, spreadSb = 0.0f, panKeyL = 0.0f, panLValue = 0.0f, panKeyR = 0.0f, panRValue = 0.0f;
    int offU = 0, offMode = 0;
    float offCurve = 0.0f, offRange = 0.0f, offValue[kMaxVoices] = {};
    float freqInc[kMaxVoices] = {};
    float laneRatio[kMaxVoices] = {};      // the argument of Wavetable::levelFor () of each lane at its last render and the answers
    int laneLevel[kMaxVoices] = {}, laneSize[kMaxVoices] = {};
    bool laneRatioOk[kMaxVoices] = {};
    double incRate[kMaxVoices] = {};
    float freqSemis[kMaxVoices] = {};   // the pitch of every voice at its last full update () and its frequency in Hz (the double exp2 is not repeated for a held pitch)
    double freqHz[kMaxVoices] = {};
    bool haveFreq[kMaxVoices] = {};
    bool haveLast = false;      // lastT / lastRate describe the inputs of the last full update ()
    OscTick lastT;
    double lastRate = 0.0;
};
} // namespace own
} // namespace synthyboy
