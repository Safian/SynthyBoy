// SynthyBoy - own engine tests: an effect module whose mix is exactly 0 is not processed (FxModule::process, economy group Control): the output is the input, the
// module costs nothing while its state is silent, a state with a tail is flushed first, and opening the mix starts the effect from silence without a burst of old sound.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "fx/fx_module.h"
#include "synthyboy/params.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
struct Case { int module; const char* name; const char* mixKey; float flushSeconds; };
// the modules that report their mix: chorus, delay, distortion, the effect filter, flanger, phaser, reverb
const Case kCases[] = { { 0, "chorus", "chorus.mix", 1.0f }, { 2, "delay", "delay.mix", 20.0f }, { 3, "distortion", "distortion.mix", 1.0f }, { 5, "fxfilter", "fxfilter.mix", 1.0f },
                        { 6, "flanger", "flanger.mix", 1.0f }, { 7, "phaser", "phaser.mix", 1.0f }, { 8, "reverb", "reverb.mix", 20.0f } };
constexpr int kP = kControlPeriod;
constexpr double kSr = 44100.0;

std::vector<float> defaults()
{
    std::vector<float> b (paramCount());
    for (size_t i = 0; i < paramCount(); ++i)
        b[i] = paramTable()[i].def;
    return b;
}

/** A module with its own parameter array, run period by period on a test signal (two detuned tones with a slow swell: never exactly periodic in a control period). */
struct Rig
{
    std::unique_ptr<FxModule> m;
    std::vector<float> base, delta;
    FxFrame frame;
    long long pos = 0;
    int mixIdx = -1;
    float l[kP] = {}, r[kP] = {}, inL[kP] = {}, inR[kP] = {};

    Rig (const Case& c, bool muteSkip, float mixPercent)
    {
        m = makeFxModule (c.module);
        m->prepare (kSr, kP);
        base = defaults();
        delta.assign (paramCount(), 0.0f);
        mixIdx = fxModuleKeyIndex (c.mixKey);
        base[static_cast<size_t> (mixIdx)] = mixPercent;
        frame.muteSkip = muteSkip;
    }
    void setMix (float percent) { base[static_cast<size_t> (mixIdx)] = percent; }
    /** One control period; silent = the input is zeros. */
    void step (bool silent = false)
    {
        for (int i = 0; i < kP; ++i)
        {
            const double t = static_cast<double> (pos + i) / kSr;
            const float x = silent ? 0.0f : static_cast<float> (0.4 * std::sin (2.0 * 3.14159265358979 * 220.0 * t) * (0.6 + 0.4 * std::sin (2.0 * 3.14159265358979 * 0.7 * t))
                                                                 + 0.2 * std::sin (2.0 * 3.14159265358979 * 331.0 * t));
            inL[i] = l[i] = x;
            inR[i] = r[i] = silent ? 0.0f : 0.8f * x;
        }
        pos += kP;
        m->update (frame, base.data(), delta.data());
        m->process (l, r, kP, 0, kP);
    }
    float peak() const
    {
        float p = 0.0f;
        for (int i = 0; i < kP; ++i)
            p = std::max (p, std::max (std::fabs (l[i]), std::fabs (r[i])));
        return p;
    }
    bool outputIsInput() const
    {
        for (int i = 0; i < kP; ++i)
            if (l[i] != inL[i] || r[i] != inR[i]) // (values: +0 and -0 are the same sample)
                return false;
        return true;
    }
};

bool sameOutput (const Rig& a, const Rig& b)
{
    for (int i = 0; i < kP; ++i)
        if (a.l[i] != b.l[i] || a.r[i] != b.r[i])
            return false;
    return true;
}

float difference (const Rig& a, const Rig& b)
{
    float d = 0.0f;
    for (int i = 0; i < kP; ++i)
        d = std::max (d, std::max (std::fabs (a.l[i] - b.l[i]), std::fabs (a.r[i] - b.r[i])));
    return d;
}
} // namespace

TEST (a_module_with_mix_zero_from_the_start_is_skipped_and_its_output_is_its_input)
{
    for (const Case& c : kCases)
    {
        Rig off (c, false, 0.0f), on (c, true, 0.0f);
        bool same = true, dry = true, dryOff = true;
        int skipped = 0;
        for (int p = 0; p < 400; ++p)
        {
            off.step();
            on.step();
            same = same && sameOutput (off, on);
            dry = dry && on.outputIsInput();
            dryOff = dryOff && off.outputIsInput(); // the premise: the module that is run puts out its input at mix 0 as well
            skipped += on.m->skippedNow() ? 1 : 0;
            CHECK (! off.m->skippedNow());
        }
        std::printf ("    %-10s skipped in %d of 400 periods\n", c.name, skipped);
        CHECK (dryOff);
        CHECK (same);
        CHECK (dry);
        CHECK (skipped == 400); // never fed: silent from the first period on (the period before the first counts as mix 0 only when the module was made with it)
    }
}

