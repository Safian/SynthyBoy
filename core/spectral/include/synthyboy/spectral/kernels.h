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
    uint16_t swapI_[kM / 2], swapJ_[kM / 2];   // the index pairs of the bit reversal, i < j
    int numSwaps_ = 0;
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
/** re[k] = mag[k] cos (angle[k]), im[k] = mag[k] sin (angle[k]) (sincosPoly (); the vector form gives the same bits). */
void spectrumFromAngles (const float* angle, const float* mag, float* re, float* im, int n, bool neon) noexcept;
/** advancePhases () and spectrumFromAngles () in one pass over the bins (the data stay in registers; the same bits as the two calls): phase[k] advanced and wrapped, then re / im of mag[k] at that
    phase. */
void advanceToSpectrum (float* phase, const float* step, const float* mag, float* re, float* im, int n, bool neon) noexcept;
/** wrapPhase () in single precision: the remainder by three part constants of 2 pi (n * 2 pi is exact in float for n < 2^16), within 3e-7 radians of wrapPhase () (the rounding of the last
    subtraction). For |x| below 1e5; beyond that (and for a NaN) wrapPhase () runs. The vector form of advancePhases () does exactly these operations below 1e5 (no phase of the voice goes beyond
    5.2e4: the step is at most pi * 32 * kHop and the sum is wrapped every hop); above that it returns some finite value. */
float wrapPhaseFloat (float x) noexcept;
/** phase[k] = wrapped (phase[k] + step[k] * kHop) where the sum is beyond +-100, else the sum: the phase update of the voice. Wrapped by wrapPhaseFloat () (`neon`: four at a time, the same bits). */
void advancePhases (float* phase, const float* step, int n, bool neon) noexcept;

/** ring[(at + n) & (kFftSize - 1)] += x[n] * w[n] * scale for n < kFftSize, where x[2 m] = zr[m], x[2 m + 1] = zi[m] and w is the Hann window times 1 / kFftSize (exact): the
    overlap add of a frame of the voice (the product is the voice's old (x / kFftSize * hann) * scale with the exact power of two moved into the table). `at` is a multiple of 4. The order of the products is the voice's: (x * w) * scale. */
void overlapAdd (float* ring, int at, const float* zr, const float* zi, float scale, bool neon) noexcept;

// ---- the per bin loops of shapeFrame () (the same values as the scalar loops of the reference, bit for bit) ----------------------------------------------

/** out[k] = a[k] + (b[k] - a[k]) * t, k < n. */
void lerpBins (float* out, const float* a, const float* b, float t, int n, bool neon) noexcept;
/** a[k] *= g[k], k < n. */
void mulBins (float* a, const float* g, int n, bool neon) noexcept;
/** The largest of a[k], k < n (0 for n <= 0, as the loop of the voice starts from 0). */
float maxBins (const float* a, int n, bool neon) noexcept;
/** The peaks of a magnitude spectrum: the bins k in 1 .. n - 2 with m[k] > m[k - 1], m[k] >= m[k + 1] and m[k] > 1e-5 top, in order, at most maxPeaks of them. Returns their number. */
int findPeaks (const float* m, int n, float top, int* peaks, int maxPeaks, bool neon) noexcept;
/** The bins from .. to (inclusive) move by `offset` to the output bins (kept to 1 .. n - 1): where the magnitude is above what the output bin holds it replaces it, with the frequency
    scaled by r (+ shiftRad) and the phase: the placing of one peak region in shapeFrame (). */
void placeRegion (const float* mag, const float* freq, const float* theta, float* om, float* of, float* oth, int from, int to, int offset, float r, float shiftRad, int n, bool neon) noexcept;
/** The placing of every peak region of a frame (the loop of shapeFrame () over the peaks): region i runs from the middle between the previous peak and this one to the middle between this one and
    the next (the first from bin 1, the last to bin n - 1) and moves by lround (peak * r + shiftBins) - peak bins (placeRegion ()). The rounding is lround's (half away from zero) without the
    library call. */
void placeAll (const float* mag, const float* freq, const float* theta, float* om, float* of, float* oth, const int* peaks, int numPeaks, double r, double shiftBins, float shiftRad, int n,
               bool neon) noexcept;
} // namespace kernels
} // namespace spectral
} // namespace synthyboy
