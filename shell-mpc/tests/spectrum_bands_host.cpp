// SynthyBoy - host test of the band mapping of the live spectrum (shell-mpc/spectrum_bands.h) on known signals, with the FFT of shell-mpc/spectrum_fft.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
// Build: c++ -std=c++17 -O1 -Wall -Wextra -I shell-mpc -o spectrum_bands_host shell-mpc/tests/spectrum_bands_host.cpp && ./spectrum_bands_host
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "spectrum_bands.h"
#include "spectrum_fft.h"

using namespace synthyboy_mpc;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static const double kRate = 44100.0;

/** The 32 levels (0 .. 1, not quantised) of one block of 2048 samples. */
static void levels (const std::vector<float>& x, size_t offset, SpectrumFft& fft, float* out)
{
    std::vector<float> mag (static_cast<size_t> (fft.bins())), db (kSpectrumBands);
    fft.magnitudes (x.data() + offset, mag.data());
    spectrumBandDb (mag.data(), fft.bins(), kRate, db.data());
    for (int k = 0; k < kSpectrumBands; ++k)
        out[k] = spectrumLevelFromDb (db[static_cast<size_t> (k)]);
}

int main()
{
    // the edges: 30 Hz .. 18 kHz, 32 bands, equal ratio
    check (std::fabs (spectrumBandEdgeHz (0) - 30.0) < 1e-9 && std::fabs (spectrumBandEdgeHz (32) - 18000.0) < 1e-6, "edges: band 0 starts at 30 Hz, band 31 ends at 18 kHz");
    {
        bool ok = true;
        const double ratio = spectrumBandEdgeHz (1) / spectrumBandEdgeHz (0);
        for (int k = 0; k < kSpectrumBands; ++k)
            ok = ok && spectrumBandEdgeHz (k + 1) > spectrumBandEdgeHz (k) && std::fabs (spectrumBandEdgeHz (k + 1) / spectrumBandEdgeHz (k) - ratio) < 1e-9;
        check (ok, "edges: strictly rising, the same ratio from band to band (logarithmic)");
    }
    check (spectrumBandEdgeHz (-3) == spectrumBandEdgeHz (0) && spectrumBandEdgeHz (99) == spectrumBandEdgeHz (32), "edges: k outside 0 .. 32 is clamped");

    // dB -> level
    check (spectrumLevelFromDb (kSpectrumCeilingDb) == 1.0f && spectrumLevelFromDb (kSpectrumCeilingDb + 6.0f) == 1.0f, "level: ceiling and above = 1");
    check (spectrumLevelFromDb (kSpectrumFloorDb) == 0.0f && spectrumLevelFromDb (kSpectrumFloorDb - 30.0f) == 0.0f && spectrumLevelFromDb (-180.0f) == 0.0f, "level: floor and below = 0");
    const float midDb = 0.5f * (kSpectrumFloorDb + kSpectrumCeilingDb);
    const float quarterDb = 0.25f * kSpectrumFloorDb + 0.75f * kSpectrumCeilingDb;
    check (std::fabs (spectrumLevelFromDb (midDb) - 0.5f) < 1e-6f && std::fabs (spectrumLevelFromDb (quarterDb) - 0.75f) < 1e-6f, "level: linear in dB between the floor and the ceiling");
    check (spectrumLevelFromDb (std::nanf ("")) == 0.0f, "level: NaN = 0");
    check (std::fabs (spectrumMagToDb (1.0f)) < 1e-6f && std::fabs (spectrumMagToDb (0.5f) + 6.0206f) < 1e-3f && spectrumMagToDb (0.0f) == -180.0f && spectrumMagToDb (-1.0f) == -180.0f && spectrumMagToDb (std::nanf ("")) == -180.0f, "magnitude to dB (zero, negative and NaN give -180)");

    // quantiser: 1/64 steps
    check (spectrumQuantize (0.0f) == 0.0f && spectrumQuantize (1.0f) == 1.0f && spectrumQuantize (-0.3f) == 0.0f && spectrumQuantize (1.7f) == 1.0f, "quantise: 0, 1, clamped");
    check (spectrumQuantize (0.5f) == 0.5f && spectrumQuantize (1.0f / 64.0f) == 1.0f / 64.0f, "quantise: exact steps stay");
    check (spectrumQuantize (0.2f / 64.0f) == 0.0f && spectrumQuantize (0.6f / 64.0f) == 1.0f / 64.0f && spectrumQuantize (10.4f / 64.0f) == 10.0f / 64.0f, "quantise: rounds to the nearest step");
    check (spectrumQuantize (std::nanf ("")) == 0.0f, "quantise: NaN = 0");

    SpectrumFft fft;
    check (fft.size() == 2048 && fft.bins() == 1025, "fft: 2048 points, 1025 bins");

    // a sine on a bin centre gives its amplitude
    {
        std::vector<float> x (2048);
        const int bin = 100;
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.5f * std::sin (2.0 * 3.14159265358979323846 * bin * static_cast<double> (i) / 2048.0);
        std::vector<float> mag (1025);
        fft.magnitudes (x.data(), mag.data());
        float peak = 0.0f;
        for (float m : mag)
            peak = std::max (peak, m);
        check (std::fabs (mag[bin] - 0.5f) < 2e-3f && peak == mag[bin], "fft: sine of amplitude 0.5 on a bin centre gives 0.5 in that bin, the maximum");
    }

    // 1 kHz full-scale sine: exactly one band at the maximum (a full column), the others fall away
    {
        std::vector<float> x (2048);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 1.0f * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double> (i) / kRate);
        float lv[kSpectrumBands];
        levels (x, 0, fft, lv);
        int top = 0;
        for (int k = 1; k < kSpectrumBands; ++k)
            if (lv[k] > lv[top])
                top = k;
        check (spectrumBandEdgeHz (top) <= 1000.0 && 1000.0 < spectrumBandEdgeHz (top + 1), "1 kHz sine: the maximum is in the band that contains 1 kHz");
        check (lv[top] == 1.0f, "1 kHz full-scale sine: the maximum is a full column (1.0)");
        bool near_ok = true, far_ok = true, one = true;
        for (int k = 0; k < kSpectrumBands; ++k)
        {
            if (k == top)
                continue;
            one = one && lv[k] < lv[top] - 0.2f;
            if (k == top - 1 || k == top + 1)
                near_ok = near_ok && lv[k] < 0.35f;
            else
                far_ok = far_ok && lv[k] < 0.05f;
        }
        check (one, "1 kHz sine: every other band is at least 0.2 below the maximum");
        check (near_ok, "1 kHz sine: the two neighbours are below 0.35");
        check (far_ok, "1 kHz sine: every band further away is at the floor (below 0.05)");
    }

    // sines at other frequencies (6 dB below ceiling): the band of the maximum follows (100 Hz, 440 Hz, 5 kHz, 15 kHz)
    {
        const double freqs[] = { 100.0, 440.0, 5000.0, 15000.0 };
        const float amp = static_cast<float> (std::pow (10.0, static_cast<double> (kSpectrumCeilingDb - 6.0f) / 20.0));
        bool ok = true;
        for (double f : freqs)
        {
            std::vector<float> x (2048);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = amp * std::sin (2.0 * 3.14159265358979323846 * f * static_cast<double> (i) / kRate);
            float lv[kSpectrumBands];
            levels (x, 0, fft, lv);
            int top = 0;
            for (int k = 1; k < kSpectrumBands; ++k)
                if (lv[k] > lv[top])
                    top = k;
            const bool inBand = spectrumBandEdgeHz (top) <= f && f < spectrumBandEdgeHz (top + 1);
            if (!inBand)
                std::printf ("INFO  %.0f Hz: maximum in band %d (%.1f .. %.1f Hz), level %.3f\n", f, top, spectrumBandEdgeHz (top), spectrumBandEdgeHz (top + 1), lv[top]);
            ok = ok && inBand && lv[top] > 0.8f;
        }
        check (ok, "sines at 100 Hz, 440 Hz, 5 kHz and 15 kHz: the maximum is in the band that contains the frequency and is above 0.8");
    }

    // silence: every band at the floor
    {
        std::vector<float> x (2048, 0.0f);
        float lv[kSpectrumBands];
        levels (x, 0, fft, lv);
        bool ok = true;
        for (float v : lv)
            ok = ok && v == 0.0f;
        check (ok, "silence: every band is 0");
    }

    // white noise (uniform, 0.5 peak, a fixed generator): about flat. The mean level of every band over 64 blocks lies within 0.2 of the mean of all bands.
    {
        std::vector<float> x (2048 + 63 * 2048);
        uint32_t s = 12345u;
        for (float& v : x)
        {
            s = s * 1664525u + 1013904223u;
            v = (static_cast<float> (s >> 8) / 8388608.0f - 1.0f) * 0.5f;
        }
        double mean[kSpectrumBands] = {};
        for (int b = 0; b < 64; ++b)
        {
            float lv[kSpectrumBands];
            levels (x, static_cast<size_t> (b) * 2048, fft, lv);
            for (int k = 0; k < kSpectrumBands; ++k)
                mean[k] += lv[k] / 64.0;
        }
        double all = 0.0, lo = 1.0, hi = 0.0;
        for (double m : mean)
        {
            all += m / kSpectrumBands;
            lo = std::min (lo, m);
            hi = std::max (hi, m);
        }
        std::printf ("INFO  white noise: band means %.3f .. %.3f, overall %.3f\n", lo, hi, all);
        check (hi - all < 0.2 && all - lo < 0.2, "white noise: the mean level of every band is within 0.2 of the mean of all bands (about flat)");
        check (all > 0.3 && all < 0.9, "white noise: the mean level is well above the floor and below full scale");
    }

    std::printf (failures ? "FAIL  (%d failures)\n" : "spectrum_bands_host: OK\n", failures);
    return failures ? 1 : 0;
}
