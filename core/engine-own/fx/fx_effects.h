// SynthyBoy - own engine: the nine effects of the standard chain (chorus, compressor, delay, distortion, EQ, filter, flanger, phaser, reverb).
// Written from public DSP knowledge and tuned against the oracle (tools/own-oracle); original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/fastmath.h"
#include "../dsp/simd.h"
#include "../dsp/neon_cmp.h"
#include <algorithm>
#include <cmath>
#include <array>

#include "../osc/warp.h"
#include "synthyboy/aa_switch.h"
#include "../voice/filter_unit.h"
#include "fx_compressor.h"
#include "fx_dsp.h"
#include "fx_adaa.h"
#include "fx_oversampler.h"
#include "fx_eq.h"
#include "fx_reverb.h"

namespace synthyboy
{
namespace own
{
/** The soft limit inside the feedback loops (document 06): hardtanh with a knee at 1/8 of the range. */
inline float loopLimit (float v) noexcept
{
    return 8.0f * std::clamp (v * 0.125f, -1.0f, 1.0f);
}

/** The loop filter of the delay type effects (document 06, 3): a band pass in the feedback path, a low pass at the upper corner minus a low pass at the lower one, with the
    compensation gain f_lo / f_hi + 1. */
struct LoopBand
{
    OnePole hi, lo;
    float aHi = 1.0f, aLo = 1.0f, gain = 1.0f;
    void reset() noexcept { hi.reset(); lo.reset(); }
    void set (float cutoffHz, float spread, double sr) noexcept
    {
        const float radius = spread * 96.0f;
        const float nyq = static_cast<float> (sr) * 0.4535f;
        const float fHi = std::clamp (cutoffHz * std::exp2 ((radius) / 12.0f), 1.0f, nyq), fLo = std::clamp (cutoffHz * std::exp2 (-radius / 12.0f), 1.0f, nyq);
        aHi = OnePole::coefficient (fHi, sr);
        aLo = OnePole::coefficient (fLo, sr);
        gain = fLo / fHi + 1.0f;
    }
    float process (float x) noexcept
    {
        const float l = hi.process (x * gain, aHi);
        return l - lo.process (l, aLo);
    }
};

/** Every effect: prepare once, setParams once per control period, process in place. `mix` is 0 .. 1; the level (dB) is applied by the chain. */
class Delay
{
public:
    struct Params
    {
        float mix = 0.3f, feedback = 0.4f, timeL = 0.3f, timeR = 0.3f, cutoff = 260.0f, spread = 1.0f, offsetL = 1.0f, offsetR = 1.0f;
        int style = 1;
        bool hq = false;
    };
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& d : line)
            d.prepare (static_cast<int> (sr * 4.2));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (auto& b : band) b.reset();
        fbRead[0] = fbRead[1] = 0.0f;
        primed = false;
    }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const MixGains mg (p.mix);
        const float sr = static_cast<float> (sampleRate);
        // the target periods (seconds) of the two sides; Mid ping pong adds the right time to the left side
        float tL = p.timeL * p.offsetL, tR = (p.style == 0 ? p.timeL : p.timeR) * p.offsetR;
        if (p.style == 0)
            tR = p.timeL * p.offsetR;
        if (p.style == 3)
            tL = tL + tR;
        const float fTarget[2] = { 1.0f / std::clamp (tL, 3.0f / sr, 4.0f), 1.0f / std::clamp (tR, 3.0f / sr, 4.0f) };
        if (!primed)
        {
            fLast[0] = fTarget[0];
            fLast[1] = fTarget[1];
            primed = true;
        }
        const float glide = 1.0f - std::pow (0.5f, 1.0f / (0.02f * sr)); // half life of 20 ms: a change of the time bends the echoes like a tape
        for (auto& b : band)
            b.set (p.cutoff, p.spread, sampleRate);
        const float fb = std::clamp (p.feedback, -1.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
                fLast[c] += (fTarget[c] - fLast[c]) * glide;
            const float period[2] = { std::clamp (sr / fLast[0], 3.0f, 4.0f * sr), std::clamp (sr / fLast[1], 3.0f, 4.0f * sr) };
            float rd[2];
            for (int c = 0; c < 2; ++c)
                rd[c] = p.hq ? line[c].readLagrange8 (period[c]) : line[c].readCubic (period[c]);
            const float xl = l[i], xr = r[i];
            float wl = 0.0f, wr = 0.0f, outL = 0.0f, outR = 0.0f;
            switch (p.style)
            {
                case 4: // tap -> delay: the left side is a tap of the mono input, the right side is a delay fed by the tap and by itself
                {
                    const float mono = 0.70710678f * (xl + xr);
                    wl = mono;
                    wr = loopLimit (rd[0] + fb * rd[1]);
                    outL = outR = rd[0] + rd[1];
                    line[0].push (band[0].process (wl));
                    line[1].push (band[1].process (wr));
                    l[i] = mg.apply (xl, outL);
                    r[i] = mg.apply (xr, outR);
                    continue;
                }
                case 2: // ping pong: the written values change the channel (the right lane feedback is 1, the left one the knob)
                    wl = loopLimit (xl + fb * rd[0]);
                    wr = loopLimit (xr + rd[1]);
                    line[0].push (band[0].process (wr));
                    line[1].push (band[1].process (wl));
                    break;
                case 3: // mid ping pong: the mono sum on the left
                    wl = loopLimit (0.70710678f * (xl + xr) + fb * rd[0]);
                    wr = loopLimit (rd[1]);
                    line[0].push (band[0].process (wr));
                    line[1].push (band[1].process (wl));
                    break;
                default: // mono, stereo
                    wl = loopLimit (xl + fb * rd[0]);
                    wr = loopLimit (xr + fb * rd[1]);
                    line[0].push (band[0].process (wl));
                    line[1].push (band[1].process (wr));
                    break;
            }
            l[i] = mg.apply (xl, rd[0]);
            r[i] = mg.apply (xr, rd[1]);
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    std::array<DelayLine, 2> line;
    LoopBand band[2];
    float fLast[2] = {}, fbRead[2] = {};
    bool primed = false;
};

class Chorus
{
public:
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
    bool scalarLoops = false; // measurements (setOwnSavingsOff, kOwnSavingVectorFilters): the LFO refresh of the taps one by one also where the four lane form exists
    struct Params { float mix = 0.5f, rate = 0.4f, depth = 0.5f, feedback = 0.4f, delay1 = 0.002f, delay2 = 0.008f, cutoff = 260.0f, spread = 1.0f; int voices = 4; };
    static constexpr int kMaxPairs = 4;
    void prepare (double sr)
    {
        sampleRate = sr;
        int size = 16; // the length of a delay line: a power of two (the same rule as DelayLine::prepare)
        while (size < static_cast<int> (sr * 0.085) + 4)
            size <<= 1;
        lineSize = size;
        lineMask = size - 1;
        store.assign (static_cast<size_t> (kMaxPairs * 4) * static_cast<size_t> (size), 0.0f);
        reset();
    }
    void reset() noexcept
    {
        std::fill (store.begin(), store.end(), 0.0f);
        linePos = 0;
        for (int k = 0; k < kMaxPairs * 4; ++k)
            hiY[k] = loY[k] = 0.0f;
        phase = 0.0f;
    }
    /** The LFO runs on while the module is skipped on silence (FxChain, tier low): the same per sample steps as process (). */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float inc = p.rate / sr; // (the same quotient every sample: computed once)
        for (int s = 0; s < n; ++s)
        {
            phase += inc;
            phase -= ffloor (phase);
        }
    }
    void set (const Params& np) noexcept { p = np; }
    /** `voices` pairs of two delay lines (A with delay 1, B with delay 2), each with a left and a right side (document 06, 2). */
    void process (float* l, float* r, int n) noexcept
    {
        const MixGains mg (p.mix);
        const float sr = static_cast<float> (sampleRate);
        const int V = std::clamp (p.voices, 1, kMaxPairs);
        const float fb = std::clamp (p.feedback, -0.95f, 0.95f);
        const float avg = 0.5f * (p.delay1 + p.delay2);
        // the band filter of every tap has the same coefficients (they come from the same parameters): computed once
        LoopBand coef;
        coef.set (p.cutoff, p.spread, sampleRate);
        coefBand = coef;
        // what does not change from sample to sample is computed once: the spread of the voices over the unison (a division each) and the phase offset of every tap
        float tOf[kMaxPairs], phOff[kMaxPairs], baseOf[2];
        for (int i = 0; i < V; ++i)
        {
            tOf[i] = (i > 0 && V > 1) ? static_cast<float> (i) / static_cast<float> (V - 1) : 0.0f;
            phOff[i] = 0.25f * static_cast<float> (i) / static_cast<float> (V);
        }
        baseOf[0] = p.delay1;
        baseOf[1] = p.delay2;
        float period[kMaxPairs * 4] = {}; // the delay of every tap in samples; tier low holds it for four samples (the LFO moves it by a few thousandths of a sample in that time)
        // the phase step: the same quotient every sample (p is a member: written through l and r as far as the compiler knows, so it divided every sample)
        const float inc = p.rate / sr;
        const float depth = p.depth;
#if SB_NEON
        const bool laneRefresh = fast && !scalarLoops; // tier low: the four taps of a voice pair in one register (refreshFour)
#endif
        for (int s = 0; s < n; ++s)
        {
            phase += inc;
            phase -= ffloor (phase);
            const float in[2] = { l[s], r[s] };
            float acc[2] = { 0.0f, 0.0f };
            const bool refresh = !fast || (s & 3) == 0;
#if SB_NEON
            if (refresh && laneRefresh)
                for (int i = 0; i < V; ++i)
                    refreshFour (period + i * 4, phase, phOff[i], baseOf, avg, tOf[i], depth, sr);
            else
#endif
            if (refresh)
                for (int i = 0; i < V; ++i)
                    for (int vi = 0; vi < 2; ++vi)
                        for (int c = 0; c < 2; ++c)
                        {
                            float ph = phase + (c == 1 ? 0.25f : 0.0f) + (vi == 1 ? 0.5f : 0.0f) + phOff[i];
                            ph -= ffloor (ph);
                            const float mod = 1.0f + 0.5f * (fast ? fastmath::sinCycles (ph) : std::sin (kTwoPif * ph));
                            const float delay = mod * 0.03f * depth + lerp (baseOf[vi], avg, tOf[i]);
                            period[((i * 2 + vi) * 2) + c] = std::clamp (delay * sr, 3.0f, 0.08f * sr);
                        }
            // the four taps of a voice pair (left / right of delay 1, left / right of delay 2) are one group: read the cubic, add the feedback, limit, band pass, write.
            // A tap only touches its own delay line and its own filter state, so the groups (and the lanes of the NEON version) are independent; acc is summed in the order of
            // the scalar loop (voice pair, delay, channel).
            for (int i = 0; i < V; ++i)
            {
                const int k0 = i * 4;
                float rd[4];
#if SB_NEON
                tapGroupNeon (k0, period + k0, in, fb, rd);
#else
                readGroup (k0, period + k0, rd);
                float x[4];
                for (int j = 0; j < 4; ++j)
                    x[j] = loopLimit (in[j & 1] + fb * rd[j]);
                bandGroup (k0, x, coef);
                pushGroup (k0, x);
#endif
                acc[0] += rd[0];
                acc[1] += rd[1];
                acc[0] += rd[2];
                acc[1] += rd[3];
            }
            linePos = (linePos + 1) & lineMask;
            l[s] = mg.apply (in[0], 0.53f * acc[0]);
            r[s] = mg.apply (in[1], 0.53f * acc[1]);
        }
    }

#if SB_NEON
    // (public for core/tests/test_own_chorus_lanes.cpp)
    /** ffloor () (dsp/util.h) of four values: truncation and the correction of a negative fraction, with the integer compare of dsp/neon_cmp.h. A lane that ffloor ()
        sends to std::floor (|x| not below 2^22, NaN, a zero) or that is subnormal (where a flush-to-zero compare could differ from the IEEE one) makes the four go
        through ffloor () itself: the same value for every input. */
    static float32x4_t ffloorFour (float32x4_t x) noexcept
    {
        const uint32x4_t mag = vandq_u32 (vreinterpretq_u32_f32 (x), vdupq_n_u32 (neoncmp::kAbsMask));
        const uint32x4_t plain = vandq_u32 (neoncmp::absLt (x, 4194304.0f), vcgeq_u32 (mag, vdupq_n_u32 (0x00800000u))); // normal and below 2^22
        const uint32x2_t both = vand_u32 (vget_low_u32 (plain), vget_high_u32 (plain));
        if ((vget_lane_u32 (both, 0) & vget_lane_u32 (both, 1)) == 0u)
        {
            float t[4];
            vst1q_f32 (t, x);
            for (float& v : t)
                v = ffloor (v);
            return vld1q_f32 (t);
        }
        const int32x4_t i = vcvtq_s32_f32 (x);
        const uint32x4_t less = neoncmp::lt (x, vcvtq_f32_s32 (i));
        return vcvtq_f32_s32 (vaddq_s32 (i, vreinterpretq_s32_u32 (less))); // a true lane is all ones: -1
    }
    /** fastmath::sinCycles () of four phases: the same truncation, wrap, table position and interpolation (the wrap test w < 0 as the integer compare). A subnormal
        phase sends the four through sinCycles () itself: NEON on 32 bit ARM flushes it to zero, the VFP code does so only in flush-to-zero mode. For a normal
        phase no step makes a subnormal (w is the phase itself or a multiple of its ulp), so the lanes compute what the scalar code computes in either mode. */
    static float32x4_t sinCyclesFour (float32x4_t c) noexcept
    {
        const uint32x4_t mag = vandq_u32 (vreinterpretq_u32_f32 (c), vdupq_n_u32 (neoncmp::kAbsMask));
        const uint32x4_t sub = vcltq_u32 (vsubq_u32 (mag, vdupq_n_u32 (1u)), vdupq_n_u32 (0x007fffffu)); // 0 < magnitude < the smallest normal
        const uint32x2_t anySub = vorr_u32 (vget_low_u32 (sub), vget_high_u32 (sub));
        if ((vget_lane_u32 (anySub, 0) | vget_lane_u32 (anySub, 1)) != 0u)
        {
            float t[4];
            vst1q_f32 (t, c);
            for (float& v : t)
                v = fastmath::sinCycles (v);
            return vld1q_f32 (t);
        }
        float32x4_t w = vsubq_f32 (c, vcvtq_f32_s32 (vcvtq_s32_f32 (c)));
        w = vbslq_f32 (neoncmp::lt (w, vdupq_n_f32 (0.0f)), vaddq_f32 (w, vdupq_n_f32 (1.0f)), w);
        const float32x4_t pos = vmulq_f32 (w, vdupq_n_f32 (1024.0f));
        const int32x4_t i = vminq_s32 (vcvtq_s32_f32 (pos), vdupq_n_s32 (1023));
        const float32x4_t fr = vsubq_f32 (pos, vcvtq_f32_s32 (i));
        const float* const tab = fastmath::kSine.v;
        float32x4_t lo = vdupq_n_f32 (0.0f), hi = lo;
        lo = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 0), lo, 0);
        lo = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 1), lo, 1);
        lo = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 2), lo, 2);
        lo = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 3), lo, 3);
        hi = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 0) + 1, hi, 0);
        hi = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 1) + 1, hi, 1);
        hi = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 2) + 1, hi, 2);
        hi = vld1q_lane_f32 (tab + vgetq_lane_s32 (i, 3) + 1, hi, 3);
        return vaddq_f32 (lo, vmulq_f32 (vsubq_f32 (hi, lo), fr));
    }
    /** The LFO refresh of the four taps of voice pair i (tier low, the table sine) in one register: lane j is tap (vi, c) = (j / 2, j % 2), and every lane does the
        operations of the scalar loop in its order: ph = phase + c offset + vi offset + phOff, ph -= ffloor (ph), mod = 1 + 0.5 sinCycles (ph),
        delay = mod * 0.03 * depth + lerp (base of vi, avg, t), period = clamp (delay * sr, 3, 0.08 sr) (the clamp with the integer compares: std::clamp's result
        for every value, NaN included). */
    static void refreshFour (float* period, float phase, float phOff, const float* baseOf, float avg, float t, float depth, float sr) noexcept
    {
        static const float kCOff[4] = { 0.0f, 0.25f, 0.0f, 0.25f }, kViOff[4] = { 0.0f, 0.0f, 0.5f, 0.5f };
        float32x4_t ph = vaddq_f32 (vaddq_f32 (vaddq_f32 (vdupq_n_f32 (phase), vld1q_f32 (kCOff)), vld1q_f32 (kViOff)), vdupq_n_f32 (phOff));
        ph = vsubq_f32 (ph, ffloorFour (ph));
        const float32x4_t mod = vaddq_f32 (vdupq_n_f32 (1.0f), vmulq_f32 (vdupq_n_f32 (0.5f), sinCyclesFour (ph)));
        const float base[4] = { baseOf[0], baseOf[0], baseOf[1], baseOf[1] };
        const float32x4_t a = vld1q_f32 (base);
        const float32x4_t lerpV = vaddq_f32 (a, vmulq_f32 (vsubq_f32 (vdupq_n_f32 (avg), a), vdupq_n_f32 (t)));
        const float32x4_t delay = vaddq_f32 (vmulq_f32 (vmulq_f32 (mod, vdupq_n_f32 (0.03f)), vdupq_n_f32 (depth)), lerpV);
        const float32x4_t v = vmulq_f32 (delay, vdupq_n_f32 (sr));
        const float32x4_t lo = vdupq_n_f32 (3.0f), hi = vdupq_n_f32 (0.08f * sr);
        // std::clamp (v, lo, hi): v < lo ? lo : (hi < v ? hi : v)
        const float32x4_t upper = vbslq_f32 (neoncmp::lt (hi, v), hi, v);
        vst1q_f32 (period, vbslq_f32 (neoncmp::lt (v, lo), lo, upper));
    }
