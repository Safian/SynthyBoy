// SynthyBoy - own engine: the built-in band-limited wavetables.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "wavetable_bank.h"

#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <mutex>

#include "../dsp/util.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace own
{
namespace
{
constexpr int kHarm = Wavetable::kCycle / 2; // 1024 harmonics

/** In place radix 2 FFT (forward when sign = -1, inverse unscaled when +1). Only used while a table is built. */
void fft (std::vector<std::complex<double>>& a, int sign)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = sign * kTwoPi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

using Spectrum = std::vector<float>; // index h = 0 .. kHarm (0 unused)

struct FramePair
{
    Spectrum c, s;
    FramePair() : c (kHarm + 1, 0.0f), s (kHarm + 1, 0.0f) {}
};

/** Spectrum of a time domain cycle (2048 points). */
FramePair spectrumOf (const std::vector<double>& x)
{
    std::vector<std::complex<double>> a (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        a[i] = x[i];
    fft (a, -1);
    FramePair f;
    for (int h = 1; h < kHarm; ++h)
    {
        f.c[static_cast<size_t> (h)] = static_cast<float> (2.0 * a[static_cast<size_t> (h)].real() / Wavetable::kCycle);
        f.s[static_cast<size_t> (h)] = static_cast<float> (-2.0 * a[static_cast<size_t> (h)].imag() / Wavetable::kCycle);
    }
    return f;
}

constexpr double kPi = 3.14159265358979323846;

void addSaw (FramePair& f, float w, int maxH = kHarm)
{
    for (int h = 1; h <= maxH && h < kHarm; ++h)
        f.s[static_cast<size_t> (h)] += w * static_cast<float> (-2.0 / (kPi * h)); // rising ramp, -1 at the start of the cycle, jump at the start
}

void addSquare (FramePair& f, float w)
{
    for (int h = 1; h < kHarm; h += 2)
        f.s[static_cast<size_t> (h)] += w * static_cast<float> (4.0 / (kPi * h));
}

void addPulse (FramePair& f, double duty)
{
    // +1 for the first `duty` of the cycle, -1 for the rest, without the DC offset
    for (int h = 1; h < kHarm; ++h)
    {
        const double a = 2.0 * kPi * h * duty;
        f.c[static_cast<size_t> (h)] += static_cast<float> (2.0 / (kPi * h) * std::sin (a));
        f.s[static_cast<size_t> (h)] += static_cast<float> (2.0 / (kPi * h) * (1.0 - std::cos (a)));
    }
}

std::vector<FramePair> makeFrames (int choice)
{
    std::vector<FramePair> out;
    constexpr int kMorph = 16;
    switch (choice)
    {
        case 0: // Sine
        {
            FramePair f;
            f.s[1] = 1.0f;
            out.push_back (f);
            break;
        }
        case 1: // Saw
        {
            FramePair f;
            addSaw (f, 1.0f);
            out.push_back (f);
            break;
        }
        case 2: // Square
        {
            FramePair f;
            addSquare (f, 1.0f);
            out.push_back (f);
            break;
        }
        case 3: // Triangle
        {
            FramePair f;
            for (int h = 1; h < kHarm; h += 2)
                f.s[static_cast<size_t> (h)] = static_cast<float> (((h / 2) % 2 == 0 ? 1.0 : -1.0) * 8.0 / (kPi * kPi * h * h));
            out.push_back (f);
            break;
        }
        case 4: // Pulse 25%
        {
            FramePair f;
            addPulse (f, 0.25);
            out.push_back (f);
            break;
        }
        case 5: // Saw to Square
        case 6: // Sine to Saw
            for (int k = 0; k < kMorph; ++k)
            {
                const float m = static_cast<float> (k) / (kMorph - 1);
                FramePair f;
                if (choice == 5)
                {
                    addSaw (f, 1.0f - m);
                    addSquare (f, m);
                }
                else
                {
                    f.s[1] = -(1.0f - m) * static_cast<float> (2.0 / kPi); // the sine (in phase with the fundamental of the saw) fades out while the saw comes in
                    addSaw (f, m);
                }
                out.push_back (f);
            }
            break;
        case 7: // Harmonic Series: the first n harmonics at 1/h
        case 8: // Odd Harmonics
            for (int k = 0; k < kMorph; ++k)
            {
                const int count = static_cast<int> (std::lround (std::pow (2.0, k * 8.0 / (kMorph - 1)))); // 1 .. 256
                FramePair f;
                for (int h = 1, n = 0; h < kHarm && n < count; h += (choice == 8 ? 2 : 1), ++n)
                    f.s[static_cast<size_t> (h)] = 1.0f / static_cast<float> (h);
                out.push_back (f);
            }
            break;
        case 9: // Formant Sweep: a resonant bump moving up the harmonics
            for (int k = 0; k < kMorph; ++k)
            {
                const double centre = 2.0 + 38.0 * k / (kMorph - 1);
                FramePair f;
                for (int h = 1; h < kHarm; ++h)
                {
                    const double d = (h - centre) / (1.0 + 0.25 * centre);
                    f.s[static_cast<size_t> (h)] = static_cast<float> (std::exp (-d * d) / std::sqrt (static_cast<double> (h)));
                }
                out.push_back (f);
            }
            break;
        case 10: // FM Sweep: sin (t + index * sin (t)), the index rises over the frames
            for (int k = 0; k < kMorph; ++k)
            {
                const double index = 8.0 * k / (kMorph - 1);
                std::vector<double> x (Wavetable::kCycle);
                for (int i = 0; i < Wavetable::kCycle; ++i)
                {
                    const double t = kTwoPi * i / Wavetable::kCycle;
                    x[static_cast<size_t> (i)] = std::sin (t + index * std::sin (t));
                }
                out.push_back (spectrumOf (x));
            }
            break;
        case 11: // PWM Sweep: pulse width from 50% to 5%
            for (int k = 0; k < kMorph; ++k)
            {
                FramePair f;
                addPulse (f, 0.5 - 0.45 * k / (kMorph - 1));
                out.push_back (f);
            }
            break;
        case 12: // Bell Partials: Fibonacci harmonics, the brightness grows over the frames
            for (int k = 0; k < kMorph; ++k)
            {
                FramePair f;
                const double decay = 2.0 + 6.0 * k / (kMorph - 1);
                int a = 1, b = 2, idx = 0;
                while (a < kHarm)
                {
                    f.s[static_cast<size_t> (a)] += static_cast<float> (std::exp (-idx / decay));
                    const int c = a + b;
                    a = b;
                    b = c;
                    ++idx;
                }
                out.push_back (f);
            }
            break;
        case 15: // internal: Rounded Rect (the sub oscillator), a square with softened edges
        {
            std::vector<double> x (Wavetable::kCycle);
            for (int i = 0; i < Wavetable::kCycle; ++i)
                x[static_cast<size_t> (i)] = std::tanh (3.0 * std::sin (kTwoPi * i / Wavetable::kCycle));
            out.push_back (spectrumOf (x));
            break;
        }
        case 14: // Saw Down: -saw (t + 180 degrees), the jump sits at mid cycle
        {
            FramePair f;
            for (int h = 1; h < kHarm; ++h)
                f.s[static_cast<size_t> (h)] = static_cast<float> (2.0 / (kPi * h) * ((h & 1) ? -1.0 : 1.0));
            out.push_back (f);
            break;
        }
        default:
            break;
    }
    return out;
}
} // namespace

int Wavetable::maxHarmonics (int level) noexcept
{
    return std::max (1, static_cast<int> (std::floor (kHarm * std::pow (2.0, -0.5 * level))));
}

int Wavetable::levelFor (float nyquistOverFundamental) noexcept
{
    if (nyquistOverFundamental >= static_cast<float> (kHarm))
        return 0;
    for (int m = 1; m < kNumLevels; ++m)
        if (static_cast<float> (maxHarmonics (m)) <= nyquistOverFundamental)
            return m;
    return kNumLevels - 1;
}

void Wavetable::build (const std::vector<std::vector<float>>& cosCoeff, const std::vector<std::vector<float>>& sinCoeff)
{
    frames = static_cast<int> (cosCoeff.size());
    data.assign (static_cast<size_t> (frames) * kNumLevels * kStride, 0.0f);
    std::vector<std::complex<double>> a (kCycle);
    for (int f = 0; f < frames; ++f)
    {
        // normalise the full band cycle to a peak of 1
        double scale = 1.0;
        for (int m = 0; m < kNumLevels; ++m)
        {
            const int top = maxHarmonics (m);
            std::fill (a.begin(), a.end(), std::complex<double> (0.0, 0.0));
            for (int h = 1; h <= top && h < kHarm; ++h)
            {
                // x[i] = sum c cos (h t) + s sin (h t); inverse FFT bin = (c - i s) / 2 and its mirror
                const std::complex<double> v (0.5 * cosCoeff[static_cast<size_t> (f)][static_cast<size_t> (h)],
                                              -0.5 * sinCoeff[static_cast<size_t> (f)][static_cast<size_t> (h)]);
                a[static_cast<size_t> (h)] = v;
                a[static_cast<size_t> (kCycle - h)] = std::conj (v);
            }
            fft (a, +1);
            if (m == 0)
            {
                double peak = 1.0e-9;
                for (int i = 0; i < kCycle; ++i)
                    peak = std::max (peak, std::fabs (a[static_cast<size_t> (i)].real()));
                scale = 1.0 / peak;
            }
            float* dst = data.data() + (static_cast<size_t> (f) * kNumLevels + static_cast<size_t> (m)) * kStride + 1;
            for (int i = 0; i < kCycle; ++i)
                dst[i] = static_cast<float> (a[static_cast<size_t> (i)].real() * scale);
            dst[-1] = dst[kCycle - 1];
            dst[kCycle] = dst[0];
            dst[kCycle + 1] = dst[1];
            dst[kCycle + 2] = dst[2];
        }
    }
}

namespace
{
struct Bank
{
    std::array<std::unique_ptr<Wavetable>, kNumWavetableChoices + 1> tables;

    Bank()
    {
        for (int c = 0; c <= kNumWavetableChoices; ++c)
        {
            if (c == kWavetablePresetTable)
                continue;
            const auto frames = makeFrames (c);
            std::vector<std::vector<float>> cc, ss;
            for (const auto& fr : frames)
            {
                cc.push_back (fr.c);
                ss.push_back (fr.s);
            }
            tables[static_cast<size_t> (c)] = std::make_unique<Wavetable>();
            tables[static_cast<size_t> (c)]->build (cc, ss);
        }
    }
};
} // namespace

const Wavetable* builtinWavetable (int choice) noexcept
{
    static std::once_flag once;
    static std::unique_ptr<Bank> bank;
    std::call_once (once, [] { bank = std::make_unique<Bank>(); });
    if (choice < 0 || choice > kNumWavetableChoices)
        return nullptr;
    return bank->tables[static_cast<size_t> (choice)].get();
}
} // namespace own
} // namespace synthyboy
