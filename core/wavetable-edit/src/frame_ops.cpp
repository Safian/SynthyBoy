// SynthyBoy - wavetable-edit: Frame operations implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace synthyboy::wtedit::ops
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Deterministic LCG
float pseudoRand(uint32_t& seed) noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 9) / 8388608.0f; // [0, 1)
}

float sampleLinear(const Frame& f, double index) noexcept
{
    double wrapped = std::fmod(index, static_cast<double>(kWaveformSize));
    if (wrapped < 0.0)
        wrapped += static_cast<double>(kWaveformSize);

    const size_t i0 = static_cast<size_t>(wrapped) % kWaveformSize;
    const size_t i1 = (i0 + 1) % kWaveformSize;
    const float t = static_cast<float>(wrapped - static_cast<double>(i0));
    return (1.0f - t) * f[i0] + t * f[i1];
}

// Wraps angle to [-pi, pi]
float wrapAngle(float angle) noexcept
{
    while (angle > static_cast<float>(kPi))
        angle -= static_cast<float>(2.0 * kPi);
    while (angle < static_cast<float>(-kPi))
        angle += static_cast<float>(2.0 * kPi);
    return angle;
}
} // namespace

void normalise(Frame& frame) noexcept
{
    float maxAbs = 0.0f;
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const float a = std::fabs(frame[i]);
        if (a > maxAbs)
            maxAbs = a;
    }

    if (maxAbs > 1e-12f)
    {
        const float invMax = 1.0f / maxAbs;
        for (size_t i = 0; i < kWaveformSize; ++i)
            frame[i] *= invMax;
    }
}

void normaliseEach(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        normalise(f);
}

void normaliseSame(Wavetable& wt) noexcept
{
    float maxAbs = 0.0f;
    for (const auto& f : wt.frames())
    {
        for (size_t i = 0; i < kWaveformSize; ++i)
        {
            const float a = std::fabs(f[i]);
            if (a > maxAbs)
                maxAbs = a;
        }
    }

    if (maxAbs > 1e-12f)
    {
        const float invMax = 1.0f / maxAbs;
        for (auto& f : wt.frames())
        {
            for (size_t i = 0; i < kWaveformSize; ++i)
                f[i] *= invMax;
        }
    }
}

void removeDc(Frame& frame) noexcept
{
    double sum = 0.0;
    for (size_t i = 0; i < kWaveformSize; ++i)
        sum += static_cast<double>(frame[i]);

    const float mean = static_cast<float>(sum / static_cast<double>(kWaveformSize));
    for (size_t i = 0; i < kWaveformSize; ++i)
        frame[i] -= mean;
}

void removeDcAll(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        removeDc(f);
}

void flipVertical(Frame& frame) noexcept
{
    for (size_t i = 0; i < kWaveformSize; ++i)
        frame[i] = -frame[i];
}

void flipVerticalAll(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        flipVertical(f);
}

void flipHorizontal(Frame& frame) noexcept
{
    for (size_t i = 0; i < kWaveformSize / 2; ++i)
        std::swap(frame[i], frame[kWaveformSize - 1 - i]);
}

void flipHorizontalAll(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        flipHorizontal(f);
}

void fadeEdges(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 16;
    const size_t win = std::max<size_t>(1, kWaveformSize / static_cast<size_t>(gridSize));

    for (size_t i = 0; i < win; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(win);
        const float factor = 0.5f * (1.0f - std::cos(static_cast<float>(kPi) * t));
        frame[i] *= factor;
        frame[kWaveformSize - 1 - i] *= factor;
    }
}

void fadeEdgesAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        fadeEdges(f, gridSize);
}

void crossfadeEdges(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 16;
    const size_t win = std::max<size_t>(1, kWaveformSize / static_cast<size_t>(gridSize));

    for (size_t i = 0; i < win; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(win);
        const float startVal = frame[i];
        const float endVal = frame[kWaveformSize - win + i];

        const float blend = (1.0f - t) * endVal + t * startVal;
        frame[i] = blend;
        frame[kWaveformSize - win + i] = blend;
    }
}

void crossfadeEdgesAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        crossfadeEdges(f, gridSize);
}

void crossfadeEdges16Samples(Frame& frame) noexcept
{
    constexpr size_t win = 16;
    for (size_t i = 0; i < win; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(win);
        const float startVal = frame[i];
        const float endVal = frame[kWaveformSize - win + i];

        const float blend = (1.0f - t) * endVal + t * startVal;
        frame[i] = blend;
        frame[kWaveformSize - win + i] = blend;
    }
}

