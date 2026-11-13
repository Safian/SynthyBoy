// SynthyBoy - own engine: the built-in band-limited wavetables.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <vector>
#include "wavetable_bank.h"

#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <memory>
#include <mutex>
#include <thread>

#include "../dsp/util.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace own
{
namespace
{
constexpr int kHarm = Wavetable::kCycle / 2; // 1024 harmonics

/** The bit reversal and the twiddle factors of an FFT of one size and direction. The factors of a stage are the sequence w = 1, w *= wl, ... that the
    butterfly loop used to compute for every block of the stage again: here it is computed once with the same recurrence, so every value (and the output)
    is bit for bit the one of the plain loop. */
struct FftPlan
{
    std::vector<uint32_t> swapA, swapB;                 // the pairs (i, j) with i < j of the bit reversal
    std::vector<std::complex<double>> twiddle;          // stage by stage: len / 2 factors of len = 2, 4, ..., n
};

FftPlan makePlan (size_t n, int sign)
{
    FftPlan plan;
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            plan.swapA.push_back (static_cast<uint32_t> (i));
            plan.swapB.push_back (static_cast<uint32_t> (j));
        }
    }
    plan.twiddle.reserve (n);
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = sign * kTwoPi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        std::complex<double> w (1.0, 0.0);
        for (size_t k = 0; k < len / 2; ++k)
        {
            plan.twiddle.push_back (w);
            w *= wl;
        }
    }
    return plan;
}

