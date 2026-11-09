// SynthyBoy - engine-vital tests: the spectral oscillator (GROUPC WP-12, block B29): kCap2Spectral / kCap2SpectralWarps are proven here, every key is rendered off and on.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "groupc_util.h"
#include "synthyboy/state.h"

using namespace groupc;

namespace
{
void spectralPatch (IEngine& e, Content& c)
{
    basePatch (e, c, kOscTypeSpectral);
    setByKey (e, "osc1.sample.scan", 0.0f); // a frozen head: a steady tone
    setByKey (e, "osc1.spec.manual", 1.0f);
    setByKey (e, "osc1.spec.position", 50.0f);
}

ContentRemap notchAt (int osc, float x)
{
    ContentRemap r;
    r.osc = osc;
    r.slot = 1;
    r.name = "notch";
    r.points = { ContentPoint (0.0f, 0.5f), ContentPoint (x - 0.06f, 0.5f), ContentPoint (x, 0.0f), ContentPoint (x + 0.06f, 0.5f), ContentPoint (1.0f, 0.5f) };
    return r;
}
} // namespace

TEST (spectral_capability_type_list_and_warp_entries)
{
    auto e = makeVital();
    CHECK ((e->capabilities2() & kCap2Spectral) != 0 && (e->capabilities2() & kCap2SpectralWarps) != 0);
    const auto* type = findParamByKey ("osc1.type");
    CHECK_EQ (type->max, 3.0f);
    CHECK_EQ (std::string (choiceName (*type, 3)), "Spectral");
    const size_t typeIndex = static_cast<size_t> (findParamIndex (type->id));
    CHECK ((requiredCapsForValue2 (typeIndex, 3.0f) & kCap2Spectral) != 0);
    const size_t warpIndex = static_cast<size_t> (findParamIndex (findParamByKey ("osc2.warp.type")->id));
    CHECK_EQ (requiredCapsForValue2 (warpIndex, 75.0f), kCap2None);
    CHECK_EQ (requiredCapsForValue2 (warpIndex, 76.0f), static_cast<EngineCaps2Mask> (kCap2SpectralWarps));
    CHECK_EQ (requiredCapsForValue2 (warpIndex, 98.0f), static_cast<EngineCaps2Mask> (kCap2SpectralWarps));
    CHECK_EQ (kNumWarpTypes, 99);
    const auto* warp = findParamByKey ("osc2.warp.type");
    CHECK_EQ (static_cast<int> (choiceCount (*warp)), 99);
    CHECK_EQ (std::string (choiceName (*warp, 76)), "Spectral Comb");
    CHECK_EQ (std::string (choiceName (*warp, 93)), "Mask from Osc 1");
    CHECK_EQ (std::string (choiceName (*warp, 98)), "Vocode from Noise");
    for (const char* key : { "osc2.spec.cut", "osc3.spec.xy_y", "osc1.spec.freq_lo" })
        CHECK ((requiredCaps2 (static_cast<size_t> (findParamIndex (findParamByKey (key)->id))) & kCap2Spectral) != 0);
}

TEST (spectral_type_plays_the_content_sample_at_the_key_pitch_independent_of_the_scan)
{
    Content content;
    content.sample (sine (440.0, 2.0));
    auto e = makeVital();
    spectralPatch (*e, content);
    const auto r = play (*e, 69, 1.0);
    CHECK (sbtest::allFinite (r.left));
    CHECK (rmsBetween (r, 0.2, 0.9) > 0.03);
    CHECK_NEAR (crossingFrequency (r, 0.3, 0.9), 440.0, 20.0);
    // the pitch follows the key (an octave up), the length does not change with it
    e = makeVital();
    spectralPatch (*e, content);
    const auto up = play (*e, 81, 1.0);
    CHECK_NEAR (crossingFrequency (up, 0.3, 0.9), 880.0, 40.0);
    CHECK (rmsBetween (up, 0.3, 0.9) > 0.5 * rmsBetween (r, 0.3, 0.9));
    // a moving head (scan 100 %) keeps the pitch as well
    e = makeVital();
    spectralPatch (*e, content);
    setByKey (*e, "osc1.spec.manual", 0.0f);
    setByKey (*e, "osc1.sample.scan", 100.0f);
    CHECK_NEAR (crossingFrequency (play (*e, 69, 1.0), 0.2, 0.7), 440.0, 20.0);
    // the note starts at full level (no fade in of the first window): the first 20 ms are as loud as the next
    CHECK (rmsBetween (r, 0.01, 0.03) > 0.5 * rmsBetween (r, 0.3, 0.9));
    // no content: silence
    Content none;
    e = makeVital();
    spectralPatch (*e, none);
    CHECK (sbtest::peak (play (*e, 69, 0.5).left) < 1.0e-6f);
}

