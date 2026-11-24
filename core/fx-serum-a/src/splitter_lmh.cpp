// SynthyBoy - Splitter L/M/H implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx/splitter_lmh.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::fx
{
void SplitterLMH::prepare (double sampleRate, int maxBlock)
{
    sampleRate_ = (sampleRate > 8000.0) ? sampleRate : 44100.0;
    maxBlock_ = (maxBlock > 0) ? maxBlock : 512;

    scratchLowL_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchLowR_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchMidL_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchMidR_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchHighL_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    scratchHighR_.assign (static_cast<size_t> (maxBlock_), 0.0f);

    reset();
}

void SplitterLMH::reset() noexcept
{
    lp1L_.reset();
    lp1R_.reset();
    hp1L_.reset();
    hp1R_.reset();
    lastFreq1_ = -1.0f;

    lp2L_.reset();
    lp2R_.reset();
    hp2L_.reset();
    hp2R_.reset();
    lastFreq2_ = -1.0f;

    ap2L_.reset();
    ap2R_.reset();

    std::fill (scratchLowL_.begin(), scratchLowL_.end(), 0.0f);
    std::fill (scratchLowR_.begin(), scratchLowR_.end(), 0.0f);
    std::fill (scratchMidL_.begin(), scratchMidL_.end(), 0.0f);
    std::fill (scratchMidR_.begin(), scratchMidR_.end(), 0.0f);
    std::fill (scratchHighL_.begin(), scratchHighL_.end(), 0.0f);
    std::fill (scratchHighR_.begin(), scratchHighR_.end(), 0.0f);

    currentLowGain_ = params_.lowBypass ? 0.0f : dbToGain (params_.lowGainDb);
    currentMidGain_ = params_.midBypass ? 0.0f : dbToGain (params_.midGainDb);
    currentHighGain_ = params_.highBypass ? 0.0f : dbToGain (params_.highGainDb);
    currentMix_ = params_.mix;
    currentLevel_ = dbToGain (params_.levelDb);
}

int SplitterLMH::tailSamples() const noexcept
{
    // Filter decay (~20ms)
    return static_cast<int> (0.020 * sampleRate_);
}

float SplitterLMH::tailLength() const noexcept
{
    return 0.020f;
}

void SplitterLMH::split (const float* inL, const float* inR,
                         float* lowL, float* lowR,
                         float* midL, float* midR,
                         float* highL, float* highR, int n) noexcept
{
    if (n <= 0)
        return;

    const float maxFreq = static_cast<float> (sampleRate_ * 0.49);
    float f1 = std::clamp (params_.splitFreq1, 20.0f, maxFreq);
    float f2 = std::clamp (params_.splitFreq2, 20.0f, maxFreq);
    if (f1 > f2)
        std::swap (f1, f2);

    if (f1 != lastFreq1_)
    {
        lastFreq1_ = f1;
        lp1L_.setCutoff (lastFreq1_, sampleRate_);
        lp1R_.setCutoff (lastFreq1_, sampleRate_);
        hp1L_.setCutoff (lastFreq1_, sampleRate_);
        hp1R_.setCutoff (lastFreq1_, sampleRate_);
    }

    if (f2 != lastFreq2_)
    {
        lastFreq2_ = f2;
        lp2L_.setCutoff (lastFreq2_, sampleRate_);
        lp2R_.setCutoff (lastFreq2_, sampleRate_);
        hp2L_.setCutoff (lastFreq2_, sampleRate_);
        hp2R_.setCutoff (lastFreq2_, sampleRate_);
        ap2L_.setCutoff (lastFreq2_, sampleRate_);
        ap2R_.setCutoff (lastFreq2_, sampleRate_);
    }

    for (int i = 0; i < n; ++i)
    {
        const float sL = inL[i];
        const float sR = inR[i];

        // Stage 1: split at f1
        const float lowRawL = lp1L_.process (sL);
        const float lowRawR = lp1R_.process (sR);
        const float restL = hp1L_.process (sL);
        const float restR = hp1R_.process (sR);

        // Align Low band phase through AP at f2
        lowL[i] = ap2L_.process (lowRawL);
        lowR[i] = ap2R_.process (lowRawR);

        // Stage 2: split rest at f2 into Mid and High
        midL[i] = lp2L_.process (restL);
        midR[i] = lp2R_.process (restR);
        highL[i] = hp2L_.process (restL);
        highR[i] = hp2R_.process (restR);
    }
}

void SplitterLMH::merge (const float* lowL, const float* lowR,
                         const float* midL, const float* midR,
                         const float* highL, const float* highR,
                         float* outL, float* outR, int n) noexcept
{
    if (n <= 0)
        return;

    const float targetLowGain = params_.lowBypass ? 0.0f : dbToGain (params_.lowGainDb);
    const float targetMidGain = params_.midBypass ? 0.0f : dbToGain (params_.midGainDb);
    const float targetHighGain = params_.highBypass ? 0.0f : dbToGain (params_.highGainDb);
    const float targetMix = std::clamp (params_.mix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);

    const float invN = 1.0f / static_cast<float> (n);
    const float stepLowGain = (targetLowGain - currentLowGain_) * invN;
    const float stepMidGain = (targetMidGain - currentMidGain_) * invN;
    const float stepHighGain = (targetHighGain - currentHighGain_) * invN;
    const float stepMix = (targetMix - currentMix_) * invN;
    const float stepLevel = (targetLevel - currentLevel_) * invN;

    for (int i = 0; i < n; ++i)
    {
        currentLowGain_ += stepLowGain;
        currentMidGain_ += stepMidGain;
        currentHighGain_ += stepHighGain;
        currentMix_ += stepMix;
        currentLevel_ += stepLevel;

        const float wetL = lowL[i] * currentLowGain_ + midL[i] * currentMidGain_ + highL[i] * currentHighGain_;
        const float wetR = lowR[i] * currentLowGain_ + midR[i] * currentMidGain_ + highR[i] * currentHighGain_;
        const float dryL = lowL[i] + midL[i] + highL[i];
        const float dryR = lowR[i] + midR[i] + highR[i];

        const float outSampleL = dryL + (wetL - dryL) * currentMix_;
        const float outSampleR = dryR + (wetR - dryR) * currentMix_;

        outL[i] = flushDenormal (outSampleL * currentLevel_);
        outR[i] = flushDenormal (outSampleR * currentLevel_);
    }

    currentLowGain_ = targetLowGain;
    currentMidGain_ = targetMidGain;
    currentHighGain_ = targetHighGain;
    currentMix_ = targetMix;
    currentLevel_ = targetLevel;
}

void SplitterLMH::process (float* l, float* r, int n) noexcept
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

    int processed = 0;
    while (processed < n)
    {
        const int block = std::min (n - processed, maxBlock_);
        float* curL = l + processed;
        float* curR = r + processed;

        split (curL, curR,
               scratchLowL_.data(), scratchLowR_.data(),
               scratchMidL_.data(), scratchMidR_.data(),
               scratchHighL_.data(), scratchHighR_.data(),
               block);

        merge (scratchLowL_.data(), scratchLowR_.data(),
               scratchMidL_.data(), scratchMidR_.data(),
               scratchHighL_.data(), scratchHighR_.data(),
               curL, curR,
               block);

        processed += block;
    }
}

} // namespace synthyboy::fx
