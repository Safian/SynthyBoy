// SynthyBoy - core/fx-serum-b: Serum FX 'Convolve' implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "convolve.h"

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

/** 8-tap windowed-sinc resampler for high quality IR rate conversion and time stretching. */
void resampleSignal (const float* in, int inFrames, float* out, int outFrames, double ratio)
{
    if (in == nullptr || out == nullptr || inFrames <= 0 || outFrames <= 0)
        return;

    if (std::abs (ratio - 1.0) < 1.0e-9 && inFrames == outFrames)
    {
        std::copy (in, in + inFrames, out);
        return;
    }

    constexpr int taps = 8;
    const double cutoff = std::min (1.0, 1.0 / std::max (ratio, 0.01)) * 0.45;

    for (int i = 0; i < outFrames; ++i)
    {
        const double srcPos = static_cast<double> (i) * ratio;
        const int center = static_cast<int> (std::floor (srcPos));
        double val = 0.0;
        double weightSum = 0.0;

        for (int k = center - taps; k <= center + taps; ++k)
        {
            if (k >= 0 && k < inFrames)
            {
                const double delta = srcPos - static_cast<double> (k);
                double w = 1.0;
                if (std::abs (delta) > 1e-7)
                {
                    const double x = kPi * delta;
                    const double sinc = std::sin (x * 2.0 * cutoff) / x;
                    const double win = 0.5 + 0.5 * std::cos (kPi * delta / static_cast<double> (taps + 1));
                    w = sinc * win;
                }
                val += static_cast<double> (in[k]) * w;
                weightSum += w;
            }
        }
        out[i] = static_cast<float> (weightSum > 1e-7 ? val : 0.0);
    }
}
} // namespace

Convolve::Convolve()
{
    m_fft.init (kFftSize);
    m_scratchComplexIn.resize (kFftSize);
    m_scratchComplexSum.resize (kFftSize);

    // Default to a unit impulse so processing without loadIr() acts as identity
    m_rawIr = { 1.0f };
    m_rawFrames = 1;
    m_rawChannels = 1;
    m_rawIrRate = 48000.0;

    prepare (48000.0, 512);
}

void Convolve::prepare (double sampleRate, int maxBlock)
{
    m_sampleRate = std::clamp (sampleRate, 22050.0, 192000.0);
    m_maxBlock = std::clamp (maxBlock, 1, 4096);

    m_fft.init (kFftSize);
    m_scratchComplexIn.resize (kFftSize);
    m_scratchComplexSum.resize (kFftSize);

    // Pre-delay buffer (up to 2.5 seconds)
    m_preDelayCapacity = static_cast<int> (std::ceil (2.5 * m_sampleRate));
    m_preDelayBufL.assign (static_cast<size_t> (m_preDelayCapacity), 0.0f);
    m_preDelayBufR.assign (static_cast<size_t> (m_preDelayCapacity), 0.0f);
    m_preDelayWritePos = 0;

    rebuildPartitions();
    reset();
}

void Convolve::reset() noexcept
{
    m_headHistoryL.assign (static_cast<size_t> (kPartitionSize * 2), 0.0f);
    m_headHistoryR.assign (static_cast<size_t> (kPartitionSize * 2), 0.0f);
    m_headHistPos = 0;

    m_blockInL.assign (static_cast<size_t> (kPartitionSize), 0.0f);
    m_blockInR.assign (static_cast<size_t> (kPartitionSize), 0.0f);
    m_blockPos = 0;

    m_currentTailBlockL.assign (static_cast<size_t> (kPartitionSize), 0.0f);
    m_currentTailBlockR.assign (static_cast<size_t> (kPartitionSize), 0.0f);
    m_overlapL.assign (static_cast<size_t> (kPartitionSize), 0.0f);
    m_overlapR.assign (static_cast<size_t> (kPartitionSize), 0.0f);

    for (auto& v : m_tailFftHistL)
        std::fill (v.begin(), v.end(), Complex (0.0f, 0.0f));
    for (auto& v : m_tailFftHistR)
        std::fill (v.begin(), v.end(), Complex (0.0f, 0.0f));
    m_tailFftHistHead = 0;

    std::fill (m_preDelayBufL.begin(), m_preDelayBufL.end(), 0.0f);
    std::fill (m_preDelayBufR.begin(), m_preDelayBufR.end(), 0.0f);
    m_preDelayWritePos = 0;

    m_toneStateL = 0.0f;
    m_toneStateR = 0.0f;
}

