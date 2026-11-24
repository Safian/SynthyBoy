// SynthyBoy - core tests: small signal analysis helpers (FFT, spectra, levels) and a parameter-by-key shortcut.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "test_runner.h"
#include "test_util.h"

namespace sbtest
{
constexpr double kTestPi = 3.14159265358979323846;

/** In-place radix-2 FFT (size a power of two). */
inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
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
        const double angle = -2.0 * kTestPi / static_cast<double> (len);
        const std::complex<double> wlen (std::cos (angle), std::sin (angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

enum class Window { hann, blackmanHarris };

/** Power spectrum (bins 0 .. fftSize / 2) of x[from, from + n) with a Hann (default) or 4-term Blackman-Harris window (sidelobes
    below -92 dB, main lobe +-4 bins of the unpadded length), zero padded to fftSize. */
inline std::vector<double> powerSpectrum (const std::vector<float>& x, size_t from, size_t n, size_t fftSize, Window window = Window::hann)
{
    std::vector<std::complex<double>> a (fftSize, std::complex<double> (0.0, 0.0));
    for (size_t i = 0; i < n && from + i < x.size() && i < fftSize; ++i)
    {
        const double t = 2.0 * kTestPi * static_cast<double> (i) / static_cast<double> (n);
        const double w = window == Window::hann ? 0.5 - 0.5 * std::cos (t)
                                                : 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        a[i] = static_cast<double> (x[from + i]) * w;
    }
    fft (a);
    std::vector<double> p (fftSize / 2 + 1);
    for (size_t k = 0; k < p.size(); ++k)
        p[k] = std::norm (a[k]);
    return p;
}

/** Frequency of the strongest spectral peak within +-6 % of guess, refined by parabolic interpolation of the log power. */
inline double peakFrequency (const std::vector<double>& power, size_t fftSize, double sampleRate, double guess)
{
    const double binHz = sampleRate / static_cast<double> (fftSize);
    const size_t lo = static_cast<size_t> (std::max (2.0, guess * 0.94 / binHz));
    const size_t hi = std::min (power.size() - 2, static_cast<size_t> (guess * 1.06 / binHz) + 1);
    size_t best = lo;
    for (size_t k = lo; k <= hi; ++k)
        if (power[k] > power[best])
            best = k;
    const double a = std::log (power[best - 1] + 1e-300), b = std::log (power[best] + 1e-300), c = std::log (power[best + 1] + 1e-300);
    const double denom = a - 2.0 * b + c;
    const double delta = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
    return (static_cast<double>(best) + delta) * binHz;
}

inline double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min (to, x.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double> (x[i]) * x[i];
    return to > from ? std::sqrt (sum / static_cast<double> (to - from)) : 0.0;
}

inline double toDb (double ratio)
{
    return 20.0 * std::log10 (std::max (ratio, 1e-12));
}

/** Spectral centroid in Hz of x[from, from + n). */
inline double centroidOf (const std::vector<float>& x, size_t from, size_t n, double sampleRate, size_t fftSize = 8192)
{
    const auto p = powerSpectrum (x, from, n, fftSize);
    double weighted = 0.0, total = 0.0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        const double amp = std::sqrt (p[k]);
        weighted += amp * static_cast<double> (k) * sampleRate / static_cast<double> (fftSize);
        total += amp;
    }
    return total > 0.0 ? weighted / total : 0.0;
}

/** Sets a parameter by its key (reports the key if it does not exist). */
inline void setByKey (synthyboy::IEngine& e, const char* key, float value)
{
    const auto* info = synthyboy::findParamByKey (key);
    if (! CHECK (info != nullptr))
    {
        std::printf ("    unknown parameter %s\n", key);
        return;
    }
    e.setParam (info->id, value);
}

/** The plain value of choice `name` of a stepped parameter (-1 if there is none). */
inline float choiceValue (const char* key, const char* name)
{
    const auto* info = synthyboy::findParamByKey (key);
    for (size_t i = 0; info != nullptr && i < synthyboy::choiceCount (*info); ++i)
        if (std::string (synthyboy::choiceName (*info, i)) == name)
            return static_cast<float> (i);
    return -1.0f;
}

/** Choice value of a modulation source / destination for the matrix, by key. */
inline float sourceChoice (const char* sourceKey)
{
    for (size_t k = 0; k < synthyboy::modSourceCount(); ++k)
        if (std::string (synthyboy::modSourceTable()[k].key) == sourceKey)
            return static_cast<float> (k + 1);
    return 0.0f;
}
} // namespace sbtest
