// SynthyBoy - own engine: spectral morph of a wavetable frame.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include "morph.h"

#include <cmath>

#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
namespace
{
constexpr int kN = Wavetable::kCycle;
constexpr int kH = Wavetable::kHarmonics; // 1024

struct Twiddles
{
    float cs[kN / 2], sn[kN / 2];
    uint16_t rev[kN];
    float rnd[16][kH + 1]; // the fixed table of the Random Amplitudes stage
    Twiddles()
    {
        for (int i = 0; i < kN / 2; ++i)
        {
            cs[i] = static_cast<float> (std::cos (kTwoPi * i / kN));
            sn[i] = static_cast<float> (std::sin (kTwoPi * i / kN));
        }
        for (int i = 0; i < kN; ++i)
        {
            int r = 0;
            for (int b = 0; b < 11; ++b)
                r |= ((i >> b) & 1) << (10 - b);
            rev[i] = static_cast<uint16_t> (r);
        }
        Rng rng;
        rng.seed (0x5EEDu);
        for (auto& row : rnd)
            for (auto& x : row)
                x = rng.bi();
    }
};

const Twiddles& tw()
{
    static const Twiddles t;
    return t;
}

/** Unscaled inverse transform (positive exponent) of n complex points (2048, or 1024 for the half size transform of the real signal), in place. */
void inverseFft (float* re, float* im, int n = kN) noexcept
{
    const Twiddles& t = tw();
    const int shift = n == kN ? 0 : 1; // the 10 bit reversal of i < 1024 is the 11 bit one shifted down
    for (int i = 0; i < n; ++i)
    {
        const int j = t.rev[i] >> shift;
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1, step = kN / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; ++k)
            {
                const float wr = t.cs[k * step], wi = t.sn[k * step];
                const int a = i + k, b = a + half;
                const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
    }
}
} // namespace

