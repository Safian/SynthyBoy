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
        check (worst <= 1.5, "a 0.1 amplitude sine at 120 frequencies around 600 Hz, 1 kHz and 1.4 kHz: the largest level error " + std::to_string (worst) + " dB (at " + std::to_string (worstHz) + " Hz)");
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
        check (std::abs (p - pointOf (1000.0)) <= 1 && std::fabs (dbOfLevel (c.level()[p]) + 20.0) <= 1.5 && c.level()[0] < 0.02f, "a 1 kHz tone of amplitude 0.1 on that offset: the same peak, and nothing at 20 Hz");
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
        check (std::fabs (avg - expectDb) <= 1.5 && hi - lo < 3.0 && std::sqrt (single) < 2.5, b);
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

    std::printf ("%s: %d failure(s)\n", g_fails == 0 ? "spectrum_curve_host: OK" : "spectrum_curve_host: FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
