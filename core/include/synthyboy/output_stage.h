// SynthyBoy - the output stage of every shell: the MPC VST2 shell (shell-mpc/output_stage.h adds its plugin setting LIMIT around it), the desktop shells (JUCE VST3 / AU, CLAP) and the offline
// renders that play what a shell plays (tools/preset-render --output-stage, tools/sound-lab "stage=on"). One header, no allocation, no lock, no libm call per sample: safe on the audio thread.
//
// The signal path (docs/MPC.md "Output level" has the reasoning and the measurements):
//   input -> non-finite samples become 0 -> make-up gain (kMakeupGainDb by default, smoothed over kGainSmoothingSeconds) -> look-ahead delay line ----> x gain -> safety clip -> output
//                                                                                     \-> detector (stereo linked) -----------------------------^
// Detector, per sample, in the linear gain domain:
//   p = max (|L|, |R|) after the make-up gain; the gain this sample needs is r = 1 below the ceiling, else kCeiling / p (the reciprocal by an integer seed and three Newton steps from
//   below: no divide instruction, never above the true quotient but for rounding, which a margin of 2^-20 under the ceiling covers);
//   h = the minimum of r over the last W = D + 1 samples (D = the look-ahead in samples; a streaming van Herk / Gil-Werman minimum: O(1) per sample, one pass of W every W samples);
//   (kept as the reduction 1 - gain: fine resolution near no reduction) a two-stage release: the SLOW stage is the reduction a lasting overload needs: it rises towards h at a rate
//   scaled by a weight s that measures how long the overload has lasted (s rises with kSustainSeconds while overloads come within kOverloadHoldSeconds, falls with kSustainFallSeconds),
//   and falls with kReleaseSlowSeconds; the FAST stage follows h down at once and releases with kReleaseFastSeconds, but only down to the slow stage. A drum hit hardly moves the
//   slow stage and recovers fast; a held chord or bass brings the slow stage to the reduction its crests need, so the gain does not move at the rate of the waveform, and its release
//   is slow; a pluck followed by a held sound recovers fast from its attack down to what the held part needs (no loudness inversion as the make-up gain rises);
//   the gain is env quantised down to 2^-22 and smoothed by two cascaded moving averages of B1 and B2 samples (B1 + B2 - 1 = W): the attack is an S-shaped ramp over the look-ahead that
//   ends exactly at the peak (every value the two averages see is at most the r of the sample that leaves the delay line then), so the limited signal never needs the safety clip.
// The output is the delayed sample times that gain; a gain of exactly 1 passes the delayed sample untouched (bit-identical, only delayed by D). While nothing needs a reduction (idle)
// the detector is not run at all. The safety clip at kCeiling after it is the hard guarantee (it also bounds absurd input, above 2^60, that the reciprocal does not cover).
//
// The limiter can be switched off while running (setLimiter): the output is then the gained input WITHOUT the delay; a switch crossfades over kSwitchFadeSeconds between the two
// paths (the detector keeps running in both states, so switching on finds the reduction already computed). latencySamples () is what a shell reports to its host (D when on, else 0).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

// 32 bit ARM and AArch64 Linux (the MPC builds); SB_TEST_NEON builds the same intrinsics on any ARM (the host tests); SB_NO_NEON forces the scalar loop.
#if (((defined(__arm__) || (defined(__aarch64__) && defined(__linux__))) && defined(__ARM_NEON)) || defined(SB_TEST_NEON)) && ! defined(SB_NO_NEON)
 #include <arm_neon.h>
 #define SYNTHYBOY_OUT_NEON 1
#else
 #define SYNTHYBOY_OUT_NEON 0
#endif

