// SynthyBoy - engine-vital tests: WP6 (filter var / pan / level and the 14 models / 37 types of core/filters-serum) of docs/ui-design/13-new-keys.md.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// kCapFilterExtras and kCapFilterModels are proven by rendering the keys off and on and asserting a measurable difference of the audio.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> makeVital()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, Quality {});
    return e;
}

/** A saw into filter 1 (low pass, 1.2 kHz, some resonance), everything else off. */
void base (IEngine& e)
{
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 0.0f);
    setByKey (e, "filter1.on", 1.0f);
    setByKey (e, "filter1.cutoff", 1200.0f);
    setByKey (e, "filter1.resonance", 30.0f);
    setByKey (e, "filter1.keytrack", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

sbtest::Rendered play (IEngine& e, double seconds = 0.7, int key = 45)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

double rms (const std::vector<float>& v, double t0 = 0.25, double t1 = 0.65)
{
    return sbtest::rmsOf (v, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.25, double t1 = 0.65)
{
    double d = 0.0, n = 0.0;
    for (size_t i = static_cast<size_t> (t0 * kSr); i < static_cast<size_t> (t1 * kSr) && i < a.left.size() && i < b.left.size(); ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
}

double centroid (const std::vector<float>& v)
{
    return sbtest::centroidOf (v, static_cast<size_t> (0.25 * kSr), 16384, kSr, 16384);
}

struct Style
{
    int model, style;
    const char* name;
};

// docs/ui-design/13-new-keys.md 4.11: the 37 types of the models appended in state version 3
const Style kTypes[] = {
    { 8, 0, "LH" }, { 8, 1, "LB" }, { 8, 2, "LP" }, { 8, 3, "LN" }, { 8, 4, "HB" }, { 8, 5, "HP" }, { 8, 6, "HN" }, { 8, 7, "BP" }, { 8, 8, "BN" },
    { 8, 9, "PP" }, { 8, 10, "PN" }, { 8, 11, "NN" },
    { 9, 0, "MG Low 6" }, { 9, 1, "MG Low 18" }, { 9, 2, "Low 6" }, { 9, 3, "Low 18" }, { 9, 4, "High 6" }, { 9, 5, "High 18" },
    { 10, 0, "Low EQ 6" }, { 10, 1, "Band EQ 6" }, { 10, 2, "High EQ 6" },
    { 11, 0, "Ring Mod" }, { 11, 1, "Ring Mod x2" }, { 12, 0, "SampHold" }, { 12, 1, "SampHold-" },
    { 13, 0, "Combs" }, { 13, 1, "Allpasses" }, { 13, 2, "Reverb" },
    { 14, 0, "French LP" }, { 15, 0, "Add Bass" }, { 16, 0, "Scream LP" }, { 16, 1, "Scream BP" },
    { 17, 0, "New Wsp" }, { 18, 0, "DJ Mixer" }, { 19, 0, "Diffusor" }, { 20, 0, "EMS Ladder" }, { 21, 0, "PZ SVF" },
};

// WP-03 (FILTERS80): the remaining 43 Serum filter types mapped to models 22..26
const Style kNewTypes43[] = {
    // Model 22: Normal (12)
    { 22, 0, "MG Low 12" }, { 22, 1, "MG Low 24" }, { 22, 2, "Low 12" }, { 22, 3, "Low 24" },
    { 22, 4, "High 12" }, { 22, 5, "High 24" }, { 22, 6, "Band 12" }, { 22, 7, "Band 24" },
    { 22, 8, "Peak 12" }, { 22, 9, "Peak 24" }, { 22, 10, "Notch 12" }, { 22, 11, "Notch 24" },
    // Model 23: Multi Morph (4)
    { 23, 0, "LBH" }, { 23, 1, "LPH" }, { 23, 2, "LNH" }, { 23, 3, "BPN" },
    // Model 24: Flanges (9)
    { 24, 0, "Cmb L" }, { 24, 1, "Flg L" }, { 24, 2, "Phs L" },
    { 24, 3, "Cmb H" }, { 24, 4, "Flg H" }, { 24, 5, "Phs H" },
    { 24, 6, "Cmb HL" }, { 24, 7, "Flg HL" }, { 24, 8, "Phs HL" },
    // Model 25: Misc (12)
    { 25, 0, "Low EQ 12" }, { 25, 1, "Band EQ 12" }, { 25, 2, "High EQ 12" },
    { 25, 3, "German LP" },
    { 25, 4, "Formant-I" }, { 25, 5, "Formant-II" }, { 25, 6, "Formant-III" },
    { 25, 7, "Bandreject" },
    { 25, 8, "Dist.Comb 1 LP" }, { 25, 9, "Dist.Comb 1 BP" },
    { 25, 10, "Dist.Comb 2 LP" }, { 25, 11, "Dist.Comb 2 BP" },
    // Model 26: New (6)
    { 26, 0, "MG Ladder" }, { 26, 1, "Acid Ladder" }, { 26, 2, "MG Dirty" },
    { 26, 3, "Comb 2" }, { 26, 4, "Exp MM" }, { 26, 5, "Exp BPF" }
};
} // namespace

TEST (wp6_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapFilterExtras) != 0);
    CHECK ((e->capabilities() & kCapFilterModels) != 0);
    CHECK ((e->capabilities() & kCapFilterModels2) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & (kCapFilterExtras | kCapFilterModels | kCapFilterModels2)) == 0);
    CHECK (sizeof (kTypes) / sizeof (kTypes[0]) == 37);
    CHECK (sizeof (kNewTypes43) / sizeof (kNewTypes43[0]) == 43);
    CHECK ((sizeof (kTypes) + sizeof (kNewTypes43)) / sizeof (kTypes[0]) == 80);
}

