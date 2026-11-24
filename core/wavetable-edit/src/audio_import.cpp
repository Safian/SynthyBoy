// SynthyBoy - wavetable-edit: Audio import, WAV decoding, pitch detection, slicing, and resampling.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace synthyboy::wtedit
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Little-endian 16-bit and 32-bit readers with bounds checks
uint16_t readU16(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32(const uint8_t* p) noexcept
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Cubic Hermite interpolation
float cubicHermite(float y0, float y1, float y2, float y3, float t) noexcept
{
    const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float a2 = -0.5f * y0 + 0.5f * y2;
    const float a3 = y1;
    return ((a0 * t + a1) * t + a2) * t + a3;
}

// Autocorrelation pitch detector: returns estimated period in samples
float detectPitchPeriod(const float* signal, size_t length, double sampleRate) noexcept
{
    if (length < 64)
        return 2048.0f;

    // Search range: 20 Hz to 2000 Hz
    size_t minLag = static_cast<size_t>(sampleRate / 2000.0);
    size_t maxLag = static_cast<size_t>(sampleRate / 20.0);
    if (minLag < 4) minLag = 4;
    if (maxLag >= length / 2) maxLag = length / 2;
    if (minLag >= maxLag) return 2048.0f;

    // Normalized Square Difference Function (NSDF). The fundamental is the FIRST peak that comes close to the highest one (McLeod's rule, k = 0.9): the
    // highest peak alone lands on a multiple of the period whenever a longer lag happens to fit the signal better (GROUPD WP-14)
    std::vector<float> nsdf(maxLag + 2, -1.0f);
    float bestCorr = -1.0f;
    size_t bestLag = minLag;

    for (size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double sumProd = 0.0;
        double sumSq1 = 0.0;
        double sumSq2 = 0.0;
        const size_t N = length - lag;

        for (size_t i = 0; i < N; ++i)
        {
            const double s1 = static_cast<double>(signal[i]);
            const double s2 = static_cast<double>(signal[i + lag]);
            sumProd += s1 * s2;
            sumSq1 += s1 * s1;
            sumSq2 += s2 * s2;
        }

        const double denom = std::sqrt(sumSq1 * sumSq2);
        if (denom > 1e-12)
        {
            nsdf[lag] = static_cast<float>(2.0 * sumProd / (sumSq1 + sumSq2));
            if (nsdf[lag] > bestCorr)
            {
                bestCorr = nsdf[lag];
                bestLag = lag;
            }
        }
    }

    if (bestCorr > 0.0f)
        for (size_t lag = minLag + 1; lag <= maxLag; ++lag)
            if (nsdf[lag] >= 0.9f * bestCorr && nsdf[lag] >= nsdf[lag - 1] && nsdf[lag] >= nsdf[lag + 1])
            {
                bestLag = lag;
                break;
            }

    return static_cast<float>(bestLag);
}

// NSDF of one lag (the quantity detectPitchPeriod maximises)
double nsdfAt(const float* signal, size_t length, size_t lag) noexcept
{
    if (lag == 0 || lag >= length)
        return -1.0;
    double sumProd = 0.0, sumSq1 = 0.0, sumSq2 = 0.0;
    for (size_t i = 0; i + lag < length; ++i)
    {
        const double s1 = static_cast<double>(signal[i]);
        const double s2 = static_cast<double>(signal[i + lag]);
        sumProd += s1 * s2;
        sumSq1 += s1 * s1;
        sumSq2 += s2 * s2;
    }
    return (sumSq1 + sumSq2) > 1e-12 ? 2.0 * sumProd / (sumSq1 + sumSq2) : -1.0;
}

// The period with a fraction: detectPitchPeriod's integer lag refined by a parabola through the NSDF at lag - 1, lag, lag + 1 (GROUPD WP-14: a
// 220 Hz tone at 44.1 kHz has the period 200.45, the integer lag 200 cuts every cycle 0.2 % short and the frames get a seam)
double refinePeriod(const float* signal, size_t length, float coarseLag) noexcept
{
    const size_t lag = static_cast<size_t>(coarseLag);
    if (lag < 3 || lag + 2 >= length)
        return static_cast<double>(coarseLag);
    const double a = nsdfAt(signal, length, lag - 1), b = nsdfAt(signal, length, lag), c = nsdfAt(signal, length, lag + 1);
    const double denom = a - 2.0 * b + c;
    if (std::fabs(denom) < 1e-12)
        return static_cast<double>(lag);
    return static_cast<double>(lag) + std::clamp(0.5 * (a - c) / denom, -1.0, 1.0);
}

// Cubic (Catmull-Rom) reading at a fractional position, the ends repeat their first / last sample
float readCubic(const float* in, size_t length, double pos) noexcept
{
    const auto at = [in, length](long i) { return in[static_cast<size_t>(std::clamp<long>(i, 0, static_cast<long>(length) - 1))]; };
    const long i = static_cast<long>(std::floor(pos));
    return cubicHermite(at(i - 1), at(i), at(i + 1), at(i + 2), static_cast<float>(pos - static_cast<double>(i)));
}

// Finds the nearest positive zero-crossing around targetIndex
size_t findNearestZeroCrossing(const float* signal, size_t length, size_t targetIndex, size_t window = 64) noexcept
{
    if (length < 2)
        return targetIndex;

    const size_t start = (targetIndex > window) ? (targetIndex - window) : 0;
    const size_t end = std::min(length - 1, targetIndex + window);

    size_t bestIdx = targetIndex;
    size_t bestDist = window + 1;

    for (size_t i = start; i < end; ++i)
    {
        if (signal[i] <= 0.0f && signal[i + 1] > 0.0f)
        {
            const size_t dist = (i >= targetIndex) ? (i - targetIndex) : (targetIndex - i);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i + 1;
            }
        }
    }

    return (bestDist <= window) ? bestIdx : targetIndex;
}

} // namespace

