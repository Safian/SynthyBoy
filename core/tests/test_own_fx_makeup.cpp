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
    for (int b = 0; b < 160; ++b)
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
        if (b >= 80) // the second half: the sustained part, after the effects have settled
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
} // namespace

TEST (tier_makeup_full_quality_is_bit_identical)
{
    // no effect that is on is bypassed: the makeup changes not one sample (the heavy patch has reverb, flanger and the rest on)
    for (Patch p : { Patch (withReverb), Patch (withFlanger), Patch (withHyper), Patch (withBode), Patch (heavy) })
        CHECK (same (render (p, quality (kFxAll, true)), render (p, quality (kFxAll, false))));
    // a mask that bypasses an effect which is off in the preset changes nothing either
    CHECK (same (render (withReverb, quality (kFxAll & ~kFxFlanger, true)), render (withReverb, quality (kFxAll & ~kFxFlanger, false))));
}

// The tolerances are those of the models on this test program, four sustained saw notes: a tonal program, where the models (calibrated on broadband noise and on the factory presets) leave
// more error than on a pad (hyper -1.9 dB, bode +1.2 dB left of 5.5 and 1.3): documented in docs/release/TIER-LEVEL-COMPENSATION.md section 6.
TEST (tier_makeup_leaves_the_loudness_of_the_bypassed_effect)
{
    struct Case { Patch patch; uint32_t bit; const char* name; double tol; };
    const Case cases[] = { { withReverb, kFxReverb, "reverb", 2.0 }, { withFlanger, kFxFlanger, "flanger", 1.5 }, { withHyper, kFxHyperDim, "hyper", 2.5 }, { withBode, kFxBode, "bode", 1.5 } };
    for (const Case& c : cases)
    {
        const double full = rmsDb (render (c.patch, quality (kFxAll, true)));
        const double off = rmsDb (render (c.patch, quality (kFxAll & ~c.bit, false)));
        const double fixed = rmsDb (render (c.patch, quality (kFxAll & ~c.bit, true)));
        std::printf ("    %s: full %.2f dB, bypassed %.2f dB (%+.2f), with makeup %.2f dB (%+.2f)\n", c.name, full, off, off - full, fixed, fixed - full);
        CHECK (std::fabs (fixed - full) <= c.tol);
        CHECK (std::fabs (fixed - full) <= std::fabs (off - full) + 0.05);
    }
}
