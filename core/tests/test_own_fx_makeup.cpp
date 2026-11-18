// SynthyBoy - own engine tests: the tier level makeup of the effects a quality bypasses (Quality::fxMakeup, core/engine-own/fx/fx_makeup.h): nothing changes while no effect that is on is bypassed (bit
// equal), and a bypassed effect leaves its loudness behind within the stated tolerance.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "own_heavy_patch.h"
#include "synthyboy/engine.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
using Patch = void (*) (IEngine&);

void heavy (IEngine& e) { bench::ownHeavyPatch (e); } // 3 oscillators x 7 unison voices, a morph (osc 1), a warp (osc 2), 2 filters, LFOs, the nine effects

/** One saw through the ladder filter with resonance and an envelope on the cutoff: no unison, warp, morph or effect, so only the Math and Control groups can act. */
void plain (IEngine& e)
{
    bench::ownSet (e, "master.polyphony", 8.0f);
    bench::ownSet (e, "filter1.model", 2.0f);
    bench::ownSet (e, "filter1.cutoff", 1200.0f);
    bench::ownSet (e, "filter1.resonance", 30.0f);
}

/** The plain patch with a sync warp on its oscillator: the Warp group acts. */
void warped (IEngine& e)
{
    plain (e);
    bench::ownSet (e, "osc1.warp.type", 1.0f);
    bench::ownSet (e, "osc1.warp.amount", 35.0f);
}

/** The plain patch with a spectral morph: the Morph group acts. */
void morphed (IEngine& e)
{
    plain (e);
    bench::ownSet (e, "osc1.wavetable", 5.0f);
    bench::ownSet (e, "osc1.morph.type", 1.0f);
    bench::ownSet (e, "osc1.morph.amount", 50.0f);
}

/** The plain patch with five unison voices: a profile with unison 3 plays three of them. */
void unison5 (IEngine& e)
{
    plain (e);
    bench::ownSet (e, "osc1.unison.voices", 5.0f);
    bench::ownSet (e, "osc1.unison.detune", 25.0f);
}

Quality quality (uint32_t fx, bool makeup)
{
    Quality q;
    q.maxVoices = 8;
    q.maxUnison = 16;
    q.oversampling = 2;
    q.fxMask = fx;
    q.fxMakeup = makeup;
    return q;
}

std::vector<float> render (Patch patch, const Quality& q)
{
    auto e = createEngine (EngineKind::own);
    patch (*e);
    e->prepare (44100.0, 128, q);
    std::vector<float> out, l (128), r (128);
    for (int b = 0; b < 1000; ++b)
    {
        std::vector<Event> ev;
        if (b == 0)
            for (int k = 0; k < 4; ++k)
            {
                Event n;
                n.type = Event::NoteOn;
                n.key = static_cast<int16_t> (48 + k * 5);
                n.value = 0.7f;
                n.frame = 0;
                ev.push_back (n);
            }
        e->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 128);
        if (b >= 500) // the second half: the sustained part, after the effects have settled (a flanger, hyper or reverb takes a second or more)
        {
            out.insert (out.end(), l.begin(), l.end());
            out.insert (out.end(), r.begin(), r.end());
        }
    }
    return out;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

double rmsDb (const std::vector<float>& a)
{
    double s = 0.0;
    for (const float x : a)
        s += static_cast<double> (x) * x;
    return 10.0 * std::log10 (s / static_cast<double> (a.size()) + 1e-30);
}

/** The loudness figure of the factory preset levels (scripts/loudness.py): the loudest 100 ms of the K-weighted signal (BS.1770 shelf and high pass, bilinear transform for 44.1 kHz), dB.
    `x` holds blocks of 128 left then 128 right samples. */
double loudestDb (const std::vector<float>& x)
{
    struct Bq { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double run (double v) { const double y = b0 * v + z1; z1 = b1 * v - a1 * y + z2; z2 = b2 * v - a2 * y; return y; } };
    const double sr = 44100.0, pi = 3.14159265358979323846;
    double g = 3.99984385397, q = 0.7071752369554193, fc = 1681.9744509555319;
    double k = std::tan (pi * fc / sr), vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416), a0 = 1 + k / q + k * k;
    Bq shelf[2] = { { (vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0 }, {} };
    shelf[1] = shelf[0];
    const double q2 = 0.5003270373253953, fc2 = 38.13547087613982;
    k = std::tan (pi * fc2 / sr);
    a0 = 1 + k / q2 + k * k;
    Bq hp[2] = { { 1.0, -2.0, 1.0, 2 * (k * k - 1) / a0, (1 - k / q2 + k * k) / a0 }, {} };
    hp[1] = hp[0];
    std::vector<double> p;
    for (size_t blk = 0; blk + 256 <= x.size(); blk += 256)
        for (int i = 0; i < 128; ++i)
        {
            const double l = hp[0].run (shelf[0].run (x[blk + static_cast<size_t> (i)])), r = hp[1].run (shelf[1].run (x[blk + 128 + static_cast<size_t> (i)]));
            p.push_back (l * l + r * r);
        }
    const size_t w = 4410;
    double sum = 0, best = 0;
    for (size_t i = 0; i < p.size(); ++i)
    {
        sum += p[i];
        if (i >= w) sum -= p[i - w];
        if (i + 1 >= w) best = std::max (best, sum / static_cast<double> (w));
    }
    return 10.0 * std::log10 (best + 1e-30);
}

