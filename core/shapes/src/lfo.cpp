// SynthyBoy - core/shapes: LFO engine pieces (phase accumulator, directions, sync, chaos, smooth).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/lfo.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::shapes
{
namespace
{
constexpr float kRosslerA = 0.2f;
constexpr float kRosslerB = 0.2f;
constexpr float kRosslerC = 5.7f;

constexpr float kLorenzSigma = 10.0f;
constexpr float kLorenzRho = 28.0f;
constexpr float kLorenzBeta = 8.0f / 3.0f;
}

const BeatDivisionInfo kBeatDivisions[kNumBeatDivisions] = {
    { "32/1", 128.0f },
    { "16/1",  64.0f },
    {  "8/1",  32.0f },
    {  "4/1",  16.0f },
    {  "2/1",   8.0f },
    {  "1/1",   4.0f },
    {  "1/2",   2.0f },
    {  "1/4",   1.0f },
    {  "1/8",   0.5f },
    { "1/16",  0.25f },
    { "1/32", 0.125f },
    { "1/64", 0.0625f }
};

float getBeatDivisionQuarterNotes (size_t divisionIndex, BeatModifier modifier) noexcept
{
    if (divisionIndex >= kNumBeatDivisions)
        divisionIndex = 7; // default 1/4

    float qn = kBeatDivisions[divisionIndex].quarterNotes;
    switch (modifier)
    {
        case BeatModifier::triplet:
            qn *= (2.0f / 3.0f);
            break;
        case BeatModifier::dotted:
            qn *= 1.5f;
            break;
        case BeatModifier::straight:
        default:
            break;
    }
    return qn;
}

float beatDivisionToFrequency (size_t divisionIndex, BeatModifier modifier, float bpm) noexcept
{
    const float qn = getBeatDivisionQuarterNotes (divisionIndex, modifier);
    if (qn <= 0.0f || bpm <= 0.0f)
        return 1.0f;

    const float beatsPerSecond = bpm / 60.0f;
    return beatsPerSecond / qn;
}

float beatDivisionToSamples (size_t divisionIndex, BeatModifier modifier, float bpm, double sampleRate) noexcept
{
    const float freq = beatDivisionToFrequency (divisionIndex, modifier, bpm);
    if (freq <= 0.0f || sampleRate <= 0.0)
        return 44100.0f;

    return static_cast<float> (sampleRate / static_cast<double> (freq));
}

// -------------------------------------------------------------------------------------------------
// PhaseAccumulator
// -------------------------------------------------------------------------------------------------

void PhaseAccumulator::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void PhaseAccumulator::reset (float initialPhase, uint32_t seed) noexcept
{
    rngState_ = seed != 0 ? seed : 123456789;
    internalPhase_ = std::clamp (static_cast<double> (initialPhase), 0.0, 1.0);
    pingPongReverse_ = false;
    finished_ = false;
    noteHeld_ = false;
}

void PhaseAccumulator::noteOn (float initialPhase) noexcept
{
    if (syncType_ != SyncType::freeRun)
    {
        internalPhase_ = std::clamp (static_cast<double> (initialPhase), 0.0, 1.0);
        pingPongReverse_ = false;
        finished_ = false;
    }
    noteHeld_ = true;
}

void PhaseAccumulator::noteOff() noexcept
{
    noteHeld_ = false;
}

void PhaseAccumulator::setFrequency (float hz) noexcept
{
    frequency_ = (std::isfinite (hz) && hz > 0.0f) ? hz : 0.001f;
}

void PhaseAccumulator::setDirection (Direction dir) noexcept
{
    direction_ = dir;
}

void PhaseAccumulator::setSyncType (SyncType sync) noexcept
{
    syncType_ = sync;
}

void PhaseAccumulator::setLoopbackPoint (float loopPoint) noexcept
{
    loopbackPoint_ = std::clamp (loopPoint, 0.0f, 1.0f);
}

void PhaseAccumulator::syncToHost (double ppq, double /*bpm*/, float beats) noexcept
{
    if (beats <= 0.0f)
        return;

    const double cycle = ppq / static_cast<double> (beats);
    double frac = cycle - std::floor (cycle);
    if (frac < 0.0) frac += 1.0;
    internalPhase_ = frac;
    finished_ = false;
}

uint32_t PhaseAccumulator::nextRandom() noexcept
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

float PhaseAccumulator::phase() const noexcept
{
    if (finished_)
    {
        if (direction_ == Direction::backward)
            return 0.0f;
        if (direction_ == Direction::pingPong)
            return 0.0f;
        return 1.0f;
    }

    double p = internalPhase_;
    switch (direction_)
    {
        case Direction::forward:
            return static_cast<float> (std::clamp (p, 0.0, 1.0));

        case Direction::backward:
            return static_cast<float> (std::clamp (1.0 - p, 0.0, 1.0));

        case Direction::pingPong:
        {
            if (p < 1.0)
                return static_cast<float> (std::clamp (p, 0.0, 1.0));
            return static_cast<float> (std::clamp (2.0 - p, 0.0, 1.0));
        }

        case Direction::random:
            return static_cast<float> (pingPongReverse_ ? std::clamp (1.0 - p, 0.0, 1.0)
                                                       : std::clamp (p, 0.0, 1.0));
    }
    return static_cast<float> (p);
}

float PhaseAccumulator::next() noexcept
{
    if (finished_)
        return phase();

    const float currentPhase = phase();

    // Advance phase
    double dPhase = static_cast<double> (frequency_) / sampleRate_;

    if (direction_ == Direction::pingPong)
    {
        // One full round-trip (0 -> 1 -> 0) in 1 / frequency seconds
        dPhase *= 2.0;
        internalPhase_ += dPhase;
        if (internalPhase_ >= 2.0)
        {
            if (syncType_ == SyncType::envelope)
            {
                finished_ = true;
                internalPhase_ = 2.0;
            }
            else
            {
                internalPhase_ = std::fmod (internalPhase_, 2.0);
            }
        }
    }
    else
    {
        internalPhase_ += dPhase;
        if (internalPhase_ >= 1.0)
        {
            if (syncType_ == SyncType::envelope)
            {
                finished_ = true;
                internalPhase_ = 1.0;
            }
            else if (syncType_ == SyncType::sustainEnvelope && noteHeld_)
            {
                internalPhase_ = static_cast<double> (loopbackPoint_);
            }
            else if (syncType_ == SyncType::loopAtPoint)
            {
                const double loop = static_cast<double> (loopbackPoint_);
                const double range = 1.0 - loop;
                if (range > 1.0e-6)
                    internalPhase_ = loop + std::fmod (internalPhase_ - 1.0, range);
                else
                    internalPhase_ = loop;
            }
            else if (syncType_ == SyncType::loopAndHold && ! noteHeld_)
            {
                finished_ = true;
                internalPhase_ = static_cast<double> (loopbackPoint_);
            }
            else
            {
                internalPhase_ -= std::floor (internalPhase_);
                if (direction_ == Direction::random)
                    pingPongReverse_ = (nextRandom() & 1) != 0;
            }
        }
    }

    return currentPhase;
}

// -------------------------------------------------------------------------------------------------
// LfoSmoother
// -------------------------------------------------------------------------------------------------

void LfoSmoother::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    setTime (smoothTime_);
}

