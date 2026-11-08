// SynthyBoy - Hyper/Dimension effect (unison micro-delay chorus and 4-tap out-of-phase expander).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

namespace synthyboy::fx
{
class HyperDimension
{
public:
    static constexpr int kMaxUnison = 7;

    struct Params
    {
        bool  enabled = true;
        float hyperRate = 1.0f;       // 0.05 .. 10.0 Hz
        int   hyperUnison = 4;        // 0 .. 7 (0 = off)
        float hyperDetune = 0.25f;    // 0.0 .. 1.0
        bool  hyperRetrig = false;    // note-on reset
        float hyperMix = 0.5f;        // 0.0 .. 1.0 (0 = 100% dry)
        float dimSize = 0.5f;         // 0.0 .. 1.0
        float dimMix = 0.5f;          // 0.0 .. 1.0 (0 = 100% dry)
        float levelDb = 0.0f;         // -24.0 .. +24.0 dB
    };

    HyperDimension() = default;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    void retrigger() noexcept;

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

    // Hyper voice delay lines & LFOs
    std::array<std::vector<float>, kMaxUnison> hyperBuffers_;
    std::array<int, kMaxUnison> hyperWriteIndices_ {};
    int hyperMask_ = 0;
    std::array<float, kMaxUnison> hyperLfoPhases_ {};

    // Dimension delay lines
    std::array<std::vector<float>, 4> dimBuffers_;
    std::array<int, 4> dimWriteIndices_ {};
    int dimMask_ = 0;
    float dimLfoPhase_ = 0.0f;

    // Parameter smoothing targets
    float currentHyperMix_ = 0.5f;
    float currentDimMix_ = 0.5f;
    float currentLevel_ = 1.0f;
};

} // namespace synthyboy::fx