namespace synthyboy
{

/** THE make-up gain of every shell, in dB (the MPC build option SYNTHYBOY_MPC_OUTPUT_GAIN_DB overrides it for the MPC shell only when it is given). Change the level of all shells here.
    0 dB (2026-11-19, the owner's rule): the default sound is unity gain staging: with every level control at 0 the track output carries the engine's output as it is, no hidden
    gain and no limiter; the whole level lives in the presets' master.volume (levelled to the stock MPC instruments: the levelling done by hand, docs/release/LEVELLING-STOCK-2026-11-19.md). */
constexpr float kMakeupGainDb = 0.0f;

class OutputStage
{
public:
    static constexpr float kCeiling = 0.94406088f;          // -0.5 dB full scale: room for the inter-sample peaks of a DAC and for a downstream stage that rounds, still within 0.5 dB of full scale
    static constexpr float kLookaheadSeconds = 0.003f;      // 3 ms: 132 samples at 44.1 kHz, 144 at 48 kHz, 288 at 96 kHz
    static constexpr float kReleaseFastSeconds = 0.030f;    // a single transient (with the hold and the weight it measures about 45 ms)
    static constexpr float kReleaseSlowSeconds = 0.300f;    // a sustained overload
    static constexpr float kSlowAttackSeconds = 0.020f;     // how fast the slow stage takes on the reduction of a lasting overload (scaled by the weight s)
    static constexpr float kSustainSeconds = 0.300f;        // the weight s: how long the overload has lasted (rises with this time constant while overloads come within kOverloadHoldSeconds)
    static constexpr float kSustainFallSeconds = 0.300f;    // ... and falls with this one when they stop
    static constexpr float kOverloadHoldSeconds = 0.025f;   // an overload within this time counts as still going on (longer than half a period of 20 Hz)
    static constexpr float kGainSmoothingSeconds = 0.020f;  // the make-up gain after setGainDb
    static constexpr float kSwitchFadeSeconds = 0.010f;     // the crossfade of setLimiter
    static constexpr int kMaxLookahead = 1023;              // samples (the delay ring has 1024)

    /** The make-up gain starts at kMakeupGainDb, the limiter OFF (the default sound: unity gain staging, no dynamics, no latency), prepared for 44.1 kHz. */
    OutputStage() noexcept
    {
        setGainDb (kMakeupGainDb);
        prepare (44100.0);
    }

    /** The look-ahead in samples at a sample rate (what a shell reports as its latency with the limiter on, before prepare () if it must). */
    static int lookaheadFor (double sr) noexcept { return std::clamp (static_cast<int> (std::lround (static_cast<double> (kLookaheadSeconds) * safeRate (sr))), 1, kMaxLookahead); }
    static double safeRate (double sr) noexcept { return (std::isfinite (sr) && sr > 0.0) ? std::clamp (sr, 8000.0, 384000.0) : 44100.0; }

    /** Not on the audio thread while it processes (the shells call it from their prepare). An invalid rate counts as 44.1 kHz. */
    void prepare (double sr) noexcept
    {
        const double rate = safeRate (sr);
        D = lookaheadFor (rate);
        W = D + 1;
        B1 = 1;
        shift1 = 0;
        while (B1 * 2 <= W / 2)
        {
            B1 *= 2;
            ++shift1;
        }
        B2 = W - B1 + 1;
        full2 = static_cast<uint32_t> (B2) * kQ;
        invScale2 = static_cast<float> (1.0 / (static_cast<double> (B2) * static_cast<double> (kQ)));
        cFast = static_cast<float> (std::exp (-1.0 / (kReleaseFastSeconds * rate)));
        cSlow = static_cast<float> (std::exp (-1.0 / (kReleaseSlowSeconds * rate)));
        sRise = static_cast<float> (std::exp (-1.0 / (kSustainSeconds * rate)));
        sFall = static_cast<float> (std::exp (-1.0 / (kSustainFallSeconds * rate)));
        kBaseAttack = static_cast<float> (1.0 - std::exp (-1.0 / (kSlowAttackSeconds * rate)));
        smCoef = static_cast<float> (std::exp (-1.0 / (kGainSmoothingSeconds * rate)));
        holdN = static_cast<int> (std::lround (kOverloadHoldSeconds * rate));
        fadeInc = static_cast<float> (1.0 / std::max (1.0, std::round (kSwitchFadeSeconds * rate)));
        reset();
    }

