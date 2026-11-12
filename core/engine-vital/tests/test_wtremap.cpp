// SynthyBoy - engine-vital tests: block B16 (WTREMAP) of docs/ui-design/13-new-keys.md 4.14 / 4.15: the wavetable position remap with RISE / FALL
// (G69), the output LEVEL of the Vital effects (G72) and Clean Mode (G54).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// kCapWtRemap, kCapFxLevel and kCapCleanMode are proven by rendering the keys off and on and asserting a measurable difference of the audio.

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/state.h"
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

/** One oscillator on a table that changes a lot over its frames (sine to saw), straight to the effects, no filter. */
void base (IEngine& e, float framePercent)
{
    setByKey (e, "osc1.wavetable", 6.0f);
    setByKey (e, "osc1.frame", framePercent);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f);
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

ContentRemap remapCurve (int osc, int slot, std::vector<ContentPoint> points)
{
    ContentRemap r;
    r.osc = osc;
    r.slot = slot;
    r.name = "test";
    r.points = std::move (points);
    return r;
}

sbtest::Rendered play (IEngine& e, double seconds = 0.7, int key = 45)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

sbtest::Rendered more (IEngine& e, double seconds)
{
    return sbtest::render (e, {}, static_cast<int> (seconds * kSr), 256);
}

double rms (const sbtest::Rendered& r, double t0 = 0.25, double t1 = 0.65)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
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

double dbOf (double ratio) { return 20.0 * std::log10 (ratio); }

void route (IEngine& e, int slot, const char* source, const char* destinationKey, float amountPercent)
{
    char key[32];
    std::snprintf (key, sizeof key, "mod%02d.source", slot);
    setByKey (e, key, sbtest::sourceChoice (source));
    std::snprintf (key, sizeof key, "mod%02d.destination", slot);
    const ParamInfo* destination = findParamByKey (destinationKey);
    CHECK (destination != nullptr);
    setByKey (e, key, destination != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))) : 0.0f);
    std::snprintf (key, sizeof key, "mod%02d.amount", slot);
    setByKey (e, key, amountPercent);
}
} // namespace

TEST (wtremap_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapWtRemap) != 0);
    CHECK ((e->capabilities() & kCapFxLevel) != 0);
    CHECK ((e->capabilities() & kCapCleanMode) != 0);
    auto own = createEngine (EngineKind::own);
    for (size_t i = 0; i < paramCount(); ++i) // the own engine never claims more than its capability words say
        if (own->supportsParam (paramTable()[i].id))
            CHECK (capsCover (own->capabilities(), requiredCaps (i)) && capsCover2 (own->capabilities2(), requiredCaps2 (i)));
}

TEST (remap_changes_the_frame_the_oscillator_plays)
{
    // a curve that maps every position to the last frame: frame 0 plays like frame 100 %
    auto reference = makeVital();
    base (*reference, 100.0f);
    const auto atEnd = play (*reference);

    auto plain = makeVital();
    base (*plain, 0.0f);
    const auto atStart = play (*plain);
    CHECK (relativeDifference (atEnd, atStart) > 0.3); // the table really changes along the frame (sine to saw)

    StateContent content;
    content.remaps.push_back (remapCurve (1, 1, { { 0.0f, 1.0f }, { 1.0f, 1.0f } }));
    CHECK (validateContent (content));

    auto remapped = makeVital();
    base (*remapped, 0.0f);
    remapped->setContent (content);
    setByKey (*remapped, "osc1.remap.on", 1.0f);
    const auto r = play (*remapped);
    const double toEnd = relativeDifference (atEnd, r), toStart = relativeDifference (atStart, r);
    std::printf ("    remap flat at 1: difference to frame 100 %% = %.4f, to frame 0 = %.3f\n", toEnd, toStart);
    CHECK (toEnd < 0.02);
    CHECK (toStart > 0.3); // the audible difference between the remap off and on

    // off again: untouched
    auto off = makeVital();
    base (*off, 0.0f);
    off->setContent (content);
    const auto o = play (*off);
    CHECK (relativeDifference (atStart, o) < 1.0e-6);

    // an inverting curve: frame 25 % remapped = frame 75 %
    StateContent inverse;
    inverse.remaps.push_back (remapCurve (1, 1, { { 0.0f, 1.0f }, { 1.0f, 0.0f } }));
    auto inv = makeVital();
    base (*inv, 25.0f);
    inv->setContent (inverse);
    setByKey (*inv, "osc1.remap.on", 1.0f);
    const auto invRender = play (*inv);
    auto at75 = makeVital();
    base (*at75, 75.0f);
    const auto r75 = play (*at75);
    auto at25 = makeVital();
    base (*at25, 25.0f);
    const auto r25 = play (*at25);
    CHECK (relativeDifference (r75, invRender) < 0.02);
    CHECK (relativeDifference (r25, invRender) > 0.2);
}

