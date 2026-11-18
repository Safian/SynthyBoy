// SynthyBoy - core/spectral: the kernels of one synthesis frame (see synthyboy/spectral/kernels.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/spectral/kernels.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
        if (r > i)
        {
            swapI_[numSwaps_] = static_cast<uint16_t> (i);
            swapJ_[numSwaps_] = static_cast<uint16_t> (r);
            ++numSwaps_;
        }
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
    // the bit reversal: the pairs that change places, listed once (no test per index)
    for (int p = 0; p < numSwaps_; ++p)
    {
        const int i = swapI_[p], j = swapJ_[p];
        std::swap (re[i], re[j]);
        std::swap (im[i], im[j]);
    }
    // the first two stages together (the twiddles are 1 and +i): blocks of four. The four lanes of a vector are four blocks (vld4 takes every fourth value into one vector).
    int i = 0;
#if SB_SPECTRAL_NEON
    if (neon)
        for (; i < kM; i += 16)
        {
            const float32x4x4_t r = vld4q_f32 (re + i), m = vld4q_f32 (im + i);
            const float32x4_t x0r = vaddq_f32 (r.val[0], r.val[1]), x0i = vaddq_f32 (m.val[0], m.val[1]);
            const float32x4_t x1r = vsubq_f32 (r.val[0], r.val[1]), x1i = vsubq_f32 (m.val[0], m.val[1]);
            const float32x4_t x2r = vaddq_f32 (r.val[2], r.val[3]), x2i = vaddq_f32 (m.val[2], m.val[3]);
            const float32x4_t x3r = vsubq_f32 (r.val[2], r.val[3]), x3i = vsubq_f32 (m.val[2], m.val[3]);
            float32x4x4_t outR, outI;
            outR.val[0] = vaddq_f32 (x0r, x2r);              outI.val[0] = vaddq_f32 (x0i, x2i);
            outR.val[2] = vsubq_f32 (x0r, x2r);              outI.val[2] = vsubq_f32 (x0i, x2i);
            outR.val[1] = vaddq_f32 (x1r, vnegq_f32 (x3i));  outI.val[1] = vaddq_f32 (x1i, x3r);
            outR.val[3] = vsubq_f32 (x1r, vnegq_f32 (x3i));  outI.val[3] = vsubq_f32 (x1i, x3r);
            vst4q_f32 (re + i, outR);
            vst4q_f32 (im + i, outI);
        }
#else
    (void) neon;