#endif

private:
    /** The cubic (Catmull-Rom) read of four taps, `d` samples back from the newest sample (the arithmetic of DelayLine::readCubic, lane by lane). */
    void readGroup (int k0, const float* d, float* out) const noexcept
    {
        int idx[4][4];
        float t[4];
        for (int j = 0; j < 4; ++j)
        {
            const float dd = std::max (d[j], 1.0f);
            const int i = static_cast<int> (dd);
            t[j] = dd - static_cast<float> (i);
            idx[j][0] = (linePos - i + 1) & lineMask;
            idx[j][1] = (linePos - i) & lineMask;
            idx[j][2] = (linePos - i - 1) & lineMask;
            idx[j][3] = (linePos - i - 2) & lineMask;
        }
#if SB_NEON
        float32x4_t y0 = vdupq_n_f32 (0.0f), y1 = y0, y2 = y0, y3 = y0;
        const float* b0 = store.data() + static_cast<size_t> (k0) * lineSize;
        const size_t ls = static_cast<size_t> (lineSize);
        y0 = vld1q_lane_f32 (b0 + idx[0][0], y0, 0);
        y0 = vld1q_lane_f32 (b0 + ls + idx[1][0], y0, 1);
        y0 = vld1q_lane_f32 (b0 + 2 * ls + idx[2][0], y0, 2);
        y0 = vld1q_lane_f32 (b0 + 3 * ls + idx[3][0], y0, 3);
        y1 = vld1q_lane_f32 (b0 + idx[0][1], y1, 0);
        y1 = vld1q_lane_f32 (b0 + ls + idx[1][1], y1, 1);
        y1 = vld1q_lane_f32 (b0 + 2 * ls + idx[2][1], y1, 2);
        y1 = vld1q_lane_f32 (b0 + 3 * ls + idx[3][1], y1, 3);
        y2 = vld1q_lane_f32 (b0 + idx[0][2], y2, 0);
        y2 = vld1q_lane_f32 (b0 + ls + idx[1][2], y2, 1);
        y2 = vld1q_lane_f32 (b0 + 2 * ls + idx[2][2], y2, 2);
        y2 = vld1q_lane_f32 (b0 + 3 * ls + idx[3][2], y2, 3);
        y3 = vld1q_lane_f32 (b0 + idx[0][3], y3, 0);
        y3 = vld1q_lane_f32 (b0 + ls + idx[1][3], y3, 1);
        y3 = vld1q_lane_f32 (b0 + 2 * ls + idx[2][3], y3, 2);
        y3 = vld1q_lane_f32 (b0 + 3 * ls + idx[3][3], y3, 3);
        const float32x4_t tv = vld1q_f32 (t), half = vdupq_n_f32 (0.5f);
        const float32x4_t c1 = vmulq_f32 (half, vsubq_f32 (y2, y0));
        float32x4_t c2 = vsubq_f32 (y0, vmulq_f32 (vdupq_n_f32 (2.5f), y1));
        c2 = vaddq_f32 (c2, vmulq_f32 (vdupq_n_f32 (2.0f), y2));
        c2 = vsubq_f32 (c2, vmulq_f32 (half, y3));
        const float32x4_t c3 = vaddq_f32 (vmulq_f32 (half, vsubq_f32 (y3, y0)), vmulq_f32 (vdupq_n_f32 (1.5f), vsubq_f32 (y1, y2)));
        vst1q_f32 (out, vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (c3, tv), c2), tv), c1), tv), y1));