void LfoSmoother::reset (float initialValue) noexcept
{
    current_ = initialValue;
}

void LfoSmoother::setTime (float seconds) noexcept
{
    smoothTime_ = std::max (0.0f, seconds);
    if (smoothTime_ <= 1.0e-5f)
    {
        coeff_ = 1.0f;
    }
    else
    {
        const double tau = static_cast<double> (smoothTime_);
        coeff_ = static_cast<float> (1.0 - std::exp (-1.0 / (tau * sampleRate_)));
    }
}

float LfoSmoother::process (float input) noexcept
{
    if (coeff_ >= 0.99999f)
    {
        current_ = input;
        return flushDenormal (current_);
    }

    current_ += coeff_ * (input - current_);
    current_ = flushDenormal (current_);
    return current_;
}

// -------------------------------------------------------------------------------------------------
// LfoFadeDelay
// -------------------------------------------------------------------------------------------------

void LfoFadeDelay::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void LfoFadeDelay::reset() noexcept
{
    elapsedSeconds_ = 0.0;
    active_ = false;
}

void LfoFadeDelay::noteOn() noexcept
{
    elapsedSeconds_ = 0.0;
    active_ = true;
}

void LfoFadeDelay::setDelay (float seconds) noexcept
{
    delaySeconds_ = std::max (0.0f, seconds);
}