#endif
    for (; i < kM; i += 4)
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
    // the remaining eight stages in pairs: the stage h and the stage 2 h on the four points of a butterfly of both, so that the data are loaded and stored once for two stages
    // (the second stage's twiddle for the points b, d is i times the one for a, c: the table value of e^(i pi (k + h) / 2 h) is exactly that rotation)
    for (int h = 4; h < kM; h <<= 2)
    {
        const float* w1r = stageC_ + (h - 1);
        const float* w1i = stageS_ + (h - 1);
        const float* w2r = stageC_ + (2 * h - 1);
        const float* w2i = stageS_ + (2 * h - 1);
        for (int start = 0; start < kM; start += 4 * h)
        {
            int k = 0;
#if SB_SPECTRAL_NEON
            if (neon)
                for (; k < h; k += 4)
                {
                    const int a = start + k, b = a + h, c = b + h, d = c + h;
                    const float32x4_t c1 = vld1q_f32 (w1r + k), s1 = vld1q_f32 (w1i + k), c2 = vld1q_f32 (w2r + k), s2 = vld1q_f32 (w2i + k);
                    const float32x4_t rb = vld1q_f32 (re + b), ib = vld1q_f32 (im + b), rd = vld1q_f32 (re + d), id = vld1q_f32 (im + d);
                    const float32x4_t ra = vld1q_f32 (re + a), ia = vld1q_f32 (im + a), rc = vld1q_f32 (re + c), ic = vld1q_f32 (im + c);
                    // the stage h: (a, b) and (c, d) with the twiddle of k
                    const float32x4_t tbr = vsubq_f32 (vmulq_f32 (rb, c1), vmulq_f32 (ib, s1)), tbi = vaddq_f32 (vmulq_f32 (rb, s1), vmulq_f32 (ib, c1));
                    const float32x4_t tdr = vsubq_f32 (vmulq_f32 (rd, c1), vmulq_f32 (id, s1)), tdi = vaddq_f32 (vmulq_f32 (rd, s1), vmulq_f32 (id, c1));
                    const float32x4_t par = vaddq_f32 (ra, tbr), pai = vaddq_f32 (ia, tbi), pbr = vsubq_f32 (ra, tbr), pbi = vsubq_f32 (ia, tbi);
                    const float32x4_t pcr = vaddq_f32 (rc, tdr), pci = vaddq_f32 (ic, tdi), pdr = vsubq_f32 (rc, tdr), pdi = vsubq_f32 (ic, tdi);
                    // the stage 2 h: (a, c) with the twiddle of k, (b, d) with i times it
                    const float32x4_t ucr = vsubq_f32 (vmulq_f32 (pcr, c2), vmulq_f32 (pci, s2)), uci = vaddq_f32 (vmulq_f32 (pcr, s2), vmulq_f32 (pci, c2));
                    const float32x4_t udr = vsubq_f32 (vmulq_f32 (pdr, c2), vmulq_f32 (pdi, s2)), udi = vaddq_f32 (vmulq_f32 (pdr, s2), vmulq_f32 (pdi, c2));
                    vst1q_f32 (re + a, vaddq_f32 (par, ucr));
                    vst1q_f32 (im + a, vaddq_f32 (pai, uci));
                    vst1q_f32 (re + c, vsubq_f32 (par, ucr));
                    vst1q_f32 (im + c, vsubq_f32 (pai, uci));
                    vst1q_f32 (re + b, vaddq_f32 (pbr, vnegq_f32 (udi)));
                    vst1q_f32 (im + b, vaddq_f32 (pbi, udr));
                    vst1q_f32 (re + d, vsubq_f32 (pbr, vnegq_f32 (udi)));
                    vst1q_f32 (im + d, vsubq_f32 (pbi, udr));
                }
#endif
            for (; k < h; ++k)
            {
                const int a = start + k, b = a + h, c = b + h, d = c + h;
                const float tbr = re[b] * w1r[k] - im[b] * w1i[k], tbi = re[b] * w1i[k] + im[b] * w1r[k];
                const float tdr = re[d] * w1r[k] - im[d] * w1i[k], tdi = re[d] * w1i[k] + im[d] * w1r[k];
                const float par = re[a] + tbr, pai = im[a] + tbi, pbr = re[a] - tbr, pbi = im[a] - tbi;
                const float pcr = re[c] + tdr, pci = im[c] + tdi, pdr = re[c] - tdr, pdi = im[c] - tdi;
                const float ucr = pcr * w2r[k] - pci * w2i[k], uci = pcr * w2i[k] + pci * w2r[k];
                const float udr = pdr * w2r[k] - pdi * w2i[k], udi = pdr * w2i[k] + pdi * w2r[k];
                re[a] = par + ucr;
                im[a] = pai + uci;
                re[c] = par - ucr;
                im[c] = pai - uci;
                re[b] = pbr + (-udi);
                im[b] = pbi + udr;
                re[d] = pbr - (-udi);
                im[d] = pbi - udr;
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

#if SB_SPECTRAL_NEON
namespace
{
/** sincosPoly () of four values. */
inline void sincos4 (float32x4_t x, float32x4_t& rs, float32x4_t& rc) noexcept
{
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
    float32x4_t os = vbslq_f32 (swap, cs, sn), oc = vbslq_f32 (swap, sn, cs);
    const uint32x4_t sign = vdupq_n_u32 (0x80000000u);
    rs = vreinterpretq_f32_u32 (veorq_u32 (vreinterpretq_u32_f32 (os), vandq_u32 (negS, sign)));
    rc = vreinterpretq_f32_u32 (veorq_u32 (vreinterpretq_u32_f32 (oc), vandq_u32 (negC, sign)));
}
} // namespace
#endif

void sincosBlock (const float* a, float* s, float* c, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
        for (; k + 4 <= n; k += 4)
        {
            float32x4_t rs, rc;
            sincos4 (vld1q_f32 (a + k), rs, rc);
            vst1q_f32 (s + k, rs);
            vst1q_f32 (c + k, rc);
        }
#else
    (void) neon;
#endif
    for (; k < n; ++k)
        sincosPoly (a[k], s[k], c[k]);
}

void spectrumFromAngles (const float* angle, const float* mag, float* re, float* im, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
        for (; k + 4 <= n; k += 4)
        {
            float32x4_t rs, rc;
            sincos4 (vld1q_f32 (angle + k), rs, rc);
            const float32x4_t m = vld1q_f32 (mag + k);
            vst1q_f32 (re + k, vmulq_f32 (m, rc));
            vst1q_f32 (im + k, vmulq_f32 (m, rs));
        }
#else
    (void) neon;
#endif
    for (; k < n; ++k)
    {
        float s, c;
        sincosPoly (angle[k], s, c);
        re[k] = mag[k] * c;
        im[k] = mag[k] * s;
    }
}

// ---- the phase wrap --------------------------------------------------------------------------------------------------------------------

float wrapPhase (float x) noexcept
{
    const float ax = std::fabs (x);
    if (! (ax < 1.0e9f))
        return wrapPiF (std::fmod (x, kTwoPiF)); // NaN, infinity or beyond any phase the voice reaches: the library's way
    constexpr double c = static_cast<double> (kTwoPiF);
    const double a = static_cast<double> (ax);
    const double n = static_cast<double> (static_cast<int> (a * (1.0 / c))); // (32 bit integers: a 64 bit conversion of a double is a library call on 32 bit ARM)
    double r = a - n * c;                // n * c is exact (n has at most 28 bits and c 24), and so is the difference: it is the remainder itself, which a float holds
    if (r >= c) r -= c;                  // the estimate of n was one short
    else if (r < 0.0) r += c;            // or one too many
    const float f = static_cast<float> (r);
    return wrapPiF (x < 0.0f ? -f : f);
}

namespace
{
// 2 pi (the float) in three parts of 8 bits each: their sum is the float exactly, and n times a part is exact for n < 2^16
constexpr float kW1 = 6.28125f, kW2 = 0.00193023681640625f, kW3 = 5.245208740234375e-06f;
constexpr float kInvTwoPiLow = 0.15915494f * (1.0f - 1.0f / 4194304.0f);   // 1 / 2 pi a little low: the quotient never rounds up past a whole number, n is at most one short
constexpr float kReachable = 1.0e5f;
}

float wrapPhaseFloat (float x) noexcept
{
    const float ax = std::fabs (x);
    if (! (ax < kReachable))
        return wrapPhase (x);
    const float n = static_cast<float> (static_cast<int> (ax * kInvTwoPiLow));
    float r = ((ax - n * kW1) - n * kW2) - n * kW3;
    if (r >= kTwoPiF) r -= kTwoPiF;      // n was one short
    if (r < 0.0f) r += kTwoPiF;
    if (r > kPiF) r -= kTwoPiF;          // the wrap to +-pi (exact: r and 2 pi are within a factor of two)
    return x < 0.0f ? -r : r;
}

void advancePhases (float* phase, const float* step, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        // no test of the lanes (a vector compare moved to the ARM registers stalls the NEON pipeline): the arithmetic is the same for every lane and the result is selected by the masks
        const float32x4_t hop = vdupq_n_f32 (static_cast<float> (kHop)), hundred = vdupq_n_f32 (100.0f), two = vdupq_n_f32 (kTwoPiF);
        const float32x4_t w1 = vdupq_n_f32 (kW1), w2 = vdupq_n_f32 (kW2), w3 = vdupq_n_f32 (kW3), inv = vdupq_n_f32 (kInvTwoPiLow), pi = vdupq_n_f32 (kPiF), zero = vdupq_n_f32 (0.0f);
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t x = vaddq_f32 (vld1q_f32 (phase + k), vmulq_f32 (vld1q_f32 (step + k), hop));
            const float32x4_t ax = vabsq_f32 (x);
            const uint32x4_t wrapIt = vcgtq_f32 (ax, hundred);
            const float32x4_t nf = vcvtq_f32_s32 (vcvtq_s32_f32 (vmulq_f32 (ax, inv)));
            float32x4_t r = vsubq_f32 (vsubq_f32 (vsubq_f32 (ax, vmulq_f32 (nf, w1)), vmulq_f32 (nf, w2)), vmulq_f32 (nf, w3));
            r = vbslq_f32 (vcgeq_f32 (r, two), vsubq_f32 (r, two), r);
            r = vbslq_f32 (vcltq_f32 (r, zero), vaddq_f32 (r, two), r);
            r = vbslq_f32 (vcgtq_f32 (r, pi), vsubq_f32 (r, two), r);
            const float32x4_t signedR = vbslq_f32 (vcltq_f32 (x, zero), vnegq_f32 (r), r);
            vst1q_f32 (phase + k, vbslq_f32 (wrapIt, signedR, x));
        }
    }
#else
    (void) neon;
#endif
    for (; k < n; ++k)
    {
        float x = phase[k] + step[k] * static_cast<float> (kHop);
        if (x > 100.0f || x < -100.0f)
            x = wrapPhaseFloat (x);
        phase[k] = x;
    }
}

void advanceToSpectrum (float* phase, const float* step, const float* mag, float* re, float* im, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        const float32x4_t hop = vdupq_n_f32 (static_cast<float> (kHop)), hundred = vdupq_n_f32 (100.0f), two = vdupq_n_f32 (kTwoPiF);
        const float32x4_t w1 = vdupq_n_f32 (kW1), w2 = vdupq_n_f32 (kW2), w3 = vdupq_n_f32 (kW3), inv = vdupq_n_f32 (kInvTwoPiLow), pi = vdupq_n_f32 (kPiF), zero = vdupq_n_f32 (0.0f);
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t x = vaddq_f32 (vld1q_f32 (phase + k), vmulq_f32 (vld1q_f32 (step + k), hop));
            const float32x4_t ax = vabsq_f32 (x);
            const uint32x4_t wrapIt = vcgtq_f32 (ax, hundred);
            const float32x4_t nf = vcvtq_f32_s32 (vcvtq_s32_f32 (vmulq_f32 (ax, inv)));
            float32x4_t r = vsubq_f32 (vsubq_f32 (vsubq_f32 (ax, vmulq_f32 (nf, w1)), vmulq_f32 (nf, w2)), vmulq_f32 (nf, w3));
            r = vbslq_f32 (vcgeq_f32 (r, two), vsubq_f32 (r, two), r);
            r = vbslq_f32 (vcltq_f32 (r, zero), vaddq_f32 (r, two), r);
            r = vbslq_f32 (vcgtq_f32 (r, pi), vsubq_f32 (r, two), r);
            const float32x4_t wrapped = vbslq_f32 (wrapIt, vbslq_f32 (vcltq_f32 (x, zero), vnegq_f32 (r), r), x);
            vst1q_f32 (phase + k, wrapped);
            float32x4_t rs, rc;
            sincos4 (wrapped, rs, rc);
            const float32x4_t m = vld1q_f32 (mag + k);
            vst1q_f32 (re + k, vmulq_f32 (m, rc));
            vst1q_f32 (im + k, vmulq_f32 (m, rs));
        }
    }
