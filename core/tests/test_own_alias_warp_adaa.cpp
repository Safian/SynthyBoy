// SynthyBoy - own engine tests: the ADAA of the sample-domain oscillator warps (docs/plans/distortion-antialiasing.md task O2, dispatcher in warp_adaa.h and WtOsc::sampleAt in wt_osc.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "alias_measure.h"
#include "osc/warp_adaa.h"
#include "osc/wavetable_bank.h"
#include "osc/wt_osc.h"
#include "synthyboy/aa_switch.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kWarpTestFs = 44100.0;

Wavetable makeSawTable()
{
    std::vector<std::vector<float>> cc (1, std::vector<float> (Wavetable::kCycle / 2 + 1, 0.0f));
    std::vector<std::vector<float>> ss = cc;
    for (int h = 1; h < Wavetable::kHarmonics; ++h)
        ss[0][static_cast<size_t> (h)] = 1.0f / static_cast<float> (h);
    Wavetable wt;
    wt.build (cc, ss, true);
    return wt;
}

std::vector<float> renderWtOsc (const Wavetable& wt, int note, int warpType, float amount, float warpPhase, bool lowWarp, size_t totalSamples, const float* remapCurve = nullptr,
                                const float* const* cross = nullptr)
{
    WtOsc osc;
    osc.prepareMorph();
    Rng rng;
    osc.start (0.0f, 0.0f, rng);

    OscTick t;
    t.wt = &wt;
    t.remap = remapCurve;
    t.semis = static_cast<float> (note);
    t.voices = 1;
    t.level = 1.0f;
    t.warp[0] = warpType;
    t.warpSrc[0] = warpCrossSource (warpType);
    t.warpAmount[0] = amount;
    t.warpPhase[0] = warpPhase;
    t.active = true;
    t.lowWarp = lowWarp;
    t.fastMath = lowWarp;

    std::vector<float> outL (totalSamples, 0.0f), outR (totalSamples, 0.0f);
    for (size_t pos = 0; pos < totalSamples; pos += kControlPeriod)
    {
        const int n = static_cast<int> (std::min<size_t> (kControlPeriod, totalSamples - pos));
        t.jump = (pos == 0);
        osc.update (t, kWarpTestFs, rng);
        osc.render (0, n, outL.data() + pos, outR.data() + pos, cross, kWarpTestFs);
    }
    return outL;
}

void printChecksum (int warpType, const char* aaTag, const std::vector<float>& buf)
{
    double sumAbs = 0.0;
    for (const float x : buf)
        sumAbs += static_cast<double> (std::fabs (x));
    const size_t n = buf.size();
    std::printf ("WARP-CHECKSUM warp=%d aa=%s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", warpType, aaTag, sumAbs,
                 static_cast<double> (buf[n - 8]), static_cast<double> (buf[n - 7]), static_cast<double> (buf[n - 6]), static_cast<double> (buf[n - 5]),
                 static_cast<double> (buf[n - 4]), static_cast<double> (buf[n - 3]), static_cast<double> (buf[n - 2]), static_cast<double> (buf[n - 1]));
}
} // namespace

TEST (own_alias_warp_adaa_dispatcher_covers_every_sample_domain_warp)
{
    // every sample-domain type is handled by one of the kernels: a step from 0 to 0.8 gives the segment average, which differs from the point law for a law with a slope
    for (int type = kWarpDistTube; type <= kWarpDistSoftSat; ++type)
    {
        CHECK (warpIsSampleDomain (type));
        warpadaa::State s;
        float out = 0.0f;
        const bool handled = warpadaa::stepClip (type, s, 0.0f, 0.5f, false, out) || warpadaa::stepTanh (type, s, 0.0f, 0.5f, false, out) || warpadaa::stepMisc (type, s, 0.0f, 0.5f, false, out);
        CHECK (handled);
    }
    CHECK (warpIsSampleDomain (kWarpQuantize));
}

TEST (own_alias_warp_adaa_dispatcher_equals_the_kernels_and_falls_back_to_the_law)
{
    warpadaa::State a, b;
    float out = 0.0f;
    (void) warpadaa::step (kWarpDistSoftClip, a, 0.1f, 0.5f, false);
    (void) warpadaa::stepTanh (kWarpDistSoftClip, b, 0.1f, 0.5f, false, out);
    const float viaDispatcher = warpadaa::step (kWarpDistSoftClip, a, 0.7f, 0.5f, false);
    (void) warpadaa::stepTanh (kWarpDistSoftClip, b, 0.7f, 0.5f, false, out);
    CHECK_EQ (viaDispatcher, out);

    // a type no kernel handles: the plain law
    warpadaa::State c;
    CHECK_EQ (warpadaa::step (kWarpSync, c, 0.3f, 0.5f, false), warpSample (kWarpSync, 0.3f, 0.5f, 0.0f, false));

    // Quantize with levels 2 + (1 - a)^2 * 62: a = 0.5 -> 17.5 levels
    warpadaa::State q, r;
    (void) warpadaa::step (kWarpQuantize, q, 0.0f, 0.5f, false);
    (void) warpadaa::stepQuantize (r, 0.0f, 17.5f);
    CHECK_EQ (warpadaa::step (kWarpQuantize, q, 0.4f, 0.5f, false), warpadaa::stepQuantize (r, 0.4f, 17.5f));
}