TEST (opening_the_mix_starts_from_silence_like_a_module_that_heard_nothing_before)
{
    // skipped for 2 s on a signal, then the mix goes to 50 %: the same output as a module that ran all the time but was fed silence until then (its state is empty,
    // its free running phase is where the skipped one's is); and with a silent input after the opening nothing comes out at all
    for (const Case& c : kCases)
    {
        Rig skippedRig (c, true, 0.0f), fedSilence (c, false, 0.0f);
        for (int p = 0; p < 2756; ++p)
        {
            skippedRig.step();
            fedSilence.step (true);
            fedSilence.pos = skippedRig.pos;
        }
        CHECK (skippedRig.m->skippedNow());
        skippedRig.setMix (50.0f);
        fedSilence.setMix (50.0f);
        float worst = 0.0f, loudest = 0.0f;
        for (int p = 0; p < 1378; ++p)
        {
            skippedRig.step();
            fedSilence.step();
            fedSilence.pos = skippedRig.pos;
            worst = std::max (worst, difference (skippedRig, fedSilence));
            loudest = std::max (loudest, skippedRig.peak());
            CHECK (! skippedRig.m->skippedNow());
        }
        std::printf ("    %-10s after the opening: largest difference to the module fed silence before %.2e (peak %.3f)\n", c.name, static_cast<double> (worst), static_cast<double> (loudest));
        CHECK (worst <= 1.0e-6f);
        CHECK (loudest < 4.0f);

        Rig quiet (c, true, 0.0f);
        for (int p = 0; p < 2756; ++p)
            quiet.step();
        quiet.setMix (100.0f);
        float out = 0.0f;
        for (int p = 0; p < 1378; ++p)
        {
            quiet.step (true);
            out = std::max (out, quiet.peak());
        }
        CHECK (out <= 1.0e-6f); // no old sound in the module
    }
}

TEST (a_module_with_a_tail_is_flushed_before_it_is_skipped_and_wakes_without_old_sound)
{
    for (const Case& c : kCases)
    {
        Rig off (c, false, 60.0f), on (c, true, 60.0f);
        for (int p = 0; p < 700; ++p) // half a second at 60 %: echoes and tails in the state
        {
            off.step();
            on.step();
            CHECK (sameOutput (off, on));
        }
        off.setMix (0.0f);
        on.setMix (0.0f);
        // from the second period at 0 the module is muted but not silent: it is flushed, the signal passes untouched meanwhile
        bool same = true, dry = true;
        int firstSkipped = -1;
        const int limit = static_cast<int> ((c.flushSeconds + 25.0f) * kSr / kP);
        for (int p = 0; p < limit && firstSkipped < 0; ++p)
        {
            off.step();
            on.step();
            same = same && sameOutput (off, on);
            if (p >= 1)
                dry = dry && on.outputIsInput();
            if (on.m->skippedNow())
                firstSkipped = p;
        }
        const double seconds = firstSkipped * kP / kSr;
        std::printf ("    %-10s flushed and skipped %.2f s after the mix went to 0\n", c.name, seconds);
        CHECK (same);
        CHECK (dry);
        CHECK (firstSkipped > 0);
        CHECK (seconds >= c.flushSeconds); // not before the tail was watched for the module's time
        // opened on a silent input: what is left of the old tail is below -120 dBFS
        on.setMix (100.0f);
        float out = 0.0f;
        for (int p = 0; p < 1378; ++p)
        {
            on.step (true);
            out = std::max (out, on.peak());
        }
        std::printf ("    %-10s peak after the opening on silence %.2e\n", c.name, static_cast<double> (out));
        CHECK (out <= 1.0e-6f);
    }
}

TEST (a_mix_that_is_modulated_or_not_reported_or_a_switched_off_rule_never_skips)
{
    for (const Case& c : kCases)
    {
        // the modulation moves the mix off 0: the module runs, and equals the module without the rule
        Rig off (c, false, 0.0f), on (c, true, 0.0f);
        off.delta[static_cast<size_t> (off.mixIdx)] = on.delta[static_cast<size_t> (on.mixIdx)] = 0.25f;
        bool same = true;
        int skipped = 0;
        for (int p = 0; p < 200; ++p)
        {
            off.step();
            on.step();
            same = same && sameOutput (off, on);
            skipped += on.m->skippedNow() ? 1 : 0;
        }
        CHECK (same);
        CHECK_EQ (skipped, 0);
        // a mix that returns to exactly 0 for single periods only (0, 30, 0, 30 ...) is never muted: the rule needs two periods at 0, and then a silent state
        Rig flip (c, true, 0.0f), flipOff (c, false, 0.0f);
        same = true;
        skipped = 0;
        for (int p = 0; p < 200; ++p)
        {
            flip.setMix (p % 2 == 0 ? 30.0f : 0.0f);
            flipOff.setMix (p % 2 == 0 ? 30.0f : 0.0f);
            flip.step();
            flipOff.step();
            same = same && sameOutput (flip, flipOff);
            skipped += flip.m->skippedNow() ? 1 : 0;
        }
        CHECK (same);
        CHECK_EQ (skipped, 0);
    }
    // the compressor (a module with latency that does not report its mix) is never skipped
    const Case comp { 1, "compressor", "compressor.mix", 1.0f };
    Rig r (comp, true, 0.0f);
    int skipped = 0;
    for (int p = 0; p < 200; ++p)
    {
        r.step();
        skipped += r.m->skippedNow() ? 1 : 0;
    }
    CHECK_EQ (skipped, 0);
}
