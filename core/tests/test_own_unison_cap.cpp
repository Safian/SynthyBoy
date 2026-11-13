// SynthyBoy - own engine tests: the unison cap of tier low (OwnEngine::setLowTierUnisonCap).
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
std::unique_ptr<IEngine> make (int patchUnison, int cap)
{
    auto e = createEngine (EngineKind::own);
    bench::ownHeavyPatch (*e); // 3 oscillators with 7 unison voices, morph, warp, 2 filters, effects
    for (int i = 1; i <= 3; ++i)
        bench::ownSet (*e, ("osc" + std::to_string (i) + ".unison.voices").c_str(), static_cast<float> (patchUnison));
    setOwnLowTierUnisonCap (*e, cap);
    return e;
}

/** A chord with a retrigger and a release, 60 blocks of 128 frames; the samples are appended to out. */
void play (IEngine& e, std::vector<float>& out, int blocks = 60)
{
    std::vector<float> l (128), r (128);
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<Event> ev;
        if (b == 0 || b == 30)
            for (int k = 0; k < 4; ++k)
            {
                Event n;
                n.type = Event::NoteOn;
                n.key = static_cast<int16_t> (48 + k * 5 + (b == 30 ? 2 : 0));
                n.value = 0.7f;
                n.frame = k * 3;
                ev.push_back (n);
            }
        if (b == 20 || b == 50)
        {
            Event o;
            o.type = Event::AllNotesOff;
            o.channel = -1;
            ev.push_back (o);
        }
        e.process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 128);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

bool finite (const std::vector<float>& a)
{
    for (const float x : a)
        if (!std::isfinite (x))
            return false;
    return true;
}
} // namespace

TEST (unison_cap_at_tier_low_sounds_like_a_patch_with_that_many_voices)
{
    // tier low also caps the unison at 4 (the quality), so 1 .. 4 are the values that can be compared with a patch value
    for (int n = 1; n <= 4; ++n)
    {
        auto capped = make (7, n);
        auto plain = make (n, 0);
        const Quality low = qualityForTier (Tier::low);
        capped->prepare (44100.0, 128, low);
        plain->prepare (44100.0, 128, low);
        std::vector<float> a, b;
        play (*capped, a);
        play (*plain, b);
        CHECK (finite (a));
        CHECK (same (a, b));
    }
}

TEST (unison_cap_does_not_apply_at_medium_and_high)
{
    for (const Tier tier : { Tier::medium, Tier::high })
    {
        auto capped = make (7, 2);
        auto plain = make (7, 0);
        capped->prepare (44100.0, 128, qualityForTier (tier));
        plain->prepare (44100.0, 128, qualityForTier (tier));
        std::vector<float> a, b;
        play (*capped, a);
        play (*plain, b);
        CHECK (same (a, b));
    }
}

TEST (unison_cap_leaves_parameters_and_state_alone)
{
    auto e = make (7, 2);
    e->prepare (44100.0, 128, qualityForTier (Tier::low));
    std::vector<float> out;
    play (*e, out, 10);
    auto ref = make (7, 0);
    ParamValues va = ParamValues::defaults(), vb = ParamValues::defaults();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& info = paramTable()[i];
        CHECK_EQ (e->getParam (info.id), ref->getParam (info.id));
        va.plain[i] = e->getParam (info.id);
        vb.plain[i] = ref->getParam (info.id);
    }
    CHECK_EQ (saveState (va), saveState (vb));
    CHECK_EQ (e->getParam (findParamByKey ("osc1.unison.voices")->id), 7.0f);
}

TEST (unison_cap_follows_a_tier_change_while_playing)
{
    auto e = make (7, 3);
    e->prepare (44100.0, 128, qualityForTier (Tier::low));
    std::vector<float> out;
    play (*e, out, 40);
    e->setQuality (qualityForTier (Tier::high)); // as the shell does it: lock free, taken over at the next block
    play (*e, out, 40);
    e->setQuality (qualityForTier (Tier::low));
    play (*e, out, 40); // back at tier low with notes sounding
    CHECK (finite (out));
    float peak = 0.0f, step = 0.0f;
    for (size_t i = 1; i < out.size(); ++i)
    {
        peak = std::max (peak, std::fabs (out[i]));
        if ((i % 256) != 0)
            step = std::max (step, std::fabs (out[i] - out[i - 1]));
    }
    CHECK (peak < 4.0f);
    CHECK (step < 1.5f); // no jump of the state: the per sample step stays in the range the patch has anyway

    // Nothing of the tier excursion stays behind: after reset () the engine sounds exactly like one that only ever ran at tier low with the cap (a reset engine does not
    // equal a brand new one in the last bits of the effect tails, that is the engine's reset () and independent of the cap, so the reference has the same history of
    // notes and the same reset)
    auto x = make (7, 3);
    x->prepare (44100.0, 128, qualityForTier (Tier::low));
    std::vector<float> warm;
    play (*x, warm, 40);
    x->setQuality (qualityForTier (Tier::high));
    play (*x, warm, 40);
    x->setQuality (qualityForTier (Tier::low));
    x->reset();
    x->restartEffectPhases();
    auto fresh = make (7, 3);
    fresh->prepare (44100.0, 128, qualityForTier (Tier::low));
    play (*fresh, warm, 40);
    fresh->reset();
    fresh->restartEffectPhases();
    std::vector<float> a, b;
    play (*x, a, 30);
    play (*fresh, b, 30);
    CHECK (finite (a));
    if (!same (a, b))
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
            {
                std::printf ("first difference at sample %zu (block %zu, %s): %.9g vs %.9g\n", i % 256, i / 256, (i % 256) < 128 ? "L" : "R", a[i], b[i]);
                break;
            }
    CHECK (same (a, b));
}

TEST (unison_cap_setter_is_for_the_own_engine)
{
    auto e = createEngine (EngineKind::own);
    CHECK (setOwnLowTierUnisonCap (*e, 5));
}

TEST (morph_spread_switch_of_tier_low)
{
    auto run = [] (Tier tier, int keep)
    {
        auto e = make (7, 4);
        bench::ownSet (*e, "osc1.morph.spread", 80.0f);
        if (keep >= 0)
            setOwnLowTierMorphSpread (*e, keep != 0);
        e->prepare (44100.0, 128, qualityForTier (tier));
        std::vector<float> out;
        play (*e, out);
        return out;
    };
    const auto def = run (Tier::low, -1), keepOn = run (Tier::low, 1), keepOff = run (Tier::low, 0);
    CHECK (finite (keepOff));
    CHECK (same (def, keepOn));      // the default keeps the spread
    CHECK (!same (keepOn, keepOff)); // sharing one cycle changes the sound of a patch with a morph spread
    CHECK (same (run (Tier::high, 1), run (Tier::high, 0))); // medium / high never share
}
