// SynthyBoy - core/fx-serum-b: Serum FX 'Bode' implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bode.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace synthyboy::fx
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

inline float sanitize (float v) noexcept
{
    if (std::abs (v) < 1.0e-15f || ! std::isfinite (v))
        return 0.0f;
    return v;
}

inline float softLimit (float x) noexcept
{
    // Fast cubic soft clipper to keep feedback stable
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}
} // namespace

Bode::Bode() noexcept
{
    initHilbertFilter();
}

void Bode::initHilbertFilter() noexcept
{
    // Blackman-windowed FIR Hilbert transformer of odd length kHilbertTaps
    const int M = kHilbertLatency;
    for (int i = 0; i < kHilbertTaps; ++i)
    {
        const int k = i - M;
        if (k == 0 || (k % 2) == 0)
        {
            m_hilbertCoeffs[static_cast<size_t> (i)] = 0.0f;
        }
        else
        {
            // Ideal impulse response: h[k] = 2 / (pi * k) for odd k
            const double idealH = 2.0 / (kPi * static_cast<double> (k));
            // Blackman window
            const double w = 0.42 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (kHilbertTaps - 1))
                           + 0.08 * std::cos (2.0 * kTwoPi * static_cast<double> (i) / static_cast<double> (kHilbertTaps - 1));
            m_hilbertCoeffs[static_cast<size_t> (i)] = static_cast<float> (idealH * w);
        }
    }
}

void Bode::prepare (double sampleRate, int maxBlock)
{
    m_sampleRate = std::clamp (sampleRate, 22050.0, 192000.0);
    m_maxBlock = std::clamp (maxBlock, 1, 4096);

    m_histL.assign (static_cast<size_t> (kHilbertTaps), 0.0f);
    m_histR.assign (static_cast<size_t> (kHilbertTaps), 0.0f);
    m_histPos = 0;

    m_dryDelayL.assign (static_cast<size_t> (kHilbertLatency), 0.0f);
    m_dryDelayR.assign (static_cast<size_t> (kHilbertLatency), 0.0f);
    m_dryPos = 0;

    // Up to 2.5 seconds delay buffer
    m_delayCapacity = static_cast<int> (std::ceil (2.5 * m_sampleRate));
    m_delayBufL.assign (static_cast<size_t> (m_delayCapacity), 0.0f);
    m_delayBufR.assign (static_cast<size_t> (m_delayCapacity), 0.0f);
    m_delayWritePos = 0;

    reset();
}

void Bode::reset() noexcept
{
    std::fill (m_histL.begin(), m_histL.end(), 0.0f);
    std::fill (m_histR.begin(), m_histR.end(), 0.0f);
    m_histPos = 0;

    std::fill (m_dryDelayL.begin(), m_dryDelayL.end(), 0.0f);
    std::fill (m_dryDelayR.begin(), m_dryDelayR.end(), 0.0f);
    m_dryPos = 0;

    std::fill (m_delayBufL.begin(), m_delayBufL.end(), 0.0f);
    std::fill (m_delayBufR.begin(), m_delayBufR.end(), 0.0f);
    m_delayWritePos = 0;

    m_carrierPhase = 0.0;
    m_blurPhaseL = 0.0;
    m_blurPhaseR = 0.0;
}

void Bode::retrig() noexcept
{
    m_carrierPhase = 0.0;
}