TEST (own_alias_warp_adaa_wt_osc_lowers_aliasing_and_keeps_harmonics)
{
    const Wavetable saw = makeSawTable();
    const int notes[] = { 84, 96 };
    const int warps[] = { kWarpDistHardClip, kWarpDistLinearFold, kWarpDistRectify, kWarpDistSoftClip, kWarpDistZeroSquare, kWarpQuantize };
    const size_t skip = 22050;
    const size_t measureN = 65536;
    const size_t total = 88192; // >= skip + measureN, multiple of kControlPeriod

    for (const int note : notes)
    {
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        for (const bool lowWarp : { false, true })
            for (const int warpType : warps)
            {
                aa::setOff (aa::kWarpAdaa);
                const std::vector<float> offBuf = renderWtOsc (saw, note, warpType, 0.5f, 0.0f, lowWarp, total);
                aa::setOff (0);
                const std::vector<float> onBuf = renderWtOsc (saw, note, warpType, 0.5f, 0.0f, lowWarp, total);

                const aliasm::Result off = aliasm::measure (offBuf.data() + skip, measureN, f0, kWarpTestFs, 6.0);
                const aliasm::Result on = aliasm::measure (onBuf.data() + skip, measureN, f0, kWarpTestFs, 6.0);
                std::printf ("    WARP-AA note=%d lowWarp=%d warp=%d alias off %.2f dB on %.2f dB (diff %.2f), harm off %.2f on %.2f\n", note, lowWarp ? 1 : 0, warpType,
                             off.aliasDb, on.aliasDb, on.aliasDb - off.aliasDb, off.harmDb, on.harmDb);
                CHECK (on.aliasDb <= off.aliasDb - 3.0);
                CHECK (std::fabs (on.harmDb - off.harmDb) < 1.0);
            }
    }
    aa::setOff (0);
}

TEST (own_alias_warp_adaa_checksums_and_non_sample_domain_identity)
{
    const Wavetable saw = makeSawTable();
    std::vector<float> remap (257);
    for (int i = 0; i <= 256; ++i)
        remap[static_cast<size_t> (i)] = std::clamp (static_cast<float> (i) / 256.0f + 0.1f * std::sin (6.2831853f * static_cast<float> (i) / 256.0f), 0.0f, 1.0f);

    float crossBuf[6][kControlPeriod];
    uint32_t lcg = 123456789u;
    for (int c = 0; c < 6; ++c)
        for (int i = 0; i < kControlPeriod; ++i)
        {
            lcg = lcg * 1664525u + 1013904223u;
            crossBuf[c][i] = static_cast<float> (lcg) * (2.0f / 4294967296.0f) - 1.0f;
        }
    const float* crossPtrs[6] = { crossBuf[0], crossBuf[1], crossBuf[2], crossBuf[3], crossBuf[4], crossBuf[5] };
    const size_t total = 2048;

    for (int warpType = kWarpNone; warpType <= kWarpAmB; ++warpType)
    {
        aa::setOff (aa::kAll);
        const std::vector<float> offAll = renderWtOsc (saw, 72, warpType, 0.5f, 0.25f, false, total, remap.data(), crossPtrs);
        printChecksum (warpType, "off", offAll);

        aa::setOff (0);
        const std::vector<float> onAll = renderWtOsc (saw, 72, warpType, 0.5f, 0.25f, false, total, remap.data(), crossPtrs);
        printChecksum (warpType, "on", onAll);

        if (!warpIsSampleDomain (warpType))
        {
            // Compare with only kWarpAdaa toggled so later BLEP/BLAMP commits do not break this non-sample-domain ADAA check.
            aa::setOff (aa::kWarpAdaa);
            const std::vector<float> offAdaaOnly = renderWtOsc (saw, 72, warpType, 0.5f, 0.25f, false, total, remap.data(), crossPtrs);
            CHECK (std::memcmp (onAll.data(), offAdaaOnly.data(), total * sizeof (float)) == 0);
        }
    }
    aa::setOff (0);
}