void LfoFadeDelay::setFadeIn (float seconds) noexcept
{
    fadeInSeconds_ = std::max (0.0f, seconds);
}

float LfoFadeDelay::nextGain() noexcept
{
    if (! active_)
        return 1.0f;

    const double dt = 1.0 / sampleRate_;
    elapsedSeconds_ += dt;

    if (elapsedSeconds_ < static_cast<double> (delaySeconds_))
        return 0.0f;

    if (fadeInSeconds_ <= 1.0e-5f)
        return 1.0f;

    const float progress = static_cast<float> (elapsedSeconds_ - static_cast<double> (delaySeconds_)) / fadeInSeconds_;
    return flushDenormal (std::clamp (progress, 0.0f, 1.0f));
}

// -------------------------------------------------------------------------------------------------
// RosslerAttractor
// -------------------------------------------------------------------------------------------------

void RosslerAttractor::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset (1);
}

void RosslerAttractor::reset (uint32_t seed) noexcept
{
    // Perturb initial state slightly based on seed around standard attractor orbit
    const float offset = static_cast<float> (seed % 100) * 0.01f;
    x_ = -6.0f + offset;
    y_ = 0.0f;
    z_ = 0.0f;
}

void RosslerAttractor::setFrequency (float hz) noexcept
{
    frequency_ = (std::isfinite (hz) && hz > 0.0f) ? hz : 1.0f;
}

float RosslerAttractor::next() noexcept
{
    // Time scale for Rossler: 1 oscillation cycle has period approx T = 6.0 units in system time.
    // So system dt = frequency * 6.0 / sampleRate.
    const float totalDt = std::clamp (frequency_ * 6.0f / static_cast<float> (sampleRate_), 0.00001f, 0.05f);

    // RK4 integration step
    auto deriv = [] (float px, float py, float pz, float& dx, float& dy, float& dz)
    {
        dx = -py - pz;
        dy = px + kRosslerA * py;
        dz = kRosslerB + pz * (px - kRosslerC);
    };

    float k1x, k1y, k1z;
    deriv (x_, y_, z_, k1x, k1y, k1z);

    const float h = totalDt;
    float k2x, k2y, k2z;
    deriv (x_ + 0.5f * h * k1x, y_ + 0.5f * h * k1y, z_ + 0.5f * h * k1z, k2x, k2y, k2z);

    float k3x, k3y, k3z;
    deriv (x_ + 0.5f * h * k2x, y_ + 0.5f * h * k2y, z_ + 0.5f * h * k2z, k3x, k3y, k3z);

    float k4x, k4y, k4z;
    deriv (x_ + h * k3x, y_ + h * k3y, z_ + h * k3z, k4x, k4y, k4z);

    x_ += (h / 6.0f) * (k1x + 2.0f * k2x + 2.0f * k3x + k4x);
    y_ += (h / 6.0f) * (k1y + 2.0f * k2y + 2.0f * k3y + k4y);
    z_ += (h / 6.0f) * (k1z + 2.0f * k2z + 2.0f * k3z + k4z);

    // Bounded safety check: if divergence or NaN occurs, recover instantly
    if (! std::isfinite (x_) || ! std::isfinite (y_) || ! std::isfinite (z_) ||
        std::fabs (x_) > 50.0f || std::fabs (y_) > 50.0f || z_ < -10.0f || z_ > 100.0f)
    {
        x_ = -6.0f;
        y_ = 0.0f;
        z_ = 0.0f;
    }

    // Normalizing x: standard Rossler x ranges roughly from -11 to +12
    const float out = std::clamp (x_ / 12.0f, -1.0f, 1.0f);
    return flushDenormal (out);
}

// -------------------------------------------------------------------------------------------------
// LorenzAttractor
// -------------------------------------------------------------------------------------------------

