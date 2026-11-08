// SynthyBoy - core/filters-serum: Unified Serum filter interface.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>

#include "synthyboy/filters/filter_types.h"

namespace synthyboy::filters
{

/** Plain C++ parameter struct for Serum filter models. */
struct FilterParams
{
    float cutoff = 1000.0f;  // Primary cutoff / center frequency in Hz [20.0f, 20000.0f]
    float resonance = 0.0f; // Resonance / Q factor [0.0f, 1.0f]
    float drive = 0.0f;     // Input drive / saturation [0.0f, 1.0f]
    float var = 0.0f;       // Contextual Var parameter [0.0f, 1.0f] (FAT, FREQ, MORPH, etc.)
    float blend = 0.0f;     // Blend / morph / style parameter [0.0f, 1.0f]
    float mix = 1.0f;       // Dry/wet mix [0.0f, 1.0f] (1.0f = 100% wet)
};

class FilterImpl;

/**
 * Real-time safe Serum filter instance.
 *
 * All 80 filter types from the Serum 2 manual are supported.
 * Memory allocation is performed only during prepare(); process() is lock-free and allocation-free.
 */
class Filter
{
public:
    Filter();
    ~Filter();

    Filter (const Filter&) = delete;
    Filter& operator= (const Filter&) = delete;
    Filter (Filter&&) noexcept;
    Filter& operator= (Filter&&) noexcept;

    /**
     * Prepares internal delay buffers and smoothers for the given sample rate.
     * Allowed to allocate memory. Safe across sample rates [22050.0, 192000.0].
     */
    void prepare (double sampleRate);

    /** Silences state buffers and resets smoothing filters to current parameter targets. */
    void reset() noexcept;

    /** Sets active filter type from FilterType enum or integer index [0, 79]. */
    void setType (int type) noexcept;

    /** Returns currently selected filter type index. */
    int getType() const noexcept;

    /** Updates target filter parameters. */
    void setParams (const FilterParams& params) noexcept;

    /** Returns current filter parameters. */
    const FilterParams& getParams() const noexcept;

    /** Processes a single sample (real-time safe, no allocation). */
    float process (float in) noexcept;

    /** Alias for process(float) for single-sample processing. */
    float processSample (float in) noexcept { return process (in); }

    /**
     * Processes a block of samples (real-time safe, no allocation).
     * @param in Input audio buffer.
     * @param out Output audio buffer (may be identical to in for in-place processing).
     * @param numSamples Number of samples to process (1 to 4096).
     * @param cutoffMod Optional per-sample cutoff modulation in Hz (nullptr if unmodulated).
     * @param resMod Optional per-sample resonance modulation [0, 1] (nullptr if unmodulated).
     */
    void process (const float* in, float* out, int numSamples,
                  const float* cutoffMod = nullptr,
                  const float* resMod = nullptr) noexcept;

    /** In-place block processing convenience overload. */
    void process (float* inOut, int numSamples,
                  const float* cutoffMod = nullptr,
                  const float* resMod = nullptr) noexcept
    {
        process (inOut, inOut, numSamples, cutoffMod, resMod);
    }

private:
    std::unique_ptr<FilterImpl> impl_;
};

} // namespace synthyboy::filters