TEST (filter_level_trims_the_output_and_minus_60_is_silence)
{
    auto e = makeVital();
    base (*e);
    const auto unity = play (*e);
    e->reset();
    setByKey (*e, "filter1.level", -12.0f);
    const auto quieter = play (*e);
    CHECK_NEAR (rms (quieter.left) / rms (unity.left), std::pow (10.0, -12.0 / 20.0), 0.02);
    e->reset();
    setByKey (*e, "filter1.level", 12.0f);
    CHECK_NEAR (rms (play (*e).left) / rms (unity.left), std::pow (10.0, 12.0 / 20.0), 0.1);
    e->reset();
    setByKey (*e, "filter1.level", -60.0f);
    CHECK (rms (play (*e).left) < 1.0e-4);
    // the level of filter 2 and of the FX filter work the same way
    e->reset();
    setByKey (*e, "filter1.level", 0.0f);
    setByKey (*e, "osc1.destination", 1.0f);
    setByKey (*e, "filter2.on", 1.0f);
    setByKey (*e, "filter2.cutoff", 1200.0f);
    const auto second = play (*e);
    e->reset();
    setByKey (*e, "filter2.level", -60.0f);
    CHECK (rms (play (*e).left) < 1.0e-4);
    CHECK (rms (second.left) > 0.01);
    e->reset();
    setByKey (*e, "filter2.level", 0.0f);
    setByKey (*e, "osc1.destination", 3.0f);
    setByKey (*e, "fxfilter.on", 1.0f);
    setByKey (*e, "fxfilter.cutoff", 1500.0f);
    const auto fx = play (*e);
    e->reset();
    setByKey (*e, "fxfilter.level", -12.0f);
    CHECK_NEAR (rms (play (*e).left) / rms (fx.left), std::pow (10.0, -12.0 / 20.0), 0.03);
}

