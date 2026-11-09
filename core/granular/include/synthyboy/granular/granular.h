// SynthyBoy - core/granular: the granular oscillator DSP (Serum 2 manual p.85 - 103, docs/ui-design/13-new-keys.md 4.37).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One GranularVoice is one cloud: a play head that scans a sample region, a spawner that starts grains at a rate (density), and a pool of at most
// kMaxGrains grains, each reading the sample at its own rate, direction, pan, level and window. A unison voice of the oscillator is one cloud
// (the clouds of the unison voices spawn at the offsets of the spawn pattern). Real time safe: process () and noteOn () allocate nothing and take
// no locks; the sample is a non owning SampleView of the sample oscillator package. Standalone: no dependency on the engine.
#pragma once

#include <cstdint>

#include "synthyboy/sampleosc/sample_types.h"

namespace synthyboy
{
namespace granular
{
using sampleosc::LoopMode;
using sampleosc::SampleView;

constexpr int kMaxGrains = 48; // per cloud; the oscillator divides its Max Grains setting by the unison voices

/** The grain window shapes in the order of the manual (p.91, the key oscN.gran.window_shape). */
enum class WindowShape : uint8_t { hann, welch, gaussian, blackmanHarris, sinc, tukey, triangle, trapezoid, expDec, expDecRev };
constexpr int kNumWindowShapes = 10;

/** The timing offset at which the grains of the unison clouds are spawned (manual p.95, oscN.gran.spawn_pattern). */
enum class SpawnPattern : uint8_t { together, even, exponential, random };
constexpr int kNumSpawnPatterns = 4;

/** A pointwise warp that is applied to every grain (the distortion group of the warp list): shape (type, x, amount) -> y. May be null. */
using ShapeFn = float (*) (int type, float x, float amount);

/** The window value at x (0 .. 1 across the grain): `amount` 0 is a rectangle (with a 1/64 edge ramp that keeps a grain from clicking),
    1 the full shape; `skew` (-1 .. 1) moves the peak of the shape towards the start (< 0) or the end (> 0). Never NaN, in 0 .. 1. */
float windowValue (WindowShape shape, float x, float amount, float skew) noexcept;

/** The settings of a cloud. The oscillator fills the struct once per block (from the keys and the modulation) and hands it to process (). */
struct GrainParams
{
    // region, playhead, loop
    float regionLo = 0.0f, regionHi = 1.0f;   // fractions of the file (the start / end markers)
    LoopMode loopMode = LoopMode::none;       // how the play head behaves at the ends of the region
    float loopLo = 0.0f, loopHi = 1.0f;       // loop markers, fractions of the file (loopHi < loopLo reverses the direction)
    bool loopGrains = false;                  // the grains respect the loop markers (Loop Grains)
    float loopCrossfade = 0.0f;               // fraction of the loop length, only with loopGrains
    bool manual = false;                      // the play head does not scan: `position` places it
    float position = 0.0f;                    // 0 .. 1 of the region, manual mode
    float scanSpeed = 1.0f;                   // play head speed in seconds of the file per second, negative reverses (0 stops)
    // grains
    float pitchRatio = 1.0f;                  // playback rate of the grains relative to the original (the pitch of the note)
    float densityHz = 20.0f;                  // grains spawned per second per cloud
    float lengthSec = 0.1f;                   // duration of a grain
    int maxGrains = kMaxGrains;               // the most grains of this cloud that play at the same time (1 .. kMaxGrains)
    WindowShape shape = WindowShape::hann;
    float windowAmount = 0.5f;                // 0 .. 1
    float windowSkew = 0.0f;                  // -1 .. 1
    // per grain randomisation, all 0 = off
    float randOffset = 0.0f;                  // 0 .. 1: +- half the region
    float randDir = 0.0f;                     // 0 .. 1: probability that a grain plays backwards
    bool reverseGrains = false;               // inverts the direction of every grain
    float randPitch = 0.0f;                   // semitones, +-
    float randLength = 0.0f;                  // 0 .. 1: shortens a grain by up to that share
    float randPan = 0.0f;                     // 0 .. 1
    float randGain = 0.0f;                    // 0 .. 1: lowers a grain by up to that share
    float randWindow = 0.0f;                  // 0 .. 1: +- on the window amount
    float randSkew = 0.0f;                    // 0 .. 1: +- on the window skew
    float randWarp[2] = { 0.0f, 0.0f };       // 0 .. 1: +- on the amount of the pointwise warps
    // pointwise warps of the grains (Quantize and the distortion group), slot 1 and 2
    ShapeFn shapeFn = nullptr;
    int shapeType[2] = { -1, -1 };            // < 0: slot not used
    float shapeAmount[2] = { 0.0f, 0.0f };    // 0 .. 1
    float gain = 1.0f;
};

class GranularVoice
{
public:
    GranularVoice() = default;

    void prepare (double hostSampleRate) noexcept;
    void reset() noexcept;

    /** Starts a cloud on `sample` (null or invalid: silent). `spawnFraction` (0 .. 1) delays the first spawn by that share of the spawn period (the
        spawn pattern of the unison voices); `jumpStart` spawns the grains of the full density at once; `startFraction` (0 .. 1) moves the play head
        by that share of the region (unison start / span). */
    void noteOn (const SampleView* sample, const GrainParams& p, uint32_t seed, float spawnFraction, bool jumpStart, float startFraction) noexcept;

    /** The key was released: Tailed plays on past the loop end to the end of the region. */
    void releaseLoop() noexcept { released_ = true; }

    /** Renders `numFrames` samples into left / right (overwritten). `rateMod` (optional) multiplies the grain read rate per sample (FM). */
    void process (const GrainParams& p, float* left, float* right, int numFrames, const float* rateMod) noexcept;

    bool isActive() const noexcept { return active_; }
    int activeGrains() const noexcept;
    double playhead() const noexcept { return head_; }

private:
    struct Grain
    {
        bool active = false;
        int delay = 0;          // samples of the current block before the grain starts
        int age = 0, length = 1;
        double pos = 0.0;       // read position in frames of the file
        double inc = 1.0;       // frames per output sample (signed)
        float gl = 1.0f, gr = 1.0f;
        float windowAmount = 0.5f, windowSkew = 0.0f;
        float warpAmount[2] = { 0.0f, 0.0f };
    };

    void spawn (const GrainParams& p, int delay, int age, double headFraction) noexcept;
    void advanceHead (const GrainParams& p) noexcept;
    float random01() noexcept;

    const SampleView* sample_ = nullptr;
    double sampleRate_ = 44100.0;
    bool active_ = false, released_ = false, headDone_ = false;
    double head_ = 0.0;         // play head, fraction of the file
    int headDirection_ = 1;     // ping pong state
    double spawnTimer_ = 0.0;   // samples until the next spawn
    uint32_t rng_ = 0x2545F491u;
    float lastNorm_ = 1.0f;
    Grain grains_[kMaxGrains];
};
} // namespace granular
} // namespace synthyboy