#else
    (void) neon;
#endif
    for (; k < n; ++k)
    {
        float x = phase[k] + step[k] * static_cast<float> (kHop);
        if (x > 100.0f || x < -100.0f)
            x = wrapPhaseFloat (x);
        phase[k] = x;
        float s, c;
        sincosPoly (x, s, c);
        re[k] = mag[k] * c;
        im[k] = mag[k] * s;
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

// ---- the per bin loops of shapeFrame () ---------------------------------------------------------------------------------------------------

void lerpBins (float* out, const float* a, const float* b, float t, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        const float32x4_t vt = vdupq_n_f32 (t);
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t va = vld1q_f32 (a + k);
            vst1q_f32 (out + k, vaddq_f32 (va, vmulq_f32 (vsubq_f32 (vld1q_f32 (b + k), va), vt)));
        }
    }
#else
    (void) neon;
#endif
    for (; k < n; ++k)
        out[k] = a[k] + (b[k] - a[k]) * t;
}

void mulBins (float* a, const float* g, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
        for (; k + 4 <= n; k += 4)
            vst1q_f32 (a + k, vmulq_f32 (vld1q_f32 (a + k), vld1q_f32 (g + k)));
#else
    (void) neon;
#endif
    for (; k < n; ++k)
        a[k] *= g[k];
}