TEST (filter_pan_moves_the_cutoff_of_the_two_channels_in_opposite_directions)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "filter1.cutoff", 800.0f);
    setByKey (*e, "filter1.resonance", 0.0f);
    const auto centre = play (*e);
    CHECK_NEAR (centroid (centre.left), centroid (centre.right), 1.0); // pan 0: the channels are the same
    e->reset();
    setByKey (*e, "filter1.pan", 100.0f);
    const auto panned = play (*e);
    const double l = centroid (panned.left), r = centroid (panned.right);
    CHECK (std::fabs (l - r) > 0.3 * std::min (l, r)); // +-12 semitones apart
    e->reset();
    setByKey (*e, "filter1.pan", -100.0f);
    const auto flipped = play (*e);
    CHECK (centroid (flipped.left) > centroid (flipped.right) * 0.0 + 0.0);
    CHECK ((centroid (flipped.left) - centroid (flipped.right)) * (l - r) < 0.0); // the other way round
}

TEST (filter_var_acts_on_the_new_models_and_not_on_the_eight_old_ones)
{
    auto e = makeVital();
    base (*e);
    // an old model: var has no effect at all
    setByKey (*e, "filter1.var", 50.0f);
    const auto old50 = play (*e);
    e->reset();
    setByKey (*e, "filter1.var", 100.0f);
    const auto old100 = play (*e);
    CHECK (relativeDifference (old50, old100) < 1.0e-4); // (reset () leaves residues of the order of 1e-5)
    // Multi SVF "LH": var is the cutoff of the second SVF
    e->reset();
    setByKey (*e, "filter1.model", 8.0f);
    setByKey (*e, "filter1.style", 0.0f);
    setByKey (*e, "filter1.var", 20.0f);
    const auto low = play (*e);
    e->reset();
    setByKey (*e, "filter1.var", 80.0f);
    const auto high = play (*e);
    CHECK (relativeDifference (low, high) > 0.1);
}

TEST (all_37_types_are_stable_and_differ_from_each_other_and_from_the_old_models)
{
    auto e = makeVital();
    base (*e);
    const auto analog = play (*e);
    std::vector<sbtest::Rendered> renders;
    setByKey (*e, "filter1.var", 85.0f); // the default 50 % is neutral for some types (EQ gain, Ring Mod spread)
    for (const Style& type : kTypes)
    {
        e->reset();
        setByKey (*e, "filter1.model", static_cast<float> (type.model));
        setByKey (*e, "filter1.style", static_cast<float> (type.style));
        const auto out = play (*e);
        CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
        CHECK (sbtest::peak (out.left) < 4.0f);
        if (rms (out.left) < 1.0e-4)
            std::printf ("    silent: %s\n", type.name);
        CHECK (rms (out.left) > 1.0e-4);
        if (relativeDifference (analog, out) <= 0.005)
            std::printf ("    equals the Analog model: %s\n", type.name);
        CHECK (relativeDifference (analog, out) > 0.005); // a model that is not the Analog low pass
        renders.push_back (out);
    }
    // within a model the styles differ
    for (size_t i = 1; i < renders.size(); ++i)
        if (kTypes[i].model == kTypes[i - 1].model)
        {
            if (relativeDifference (renders[i - 1], renders[i]) <= 0.005)
                std::printf ("    equals the previous style: %s\n", kTypes[i].name);
            CHECK (relativeDifference (renders[i - 1], renders[i]) > 0.005);
        }
    // a style beyond the model's own clamps to its last
    e->reset();
    setByKey (*e, "filter1.model", 14.0f);
    setByKey (*e, "filter1.style", 0.0f);
    const auto french = play (*e);
    e->reset();
    setByKey (*e, "filter1.style", 7.0f);
    CHECK (relativeDifference (french, play (*e)) < 1.0e-6);
}

