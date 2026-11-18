// SynthyBoy - core/spectral: the kernels of one synthesis frame (see synthyboy/spectral/kernels.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/spectral/kernels.h"

#include <algorithm>
#include <cmath>

#if SB_SPECTRAL_NEON
 #include <arm_neon.h>
#endif

// The scalar and the vector forms must compute the same bits: no fused multiply-add in either, on any compiler / CPU (AArch64 always has it and GCC / clang contract by default).
#if defined(__clang__)
 #pragma clang fp contract(off)
#elif defined(__GNUC__)
 #pragma GCC optimize ("fp-contract=off")
#endif

namespace synthyboy
{
namespace spectral
{
namespace kernels
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kTwoPiF = 6.28318530717958647692f;
constexpr float kPiF = 3.14159265f;

float wrapPiF (float x) noexcept
{
    while (x > kPiF) x -= kTwoPiF;
    while (x < -kPiF) x += kTwoPiF;
    return x;
}
} // namespace

// ---- the inverse transform of a real signal -----------------------------------------------------------------------------------------

RealIfft::RealIfft()
{
    for (int i = 0; i < kM; ++i)
    {
        int r = 0;
        for (int b = 0; b < 10; ++b)
            r |= ((i >> b) & 1) << (9 - b);
        rev_[i] = static_cast<uint16_t> (r);
        packC_[i] = static_cast<float> (std::cos (2.0 * kPi * i / kFftSize));
        packS_[i] = static_cast<float> (std::sin (2.0 * kPi * i / kFftSize));
    }
    for (int h = 1; h < kM; h <<= 1)
        for (int k = 0; k < h; ++k)
        {
            stageC_[h - 1 + k] = static_cast<float> (std::cos (kPi * k / h));
            stageS_[h - 1 + k] = static_cast<float> (std::sin (kPi * k / h));
        }
}

const RealIfft& RealIfft::instance() noexcept
{
    static const RealIfft inst;
    return inst;
}

void RealIfft::transform (float* re, float* im, bool neon) const noexcept
{
    for (int i = 0; i < kM; ++i)
    {
        const int j = rev_[i];
        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    // the first two stages together (the twiddles are 1 and +i): blocks of four
    for (int i = 0; i < kM; i += 4)
    {
        const float x0r = re[i] + re[i + 1], x0i = im[i] + im[i + 1];
        const float x1r = re[i] - re[i + 1], x1i = im[i] - im[i + 1];
        const float x2r = re[i + 2] + re[i + 3], x2i = im[i + 2] + im[i + 3];
        const float x3r = re[i + 2] - re[i + 3], x3i = im[i + 2] - im[i + 3];
        // the second stage: (x0, x2) with 1, (x1, x3) with +i: i * (x3r + i x3i) = -x3i + i x3r
        re[i] = x0r + x2r;      im[i] = x0i + x2i;
        re[i + 2] = x0r - x2r;  im[i + 2] = x0i - x2i;
        re[i + 1] = x1r + (-x3i); im[i + 1] = x1i + x3r;
        re[i + 3] = x1r - (-x3i); im[i + 3] = x1i - x3r;
    }
    for (int h = 4; h < kM; h <<= 1)
    {
        const float* wr = stageC_ + (h - 1);
        const float* wi = stageS_ + (h - 1);
        for (int start = 0; start < kM; start += 2 * h)
        {
            int k = 0;
#if SB_SPECTRAL_NEON
            if (neon)
                for (; k < h; k += 4)
                {
                    const int a = start + k, b = a + h;
                    const float32x4_t cr = vld1q_f32 (wr + k), ci = vld1q_f32 (wi + k);
                    const float32x4_t rb = vld1q_f32 (re + b), ib = vld1q_f32 (im + b);
                    const float32x4_t tr = vsubq_f32 (vmulq_f32 (rb, cr), vmulq_f32 (ib, ci));
                    const float32x4_t ti = vaddq_f32 (vmulq_f32 (rb, ci), vmulq_f32 (ib, cr));
                    const float32x4_t ra = vld1q_f32 (re + a), ia = vld1q_f32 (im + a);
                    vst1q_f32 (re + b, vsubq_f32 (ra, tr));
                    vst1q_f32 (im + b, vsubq_f32 (ia, ti));
                    vst1q_f32 (re + a, vaddq_f32 (ra, tr));
                    vst1q_f32 (im + a, vaddq_f32 (ia, ti));
                }
#else
            (void) neon;
#endif
            for (; k < h; ++k)
            {
                const int a = start + k, b = a + h;
                const float tr = re[b] * wr[k] - im[b] * wi[k], ti = re[b] * wi[k] + im[b] * wr[k];
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void RealIfft::inverse (const float* xr, const float* xi, float* zr, float* zi, bool neon) const noexcept
{
    // Z[k] = A[k] + i B[k], A = X[k] + conj (X[M - k]), B = (X[k] - conj (X[M - k])) e^(+2 pi i k / N): the even and odd samples folded into one complex spectrum of half the length
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
        for (; k < kM; k += 4)
        {
            const auto rev = [] (float32x4_t v) { v = vrev64q_f32 (v); return vcombine_f32 (vget_high_f32 (v), vget_low_f32 (v)); };
            const float32x4_t kr = vld1q_f32 (xr + k), ki = vld1q_f32 (xi + k);
            const float32x4_t mr = rev (vld1q_f32 (xr + (kM - k - 3))), mi = rev (vld1q_f32 (xi + (kM - k - 3)));
            const float32x4_t ar = vaddq_f32 (kr, mr), ai = vsubq_f32 (ki, mi);
            const float32x4_t dr = vsubq_f32 (kr, mr), di = vaddq_f32 (ki, mi);
            const float32x4_t c = vld1q_f32 (packC_ + k), s = vld1q_f32 (packS_ + k);
            const float32x4_t br = vsubq_f32 (vmulq_f32 (dr, c), vmulq_f32 (di, s));
            const float32x4_t bi = vaddq_f32 (vmulq_f32 (dr, s), vmulq_f32 (di, c));
            vst1q_f32 (zr + k, vsubq_f32 (ar, bi));
            vst1q_f32 (zi + k, vaddq_f32 (ai, br));
        }
#endif
    for (; k < kM; ++k)
    {
        const int m = kM - k;
        const float ar = xr[k] + xr[m], ai = xi[k] - xi[m];
        const float dr = xr[k] - xr[m], di = xi[k] + xi[m];
        const float br = dr * packC_[k] - di * packS_[k];
        const float bi = dr * packS_[k] + di * packC_[k];
        zr[k] = ar - bi;
        zi[k] = ai + br;
    }
    transform (zr, zi, neon);
}

// ---- sine and cosine ------------------------------------------------------------------------------------------------------------------

namespace
{
constexpr float kTwoOverPi = 0.636619772367581343f;
constexpr float kC1 = 1.5703125f, kC2 = 4.837512969970703125e-4f, kC3 = 7.549789948768648e-8f; // pi / 2 in three parts (the first two have few enough bits for k * C to be exact)
constexpr float kS0 = -1.9515295891e-4f, kS1 = 8.3321608736e-3f, kS2 = -1.6666654611e-1f;
constexpr float kK0 = 2.443315711809948e-5f, kK1 = -1.388731625493765e-3f, kK2 = 4.166664568298827e-2f;
} // namespace

void sincosPoly (float a, float& s, float& c) noexcept
{
    const int j = static_cast<int> (a * kTwoOverPi + (a < 0.0f ? -0.5f : 0.5f));
    const float y = static_cast<float> (j);
    const float r = ((a - y * kC1) - y * kC2) - y * kC3;
    const float z = r * r;
    const float sn = (((kS0 * z + kS1) * z + kS2) * z) * r + r;
    const float cs = ((kK0 * z + kK1) * z + kK2) * z * z - 0.5f * z + 1.0f;
    const bool swap = (j & 1) != 0, negS = (j & 2) != 0, negC = ((j & 1) ^ ((j >> 1) & 1)) != 0;
    float rs = swap ? cs : sn, rc = swap ? sn : cs;
    if (negS) rs = -rs;
    if (negC) rc = -rc;
    s = rs;
    c = rc;
}

void sincosBlock (const float* a, float* s, float* c, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t x = vld1q_f32 (a + k);
            const uint32x4_t negIn = vcltq_f32 (x, vdupq_n_f32 (0.0f));
            const float32x4_t half = vbslq_f32 (negIn, vdupq_n_f32 (-0.5f), vdupq_n_f32 (0.5f));
            const int32x4_t j = vcvtq_s32_f32 (vaddq_f32 (vmulq_f32 (x, vdupq_n_f32 (kTwoOverPi)), half));
            const float32x4_t y = vcvtq_f32_s32 (j);
            float32x4_t r = vsubq_f32 (x, vmulq_f32 (y, vdupq_n_f32 (kC1)));
            r = vsubq_f32 (r, vmulq_f32 (y, vdupq_n_f32 (kC2)));
            r = vsubq_f32 (r, vmulq_f32 (y, vdupq_n_f32 (kC3)));
            const float32x4_t z = vmulq_f32 (r, r);
            float32x4_t ps = vaddq_f32 (vmulq_f32 (vdupq_n_f32 (kS0), z), vdupq_n_f32 (kS1));
            ps = vaddq_f32 (vmulq_f32 (ps, z), vdupq_n_f32 (kS2));
            const float32x4_t sn = vaddq_f32 (vmulq_f32 (vmulq_f32 (ps, z), r), r);
            float32x4_t pc = vaddq_f32 (vmulq_f32 (vdupq_n_f32 (kK0), z), vdupq_n_f32 (kK1));
            pc = vaddq_f32 (vmulq_f32 (pc, z), vdupq_n_f32 (kK2));
            const float32x4_t zz = vmulq_f32 (pc, z);
            const float32x4_t cs = vaddq_f32 (vsubq_f32 (vmulq_f32 (zz, z), vmulq_f32 (vdupq_n_f32 (0.5f), z)), vdupq_n_f32 (1.0f));
            const int32x4_t one = vdupq_n_s32 (1);
            const uint32x4_t swap = vceqq_s32 (vandq_s32 (j, one), one);
            const int32x4_t b0 = vandq_s32 (j, one), b1 = vandq_s32 (vshrq_n_s32 (j, 1), one);
            const uint32x4_t negS = vceqq_s32 (b1, one), negC = vceqq_s32 (veorq_s32 (b0, b1), one);
            float32x4_t rs = vbslq_f32 (swap, cs, sn), rc = vbslq_f32 (swap, sn, cs);
            const uint32x4_t sign = vdupq_n_u32 (0x80000000u);
            rs = vreinterpretq_f32_u32 (veorq_u32 (vreinterpretq_u32_f32 (rs), vandq_u32 (negS, sign)));
            rc = vreinterpretq_f32_u32 (veorq_u32 (vreinterpretq_u32_f32 (rc), vandq_u32 (negC, sign)));
            vst1q_f32 (s + k, rs);
            vst1q_f32 (c + k, rc);
        }
#else
    (void) neon;
#endif
    for (; k < n; ++k)
        sincosPoly (a[k], s[k], c[k]);
}

// ---- the phase wrap --------------------------------------------------------------------------------------------------------------------

float wrapPhase (float x) noexcept
{
    const float ax = std::fabs (x);
    if (! (ax < 1.0e9f))
        return wrapPiF (std::fmod (x, kTwoPiF)); // NaN, infinity or beyond any phase the voice reaches: the library's way
    constexpr double c = static_cast<double> (kTwoPiF);
    const double a = static_cast<double> (ax);
    const double n = static_cast<double> (static_cast<int64_t> (a * (1.0 / c)));
    double r = a - n * c;                // n * c is exact (n has at most 30 bits and c 24), and so is the difference: it is the remainder itself, which a float holds
    if (r >= c) r -= c;                  // the estimate of n was one short
    else if (r < 0.0) r += c;            // or one too many
    const float f = static_cast<float> (r);
    return wrapPiF (x < 0.0f ? -f : f);
}

void advancePhases (float* phase, const float* step, int n) noexcept
{
    for (int k = 0; k < n; ++k)
    {
        float x = phase[k] + step[k] * static_cast<float> (kHop);
        if (x > 100.0f || x < -100.0f)
            x = wrapPhase (x);
        phase[k] = x;
    }
}

// ---- the overlap add -------------------------------------------------------------------------------------------------------------------

namespace
{
const float* windowTable() noexcept
{
    static const struct Table
    {
        float w[kFftSize];
        Table()
        {
            for (int i = 0; i < kFftSize; ++i)
                w[i] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * kPi * i / kFftSize)) * (1.0f / kFftSize); // the exact power of two
        }
    } table;
    return table.w;
}
} // namespace

void overlapAdd (float* ring, int at, const float* zr, const float* zi, float scale, bool neon) noexcept
{
    const float* w = windowTable();
    int n = 0;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        const float32x4_t vs = vdupq_n_f32 (scale);
        for (; n < kFftSize; n += 8)
        {
            const float32x4x2_t x = vzipq_f32 (vld1q_f32 (zr + n / 2), vld1q_f32 (zi + n / 2));
            const int p = (at + n) & (kFftSize - 1);
            vst1q_f32 (ring + p, vaddq_f32 (vld1q_f32 (ring + p), vmulq_f32 (vmulq_f32 (x.val[0], vld1q_f32 (w + n)), vs)));
            vst1q_f32 (ring + p + 4, vaddq_f32 (vld1q_f32 (ring + p + 4), vmulq_f32 (vmulq_f32 (x.val[1], vld1q_f32 (w + n + 4)), vs)));
        }
    }
#else
    (void) neon;
#endif
    for (; n < kFftSize; n += 2)
    {
        const int p = (at + n) & (kFftSize - 1);
        ring[p] += zr[n / 2] * w[n] * scale;
        ring[(p + 1) & (kFftSize - 1)] += zi[n / 2] * w[n + 1] * scale;
    }
}
} // namespace kernels
} // namespace spectral
} // namespace synthyboy
