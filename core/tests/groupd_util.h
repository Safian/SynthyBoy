// SynthyBoy - tests of GROUPD (WP-14 / WP-15 / WP-16): the helpers the pure and the engine tests share.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace groupd
{
constexpr double kPi = 3.14159265358979323846;

/** Amplitude of the component at `hz` in x[from, from + n) (Hann window, the exact frequency, so no leakage into the estimate): Goertzel-like single DFT bin. */
inline double amplitudeAt (const std::vector<float>& x, size_t from, size_t n, double hz, double sampleRate)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n && from + i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n));
        const double ph = 2.0 * kPi * hz * static_cast<double> (i) / sampleRate;
        re += w * x[from + i] * std::cos (ph);
        im -= w * x[from + i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / std::max (wsum, 1.0e-12);
}

/** The first `count` harmonic amplitudes (in dB) of a periodic signal with fundamental `f0`. */
inline std::vector<double> harmonicDb (const std::vector<float>& x, size_t from, size_t n, double f0, double sampleRate, int count)
{
    std::vector<double> out;
    for (int h = 1; h <= count; ++h)
        out.push_back (20.0 * std::log10 (std::max (amplitudeAt (x, from, n, f0 * h, sampleRate), 1.0e-9)));
    return out;
}

/** Largest difference in dB between two harmonic lists over the harmonics of `a` that are within `floorDb` of its loudest one. */
inline double worstHarmonicDifference (const std::vector<double>& a, const std::vector<double>& b, double floorDb = 40.0)
{
    double loudest = -1000.0, worst = 0.0;
    for (double v : a)
        loudest = std::max (loudest, v);
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] > loudest - floorDb)
            worst = std::max (worst, std::fabs (a[i] - b[i]));
    return worst;
}

/** A tone of `partials` harmonics of 1/h amplitude (a band limited saw), peak 0.8. */
inline std::vector<float> sawTone (double f0, double seconds, double sampleRate, int partials = 20)
{
    std::vector<float> v (static_cast<size_t> (seconds * sampleRate), 0.0f);
    for (int h = 1; h <= partials; ++h)
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += (1.0f / static_cast<float> (h)) * static_cast<float> (std::sin (2.0 * kPi * f0 * h * static_cast<double> (i) / sampleRate));
    float p = 0.0f;
    for (float s : v)
        p = std::max (p, std::fabs (s));
    for (float& s : v)
        s *= 0.8f / p;
    return v;
}

inline double crossings (const std::vector<float>& x, size_t from, size_t to, double sampleRate)
{
    int n = 0;
    for (size_t i = std::max<size_t> (from, 1); i < to && i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            ++n;
    return n / (static_cast<double> (to - from) / sampleRate);
}
} // namespace groupd