void resampleBuffer(const float* in, size_t inLength, float* out, size_t outLength) noexcept
{
    if (inLength == 0 || outLength == 0)
        return;

    if (inLength == 1)
    {
        for (size_t i = 0; i < outLength; ++i)
            out[i] = in[0];
        return;
    }

    if (inLength == outLength)
    {
        std::memcpy(out, in, outLength * sizeof(float));
        return;
    }

    const double ratio = static_cast<double>(inLength) / static_cast<double>(outLength);

    for (size_t i = 0; i < outLength; ++i)
    {
        const double pos = static_cast<double>(i) * ratio;
        const size_t i1 = static_cast<size_t>(pos);
        const float t = static_cast<float>(pos - static_cast<double>(i1));

        const size_t i0 = (i1 > 0) ? (i1 - 1) : 0;
        const size_t i2 = std::min(inLength - 1, i1 + 1);
        const size_t i3 = std::min(inLength - 1, i1 + 2);

        out[i] = cubicHermite(in[i0], in[i1], in[i2], in[i3], t);
    }
}

bool importAudioWav(const uint8_t* wavData, size_t dataSize, const ImportOptions& options, Wavetable& outWt, std::string* errorMsg)
{
    if (wavData == nullptr || dataSize < 44)
    {
        if (errorMsg) *errorMsg = "Data size too small for WAV header";
        return false;
    }

    // Check RIFF header
    if (std::memcmp(wavData, "RIFF", 4) != 0 || std::memcmp(wavData + 8, "WAVE", 4) != 0)
    {
        if (errorMsg) *errorMsg = "Missing RIFF/WAVE header";
        return false;
    }

    size_t offset = 12;
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* pAudioData = nullptr;
    size_t audioDataBytes = 0;

    // Parse RIFF chunks
    while (offset + 8 <= dataSize)
    {
        const char chunkId[5] = {
            static_cast<char>(wavData[offset]),
            static_cast<char>(wavData[offset + 1]),
            static_cast<char>(wavData[offset + 2]),
            static_cast<char>(wavData[offset + 3]),
            '\0'
        };
        const uint32_t chunkSize = readU32(wavData + offset + 4);
        offset += 8;

        if (offset + chunkSize > dataSize)
        {
            // Truncated chunk: clamp to remaining data
            const size_t available = dataSize - offset;
            if (std::strcmp(chunkId, "data") == 0)
            {
                pAudioData = wavData + offset;
                audioDataBytes = available;
            }
            break;
        }

        if (std::strcmp(chunkId, "fmt ") == 0)
        {
            if (chunkSize < 16)
            {
                if (errorMsg) *errorMsg = "fmt chunk too small";
                return false;
            }
            audioFormat = readU16(wavData + offset);
            numChannels = readU16(wavData + offset + 2);
            sampleRate = readU32(wavData + offset + 4);
            bitsPerSample = readU16(wavData + offset + 14);

            // Handle WAVE_FORMAT_EXTENSIBLE (0xFFFE)
            if (audioFormat == 0xFFFE && chunkSize >= 40)
            {
                audioFormat = readU16(wavData + offset + 24);
            }
        }
        else if (std::strcmp(chunkId, "data") == 0)
        {
            pAudioData = wavData + offset;
            audioDataBytes = chunkSize;
        }

        // Chunks are 2-byte aligned in RIFF
        offset += chunkSize + (chunkSize & 1);
    }

    if (pAudioData == nullptr || audioDataBytes == 0)
    {
        if (errorMsg) *errorMsg = "No audio data found in WAV";
        return false;
    }

    if (numChannels == 0 || sampleRate == 0)
    {
        if (errorMsg) *errorMsg = "Invalid channel count or sample rate";
        return false;
    }

    if (audioFormat != 1 && audioFormat != 3) // 1 = PCM, 3 = IEEE Float
    {
        if (errorMsg) *errorMsg = "Unsupported audio format (only PCM and IEEE Float supported)";
        return false;
    }

    const size_t bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0)
    {
        if (errorMsg) *errorMsg = "Invalid bits per sample";
        return false;
    }

    const size_t totalSamples = audioDataBytes / (bytesPerSample * numChannels);
    if (totalSamples == 0)
    {
        if (errorMsg) *errorMsg = "Audio data has 0 valid samples";
        return false;
    }

    const size_t selectedChannel = (options.channel < numChannels) ? static_cast<size_t>(options.channel) : 0;
    std::vector<float> samples(totalSamples);

    if (audioFormat == 1) // PCM integer
    {
        if (bitsPerSample == 16)
        {
            for (size_t i = 0; i < totalSamples; ++i)
            {
                const size_t byteIdx = (i * numChannels + selectedChannel) * 2;
                const int16_t raw = static_cast<int16_t>(readU16(pAudioData + byteIdx));
                samples[i] = static_cast<float>(raw) / 32768.0f;
            }
        }
        else if (bitsPerSample == 24)
        {
            for (size_t i = 0; i < totalSamples; ++i)
            {
                const size_t byteIdx = (i * numChannels + selectedChannel) * 3;
                const int32_t raw = (static_cast<int32_t>(pAudioData[byteIdx + 2]) << 24) |
                                    (static_cast<int32_t>(pAudioData[byteIdx + 1]) << 16) |
                                    (static_cast<int32_t>(pAudioData[byteIdx]) << 8);
                samples[i] = static_cast<float>(raw) / 2147483648.0f;
            }
        }
        else if (bitsPerSample == 32)
        {
            for (size_t i = 0; i < totalSamples; ++i)
            {
                const size_t byteIdx = (i * numChannels + selectedChannel) * 4;
                const int32_t raw = static_cast<int32_t>(readU32(pAudioData + byteIdx));
                samples[i] = static_cast<float>(raw) / 2147483648.0f;
            }
        }
        else
        {
            if (errorMsg) *errorMsg = "Unsupported PCM bit depth (only 16, 24, 32 supported)";
            return false;
        }
    }
    else if (audioFormat == 3) // IEEE Float 32-bit
    {
        if (bitsPerSample != 32)
        {
            if (errorMsg) *errorMsg = "Float WAV must be 32-bit";
            return false;
        }
        for (size_t i = 0; i < totalSamples; ++i)
        {
            const size_t byteIdx = (i * numChannels + selectedChannel) * 4;
            float val = 0.0f;
            std::memcpy(&val, pAudioData + byteIdx, sizeof(float));
            if (std::isnan(val) || std::isinf(val)) val = 0.0f;
            samples[i] = val;
        }
    }

    return importAudioSamples(samples.data(), samples.size(), static_cast<double>(sampleRate), options, outWt);
}