void crossfadeEdges16SamplesAll(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        crossfadeEdges16Samples(f);
}

void shiftToZeroCrossing(Frame& frame) noexcept
{
    size_t zeroCrossIdx = 0;
    bool found = false;

    // Look for positive zero-crossing (negative to positive or zero to positive)
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const float curr = frame[i];
        const float next = frame[(i + 1) % kWaveformSize];
        if (curr <= 0.0f && next > 0.0f)
        {
            zeroCrossIdx = (i + 1) % kWaveformSize;
            found = true;
            break;
        }
    }

    if (found && zeroCrossIdx != 0)
    {
        Frame shifted;
        for (size_t i = 0; i < kWaveformSize; ++i)
            shifted[i] = frame[(i + zeroCrossIdx) % kWaveformSize];
        frame = shifted;
    }
}

void shiftToZeroCrossingAll(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        shiftToZeroCrossing(f);
}

void initSilence(Frame& frame) noexcept
{
    frame.data.fill(0.0f);
}

void initAllDefault(Wavetable& wt) noexcept
{
    wt.initDefault();
}

bool insertFrame(Wavetable& wt, size_t index, const Frame& frame)
{
    return wt.insertFrame(index, frame);
}

bool removeFrame(Wavetable& wt, size_t index)
{
    return wt.removeFrame(index);
}

bool removeRange(Wavetable& wt, size_t startIndex, size_t endIndex)
{
    if (startIndex > endIndex || endIndex >= wt.numFrames())
        return false;
    const size_t count = endIndex - startIndex + 1;
    if (wt.numFrames() <= count)
        return false; // Preserve at least 1 frame

    wt.frames().erase(wt.frames().begin() + static_cast<std::ptrdiff_t>(startIndex),
                      wt.frames().begin() + static_cast<std::ptrdiff_t>(endIndex + 1));
    return true;
}

bool removeBeginningToSelected(Wavetable& wt, size_t selectedIndex)
{
    if (selectedIndex >= wt.numFrames() - 1)
        return false;
    return removeRange(wt, 0, selectedIndex);
}

bool removeSelectedToEnd(Wavetable& wt, size_t selectedIndex)
{
    if (selectedIndex == 0 || selectedIndex >= wt.numFrames())
        return false;
    return removeRange(wt, selectedIndex, wt.numFrames() - 1);
}

bool removeAllExceptSelected(Wavetable& wt, size_t startIndex, size_t endIndex)
{
    if (startIndex > endIndex || endIndex >= wt.numFrames())
        return false;

    std::vector<Frame> kept(wt.frames().begin() + static_cast<std::ptrdiff_t>(startIndex),
                            wt.frames().begin() + static_cast<std::ptrdiff_t>(endIndex + 1));
    wt.frames() = std::move(kept);
    return true;
}

void reduceTo(Wavetable& wt, size_t step)
{
    if (step <= 1 || wt.numFrames() <= 1)
        return;

    std::vector<Frame> kept;
    for (size_t i = 0; i < wt.numFrames(); i += step)
        kept.push_back(wt.getFrame(i));

    if (kept.empty())
        kept.push_back(wt.getFrame(0));

    wt.frames() = std::move(kept);
}

bool resizeTablesHalf(Wavetable& wt)
{
    const size_t origCount = wt.numFrames();
    if (origCount * 2 > kMaxFrames)
        return false;

    std::vector<Frame> doubled;
    doubled.reserve(origCount * 2);

    for (size_t f = 0; f < origCount; ++f)
    {
        const Frame& orig = wt.getFrame(f);
        Frame half1;
        Frame half2;

        for (size_t i = 0; i < kWaveformSize; ++i)
        {
            const double t1 = static_cast<double>(i) / static_cast<double>(kWaveformSize);
            half1[i] = sampleLinear(orig, t1 * (kWaveformSize / 2));

            const double t2 = static_cast<double>(i) / static_cast<double>(kWaveformSize);
            half2[i] = sampleLinear(orig, (kWaveformSize / 2) + t2 * (kWaveformSize / 2));
        }

        doubled.push_back(half1);
        doubled.push_back(half2);
    }

    wt.frames() = std::move(doubled);
    return true;
}

