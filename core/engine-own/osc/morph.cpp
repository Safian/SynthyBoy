// SynthyBoy - own engine: spectral morph of a wavetable frame.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include "morph.h"

#include <cmath>

#include "../dsp/simd.h"
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
    float stageC[kN], stageS[kN]; // the twiddles of each stage side by side: stage of length len (half = len / 2) at offset half - 1, the very values cs[k * step], sn[k * step] of the strided read
    float rnd[16][kH + 1]; // the fixed table of the Random Amplitudes stage
    float lg[kH];          // std::log2 (float (i)) of every harmonic i (Time Skew, Inharmonic Stretch): the very values of the per harmonic call, made once
    // the pairs (i, j) with i < j = rev[i] of the bit reversal, for the transform of 2048 points and of 1024 points (j = rev[i] >> 1): only these elements change places
    uint16_t swaps[2][kN];
    int swapCount[2] = { 0, 0 };
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
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < (s == 0 ? kN : kN / 2); ++i)
            {
                const int j = rev[i] >> s;
                if (i < j)
                {
                    swaps[s][swapCount[s]++] = static_cast<uint16_t> (i);
                    swaps[s][swapCount[s]++] = static_cast<uint16_t> (j);
                }
            }
        for (int len = 2; len <= kN; len <<= 1)
            for (int k = 0; k < len / 2; ++k)
            {
                stageC[len / 2 - 1 + k] = cs[k * (kN / len)];
                stageS[len / 2 - 1 + k] = sn[k * (kN / len)];
            }
        lg[0] = 0.0f;
        for (int i = 1; i < kH; ++i)
            lg[i] = std::log2 (static_cast<float> (i));
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
void inverseFft (float* re, float* im, int n = kN, bool scalarOnly = false, bool reference = false) noexcept
{
    const Twiddles& t = tw();
    const int shift = n == kN ? 0 : 1; // the 10 bit reversal of i < 1024 is the 11 bit one shifted down
    if (reference)
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = t.rev[i] >> shift;
            if (i < j)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }
    }
    else
    {
        // the same exchanges, from the list of the pairs that move (disjoint pairs: the order does not matter)
        const uint16_t* sw = t.swaps[shift];
        for (int q = 0, cnt = t.swapCount[shift]; q < cnt; q += 2)
        {
            std::swap (re[sw[q]], re[sw[q + 1]]);
            std::swap (im[sw[q]], im[sw[q + 1]]);
        }
    }
    int firstLen = 2;
