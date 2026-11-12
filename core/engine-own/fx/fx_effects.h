// SynthyBoy - own engine: the nine effects of the standard chain (chorus, compressor, delay, distortion, EQ, filter, flanger, phaser, reverb).
// Written from public DSP knowledge and tuned against the oracle (tools/own-oracle); original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "../osc/warp.h"
#include "../voice/filter_unit.h"
#include "fx_dsp.h"

namespace synthyboy
{
namespace own
{
/** Dry / wet crossfade of the effects (equal power). */
inline float mixLaw (float dry, float wet, float mix) noexcept
{
    const float a = clamp01 (mix) * (kPif * 0.5f);
    return dry * std::cos (a) + wet * std::sin (a);
}

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
                    l[i] = mixLaw (xl, outL, p.mix);
                    r[i] = mixLaw (xr, outR, p.mix);
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
            l[i] = mixLaw (xl, rd[0], p.mix);
            r[i] = mixLaw (xr, rd[1], p.mix);
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
    struct Params { float mix = 0.5f, rate = 0.4f, depth = 0.5f, feedback = 0.4f, delay1 = 0.002f, delay2 = 0.008f, cutoff = 260.0f, spread = 1.0f; int voices = 4; };
    static constexpr int kMaxPairs = 4;
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& d : line)
            d.prepare (static_cast<int> (sr * 0.085));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (auto& b : band) b.reset();
        phase = 0.0f;
    }
    void set (const Params& np) noexcept { p = np; }
    /** `voices` pairs of two delay lines (A with delay 1, B with delay 2), each with a left and a right side (document 06, 2). */
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const int V = std::clamp (p.voices, 1, kMaxPairs);
        const float fb = std::clamp (p.feedback, -0.95f, 0.95f);
        const float avg = 0.5f * (p.delay1 + p.delay2);
        for (int k = 0; k < kMaxPairs * 4; ++k)
            band[k].set (p.cutoff, p.spread, sampleRate);
        for (int s = 0; s < n; ++s)
        {
            phase += p.rate / sr;
            phase -= std::floor (phase);
            const float in[2] = { l[s], r[s] };
            float acc[2] = { 0.0f, 0.0f };
            for (int i = 0; i < V; ++i)
            {
                const float t = (i > 0 && V > 1) ? static_cast<float> (i) / static_cast<float> (V - 1) : 0.0f;
                for (int vi = 0; vi < 2; ++vi)
                    for (int c = 0; c < 2; ++c)
                    {
                        const int k = ((i * 2 + vi) * 2) + c;
                        float ph = phase + (c == 1 ? 0.25f : 0.0f) + (vi == 1 ? 0.5f : 0.0f) + 0.25f * static_cast<float> (i) / static_cast<float> (V);
                        ph -= std::floor (ph);
                        const float mod = 1.0f + 0.5f * std::sin (kTwoPif * ph);
                        const float base = vi == 0 ? p.delay1 : p.delay2;
                        const float delay = mod * 0.03f * p.depth + lerp (base, avg, t);
                        const float period = std::clamp (delay * sr, 3.0f, 0.08f * sr);
                        const float rd = line[k].readCubic (period);
                        line[k].push (band[k].process (loopLimit (in[c] + fb * rd)));
                        acc[c] += rd;
                    }
            }
            l[s] = mixLaw (in[0], 0.53f * acc[0], p.mix);
            r[s] = mixLaw (in[1], 0.53f * acc[1], p.mix);
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    std::array<DelayLine, kMaxPairs * 4> line;
    LoopBand band[kMaxPairs * 4];
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
            d.prepare (static_cast<int> (sr * 0.9));
        reset();
    }
    void reset() noexcept { for (auto& d : line) d.reset(); phase = 0.0f; }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float f = std::clamp (p.feedback, -1.0f, 1.0f);
        const float dc = 1.0f / std::clamp (p.centerHz, 20.0f, 16000.0f);
        const float m = 0.5f * clamp01 (p.mix); // the mix knob runs over half of the equal power law
        const float wetG = std::sin (m * kPif * 0.5f), dryG = std::cos (m * kPif * 0.5f);
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            phase += p.rate / sr;
            phase -= std::floor (phase);
            for (int c = 0; c < 2; ++c)
            {
                const float ph = phase - 0.5f * p.phaseOffset + (c == 1 ? p.phaseOffset : 0.0f);
                const float x = ph + 0.75f;
                const float tri = std::fabs (2.0f * (x - std::floor (x)) - 1.0f);
                const float mod = p.depth * (2.0f * tri - 1.0f) + 1.0f;
                const float delay = std::max ((dc - 0.0005f) * mod + 0.0005f, 1.0f / 20000.0f);
                const float period = std::clamp (delay * sr, 2.0f, 0.85f * sr);
                const float rd = line[c].readCubic (period);
                const float in = io[c][i];
                line[c].push (std::clamp (in + f * rd, -1.0f, 1.0f) * 1.0f);
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
    static constexpr int kStages = 12;
    struct Params { float mix = 0.5f, feedback = 0.5f, rate = 0.12f, depthSemis = 24.0f, centerHz = 2000.0f, blend = 0.5f, phaseOffset = 120.0f / 360.0f; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& c : z) for (auto& x : c) x = 0.0f; phase = 0.0f; fb[0] = fb[1] = 0.0f; for (auto& x : bpA) x = 0.0f; for (auto& x : bpB) x = 0.0f; }
    void set (const Params& np) noexcept { p = np; }
    /** The phaser of document 06, 8: the twelve stage all-pass filter of document 05, 7 (taps after 4, 8 and 12 stages by the blend), fed with a cutoff that follows a
        triangle LFO (the right channel offset by the phase offset), a band passed feedback with tanh, output (x + all-pass) / 2, linear dry / wet. */
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float fbk = clamp01 (p.feedback);
        const float braw = clamp01 (p.blend) * 2.0f;
        const float p1 = clamp01 (1.0f - braw), p5 = clamp01 (braw - 1.0f), p3 = 1.0f - p1 - p5;
        const float centreNote = 12.0f * std::log2 (std::max (p.centerHz, 1.0f) / 8.1757989f);
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            phase += p.rate / sr;
            phase -= std::floor (phase);
            for (int c = 0; c < 2; ++c)
            {
                const float ph = phase + (c == 1 ? p.phaseOffset : -p.phaseOffset) * 0.5f;
                const float x01 = ph - std::floor (ph);
                const float v = 2.0f * std::fabs (2.0f * x01 - 1.0f) - 1.0f; // triangle -1 .. 1
                const float hz = std::clamp (8.1757989f * std::exp2 ((centreNote + p.depthSemis * v) * (1.0f / 12.0f)), 20.0f, 0.45f * sr);
                const float x0 = kPif * hz / sr;
                const float g = std::tan (std::min (0.499f * kPif, x0 / (1.0f + x0)));
                const float a = g / (1.0f + g);
                const float c1 = OnePole::coefficient (hz / 20.0f, sampleRate), c2 = OnePole::coefficient (hz * 20.0f, sampleRate);
                const float dry = io[c][i];
                float x = dry * (1.0f + 0.5f * fbk) + fb[c];
                float tap[3] = { 0.0f, 0.0f, 0.0f };
                for (int s = 0; s < kStages; ++s)
                {
                    const float vv = (x - z[c][s]) * a;
                    const float lp = vv + z[c][s];
                    z[c][s] = flushDenormal (lp + vv);
                    x = x - 2.0f * lp;
                    if (s == 3) tap[0] = x;
                    if (s == 7) tap[1] = x;
                    if (s == 11) tap[2] = x;
                }
                const float ap = p1 * tap[0] + p3 * tap[1] + p5 * tap[2];
                bpA[c] += c2 * (ap - bpA[c]);
                bpB[c] += c1 * (bpA[c] - bpB[c]);
                fb[c] = std::tanh (fbk * (bpA[c] - bpB[c]));
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
    struct Params { int type = 0; float driveDb = 0.0f, mix = 1.0f, cutoff = 4000.0f, resonance = 0.5f, blend = 0.0f; int order = 0; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& s : svf) s.reset(); held[0] = held[1] = 0.0f; counter = 0; }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float g = std::pow (10.0f, p.driveDb * 0.05f);
        const float cg = Svf::coefficient (p.cutoff, sampleRate);
        const float k = 2.0f - 1.9f * clamp01 (p.resonance);
        float* io[2] = { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float dry = io[c][i];
                float x = dry;
                if (p.order == 1) x = filt (c, x, cg, k);
                x = shape (c, x * g);
                if (p.order == 2) x = filt (c, x, cg, k);
                io[c][i] = dry * (1.0f - p.mix) + x * p.mix;
            }
    }

