// SynthyBoy - core/shapes tests: curve-segment envelope and velocity/note mapping curves.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/envelope.h"
#include "test_runner.h"

#include <cmath>

using namespace synthyboy::shapes;

TEST (envelope_adshr_curve_evaluator)
{
    const double sr = 44100.0;
    CurveEnvelope env;
    env.prepare (sr);

    EnvelopeParameters params;
    params.delay = 0.01f;        // 10 ms
    params.attack = 0.02f;       // 20 ms
    params.attackCurve = 3.0f;
    params.hold = 0.01f;         // 10 ms
    params.decay = 0.03f;        // 30 ms
    params.decayCurve = -2.0f;
    params.sustain = 0.5f;       // 50%
    params.release = 0.02f;      // 20 ms
    params.releaseCurve = -3.0f;
    env.setParameters (params);

    env.reset();
    CHECK (! env.isActive());
    CHECK_NEAR (env.currentLevel(), 0.0f, 1.0e-5f);

    env.noteOn();
    CHECK (env.isActive());
    CHECK (env.state() == EnvelopeState::delay);

    // 1. Delay phase
    for (int i = 0; i < 441; ++i)
    {
        const float val = env.next();
        CHECK_NEAR (val, 0.0f, 1.0e-5f);
    }

    // 2. Attack phase: monotonic rise
    CHECK (env.state() == EnvelopeState::attack);
    float prevVal = 0.0f;
    for (int i = 0; i < 882; ++i)
    {
        const float val = env.next();
        CHECK (val >= prevVal - 1.0e-6f);
        CHECK (val <= 1.0001f);
        prevVal = val;
    }
    CHECK_NEAR (prevVal, 1.0f, 0.01f);

    // 3. Hold phase: holds at 1.0
    CHECK (env.state() == EnvelopeState::hold);
    for (int i = 0; i < 441; ++i)
    {
        const float val = env.next();
        CHECK_NEAR (val, 1.0f, 1.0e-5f);
    }

    // 4. Decay phase: monotonic decay to sustain
    CHECK (env.state() == EnvelopeState::decay);
    prevVal = 1.0f;
    for (int i = 0; i < 1323; ++i)
    {
        const float val = env.next();
        CHECK (val <= prevVal + 1.0e-6f);
        CHECK (val >= 0.4999f);
        prevVal = val;
    }
    CHECK_NEAR (prevVal, 0.5f, 0.01f);

    // 5. Sustain phase: stays at 0.5
    CHECK (env.state() == EnvelopeState::sustain);
    for (int i = 0; i < 500; ++i)
    {
        const float val = env.next();
        CHECK_NEAR (val, 0.5f, 1.0e-5f);
    }

    // 6. Release phase
    env.noteOff();
    CHECK (env.state() == EnvelopeState::release);
    prevVal = 0.5f;
    for (int i = 0; i < 882; ++i)
    {
        const float val = env.next();
        CHECK (val <= prevVal + 1.0e-6f);
        CHECK (val >= 0.0f);
        prevVal = val;
    }
    CHECK_NEAR (prevVal, 0.0f, 0.01f);
    CHECK (env.state() == EnvelopeState::idle);
}

TEST (envelope_instant_phases_and_interrupted_release)
{
    const double sr = 48000.0;
    CurveEnvelope env;
    env.prepare (sr);

    EnvelopeParameters params;
    params.delay = 0.0f;
    params.attack = 0.0f;
    params.hold = 0.0f;
    params.decay = 0.0f;
    params.sustain = 0.8f;
    params.release = 0.01f;
    env.setParameters (params);

    env.reset();
    env.noteOn();

    // Instant attack, hold, decay jumps straight to sustain
    CHECK (env.state() == EnvelopeState::sustain);
    CHECK_NEAR (env.next(), 0.8f, 1.0e-5f);

    // Interrupted note during attack: release should start smoothly from current level
    params.attack = 0.1f;
    params.sustain = 1.0f;
    params.release = 0.05f;
    env.setParameters (params);
    env.reset();
    env.noteOn();

    // Run halfway through attack
    for (int i = 0; i < 2400; ++i)
        env.next();

    const float levelAtRelease = env.currentLevel();
    CHECK (levelAtRelease > 0.1f && levelAtRelease < 0.9f);

    env.noteOff();
    CHECK (env.state() == EnvelopeState::release);
    const float firstReleaseVal = env.next();
    // No jump discontinuity
    CHECK_NEAR (firstReleaseVal, levelAtRelease, 0.01f);
}

TEST (mapping_curve_velocity_and_note)
{
    MappingCurve map;
    map.initLinear();

    // Normalized [0, 1] evaluation
    CHECK_NEAR (map.evaluate (0.0f), 0.0f, 1.0e-5f);
    CHECK_NEAR (map.evaluate (0.5f), 0.5f, 1.0e-5f);
    CHECK_NEAR (map.evaluate (1.0f), 1.0f, 1.0e-5f);

    // 7-bit MIDI velocity evaluation
    CHECK_NEAR (map.evaluateVelocity7Bit (0), 0.0f, 1.0e-5f);
    CHECK_NEAR (map.evaluateVelocity7Bit (127), 1.0f, 1.0e-5f);

    // 7-bit MIDI note evaluation
    CHECK_NEAR (map.evaluateMidiNote (0), 0.0f, 1.0e-5f);
    CHECK_NEAR (map.evaluateMidiNote (127), 1.0f, 1.0e-5f);

    // Custom non-linear curve
    ShapeData custom;
    custom.numPoints = 3;
    custom.points[0] = { 0.0f, 0.0f, 0.0f, false, false };
    custom.points[1] = { 0.5f, 0.2f, 0.0f, false, false };
    custom.points[2] = { 1.0f, 1.0f, 0.0f, false, false };
    map.setShape (custom);

    CHECK_NEAR (map.evaluate (0.5f), 0.2f, 1.0e-5f);

    // Velocity legato portamento smoothing
    map.setSmoothTime (0.01f, 44100.0); // 10 ms smoother
    map.resetSmoother (0.0f);

    const float smoothed0 = map.evaluateSmoothed (1.0f);
    CHECK (smoothed0 > 0.0f && smoothed0 < 1.0f);

    for (int i = 0; i < 3000; ++i)
        map.evaluateSmoothed (1.0f);

    CHECK_NEAR (map.evaluateSmoothed (1.0f), 1.0f, 0.002f);
}