/** In place radix 2 FFT (forward when sign = -1, inverse unscaled when +1). Only used while a table is built; n is a power of two <= 2^15. */
void fft (std::vector<std::complex<double>>& a, int sign)
{
    const size_t n = a.size();
    thread_local std::unique_ptr<FftPlan> plans[2][16];
    int lg = 0;
    while ((static_cast<size_t> (1) << lg) < n)
        ++lg;
    std::unique_ptr<FftPlan>& slot = plans[sign < 0 ? 0 : 1][lg];
    if (!slot)
        slot = std::make_unique<FftPlan> (makePlan (n, sign));
    const FftPlan& plan = *slot;
    for (size_t p = 0; p < plan.swapA.size(); ++p)
        std::swap (a[plan.swapA[p]], a[plan.swapB[p]]);
    const std::complex<double>* tw = plan.twiddle.data();
    for (size_t len = 2; len <= n; len <<= 1)
    {
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * tw[k];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        tw += len / 2;
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
    // the recipes of docs/engine-spec/02-oscillator.md section 1 (verified there against the reference frames): 64 frames, t = f / 63, harmonics 1 .. 1023
    std::vector<FramePair> out;
    constexpr int kMorph = 64;
    auto sineB = [] (FramePair& f, int k, double v) { f.s[static_cast<size_t> (k)] += static_cast<float> (v); };
    switch (choice)
    {
        case 0: { FramePair f; sineB (f, 1, 1.0); out.push_back (f); break; }
        case 1: { FramePair f; for (int k = 1; k < kHarm; ++k) sineB (f, k, -2.0 / (kPi * k)); out.push_back (f); break; }
        case 2: { FramePair f; for (int k = 1; k < kHarm; k += 2) sineB (f, k, 4.0 / (kPi * k)); out.push_back (f); break; }
        case 3: { FramePair f; for (int k = 1; k < kHarm; k += 2) sineB (f, k, 8.0 / (kPi * kPi * k * k) * (((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0)); out.push_back (f); break; }
        case 4: { FramePair f; addPulse (f, 0.25); out.push_back (f); break; }
        case 14: { FramePair f; for (int k = 1; k < kHarm; ++k) sineB (f, k, 2.0 * ((k & 1) ? -1.0 : 1.0) / (kPi * k)); out.push_back (f); break; }
        case 15: // internal: Rounded Rect (the sub oscillator), a square with softened edges
        {
            std::vector<double> x (Wavetable::kCycle);
            for (int i = 0; i < Wavetable::kCycle; ++i)
                x[static_cast<size_t> (i)] = std::tanh (3.0 * std::sin (kTwoPi * i / Wavetable::kCycle));
            out.push_back (spectrumOf (x));
            break;
        }
        case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12:
            for (int fr = 0; fr < kMorph; ++fr)
            {
                const double t = static_cast<double> (fr) / (kMorph - 1);
                FramePair f;
                switch (choice)
                {
                    case 5: for (int k = 1; k < kHarm; ++k) sineB (f, k, 2.0 / (kPi * k) * ((k & 1) ? 1.0 + t : 1.0 - t)); break;
                    case 6: for (int k = 1; k < kHarm; ++k) sineB (f, k, k == 1 ? (1.0 - t) + t * 2.0 / kPi : t * 2.0 / (kPi * k)); break;
                    case 7: case 8:
                    {
                        const double count = 1.0 + 63.0 * t;
                        for (int k = 1; k <= 64; ++k)
                            if (choice == 7 || (k & 1))
                                sineB (f, k, std::clamp (count - k + 1.0, 0.0, 1.0) / k);
                        break;
                    }
                    case 9:
                    {
                        const double c1 = 2.0 + 10.0 * t, c2 = 6.0 + 26.0 * t;
                        for (int k = 1; k < kHarm; ++k)
                        {
                            const double g1 = std::exp (-std::pow ((k - c1) / (1.0 + 0.4 * c1), 2.0)), g2 = 0.6 * std::exp (-std::pow ((k - c2) / (1.0 + 0.4 * c2), 2.0));
                            sineB (f, k, (g1 + g2) / std::sqrt (static_cast<double> (k)) + (k == 1 ? 0.15 : 0.0));
                        }
                        break;
                    }
                    case 10:
                    {
                        std::vector<double> x (Wavetable::kCycle);
                        for (int i = 0; i < Wavetable::kCycle; ++i)
                        {
                            const double ph = 2.0 * kPi * i / Wavetable::kCycle;
                            x[static_cast<size_t> (i)] = std::sin (ph + 6.0 * t * std::sin (2.0 * ph));
                        }
                        f = spectrumOf (x);
                        break;
                    }
                    case 11: addPulse (f, 0.05 + 0.45 * t); break;
                    default:
                    {
                        static const int kPartials[] = { 1, 2, 3, 4, 6, 8, 11, 15, 19, 26, 34 };
                        for (const int k : kPartials)
                        {
                            const double w = std::exp (-k / (4.0 + 40.0 * t)) + 0.3 * std::exp (-std::pow ((k - 3.0 - 20.0 * t) / 4.0, 2.0));
                            f.s[static_cast<size_t> (k)] += static_cast<float> (w * (k % 3 == 0 ? 0.8 : 1.0));
                            f.c[static_cast<size_t> (k)] += static_cast<float> (0.4 * w * ((k & 1) ? -1.0 : 1.0));
                        }
                        break;
                    }
                }
                out.push_back (f);
            }
            break;
        default:
            break;
    }
    return out;
}
} // namespace

namespace
{
struct LevelTable
{
    int harmonics[Wavetable::kNumLevels], size[Wavetable::kNumLevels];
    LevelTable()
    {
        for (int m = 0; m < Wavetable::kNumLevels; ++m)
        {
            static const int kHarmonics[Wavetable::kNumLevels] = { 1024, 512, 256, 181, 128, 91, 64, 45, 32, 23, 16, 11, 8, 6, 4, 3, 2, 1 };
            harmonics[m] = kHarmonics[m];
            int n = 256; // never fewer than 256 samples: at the high pitches the interpolation images of a tiny table are what is left to hear
            while (n < 8 * harmonics[m] && n < Wavetable::kCycle)
                n <<= 1;
            size[m] = m == 0 ? Wavetable::kCycle : n;
        }
    }
};
const LevelTable& levels()
{
    static const LevelTable t;
    return t;
}
} // namespace

int Wavetable::maxHarmonics (int level) noexcept
{
    return levels().harmonics[std::clamp (level, 0, kNumLevels - 1)];
}

int Wavetable::size (int level) noexcept
{
    return levels().size[std::clamp (level, 0, kNumLevels - 1)];
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

void Wavetable::build (const std::vector<std::vector<float>>& cosCoeff, const std::vector<std::vector<float>>& sinCoeff, bool normalizePeak)
{
    frames = static_cast<int> (cosCoeff.size());
    size_t off = 0;
    for (int m = 0; m < kNumLevels; ++m)
    {
        levelOffset[m] = off;
        off += static_cast<size_t> (size (m)) + 4;
    }
    frameStride = off;
    data.assign (static_cast<size_t> (frames) * frameStride, 0.0f);
    spec.assign (static_cast<size_t> (frames) * 2 * kHarmonics, 0.0f);
    std::vector<std::complex<double>> a;
    for (int f = 0; f < frames; ++f)
    {
        double scale = 1.0;
        for (int m = 0; m < kNumLevels; ++m)
        {
            const int n = size (m);
            const int top = maxHarmonics (m);
            a.assign (static_cast<size_t> (n), std::complex<double> (0.0, 0.0));
            a[0] = cosCoeff[static_cast<size_t> (f)][0]; // the mean of the frame (user tables keep it; the built-in tables have none)
            for (int h = 1; h <= top && h < kHarm && h < n / 2; ++h)
            {
                // x[i] = sum c cos (h t) + s sin (h t); inverse FFT bin = (c - i s) / 2 and its mirror
                // the top fifth of every mip level (not level 0) is tapered with a raised cosine: a brickwall at the edge of the band rings (Gibbs, 9 % overshoot) and
                // the ringing is what makes a baked table sound louder in its peaks than the live signal it was rendered from
                double taper = 1.0;
                if (m > 0 && top > 4 && h > top * 4 / 5)
                    taper = 0.5 + 0.5 * std::cos (kPi * static_cast<double> (h - top * 85 / 100) / static_cast<double> (top - top * 85 / 100 + 1));
                const std::complex<double> v (0.5 * taper * cosCoeff[static_cast<size_t> (f)][static_cast<size_t> (h)],
                                              -0.5 * taper * sinCoeff[static_cast<size_t> (f)][static_cast<size_t> (h)]);
                a[static_cast<size_t> (h)] = v;
                a[static_cast<size_t> (n - h)] = std::conj (v);
            }
            fft (a, +1);
            if (m == 0 && normalizePeak)
            {
                double peak = 1.0e-9;
                for (int i = 0; i < n; ++i)
                    peak = std::max (peak, std::fabs (a[static_cast<size_t> (i)].real()));
                scale = 1.0 / peak;
            }
            float* dst = data.data() + static_cast<size_t> (f) * frameStride + levelOffset[m] + 1;
            for (int i = 0; i < n; ++i)
                dst[i] = static_cast<float> (a[static_cast<size_t> (i)].real() * scale);
            dst[-1] = dst[n - 1];
            dst[n] = dst[0];
            dst[n + 1] = dst[1];
            dst[n + 2] = dst[2];
        }
        float* sp = spec.data() + static_cast<size_t> (f) * 2 * kHarmonics;
        sp[0] = static_cast<float> (cosCoeff[static_cast<size_t> (f)][0] * scale);
        for (int h = 1; h < kHarm; ++h)
        {
            sp[h] = static_cast<float> (cosCoeff[static_cast<size_t> (f)][static_cast<size_t> (h)] * scale);
            sp[kHarmonics + h] = static_cast<float> (sinCoeff[static_cast<size_t> (f)][static_cast<size_t> (h)] * scale);
        }
    }
}

std::unique_ptr<Wavetable> makeWavetableFromSamples (const float* samples, int numFrames)
{
    if (samples == nullptr || numFrames < 1)
        return nullptr;
    std::vector<std::vector<float>> cc, ss;
    std::vector<double> x (static_cast<size_t> (Wavetable::kCycle));
    for (int f = 0; f < numFrames; ++f)
    {
        double mean = 0.0;
        for (int i = 0; i < Wavetable::kCycle; ++i)
            mean += samples[static_cast<size_t> (f) * Wavetable::kCycle + static_cast<size_t> (i)];
        mean /= Wavetable::kCycle;
        for (int i = 0; i < Wavetable::kCycle; ++i)
            x[static_cast<size_t> (i)] = samples[static_cast<size_t> (f) * Wavetable::kCycle + static_cast<size_t> (i)] - mean;
        FramePair fp = spectrumOf (x);
        fp.c[0] = static_cast<float> (mean); // a user table keeps its DC (the mean of the frame)
        cc.push_back (fp.c);
        ss.push_back (fp.s);
    }
    auto t = std::make_unique<Wavetable>();
    t->build (cc, ss, false);
    return t;
}

namespace
{
struct Bank
{
    std::array<std::unique_ptr<Wavetable>, kNumWavetableChoices + 1> tables;

    static void buildOne (Bank& bank, int c)
    {
        const auto frames = makeFrames (c);
        std::vector<std::vector<float>> cc, ss;
        for (const auto& fr : frames)
        {
            cc.push_back (fr.c);
            ss.push_back (fr.s);
        }
        bank.tables[static_cast<size_t> (c)] = std::make_unique<Wavetable>();
        bank.tables[static_cast<size_t> (c)]->build (cc, ss);
    }

    /** The tables are independent of each other: they are built on a few helper threads (joined before the constructor returns; prepare () is not
        the audio thread). Every table is computed by the same code on its own data, so the result does not depend on the number of threads. If a
        thread cannot be created the calling thread does the rest. */
    Bank()
    {
        std::atomic<int> next { 0 };
        auto work = [this, &next]
        {
            for (int c = next.fetch_add (1); c <= kNumWavetableChoices; c = next.fetch_add (1))
                if (c != kWavetablePresetTable)
                    buildOne (*this, c);
        };
        const int helpers = static_cast<int> (std::min (3u, std::max (1u, std::thread::hardware_concurrency()) - 1u));
        std::vector<std::thread> threads;
        for (int i = 0; i < helpers; ++i)
        {
            try
            {
                threads.emplace_back (work);
            }
            catch (...)
            {
                break;
            }
        }
        work();
        for (auto& t : threads)
            t.join();
    }
};
} // namespace

size_t builtinBankBytes() noexcept
{
    size_t total = 0;
    for (int c = 0; c <= kNumWavetableChoices; ++c)
        if (const Wavetable* t = builtinWavetable (c))
            total += t->memoryBytes();
    return total;
}

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
