// SynthyBoy - engine-vital tests: the Vital engine adapter (init patch, parameter mapping, "feature golden" measurements).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The golden test measures properties of the sound (fundamental, level, harmonic ratios, spectral centroid), never sample
// values: the SSE and NEON paths of the DSP differ in the last bits.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "synthyboy/engine.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"
#include "wave_frame.h" // Vital-derived

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

/** Amplitude of the sinusoid at freq in x[from, from + n) (single bin DFT with a Hann window). */
double amplitudeAt (const std::vector<float>& x, size_t from, size_t n, double freq)
{
    double re = 0.0, im = 0.0, windowSum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n));
        const double phase = 2.0 * kPi * freq * static_cast<double> (i) / kSampleRate;
        re += w * static_cast<double> (x[from + i]) * std::cos (phase);
        im -= w * static_cast<double> (x[from + i]) * std::sin (phase);
        windowSum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / windowSum;
}

/** Frequency of the strongest component within +-2% of guess. */
double fundamental (const std::vector<float>& x, size_t from, size_t n, double guess)
{
    double best = 0.0, bestAmp = -1.0;
    for (double f = guess * 0.98; f <= guess * 1.02; f += guess * 0.0001)
    {
        const double a = amplitudeAt (x, from, n, f);
        if (a > bestAmp)
        {
            bestAmp = a;
            best = f;
        }
    }
    return best;
}

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double> (x[i]) * static_cast<double> (x[i]);
    return std::sqrt (sum / static_cast<double> (to - from));
}

/** Spectral centroid over the harmonics of f0 up to 20 kHz. */
double harmonicCentroid (const std::vector<float>& x, size_t from, size_t n, double f0)
{
    double weighted = 0.0, total = 0.0;
    for (int k = 1; k * f0 < 20000.0; ++k)
    {
        const double a = amplitudeAt (x, from, n, k * f0);
        weighted += k * f0 * a;
        total += a;
    }
    return total > 0.0 ? weighted / total : 0.0;
}

struct Held
{
    std::vector<float> left, right;
};

/** One held note for one second at 48 kHz in blocks of 256 with the given parameters (set before prepare()). */
Held renderHeld (int key, float waveform, float cutoff, float gain = -12.0f, float velocity = 1.0f)
{
    auto e = createVitalEngine();
    e->setParam (kParamOsc1Wavetable, waveform);
    e->setParam (kParamFilter1Cutoff, cutoff);
    e->setParam (kParamMasterVolume, gain);
    e->prepare (kSampleRate, 256, Quality {});
    const auto out = sbtest::render (*e, { noteOn (0, key, velocity) }, 48000, 256);
    return { out.left, out.right };
}
} // namespace

TEST (vital_engine_is_configured)
{
    auto e = createVitalEngine();
    CHECK (! isVitalEngineConfigured (*e)); // not prepared yet
    e->prepare (kSampleRate, 256, Quality {});
    CHECK (isVitalEngineConfigured (*e));   // every control name the adapter uses exists in Vital's table
    auto own = createOwnEngine();
    own->prepare (kSampleRate, 256, Quality {});
    CHECK (! isVitalEngineConfigured (*own));

    // prepare() can be repeated (new sample rate) and keeps working
    e->prepare (44100.0, 128, qualityForTier (Tier::low));
    CHECK (isVitalEngineConfigured (*e));
    const auto out = sbtest::render (*e, { noteOn (0, 69) }, 8820, 128);
    CHECK (sbtest::peak (out.left) > 0.05f && sbtest::allFinite (out.left));
}

