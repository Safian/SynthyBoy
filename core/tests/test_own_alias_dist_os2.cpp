// SynthyBoy - own engine tests: 2x oversampled ADAA in Distortion (docs/plans/distortion-antialiasing.md task E8).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "alias_dist_support.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
std::vector<float> runOs2 (int type, float driveDb, bool fast, bool os2, const std::vector<float>& in, float mix = 1.0f, size_t chunk = 32)
{
    own::Distortion::Params p = aliasfx::params (type, driveDb);
    p.mix = mix;
    p.os2 = os2;
    return aliasfx::run (p, fast, in, chunk);
}
} // namespace

TEST (own_alias_dist_os2_lowers_the_aliasing_against_adaa_only)
{
    synthyboy::aa::setOff (0);
    const std::vector<float> in = aliasfx::sine();
    for (const int type : { 1, 0, 9 })
        for (const bool fast : { false, true })
        {
            const std::vector<float> yOff = runOs2 (type, 18.0f, fast, false, in);
            const std::vector<float> yOn = runOs2 (type, 18.0f, fast, true, in);
            const aliasm::Result off = aliasm::measure (yOff.data() + aliasfx::kSkip, aliasfx::kN, aliasfx::f0(), aliasfx::kFs);
            const aliasm::Result on = aliasm::measure (yOn.data() + aliasfx::kSkip, aliasfx::kN, aliasfx::f0(), aliasfx::kFs);
            std::printf ("    DIST-OS2-AA type=%d fast=%d alias adaa %.2f dB os2 %.2f dB (difference %.2f), harm adaa %.2f os2 %.2f\n",
                         type, fast ? 1 : 0, off.aliasDb, on.aliasDb, on.aliasDb - off.aliasDb, off.harmDb, on.harmDb);
            CHECK (on.aliasDb <= off.aliasDb - 3.0);
            CHECK (std::fabs (on.harmDb - off.harmDb) < 1.0);
        }
}

TEST (own_alias_dist_os2_halves_the_passband_droop_at_15khz)
{
    synthyboy::aa::setOff (0);
    const size_t n = 4096, warm = 4096, bin = 1393; // 1393 * 44100 / 4096 = 14998.0 Hz
    const double f = static_cast<double> (bin) * aliasfx::kFs / static_cast<double> (n);
    std::vector<float> in (warm + n);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float> (0.5 * std::sin (6.283185307179586 * static_cast<double> ((bin * i) % n) / static_cast<double> (n)));

    auto measureGainDb = [&] (bool os2)
    {
        const std::vector<float> y = runOs2 (1, -30.0f, false, os2, in);
        double sc = 0.0, ss = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double ph = 6.283185307179586 * static_cast<double> ((bin * i) % n) / static_cast<double> (n);
            sc += static_cast<double> (y[warm + i]) * std::cos (ph);
            ss += static_cast<double> (y[warm + i]) * std::sin (ph);
        }
        const double amp = 2.0 / static_cast<double> (n) * std::sqrt (sc * sc + ss * ss);
        return 20.0 * std::log10 (amp / (0.5 * std::pow (10.0, -30.0 / 20.0)));
    };

    const double gainOffDb = measureGainDb (false);
    const double gainOnDb = measureGainDb (true);
    std::printf ("DIST-OS2-DROOP type=1 f=%.1f os2_off=%.3f dB os2_on=%.3f dB\n", f, gainOffDb, gainOnDb);
    CHECK (gainOnDb > -2.0);
    CHECK (gainOnDb <= 0.5);
    CHECK (gainOnDb > gainOffDb + 3.0);
}

TEST (own_alias_dist_os2_impulse_delay_is_15_samples_for_every_type)
{
    synthyboy::aa::setOff (0);
    CHECK_EQ (own::Distortion::kLatency, 15);
    std::vector<float> impulse (64, 0.0f);
    impulse[0] = 1.0f;

    for (int type = 0; type <= 13; ++type)
    {
        for (const float mix : { 1.0f, 0.5f, 0.0f })
        {
            const float drive = (type == 5) ? -30.0f : 0.0f;
            const std::vector<float> y = runOs2 (type, drive, false, true, impulse, mix);
            size_t peakIdx = 0;
            float peakVal = 0.0f;
            for (size_t i = 0; i < y.size(); ++i)
            {
                const float a = std::fabs (y[i]);
                if (a > peakVal)
                {
                    peakVal = a;
                    peakIdx = i;
                }
            }
            CHECK_EQ (static_cast<int> (peakIdx), own::Distortion::kLatency);
            CHECK (peakVal > 0.1f);
            if (mix == 0.0f || type == 4 || type == 5)
            {
                CHECK_NEAR (y[static_cast<size_t> (own::Distortion::kLatency)], 1.0, 1e-6);
                for (size_t i = 0; i < y.size(); ++i)
                    if (i != static_cast<size_t> (own::Distortion::kLatency))
                        CHECK_EQ (y[i], 0.0f);
            }
        }
    }
}