bool resizeTablesDouble(Wavetable& wt)
{
    const size_t origCount = wt.numFrames();
    if (origCount <= 1)
        return false;

    const size_t newCount = (origCount + 1) / 2;
    std::vector<Frame> merged;
    merged.reserve(newCount);

    for (size_t f = 0; f < origCount; f += 2)
    {
        const Frame& f1 = wt.getFrame(f);
        const Frame& f2 = (f + 1 < origCount) ? wt.getFrame(f + 1) : f1;

        Frame combined;
        for (size_t i = 0; i < kWaveformSize / 2; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize / 2);
            combined[i] = sampleLinear(f1, t * kWaveformSize);
        }
        for (size_t i = 0; i < kWaveformSize / 2; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize / 2);
            combined[(kWaveformSize / 2) + i] = sampleLinear(f2, t * kWaveformSize);
        }
        merged.push_back(combined);
    }

    wt.frames() = std::move(merged);
    return true;
}

void createPwmFromFrame(Wavetable& wt, size_t frameIndex)
{
    if (frameIndex >= wt.numFrames())
        frameIndex = 0;

    const Frame source = wt.getFrame(frameIndex);
    std::vector<Frame> pwmFrames;
    pwmFrames.reserve(kMaxFrames);

    for (size_t f = 0; f < kMaxFrames; ++f)
    {
        const double duty = 0.05 + 0.90 * (static_cast<double>(f) / static_cast<double>(kMaxFrames - 1));
        Frame pf;

        for (size_t i = 0; i < kWaveformSize; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize);
            double mappedT = 0.0;
            if (t < duty)
                mappedT = 0.5 * (t / duty);
            else
                mappedT = 0.5 + 0.5 * ((t - duty) / (1.0 - duty));

            pf[i] = sampleLinear(source, mappedT * static_cast<double>(kWaveformSize));
        }

        pwmFrames.push_back(pf);
    }

    wt.frames() = std::move(pwmFrames);
}

void sampleRedux(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 16;
    const size_t hold = std::max<size_t>(1, kWaveformSize / static_cast<size_t>(gridSize));

    for (size_t i = 0; i < kWaveformSize; i += hold)
    {
        const float val = frame[i];
        const size_t end = std::min(kWaveformSize, i + hold);
        for (size_t j = i + 1; j < end; ++j)
            frame[j] = val;
    }
}

void sampleReduxAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        sampleRedux(f, gridSize);
}

void filterLowPass(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 16;
    const size_t cutoffBin = std::max<size_t>(1, std::min<size_t>(kNumFftBins, static_cast<size_t>(gridSize) * 4));

    fftClearHf(frame, cutoffBin);
}

void filterLowPassAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        filterLowPass(f, gridSize);
}

void removeFundamental(Frame& frame) noexcept
{
    fftSetHarmonic(frame, 1, 0.0f, 0.0f);
}

void removeFundamentalAll(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        removeFundamental(f);
}

void removeLowSpectra(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 1;
    const size_t maxBin = std::min<size_t>(kNumFftBins, static_cast<size_t>(gridSize));

    fftClearLf(frame, maxBin);
}

void removeLowSpectraAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        removeLowSpectra(f, gridSize);
}

void removeLowPhases(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 1;
    const size_t maxBin = std::min<size_t>(kNumFftBins, static_cast<size_t>(gridSize));

    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    for (size_t k = 1; k <= maxBin; ++k)
        phases[k] = 0.0f;

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void removeLowPhasesAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        removeLowPhases(f, gridSize);
}

void nudgeFundamentalPhaseTo50Percent(Frame& frame) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    phases[1] = static_cast<float>(kPi * 0.5); // 50% = 90 degrees / pi*0.5

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void nudgeFundamentalPhaseTo50PercentAll(Wavetable& wt) noexcept
{
    for (auto& f : wt.frames())
        nudgeFundamentalPhaseTo50Percent(f);
}

