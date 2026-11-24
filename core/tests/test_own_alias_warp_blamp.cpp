// SynthyBoy - own engine tests: PolyBLAMP at slope discontinuities and kind-1 wraps (docs/plans/distortion-antialiasing.md task O4).
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
constexpr double kBlampTestFs = 44100.0;

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

std::vector<float> renderSplit (const Wavetable& wt, int note, int warpType, float amount, bool lowWarp, size_t periods, int splitFirst)
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

    const size_t total = periods * kControlPeriod;
    std::vector<float> outL (total, 0.0f), outR (total, 0.0f);
    for (size_t p = 0; p < periods; ++p)
    {
        t.jump = (p == 0);
        osc.update (t, kBlampTestFs, rng);
        const size_t base = p * kControlPeriod;
        if (splitFirst <= 0 || splitFirst >= kControlPeriod)
        {
            osc.render (0, kControlPeriod, outL.data() + base, outR.data() + base, nullptr, kBlampTestFs);
        }
        else
        {
            osc.render (0, splitFirst, outL.data() + base, outR.data() + base, nullptr, kBlampTestFs);
            osc.render (splitFirst, kControlPeriod - splitFirst, outL.data() + base + splitFirst, outR.data() + base + splitFirst, nullptr, kBlampTestFs);
        }
    }
    return outL;
}

void printBlampChecksum (const char* name, const char* aaTag, const std::vector<float>& buf)
{
    double sumAbs = 0.0;
    for (const float x : buf)
        sumAbs += static_cast<double> (std::fabs (x));
    const size_t n = buf.size();
    std::printf ("WARP-BLAMP-CHECKSUM case=%s aa=%s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", name, aaTag, sumAbs,
                 static_cast<double> (buf[n - 8]), static_cast<double> (buf[n - 7]), static_cast<double> (buf[n - 6]), static_cast<double> (buf[n - 5]),
                 static_cast<double> (buf[n - 4]), static_cast<double> (buf[n - 3]), static_cast<double> (buf[n - 2]), static_cast<double> (buf[n - 1]));
}
} // namespace

TEST (own_alias_warp_blamp_lowers_kink_aliasing_on_mirror_flip_and_pulse)
{
    const Wavetable saw = makeSawTable();
    struct Case { const char* name; int type; float amount; };
    const Case cases[] = {
        { "mirror80", kWarpMirror, 0.8f },
        { "flip50", kWarpFlip, 0.5f },
        { "pulse50", kWarpPulse, 0.5f },
    };
    const size_t skip = 22050;
    const size_t measureN = 65536;
    const size_t periods = 2756;

    for (const Case& c : cases)
        for (const int note : { 72, 84 })
        {
            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            aa::setOff (aa::kBlamp);
            const std::vector<float> offBuf = renderSplit (saw, note, c.type, c.amount, true, periods, 0);
            aa::setOff (0);
            const std::vector<float> onBuf = renderSplit (saw, note, c.type, c.amount, true, periods, 0);

            const aliasm::Result off = aliasm::measure (offBuf.data() + skip, measureN, f0, kBlampTestFs, 6.0);
            const aliasm::Result on = aliasm::measure (onBuf.data() + skip, measureN, f0, kBlampTestFs, 6.0);
            std::printf ("    WARP-BLAMP case=%s note=%d alias off %.2f dB on %.2f dB (diff %.2f), harm off %.2f on %.2f\n", c.name, note,
                         off.aliasDb, on.aliasDb, on.aliasDb - off.aliasDb, off.harmDb, on.harmDb);
            CHECK (on.aliasDb <= off.aliasDb - 3.0);
            CHECK (std::fabs (on.harmDb - off.harmDb) < 1.0);
        }
    aa::setOff (0);
}

TEST (own_alias_warp_blamp_sync_with_blep_and_block_splits)
{
    const Wavetable saw = makeSawTable();
    const size_t skip = 22050;
    const size_t measureN = 65536;
    const size_t periods = 2756;

    for (const int note : { 60, 72, 84, 96 })
    {
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        for (const bool lowWarp : { false, true })
        {
            aa::setOff (aa::kBlamp);
            const std::vector<float> blepOnlyBuf = renderSplit (saw, note, kWarpSync, 0.35f, lowWarp, periods, 0);
            aa::setOff (0);
            const std::vector<float> bothBuf = renderSplit (saw, note, kWarpSync, 0.35f, lowWarp, periods, 0);

            const aliasm::Result blepOnly = aliasm::measure (blepOnlyBuf.data() + skip, measureN, f0, kBlampTestFs, 6.0);
            const aliasm::Result both = aliasm::measure (bothBuf.data() + skip, measureN, f0, kBlampTestFs, 6.0);
            std::printf ("    WARP-BLAMP sync35 note=%d lowWarp=%d blepOnly %.2f dB blep+blamp %.2f dB (diff %.2f)\n", note, lowWarp ? 1 : 0,
                         blepOnly.aliasDb, both.aliasDb, both.aliasDb - blepOnly.aliasDb);
            CHECK (both.aliasDb <= blepOnly.aliasDb + 0.5);
        }
    }

    struct SplitCase { const char* name; int type; float amount; };
    const SplitCase splitCases[] = {
        { "mirror80", kWarpMirror, 0.8f },
        { "flip50", kWarpFlip, 0.5f },
        { "pulse50", kWarpPulse, 0.5f },
        { "sync35", kWarpSync, 0.35f },
    };
    aa::setOff (0);
    for (const SplitCase& c : splitCases)
        for (const bool lowWarp : { false, true })
        {
            const std::vector<float> full = renderSplit (saw, 84, c.type, c.amount, lowWarp, 2048, 0);
            const std::vector<float> split7 = renderSplit (saw, 84, c.type, c.amount, lowWarp, 2048, 7);
            const std::vector<float> split1 = renderSplit (saw, 84, c.type, c.amount, lowWarp, 2048, 1);
            CHECK (std::memcmp (full.data(), split7.data(), full.size() * sizeof (float)) == 0);
            CHECK (std::memcmp (full.data(), split1.data(), full.size() * sizeof (float)) == 0);
        }

    const SplitCase allCases[] = {
        { "mirror80", kWarpMirror, 0.8f },
        { "flip50", kWarpFlip, 0.5f },
        { "pulse50", kWarpPulse, 0.5f },
        { "sync35", kWarpSync, 0.35f },
        { "formant50", kWarpFormant, 0.5f },
        { "bend50", kWarpBend, 0.5f },
    };
    for (const SplitCase& c : allCases)
    {
        aa::setOff (aa::kAll);
        const std::vector<float> offAll = renderSplit (saw, 84, c.type, c.amount, true, 64, 0);
        printBlampChecksum (c.name, "off", offAll);

        aa::setOff (0);
        const std::vector<float> onAll = renderSplit (saw, 84, c.type, c.amount, true, 64, 0);
        printBlampChecksum (c.name, "on", onAll);

        if (c.type == kWarpFormant || c.type == kWarpBend)
            CHECK (std::memcmp (onAll.data(), offAll.data(), onAll.size() * sizeof (float)) == 0);
    }
    aa::setOff (0);
}