    /** Silence in the delay line, no reduction, the make-up gain at its target, the switch where it was asked to be (no fade). */
    void reset() noexcept
    {
        std::memset (dl, 0, sizeof (dl));
        std::memset (dr, 0, sizeof (dr));
        w = 0;
        for (int k = 0; k <= kMaxLookahead + 1; ++k)
        {
            win[k] = 0.0f;
            suf[k] = 0.0f;
        }
        pre = 0.0f;
        j = 0;
        for (int k = 0; k < kRing1; ++k)
            ring1[k] = kQ;
        for (int k = 0; k < kRing2; ++k)
            ring2[k] = kQ;
        i1 = i2 = 0;
        sum1 = static_cast<uint32_t> (B1) * kQ;
        sum2 = full2;
        red = 0.0f;
        base = 0.0f;
        s = 0.0f;
        since = holdN;
        calm = 0;
        idle = true;
        lastG = 1.0f;
        curGain = targetGain.load (std::memory_order_relaxed);
        target = wanted.load (std::memory_order_relaxed);
        mix = target ? 1.0f : 0.0f;
        lastMinGain.store (1.0f, std::memory_order_relaxed);
    }

    /** Lock free from any thread. A gain change takes effect at the start of the next process () block (one-block latency); smoothing starts from that block. */
    void setGainDb (float db) noexcept
    {
        if (! std::isfinite (db))
            db = 0.0f;
        const float clamped = std::clamp (db, -24.0f, 24.0f);
        gDb.store (clamped, std::memory_order_relaxed);
        targetGain.store (clamped == 0.0f ? 1.0f : std::pow (10.0f, clamped / 20.0f), std::memory_order_relaxed);
    }
    float gainDb() const noexcept { return gDb.load (std::memory_order_relaxed); }

    /** Lock free from any thread; the audio thread starts the crossfade at its next process (). */
    void setLimiter (bool on) noexcept { wanted.store (on, std::memory_order_relaxed); }
    bool limiter() const noexcept { return wanted.load (std::memory_order_relaxed); }
    /** The delay the stage adds while the limiter is on (what a shell reports to its host), in samples at the prepared rate. */
    int latencySamples() const noexcept { return limiter() ? D : 0; }
    /** The look-ahead in samples whatever the switch says. */
    int lookaheadSamples() const noexcept { return D; }
    /** The deepest reduction of the last block in dB (0 while the limiter is off). Any thread. */
    float gainReductionDb() const noexcept
    {
        if (! limiter())
            return 0.0f;
        const float g = lastMinGain.load (std::memory_order_relaxed);
        return g >= 1.0f ? 0.0f : -20.0f * std::log10 (std::max (g, 1e-9f));
    }
    /** The limiter gain applied to the last sample (1 = none): the tests read it sample by sample. */
    float lastGain() const noexcept { return lastG; }
    /** How often the safety clip had to act on a limited sample (the tests expect 0: the limiter holds the ceiling by itself). */
    uint32_t safetyClips() const noexcept { return clips; }
    /** The groups of four the NEON form did (0 without it): the tests check that they cover it. */
    uint32_t vectorGroups() const noexcept { return laneGroups; }

    /** true: the scalar per sample loop for every block (the reference the NEON form is tested against). */
    bool scalarOnly = false;

    /** In place; l or r may be null (mono), l == r is one mono channel.
        Target gain is latched once per block at the top of process () (one-block latency for setGainDb). */
    void process (float* l, float* r, int n) noexcept
    {
        if (n <= 0 || (l == nullptr && r == nullptr))
            return;
        if (l == r)
            r = nullptr;
        target = wanted.load (std::memory_order_relaxed);
        const float tg = targetGain.load (std::memory_order_relaxed);
        minG = 1.0f;
        int i = 0;
#if SYNTHYBOY_OUT_NEON
        if (! scalarOnly)
            i = processLanes (l, r, n, tg);
#endif
        for (; i < n; ++i)
            step (l, r, i, tg);
        lastMinGain.store (minG, std::memory_order_relaxed);
    }

private:
    static constexpr uint32_t kQ = 1u << 22;
    static constexpr int kRing = 1024, kMask = kRing - 1;
    static constexpr int kRing1 = 512, kRing2 = 1024;
    static constexpr float kCeilingSafe = kCeiling * (1.0f - 1.0f / 1048576.0f);
    static constexpr float kMaxPeak = 1.152921504606846976e18f;   // 2^60
    static constexpr float kSnap = 1.0e-5f;

