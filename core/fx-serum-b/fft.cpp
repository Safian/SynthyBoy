// SynthyBoy - core/fx-serum-b: In-place radix-2 Cooley-Tukey FFT implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "fft.h"

#include <cassert>
#include <cstring>

namespace synthyboy::fx
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
} // namespace

Radix2FFT::Radix2FFT (int size)
{
    init (size);
}

void Radix2FFT::init (int size)
{
    assert (size > 0 && (size & (size - 1)) == 0 && "Radix2FFT size must be a power of 2");
    m_size = size;
    m_bitRev.resize (static_cast<size_t> (m_size));
    m_twiddles.resize (static_cast<size_t> (m_size / 2));

    // Bit-reversal permutation table
    int j = 0;
    for (int i = 0; i < m_size - 1; ++i)
    {
        m_bitRev[static_cast<size_t> (i)] = j;
        int k = m_size >> 1;
        while (k <= j)
        {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
    m_bitRev[static_cast<size_t> (m_size - 1)] = m_size - 1;

    // Twiddle factors: W_N^k = exp(-2*pi*j*k / N)
    for (int k = 0; k < m_size / 2; ++k)
    {
        const double angle = -2.0 * kPi * static_cast<double> (k) / static_cast<double> (m_size);
        m_twiddles[static_cast<size_t> (k)] = Complex (static_cast<float> (std::cos (angle)),
                                                       static_cast<float> (std::sin (angle)));
    }
}

void Radix2FFT::fft (Complex* buffer) const noexcept
{
    if (m_size <= 1 || buffer == nullptr)
        return;

    // 1. Bit-reversal reordering
    for (int i = 0; i < m_size; ++i)
    {
        const int j = m_bitRev[static_cast<size_t> (i)];
        if (i < j)
        {
            const Complex tmp = buffer[i];
            buffer[i] = buffer[j];
            buffer[j] = tmp;
        }
    }

    // 2. Cooley-Tukey decimation-in-time butterflies
    for (int len = 2; len <= m_size; len <<= 1)
    {
        const int halfLen = len >> 1;
        const int twiddleStep = m_size / len;
        for (int i = 0; i < m_size; i += len)
        {
            for (int k = 0; k < halfLen; ++k)
            {
                const Complex w = m_twiddles[static_cast<size_t> (k * twiddleStep)];
                const Complex u = buffer[i + k];
                const Complex v = buffer[i + k + halfLen] * w;
                buffer[i + k] = u + v;
                buffer[i + k + halfLen] = u - v;
            }
        }
    }
}

void Radix2FFT::ifft (Complex* buffer) const noexcept
{
    if (m_size <= 1 || buffer == nullptr)
        return;

    // IFFT via conjugate trick: ifft(X) = conj(fft(conj(X))) / N
    for (int i = 0; i < m_size; ++i)
        buffer[i].i = -buffer[i].i;

    fft (buffer);

    const float invN = 1.0f / static_cast<float> (m_size);
    for (int i = 0; i < m_size; ++i)
    {
        buffer[i].r *= invN;
        buffer[i].i = -buffer[i].i * invN;
    }
}

void Radix2FFT::forwardReal (const float* realIn, int realCount, Complex* complexOut) const noexcept
{
    if (complexOut == nullptr || m_size <= 0)
        return;

    const int count = std::min (realCount, m_size);
    for (int i = 0; i < count; ++i)
        complexOut[i] = Complex (realIn[i], 0.0f);
    for (int i = count; i < m_size; ++i)
        complexOut[i] = Complex (0.0f, 0.0f);

    fft (complexOut);
}

void Radix2FFT::inverseReal (Complex* complexInOut, float* realOut) const noexcept
{
    if (complexInOut == nullptr || realOut == nullptr || m_size <= 0)
        return;

    ifft (complexInOut);

    for (int i = 0; i < m_size; ++i)
        realOut[i] = complexInOut[i].r;
}
} // namespace synthyboy::fx