void base (IEngine& e)
{
    bench::ownSet (e, "master.polyphony", 8.0f);
    bench::ownSet (e, "osc1.unison.voices", 3.0f);
    bench::ownSet (e, "osc1.unison.detune", 20.0f);
}
void withReverb (IEngine& e) { base (e); bench::ownSet (e, "reverb.on", 1.0f); bench::ownSet (e, "reverb.mix", 70.0f); }
void withFlanger (IEngine& e) { base (e); bench::ownSet (e, "flanger.on", 1.0f); bench::ownSet (e, "flanger.mix", 80.0f); bench::ownSet (e, "flanger.feedback", 70.0f); }
void withHyper (IEngine& e)
{
    base (e);
    bench::ownSet (e, "hyperdim.on", 1.0f);
    bench::ownSet (e, "hyperdim.mix", 60.0f);
    bench::ownSet (e, "hyperdim.unison", 5.0f);
    bench::ownSet (e, "hyperdim.dim_mix", 40.0f);
}
void withBode (IEngine& e)
{
    base (e);
    bench::ownSet (e, "bode.on", 1.0f);
    bench::ownSet (e, "bode.mix", 100.0f);
    bench::ownSet (e, "bode.shift", 50.0f);
    bench::ownSet (e, "bode.range", 400.0f);
    bench::ownSet (e, "bode.feed", 40.0f);
}
/** The routing of Frozen Pluck Drone: oscillator 1 plays in Main and sends a copy (55 %) to bus 1, whose rack holds a reverb with mix 100 (wet only). */
void withBusReverb (IEngine& e)
{
    base (e);
    bench::ownSet (e, "osc1.bus1_send", 55.0f);
    bench::ownSet (e, "reverb.rack", 1.0f);
    bench::ownSet (e, "reverb.on", 1.0f);
    bench::ownSet (e, "reverb.mix", 100.0f);
}
} // namespace

TEST (tier_makeup_full_quality_is_bit_identical)
{
    // no effect that is on is bypassed: the makeup changes not one sample (the heavy patch has reverb, flanger and the rest on)
    for (Patch p : { Patch (withReverb), Patch (withFlanger), Patch (withHyper), Patch (withBode), Patch (heavy) })
        CHECK (same (render (p, quality (kFxAll, true)), render (p, quality (kFxAll, false))));
    // a mask that bypasses an effect which is off in the preset changes nothing either
    CHECK (same (render (withReverb, quality (kFxAll & ~kFxFlanger, true)), render (withReverb, quality (kFxAll & ~kFxFlanger, false))));
}

// The figure is the loudest 100 ms of the K-weighted signal (what the factory levels use), dB; the tolerance is 1 dB for every model, on sustained saw notes through the engine.
TEST (tier_makeup_bus_copy_of_main)
{
    // a wet only effect on a bus that carries a copy of a source that also plays in Main: bypassed, the bus would add up coherently with Main; the makeup of the bus must give the level of the
    // active effect (here a reverb: its wet part is unrelated to Main) within 1 dB
    const double full = loudestDb (render (withBusReverb, quality (kFxAll, true)));
    const double off = loudestDb (render (withBusReverb, quality (kFxAll & ~kFxReverb, false)));
    const double fixed = loudestDb (render (withBusReverb, quality (kFxAll & ~kFxReverb, true)));
    std::printf ("    bus reverb: full %.2f dB, bypassed %.2f dB (%+.2f), with makeup %.2f dB (%+.2f)\n", full, off, off - full, fixed, fixed - full);
    CHECK (std::fabs (fixed - full) <= 1.0);
    CHECK (std::fabs (fixed - full) < std::fabs (off - full));
}

TEST (tier_makeup_leaves_the_loudness_of_the_bypassed_effect)
{
    struct Case { Patch patch; uint32_t bit; const char* name; double tol; };
    const Case cases[] = { { withReverb, kFxReverb, "reverb", 1.0 }, { withFlanger, kFxFlanger, "flanger", 1.0 }, { withHyper, kFxHyperDim, "hyper", 1.0 }, { withBode, kFxBode, "bode", 1.0 } };
    for (const Case& c : cases)
    {
        const double full = loudestDb (render (c.patch, quality (kFxAll, true)));
        const double off = loudestDb (render (c.patch, quality (kFxAll & ~c.bit, false)));
        const double fixed = loudestDb (render (c.patch, quality (kFxAll & ~c.bit, true)));
        std::printf ("    %s: full %.2f dB, bypassed %.2f dB (%+.2f), with makeup %.2f dB (%+.2f)\n", c.name, full, off, off - full, fixed, fixed - full);
        CHECK (std::fabs (fixed - full) <= c.tol);
        CHECK (std::fabs (off - full) <= c.tol || std::fabs (fixed - full) < std::fabs (off - full)); // (an effect that changes the level by less than the tolerance needs no makeup)
    }
}
