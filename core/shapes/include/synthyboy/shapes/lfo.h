// SynthyBoy - core/shapes: LFO engine pieces (phase accumulator, directions, sync, chaos, smooth).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "synthyboy/shapes/shape.h"

namespace synthyboy::shapes
{

enum class Direction
{
    forward = 0,
    backward = 1,
    pingPong = 2,
    random = 3
};

enum class SyncType
{
    retrigger = 0,       // Restarts phase on note-on
    freeRun = 1,         // Free-running phase, ignores note-on
    envelope = 2,        // Plays 1 cycle then holds the final value
    sustainEnvelope = 3, // Plays to loopback point, holds during note, finishes on note-off
    loopAtPoint = 4,     // Plays from 0 to 1, then loops from loopback point to end
    loopAndHold = 5      // Loops from 0 to loopback point, holds on note-off
};

enum class BeatModifier
{
    straight = 0,
    triplet = 1,
    dotted = 2
};

struct BeatDivisionInfo
{
    const char* name;
    float quarterNotes; // duration in quarter notes (PPQ)
};

/** Beat division table: 32/1 down to 1/64 (12 divisions). */
constexpr size_t kNumBeatDivisions = 12;
extern const BeatDivisionInfo kBeatDivisions[kNumBeatDivisions];

/** Computes duration in quarter notes for a beat division index and modifier. */
float getBeatDivisionQuarterNotes (size_t divisionIndex, BeatModifier modifier) noexcept;

/** Computes frequency in Hz from BPM, division index, and modifier. */
float beatDivisionToFrequency (size_t divisionIndex, BeatModifier modifier, float bpm) noexcept;

/** Computes period in samples for beat sync. */
float beatDivisionToSamples (size_t divisionIndex, BeatModifier modifier, float bpm, double sampleRate) noexcept;

/** Phase accumulator supporting forward, backward, ping-pong, random directions and sync modes. */
class PhaseAccumulator
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (float initialPhase = 0.0f, uint32_t seed = 42) noexcept;
    void noteOn (float initialPhase = 0.0f) noexcept;
    void noteOff() noexcept;

    void setFrequency (float hz) noexcept;
    void setDirection (Direction dir) noexcept;
    void setSyncType (SyncType sync) noexcept;
    void setLoopbackPoint (float loopPoint) noexcept;

    /** Transport sync hook: anchors phase directly to PPQ position and tempo. */
    void syncToHost (double ppq, double bpm, float beats) noexcept;

    /** Advances phase by 1 sample and returns effective shape phase in [0.0, 1.0]. */
    float next() noexcept;

    /** Returns current effective phase in [0.0, 1.0] without advancing. */
    float phase() const noexcept;

    bool isFinished() const noexcept { return finished_; }

private:
    double sampleRate_ = 44100.0;
    float frequency_ = 1.0f;
    Direction direction_ = Direction::forward;
    SyncType syncType_ = SyncType::retrigger;
    float loopbackPoint_ = 0.0f;

    double internalPhase_ = 0.0;
    bool pingPongReverse_ = false;
    bool finished_ = false;
    bool noteHeld_ = false;

    uint32_t rngState_ = 123456789;
    uint32_t nextRandom() noexcept;
};

/** One-pole smoothing filter for LFO output. Denormal-safe. */
class LfoSmoother
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (float initialValue = 0.0f) noexcept;
    void setTime (float seconds) noexcept;
    float process (float input) noexcept;

private:
    double sampleRate_ = 44100.0;
    float smoothTime_ = 0.0f;
    float current_ = 0.0f;
    float coeff_ = 1.0f;
};

/** Delay and Fade-in (rise) gain controller. */
class LfoFadeDelay
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void noteOn() noexcept;
    void setDelay (float seconds) noexcept;
    void setFadeIn (float seconds) noexcept;
    float nextGain() noexcept; // returns multiplier in [0.0, 1.0]

