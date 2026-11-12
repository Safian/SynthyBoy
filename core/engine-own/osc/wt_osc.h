// SynthyBoy - own engine: wavetable oscillator with unison and warps (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/util.h"
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
    float level = 0.0f, pan = 0.0f;       // amplitude (linear), -1 .. 1
    int voices = 1;
    float detuneSemis = 0.0f;             // offset of the outermost voice
    float curveExp = 1.0f;                // exponent of the detune law
    int mode = 0;                         // unison mode
    float spread = 1.0f, blend = 1.0f;    // 0 .. 1
    int warp[2] = { 0, 0 };
    float warpAmount[2] = { 0, 0 }, warpPhase[2] = { 0, 0 }, warpSpread[2] = { 0, 0 };
    float startPhase = 0.0f;              // only used when a note starts
    bool active = false;
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

    void start (float startPhase, float randomPhase, Rng& rng) noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const float p = startPhase + randomPhase * rng.uni();
            phase[i] = static_cast<uint32_t> (static_cast<double> (p - std::floor (p)) * 4294967296.0);
        }
        havePrev = false;
    }

    /** Computes the per voice frequencies, levels and pans of the period that ends now (call once per control period). */
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
        const int n = std::clamp (t.voices, 1, kMaxVoices);
        curCount = n;
        float sumL = 0.0f, sumR = 0.0f;
        float wl[kMaxVoices], wr[kMaxVoices];
        const double nyq = 0.5 * sampleRate;
        for (int i = 0; i < n; ++i)
        {
            const float u = n > 1 ? 2.0f * static_cast<float> (i) / static_cast<float> (n - 1) - 1.0f : 0.0f;
            float shaped;
            const float au = std::fabs (u);
            switch (t.mode)
            {
                case 1: shaped = std::copysign (std::pow (au, t.curveExp * 0.6f + 0.4f), u); break;
                case 2: shaped = std::copysign (std::pow (au, t.curveExp * 1.8f), u); break;
                case 3: shaped = std::copysign (1.0f - std::pow (1.0f - au, t.curveExp), u); break;
                case 4:
                {
                    Rng r;
                    r.seed (static_cast<uint32_t> (i) * 7919u + 13u);
                    shaped = r.bi();
                    break;
                }
                default: shaped = std::copysign (std::pow (au, t.curveExp), u); break;
            }
            const float semis = t.semis + t.detuneSemis * shaped;
            const double f = 440.0 * std::exp2 ((static_cast<double> (semis) - 69.0) / 12.0);
            curInc[i] = static_cast<float> (std::min (f, nyq) / sampleRate);
            // blend: the centre voice(s) at full weight, the others at `blend`
            const float centre = n > 1 ? 1.0f / static_cast<float> (n - 1) : 1.0f;
            const float w = (au <= centre + 1.0e-4f || n == 1) ? 1.0f : t.blend;
            const float pan = std::clamp (t.pan + t.spread * u, -1.0f, 1.0f);
            const float ang = (pan + 1.0f) * (kPif * 0.25f);
            wr[i] = w * 1.41421356f * std::sin (ang);
            wl[i] = w * 1.41421356f * std::sin (kPif * 0.5f - ang); // identical bits for both sides at the centre
            sumL += wl[i] * wl[i];
            sumR += wr[i] * wr[i];
        }
        const float norm = t.level / std::sqrt (std::max (std::max (sumL, sumR), 1.0e-6f));
        for (int i = 0; i < n; ++i)
        {
            curGainL[i] = wl[i] * norm;
            curGainR[i] = wr[i] * norm;
            const float u = n > 1 ? 2.0f * static_cast<float> (i) / static_cast<float> (n - 1) - 1.0f : 0.0f;
            curFrameOf[i] = std::clamp (t.frame + t.frameSpread * u, 0.0f, 1.0f);
        }
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

    /** Renders samples t0 .. t0 + n - 1 of the period into L and R (overwritten). `cross` are the signals of the other two oscillators (may be null). */
    void render (int t0, int n, float* L, float* R, const float* const* cross, double sampleRate) noexcept
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        if (tick.wt == nullptr || tick.level <= 0.0f)
        {
            // keep the phases running (cheap) so that a level modulation from zero does not restart the cycle
            return;
        }
        const Wavetable& wt = *tick.wt;
        const int frames = wt.numFrames();
        const double nyq = 0.5 * sampleRate;
        const int count = std::max (curCount, prevCount);
        const float invP = 1.0f / static_cast<float> (kControlPeriod);
        const float a0 = tick.warpAmount[0], a1 = tick.warpAmount[1];
        for (int v = 0; v < count; ++v)
        {
            if (curGainL[v] == 0.0f && curGainR[v] == 0.0f && prevGainL[v] == 0.0f && prevGainR[v] == 0.0f)
            {
                // silent voice: advance the phase only
                const float inc = curInc[v];
                phase[v] += static_cast<uint32_t> (static_cast<uint64_t> (static_cast<double> (inc) * 4294967296.0 * n));
                continue;
            }
            // band limit: the coarsest level that keeps the highest harmonic (including what the warps create) below Nyquist
            float bw = 1.0f;
            for (int w = 0; w < 2; ++w)
                bw = std::max (bw, warpBandwidthFactor (tick.warp[w], tick.warpAmount[w]));
            const float fmax = std::max (curInc[v], prevInc[v]) * static_cast<float> (sampleRate) * bw;
            const int level = Wavetable::levelFor (static_cast<float> (nyq) / std::max (fmax, 1.0f));
            const float frameV = curFrameOf[v];
            const float fpos = frameV * static_cast<float> (frames - 1);
            const int f0 = std::min (static_cast<int> (fpos), std::max (frames - 2, 0));
            const float ff = frames > 1 ? fpos - static_cast<float> (f0) : 0.0f;
            const float* c0 = wt.cycle (f0, level);
            const float* c1 = frames > 1 && ff > 1.0e-4f ? wt.cycle (f0 + 1, level) : nullptr;
            uint32_t ph = phase[v];
            for (int k = 0; k < n; ++k)
            {
                const float t = static_cast<float> (t0 + k + 1) * invP;
                const float inc = lerp (prevInc[v], curInc[v], t);
                const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
                float window = 1.0f, w2 = 1.0f, pp = p;
                if (tick.warp[0] != 0)
                {
                    const int cs = warpCrossSource (tick.warp[0]);
                    pp = warpPhase (tick.warp[0], pp, a0, tick.warpPhase[0], cs >= 0 && cross != nullptr && cross[cs] ? cross[cs][k] : 0.0f, window);
                }
                if (tick.warp[1] != 0)
                {
                    const int cs = warpCrossSource (tick.warp[1]);
                    pp = warpPhase (tick.warp[1], pp, a1, tick.warpPhase[1], cs >= 0 && cross != nullptr && cross[cs] ? cross[cs][k] : 0.0f, w2);
                    window *= w2;
                }
                const float pos = pp * static_cast<float> (Wavetable::kCycle);
                int idx = static_cast<int> (pos);
                idx = std::clamp (idx, 0, Wavetable::kCycle);
                const float fr = pos - static_cast<float> (idx);
                float y = hermite (c0 + idx, fr);
                if (c1 != nullptr)
                    y = lerp (y, hermite (c1 + idx, fr), ff);
                y *= window;
                if (tick.warp[0] != 0)
                {
                    const int cs = warpCrossSource (tick.warp[0]);
                    y = warpSample (tick.warp[0], y, a0, cs >= 0 && cross != nullptr && cross[cs] ? cross[cs][k] : 0.0f);
                }
                if (tick.warp[1] != 0)
                {
                    const int cs = warpCrossSource (tick.warp[1]);
                    y = warpSample (tick.warp[1], y, a1, cs >= 0 && cross != nullptr && cross[cs] ? cross[cs][k] : 0.0f);
                }
                L[k] += y * lerp (prevGainL[v], curGainL[v], t);
                R[k] += y * lerp (prevGainR[v], curGainR[v], t);
                ph += static_cast<uint32_t> (static_cast<double> (inc) * 4294967296.0);
            }
            phase[v] = ph;
        }
    }

    /** Phase of the first voice in cycles (for the "osc" sources and the phase legato). */
    float firstPhase() const noexcept { return static_cast<float> (phase[0] >> 8) * (1.0f / 16777216.0f); }
    void setPhase (float p) noexcept
    {
        for (auto& x : phase)
            x = static_cast<uint32_t> (static_cast<double> (p - std::floor (p)) * 4294967296.0);
    }

private:
    OscTick tick;
    uint32_t phase[kMaxVoices] = {};
    float prevInc[kMaxVoices] = {}, curInc[kMaxVoices] = {}, prevGainL[kMaxVoices] = {}, curGainL[kMaxVoices] = {}, prevGainR[kMaxVoices] = {},
          curGainR[kMaxVoices] = {}, curFrameOf[kMaxVoices] = {};
    float prevFrame = 0.0f, curFrame = 0.0f;
    int curCount = 1, prevCount = 1;
    bool havePrev = false;
};
} // namespace own
} // namespace synthyboy