TEST (spectral_off_and_on_render_differently_and_the_keys_each_move_the_sound)
{
    Content content;
    content.sample (richTone (220.0, 2.0));
    auto render = [&] (const std::vector<std::pair<const char*, float>>& keys, int key = 57)
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        for (const auto& k : keys)
            setByKey (*e, k.first, k.second);
        return play (*e, key, 0.8);
    };
    const auto base = render ({});
    CHECK (rmsBetween (base, 0.2, 0.7) > 0.02);
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        setByKey (*e, "osc1.type", 0.0f);
        CHECK (relativeDifference (base, play (*e, 57, 0.8), 0.2, 0.7) > 0.3);
    }
    struct Case { const char* name; std::vector<std::pair<const char*, float>> keys; };
    const std::vector<Case> cases = {
        { "freq hi", { { "osc1.spec.freq_hi", 700.0f }, { "osc1.spec.lohi_smooth", 0.0f } } },
        { "freq lo", { { "osc1.spec.freq_lo", 800.0f }, { "osc1.spec.lohi_smooth", 0.0f } } },
        { "smooth edges", { { "osc1.spec.freq_hi", 700.0f }, { "osc1.spec.lohi_smooth", 1.0f } } },
        { "band after warp", { { "osc1.spec.freq_hi", 700.0f }, { "osc1.spec.lohi_post_warp", 1.0f }, { "osc1.spec.lohi_smooth", 0.0f }, { "osc1.warp.type", 87.0f }, { "osc1.warp.amount", 100.0f } } },
        { "frequency shift", { { "osc1.spec.freq_shift", 300.0f } } },
        { "phase lock", { { "osc1.spec.phase_lock", 1.0f }, { "osc1.sample.scan", 30.0f }, { "osc1.spec.manual", 0.0f }, { "osc1.warp.type", 79.0f }, { "osc1.warp.amount", 60.0f } } },
        { "scan key track", { { "osc1.spec.manual", 0.0f }, { "osc1.spec.scan_keytrack", 1.0f }, { "osc1.sample.scan", 40.0f } } },
        { "warp 1 amount", { { "osc1.warp.type", 76.0f }, { "osc1.warp.amount", 90.0f } } },
        { "warp 2", { { "osc1.warp2.type", 89.0f }, { "osc1.warp2.amount", 90.0f } } },
        { "y axis warp", { { "osc1.warp.type", 76.0f }, { "osc1.spec.y_axis", 2.0f }, { "osc1.spec.xy_y", 90.0f } } },
        { "y axis level", { { "osc1.spec.y_axis", 1.0f }, { "osc1.spec.xy_y", 10.0f } } },
        { "y axis freq hi", { { "osc1.spec.y_axis", 7.0f }, { "osc1.spec.xy_y", 20.0f } } },
        { "unison", { { "osc1.unison.voices", 3.0f }, { "osc1.unison.detune", 40.0f } } },
        { "sample region", { { "osc1.sample.start", 30.0f }, { "osc1.sample.end", 70.0f }, { "osc1.spec.manual", 0.0f }, { "osc1.sample.scan", 100.0f } } },
    };
    for (const Case& c : cases)
    {
        const auto r = render (c.keys);
        const bool ok = relativeDifference (base, r, 0.2, 0.7) > 0.02 && sbtest::allFinite (r.left) && sbtest::peak (r.left) < 4.0f;
        if (! CHECK (ok))
            std::printf ("    spectral key case \"%s\" did not change the render (or was not finite)\n", c.name);
    }
}