void LorenzAttractor::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void LorenzAttractor::reset (uint32_t seed) noexcept
{
    const float offset = static_cast<float> (seed % 100) * 0.001f;
    x_ = 0.1f + offset;
    y_ = 0.0f;
    z_ = 0.0f;
}

void LorenzAttractor::setFrequency (float hz) noexcept
{
    frequency_ = (std::isfinite (hz) && hz > 0.0f) ? hz : 1.0f;
}

float LorenzAttractor::next() noexcept
{
    const float totalDt = std::clamp (frequency_ * 2.0f / static_cast<float> (sampleRate_), 0.0001f, 0.02f);

    auto deriv = [] (float px, float py, float pz, float& dx, float& dy, float& dz)
    {
        dx = kLorenzSigma * (py - px);
        dy = px * (kLorenzRho - pz) - py;
        dz = px * py - kLorenzBeta * pz;
    };

    float k1x, k1y, k1z;
    deriv (x_, y_, z_, k1x, k1y, k1z);

    const float h = totalDt;
    float k2x, k2y, k2z;
    deriv (x_ + 0.5f * h * k1x, y_ + 0.5f * h * k1y, z_ + 0.5f * h * k1z, k2x, k2y, k2z);

    float k3x, k3y, k3z;
    deriv (x_ + 0.5f * h * k2x, y_ + 0.5f * h * k2y, z_ + 0.5f * h * k2z, k3x, k3y, k3z);

    float k4x, k4y, k4z;
    deriv (x_ + h * k3x, y_ + h * k3y, z_ + h * k3z, k4x, k4y, k4z);

    x_ += (h / 6.0f) * (k1x + 2.0f * k2x + 2.0f * k3x + k4x);
    y_ += (h / 6.0f) * (k1y + 2.0f * k2y + 2.0f * k3y + k4y);
    z_ += (h / 6.0f) * (k1z + 2.0f * k2z + 2.0f * k3z + k4z);

    if (! std::isfinite (x_) || ! std::isfinite (y_) || ! std::isfinite (z_) ||
        std::fabs (x_) > 100.0f || std::fabs (y_) > 100.0f || z_ < -10.0f || z_ > 150.0f)
    {
        x_ = 0.1f;
        y_ = 0.0f;
        z_ = 0.0f;
    }

    const float out = std::clamp (x_ / 20.0f, -1.0f, 1.0f);
    return flushDenormal (out);
}

// -------------------------------------------------------------------------------------------------
// RandomLfoEngine
// -------------------------------------------------------------------------------------------------

void RandomLfoEngine::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    phaseAcc_.prepare (sampleRate_);
    rossler_.prepare (sampleRate_);
    lorenz_.prepare (sampleRate_);
}

void RandomLfoEngine::reset (uint32_t seed) noexcept
{
    rngState_ = seed != 0 ? seed : 42;
    phaseAcc_.reset (0.0f, rngState_);
    rossler_.reset (rngState_);
    lorenz_.reset (rngState_);
    lastRandom_ = nextFloatBipolar();
    nextRandom_ = nextFloatBipolar();
    lastPhase_ = 0.0f;
}

void RandomLfoEngine::noteOn() noexcept
{
    phaseAcc_.noteOn (0.0f);
    lastRandom_ = nextRandom_;
    nextRandom_ = nextFloatBipolar();
}

void RandomLfoEngine::setStyle (RandomLfoStyle style) noexcept
{
    style_ = style;
}

void RandomLfoEngine::setFrequency (float hz) noexcept
{
    frequency_ = hz;
    phaseAcc_.setFrequency (hz);
    rossler_.setFrequency (hz);
    lorenz_.setFrequency (hz);
}

void RandomLfoEngine::syncToHost (double ppq, double bpm, float beats) noexcept
{
    phaseAcc_.syncToHost (ppq, bpm, beats);
}

uint32_t RandomLfoEngine::nextRng() noexcept
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

float RandomLfoEngine::nextFloatBipolar() noexcept
{
    const uint32_t r = nextRng();
    // Maps uint32 to [-1.0, 1.0]
    return (static_cast<float> (r) / 2147483648.0f) - 1.0f;
}

