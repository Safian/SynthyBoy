// SynthyBoy - MPC shell: a small radix-2 FFT with a Hann window for the live spectrum display (no external dependency).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Used by the display thread only (never by the audio thread): the constructor allocates, magnitudes() does not. Real input of n samples (n a power of two), Hann window
// (periodic), complex FFT of size n in float, output n / 2 + 1 magnitudes scaled so that a sine of amplitude A (full scale = 1) whose frequency is a bin centre gives A
// (the sum of the window is n / 2, the sine splits its amplitude over the two sides of the spectrum: scale 4 / n).
#pragma once

#include <cmath>
#include <vector>

namespace synthyboy_mpc
{
constexpr int kSpectrumFftSize = 2048;

class SpectrumFft
{
public:
    explicit SpectrumFft (int n = kSpectrumFftSize) : n_ (n), window_ (static_cast<size_t> (n)), cos_ (static_cast<size_t> (n / 2)), sin_ (static_cast<size_t> (n / 2)),
                                                      rev_ (static_cast<size_t> (n)), re_ (static_cast<size_t> (n)), im_ (static_cast<size_t> (n))
    {
        const double pi = 3.14159265358979323846;
        int bits = 0;
        while ((1 << bits) < n_)
            ++bits;
        for (int i = 0; i < n_; ++i)
        {
            window_[static_cast<size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / n_));
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev_[static_cast<size_t> (i)] = r;
        }
        for (int i = 0; i < n_ / 2; ++i)
        {
            cos_[static_cast<size_t> (i)] = static_cast<float> (std::cos (2.0 * pi * i / n_));
            sin_[static_cast<size_t> (i)] = static_cast<float> (-std::sin (2.0 * pi * i / n_));
        }
    }

    int size() const noexcept { return n_; }
    int bins() const noexcept { return n_ / 2 + 1; }

    /** `in`: size() samples; `out`: bins() magnitudes. */
    void magnitudes (const float* in, float* out)
    {
        for (int i = 0; i < n_; ++i)
        {
            const size_t r = static_cast<size_t> (rev_[static_cast<size_t> (i)]);
            re_[r] = in[i] * window_[static_cast<size_t> (i)];
            im_[r] = 0.0f;
        }
        for (int len = 2; len <= n_; len <<= 1)
        {
            const int half = len / 2, step = n_ / len;
            for (int start = 0; start < n_; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const float c = cos_[static_cast<size_t> (k * step)], s = sin_[static_cast<size_t> (k * step)];
                    const size_t a = static_cast<size_t> (start + k), b = a + static_cast<size_t> (half);
                    const float tr = re_[b] * c - im_[b] * s, ti = re_[b] * s + im_[b] * c;
                    re_[b] = re_[a] - tr;
                    im_[b] = im_[a] - ti;
                    re_[a] += tr;
                    im_[a] += ti;
                }
        }
        const float scale = 4.0f / static_cast<float> (n_);
        for (int i = 0; i <= n_ / 2; ++i)
            out[i] = std::sqrt (re_[static_cast<size_t> (i)] * re_[static_cast<size_t> (i)] + im_[static_cast<size_t> (i)] * im_[static_cast<size_t> (i)]) * scale;
    }

private:
    int n_;
    std::vector<float> window_, cos_, sin_;
    std::vector<int> rev_;
    std::vector<float> re_, im_;
};
} // namespace synthyboy_mpc
