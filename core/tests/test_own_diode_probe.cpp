// SynthyBoy - own engine tests: probe of the classic Diode (filter model 4) BEFORE any change (docs/plans/diode-ladder.md, task Z3).
// Prints checksums and the response peak for the two shipped Diode presets and one high-cutoff setting; no numeric assertion, so it passes on
// both sides of a change and the build worker compares the printed lines.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "synthyboy/filters/filter_types.h"
#include "voice/classic_filters.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy::own;

namespace
{
struct Setting
{
    float cutoffHz, res, driveDb;
};

// The two shipped presets (Diode Saturated Bass, Diode Bite Lead) and a high-cutoff, full-resonance case.
const Setting kSettings[3] = { { 420.0f, 0.20f, 20.0f }, { 3100.0f, 0.45f, 20.0f }, { 12000.0f, 1.0f, 0.0f } };

ClassicParams makeParams (int style, bool fast, const Setting& s)
{
    ClassicParams p;
    p.model = 4;
    p.style = style;
    p.cutoffHz = s.cutoffHz;
    p.res = s.res;
    p.driveDb = s.driveDb;
    p.blendRaw = 0.0f;
    p.fast = fast;
    return p;
}

/** 1 s of noise at amplitude 0.5 in blocks of 32: sum of |y| and the last 8 samples. */
void checksum (int style, bool fast, const Setting& s, double& sumAbs, float* last8)
{
    ClassicFilter f;
    f.prepare (44100.0);
    const ClassicParams p = makeParams (style, fast, s);
    sbtest::Rng rng (0xd10deull);
    const int total = 44100, block = 32;
    std::vector<float> in (static_cast<size_t> (block)), out (static_cast<size_t> (block));
    sumAbs = 0.0;
    for (int i = 0; i < 8; ++i)
        last8[i] = 0.0f;
    int done = 0;
    while (done < total)
    {
        const int n = std::min (block, total - done);
        for (int i = 0; i < n; ++i)
            in[static_cast<size_t> (i)] = static_cast<float> (rng.uniform() * 2.0 - 1.0) * 0.5f;
        f.process (in.data(), out.data(), n, p, p, 0);
        for (int i = 0; i < n; ++i)
        {
            const float y = out[static_cast<size_t> (i)];
            sumAbs += std::fabs (static_cast<double> (y));
            const int pos = done + i;
            if (pos >= total - 8)
                last8[pos - (total - 8)] = y;
        }
        done += n;
    }
}

/** Impulse response of 4096 samples (1e-3 impulse, divided by 1e-3), blocks of 32. */
std::vector<float> impulse (int style, bool fast, const Setting& s)
{
    ClassicFilter f;
    f.prepare (44100.0);
    const ClassicParams p = makeParams (style, fast, s);
    const int total = 4096, block = 32;
    std::vector<float> in (static_cast<size_t> (block)), ir (static_cast<size_t> (total));
    for (int pos = 0; pos < total; pos += block)
    {
        std::fill (in.begin(), in.end(), 0.0f);
        if (pos == 0)
            in[0] = 1.0e-3f;
        f.process (in.data(), ir.data() + pos, block, p, p, 0);
    }
    for (auto& v : ir)
        v /= 1.0e-3f;
    return ir;
}
} // namespace

TEST (own_diode_probe_before_change)
{
    const double sr = 44100.0;
    for (int style = 0; style < 2; ++style)
    {
        for (int fastI = 0; fastI < 2; ++fastI)
        {
            const bool fast = fastI == 1;
            for (const Setting& s : kSettings)
            {
                double sumAbs = 0.0;
                float last8[8];
                checksum (style, fast, s, sumAbs, last8);
                std::printf ("DIODECL-CHECKSUM style=%d fast=%d fc=%.0f res=%.2f sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", style, fastI,
                             static_cast<double> (s.cutoffHz), static_cast<double> (s.res), sumAbs, static_cast<double> (last8[0]),
                             static_cast<double> (last8[1]), static_cast<double> (last8[2]), static_cast<double> (last8[3]),
                             static_cast<double> (last8[4]), static_cast<double> (last8[5]), static_cast<double> (last8[6]),
                             static_cast<double> (last8[7]));

                // DFT peak of the impulse response, 50 Hz to 0.45 sr in 5 Hz steps, in dB re the DC sum.
                const std::vector<float> ir = impulse (style, fast, s);
                const int n = static_cast<int> (ir.size());
                double dcGain = 0.0;
                for (int i = 0; i < n; ++i)
                    dcGain += ir[static_cast<size_t> (i)];
                const double dcAbs = std::max (std::fabs (dcGain), 1.0e-30);
                double bestMag = 0.0, bestHz = 0.0;
                for (double hz = 50.0; hz <= 0.45 * sr; hz += 5.0)
                {
                    double re = 0.0, im = 0.0;
                    const double omega = 2.0 * 3.14159265358979323846 * hz / sr;
                    for (int i = 0; i < n; ++i)
                    {
                        re += ir[static_cast<size_t> (i)] * std::cos (omega * i);
                        im -= ir[static_cast<size_t> (i)] * std::sin (omega * i);
                    }
                    const double mag = std::sqrt (re * re + im * im);
                    if (mag > bestMag)
                    {
                        bestMag = mag;
                        bestHz = hz;
                    }
                }
                const double peakDb = 20.0 * std::log10 (std::max (bestMag, 1.0e-30) / dcAbs);
                std::printf ("DIODECL-PEAK style=%d fast=%d fc=%.0f res=%.2f peakHz=%.1f peakDb=%.3f\n", style, fastI,
                             static_cast<double> (s.cutoffHz), static_cast<double> (s.res), bestHz, peakDb);
            }
        }
    }
    CHECK (true);
}
