// SynthyBoy - wavetable-edit: Real-time safe wavetable player implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::wtedit
{
void WavetablePlayer::prepare(double sampleRate, int /*maxBlockFrames*/)
{
    sampleRate_ = (sampleRate >= 22050.0 && sampleRate <= 192000.0) ? sampleRate : 44100.0;
    phase_ = 0.0;
}

void WavetablePlayer::reset() noexcept
{
    phase_ = 0.0;
}

void WavetablePlayer::setWavetable(const Wavetable* wt) noexcept
{
    currentWavetable_ = wt;
}

void WavetablePlayer::process(const PlayerParams& params, float* output, int numSamples) noexcept
{
    if (output == nullptr || numSamples <= 0)
        return;

    if (currentWavetable_ == nullptr || currentWavetable_->numFrames() == 0)
    {
        for (int i = 0; i < numSamples; ++i)
            output[i] = 0.0f;
        return;
    }

    const size_t numF = currentWavetable_->numFrames();

    // Sanitize parameters: check NaN/inf and clamp
    float freq = params.frequency;
    if (std::isnan(freq) || std::isinf(freq) || freq < 0.0f)
        freq = 0.0f;
    else if (freq > 24000.0f)
        freq = 24000.0f;

    float gain = params.gain;
    if (std::isnan(gain) || std::isinf(gain))
        gain = 0.0f;

    float pos = params.framePosition;
    if (std::isnan(pos) || std::isinf(pos) || pos < 0.0f)
        pos = 0.0f;
    else if (pos > static_cast<float>(numF - 1))
        pos = static_cast<float>(numF - 1);

    const size_t f0 = static_cast<size_t>(pos);
    const size_t f1 = std::min(numF - 1, f0 + 1);
    const float frameFrac = pos - static_cast<float>(f0);

    const Frame& frame0 = currentWavetable_->getFrame(f0);
    const Frame& frame1 = currentWavetable_->getFrame(f1);

    const double phaseInc = static_cast<double>(freq) / sampleRate_;

    for (int i = 0; i < numSamples; ++i)
    {
        double currentP = phase_ + static_cast<double>(params.phaseOffset);
        currentP -= std::floor(currentP);
        if (currentP < 0.0) currentP += 1.0;

        const double samplePos = currentP * static_cast<double>(kWaveformSize);
        const size_t s0 = static_cast<size_t>(samplePos) % kWaveformSize;
        const size_t s1 = (s0 + 1) % kWaveformSize;
        const float sampleFrac = static_cast<float>(samplePos - static_cast<double>(s0));

        const float val0 = (1.0f - sampleFrac) * frame0[s0] + sampleFrac * frame0[s1];
        const float val1 = (1.0f - sampleFrac) * frame1[s0] + sampleFrac * frame1[s1];

        float out = ((1.0f - frameFrac) * val0 + frameFrac * val1) * gain;

        // Anti-denormal and NaN/inf protection
        if (std::isnan(out) || std::isinf(out))
            out = 0.0f;
        out += 1e-30f;
        out -= 1e-30f;

        output[i] = out;

        phase_ += phaseInc;
        if (phase_ >= 1.0)
            phase_ -= std::floor(phase_);
    }
}

} // namespace synthyboy::wtedit