float maxBins (const float* a, int n, bool neon) noexcept
{
    float top = 0.0f;
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon && n >= 4)
    {
        float32x4_t v = vdupq_n_f32 (0.0f);
        for (; k + 4 <= n; k += 4)
            v = vmaxq_f32 (v, vld1q_f32 (a + k));
        const float32x2_t m = vpmax_f32 (vget_low_f32 (v), vget_high_f32 (v));
        top = std::max (vget_lane_f32 (m, 0), vget_lane_f32 (m, 1));
    }
#else
    (void) neon;
#endif
    for (; k < n; ++k)
        top = std::max (top, a[k]);
    return top;
}

int findPeaks (const float* m, int n, float top, int* peaks, int maxPeaks, bool neon) noexcept
{
    const float threshold = 1.0e-5f * top;
    int count = 0;
    int k = 1;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        // sixteen bins at a time: the four comparisons become one byte per bin (0xff for a peak), and only a group of sixteen that has a peak is looked at one by one (a vector compare moved to
        // the ARM registers for every four bins stalls the NEON pipeline)
        const float32x4_t vth = vdupq_n_f32 (threshold);
        const auto mask4 = [&] (int at)
        {
            const float32x4_t c = vld1q_f32 (m + at), p = vld1q_f32 (m + at - 1), q = vld1q_f32 (m + at + 1);
            return vandq_u32 (vandq_u32 (vcgtq_f32 (c, p), vcgeq_f32 (c, q)), vcgtq_f32 (c, vth));
        };
        for (; k + 16 <= n - 1 && count < maxPeaks; k += 16)
        {
            const uint16x8_t lo = vcombine_u16 (vmovn_u32 (mask4 (k)), vmovn_u32 (mask4 (k + 4)));
            const uint16x8_t hi = vcombine_u16 (vmovn_u32 (mask4 (k + 8)), vmovn_u32 (mask4 (k + 12)));
            uint8_t flags[16];
            vst1q_u8 (flags, vcombine_u8 (vmovn_u16 (lo), vmovn_u16 (hi)));
            uint32_t words[4];
            std::memcpy (words, flags, sizeof words);
            if ((words[0] | words[1] | words[2] | words[3]) == 0u)
                continue;
            for (int j = 0; j < 16 && count < maxPeaks; ++j)
                if (flags[j] != 0)
                    peaks[count++] = k + j;
        }
    }