bool Convolve::loadIr (const float* data, int frames, int channels, double irRate)
{
    if (data == nullptr || frames <= 0 || (channels != 1 && channels != 2) || irRate <= 0.0)
        return false;

    const int clampedFrames = std::min (frames, kMaxIrFrames);
    const size_t totalSamples = static_cast<size_t> (clampedFrames * channels);

    m_rawIr.assign (data, data + totalSamples);
    m_rawFrames = clampedFrames;
    m_rawChannels = channels;
    m_rawIrRate = irRate;

    rebuildPartitions();
    reset();
    return true;
}

void Convolve::convertToMinimumPhase (std::vector<float>& ir, const Radix2FFT& fft)
{
    const int N = fft.size();
    if (N <= 0 || ir.empty())
        return;

    std::vector<Complex> buf (static_cast<size_t> (N));
    fft.forwardReal (ir.data(), static_cast<int> (ir.size()), buf.data());

    // Cepstrum: ln(|H(k)|)
    for (int k = 0; k < N; ++k)
    {
        const float mag = std::max (buf[static_cast<size_t> (k)].abs(), 1.0e-12f);
        buf[static_cast<size_t> (k)] = Complex (std::log (mag), 0.0f);
    }

    fft.ifft (buf.data());

    // Minimum phase cepstrum windowing
    // c_min[0] = c[0], c_min[k] = 2*c[k] for 1 <= k < N/2, c_min[N/2] = c[N/2], rest 0
    buf[0].i = 0.0f;
    for (int k = 1; k < N / 2; ++k)
    {
        buf[static_cast<size_t> (k)].r *= 2.0f;
        buf[static_cast<size_t> (k)].i = 0.0f;
    }
    buf[static_cast<size_t> (N / 2)].i = 0.0f;
    for (int k = N / 2 + 1; k < N; ++k)
        buf[static_cast<size_t> (k)] = Complex (0.0f, 0.0f);

    fft.fft (buf.data());

    // Complex exponential: exp(C_min(k))
    for (int k = 0; k < N; ++k)
    {
        const float mag = std::exp (buf[static_cast<size_t> (k)].r);
        const float phase = buf[static_cast<size_t> (k)].i;
        buf[static_cast<size_t> (k)] = Complex (mag * std::cos (phase), mag * std::sin (phase));
    }

    fft.ifft (buf.data());

    for (size_t i = 0; i < ir.size() && i < static_cast<size_t> (N); ++i)
        ir[i] = buf[i].r;
}