TEST (own_alias_dist_os2_drive_ramps_across_the_period)
{
    synthyboy::aa::setOff (0);
    const int period = 32;
    auto runPeriods = [period] (int type, float driveA, float driveB, int jumpPeriod, int periods, std::vector<float>& out)
    {
        own::Distortion d;
        d.prepare (aliasfx::kFs);
        out.clear();
        for (int k = 0; k < periods; ++k)
        {
            own::Distortion::Params p = aliasfx::params (type, k >= jumpPeriod ? driveB : driveA);
            p.os2 = true;
            d.set (p);
            std::vector<float> l (period), r (period);
            for (int i = 0; i < period; ++i)
                l[i] = r[i] = 0.6f * std::sin (2.0f * 3.14159265f * 440.0f * static_cast<float> (k * period + i) / static_cast<float> (aliasfx::kFs));
            d.process (l.data(), r.data(), period, 0, period);
            out.insert (out.end(), l.begin(), l.end());
        }
    };

    // 1. Step of drive -10 -> +20 dB on constant 0.05 input: smooth ramp across the period (plus 15-sample oversampler delay), no step
    {
        own::Distortion d;
        d.prepare (aliasfx::kFs);
        own::Distortion::Params p = aliasfx::params (0, -10.0f);
        p.os2 = true;
        std::vector<float> y;
        for (int k = 0; k < 6; ++k)
        {
            p.driveDb = k < 2 ? -10.0f : 20.0f;
            d.set (p);
            std::vector<float> l (period, 0.05f), r (period, 0.05f);
            d.process (l.data(), r.data(), period, 0, period);
            y.insert (y.end(), l.begin(), l.end());
        }
        const float before = y[2 * period - 1];
        const float after = y[5 * period - 1];
        double maxStep = 0.0;
        for (size_t i = 2 * period; i < y.size(); ++i)
            maxStep = std::max (maxStep, static_cast<double> (std::fabs (y[i] - y[i - 1])));
        CHECK (after - before > 0.3f);
        CHECK (maxStep < 0.4 * static_cast<double> (after - before));
        CHECK (std::fabs (after - std::tanh (0.5f)) < 1.0e-4f);
    }

    // 2. Settled output after the ramp matches an instance that ran the new drive from the start
    for (int type : { 0, 1, 2, 3, 8, 9, 12 })
    {
        std::vector<float> jumped, steady;
        runPeriods (type, -10.0f, 20.0f, 100, 300, jumped);
        runPeriods (type, 20.0f, 20.0f, 0, 300, steady);
        double maxDiff = 0.0;
        for (size_t i = 104 * period; i < jumped.size(); ++i)
            maxDiff = std::max (maxDiff, static_cast<double> (std::fabs (jumped[i] - steady[i])));
        bool finite = true;
        for (float v : jumped)
            finite = finite && std::isfinite (v);
        CHECK (finite);
        CHECK (maxDiff < 1.0e-4);
    }

    // 3. Still drive with periods is bit-identical to period == 0
    for (int type : { 0, 4, 5, 12 })
    {
        std::vector<float> withPeriods;
        runPeriods (type, 12.0f, 12.0f, 0, 50, withPeriods);
        own::Distortion d;
        d.prepare (aliasfx::kFs);
        own::Distortion::Params p = aliasfx::params (type, 12.0f);
        p.os2 = true;
        d.set (p);
        std::vector<float> l (period * 50), r (period * 50);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.6f * std::sin (2.0f * 3.14159265f * 440.0f * static_cast<float> (i) / static_cast<float> (aliasfx::kFs));
        d.process (l.data(), r.data(), static_cast<int> (l.size()));
        CHECK (aliasfx::same (l, withPeriods));
    }
}

