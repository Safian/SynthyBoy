// SynthyBoy - own engine: oscillator warp modes (phase and sample domain; original definitions).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
/** The warp entries (oscN.warp.type / warp2.type) the engine implements. Entries not listed here leave the oscillator unwarped. */
enum WarpType : int
{
    kWarpNone = 0, kWarpSync, kWarpFormant, kWarpQuantize, kWarpBend, kWarpSqueeze, kWarpPulse,
    kWarpFmA, kWarpFmB, kWarpRmA, kWarpRmB,
    kWarpSyncVar = 11, kWarpBendPlus, kWarpBendMinus, kWarpBendBoth, kWarpAsymPlus, kWarpAsymMinus, kWarpAsymBoth, kWarpFlip, kWarpMirror,
    kWarpOddEven = 24,
    kWarpDistTube = 27, kWarpDistSoftClip, kWarpDistHardClip, kWarpDistDiode1, kWarpDistDiode2, kWarpDistLinearFold, kWarpDistSineFold,
    kWarpDistZeroSquare, kWarpDistAsym, kWarpDistRectify, kWarpDistSineShaper, kWarpDistStomp, kWarpDistTape, kWarpDistSoftSat,
    kWarpFmTzA = 41, kWarpFmTzB, kWarpFmExpA, kWarpFmExpB, kWarpFmLinA, kWarpFmLinB, kWarpPdA, kWarpPdB, kWarpPdSelf, kWarpAmA, kWarpAmB
};

/** True if the mode needs the signal of another oscillator (its index in the cross list: 0 = first other oscillator, 1 = second). */
inline int warpCrossSource (int type) noexcept
{
    switch (type)
    {
        case kWarpFmA: case kWarpRmA: case kWarpFmTzA: case kWarpFmExpA: case kWarpFmLinA: case kWarpPdA: case kWarpAmA: return 0;
        case kWarpFmB: case kWarpRmB: case kWarpFmTzB: case kWarpFmExpB: case kWarpFmLinB: case kWarpPdB: case kWarpAmB: return 1;
        default: return -1;
    }
}

/** How many times the base frequency the warp can create (it lowers the wavetable level so the result does not alias). */
inline float warpBandwidthFactor (int type, float a) noexcept
{
    switch (type)
    {
        case kWarpSync: case kWarpSyncVar: return 1.0f + a * 11.0f;
        case kWarpFormant: return 1.0f + a * 7.0f;
        case kWarpPulse: case kWarpSqueeze: return 1.0f / (1.0f - 0.9f * a);
        case kWarpBend: case kWarpBendPlus: case kWarpBendMinus: case kWarpBendBoth: return 1.0f + 1.5f * std::fabs (a - 0.5f);
        case kWarpFmA: case kWarpFmB: case kWarpFmTzA: case kWarpFmTzB: case kWarpFmExpA: case kWarpFmExpB: case kWarpFmLinA: case kWarpFmLinB:
        case kWarpPdA: case kWarpPdB: case kWarpPdSelf: return 1.0f + 3.0f * a;
        default: return 1.0f;
    }
}

inline float frac (float x) noexcept
{
    return x - std::floor (x);
}

/** Phase domain stage. p = cycle phase 0 .. 1, a = amount 0 .. 1, ph = the phase knob 0 .. 1, m = signal of the other oscillator (-1 .. 1).
    Returns the phase to read the table at (0 .. 1) and sets `window` to a factor applied to the sample. */
