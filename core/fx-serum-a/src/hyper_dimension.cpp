// SynthyBoy - Hyper/Dimension effect implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx/hyper_dimension.h"

#include <algorithm>
#include <cmath>

#include "synthyboy/fx/dsp_utils.h"

namespace synthyboy::fx
{
namespace
{
constexpr std::array<float, HyperDimension::kMaxUnison> kRateSpreads = {
    0.85f, 0.93f, 1.0f, 1.07f, 1.15f, 1.22f, 1.30f
};

inline float readCubicFromBuffer (const float* buf, int mask, int writeIdx, float delaySamples) noexcept
{
    if (delaySamples < 1.0f)
        delaySamples = 1.0f;
    const int dInt = static_cast<int> (delaySamples);
    const float frac = delaySamples - static_cast<float> (dInt);
    const int i0 = (writeIdx - 1 - dInt) & mask;
    const int im1 = (i0 + 1) & mask;
    const int i1 = (i0 - 1) & mask;
    const int i2 = (i0 - 2) & mask;

    const float y_m1 = buf[im1];
    const float y_0  = buf[i0];
    const float y_1  = buf[i1];
    const float y_2  = buf[i2];

    const float c0 = y_0;
    const float c1 = 0.5f * (y_1 - y_m1);
    const float c2 = y_m1 - 2.5f * y_0 + 2.0f * y_1 - 0.5f * y_2;
    const float c3 = 0.5f * (y_2 - y_m1) + 1.5f * (y_0 - y_1);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}
} // namespace

void HyperDimension::prepare (double sampleRate, int maxBlock)
{
    sampleRate_ = (sampleRate > 8000.0) ? sampleRate : 44100.0;
    maxBlock_ = (maxBlock > 0) ? maxBlock : 512;

    // Maximum delay: ~60ms
    const int minCapacity = static_cast<int> (0.070 * sampleRate_) + 32;
    int cap = 64;
    while (cap < minCapacity)
        cap <<= 1;

    hyperMask_ = cap - 1;
    for (auto& buf : hyperBuffers_)
        buf.assign (static_cast<size_t> (cap), 0.0f);

    dimMask_ = cap - 1;
    for (auto& buf : dimBuffers_)
        buf.assign (static_cast<size_t> (cap), 0.0f);

    reset();
}

void HyperDimension::reset() noexcept
{
    for (auto& buf : hyperBuffers_)
        std::fill (buf.begin(), buf.end(), 0.0f);
    hyperWriteIndices_.fill (0);

    for (auto& buf : dimBuffers_)
        std::fill (buf.begin(), buf.end(), 0.0f);
    dimWriteIndices_.fill (0);

    dimLfoPhase_ = 0.0f;
    retrigger();

    currentHyperMix_ = params_.hyperMix;
    currentDimMix_ = params_.dimMix;
    currentLevel_ = dbToGain (params_.levelDb);
}

void HyperDimension::retrigger() noexcept
{
    for (int v = 0; v < kMaxUnison; ++v)
    {
        hyperLfoPhases_[static_cast<size_t> (v)] = kTwoPi * static_cast<float> (v) / static_cast<float> (kMaxUnison);
    }
}

int HyperDimension::tailSamples() const noexcept
{
    return static_cast<int> (0.060 * sampleRate_);
}

float HyperDimension::tailLength() const noexcept
{
    return 0.060f;
}

void HyperDimension::process (float* l, float* r, int n) noexcept
{
    if (n <= 0)
        return;

    if (! params_.enabled)
        return;

    const float targetHyperMix = std::clamp (params_.hyperMix, 0.0f, 1.0f);
    const float targetDimMix = std::clamp (params_.dimMix, 0.0f, 1.0f);
    const float targetLevel = dbToGain (params_.levelDb);

    // Bypass check: if mix is 0 and level is unity (0 dB), output is bit-for-bit identical to dry
    const bool isBypassed = (targetHyperMix == 0.0f && targetDimMix == 0.0f && targetLevel == 1.0f
                             && currentHyperMix_ == 0.0f && currentDimMix_ == 0.0f && currentLevel_ == 1.0f);
    if (isBypassed)
        return;

    const float invN = 1.0f / static_cast<float> (n);
    const float stepHyperMix = (targetHyperMix - currentHyperMix_) * invN;
    const float stepDimMix = (targetDimMix - currentDimMix_) * invN;
    const float stepLevel = (targetLevel - currentLevel_) * invN;

    const int unison = std::clamp (params_.hyperUnison, 0, kMaxUnison);
    const float rate = std::clamp (params_.hyperRate, 0.05f, 20.0f);
    const float detune = std::clamp (params_.hyperDetune, 0.0f, 1.0f);
    const float invFs = 1.0f / static_cast<float> (sampleRate_);
    const float dimLfoInc = kTwoPi * 0.25f * invFs;

    const float hyperVoiceNorm = (unison > 0) ? (1.0f / std::sqrt (static_cast<float> (unison))) : 0.0f;

    // Dimension base delays
    const float dimBase1 = (0.0050f + 0.0120f * std::clamp (params_.dimSize, 0.0f, 1.0f)) * static_cast<float> (sampleRate_);
    const float dimBase2 = (0.0085f + 0.0180f * std::clamp (params_.dimSize, 0.0f, 1.0f)) * static_cast<float> (sampleRate_);
    const float dimBase3 = (0.0125f + 0.0150f * std::clamp (params_.dimSize, 0.0f, 1.0f)) * static_cast<float> (sampleRate_);
    const float dimBase4 = (0.0175f + 0.0220f * std::clamp (params_.dimSize, 0.0f, 1.0f)) * static_cast<float> (sampleRate_);

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i];
        const float inR = r[i];
        const float monoIn = 0.5f * (inL + inR);