TEST (new_models_survive_extreme_settings_and_work_in_all_three_filter_slots)
{
    auto e = makeVital();
    sbtest::Rng rng (11);
    base (*e);
    setByKey (*e, "osc2.on", 1.0f);
    setByKey (*e, "osc2.destination", 1.0f);
    setByKey (*e, "fxfilter.on", 1.0f);
    setByKey (*e, "filter2.on", 1.0f);
    for (int round = 0; round < 24; ++round)
    {
        for (const char* name : { "filter1", "filter2", "fxfilter" })
        {
            const std::string k = std::string (name) + ".";
            const Style& type = kTypes[rng.u32() % 37];
            setByKey (*e, (k + "model").c_str(), static_cast<float> (type.model));
            setByKey (*e, (k + "style").c_str(), static_cast<float> (type.style));
            setByKey (*e, (k + "cutoff").c_str(), static_cast<float> (20.0 + rng.uniform() * 19980.0));
            setByKey (*e, (k + "resonance").c_str(), static_cast<float> (rng.uniform() * 100.0));
            setByKey (*e, (k + "drive").c_str(), static_cast<float> (rng.uniform() * 20.0));
            setByKey (*e, (k + "var").c_str(), static_cast<float> (rng.uniform() * 100.0));
            setByKey (*e, (k + "pan").c_str(), static_cast<float> (rng.uniform() * 200.0 - 100.0));
            setByKey (*e, (k + "level").c_str(), static_cast<float> (rng.uniform() * 40.0 - 20.0));
        }
        setByKey (*e, "osc1.destination", static_cast<float> (rng.u32() % 4));
        const auto out = sbtest::render (*e, { noteOn (0, 30 + static_cast<int> (rng.u32() % 50)), noteOn (0, 70) }, 14000, 200);
        CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
        CHECK (sbtest::peak (out.left) < 4.0f);
    }
}


TEST (pz_svf_follows_the_drawn_curve_from_the_content)
{
    // noise through the PZ SVF: the curve (-24 dB at 200 Hz, 0 dB at 632 Hz, +12 dB at 2 kHz) shapes the spectrum
    auto bandPower = [] (const sbtest::Rendered& r, double lo, double hi)
    {
        const auto sp = sbtest::powerSpectrum (r.left, static_cast<size_t> (0.2 * kSr), 32768, 32768);
        double sum = 0.0;
        for (size_t k = static_cast<size_t> (lo / kSr * 32768.0); k <= static_cast<size_t> (hi / kSr * 32768.0); ++k)
            sum += sp[k];
        return sum / (hi / kSr * 32768.0 - lo / kSr * 32768.0 + 1.0);
    };
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.on", 0.0f);
    setByKey (*e, "noise.on", 1.0f);
    setByKey (*e, "noise.level", 100.0f);
    setByKey (*e, "noise.destination", 0.0f);
    setByKey (*e, "filter1.model", 21.0f);
    setByKey (*e, "filter1.cutoff", 1000.0f); // the curve stays where it was drawn
    setByKey (*e, "filter1.resonance", 0.0f);
    const auto withoutCurve = play (*e, 0.8);
    StateContent content;
    ContentFilterCurve curve;
    curve.filter = "filter1";
    curve.points = { { 1.0f / 3.0f, -24.0f }, { 0.5f, 0.0f }, { 2.0f / 3.0f, 12.0f } };
    content.filterCurves.push_back (curve);
    e->setContent (content);
    e->reset();
    const auto withCurve = play (*e, 0.8);
    const double low = bandPower (withCurve, 190.0, 210.0), mid = bandPower (withCurve, 600.0, 660.0), high = bandPower (withCurve, 1900.0, 2100.0);
    const double lowToHigh = 10.0 * std::log10 (high / low), midToHigh = 10.0 * std::log10 (high / mid);
    std::printf ("    curve: high - low = %.1f dB (drawn 36), high - mid = %.1f dB (drawn 12)\n", lowToHigh, midToHigh);
    CHECK_NEAR (lowToHigh, 36.0, 6.0);
    CHECK_NEAR (midToHigh, 12.0, 4.0);
    const double flatRatio = 10.0 * std::log10 (bandPower (withoutCurve, 1900.0, 2100.0) / bandPower (withoutCurve, 190.0, 210.0));
    CHECK (std::fabs (flatRatio - lowToHigh) > 6.0); // without the curve the PZ SVF has its own response
    // moving the cutoff moves the curve along the frequency axis
    e->reset();
    setByKey (*e, "filter1.cutoff", 2000.0f);
    const auto shifted = play (*e, 0.8);
    CHECK (relativeDifference (withCurve, shifted, 0.25, 0.65) > 0.3);
}

