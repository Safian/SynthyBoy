// SynthyBoy - core tests: the aliasing meter (docs/plans/distortion-antialiasing.md task A1, section 2 M-A and M-B).
// A coherent periodic signal of the fundamental f0 is windowed (4-term Blackman-Harris, side lobes -92 dB), transformed (radix 2) and its power is split into the harmonic bins
// (within excludeBins of h * f0, h * f0 < fs / 2) and every other bin from firstBin to n / 2 (the non-harmonic bins: the aliasing and the noise).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace aliasm
{
struct Result
{
    double aliasDb; // 10 log10 (non-harmonic power / harmonic power)
    double harmDb;  // 10 log10 (harmonic power re the power of a unit sine): a unit sine reads 0 dB
};

/** x: n samples (n a power of two, at least 16), f0 and fs in Hz. */
inline Result measure (const float* x, size_t n, double f0, double fs, double excludeBins = 5.0, size_t firstBin = 6)
{
    const double twoPi = 6.283185307179586;
    std::vector<std::complex<double>> a (n);
    double sumSq = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = twoPi * static_cast<double> (i) / static_cast<double> (n);
        const double w = 0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2.0 * ph) - 0.01168 * std::cos (3.0 * ph);
        sumSq += w * w;
        a[i] = w * static_cast<double> (x[i]);
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl = std::polar (1.0, -twoPi / static_cast<double> (len));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }

    const size_t half = n / 2;
    std::vector<char> harmonic (half, 0);
    for (int h = 1; static_cast<double> (h) * f0 < fs * 0.5; ++h)
    {
        const double centre = static_cast<double> (h) * f0 * static_cast<double> (n) / fs;
        const double lo = std::max (0.0, std::ceil (centre - excludeBins)), hi = std::min (static_cast<double> (half) - 1.0, std::floor (centre + excludeBins));
        for (double k = lo; k <= hi; k += 1.0)
            harmonic[static_cast<size_t> (k)] = 1;
    }
    double pHarm = 0.0, pNon = 0.0;
    for (size_t k = 0; k < half; ++k)
    {
        const double p = std::norm (a[k]);
        if (harmonic[k] != 0)
            pHarm += p;
        else if (k >= firstBin)
            pNon += p;
    }
    // A unit sine on a bin: the energy of its positive-frequency lobe is (1 / 2)^2 * n * sum (w^2) (Parseval), which is the 0 dB reference.
    const double ref = 0.25 * static_cast<double> (n) * sumSq;
    Result r;
    r.aliasDb = 10.0 * std::log10 (std::max (pNon, 1.0e-300) / std::max (pHarm, 1.0e-300));
    r.harmDb = 10.0 * std::log10 (std::max (pHarm, 1.0e-300) / ref);
    return r;
}
} // namespace aliasm
