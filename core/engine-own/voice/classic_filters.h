// SynthyBoy - own engine: the eight classic filter models (Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser), written from
// docs/engine-spec/05-filters.md and its measured responses (original code). Models 8 and up are the filters-serum collection.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "../dsp/util.h"
#include "../fx/fx_dsp.h"

#ifndef SB_DIODE_CORNER
#define SB_DIODE_CORNER 0.765f
#endif
#ifndef SB_DIODE_GAIN
#define SB_DIODE_GAIN 9.8f
#endif
#ifndef SB_DIODE_FB
#define SB_DIODE_FB 0.28f
#endif

namespace synthyboy
{
namespace own
{
struct ClassicParams
{
    int model = 3, style = 0;
    float cutoffHz = 1000.0f, res = 0.2f, driveDb = 0.0f, blendRaw = 0.0f; // blendRaw 0 .. 2
    float combOffset = 42.0f, fx = 0.5f, fy = 0.5f, fres = 0.85f, fspread = 0.0f, ftranspose = 0.0f;
};

/** One channel of a classic model. process () takes the block's start and end parameters and interpolates the coefficients every 8 samples. */
class ClassicFilter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        comb.prepare (static_cast<int> (sampleRate * 0.1));
        reset();
    }

    void reset() noexcept
    {
        s[0] = s[1] = s[2] = s[3] = s[4] = s[5] = 0.0f;
        for (auto& x : dv) x = 0.0f;
        dHp[0] = dHp[1] = dFb = dU = dPre = tilt = 0.0f;
        for (auto& f : fm) f.reset();
        comb.reset();
        for (auto& z : ap) z = 0.0f;
        fbState = 0.0f;
        lpA = lpB = 0.0f;
    }

    /** The coefficients change on the absolute 8 sample grid of the control period (t0 = position in the period), so the result does not depend on how the
        host cuts the blocks. */
    void process (const float* in, float* out, int n, const ClassicParams& a, const ClassicParams& b, int t0) noexcept
    {
        for (int i = 0; i < n;)
        {
            const int pos = t0 + i;
            const int m = std::min (8 - (pos % 8), n - i);
            const float t = static_cast<float> ((pos / 8) * 8 + 8) / static_cast<float> (kControlPeriod);
            ClassicParams p = b;
            p.cutoffHz = a.cutoffHz * std::pow (b.cutoffHz / std::max (a.cutoffHz, 1.0f), t);
            p.res = lerp (a.res, b.res, t);
            p.driveDb = lerp (a.driveDb, b.driveDb, t);
            p.blendRaw = lerp (a.blendRaw, b.blendRaw, t);
            block (in + i, out + i, m, p);
            i += m;
        }
    }

private:
    static float sat (float v) noexcept { return v - 0.9f * v * v * v / (v * v + 3.0f); }
    static float hardtanh (float v) noexcept
    {
        const float a = std::fabs (v);
        return a <= 0.66f ? v : std::copysign (0.66f + 0.34f * std::tanh ((a - 0.66f) / 0.34f), v);
    }
    void block (const float* in, float* out, int n, const ClassicParams& p) noexcept
    {
        const float f = std::clamp (p.cutoffHz, 8.0f, static_cast<float> (sr) * 0.45f);
        const float d = std::pow (10.0f, p.driveDb * 0.05f);
        const float r = clamp01 (p.res);
        const float braw = std::clamp (p.blendRaw, 0.0f, 2.0f), bb = std::clamp (braw - 1.0f, -1.0f, 1.0f);
        const float lpAmt = bb < 0.0f ? -bb : 0.0f, hpAmt = bb > 0.0f ? bb : 0.0f, bpAmt = std::sqrt(std::max(0.0f, 1.0f - bb * bb));
        switch (p.model)
        {
            case 3: satLoop = false; svfModel (in, out, n, f, 0.5f + 15.5f * r * r * r, 1.0f, d / (2.0f * r * r * r + 1.0f), 1.0f / std::sqrt (d), p.style, lpAmt, bpAmt, hpAmt, true); break;
            case 0: satLoop = false; svfModel (in, out, n, f * kAn[0], kAn[1] + kAn[2] * std::pow (2.0f * r, 5.1f), kAn[4], d / (2.0f * r + 1.0f), 1.0f / std::sqrt (d) * (p.style == 1 ? kAn[5] : kAn[3]), p.style, lpAmt, bpAmt, hpAmt, false); break;
            case 1: satLoop = true; svfModel (in, out, n, f, 0.556f + 0.634f * std::pow (2.0f * r, 6.5f), 1.1f, (2.0f * (d - 1.0f) + 1.0f) / (r + 1.0f), 1.0f / std::sqrt (d) * 1.05f, p.style, lpAmt, bpAmt, hpAmt, false); break;
            case 2: ladder (in, out, n, f, r, d, p.style, 1.0f, lpAmt, bpAmt, hpAmt); break;
            case 4: diode (in, out, n, f, r, d, p); break;
            case 5: formant (in, out, n, p); break;
            case 6: combFilter (in, out, n, f, r, p); break;
            case 7: phaserFilter (in, out, n, f, r, d, p); break;
            default: std::copy (in, in + n, out); break;
        }
    }