#else
        for (int j = 0; j < 4; ++j)
        {
            const float* b = store.data() + static_cast<size_t> (k0 + j) * lineSize;
            const float y0 = b[idx[j][0]], y1 = b[idx[j][1]], y2 = b[idx[j][2]], y3 = b[idx[j][3]];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            out[j] = ((c3 * t[j] + c2) * t[j] + c1) * t[j] + y1;
        }
#endif
    }
#if SB_NEON
    /** The whole work of four taps in vector registers: the cubic read (four values gathered per tap), the feedback, the limiter, the band pass and the write. */
    void tapGroupNeon (int k0, const float* d, const float* in, float fb, float* rdOut) noexcept
    {
        const float32x4_t one = vdupq_n_f32 (1.0f);
        const float32x4_t dd = vmaxq_f32 (vld1q_f32 (d), one);
        const int32x4_t iv = vcvtq_s32_f32 (dd);
        const float32x4_t tv = vsubq_f32 (dd, vcvtq_f32_s32 (iv));
        const int32x4_t base = vsubq_s32 (vdupq_n_s32 (linePos), iv), mask = vdupq_n_s32 (lineMask);
        const int32x4_t ix0 = vandq_s32 (vaddq_s32 (base, vdupq_n_s32 (1)), mask), ix1 = vandq_s32 (base, mask), ix2 = vandq_s32 (vsubq_s32 (base, vdupq_n_s32 (1)), mask),
                        ix3 = vandq_s32 (vsubq_s32 (base, vdupq_n_s32 (2)), mask);
        const float* b0 = store.data() + static_cast<size_t> (k0) * lineSize;
        const size_t ls = static_cast<size_t> (lineSize);
        float32x4_t y0 = vdupq_n_f32 (0.0f), y1 = y0, y2 = y0, y3 = y0;
        y0 = vld1q_lane_f32 (b0 + vgetq_lane_s32 (ix0, 0), y0, 0);
        y0 = vld1q_lane_f32 (b0 + ls + vgetq_lane_s32 (ix0, 1), y0, 1);
        y0 = vld1q_lane_f32 (b0 + 2 * ls + vgetq_lane_s32 (ix0, 2), y0, 2);
        y0 = vld1q_lane_f32 (b0 + 3 * ls + vgetq_lane_s32 (ix0, 3), y0, 3);
        y1 = vld1q_lane_f32 (b0 + vgetq_lane_s32 (ix1, 0), y1, 0);
        y1 = vld1q_lane_f32 (b0 + ls + vgetq_lane_s32 (ix1, 1), y1, 1);
        y1 = vld1q_lane_f32 (b0 + 2 * ls + vgetq_lane_s32 (ix1, 2), y1, 2);
        y1 = vld1q_lane_f32 (b0 + 3 * ls + vgetq_lane_s32 (ix1, 3), y1, 3);
        y2 = vld1q_lane_f32 (b0 + vgetq_lane_s32 (ix2, 0), y2, 0);
        y2 = vld1q_lane_f32 (b0 + ls + vgetq_lane_s32 (ix2, 1), y2, 1);
        y2 = vld1q_lane_f32 (b0 + 2 * ls + vgetq_lane_s32 (ix2, 2), y2, 2);
        y2 = vld1q_lane_f32 (b0 + 3 * ls + vgetq_lane_s32 (ix2, 3), y2, 3);
        y3 = vld1q_lane_f32 (b0 + vgetq_lane_s32 (ix3, 0), y3, 0);
        y3 = vld1q_lane_f32 (b0 + ls + vgetq_lane_s32 (ix3, 1), y3, 1);
        y3 = vld1q_lane_f32 (b0 + 2 * ls + vgetq_lane_s32 (ix3, 2), y3, 2);
        y3 = vld1q_lane_f32 (b0 + 3 * ls + vgetq_lane_s32 (ix3, 3), y3, 3);
        const float32x4_t half = vdupq_n_f32 (0.5f);
        const float32x4_t c1 = vmulq_f32 (half, vsubq_f32 (y2, y0));
        float32x4_t c2 = vsubq_f32 (y0, vmulq_f32 (vdupq_n_f32 (2.5f), y1));
        c2 = vaddq_f32 (c2, vmulq_f32 (vdupq_n_f32 (2.0f), y2));
        c2 = vsubq_f32 (c2, vmulq_f32 (half, y3));
        const float32x4_t c3 = vaddq_f32 (vmulq_f32 (half, vsubq_f32 (y3, y0)), vmulq_f32 (vdupq_n_f32 (1.5f), vsubq_f32 (y1, y2)));
        const float32x4_t rd = vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (vaddq_f32 (vmulq_f32 (c3, tv), c2), tv), c1), tv), y1);
        vst1q_f32 (rdOut, rd);
        // the feedback and the limiter: x = 8 * clamp ((in + fb * rd) / 8, -1, 1)
        const float inArr[4] = { in[0], in[1], in[0], in[1] };
        float32x4_t x = vaddq_f32 (vld1q_f32 (inArr), vmulq_f32 (vdupq_n_f32 (fb), rd));
        x = vmulq_f32 (vminq_f32 (vmaxq_f32 (vmulq_f32 (x, vdupq_n_f32 (0.125f)), vdupq_n_f32 (-1.0f)), one), vdupq_n_f32 (8.0f));
        // the band pass
        const float32x4_t aHi = vdupq_n_f32 (coefBand.aHi), aLo = vdupq_n_f32 (coefBand.aLo), gain = vdupq_n_f32 (coefBand.gain);
        float32x4_t hi = vld1q_f32 (hiY + k0), lo = vld1q_f32 (loY + k0);
        hi = vaddq_f32 (hi, vmulq_f32 (aHi, vsubq_f32 (vmulq_f32 (x, gain), hi)));
        hi = neoncmp::flushTiny (hi); // flushDenormal () on the bit patterns (dsp/neon_cmp.h: a float compare is made lane by lane through the VFP flags on 32 bit ARM)
        lo = vaddq_f32 (lo, vmulq_f32 (aLo, vsubq_f32 (hi, lo)));
        lo = neoncmp::flushTiny (lo);
        vst1q_f32 (hiY + k0, hi);
        vst1q_f32 (loY + k0, lo);
        // the write: lane j into the line of tap k0 + j at the next position
        const int at = (linePos + 1) & lineMask;
        const float32x4_t y = vsubq_f32 (hi, lo);
        float* w = store.data() + static_cast<size_t> (k0) * ls + static_cast<size_t> (at);
        vst1q_lane_f32 (w, y, 0);
        vst1q_lane_f32 (w + ls, y, 1);
        vst1q_lane_f32 (w + 2 * ls, y, 2);
        vst1q_lane_f32 (w + 3 * ls, y, 3);
    }
