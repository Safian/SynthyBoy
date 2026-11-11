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

namespace
{
/** The damping of Serum 2.1.5 (measured with an impulse of exponentially decaying noise: the wet signal at times 0.4 .. 4 s for the knob 0 .. 100 %, bands 300 Hz .. 17 kHz, fitted with three equal
    one pole low passes): the corner (log10 Hz) by knob and time. Below 50 % the corner stays above 20 kHz for the first seconds (the effect starts around 30 %). */
double serumDampLogCorner (double knob, double t) noexcept
{
    static const double kD[] = { 0.0, 30.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0 };
    static const double kT[] = { 0.4, 1.0, 2.0, 3.0, 4.0 };
    static const double kL[8][5] = {
        { 9.0, 9.0, 9.0, 9.0, 9.0 },                       // 0 %: no damping
        { 6.5, 5.96, 5.38, 5.05, 4.81 },
        { 6.16, 5.41, 4.865, 4.60, 4.417 },
        { 5.94, 5.19, 4.68, 4.44, 4.277 },
        { 5.72, 4.98, 4.52, 4.30, 4.148 },
        { 5.49, 4.78, 4.365, 4.16, 4.01 },
        { 5.21, 4.56, 4.19, 3.99, 3.83 },
        { 4.69, 4.18, 3.82, 3.55, 3.31 } };
    const double d = std::max (0.0, std::min (100.0, knob));
    int di = 0;
    while (di < 6 && d > kD[di + 1])
        ++di;
    const double fd = (d - kD[di]) / (kD[di + 1] - kD[di]);
    const auto at = [&] (int row)
    {
        if (t <= kT[0])
            return kL[row][0];
        int ti = 0;
        while (ti < 3 && t > kT[ti + 1])
            ++ti;
        const double ft = (t - kT[ti]) / (kT[ti + 1] - kT[ti]);   // beyond 4 s the last slope goes on
        return kL[row][ti] + (kL[row][ti + 1] - kL[row][ti]) * ft;
    };
    return at (di) + (at (di + 1) - at (di)) * fd;
}

/** The level offset that goes with the damping of Serum (dB). */
double serumDampGainDb (double knob) noexcept
{
    static const double kD[] = { 0.0, 25.0, 50.0, 75.0, 90.0, 100.0 };
    static const double kG[] = { 0.0, -0.1, -0.7, -1.1, -1.4, -1.7 };
    const double d = std::max (0.0, std::min (100.0, knob));
    int i = 0;
    while (i < 4 && d > kD[i + 1])
        ++i;
    return kG[i] + (kG[i + 1] - kG[i]) * (d - kD[i]) / (kD[i + 1] - kD[i]);
}
} // namespace

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

    // 0. The Serum law: the impulse is normalised to the peak 1 over both channels (measured: the level of the wet signal is the level of the convolution with the peak
    //    normalised impulse times IR GAIN, whatever the level of the file; the default IR GAIN of Serum is -18 dB)
    float normalise = 1.0f;
    if (params.serumLaw)
    {
        float peak = 0.0f;
        for (const float v : m_rawIr)
            peak = std::max (peak, std::fabs (v));
        if (peak > 1.0e-9f)
            normalise = 1.0f / peak;
        // Serum convolves at the rate of the host; the engine at the oversampled rate: an impulse resampled to twice the rate has twice the samples, so it is scaled to keep the gain
        if (params.hostRate > 0.0)
            normalise *= static_cast<float> (params.hostRate / m_sampleRate);
    }

    // 1. Resample raw IR to engine sample rate and stretch/contract by params.size
    const float sizeParam = params.serumLaw ? std::clamp (params.size, 0.1f, 10.0f) : std::clamp (params.size, 0.25f, 4.0f);
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

    // 2. Attack fade-in (the Serum law: an amplitude ramp (t / attack) ^ 0.6, measured)
    if (params.attack > 0.0f && params.serumLaw)
    {
        const int attFrames = std::min (targetFrames, static_cast<int> (std::round (params.attack * m_sampleRate)));
        for (int i = 0; i < attFrames; ++i)
        {
            const float ramp = std::pow (static_cast<float> (i) / static_cast<float> (attFrames), 0.6f);
            irL[static_cast<size_t> (i)] *= ramp;
            irR[static_cast<size_t> (i)] *= ramp;
        }
    }
    else if (params.attack > 0.0f)
    {
        const int attFrames = std::min (targetFrames, static_cast<int> (std::round (params.attack * m_sampleRate)));
        for (int i = 0; i < attFrames; ++i)
        {
            const float ramp = 0.5f * (1.0f - std::cos (static_cast<float> (kPi * i / attFrames)));
            irL[static_cast<size_t> (i)] *= ramp;
            irR[static_cast<size_t> (i)] *= ramp;
        }
    }

    // 3. Decay shortening (the Serum law: exp (-6.9078 t / T60) with the T60 = decay, measured on Serum 2.1.5; ours: -26 dB after `decay` seconds)
    if (params.decay > 0.0f)
    {
        const double decaySec = std::max (params.serumLaw ? 0.0005 : 0.001, static_cast<double> (params.decay));
        const double decayFactor = (params.serumLaw ? 6.907755278982137 : 3.0) / (decaySec * m_sampleRate);
        for (int i = 0; i < targetFrames; ++i)
        {
            const float env = static_cast<float> (std::exp (-decayFactor * static_cast<double> (i)));
            irL[static_cast<size_t> (i)] *= env;
            irR[static_cast<size_t> (i)] *= env;
        }
    }

    // 4. Damp (high frequency damping across time; the Serum law: three one pole low passes whose corner falls with the time of the impulse, measured)
    if (params.damp > 0.0f && params.serumLaw)
    {
        const double knob = static_cast<double> (std::clamp (params.damp, 0.0f, 1.0f)) * 100.0;
        const float gain = static_cast<float> (std::pow (10.0, serumDampGainDb (knob) / 20.0));
        float sL[3] = { 0.0f, 0.0f, 0.0f }, sR[3] = { 0.0f, 0.0f, 0.0f };
        float alpha = 1.0f;
        for (int i = 0; i < targetFrames; ++i)
        {
            if ((i & 255) == 0)
            {
                const double fc = std::min (std::pow (10.0, serumDampLogCorner (knob, static_cast<double> (i) / m_sampleRate)), 0.45 * m_sampleRate);
                alpha = static_cast<float> (1.0 - std::exp (-kTwoPi * fc / m_sampleRate));
            }
            float xL = irL[static_cast<size_t> (i)], xR = irR[static_cast<size_t> (i)];
            for (int k = 0; k < 3; ++k)
            {
                sL[k] += alpha * (xL - sL[k]);
                sR[k] += alpha * (xR - sR[k]);
                xL = sL[k];
                xR = sR[k];
            }
            irL[static_cast<size_t> (i)] = xL * gain;
            irR[static_cast<size_t> (i)] = xR * gain;
        }
    }
    else if (params.damp > 0.0f)
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
        if (params.serumLaw)
        {
            while (minFftSize < 2 * targetFrames && minFftSize < (1 << 21))   // the whole impulse and room for the cepstrum (no time aliasing)
                minFftSize <<= 1;
        }
        else
            while (minFftSize < targetFrames && minFftSize < 65536)
                minFftSize <<= 1;
        Radix2FFT minFft (minFftSize);
        convertToMinimumPhase (irL, minFft);
        convertToMinimumPhase (irR, minFft);
        if (params.serumLaw)
        {
            // Serum normalises the impulse after the minimum phase conversion (measured: the level of the wet signal falls by the ratio of the peaks, 0 .. 17 dB)
            float peak = 0.0f;
            for (int i = 0; i < targetFrames; ++i)
                peak = std::max ({ peak, std::fabs (irL[static_cast<size_t> (i)]), std::fabs (irR[static_cast<size_t> (i)]) });
            if (peak > 1.0e-9f)
                normalise = (params.hostRate > 0.0 ? static_cast<float> (params.hostRate / m_sampleRate) : 1.0f) / peak;
        }
    }

    // 6. IR Gain
    const float irGainFactor = std::pow (10.0f, std::clamp (params.irGain, -40.0f, 24.0f) / 20.0f) * normalise;
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

