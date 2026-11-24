// SynthyBoy - core: the shape of an envelope of the own engine as the voice plays it (for the envelope displays: the MPC overlay's Play page).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ---- Envelope display (how a display uses this) ----
//
//   EnvDisplay::Request req;  req.env = 1 .. 6;  req.bpm = transport bpm;
//   EnvDisplay::Geometry g;   float y[256];
//   EnvDisplay::compute (values, req, g, y, 256);              // any thread, no allocation, no engine involved, deterministic
//   // the live dot: LiveValues::envFineStage[env - 1] and envFineFraction[env - 1] -> EnvDisplay::dotFor (g, y, 256, stage, fraction, x, level)
//
// The picture is the response of the engine's own envelope code (own::Envelope: delay, attack with attackCurve, hold, decay with decayCurve down to the sustain level, a sustain
// segment, release with releaseCurve from the sustain level; own::powerShape is the curve function), started from level 0. The x axis is TIME (0 .. 1): the stages follow each
// other in their real durations (seconds, or beats at `bpm` when the envelope's units are beats); the sustain segment is drawn 25 percent as long as the timed stages together
// (at least 0.1 s); a stage that has a duration is at least 1.5 percent of the axis wide, so that a 5 ms attack still shows. y is the level, 0 .. 1.
#pragma once

#include "state.h"

namespace synthyboy
{
class EnvDisplay
{
public:
    struct Request
    {
        int env = 1;           // 1 .. 6
        double bpm = 120.0;    // tempo for the envelopes whose units are beats
    };

    /** Where the stages end on the x axis (0 .. 1): delay, attack, hold, decay, sustain, release (end[5] is 1). `sustain` is the level of the sustain segment (0 .. 1). */
    struct Geometry
    {
        double end[6] = { 0, 0, 0, 0, 0, 1 };
        double sustain = 1.0;
    };

    /** Writes n (>= 2) levels at x = i / (n - 1) and the geometry. false (and zeros) for a bad envelope number, null output or n < 2. */
    static bool compute (const ParamValues& values, const Request& request, Geometry& geometry, float* out, int n) noexcept;

    /** The point of the live dot for LiveValues::envFineStage (1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release; the other stages: idle or finished) and the fraction 0 .. 1 of that
        stage that has run. false for those: no dot. x in 0 .. 1; level in 0 .. 1 is taken from the curve `y` (n values) at x. The dot of the sustain stands in the middle of its segment. */
    static bool dotFor (const Geometry& geometry, const float* y, int n, float fineStage, float fraction, double& x, double& level) noexcept;
};
} // namespace synthyboy
