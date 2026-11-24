// SynthyBoy - Utility effect (gain, pan, width, mono bass, phase invert, filters, DC block).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "synthyboy/fx/dsp_utils.h"

namespace synthyboy::fx
{
class Utility
{
public:
    struct Params
    {
        bool  enabled = true;
        bool  invertL = false;
        bool  invertR = false;
        bool  lpfEnabled = false;
        float lpfFreq = 20000.0f;     // 20.0 .. 20000.0 Hz
        bool  hpfEnabled = false;
        float hpfFreq = 20.0f;        // 20.0 .. 20000.0 Hz
        bool  monoBassEnabled = false;
        float monoBassFreq = 120.0f;  // 20.0 .. 1000.0 Hz
        float width = 1.0f;           // 0.0 (mono) .. 1.0 (normal) .. 2.0 (expanded)
        float pan = 0.0f;             // -1.0 (L) .. 0.0 (center) .. +1.0 (R)
        bool  dcFilter = false;       // sub-audible DC removal
        float mix = 1.0f;             // 0.0 .. 1.0 (0 = 100% dry)
        float levelDb = 0.0f;         // -24.0 .. +24.0 dB
    };

    Utility() = default;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    void setParams (const Params& p) noexcept { params_ = p; }
    const Params& params() const noexcept { return params_; }

    void process (float* l, float* r, int n) noexcept;

    int latency() const noexcept { return 0; }
    int tailSamples() const noexcept;
    float tailLength() const noexcept;

private:
    Params params_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;

    // Filters
    DcBlocker dcBlockerL_;
    DcBlocker dcBlockerR_;

    Biquad hpfL_;
    Biquad hpfR_;
    float lastHpfFreq_ = -1.0f;

    Biquad lpfL_;
    Biquad lpfR_;
    float lastLpfFreq_ = -1.0f;

    // Mono bass crossover (LR4)
    LinkwitzRiley4LP monoBassLpL_;
    LinkwitzRiley4LP monoBassLpR_;
    LinkwitzRiley4HP monoBassHpL_;
    LinkwitzRiley4HP monoBassHpR_;
    float lastMonoBassFreq_ = -1.0f;

    // Parameter smoothers
    float currentWidth_ = 1.0f;
    float currentPan_ = 0.0f;
    float currentMix_ = 1.0f;
    float currentLevel_ = 1.0f;
};

} // namespace synthyboy::fx