void Bode::process (float* l, float* r, int n) noexcept
{
    if (l == nullptr || r == nullptr || n <= 0)
        return;

    const float shiftNorm = std::clamp (params.shift, -1.0f, 1.0f);
    const float rangeHz = std::clamp (params.range, 0.0f, 5000.0f);
    const double deltaFreq = static_cast<double> (shiftNorm * rangeHz);
    const double phaseInc = kTwoPi * deltaFreq / m_sampleRate;

    // DIR and WIDTH sideband gains
    const float dir = std::clamp (params.dir, -1.0f, 1.0f);
    const float width = std::clamp (params.width, 0.0f, 1.0f);
    const float u = 0.5f * (dir + 1.0f);
    const float w = 0.5f * width * (1.0f - std::abs (dir));
    const float wUpL = std::clamp (u + w, 0.0f, 1.0f);
    const float wUpR = std::clamp (u - w, 0.0f, 1.0f);

    // Feedback and balance
    const float feedback = std::clamp (params.feedback, -0.95f, 0.95f);
    const float balance = std::clamp (params.balance, -1.0f, 1.0f);
    const float b = 0.5f * (balance + 1.0f);

    // Delay time in seconds
    double delaySec = 0.0;
    if (params.bpm && params.tempoBpm > 10.0)
        delaySec = params.delayBeats * (60.0 / params.tempoBpm);
    else
        delaySec = static_cast<double> (params.delay);
    delaySec = std::clamp (delaySec, 0.0001, 2.0);

    const float blurAmount = std::clamp (params.blur, 0.0f, 1.0f);
    const double blurIncL = kTwoPi * 1.3 / m_sampleRate;
    const double blurIncR = kTwoPi * 1.9 / m_sampleRate;

    // Output trim and wet/dry mix
    const float mix = std::clamp (params.mix, 0.0f, 1.0f);
    const float outGain = std::pow (10.0f, std::clamp (params.level, -60.0f, 24.0f) / 20.0f);

    const int M = kHilbertLatency;

    for (int i = 0; i < n; ++i)
    {
        float inL = sanitize (l[i]);
        float inR = sanitize (r[i]);

        if (params.monoInput)
        {
            const float mono = 0.5f * (inL + inR);
            inL = mono;
            inR = mono;
        }

        // Align dry signal with Hilbert filter latency
        float dryL = inL;
        float dryR = inR;
        if (! m_dryDelayL.empty())
        {
            dryL = m_dryDelayL[static_cast<size_t> (m_dryPos)];
            dryR = m_dryDelayR[static_cast<size_t> (m_dryPos)];
            m_dryDelayL[static_cast<size_t> (m_dryPos)] = inL;
            m_dryDelayR[static_cast<size_t> (m_dryPos)] = inR;
            m_dryPos = (m_dryPos + 1) % kHilbertLatency;
        }

        // Delay line reading with Blur modulation
        float delOutL = 0.0f;
        float delOutR = 0.0f;
        if (m_delayCapacity > 0)
        {
            const double modL = blurAmount * 0.002 * std::sin (m_blurPhaseL);
            const double modR = blurAmount * 0.002 * std::cos (m_blurPhaseR);
            m_blurPhaseL += blurIncL;
            if (m_blurPhaseL > kTwoPi) m_blurPhaseL -= kTwoPi;
            m_blurPhaseR += blurIncR;
            if (m_blurPhaseR > kTwoPi) m_blurPhaseR -= kTwoPi;

            const double totalDelayL = std::clamp (delaySec + modL, 0.0001, 2.2) * m_sampleRate;
            const double totalDelayR = std::clamp (delaySec + modR, 0.0001, 2.2) * m_sampleRate;

            // Linear interpolation read
            auto readDelay = [this] (const std::vector<float>& buf, double delaySamples) noexcept -> float
            {
                const double rpos = static_cast<double> (m_delayWritePos) - delaySamples;
                double wrapPos = std::fmod (rpos, static_cast<double> (m_delayCapacity));
                if (wrapPos < 0.0) wrapPos += static_cast<double> (m_delayCapacity);
                const int idx0 = static_cast<int> (std::floor (wrapPos));
                const int idx1 = (idx0 + 1) % m_delayCapacity;
                const float frac = static_cast<float> (wrapPos - static_cast<double> (idx0));
                return buf[static_cast<size_t> (idx0)] * (1.0f - frac) + buf[static_cast<size_t> (idx1)] * frac;
            };

            delOutL = readDelay (m_delayBufL, totalDelayL);
            delOutR = readDelay (m_delayBufR, totalDelayR);
        }

        // Add feedback to input before Hilbert frequency shifter
        const float shiftInL = softLimit (inL + feedback * delOutL);
        const float shiftInR = softLimit (inR + feedback * delOutR);

        // Store into Hilbert history ring buffers
        m_histL[static_cast<size_t> (m_histPos)] = shiftInL;
        m_histR[static_cast<size_t> (m_histPos)] = shiftInR;

        // In-phase signal is the center tap (delayed by M)
        const int centerIdx = (m_histPos - M + kHilbertTaps) % kHilbertTaps;
        const float iL = m_histL[static_cast<size_t> (centerIdx)];
        const float iR = m_histR[static_cast<size_t> (centerIdx)];

        // Quadrature signal is the FIR convolution
        float qL = 0.0f;
        float qR = 0.0f;
        for (int k = 1; k <= M; k += 2)
        {
            const float c = m_hilbertCoeffs[static_cast<size_t> (M + k)];
            const int idxPos = (m_histPos - (M + k) + kHilbertTaps) % kHilbertTaps;
            const int idxNeg = (m_histPos - (M - k) + kHilbertTaps) % kHilbertTaps;
            qL += c * (m_histL[static_cast<size_t> (idxPos)] - m_histL[static_cast<size_t> (idxNeg)]);
            qR += c * (m_histR[static_cast<size_t> (idxPos)] - m_histR[static_cast<size_t> (idxNeg)]);
        }

        m_histPos = (m_histPos + 1) % kHilbertTaps;

        // Single-sideband modulation
        const float cosOsc = static_cast<float> (std::cos (m_carrierPhase));
        const float sinOsc = static_cast<float> (std::sin (m_carrierPhase));
        m_carrierPhase += phaseInc;
        if (m_carrierPhase >= kTwoPi) m_carrierPhase -= kTwoPi;
        if (m_carrierPhase < 0.0) m_carrierPhase += kTwoPi;

        const float upL = iL * cosOsc - qL * sinOsc;
        const float downL = iL * cosOsc + qL * sinOsc;

        const float upR = iR * cosOsc - qR * sinOsc;
        const float downR = iR * cosOsc + qR * sinOsc;

        // Direction and Width mixing
        const float shiftedL = wUpL * upL + (1.0f - wUpL) * downL;
        const float shiftedR = wUpR * upR + (1.0f - wUpR) * downR;

        // Write to delay buffer based on BALANCE parameter
        if (m_delayCapacity > 0)
        {
            m_delayBufL[static_cast<size_t> (m_delayWritePos)] = (1.0f - b) * downL + b * upL;
            m_delayBufR[static_cast<size_t> (m_delayWritePos)] = (1.0f - b) * downR + b * upR;
            m_delayWritePos = (m_delayWritePos + 1) % m_delayCapacity;
        }

        // Wet/Dry mix and output level
        l[i] = sanitize (((1.0f - mix) * dryL + mix * shiftedL) * outGain);
        r[i] = sanitize (((1.0f - mix) * dryR + mix * shiftedR) * outGain);
    }
}
} // namespace synthyboy::fx
