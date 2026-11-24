// SynthyBoy - core: clean-room wavetable renderer for the component types of the Vital wavetable format.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// docs/ui-design/11-vital-preset-import.md 1.3.1 and Annex A.5 (approved: docs/ui-design/12-decisions.md Q31). Written from the
// procedure of that design only; nothing is taken from the imported Vital sources. Where the design does not define a behaviour the
// code carries a "// SPEC-GAP:" comment (list: grep SPEC-GAP core/). JUCE-free, Vital-free, no I/O, no globals.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace synthyboy
{
namespace wtrender
{
constexpr int kFrameSize = 2048;   // samples per frame (one cycle)
constexpr int kMaxFrames = 257;    // keyframe positions 0 .. 256
constexpr int kMaxPosition = 256;

enum class ComponentType : uint8_t
{
    waveSource,
    lineSource,
    audioFileSource,
    shepardToneSource,
    phaseShift,
    waveWindow,
    frequencyFilter,
    slewLimiter,
    waveFolder,
    waveWarp
};

/** The JSON "type" string of a component type, and the reverse (false for an unknown name). */
const char* componentTypeName (ComponentType type) noexcept;
bool componentTypeFromName (const char* name, ComponentType& out) noexcept;

/** A line (the LFO line object): points x, y in 0..1, one power per segment starting at that point. */
struct Line
{
    std::vector<float> x, y, power; // x.size() == y.size() >= 1; power.size() >= x.size() - 1 (missing powers count as 0)
    bool smooth = false;
};

/** Renders the line into n samples (x = i / n over 0..1, 1 excluded). Power curve per segment, see wavetable_render.cpp. */
void renderLine (const Line& line, float* out, int n) noexcept;

/** One keyframe. A field that does not belong to the component type is ignored. */
struct Keyframe
{
    int position = 0;                   // 0 .. kMaxPosition
    std::vector<float> wave;            // Wave Source: kFrameSize samples (shorter: zero padded, longer: cut)
    Line line;                          // Line Source
    float pullPower = 0.0f;             // Line Source
    float startPosition = 0.0f;         // Audio File Source: first sample of the window
    float windowFade = 1.0f;            // Audio File Source: 0 .. 1
    float windowSize = 2048.0f;         // Audio File Source: samples
    float phase = 0.0f;                 // Phase Shift: 0 .. 1 of a cycle
    float mix = 1.0f;                   // Phase Shift: 0 .. 1
    int style = 0;                      // Phase Shift: 0 normal, 1 harmonic, 2 inharmonic; Frequency Filter: 0 low pass, 1 band pass, 2 high pass, 3 band reject
    float cutoff = 0.5f;                // Frequency Filter: 0 .. 1
    float shape = 0.5f;                 // Frequency Filter: 0 .. 1
    float upRunRise = 0.0f;             // Slew Limiter: 0 .. 1
    float downRunRise = 0.0f;           // Slew Limiter: 0 .. 1
    float leftPosition = 0.0f;          // Wave Window: 0 .. 1
    float rightPosition = 1.0f;         // Wave Window: 0 .. 1
    float windowShape = 0.0f;           // Wave Window: 0 cosine, 1 half sine, 2 linear, 3 square, 4 wiggle
    float foldBoost = 0.0f;             // Wave Folder: 0 .. 1
    float horizontalPower = 0.0f;       // Wave Warp
    float verticalPower = 0.0f;         // Wave Warp
    bool horizontalAsymmetric = false;  // Wave Warp
    bool verticalAsymmetric = false;    // Wave Warp
};

enum class Interpolation : uint8_t { none = 0, linear = 1, cubic = 2 };

struct Component
{
    ComponentType type = ComponentType::waveSource;
    Interpolation interpolationStyle = Interpolation::linear; // between keyframes
    bool frequencyInterpolation = false;                      // Wave Source: interpolate spectra instead of samples
    bool normalize = true;                                    // Frequency Filter
    bool normalizeGain = false;                               // Audio File Source: with normalizeMult, a frame below full scale is raised to it (else only lowered)
    bool normalizeMult = true;                                // Audio File Source: normalise every frame so that its peak is not above 1 (both are JSON booleans)
    int windowSize = 2048;                                    // Audio File Source: default of a keyframe without its own
    int fadeStyle = 0;                                        // Audio File Source: 0 wave blend, 1 none, 2 time, 3 frequency
    int phaseStyle = 0;                                       // Audio File Source: 0 none, 1 clear, 2 vocode
    uint32_t randomSeed = 0;                                  // Audio File Source
    float audioSampleRate = 44100.0f;                         // Audio File Source (kept, not used by the renderer)
    std::vector<int16_t> audio;                               // Audio File Source: mono PCM
    std::vector<Keyframe> keyframes;                          // any order; sorted by position (stable) by the renderer
};

struct Group
{
    std::vector<Component> components; // run in order on one buffer, a source replaces the buffer
};

struct Description
{
    std::vector<Group> groups;         // summed
    bool removeAllDc = false;
    bool fullNormalize = false;
};

struct Options
{
    uint64_t maxOps = 8000000000ull;    // work bound: about frames x components x kFrameSize x constant
    std::function<bool()> cancel;       // polled between components; true = stop
};

enum class Status : uint8_t { ok, cancelled, budgetExceeded };

struct Result
{
    int numFrames = 0;
    std::vector<float> frames; // numFrames * kFrameSize
};

/** out[i] = sum_{k=1..count} cosCoef[k] cos (2 pi k i / kFrameSize) + sinCoef[k] sin (2 pi k i / kFrameSize), i < kFrameSize.
    Arrays are indexed 1..count (index 0 unused); count is limited to kFrameSize / 2 - 1. */
void synthesizeHarmonics (const double* cosCoef, const double* sinCoef, int count, float* out) noexcept;

/** The inverse of synthesizeHarmonics for one frame: cosCoef[k], sinCoef[k], k = 1 .. count (index 0 = DC in cosCoef). */
void analyzeHarmonics (const float* frame, int count, double* cosCoef, double* sinCoef) noexcept;

/** mag[k] = amplitude of harmonic k of one frame (a sine of amplitude 1 gives 1), k = 1 .. count (mag[0] = DC). */
void harmonicMagnitudes (const float* frame, int count, double* mag) noexcept;

/** Frames of a description: the largest keyframe position over all groups and components + 1, in 1 .. kMaxFrames. */
int frameCount (const Description& d) noexcept;

/** Renders the table. Never throws; a std::bad_alloc is mapped to budgetExceeded. */
Status render (const Description& description, const Options& options, Result& result) noexcept;
} // namespace wtrender
} // namespace synthyboy