    // 2 pole (12 dB) or two cascaded (24 dB) state variable filters with an optional saturation in the loop
    void svfModel (const float* in, float* out, int n, float f, float Q, float Q1, float pre, float post, int style, float lpAmt, float bpAmt, float hpAmt, bool linearHard) noexcept
    {
        const float g = std::tan (kPif * std::min (f / static_cast<float> (sr), 0.499f));
        const float R = 1.0f / std::max (Q, 0.3f), R1 = 1.0f / Q1;
        for (int i = 0; i < n; ++i)
        {
            float u = in[i] * pre;
            if (style == 1)
            {
                // 24 dB: a first section with fixed damping, limited, then the resonant one
                float lp, bp, hp;
                tpt (0, u, g, R1, lp, bp, hp, false);
                u = linearHard ? hardtanh (lp) : std::tanh (lp);
            }
            float lp, bp, hp;
            tpt (style == 1 ? 3 : 0, u, g, R, lp, bp, hp, satLoop);
            float y;
            switch (style)
            {
                case 2: y = lerp (lp + hp, bp * R, clamp01 (bpAmt * 0.0f + (1.0f - lpAmt - hpAmt))); break; // notch <-> pass swap
                case 3: y = lp + hp * hpAmt - lp * hpAmt; break;
                case 4: y = lp + hp - bp * R * bpAmt; break;
                case 5: y = hp + R * bp + lp; break; // the shelving mode at 0 dB (document 05, 2 and 06, 5: A = 1 makes it the identity)
                default: y = lpAmt * lp + bpAmt * bp + hpAmt * hp; break;
            }
            y = linearHard ? hardtanh (y) : y;
            out[i] = y * post;
        }
    }
    bool satLoop = false;
    void tpt (int k, float u, float g, float R, float& lp, float& bp, float& hp, bool saturate) noexcept
    {
        float& ic1 = s[k];
        float& ic2 = s[k + 1];
        const float v3 = u - ic2;
        const float v1 = (ic1 + g * v3) / (1.0f + g * R + g * g);
        const float v2 = ic2 + g * v1;
        ic1 = flushDenormal (2.0f * (saturate ? std::tanh (v1 * 0.5f) * 2.0f : v1) - ic1);
        ic2 = flushDenormal (2.0f * v2 - ic2);
        lp = v2;
        bp = v1;
        hp = u - R * v1 - v2;
    }