#endif
    /** The loop band pass (LoopBand::process: two one pole low passes subtracted) of four taps, in place on x. */
    void bandGroup (int k0, float* x, const LoopBand& cf) noexcept
    {
#if SB_NEON
        const float32x4_t aHi = vdupq_n_f32 (cf.aHi), aLo = vdupq_n_f32 (cf.aLo), gain = vdupq_n_f32 (cf.gain);
        float32x4_t hi = vld1q_f32 (hiY + k0), lo = vld1q_f32 (loY + k0);
        const float32x4_t xg = vmulq_f32 (vld1q_f32 (x), gain);
        hi = vaddq_f32 (hi, vmulq_f32 (aHi, vsubq_f32 (xg, hi)));
        hi = neoncmp::flushTiny (hi); // flushDenormal (dsp/neon_cmp.h)
        lo = vaddq_f32 (lo, vmulq_f32 (aLo, vsubq_f32 (hi, lo)));
        lo = neoncmp::flushTiny (lo);
        vst1q_f32 (hiY + k0, hi);
        vst1q_f32 (loY + k0, lo);
        vst1q_f32 (x, vsubq_f32 (hi, lo));
#else
        for (int j = 0; j < 4; ++j)
        {
            float& hy = hiY[k0 + j];
            float& ly = loY[k0 + j];
            const float xg = x[j] * cf.gain; // (a separate statement, as the argument of LoopBand::process was: the compiler may not fuse it with the subtraction)
            hy += cf.aHi * (xg - hy);
            hy = flushDenormal (hy);
            ly += cf.aLo * (hy - ly);
            ly = flushDenormal (ly);
            x[j] = hy - ly;
        }
#endif
    }
    void pushGroup (int k0, const float* x) noexcept
    {
        const int at = (linePos + 1) & lineMask;
        for (int j = 0; j < 4; ++j)
            store[static_cast<size_t> (k0 + j) * lineSize + static_cast<size_t> (at)] = x[j];
    }

    Params p;
    double sampleRate = 48000.0;
    std::vector<float> store;         // the delay lines of the 16 taps, one after the other (all with the same length and the same write position)
    LoopBand coefBand;                // the coefficients of the band filters of this block
    int lineSize = 16, lineMask = 15, linePos = 0;
    float hiY[kMaxPairs * 4] = {}, loY[kMaxPairs * 4] = {}; // the state of the band filters
    float phase = 0.0f;
};

