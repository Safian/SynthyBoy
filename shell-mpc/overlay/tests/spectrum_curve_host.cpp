// SynthyBoy - MPC overlay tests: the spectrum curve (shell-mpc/overlay/spectrum_curve.h): known signals through the ring of the audio thread give the expected curve (peak position, level
// against the scale of the 32 meters, flat noise, silence at the floor and no redraw), the attack / release timing, and the ring between an audio thread that pushes and the overlay
// thread that analyses (also under ThreadSanitizer).
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I shell-mpc/overlay -o spectrum_curve_host shell-mpc/overlay/tests/spectrum_curve_host.cpp -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../spectrum_curve.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
constexpr double SpectrumView_kMargins = 13.0;   // SpectrumView::kTopMargin + kBottomMargin (views.h): a level 0 .. 1 spans the canvas height minus these rows
int g_fails = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}

constexpr double kRate = 44100.0, kPi = 3.14159265358979323846, kHopMs = 1000.0 / 30.0;
constexpr int kHop = 1470;   // one picture of 33.3 ms at 44.1 kHz

struct Signal
{
    // a sum of sines (frequency, amplitude) plus white noise of the given peak value; both channels carry it
    std::vector<std::pair<double, double>> sines;
    double noise = 0.0;
    size_t t = 0;
    std::mt19937 rng { 12345 };
    void push (SpectrumRing& ring, int n)
    {
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        std::vector<float> l (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i, ++t)
        {
            double v = noise * u (rng);
            for (auto& s : sines)
                v += s.second * std::sin (2.0 * kPi * s.first * static_cast<double> (t) / kRate);
            l[static_cast<size_t> (i)] = static_cast<float> (v);
        }
        ring.push (l.data(), l.data(), n);
    }
};

int pointOf (double hz) { return static_cast<int> (std::lround (std::log (hz / SpectrumCurve::kFMin) / std::log (SpectrumCurve::kFMax / SpectrumCurve::kFMin) * (SpectrumCurve::kPoints - 1))); }
int peakOf (const float* v, int from = 0, int to = SpectrumCurve::kPoints - 1)
{
    int p = from;
    for (int i = from; i <= to; ++i)
        if (v[i] > v[p])
            p = i;
    return p;
}
double dbOfLevel (double level) { return kSpectrumFloorDb + level * (kSpectrumCeilingDb - kSpectrumFloorDb); }
double levelOfDb (double db) { return (db - kSpectrumFloorDb) / (kSpectrumCeilingDb - kSpectrumFloorDb); }

/** Runs `frames` pictures of the signal through the ring and the curve; returns how many of them said "changed". */
int run (SpectrumCurve& c, SpectrumRing& ring, Signal& sig, int frames, int* analysed = nullptr)
{
    int changed = 0;
    for (int f = 0; f < frames; ++f)
    {
        sig.push (ring, kHop);
        bool a = false;
        changed += c.update (ring, kRate, kHopMs, &a);
        if (analysed)
            *analysed += a;
    }
    return changed;
}
} // namespace

