// SynthyBoy - engine-vital tests: the granular oscillator (GROUPC WP-11, block B28): kCap2Granular is proven here, every key is rendered off and on.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "groupc_util.h"
#include "synthyboy/state.h"

using namespace groupc;

namespace
{
void granularPatch (IEngine& e, Content& c)
{
    basePatch (e, c, kOscTypeGranular);
    setByKey (e, "osc1.gran.density", 80.0f);
    setByKey (e, "osc1.gran.length", 120.0f);
    setByKey (e, "osc1.sample.scan", 10.0f); // a slow scan
}
} // namespace

TEST (granular_capability_and_type_list)
{
    auto e = makeVital();
    CHECK ((e->capabilities2() & kCap2Granular) != 0);
    const auto* type = findParamByKey ("osc1.type");
    CHECK (type->max >= 2.0f);
    CHECK_EQ (std::string (choiceName (*type, 2)), "Granular");
    CHECK ((requiredCapsForValue2 (static_cast<size_t> (findParamIndex (type->id)), 2.0f) & kCap2Granular) != 0);
    CHECK_EQ (requiredCapsForValue2 (static_cast<size_t> (findParamIndex (type->id)), 1.0f), kCap2None);
    for (const char* key : { "osc2.gran.density", "osc3.gran.xy_y", "osc1.gran.window_shape" })
        CHECK ((requiredCaps2 (static_cast<size_t> (findParamIndex (findParamByKey (key)->id))) & kCap2Granular) != 0);
    CHECK_EQ (requiredCaps2 (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.sample.scan")->id))), kCap2None);
    // an engine without the bit does not support the key
    CHECK (e->supportsParam (findParamByKey ("osc1.gran.density")->id));
}

TEST (granular_type_plays_the_content_sample_at_the_key_pitch_and_is_silent_without_content)
{
    Content content;
    content.sample (sine (440.0, 2.0));
    auto e = makeVital();
    granularPatch (*e, content);
    const auto r = play (*e, 69, 1.0);
    CHECK (sbtest::allFinite (r.left));
    CHECK (rmsBetween (r, 0.3, 0.9) > 0.02);
    CHECK_NEAR (crossingFrequency (r, 0.3, 0.9), 440.0, 25.0);

    // pitch tracking: an octave up
    e = makeVital();
    granularPatch (*e, content);
    CHECK_NEAR (crossingFrequency (play (*e, 81, 1.0), 0.3, 0.9), 880.0, 50.0);

    // the scan does not change the pitch: a frozen play head (scan 0) plays the same pitch. The grains restart at the same place, so the tone is
    // measured with a grain rate that is a whole number of cycles (480 Hz tone, 120 grains per second)
    {
        Content aligned;
        aligned.sample (sine (480.0, 2.0));
        e = makeVital();
        granularPatch (*e, aligned);
        setByKey (*e, "osc1.sample.scan", 0.0f);
        setByKey (*e, "osc1.gran.density", 120.0f);
        const double f = crossingFrequency (play (*e, 69, 1.0), 0.3, 0.9);
        CHECK_NEAR (f, 480.0, 25.0);
    }

    // no content: the oscillator is silent (the documented fallback of every sample class type)
    Content none;
    e = makeVital();
    granularPatch (*e, none);
    const auto silent = play (*e, 69, 0.5);
    CHECK (sbtest::peak (silent.left) < 1.0e-6f);
}

TEST (granular_off_and_on_render_differently_and_the_keys_each_move_the_sound)
{
    Content content;
    content.sample (twoTones (300.0, 900.0, 2.0));
    auto render = [&] (const std::vector<std::pair<const char*, float>>& keys, int key = 69)
    {
        auto e = makeVital();
        granularPatch (*e, content);
        for (const auto& k : keys)
            setByKey (*e, k.first, k.second);
        return play (*e, key, 0.8);
    };
    const auto base = render ({});
    // type off: the wavetable oscillator
    {
        auto e = makeVital();
        granularPatch (*e, content);
        setByKey (*e, "osc1.type", 0.0f);
        const auto wt = play (*e, 69, 0.8);
        CHECK (relativeDifference (base, wt, 0.2, 0.7) > 0.3);
    }
    struct Case { const char* name; std::vector<std::pair<const char*, float>> keys; };
    const std::vector<Case> cases = {
        { "window amount", { { "osc1.gran.window_amount", 0.0f } } },
        { "window skew", { { "osc1.gran.window_skew", 90.0f } } },
        { "window shape", { { "osc1.gran.window_shape", 8.0f } } },
        { "density", { { "osc1.gran.density", 12.0f } } },
        { "density mode bpm", { { "osc1.gran.density_mode", 1.0f }, { "osc1.gran.density_sync", 6.0f } } },
        { "density mode grains", { { "osc1.gran.density_mode", 2.0f }, { "osc1.gran.density_grains", 3.0f } } },
        { "length", { { "osc1.gran.length", 15.0f } } },
        { "length mode bpm", { { "osc1.gran.length_mode", 1.0f }, { "osc1.gran.length_sync", 18.0f } } },
        { "length mode percent", { { "osc1.gran.length_mode", 2.0f }, { "osc1.gran.length_percent", 40.0f } } },
        { "jump start", { { "osc1.gran.jump_start", 0.0f } } },
        { "max grains", { { "osc1.gran.max_grains", 2.0f } } },
        { "manual position", { { "osc1.gran.manual", 1.0f }, { "osc1.gran.position", 90.0f } } },
        { "rand offset", { { "osc1.gran.rand_offset", 100.0f } } },
        { "rand dir", { { "osc1.gran.rand_dir", 100.0f } } },
        { "reverse grains", { { "osc1.gran.reverse_grains", 1.0f } } },
        { "rand pitch", { { "osc1.gran.rand_pitch", 12.0f } } },
        { "rand length", { { "osc1.gran.rand_length", 80.0f } } },
        { "rand pan", { { "osc1.gran.rand_pan", 100.0f } } },
        { "rand level", { { "osc1.gran.rand_level", 100.0f } } },
        { "rand window", { { "osc1.gran.rand_window", 100.0f } } },
        { "rand skew", { { "osc1.gran.rand_skew", 100.0f } } },
        { "scan key track", { { "osc1.gran.scan_keytrack", 1.0f } } },
        { "scan", { { "osc1.sample.scan", 60.0f } } },
        { "spawn pattern even", { { "osc1.gran.spawn_pattern", 1.0f }, { "osc1.unison.voices", 4.0f } } },
        { "y axis level", { { "osc1.gran.y_axis", 1.0f }, { "osc1.gran.xy_y", 10.0f } } },
        { "y axis density", { { "osc1.gran.y_axis", 4.0f }, { "osc1.gran.xy_y", 20.0f } } },
        { "warp (per grain shape) with random", { { "osc1.warp.type", 27.0f }, { "osc1.warp.amount", 100.0f }, { "osc1.gran.rand_warp", 100.0f } } },
    };
    for (const Case& c : cases)
    {
        const auto r = render (c.keys);
        const bool ok = relativeDifference (base, r, 0.1, 0.7) > 0.02 && sbtest::allFinite (r.left) && sbtest::peak (r.left) < 4.0f;
        if (! CHECK (ok))
            std::printf ("    granular key case \"%s\" did not change the render (or was not finite)\n", c.name);
    }
    // the manual position places the head: 300 Hz at 10 %, 900 Hz at 90 %
    CHECK_NEAR (crossingFrequency (render ({ { "osc1.gran.manual", 1.0f }, { "osc1.gran.position", 10.0f } }), 0.3, 0.7), 300.0, 30.0);
    CHECK_NEAR (crossingFrequency (render ({ { "osc1.gran.manual", 1.0f }, { "osc1.gran.position", 90.0f } }), 0.3, 0.7), 900.0, 60.0);
}

TEST (granular_is_modulatable_and_the_destinations_exist)
{
    const auto destination = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.gran.position")->id)));
    CHECK (destination >= 912);
    Content content;
    content.sample (twoTones (300.0, 900.0, 2.0));
    const auto withMacro = [&] (float macro)
    {
        auto e = makeVital();
        granularPatch (*e, content);
        setByKey (*e, "osc1.gran.manual", 1.0f);
        setByKey (*e, "osc1.gran.position", 0.0f);
        setByKey (*e, "mod01.source", 19.0f); // Macro 1
        setByKey (*e, "mod01.destination", static_cast<float> (destination));
        setByKey (*e, "mod01.amount", 80.0f);
        setByKey (*e, "macro.1", macro);
        return crossingFrequency (play (*e, 69, 0.8), 0.3, 0.7);
    };
    CHECK_NEAR (withMacro (0.0f), 300.0, 30.0);
    CHECK (withMacro (100.0f) > 700.0);
}

TEST (granular_is_deterministic_and_finite_at_extreme_settings)
{
    Content content;
    content.sample (twoTones (300.0, 900.0, 2.0));
    const auto extreme = [&]
    {
        auto e = makeVital();
        granularPatch (*e, content);
        setByKey (*e, "osc1.unison.voices", 16.0f);
        setByKey (*e, "osc1.gran.density", 1000.0f);
        setByKey (*e, "osc1.gran.length", 4000.0f);
        setByKey (*e, "osc1.gran.max_grains", 128.0f);
        setByKey (*e, "osc1.gran.rand_offset", 100.0f);
        setByKey (*e, "osc1.gran.rand_pitch", 24.0f);
        setByKey (*e, "osc1.gran.rand_dir", 100.0f);
        setByKey (*e, "osc1.gran.rand_pan", 100.0f);
        setByKey (*e, "osc1.gran.rand_level", 100.0f);
        setByKey (*e, "osc1.gran.rand_length", 100.0f);
        setByKey (*e, "osc1.gran.window_amount", 100.0f);
        setByKey (*e, "osc1.gran.window_skew", -100.0f);
        setByKey (*e, "osc1.sample.scan", 100.0f);
        setByKey (*e, "osc1.sample.scan_range", 3.0f);
        setByKey (*e, "osc1.sample.loop_mode", 3.0f);
        setByKey (*e, "osc1.gran.loop_grains", 1.0f);
        return sbtest::render (*e, { noteOn (0, 40), noteOn (2000, 52), noteOn (4000, 100) }, static_cast<int> (1.5 * kSr), 333);
    };
    const auto a = extreme(), b = extreme();
    CHECK (sbtest::allFinite (a.left) && sbtest::allFinite (a.right));
    CHECK (sbtest::peak (a.left) < 16.0f && sbtest::peak (a.right) < 16.0f);
    CHECK (identical (a, b)); // the same patch, the same notes: the same samples (the grain randomness is seeded)
}

TEST (granular_each_window_shape_and_spawn_pattern_and_y_axis_is_finite)
{
    Content content;
    content.sample (sine (500.0, 1.0));
    for (int shape = 0; shape < 10; ++shape)
        for (int pattern = 0; pattern < 4; ++pattern)
        {
            auto e = makeVital();
            granularPatch (*e, content);
            setByKey (*e, "osc1.gran.window_shape", static_cast<float> (shape));
            setByKey (*e, "osc1.gran.spawn_pattern", static_cast<float> (pattern));
            setByKey (*e, "osc1.unison.voices", 3.0f);
            setByKey (*e, "osc1.gran.y_axis", static_cast<float> ((shape * 4 + pattern) % 18));
            setByKey (*e, "osc1.gran.xy_y", 75.0f);
            const auto r = play (*e, 60, 0.25);
            CHECK (sbtest::allFinite (r.left));
            CHECK (sbtest::peak (r.left) < 8.0f);
        }
}

TEST (granular_keys_survive_a_state_round_trip)
{
    ParamValues values = ParamValues::defaults();
    const char* keys[] = { "osc2.type", "osc2.gran.window_shape", "osc2.gran.density", "osc2.gran.length_mode", "osc2.gran.y_axis", "osc3.gran.rand_pitch", "osc1.gran.xy_y" };
    const float vals[] = { 2.0f, 7.0f, 33.5f, 2.0f, 11.0f, 5.25f, 12.0f };
    for (size_t i = 0; i < 7; ++i)
        values.plain[static_cast<size_t> (findParamIndex (findParamByKey (keys[i])->id))] = vals[i];
    const std::string text = saveState (values);
    ParamValues loaded = ParamValues::defaults();
    CHECK (loadState (text.data(), text.size(), loaded).status == LoadStatus::ok);
    for (size_t i = 0; i < 7; ++i)
        CHECK_EQ (loaded.plain[static_cast<size_t> (findParamIndex (findParamByKey (keys[i])->id))], vals[i]);
    // and the engine renders the loaded patch the same as the one built by hand
    Content content;
    content.sample (sine (440.0, 1.0));
    const auto run = [&] (const ParamValues& v)
    {
        auto e = makeVital();
        content.apply (*e);
        for (size_t i = 0; i < paramCount(); ++i)
            e->setParam (paramTable()[i].id, v.plain[i]);
        setByKey (*e, "osc2.on", 1.0f);
        setByKey (*e, "osc1.on", 0.0f);
        setByKey (*e, "osc2.level", 100.0f);
        return play (*e, 69, 0.4);
    };
    CHECK (identical (run (values), run (loaded)));
}
