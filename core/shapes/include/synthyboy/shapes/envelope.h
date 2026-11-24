// SynthyBoy - core/shapes: curve-segment envelope and velocity/note mapping curves.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>

#include "synthyboy/shapes/shape.h"

namespace synthyboy::shapes
{

enum class EnvelopeState
{
    idle = 0,
    delay = 1,
    attack = 2,
    hold = 3,
    decay = 4,
    sustain = 5,
    release = 6
};

struct EnvelopeParameters
{
    float delay = 0.0f;         // Delay time before attack (0..4s)
    float attack = 0.005f;      // Attack time (0..10s)
    float attackCurve = 0.0f;   // Attack curvature (-20..20, 0 = linear)
    float hold = 0.0f;          // Hold peak time (0..4s)
    float decay = 0.4f;         // Decay time (0..10s)
    float decayCurve = -3.0f;   // Decay curvature (-20..20)
    float sustain = 1.0f;       // Sustain level (0..1)
    float release = 0.2f;       // Release time (0..10s)
    float releaseCurve = -3.0f; // Release curvature (-20..20)
};

/** Curve-segment ADSHR envelope evaluator. Real-time safe, denormal-safe. */
class CurveEnvelope
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void noteOn() noexcept;
    void noteOff() noexcept;

    void setParameters (const EnvelopeParameters& params) noexcept;

    EnvelopeState state() const noexcept { return state_; }
    bool isActive() const noexcept { return state_ != EnvelopeState::idle; }

    float next() noexcept;
    float currentLevel() const noexcept { return currentLevel_; }

private:
    double sampleRate_ = 44100.0;
    EnvelopeParameters params_;
    EnvelopeState state_ = EnvelopeState::idle;
    double elapsed_ = 0.0;
    float currentLevel_ = 0.0f;
    float releaseStartLevel_ = 0.0f;
};

/** Velocity and Note mapping curves (06-modulation.md). */
class MappingCurve
{
public:
    void initLinear() noexcept;
    void setShape (const ShapeData& shape) noexcept;
    const ShapeData& shape() const noexcept { return shape_; }
    ShapeData& shape() noexcept { return shape_; }

    /** Evaluates mapped output for normalized input [0.0, 1.0]. */
    float evaluate (float normalizedInput) const noexcept;

    /** Evaluates mapped output for MIDI note [0..127]. */
    float evaluateMidiNote (int midiNote) const noexcept;

    /** Evaluates mapped output for MIDI velocity [0..127]. */
    float evaluateVelocity7Bit (int velocity) const noexcept;

    /** Optional smoothing / portamento for velocity legato. */
    void setSmoothTime (float seconds, double sampleRate) noexcept;
    void resetSmoother (float initialValue = 0.0f) noexcept;
    float evaluateSmoothed (float normalizedInput) noexcept;

private:
    ShapeData shape_;
    float smoothValue_ = 0.0f;
    float smoothCoeff_ = 1.0f;
};

} // namespace synthyboy::shapes