void setSpectraThisFrameToAll(Wavetable& wt, size_t frameIndex) noexcept
{
    if (frameIndex >= wt.numFrames())
        return;

    std::array<float, kNumFftBins + 1> refMags {};
    std::array<float, kNumFftBins + 1> dummyPhases {};
    Radix2Fft::realToPolar(wt.getFrame(frameIndex).sampleData(), refMags.data(), dummyPhases.data(), kWaveformSize);

    for (size_t f = 0; f < wt.numFrames(); ++f)
    {
        if (f == frameIndex)
            continue;
        std::array<float, kNumFftBins + 1> curMags {};
        std::array<float, kNumFftBins + 1> curPhases {};
        Radix2Fft::realToPolar(wt.getFrame(f).sampleData(), curMags.data(), curPhases.data(), kWaveformSize);
        Radix2Fft::polarToReal(refMags.data(), curPhases.data(), wt.getFrame(f).sampleData(), kWaveformSize);
    }
}

void setPhasesThisFrameToAll(Wavetable& wt, size_t frameIndex) noexcept
{
    if (frameIndex >= wt.numFrames())
        return;

    std::array<float, kNumFftBins + 1> dummyMags {};
    std::array<float, kNumFftBins + 1> refPhases {};
    Radix2Fft::realToPolar(wt.getFrame(frameIndex).sampleData(), dummyMags.data(), refPhases.data(), kWaveformSize);

    for (size_t f = 0; f < wt.numFrames(); ++f)
    {
        if (f == frameIndex)
            continue;
        std::array<float, kNumFftBins + 1> curMags {};
        std::array<float, kNumFftBins + 1> curPhases {};
        Radix2Fft::realToPolar(wt.getFrame(f).sampleData(), curMags.data(), curPhases.data(), kWaveformSize);
        Radix2Fft::polarToReal(curMags.data(), refPhases.data(), wt.getFrame(f).sampleData(), kWaveformSize);
    }
}

void setSpectraFromOther(Wavetable& target, const Wavetable& source) noexcept
{
    const size_t count = std::min(target.numFrames(), source.numFrames());
    for (size_t f = 0; f < count; ++f)
    {
        std::array<float, kNumFftBins + 1> srcMags {};
        std::array<float, kNumFftBins + 1> srcPhases {};
        Radix2Fft::realToPolar(source.getFrame(f).sampleData(), srcMags.data(), srcPhases.data(), kWaveformSize);

        std::array<float, kNumFftBins + 1> tgtMags {};
        std::array<float, kNumFftBins + 1> tgtPhases {};
        Radix2Fft::realToPolar(target.getFrame(f).sampleData(), tgtMags.data(), tgtPhases.data(), kWaveformSize);

        Radix2Fft::polarToReal(srcMags.data(), tgtPhases.data(), target.getFrame(f).sampleData(), kWaveformSize);
    }
}

void setPhasesFromOther(Wavetable& target, const Wavetable& source) noexcept
{
    const size_t count = std::min(target.numFrames(), source.numFrames());
    for (size_t f = 0; f < count; ++f)
    {
        std::array<float, kNumFftBins + 1> srcMags {};
        std::array<float, kNumFftBins + 1> srcPhases {};
        Radix2Fft::realToPolar(source.getFrame(f).sampleData(), srcMags.data(), srcPhases.data(), kWaveformSize);

        std::array<float, kNumFftBins + 1> tgtMags {};
        std::array<float, kNumFftBins + 1> tgtPhases {};
        Radix2Fft::realToPolar(target.getFrame(f).sampleData(), tgtMags.data(), tgtPhases.data(), kWaveformSize);

        Radix2Fft::polarToReal(tgtMags.data(), srcPhases.data(), target.getFrame(f).sampleData(), kWaveformSize);
    }
}

void subtractSpectraFromOther(Wavetable& target, const Wavetable& source) noexcept
{
    const size_t count = std::min(target.numFrames(), source.numFrames());
    for (size_t f = 0; f < count; ++f)
    {
        std::array<float, kNumFftBins + 1> srcMags {};
        std::array<float, kNumFftBins + 1> srcPhases {};
        Radix2Fft::realToPolar(source.getFrame(f).sampleData(), srcMags.data(), srcPhases.data(), kWaveformSize);

        std::array<float, kNumFftBins + 1> tgtMags {};
        std::array<float, kNumFftBins + 1> tgtPhases {};
        Radix2Fft::realToPolar(target.getFrame(f).sampleData(), tgtMags.data(), tgtPhases.data(), kWaveformSize);

        for (size_t k = 0; k <= kNumFftBins; ++k)
        {
            tgtMags[k] = std::max(0.0f, tgtMags[k] - srcMags[k]);
        }

        Radix2Fft::polarToReal(tgtMags.data(), tgtPhases.data(), target.getFrame(f).sampleData(), kWaveformSize);
    }
}

