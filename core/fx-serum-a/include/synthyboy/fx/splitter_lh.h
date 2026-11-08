// SynthyBoy - Splitter L/H (Low/High 2-band Linkwitz-Riley crossover splitter and recombiner).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <vector>

#include "synthyboy/fx/dsp_utils.h"

namespace synthyboy::fx
{
class SplitterLH
{
public:
    struct Params
    {
        bool  enabled = true;
        float splitFreq = 500.0f;     // 20.0 .. 20000.0 Hz
        bool  lowBypass = false;
        float lowGainDb = 0.0f;       // -60.0 .. +12.0 dB
        bool  highBypass = false;
        float highGainDb = 0.0f;      // -60.0 .. +12.0 dB
        float mix = 1.0f;             // 0.0 .. 1.0
        float levelDb = 0.0f;         // -24.0 .. +24.0 dB
    };

    SplitterLH() = default;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    void setParams (const Params& p) noexcept { params_ = p; }
    const Params& params() const noexcept { return params_; }

    void process (float* l, float* r, int n) noexcept;

    /** Splits input stereo stream into low and high band stereo streams. */
    void split (const float* inL, const float* inR,
                float* lowL, float* lowR,
                float* highL, float* highR, int n) noexcept;

    /** Recombines low and high band stereo streams into output with gains, mix, level. */
    void merge (const float* lowL, const float* lowR,
                const float* highL, const float* highR,
                float* outL, float* outR, int n) noexcept;

    int latency() const noexcept { return 0; }
    int tailSamples() const noexcept;
    float tailLength() const noexcept;

private:
    Params params_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;

    LinkwitzRiley4LP lpL_;
    LinkwitzRiley4LP lpR_;
    LinkwitzRiley4HP hpL_;
    LinkwitzRiley4HP hpR_;
    float lastSplitFreq_ = -1.0f;

    // Preallocated scratch buffers for process()
    std::vector<float> scratchLowL_;
    std::vector<float> scratchLowR_;
    std::vector<float> scratchHighL_;
    std::vector<float> scratchHighR_;

    // Parameter smoothers
    float currentLowGain_ = 1.0f;
    float currentHighGain_ = 1.0f;
    float currentMix_ = 1.0f;
    float currentLevel_ = 1.0f;
};

} // namespace synthyboy::fx
