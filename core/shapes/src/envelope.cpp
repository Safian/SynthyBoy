// SynthyBoy - core/shapes: curve-segment envelope and velocity/note mapping curves.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/envelope.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::shapes
{

void CurveEnvelope::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void CurveEnvelope::reset() noexcept
{
    state_ = EnvelopeState::idle;
    elapsed_ = 0.0;
    currentLevel_ = 0.0f;
    releaseStartLevel_ = 0.0f;
}

void CurveEnvelope::setParameters (const EnvelopeParameters& params) noexcept
{
    params_ = params;
    params_.delay = std::max (0.0f, params_.delay);
    params_.attack = std::max (0.0f, params_.attack);
    params_.hold = std::max (0.0f, params_.hold);
    params_.decay = std::max (0.0f, params_.decay);
    params_.sustain = std::clamp (params_.sustain, 0.0f, 1.0f);
    params_.release = std::max (0.0f, params_.release);
}

void CurveEnvelope::noteOn() noexcept
{
    elapsed_ = 0.0;
    if (params_.delay > 1.0e-5f)
    {
        state_ = EnvelopeState::delay;
        currentLevel_ = 0.0f;
    }
    else if (params_.attack > 1.0e-5f)
    {
        state_ = EnvelopeState::attack;
        currentLevel_ = 0.0f;
    }
    else if (params_.hold > 1.0e-5f)
    {
        state_ = EnvelopeState::hold;
        currentLevel_ = 1.0f;
    }
    else if (params_.decay > 1.0e-5f)
    {
        state_ = EnvelopeState::decay;
        currentLevel_ = 1.0f;
    }
    else
    {
        state_ = EnvelopeState::sustain;
        currentLevel_ = params_.sustain;
    }
}

void CurveEnvelope::noteOff() noexcept
{
    if (state_ == EnvelopeState::idle)
        return;

    releaseStartLevel_ = currentLevel_;
    elapsed_ = 0.0;
    if (params_.release > 1.0e-5f)
    {
        state_ = EnvelopeState::release;
    }
    else
    {
        state_ = EnvelopeState::idle;
        currentLevel_ = 0.0f;
    }
}

float CurveEnvelope::next() noexcept
{
    const double dt = 1.0 / sampleRate_;

    switch (state_)
    {
        case EnvelopeState::idle:
            currentLevel_ = 0.0f;
            break;

        case EnvelopeState::delay:
            elapsed_ += dt;
            currentLevel_ = 0.0f;
            if (elapsed_ >= static_cast<double> (params_.delay))
            {
                elapsed_ = 0.0;
                if (params_.attack > 1.0e-5f)
                {
                    state_ = EnvelopeState::attack;
                }
                else if (params_.hold > 1.0e-5f)
                {
                    state_ = EnvelopeState::hold;
                    currentLevel_ = 1.0f;
                }
                else if (params_.decay > 1.0e-5f)
                {
                    state_ = EnvelopeState::decay;
                    currentLevel_ = 1.0f;
                }
                else
                {
                    state_ = EnvelopeState::sustain;
                    currentLevel_ = params_.sustain;
                }
            }
            break;

        case EnvelopeState::attack:
            elapsed_ += dt;
            if (params_.attack <= 1.0e-5f)
            {
                currentLevel_ = 1.0f;
                elapsed_ = 0.0;
                state_ = (params_.hold > 1.0e-5f) ? EnvelopeState::hold
                       : (params_.decay > 1.0e-5f ? EnvelopeState::decay : EnvelopeState::sustain);
            }
            else
            {
                const float t = std::clamp (static_cast<float> (elapsed_ / static_cast<double> (params_.attack)), 0.0f, 1.0f);
                currentLevel_ = powerScale (t, params_.attackCurve);
                if (t >= 1.0f)
                {
                    currentLevel_ = 1.0f;
                    elapsed_ = 0.0;
                    state_ = (params_.hold > 1.0e-5f) ? EnvelopeState::hold
                           : (params_.decay > 1.0e-5f ? EnvelopeState::decay : EnvelopeState::sustain);
                }
            }
            break;

        case EnvelopeState::hold:
            elapsed_ += dt;
            currentLevel_ = 1.0f;
            if (elapsed_ >= static_cast<double> (params_.hold))
            {
                elapsed_ = 0.0;
                state_ = (params_.decay > 1.0e-5f) ? EnvelopeState::decay : EnvelopeState::sustain;
            }
            break;

        case EnvelopeState::decay:
            elapsed_ += dt;
            if (params_.decay <= 1.0e-5f)
            {
                currentLevel_ = params_.sustain;
                state_ = EnvelopeState::sustain;
            }
            else
            {
                const float t = std::clamp (static_cast<float> (elapsed_ / static_cast<double> (params_.decay)), 0.0f, 1.0f);
                const float curved = powerScale (t, params_.decayCurve);
                currentLevel_ = 1.0f - curved * (1.0f - params_.sustain);
                if (t >= 1.0f)
                {
                    currentLevel_ = params_.sustain;
                    state_ = EnvelopeState::sustain;
                }
            }
            break;

        case EnvelopeState::sustain:
            currentLevel_ = params_.sustain;
            break;

        case EnvelopeState::release:
            elapsed_ += dt;
            if (params_.release <= 1.0e-5f)
            {
                currentLevel_ = 0.0f;
                state_ = EnvelopeState::idle;
            }
            else
            {
                const float t = std::clamp (static_cast<float> (elapsed_ / static_cast<double> (params_.release)), 0.0f, 1.0f);
                const float curved = powerScale (t, params_.releaseCurve);
                currentLevel_ = releaseStartLevel_ * (1.0f - curved);
                if (t >= 1.0f)
                {
                    currentLevel_ = 0.0f;
                    state_ = EnvelopeState::idle;
                }
            }
            break;
    }

    return flushDenormal (currentLevel_);
}

// -------------------------------------------------------------------------------------------------
// MappingCurve
// -------------------------------------------------------------------------------------------------

void MappingCurve::initLinear() noexcept
{
    shape_.initLinear();
    smoothValue_ = 0.0f;
    smoothCoeff_ = 1.0f;
}

void MappingCurve::setShape (const ShapeData& shape) noexcept
{
    shape_ = shape;
    shape_.sanitize();
}

float MappingCurve::evaluate (float normalizedInput) const noexcept
{
    return shape_.evaluate (normalizedInput);
}

float MappingCurve::evaluateMidiNote (int midiNote) const noexcept
{
    const float norm = std::clamp (static_cast<float> (midiNote) / 127.0f, 0.0f, 1.0f);
    return evaluate (norm);
}

float MappingCurve::evaluateVelocity7Bit (int velocity) const noexcept
{
    const float norm = std::clamp (static_cast<float> (velocity) / 127.0f, 0.0f, 1.0f);
    return evaluate (norm);
}

void MappingCurve::setSmoothTime (float seconds, double sampleRate) noexcept
{
    if (seconds <= 1.0e-5f || sampleRate <= 0.0)
    {
        smoothCoeff_ = 1.0f;
    }
    else
    {
        smoothCoeff_ = static_cast<float> (1.0 - std::exp (-1.0 / (static_cast<double> (seconds) * sampleRate)));
    }
}

void MappingCurve::resetSmoother (float initialValue) noexcept
{
    smoothValue_ = initialValue;
}

float MappingCurve::evaluateSmoothed (float normalizedInput) noexcept
{
    const float target = evaluate (normalizedInput);
    if (smoothCoeff_ >= 0.99999f)
    {
        smoothValue_ = target;
        return flushDenormal (smoothValue_);
    }

    smoothValue_ += smoothCoeff_ * (target - smoothValue_);
    return flushDenormal (smoothValue_);
}

} // namespace synthyboy::shapes
