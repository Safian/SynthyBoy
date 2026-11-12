// SynthyBoy - own engine: LFO shapes and random LFOs (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/util.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace own
{
/** The shape of a cycle position 0 .. 1 (bipolar -1 .. 1). Shapes: the first kNumLfoShapes entries of lfoN.shape; `stepSeed` selects the value of Random Steps. */
inline float lfoShape (int shape, float p, uint32_t stepSeed) noexcept
{
    switch (shape)
    {
        case 0: return std::sin (kTwoPif * p);
        case 1: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;           // triangle starting at -1
        case 2: return 2.0f * p - 1.0f;                                         // saw up
        case 3: return 1.0f - 2.0f * p;                                         // saw down
        case 4: return p < 0.5f ? 1.0f : -1.0f;                                 // square
        case 5: return p < 0.25f ? 1.0f : -1.0f;                                // pulse 25 %
        case 6: return std::floor (p * 8.0f) / 3.5f - 1.0f;                     // 8 steps up
        case 7: return 1.0f - std::floor (p * 8.0f) / 3.5f;                     // 8 steps down
        case 8:
        {
            Rng r;
            r.seed (stepSeed * 977u + static_cast<uint32_t> (p * 8.0f));
            return r.bi();
        }
        case 9: return 2.0f * ((std::exp (4.0f * p) - 1.0f) / (std::exp (4.0f) - 1.0f)) - 1.0f; // exp up
        case 10: return 1.0f - 2.0f * ((std::exp (4.0f * p) - 1.0f) / (std::exp (4.0f) - 1.0f)); // exp down
        case 11: return std::sin (kPif * p) * 2.0f - 1.0f;                      // half sine (rectified), spans -1 .. 1
        default: return 0.0f;
    }
}

/** The random LFO styles of rlfoN.style, one value per control period. */
class RandomLfo
{
public:
    void seed (uint32_t s) noexcept
    {
        rng.seed (s);
        prev = rng.bi();
        next = rng.bi();
        lx = 0.1f + 0.1f * rng.uni();
        ly = 0.0f;
        lz = 0.0f;
        phase = 0.0f;
        held = prev;
        grad0 = rng.bi();
        grad1 = rng.bi();
    }

    /** Advances `cycles` (rate * time) and returns -1 .. 1. */
    float advance (int style, float cycles) noexcept
    {
        phase += cycles;
        int wraps = 0;
        while (phase >= 1.0f && wraps < 64)
        {
            phase -= 1.0f;
            ++wraps;
            prev = next;
            next = rng.bi();
            grad0 = grad1;
            grad1 = rng.bi();
        }
        if (phase >= 1.0f)
            phase = 0.0f;
        switch (style)
        {
            case 0: // Perlin: gradient noise with the quintic fade
            {
                const float t = phase;
                const float f = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
                const float a = grad0 * t, b = grad1 * (t - 1.0f);
                return std::clamp (2.0f * lerp (a, b, f), -1.0f, 1.0f);
            }
            case 1: // sample and hold
                return prev;
            case 2: // sine interpolation between random values
                return lerp (prev, next, 0.5f - 0.5f * std::cos (kPif * phase));
            case 3: // Lorenz attractor
            case 4: // Rossler attractor
            {
                const float dt = std::min (0.02f * std::max (cycles, 0.001f) * 50.0f, 0.02f);
                const int steps = 4;
                for (int i = 0; i < steps; ++i)
                {
                    if (style == 3)
                    {
                        const float dx = 10.0f * (ly - lx), dy = lx * (28.0f - lz) - ly, dz = lx * ly - (8.0f / 3.0f) * lz;
                        lx += dx * dt / steps * 8.0f;
                        ly += dy * dt / steps * 8.0f;
                        lz += dz * dt / steps * 8.0f;
                    }
                    else
                    {
                        const float dx = -ly - lz, dy = lx + 0.2f * ly, dz = 0.2f + lz * (lx - 5.7f);
                        lx += dx * dt / steps * 8.0f;
                        ly += dy * dt / steps * 8.0f;
                        lz += dz * dt / steps * 8.0f;
                    }
                }
                if (!std::isfinite (lx) || !std::isfinite (ly) || !std::isfinite (lz))
                {
                    lx = 0.1f;
                    ly = lz = 0.0f;
                }
                return std::clamp (lx * (style == 3 ? 0.05f : 0.09f), -1.0f, 1.0f);
            }
            default:
                return 0.0f;
        }
    }

private:
    Rng rng;
    float prev = 0, next = 0, phase = 0, held = 0, grad0 = 0, grad1 = 0, lx = 0.1f, ly = 0, lz = 0;
};
} // namespace own
} // namespace synthyboy