    /** 1 / p from below for p in (0.9, 2^60]: an integer seed (relative error below 13 %), then three Newton steps (each squares the error and stays below the true value). */
    static float recipBelow (float p) noexcept
    {
        uint32_t u;
        std::memcpy (&u, &p, 4);
        u = 0x7EF311C7u - u;
        float y;
        std::memcpy (&y, &u, 4);
        y = y * (2.0f - p * y);
        y = y * (2.0f - p * y);
        y = y * (2.0f - p * y);
        return y;
    }

    /** The detector for one sample of peak p: returns the gain for the sample that leaves the delay line now. */
    float detect (float p) noexcept
    {
        float rq = 1.0f;
        if (p > kCeiling)
            rq = kCeilingSafe * recipBelow (std::min (p, kMaxPeak));
        // the reduction 1 - gain is what the detector keeps (fine resolution near no reduction, where a gain near 1 would get stuck one float step under it)
        const float rd = 1.0f - rq;
        win[j] = rd;
        pre = std::max (pre, rd);
        const float hr = std::max (pre, suf[j + 1]);
        if (++j == W)
        {
            float m = 0.0f;
            for (int k = W - 1; k >= 0; --k)
            {
                m = std::max (m, win[k]);
                suf[k] = m;
            }
            pre = 0.0f;
            j = 0;
        }
        if (hr > 0.0f)
            since = 0;
        else if (since < holdN)
            ++since;
        if (since < holdN)
            s = 1.0f + (s - 1.0f) * sRise;
        else
            s = s * sFall;
        // the slow stage: the reduction a lasting overload needs. It follows hr up at a rate that grows with s (a single hit hardly moves it, a held chord or bass brings it to the
        // reduction its crests need within some tens of milliseconds) and back down with the slow release
        if (hr > base)
            base += (hr - base) * (kBaseAttack * s);
        else
        {
            base = hr + (base - hr) * cSlow;
            if (base - hr < kSnap)
                base = hr;
        }
        // the fast stage: down at once to what this sample needs, back up with the fast release, but not above the slow stage
        if (hr >= red)
            red = hr;
        else
        {
            const float fl = std::max (hr, base);
            red = fl + (red - fl) * cFast;
            if (red - fl < kSnap)
                red = fl;
        }
        // the gain quantised DOWN to 2^-22: the reduction rounded up
        const float rq22 = red * static_cast<float> (kQ);
        uint32_t t = static_cast<uint32_t> (rq22);
        if (static_cast<float> (t) < rq22)
            ++t;
        const uint32_t q = kQ - std::min (t, kQ);
        sum1 = sum1 + q - ring1[i1];
        ring1[i1] = q;
        if (++i1 == B1)
            i1 = 0;
        const uint32_t a1 = sum1 >> shift1;
        sum2 = sum2 + a1 - ring2[i2];
        ring2[i2] = a1;
        if (++i2 == B2)
            i2 = 0;
        const float g = sum2 == full2 ? 1.0f : static_cast<float> (sum2) * invScale2;
        if (hr == 0.0f && red == 0.0f && base == 0.0f)
        {
            if (++calm >= W && sum2 == full2)
            {
                idle = true;   // every value of the window and of both averages is 1: the state is that of reset (but the delay line), s and the hold start again
                s = 0.0f;
                since = holdN;
                calm = 0;
            }
        }
        else
            calm = 0;
        return g;
    }

