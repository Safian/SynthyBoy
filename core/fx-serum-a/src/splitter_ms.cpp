// SynthyBoy - Splitter M/S implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx/splitter_ms.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::fx
{
void SplitterMS::prepare (double sampleRate, int maxBlock)
{
    sampleRate_ = (sampleRate > 8000.0) ? sampleRate : 44100.0;
    maxBlock_ = (maxBlock > 0) ? maxBlock : 512;

    scratchMidL_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchMidR_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchSideL_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchSideR_.assign (static_cast<size_t> (maxBlock_), 0.0f);

    reset();
}

void SplitterMS::reset() noexcept
{
    std::fill (scratchMidL_.begin(), scratchMidL_.end(), 0.0f);
    std::fill (scratchMidR_.begin(), scratchMidR_.end(), 0.0f);
    std::fill (scratchSideL_.begin(), scratchSideL_.end(), 0.0f);
    std::fill (scratchSideR_.begin(), scratchSideR_.end(), 0.0f);

    currentMidGain_ = params_.midBypass ? 0.0f : dbToGain (params_.midGainDb);
    currentSideGain_ = params_.sideBypass ? 0.0f : dbToGain (params_.sideGainDb);
    currentMix_ = params_.mix;
    currentLevel_ = dbToGain (params_.levelDb);
}

void SplitterMS::split (const float* inL, const float* inR,
                        float* midL, float* midR,
                        float* sideL, float* sideR, int n) noexcept
{
    if (n <= 0)
        return;

    for (int i = 0; i < n; ++i)
    {
        const float sL = inL[i];
        const float sR = inR[i];
        const float m = 0.5f * (sL + sR);
        const float s = 0.5f * (sL - sR);

        midL[i] = m;
        midR[i] = m;
        sideL[i] = s;
        sideR[i] = -s;
    }
}

void SplitterMS::merge (const float* midL, const float* midR,
                        const float* sideL, const float* sideR,
                        float* outL, float* outR, int n) noexcept
{
    if (n <= 0)
        return;

    const float targetMidGain = params_.midBypass ? 0.0f : dbToGain (params_.midGainDb);
    const float targetSideGain = params_.sideBypass ? 0.0f : dbToGain (params_.sideGainDb);
    const float targetMix = std::clamp (params_.mix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);

    const float invN = 1.0f / static_cast<float> (n);
    const float stepMidGain = (targetMidGain - currentMidGain_) * invN;
    const float stepSideGain = (targetSideGain - currentSideGain_) * invN;
    const float stepMix = (targetMix - currentMix_) * invN;
    const float stepLevel = (targetLevel - currentLevel_) * invN;

    for (int i = 0; i < n; ++i)
    {
        currentMidGain_ += stepMidGain;
        currentSideGain_ += stepSideGain;
        currentMix_ += stepMix;
        currentLevel_ += stepLevel;

        const float wetL = midL[i] * currentMidGain_ + sideL[i] * currentSideGain_;
        const float wetR = midR[i] * currentMidGain_ + sideR[i] * currentSideGain_;
        const float dryL = midL[i] + sideL[i];
        const float dryR = midR[i] + sideR[i];

        const float outSampleL = dryL + (wetL - dryL) * currentMix_;
        const float outSampleR = dryR + (wetR - dryR) * currentMix_;

        outL[i] = flushDenormal (outSampleL * currentLevel_);
        outR[i] = flushDenormal (outSampleR * currentLevel_);
    }

    currentMidGain_ = targetMidGain;
    currentSideGain_ = targetSideGain;
    currentMix_ = targetMix;
    currentLevel_ = targetLevel;
}

void SplitterMS::process (float* l, float* r, int n) noexcept
{
    if (n <= 0)
        return;

    if (! params_.enabled)
        return;

    const float targetMix = std::clamp (params_.mix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);

    const bool isBypassed = (targetMix == 0.0f && targetLevel == 1.0f
                             && currentMix_ == 0.0f && currentLevel_ == 1.0f);
    if (isBypassed)
        return;

    int processed = 0;
    while (processed < n)
    {
        const int block = std::min (n - processed, maxBlock_);
        float* curL = l + processed;
        float* curR = r + processed;

        split (curL, curR,
               scratchMidL_.data(), scratchMidR_.data(),
               scratchSideL_.data(), scratchSideR_.data(),
               block);

        merge (scratchMidL_.data(), scratchMidR_.data(),
               scratchSideL_.data(), scratchSideR_.data(),
               curL, curR,
               block);

        processed += block;
    }
}

} // namespace synthyboy::fx