/** The flanger of document 06, 7 (the intended design): a stereo feedback delay whose time follows a triangle LFO around the note `center`. */
class Flanger
{
public:
    struct Params { float mix = 0.5f, feedback = 0.5f, rate = 0.25f, depth = 0.5f, centerHz = 330.0f, phaseOffset = 120.0f / 360.0f; };
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& d : line)
            d.prepare (static_cast<int> (sr * 0.105));
        reset();
    }
    void reset() noexcept { for (auto& d : line) d.reset(); phase = 0.0f; }
    /** The LFO runs on while the module is skipped on silence (FxChain, tier low): the same per sample steps as process (). */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float inc = p.rate / sr;
        for (int s = 0; s < n; ++s)
        {
            phase += inc;
            phase -= ffloor (phase);
        }
    }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float inc = p.rate / sr;
        const float f = std::clamp (p.feedback, -1.0f, 1.0f);
        const float dc = 1.0f / std::clamp (p.centerHz, 20.0f, 16000.0f);
        const float m = 0.5f * clamp01 (p.mix); // the mix knob runs over half of the equal power law
        const float wetG = std::sin (m * kPif * 0.5f), dryG = std::cos (m * kPif * 0.5f);
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            phase -= ffloor (phase);
            for (int c = 0; c < 2; ++c)
            {
                const float ph = phase - 0.5f * p.phaseOffset + (c == 1 ? p.phaseOffset : 0.0f);
                const float x = ph + 0.75f;
                const float tri = std::fabs (2.0f * (x - ffloor (x)) - 1.0f);
                const float mod = p.depth * (2.0f * tri - 1.0f) + 1.0f;
                const float delay = std::max ((dc - 0.0005f) * mod + 0.0005f, 1.0f / 20000.0f);
                const float period = std::clamp (delay * sr, 2.0f, 0.1f * sr);
                const float rd = line[c].readCubic (period);
                const float in = io[c][i];
                line[c].push (loopLimit (in + f * rd));
                io[c][i] = dryG * in + wetG * rd;
            }
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    std::array<DelayLine, 2> line;
    float phase = 0.0f;
};