TEST (own_alias_dist_os2_is_block_size_independent_and_reset_is_complete)
{
    synthyboy::aa::setOff (0);
    std::vector<float> in (4096);
    aliasfx::Lcg rng (987654321u);
    for (auto& v : in)
        v = 0.8f * rng.sample();

    for (const int type : { 0, 1, 4, 5, 9, 12 })
        for (const bool fast : { false, true })
        {
            const std::vector<float> a = runOs2 (type, 12.0f, fast, true, in, 0.5f, 32);
            const std::vector<float> b = runOs2 (type, 12.0f, fast, true, in, 0.5f, 7);
            const std::vector<float> c = runOs2 (type, 12.0f, fast, true, in, 0.5f, 128);
            CHECK (aliasfx::same (a, b));
            CHECK (aliasfx::same (a, c));

            own::Distortion warm, fresh;
            warm.prepare (aliasfx::kFs);
            fresh.prepare (aliasfx::kFs);
            warm.fast = fresh.fast = fast;
            own::Distortion::Params p = aliasfx::params (type, 12.0f);
            p.mix = 0.5f;
            p.os2 = true;
            warm.set (p);
            fresh.set (p);
            std::vector<float> w1 (in.begin(), in.begin() + 1000), w2 (w1);
            warm.process (w1.data(), w2.data(), 1000);
            warm.reset();
            std::vector<float> l1 (in.begin() + 1000, in.begin() + 2000), r1 (l1), l2 (l1), r2 (l1);
            warm.process (l1.data(), r1.data(), 1000);
            fresh.process (l2.data(), r2.data(), 1000);
            CHECK (aliasfx::same (l1, l2));
            CHECK (aliasfx::same (r1, r2));
        }
}

TEST (own_alias_dist_os2_off_is_bit_identical_and_prints_checksums)
{
    synthyboy::aa::setOff (0);
    // Exact DIST-CHECKSUM lines matching core/tests/test_own_fx.cpp with os2 = false
    for (const int t : { 0, 4, 6 })
    {
        own::Distortion dist;
        dist.prepare (48000.0);
        own::Distortion::Params p;
        p.type = t;
        p.driveDb = 12.0f;
        p.mix = 1.0f;
        p.cutoff = 4000.0f;
        p.resonance = 0.5f;
        p.blend = 0.0f;
        p.order = 1;
        p.os2 = false;
        dist.set (p);

        const int n = 512;
        std::vector<float> bufL (n), bufR (n);
        aliasfx::Lcg rng (123456789u);
        for (int i = 0; i < n; ++i)
        {
            bufL[i] = rng.sample();
            bufR[i] = rng.sample();
        }
        dist.process (bufL.data(), bufR.data(), n);
        double sumAbs = 0.0;
        for (int i = 0; i < n; ++i)
            sumAbs += static_cast<double> (std::fabs (bufL[i])) + static_cast<double> (std::fabs (bufR[i]));
        std::printf ("DIST-CHECKSUM type=%d sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     t, sumAbs,
                     static_cast<double> (bufL[n - 8]), static_cast<double> (bufL[n - 7]),
                     static_cast<double> (bufL[n - 6]), static_cast<double> (bufL[n - 5]),
                     static_cast<double> (bufL[n - 4]), static_cast<double> (bufL[n - 3]),
                     static_cast<double> (bufL[n - 2]), static_cast<double> (bufL[n - 1]));
    }

    // All types with os2 = false vs default Params (must be bit-identical)
    for (int t = 0; t <= 13; ++t)
    {
        own::Distortion dDefault, dOff;
        dDefault.prepare (aliasfx::kFs);
        dOff.prepare (aliasfx::kFs);
        const own::Distortion::Params pDef = aliasfx::params (t, 12.0f);
        own::Distortion::Params pOff = pDef;
        pOff.os2 = false;
        dDefault.set (pDef);
        dOff.set (pOff);

        const int n = 512;
        std::vector<float> l1 (n), r1 (n), l2 (n), r2 (n);
        aliasfx::Lcg rng (123456789u);
        for (int i = 0; i < n; ++i)
        {
            l1[i] = l2[i] = rng.sample();
            r1[i] = r2[i] = rng.sample();
        }
        dDefault.process (l1.data(), r1.data(), n);
        dOff.process (l2.data(), r2.data(), n);
        CHECK (aliasfx::same (l1, l2));
        CHECK (aliasfx::same (r1, r2));

        double sumAbs = 0.0;
        for (int i = 0; i < n; ++i)
            sumAbs += static_cast<double> (std::fabs (l2[i])) + static_cast<double> (std::fabs (r2[i]));
        std::printf ("DIST-OS2-CHECKSUM type=%d os2=0 sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     t, sumAbs,
                     static_cast<double> (l2[n - 8]), static_cast<double> (l2[n - 7]),
                     static_cast<double> (l2[n - 6]), static_cast<double> (l2[n - 5]),
                     static_cast<double> (l2[n - 4]), static_cast<double> (l2[n - 3]),
                     static_cast<double> (l2[n - 2]), static_cast<double> (l2[n - 1]));
    }
}