void blurSpectraAdjacentBins(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 4;
    const int radius = std::max(1, gridSize / 2);

    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    std::array<float, kNumFftBins + 1> blurredMags = mags;
    for (int k = 1; k <= static_cast<int>(kNumFftBins); ++k)
    {
        double sum = 0.0;
        int count = 0;
        for (int r = -radius; r <= radius; ++r)
        {
            const int neighbor = k + r;
            if (neighbor >= 1 && neighbor <= static_cast<int>(kNumFftBins))
            {
                sum += static_cast<double>(mags[static_cast<size_t>(neighbor)]);
                ++count;
            }
        }
        blurredMags[static_cast<size_t>(k)] = static_cast<float>(sum / static_cast<double>(count));
    }

    Radix2Fft::polarToReal(blurredMags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void blurSpectraAdjacentBinsAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        blurSpectraAdjacentBins(f, gridSize);
}

void blurPhasesAdjacentBins(Frame& frame, int gridSize) noexcept
{
    if (gridSize <= 0)
        gridSize = 4;
    const int radius = std::max(1, gridSize / 2);

    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    std::array<float, kNumFftBins + 1> blurredPhases = phases;
    for (int k = 1; k <= static_cast<int>(kNumFftBins); ++k)
    {
        double sumSin = 0.0;
        double sumCos = 0.0;
        for (int r = -radius; r <= radius; ++r)
        {
            const int neighbor = k + r;
            if (neighbor >= 1 && neighbor <= static_cast<int>(kNumFftBins))
            {
                const float p = phases[static_cast<size_t>(neighbor)];
                sumSin += std::sin(p);
                sumCos += std::cos(p);
            }
        }
        blurredPhases[static_cast<size_t>(k)] = static_cast<float>(std::atan2(sumSin, sumCos));
    }

    Radix2Fft::polarToReal(mags.data(), blurredPhases.data(), frame.sampleData(), kWaveformSize);
}

void blurPhasesAdjacentBinsAll(Wavetable& wt, int gridSize) noexcept
{
    for (auto& f : wt.frames())
        blurPhasesAdjacentBins(f, gridSize);
}

void blurSpectraAdjacentFrames(Wavetable& wt, int gridSize) noexcept
{
    if (wt.numFrames() <= 1)
        return;
    if (gridSize <= 0)
        gridSize = 4;
    const int radius = std::max(1, gridSize / 2);

    const size_t numF = wt.numFrames();
    std::vector<std::array<float, kNumFftBins + 1>> allMags(numF);
    std::vector<std::array<float, kNumFftBins + 1>> allPhases(numF);

    for (size_t f = 0; f < numF; ++f)
        Radix2Fft::realToPolar(wt.getFrame(f).sampleData(), allMags[f].data(), allPhases[f].data(), kWaveformSize);

    for (size_t f = 0; f < numF; ++f)
    {
        std::array<float, kNumFftBins + 1> blurredMag {};
        for (size_t k = 0; k <= kNumFftBins; ++k)
        {
            double sum = 0.0;
            int count = 0;
            for (int r = -radius; r <= radius; ++r)
            {
                const int neighborF = static_cast<int>(f) + r;
                if (neighborF >= 0 && neighborF < static_cast<int>(numF))
                {
                    sum += static_cast<double>(allMags[static_cast<size_t>(neighborF)][k]);
                    ++count;
                }
            }
            blurredMag[k] = static_cast<float>(sum / static_cast<double>(count));
        }

        Radix2Fft::polarToReal(blurredMag.data(), allPhases[f].data(), wt.getFrame(f).sampleData(), kWaveformSize);
    }
}

void blurPhasesAdjacentFrames(Wavetable& wt, int gridSize) noexcept
{
    if (wt.numFrames() <= 1)
        return;
    if (gridSize <= 0)
        gridSize = 4;
    const int radius = std::max(1, gridSize / 2);

    const size_t numF = wt.numFrames();
    std::vector<std::array<float, kNumFftBins + 1>> allMags(numF);
    std::vector<std::array<float, kNumFftBins + 1>> allPhases(numF);

    for (size_t f = 0; f < numF; ++f)
        Radix2Fft::realToPolar(wt.getFrame(f).sampleData(), allMags[f].data(), allPhases[f].data(), kWaveformSize);

    for (size_t f = 0; f < numF; ++f)
    {
        std::array<float, kNumFftBins + 1> blurredPhase {};
        for (size_t k = 0; k <= kNumFftBins; ++k)
        {
            double sumSin = 0.0;
            double sumCos = 0.0;
            for (int r = -radius; r <= radius; ++r)
            {
                const int neighborF = static_cast<int>(f) + r;
                if (neighborF >= 0 && neighborF < static_cast<int>(numF))
                {
                    const float p = allPhases[static_cast<size_t>(neighborF)][k];
                    sumSin += std::sin(p);
                    sumCos += std::cos(p);
                }
            }
            blurredPhase[k] = static_cast<float>(std::atan2(sumSin, sumCos));
        }

        Radix2Fft::polarToReal(allMags[f].data(), blurredPhase.data(), wt.getFrame(f).sampleData(), kWaveformSize);
    }
}

void smoothTimeDomain(Frame& frame, int radius) noexcept
{
    if (radius <= 0)
        return;
    if (radius > 128)
        radius = 128;

    Frame smoothed;
    const double winSize = static_cast<double>(2 * radius + 1);

    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        double sum = 0.0;
        for (int r = -radius; r <= radius; ++r)
        {
            const int idx = (static_cast<int>(i) + r + static_cast<int>(kWaveformSize)) % static_cast<int>(kWaveformSize);
            sum += static_cast<double>(frame[static_cast<size_t>(idx)]);
        }
        smoothed[i] = static_cast<float>(sum / winSize);
    }
    frame = smoothed;
}

