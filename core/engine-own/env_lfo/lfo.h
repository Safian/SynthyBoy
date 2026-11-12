// SynthyBoy - own engine: LFO shapes and random LFOs (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
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
        case 0: return -std::cos (kTwoPif * p);                                   // starts at the bottom
        case 1: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;           // triangle starting at -1
        case 2: return 2.0f * p - 1.0f;                                         // saw up
        case 3: return 1.0f - 2.0f * p;                                         // saw down
        case 4: return p < 0.5f ? 1.0f : -1.0f;                                 // square
        case 5: return p < 0.25f ? 1.0f : -1.0f;                                // pulse 25 %
        case 6: return 2.0f * std::floor (p * 4.0f) / 3.0f - 1.0f;             // 4 steps up
        case 7: return 1.0f - 2.0f * std::floor (p * 4.0f) / 3.0f;             // 4 steps down
        case 8: // eight fixed levels over eighths of a cycle (docs/engine-spec/03-envelopes-lfos.md 3.2)
        {
            static const float kLevels[8] = { 0.2f, 0.9f, 0.5f, 1.0f, 0.1f, 0.7f, 0.35f, 0.8f };
            (void) stepSeed;
            return 2.0f * kLevels[std::min (static_cast<int> (p * 8.0f), 7)] - 1.0f;
        }
        case 9: return 2.0f * p * p * p - 1.0f;                        // exp up
        case 10: return 2.0f * (1.0f - p) * (1.0f - p) * (1.0f - p) - 1.0f;                // exp down
        case 11: return std::sin (kPif * p) * 2.0f - 1.0f;                      // half sine (rectified), spans -1 .. 1
        default: return 0.0f;
    }
}

/** The rate in Hz of an LFO or random LFO from its sync mode (docs/engine-spec/03-envelopes-lfos.md 2): 0 free, 1 tempo, 2 dotted, 3 triplet, 4 key follow. */
inline float lfoRateHz (int sync, float rateHz, int division, double bpm, float concertA, float keyNote, float keyTranspose, float keyTuneCents) noexcept
{
    static const float kBeats[13] = { 0.0f, 128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
    if (sync == 0)
        return rateHz;
    if (sync == 4)
        return concertA * std::exp2 ((keyNote + keyTranspose + keyTuneCents * 0.01f - 69.0f) * (1.0f / 12.0f));
    const float b = kBeats[std::clamp (division, 0, 12)];
    if (!(b > 0.0f))
        return 0.0f; // Freeze
    const float bps = static_cast<float> (std::max (bpm, 0.0) / 60.0);
    return bps / b * (sync == 2 ? 1.0f / 1.5f : (sync == 3 ? 1.5f : 1.0f));
}

/** Swing (document 03, 3.3): the cycle is four pairs of slots; inside a pair the boundary between the two halves moves from 1/2 to 1/2 + sw / 4. */
inline float lfoSwing (float u, float sw) noexcept
{
    if (!(sw > 0.0f))
        return u;
    const float b = 0.5f + 0.25f * std::min(sw, 1.0f);
    const float x = u * 4.0f;
    const float k = std::floor (x);
    const float w = x - k;
    const float w2 = w < b ? 0.5f * w / b : 0.5f + 0.5f * (w - b) / (1.0f - b);
    return (k + w2) * 0.25f;
}

/** The running offset of an LFO in its sync type (document 03, 3.4): 0 retrigger, 1 free-run (the clock decides, see Voice), 2 envelope,
    3 sustain envelope, 4 loop at point, 5 loop and hold. `position()` is the position on the cycle that the shape is read at (before swing and direction). */
struct LfoCore
{
    float offset = 0.0f, age = 0.0f;

    void restart() noexcept { offset = 0.0f; age = 0.0f; }

    /** `knob` is the phase knob in cycles (0 .. 1), `held` is the key (or pedal) state. */
    void advance (int syncType, float knob, float rateHz, float delaySeconds, float dt, bool held) noexcept
    {
        age += dt;
        if (age < delaySeconds)
            return; // the offset does not advance during the delay
        const float inc = rateHz * dt;
        switch (syncType)
        {
            case 2:
                offset = std::min (offset + inc, 1.0f);
                break;
            case 3:
                if (held)
                {
                    if (offset < knob)
                        offset = std::min (offset + inc, knob); // waits at the sustain point
                }
                else
                    offset = std::min (offset + inc, 1.0f);
                break;
            case 4:
                offset += inc;
                if (offset >= 1.0f)
                    offset = std::min (offset - 1.0f + knob, 1.0f);
                break;
            case 5:
                if (held)
                {
                    offset += inc;
                    if (knob > 1.0e-4f)
                    {
                        while (offset >= knob)
                            offset -= knob;
                    }
                }
                else
                    offset = std::min (offset + inc, 1.0f);
                break;
            default:
                offset += inc;
                offset -= std::floor (offset);
                break;
        }
    }

    float position (int syncType, float knob) const noexcept
    {
        switch (syncType)
        {
            case 2: return std::min (offset + knob, 1.0f);
            case 3: case 4: case 5: return std::min (offset, 1.0f);
            default: { const float u = offset + knob; return u - std::floor (u); }
        }
    }
};

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
