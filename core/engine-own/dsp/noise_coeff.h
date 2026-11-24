// SynthyBoy - own engine: the per period coefficients of the noise source (noise.pitch and noise.filter, docs/engine-spec/02 section 10) and their cache (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "util.h"

namespace synthyboy
{
namespace own
{
struct NoiseCoeffs
{
    float pitchGain = 1.0f, lpC = 1.0f, fC = 0.0f;
};

/** The generator played at another rate (down: the band shrinks, up: white but quieter) and the noise.filter pole: six library calls that depend on the pitch, the filter
    amount and the sample rate only. */
inline NoiseCoeffs computeNoiseCoeffs (float pitchSemis, float filterAmount, double sampleRate) noexcept
{
    NoiseCoeffs c;
    const float rate = std::exp2 (pitchSemis * (1.0f / 12.0f));
    const float oct = std::log2 (std::max (rate, 1.0e-3f));
    c.pitchGain = oct <= 0.0f ? 1.0f : (oct <= 1.0f ? lerp (1.0f, 0.757f, oct) : (oct <= 2.0f ? lerp (0.757f, 0.628f, oct - 1.0f) : 0.628f * std::pow (0.83f, oct - 2.0f)));
    c.lpC = rate < 0.999f ? 1.0f - std::exp (-kTwoPif * std::min (0.45f, 0.4f * rate) * 1.0f) : 1.0f;
    const float fa = filterAmount;
    const float fHz = fa < 0.0f ? 20000.0f * std::pow (0.002f, -fa) : 20.0f * std::pow (800.0f, std::max (fa, 0.0f));
    c.fC = std::min (0.99f, 1.0f - std::exp (-kTwoPif * std::min (fHz, 0.45f * static_cast<float> (sampleRate)) / static_cast<float> (sampleRate)));
    return c;
}

/** The last answer of computeNoiseCoeffs () for its three inputs (compared by bits): the same values as computing them again. */
class NoiseCoeffCache
{
public:
    const NoiseCoeffs& get (float pitchSemis, float filterAmount, double sampleRate) noexcept
    {
        if (!ok || !same (pitch, pitchSemis) || !same (filter, filterAmount) || rate != sampleRate)
        {
            coeffs = computeNoiseCoeffs (pitchSemis, filterAmount, sampleRate);
            pitch = pitchSemis;
            filter = filterAmount;
            rate = sampleRate;
            ok = true;
        }
        return coeffs;
    }

private:
    static bool same (float a, float b) noexcept { uint32_t x, y; std::memcpy (&x, &a, 4); std::memcpy (&y, &b, 4); return x == y; }
    bool ok = false;
    float pitch = 0.0f, filter = 0.0f;
    double rate = 0.0;
    NoiseCoeffs coeffs;
};
} // namespace own
} // namespace synthyboy