TEST (remap_slot_picks_the_drawn_curve_and_an_empty_slot_is_the_identity)
{
    StateContent content;
    content.remaps.push_back (remapCurve (1, 1, { { 0.0f, 1.0f }, { 1.0f, 1.0f } }));
    content.remaps.push_back (remapCurve (1, 3, { { 0.0f, 0.0f }, { 1.0f, 0.0f } }));

    auto plain = makeVital();
    base (*plain, 50.0f);
    const auto identity = play (*plain);

    const auto render = [&] (float slot, int osc2Remap)
    {
        auto e = makeVital();
        base (*e, 50.0f);
        e->setContent (content);
        setByKey (*e, "osc1.remap.on", 1.0f);
        setByKey (*e, "osc1.remap.slot", slot);
        setByKey (*e, "osc2.remap.on", static_cast<float> (osc2Remap)); // osc 2 is off: its remap must not matter
        return play (*e);
    };
    const auto slot1 = render (0.0f, 1), slot2 = render (1.0f, 0), slot3 = render (2.0f, 0), slot4 = render (3.0f, 0);
    CHECK (relativeDifference (identity, slot1) > 0.2);    // Remap 1 maps to the last frame
    CHECK (relativeDifference (identity, slot2) < 1.0e-6); // Remap 2: nothing drawn, identity
    CHECK (relativeDifference (identity, slot3) > 0.2);    // Remap 3 maps to the first frame
    CHECK (relativeDifference (slot1, slot3) > 0.3);
    CHECK (relativeDifference (identity, slot4) < 1.0e-6);
}

TEST (remap_rise_and_fall_glide_the_position)
{
    // identity remap (nothing drawn), the frame knob jumps after 0.3 s: RISE slows an upward jump only, FALL a downward one only
    const auto jump = [] (float from, float to, float rise, float fall)
    {
        auto e = makeVital();
        base (*e, from);
        setByKey (*e, "osc1.remap.on", 1.0f);
        setByKey (*e, "osc1.remap.rise", rise);
        setByKey (*e, "osc1.remap.fall", fall);
        play (*e, 0.3);
        setByKey (*e, "osc1.frame", to);
        return more (*e, 0.4);
    };
    const auto upInstant = jump (0.0f, 100.0f, 0.0f, 0.0f);
    const auto upSlow = jump (0.0f, 100.0f, 1500.0f, 0.0f);
    const auto upFallOnly = jump (0.0f, 100.0f, 0.0f, 1500.0f);
    CHECK (relativeDifference (upInstant, upSlow, 0.02, 0.2) > 0.2);       // RISE: still near the first frame 20 ms .. 200 ms after the jump
    CHECK (relativeDifference (upInstant, upFallOnly, 0.02, 0.2) < 1.0e-3); // FALL does not touch an upward move

    const auto downInstant = jump (100.0f, 0.0f, 0.0f, 0.0f);
    const auto downSlow = jump (100.0f, 0.0f, 0.0f, 1500.0f);
    const auto downRiseOnly = jump (100.0f, 0.0f, 1500.0f, 0.0f);
    CHECK (relativeDifference (downInstant, downSlow, 0.02, 0.2) > 0.2);
    CHECK (relativeDifference (downInstant, downRiseOnly, 0.02, 0.2) < 1.0e-3);

    // the glide arrives: seconds later the position is at the target (RISE 100 ms)
    auto e = makeVital();
    base (*e, 0.0f);
    setByKey (*e, "osc1.remap.on", 1.0f);
    setByKey (*e, "osc1.remap.rise", 100.0f);
    play (*e, 0.3);
    setByKey (*e, "osc1.frame", 100.0f);
    more (*e, 1.0);
    const auto settled = more (*e, 0.3);
    auto target = makeVital();
    base (*target, 100.0f);
    play (*target, 1.3);
    const auto reached = more (*target, 0.3);
    CHECK (relativeDifference (reached, settled, 0.0, 0.3) < 0.05);

    // a new note jumps to its position instead of gliding from the last one
    auto n = makeVital();
    base (*n, 100.0f);
    setByKey (*n, "osc1.remap.on", 1.0f);
    setByKey (*n, "osc1.remap.rise", 1500.0f);
    setByKey (*n, "osc1.remap.fall", 1500.0f);
    const auto firstNote = play (*n, 0.7);
    auto instant = makeVital();
    base (*instant, 100.0f);
    const auto direct = play (*instant, 0.7);
    CHECK (relativeDifference (direct, firstNote) < 0.02);
}