class Phaser
{
public:
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
    static constexpr int kStages = 12;
    struct Params { float mix = 0.5f, feedback = 0.5f, rate = 0.12f, depthSemis = 24.0f, centerHz = 2000.0f, blend = 0.5f, phaseOffset = 120.0f / 360.0f, depth2 = 0.0f; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& c : z) for (auto& x : c) x = 0.0f; phase = 0.0f; fb[0] = fb[1] = 0.0f; for (auto& x : bpA) x = 0.0f; for (auto& x : bpB) x = 0.0f; }
    /** The LFO runs on while the module is skipped on silence (FxChain, tier low): the same per sample steps as process (). */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float inc = p.rate / sr;
        for (int s = 0; s < n; ++s)
        {
            phase += inc;
            phase -= ffloor (phase);
        }
    }
    void set (const Params& np) noexcept { p = np; }
    /** The phaser of document 06, 8: the twelve stage all-pass filter of document 05, 7 (taps after 4, 8 and 12 stages by the blend), fed with a cutoff that follows a
        triangle LFO (the right channel offset by the phase offset), a band passed feedback with tanh, output (x + all-pass) / 2, linear dry / wet. */
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float inc = p.rate / sr;
        const float fbk = clamp01 (p.feedback);
        const float braw = clamp01 (p.blend) * 2.0f;
        const float p1 = clamp01 (1.0f - braw), p5 = clamp01 (braw - 1.0f), p3 = 1.0f - p1 - p5;
        const float centreNote = 12.0f * std::log2 (std::max (p.centerHz, 1.0f) / 8.1757989f);
        const bool spread = std::fabs (p.depth2) > 1.0e-4f;
        float* io[2] = { l, r };
        float hc1[2] = {}, hc2[2] = {}, hS[2][kStages] = {};
        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            phase -= ffloor (phase);
            for (int c = 0; c < 2; ++c)
            {
                // the cutoff of the stages, and the coefficients of the feedback band pass, follow the LFO: every sample, at tier low every eighth (the LFO moves the
                // cutoff by a few thousandths of an octave in that time)
                if (!fast || (i & 7) == 0)
                {
                    const float ph = phase + (c == 1 ? p.phaseOffset : -p.phaseOffset) * 0.5f;
                    const float x01 = ph - ffloor (ph);
                    const float v = 2.0f * std::fabs (2.0f * x01 - 1.0f) - 1.0f; // triangle -1 .. 1
                    const float e2 = (centreNote + p.depthSemis * v) * (1.0f / 12.0f);
                    const float hz = std::clamp (8.1757989f * (fast ? fastmath::exp2 (e2) : std::exp2 (e2)), 20.0f, 0.45f * sr);
                    hc1[c] = OnePole::coefficient (hz / 20.0f, sampleRate);
                    hc2[c] = OnePole::coefficient (hz * 20.0f, sampleRate);
                    if (spread)
                    {
                        // depth 2: the stage cutoffs spread linearly in semitones over [-depth2 / 2, +depth2 / 2] around the centre (stage 0 lowest for a positive value)
                        for (int s = 0; s < kStages; ++s)
                        {
                            const float hs = std::max (hz * std::exp2 (p.depth2 * (static_cast<float> (s) / 11.0f - 0.5f) * (1.0f / 12.0f)), 5.0f);
                            const float g = phaserStageG (hs, sampleRate);
                            hS[c][s] = g / (1.0f + g);
                        }
                    }
                    else
                    {
                        const float g = phaserStageG (hz, sampleRate);
                        const float a0 = g / (1.0f + g);
                        for (int s = 0; s < kStages; ++s)
                            hS[c][s] = a0;
                    }
                }
                const float c1 = hc1[c], c2 = hc2[c];
                const float* aS = hS[c];
                const float dry = io[c][i];
                float x = dry * (1.0f + 0.5f * fbk) + fb[c];
                float tap[3] = { 0.0f, 0.0f, 0.0f };
                for (int s = 0; s < kStages; ++s)
                {
                    const float vv = (x - z[c][s]) * aS[s];
                    const float lp = vv + z[c][s];
                    z[c][s] = flushDenormal (lp + vv);
                    x = x - 2.0f * lp;
                    if (s == 3) tap[0] = x;
                    if (s == 7) tap[1] = x;
                    if (s == 11) tap[2] = x;
                }
                const float ap = p1 * tap[0] + p3 * tap[1] + p5 * tap[2];
                bpA[c] = flushDenormal (bpA[c] + c2 * (ap - bpA[c]));
                bpB[c] = flushDenormal (bpB[c] + c1 * (bpA[c] - bpB[c]));
                fb[c] = fast ? fastmath::tanh (fbk * (bpA[c] - bpB[c])) : std::tanh (fbk * (bpA[c] - bpB[c]));
                const float wet = 0.5f * (dry + ap);
                io[c][i] = dry * (1.0f - p.mix) + wet * p.mix; // linear crossfade (document 06, section 1)
            }
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    float z[2][kStages] = {};
    float phase = 0.0f, fb[2] = {}, bpA[2] = {}, bpB[2] = {};
};

