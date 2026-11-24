// SynthyBoy - core/fx-serum-b: real input FFT on top of Radix2FFT (docs/plans/reverb-convolve.md section 3.4.3).
// A real transform of n points is a complex transform of n/2 points (the even samples as the real part, the odd samples as the imaginary part) plus one untangling pass; the output is the
// n/2 + 1 independent bins. About 2.2 times fewer operations than the complex transform of the zero padded input.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <vector>

#include "fft.h"

namespace synthyboy::fx
{
/** Not thread safe: forward () and inverse () use the scratch of the object. One instance per user. */
class RealFft
{
public:
    /** n: a power of two, 8 .. 32768. (non-RT: builds the tables) */
    void init (int n)
    {
        m_n = n;
        const int h = n / 2;
        m_fft.init (h);
        m_wr.assign (static_cast<size_t> (h + 1), 0.0f);
        m_wi.assign (static_cast<size_t> (h + 1), 0.0f);
        for (int k = 0; k < h; ++k)
        {
            const double angle = 2.0 * 3.14159265358979323846 * static_cast<double> (k) / static_cast<double> (n);
            m_wr[static_cast<size_t> (k)] = static_cast<float> (std::cos (angle)); // W^k = exp (-i angle) = wr + i wi
            m_wi[static_cast<size_t> (k)] = static_cast<float> (-std::sin (angle));
        }
        m_wr[static_cast<size_t> (h)] = -1.0f; // W^(n/2) = -1
        m_wi[static_cast<size_t> (h)] = 0.0f;
        m_z.assign (static_cast<size_t> (h), Complex());
    }

    int size() const noexcept { return m_n; }
    int bins() const noexcept { return m_n / 2 + 1; }

    /** The spectrum (bins() values each of re and im) of in[0 .. count), zero padded to n. (RT safe) */
    void forward (const float* in, int count, float* re, float* im) const noexcept
    {
        const int h = m_n / 2;
        Complex* z = m_z.data();
        for (int m = 0; m < h; ++m)
        {
            const int a = 2 * m, b = 2 * m + 1;
            z[m] = Complex (a < count ? in[a] : 0.0f, b < count ? in[b] : 0.0f);
        }
        m_fft.fft (z);
        for (int k = 0; k <= h; ++k)
        {
            const Complex zk = z[k == h ? 0 : k];
            const Complex zc = z[h - k == h ? 0 : h - k].conj(); // conj Z[n/2 - k]
            const Complex a = zk + zc, b = zk - zc;
            const float tr = m_wr[static_cast<size_t> (k)] * b.r - m_wi[static_cast<size_t> (k)] * b.i; // t = W^k * b
            const float ti = m_wr[static_cast<size_t> (k)] * b.i + m_wi[static_cast<size_t> (k)] * b.r;
            re[k] = 0.5f * (a.r + ti);
            im[k] = 0.5f * (a.i - tr);
        }
    }

    /** The n samples of the spectrum (bins() values each of re and im), scaled so that inverse (forward (x)) = x. (RT safe) */
    void inverse (const float* re, const float* im, float* out) const noexcept
    {
        const int h = m_n / 2;
        Complex* z = m_z.data();
        for (int k = 0; k < h; ++k)
        {
            const Complex xk (re[k], im[k]);
            const Complex xc (re[h - k], -im[h - k]); // conj X[n/2 - k]
            const Complex e = (xk + xc) * 0.5f;       // the spectrum of the even samples
            const Complex d = (xk - xc) * 0.5f;       // W^k times the spectrum of the odd samples
            const float wr = m_wr[static_cast<size_t> (k)], wi = m_wi[static_cast<size_t> (k)];
            const Complex o (wr * d.r + wi * d.i, wr * d.i - wi * d.r); // conj (W^k) * d
            z[k] = Complex (e.r - o.i, e.i + o.r);                      // e + i o
        }
        m_fft.ifft (z);
        for (int m = 0; m < h; ++m)
        {
            out[2 * m] = z[m].r;
            out[2 * m + 1] = z[m].i;
        }
    }

private:
    int m_n = 0;
    Radix2FFT m_fft;
    std::vector<float> m_wr, m_wi;
    mutable std::vector<Complex> m_z;
};
} // namespace synthyboy::fx
