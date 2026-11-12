// SynthyBoy - own engine: the built-in band-limited wavetables (original definitions, additive spectra + inverse FFT).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace synthyboy
{
namespace own
{
/** One wavetable: `numFrames` single cycles of kCycle samples, each stored as kNumLevels band-limited copies. Level m keeps the harmonics
    1 .. maxHarmonics (m): half octave steps from 1024 harmonics (level 0) down to 1. A cycle is stored with one guard sample in front and two behind
    (the 4 point interpolation never wraps: indices -1 .. kCycle + 2 are valid). Immutable after construction: shared by all voices and engines. */
class Wavetable
{
public:
    static constexpr int kCycle = 2048;
    static constexpr int kStride = kCycle + 4;
    static constexpr int kNumLevels = 11;

    int numFrames() const noexcept { return frames; }

    /** The highest harmonic kept by level m. */
    static int maxHarmonics (int level) noexcept;
    /** The coarsest level that keeps every harmonic below `nyquistOverFundamental` (>= 1). */
    static int levelFor (float nyquistOverFundamental) noexcept;

    /** Pointer to sample 0 of the cycle (index -1 .. kCycle + 2 are valid). */
    const float* cycle (int frame, int level) const noexcept
    {
        return data.data() + (static_cast<size_t> (frame) * kNumLevels + static_cast<size_t> (level)) * kStride + 1;
    }

    /** Builds a table from per frame harmonic spectra: sine[h] and cosine[h] coefficients (h = 1 .. 1024) of  sum a_h cos (h t) + b_h sin (h t),
        every frame normalised to a peak of 1. */
    void build (const std::vector<std::vector<float>>& cosCoeff, const std::vector<std::vector<float>>& sinCoeff);

private:
    int frames = 0;
    std::vector<float> data;
};

/** Shared immutable bank of the built-in tables (index = oscN.wavetable choice; 13 "Preset table" has no entry here: nullptr; index kNumWavetableChoices is the
    rounded rectangle of the sub oscillator). The first call builds the bank (allocates): call it from prepare (). */
constexpr int kRoundedRectTable = 15;
const Wavetable* builtinWavetable (int choice) noexcept;
} // namespace own
} // namespace synthyboy