TEST (vital_golden_saw_a4)
{
    // saw, A4, 8 kHz low-pass, -12 dB: the analysis window is the last half second (steady state)
    const auto h = renderHeld (69, 1.0f, 8000.0f);
    const size_t from = 24000, n = 24000;

    const double f0 = fundamental (h.left, from, n, 440.0);
    const double cents = 1200.0 * std::log2 (f0 / 440.0);
    std::printf ("    saw A4: fundamental %.3f Hz (%.3f cents), RMS %.4f\n", f0, cents, rms (h.left, from, from + n));
    CHECK (std::fabs (cents) < 1.0);

    const double level = rms (h.left, from, from + n);
    CHECK (level > 0.05 && level < 0.16);

    const double a1 = amplitudeAt (h.left, from, n, f0);
    const double a2 = amplitudeAt (h.left, from, n, 2 * f0);
    const double a3 = amplitudeAt (h.left, from, n, 3 * f0);
    const double a4 = amplitudeAt (h.left, from, n, 4 * f0);
    std::printf ("    saw A4: h2/h1 %.3f h3/h1 %.3f h4/h1 %.3f\n", a2 / a1, a3 / a1, a4 / a1);
    CHECK (a2 / a1 > 0.44 && a2 / a1 < 0.56); // ideal saw: 1/2
    CHECK (a3 / a1 > 0.29 && a3 / a1 < 0.37); // 1/3
    CHECK (a4 / a1 > 0.21 && a4 / a1 < 0.29); // 1/4

    CHECK (sbtest::allFinite (h.left) && sbtest::allFinite (h.right));
    CHECK (sbtest::peak (h.left) <= 1.0f);
}

TEST (vital_golden_sine_a4)
{
    const auto h = renderHeld (69, 0.0f, 8000.0f);
    const size_t from = 24000, n = 24000;
    const double f0 = fundamental (h.left, from, n, 440.0);
    CHECK (std::fabs (1200.0 * std::log2 (f0 / 440.0)) < 1.0);

    const double a1 = amplitudeAt (h.left, from, n, f0);
    const double a2 = amplitudeAt (h.left, from, n, 2 * f0);
    const double a3 = amplitudeAt (h.left, from, n, 3 * f0);
    std::printf ("    sine A4: amplitude %.4f, h2/h1 %.5f, h3/h1 %.5f, RMS %.4f\n", a1, a2 / a1, a3 / a1, rms (h.left, from, from + n));
    CHECK (a2 / a1 < 0.01 && a3 / a1 < 0.01); // below -40 dB
    // sine of amplitude A has RMS A / sqrt (2)
    CHECK_NEAR (rms (h.left, from, from + n), a1 / std::sqrt (2.0), 0.01 * a1);
    CHECK (a1 > 0.07 && a1 < 0.25);
}

TEST (vital_golden_cutoff_darkens_the_saw)
{
    const size_t from = 24000, n = 24000;
    const auto dark = renderHeld (69, 1.0f, 500.0f);
    const auto bright = renderHeld (69, 1.0f, 8000.0f);
    const double darkCentroid = harmonicCentroid (dark.left, from, n, 440.0);
    const double brightCentroid = harmonicCentroid (bright.left, from, n, 440.0);
    std::printf ("    saw A4: spectral centroid %.0f Hz at cutoff 500 Hz, %.0f Hz at cutoff 8000 Hz\n", darkCentroid, brightCentroid);

    CHECK (darkCentroid < 1000.0);                // the 500 Hz filter keeps the energy near the cutoff
    CHECK (darkCentroid < 0.5 * brightCentroid);  // and the 8 kHz setting is clearly brighter
    CHECK (brightCentroid > 1500.0);
    CHECK (rms (dark.left, from, from + n) < rms (bright.left, from, from + n));
}

TEST (vital_golden_gain_and_velocity)
{
    const size_t from = 24000;
    const double base = rms (renderHeld (69, 0.0f, 8000.0f, -12.0f, 1.0f).left, from, 48000);
    const double quiet = rms (renderHeld (69, 0.0f, 8000.0f, -24.0f, 1.0f).left, from, 48000);
    const double soft = rms (renderHeld (69, 0.0f, 8000.0f, -12.0f, 0.5f).left, from, 48000);
    std::printf ("    sine: -12 dB %.4f, -24 dB %.4f (ratio %.3f), velocity 0.5 %.4f (ratio %.3f)\n", base, quiet, quiet / base, soft, soft / base);
    CHECK_NEAR (quiet / base, std::pow (10.0, -12.0 / 20.0), 0.02 * std::pow (10.0, -12.0 / 20.0)); // gain is in dB
    CHECK_NEAR (soft / base, 0.25, 0.01);                                                          // Vital's velocity curve is quadratic
}