void bendTimeDomain(Frame& frame, float amount) noexcept
{
    if (std::fabs(amount) < 1e-4f)
        return;

    const double power = (amount >= 0.0f) ? (1.0 + static_cast<double>(amount) * 4.0)
                                          : (1.0 / (1.0 - static_cast<double>(amount) * 4.0));

    Frame bent;
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(kWaveformSize);
        const double warpedT = std::pow(t, power);
        bent[i] = sampleLinear(frame, warpedT * static_cast<double>(kWaveformSize));
    }
    frame = bent;
}

void fmBetweenFrames(Frame& carrier, const Frame& modulator, float depth) noexcept
{
    Frame result;
    for (size_t i = 0; i < kWaveformSize; ++i)
    {
        const double modOffset = static_cast<double>(depth * modulator[i] * static_cast<float>(kWaveformSize));
        const double readPos = static_cast<double>(i) + modOffset;
        result[i] = sampleLinear(carrier, readPos);
    }
    carrier = result;
}

void amBetweenFrames(Frame& carrier, const Frame& modulator, float depth) noexcept
{
    for (size_t i = 0; i < kWaveformSize; ++i)
        carrier[i] *= (1.0f + depth * modulator[i]);
}

void interpolateBetweenKeyframes(Wavetable& wt, const std::vector<size_t>& keyframeIndices, MorphMode mode)
{
    if (keyframeIndices.size() < 2 || wt.numFrames() < 2)
        return;

    for (size_t s = 0; s < keyframeIndices.size() - 1; ++s)
    {
        const size_t kStart = keyframeIndices[s];
        const size_t kEnd = keyframeIndices[s + 1];
        if (kEnd <= kStart || kEnd >= wt.numFrames())
            continue;

        const Frame& fA = wt.getFrame(kStart);
        const Frame& fB = wt.getFrame(kEnd);

        std::array<float, kNumFftBins + 1> magA {};
        std::array<float, kNumFftBins + 1> phaseA {};
        std::array<float, kNumFftBins + 1> magB {};
        std::array<float, kNumFftBins + 1> phaseB {};

        if (mode != MorphMode::Crossfade)
        {
            Radix2Fft::realToPolar(fA.sampleData(), magA.data(), phaseA.data(), kWaveformSize);
            Radix2Fft::realToPolar(fB.sampleData(), magB.data(), phaseB.data(), kWaveformSize);
        }

        const size_t span = kEnd - kStart;
        for (size_t idx = kStart + 1; idx < kEnd; ++idx)
        {
            const float t = static_cast<float>(idx - kStart) / static_cast<float>(span);
            Frame& target = wt.getFrame(idx);

            if (mode == MorphMode::Crossfade)
            {
                for (size_t i = 0; i < kWaveformSize; ++i)
                    target[i] = (1.0f - t) * fA[i] + t * fB[i];
            }
            else
            {
                std::array<float, kNumFftBins + 1> interMag {};
                std::array<float, kNumFftBins + 1> interPhase {};

                for (size_t k = 0; k <= kNumFftBins; ++k)
                {
                    interMag[k] = (1.0f - t) * magA[k] + t * magB[k];

                    if (mode == MorphMode::SpectralZeroAllPhases)
                    {
                        interPhase[k] = 0.0f;
                    }
                    else if (mode == MorphMode::SpectralZeroFundPhase && k == 1)
                    {
                        interPhase[k] = 0.0f;
                    }
                    else
                    {
                        const float diff = wrapAngle(phaseB[k] - phaseA[k]);
                        interPhase[k] = phaseA[k] + t * diff;
                    }
                }

                Radix2Fft::polarToReal(interMag.data(), interPhase.data(), target.sampleData(), kWaveformSize);
            }
        }
    }
}