class Distortion
{
public:
    static constexpr int kLatency = HalfBand2x::kLatency;
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
    struct Params { int type = 0; float driveDb = 0.0f, mix = 1.0f, cutoff = 4000.0f, resonance = 0.5f, blend = 0.0f; int order = 0; bool os2 = false; };
    int latencySamples() const noexcept { return p.os2 ? kLatency : 0; }
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept
    {
        for (auto& s : svf) s.reset();
        held[0] = held[1] = 0.0f;
        holdPhase[0] = holdPhase[1] = 0.0f;
        dc[0] = dc[1] = 0.0f;
        adaaState[0] = adaaState[1] = adaa::State();
        for (auto& o : oversampler) o.reset();
        for (auto& d : dryDelay) d.reset();
        for (auto& d : lofiDelay) d.reset();
        counter = 0;
        primed = false;
    }
    /** The drive of the new control period: it is reached at the end of the period (process (): linear across it, nothing is stepped, docs/engine-spec/06-effects.md section 1), the
        period before it is where the ramp starts. The first set () after a reset () has no period before: no ramp. */
    void set (const Params& np) noexcept
    {
        if (np.type != p.type) // switching the type clears the state (docs/engine-spec/06-effects.md section 4)
            adaaState[0] = adaaState[1] = adaa::State();
        if (np.os2 != p.os2)
        {
            adaaState[0] = adaaState[1] = adaa::State();
            for (auto& o : oversampler) o.reset();
            for (auto& d : dryDelay) d.reset();
            for (auto& d : lofiDelay) d.reset();
        }
        p = np;
        const DriveLaw now = driveLaw (p.driveDb);
        prevLaw = primed ? curLaw : now;
        curLaw = now;
        primed = true;
    }
    void setShaper (const float* yA, const float* IA, const float* yB, const float* IB) noexcept
    {
        shaperA = yA;
        shaperIntA = IA;
        shaperB = yB;
        shaperIntB = IB;
    }
    /** t0 / period: the position of the call in the control period (the gain, the morph and the divisor of the drive are interpolated across it from the period before to this one,
        like the Serum law of the EQ); period 0 (a caller without periods): the drive of the last set () at once. */
    void process (float* l, float* r, int n, int t0 = 0, int period = 0) noexcept
    {
        aaOn = (synthyboy::aa::off() & synthyboy::aa::kDistortion) == 0; // the test and benchmark switch, read once per call
        const float cg = Svf::coefficient (p.cutoff, sampleRate);
        const float k = 2.0f - 1.9f * clamp01 (p.resonance);
        const bool ramp = period > 0 && (prevLaw.g != curLaw.g || prevLaw.u != curLaw.u || prevLaw.d != curLaw.d); // a still drive: the same values as without the ramp, bit for bit
        const float invP = 1.0f / static_cast<float> (std::max (period, 1));
        const float dcA = 1.0f - (fast ? fastmath::exp (-kTwoPif * 10.0f / static_cast<float> (sampleRate)) : std::exp (-kTwoPif * 10.0f / static_cast<float> (sampleRate)));
        float* io[2] = { l, r };
        if (!p.os2)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float dry = io[c][i];
                    float x = dry;
                    float g = curLaw.g, u = curLaw.u, d = curLaw.d;
                    if (ramp)
                    {
                        const float t = std::min (static_cast<float> (t0 + i + 1) * invP, 1.0f);
                        g = prevLaw.g + (curLaw.g - prevLaw.g) * t;
                        u = prevLaw.u + (curLaw.u - prevLaw.u) * t;
                        d = prevLaw.d + (curLaw.d - prevLaw.d) * t;
                    }
                    if (p.order == 1) x = filt (c, x, cg, k);
                    x = applyDc (c, shape (c, x, g, u, d), dcA);
                    if (p.order == 2) x = filt (c, x, cg, k);
                    io[c][i] = dry * (1.0f - p.mix) + x * p.mix;
                }
            return;
        }
        const bool lofi = p.type == 4 || p.type == 5; // Bit Crush and Down Sample are lo-fi effects on purpose and are delayed without oversampling
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float dry = io[c][i];
                const float dryDelayed = dryDelay[c].pushPop (dry);
                float x = dry;
                if (p.order == 1) x = filt (c, x, cg, k);
                if (lofi)
                {
                    float g = curLaw.g, u = curLaw.u, d = curLaw.d;
                    if (ramp)
                    {
                        const float t = std::min (static_cast<float> (t0 + i + 1) * invP, 1.0f);
                        g = prevLaw.g + (curLaw.g - prevLaw.g) * t;
                        u = prevLaw.u + (curLaw.u - prevLaw.u) * t;
                        d = prevLaw.d + (curLaw.d - prevLaw.d) * t;
                    }
                    x = lofiDelay[c].pushPop (shape (c, x, g, u, d));
                }
                else
                {
                    const auto up2 = oversampler[c].up (x);
                    float g0 = curLaw.g, u0 = curLaw.u, d0 = curLaw.d;
                    float g1 = curLaw.g, u1 = curLaw.u, d1 = curLaw.d;
                    if (ramp)
                    {
                        const float t05 = std::min ((static_cast<float> (t0 + i) + 0.5f) * invP, 1.0f);
                        const float t1 = std::min (static_cast<float> (t0 + i + 1) * invP, 1.0f);
                        g0 = prevLaw.g + (curLaw.g - prevLaw.g) * t05;
                        u0 = prevLaw.u + (curLaw.u - prevLaw.u) * t05;
                        d0 = prevLaw.d + (curLaw.d - prevLaw.d) * t05;
                        g1 = prevLaw.g + (curLaw.g - prevLaw.g) * t1;
                        u1 = prevLaw.u + (curLaw.u - prevLaw.u) * t1;
                        d1 = prevLaw.d + (curLaw.d - prevLaw.d) * t1;
                    }
                    const float y0 = shape (c, up2[0], g0, u0, d0);
                    const float y1 = shape (c, up2[1], g1, u1, d1);
                    x = applyDc (c, oversampler[c].down (y0, y1), dcA);
                }
                if (p.order == 2) x = filt (c, x, cg, k);
                io[c][i] = dryDelayed * (1.0f - p.mix) + x * p.mix;
            }
    }

