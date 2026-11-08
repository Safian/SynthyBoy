// SynthyBoy - own engine: the spike's 8-voice synth behind IEngine, without JUCE.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "synthyboy/engine.h"
#include "voice.h"

namespace synthyboy
{
namespace own
{
/** Linear ramp to the target over a fixed number of samples (same behaviour as juce::SmoothedValue, linear). */
class LinearSmoother
{
public:
    void reset (double sampleRate, double seconds) noexcept
    {
        steps = static_cast<int> (std::floor (seconds * sampleRate));
        countdown = 0;
    }

    void setCurrentAndTarget (float v) noexcept
    {
        current = target = v;
        countdown = 0;
    }

    void setTarget (float t) noexcept
    {
        if (t == target)
            return;
        if (steps <= 0)
        {
            setCurrentAndTarget (t);
            return;
        }
        target = t;
        countdown = steps;
        step = (target - current) / static_cast<float> (countdown);
    }

    float next() noexcept
    {
        if (countdown <= 0)
            return target;
        if (--countdown > 0)
            current += step;
        else
            current = target;
        return current;
    }

private:
    float current = 1.0f, target = 1.0f, step = 0.0f;
    int steps = 0, countdown = 0;
};

class OwnEngine final : public IEngine
{
public:
    OwnEngine();

    void prepare (double sampleRate, int maxBlockFrames, const Quality& quality) override;
    void setQuality (const Quality& newQuality) noexcept override;
    void reset() override;
    void setParam (uint32_t id, float plain) noexcept override;
    float getParam (uint32_t id) const noexcept override;
    bool supportsParam (uint32_t id) const noexcept override;
    void process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept override;
    int activeVoices() const noexcept override { return activeCount.load (std::memory_order_relaxed); }
    EngineCapsMask capabilities() const noexcept override { return kCapEventContract; } // sustain pedal and per-note tuning (README)

private:
    static constexpr int numChannels = 16;

    void handleEvent (const Event& e) noexcept;
    void noteOn (const Event& e) noexcept;
    void noteOff (int key, int channel) noexcept;
    void setSustain (int channel, bool down) noexcept;
    void renderVoices (float* out, int numFrames) noexcept;
    float readParam (uint32_t id) const noexcept;
    static float gainFromDb (float db) noexcept;

    std::array<std::atomic<float>, kMaxParams> params;
    std::atomic<int> voiceCap { 8 };
    std::atomic<int> activeCount { 0 };

    std::vector<Voice> voices;
    std::vector<uint8_t> sustainPending; // per voice: released by its key while the pedal was down
    bool sustainDown[numChannels] = {};
    LinearSmoother smoothedGain;
    double sampleRate = 0.0;
    bool prepared = false;
    uint32_t noteCounter = 0;
    float bendSemitones[numChannels] = {};
    Waveform waveform = Waveform::saw;
    float bendRange = 2.0f;
    float noteOffsetSemitones = 0.0f;
    float filterCoeff = 1.0f;
};
} // namespace own
} // namespace synthyboy