namespace
{
/** The corner of the TONE filter of Serum 2.1.5 (measured with an impulse of exponentially decaying noise: first order filters, fitted to the transfer function of the wet signal): TONE < 0 a low pass,
    TONE > 0 a high pass, log10 (Hz) piecewise linear over the knob (-100 .. 100); 0 is off. */
double serumToneCorner (double toneKnob) noexcept
{
    static const double kKnob[] = { -100.0, -80.0, -70.0, -60.0, -40.0, -20.0, -10.0, 0.0, 20.0, 40.0, 60.0, 80.0, 90.0, 95.0, 100.0 };
    static const double kLog[]  = {   1.497,  2.374,  2.70, 3.019, 3.613, 4.178, 4.471, 5.0, 1.50, 2.305, 2.797, 3.508, 3.846, 4.123, 4.561 };
    const double t = std::max (-100.0, std::min (100.0, toneKnob));
    if (t > 0.0)
    {
        // the high pass starts at 5 Hz (log10 0.7) at the middle of the knob
        if (t < 20.0)
            return std::pow (10.0, 0.7 + (kLog[8] - 0.7) * t / 20.0);
        for (int i = 8; i < 14; ++i)
            if (t <= kKnob[i + 1])
                return std::pow (10.0, kLog[i] + (kLog[i + 1] - kLog[i]) * (t - kKnob[i]) / (kKnob[i + 1] - kKnob[i]));
        return std::pow (10.0, kLog[14]);
    }
    for (int i = 0; i < 7; ++i)
        if (t <= kKnob[i + 1])
            return std::pow (10.0, kLog[i] + (kLog[i + 1] - kLog[i]) * (t - kKnob[i]) / (kKnob[i + 1] - kKnob[i]));
    return std::pow (10.0, kLog[7]);
}
} // namespace

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
    // the Serum law: a first order low pass (TONE < 0) or high pass (TONE > 0) at the measured corner
    const float serumAlpha = params.serumLaw ? static_cast<float> (1.0 - std::exp (-kTwoPi * std::min (serumToneCorner (static_cast<double> (tone) * 100.0), 0.45 * m_sampleRate) / m_sampleRate)) : 0.0f;

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

        // Apply Tone (1-pole spectral tilt; the Serum law: a 1-pole low / high pass)
        if (params.serumLaw)
        {
            if (tone != 0.0f)
            {
                m_toneStateL += serumAlpha * (wetL - m_toneStateL);
                m_toneStateR += serumAlpha * (wetR - m_toneStateR);
                if (tone < 0.0f)
                {
                    wetL = m_toneStateL;
                    wetR = m_toneStateR;
                }
                else
                {
                    wetL -= m_toneStateL;
                    wetR -= m_toneStateR;
                }
            }
        }
        else if (tone != 0.0f)
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