void Convolve::rebuildPartitions()
{
    if (m_rawIr.empty() || m_rawFrames <= 0)
    {
        m_headL.assign (static_cast<size_t> (kPartitionSize), 0.0f);
        m_headR.assign (static_cast<size_t> (kPartitionSize), 0.0f);
        m_headL[0] = 1.0f;
        m_headR[0] = 1.0f;
        m_numTailPartitions = 1;
        m_tailSpectraL.assign (1, std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));
        m_tailSpectraR.assign (1, std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));
        m_tailFftHistL.assign (1, std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));
        m_tailFftHistR.assign (1, std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));
        return;
    }

    // 1. Resample raw IR to engine sample rate and stretch/contract by params.size
    const float sizeParam = std::clamp (params.size, 0.25f, 4.0f);
    const double resampleRatio = (m_rawIrRate / m_sampleRate) / static_cast<double> (sizeParam);
    const int targetFrames = std::clamp (static_cast<int> (std::round (static_cast<double> (m_rawFrames) / resampleRatio)),
                                         1,
                                         kMaxIrFrames);

    std::vector<float> irL (static_cast<size_t> (targetFrames), 0.0f);
    std::vector<float> irR (static_cast<size_t> (targetFrames), 0.0f);

    if (m_rawChannels == 1)
    {
        resampleSignal (m_rawIr.data(), m_rawFrames, irL.data(), targetFrames, resampleRatio);
        irR = irL;
    }
    else
    {
        // De-interleave
        std::vector<float> srcL (static_cast<size_t> (m_rawFrames));
        std::vector<float> srcR (static_cast<size_t> (m_rawFrames));
        for (int i = 0; i < m_rawFrames; ++i)
        {
            srcL[static_cast<size_t> (i)] = m_rawIr[static_cast<size_t> (i * 2)];
            srcR[static_cast<size_t> (i)] = m_rawIr[static_cast<size_t> (i * 2 + 1)];
        }
        resampleSignal (srcL.data(), m_rawFrames, irL.data(), targetFrames, resampleRatio);
        resampleSignal (srcR.data(), m_rawFrames, irR.data(), targetFrames, resampleRatio);
    }

    // 2. Attack fade-in
    if (params.attack > 0.0f)
    {
        const int attFrames = std::min (targetFrames, static_cast<int> (std::round (params.attack * m_sampleRate)));
        for (int i = 0; i < attFrames; ++i)
        {
            const float ramp = 0.5f * (1.0f - std::cos (static_cast<float> (kPi * i / attFrames)));
            irL[static_cast<size_t> (i)] *= ramp;
            irR[static_cast<size_t> (i)] *= ramp;
        }
    }

    // 3. Decay shortening
    if (params.decay > 0.0f)
    {
        const double decaySec = std::max (0.001, static_cast<double> (params.decay));
        const double decayFactor = 3.0 / (decaySec * m_sampleRate);
        for (int i = 0; i < targetFrames; ++i)
        {
            const float env = static_cast<float> (std::exp (-decayFactor * static_cast<double> (i)));
            irL[static_cast<size_t> (i)] *= env;
            irR[static_cast<size_t> (i)] *= env;
        }
    }

    // 4. Damp (high frequency damping across time)
    if (params.damp > 0.0f)
    {
        const float dampAmount = std::clamp (params.damp, 0.0f, 1.0f);
        float stateL = 0.0f;
        float stateR = 0.0f;
        for (int i = 0; i < targetFrames; ++i)
        {
            const float progress = static_cast<float> (i) / static_cast<float> (targetFrames);
            const float alpha = std::clamp (1.0f - dampAmount * progress * 0.95f, 0.05f, 1.0f);
            stateL += alpha * (irL[static_cast<size_t> (i)] - stateL);
            stateR += alpha * (irR[static_cast<size_t> (i)] - stateR);
            irL[static_cast<size_t> (i)] = stateL;
            irR[static_cast<size_t> (i)] = stateR;
        }
    }

    // 5. Minimum Phase conversion (phi MIN)
    if (params.minimumPhase)
    {
        // Find next power of 2 >= targetFrames
        int minFftSize = 256;
        while (minFftSize < targetFrames && minFftSize < 65536)
            minFftSize <<= 1;
        Radix2FFT minFft (minFftSize);
        convertToMinimumPhase (irL, minFft);
        convertToMinimumPhase (irR, minFft);
    }

    // 6. IR Gain
    const float irGainFactor = std::pow (10.0f, std::clamp (params.irGain, -24.0f, 24.0f) / 20.0f);
    for (int i = 0; i < targetFrames; ++i)
    {
        irL[static_cast<size_t> (i)] *= irGainFactor;
        irR[static_cast<size_t> (i)] *= irGainFactor;
    }

    // 7. Partition into Head (first kPartitionSize samples) and Tail (remaining partitions)
    m_headL.assign (static_cast<size_t> (kPartitionSize), 0.0f);
    m_headR.assign (static_cast<size_t> (kPartitionSize), 0.0f);
    const int headCopy = std::min (targetFrames, kPartitionSize);
    std::copy (irL.begin(), irL.begin() + headCopy, m_headL.begin());
    std::copy (irR.begin(), irR.begin() + headCopy, m_headR.begin());

    const int tailFrames = std::max (0, targetFrames - kPartitionSize);
    m_numTailPartitions = std::max (1, (tailFrames + kPartitionSize - 1) / kPartitionSize);

    m_tailSpectraL.assign (static_cast<size_t> (m_numTailPartitions),
                           std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));
    m_tailSpectraR.assign (static_cast<size_t> (m_numTailPartitions),
                           std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));

    for (int p = 0; p < m_numTailPartitions; ++p)
    {
        const int startFrame = kPartitionSize + p * kPartitionSize;
        float partitionBufL[kFftSize] = {};
        float partitionBufR[kFftSize] = {};
        for (int k = 0; k < kPartitionSize; ++k)
        {
            const int idx = startFrame + k;
            if (idx < targetFrames)
            {
                partitionBufL[k] = irL[static_cast<size_t> (idx)];
                partitionBufR[k] = irR[static_cast<size_t> (idx)];
            }
        }
        m_fft.forwardReal (partitionBufL, kFftSize, m_tailSpectraL[static_cast<size_t> (p)].data());
        m_fft.forwardReal (partitionBufR, kFftSize, m_tailSpectraR[static_cast<size_t> (p)].data());
    }

    // Allocate tail FFT delay line history
    m_tailFftHistL.assign (static_cast<size_t> (m_numTailPartitions),
                           std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));
    m_tailFftHistR.assign (static_cast<size_t> (m_numTailPartitions),
                           std::vector<Complex> (static_cast<size_t> (kFftSize), Complex (0.0f, 0.0f)));
    m_tailFftHistHead = 0;
}

