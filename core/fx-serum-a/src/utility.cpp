// SynthyBoy - Utility effect implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx/utility.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::fx
{
void Utility::prepare (double sampleRate, int maxBlock)
{
    sampleRate_ = (sampleRate > 8000.0) ? sampleRate : 44100.0;
    maxBlock_ = (maxBlock > 0) ? maxBlock : 512;

    dcBlockerL_.prepare (sampleRate_);
    dcBlockerR_.prepare (sampleRate_);

    reset();
}

void Utility::reset() noexcept
{
    dcBlockerL_.reset();
    dcBlockerR_.reset();

    hpfL_.reset();
    hpfR_.reset();
    lastHpfFreq_ = -1.0f;

    lpfL_.reset();
    lpfR_.reset();
    lastLpfFreq_ = -1.0f;

    monoBassLpL_.reset();
    monoBassLpR_.reset();
    monoBassHpL_.reset();
    monoBassHpR_.reset();
    lastMonoBassFreq_ = -1.0f;

    currentWidth_ = params_.width;
    currentPan_ = params_.pan;
    currentMix_ = params_.mix;
    currentLevel_ = dbToGain (params_.levelDb);
}

int Utility::tailSamples() const noexcept
{
    // Filter ring-down tail (~10ms)
    return static_cast<int> (0.010 * sampleRate_);
}

float Utility::tailLength() const noexcept
{
    return 0.010f;
}

void Utility::process (float* l, float* r, int n) noexcept
{
    if (n <= 0)
        return;

    if (! params_.enabled)
        return;

    const float targetMix = std::clamp (params_.mix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);
    const float targetWidth = std::clamp (params_.width, 0.0f, 2.0f);
    const float targetPan = std::clamp (params_.pan, -1.0f, 1.0f);

    // Bypass check: if mix is 0 and level is unity (0 dB), output is bit-for-bit identical to dry
    const bool isBypassed = (targetMix == 0.0f && targetLevel == 1.0f
                             && currentMix_ == 0.0f && currentLevel_ == 1.0f);
    if (isBypassed)
        return;

    // Update filter coefficients if frequencies changed
    if (params_.hpfEnabled && params_.hpfFreq != lastHpfFreq_)
    {
        lastHpfFreq_ = params_.hpfFreq;
        hpfL_.setButterworthHP (lastHpfFreq_, sampleRate_);
        hpfR_.setButterworthHP (lastHpfFreq_, sampleRate_);
    }

    if (params_.lpfEnabled && params_.lpfFreq != lastLpfFreq_)
    {
        lastLpfFreq_ = params_.lpfFreq;
        lpfL_.setButterworthLP (lastLpfFreq_, sampleRate_);
        lpfR_.setButterworthLP (lastLpfFreq_, sampleRate_);
    }

    if (params_.monoBassEnabled && params_.monoBassFreq != lastMonoBassFreq_)
    {
        lastMonoBassFreq_ = params_.monoBassFreq;
        monoBassLpL_.setCutoff (lastMonoBassFreq_, sampleRate_);
        monoBassLpR_.setCutoff (lastMonoBassFreq_, sampleRate_);
        monoBassHpL_.setCutoff (lastMonoBassFreq_, sampleRate_);
        monoBassHpR_.setCutoff (lastMonoBassFreq_, sampleRate_);
    }

    const float invN = 1.0f / static_cast<float> (n);
    const float stepMix = (targetMix - currentMix_) * invN;
    const float stepLevel = (targetLevel - currentLevel_) * invN;
    const float stepWidth = (targetWidth - currentWidth_) * invN;
    const float stepPan = (targetPan - currentPan_) * invN;

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i];
        const float inR = r[i];

        currentMix_ += stepMix;
        currentLevel_ += stepLevel;
        currentWidth_ += stepWidth;
        currentPan_ += stepPan;

        float sigL = inL;
        float sigR = inR;

        // 1. DC Filter
        if (params_.dcFilter)
        {
            sigL = dcBlockerL_.process (sigL);
            sigR = dcBlockerR_.process (sigR);
        }

        // 2. Polarity inversion
        if (params_.invertL)
            sigL = -sigL;
        if (params_.invertR)
            sigR = -sigR;

        // 3. High-Pass Filter
        if (params_.hpfEnabled)
        {
            sigL = hpfL_.process (sigL);
            sigR = hpfR_.process (sigR);
        }

        // 4. Low-Pass Filter
        if (params_.lpfEnabled)
        {
            sigL = lpfL_.process (sigL);
            sigR = lpfR_.process (sigR);
        }

        // 5. Mono Bass
        if (params_.monoBassEnabled)
        {
            const float lowL = monoBassLpL_.process (sigL);
            const float lowR = monoBassLpR_.process (sigR);
            const float highL = monoBassHpL_.process (sigL);
            const float highR = monoBassHpR_.process (sigR);

            const float monoLow = 0.5f * (lowL + lowR);
            sigL = monoLow + highL;
            sigR = monoLow + highR;
        }

        // 6. Stereo Width
        const float mid = 0.5f * (sigL + sigR);
        const float side = 0.5f * (sigL - sigR);
        const float modSide = side * currentWidth_;
        sigL = mid + modSide;
        sigR = mid - modSide;

        // 7. Pan balance
        float panGainL = 1.0f;
        float panGainR = 1.0f;
        if (currentPan_ < 0.0f)
        {
            panGainR = 1.0f + currentPan_;
        }
        else if (currentPan_ > 0.0f)
        {
            panGainL = 1.0f - currentPan_;
        }
        sigL *= panGainL;
        sigR *= panGainR;

        // 8. Mix & Level
        const float wetL = inL + (sigL - inL) * currentMix_;
        const float wetR = inR + (sigR - inR) * currentMix_;

        l[i] = flushDenormal (wetL * currentLevel_);
        r[i] = flushDenormal (wetR * currentLevel_);
    }

    currentMix_ = targetMix;
    currentLevel_ = targetLevel;
    currentWidth_ = targetWidth;
    currentPan_ = targetPan;
}

} // namespace synthyboy::fx
