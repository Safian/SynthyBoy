// SynthyBoy - Splitter M/S (Mid/Side stereo matrix splitter and recombiner).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <vector>

#include "synthyboy/fx/dsp_utils.h"

namespace synthyboy::fx
{
class SplitterMS
{
public:
    struct Params
    {
        bool  enabled = true;
        bool  midBypass = false;
        float midGainDb = 0.0f;       // -60.0 .. +12.0 dB
        bool  sideBypass = false;
        float sideGainDb = 0.0f;      // -60.0 .. +12.0 dB
        float mix = 1.0f;             // 0.0 .. 1.0
        float levelDb = 0.0f;         // -24.0 .. +24.0 dB
    };

    SplitterMS() = default;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    void setParams (const Params& p) noexcept { params_ = p; }
    const Params& params() const noexcept { return params_; }

    void process (float* l, float* r, int n) noexcept;

    /** Splits input stereo stream into Mid and Side stereo streams. */
    void split (const float* inL, const float* inR,
                float* midL, float* midR,
                float* sideL, float* sideR, int n) noexcept;

    /** Recombines Mid and Side stereo streams into output with gains, mix, level. */
    void merge (const float* midL, const float* midR,
                const float* sideL, const float* sideR,
                float* outL, float* outR, int n) noexcept;

    int latency() const noexcept { return 0; }
    int tailSamples() const noexcept { return 0; }
    float tailLength() const noexcept { return 0.0f; }

private:
    Params params_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;

    // Preallocated scratch buffers
    std::vector<float> scratchMidL_;
    std::vector<float> scratchMidR_;
    std::vector<float> scratchSideL_;
    std::vector<float> scratchSideR_;

    // Parameter smoothers
    float currentMidGain_ = 1.0f;
    float currentSideGain_ = 1.0f;
    float currentMix_ = 1.0f;
    float currentLevel_ = 1.0f;
};

} // namespace synthyboy::fx