void Convolve::computeTailFftBlock() noexcept
{
    // Channel L
    m_fft.forwardReal (m_blockInL.data(), kPartitionSize, m_scratchComplexIn.data());
    m_tailFftHistL[static_cast<size_t> (m_tailFftHistHead)] = m_scratchComplexIn;

    for (int k = 0; k < kFftSize; ++k)
        m_scratchComplexSum[static_cast<size_t> (k)] = Complex (0.0f, 0.0f);

    for (int p = 0; p < m_numTailPartitions; ++p)
    {
        const int histIdx = (m_tailFftHistHead - p + m_numTailPartitions) % m_numTailPartitions;
        const auto& X = m_tailFftHistL[static_cast<size_t> (histIdx)];
        const auto& H = m_tailSpectraL[static_cast<size_t> (p)];
        for (int k = 0; k < kFftSize; ++k)
            m_scratchComplexSum[static_cast<size_t> (k)] = m_scratchComplexSum[static_cast<size_t> (k)] + X[static_cast<size_t> (k)] * H[static_cast<size_t> (k)];
    }

    m_fft.ifft (m_scratchComplexSum.data());
    for (int k = 0; k < kPartitionSize; ++k)
    {
        m_currentTailBlockL[static_cast<size_t> (k)] = m_scratchComplexSum[static_cast<size_t> (k)].r + m_overlapL[static_cast<size_t> (k)];
        m_overlapL[static_cast<size_t> (k)] = m_scratchComplexSum[static_cast<size_t> (k + kPartitionSize)].r;
    }

    // Channel R
    m_fft.forwardReal (m_blockInR.data(), kPartitionSize, m_scratchComplexIn.data());
    m_tailFftHistR[static_cast<size_t> (m_tailFftHistHead)] = m_scratchComplexIn;

    for (int k = 0; k < kFftSize; ++k)
        m_scratchComplexSum[static_cast<size_t> (k)] = Complex (0.0f, 0.0f);

    for (int p = 0; p < m_numTailPartitions; ++p)
    {
        const int histIdx = (m_tailFftHistHead - p + m_numTailPartitions) % m_numTailPartitions;
        const auto& X = m_tailFftHistR[static_cast<size_t> (histIdx)];
        const auto& H = m_tailSpectraR[static_cast<size_t> (p)];
        for (int k = 0; k < kFftSize; ++k)
            m_scratchComplexSum[static_cast<size_t> (k)] = m_scratchComplexSum[static_cast<size_t> (k)] + X[static_cast<size_t> (k)] * H[static_cast<size_t> (k)];
    }

    m_fft.ifft (m_scratchComplexSum.data());
    for (int k = 0; k < kPartitionSize; ++k)
    {
        m_currentTailBlockR[static_cast<size_t> (k)] = m_scratchComplexSum[static_cast<size_t> (k)].r + m_overlapR[static_cast<size_t> (k)];
        m_overlapR[static_cast<size_t> (k)] = m_scratchComplexSum[static_cast<size_t> (k + kPartitionSize)].r;
    }

    m_tailFftHistHead = (m_tailFftHistHead + 1) % m_numTailPartitions;
}