    // four saturating one pole stages with global feedback (zero delay feedback solved linearly)
    void ladder (const float* in, float* out, int n, float f, float r, float d, int style, float, float lpAmt, float bpAmt, float hpAmt) noexcept
    {
        const float g = std::tan (kPif * std::min (f / static_cast<float> (sr), 0.499f)), G = g / (1.0f + g);
        const float k = 0.001f + 4.099f * (style == 0 ? r : std::sin (kPif * 0.5f * r));
        const float pre = d * (1.0f + r), post = 1.0f / std::sqrt ((d - 1.0f) * 0.25f + 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float S = G * G * G * s[0] + G * G * s[1] + G * s[2] + s[3];
            const float u = std::tanh ((pre * in[i] - k * S) / (1.0f + k * G * G * G * G) * 0.9f) * 1.1f;
            float x = u, st[4];
            for (int j = 0; j < 4; ++j)
            {
                const float v = (x - s[j]) * G;
                const float y = v + s[j];
                s[j] = flushDenormal (y + v);
                st[j] = y;
                x = sat (y);
            }
            const float lp12 = st[1], lp24 = st[3];
            const float bp = st[0] - st[1], hp = u - 2.0f * st[0] + st[1];
            float y = style == 1 ? lp24 : lp12;
            if (lpAmt < 0.999f)
                y = lpAmt * y + bpAmt * bp + hpAmt * hp;
            out[i] = y * post;
        }
    }