int main()
{
    check (SpectrumCurve::kPoints == 256 && SpectrumCurve::kFftSize == 4096, "256 points, FFT of 4096");
    check (kSpectrumFloorDb == -60.0f && kSpectrumCeilingDb == -12.0f, "the scale of the meters: -60 .. -12 dBFS");
    check (pointOf (20.0) == 0 && pointOf (20000.0) == 255 && std::fabs (SpectrumCurve::pointHz (pointOf (1000.0)) - 1000.0) < 14.0, "the log axis: point 0 = 20 Hz, point 255 = 20 kHz");

    // ---- a sine at several frequencies and levels: peak position within one point (or within half a bin where the points are narrower than a bin), level within 1.5 dB -------
    const double binHz = kRate / SpectrumCurve::kFftSize;
    for (double hz : { 60.0, 100.0, 250.0, 440.0, 700.0, 1000.0, 1234.5 })
        for (double amp : { 0.03, 0.1, 0.2 })
        {
            static SpectrumRing ring;
            SpectrumCurve c;
            Signal sig;
            sig.sines = { { hz, amp } };
            run (c, ring, sig, 40);
            const int p = peakOf (c.level());
            const double expectDb = 20.0 * std::log10 (amp);
            const double gotDb = dbOfLevel (c.level()[p]);
            const double pointWidth = SpectrumCurve::pointHz (p + 1) - SpectrumCurve::pointHz (p);
            const double off = std::fabs (SpectrumCurve::pointHz (p) - hz);
            char b[200];
            std::snprintf (b, sizeof b, "sine %.1f Hz amplitude %.2f: peak at point %d = %.1f Hz (%.1f Hz away, a point is %.1f Hz), %.2f dB (expected %.2f dB)", hz, amp, p, SpectrumCurve::pointHz (p), off, pointWidth, gotDb, expectDb);
            check (off <= std::max (pointWidth, 0.5 * binHz) && std::fabs (gotDb - expectDb) <= 1.5, b);
        }
    {   // the worst offset of a tone against the points: 40 frequencies over two points around 1 kHz and around 600 Hz, every level within 1.5 dB
        double worst = 0.0, worstHz = 0.0;
        for (double base : { 1000.0, 600.0, 1400.0 })
            for (int k = 0; k < 40; ++k)
            {
                const double hz = base * std::pow (SpectrumCurve::kFMax / SpectrumCurve::kFMin, 2.0 * k / 39.0 / (SpectrumCurve::kPoints - 1));
                static SpectrumRing ring;
                SpectrumCurve c;
                Signal sig;
                sig.sines = { { hz, 0.1 } };
                run (c, ring, sig, 30);
                const double err = std::fabs (dbOfLevel (c.level()[peakOf (c.level())]) + 20.0);
                if (err > worst)
                    worst = err, worstHz = hz;
            }
        check (worst <= 2.5, "a 0.1 amplitude sine (the curve has the height of the meters, which read a tone up to 2 dB low where it lies between the bins of their band) at 120 frequencies around 600 Hz, 1 kHz and 1.4 kHz: the largest level error " + std::to_string (worst) + " dB (at " + std::to_string (worstHz) + " Hz)");
    }
    {   // a full scale sine is over the ceiling of the scale: the curve is at the top
        static SpectrumRing ring;
        SpectrumCurve c;
        Signal sig;
        sig.sines = { { 1000.0, 1.0 } };
        run (c, ring, sig, 40);
        const int p = peakOf (c.level());
        check (std::abs (p - pointOf (1000.0)) <= 1 && c.level()[p] >= levelOfDb (-12.0 - 1.5), "a full scale 1 kHz sine: the top of the scale (expected 1.0, within 1.5 dB), got " + std::to_string (c.level()[p]));
        check (c.level()[pointOf (100.0)] < 0.05 && c.level()[pointOf (10000.0)] < 0.05, "a full scale sine: the rest of the curve is at the floor (the window's skirts are low)");
    }
    {   // a tone that falls between two bins reads as high as one on a bin (the lobe power, no scalloping loss)
        double lo = 1e9, hi = -1e9;
        for (double hz : { 1000.0, 1000.0 + 0.25 * kRate / 4096, 1000.0 + 0.5 * kRate / 4096 })
        {
            static SpectrumRing ring;
            SpectrumCurve c;
            Signal sig;
            sig.sines = { { hz, 0.1 } };
            run (c, ring, sig, 40);
            const double db = dbOfLevel (c.level()[peakOf (c.level())]);
            lo = std::min (lo, db);
            hi = std::max (hi, db);
        }
        check (hi - lo < 0.8, "the same tone on a bin centre and half a bin away: the same level within " + std::to_string (hi - lo) + " dB");
    }

    // ---- two sines ---------------------------------------------------------------------------------------------------------------------------------------------
    {
        static SpectrumRing ring;
        SpectrumCurve c;
        Signal sig;
        sig.sines = { { 500.0, 0.1 }, { 3000.0, 0.1 } };
        run (c, ring, sig, 40);
        const int p1 = peakOf (c.level(), 0, pointOf (1500.0)), p2 = peakOf (c.level(), pointOf (1500.0), SpectrumCurve::kPoints - 1);
        const double l1 = dbOfLevel (c.level()[p1]);
        char b[200];
        std::snprintf (b, sizeof b, "two sines: peaks at points %d and %d (expected %d and %d), 500 Hz at %.2f dB (expected -20)", p1, p2, pointOf (500.0), pointOf (3000.0), l1);
        check (std::abs (p1 - pointOf (500.0)) <= 1 && std::abs (p2 - pointOf (3000.0)) <= 1 && std::fabs (l1 + 20.0) <= 1.5, b);
        check (c.level()[pointOf (1200.0)] < 0.5 * c.level()[p1], "two sines: the curve between them is far below the peaks");
    }

    // ---- a DC offset is not sound: the curve stays at the floor ------------------------------------------------------------------------------------------------------
    {
        SpectrumCurve c;
        std::vector<float> w (SpectrumCurve::kFftSize, 0.3f);
        for (int i = 0; i < 10; ++i)
            c.step (w.data(), kHopMs);
        check (c.maxLevel() == 0.0f, "a constant offset of 0.3 (-10 dBFS) with no sound in it: the curve stays at the floor (no hump at the low end)");
        for (int i = 0; i < SpectrumCurve::kFftSize; ++i)
            w[static_cast<size_t> (i)] = 0.3f + 0.1f * static_cast<float> (std::sin (2.0 * kPi * 1000.0 * i / kRate));
        for (int i = 0; i < 30; ++i)
            c.step (w.data(), kHopMs);
        const int p = peakOf (c.level());
        check (std::abs (p - pointOf (1000.0)) <= 1 && std::fabs (dbOfLevel (c.level()[p]) + 20.0) <= 2.5 && c.level()[0] < 0.02f, "a 1 kHz tone of amplitude 0.1 on that offset: the same peak, and nothing at 20 Hz");
    }

    // ---- white noise: flat after the per point averaging -------------------------------------------------------------------------------------------------------
    {
        static SpectrumRing ring;
        SpectrumCurve c;
        Signal sig;
        sig.noise = 0.3;   // uniform in +-0.3: variance 0.03
        run (c, ring, sig, 12);
        const int from = pointOf (1000.0), to = pointOf (16000.0);
        std::vector<double> mean (SpectrumCurve::kPoints, 0.0);
        const int frames = 60;
        for (int f = 0; f < frames; ++f)
        {
            run (c, ring, sig, 1);
            for (int i = from; i <= to; ++i)
                mean[static_cast<size_t> (i)] += dbOfLevel (c.target()[i]) / frames;
        }
        double avg = 0.0, lo = 1e9, hi = -1e9, single = 0.0;
        for (int i = from; i <= to; ++i)
        {
            avg += mean[static_cast<size_t> (i)] / (to - from + 1);
            lo = std::min (lo, mean[static_cast<size_t> (i)]);
            hi = std::max (hi, mean[static_cast<size_t> (i)]);
        }
        for (int i = from; i <= to; ++i)
            single += std::pow (dbOfLevel (c.target()[i]) - avg, 2.0) / (to - from + 1);
        const double expectDb = 10.0 * std::log10 (12.0 * 0.03 / SpectrumCurve::kFftSize);
        char b[240];
        std::snprintf (b, sizeof b, "white noise 1 - 16 kHz: mean level %.2f dB (expected %.2f), the mean of every point within %.2f .. %.2f dB (flat), one picture's spread %.2f dB", avg, expectDb, lo, hi, std::sqrt (single));
        // the curve reads like the meters: the highest of the bins of a band (a few dB over the mean of white noise), not the mean
        check (avg - expectDb >= 0.0 && avg - expectDb <= 6.0 && hi - lo < 6.5 && std::sqrt (single) < 3.5, b);
    }

    // ---- silence: the floor, no analysis, no redraw ------------------------------------------------------------------------------------------------------------
    {
        static SpectrumRing ring;
        SpectrumCurve c;
        Signal sig;
        sig.sines = { { 1000.0, 0.3 } };
        run (c, ring, sig, 30);
        check (c.maxLevel() > 0.8, "a note: the curve is up");
        sig.sines.clear();
        int analysed = 0;
        const int changedWhileFalling = run (c, ring, sig, 6, &analysed);   // the window still holds the note for the first picture or two
        check (changedWhileFalling > 0, "the note ends: the curve falls");
        analysed = 0;
        const int changed = run (c, ring, sig, 200, &analysed);   // 6.7 s of exact digital silence
        check (c.maxLevel() == 0.0f && c.settled(), "after the note the curve sits at the floor (exactly 0) and has settled");
        check (changed <= 100, "the fall is a limited number of pictures (" + std::to_string (changed) + ")");
        analysed = 0;
        const int again = run (c, ring, sig, 100, &analysed);
        check (again == 0 && analysed == 0, "silence: no redraw requested in 100 pictures, no FFT run");
        // a ring that does not move (the host stopped calling) is silence too
        Signal tone;
        tone.sines = { { 800.0, 0.2 } };
        run (c, ring, tone, 20);
        check (c.maxLevel() > 0.5, "the tone is back");
        int moved = 0;
        for (int f = 0; f < 120; ++f)
            moved += c.update (ring, kRate, kHopMs);   // nothing pushed: the position stands still
        check (c.maxLevel() == 0.0f && moved > 0, "a ring that stands still counts as silence: the curve falls to the floor");
        int after = 0;
        for (int f = 0; f < 50; ++f)
            after += c.update (ring, kRate, kHopMs);
        check (after == 0, "and then no redraw");
        // a very quiet signal (-90 dBFS) is under the floor of the scale and under the silence threshold: no FFT
        Signal quiet;
        quiet.noise = 3.0e-5;
        Signal zeros;
        run (c, ring, zeros, 250);   // the window holds the tone of before for two more pictures; the curve rises with it and falls again
        analysed = 0;
        const int q = run (c, ring, quiet, 60, &analysed);
        check (analysed == 0 && q == 0 && c.maxLevel() == 0.0f, "a signal at -90 dBFS: floor, no FFT, no redraw (FFTs " + std::to_string (analysed) + ", pictures " + std::to_string (q) + ", level " + std::to_string (c.maxLevel()) + ")");
    }

    // ---- timing: fast attack, slow release ---------------------------------------------------------------------------------------------------------------------
    {
        static SpectrumRing ring;
        SpectrumCurve c;
        Signal sig;
        sig.sines = { { 1000.0, 0.1 } };
        // the first window is full of tone after three pictures; look at the target and the smoothed level of the peak point
        run (c, ring, sig, 4);   // 4 x 33 ms = 133 ms: the 93 ms window is full
        const int p = pointOf (1000.0);
        const double target = c.target()[peakOf (c.level())];
        run (c, ring, sig, 20);
        const double full = c.level()[peakOf (c.level())];
        check (std::fabs (full - target) < 0.02, "attack: the level reaches the target (" + std::to_string (full) + " against " + std::to_string (target) + ")");
        (void) p;
    }
    {
        // attack: one picture after the tone starts the point has covered 1 - exp (-dt / 20 ms) = 81 % of the way
        SpectrumCurve c;
        std::vector<float> w (SpectrumCurve::kFftSize);
        for (int i = 0; i < SpectrumCurve::kFftSize; ++i)
            w[static_cast<size_t> (i)] = static_cast<float> (0.1 * std::sin (2.0 * kPi * 1000.0 * i / kRate));
        c.setSampleRate (kRate);
        c.step (w.data(), kHopMs);
        const int p = peakOf (c.target());
        const double r = c.level()[p] / c.target()[p];
        check (std::fabs (r - (1.0 - std::exp (-kHopMs / SpectrumCurve::kAttackMs))) < 0.01, "attack: one picture of 33 ms covers 81 % of the rise (got " + std::to_string (r) + ")");
        // release: the level falls with the time constant of 350 ms (a window of digital silence: nothing is analysed)
        const double top = c.level()[p];
        for (int i = 0; i < 30; ++i)
            c.step (w.data(), kHopMs);
        const double settledTop = c.level()[p];
        double t = 0.0;
        int steps = 0;
        const double start = settledTop;
        while (c.level()[p] > start * std::exp (-1.0) && steps < 1000)
        {
            c.step (nullptr, 10.0);
            t += 10.0;
            ++steps;
        }
        check (std::fabs (t - SpectrumCurve::kReleaseMs) < 30.0, "release: the level is at 1/e after about 350 ms (got " + std::to_string (t) + " ms)");
        (void) top;
        for (int i = 0; i < 1000; ++i)
            c.step (nullptr, 10.0);
        check (c.maxLevel() == 0.0f, "release: to exactly the floor within 10 s");
    }

    // ---- the sample rate: a change resets, 48 kHz and 96 kHz put the peak at the same point ---------------------------------------------------------------------------
    for (double rate : { 48000.0, 96000.0 })
    {
        SpectrumCurve c (rate);
        std::vector<float> w (SpectrumCurve::kFftSize);
        for (int i = 0; i < SpectrumCurve::kFftSize; ++i)
            w[static_cast<size_t> (i)] = static_cast<float> (0.1 * std::sin (2.0 * kPi * 1000.0 * i / rate));
        for (int i = 0; i < 20; ++i)
            c.step (w.data(), kHopMs);
        const int p = peakOf (c.level());
        check (std::abs (p - pointOf (1000.0)) <= 1, "sample rate " + std::to_string (static_cast<int> (rate)) + ": the 1 kHz peak is at its point (" + std::to_string (p) + ")");
    }

    // ---- the ring between two threads: the audio thread pushes blocks of 128 samples, the overlay thread analyses ------------------------------------------------------
    {
        static SpectrumRing ring;
        SpectrumCurve c;
        std::atomic<bool> stop { false };
        std::thread audio ([&]
        {
            size_t t = 0;
            std::vector<float> l (128);
            while (! stop.load())
            {
                for (size_t i = 0; i < 128; ++i, ++t)
                    l[i] = static_cast<float> (0.2 * std::sin (2.0 * kPi * 2000.0 * static_cast<double> (t) / kRate));
                ring.push (l.data(), l.data(), 128);
                std::this_thread::sleep_for (std::chrono::microseconds (500));   // faster than real time: the writer overtakes the reader sometimes
            }
        });
        int bad = 0, frames = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (1200);
        while (std::chrono::steady_clock::now() < end)
        {
            c.update (ring, kRate, kHopMs);
            ++frames;
            std::this_thread::sleep_for (std::chrono::milliseconds (15));
        }
        stop = true;
        audio.join();
        const int p = peakOf (c.level());
        bad = std::abs (p - pointOf (2000.0)) > 1;
        check (frames > 30 && bad == 0, "two threads: the audio thread pushes while the overlay thread analyses: the peak is at 2 kHz (point " + std::to_string (p) + ")");
    }

    // ---- the curve is as high as the 32 meters under it: in the frequency band of each meter the highest point of the curve is the meter's value (within one pixel) ---------------------
    {
        constexpr int kViewH = 136;                                   // the spectrum view's canvas height (layout): a level 0 .. 1 is (kViewH - 13) rows
        constexpr double kPixel = 1.0 / (kViewH - SpectrumView_kMargins);
        const double hzs[] = { 40.0, 55.0, 100.0, 262.0, 440.0, 1000.0, 1700.0, 3000.0, 5200.0, 8000.0, 12500.0, 16000.0 };
        const double amps[] = { 0.7, 0.2, 0.05, 0.01 };               // -3, -14, -26, -40 dBFS
        int cases = 0, bad = 0, badAnalyser = 0;
        double worst = 0.0, worstHz = 0.0, worstAmp = 0.0, worstOld = 0.0;
        for (double hz : hzs)
            for (double amp : amps)
            {
                std::vector<float> w (static_cast<size_t> (SpectrumCurve::kFftSize));
                for (size_t i = 0; i < w.size(); ++i)
                    w[i] = static_cast<float> (amp * std::sin (2.0 * kPi * hz * static_cast<double> (i) / kRate));
                SpectrumCurve c (kRate);
                for (int i = 0; i < 80; ++i)
                    c.step (w.data(), kHopMs);
                // the meters' value: the 2048 point analysis of the newest samples, the maximum of the bins of the band (shell-mpc/spectrum_bands.h), before the time smoothing
                SpectrumFft fft (kSpectrumFftSize);
                std::vector<float> mag (static_cast<size_t> (fft.bins()));
                fft.magnitudes (w.data() + (w.size() - kSpectrumFftSize), mag.data());
                float db[kSpectrumBands];
                spectrumBandDb (mag.data(), fft.bins(), kRate, db);
                // the meters as the plugin shows them (smoothed in time, quantised to 1/64): the same signal, long enough to settle
                SpectrumAnalyser an;
                float q[kSpectrumBands] = {};
                for (int i = 0; i < 80; ++i)
                    an.tickSamples (w.data() + (w.size() - kSpectrumFftSize), kRate, q);
                int band = -1;
                for (int k = 0; k < kSpectrumBands; ++k)
                    if (hz >= spectrumBandEdgeHz (k) && hz < spectrumBandEdgeHz (k + 1))
                        band = k;
                if (band < 0) { check (false, "the test frequency " + std::to_string (hz) + " is in a band"); continue; }
                // the highest point of the curve among the points of each of the bands: the sine's band and the bands next to it (their meters show the window's skirt)
                for (int k = std::max (0, band - 1); k <= std::min (kSpectrumBands - 1, band + 1); ++k)
                {
                    float top = -1.0f;
                    for (int i = 0; i < SpectrumCurve::kPoints; ++i)
                    {
                        const double f = SpectrumCurve::pointHz (i);
                        if (f >= spectrumBandEdgeHz (k) && f < spectrumBandEdgeHz (k + 1))
                            top = std::max (top, c.level()[i]);
                    }
                    const double meter = spectrumLevelFromDb (db[k]);
                    ++cases;
                    const double diff = std::fabs (static_cast<double> (top) - meter);
                    if (diff > kPixel)
                    {
                        ++bad;
                        std::printf ("      %.0f Hz amplitude %.2f band %d: curve top %.4f, meter %.4f (%.2f rows apart)\n", hz, amp, k, top, meter, diff / kPixel);
                    }
                    if (k == band && diff > worst) { worst = diff; worstHz = hz; worstAmp = amp; }
                    // the meters on the device are quantised to 1/64 (and at their time constants the same here): the curve is within one row plus half a step of them
                    if (std::fabs (static_cast<double> (top) - q[k]) > kPixel + 0.5 / kSpectrumSteps + 1.0e-6)
                        ++badAnalyser;
                    (void) worstOld;
                }
            }
        check (cases > 0 && bad == 0, "sines of 12 frequencies and 4 levels: in the band of the meter and the bands next to it the curve's highest point is the meter's value within one row (" + std::to_string (cases) + " bands, " + std::to_string (bad) + " off; worst in the sine's band " + std::to_string (worst / kPixel) + " rows at " + std::to_string (worstHz) + " Hz, amplitude " + std::to_string (worstAmp) + ")");
        check (badAnalyser == 0, "... and the meters of the plugin as the host shows them (quantised to 1/64) are within one row plus half a step (" + std::to_string (badAnalyser) + " off)");
    }
    {   // a chord (several partials in one band): the curve keeps every peak and the highest one is the meter's value; the curve is never above the meter's value inside a band
        std::vector<float> w (static_cast<size_t> (SpectrumCurve::kFftSize));
        const double partials[][2] = { { 220.0, 0.2 }, { 440.0, 0.1 }, { 660.0, 0.12 }, { 880.0, 0.05 }, { 1320.0, 0.06 }, { 2640.0, 0.03 }, { 5280.0, 0.02 } };
        for (size_t i = 0; i < w.size(); ++i)
        {
            double v = 0.0;
            for (auto& pr : partials)
                v += pr[1] * std::sin (2.0 * kPi * pr[0] * static_cast<double> (i) / kRate);
            w[i] = static_cast<float> (v);
        }
        SpectrumCurve c (kRate);
        for (int i = 0; i < 80; ++i)
            c.step (w.data(), kHopMs);
        SpectrumFft fft (kSpectrumFftSize);
        std::vector<float> mag (static_cast<size_t> (fft.bins()));
        {   // (the mean of the meters' window out: the sines of this chord do not fill whole periods, and a DC offset is not sound)
            std::vector<float> m (w.end() - kSpectrumFftSize, w.end());
            double sum = 0.0;
            for (float v : m) sum += v;
            for (float& v : m) v -= static_cast<float> (sum / kSpectrumFftSize);
            fft.magnitudes (m.data(), mag.data());
        }
        float db[kSpectrumBands];
        spectrumBandDb (mag.data(), fft.bins(), kRate, db);
        int off = 0, above = 0;
        for (int k = 0; k < kSpectrumBands; ++k)
        {
            float top = -1.0f;
            for (int i = 0; i < SpectrumCurve::kPoints; ++i)
            {
                const double f = SpectrumCurve::pointHz (i);
                if (f >= spectrumBandEdgeHz (k) && f < spectrumBandEdgeHz (k + 1))
                    top = std::max (top, c.level()[i]);
            }
            const double meter = spectrumLevelFromDb (db[k]);
            if (std::fabs (static_cast<double> (top) - meter) > 1.0 / (136 - SpectrumView_kMargins) || top > meter + 1.0e-4)
                std::printf ("      band %d: curve top %.4f, meter %.4f\n", k, top, meter);
            off += std::fabs (static_cast<double> (top) - meter) > 1.0 / (136 - SpectrumView_kMargins);
            above += top > meter + 1.0e-4;
        }
        check (off == 0, "seven partials: in every one of the 32 bands the curve's highest point is the meter's value within one row (" + std::to_string (off) + " bands off)");
        check (above == 0, "... and the curve never rises above the meter's value inside a band");
        check (c.level()[pointOf (220.0)] > c.level()[pointOf (330.0)] + 0.1, "... while the valley between two partials stays a valley (the curve is not flattened to the band)");
    }

    std::printf ("%s: %d failure(s)\n", g_fails == 0 ? "spectrum_curve_host: OK" : "spectrum_curve_host: FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