    static float clip (float x, uint32_t& count) noexcept
    {
        if (x > kCeiling)
        {
            ++count;
            return kCeiling;
        }
        if (x < -kCeiling)
        {
            ++count;
            return -kCeiling;
        }
        return x;
    }

    /** One sample: the reference loop body, also what the NEON form falls back to wherever its conditions do not hold.
        tg is the block-latched target gain from process () (one-block latency for setGainDb). */
    void step (float* l, float* r, int i, float tg) noexcept
    {
        if (curGain != tg)
        {
            curGain = tg + (curGain - tg) * smCoef;
            if (std::fabs (curGain - tg) < 1e-6f * tg)
                curGain = tg;
        }
        float a = (l != nullptr) ? l[i] : 0.0f;
        float b = (r != nullptr) ? r[i] : 0.0f;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;
        a *= curGain;
        b *= curGain;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;

        dl[w] = a;
        dr[w] = b;
        const int rd = (w - D) & kMask;
        const float da = dl[rd], db = dr[rd];
        w = (w + 1) & kMask;

        const float p = std::max (std::fabs (a), std::fabs (b));
        float g = 1.0f;
        if (! idle || p > kCeiling)
        {
            idle = false;
            g = detect (p);
        }
        lastG = g;

        const float on = target ? 1.0f : 0.0f;
        if (mix != on)
        {
            mix = target ? mix + fadeInc : mix - fadeInc;
            if (mix > 1.0f) mix = 1.0f;
            if (mix < 0.0f) mix = 0.0f;
        }
        float oa, ob;
        if (mix == 0.0f)
        {
            oa = a;
            ob = b;
        }
        else
        {
            if (g < minG)
                minG = g;
            float la = da, lb = db;
            if (g != 1.0f)
            {
                la = da * g;
                lb = db * g;
            }
            if (mix == 1.0f)
            {
                oa = clip (la, clips);
                ob = clip (lb, clips);
            }
            else
            {
                oa = a + (la - a) * mix;
                ob = b + (lb - b) * mix;
                if (target)
                {
                    uint32_t fadeClips = 0;   // the unlimited path in the crossfade towards ON is clipped: not a fault of the limiter
                    oa = clip (oa, fadeClips);
                    ob = clip (ob, fadeClips);
                }
            }
        }
        if (l != nullptr) l[i] = oa;
        if (r != nullptr) r[i] = ob;
    }

#if SYNTHYBOY_OUT_NEON
    /** x with a non finite lane turned into +0, multiplied by g, and a result that overflowed to infinity turned into +0: the two isfinite tests and the multiply of step (), four samples
        at a time (integer compares on the bit patterns: GCC for 32 bit ARM has no NEON float compare). The multiply is always made, so flush-to-zero acts on it like on the scalar one. */
    static float32x4_t finiteMul (float32x4_t x, float32x4_t g) noexcept
    {
        const uint32x4_t inf = vdupq_n_u32 (0x7f800000u), absMask = vdupq_n_u32 (0x7fffffffu);
        const uint32x4_t fin = vcltq_u32 (vandq_u32 (vreinterpretq_u32_f32 (x), absMask), inf);
        const float32x4_t y = vmulq_f32 (vreinterpretq_f32_u32 (vandq_u32 (vreinterpretq_u32_f32 (x), fin)), g);
        const uint32x4_t fin2 = vcltq_u32 (vandq_u32 (vreinterpretq_u32_f32 (y), absMask), inf);
        return vreinterpretq_f32_u32 (vandq_u32 (vreinterpretq_u32_f32 (y), fin2));
    }
    static uint32_t bitsOf (float c) noexcept
    {
        uint32_t u;
        std::memcpy (&u, &c, 4);
        return u;
    }
    /** The groups of four in the steady state: the make-up gain settled, no crossfade, the detector idle, no sample of the group above the ceiling (|x| <= kCeiling as an integer compare
        of the magnitude bits: the samples are finite here) and the four slots of the delay ring contiguous. There the output is the delayed samples (limiter on) or the gained ones (off)
        and nothing else changes: exactly what four calls of step () give. Any other group goes through step (). Returns the number of samples done (n). */
    int processLanes (float* l, float* r, int n, float tg) noexcept
    {
        int i = 0;
        const uint32x4_t absMask = vdupq_n_u32 (0x7fffffffu), c = vdupq_n_u32 (bitsOf (kCeiling));
        const float32x4_t zero = vdupq_n_f32 (0.0f);
        while (i < n)
        {
            const int rd = (w - D) & kMask;
            if (i + 4 > n || ! idle || curGain != tg || mix != (target ? 1.0f : 0.0f) || w + 4 > kRing || rd + 4 > kRing)
            {
                step (l, r, i, tg);
                ++i;
                continue;
            }
            const float32x4_t g = vdupq_n_f32 (curGain);
            const float32x4_t a = l != nullptr ? finiteMul (vld1q_f32 (l + i), g) : finiteMul (zero, g);
            const float32x4_t b = r != nullptr ? finiteMul (vld1q_f32 (r + i), g) : finiteMul (zero, g);
            const uint32x4_t under = vandq_u32 (vcleq_u32 (vandq_u32 (vreinterpretq_u32_f32 (a), absMask), c), vcleq_u32 (vandq_u32 (vreinterpretq_u32_f32 (b), absMask), c));
            const uint32x2_t hh = vand_u32 (vget_low_u32 (under), vget_high_u32 (under));
            if ((vget_lane_u32 (hh, 0) & vget_lane_u32 (hh, 1)) == 0u)
            {
                for (int k = 0; k < 4; ++k)
                    step (l, r, i + k, tg);
                i += 4;
                continue;
            }
            vst1q_f32 (dl + w, a);
            vst1q_f32 (dr + w, b);
            const float32x4_t da = vld1q_f32 (dl + rd), db = vld1q_f32 (dr + rd);   // after the store: a look-ahead below 4 reads this group's own samples, as step () does
            w = (w + 4) & kMask;
            lastG = 1.0f;
            ++laneGroups;
            if (mix == 1.0f)
            {
                if (l != nullptr) vst1q_f32 (l + i, da);
                if (r != nullptr) vst1q_f32 (r + i, db);
            }
            else
            {
                if (l != nullptr) vst1q_f32 (l + i, a);
                if (r != nullptr) vst1q_f32 (r + i, b);
            }
            i += 4;
        }
        return i;
    }
#endif

