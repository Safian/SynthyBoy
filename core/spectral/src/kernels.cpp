// SynthyBoy - core/spectral: the kernels of one synthesis frame (see synthyboy/spectral/kernels.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/spectral/kernels.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
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

namespace
{
std::atomic<int> gTablesBuilt { 0 };
}

int tablesBuilt() noexcept { return gTablesBuilt.load(); }

// ---- the inverse transform of a real signal -----------------------------------------------------------------------------------------

RealIfft::RealIfft()
{
    gTablesBuilt.fetch_add (1);
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

namespace
{
// 2 pi (the float) in three parts of 8 bits each: their sum is the float exactly, and n times a part is exact for n < 2^16
constexpr float kW1 = 6.28125f, kW2 = 0.00193023681640625f, kW3 = 5.245208740234375e-06f;
constexpr float kInvTwoPiLow = 0.15915494f * (1.0f - 1.0f / 4194304.0f);   // 1 / 2 pi a little low: the quotient never rounds up past a whole number, n is at most one short
constexpr float kReachable = 1.0e5f;
}

#if SB_SPECTRAL_NEON
namespace
{
/** sincosPoly () of U groups of four values, the operations of the groups interleaved (every statement of the polynomial for all groups before the next): the vector unit of the Key 61's
    core issues in order, so the chain of one group (about 25 dependent operations of 4 to 6 cycles each) idles it; with two groups in flight the second one fills the gaps. */
template <int U>
inline void sincosN (const float32x4_t* x, float32x4_t* rs, float32x4_t* rc) noexcept
{
    float32x4_t half[U], r[U], y[U], z[U], ps[U], pc[U], sn[U], cs[U], os[U], oc[U];
    int32x4_t j[U], b0[U], b1[U];
    uint32x4_t swap[U], negS[U], negC[U];
    const int32x4_t one = vdupq_n_s32 (1);
    const uint32x4_t sign = vdupq_n_u32 (0x80000000u);
    for (int u = 0; u < U; ++u) half[u] = vbslq_f32 (vcltq_f32 (x[u], vdupq_n_f32 (0.0f)), vdupq_n_f32 (-0.5f), vdupq_n_f32 (0.5f));
    for (int u = 0; u < U; ++u) j[u] = vcvtq_s32_f32 (vaddq_f32 (vmulq_f32 (x[u], vdupq_n_f32 (kTwoOverPi)), half[u]));
    for (int u = 0; u < U; ++u) y[u] = vcvtq_f32_s32 (j[u]);
    for (int u = 0; u < U; ++u) r[u] = vsubq_f32 (x[u], vmulq_f32 (y[u], vdupq_n_f32 (kC1)));
    for (int u = 0; u < U; ++u) r[u] = vsubq_f32 (r[u], vmulq_f32 (y[u], vdupq_n_f32 (kC2)));
    for (int u = 0; u < U; ++u) r[u] = vsubq_f32 (r[u], vmulq_f32 (y[u], vdupq_n_f32 (kC3)));
    for (int u = 0; u < U; ++u) z[u] = vmulq_f32 (r[u], r[u]);
    for (int u = 0; u < U; ++u) ps[u] = vaddq_f32 (vmulq_f32 (vdupq_n_f32 (kS0), z[u]), vdupq_n_f32 (kS1));
    for (int u = 0; u < U; ++u) pc[u] = vaddq_f32 (vmulq_f32 (vdupq_n_f32 (kK0), z[u]), vdupq_n_f32 (kK1));
    for (int u = 0; u < U; ++u) ps[u] = vaddq_f32 (vmulq_f32 (ps[u], z[u]), vdupq_n_f32 (kS2));
    for (int u = 0; u < U; ++u) pc[u] = vaddq_f32 (vmulq_f32 (pc[u], z[u]), vdupq_n_f32 (kK2));
    for (int u = 0; u < U; ++u) sn[u] = vaddq_f32 (vmulq_f32 (vmulq_f32 (ps[u], z[u]), r[u]), r[u]);
    for (int u = 0; u < U; ++u) cs[u] = vaddq_f32 (vsubq_f32 (vmulq_f32 (vmulq_f32 (pc[u], z[u]), z[u]), vmulq_f32 (vdupq_n_f32 (0.5f), z[u])), vdupq_n_f32 (1.0f));
    for (int u = 0; u < U; ++u) { b0[u] = vandq_s32 (j[u], one); b1[u] = vandq_s32 (vshrq_n_s32 (j[u], 1), one); }
    for (int u = 0; u < U; ++u) { swap[u] = vceqq_s32 (b0[u], one); negS[u] = vceqq_s32 (b1[u], one); negC[u] = vceqq_s32 (veorq_s32 (b0[u], b1[u]), one); }
    for (int u = 0; u < U; ++u) { os[u] = vbslq_f32 (swap[u], cs[u], sn[u]); oc[u] = vbslq_f32 (swap[u], sn[u], cs[u]); }
    for (int u = 0; u < U; ++u)
    {
        rs[u] = vreinterpretq_f32_u32 (veorq_u32 (vreinterpretq_u32_f32 (os[u]), vandq_u32 (negS[u], sign)));
        rc[u] = vreinterpretq_f32_u32 (veorq_u32 (vreinterpretq_u32_f32 (oc[u]), vandq_u32 (negC[u], sign)));
    }
}

/** advance4 () of U groups: the phase sum and its wrap, interleaved the same way. */
template <int U>
inline void wrapN (const float32x4_t* x, float32x4_t* out) noexcept
{
    const float32x4_t hundred = vdupq_n_f32 (100.0f), two = vdupq_n_f32 (kTwoPiF), zero = vdupq_n_f32 (0.0f);
    float32x4_t ax[U], nf[U], r[U], r1[U], t2[U], u3[U], s[U], bb[U], e[U], w[U], bw[U], e2[U];
    uint32x4_t wrapIt[U];
    for (int u = 0; u < U; ++u) ax[u] = vabsq_f32 (x[u]);
    for (int u = 0; u < U; ++u) wrapIt[u] = vcgtq_f32 (ax[u], hundred);
    for (int u = 0; u < U; ++u) nf[u] = vcvtq_f32_s32 (vcvtq_s32_f32 (vmulq_f32 (ax[u], vdupq_n_f32 (kInvTwoPiLow))));
    for (int u = 0; u < U; ++u) r1[u] = vsubq_f32 (ax[u], vmulq_f32 (nf[u], vdupq_n_f32 (kW1)));
    for (int u = 0; u < U; ++u) t2[u] = vmulq_f32 (nf[u], vdupq_n_f32 (kW2));
    for (int u = 0; u < U; ++u) u3[u] = vmulq_f32 (nf[u], vdupq_n_f32 (kW3));
    for (int u = 0; u < U; ++u) s[u] = vsubq_f32 (r1[u], t2[u]);
    for (int u = 0; u < U; ++u) bb[u] = vsubq_f32 (s[u], r1[u]);
    for (int u = 0; u < U; ++u) e[u] = vaddq_f32 (vsubq_f32 (r1[u], vsubq_f32 (s[u], bb[u])), vsubq_f32 (vnegq_f32 (t2[u]), bb[u]));
    for (int u = 0; u < U; ++u) w[u] = vsubq_f32 (s[u], u3[u]);
    for (int u = 0; u < U; ++u) bw[u] = vsubq_f32 (w[u], s[u]);
    for (int u = 0; u < U; ++u) e2[u] = vaddq_f32 (vsubq_f32 (s[u], vsubq_f32 (w[u], bw[u])), vsubq_f32 (vnegq_f32 (u3[u]), bw[u]));
    for (int u = 0; u < U; ++u) r[u] = vaddq_f32 (w[u], vaddq_f32 (e2[u], e[u]));
    for (int u = 0; u < U; ++u) r[u] = vbslq_f32 (vcgeq_f32 (r[u], two), vsubq_f32 (r[u], two), r[u]);
    for (int u = 0; u < U; ++u) r[u] = vbslq_f32 (vcltq_f32 (r[u], zero), vaddq_f32 (r[u], two), r[u]);
    for (int u = 0; u < U; ++u) r[u] = vbslq_f32 (vcgtq_f32 (r[u], vdupq_n_f32 (kPiF)), vsubq_f32 (r[u], two), r[u]);
    for (int u = 0; u < U; ++u) out[u] = vbslq_f32 (wrapIt[u], vbslq_f32 (vcltq_f32 (x[u], zero), vnegq_f32 (r[u]), r[u]), x[u]);
}
} // namespace
#endif

void sincosBlock (const float* a, float* s, float* c, int n, bool neon) noexcept
{
    int k = 0;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        for (; k + 8 <= n; k += 8)
        {
            const float32x4_t x[2] = { vld1q_f32 (a + k), vld1q_f32 (a + k + 4) };
            float32x4_t rs[2], rc[2];
            sincosN<2> (x, rs, rc);
            vst1q_f32 (s + k, rs[0]);
            vst1q_f32 (c + k, rc[0]);
            vst1q_f32 (s + k + 4, rs[1]);
            vst1q_f32 (c + k + 4, rc[1]);
        }
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t x[1] = { vld1q_f32 (a + k) };
            float32x4_t rs[1], rc[1];
            sincosN<1> (x, rs, rc);
            vst1q_f32 (s + k, rs[0]);
            vst1q_f32 (c + k, rc[0]);
        }
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
    {
        for (; k + 8 <= n; k += 8)
        {
            const float32x4_t x[2] = { vld1q_f32 (angle + k), vld1q_f32 (angle + k + 4) };
            float32x4_t rs[2], rc[2];
            sincosN<2> (x, rs, rc);
            const float32x4_t m0 = vld1q_f32 (mag + k), m1 = vld1q_f32 (mag + k + 4);
            vst1q_f32 (re + k, vmulq_f32 (m0, rc[0]));
            vst1q_f32 (im + k, vmulq_f32 (m0, rs[0]));
            vst1q_f32 (re + k + 4, vmulq_f32 (m1, rc[1]));
            vst1q_f32 (im + k + 4, vmulq_f32 (m1, rs[1]));
        }
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t x[1] = { vld1q_f32 (angle + k) };
            float32x4_t rs[1], rc[1];
            sincosN<1> (x, rs, rc);
            const float32x4_t m = vld1q_f32 (mag + k);
            vst1q_f32 (re + k, vmulq_f32 (m, rc[0]));
            vst1q_f32 (im + k, vmulq_f32 (m, rs[0]));
        }
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


float wrapPhaseFloat (float x) noexcept
{
    const float ax = std::fabs (x);
    if (! (ax < kReachable))
        return wrapPhase (x);
    const float n = static_cast<float> (static_cast<int> (ax * kInvTwoPiLow));
    // ax - n 2 pi = (ax - n W1) - n W2 - n W3 with every product and the first difference exact; the second difference is split into its sum and rounding error (the exact remainder
    // is a float, so the corrected sum is that remainder to the last bit: wrapPhase () exactly)
    const float r1 = ax - n * kW1, t2 = n * kW2, u3 = n * kW3;
    const float s = r1 - t2, bb = s - r1, e = (r1 - (s - bb)) + (-t2 - bb);
    const float w = s - u3, bw = w - s, e2 = (s - (w - bw)) + (-u3 - bw);
    float r = w + (e2 + e);
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
        const float32x4_t hop = vdupq_n_f32 (static_cast<float> (kHop));
        for (; k + 8 <= n; k += 8)
        {
            const float32x4_t x[2] = { vaddq_f32 (vld1q_f32 (phase + k), vmulq_f32 (vld1q_f32 (step + k), hop)), vaddq_f32 (vld1q_f32 (phase + k + 4), vmulq_f32 (vld1q_f32 (step + k + 4), hop)) };
            float32x4_t out[2];
            wrapN<2> (x, out);
            vst1q_f32 (phase + k, out[0]);
            vst1q_f32 (phase + k + 4, out[1]);
        }
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t x[1] = { vaddq_f32 (vld1q_f32 (phase + k), vmulq_f32 (vld1q_f32 (step + k), hop)) };
            float32x4_t out[1];
            wrapN<1> (x, out);
            vst1q_f32 (phase + k, out[0]);
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
        const float32x4_t hop = vdupq_n_f32 (static_cast<float> (kHop));
        for (; k + 8 <= n; k += 8)
        {
            const float32x4_t x[2] = { vaddq_f32 (vld1q_f32 (phase + k), vmulq_f32 (vld1q_f32 (step + k), hop)), vaddq_f32 (vld1q_f32 (phase + k + 4), vmulq_f32 (vld1q_f32 (step + k + 4), hop)) };
            float32x4_t wrapped[2], rs[2], rc[2];
            wrapN<2> (x, wrapped);
            vst1q_f32 (phase + k, wrapped[0]);
            vst1q_f32 (phase + k + 4, wrapped[1]);
            sincosN<2> (wrapped, rs, rc);
            const float32x4_t m0 = vld1q_f32 (mag + k), m1 = vld1q_f32 (mag + k + 4);
            vst1q_f32 (re + k, vmulq_f32 (m0, rc[0]));
            vst1q_f32 (im + k, vmulq_f32 (m0, rs[0]));
            vst1q_f32 (re + k + 4, vmulq_f32 (m1, rc[1]));
            vst1q_f32 (im + k + 4, vmulq_f32 (m1, rs[1]));
        }
        for (; k + 4 <= n; k += 4)
        {
            const float32x4_t x[1] = { vaddq_f32 (vld1q_f32 (phase + k), vmulq_f32 (vld1q_f32 (step + k), hop)) };
            float32x4_t wrapped[1], rs[1], rc[1];
            wrapN<1> (x, wrapped);
            vst1q_f32 (phase + k, wrapped[0]);
            sincosN<1> (wrapped, rs, rc);
            const float32x4_t m = vld1q_f32 (mag + k);
            vst1q_f32 (re + k, vmulq_f32 (m, rc[0]));
            vst1q_f32 (im + k, vmulq_f32 (m, rs[0]));
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
            gTablesBuilt.fetch_add (1);
            for (int i = 0; i < kFftSize; ++i)
                w[i] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * kPi * i / kFftSize)) * (1.0f / kFftSize); // the exact power of two
        }
    } table;
    return table.w;
}
} // namespace

