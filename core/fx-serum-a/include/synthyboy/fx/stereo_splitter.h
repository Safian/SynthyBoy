// SynthyBoy - Stereo Splitter (Left/Right channel splitter, router and recombiner).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <vector>

#include "synthyboy/fx/dsp_utils.h"

namespace synthyboy::fx
{
class StereoSplitter
{
public:
    struct Params
    {
        bool  enabled = true;
        bool  leftBypass = false;
        float leftGainDb = 0.0f;      // -60.0 .. +12.0 dB
        float leftPan = -1.0f;        // -1.0 (hard L) .. +1.0 (hard R)
        bool  rightBypass = false;
        float rightGainDb = 0.0f;     // -60.0 .. +12.0 dB
        float rightPan = 1.0f;        // -1.0 (hard L) .. +1.0 (hard R)
        float mix = 1.0f;             // 0.0 .. 1.0
        float levelDb = 0.0f;         // -24.0 .. +24.0 dB
    };

    StereoSplitter() = default;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    void setParams (const Params& p) noexcept { params_ = p; }
    const Params& params() const noexcept { return params_; }

    void process (float* l, float* r, int n) noexcept;

    /** Splits input stereo stream into discrete Left and Right channels. */
    void split (const float* inL, const float* inR,
                float* leftCh, float* rightCh, int n) noexcept;

    /** Recombines Left and Right channels into stereo output with gains, pan, mix, level. */
    void merge (const float* leftCh, const float* rightCh,
                float* outL, float* outR, int n) noexcept;

    int latency() const noexcept { return 0; }
    int tailSamples() const noexcept { return 0; }
    float tailLength() const noexcept { return 0.0f; }

private:
    Params params_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;

    // Preallocated scratch buffers
    std::vector<float> scratchLeft_;
    std::vector<float> scratchRight_;

    // Parameter smoothers
    float currentLeftGain_ = 1.0f;
    float currentRightGain_ = 1.0f;
    float currentLeftPan_ = -1.0f;
    float currentRightPan_ = 1.0f;
    float currentMix_ = 1.0f;
    float currentLevel_ = 1.0f;
};

} // namespace synthyboy::fx