void removeMorphTables(Wavetable& wt, const std::vector<size_t>& keyframeIndices)
{
    if (keyframeIndices.empty())
        return;

    std::vector<Frame> kept;
    kept.reserve(keyframeIndices.size());

    for (size_t idx : keyframeIndices)
    {
        if (idx < wt.numFrames())
            kept.push_back(wt.getFrame(idx));
    }

    if (!kept.empty())
        wt.frames() = std::move(kept);
}

void sortFrames(Wavetable& wt, SortMode mode)
{
    const size_t numF = wt.numFrames();
    if (numF <= 1)
        return;

    if (mode == SortMode::Reverse)
    {
        std::reverse(wt.frames().begin(), wt.frames().end());
        return;
    }

    if (mode == SortMode::Randomize)
    {
        uint32_t seed = 42u;
        for (size_t i = numF - 1; i > 0; --i)
        {
            const size_t j = static_cast<size_t>(pseudoRand(seed) * static_cast<float>(i + 1)) % (i + 1);
            std::swap(wt.getFrame(i), wt.getFrame(j));
        }
        return;
    }

    struct FrameScore
    {
        size_t index;
        float score;
    };

    std::vector<FrameScore> scores(numF);

    for (size_t f = 0; f < numF; ++f)
    {
        std::array<float, kNumFftBins + 1> mags {};
        std::array<float, kNumFftBins + 1> phases {};
        Radix2Fft::realToPolar(wt.getFrame(f).sampleData(), mags.data(), phases.data(), kWaveformSize);

        float score = 0.0f;
        switch (mode)
        {
        case SortMode::PeakSpect:
        {
            size_t maxBin = 1;
            float maxM = 0.0f;
            for (size_t k = 1; k <= kNumFftBins; ++k)
            {
                if (mags[k] > maxM)
                {
                    maxM = mags[k];
                    maxBin = k;
                }
            }
            score = static_cast<float>(maxBin);
            break;
        }

        case SortMode::AverageSpect:
        {
            double sumK = 0.0;
            double sumM = 0.0;
            for (size_t k = 1; k <= kNumFftBins; ++k)
            {
                sumK += static_cast<double>(k) * static_cast<double>(mags[k]);
                sumM += static_cast<double>(mags[k]);
            }
            score = (sumM > 1e-9) ? static_cast<float>(sumK / sumM) : 0.0f;
            break;
        }

        case SortMode::PeakAmount:
        {
            float maxM = 0.0f;
            for (size_t k = 1; k <= kNumFftBins; ++k)
                if (mags[k] > maxM)
                    maxM = mags[k];
            score = maxM;
            break;
        }

        case SortMode::NumWithSpect:
        {
            size_t count = 0;
            for (size_t k = 1; k <= kNumFftBins; ++k)
                if (mags[k] > 0.005f)
                    ++count;
            score = static_cast<float>(count);
            break;
        }

        case SortMode::HighestWithSpect:
        {
            size_t highest = 0;
            for (size_t k = 1; k <= kNumFftBins; ++k)
                if (mags[k] > 0.005f)
                    highest = k;
            score = static_cast<float>(highest);
            break;
        }

        case SortMode::FundamentalAmount:
            score = mags[1];
            break;

        case SortMode::Randomize:
        case SortMode::Reverse:
            break;
        }

        scores[f] = { f, score };
    }

    std::sort(scores.begin(), scores.end(), [](const FrameScore& a, const FrameScore& b) {
        return a.score < b.score;
    });

    std::vector<Frame> sorted;
    sorted.reserve(numF);
    for (const auto& s : scores)
        sorted.push_back(wt.getFrame(s.index));

    wt.frames() = std::move(sorted);
}

