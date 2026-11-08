// SynthyBoy - wavetable-edit: Image import (grayscale/RGBA to additive wavetable).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace synthyboy::wtedit
{
bool importImageGrayscale(const uint8_t* grayscale, int width, int height, Wavetable& outWt)
{
    if (grayscale == nullptr || width <= 0 || height <= 0)
        return false;

    const size_t numFrames = std::min<size_t>(static_cast<size_t>(width), kMaxFrames);
    const size_t maxHarmonics = std::min<size_t>(static_cast<size_t>(height), kNumFftBins);

    std::vector<Frame> frames;
    frames.reserve(numFrames);

    std::array<float, kNumFftBins + 1> mags {};
    std::array<float, kNumFftBins + 1> phases {};

    for (size_t x = 0; x < numFrames; ++x)
    {
        mags.fill(0.0f);
        phases.fill(0.0f);

        for (size_t y = 0; y < maxHarmonics; ++y)
        {
            // Bottom of the image corresponds to the fundamental harmonic (y = 0 -> k = 1)
            // or from bottom up: row (height - 1 - y)
            const size_t row = static_cast<size_t>(height - 1) - y;
            const size_t pixelIdx = row * static_cast<size_t>(width) + x;
            const float lum = static_cast<float>(grayscale[pixelIdx]) / 255.0f;

            const size_t bin = y + 1;
            mags[bin] = lum;
        }

        Frame frame;
        Radix2Fft::polarToReal(mags.data(), phases.data(), frame.sampleData(), kWaveformSize);
        ops::normalise(frame);
        frames.push_back(frame);
    }

    outWt.frames() = std::move(frames);
    return true;
}

bool importImageRgba(const uint8_t* rgba, int width, int height, Wavetable& outWt)
{
    if (rgba == nullptr || width <= 0 || height <= 0)
        return false;

    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> gray(totalPixels);

    for (size_t i = 0; i < totalPixels; ++i)
    {
        const size_t byteIdx = i * 4;
        const float r = static_cast<float>(rgba[byteIdx]);
        const float g = static_cast<float>(rgba[byteIdx + 1]);
        const float b = static_cast<float>(rgba[byteIdx + 2]);
        const float a = static_cast<float>(rgba[byteIdx + 3]) / 255.0f;

        // ITU-R BT.601 luminance
        const float lum = (0.299f * r + 0.587f * g + 0.114f * b) * a;
        gray[i] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, lum)));
    }

    return importImageGrayscale(gray.data(), width, height, outWt);
}

} // namespace synthyboy::wtedit