private:
    struct Delay15
    {
        std::array<float, 16> buf {};
        int pos = 0;
        void reset() noexcept { buf.fill (0.0f); pos = 0; }
        float pushPop (float x) noexcept
        {
            buf[static_cast<size_t> (pos)] = x;
            const float out = buf[static_cast<size_t> ((pos - kLatency) & 15)];
            pos = (pos + 1) & 15;
            return out;
        }
    };
    /** What the drive in dB makes of the shaper: the gain, the morph / bit depth position and the divisor of the sample rate and bit laws. */
    struct DriveLaw { float g = 1.0f, u = 0.5f, d = 1.0f; };
    DriveLaw driveLaw (float driveDb) const noexcept
    {
        DriveLaw w;
        w.g = std::pow (10.0f, driveDb * 0.05f);
        w.u = clamp01 ((driveDb + 30.0f) / 60.0f);
        const float dExp = std::clamp (driveDb, -30.0f, 30.0f) * 0.05f;
        w.d = fast ? fastmath::pow10 (dExp) : std::pow (10.0f, dExp);
        return w;
    }
    float filt (int c, float x, float g, float k) noexcept
    {
        svf[c].process (x, g, k);
        // blend 0 .. 2 : low pass, band pass, high pass
        const float b = std::clamp (p.blend, 0.0f, 2.0f);
        return b < 1.0f ? lerp (svf[c].lp, svf[c].bp * k, b) : lerp (svf[c].bp * k, svf[c].hp, b - 1.0f);
    }
    float shape (int c, float x, float g, float u, float d) noexcept
    {
        // v = x * drive; the laws of docs/engine-spec/06-effects.md section 4
        const float v = x * g;
        auto th = [this] (float a) { return fast ? fastmath::tanh (a) : std::tanh (a); };
        auto ex = [this] (float a) { return fast ? fastmath::exp (a) : std::exp (a); };
        float y = std::clamp (v, -1.0f, 1.0f);
        if (aaOn && p.type >= 0 && p.type < 32 && ((adaa::kConverted >> p.type) & 1u) != 0u)
            y = adaa::step (p.type, adaaState[c], x, g, fast); // first-order ADAA (fx_adaa.h)
        else
        switch (p.type)
        {
            case 0: y = th (v); break;
            case 1: y = std::clamp (v, -1.0f, 1.0f); break;
            case 2: { const float t = 0.25f * v + 0.75f; y = std::fabs (-4.0f * (t - ffloor (t)) + 2.0f) - 1.0f; break; }
            case 3: { const float t = 0.5f - 0.25f * v; y = fast ? fastmath::sinCycles (t) : std::sin (kTwoPif * (t - ffloor (t))); break; }
            case 4:
            {
                const float qx = d != 0.0f ? v / d : v;
                const float q = std::clamp (u * u, 1.5e-8f, 1.0f);
                y = std::round (qx / q) * q;
                break;
            }
            case 5:
            {
                const float dx = d != 0.0f ? v / d : v;
                const float period = std::max (static_cast<float> (sampleRate) * std::max (0.99f / std::max ((1.0f - u) * (1.0f - u), 1.0e-4f), 1.0f) / 88200.0f, 1.0f);
                holdPhase[c] += 1.0f;
                if (holdPhase[c] >= period) { holdPhase[c] -= period; held[c] = dx; }
                y = held[c];
                break;
            }
            case 6: y = th (v + 0.3f) - th (0.3f); break;
            case 7: { const float vp = std::max (v, 0.0f), vn = std::min (v, 0.0f); y = (1.0f - ex (-vp)) - 0.5f * (1.0f - ex (2.0f * vn)); break; }
            case 8: y = ex (std::min (v, 0.0f)) - ex (-std::max (v, 0.0f)); break;
            case 9: y = th (32.0f * v); break;
            case 10: y = th (std::max (v, 0.0f)) + std::max (std::min (v, 0.0f), -0.5f); break;
            case 11: y = std::fabs (std::clamp (v, -1.0f, 1.0f)); break;
            case 12:
            case 13:
                y = aaOn ? adaa::stepShaper (p.type, adaaState[c], x, u, shaperA, shaperIntA, shaperB, shaperIntB) : std::clamp (v, -1.0f, 1.0f);
                break;
        }
        return y;
    }
    float applyDc (int c, float y, float dcA) noexcept
    {
        const bool asym = p.type == 6 || p.type == 7 || p.type == 10 || p.type == 11 || p.type == 13;
        if (asym) // 10 Hz DC blocker
        {
            dc[c] += dcA * (y - dc[c]);
            dc[c] = flushDenormal (dc[c]);
            y -= dc[c];
        }
        return y;
    }
    Params p;
    DriveLaw prevLaw, curLaw;
    bool primed = false;
    double sampleRate = 48000.0;
    Svf svf[2];
    HalfBand2x oversampler[2];
    Delay15 dryDelay[2];
    Delay15 lofiDelay[2];
    float held[2] = {}, holdPhase[2] = {}, dc[2] = {};
    adaa::State adaaState[2];
    const float* shaperA = nullptr;
    const float* shaperIntA = nullptr;
    const float* shaperB = nullptr;
    const float* shaperIntB = nullptr;
    bool aaOn = true;
    unsigned counter = 0;
};

} // namespace own
} // namespace synthyboy
