// SynthyBoy - MPC shell: map FFT magnitudes to 32 logarithmic bands for the live spectrum display.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace synthyboy_mpc
{

constexpr int kSpectrumBands = 32;
constexpr double kSpectrumLowHz = 30.0;
constexpr double kSpectrumHighHz = 18000.0;
constexpr float kSpectrumFloorDb = -60.0f;
constexpr int kSpectrumSteps = 64;

/** Edge k of the bands, k = 0 .. 32. k outside 0 .. 32 is clamped. */
inline double spectrumBandEdgeHz (int k) noexcept
{
    if (k < 0)
        k = 0;
    else if (k > 32)
        k = 32;
    return kSpectrumLowHz * std::pow (kSpectrumHighHz / kSpectrumLowHz, static_cast<double> (k) / 32.0);
}

/** 20 * log10(mag); returns -180.0f for zero, negative, NaN, or values <= 1e-9. */
inline float spectrumMagToDb (float mag) noexcept
{
    if (!(mag > 1e-9f)) // zero, negative and NaN
        return -180.0f;
    return 20.0f * std::log10 (static_cast<double> (mag));
}

/** Linear in dB from the floor to 0 dBFS. NaN gives 0. */
inline float spectrumLevelFromDb (float db) noexcept
{
    if (std::isnan (db))
        return 0.0f;
    if (db <= kSpectrumFloorDb)
        return 0.0f;
    if (db >= 0.0f)
        return 1.0f;
    return (db - kSpectrumFloorDb) / (-kSpectrumFloorDb);
}

/** Rounds to the nearest multiple of 1/kSpectrumSteps (half rounds up). NaN, <= 0 give 0; >= 1 gives 1. */
inline float spectrumQuantize (float level) noexcept
{
    if (std::isnan (level) || level <= 0.0f)
        return 0.0f;
    if (level >= 1.0f)
        return 1.0f;
    return std::floor (level * kSpectrumSteps + 0.5f) / static_cast<float> (kSpectrumSteps);
}

/** Map bins magnitudes to 32 band dB values. */
inline void spectrumBandDb (const float* mag, int bins, double sampleRate, float* dbOut) noexcept
{
    const double df = sampleRate / (2.0 * (bins - 1));
    const double nyquist = sampleRate / 2.0;

    for (int k = 0; k < kSpectrumBands; ++k)
    {
        double lo = spectrumBandEdgeHz (k);
        double hi = spectrumBandEdgeHz (k + 1);

        // Band starts at or above Nyquist: magnitude is 0
        if (lo >= nyquist)
        {
            dbOut[static_cast<size_t> (k)] = spectrumMagToDb (0.0f);
            continue;
        }

        int i0 = static_cast<int> (std::ceil (lo / df));
        int i1 = static_cast<int> (std::ceil (hi / df)) - 1;
        i0 = std::max (i0, 1);
        i1 = std::min (i1, bins - 1);

        float magnitude;
        if (i1 >= i0)
        {
            // Maximum of mag[i0 .. i1]
            magnitude = mag[static_cast<size_t> (i0)];
            for (int i = i0 + 1; i <= i1; ++i)
                magnitude = std::max (magnitude, mag[static_cast<size_t> (i)]);
        }
        else
        {
            // Band narrower than one bin: linear interpolation at geometric centre
            double c = std::sqrt (lo * hi);
            double pos = c / df;
            int i = static_cast<int> (pos);
            if (i < 0)
                i = 0;
            if (i > bins - 2)
                i = bins - 2;
            double frac = pos - i;
            magnitude = static_cast<float> (mag[static_cast<size_t> (i)] * (1.0 - frac)
                                            + mag[static_cast<size_t> (i + 1)] * frac);
        }

        dbOut[static_cast<size_t> (k)] = spectrumMagToDb (magnitude);
    }
}

} // namespace synthyboy_mpc
