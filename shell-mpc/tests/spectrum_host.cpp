// SynthyBoy - host test of the spectrum ring and analyser (shell-mpc/spectrum.h): silence, a known sine, quantisation, smoothing, and the ring between two threads.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
// Build: c++ -std=c++17 -O1 -Wall -Wextra -I shell-mpc -o spectrum_host shell-mpc/tests/spectrum_host.cpp -lpthread && ./spectrum_host   (also with -fsanitize=thread)
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "spectrum.h"

using namespace synthyboy_mpc;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static const double kRate = 44100.0;
static const double kPi = 3.14159265358979323846;

static void pushSine (SpectrumRing& ring, double hz, float amp, size_t& phase, int n)
{
    std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i, ++phase)
        l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * kPi * hz * static_cast<double> (phase) / kRate));
    ring.push (l.data(), r.data(), n);
}

static int top (const float* v)
{
    int t = 0;
    for (int k = 1; k < kSpectrumCount; ++k)
        if (v[k] > v[t])
            t = k;
    return t;
}

int main()
{
    static SpectrumRing ring;
    SpectrumAnalyser an;
    float q[kSpectrumCount];

    // the ring: nothing written yet reads as zeros, then the newest samples, oldest first, mono sum (l + r) / 2
    {
        float out[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
        check (ring.latest (out, 8) && out[0] == 0.0f && out[7] == 0.0f && ring.position() == 0, "ring: empty reads as zeros");
        const float l[3] = { 1.0f, 0.5f, -1.0f }, r[3] = { 0.0f, 0.5f, -1.0f };
        ring.push (l, r, 3);
        check (ring.latest (out, 8) && out[4] == 0.0f && out[5] == 0.5f && out[6] == 0.5f && out[7] == -1.0f && ring.position() == 3, "ring: the mono sum (l + r) / 2, newest last, older slots zero");
        ring.push (l, nullptr, 3);
        check (ring.latest (out, 3) && out[0] == 1.0f && out[1] == 0.5f && out[2] == -1.0f, "ring: a null right channel (mono) is the left channel alone");
        std::vector<float> big (SpectrumRing::kSize + 100, 0.25f);
        ring.push (big.data(), big.data(), static_cast<int> (big.size()));
        float tail[16];
        check (ring.latest (tail, 16) && tail[0] == 0.25f && tail[15] == 0.25f, "ring: wraps around");
    }

    // silence: every column 0, also after a block of zeros pushed on top of the sine below
    {
        SpectrumRing quiet;
        std::vector<float> z (2048, 0.0f);
        quiet.push (z.data(), z.data(), 2048);
        SpectrumAnalyser a2;
        a2.tick (quiet, kRate, q);
        bool zero = true;
        for (float v : q)
            zero = zero && v == 0.0f;
        check (zero, "silence: all 32 columns are 0");
    }

    // 1 kHz sine at -6 dBFS: the column that contains 1 kHz is the maximum, within 0.04 of 0.90, quantised, others lower
    {
        SpectrumRing r1;
        size_t ph = 0;
        pushSine (r1, 1000.0, 0.5f, ph, 4096);
        SpectrumAnalyser a1;
        a1.tick (r1, kRate, q);
        // the first tick covers the attack only (0.7 of the way): run more ticks with new audio until it settles
        for (int i = 0; i < 12; ++i)
        {
            pushSine (r1, 1000.0, 0.5f, ph, 4410);
            a1.tick (r1, kRate, q);
        }
        const int t = top (q);
        check (spectrumBandEdgeHz (t) <= 1000.0 && 1000.0 < spectrumBandEdgeHz (t + 1), "sine 1 kHz: the largest column is the band of 1 kHz");
        check (std::fabs (q[t] - spectrumLevelFromDb (-6.02f)) < 0.04f, "sine 1 kHz: the largest column is about 0.90 (-6 dBFS)");
        bool grid = true, others = true;
        for (int k = 0; k < kSpectrumCount; ++k)
        {
            grid = grid && std::fabs (q[k] * 64.0f - std::round (q[k] * 64.0f)) < 1e-4f && q[k] >= 0.0f && q[k] <= 1.0f;
            others = others && (k == t || q[k] < q[t] - 0.2f);
        }
        check (grid, "sine 1 kHz: every value is a multiple of 1/64 in 0 .. 1");
        check (others, "sine 1 kHz: all other columns are at least 0.2 lower");
    }

    // attack fast, release slow: after the sine stops the columns fall in several ticks and reach 0
    {
        SpectrumRing r2;
        size_t ph = 0;
        SpectrumAnalyser a2;
        pushSine (r2, 1000.0, 0.5f, ph, 4096);
        a2.tick (r2, kRate, q);
        const int t = top (q);
        check (q[t] > 0.5f * spectrumLevelFromDb (-6.02f), "smoothing: the first tick already covers more than half of the rise (fast attack)");
        std::vector<float> z (4410, 0.0f);
        r2.push (z.data(), z.data(), 4096);
        a2.tick (r2, kRate, q);
        const float first = q[t];
        check (first > 0.0f && first < 0.9f, "smoothing: the first tick of silence has fallen only part of the way (slow release)");
        int ticks = 1;
        float last = first;
        bool falling = true;
        while (q[t] > 0.0f && ticks < 100)
        {
            r2.push (z.data(), z.data(), 4410);
            a2.tick (r2, kRate, q);
            falling = falling && q[t] <= last;
            last = q[t];
            ++ticks;
        }
        check (falling && q[t] == 0.0f && ticks > 8 && ticks < 60, "smoothing: the column falls steadily to 0 within 9 .. 59 ticks of silence");
        bool zero = true;
        for (float v : q)
            zero = zero && v == 0.0f;
        check (zero, "smoothing: after that every column is 0 (no report is needed)");
    }

    // a ring that does not move counts as silence (the host stopped calling)
    {
        SpectrumRing r3;
        size_t ph = 0;
        pushSine (r3, 1000.0, 0.5f, ph, 4096);
        SpectrumAnalyser a3;
        check (!a3.tick (r3, kRate, q), "stale: a ring that moved is not stale");
        float before = q[top (q)];
        check (a3.tick (r3, kRate, q), "stale: a ring that did not move since the last tick is");
        check (q[top (q)] < before || before == 0.0f, "stale: the columns fall as for silence");
    }

    // two threads: the audio thread pushes while the display thread analyses; nothing must be torn (run under ThreadSanitizer too)
    {
        SpectrumRing shared;
        std::atomic<bool> stop { false };
        std::thread audio ([&] {
            size_t ph = 0;
            std::vector<float> l (128), r (128);
            while (!stop.load())
            {
                for (size_t i = 0; i < 128; ++i, ++ph)
                    l[i] = r[i] = 0.5f * static_cast<float> (std::sin (2.0 * kPi * 440.0 * static_cast<double> (ph) / kRate));
                shared.push (l.data(), r.data(), 128);
                std::this_thread::sleep_for (std::chrono::microseconds (200));
            }
        });
        SpectrumAnalyser a4;
        bool allValid = true;
        int maxTop = -1;
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
        for (int i = 0; i < 30; ++i)
        {
            a4.tick (shared, kRate, q);
            for (float v : q)
                allValid = allValid && v >= 0.0f && v <= 1.0f;
            maxTop = top (q);
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        stop.store (true);
        audio.join();
        check (allValid, "threads: every value stays within 0 .. 1 while the other thread writes");
        check (maxTop >= 0 && spectrumBandEdgeHz (maxTop) <= 440.0 && 440.0 < spectrumBandEdgeHz (maxTop + 1), "threads: a 440 Hz sine shows in the band of 440 Hz");
    }

    std::printf (failures ? "FAIL  (%d failures)\n" : "spectrum_host: OK\n", failures);
    return failures ? 1 : 0;
}
