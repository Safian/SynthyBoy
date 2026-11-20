// SynthyBoy - own engine: the dispatcher of the ADAA kernels of the sample-domain oscillator warps (docs/plans/distortion-antialiasing.md task O2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "warp.h"
#include "warp_adaa_clip.h"
#include "warp_adaa_misc.h"
#include "warp_adaa_tanh.h"

namespace synthyboy
{
namespace own
{
namespace warpadaa
{
/** One sample of a sample-domain warp (warpIsSampleDomain) with the first-order ADAA of its kernel; a type that no kernel handles gets the plain law of warpSample (the modulation
    warps need the other oscillator and never come here). The state `s` belongs to one oscillator lane and one warp slot and is zeroed when the lane starts. */
inline float step (int type, State& s, float y, float a, bool fast) noexcept
{
    float out;
    if (type == kWarpQuantize)
        return stepQuantize (s, y, 2.0f + (1.0f - a) * (1.0f - a) * 62.0f);
    if (stepClip (type, s, y, a, fast, out) || stepTanh (type, s, y, a, fast, out) || stepMisc (type, s, y, a, fast, out))
        return out;
    return warpSample (type, y, a, 0.0f, fast);
}
} // namespace warpadaa
} // namespace own
} // namespace synthyboy
