// SynthyBoy - Splitter L/H implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx/splitter_lh.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::fx
{
void SplitterLH::prepare (double sampleRate, int maxBlock)
{
    sampleRate_ = (sampleRate > 8000.0) ? sampleRate : 44100.0;
    maxBlock_ = (maxBlock > 0) ? maxBlock : 512;

    scratchLowL_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchLowR_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchHighL_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchHighR_.assign (static_cast<size_t> (maxBlock_), 0.0f);

    reset();
}

void SplitterLH::reset() noexcept
{
    lpL_.reset();
    lpR_.reset();
    hpL_.reset();
    hpR_.reset();
    lastSplitFreq_ = -1.0f;

    std::fill (scratchLowL_.begin(), scratchLowL_.end(), 0.0f);
    std::fill (scratchLowR_.begin(), scratchLowR_.end(), 0.0f);
    std::fill (scratchHighL_.begin(), scratchHighL_.end(), 0.0f);
    std::fill (scratchHighR_.begin(), scratchHighR_.end(), 0.0f);

    currentLowGain_ = params_.lowBypass ? 0.0f : dbToGain (params_.lowGainDb);
    currentHighGain_ = params_.highBypass ? 0.0f : dbToGain (params_.highGainDb);
    currentMix_ = params_.mix;
    currentLevel_ = dbToGain (params_.levelDb);
}

int SplitterLH::tailSamples() const noexcept
{
    // Filter decay (~15ms)
    return static_cast<int> (0.015 * sampleRate_);
}

float SplitterLH::tailLength() const noexcept
{
    return 0.015f;
}

void SplitterLH::split (const float* inL, const float* inR,
                        float* lowL, float* lowR,
                        float* highL, float* highR, int n) noexcept
{
    if (n <= 0)
        return;

    const float targetFreq = std::clamp (params_.splitFreq, 20.0f, static_cast<float> (sampleRate_ * 0.49));
    if (targetFreq != lastSplitFreq_)
    {
        lastSplitFreq_ = targetFreq;
        lpL_.setCutoff (lastSplitFreq_, sampleRate_);
        lpR_.setCutoff (lastSplitFreq_, sampleRate_);
        hpL_.setCutoff (lastSplitFreq_, sampleRate_);
        hpR_.setCutoff (lastSplitFreq_, sampleRate_);
    }

    for (int i = 0; i < n; ++i)
    {
        const float sL = inL[i];
        const float sR = inR[i];

        lowL[i] = lpL_.process (sL);
        lowR[i] = lpR_.process (sR);
        highL[i] = hpL_.process (sL);
        highR[i] = hpR_.process (sR);
    }
}

void SplitterLH::merge (const float* lowL, const float* lowR,
                        const float* highL, const float* highR,
                        float* outL, float* outR, int n) noexcept
{
    if (n <= 0)
        return;

    const float targetLowGain = params_.lowBypass ? 0.0f : dbToGain (params_.lowGainDb);
    const float targetHighGain = params_.highBypass ? 0.0f : dbToGain (params_.highGainDb);
    const float targetMix = std::clamp (params_.mix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);

    const float invN = 1.0f / static_cast<float> (n);
    const float stepLowGain = (targetLowGain - currentLowGain_) * invN;
    const float stepHighGain = (targetHighGain - currentHighGain_) * invN;
    const float stepMix = (targetMix - currentMix_) * invN;
    const float stepLevel = (targetLevel - currentLevel_) * invN;

    for (int i = 0; i < n; ++i)
    {
        currentLowGain_ += stepLowGain;
        currentHighGain_ += stepHighGain;
        currentMix_ += stepMix;
        currentLevel_ += stepLevel;

        const float wetL = lowL[i] * currentLowGain_ + highL[i] * currentHighGain_;
        const float wetR = lowR[i] * currentLowGain_ + highR[i] * currentHighGain_;
        const float dryL = lowL[i] + highL[i];
        const float dryR = lowR[i] + highR[i];

        const float outSampleL = dryL + (wetL - dryL) * currentMix_;
        const float outSampleR = dryR + (wetR - dryR) * currentMix_;

        outL[i] = flushDenormal (outSampleL * currentLevel_);
        outR[i] = flushDenormal (outSampleR * currentLevel_);
    }

    currentLowGain_ = targetLowGain;
    currentHighGain_ = targetHighGain;
    currentMix_ = targetMix;
    currentLevel_ = targetLevel;
}

void SplitterLH::process (float* l, float* r, int n) noexcept
{
    if (n <= 0)
        return;

    if (! params_.enabled)
        return;

    const float targetMix = std::clamp (params_.mix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);

    // Bypass check: if mix is 0 and level is unity (0 dB), output is bit-for-bit identical to dry
    const bool isBypassed = (targetMix == 0.0f && targetLevel == 1.0f
                             && currentMix_ == 0.0f && currentLevel_ == 1.0f);
    if (isBypassed)
        return;

    // Use preallocated scratch buffers
    int processed = 0;
    while (processed < n)
    {
        const int block = std::min (n - processed, maxBlock_);
        float* curL = l + processed;
        float* curR = r + processed;

        split (curL, curR,
               scratchLowL_.data(), scratchLowR_.data(),
               scratchHighL_.data(), scratchHighR_.data(),
               block);

        merge (scratchLowL_.data(), scratchLowR_.data(),
               scratchHighL_.data(), scratchHighR_.data(),
               curL, curR,
               block);

        processed += block;
    }
}

} // namespace synthyboy::fx