    // the delay line
    float dl[kRing] = {}, dr[kRing] = {};
    int w = 0;
    // the window minimum (van Herk / Gil-Werman, streaming)
    float win[kMaxLookahead + 2] = {}, suf[kMaxLookahead + 2] = {};   // reductions (1 - the needed gain)
    float pre = 0.0f;
    int j = 0;
    // the two moving averages of the quantised envelope
    uint32_t ring1[kRing1] = {}, ring2[kRing2] = {};
    uint32_t sum1 = 0, sum2 = 0, full2 = 0;
    int i1 = 0, i2 = 0, B1 = 1, B2 = 1, shift1 = 0;
    float invScale2 = 1.0f;
    // envelope, release crossfade
    float red = 0.0f, base = 0.0f, s = 0.0f, cFast = 0.0f, cSlow = 0.0f, sRise = 0.0f, sFall = 0.0f, kBaseAttack = 0.0f;
    int since = 0, holdN = 1, calm = 0, D = 1, W = 2;
    bool idle = true;
    // make-up gain, switch
    static_assert (std::atomic<float>::is_always_lock_free, "output stage atomics must be lock free (audio thread)");
    std::atomic<float> gDb { 0.0f };
    std::atomic<float> targetGain { 1.0f };
    float curGain = 1.0f, smCoef = 0.99886686f;
    float mix = 1.0f, fadeInc = 0.01f, minG = 1.0f, lastG = 1.0f;
    bool target = true;
    uint32_t clips = 0, laneGroups = 0;
    std::atomic<bool> wanted { false };
    std::atomic<float> lastMinGain { 1.0f };
};

} // namespace synthyboy