private:
    float filt (int c, float x, float g, float k) noexcept
    {
        svf[c].process (x, g, k);
        // blend 0 .. 2 : low pass, band pass, high pass
        const float b = std::clamp (p.blend, 0.0f, 2.0f);
        return b < 1.0f ? lerp (svf[c].lp, svf[c].bp * k, b) : lerp (svf[c].bp * k, svf[c].hp, b - 1.0f);
    }
    float shape (int c, float v) noexcept
    {
        // v = x * drive; the laws of docs/engine-spec/06-effects.md section 4
        const float u = clamp01 ((p.driveDb + 30.0f) / 60.0f);
        const float d = std::pow (10.0f, std::clamp (p.driveDb, -30.0f, 30.0f) * 0.05f);
        const float x = d != 0.0f ? v / d : v; // the unscaled input
        float y;
        switch (p.type)
        {
            case 0: y = std::tanh (v); break;
            case 1: y = std::clamp (v, -1.0f, 1.0f); break;
            case 2: { const float t = 0.25f * v + 0.75f; y = std::fabs (-4.0f * (t - std::floor (t)) + 2.0f) - 1.0f; break; }
            case 3: { const float t = 0.5f - 0.25f * v; y = std::sin (kTwoPif * (t - std::floor (t))); break; }
            case 4: { const float q = std::clamp (u * u, 1.5e-8f, 1.0f); y = std::round (x / q) * q; break; }
            case 5:
            {
                const float period = std::max (static_cast<float> (sampleRate) * std::max (0.99f / std::max ((1.0f - u) * (1.0f - u), 1.0e-4f), 1.0f) / 88200.0f, 1.0f);
                holdPhase[c] += 1.0f;
                if (holdPhase[c] >= period) { holdPhase[c] -= period; held[c] = x; }
                y = held[c];
                break;
            }
            case 6: y = std::tanh (v + 0.3f) - std::tanh (0.3f); break;
            case 7: { const float vp = std::max (v, 0.0f), vn = std::min (v, 0.0f); y = (1.0f - std::exp (-vp)) - 0.5f * (1.0f - std::exp (2.0f * vn)); break; }
            case 8: y = std::exp (std::min (v, 0.0f)) - std::exp (-std::max (v, 0.0f)); break;
            case 9: y = std::tanh (32.0f * v); break;
            case 10: y = std::tanh (std::max (v, 0.0f)) + std::max (std::min (v, 0.0f), -0.5f); break;
            case 11: y = std::fabs (std::clamp (v, -1.0f, 1.0f)); break;
            case 12: y = std::clamp (v, -1.0f, 1.0f); break; // X-Shaper: the drawn curves default to the identity
            default: y = std::clamp (v, -1.0f, 1.0f); break;
        }
        const bool asym = p.type == 6 || p.type == 7 || p.type == 10 || p.type == 11 || p.type == 13;
        if (asym) // 10 Hz DC blocker
        {
            const float a = 1.0f - std::exp (-kTwoPif * 10.0f / static_cast<float> (sampleRate));
            dc[c] += a * (y - dc[c]);
            y -= dc[c];
        }
        return y;
    }
    Params p;
    double sampleRate = 48000.0;
    Svf svf[2];
    float held[2] = {}, holdPhase[2] = {}, dc[2] = {};
    unsigned counter = 0;
};

