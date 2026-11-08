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

    /** Gain that brings the peak of the whole file to 1 (1 for silence); computed at load time. */
    float normalizeGain() const noexcept { return normalizeGain_; }

    /** Transient detection for the Auto slice mode (non-RT, deterministic: the same file and threshold give the same slices).
        threshold 0..1 (0 = most slices): an onset is a rise of the short-term energy envelope (2 ms resolution) of at least
        `1 - threshold` times the loudest rise, with a minimum distance of 20 ms. Returns slice starts as fractions 0..1 of the
        file, ascending, the first one is always 0; at most maxSlices entries. */
    std::vector<float> detectSlices (float threshold, int maxSlices = 128) const;

    /** The onsets detectSlices () chooses from: position (fraction of the file) and strength (0..1, 1 = the strongest rise), ascending in
        position, at most maxOnsets. A slice list for a threshold is the first slice 0 plus every onset with strength >= 1 - threshold. */
    struct Onset { float position = 0.0f; float strength = 0.0f; };
    std::vector<Onset> detectOnsets (int maxOnsets = 256) const;

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
    float normalizeGain_ = 1.0f;

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