float RandomLfoEngine::next() noexcept
{
    if (style_ == RandomLfoStyle::rossler)
        return rossler_.next();

    if (style_ == RandomLfoStyle::lorenz)
        return lorenz_.next();

    const float phase = phaseAcc_.next();

    // Check for phase wrap
    if (phase < lastPhase_)
    {
        lastRandom_ = nextRandom_;
        nextRandom_ = nextFloatBipolar();
    }
    lastPhase_ = phase;

    float result = 0.0f;
    switch (style_)
    {
        case RandomLfoStyle::sampleAndHold:
            result = lastRandom_;
            break;

        case RandomLfoStyle::sineInterpolate:
        {
            const float t = 0.5f * (1.0f - std::cos (3.14159265358979323846f * phase));
            result = lastRandom_ + t * (nextRandom_ - lastRandom_);
            break;
        }

        case RandomLfoStyle::perlin:
        {
            // 1D Perlin noise Hermite gradient interpolation
            const float t = phase;
            const float from = lastRandom_ * t;
            const float to = nextRandom_ * (t - 1.0f);
            const float st = t * t * (3.0f - 2.0f * t);
            result = std::clamp ((from + st * (to - from)) * 2.0f, -1.0f, 1.0f);
            break;
        }

        default:
            result = 0.0f;
            break;
    }

    return flushDenormal (result);
}

// -------------------------------------------------------------------------------------------------
// LfoEngine
// -------------------------------------------------------------------------------------------------

void LfoEngine::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    phaseAcc_.prepare (sampleRate_);
    smoother_.prepare (sampleRate_);
    fadeDelay_.prepare (sampleRate_);
    rossler_.prepare (sampleRate_);
    lorenz_.prepare (sampleRate_);
    randomLfo_.prepare (sampleRate_);
}

void LfoEngine::reset (uint32_t seed) noexcept
{
    phaseAcc_.reset (params_.phaseOffset, seed);
    smoother_.reset (0.0f);
    fadeDelay_.reset();
    rossler_.reset (seed);
    lorenz_.reset (seed);
    randomLfo_.reset (seed);
}

void LfoEngine::noteOn() noexcept
{
    phaseAcc_.noteOn (params_.phaseOffset);
    fadeDelay_.noteOn();
    randomLfo_.noteOn();
}

void LfoEngine::noteOff() noexcept
{
    phaseAcc_.noteOff();
}

void LfoEngine::setParameters (const LfoParameters& params) noexcept
{
    params_ = params;
    phaseAcc_.setDirection (params.direction);
    phaseAcc_.setSyncType (params.syncType);
    smoother_.setTime (params.smoothTime);
    fadeDelay_.setDelay (params.delay);
    fadeDelay_.setFadeIn (params.fadeIn);

    if (! params.tempoSync)
    {
        phaseAcc_.setFrequency (params.frequency);
        rossler_.setFrequency (params.frequency);
        lorenz_.setFrequency (params.frequency);
        randomLfo_.setFrequency (params.frequency);
    }

    randomLfo_.setStyle (params.chaosStyle);
}

void LfoEngine::setShape (const ShapeData& shape) noexcept
{
    shape_ = shape;
}

void LfoEngine::syncToHost (double ppq, double bpm) noexcept
{
    if (params_.tempoSync)
    {
        const float qn = getBeatDivisionQuarterNotes (params_.beatDivision, params_.beatModifier);
        const float freq = beatDivisionToFrequency (params_.beatDivision, params_.beatModifier, bpm);
        phaseAcc_.setFrequency (freq);
        phaseAcc_.syncToHost (ppq, bpm, qn);
        randomLfo_.syncToHost (ppq, bpm, qn);
    }
}

float LfoEngine::next() noexcept
{
    float val = 0.0f;
    if (params_.useChaos)
    {
        val = randomLfo_.next();
    }
    else
    {
        const float p = phaseAcc_.next();
        val = shape_.evaluate (p);
    }

    const float gain = fadeDelay_.nextGain();
    val *= gain;

    return smoother_.process (val);
}

} // namespace synthyboy::shapes
