// SynthyBoy - core/sample-osc: non-RT sample buffer container, mipmap generator, and UI overview.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "synthyboy/sampleosc/sample_types.h"
#include "synthyboy/sampleosc/wav_reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace synthyboy
{
namespace sampleosc
{
/**
 * Non-RT owning sample buffer and precomputed mipmap pyramid.
 * Allocations and heavy filtering take place here (prepare/load time).
 * Audio thread accesses sample data via the lightweight, non-allocating SampleView.
 */
class SampleData
{
public:
    SampleData() = default;
    ~SampleData() = default;

    // Non-copyable to prevent accidental large memory copies; movable
    SampleData (const SampleData&) = delete;
    SampleData& operator= (const SampleData&) = delete;
    SampleData (SampleData&&) noexcept;
    SampleData& operator= (SampleData&&) noexcept;

    /** Loads and decodes WAV from memory buffer. Returns true on success. */
    bool loadFromMemory (const void* data, size_t size, const char* filename = nullptr);

    /** Loads and decodes WAV file from disk. Returns true on success. */
    bool loadFromFile (const char* filePath);

    /**
     * Initializes from existing planar float channels.
     * If right is nullptr, sample is treated as mono (right duplicated from left).
     */
    bool loadFromFrames (const float* left, const float* right, int64_t numFrames, double sampleRate,
                         int rootNote = 60, float fineTuneCents = 0.0f);

    /** Clears all sample buffers and mipmaps. */
    void clear() noexcept;

    /** Produces a non-owning real-time view of this sample and its mip levels. */
    SampleView view() const noexcept;

    /**
     * Computes min/max envelope for waveform display with numPixels columns.
     * channel: 0 = left/mono, 1 = right.
     */
    std::vector<OverviewPoint> computeOverview (int numPixels, int channel = 0) const;

    /** Computes both left and right waveform overviews in a single pass. */
    void computeOverviewStereo (int numPixels, std::vector<OverviewPoint>& outLeft,
                                std::vector<OverviewPoint>& outRight) const;

    bool isValid() const noexcept { return numFrames_ > 0 && sampleRate_ > 0.0; }
    int numChannels() const noexcept { return numChannels_; }
    int64_t numFrames() const noexcept { return numFrames_; }
    double sampleRate() const noexcept { return sampleRate_; }

    int rootNote() const noexcept { return rootNote_; }
    void setRootNote (int note) noexcept { rootNote_ = note; }

    float fineTuneCents() const noexcept { return fineTuneCents_; }
    void setFineTuneCents (float cents) noexcept { fineTuneCents_ = cents; }

    int numMipLevels() const noexcept { return numMipLevels_; }

    int64_t loopStart() const noexcept { return loopStart_; }
    int64_t loopEnd() const noexcept { return loopEnd_; }
    int loopType() const noexcept { return loopType_; }
    bool hasSmplLoop() const noexcept { return hasSmplLoop_; }

private:
    void buildMipLevels();
    void updateViews() noexcept;

    int numChannels_ = 0;
    int64_t numFrames_ = 0;
    double sampleRate_ = 44100.0;
    int rootNote_ = 60;
    float fineTuneCents_ = 0.0f;

    int64_t loopStart_ = -1;
    int64_t loopEnd_ = -1;
    int loopType_ = 0;
    bool hasSmplLoop_ = false;

    int numMipLevels_ = 0;
    std::vector<float> leftData_[kMaxMipLevels];
    std::vector<float> rightData_[kMaxMipLevels];
    MipLevel mipLevels_[kMaxMipLevels];
    const float* channelPtrs_[2] = { nullptr, nullptr };
};

} // namespace sampleosc
} // namespace synthyboy