TEST (remap_content_round_trips_and_is_validated)
{
    StateContent content;
    content.remaps.push_back (remapCurve (2, 4, { { 0.0f, 0.0f, 0.0f }, { 0.4f, 0.9f, 2.0f }, { 1.0f, 1.0f, 0.0f } }));
    content.remaps.push_back (remapCurve (3, 1, { { 0.0f, 1.0f }, { 1.0f, 0.0f } }));
    CHECK (validateContent (content));

    const ParamValues values = ParamValues::defaults();
    const std::string text = saveState (values, content);
    CHECK (text.find ("\"remaps\"") != std::string::npos);

    ParamValues loaded = values;
    StateContent back;
    const LoadResult r = loadStateWithContent (text.data(), text.size(), loaded, back);
    CHECK (r.status == LoadStatus::ok);
    CHECK (r.content == ContentStatus::ok);
    CHECK (back == content);

    StateContent bad = content;
    bad.remaps[0].slot = 5;
    CHECK (! validateContent (bad));
    bad = content;
    bad.remaps[1].osc = 2;
    bad.remaps[1].slot = 4; // the same oscillator and slot twice
    CHECK (! validateContent (bad));
    bad = content;
    bad.remaps[0].points.resize (1);
    CHECK (! validateContent (bad));
    bad = content;
    bad.remaps[0].points[1].y = 1.5f;
    CHECK (! validateContent (bad));
}

TEST (fx_level_trims_every_module_output)
{
    struct Module { const char* key; };
    const Module modules[] = { { "chorus" }, { "compressor" }, { "delay" }, { "distortion" }, { "eq" }, { "flanger" }, { "phaser" }, { "reverb" } };
    for (const Module& m : modules)
    {
        const auto render = [&] (float level)
        {
            auto e = makeVital();
            base (*e, 100.0f);
            setByKey (*e, (std::string (m.key) + ".on").c_str(), 1.0f);
            setByKey (*e, (std::string (m.key) + ".level").c_str(), level);
            return play (*e, 0.9);
        };
        const auto zero = render (0.0f), down = render (-12.0f), up = render (6.0f);
        const double a = rms (zero, 0.3, 0.85);
        const double dDown = dbOf (rms (down, 0.3, 0.85) / a), dUp = dbOf (rms (up, 0.3, 0.85) / a);
        std::printf ("    %s: -12 dB -> %.2f dB, +6 dB -> %.2f dB\n", m.key, dDown, dUp);
        CHECK (a > 1.0e-4);
        CHECK_NEAR (dDown, -12.0, 0.6);
        CHECK_NEAR (dUp, 6.0, 0.6);
    }

    // a module that is off has no level (the key is inert)
    auto reference = makeVital();
    base (*reference, 100.0f);
    const auto withoutKey = play (*reference);
    auto off = makeVital();
    base (*off, 100.0f);
    setByKey (*off, "chorus.level", -24.0f);
    CHECK (relativeDifference (withoutKey, play (*off)) < 1.0e-6);
}

