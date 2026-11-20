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

    mask_ = cap - 1;
    buf_.assign (static_cast<size_t> (cap), 0.0f);

    reset();
}

void HyperDimension::reset() noexcept
{
    std::fill (buf_.begin(), buf_.end(), 0.0f);
    wr_ = 0;

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

    // What depends only on the parameters, once per call (the same expressions as per sample before): the voice delays, pans and LFO increments
    const float maxExcursion = detune * 0.003f * static_cast<float> (sampleRate_);
    float baseDelay[kMaxUnison] = {}, panL[kMaxUnison] = {}, panR[kMaxUnison] = {}, lfoInc[kMaxUnison] = {};
    for (int v = 0; v < unison; ++v)
    {
        const size_t uv = static_cast<size_t> (v);
        const float vCenter = 0.012f + 0.0025f * (static_cast<float> (v) - static_cast<float> (unison - 1) * 0.5f);
        baseDelay[uv] = vCenter * static_cast<float> (sampleRate_);
        float pan = 0.0f;
        if (unison > 1)
            pan = -1.0f + 2.0f * static_cast<float> (v) / static_cast<float> (unison - 1);
        const float angle = (pan + 1.0f) * (0.25f * kPi);
        panL[uv] = std::cos (angle);
        panR[uv] = std::sin (angle);
        const float lfoFreq = rate * kRateSpreads[uv];
        lfoInc[uv] = kTwoPi * lfoFreq * invFs;
    }
    // A mix that is 0 now and stays 0 makes its part of the sum exactly 0 (the output is the same bit for bit): the work is skipped, the phases still advance
    const bool hypActive = currentHyperMix_ != 0.0f || targetHyperMix != 0.0f; // (with unison 0 the voices add nothing, and the blend still pulls the dry signal down)
    const bool dimActive = currentDimMix_ != 0.0f || targetDimMix != 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i];
        const float inR = r[i];
        const float monoIn = 0.5f * (inL + inR);

        // Advance smoothers
        currentHyperMix_ += stepHyperMix;
        currentDimMix_ += stepDimMix;
        currentLevel_ += stepLevel;

        // The one delay line: written before the voices and the taps read it
        buf_[static_cast<size_t> (wr_)] = monoIn;
        wr_ = (wr_ + 1) & mask_;

        // Process Hyper
        float hypL = 0.0f;
        float hypR = 0.0f;

        if (hypActive)
        {
            for (int v = 0; v < unison; ++v)
            {
                const size_t uv = static_cast<size_t> (v);
                float& phase = hyperLfoPhases_[uv];

                const float delaySamples = baseDelay[uv] + maxExcursion * std::sin (phase);
                const float voiceSample = readCubicFromBuffer (buf_.data(), mask_, wr_, delaySamples);

                // Stereo spread
                hypL += voiceSample * panL[uv];
                hypR += voiceSample * panR[uv];

                // Advance LFO
                phase += lfoInc[uv];
                if (phase >= kTwoPi)
                    phase -= kTwoPi;
            }

            hypL *= hyperVoiceNorm;
            hypR *= hyperVoiceNorm;
        }
        else
        {
            for (int v = 0; v < unison; ++v)
            {
                float& phase = hyperLfoPhases_[static_cast<size_t> (v)];
                phase += lfoInc[static_cast<size_t> (v)];
                if (phase >= kTwoPi)
                    phase -= kTwoPi;
            }
        }

        // Hyper blend
        const float hyperOutL = hypActive ? inL + (hypL - inL) * currentHyperMix_ : inL;
        const float hyperOutR = hypActive ? inR + (hypR - inR) * currentHyperMix_ : inR;

        // Process Dimension: feeds from the input signal (mono sum), which is in the delay line
        float dimL = 0.0f, dimR = 0.0f;
        if (dimActive)
        {
            const float tap1 = readCubicFromBuffer (buf_.data(), mask_, wr_, dimBase1);
            const float tap2 = readCubicFromBuffer (buf_.data(), mask_, wr_, dimBase2);
            const float tap3 = readCubicFromBuffer (buf_.data(), mask_, wr_, dimBase3);
            const float tap4 = readCubicFromBuffer (buf_.data(), mask_, wr_, dimBase4);

            // Slow subtle amplitude modulation
            const float modSin = std::sin (dimLfoPhase_);
            const float modCos = std::cos (dimLfoPhase_);
            dimL = (tap1 * (1.0f + 0.12f * modSin) - tap2 * (1.0f + 0.12f * modCos)) * 0.5f;
            dimR = (-tap3 * (1.0f - 0.12f * modSin) + tap4 * (1.0f - 0.12f * modCos)) * 0.5f;
        }
        dimLfoPhase_ += dimLfoInc;
        if (dimLfoPhase_ >= kTwoPi)
            dimLfoPhase_ -= kTwoPi;

        // Dimension blend
        const float wetL = dimActive ? hyperOutL + dimL * currentDimMix_ : hyperOutL;
        const float wetR = dimActive ? hyperOutR + dimR * currentDimMix_ : hyperOutR;

        // Apply final level
        l[i] = flushDenormal (wetL * currentLevel_);
        r[i] = flushDenormal (wetR * currentLevel_);
    }

    currentHyperMix_ = targetHyperMix;
    currentDimMix_ = targetDimMix;
    currentLevel_ = targetLevel;
}

} // namespace synthyboy::fx