#if SB_NEON
    if (! scalarOnly && ! reference)
    {
        // the stages of length 2 and 4 together, eight points at a time, in registers: the butterflies of the scalar loops below with the same twiddle values (stageC / stageS at
        // offset 0 and 1 .. 2) and the same operations in the same order for every output
        const float32x4_t w1r = vdupq_n_f32 (t.stageC[0]), w1i = vdupq_n_f32 (t.stageS[0]);
        const float32x2_t w2r2 = vld1_f32 (t.stageC + 1), w2i2 = vld1_f32 (t.stageS + 1);
        const float32x4_t w2r = vcombine_f32 (w2r2, w2r2), w2i = vcombine_f32 (w2i2, w2i2);
        for (int i = 0; i < n; i += 8)
        {
            // stage of length 2: the pairs (2m, 2m + 1): a = even points, b = odd points
            const float32x4x2_t r = vld2q_f32 (re + i), m = vld2q_f32 (im + i);
            const float32x4_t xr1 = vsubq_f32 (vmulq_f32 (r.val[1], w1r), vmulq_f32 (m.val[1], w1i)), xi1 = vaddq_f32 (vmulq_f32 (r.val[1], w1i), vmulq_f32 (m.val[1], w1r));
            const float32x4x2_t zr = vzipq_f32 (vaddq_f32 (r.val[0], xr1), vsubq_f32 (r.val[0], xr1)); // points 0 .. 3 and 4 .. 7
            const float32x4x2_t zi = vzipq_f32 (vaddq_f32 (m.val[0], xi1), vsubq_f32 (m.val[0], xi1));
            // stage of length 4: in each group of four, a = points 0, 1 and b = points 2, 3
            const float32x4_t ar = vcombine_f32 (vget_low_f32 (zr.val[0]), vget_low_f32 (zr.val[1])), br = vcombine_f32 (vget_high_f32 (zr.val[0]), vget_high_f32 (zr.val[1]));
            const float32x4_t ai = vcombine_f32 (vget_low_f32 (zi.val[0]), vget_low_f32 (zi.val[1])), bi = vcombine_f32 (vget_high_f32 (zi.val[0]), vget_high_f32 (zi.val[1]));
            const float32x4_t xr2 = vsubq_f32 (vmulq_f32 (br, w2r), vmulq_f32 (bi, w2i)), xi2 = vaddq_f32 (vmulq_f32 (br, w2i), vmulq_f32 (bi, w2r));
            const float32x4_t nar = vaddq_f32 (ar, xr2), nbr = vsubq_f32 (ar, xr2), nai = vaddq_f32 (ai, xi2), nbi = vsubq_f32 (ai, xi2);
            vst1q_f32 (re + i, vcombine_f32 (vget_low_f32 (nar), vget_low_f32 (nbr)));
            vst1q_f32 (re + i + 4, vcombine_f32 (vget_high_f32 (nar), vget_high_f32 (nbr)));
            vst1q_f32 (im + i, vcombine_f32 (vget_low_f32 (nai), vget_low_f32 (nbi)));
            vst1q_f32 (im + i + 4, vcombine_f32 (vget_high_f32 (nai), vget_high_f32 (nbi)));
        }
        firstLen = 8;
    }
#endif
    for (int len = firstLen; len <= n; len <<= 1)
    {
        const int half = len >> 1, step = kN / len;
        for (int i = 0; i < n; i += len)
        {
            int k = 0;
#if SB_NEON
            // four butterflies at a time with the operations of the scalar loop below in the same order (the twiddles are the same values, read side by side): the same bits
            if (! scalarOnly && half >= 4)
            {
                const float* wr4 = t.stageC + (half - 1);
                const float* wi4 = t.stageS + (half - 1);
                for (; k < half; k += 4)
                {
                    const int a = i + k, b = a + half;
                    const float32x4_t wr = vld1q_f32 (wr4 + k), wi = vld1q_f32 (wi4 + k);
                    const float32x4_t rb = vld1q_f32 (re + b), ib = vld1q_f32 (im + b);
                    const float32x4_t xr = vsubq_f32 (vmulq_f32 (rb, wr), vmulq_f32 (ib, wi)), xi = vaddq_f32 (vmulq_f32 (rb, wi), vmulq_f32 (ib, wr));
                    const float32x4_t ra = vld1q_f32 (re + a), ia = vld1q_f32 (im + a);
                    vst1q_f32 (re + b, vsubq_f32 (ra, xr));
                    vst1q_f32 (im + b, vsubq_f32 (ia, xi));
                    vst1q_f32 (re + a, vaddq_f32 (ra, xr));
                    vst1q_f32 (im + a, vaddq_f32 (ia, xi));
                }
            }
#else
            (void) scalarOnly;
#endif
            for (; k < half; ++k)
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
}
} // namespace

