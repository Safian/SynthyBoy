// SynthyBoy - own engine: the built-in band-limited wavetables (original definitions, additive spectra + inverse FFT).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace synthyboy
{
namespace own
{
/** One wavetable: `numFrames` single cycles, each stored as kNumLevels band-limited copies (mip levels). Level m keeps the harmonics
    1 .. maxHarmonics (m): octave steps from 1024 down to 256 harmonics, half octave steps below (that is where the pitches of music are: from about 94 Hz up). Memory: a level is
    stored with only as many samples as its band needs (eight times its highest harmonic, at most 2048, at least 16: the interpolation images of the 4 point read
    stay as low as they were with 2048 samples at every level - at 4 times the oversampling they alias into the audible band, measured at -43 dB against -58 dB)
    - the 18 levels cost 6.0 times one cycle instead of 18 (the eight 64 frame tables of the bank: 29 MB instead of 46 MB with the 11 octave levels of 2048
    samples, measured by `own_wavetable_bank_memory`).
    A cycle is stored with one guard sample in front and two behind (the 4 point interpolation never wraps: indices -1 .. size (level) + 2 are valid).
    The spectrum of every frame (cosine / sine coefficients, harmonics 1 .. 1023) is kept too: the spectral morph rebuilds a cycle from it.
    Immutable after construction: shared by all voices and engines. */
class Wavetable
{
public:
    static constexpr int kCycle = 2048;
    static constexpr int kNumLevels = 18; // 1024, 512, 256 harmonics, then half octave steps (181, 128, 91 ... 1)
    static constexpr int kHarmonics = kCycle / 2; // spectrum arrays are indexed 0 .. kHarmonics - 1 (0 unused)

    /** Samples per cycle of a level (power of two). */
    static int size (int level) noexcept;

    int numFrames() const noexcept { return frames; }

    /** The highest harmonic kept by level m. */
    static int maxHarmonics (int level) noexcept;
    /** The coarsest level that keeps every harmonic below `nyquistOverFundamental` (>= 1). */
    static int levelFor (float nyquistOverFundamental) noexcept;

    /** Pointer to sample 0 of the cycle (index -1 .. kCycle + 2 are valid). */
    const float* cycle (int frame, int level) const noexcept
    {
        return data.data() + static_cast<size_t> (frame) * frameStride + levelOffset[level] + 1;
    }

    /** Cosine coefficients (index h) of a frame; the sine coefficients follow at + kHarmonics. Peak normalised like the time signal. */
    const float* spectrum (int frame) const noexcept { return spec.data() + static_cast<size_t> (frame) * 2 * kHarmonics; }
    size_t memoryBytes() const noexcept { return (data.size() + spec.size()) * sizeof (float); }

    /** Builds a table from per frame harmonic spectra: sine[h] and cosine[h] coefficients (h = 1 .. 1024) of  sum a_h cos (h t) + b_h sin (h t),
        every frame normalised to a peak of 1 when `normalizePeak` (the built-in tables; user tables keep their level: the bake of Render OSC Warp relies on it). */
    void build (const std::vector<std::vector<float>>& cosCoeff, const std::vector<std::vector<float>>& sinCoeff, bool normalizePeak = true);

private:
    int frames = 0;
    size_t frameStride = 0;
    size_t levelOffset[kNumLevels] = {};
    std::vector<float> data, spec;
};

/** Shared immutable bank of the built-in tables (index = oscN.wavetable choice; 13 "Preset table" has no entry here: nullptr; index kNumWavetableChoices is the
    rounded rectangle of the sub oscillator). The first call builds the bank (allocates): call it from prepare (). */
constexpr int kRoundedRectTable = 15;
const Wavetable* builtinWavetable (int choice) noexcept;

/** A table from decoded audio: `samples` holds numFrames * 2048 samples (frames one after the other). Not real time (allocates, FFTs). Null for no frames. */
std::unique_ptr<Wavetable> makeWavetableFromSamples (const float* samples, int numFrames);
/** Total memory of the built-in bank (all tables), in bytes. */
size_t builtinBankBytes() noexcept;
} // namespace own
} // namespace synthyboy