TEST (vital_golden_pitch_across_the_keyboard)
{
    // sine at C1..C7 and the three reference keys, 1 cent
    for (const int key : { 24, 36, 48, 57, 60, 69, 72, 81, 84, 96 })
    {
        const double expected = 440.0 * std::pow (2.0, (key - 69) / 12.0);
        const auto h = renderHeld (key, 0.0f, 20000.0f);
        const double f0 = fundamental (h.left, 24000, 24000, expected);
        const double cents = 1200.0 * std::log2 (f0 / expected);
        if (! CHECK (std::fabs (cents) < 1.0))
            std::printf ("    key %d: %.3f Hz expected %.3f Hz (%.3f cents)\n", key, f0, expected, cents);
    }
}

TEST (vital_unison_and_effects_are_off_in_the_init_patch)
{
    // one sounding voice is one oscillator voice: a sine has a single spectral line
    const auto h = renderHeld (60, 0.0f, 20000.0f);
    const double f0 = 261.6255653;
    const double main = amplitudeAt (h.left, 24000, 24000, f0);
    const double detuned = amplitudeAt (h.left, 24000, 24000, f0 * 1.1);
    CHECK (detuned < 0.01 * main);
    CHECK (std::fabs (static_cast<double> (sbtest::peak (h.left)) - static_cast<double> (sbtest::peak (h.right))) < 1e-6); // centred
    CHECK (h.left == h.right);
}

TEST (vital_fft_matches_a_direct_dft)
{
    // the kissfft based FourierTransform (the only FFT in this build) against the definition: bins 0 .. N/2 interleaved,
    // Nyquist at data[N], unnormalised forward; the inverse restores the signal including the Nyquist component
    sbtest::Rng rng (11);
    sbvital::WaveFrame frame;
    constexpr int n = sbvital::WaveFrame::kWaveformSize;
    std::vector<double> input (n);
    // noise plus a strong Nyquist component (the part an incorrect real transform loses)
    for (int i = 0; i < n; ++i)
    {
        input[static_cast<size_t> (i)] = rng.uniform() * 2.0 - 1.0 + (i % 2 == 0 ? 0.5 : -0.5);
        frame.time_domain[i] = static_cast<float> (input[static_cast<size_t> (i)]);
    }
    const std::vector<float> original (frame.time_domain, frame.time_domain + n);

    frame.toFrequencyDomain();
    double worst = 0.0;
    for (int k : { 0, 1, 2, 7, 100, 511, 1023, 1024 })
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            re += input[static_cast<size_t> (i)] * std::cos (2.0 * kPi * k * i / n);
            im -= input[static_cast<size_t> (i)] * std::sin (2.0 * kPi * k * i / n);
        }
        worst = std::max (worst, std::fabs (re - static_cast<double> (frame.frequency_domain[k].real())));
        worst = std::max (worst, std::fabs (im - static_cast<double> (frame.frequency_domain[k].imag())));
    }
    CHECK (worst < 0.05); // sums of 2048 values of size 1, float precision
    CHECK_EQ (frame.frequency_domain[0].imag(), 0.0f);
    CHECK_EQ (frame.frequency_domain[n / 2].imag(), 0.0f);
    CHECK (std::fabs (frame.frequency_domain[n / 2].real()) > 100.0f); // the Nyquist bin is there
    CHECK_EQ (frame.frequency_domain[n / 2 + 1], std::complex<float> (0.0f, 0.0f)); // nothing above it

    frame.toTimeDomain();
    float roundTrip = 0.0f;
    for (int i = 0; i < n; ++i)
        roundTrip = std::max (roundTrip, std::fabs (frame.time_domain[i] - original[static_cast<size_t> (i)]));
    CHECK (roundTrip < 1.0e-5f);
}
