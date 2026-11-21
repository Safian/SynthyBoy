// SynthyBoy - own engine tests: PolyBLEP at the master wrap for discontinuous phase warps (docs/plans/distortion-antialiasing.md task O3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "alias_measure.h"
#include "osc/wavetable_bank.h"
#include "osc/wt_osc.h"
#include "synthyboy/aa_switch.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kBlepTestFs = 44100.0;

Wavetable makeSawTable()
{
    constexpr double kPiLocal = 3.14159265358979323846;
    std::vector<std::vector<float>> cc (1, std::vector<float> (Wavetable::kCycle / 2 + 1, 0.0f));
    std::vector<std::vector<float>> ss = cc;
    for (int h = 1; h < Wavetable::kHarmonics; ++h)
        ss[0][static_cast<size_t> (h)] = static_cast<float> (-2.0 / (kPiLocal * static_cast<double> (h)));
    Wavetable wt;
    wt.build (cc, ss, true);
    return wt;
}

std::vector<float> renderSplit (const Wavetable& wt, int note, int warpType, float amount, bool lowWarp, bool scalarWarp, size_t periods, int splitFirst)
{
    WtOsc osc;
    osc.prepareMorph();
    Rng rng;
    osc.start (0.0f, 0.0f, rng);

    OscTick t;
    t.wt = &wt;
    t.semis = static_cast<float> (note);
    t.voices = 1;
    t.level = 1.0f;
    t.warp[0] = warpType;
    t.warpAmount[0] = amount;
    t.active = true;
    t.lowWarp = lowWarp;
    t.fastMath = lowWarp;
    t.scalarWarp = scalarWarp;

    const size_t total = periods * kControlPeriod;
    std::vector<float> outL (total, 0.0f), outR (total, 0.0f);
    for (size_t p = 0; p < periods; ++p)
    {
        t.jump = (p == 0);
        osc.update (t, kBlepTestFs, rng);
        const size_t base = p * kControlPeriod;
        if (splitFirst <= 0 || splitFirst >= kControlPeriod)
        {
            osc.render (0, kControlPeriod, outL.data() + base, outR.data() + base, nullptr, kBlepTestFs);
        }
        else
        {
            osc.render (0, splitFirst, outL.data() + base, outR.data() + base, nullptr, kBlepTestFs);
            osc.render (splitFirst, kControlPeriod - splitFirst, outL.data() + base + splitFirst, outR.data() + base + splitFirst, nullptr, kBlepTestFs);
        }
    }
    return outL;
}

double alignedDifferenceDb (const std::vector<float>& a, const std::vector<float>& b)
{
    double best = 1.0e300, ref = 0.0;
    const size_t n = a.size();
    for (size_t i = 32; i + 32 < n; ++i)
        ref += static_cast<double> (a[i]) * static_cast<double> (a[i]);
    for (int lag = -16; lag <= 16; ++lag)
    {
        double s = 0.0;
        for (size_t i = 32; i + 32 < n; ++i)
        {
            const double d = static_cast<double> (a[i]) - static_cast<double> (b[static_cast<size_t> (static_cast<long> (i) + lag)]);
            s += d * d;
        }
        best = std::min (best, s);
    }
    return 10.0 * std::log10 (best / (ref + 1.0e-30) + 1.0e-30);
}

void printBlepChecksum (const char* name, const char* aaTag, const std::vector<float>& buf)
{
    double sumAbs = 0.0;
    for (const float x : buf)
        sumAbs += static_cast<double> (std::fabs (x));
    const size_t n = buf.size();
    std::printf ("WARP-BLEP-CHECKSUM case=%s aa=%s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", name, aaTag, sumAbs,
                 static_cast<double> (buf[n - 8]), static_cast<double> (buf[n - 7]), static_cast<double> (buf[n - 6]), static_cast<double> (buf[n - 5]),
                 static_cast<double> (buf[n - 4]), static_cast<double> (buf[n - 3]), static_cast<double> (buf[n - 2]), static_cast<double> (buf[n - 1]));
}
} // namespace