void buildMorphCycle (const Wavetable& wt, float framePos, bool interpolate, int type, float m, int maxHarmonic, float phaseInc, double hostRate, float* out,
                      MorphScratch& sc, bool fast) noexcept
{
    const int frames = wt.numFrames();
    const int f0 = std::clamp (static_cast<int> (framePos), 0, std::max (frames - 1, 0));
    const float u = interpolate && frames > 1 && f0 < frames - 1 ? framePos - static_cast<float> (f0) : 0.0f;
    const float* a = wt.spectrum (f0);
    const float* b = u > 0.0f ? wt.spectrum (f0 + 1) : nullptr;
    // source coefficients w_i = (c_i, s_i) of the (interpolated) frame, harmonics 1 .. 1023, zero outside
    float* sc_c = sc.c;
    float* sc_s = sc.s;
    for (int i = 0; i < kH; ++i)
    {
        sc_c[i] = b != nullptr ? lerp (a[i], b[i], u) : a[i];
        sc_s[i] = b != nullptr ? lerp (a[kH + i], b[kH + i], u) : a[kH + i];
    }
    const float dc = sc_c[0];
    sc_c[0] = sc_s[0] = 0.0f;
    sc_c[kH] = sc_s[kH] = sc_c[kH + 1] = sc_s[kH + 1] = 0.0f;
    const int K = std::clamp (maxHarmonic, 0, kH - 1);
    m = clamp01 (m);
    std::fill (sc.re, sc.re + kN, 0.0f);
    std::fill (sc.im, sc.im + kN, 0.0f);
    auto put = [&] (int i, float c, float s) noexcept
    {
        if (i < 1 || i > K || i >= kH)
            return;
        // bin i of the inverse transform = (c - j s) / 2, and its mirror
        sc.re[i] += 0.5f * c;
        sc.im[i] -= 0.5f * s;
        sc.re[kN - i] += 0.5f * c;
        sc.im[kN - i] += 0.5f * s;
    };
    auto src = [&] (int i, float& c, float& s) noexcept
    {
        if (i < 1 || i >= kH) { c = s = 0.0f; return; }
        c = sc_c[i];
        s = sc_s[i];
    };
    auto scaleRead = [&] (float scale) noexcept // Vocode / Formant Scale: harmonic i reads the source at x = i * scale
    {
        const int top = static_cast<int> (std::min (static_cast<float> (K), static_cast<float> (kH - 1) / std::max (scale, 1.0e-6f)));
        for (int i = 1; i <= top; ++i)
        {
            const float x = std::max (1.0f, static_cast<float> (i) * scale);
            int i0 = static_cast<int> (x);
            if (((i0 ^ i) & 1) != 0)
                --i0;
            if (i0 < 1)
                i0 = (i & 1) ? 1 : 2;
            const float t = std::clamp ((x - static_cast<float> (i0)) * 0.5f, 0.0f, 1.0f);
            float c0, s0, c1, s1;
            src (i0, c0, s0);
            src (i0 + 2, c1, s1);
            put (i, scale * lerp (c0, c1, t), scale * lerp (s0, s1, t));
        }
    };
    switch (type)
    {
        case kMorphVocode:
        {
            const float s0 = std::exp2 (1.0f - 2.0f * m);
            const float s = s0 * static_cast<float> (kN) * phaseInc * static_cast<float> (hostRate / 44100.0);
            scaleRead (std::max (s, 1.0e-3f));
            break;
        }
        case kMorphFormantScale:
            scaleRead (std::exp2 (2.0f - 4.0f * m));
            break;
        case kMorphHarmonicStretch:
        case kMorphInharmonicStretch:
        {
            const bool inh = type == kMorphInharmonicStretch;
            const float s = inh ? std::exp2 (24.0f * m - 12.0f) : std::exp2 (8.0f * m - 4.0f);
            for (int i = 1; i < kH; ++i)
            {
                const float shift = inh ? std::pow (s, std::log2 (static_cast<float> (std::max (i, 1))) * 0.1f) : s;
                const float x = std::max (1.0f, shift * static_cast<float> (i - 1) + 1.0f);
                if (x >= static_cast<float> (K))
                    break;
                const int lo = static_cast<int> (x);
                const float t = x - static_cast<float> (lo);
                float c, sn;
                src (i, c, sn);
                put (lo, (1.0f - t) * c, (1.0f - t) * sn);
                put (lo + 1, t * c, t * sn);
            }
            break;
        }
        case kMorphSmear:
        {
            const float mp = 1.0f - (1.0f - m) * (1.0f - m) * (1.0f - m);
            float acc = 0.0f;
            for (int i = 1; i <= K; ++i)
            {
                float c, s;
                src (i, c, s);
                const float amp = std::sqrt (c * c + s * s);
                acc = amp + (acc - amp) * mp;
                const float g = amp > 1.0e-12f ? acc / amp : 0.0f;
                put (i, c * g, s * g);
                acc *= (static_cast<float> (i) + 0.25f) / static_cast<float> (i);
            }
            break;
        }
        case kMorphRandomAmplitudes:
        {
            const float q = 15.0f * m;
            const int j = std::min (static_cast<int> (q), 14);
            const float t = q - static_cast<float> (j);
            const Twiddles& T = tw();
            for (int i = 1; i <= K; ++i)
            {
                const float g0 = (1.0f + q) * std::max(1.0f - q - q * T.rnd[j][i], 0.0f);
                const float g1 = (1.0f + q) * std::max(1.0f - q - q * T.rnd[j + 1][i], 0.0f);
                const float g = lerp (g0, g1, t);
                float c, s;
                src (i, c, s);
                const float amp = std::sqrt (c * c + s * s);
                const float lim = amp > 1.0e-12f ? std::min (g * amp, 1024.0f) / amp : 0.0f;
                put (i, c * lim, s * lim);
            }
            break;
        }
        case kMorphLowPass:
        case kMorphHighPass:
        {
            const bool hp = type == kMorphHighPass;
            const float cut = std::exp2 (10.0f * m) * (hp ? 1025.0f / 1024.0f : 1.0f) + (hp ? 0.0f : 1.0f);
            const int fl = static_cast<int> (cut);
            const float fr = cut - static_cast<float> (fl);
            for (int i = 1; i <= K; ++i)
            {
                float g = i < fl ? 1.0f : (i == fl ? fr : 0.0f);
                if (hp)
                    g = 1.0f - g;
                float c, s;
                src (i, c, s);
                put (i, c * g, s * g);
            }
            break;
        }
        case kMorphPhaseDisperse:
        {
            const float p = -(2.0f * m - 1.0f) * 0.05f;
            for (int i = 1; i <= K; ++i)
            {
                const float th = p * (static_cast<float> ((i - 24) * (i - 24)) - 529.0f);
                const float ct = std::cos (th), st = std::sin (th);
                float c, s;
                src (i, c, s);
                put (i, c * ct - s * st, s * ct + c * st);
            }
            break;
        }
        case kMorphShepard:
        {
            const float mm = std::fmod (0.99f * m, 1.0f) / 0.99f;
            for (int i = 1; i <= K; ++i)
            {
                float c, s;
                src (i, c, s);
                if ((i & 1) == 0)
                {
                    float c2, s2;
                    src (i / 2, c2, s2);
                    put (i, lerp (c, c2, mm), lerp (s, s2, mm));
                }
                else
                    put (i, (1.0f - mm) * c, (1.0f - mm) * s);
            }
            break;
        }
        case kMorphTimeSkew:
        {
            const float mp = 16.0f * m * m;
            const float pos = framePos;
            for (int i = 1; i <= K; ++i)
            {
                if (frames < 2)
                {
                    put (i, sc_c[i], sc_s[i]);
                    continue;
                }
                float x = pos / 256.0f + mp * std::log2 (static_cast<float> (i)) / 11.0f;
                x = x * 0.5f;
                x -= std::floor (x);
                const float tri = 1.0f - std::fabs (1.0f - 2.0f * x);
                const float fp = std::clamp (tri * 256.0f, 0.0f, static_cast<float> (frames - 1));
                const int lo = std::min (static_cast<int> (fp), frames - 2);
                const float t = fp - static_cast<float> (lo);
                const float* p0 = wt.spectrum (lo);
                const float* p1 = wt.spectrum (lo + 1);
                put (i, lerp (p0[i], p1[i], t), lerp (p0[kH + i], p1[kH + i], t));
            }
            break;
        }
        default:
            for (int i = 1; i <= K; ++i)
                put (i, sc_c[i], sc_s[i]);
            break;
    }
    sc.re[0] = dc; // the mean of the frame is kept by every morph type
    float* dst = out;
    if (fast)
    {
        // the signal is real: x[2m] + j x[2m + 1] is the transform of half the size of Z[k] = E[k] + j O[k], with E[k] = X[k] + X[k + M] and
        // O[k] = (X[k] - X[k + M]) e^{j 2 pi k / N}, M = N / 2
        constexpr int kM = kN / 2;
        const Twiddles& t = tw();
        float zr[kM], zi[kM];
        for (int k = 0; k < kM; ++k)
        {
            const float er = sc.re[k] + sc.re[k + kM], ei = sc.im[k] + sc.im[k + kM];
            const float dr = sc.re[k] - sc.re[k + kM], di = sc.im[k] - sc.im[k + kM];
            const float orr = dr * t.cs[k] - di * t.sn[k], oi = dr * t.sn[k] + di * t.cs[k];
            zr[k] = er - oi; // E + j O
            zi[k] = ei + orr;
        }
        inverseFft (zr, zi, kM);
        for (int m = 0; m < kM; ++m)
        {
            dst[2 * m] = zr[m];
            dst[2 * m + 1] = zi[m];
        }
    }
    else
    {
        inverseFft (sc.re, sc.im);
        for (int i = 0; i < kN; ++i)
            dst[i] = sc.re[i];
    }
    dst[-1] = dst[kN - 1];
    dst[kN] = dst[0];
    dst[kN + 1] = dst[1];
    dst[kN + 2] = dst[2];
}
} // namespace own
} // namespace synthyboy
