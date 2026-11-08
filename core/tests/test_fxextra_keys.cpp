// SynthyBoy - core tests: block B17 (compressor, delay, distortion, phaser, reverb extras; docs/ui-design/13-new-keys.md 4.17): the 14 keys,
// their positions, neutral defaults, destination numbers, the appended distortion types, the Kick pages and the X-Shaper curves of the
// content section.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>
#include <cstring>
#include <string>

#include "synthyboy/content_state.h"
#include "synthyboy/engine_caps.h"
#include "synthyboy/kick_pages.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
const ParamInfo& byKey (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    static const ParamInfo none {};
    return p != nullptr ? *p : none;
}

int positionOf (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? findParamIndex (p->id) : -1;
}

struct Key { const char* key; float def, min, max; bool mod; };
const Key kKeys[] = {
    { "compressor.limit", 0.0f, 0.0f, 1.0f, false },
    { "compressor.limit_latency_comp", 1.0f, 0.0f, 1.0f, false },
    { "compressor.crossover_low", 120.0f, 20.0f, 1000.0f, true },
    { "compressor.crossover_high", 2500.0f, 200.0f, 16000.0f, true },
    { "delay.hq", 0.0f, 0.0f, 1.0f, false },
    { "delay.offset", 1.0f, 0.25f, 4.0f, true },
    { "distortion.filter_keytrack", 0.0f, -100.0f, 100.0f, true },
    { "phaser.depth2", 0.0f, -48.0f, 48.0f, true },
    { "reverb.type", 0.0f, 0.0f, 5.0f, false },
    { "reverb.room", 2.0f, 0.0f, 4.0f, false },
    { "reverb.width", 100.0f, 0.0f, 200.0f, true },
    { "reverb.er_size", 50.0f, 0.0f, 100.0f, false },
    { "reverb.diff_a", 70.0f, 0.0f, 100.0f, true },
    { "reverb.diff_b", 60.0f, 0.0f, 100.0f, true },
};
} // namespace

TEST (b17_keys_are_appended_in_order_with_neutral_defaults)
{
    int position = 1544;
    int destination = 424;
    for (const Key& k : kKeys)
    {
        const auto& p = byKey (k.key);
        CHECK (p.key != nullptr && std::strcmp (p.key, k.key) == 0);
        if (! CHECK_EQ (positionOf (k.key), position))
            std::printf ("    %s\n", k.key);
        CHECK_EQ (p.def, k.def);
        CHECK_EQ (p.min, k.min);
        CHECK_EQ (p.max, k.max);
        CHECK_EQ ((p.flags & kParamModulatable) != 0, k.mod);
        CHECK ((p.flags & kParamAutomatable) != 0);
        if (k.mod)
            CHECK_EQ (static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (position))), destination++);
        else
            CHECK_EQ (static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (position))), 0);
        CHECK (requiredCaps (static_cast<size_t> (position)) != kCapNone);
        ++position;
    }
    CHECK_EQ (position, 1558);
    CHECK_EQ (destination, 432);
    CHECK_EQ (static_cast<int> (paramCount()), 1558);
    CHECK_EQ (modDestinationParamIndex (424), 1546);
    CHECK_EQ (modDestinationParamIndex (431), 1557);
    CHECK_EQ (positionOf ("splitter_ms.level"), 1483); // B12 is unchanged
    CHECK_EQ (positionOf ("fxfilter.clean_mode"), 1506); // B16 is unchanged
}

TEST (b17_choice_lists_and_names)
{
    const auto& type = byKey ("distortion.type");
    CHECK_EQ (choiceCount (type), static_cast<size_t> (14));
    const char* const names[] = { "Soft clip", "Hard clip", "Linear fold", "Sine fold", "Bit crush", "Down sample", "Tube", "Diode 1", "Diode 2",
                                  "Zero square", "Asym", "Rectify", "X-Shaper", "X-Shaper (Asym)" };
    for (size_t i = 0; i < 14; ++i)
        CHECK (std::strcmp (choiceName (type, i), names[i]) == 0);
    CHECK_EQ (kNumLegacyDistortionTypes, 6);
    CHECK_EQ (choiceCount (byKey ("reverb.type")), static_cast<size_t> (6));
    CHECK (std::strcmp (choiceName (byKey ("reverb.type"), 0), "Vital") == 0);
    CHECK (std::strcmp (choiceName (byKey ("reverb.type"), 5), "Basin") == 0);
    CHECK_EQ (choiceCount (byKey ("reverb.room")), static_cast<size_t> (5));
    CHECK (std::strcmp (choiceName (byKey ("reverb.room"), 0), "Space") == 0);
    CHECK (std::strcmp (choiceName (byKey ("reverb.room"), 4), "Box") == 0);
}

TEST (b17_kick_pages_reach_every_key)
{
    CHECK_EQ (kickPageCount(), static_cast<size_t> (115));
    const Page* pages = kickPageTable();
    for (const Key& k : kKeys)
    {
        bool found = false;
        for (size_t page = 0; page < kickPageCount() && ! found; ++page)
            for (size_t i = 0; i < pages[page].count; ++i)
                found = found || (pages[page].keys[i] != nullptr && std::strcmp (pages[page].keys[i], k.key) == 0);
        if (! CHECK (found))
            std::printf ("    %s\n", k.key);
    }
}

TEST (x_shaper_curves_round_trip_and_are_validated)
{
    StateContent c;
    c.shaperA = { ContentPoint (0.0f, 0.0f), ContentPoint (0.3f, 0.8f, -2.0f), ContentPoint (1.0f, 0.5f) };
    c.shaperB = { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 1.0f) };
    CHECK (! c.empty());
    CHECK (validateContent (c));

    const std::string text = saveState (ParamValues::defaults(), c);
    CHECK (text.find ("\"shaper_a\"") != std::string::npos && text.find ("\"shaper_b\"") != std::string::npos);
    StateContent back;
    ParamValues values;
    const auto r = loadStateWithContent (text.data(), text.size(), values, back);
    CHECK (r.status == LoadStatus::ok);
    CHECK (r.content == ContentStatus::ok);
    CHECK (back == c);
    CHECK (saveState (values, back) == text); // byte stable

    // only A
    StateContent onlyA;
    onlyA.shaperA = c.shaperA;
    const std::string textA = saveState (ParamValues::defaults(), onlyA);
    CHECK (textA.find ("\"shaper_b\"") == std::string::npos);
    StateContent backA;
    ParamValues values2;
    CHECK (loadStateWithContent (textA.data(), textA.size(), values2, backA).content == ContentStatus::ok);
    CHECK (backA == onlyA);

    // the rules of the velocity curve: 2 .. 64 points, x ascending in 0..1, y in 0..1, |power| <= 20
    StateContent bad = c;
    bad.shaperA[1].y = 1.5f;
    CHECK (! validateContent (bad));
    bad = c;
    bad.shaperB.resize (1);
    CHECK (! validateContent (bad));
    bad = c;
    bad.shaperA[1].x = 1.2f;
    CHECK (! validateContent (bad));
    bad = c;
    bad.shaperA[1].power = 30.0f;
    CHECK (! validateContent (bad));
    bad = c;
    bad.shaperA.assign (65, ContentPoint (0.5f, 0.5f));
    CHECK (! validateContent (bad));
    StateContent none;
    CHECK (none.empty() && validateContent (none));
}