TEST (all_43_remaining_types_are_stable_and_differ_from_each_other_and_from_analog)
{
    auto e = makeVital();
    base (*e);
    const auto analog = play (*e);
    std::vector<sbtest::Rendered> renders;
    setByKey (*e, "filter1.var", 85.0f);
    for (const Style& type : kNewTypes43)
    {
        e->reset();
        setByKey (*e, "filter1.model", static_cast<float> (type.model));
        setByKey (*e, "filter1.style", static_cast<float> (type.style));
        const auto out = play (*e);
        CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
        CHECK (sbtest::peak (out.left) < 4.0f);
        if (rms (out.left) < 1.0e-4)
            std::printf ("    silent: %s\n", type.name);
        CHECK (rms (out.left) > 1.0e-4);
        if (relativeDifference (analog, out) <= 0.005)
            std::printf ("    equals the Analog model: %s\n", type.name);
        CHECK (relativeDifference (analog, out) > 0.005);
        renders.push_back (out);
    }
    // every type differs from every other one, also from the 37 types of the models 8 .. 21. Two settings are rendered: the one above and a second one
    // (more resonance, drive, blend, other Var) that wakes up what the first leaves neutral (blend of PZ SVF, FAT of the state variable filters);
    // two types are only the same type if they are equal at both
    std::vector<Style> all (std::begin (kNewTypes43), std::end (kNewTypes43));
    all.insert (all.end(), std::begin (kTypes), std::end (kTypes));
    for (const Style& type : kTypes)
    {
        e->reset();
        setByKey (*e, "filter1.model", static_cast<float> (type.model));
        setByKey (*e, "filter1.style", static_cast<float> (type.style));
        renders.push_back (play (*e));
    }
    std::vector<sbtest::Rendered> second;
    setByKey (*e, "filter1.resonance", 70.0f);
    setByKey (*e, "filter1.drive", 8.0f);
    setByKey (*e, "filter1.blend", 100.0f);
    setByKey (*e, "filter1.var", 15.0f);
    for (const Style& type : all)
    {
        e->reset();
        setByKey (*e, "filter1.model", static_cast<float> (type.model));
        setByKey (*e, "filter1.style", static_cast<float> (type.style));
        second.push_back (play (*e));
    }
    for (size_t i = 0; i < renders.size(); ++i)
        for (size_t j = i + 1; j < renders.size(); ++j)
        {
            const double d = std::max (relativeDifference (renders[i], renders[j]), relativeDifference (second[i], second[j]));
            if (d <= 0.005)
                std::printf ("    equal renders: %s and %s\n", all[i].name, all[j].name);
            CHECK (d > 0.005);
        }
    // style beyond the model's own clamps to its last
    e->reset();
    setByKey (*e, "filter1.model", 23.0f); // Multi Morph has 4 styles (0..3)
    setByKey (*e, "filter1.style", 3.0f);  // style 3: BPN
    const auto bpn = play (*e);
    e->reset();
    setByKey (*e, "filter1.style", 9.0f);  // clamped to 3
    CHECK (relativeDifference (bpn, play (*e)) < 1.0e-6);
}

