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

/** Every effect: prepare once, setParams once per control period, process in place. `mix` is 0 .. 1; the level (dB) is applied by the chain. */
class Delay
{
public:
    struct Params { float mix = 0.3f, feedback = 0.4f, timeL = 0.3f, timeR = 0.3f, cutoff = 260.0f, spread = 1.0f; int style = 1; };
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
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        timeSmooth[0].snap (p.timeL * static_cast<float> (sampleRate));
        timeSmooth[1].snap (p.timeR * static_cast<float> (sampleRate));
    }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        timeSmooth[0].target (std::clamp (p.timeL, 0.002f, 4.0f) * sr, n);
        timeSmooth[1].target (std::clamp (p.timeR, 0.002f, 4.0f) * sr, n);
        const float hpHz = p.cutoff / std::exp2 (p.spread * 3.6f), lpHz = p.cutoff * std::exp2 (p.spread * 6.3f);
        const float ghp = Svf::coefficient (hpHz, sampleRate), alp = OnePole::coefficient (lpHz, sampleRate);
        const float fb = std::clamp (p.feedback, -0.98f, 0.98f);
        for (int i = 0; i < n; ++i)
        {
            const float dl = timeSmooth[0].next(), dr = timeSmooth[1].next();
            float tl = line[0].read (dl - 1.0f), tr = line[1].read (dr - 1.0f);
            // feedback path: high pass and low pass at the band of the filter
            hp[0].process (tl, ghp, 1.4142f); tl = lp[0].process (hp[0].hp, alp);
            hp[1].process (tr, ghp, 1.4142f); tr = lp[1].process (hp[1].hp, alp);
            float inL = l[i], inR = r[i], wl = tl, wr = tr;
            float feedL, feedR;
            switch (p.style)
            {
                case 0: { const float m = 0.5f * (inL + inR); feedL = m + fb * 0.5f * (tl + tr); feedR = feedL; break; }       // mono
                case 2: feedL = 0.5f * (inL + inR) + fb * tr; feedR = fb * tl; break;                                          // ping pong
                case 3: feedL = 0.5f * (inL + inR) + fb * tr; feedR = fb * tl; break;                                          // mid ping pong
                case 4: feedL = 0.5f * (inL + inR); feedR = 0.0f + fb * tl; break;                                             // tap -> delay
                default: feedL = inL + fb * tl; feedR = inR + fb * tr; break;                                                  // stereo
            }
            line[0].push (feedL);
            line[1].push (feedR);
            if (p.style == 0)
                wr = wl;
            l[i] = mixLaw (inL, wl, p.mix);
            r[i] = mixLaw (inR, wr, p.mix);
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    std::array<DelayLine, 2> line;
    Svf hp[2];
    OnePole lp[2];
    Ramp timeSmooth[2];
};