void buildMorphCycle (const Wavetable& wt, float framePos, bool interpolate, int type, float m, int maxHarmonic, float phaseInc, double hostRate, float* out,
                      MorphScratch& sc, bool fast, bool scalarTransform, bool referenceBuild) noexcept
{
    const int frames = wt.numFrames();
    const int f0 = std::clamp (static_cast<int> (framePos), 0, std::max (frames - 1, 0));
    const float u = interpolate && frames > 1 && f0 < frames - 1 ? framePos - static_cast<float> (f0) : 0.0f;
    const float* a = wt.spectrum (f0);
    const float* b = u > 0.0f ? wt.spectrum (f0 + 1) : nullptr;
    // source coefficients w_i = (c_i, s_i) of the (interpolated) frame, harmonics 1 .. 1023, zero outside (src () below). The build reads them only up to `readTop` (see below), so only
    // these are interpolated, and a frame that is not interpolated (u = 0) is read in place instead of copied; the reference build makes the whole copy as it always did.
    float* sc_c = sc.c;
    float* sc_s = sc.s;
    const int K = std::clamp (maxHarmonic, 0, kH - 1);
    m = clamp01 (m);
    // the factor of the harmonic that Vocode and Formant Scale read at (scaleRead () below), made once here
    float scaleOfType = 1.0f;
    if (type == kMorphVocode)
        scaleOfType = std::max (std::exp2 (1.0f - 2.0f * m) * static_cast<float> (kN) * phaseInc * static_cast<float> (hostRate / 44100.0), 1.0e-3f);
    else if (type == kMorphFormantScale)
        scaleOfType = std::exp2 (2.0f - 4.0f * m);
    float dc;
    const float* srcC;
    const float* srcS;
    if (referenceBuild)
    {
        for (int i = 0; i < kH; ++i)
        {
            sc_c[i] = b != nullptr ? lerp (a[i], b[i], u) : a[i];
            sc_s[i] = b != nullptr ? lerp (a[kH + i], b[kH + i], u) : a[kH + i];
        }
        dc = sc_c[0];
        sc_c[0] = sc_s[0] = 0.0f;
        sc_c[kH] = sc_s[kH] = sc_c[kH + 1] = sc_s[kH + 1] = 0.0f;
        srcC = sc_c;
        srcS = sc_s;
    }
    else if (b == nullptr)
    {
        dc = a[0];
        srcC = a;
        srcS = a + kH;
    }
    else
    {
        // the highest harmonic any type reads: K; Vocode and Formant Scale read at i * scale for i up to min (K, 1023 / scale) (and two above: the window of the linear read); Harmonic and
        // Inharmonic Stretch read the harmonics i = 1 .. 1023 until their target passes K (any i for a small stretch); Shepard reads i and i / 2.
        int readTop = K;
        if (type == kMorphVocode || type == kMorphFormantScale)
        {
            const double top = std::min (static_cast<double> (K), static_cast<double> (kH - 1) / static_cast<double> (std::max (scaleOfType, 1.0e-6f)));
            readTop = static_cast<int> (std::min (static_cast<double> (kH - 1), top * static_cast<double> (std::max (scaleOfType, 1.0f)) * 1.001 + 4.0));
        }
        else if (type == kMorphHarmonicStretch || type == kMorphInharmonicStretch)
            readTop = kH - 1;
        readTop = std::clamp (readTop, 0, kH - 1);
        dc = lerp (a[0], b[0], u);
        for (int i = 1; i <= readTop; ++i)
        {
            sc_c[i] = lerp (a[i], b[i], u);
            sc_s[i] = lerp (a[kH + i], b[kH + i], u);
        }
        srcC = sc_c;
        srcS = sc_s;
    }
    // The half size transform (fast) reads only bins 0 .. K, kM - K .. kM + K and kN - K .. kN - 1 (see the pre-twiddle below), so only these are cleared; the other
    // bins of the scratch hold stale values that nothing reads. The full clear stays for the full transform and for the reference build of the tests.
    if (fast && !referenceBuild)
    {
        constexpr int kMh = kN / 2;
        auto clear = [&] (int from, int to) noexcept // [from, to)
        {
            if (to > from)
            {
                std::fill (sc.re + from, sc.re + to, 0.0f);
                std::fill (sc.im + from, sc.im + to, 0.0f);
            }
        };
        const int K1 = std::clamp (maxHarmonic, 0, kH - 1);
        clear (0, K1 + 1);
        clear (kMh - K1, kMh + K1 + 1);
        clear (kN - K1, kN);
    }
    else
    {
        std::fill (sc.re, sc.re + kN, 0.0f);
        std::fill (sc.im, sc.im + kN, 0.0f);
    }
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
        c = srcC[i];
        s = srcS[i];
    };
    // std::log2 of the harmonic number as a float: from the table (the same call, made once at program start); the reference build calls it as it did
    auto log2Of = [&] (int i) noexcept { return referenceBuild ? std::log2 (static_cast<float> (i)) : tw().lg[i]; };
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
            scaleRead (scaleOfType); // = max (2^(1 - 2m) * N * phaseInc * hostRate / 44100, 1e-3)
            break;
        }
        case kMorphFormantScale:
            scaleRead (scaleOfType); // = 2^(2 - 4m)
            break;
        case kMorphHarmonicStretch:
        case kMorphInharmonicStretch:
        {
            const bool inh = type == kMorphInharmonicStretch;
            const float s = inh ? std::exp2 (24.0f * m - 12.0f) : std::exp2 (8.0f * m - 4.0f);
            for (int i = 1; i < kH; ++i)
            {
                const float shift = inh ? std::pow (s, log2Of (std::max (i, 1)) * 0.1f) : s;
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
            // m' = 1 at the end of the range leaves no memory of the input at all (a = A (1 - m') = 0 for ever: silence): the leak is capped at 0.999 so that the
            // top of the range is the strongest smear, not nothing (still a heavy low-pass-like smear, about -28 dB under the dry saw at 64 harmonics)
            const float mp = std::min (1.0f - (1.0f - m) * (1.0f - m) * (1.0f - m), 0.999f);
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
            auto gain = [&] (int i) noexcept
            {
                const float g0 = (1.0f + q) * std::max(1.0f - q - q * T.rnd[j][i], 0.0f);
                const float g1 = (1.0f + q) * std::max(1.0f - q - q * T.rnd[j + 1][i], 0.0f);
                return lerp (g0, g1, t);
            };
            // The deletion is random per harmonic, so on a table with few partials the level jumped from -73 dB (every partial deleted) to +6 dB from one step of the amount
            // to the next. The morph changes the spectrum, not the loudness: the energy of the harmonics 1 .. K is kept at the dry value, and if the draw deletes every
            // partial the strongest one is kept (q = 0, no deletion yet: the gains are 1 and the samples are those of the plain table).
            float scale = 1.0f;
            int keep = 0;
            if (q > 0.0f)
            {
                float eIn = 0.0f, eOut = 0.0f, best = 0.0f;
                for (int i = 1; i <= K; ++i)
                {
                    float c, s;
                    src (i, c, s);
                    const float e = c * c + s * s;
                    const float ao = std::min (gain (i) * std::sqrt (e), 1024.0f);
                    eIn += e;
                    eOut += ao * ao;
                    if (e > best)
                    {
                        best = e;
                        keep = i;
                    }
                }
                if (eOut > 1.0e-12f * eIn)
                {
                    scale = std::sqrt (eIn / eOut);
                    keep = 0;
                }
                else if (best > 0.0f)
                    scale = std::sqrt (eIn / best); // only `keep` survives, with gain 1
                else
                    keep = 0;
            }
            for (int i = 1; i <= K; ++i)
            {
                const float g = keep != 0 ? (i == keep ? 1.0f : 0.0f) : gain (i);
                float c, s;
                src (i, c, s);
                const float amp = std::sqrt (c * c + s * s);
                const float lim = amp > 1.0e-12f ? std::min (g * scale * amp, 1024.0f) / amp : 0.0f;
                put (i, c * lim, s * lim);
            }
            break;
        }
        case kMorphLowPass:
        case kMorphHighPass:
        {
            const bool hp = type == kMorphHighPass;
            float cut = std::exp2 (10.0f * m) * (hp ? 1025.0f / 1024.0f : 1.0f) + (hp ? 0.0f : 1.0f);
            // a high pass cutoff above the band limit of the voice (64 harmonics at 60 % is above the 42nd harmonic of a note above C5) removes every harmonic the note
            // has: silence. The cutoff stops at the band limit K, so the top harmonic of the voice always remains (full gain at i = K)
            if (hp)
                cut = std::min (cut, static_cast<float> (std::max (K, 1)));
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
            const float posN = framePos / 256.0f;
            for (int i = 1; i <= K; ++i)
            {
                if (frames < 2)
                {
                    put (i, srcC[i], srcS[i]);
                    continue;
                }
                float x = posN + mp * log2Of (i) / 11.0f;
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
                put (i, srcC[i], srcS[i]);
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
        auto twiddle = [&] (int k) noexcept
        {
            const float er = sc.re[k] + sc.re[k + kM], ei = sc.im[k] + sc.im[k + kM];
            const float dr = sc.re[k] - sc.re[k + kM], di = sc.im[k] - sc.im[k + kM];
            const float orr = dr * t.cs[k] - di * t.sn[k], oi = dr * t.sn[k] + di * t.cs[k];
            zr[k] = er - oi; // E + j O
            zi[k] = ei + orr;
        };
        // For K < k < kM - K both X[k] and X[k + kM] are +0, and the loop gives zr = +0 - (+-0) = +0 and zi = +0 + (+-0) = +0 (round to nearest): the same bits as writing +0.
        const int lo = referenceBuild ? kM : std::min (K + 1, kM), hi = referenceBuild ? kM : std::max (kM - K, lo);
        // four bins at a time with the operations of twiddle () in the same order (NEON; the scalar loops where it is off): the same bits
        auto twiddleRange = [&] (int from, int to) noexcept // [from, to)
        {
            int k = from;
#if SB_NEON
            if (! scalarTransform && ! referenceBuild)
                for (; k + 4 <= to; k += 4)
                {
                    const float32x4_t rk = vld1q_f32 (sc.re + k), rk2 = vld1q_f32 (sc.re + k + kM), ik = vld1q_f32 (sc.im + k), ik2 = vld1q_f32 (sc.im + k + kM);
                    const float32x4_t er = vaddq_f32 (rk, rk2), ei = vaddq_f32 (ik, ik2), dr = vsubq_f32 (rk, rk2), di = vsubq_f32 (ik, ik2);
                    const float32x4_t c = vld1q_f32 (t.cs + k), sn = vld1q_f32 (t.sn + k);
                    const float32x4_t orr = vsubq_f32 (vmulq_f32 (dr, c), vmulq_f32 (di, sn)), oi = vaddq_f32 (vmulq_f32 (dr, sn), vmulq_f32 (di, c));
                    vst1q_f32 (zr + k, vsubq_f32 (er, oi));
                    vst1q_f32 (zi + k, vaddq_f32 (ei, orr));
                }
#endif
            for (; k < to; ++k)
                twiddle (k);
        };
        twiddleRange (0, lo);
        if (hi > lo)
        {
            std::fill (zr + lo, zr + hi, 0.0f);
            std::fill (zi + lo, zi + hi, 0.0f);
        }
        twiddleRange (hi, kM);
        inverseFft (zr, zi, kM, scalarTransform, referenceBuild);
        int m = 0;
#if SB_NEON
        if (! scalarTransform && ! referenceBuild)
            for (; m + 4 <= kM; m += 4) // x[2m] = Re Z[m], x[2m + 1] = Im Z[m]: the interleaved store
            {
                float32x4x2_t zz;
                zz.val[0] = vld1q_f32 (zr + m);
                zz.val[1] = vld1q_f32 (zi + m);
                vst2q_f32 (dst + 2 * m, zz);
            }
#endif
        for (; m < kM; ++m)
        {
            dst[2 * m] = zr[m];
            dst[2 * m + 1] = zi[m];
        }
    }
    else
    {
        inverseFft (sc.re, sc.im, kN, scalarTransform, referenceBuild);
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