TEST (spectral_filter_curve_cut_and_mix_shape_the_spectrum)
{
    // a 632 Hz tone; the drawn mask has a notch there. Mix 100 %: the tone is gone; mix 0 % (dry): it stays; CUT moves the notch away
    Content content;
    content.sample (sine (632.5, 2.0));
    content.state.spectralFilters.push_back (notchAt (1, 0.5f));
    const auto level = [&] (float mix, float cut)
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        setByKey (*e, "osc1.spec.mix", mix);
        setByKey (*e, "osc1.spec.cut", cut);
        return rmsBetween (play (*e, 69, 0.8), 0.25, 0.7);
    };
    const double open = level (0.0f, 0.0f);
    CHECK (open > 0.03);
    CHECK (level (100.0f, 0.0f) < 0.2 * open);
    CHECK (level (100.0f, 60.0f) > 0.8 * open);   // the curve shifted away from the tone
    CHECK (level (50.0f, 0.0f) < 0.9 * open && level (50.0f, 0.0f) > 0.2 * open);
    // without a drawn curve the filter is flat: mix and cut change nothing
    Content flat;
    flat.sample (sine (632.5, 2.0));
    const auto withoutCurve = [&] (float mix, float cut)
    {
        auto e = makeVital();
        spectralPatch (*e, flat);
        setByKey (*e, "osc1.spec.mix", mix);
        setByKey (*e, "osc1.spec.cut", cut);
        return play (*e, 69, 0.5);
    };
    CHECK (identical (withoutCurve (100.0f, 0.0f), withoutCurve (30.0f, 80.0f)));
}

TEST (spectral_manual_position_and_the_modulatable_keys)
{
    Content content;
    content.sample (twoTones (300.0, 900.0, 2.0));
    const auto at = [&] (float position)
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        setByKey (*e, "osc1.spec.position", position);
        return crossingFrequency (play (*e, 69, 0.8), 0.3, 0.7);
    };
    CHECK_NEAR (at (15.0f), 300.0, 30.0);
    CHECK_NEAR (at (85.0f), 900.0, 70.0);
    // modulation: macro 1 -> position
    const auto destination = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.spec.position")->id)));
    CHECK (destination >= 963 && destination <= 983);
    const auto withMacro = [&] (float macro)
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        setByKey (*e, "osc1.spec.position", 10.0f);
        setByKey (*e, "mod01.source", 19.0f); // Macro 1
        setByKey (*e, "mod01.destination", static_cast<float> (destination));
        setByKey (*e, "mod01.amount", 80.0f);
        setByKey (*e, "macro.1", macro);
        return crossingFrequency (play (*e, 69, 0.8), 0.3, 0.7);
    };
    CHECK_NEAR (withMacro (0.0f), 300.0, 30.0);
    CHECK (withMacro (100.0f) > 700.0); // 10 % + 80 %
    // the cut as a destination
    CHECK (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc2.spec.cut")->id))) > 962);
}

TEST (spectral_warps_run_on_the_spectral_type_and_are_inert_elsewhere)
{
    Content content;
    content.sample (richTone (220.0, 2.0));
    const auto render = [&] (int type, float warpType, bool otherOsc)
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        setByKey (*e, "osc1.type", static_cast<float> (type));
        if (type == kOscTypeWavetable)
            setByKey (*e, "osc1.wavetable", 1.0f);
        setByKey (*e, "osc1.warp.type", warpType);
        setByKey (*e, "osc1.warp.amount", 85.0f);
        setByKey (*e, "osc1.warp.phase", 150.0f);
        if (otherOsc) // the first other oscillator (osc 2) feeds the mask and vocode warps; its level is 0 so only the warp hears it
        {
            setByKey (*e, "osc2.on", 1.0f);
            setByKey (*e, "osc2.level", 0.0f);
            setByKey (*e, "osc2.wavetable", 1.0f);
        }
        return play (*e, 57, 0.6);
    };
    const auto none = render (kOscTypeSpectral, 0.0f, true);
    for (int i = 0; i < 23; ++i)
    {
        if (i == 15) // Smear blends the frames over time: a frozen head has nothing to smear (tested with a moving head below)
            continue;
        const auto r = render (kOscTypeSpectral, static_cast<float> (kFirstSpectralWarp + i), true);
        const auto again = render (kOscTypeSpectral, static_cast<float> (kFirstSpectralWarp + i), true);
        const bool ok = sbtest::allFinite (r.left) && sbtest::peak (r.left) < 4.0f && relativeDifference (none, r, 0.15, 0.55) > 0.01 && identical (r, again);
        if (! CHECK (ok))
            std::printf ("    spectral warp entry %d (%s) did not change the sound / was not finite / was not deterministic\n", kFirstSpectralWarp + i,
                         choiceName (*findParamByKey ("osc2.warp.type"), static_cast<size_t> (kFirstSpectralWarp + i)));
    }
    {
        Content moving;
        moving.sample (twoTones (300.0, 900.0, 1.0));
        const auto smeared = [&] (float amount)
        {
            auto e = makeVital();
            basePatch (*e, moving, kOscTypeSpectral);
            setByKey (*e, "osc1.sample.scan", 100.0f);
            setByKey (*e, "osc1.warp.type", 91.0f);
            setByKey (*e, "osc1.warp.amount", amount);
            return play (*e, 69, 0.9);
        };
        CHECK (relativeDifference (smeared (0.0f), smeared (95.0f), 0.1, 0.8) > 0.02);
    }
    // on a wavetable oscillator a spectral warp is the same as no warp
    CHECK (identical (render (kOscTypeWavetable, 0.0f, false), render (kOscTypeWavetable, 80.0f, false)));
    // and on a sample oscillator
    {
        Content c2;
        c2.sample (sine (440.0, 1.0));
        auto a = makeVital(), b = makeVital();
        basePatch (*a, c2, kOscTypeSample);
        basePatch (*b, c2, kOscTypeSample);
        setByKey (*b, "osc1.warp.type", 90.0f);
        setByKey (*b, "osc1.warp.amount", 100.0f);
        CHECK (identical (play (*a, 69, 0.4), play (*b, 69, 0.4)));
    }
}

