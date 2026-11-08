// SynthyBoy - core/fx-serum-b: In-place radix-2 Cooley-Tukey FFT.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace synthyboy::fx
{
struct Complex
{
    float r = 0.0f;
    float i = 0.0f;

    constexpr Complex() noexcept = default;
    constexpr Complex (float real, float imag) noexcept : r (real), i (imag) {}

    constexpr Complex operator+ (const Complex& o) const noexcept { return { r + o.r, i + o.i }; }
    constexpr Complex operator- (const Complex& o) const noexcept { return { r - o.r, i - o.i }; }
    constexpr Complex operator* (const Complex& o) const noexcept
    {
        return { r * o.r - i * o.i, r * o.i + i * o.r };
    }
    constexpr Complex operator* (float s) const noexcept { return { r * s, i * s }; }
    constexpr Complex conj() const noexcept { return { r, -i }; }
    float norm() const noexcept { return r * r + i * i; }
    float abs() const noexcept { return std::sqrt (norm()); }
};

/** Standalone in-place radix-2 FFT and IFFT for real-time partitioned convolution.
    Size must be a power of 2. Pre-computes bit-reversal and twiddle tables during init().
    No allocations or locks occur during fft() and ifft(). */
class Radix2FFT
{
public:
    Radix2FFT() = default;
    explicit Radix2FFT (int size);

    /** Pre-allocates and initializes tables for the given power-of-2 size. (non-RT) */
    void init (int size);

    int size() const noexcept { return m_size; }

    /** In-place forward complex FFT. Buffer must contain size() Complex entries. (RT safe) */
    void fft (Complex* buffer) const noexcept;

    /** In-place inverse complex FFT. Buffer must contain size() Complex entries. (RT safe) */
    void ifft (Complex* buffer) const noexcept;

    /** Convenience forward FFT for real input (zero-padded to size()). (RT safe) */
    void forwardReal (const float* realIn, int realCount, Complex* complexOut) const noexcept;

    /** Convenience inverse FFT writing real parts into realOut. (RT safe) */
    void inverseReal (Complex* complexInOut, float* realOut) const noexcept;

private:
    int m_size = 0;
    std::vector<int> m_bitRev;
    std::vector<Complex> m_twiddles;
};
} // namespace synthyboy::fx
