// SynthyBoy - wavetable-edit: Radix-2 FFT implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "fft.h"

#include <cmath>
#include <vector>

namespace synthyboy::wtedit
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

void bitReverse(float* real, float* imag, size_t size) noexcept
{
    size_t j = 0;
    for (size_t i = 0; i < size; ++i)
    {
        if (i < j)
        {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        size_t bit = size >> 1;
        while (bit > 0 && (j & bit))
        {
            j &= ~bit;
            bit >>= 1;
        }
        j |= bit;
    }
}

void transform(float* real, float* imag, size_t size, bool inverse) noexcept
{
    if (!Radix2Fft::isPowerOfTwo(size) || size <= 1)
        return;

    bitReverse(real, imag, size);

    const double angleSign = inverse ? 1.0 : -1.0;

    for (size_t len = 2; len <= size; len <<= 1)
    {
        const double angle = angleSign * 2.0 * kPi / static_cast<double>(len);
        const double wstepRe = std::cos(angle);
        const double wstepIm = std::sin(angle);

        const size_t halfLen = len >> 1;

        for (size_t i = 0; i < size; i += len)
        {
            double wRe = 1.0;
            double wIm = 0.0;

            for (size_t k = 0; k < halfLen; ++k)
            {
                const size_t uIdx = i + k;
                const size_t vIdx = i + k + halfLen;

                const double vRe = static_cast<double>(real[vIdx]);
                const double vIm = static_cast<double>(imag[vIdx]);

                const double tRe = vRe * wRe - vIm * wIm;
                const double tIm = vRe * wIm + vIm * wRe;

                const double uRe = static_cast<double>(real[uIdx]);
                const double uIm = static_cast<double>(imag[uIdx]);

                real[uIdx] = static_cast<float>(uRe + tRe);
                imag[uIdx] = static_cast<float>(uIm + tIm);
                real[vIdx] = static_cast<float>(uRe - tRe);
                imag[vIdx] = static_cast<float>(uIm - tIm);

                const double nextWre = wRe * wstepRe - wIm * wstepIm;
                const double nextWim = wRe * wstepIm + wIm * wstepRe;
                wRe = nextWre;
                wIm = nextWim;
            }
        }
    }

    if (inverse)
    {
        const float invScale = 1.0f / static_cast<float>(size);
        for (size_t i = 0; i < size; ++i)
        {
            real[i] *= invScale;
            imag[i] *= invScale;
        }
    }
}
} // namespace

void Radix2Fft::forward(float* real, float* imag, size_t size) noexcept
{
    transform(real, imag, size, false);
}

void Radix2Fft::inverse(float* real, float* imag, size_t size) noexcept
{
    transform(real, imag, size, true);
}

void Radix2Fft::realToPolar(const float* timeSamples, float* magnitudes, float* phases, size_t size) noexcept
{
    if (!isPowerOfTwo(size))
        return;

    std::vector<float> real(size);
    std::vector<float> imag(size, 0.0f);

    for (size_t i = 0; i < size; ++i)
        real[i] = timeSamples[i];

    forward(real.data(), imag.data(), size);

    const size_t numBins = (size / 2) + 1;
    for (size_t k = 0; k < numBins; ++k)
    {
        const float r = real[k];
        const float im = imag[k];
        magnitudes[k] = std::sqrt(r * r + im * im);
        phases[k] = std::atan2(im, r);
    }
}

void Radix2Fft::polarToReal(const float* magnitudes, const float* phases, float* timeSamples, size_t size) noexcept
{
    if (!isPowerOfTwo(size))
        return;

    std::vector<float> real(size, 0.0f);
    std::vector<float> imag(size, 0.0f);

    const size_t half = size / 2;

    // DC bin (k = 0)
    real[0] = magnitudes[0] * std::cos(phases[0]);
    imag[0] = 0.0f;

    // Nyquist bin (k = half)
    real[half] = magnitudes[half] * std::cos(phases[half]);
    imag[half] = 0.0f;

    // Bins 1 .. half - 1 with Hermitian conjugate symmetry
    for (size_t k = 1; k < half; ++k)
    {
        const float r = magnitudes[k] * std::cos(phases[k]);
        const float im = magnitudes[k] * std::sin(phases[k]);

        real[k] = r;
        imag[k] = im;

        real[size - k] = r;
        imag[size - k] = -im;
    }

    inverse(real.data(), imag.data(), size);

    for (size_t i = 0; i < size; ++i)
        timeSamples[i] = real[i];
}

} // namespace synthyboy::wtedit