        // Advance smoothers
        currentHyperMix_ += stepHyperMix;
        currentDimMix_ += stepDimMix;
        currentLevel_ += stepLevel;

        // Process Hyper
        float hypL = 0.0f;
        float hypR = 0.0f;

        if (unison > 0)
        {
            for (int v = 0; v < unison; ++v)
            {
                const size_t uv = static_cast<size_t> (v);
                auto& buf = hyperBuffers_[uv];
                int& wr = hyperWriteIndices_[uv];
                float& phase = hyperLfoPhases_[uv];

                buf[static_cast<size_t> (wr)] = monoIn;
                wr = (wr + 1) & hyperMask_;

                // Delay time for voice
                const float vCenter = 0.012f + 0.0025f * (static_cast<float> (v) - static_cast<float> (unison - 1) * 0.5f);
                const float baseDelaySamples = vCenter * static_cast<float> (sampleRate_);
                const float maxExcursion = detune * 0.003f * static_cast<float> (sampleRate_);
                const float delaySamples = baseDelaySamples + maxExcursion * std::sin (phase);

                const float voiceSample = readCubicFromBuffer (buf.data(), hyperMask_, wr, delaySamples);

                // Stereo spread
                float pan = 0.0f;
                if (unison > 1)
                    pan = -1.0f + 2.0f * static_cast<float> (v) / static_cast<float> (unison - 1);
                const float angle = (pan + 1.0f) * (0.25f * kPi);
                hypL += voiceSample * std::cos (angle);
                hypR += voiceSample * std::sin (angle);

                // Advance LFO
                const float lfoFreq = rate * kRateSpreads[uv];
                phase += kTwoPi * lfoFreq * invFs;
                if (phase >= kTwoPi)
                    phase -= kTwoPi;
            }

            hypL *= hyperVoiceNorm;
            hypR *= hyperVoiceNorm;
        }

        // Hyper blend
        const float hyperOutL = inL + (hypL - inL) * currentHyperMix_;
        const float hyperOutR = inR + (hypR - inR) * currentHyperMix_;

        // Process Dimension
        // Dimension feeds from input signal (mono sum)
        for (size_t d = 0; d < 4; ++d)
        {
            auto& buf = dimBuffers_[d];
            int& wr = dimWriteIndices_[d];
            buf[static_cast<size_t> (wr)] = monoIn;
            wr = (wr + 1) & dimMask_;
        }

        const float tap1 = readCubicFromBuffer (dimBuffers_[0].data(), dimMask_, dimWriteIndices_[0], dimBase1);
        const float tap2 = readCubicFromBuffer (dimBuffers_[1].data(), dimMask_, dimWriteIndices_[1], dimBase2);
        const float tap3 = readCubicFromBuffer (dimBuffers_[2].data(), dimMask_, dimWriteIndices_[2], dimBase3);
        const float tap4 = readCubicFromBuffer (dimBuffers_[3].data(), dimMask_, dimWriteIndices_[3], dimBase4);

        // Slow subtle amplitude modulation
        const float modSin = std::sin (dimLfoPhase_);
        const float modCos = std::cos (dimLfoPhase_);
        dimLfoPhase_ += dimLfoInc;
        if (dimLfoPhase_ >= kTwoPi)
            dimLfoPhase_ -= kTwoPi;

        const float dimL = (tap1 * (1.0f + 0.12f * modSin) - tap2 * (1.0f + 0.12f * modCos)) * 0.5f;
        const float dimR = (-tap3 * (1.0f - 0.12f * modSin) + tap4 * (1.0f - 0.12f * modCos)) * 0.5f;

        // Dimension blend
        const float wetL = hyperOutL + dimL * currentDimMix_;
        const float wetR = hyperOutR + dimR * currentDimMix_;

        // Apply final level
        l[i] = flushDenormal (wetL * currentLevel_);
        r[i] = flushDenormal (wetR * currentLevel_);
    }

    currentHyperMix_ = targetHyperMix;
    currentDimMix_ = targetDimMix;
    currentLevel_ = targetLevel;
}

} // namespace synthyboy::fx
