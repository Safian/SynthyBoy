// SynthyBoy - MPC shell: the cursor of the live LFO display (pure functions, no state).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The skin shows, for each of LFO 1 .. 4, a film strip of the selected shape with a cursor. The strip frame follows a display-only plugin
// parameter (index paramCount() + n - 1) that holds step / kLfoLiveSteps: step 0 = idle (no voice sounds: the picture without a cursor),
// steps 1 .. kLfoLiveSteps = the cursor in cell step - 1 of the cycle. Both ways of reading a frame number from a normalised value (floor(v * (steps + 1))
// and round(v * steps), the last clamped to the last frame) give the step.
#pragma once

#include <algorithm>
#include <cmath>

namespace synthyboy_mpc
{
constexpr int kLfoLiveCount = 4;   // LFO 1 .. 4
constexpr int kLfoLiveSteps = 32;  // cursor cells per cycle

/** `readout` is LiveValues::lfoPhase of the LFO (the cycle position of the voice that is heard, -1 when none sounds), `syncType` is lfoN.sync_type
    and `phaseDeg` lfoN.phase: the position the shape is read at is the engine's LfoCore::position() (swing and direction are not drawn).
    For the envelope types (2 .. 5) the engine's readout wraps at 1, so a finished envelope LFO shows its cursor at the start again. */
inline int lfoLiveStep (float readout, int syncType, float phaseDeg) noexcept
{
    if (!(readout >= 0.0f))
        return 0; // no voice (or NaN)
    const float knob = std::min (std::max (phaseDeg / 360.0f, 0.0f), 1.0f);
    const float r = std::min (readout, 1.0f);
    float pos;
    if (syncType == 2)
        pos = std::min (r + knob, 1.0f);
    else if (syncType >= 3 && syncType <= 5)
        pos = r;
    else
    {
        const float u = r + knob;
        pos = u - std::floor (u);
    }
    const int cell = static_cast<int> (pos * static_cast<float> (kLfoLiveSteps));
    return 1 + std::min (std::max (cell, 0), kLfoLiveSteps - 1);
}

inline float lfoLiveValue (int step) noexcept
{
    return static_cast<float> (step) / static_cast<float> (kLfoLiveSteps);
}
} // namespace synthyboy_mpc
