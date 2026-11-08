// SynthyBoy - Splitter L/M/H (Low/Mid/High 3-band Linkwitz-Riley crossover splitter and recombiner).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <vector>

#include "synthyboy/fx/dsp_utils.h"

namespace synthyboy::fx
{
class SplitterLMH
{
public:
    struct Params
    {
        bool  enabled = true;
        float splitFreq1 = 250.0f;    // 20.0 .. 20000.0 Hz (Low/Mid)
        float splitFreq2 = 2500.0f;   // 20.0 .. 20000.0 Hz (Mid/High)
        bool  lowBypass = false;
        float lowGainDb = 0.0f;       // -60.0 .. +12.0 dB
        bool  midBypass = false;
        float midGainDb = 0.0f;       // -60.0 .. +12.0 dB
        bool  highBypass = false;
        float highGainDb = 0.0f;      // -60.0 .. +12.0 dB
        float mix = 1.0f;             // 0.0 .. 1.0
        float levelDb = 0.0f;         // -24.0 .. +24.0 dB
    };

    SplitterLMH() = default;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    void setParams (const Params& p) noexcept { params_ = p; }
    const Params& params() const noexcept { return params_; }

    void process (float* l, float* r, int n) noexcept;

    /** Splits input stereo stream into low, mid, and high band stereo streams. */
    void split (const float* inL, const float* inR,
                float* lowL, float* lowR,
                float* midL, float* midR,
                float* highL, float* highR, int n) noexcept;

    /** Recombines low, mid, and high band stereo streams into output with gains, mix, level. */
    void merge (const float* lowL, const float* lowR,
                const float* midL, const float* midR,
                const float* highL, const float* highR,
                float* outL, float* outR, int n) noexcept;

    int latency() const noexcept { return 0; }
    int tailSamples() const noexcept;
    float tailLength() const noexcept;

private:
    Params params_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;

    // Stage 1 (crossover f1: splits Low and Rest)
    LinkwitzRiley4LP lp1L_;
    LinkwitzRiley4LP lp1R_;
    LinkwitzRiley4HP hp1L_;
    LinkwitzRiley4HP hp1R_;
    float lastFreq1_ = -1.0f;

    // Stage 2 (crossover f2: splits Rest into Mid and High)
    LinkwitzRiley4LP lp2L_;
    LinkwitzRiley4LP lp2R_;
    LinkwitzRiley4HP hp2L_;
    LinkwitzRiley4HP hp2R_;
    float lastFreq2_ = -1.0f;

    // Phase alignment for Low band (all-pass at f2)
    LinkwitzRiley4AP ap2L_;
    LinkwitzRiley4AP ap2R_;

    // Preallocated scratch buffers
    std::vector<float> scratchLowL_;
    std::vector<float> scratchLowR_;
    std::vector<float> scratchMidL_;
    std::vector<float> scratchMidR_;
    std::vector<float> scratchHighL_;
    std::vector<float> scratchHighR_;

    // Parameter smoothers
    float currentLowGain_ = 1.0f;
    float currentMidGain_ = 1.0f;
    float currentHighGain_ = 1.0f;
    float currentMix_ = 1.0f;
    float currentLevel_ = 1.0f;
};

} // namespace synthyboy::fx
