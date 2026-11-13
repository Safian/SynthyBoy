// SynthyBoy - own engine: the nine effects of the standard chain (chorus, compressor, delay, distortion, EQ, filter, flanger, phaser, reverb).
// Written from public DSP knowledge and tuned against the oracle (tools/own-oracle); original code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/fastmath.h"
#include <algorithm>
#include <cmath>
#include <array>

#include "../osc/warp.h"
#include "../voice/filter_unit.h"
#include "fx_compressor.h"
#include "fx_dsp.h"
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
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
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
    /** The LFO runs on while the module is skipped on silence (FxChain, tier low): the same per sample steps as process (). */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        for (int s = 0; s < n; ++s)
        {
            phase += p.rate / sr;
            phase -= ffloor (phase);
        }
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
            phase -= ffloor (phase);
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
                        ph -= ffloor (ph);
                        const float mod = 1.0f + 0.5f * (fast ? fastmath::sinCycles (ph) : std::sin (kTwoPif * ph));
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
    /** The LFO runs on while the module is skipped on silence (FxChain, tier low): the same per sample steps as process (). */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        for (int s = 0; s < n; ++s)
        {
            phase += p.rate / sr;
            phase -= ffloor (phase);
        }
    }
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
            phase -= ffloor (phase);
            for (int c = 0; c < 2; ++c)
            {
                const float ph = phase - 0.5f * p.phaseOffset + (c == 1 ? p.phaseOffset : 0.0f);
                const float x = ph + 0.75f;
                const float tri = std::fabs (2.0f * (x - ffloor (x)) - 1.0f);
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
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
    static constexpr int kStages = 12;
    struct Params { float mix = 0.5f, feedback = 0.5f, rate = 0.12f, depthSemis = 24.0f, centerHz = 2000.0f, blend = 0.5f, phaseOffset = 120.0f / 360.0f, depth2 = 0.0f; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& c : z) for (auto& x : c) x = 0.0f; phase = 0.0f; fb[0] = fb[1] = 0.0f; for (auto& x : bpA) x = 0.0f; for (auto& x : bpB) x = 0.0f; }
    /** The LFO runs on while the module is skipped on silence (FxChain, tier low): the same per sample steps as process (). */
    void idle (int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        for (int s = 0; s < n; ++s)
        {
            phase += p.rate / sr;
            phase -= ffloor (phase);
        }
    }
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
        const bool spread = std::fabs (p.depth2) > 1.0e-4f;
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            phase += p.rate / sr;
            phase -= ffloor (phase);
            for (int c = 0; c < 2; ++c)
            {
                const float ph = phase + (c == 1 ? p.phaseOffset : -p.phaseOffset) * 0.5f;
                const float x01 = ph - ffloor (ph);
                const float v = 2.0f * std::fabs (2.0f * x01 - 1.0f) - 1.0f; // triangle -1 .. 1
                const float e2 = (centreNote + p.depthSemis * v) * (1.0f / 12.0f);
                const float hz = std::clamp (8.1757989f * (fast ? fastmath::exp2 (e2) : std::exp2 (e2)), 20.0f, 0.45f * sr);
                const float c1 = OnePole::coefficient (hz / 20.0f, sampleRate), c2 = OnePole::coefficient (hz * 20.0f, sampleRate);
                float aS[kStages];
                if (spread)
                {
                    // depth 2: the stage cutoffs spread linearly in semitones over [-depth2 / 2, +depth2 / 2] around the centre (stage 0 lowest for a positive value)
                    for (int s = 0; s < kStages; ++s)
                    {
                        const float hs = std::max (hz * std::exp2 (p.depth2 * (static_cast<float> (s) / 11.0f - 0.5f) * (1.0f / 12.0f)), 5.0f);
                        const float g = phaserStageG (hs, sampleRate);
                        aS[s] = g / (1.0f + g);
                    }
                }
                else
                {
                    const float g = phaserStageG (hz, sampleRate);
                    const float a0 = g / (1.0f + g);
                    for (int s = 0; s < kStages; ++s)
                        aS[s] = a0;
                }
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
                bpA[c] += c2 * (ap - bpA[c]);
                bpB[c] += c1 * (bpA[c] - bpB[c]);
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
    bool fast = false; // tier low: tanh, sin, exp, exp2 and pow by the cheaper forms of dsp/fastmath.h
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
        const float dExp = std::clamp (p.driveDb, -30.0f, 30.0f) * 0.05f;
        const float d = fast ? fastmath::pow10 (dExp) : std::pow (10.0f, dExp);
        auto th = [this] (float a) { return fast ? fastmath::tanh (a) : std::tanh (a); };
        auto ex = [this] (float a) { return fast ? fastmath::exp (a) : std::exp (a); };
        const float x = d != 0.0f ? v / d : v; // the unscaled input
        float y;
        switch (p.type)
        {
            case 0: y = th (v); break;
            case 1: y = std::clamp (v, -1.0f, 1.0f); break;
            case 2: { const float t = 0.25f * v + 0.75f; y = std::fabs (-4.0f * (t - ffloor (t)) + 2.0f) - 1.0f; break; }
            case 3: { const float t = 0.5f - 0.25f * v; y = fast ? fastmath::sinCycles (t) : std::sin (kTwoPif * (t - ffloor (t))); break; }
            case 4: { const float q = std::clamp (u * u, 1.5e-8f, 1.0f); y = std::round (x / q) * q; break; }
            case 5:
            {
                const float period = std::max (static_cast<float> (sampleRate) * std::max (0.99f / std::max ((1.0f - u) * (1.0f - u), 1.0e-4f), 1.0f) / 88200.0f, 1.0f);
                holdPhase[c] += 1.0f;
                if (holdPhase[c] >= period) { holdPhase[c] -= period; held[c] = x; }
                y = held[c];
                break;
            }
            case 6: y = th (v + 0.3f) - th (0.3f); break;
            case 7: { const float vp = std::max (v, 0.0f), vn = std::min (v, 0.0f); y = (1.0f - ex (-vp)) - 0.5f * (1.0f - ex (2.0f * vn)); break; }
            case 8: y = ex (std::min (v, 0.0f)) - ex (-std::max (v, 0.0f)); break;
            case 9: y = th (32.0f * v); break;
            case 10: y = th (std::max (v, 0.0f)) + std::max (std::min (v, 0.0f), -0.5f); break;
            case 11: y = std::fabs (std::clamp (v, -1.0f, 1.0f)); break;
            case 12: y = std::clamp (v, -1.0f, 1.0f); break; // X-Shaper: the drawn curves default to the identity
            default: y = std::clamp (v, -1.0f, 1.0f); break;
        }
        const bool asym = p.type == 6 || p.type == 7 || p.type == 10 || p.type == 11 || p.type == 13;
        if (asym) // 10 Hz DC blocker
        {
            const float a = 1.0f - (fast ? fastmath::exp (-kTwoPif * 10.0f / static_cast<float> (sampleRate)) : std::exp (-kTwoPif * 10.0f / static_cast<float> (sampleRate)));
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

} // namespace own
} // namespace synthyboy