class Chorus
{
public:
    struct Params { float mix = 0.5f, rate = 0.4f, depth = 0.5f, feedback = 0.4f, delay1 = 0.002f, delay2 = 0.008f, cutoff = 260.0f, spread = 1.0f; int voices = 4; };
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& d : line)
            d.prepare (static_cast<int> (sr * 0.06));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        phase = 0.0f;
        fbState[0] = fbState[1] = 0.0f;
    }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float hpHz = p.cutoff / std::exp2 (p.spread * 3.6f), lpHz = p.cutoff * std::exp2 (p.spread * 6.3f);
        const float ghp = Svf::coefficient (hpHz, sampleRate), alp = OnePole::coefficient (lpHz, sampleRate);
        const float fb = std::clamp (p.feedback, -0.95f, 0.95f);
        const int V = std::clamp (p.voices, 1, 4);
        for (int i = 0; i < n; ++i)
        {
            phase += p.rate / sr;
            phase -= std::floor (phase);
            line[0].push (l[i] + fb * fbState[0]);
            line[1].push (r[i] + fb * fbState[1]);
            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < V; ++v)
            {
                // delay between delay1 and delay2, modulated by depth around the mean; voices are spread over the LFO cycle
                const float base = (v & 1) ? p.delay2 : p.delay1;
                const float lfoPh = phase + static_cast<float> (v) / static_cast<float> (V);
                const float modL = std::sin (kTwoPif * lfoPh), modR = std::sin (kTwoPif * (lfoPh + 0.25f));
                const float dL = base * (1.0f + 0.5f * p.depth * modL) * sr, dR = base * (1.0f + 0.5f * p.depth * modR) * sr;
                wl += line[0].read (dL);
                wr += line[1].read (dR);
            }
            wl /= static_cast<float> (V);
            wr /= static_cast<float> (V);
            hp[0].process (wl, ghp, 1.4142f); wl = lp[0].process (hp[0].hp, alp);
            hp[1].process (wr, ghp, 1.4142f); wr = lp[1].process (hp[1].hp, alp);
            fbState[0] = wl;
            fbState[1] = wr;
            l[i] += 2.4f * p.mix * wl; // the wet voices add to the dry signal (level matched to the oracle)
            r[i] += 2.4f * p.mix * wr;
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    std::array<DelayLine, 2> line;
    Svf hp[2];
    OnePole lp[2];
    float phase = 0.0f, fbState[2] = {};
};

class Flanger
{
public:
    struct Params { float mix = 0.5f, feedback = 0.5f, rate = 0.25f, depth = 0.5f, centerHz = 330.0f, phaseOffset = 120.0f / 360.0f; };
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& d : line)
            d.prepare (static_cast<int> (sr * 0.06));
        reset();
    }
    void reset() noexcept { for (auto& d : line) d.reset(); phase = 0.0f; fb[0] = fb[1] = 0.0f; }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float f = std::clamp (p.feedback, -0.97f, 0.97f);
        for (int i = 0; i < n; ++i)
        {
            phase += p.rate / sr;
            phase -= std::floor (phase);
            const float modL = std::sin (kTwoPif * phase), modR = std::sin (kTwoPif * (phase + p.phaseOffset));
            // the comb notch pattern is centred at `center`: delay = 1 / (2 f); depth moves it by up to two octaves
            const float fL = p.centerHz * std::exp2 (2.0f * p.depth * modL), fR = p.centerHz * std::exp2 (2.0f * p.depth * modR);
            const float dL = std::clamp (sr / (2.0f * std::clamp (fL, 20.0f, 20000.0f)), 1.0f, sr * 0.05f), dR = std::clamp (sr / (2.0f * std::clamp (fR, 20.0f, 20000.0f)), 1.0f, sr * 0.05f);
            line[0].push (l[i] + f * fb[0]);
            line[1].push (r[i] + f * fb[1]);
            const float wl = line[0].read (dL), wr = line[1].read (dR);
            fb[0] = wl;
            fb[1] = wr;
            l[i] = mixLaw (l[i], wl * 0.7f, p.mix);
            r[i] = mixLaw (r[i], wr * 0.7f, p.mix);
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    std::array<DelayLine, 2> line;
    float phase = 0.0f, fb[2] = {};
};