bool importAudioSamples(const float* samples, size_t numSamples, double sampleRate, const ImportOptions& options, Wavetable& outWt)
{
    if (samples == nullptr || numSamples == 0)
        return false;

    std::vector<Frame> importedFrames;
    const size_t maxF = (options.maxFrames > 0) ? std::min(options.maxFrames, kMaxFrames) : kMaxFrames;

    switch (options.mode)
    {
    case ImportMode::FixedSingleCycle:
    {
        // First cycle or 2048 samples
        Frame f;
        const size_t take = std::min(numSamples, kWaveformSize);
        resampleBuffer(samples, take, f.sampleData(), kWaveformSize);
        importedFrames.push_back(f);
        break;
    }

    case ImportMode::ByFrameSize:
    {
        size_t fSize = options.frameSize;
        if (fSize < 32) fSize = 32;
        if (fSize > 9999) fSize = 9999;

        const size_t totalF = std::min(maxF, numSamples / fSize);
        for (size_t f = 0; f < totalF; ++f)
        {
            Frame frame;
            resampleBuffer(samples + f * fSize, fSize, frame.sampleData(), kWaveformSize);
            importedFrames.push_back(frame);
        }
        break;
    }

    case ImportMode::PitchDetectZeroSnap:
    {
        const float detectedPeriod = detectPitchPeriod(samples, numSamples, sampleRate);
        const size_t period = static_cast<size_t>(std::max(16.0f, detectedPeriod));

        size_t currentPos = findNearestZeroCrossing(samples, numSamples, 0);

        while (currentPos < numSamples && importedFrames.size() < maxF)
        {
            size_t nextPos = currentPos + period;
            if (nextPos >= numSamples)
                break;

            nextPos = findNearestZeroCrossing(samples, numSamples, nextPos, period / 4);
            if (nextPos <= currentPos)
                nextPos = currentPos + period;

            const size_t cycleLen = nextPos - currentPos;
            Frame frame;
            resampleBuffer(samples + currentPos, cycleLen, frame.sampleData(), kWaveformSize);
            importedFrames.push_back(frame);

            currentPos = nextPos;
        }
        break;
    }

    case ImportMode::PitchDetectFollow:
    {
        // Cycle-by-cycle dynamic pitch tracking
        size_t currentPos = findNearestZeroCrossing(samples, numSamples, 0);

        while (currentPos < numSamples && importedFrames.size() < maxF)
        {
            const size_t remaining = numSamples - currentPos;
            if (remaining < 32)
                break;

            const float localPeriod = detectPitchPeriod(samples + currentPos, std::min<size_t>(remaining, 2048), sampleRate);
            const size_t period = static_cast<size_t>(std::max(16.0f, localPeriod));

            size_t nextPos = currentPos + period;
            if (nextPos >= numSamples)
                break;

            nextPos = findNearestZeroCrossing(samples, numSamples, nextPos, period / 4);
            if (nextPos <= currentPos)
                nextPos = currentPos + period;

            const size_t cycleLen = nextPos - currentPos;
            Frame frame;
            resampleBuffer(samples + currentPos, cycleLen, frame.sampleData(), kWaveformSize);
            importedFrames.push_back(frame);

            currentPos = nextPos;
        }
        break;
    }

    case ImportMode::PitchAverage:
    {
        const float coarse = detectPitchPeriod(samples, numSamples, sampleRate);
        const double period = std::max(16.0, refinePeriod(samples, numSamples, coarse));

        const size_t totalF = std::min(maxF, static_cast<size_t>(static_cast<double>(numSamples) / period));
        for (size_t f = 0; f < totalF; ++f)
        {
            Frame frame;
            const double start = static_cast<double>(f) * period;
            for (size_t i = 0; i < kWaveformSize; ++i)
                frame[i] = readCubic(samples, numSamples, start + static_cast<double>(i) * period / static_cast<double>(kWaveformSize));
            importedFrames.push_back(frame);
        }
        break;
    }

    case ImportMode::Fft256:
    case ImportMode::Fft512:
    case ImportMode::Fft1024:
    case ImportMode::Fft2048:
    {
        size_t fftSize = 256;
        if (options.mode == ImportMode::Fft512) fftSize = 512;
        else if (options.mode == ImportMode::Fft1024) fftSize = 1024;
        else if (options.mode == ImportMode::Fft2048) fftSize = 2048;

        const size_t totalF = std::min(maxF, numSamples / fftSize);
        std::vector<float> window(fftSize);
        for (size_t i = 0; i < fftSize; ++i)
        {
            // Hann window
            window[i] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(fftSize))));
        }

        for (size_t f = 0; f < totalF; ++f)
        {
            std::vector<float> windowed(fftSize);
            for (size_t i = 0; i < fftSize; ++i)
                windowed[i] = samples[f * fftSize + i] * window[i];

            std::vector<float> mags((fftSize / 2) + 1);
            std::vector<float> phases((fftSize / 2) + 1);
            Radix2Fft::realToPolar(windowed.data(), mags.data(), phases.data(), fftSize);

            // Reconstruct into 2048 samples
            std::array<float, kNumFftBins + 1> targetMags {};
            std::array<float, kNumFftBins + 1> targetPhases {};

            const size_t copyBins = std::min((fftSize / 2) + 1, kNumFftBins + 1);
            for (size_t k = 0; k < copyBins; ++k)
            {
                targetMags[k] = mags[k];
                targetPhases[k] = phases[k];
            }

            Frame frame;
            Radix2Fft::polarToReal(targetMags.data(), targetPhases.data(), frame.sampleData(), kWaveformSize);
            importedFrames.push_back(frame);
        }
        break;
    }
    }

    if (importedFrames.empty())
    {
        // Fallback: single cycle from the whole buffer
        Frame f;
        resampleBuffer(samples, numSamples, f.sampleData(), kWaveformSize);
        importedFrames.push_back(f);
    }

    if (options.fadeEdges)
    {
        for (auto& f : importedFrames)
            ops::crossfadeEdges16Samples(f);
    }

    if (options.normalize)
    {
        for (auto& f : importedFrames)
            ops::normalise(f);
    }

    outWt.frames() = std::move(importedFrames);
    outWt.setSampleRate(sampleRate);
    return true;
}

} // namespace synthyboy::wtedit