TEST (fx_level_is_a_modulation_destination)
{
    CHECK ((findParamByKey ("chorus.level")->flags & kParamModulatable) != 0);
    const auto render = [] (float macro)
    {
        auto e = makeVital();
        base (*e, 100.0f);
        setByKey (*e, "chorus.on", 1.0f);
        route (*e, 1, "macro1", "chorus.level", -100.0f);
        setByKey (*e, "macro.1", macro);
        return play (*e, 0.9);
    };
    const double open = rms (render (0.0f), 0.3, 0.85), closed = rms (render (100.0f), 0.3, 0.85);
    std::printf("    chorus.level by macro 1: %.1f dB\n", dbOf (closed / open));
    CHECK (dbOf (closed / open) < -15.0);
}

TEST (clean_mode_feeds_the_filter_24_db_down_and_boosts_24_db_after_it)
{
    // Serum 2 manual p.142 / p.171: "the filter pre-gain stage the filter input -24 dB (with a +24 dB boost post-filter). This reduces saturation
    // and input drive in the filter models."
    for (const char* filter : { "filter1", "fxfilter" })
    {
        const std::string f = filter;
        const bool fx = f == "fxfilter";
        const auto render = [&] (float drive, float clean, float model, float level)
        {
            auto e = makeVital();
            base (*e, 100.0f);
            setByKey (*e, "osc1.level", level);
            setByKey (*e, "osc1.destination", fx ? 3.0f : 0.0f);
            setByKey (*e, (f + ".on").c_str(), 1.0f);
            setByKey (*e, (f + ".model").c_str(), model);
            setByKey (*e, (f + ".cutoff").c_str(), 1200.0f);
            setByKey (*e, (f + ".resonance").c_str(), 40.0f);
            setByKey (*e, (f + ".keytrack").c_str(), 0.0f);
            setByKey (*e, (f + ".drive").c_str(), drive);
            setByKey (*e, (f + ".clean_mode").c_str(), clean);
            return play (*e);
        };
        // a hot signal with drive saturates the filter; the clean mode takes the saturation away
        const auto driven = render (12.0f, 0.0f, 0.0f, 100.0f), clean = render (12.0f, 1.0f, 0.0f, 100.0f);
        const double difference = relativeDifference (driven, clean);
        std::printf ("    %s: clean on vs off at 12 dB drive: relative difference %.3f\n", filter, difference);
        CHECK (difference > 0.05); // the audible difference between clean mode off and on

        // a quiet signal stays in the linear range: -24 dB in and +24 dB out cancel, on and off are the same sound
        const auto quietOff = render (0.0f, 0.0f, 0.0f, 1.0f), quietOn = render (0.0f, 1.0f, 0.0f, 1.0f);
        const double quiet = relativeDifference (quietOff, quietOn);
        std::printf ("    %s: clean on vs off for a quiet signal: relative difference %.4f\n", filter, quiet);
        CHECK (quiet < 0.05);

        // the clean output is a plain multiple of the off output in the linear range, so the level is the same (not +drive dB)
        const double level = dbOf (rms (quietOn) / rms (quietOff));
        std::printf ("    %s: clean on / off level for a quiet signal: %.2f dB\n", filter, level);
        CHECK_NEAR (level, 0.0, 0.5);

        // the comb filter has no drive stage: the switch does nothing
        CHECK (relativeDifference (render (12.0f, 0.0f, 6.0f, 100.0f), render (12.0f, 1.0f, 6.0f, 100.0f)) < 1.0e-6);
    }
}