TEST (remaining_models_survive_extreme_settings_and_work_in_all_three_filter_slots)
{
    auto e = makeVital();
    sbtest::Rng rng (42);
    base (*e);
    setByKey (*e, "osc2.on", 1.0f);
    setByKey (*e, "osc2.destination", 1.0f);
    setByKey (*e, "fxfilter.on", 1.0f);
    setByKey (*e, "filter2.on", 1.0f);
    for (int round = 0; round < 24; ++round)
    {
        for (const char* name : { "filter1", "filter2", "fxfilter" })
        {
            const std::string k = std::string (name) + ".";
            const Style& type = kNewTypes43[rng.u32() % 43];
            setByKey (*e, (k + "model").c_str(), static_cast<float> (type.model));
            setByKey (*e, (k + "style").c_str(), static_cast<float> (type.style));
            setByKey (*e, (k + "cutoff").c_str(), static_cast<float> (20.0 + rng.uniform() * 19980.0));
            setByKey (*e, (k + "resonance").c_str(), static_cast<float> (rng.uniform() * 100.0));
            setByKey (*e, (k + "drive").c_str(), static_cast<float> (rng.uniform() * 20.0));
            setByKey (*e, (k + "var").c_str(), static_cast<float> (rng.uniform() * 100.0));
            setByKey (*e, (k + "pan").c_str(), static_cast<float> (rng.uniform() * 200.0 - 100.0));
            setByKey (*e, (k + "level").c_str(), static_cast<float> (rng.uniform() * 40.0 - 20.0));
        }
        setByKey (*e, "osc1.destination", static_cast<float> (rng.u32() % 4));
        const auto out = sbtest::render (*e, { noteOn (0, 30 + static_cast<int> (rng.u32() % 50)), noteOn (0, 70) }, 14000, 200);
        CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
        CHECK (sbtest::peak (out.left) < 4.0f);
    }
}

TEST (all_43_remaining_types_stay_finite_and_bounded_at_the_corners_of_every_control_in_all_three_slots)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc2.on", 1.0f);
    setByKey (*e, "osc2.destination", 1.0f);
    setByKey (*e, "fxfilter.on", 1.0f);
    setByKey (*e, "filter2.on", 1.0f);
    const float cutoffs[] = { 20.0f, 20000.0f };
    const float resonances[] = { 0.0f, 100.0f };
    const float drives[] = { 0.0f, 20.0f };
    const float vars[] = { 0.0f, 100.0f };
    int slotIndex = 0;
    for (const char* name : { "filter1", "filter2", "fxfilter" })
    {
        const std::string k = std::string (name) + ".";
        for (const Style& type : kNewTypes43)
            for (const float cutoff : cutoffs)
                for (const float resonance : resonances)
                    for (const float drive : drives)
                        for (const float var : vars)
                        {
                            e->reset();
                            setByKey (*e, (k + "model").c_str(), static_cast<float> (type.model));
                            setByKey (*e, (k + "style").c_str(), static_cast<float> (type.style));
                            setByKey (*e, (k + "cutoff").c_str(), cutoff);
                            setByKey (*e, (k + "resonance").c_str(), resonance);
                            setByKey (*e, (k + "drive").c_str(), drive);
                            setByKey (*e, (k + "var").c_str(), var);
                            // the three slots are fed: osc1 -> filter 1, osc2 -> filter 2; the FX filter sits in the effect chain
                            setByKey (*e, "osc1.destination", slotIndex == 2 ? 3.0f : 0.0f);
                            setByKey (*e, "osc2.destination", slotIndex == 1 ? 1.0f : 3.0f);
                            const auto out = sbtest::render (*e, { noteOn (0, 40), noteOn (0, 88) }, 9000, 256);
                            const bool ok = sbtest::allFinite (out.left) && sbtest::allFinite (out.right) && sbtest::peak (out.left) < 4.0f
                                            && sbtest::peak (out.right) < 4.0f;
                            if (! ok)
                                std::printf ("    unstable: %s in %s cutoff %.0f res %.0f drive %.0f var %.0f\n", type.name, name, cutoff, resonance, drive, var);
                            CHECK (ok);
                        }
        ++slotIndex;
    }
}

TEST (the_drawn_filter_curve_is_ignored_by_every_type_but_the_pz_svf)
{
    // the curve of content.filter_curves belongs to PZ SVF only: a 43-type model with a curve in the content renders as without it
    auto e = makeVital();
    base (*e);
    setByKey (*e, "filter1.model", 22.0f);
    setByKey (*e, "filter1.style", 2.0f);
    const auto without = play (*e);
    StateContent content;
    ContentFilterCurve curve;
    curve.filter = "filter1";
    curve.points = { { 0.0f, -30.0f }, { 1.0f, 12.0f } };
    content.filterCurves.push_back (curve);
    e->setContent (content);
    e->reset();
    CHECK (relativeDifference (without, play (*e)) < 1.0e-4);
}