void Convolve::process (float* l, float* r, int n) noexcept
{
    if (l == nullptr || r == nullptr || n <= 0)
        return;

    const float mix = std::clamp (params.mix, 0.0f, 1.0f);
    const float outGain = std::pow (10.0f, std::clamp (params.level, -60.0f, 24.0f) / 20.0f);

    // Tone tilt filter parameter
    const float tone = std::clamp (params.tone, -1.0f, 1.0f);
    const float lowGain = std::pow (10.0f, (-tone * 6.0f) / 20.0f);
    const float highGain = std::pow (10.0f, (tone * 6.0f) / 20.0f);
    // Filter coefficient for 1 kHz tilt center
    const float tiltAlpha = static_cast<float> (1.0 - std::exp (-kTwoPi * 1000.0 / m_sampleRate));

    // Pre-delay in samples
    double preDelaySec = 0.0;
    if (params.bpm && params.tempoBpm > 10.0)
        preDelaySec = params.preDelayBeats * (60.0 / params.tempoBpm);
    else
        preDelaySec = static_cast<double> (params.preDelay);
    preDelaySec = std::clamp (preDelaySec, 0.0, 2.0);
    const int preDelaySamples = static_cast<int> (std::round (preDelaySec * m_sampleRate));

    const float* hL = m_headL.data();
    const float* hR = m_headR.data();

    for (int i = 0; i < n; ++i)
    {
        const float inL = sanitize (l[i]);
        const float inR = sanitize (r[i]);

        // Push input into duplicate ring buffer for branchless contiguous access
        m_headHistoryL[static_cast<size_t> (m_headHistPos)] = inL;
        m_headHistoryL[static_cast<size_t> (m_headHistPos + kPartitionSize)] = inL;
        m_headHistoryR[static_cast<size_t> (m_headHistPos)] = inR;
        m_headHistoryR[static_cast<size_t> (m_headHistPos + kPartitionSize)] = inR;

        // Head FIR convolution
        float headOutL = 0.0f;
        float headOutR = 0.0f;
        const float* pL = m_headHistoryL.data() + (m_headHistPos + 1);
        const float* pR = m_headHistoryR.data() + (m_headHistPos + 1);

        for (int k = 0; k < kPartitionSize; ++k)
        {
            headOutL += hL[k] * pL[kPartitionSize - 1 - k];
            headOutR += hR[k] * pR[kPartitionSize - 1 - k];
        }

        m_headHistPos = (m_headHistPos + 1) & (kPartitionSize - 1);

        // Add tail block output
        float wetL = headOutL + m_currentTailBlockL[static_cast<size_t> (m_blockPos)];
        float wetR = headOutR + m_currentTailBlockR[static_cast<size_t> (m_blockPos)];

        // Store into tail block input accumulator
        m_blockInL[static_cast<size_t> (m_blockPos)] = inL;
        m_blockInR[static_cast<size_t> (m_blockPos)] = inR;
        ++m_blockPos;

        if (m_blockPos == kPartitionSize)
        {
            computeTailFftBlock();
            m_blockPos = 0;
        }

        // Apply Tone (1-pole spectral tilt)
        if (tone != 0.0f)
        {
            m_toneStateL += tiltAlpha * (wetL - m_toneStateL);
            m_toneStateR += tiltAlpha * (wetR - m_toneStateR);
            const float lowL = m_toneStateL;
            const float highL = wetL - lowL;
            const float lowR = m_toneStateR;
            const float highR = wetR - lowR;
            wetL = lowL * lowGain + highL * highGain;
            wetR = lowR * lowGain + highR * highGain;
        }

        // Apply Pre-Delay
        if (preDelaySamples > 0 && m_preDelayCapacity > 0)
        {
            const int readPos = (m_preDelayWritePos - preDelaySamples + m_preDelayCapacity) % m_preDelayCapacity;
            const float delayedL = m_preDelayBufL[static_cast<size_t> (readPos)];
            const float delayedR = m_preDelayBufR[static_cast<size_t> (readPos)];
            m_preDelayBufL[static_cast<size_t> (m_preDelayWritePos)] = wetL;
            m_preDelayBufR[static_cast<size_t> (m_preDelayWritePos)] = wetR;
            m_preDelayWritePos = (m_preDelayWritePos + 1) % m_preDelayCapacity;
            wetL = delayedL;
            wetR = delayedR;
        }

        // Wet/Dry mix and output level
        l[i] = sanitize (((1.0f - mix) * inL + mix * wetL) * outGain);
        r[i] = sanitize (((1.0f - mix) * inR + mix * wetR) * outGain);
    }
}
} // namespace synthyboy::fx
