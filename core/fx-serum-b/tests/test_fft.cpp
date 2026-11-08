// SynthyBoy - core/fx-serum-b tests: Radix-2 FFT tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "fft.h"

#include <cmath>
#include <vector>

#include "test_runner.h"

using namespace synthyboy::fx;

namespace
{
constexpr double kPi = 3.14159265358979323846;
} // namespace

TEST (fft_impulse_roundtrip)
{
    const int sizes[] = { 64, 128, 256, 512 };
    for (int size : sizes)
    {
        Radix2FFT fft (size);
        std::vector<Complex> buf (static_cast<size_t> (size));
        buf[0] = Complex (1.0f, 0.0f); // unit impulse

        fft.fft (buf.data());

        // Forward FFT of unit impulse should have magnitude 1.0 at all frequencies
        for (int i = 0; i < size; ++i)
        {
            CHECK_NEAR (buf[static_cast<size_t> (i)].r, 1.0f, 1e-5);
            CHECK_NEAR (buf[static_cast<size_t> (i)].i, 0.0f, 1e-5);
        }

        // Inverse FFT should recover the unit impulse
        fft.ifft (buf.data());
        CHECK_NEAR (buf[0].r, 1.0f, 1e-5);
        CHECK_NEAR (buf[0].i, 0.0f, 1e-5);
        for (int i = 1; i < size; ++i)
        {
            CHECK_NEAR (buf[static_cast<size_t> (i)].r, 0.0f, 1e-5);
            CHECK_NEAR (buf[static_cast<size_t> (i)].i, 0.0f, 1e-5);
        }
    }
}

TEST (fft_sine_peak)
{
    const int N = 256;
    Radix2FFT fft (N);
    std::vector<Complex> buf (N);
    const int bin = 17; // pure sinusoidal frequency bin
    for (int i = 0; i < N; ++i)
    {
        const double phase = 2.0 * kPi * bin * i / N;
        buf[static_cast<size_t> (i)] = Complex (static_cast<float> (std::cos (phase)), 0.0f);
    }

    fft.fft (buf.data());

    // Peak at bin and N - bin, amplitude ~ N / 2
    float maxMag = 0.0f;
    int maxBin = -1;
    for (int i = 0; i < N / 2; ++i)
    {
        const float mag = buf[static_cast<size_t> (i)].abs();
        if (mag > maxMag)
        {
            maxMag = mag;
            maxBin = i;
        }
    }
    CHECK_EQ (maxBin, bin);
    CHECK_NEAR (maxMag, static_cast<float> (N / 2), 1e-4);
}