TEST (own_alias_warp_blep_sync_lowers_aliasing_and_keeps_harmonics)
{
    const Wavetable saw = makeSawTable();
    const int notes[] = { 60, 72, 84, 96 };
    const size_t skip = 22050;
    const size_t measureN = 65536;
    const size_t periods = 2756; // 88192 samples >= skip + measureN

    for (const int note : notes)
    {
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        for (const bool lowWarp : { false, true })
        {
            aa::setOff (aa::kBlep);
            const std::vector<float> offBuf = renderSplit (saw, note, kWarpSync, 0.35f, lowWarp, false, periods, 0);
            aa::setOff (0);
            const std::vector<float> onBuf = renderSplit (saw, note, kWarpSync, 0.35f, lowWarp, false, periods, 0);

            const aliasm::Result off = aliasm::measure (offBuf.data() + skip, measureN, f0, kBlepTestFs, 6.0);
            const aliasm::Result on = aliasm::measure (onBuf.data() + skip, measureN, f0, kBlepTestFs, 6.0);
            std::printf ("    WARP-BLEP note=%d lowWarp=%d sync=0.35 alias off %.2f dB on %.2f dB (diff %.2f), harm off %.2f on %.2f\n", note, lowWarp ? 1 : 0,
                         off.aliasDb, on.aliasDb, on.aliasDb - off.aliasDb, off.harmDb, on.harmDb);
            const double minDrop = note >= 84 ? 6.0 : 3.0;
            CHECK (on.aliasDb <= off.aliasDb - minDrop);
            CHECK (std::fabs (on.harmDb - off.harmDb) < 1.0);
        }
    }

    // Print the 2x vs 4x difference at note 60 Sync 35 % (companion to synthyboy_test_own_profile -V).
    aa::setOff (aa::kBlep);
    const double diffOff = alignedDifferenceDb (renderSplit (saw, 60, kWarpSync, 0.35f, true, false, 320, 0),
                                                renderSplit (saw, 60, kWarpSync, 0.35f, false, false, 320, 0));
    aa::setOff (0);
    const double diffOn = alignedDifferenceDb (renderSplit (saw, 60, kWarpSync, 0.35f, true, false, 320, 0),
                                               renderSplit (saw, 60, kWarpSync, 0.35f, false, false, 320, 0));
    std::printf ("    WARP-BLEP note=60 sync=0.35 2x-vs-4x difference: off %.2f dB, on %.2f dB\n", diffOff, diffOn);
    aa::setOff (0);
}

TEST (own_alias_warp_blep_block_splits_and_scalar_warp_are_bit_identical)
{
    const Wavetable saw = makeSawTable();
    aa::setOff (0);
    for (const bool lowWarp : { false, true })
    {
        const std::vector<float> full = renderSplit (saw, 84, kWarpSync, 0.35f, lowWarp, false, 2048, 0);
        const std::vector<float> split7 = renderSplit (saw, 84, kWarpSync, 0.35f, lowWarp, false, 2048, 7);
        const std::vector<float> split1 = renderSplit (saw, 84, kWarpSync, 0.35f, lowWarp, false, 2048, 1);
        const std::vector<float> scalar = renderSplit (saw, 84, kWarpSync, 0.35f, lowWarp, true, 2048, 0);

        CHECK (std::memcmp (full.data(), split7.data(), full.size() * sizeof (float)) == 0);
        CHECK (std::memcmp (full.data(), split1.data(), full.size() * sizeof (float)) == 0);
        CHECK (std::memcmp (full.data(), scalar.data(), full.size() * sizeof (float)) == 0);
    }
}

TEST (own_alias_warp_blep_sync_zero_and_smooth_warps_are_bit_identical)
{
    const Wavetable saw = makeSawTable();
    struct Case { const char* name; int type; float amount; };
    const Case cases[] = {
        { "sync0", kWarpSync, 0.0f },
        { "formant50", kWarpFormant, 0.5f },
        { "bend50", kWarpBend, 0.5f },
    };
    for (const Case& c : cases)
        for (const int note : { 72, 84 })
            for (const bool lowWarp : { false, true })
            {
                aa::setOff (aa::kBlep);
                const std::vector<float> offBuf = renderSplit (saw, note, c.type, c.amount, lowWarp, false, 64, 0);
                aa::setOff (0);
                const std::vector<float> onBuf = renderSplit (saw, note, c.type, c.amount, lowWarp, false, 64, 0);
                CHECK (std::memcmp (onBuf.data(), offBuf.data(), onBuf.size() * sizeof (float)) == 0);
                if (note == 84 && !lowWarp)
                {
                    aa::setOff (aa::kAll);
                    printBlepChecksum (c.name, "off", renderSplit (saw, note, c.type, c.amount, lowWarp, false, 64, 0));
                    aa::setOff (0);
                    printBlepChecksum (c.name, "on", onBuf);
                }
            }
    aa::setOff (aa::kAll);
    printBlepChecksum ("sync35", "off", renderSplit (saw, 84, kWarpSync, 0.35f, false, false, 64, 0));
    aa::setOff (0);
    printBlepChecksum ("sync35", "on", renderSplit (saw, 84, kWarpSync, 0.35f, false, false, 64, 0));
}