#else
    (void) neon;
#endif
    for (; k < n - 1 && count < maxPeaks; ++k)
        if (m[k] > m[k - 1] && m[k] >= m[k + 1] && m[k] > threshold)
            peaks[count++] = k;
    return count;
}

void placeRegion (const float* mag, const float* freq, const float* theta, float* om, float* of, float* oth, int from, int to, int offset, float r, float shiftRad, int n, bool neon) noexcept
{
    from = std::max (from, 1 - offset);
    to = std::min (to, n - 1 - offset);
    int k = from;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        const float32x4_t vr = vdupq_n_f32 (r), vs = vdupq_n_f32 (shiftRad);
        for (; k + 3 <= to; k += 4)
        {
            const float32x4_t mv = vld1q_f32 (mag + k), ov = vld1q_f32 (om + k + offset);
            const uint32x4_t above = vcgtq_f32 (mv, ov);
            vst1q_f32 (om + k + offset, vbslq_f32 (above, mv, ov));
            vst1q_f32 (of + k + offset, vbslq_f32 (above, vaddq_f32 (vmulq_f32 (vld1q_f32 (freq + k), vr), vs), vld1q_f32 (of + k + offset)));
            vst1q_f32 (oth + k + offset, vbslq_f32 (above, vld1q_f32 (theta + k), vld1q_f32 (oth + k + offset)));
        }
    }
#else
    (void) neon;
#endif
    for (; k <= to; ++k)
    {
        const int j = k + offset;
        if (mag[k] > om[j])
        {
            om[j] = mag[k];
            of[j] = freq[k] * r + shiftRad;
            oth[j] = theta[k];
        }
    }
}
} // namespace kernels
} // namespace spectral
} // namespace synthyboy