inline float warpPhase (int type, float p, float a, float ph, float m, float& window) noexcept
{
    window = 1.0f;
    switch (type)
    {
        case kWarpSync:
            return frac (p * (1.0f + a * 11.0f));
        case kWarpSyncVar:
            return frac (p * (1.0f + a * 11.0f) + ph);
        case kWarpFormant:
        {
            window = 0.5f - 0.5f * std::cos (kTwoPif * p); // raised cosine over the cycle keeps the seam closed
            return frac (p * (1.0f + a * 7.0f) + ph - 0.5f);
        }
        case kWarpBend: case kWarpBendBoth:
            return std::pow (p, std::exp2 (4.0f * (0.5f - a)));
        case kWarpBendPlus:
            return std::pow (p, std::exp2 (-3.0f * a));
        case kWarpBendMinus:
            return std::pow (p, std::exp2 (3.0f * a));
        case kWarpAsymPlus:
        {
            const float k = 0.5f + 0.45f * a; // the first part of the cycle takes k of the table
            return p < 0.5f ? p * 2.0f * k : k + (p - 0.5f) * 2.0f * (1.0f - k);
        }
        case kWarpAsymMinus:
        {
            const float k = 0.5f - 0.45f * a;
            return p < 0.5f ? p * 2.0f * k : k + (p - 0.5f) * 2.0f * (1.0f - k);
        }
        case kWarpAsymBoth:
        {
            const float k = 0.5f + 0.45f * (a - 0.5f) * 2.0f;
            return p < 0.5f ? p * 2.0f * k : k + (p - 0.5f) * 2.0f * (1.0f - k);
        }
        case kWarpFlip:
            return p < 0.5f ? p : lerp (p, 1.0f - p, a);
        case kWarpMirror:
            return p < 0.5f ? p * 2.0f * (1.0f - 0.5f * a) : (1.0f - p) * 2.0f * (1.0f - 0.5f * a);
        case kWarpSqueeze:
            return std::clamp ((p - 0.5f) / (1.0f - 0.95f * a) + 0.5f, 0.0f, 1.0f);
        case kWarpPulse:
            return std::min (p / (1.0f - 0.95f * a), 1.0f);
        case kWarpFmA: case kWarpFmB: case kWarpFmTzA: case kWarpFmTzB:
            return frac (p + m * a * 2.0f);
        case kWarpFmExpA: case kWarpFmExpB:
            return frac (p + std::copysign (std::expm1 (std::fabs (m) * 2.0f) * (1.0f / 6.389f), m) * a * 2.0f);
        case kWarpFmLinA: case kWarpFmLinB:
            return frac (p + m * a * 0.5f);
        case kWarpPdA: case kWarpPdB:
            return frac (p + m * a * 0.25f * (1.0f - p));
        case kWarpPdSelf:
            return frac (p + a * 0.25f * std::sin (kTwoPif * p));
        default:
            return p;
    }
}

/** Sample domain stage (after the table read). `m` is the other oscillator for the ring / amplitude modulation entries. */
inline float warpSample (int type, float y, float a, float m) noexcept
{
    switch (type)
    {
        case kWarpQuantize:
        {
            const float levels = 2.0f + (1.0f - a) * (1.0f - a) * 62.0f;
            return std::round (y * levels) / levels;
        }
        case kWarpRmA: case kWarpRmB:
            return y * lerp (1.0f, m, a);
        case kWarpAmA: case kWarpAmB:
            return y * lerp (1.0f, 0.5f + 0.5f * m, a);
        case kWarpOddEven:
            return y;
        case kWarpDistTube:
        {
            const float g = 1.0f + 6.0f * a;
            return std::tanh (g * y + 0.2f * a) - std::tanh (0.2f * a);
        }
        case kWarpDistSoftClip:
            return std::tanh (y * (1.0f + 8.0f * a));
        case kWarpDistHardClip:
            return std::clamp (y * (1.0f + 8.0f * a), -1.0f, 1.0f);
        case kWarpDistDiode1:
        {
            const float v = y * (1.0f + 6.0f * a);
            return v > 0.0f ? std::tanh (v) : 0.4f * std::tanh (v);
        }
        case kWarpDistDiode2:
        {
            const float v = y * (1.0f + 6.0f * a);
            return v / (1.0f + std::fabs (v));
        }
        case kWarpDistLinearFold:
        {
            float v = y * (1.0f + 6.0f * a);
            v = std::fmod (v + 1.0f, 4.0f);
            if (v < 0.0f)
                v += 4.0f;
            return v < 2.0f ? v - 1.0f : 3.0f - v;
        }
        case kWarpDistSineFold:
            return std::sin (y * (1.0f + 5.0f * a) * kPif * 0.5f);
        case kWarpDistZeroSquare:
            return lerp (y, y >= 0.0f ? 1.0f : -1.0f, a);
        case kWarpDistAsym:
            return std::tanh ((y + 0.5f * a) * (1.0f + 3.0f * a)) - std::tanh (0.5f * a * (1.0f + 3.0f * a));
        case kWarpDistRectify:
            return lerp (y, std::fabs (y) * 2.0f - 0.6366f, a);
        case kWarpDistSineShaper:
            return lerp (y, std::sin (kTwoPif * y), a);
        case kWarpDistStomp:
        {
            const float v = y * (1.0f + 10.0f * a);
            return v / std::sqrt (1.0f + v * v);
        }
        case kWarpDistTape:
        {
            const float v = std::clamp (y * (1.0f + 3.0f * a), -1.5f, 1.5f);
            return v - v * v * v * (4.0f / 27.0f); // cubic soft clip, peak 1 at +-1.5
        }
        case kWarpDistSoftSat:
            return std::atan (y * (1.0f + 6.0f * a)) * (2.0f / kPif);
        default:
            return y;
    }
}
} // namespace own
} // namespace synthyboy