class Equalizer
{
public:
    struct Band { bool on = true; int mode = 0; float hz = 1000.0f, gainDb = 0.0f, res = 0.3f; };
    struct Params { Band low, mid, mid2, high; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& c : bq) for (auto& b : c) b.reset(); }
    void set (const Params& np) noexcept
    {
        p = np;
        for (int c = 0; c < 2; ++c)
        {
            auto q = [] (float res) { return 0.3f + 2.5f * res * res; };
            bq[c][0].design (p.low.mode == 1 ? Biquad::highpass : Biquad::lowShelf, p.low.hz, p.low.mode == 1 ? 0.5f + 1.5f * p.low.res : 0.5f, p.low.gainDb, sampleRate);
            bq[c][1].design (p.mid.mode == 1 ? Biquad::notch : Biquad::peak, p.mid.hz, q (p.mid.res), p.mid.gainDb, sampleRate);
            bq[c][2].design (p.mid2.mode == 2 ? Biquad::notch : Biquad::peak, p.mid2.hz, q (p.mid2.res), p.mid2.gainDb, sampleRate);
            bq[c][3].design (p.high.mode == 1 ? Biquad::lowpass : Biquad::highShelf, p.high.hz, p.high.mode == 1 ? 0.5f + 1.5f * p.high.res : 0.5f, p.high.gainDb, sampleRate);
        }
    }
    void process (float* l, float* r, int n) noexcept
    {
        float* io[2] = { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                float x = io[c][i];
                x = bq[c][0].process (x);
                x = bq[c][1].process (x);
                if (p.mid2.mode != 0)
                    x = bq[c][2].process (x);
                io[c][i] = bq[c][3].process (x);
            }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    Biquad bq[2][4];
};

