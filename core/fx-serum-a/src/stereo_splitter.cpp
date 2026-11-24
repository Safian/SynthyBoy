// SynthyBoy - Stereo Splitter implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx/stereo_splitter.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::fx
{
void StereoSplitter::prepare (double sampleRate, int maxBlock)
{
    sampleRate_ = (sampleRate > 8000.0) ? sampleRate : 44100.0;
    maxBlock_ = (maxBlock > 0) ? maxBlock : 512;

    scratchLeft_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchRight_.assign (static_cast<size_t> (maxBlock_), 0.0f);

    reset();
}

void StereoSplitter::reset() noexcept
{
    std::fill (scratchLeft_.begin(), scratchLeft_.end(), 0.0f);
    std::fill (scratchRight_.begin(), scratchRight_.end(), 0.0f);

    currentLeftGain_ = params_.leftBypass ? 0.0f : dbToGain (params_.leftGainDb);
    currentRightGain_ = params_.rightBypass ? 0.0f : dbToGain (params_.rightGainDb);
    currentLeftPan_ = params_.leftPan;
    currentRightPan_ = params_.rightPan;
    currentMix_ = params_.mix;
    currentLevel_ = dbToGain (params_.levelDb);
}

void StereoSplitter::split (const float* inL, const float* inR,
                            float* leftCh, float* rightCh, int n) noexcept
{
    if (n <= 0)
        return;

    for (int i = 0; i < n; ++i)
    {
        leftCh[i] = inL[i];
        rightCh[i] = inR[i];
    }
}

void StereoSplitter::merge (const float* leftCh, const float* rightCh,
                            float* outL, float* outR, int n) noexcept
{
    if (n <= 0)
        return;

    const float targetLeftGain = params_.leftBypass ? 0.0f : dbToGain (params_.leftGainDb);
    const float targetRightGain = params_.rightBypass ? 0.0f : dbToGain (params_.rightGainDb);
    const float targetLeftPan = std::clamp (params_.leftPan, -1.0f, 1.0f);
    const float targetRightPan = std::clamp (params_.rightPan, -1.0f, 1.0f);
    const float targetMix = std::clamp (params_.mix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);

    const float invN = 1.0f / static_cast<float> (n);
    const float stepLeftGain = (targetLeftGain - currentLeftGain_) * invN;
    const float stepRightGain = (targetRightGain - currentRightGain_) * invN;
    const float stepLeftPan = (targetLeftPan - currentLeftPan_) * invN;
    const float stepRightPan = (targetRightPan - currentRightPan_) * invN;
    const float stepMix = (targetMix - currentMix_) * invN;
    const float stepLevel = (targetLevel - currentLevel_) * invN;

    for (int i = 0; i < n; ++i)
    {
        currentLeftGain_ += stepLeftGain;
        currentRightGain_ += stepRightGain;
        currentLeftPan_ += stepLeftPan;
        currentRightPan_ += stepRightPan;
        currentMix_ += stepMix;
        currentLevel_ += stepLevel;

        const float lSig = leftCh[i] * currentLeftGain_;
        const float rSig = rightCh[i] * currentRightGain_;

        const float angleL = (currentLeftPan_ + 1.0f) * (0.25f * kPi);
        const float angleR = (currentRightPan_ + 1.0f) * (0.25f * kPi);

        const float wetL = lSig * std::cos (angleL) + rSig * std::cos (angleR);
        const float wetR = lSig * std::sin (angleL) + rSig * std::sin (angleR);
        const float dryL = leftCh[i];
        const float dryR = rightCh[i];

        const float outSampleL = dryL + (wetL - dryL) * currentMix_;
        const float outSampleR = dryR + (wetR - dryR) * currentMix_;

        outL[i] = flushDenormal (outSampleL * currentLevel_);
        outR[i] = flushDenormal (outSampleR * currentLevel_);
    }

    currentLeftGain_ = targetLeftGain;
    currentRightGain_ = targetRightGain;
    currentLeftPan_ = targetLeftPan;
    currentRightPan_ = targetRightPan;
    currentMix_ = targetMix;
    currentLevel_ = targetLevel;
}

void StereoSplitter::process (float* l, float* r, int n) noexcept
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

        split (curL, curR, scratchLeft_.data(), scratchRight_.data(), block);
        merge (scratchLeft_.data(), scratchRight_.data(), curL, curR, block);

        processed += block;
    }
}

} // namespace synthyboy::fx
