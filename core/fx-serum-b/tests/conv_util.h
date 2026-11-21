// SynthyBoy - core/fx-serum-b tests: helpers of the Convolve optimisation tests (docs/plans/reverb-convolve.md section 4.0): a direct double precision convolution as the reference and
// deterministic impulses (decaying noise, a resonant band-limited one).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace convutil
{
/** y[t] = sum_m h[m] x[t - offset - m] in double precision; the result has x.size () + h.size () - 1 + offset samples. */
inline std::vector<double> directConvolve (const std::vector<float>& x, const std::vector<float>& h, int offset)
{
    std::vector<double> y;
    if (x.empty() || h.empty())
        return y;
    y.assign (x.size() + h.size() - 1 + static_cast<size_t> (offset), 0.0);
    for (size_t m = 0; m < h.size(); ++m)
        for (size_t n = 0; n < x.size(); ++n)
            y[n + m + static_cast<size_t> (offset)] += static_cast<double> (h[m]) * static_cast<double> (x[n]);
    return y;
}

/** Uniform noise in -1 .. 1 under the envelope exp (-i / tauSamples); the first sample is the envelope itself (1), a clear start of the impulse. Deterministic in `seed`. */
inline std::vector<float> decayingNoise (int frames, double tauSamples, uint32_t seed)
{
    std::vector<float> x (static_cast<size_t> (std::max (frames, 0)));
    uint32_t state = seed * 2654435761u + 977u;
    for (size_t i = 0; i < x.size(); ++i)
    {
        state = state * 1664525u + 1013904223u;
        const double white = static_cast<double> (state) * (2.0 / 4294967296.0) - 1.0;
        const double env = std::exp (-static_cast<double> (i) / tauSamples);
        x[i] = static_cast<float> (i == 0 ? env : white * env);
    }
    return x;
}

/** Eight damped sinusoids at 220 .. 1800 Hz (log spaced, decay times 0.15 .. 0.5 s, phases from `seed`), peak normalised to 0.5: a smooth, band-limited impulse for click tests. */
inline std::vector<float> resonantImpulse (int frames, double sr, uint32_t seed)
{
    std::vector<float> x (static_cast<size_t> (std::max (frames, 0)), 0.0f);
    uint32_t state = seed * 2654435761u + 31u;
    double peak = 0.0;
    std::vector<double> acc (x.size(), 0.0);
    for (int k = 0; k < 8; ++k)
    {
        state = state * 1664525u + 1013904223u;
        const double phase = static_cast<double> (state) * (6.283185307179586 / 4294967296.0);
        const double hz = 220.0 * std::pow (1800.0 / 220.0, static_cast<double> (k) / 7.0);
        const double tau = (0.15 + 0.05 * static_cast<double> (k)) * sr;
        for (size_t i = 0; i < acc.size(); ++i)
            acc[i] += std::exp (-static_cast<double> (i) / tau) * std::sin (6.283185307179586 * hz * static_cast<double> (i) / sr + phase);
    }
    for (const double v : acc)
        peak = std::max (peak, std::fabs (v));
    const double scale = peak > 0.0 ? 0.5 / peak : 0.0;
    for (size_t i = 0; i < acc.size(); ++i)
        x[i] = static_cast<float> (acc[i] * scale);
    return x;
}
} // namespace convutil