    /** The diode ladder (document 05, 6): a two pole high pass in front (cutoff by the blend), four stages that average with their neighbours, a feedback of the
        fourth stage through a fixed 20 Hz high pass, the input stage tanh (0.5 (d' hp - k s4)). The stage dynamics are solved with the trapezoidal rule as a tridiagonal
        system (zero delay feedback inside the ladder); the saturators act on the old states. The corner is on the scale of the measured responses (kDiodeCorner). */
    static inline float kDiodeCorner = SB_DIODE_CORNER, kDiodeGain = SB_DIODE_GAIN, kDiodePre = 100.0f, kDiodeFb = SB_DIODE_FB, kDiodeW = 1.0f;
    void diode (const float* in, float* out, int n, float f, float r, float d, const ClassicParams& p) noexcept
    {
        const float k = 0.7f + 16.3f * r * r * r;
        const float dIn = (0.5f * k + 1.0f) * d, post = 1.0f / std::sqrt (d);
        const float fc = std::clamp (f * kDiodeCorner, 5.0f, static_cast<float> (sr) * 0.45f);
        const float g = std::tan (kPif * std::min (fc / static_cast<float> (sr), 0.499f));
        const float braw = clamp01 (p.blendRaw * 0.5f); // 0 .. 1
        // the high pass in front: 24 dB style: the corner moves from cutoff 2^-9 to 2^-1 with the blend; 12 dB style: fixed 2^-1, the amount is blend^2 / 4
        const bool style12 = p.style == 0;
        const float hpHz = f * std::exp2 (style12 ? -1.0f : -9.0f + 8.0f * braw);
        const float amount = style12 ? braw * braw * 0.25f : 1.0f;
        const float gh = std::tan (kPif * std::clamp (hpHz, 1.0f, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr));
        const float gf = std::tan (kPif * 20.0f / static_cast<float> (sr));
        const float gp = std::tan (kPif * std::clamp (f * kDiodePre, 5.0f, static_cast<float> (sr) * 0.45f) / static_cast<float> (sr));
        const float inv = 1.0f / (1.0f + g);
        // tridiagonal matrix (constant per block): stage i takes w/2 of each neighbour and (1 - w) of the previous stage; w = 1 averages the neighbours
        const float w = kDiodeW, up = 0.5f * w, lo = 1.0f - 0.5f * w;
        const float bb = 1.0f + g;
        float cp[4];
        cp[0] = -g * up / bb;
        for (int j = 1; j < 4; ++j)
            cp[j] = (j < 3 ? -g * up : 0.0f) / (bb + g * lo * cp[j - 1]);
        (void) inv;
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            // two pole high pass (two cascaded one pole trapezoidal sections)
            float h = x;
            for (int j = 0; j < 2; ++j)
            {
                const float v = (h - dHp[j]) * gh / (1.0f + gh);
                const float lp = v + dHp[j];
                dHp[j] = flushDenormal (lp + v);
                h = h - lp;
            }
            const float xin = lerp (x, h, amount);
            // 20 Hz high pass of the fed back fourth stage
            const float vf = (dv[3] - dFb) * gf / (1.0f + gf);
            const float lpf = vf + dFb;
            dFb = flushDenormal (lpf + vf);
            const float fb = dv[3] - lpf;
            float xp = xin;
            if (kDiodePre < 50.0f) // optional extra pole in front (calibration of the measured response)
            {
                dPre += gp / (1.0f + gp) * (xin - dPre);
                xp = dPre;
            }
            const float u = std::tanh (0.5f * (dIn * xp - k * kDiodeFb * fb)) * kDiodeGain;
            // right hand side with the old states (the neighbours through the algebraic saturator)
            const float s0 = sat (dv[0]), s1 = sat (dv[1]), s2 = sat (dv[2]), s3 = sat (dv[3]);
            const float in0 = up * (dU + s1) + (1.0f - w) * dU, in1 = up * (s0 + s2) + (1.0f - w) * s0, in2 = up * (s1 + s3) + (1.0f - w) * s1,
                        in3 = up * 0.0f + lo * s2;
            float rhs[4] = { (1.0f - g) * dv[0] + g * in0 + g * lo * u, (1.0f - g) * dv[1] + g * in1, (1.0f - g) * dv[2] + g * in2, (1.0f - g) * dv[3] + g * in3 };
            dU = u;
            float dp[4];
            dp[0] = rhs[0] / bb;
            for (int j = 1; j < 4; ++j)
                dp[j] = (rhs[j] + g * lo * dp[j - 1]) / (bb + g * lo * cp[j - 1]);
            float v[4];
            v[3] = dp[3];
            for (int j = 2; j >= 0; --j)
                v[j] = dp[j] - cp[j] * v[j + 1];
            for (int j = 0; j < 4; ++j)
                dv[j] = flushDenormal (v[j]);
            out[i] = dv[3] * post;
        }
    }
    float dv[4] = {}, dHp[2] = {}, dFb = 0.0f, dU = 0.0f, dPre = 0.0f, tilt = 0.0f;

    void formant (const float* in, float* out, int n, const ClassicParams& p) noexcept
    {
        // vowels: gain dB, resonance share, centre note per formant (document 05, section 9)
        static const float kV[5][4][3] = {
            { { -2, 0.66f, 75.755f }, { -8, 0.75f, 84.545f }, { -9, 1.0f, 100.085f }, { -10, 1.0f, 101.646f } },   // A
            { { 0, 0.66f, 67.350f }, { -14, 0.75f, 92.400f }, { -4, 1.0f, 99.755f }, { -14, 1.0f, 103.350f } },    // E
            { { 0, 0.80f, 61.783f }, { -15, 0.75f, 94.050f }, { -17, 1.0f, 101.038f }, { -20, 1.0f, 103.618f } },  // I
            { { -2, 0.70f, 67.350f }, { -6, 0.75f, 79.350f }, { -14, 1.0f, 99.755f }, { -14, 1.0f, 101.038f } },   // O
            { { 0, 0.70f, 65.038f }, { -20, 0.75f, 74.370f }, { -17, 1.0f, 100.409f }, { -14, 1.0f, 101.646f } } };// U
        static const int kStyle0[4] = { 0, 2, 4, 3 }, kStyle1[4] = { 0, 3, 2, 1 }, kStyle2[4] = { 1, 4, 3, 0 }; // corners (0,0) (1,0) (0,1) (1,1): A I U O / A O I E / E U O A
        const int* c = p.style == 0 ? kStyle0 : (p.style == 1 ? kStyle1 : kStyle2);
        const float tract = p.style >= 2 ? 1.25f : 1.0f; // the vocal tract style: the same four resonators, narrower, with a gentle spectral tilt (the glottal source)
        float gain[4], note[4], q[4];
        for (int k = 0; k < 4; ++k)
        {
            auto mixv = [&] (int comp)
            {
                const float b0 = lerp (kV[c[0]][k][comp], kV[c[1]][k][comp], p.fx), b1 = lerp (kV[c[2]][k][comp], kV[c[3]][k][comp], p.fx);
                return lerp (b0, b1, p.fy);
            };
            gain[k] = std::pow (10.0f, mixv (0) * 0.05f);
            float nt = mixv (2) + p.ftranspose;
            nt = lerp (nt, 80.0f, std::clamp (p.fspread, -1.0f, 1.0f));
            note[k] = nt;
            const float rr = clamp01 (p.fres * mixv (1));
            q[k] = (4.0f + 26.0f * rr * rr * rr) * tract;
        }
        for (int i = 0; i < n; ++i)
        {
            float y = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                const float hz = 8.1757989f * std::exp2 (note[k] / 12.0f);
                fm[k].process (in[i], Svf::coefficient (hz, sr), 1.0f / q[k]);
                y += fm[k].bp * gain[k] * (1.0f / q[k]) * 8.0f;
            }
            if (p.style >= 2)
            {
                tilt += 0.35f * (y - tilt); // a one pole low pass mixed in: -6 dB per octave above 3 kHz like a glottal source
                y = 0.6f * y + 0.4f * tilt;
            }
            out[i] = y;
        }
    }

    void combFilter (const float* in, float* out, int n, float f, float r, const ClassicParams& p) noexcept
    {
        const float T = std::clamp (static_cast<float> (sr) / std::max (f, 20.0f), 2.0f, static_cast<float> (sr) * 0.09f);
        float fb = lerp (-1.0f, 1.0f, r);
        fb = fb / std::sqrt (std::fabs (fb) + 1.0e-5f);
        fb = std::clamp (fb, -0.98f, 0.98f);
        const float scale = 1.0f - 0.5f * fb * fb;
        const float loopHz = std::clamp (f * std::exp2 (p.combOffset / 12.0f), 20.0f, static_cast<float> (sr) * 0.45f);
        const float a = OnePole::coefficient (loopHz, sr);
        for (int i = 0; i < n; ++i)
        {
            const float v = scale * in[i] + fb * comb.read (T - 1.0f);
            lpA += a * (v - lpA);
            lpA = flushDenormal (lpA);
            const float w = hardtanh (lpA);
            comb.push (w);
            out[i] = w;
        }
    }

    void phaserFilter (const float* in, float* out, int n, float f, float r, float d, const ClassicParams& p) noexcept
    {
        const float g = phaserStageG (f, sr);
        const float a = g / (1.0f + g);
        const float b = clamp01 (p.blendRaw);
        const float braw = std::clamp (p.blendRaw, 0.0f, 2.0f);
        const float p1 = clamp01 (1.0f - braw), p5 = clamp01 (braw - 1.0f), p3 = 1.0f - p1 - p5;
        const float inv = p.style == 1 ? -1.0f : 1.0f;
        (void) b;
        const float c1 = OnePole::coefficient (f / 20.0f, sr), c2 = OnePole::coefficient (f * 20.0f, sr);
        for (int i = 0; i < n; ++i)
        {
            float x = hardtanh (d * (1.0f + 0.5f * r) * in[i] + inv * fbState);
            float tap[3] = { 0, 0, 0 };
            for (int st = 0; st < 12; ++st)
            {
                const float v = (x - ap[st]) * a;
                const float lp = v + ap[st];
                ap[st] = flushDenormal (lp + v);
                x = x - 2.0f * lp;
                if (st == 3) tap[0] = x;
                if (st == 7) tap[1] = x;
                if (st == 11) tap[2] = x;
            }
            const float outAp = p1 * tap[0] + p3 * tap[1] + p5 * tap[2];
            lpA += c2 * (outAp - lpA);
            lpB += c1 * (lpA - lpB);
            fbState = flushDenormal (r * (lpA - lpB));
            out[i] = 0.5f * (in[i] + inv * outAp);
        }
    }

    double sr = 48000.0;
    static inline float kAn[6] = { 0.992f, 0.462f, 1.088f, 0.915f, 1.249f, 0.97f };
    float s[6] = {};
    Svf fm[4];
    DelayLine comb;
    float ap[12] = {};
    float fbState = 0.0f, lpA = 0.0f, lpB = 0.0f;
};
} // namespace own
} // namespace synthyboy