// FFT Area Menu Operations
void fftClearAll(Frame& frame) noexcept
{
    initSilence(frame);
}

void fftClearHf(Frame& frame, size_t clickedBin) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    for (size_t k = clickedBin; k <= kNumFftBins; ++k)
        mags[k] = 0.0f;

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftClearLf(Frame& frame, size_t clickedBin) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    const size_t end = std::min(clickedBin, kNumFftBins);
    for (size_t k = 1; k <= end; ++k)
        mags[k] = 0.0f;

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftGenerateSaw(Frame& frame) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    mags[0] = 0.0f;
    phases[0] = 0.0f;

    for (size_t k = 1; k <= kNumFftBins; ++k)
    {
        mags[k] = 1.0f / static_cast<float>(k);
        phases[k] = 0.0f;
    }

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftRandomizeLowBins(Frame& frame, size_t maxBin, bool allowHalfOnly) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    const size_t limit = std::min(maxBin, kNumFftBins);
    uint32_t seed = 777u;

    for (size_t k = 1; k <= limit; ++k)
    {
        float rnd = pseudoRand(seed);
        if (allowHalfOnly)
            rnd = (rnd < 0.33f) ? 0.0f : ((rnd < 0.66f) ? 0.5f : 1.0f);
        mags[k] = rnd;
    }

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftRandomizeAll(Frame& frame) noexcept
{
    fftRandomizeLowBins(frame, kNumFftBins, false);
}

void fftCreateRandomSeriesGaps(Frame& frame, float gapProbability) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    uint32_t seed = 12345u;
    for (size_t k = 1; k <= kNumFftBins; ++k)
    {
        if (pseudoRand(seed) < gapProbability)
            mags[k] = 0.0f;
    }

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftProgressiveFade(Frame& frame, float fadePower) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    for (size_t k = 1; k <= kNumFftBins; ++k)
    {
        const float factor = 1.0f / std::pow(static_cast<float>(k), fadePower * 0.5f);
        mags[k] *= factor;
    }

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftShiftOctaveUp(Frame& frame) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    std::array<float, kNumFftBins + 1> shiftedMags {};
    std::array<float, kNumFftBins + 1> shiftedPhases {};

    for (size_t k = 1; k <= kNumFftBins / 2; ++k)
    {
        shiftedMags[k * 2] = mags[k];
        shiftedPhases[k * 2] = phases[k];
    }

    Radix2Fft::polarToReal(shiftedMags.data(), shiftedPhases.data(), frame.sampleData(), kWaveformSize);
}

void fftShiftOctaveDown(Frame& frame) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    std::array<float, kNumFftBins + 1> shiftedMags {};
    std::array<float, kNumFftBins + 1> shiftedPhases {};

    for (size_t k = 2; k <= kNumFftBins; k += 2)
    {
        shiftedMags[k / 2] = mags[k];
        shiftedPhases[k / 2] = phases[k];
    }

    Radix2Fft::polarToReal(shiftedMags.data(), shiftedPhases.data(), frame.sampleData(), kWaveformSize);
}

void fftRepeatBinGroup(Frame& frame, size_t cursorBin) noexcept
{
    if (cursorBin <= 1)
        return;
    if (cursorBin > kNumFftBins)
        cursorBin = kNumFftBins;

    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    for (size_t k = cursorBin + 1; k <= kNumFftBins; ++k)
    {
        const size_t srcIdx = 1 + ((k - 1) % cursorBin);
        mags[k] = mags[srcIdx];
        phases[k] = phases[srcIdx];
    }

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftScaleFreqValuesByBinIndex(Frame& frame) noexcept
{
    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    for (size_t k = 1; k <= kNumFftBins; ++k)
        mags[k] *= static_cast<float>(k);

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

void fftSetHarmonic(Frame& frame, size_t binIndex, float amplitude, float phase) noexcept
{
    if (binIndex > kNumFftBins)
        return;

    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};
    Radix2Fft::realToPolar(frame.sampleData(), mags.data(), phases.data(), kWaveformSize);

    mags[binIndex] = amplitude;
    phases[binIndex] = phase;

    Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
}

} // namespace synthyboy::wtedit::ops
