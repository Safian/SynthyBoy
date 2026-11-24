// SynthyBoy - own engine tests: the aliasing baseline recorder (docs/plans/distortion-antialiasing.md task A2, sections 2 M-A and M-B).
// It prints the aliasing level of the distortion effect and of rendered notes with the legacy path (aa::kAll) and asserts only that the meter works. The build worker runs it once on the
// unchanged tree and once after the last batch and stores both outputs in docs/plans/distortion-antialiasing-baseline.txt.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "alias_measure.h"
#include "fx/fx_effects.h"
#include "synthyboy/aa_switch.h"
#include "synthyboy/engine.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kFs = 44100.0;
constexpr size_t kN = 65536;
constexpr size_t kBin = 1487;

double effectF0()
{
    return static_cast<double> (kBin) * kFs / static_cast<double> (kN);
}

// The skeleton of the plan 4.0: a coherent sine of peak 0.5 through Distortion::process in chunks of 32; returns the meter result of the last kN samples.
aliasm::Result measureEffect (int type, float driveDb, bool fast)
{
    const size_t total = 16384 + kN;
    std::vector<float> l (total), r;
    for (size_t i = 0; i < total; ++i)
        l[i] = static_cast<float> (0.5 * std::sin (6.283185307179586 * static_cast<double> ((kBin * i) % kN) / static_cast<double> (kN)));
    r = l;
    own::Distortion d;
    d.prepare (kFs);
    d.fast = fast;
    own::Distortion::Params p;
    p.type = type;
    p.driveDb = driveDb;
    p.mix = 1.0f;
    p.order = 0;
    d.set (p);
    for (size_t pos = 0; pos < total; pos += 32)
        d.process (l.data() + pos, r.data() + pos, static_cast<int> (std::min<size_t> (32, total - pos)));
    return aliasm::measure (l.data() + 16384, kN, effectF0(), kFs);
}

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

struct WarpCase
{
    const char* name;
    int type;
    float amount;
};

// One held note, the left channel (the skeleton of the plan 4.0). 2.0 s are rendered (the plan says 1.5 s, but 0.5 s + 65536 samples need 87586 samples = 1.99 s at 44.1 kHz).
aliasm::Result measureNote (int note, const WarpCase& w, Tier tier)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kFs, 256, qualityForTier (tier));
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "env1.attack", 0.0f);
    set (*e, "osc1.random_phase", 0.0f);
    set (*e, "osc1.level", 100.0f);
    set (*e, "osc1.wavetable", 1.0f);
    set (*e, "osc1.unison.voices", 1.0f);
    set (*e, "osc1.warp.type", static_cast<float> (w.type));
    set (*e, "osc1.warp.amount", w.amount);
    const size_t total = static_cast<size_t> (2.0 * kFs);
    std::vector<float> l (total), r (total);
    for (size_t pos = 0; pos < total; pos += 256)
    {
        const size_t n = std::min<size_t> (256, total - pos);
        Event ev[1];
        int ne = 0;
        if (pos == 0)
            ev[ne++] = sbtest::noteOn (0, note, 1.0f);
        e->process (ev, ne, l.data() + pos, r.data() + pos, static_cast<int> (n));
    }
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    return aliasm::measure (l.data() + 22050, kN, f0, kFs, 6.0);
}
} // namespace

TEST (own_alias_baseline_effect_grid)
{
    aa::setOff (aa::kAll); // the legacy path
    const int types[] = { 0, 1, 2, 3, 6, 7, 8, 9, 10, 11 };
    const float drives[] = { 0.0f, 12.0f, 24.0f };
    double hardClipAt24 = -300.0;
    for (const int t : types)
        for (const float dr : drives)
            for (const bool fast : { false, true })
            {
                const aliasm::Result m = measureEffect (t, dr, fast);
                std::printf ("ALIAS-BASELINE fx type=%d drive=%g fast=%d alias=%.2f harm=%.2f\n", t, static_cast<double> (dr), fast ? 1 : 0, m.aliasDb, m.harmDb);
                CHECK (std::isfinite (m.aliasDb) && std::isfinite (m.harmDb));
                // not -140: a law that does not bend the signal (Hard Clip at drive 0 with a peak of 0.5 is a bit exact pass-through) measures the float rounding floor of the sine, -153.8 dB;
                // -160 only catches a meter that returns garbage (the meter itself is guarded by hardClipAt24 below)
                CHECK (m.aliasDb > -160.0);
                if (t == 1 && dr == 24.0f && !fast)
                    hardClipAt24 = m.aliasDb;
            }
    CHECK (hardClipAt24 > -90.0); // a meter that finds no aliasing in a hard clip is broken
    aa::setOff (0);
}

TEST (own_alias_baseline_note_grid)
{
    aa::setOff (aa::kAll);
    const int notes[] = { 60, 72, 84, 96, 100 };
    const WarpCase warps[] = { { "sync", 1, 35.0f },           { "sync", 1, 15.0f },           { "dist_hard_clip", 29, 50.0f }, { "dist_linear_fold", 32, 50.0f }, { "dist_rectify", 36, 50.0f },
                               { "dist_soft_clip", 28, 50.0f }, { "dist_zero_square", 34, 50.0f }, { "quantize", 3, 50.0f },        { "none", 0, 0.0f } };
    for (const int note : notes)
        for (const WarpCase& w : warps)
            for (const Tier tier : { Tier::low, Tier::high })
            {
                const aliasm::Result m = measureNote (note, w, tier);
                std::printf ("ALIAS-BASELINE osc note=%d warp=%s amount=%g quality=%s alias=%.2f harm=%.2f\n", note, w.name, static_cast<double> (w.amount),
                             tier == Tier::low ? "low" : "high", m.aliasDb, m.harmDb);
                CHECK (std::isfinite (m.aliasDb) && std::isfinite (m.harmDb));
                CHECK (m.aliasDb > -140.0);
            }
    aa::setOff (0);
}
