// SynthyBoy - own engine: small DSP helpers shared by all modules (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace synthyboy
{
namespace own
{
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr float kTwoPif = 6.28318530717958647692f;
constexpr float kPif = 3.14159265358979323846f;

/** Length of a control period in samples: envelopes, LFOs and the modulation matrix advance in steps of this size on an absolute sample grid
    (independent of how the host splits the stream into blocks); audio rate values are interpolated linearly across the step. */
constexpr int kControlPeriod = 32;

/** Flushes values that would become denormal in a recursion. */
inline float flushDenormal (float v) noexcept
{
    return std::fabs (v) < 1.0e-18f ? 0.0f : v;
}

inline float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

inline float semitonesToRatio (float st) noexcept
{
    return std::exp2 (st * (1.0f / 12.0f));
}

inline float clamp01 (float v) noexcept
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** Equal power pan, pan -1 .. 1; centre gives bit identical gains for both sides. */
inline void panGains (float pan, float& left, float& right) noexcept
{
    const float p = std::clamp (pan, -1.0f, 1.0f);
    left = std::sqrt (0.5f * (1.0f - p));
    right = std::sqrt (0.5f * (1.0f + p));
}

/** Curve shape of the envelopes and the matrix: x in 0 .. 1, power 0 = linear, negative = fast start (convex), positive = slow start. */
inline float powerShape (float x, float power) noexcept
{
    if (std::fabs (power) < 1.0e-4f)
        return x;
    return std::expm1 (power * x) / std::expm1 (power);
}

/** Deterministic xorshift generator (per voice and per engine, seeded from counters: renders are reproducible). */
struct Rng
{
    uint32_t s = 0x9E3779B9u;

    void seed (uint32_t v) noexcept { s = v * 2654435761u + 0x7F4A7C15u; if (s == 0) s = 1; next(); next(); }
    uint32_t next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    /** 0 .. 1 */
    float uni() noexcept { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }
    /** -1 .. 1 */
    float bi() noexcept { return uni() * 2.0f - 1.0f; }
};

/** Linear ramp to the target over a fixed number of samples (same behaviour as juce::SmoothedValue, linear). */
class LinearSmoother
{
public:
    void reset (double sampleRate, double seconds) noexcept
    {
        steps = static_cast<int> (std::floor (seconds * sampleRate));
        countdown = 0;
    }

    void setCurrentAndTarget (float v) noexcept
    {
        current = target = v;
        countdown = 0;
    }

    void setTarget (float t) noexcept
    {
        if (t == target)
            return;
        if (steps <= 0)
        {
            setCurrentAndTarget (t);
            return;
        }
        target = t;
        countdown = steps;
        step = (target - current) / static_cast<float> (countdown);
    }

    float next() noexcept
    {
        if (countdown <= 0)
            return target;
        if (--countdown > 0)
            current += step;
        else
            current = target;
        return current;
    }

private:
    float current = 1.0f, target = 1.0f, step = 0.0f;
    int steps = 0, countdown = 0;
};
} // namespace own
} // namespace synthyboy