class Phaser
{
public:
    static constexpr int kStages = 8;
    struct Params { float mix = 0.5f, feedback = 0.5f, rate = 0.12f, depthSemis = 24.0f, centerHz = 2000.0f, blend = 0.5f, phaseOffset = 120.0f / 360.0f; };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& c : z) for (auto& x : c) x = 0.0f; phase = 0.0f; fb[0] = fb[1] = 0.0f; }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float f = std::clamp (p.feedback, 0.0f, 0.97f);
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            phase += p.rate / sr;
            phase -= std::floor (phase);
            for (int c = 0; c < 2; ++c)
            {
                const float mod = std::sin (kTwoPif * (phase + (c == 1 ? p.phaseOffset : 0.0f)));
                const float hz = std::clamp (p.centerHz * std::exp2 (p.depthSemis * mod / 12.0f), 20.0f, 0.45f * sr);
                const float t = std::tan (kPif * hz / sr);
                const float a = (t - 1.0f) / (t + 1.0f);
                float x = io[c][i] + f * fb[c];
                const float dry = io[c][i];
                for (int s = 0; s < kStages; ++s)
                {
                    const float y = a * x + z[c][s];
                    z[c][s] = flushDenormal (x - a * y);
                    x = y;
                }
                fb[c] = x;
                // blend: 0 = the all pass chain adds to the dry signal (notches), 1 = it replaces it (all pass, no notches in the sum)
                const float wet = lerp (0.5f * (dry + x), x, clamp01 (p.blend));
                io[c][i] = dry * (1.0f - p.mix) + wet * p.mix;
            }
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    float z[2][kStages] = {};
    float phase = 0.0f, fb[2] = {};
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
    float shape (int c, float x) noexcept
    {
        switch (p.type)
        {
            case 0: return std::tanh (x);
            case 1: return std::clamp (x, -1.0f, 1.0f);
            case 2: { float v = std::fmod (x + 1.0f, 4.0f); if (v < 0.0f) v += 4.0f; return v < 2.0f ? v - 1.0f : 3.0f - v; }
            case 3: return std::sin (x * kPif * 0.5f);
            case 4: { const float steps = std::max (2.0f, 256.0f / (1.0f + std::fabs (x) * 0.0f)); return std::round (std::clamp (x, -1.0f, 1.0f) * steps * 0.5f) / (steps * 0.5f) ; }
            case 5: { if ((counter++ & 3) == 0) held[c] = x; return std::clamp (held[c], -1.5f, 1.5f); }
            case 6: return std::tanh (x + 0.2f) - std::tanh (0.2f);
            case 7: return x > 0.0f ? std::tanh (x) : 0.4f * std::tanh (x);
            case 8: return x / (1.0f + std::fabs (x));
            case 9: return x >= 0.0f ? 1.0f : -1.0f;
            case 10: return std::tanh (x + 0.5f) - std::tanh (0.5f);
            case 11: return std::fabs (x) * 2.0f - 0.6366f;
            case 12: return x / std::sqrt (1.0f + x * x);
            case 13: return (x + 0.3f) / std::sqrt (1.0f + (x + 0.3f) * (x + 0.3f)) - 0.3f / std::sqrt (1.09f);
            default: return x;
        }
    }
    Params p;
    double sampleRate = 48000.0;
    Svf svf[2];
    float held[2] = {};
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
    void reset() noexcept { for (auto& c : lr) for (auto& b : c) b.reset(); for (auto& e : env) e = 0.0f; }
    void set (const Params& np) noexcept
    {
        p = np;
        for (int c = 0; c < 2; ++c)
        {
            lr[c][0].design (Biquad::lowpass, p.crossLow, 0.7071f, 0.0f, sampleRate);
            lr[c][1].copyCoefficients (lr[c][0]);
            lr[c][2].design (Biquad::highpass, p.crossLow, 0.7071f, 0.0f, sampleRate);
            lr[c][3].copyCoefficients (lr[c][2]);
            lr[c][4].design (Biquad::lowpass, p.crossHigh, 0.7071f, 0.0f, sampleRate);
            lr[c][5].copyCoefficients (lr[c][4]);
            lr[c][6].design (Biquad::highpass, p.crossHigh, 0.7071f, 0.0f, sampleRate);
            lr[c][7].copyCoefficients (lr[c][6]);
        }
    }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float atk = 1.0f - std::exp (-1.0f / (sr * (0.0002f + 0.05f * p.attack * p.attack)));
        const float rel = 1.0f - std::exp (-1.0f / (sr * (0.01f + 1.5f * p.release * p.release)));
        for (int i = 0; i < n; ++i)
        {
            float in[2] = { l[i], r[i] }, out[2] = { 0.0f, 0.0f };
            float bandSig[3][2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = in[c];
                // Linkwitz-Riley 4 split (two biquads each)
                float lowS = lr[c][1].process (lr[c][0].process (x));
                float restS = lr[c][3].process (lr[c][2].process (x));
                float midS = lr[c][5].process (lr[c][4].process (restS));
                float highS = lr[c][7].process (lr[c][6].process (restS));
                if (p.bands == 3) { lowS = 0.0f; midS = x; highS = 0.0f; }
                bandSig[0][c] = lowS; bandSig[1][c] = midS; bandSig[2][c] = highS;
            }
            for (int b = 0; b < 3; ++b)
            {
                const Band& bd = p.band[p.bands == 3 ? 1 : b];
                const bool active = !(p.bands == 1 && b != 0) && !(p.bands == 2 && b != 2);
                // linked detector
                const float power = 0.5f * (bandSig[b][0] * bandSig[b][0] + bandSig[b][1] * bandSig[b][1]); // mean square detector (a sine of peak 1 reads 0.5)
                float& e = env[b];
                e += (power > e ? atk : rel) * (power - e);
                e = flushDenormal (e);
                float gainDb = bd.gainDb;
                if (active)
                {
                    const float ldb = 10.0f * std::log10 (std::max (e, 1.0e-12f)) + 3.0f; // dB of the equivalent sine peak
                    if (ldb > bd.upperDb)
                        gainDb += -(ldb - bd.upperDb) * clamp01 (bd.upperRatio);
                    else if (ldb < bd.lowerDb)
                        gainDb += (bd.lowerDb - ldb) * std::clamp (bd.lowerRatio, -1.0f, 1.0f);
                    gainDb = std::clamp (gainDb, -80.0f, 60.0f);
                }
                const float gain = std::pow (10.0f, gainDb * 0.05f);
                out[0] += bandSig[b][0] * (active ? gain : 1.0f);
                out[1] += bandSig[b][1] * (active ? gain : 1.0f);
            }
            l[i] = in[0] * (1.0f - p.mix) + out[0] * p.mix;
            r[i] = in[1] * (1.0f - p.mix) + out[1] * p.mix;
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    Biquad lr[2][8];
    float env[3] = {};
};