TEST (spectral_is_deterministic_finite_at_extreme_settings_and_survives_a_state_round_trip)
{
    Content content;
    content.sample (twoTones (300.0, 900.0, 3.0));
    content.state.spectralFilters.push_back (notchAt (1, 0.4f));
    const auto extreme = [&]
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        setByKey (*e, "osc1.unison.voices", 16.0f);
        setByKey (*e, "osc1.spec.manual", 0.0f);
        setByKey (*e, "osc1.sample.scan", 100.0f);
        setByKey (*e, "osc1.sample.scan_range", 3.0f);
        setByKey (*e, "osc1.sample.loop_mode", 3.0f);
        setByKey (*e, "osc1.spec.freq_shift", -2000.0f);
        setByKey (*e, "osc1.spec.freq_lo", 20000.0f);
        setByKey (*e, "osc1.spec.freq_hi", 20.0f);
        setByKey (*e, "osc1.spec.phase_lock", 1.0f);
        setByKey (*e, "osc1.spec.transients", 1.0f);
        setByKey (*e, "osc1.warp.type", 100.0f - 3.0f);
        setByKey (*e, "osc1.warp.amount", 100.0f);
        setByKey (*e, "osc1.warp2.type", 77.0f);
        setByKey (*e, "osc1.warp2.amount", 100.0f);
        return sbtest::render (*e, { noteOn (0, 30), noteOn (2000, 60), noteOn (4000, 110) }, static_cast<int> (1.2 * kSr), 333);
    };
    const auto a = extreme(), b = extreme();
    CHECK (sbtest::allFinite (a.left) && sbtest::allFinite (a.right));
    CHECK (sbtest::peak (a.left) < 16.0f && sbtest::peak (a.right) < 16.0f);
    CHECK (identical (a, b));

    ParamValues values = ParamValues::defaults();
    const char* keys[] = { "osc2.type", "osc2.spec.freq_lo", "osc2.spec.cut", "osc2.spec.y_axis", "osc3.spec.phase_lock", "osc2.warp.type" };
    const float vals[] = { 3.0f, 440.0f, -35.0f, 5.0f, 1.0f, 91.0f };
    for (size_t i = 0; i < 6; ++i)
        values.plain[static_cast<size_t> (findParamIndex (findParamByKey (keys[i])->id))] = vals[i];
    const std::string text = saveState (values, content.state);
    ParamValues loaded = ParamValues::defaults();
    StateContent loadedContent;
    CHECK (loadStateWithContent (text.data(), text.size(), loaded, loadedContent).status == LoadStatus::ok);
    for (size_t i = 0; i < 6; ++i)
        CHECK_EQ (loaded.plain[static_cast<size_t> (findParamIndex (findParamByKey (keys[i])->id))], vals[i]);
    CHECK (loadedContent == content.state);
    CHECK_EQ (loadedContent.spectralFilters.size(), static_cast<size_t> (1));
}
