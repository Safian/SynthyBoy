// SynthyBoy - core/spectral: the kernels of one synthesis frame (the inverse transform of a real signal, the sine and cosine of the bin phases, the phase wrap, the windowed overlap add).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Each kernel has a scalar form (the reference of the vector form) and a NEON form (32 bit and 64 bit ARM: the Key 61 and the XL) that computes, in every lane, the same operations in the
// same order, so the two give the same bits (core/spectral/tests). The scalar forms compare with the library's code (Fft::inverse, std::sin, std::cos, std::fmod) within the tolerance stated
// at each. Nothing allocates. Internal to core/spectral (the header is public only for the tests).
#pragma once

#include <cstdint>

#include "synthyboy/spectral/spectral.h"

#if defined(__ARM_NEON) && ! defined(SB_NO_NEON)
 #define SB_SPECTRAL_NEON 1
#else
 #define SB_SPECTRAL_NEON 0
#endif

namespace synthyboy
{
namespace spectral
{
namespace kernels
{
/** True if this build has the vector forms. */
constexpr bool haveNeon() noexcept { return SB_SPECTRAL_NEON != 0; }

/** The inverse transform of the spectrum of a real signal: the bins 0 .. kFftSize / 2 (xr, xi: kBins values each; xi[0] and xi[kFftSize / 2] must be 0) to the kFftSize real samples
    x[n] = sum_k X[k] e^(+2 pi i k n / kFftSize) over the full Hermitian spectrum (NOT divided by kFftSize: overlapAdd () applies that factor, which is a power of two and so exact), returned as the kFftSize / 2 complex values z[m] = x[2 m] + i x[2 m + 1] (zr, zi).
    One complex transform of half the length instead of the full one that Fft::inverse () runs on the mirrored arrays: about 2.5 times less work; the result differs from Fft::inverse ()
    by rounding (a few 1e-7 of the largest sample). `neon` selects the vector form (ignored without NEON); both forms give the same bits. xr, xi, zr, zi must not overlap. */
class RealIfft
{
public:
    static const RealIfft& instance() noexcept;
    void inverse (const float* xr, const float* xi, float* zr, float* zi, bool neon) const noexcept;

private:
    RealIfft();
    static constexpr int kM = kFftSize / 2;
    void transform (float* re, float* im, bool neon) const noexcept;
    uint16_t rev_[kM];
    float packC_[kM], packS_[kM];       // e^(+2 pi i k / kFftSize), k = 0 .. kM - 1
    float stageC_[kM], stageS_[kM];     // e^(+2 pi i k / (2 h)) for k < h at offset h - 1, h = 1, 2, 4 .. kM / 2
};

/** sin and cos of a (any finite value up to a few thousand radians): the quadrant by rounding a * 2 / pi, the remainder by three part constants, a polynomial on [-pi/4, pi/4]. The error is
    below 2e-7 absolute for |a| < 1000 (std::sin / std::cos agree within that, tested). */
void sincosPoly (float a, float& s, float& c) noexcept;
/** s[k], c[k] = sin, cos of a[k], k < n. `neon`: four at a time (n need not be a multiple of 4), the same bits as sincosPoly. */
void sincosBlock (const float* a, float* s, float* c, int n, bool neon) noexcept;

/** What the voice has always done to a bin phase that left +-100 radians: wrapPi (fmodf (x, 2 pi)), bit for bit, without the library's slow fmodf (the remainder in double precision, where
    n * 2 pi is exact for any phase the voice can reach; the result is the exact remainder fmodf returns). */
float wrapPhase (float x) noexcept;
/** phase[k] = wrapped (phase[k] + step[k] * kHop) where the sum is beyond +-100, else the sum: the phase update of the voice (the old code's loop), bit for bit. */
void advancePhases (float* phase, const float* step, int n) noexcept;

/** ring[(at + n) & (kFftSize - 1)] += x[n] * w[n] * scale for n < kFftSize, where x[2 m] = zr[m], x[2 m + 1] = zi[m] and w is the Hann window times 1 / kFftSize (exact): the
    overlap add of a frame of the voice (the product is the voice's old (x / kFftSize * hann) * scale with the exact power of two moved into the table). `at` is a multiple of 4. The order of the products is the voice's: (x * w) * scale. */
void overlapAdd (float* ring, int at, const float* zr, const float* zi, float scale, bool neon) noexcept;
} // namespace kernels
} // namespace spectral
} // namespace synthyboy
