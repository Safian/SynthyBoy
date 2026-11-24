// SynthyBoy - core: the shape of an LFO of the own engine as the voice plays it (for the LFO displays: the MPC overlay).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ---- LFO display (how a display uses this) ----
//
//   LfoDisplay::Request req;  req.lfo = 1;  req.bpm = transport bpm;
//   float y[256];
//   LfoDisplay::compute (values, &content, req, y, 256);       // any thread, no allocation, no engine involved, deterministic
//   // the cursor: LiveValues::lfoPhase[lfo - 1] (engine->copyReadout) is the x of the cursor on the same axis: x = LfoDisplay::cursorX (readout) (-1: no voice sounds)
//
// The picture is the response of the engine's own shape code (own::lfoShapeAt, the function Voice::lfoValue calls): the shape of lfoN.shape (the 12 built-in
// ones, or the custom shape of `content` for shape 12 with its point modulation read from req.sources), swing, direction, the phase knob, the sync type and the
// smoothing. The x axis is TIME SINCE THE NOTE STARTS, in cycles of the LFO (0 .. 1): for the retrigger and free-running types the shape starts at the phase knob and
// wraps (so the phase offset shifts the picture and the cursor starts at 0); for the envelope types (2 .. 5) the shape is read once from the position of that type.
// y is the value of the LEFT lane, -1 .. 1 (the lane the readout and the matrix call the LFO value; the stereo knob shifts the left lane by stereo / 720 of a cycle).
// Smoothing (lfoN.smooth) is a one pole filter in time: it is run over three cycles at the rate of the LFO (the tempo of `bpm` for the synced rates; a rate of 0
// is not smoothed) and the last cycle is shown, as it settles; for the envelope types the first and only pass from the voice's start value 0.
// Not drawn: the delay and fade in of the LFO (time domain, they scale the whole picture), mono (shared core) and point modulation without `sources`.
#pragma once

#include "state.h"

namespace synthyboy
{
struct StateContent;

class LfoDisplay
{
public:
    struct Request
    {
        int lfo = 1;                          // 1 .. 10
        double bpm = 120.0;                   // tempo for the synced rates
        const float* sources = nullptr;       // source values by source id as the voice sees them (0 .. 1, LFOs as 0.5 + 0.5 value), kNumSourceIds entries; null: none
    };

    /** Writes n (>= 2) values of the left lane (-1 .. 1) at x = i / (n - 1). false (and zeros) for a bad lfo number, null output or n < 2. `content` may be null (a custom
        shape that is not there is read as the engine reads it: the built-in shape number 12 has no shape, a flat line at 0). */
    static bool compute (const ParamValues& values, const StateContent* content, const Request& request, float* out, int n) noexcept;

    /** The x of the cursor for LiveValues::lfoPhase (-1 or NaN: no voice sounds -> -1); otherwise the value limited to 0 .. 1. */
    static float cursorX (float readout) noexcept;
};
} // namespace synthyboy