void gainClamp (float* out, const float* in, float gain, int n, bool neon) noexcept
{
    int i = 0;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        const float32x4_t vg = vdupq_n_f32 (gain), vhi = vdupq_n_f32 (16.0f), vlo = vdupq_n_f32 (-16.0f), vinf = vdupq_n_f32 (std::numeric_limits<float>::infinity());
        for (; i + 4 <= n; i += 4)
        {
            const float32x4_t x = vmulq_f32 (vld1q_f32 (in + i), vg);
            const uint32x4_t finite = vcltq_f32 (vabsq_f32 (x), vinf);   // false for infinity and NaN
            const float32x4_t c = vminq_f32 (vmaxq_f32 (x, vlo), vhi);
            vst1q_f32 (out + i, vreinterpretq_f32_u32 (vandq_u32 (vreinterpretq_u32_f32 (c), finite)));
        }
    }
#else
    (void) neon;
#endif
    for (; i < n; ++i)
    {
        const float x = in[i] * gain;
        out[i] = std::isfinite (x) ? std::max (-16.0f, std::min (16.0f, x)) : 0.0f;
    }
}

void warmTables() noexcept
{
    RealIfft::instance();
    windowTable();
}

void overlapAdd (float* ring, int at, const float* zr, const float* zi, float scale, bool neon, int first) noexcept
{
    const float* w = windowTable();
    int n = first;
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

void lockRun (float* phase, const float* oth, int from, int to, float phase0, float oth0, bool neon) noexcept
{
    int q = from;
#if SB_SPECTRAL_NEON
    if (neon)
    {
        const float32x4_t vp = vdupq_n_f32 (phase0), vo = vdupq_n_f32 (oth0);
        for (; q + 4 <= to + 1; q += 4)
            vst1q_f32 (phase + q, vaddq_f32 (vp, vsubq_f32 (vld1q_f32 (oth + q), vo)));
    }
#else
    (void) neon;
#endif
    for (; q <= to; ++q)
        phase[q] = phase0 + (oth[q] - oth0);
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
    if (neon && n <= 1056)
    {
        // pass 1: one flag byte (1 or 0) per bin, sixteen bins per iteration, nothing moved to the ARM registers; pass 2: the flags are read in order and the index appended without a branch (a
        // compare and branch per bin of the plain loop mispredicts at every peak: a recorded sample has one every third bin or so)
        uint8_t flags[1056];
        const float32x4_t vth = vdupq_n_f32 (threshold);
        const auto mask4 = [&] (int at)
        {
            const float32x4_t c = vld1q_f32 (m + at), p = vld1q_f32 (m + at - 1), q = vld1q_f32 (m + at + 1);
            return vandq_u32 (vandq_u32 (vcgtq_f32 (c, p), vcgeq_f32 (c, q)), vcgtq_f32 (c, vth));
        };
        int at = 1;
        for (; at + 16 <= n - 1; at += 16)
        {
            const uint16x8_t lo = vcombine_u16 (vmovn_u32 (mask4 (at)), vmovn_u32 (mask4 (at + 4)));
            const uint16x8_t hi = vcombine_u16 (vmovn_u32 (mask4 (at + 8)), vmovn_u32 (mask4 (at + 12)));
            vst1q_u8 (flags + at, vshrq_n_u8 (vcombine_u8 (vmovn_u16 (lo), vmovn_u16 (hi)), 7));
        }
        for (int j = at; j < n - 1; ++j)
            flags[j] = (m[j] > m[j - 1] && m[j] >= m[j + 1] && m[j] > threshold) ? 1 : 0;
        for (int j = 1; j < n - 1 && count < maxPeaks; ++j)
        {
            peaks[count] = j;
            count += flags[j];
        }
        return count;
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

void placeAll (const float* mag, const float* freq, const float* theta, float* om, float* of, float* oth, const int* peaks, int numPeaks, double r, double shiftBins, float shiftRad, int n,
               bool neon) noexcept
{
    const float rf = static_cast<float> (r);
    for (int i = 0; i < numPeaks; ++i)
    {
        const int centre = peaks[i];
        const int from = i == 0 ? 1 : (peaks[i - 1] + centre + 1) / 2;
        const int to = i + 1 == numPeaks ? n - 1 : (centre + peaks[i + 1]) / 2;
        const double target = static_cast<double> (centre) * r + shiftBins;
        // lround (target) without the library call for the usual target >= 0: the truncation and the exact remainder (half rounds up, as lround rounds half away from zero)
        int rounded;
        if (target >= 0.0)
        {
            const int t = static_cast<int> (target);
            rounded = t + (target - static_cast<double> (t) >= 0.5 ? 1 : 0);
        }
        else
            rounded = static_cast<int> (std::lround (target));
        const int offset = rounded - centre;
        const int lo = std::max (from, 1 - offset), hi = std::min (to, n - 1 - offset);
        if (hi - lo >= 7)
            placeRegion (mag, freq, theta, om, of, oth, lo, hi, offset, rf, shiftRad, n, neon);
        else
            for (int k = lo; k <= hi; ++k)
            {
                const int j = k + offset;
                if (mag[k] > om[j])
                {
                    om[j] = mag[k];
                    of[j] = freq[k] * rf + shiftRad;
                    oth[j] = theta[k];
                }
            }
    }
}
} // namespace kernels
} // namespace spectral
} // namespace synthyboy
