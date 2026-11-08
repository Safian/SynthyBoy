// SynthyBoy - core/sample-osc: common types, parameter structures, and sample views.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace synthyboy
{
namespace sampleosc
{
/** Flush values that would become denormal. */
inline float flushDenormal (float v) noexcept
{
    return std::fabs (v) < 1.0e-18f ? 0.0f : v;
}

/** Sanitize floats against NaN, infinity, and denormals. */
inline float sanitizeFloat (float v) noexcept
{
    return std::isfinite (v) ? flushDenormal (v) : 0.0f;
}

/** Playback loop modes. */
enum class LoopMode : uint8_t
{
    none = 0,     // One-shot: play from start to end and stop
    forward = 1,  // Fwd Loop: loops forward between loopStart and loopEnd
    pingPong = 2, // Fwd/Rwd Loop: alternates forward and backward between loopStart and loopEnd
    reverse = 3   // Rev Loop: loops backward repeatedly
};

/** Interpolation quality tiers. */
enum class QualityTier : uint8_t
{
    low = 0,    // 4-point windowed-sinc interpolation, single mip level
    medium = 1, // 8-point windowed-sinc interpolation, nearest mip level
    high = 2    // 16-point windowed-sinc interpolation with inter-mip blending
};

/** Plain C++ parameter struct for the sample oscillator. Real-time safe. */
struct SampleOscParams
{
    int rootKey = 60;                // Root key (MIDI note 0..127; 60 = C3)
    float tune = 0.0f;               // Coarse tuning in semitones (-48 .. +48)
    float fine = 0.0f;               // Fine tuning in cents (-100 .. +100)
    bool pitchTracking = true;       // Note tracking enabled (if false, plays at root key pitch)

    LoopMode loopMode = LoopMode::none;
    float startOffset = 0.0f;        // Normalized playback start position [0.0, 1.0]
    float endOffset = 1.0f;          // Normalized playback end position [0.0, 1.0]
    float loopStart = 0.0f;          // Normalized loop start [0.0, 1.0]
    float loopEnd = 1.0f;            // Normalized loop end [0.0, 1.0]
    float loopCrossfade = 0.0f;      // Normalized crossfade amount [0.0, 1.0] relative to loop length
    bool reverse = false;            // Reverse playback direction

    QualityTier quality = QualityTier::high;

    float level = 1.0f;              // Linear output level [0.0, 2.0]
    float pan = 0.0f;                // Stereo pan [-1.0 = left, 0.0 = center, +1.0 = right]
};

/** UI overview min/max envelope per pixel column. */
struct OverviewPoint
{
    float minVal = 0.0f;
    float maxVal = 0.0f;
};

/** Single mip level data view. */
struct MipLevel
{
    const float* channels[2] = { nullptr, nullptr };
    int64_t numFrames = 0;
};

/** Maximum supported mipmap octaves. */
constexpr int kMaxMipLevels = 8;

/**
 * Non-owning view of audio sample data (mono or stereo).
 * Thread-safe for read operations from the audio thread.
 */
struct SampleView
{
    const float* const* channelData = nullptr; // Level 0 channels (pointers to left and right/mono arrays)
    int numChannels = 1;                        // 1 = mono, 2 = stereo
    int64_t numFrames = 0;                     // Frame count at level 0
    double sampleRate = 44100.0;               // Sample rate of the audio file in Hz
    int rootNote = 60;                         // Default MIDI root note (60 = C3)
    float fineTuneCents = 0.0f;                // Embedded fine-tune offset in cents

    int numMipLevels = 0;                      // 0 or 1 if no downsampled levels, up to kMaxMipLevels
    const MipLevel* mipLevels = nullptr;       // Array of mip levels if precomputed

    bool isValid() const noexcept
    {
        return channelData != nullptr && numFrames > 0 && sampleRate > 0.0 && numChannels >= 1;
    }

    float getSample (int channel, int64_t frame) const noexcept
    {
        if (frame < 0 || frame >= numFrames || channelData == nullptr)
            return 0.0f;
        const int ch = (channel < numChannels) ? channel : 0;
        return channelData[ch] != nullptr ? channelData[ch][frame] : 0.0f;
    }
};

} // namespace sampleosc
} // namespace synthyboy