class Reverb
{
public:
    static constexpr int kLines = 8;
    struct Params { float mix = 0.25f, size = 0.5f, decay = 1.5f, predelay = 0.0f, lowCut = 20.0f, highCut = 8000.0f, highShelfDb = -1.0f, highShelfHz = 1400.0f,
                          chorusAmount = 0.2f, chorusRate = 0.25f, width = 1.0f, diffA = 0.7f, diffB = 0.6f; int type = 0; };
    void prepare (double sr)
    {
        sampleRate = sr;
        for (int i = 0; i < kLines; ++i)
            line[i].prepare (static_cast<int> (sr * 0.5));
        pre[0].prepare (static_cast<int> (sr * 0.32));
        pre[1].prepare (static_cast<int> (sr * 0.32));
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 4; ++s)
                ap[c][s].prepare (static_cast<int> (sr * 0.05));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (auto& d : pre) d.reset();
        for (auto& c : ap) for (auto& d : c) d.reset();
        for (auto& x : damp) x = 0.0f;
        for (auto& x : lpIn) x = 0.0f;
        for (auto& f : hpIn) f.reset();
        phase = 0.0f;
    }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        // delay lengths (ms) of the late field, scaled by the size and the type
        static const float kBase[kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.7f };
        const float typeScale = p.type == 1 ? 0.6f : (p.type == 2 ? 1.5f : (p.type == 3 ? 1.1f : (p.type == 4 ? 0.8f : (p.type == 5 ? 2.0f : 1.0f))));
        const float scale = (0.25f + 1.5f * p.size) * typeScale;
        float len[kLines], g[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            len[i] = std::clamp (kBase[i] * 0.001f * scale * sr, 8.0f, sr * 0.45f);
            g[i] = std::pow (10.0f, -3.0f * len[i] / (sr * std::max (p.decay * 1.45f, 0.05f))); // decay knob -> RT60 matched to the oracle
        }
        const float lpA = OnePole::coefficient (std::clamp (p.highShelfHz * std::exp2 (-p.highShelfDb * 0.25f), 500.0f, 16000.0f), sampleRate);
        const float inLp = OnePole::coefficient (p.highCut, sampleRate), inHp = Svf::coefficient (p.lowCut, sampleRate);
        const float pd = p.predelay * sr;
        const float dif = 0.2f + 0.5f * p.diffA;
        for (int i = 0; i < n; ++i)
        {
            phase += p.chorusRate / sr;
            phase -= std::floor (phase);
            const float mono = 0.5f * (l[i] + r[i]);
            pre[0].push (mono);
            float x = pd > 0.5f ? pre[0].read (pd) : mono;
            hpIn[0].process (x, inHp, 1.4142f);
            x = lpIn[0] += inLp * (hpIn[0].hp - lpIn[0]);
            lpIn[0] = flushDenormal (lpIn[0]);
            // input diffusion
            static const float kAp[4] = { 5.3f, 7.9f, 11.3f, 13.7f };
            for (int s = 0; s < 4; ++s)
            {
                const float d = kAp[s] * 0.001f * sr * (0.5f + p.size);
                const float delayed = ap[0][s].read (d);
                const float v = x - dif * delayed;
                ap[0][s].push (v);
                x = delayed + dif * v;
            }
            float tap[kLines];
            for (int k = 0; k < kLines; ++k)
            {
                const float mod = 1.0f + 0.0035f * p.chorusAmount * std::sin (kTwoPif * (phase + 0.125f * static_cast<float> (k)));
                tap[k] = line[k].read (len[k] * mod);
            }
            // Hadamard feedback matrix (orthogonal, energy preserving)
            float mixed[kLines];
            hadamard8 (tap, mixed);
            for (int k = 0; k < kLines; ++k)
            {
                float v = mixed[k] * g[k] + x * kInjection[k];
                damp[k] += lpA * (v - damp[k]);
                damp[k] = flushDenormal (damp[k]);
                line[k].push (damp[k]);
            }
            const float outL = (tap[0] + tap[2] + tap[4] + tap[6]) * 0.35f, outR = (tap[1] + tap[3] + tap[5] + tap[7]) * 0.35f;
            const float m = 0.5f * (outL + outR), s = 0.5f * (outL - outR) * p.width;
            const float wl = m + s, wr = m - s;
            l[i] = mixLaw (l[i], wl, p.mix);
            r[i] = mixLaw (r[i], wr, p.mix);
        }
    }

private:
    static constexpr float kInjection[kLines] = { 0.6f, -0.5f, 0.45f, -0.4f, 0.55f, -0.35f, 0.4f, -0.5f };
    static void hadamard8 (const float* in, float* out) noexcept
    {
        float a[8];
        for (int i = 0; i < 8; i += 2) { a[i] = in[i] + in[i + 1]; a[i + 1] = in[i] - in[i + 1]; }
        float b[8];
        for (int i = 0; i < 8; i += 4) { b[i] = a[i] + a[i + 2]; b[i + 1] = a[i + 1] + a[i + 3]; b[i + 2] = a[i] - a[i + 2]; b[i + 3] = a[i + 1] - a[i + 3]; }
        for (int i = 0; i < 4; ++i) { out[i] = (b[i] + b[i + 4]) * 0.35355339f; out[i + 4] = (b[i] - b[i + 4]) * 0.35355339f; }
    }
    Params p;
    double sampleRate = 48000.0;
    DelayLine line[kLines], pre[2], ap[2][4];
    float damp[kLines] = {};
    float lpIn[1] = {};
    Svf hpIn[1];
    float phase = 0.0f;
};
} // namespace own
} // namespace synthyboy