class Compressor
{
public:
    struct Band { float lowerDb = -40, upperDb = -20, lowerRatio = 0, upperRatio = 0.5f, gainDb = 0; };
    struct Params { float mix = 1.0f, attack = 0.5f, release = 0.5f, crossLow = 120.0f, crossHigh = 2500.0f; int bands = 0; Band band[3]; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept
    {
        for (auto& c : lr) for (auto& b : c) b.reset();
        for (auto& e : envHi) e = 0.0f;
        for (auto& e : envLo) e = 1.0f;
    }
    void set (const Params& np) noexcept
    {
        p = np;
        // one split (two bands) for the Low / High band modes, two splits for Multiband
        const float f1 = p.bands == 2 ? p.crossHigh : p.crossLow, f2 = p.crossHigh;
        for (int c = 0; c < 2; ++c)
        {
            lr[c][0].design (Biquad::lowpass, f1, 0.7071f, 0.0f, sampleRate);
            lr[c][1].copyCoefficients (lr[c][0]);
            lr[c][2].design (Biquad::highpass, f1, 0.7071f, 0.0f, sampleRate);
            lr[c][3].copyCoefficients (lr[c][2]);
            lr[c][4].design (Biquad::lowpass, f2, 0.7071f, 0.0f, sampleRate);
            lr[c][5].copyCoefficients (lr[c][4]);
            lr[c][6].design (Biquad::highpass, f2, 0.7071f, 0.0f, sampleRate);
            lr[c][7].copyCoefficients (lr[c][6]);
        }
    }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        static const float kAtk[3] = { 2.8f, 1.4f, 0.7f }, kRel[3] = { 40.0f, 28.0f, 15.0f };
        // which parameter set each signal band uses
        int use[3] = { 0, 1, 2 };
        if (p.bands == 1) { use[0] = 0; use[1] = 1; use[2] = 1; }
        if (p.bands == 2) { use[0] = 1; use[1] = 2; use[2] = 2; }
        if (p.bands == 3) { use[0] = use[1] = use[2] = 1; }
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
                }
                else if (p.bands == 3) { lowS = 0.0f; midS = x; }
                else midS = restS; // two bands: low | rest
                sig[0][c] = lowS; sig[1][c] = midS; sig[2][c] = highS;
            }
            for (int b = 0; b < 3; ++b)
            {
                const Band& bd = p.band[use[b]];
                const float x2 = 0.5f * (sig[b][0] * sig[b][0] + sig[b][1] * sig[b][1]);
                const float thrU = std::pow (10.0f, std::clamp (bd.upperDb, -100.0f, 12.0f) * 0.1f), thrL = std::pow (10.0f, std::clamp (bd.lowerDb, -100.0f, 12.0f) * 0.1f);
                const int tb = use[b];
                const float expo = std::exp (8.0f * p.attack - 4.0f), expr = std::exp (8.0f * p.release - 4.0f);
                const float Na = std::max (5.0f, kAtk[tb] * sr * 0.001f * expo), Nr = std::max (5.0f, kRel[tb] * sr * 0.001f * expr);
                float& eh = envHi[b];
                float& el = envLo[b];
                const float Nh = x2 > eh ? Na : Nr, Nl = x2 > el ? Na : Nr;
                eh = (x2 + eh * Nh) / (Nh + 1.0f);
                el = (x2 + el * Nl) / (Nl + 1.0f);
                eh = flushDenormal (eh);
                const float envH = std::max (eh, thrU), envL = std::min (std::max (el, 1.0e-20f), thrL);
                const float gHi = std::pow (thrU / envH, clamp01 (bd.upperRatio) * 0.5f);
                const float gLo = std::pow (thrL / envL, std::clamp (bd.lowerRatio, -1.0f, 1.0f) * 0.5f);
                const float gain = std::clamp (gHi * gLo, 0.0f, 32.0f) * std::pow (10.0f, bd.gainDb * 0.05f);
                const bool skip = (p.bands == 1 && b == 2) || (p.bands == 2 && b == 2) || (p.bands == 3 && b != 1);
                for (int c = 0; c < 2; ++c)
                    if (!skip) out[c] += sig[b][c] * gain;
            }
            l[i] = in[0] + (out[0] - in[0]) * p.mix;
            r[i] = in[1] + (out[1] - in[1]) * p.mix;
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    Biquad lr[2][8];
    float envHi[3] = {}, envLo[3] = { 1.0f, 1.0f, 1.0f };
};

