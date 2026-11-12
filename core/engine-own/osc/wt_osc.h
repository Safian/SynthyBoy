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
                const double cutoff = 0.098, beta = 7.5; // in cycles per sample of the 4x signal (18.8 kHz at 48 kHz)
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
        for (auto& h : history)
            for (auto& x : h)
                x = 0.0f;
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
        float sumW = 0.0f;
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
            sumW += w * w;
        }
        // equal power: the sum of the squared weights of the voices (a single voice keeps its pan law, hard panned it is 3 dB louder than centred)
        const float norm = t.level / std::sqrt (std::max (sumW, 1.0e-6f));
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
            return;
        const Wavetable& wt = *tick.wt;
        const int frames = wt.numFrames();
        const double nyq = 0.5 * sampleRate;
        const int count = std::max (curCount, prevCount);
        const float invP = 1.0f / static_cast<float> (kControlPeriod);
        const float a0 = tick.warpAmount[0], a1 = tick.warpAmount[1];
        // a warp can create content far above the pitch: the oscillator then runs at four times the rate, with a band limited decimation
        const bool warped = tick.warp[0] != 0 || tick.warp[1] != 0;
        const int cs0 = warpCrossSource (tick.warp[0]), cs1 = warpCrossSource (tick.warp[1]);
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
            // band limit: the coarsest level that keeps the highest harmonic below Nyquist (of the rate the oscillator runs at)
            const int level = Wavetable::levelFor (static_cast<float> (nyq) * (warped ? 4.0f : 1.0f) / std::max (fmax, 1.0f));
            const float fpos = curFrameOf[v] * static_cast<float> (frames - 1);
            const int f0 = std::min (static_cast<int> (fpos), std::max (frames - 2, 0));
            const float ff = frames > 1 ? fpos - static_cast<float> (f0) : 0.0f;
            const float* c0 = wt.cycle (f0, level);
            const float* c1 = frames > 1 && ff > 1.0e-4f ? wt.cycle (f0 + 1, level) : nullptr;
            // one sample of the oscillator at the (32 bit) phase `ph`; k selects the sample of the cross signals
            auto sampleAt = [&] (uint32_t ph, int k, float fk) noexcept
            {
                const float p = static_cast<float> (ph >> 8) * (1.0f / 16777216.0f);
                float window = 1.0f, w2 = 1.0f, pp = p;
                // the other oscillator is interpolated between its samples (fk = position inside the sample k, 1 = at k)
                const float m0 = cs0 >= 0 && cross != nullptr && cross[cs0] ? lerp (k > 0 ? cross[cs0][k - 1] : lastCross[cs0], cross[cs0][k], fk) : 0.0f;
                const float m1 = cs1 >= 0 && cross != nullptr && cross[cs1] ? lerp (k > 0 ? cross[cs1][k - 1] : lastCross[cs1], cross[cs1][k], fk) : 0.0f;
                if (tick.warp[0] != 0)
                    pp = warpPhase (tick.warp[0], pp, a0, tick.warpPhase[0], m0, window);
                if (tick.warp[1] != 0)
                {
                    pp = warpPhase (tick.warp[1], pp, a1, tick.warpPhase[1], m1, w2);
                    window *= w2;
                }
                const float pos = pp * static_cast<float> (Wavetable::kCycle);
                const int idx = std::clamp (static_cast<int> (pos), 0, Wavetable::kCycle);
                const float fr = pos - static_cast<float> (idx);
                float y = hermite (c0 + idx, fr);
                if (c1 != nullptr)
                    y = lerp (y, hermite (c1 + idx, fr), ff);
                y *= window;
                if (tick.warp[0] != 0)
                    y = warpSample (tick.warp[0], y, a0, m0);
                if (tick.warp[1] != 0)
                    y = warpSample (tick.warp[1], y, a1, m1);
                return y;
            };
            uint32_t ph = phase[v];
            if (!warped)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float t = static_cast<float> (t0 + k + 1) * invP;
                    const float y = sampleAt (ph, k, 1.0f);
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
                        buf[kTaps - 1 + k * kOversample + j] = sampleAt (ph, k, static_cast<float> (j + 1) * (1.0f / kOversample));
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
            for (int c = 0; c < 2; ++c)
                if (cross[c] != nullptr)
                    lastCross[c] = cross[c][n - 1];
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
    float history[kMaxVoices][kTaps - 1] = {};
    float prevInc[kMaxVoices] = {}, curInc[kMaxVoices] = {}, prevGainL[kMaxVoices] = {}, curGainL[kMaxVoices] = {}, prevGainR[kMaxVoices] = {},
          curGainR[kMaxVoices] = {}, curFrameOf[kMaxVoices] = {};
    float prevFrame = 0.0f, curFrame = 0.0f;
    float lastCross[2] = {};
    int curCount = 1, prevCount = 1;
    bool havePrev = false;
};
} // namespace own
} // namespace synthyboy