private:
    double sampleRate_ = 44100.0;
    float delaySeconds_ = 0.0f;
    float fadeInSeconds_ = 0.0f;
    double elapsedSeconds_ = 0.0;
    bool active_ = false;
};

/** Rössler chaotic attractor generator. Numerically stable, bounded in [-1.0, 1.0]. */
class RosslerAttractor
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (uint32_t seed = 1) noexcept;
    void setFrequency (float hz) noexcept;
    float next() noexcept; // returns value in [-1.0, 1.0]

private:
    double sampleRate_ = 44100.0;
    float frequency_ = 1.0f;
    float x_ = 0.1f;
    float y_ = 0.0f;
    float z_ = 0.0f;
};

/** Lorenz chaotic attractor generator. Numerically stable, bounded in [-1.0, 1.0]. */
class LorenzAttractor
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (uint32_t seed = 1) noexcept;
    void setFrequency (float hz) noexcept;
    float next() noexcept; // returns value in [-1.0, 1.0]

private:
    double sampleRate_ = 44100.0;
    float frequency_ = 1.0f;
    float x_ = 0.1f;
    float y_ = 0.0f;
    float z_ = 0.0f;
};

/** Random LFO styles matching 05-envelopes-lfos.md and Serum/Vital. */
enum class RandomLfoStyle
{
    perlin = 0,
    sampleAndHold = 1,
    sineInterpolate = 2,
    lorenz = 3,
    rossler = 4
};

/** Complete random LFO engine. Deterministic with seed. */
class RandomLfoEngine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (uint32_t seed = 42) noexcept;
    void noteOn() noexcept;
    void setStyle (RandomLfoStyle style) noexcept;
    void setFrequency (float hz) noexcept;
    void syncToHost (double ppq, double bpm, float beats) noexcept;
    float next() noexcept; // returns value in [-1.0, 1.0]

private:
    double sampleRate_ = 44100.0;
    float frequency_ = 1.0f;
    RandomLfoStyle style_ = RandomLfoStyle::sampleAndHold;
    PhaseAccumulator phaseAcc_;
    RosslerAttractor rossler_;
    LorenzAttractor lorenz_;

    uint32_t rngState_ = 123456789;
    float lastRandom_ = 0.0f;
    float nextRandom_ = 0.0f;
    float lastPhase_ = 0.0f;

    uint32_t nextRng() noexcept;
    float nextFloatBipolar() noexcept;
};

/** Unified LFO engine combining phase accumulator, custom shape, chaotic generator, fade/delay, and smooth. */
struct LfoParameters
{
    float frequency = 1.0f;       // in Hz (when not synced)
    bool tempoSync = false;       // beat-synced
    size_t beatDivision = 7;      // 1/4 note default
    BeatModifier beatModifier = BeatModifier::straight;
    Direction direction = Direction::forward;
    SyncType syncType = SyncType::retrigger;
    bool mono = false;
    float phaseOffset = 0.0f;
    float delay = 0.0f;           // delay in seconds
    float fadeIn = 0.0f;          // rise/fade-in in seconds
    float smoothTime = 0.0f;      // smooth time in seconds
    bool useChaos = false;
    RandomLfoStyle chaosStyle = RandomLfoStyle::rossler;
};

class LfoEngine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (uint32_t seed = 42) noexcept;
    void noteOn() noexcept;
    void noteOff() noexcept;

    void setParameters (const LfoParameters& params) noexcept;
    void setShape (const ShapeData& shape) noexcept;

    /** Host sync hook. */
    void syncToHost (double ppq, double bpm) noexcept;

    /** Generate next sample in [-1.0, 1.0]. */
    float next() noexcept;

private:
    double sampleRate_ = 44100.0;
    LfoParameters params_;
    ShapeData shape_;
    PhaseAccumulator phaseAcc_;
    LfoSmoother smoother_;
    LfoFadeDelay fadeDelay_;
    RosslerAttractor rossler_;
    LorenzAttractor lorenz_;
    RandomLfoEngine randomLfo_;
};

} // namespace synthyboy::shapes