/** The reverb: a 16 line feedback delay network with Schroeder all-pass diffusers in the lines (docs/engine-spec/06-effects.md 9). */
class Reverb
{
public:
    static constexpr int kLines = 16;
    struct Params { float mix = 0.25f, size = 0.5f, decay = 1.5f, predelay = 0.0f, lowCut = 20.0f, highCut = 8000.0f, highShelfDb = -1.0f, highShelfHz = 1400.0f,
                          lowShelfDb = 0.0f, lowShelfHz = 100.0f, chorusAmount = 0.2f, chorusRate = 0.25f, width = 1.0f, diffA = 0.7f, diffB = 0.6f; int type = 0; };
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
        pre[0].prepare (static_cast<int> (sr * 0.32));
        pre[1].prepare (static_cast<int> (sr * 0.32));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (auto& d : apLine) d.reset();
        for (auto& d : pre) d.reset();
        for (auto& x : hsLp) x = 0.0f;
        for (auto& x : lsLp) x = 0.0f;
        for (auto& x : inLp) x = 0.0f;
        phase = 0.0f;
        for (auto& x : stored) x = 0.0f;
    }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        static const float kD[kLines] = { 6753.2f, 9278.4f, 7704.5f, 11328.5f, 9701.12f, 5512.5f, 8480.45f, 5638.65f,
                                          3120.73f, 3429.5f, 3626.37f, 7713.52f, 4521.54f, 6518.97f, 5265.56f, 5630.25f };
        static const float kA[kLines] = { 1001, 799, 933, 876, 895, 807, 907, 853, 957, 1019, 711, 567, 833, 779, 663, 997 };
        const float sr = static_cast<float> (sampleRate), rate = sr / 44100.0f;
        const float sizeMult = std::exp2 (4.0f * p.size - 3.0f) * (p.type == 5 ? 1.5f : (p.type == 1 ? 0.7f : 1.0f));
        const float T = std::clamp (p.decay, 0.1f, 100.0f);
        float len[kLines], gain[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            len[k] = sizeMult * kD[k] * rate;
            gain[k] = std::pow (0.001f, sizeMult * kD[k] / (T * 44100.0f));
        }
        const float aHs = std::pow (10.0f, std::clamp (p.highShelfDb, -24.0f, 0.0f) * 0.05f);
        const float aLs = 1.0f - std::pow (10.0f, std::clamp (p.lowShelfDb, -24.0f, 0.0f) * 0.05f);
        const float cHs = OnePole::coefficient (p.highShelfHz, sampleRate), cLs = OnePole::coefficient (p.lowShelfHz, sampleRate);
        const float cHi = OnePole::coefficient (p.highCut, sampleRate), cLo = OnePole::coefficient (p.lowCut, sampleRate);
        const float depth = p.chorusAmount * p.chorusAmount * 2500.0f * rate;
        const float pd = p.predelay * sr;
        for (int i = 0; i < n; ++i)
        {
            phase += std::min (p.chorusRate, 16.0f) / sr;
            phase -= std::floor (phase);
            // input: inverted band pass between the two cuts, scaled
            float in[2] = { l[i], r[i] }, bp[2];
            for (int c = 0; c < 2; ++c)
            {
                inLp[c] += cHi * (in[c] - inLp[c]);
                inLp[c + 2] += cLo * (in[c] - inLp[c + 2]);
                bp[c] = (inLp[c + 2] - inLp[c]) * 0.25f;
            }
            float v[kLines];
            for (int k = 0; k < kLines; ++k)
            {
                const int group = k / 4, lane = k % 4;
                const float ph = kTwoPif * (phase + static_cast<float> (lane) / 16.0f);
                const float mod = depth * (group == 0 ? std::cos (ph) : (group == 1 ? -std::cos (ph) : (group == 2 ? std::sin (ph) : -std::sin (ph))));
                const float rd = line[k].read (std::max (len[k] + mod, 32.0f));
                const float aRead = apLine[k].read (kA[k] * static_cast<float> (scale));
                const float w = rd - 0.6f * aRead;
                apLine[k].push (bp[k < 8 ? 0 : 1] + w);
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
            float outSide[2] = { 0.0f, 0.0f };
            for (int k = 0; k < kLines; ++k)
            {
                const int g = k / 4, j = k % 4;
                float y = v[k] + 0.25f * S - 0.5f * C[j] - 0.5f * R[g];
                hsLp[k] += cHs * (y - hsLp[k]);
                y = hsLp[k] + aHs * (y - hsLp[k]) * 0.0f + aHs * (y - hsLp[k]);
                lsLp[k] += cLs * (y - lsLp[k]);
                y -= aLs * lsLp[k];
                hsLp[k] = flushDenormal (hsLp[k]);
                lsLp[k] = flushDenormal (lsLp[k]);
                y *= gain[k];
                line[k].push (y);
                outSide[k < 8 ? 0 : 1] += y;
            }
            const float wl0 = outSide[0] * kOutGain, wr0 = outSide[1] * kOutGain;
            const float m = 0.5f * (wl0 + wr0), sd = 0.5f * (wl0 - wr0) * p.width;
            pre[0].push (m + sd);
            pre[1].push (m - sd);
            const float wl = pd > 0.5f ? pre[0].read (pd) : m + sd, wr = pd > 0.5f ? pre[1].read (pd) : m - sd;
            l[i] = mixLaw (l[i], wl, p.mix);
            r[i] = mixLaw (r[i], wr, p.mix);
        }
    }

private:
    static constexpr float kOutGain = 0.46f;
    Params p;
    double sampleRate = 48000.0;
    int scale = 2;
    DelayLine line[kLines], apLine[kLines], pre[2];
    float hsLp[kLines] = {}, lsLp[kLines] = {}, inLp[4] = {}, stored[kLines] = {};
    float phase = 0.0f;
};
} // namespace own
} // namespace synthyboy
