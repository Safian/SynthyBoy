// SynthyBoy - engine-vital tests: B24 (GROUPA1, WP-09): MPE completion - XYZ -> Macro 1, 2, 3 / YZ -> Macro 1, 2, Y -> Mod Wheel, Pitch Bend also to X,
// Y bi-directional (13-new-keys.md 4.28). Proves kCapMpeRouting: the keys are rendered off and on and must make a measured difference.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
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

int frames (double seconds) { return static_cast<int> (seconds * kSr); }

Event event (Event::Type type, int frame, int channel, int key, float value, Event::Expression expression = Event::Tuning)
{
    Event e = noteOn (frame, key, value, channel);
    e.type = type;
    e.expression = expression;
    return e;
}

Event expression (int frame, int channel, Event::Expression kind, float value) { return event (Event::NoteExpression, frame, channel, -1, value, kind); }

/** Renders blocks of 256 with the events at their frames. */
sbtest::Rendered play (IEngine& e, const std::vector<Event>& events, int total)
{
    sbtest::Rendered out;
    out.left.assign (static_cast<size_t> (total), 0.0f);
    out.right.assign (static_cast<size_t> (total), 0.0f);
    std::vector<Event> local;
    size_t idx = 0;
    for (int start = 0; start < total; start += 256)
    {
        const int n = std::min (256, total - start);
        local.clear();
        while (idx < events.size() && events[idx].frame < start + n)
        {
            auto ev = events[idx++];
            ev.frame -= start;
            local.push_back (ev);
        }
        e.process (local.data(), local.size(), out.left.data() + start, out.right.data() + start, n);
    }
    return out;
}

double macro (IEngine& e, int n) { return vitalControlValue (e, ("macro_control_" + std::to_string (n)).c_str()); }

double rmsFrom (const sbtest::Rendered& r, double t0, double t1)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

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

/** One note that does not play (osc 1 off): only the controls are looked at. */
std::unique_ptr<IEngine> quiet()
{
    auto e = makeVital();
    setByKey (*e, "osc1.on", 0.0f);
    return e;
}

const std::vector<Event> kXyz { noteOn (0, 60, 1.0f, 1), expression (100, 1, Event::Pan, 0.8f), expression (100, 1, Event::Brightness, 0.6f),
                                expression (100, 1, Event::Pressure, 0.4f) };
}

TEST (mpe_routing_capability_and_keys)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapMpeRouting) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & kCapMpeRouting) == 0);
    for (const char* key : { "master.mpe.xyz_macro", "master.mpe.y_modwheel", "master.mpe.bend_to_x", "master.mpe.y_bipolar" })
        CHECK (findParamByKey (key) != nullptr);
}

TEST (mpe_xyz_drives_macros_1_2_3_and_yz_drives_1_2)
{
    // off: the axes do not touch the macros
    {
        auto e = quiet();
        setByKey (*e, "master.mpe.on", 1.0f);
        (void) play (*e, kXyz, frames (0.2));
        CHECK_NEAR (macro (*e, 1), 0.0, 1.0e-6);
        CHECK_NEAR (macro (*e, 2), 0.0, 1.0e-6);
        CHECK_NEAR (macro (*e, 3), 0.0, 1.0e-6);
    }
    // XYZ -> Macro 1, 2, 3 (X = pan, Y = slide, Z = pressure), with MPE on and with MPE off (the host's note expressions, manual p.325)
    for (const bool mpe : { true, false })
    {
        auto e = quiet();
        setByKey (*e, "master.mpe.on", mpe ? 1.0f : 0.0f);
        setByKey (*e, "master.mpe.xyz_macro", 1.0f);
        (void) play (*e, kXyz, frames (0.2));
        CHECK_NEAR (macro (*e, 1), 0.8, 1.0e-4);
        CHECK_NEAR (macro (*e, 2), 0.6, 1.0e-4);
        CHECK_NEAR (macro (*e, 3), 0.4, 1.0e-4);
        CHECK_NEAR (macro (*e, 4), 0.0, 1.0e-6);
    }
    // YZ -> Macro 1, 2: Y on macro 1, Z on macro 2, macro 3 stays on its knob
    {
        auto e = quiet();
        setByKey (*e, "master.mpe.xyz_macro", 2.0f);
        setByKey (*e, "macro.3", 70.0f);
        (void) play (*e, kXyz, frames (0.2));
        CHECK_NEAR (macro (*e, 1), 0.6, 1.0e-4);
        CHECK_NEAR (macro (*e, 2), 0.4, 1.0e-4);
        CHECK_NEAR (macro (*e, 3), 0.7, 1.0e-4);
    }
    // the member channel's pressure (channel aftertouch) is Z with MPE on, and a plain channel pressure of the manager is not
    {
        auto e = quiet();
        setByKey (*e, "master.mpe.on", 1.0f);
        setByKey (*e, "master.mpe.xyz_macro", 1.0f);
        (void) play (*e, { noteOn (0, 60, 1.0f, 1), event (Event::Aftertouch, 100, 1, 0, 0.9f) }, frames (0.2));
        CHECK_NEAR (macro (*e, 3), 0.9, 1.0e-4);
        auto m = quiet();
        setByKey (*m, "master.mpe.on", 1.0f);
        setByKey (*m, "master.mpe.xyz_macro", 1.0f);
        (void) play (*m, { noteOn (0, 60, 1.0f, 0), event (Event::Aftertouch, 100, 0, 0, 0.9f) }, frames (0.2));
        CHECK_NEAR (macro (*m, 3), 0.0, 1.0e-6);
    }
}

TEST (mpe_switching_the_mapping_off_gives_the_macros_back_to_their_knobs)
{
    auto e = quiet();
    setByKey (*e, "master.mpe.xyz_macro", 1.0f);
    setByKey (*e, "macro.1", 25.0f);
    (void) play (*e, kXyz, frames (0.2));
    CHECK_NEAR (macro (*e, 1), 0.8, 1.0e-4);
    setByKey (*e, "master.mpe.xyz_macro", 0.0f);
    (void) play (*e, {}, frames (0.1));
    CHECK_NEAR (macro (*e, 1), 0.25, 1.0e-4);   // the knob again
    CHECK_NEAR (macro (*e, 2), 0.0, 1.0e-4);
    // the mapping is audible: macro 1 opens the oscillator, the X axis plays it
    auto s = makeVital();
    setByKey (*s, "osc1.wavetable", 0.0f);
    setByKey (*s, "osc1.level", 0.0f);
    setByKey (*s, "osc1.unison.voices", 1.0f);
    setByKey (*s, "master.volume", -6.0f);
    route (*s, 1, "macro1", "osc1.level", 100.0f);
    const std::vector<Event> x { noteOn (0, 69, 1.0f, 1), expression (frames (0.2), 1, Event::Pan, 1.0f) };
    const auto closed = play (*s, x, frames (0.5));
    CHECK (rmsFrom (closed, 0.3, 0.5) < 0.004);
    auto t = makeVital();
    setByKey (*t, "osc1.wavetable", 0.0f);
    setByKey (*t, "osc1.level", 0.0f);
    setByKey (*t, "osc1.unison.voices", 1.0f);
    setByKey (*t, "master.volume", -6.0f);
    route (*t, 1, "macro1", "osc1.level", 100.0f);
    setByKey (*t, "master.mpe.xyz_macro", 1.0f);
    const auto open = play (*t, x, frames (0.5));
    CHECK (rmsFrom (open, 0.3, 0.5) > 0.02);
}

TEST (mpe_y_to_mod_wheel_and_y_bipolar)
{
    // Y -> Mod Wheel: the slide of a member channel opens a level that the mod wheel drives
    const std::vector<Event> y { noteOn (0, 69, 1.0f, 1), expression (frames (0.2), 1, Event::Brightness, 1.0f) };
    const auto build = [] (bool toWheel)
    {
        auto e = makeVital();
        setByKey (*e, "osc1.wavetable", 0.0f);
        setByKey (*e, "osc1.level", 0.0f);
        setByKey (*e, "osc1.unison.voices", 1.0f);
        setByKey (*e, "master.volume", -6.0f);
        route (*e, 1, "mod_wheel", "osc1.level", 100.0f);
        setByKey (*e, "master.mpe.y_modwheel", toWheel ? 1.0f : 0.0f);
        return e;
    };
    CHECK (rmsFrom (play (*build (false), y, frames (0.5)), 0.3, 0.5) < 0.004);
    CHECK (rmsFrom (play (*build (true), y, frames (0.5)), 0.3, 0.5) > 0.02);

    // Y bi-directional: unipolar Y is the value, bi-directional Y is the deflection from the middle
    const std::vector<std::pair<float, std::pair<double, double>>> cases { { 0.0f, { 0.0, 1.0 } }, { 0.5f, { 0.5, 0.0 } }, { 0.75f, { 0.75, 0.5 } }, { 1.0f, { 1.0, 1.0 } } };
    for (const auto& c : cases)
        for (const bool bipolar : { false, true })
        {
            auto e = quiet();
            setByKey (*e, "master.mpe.xyz_macro", 1.0f);
            setByKey (*e, "master.mpe.y_bipolar", bipolar ? 1.0f : 0.0f);
            (void) play (*e, { noteOn (0, 60, 1.0f, 1), expression (100, 1, Event::Brightness, c.first) }, frames (0.2));
            const double expected = bipolar ? c.second.second : c.second.first;
            if (! CHECK_NEAR (macro (*e, 2), expected, 1.0e-4))
                std::printf ("    Y %.2f bipolar %d: macro 2 = %.4f, expected %.4f\n", static_cast<double>(c.first), bipolar ? 1 : 0, macro (*e, 2), expected);
        }
}

TEST (mpe_pitch_bend_also_maps_to_x)
{
    // the wheel of a member channel is the X axis as well (MPE on): -1 .. 1 is 0 .. 1 with 0.5 at rest
    for (const bool alsoX : { false, true })
    {
        auto e = quiet();
        setByKey (*e, "master.mpe.on", 1.0f);
        setByKey (*e, "master.mpe.xyz_macro", 1.0f);
        setByKey (*e, "master.mpe.bend_to_x", alsoX ? 1.0f : 0.0f);
        (void) play (*e, { noteOn (0, 60, 1.0f, 1), sbtest::pitchBend (100, 0.5f, 1) }, frames (0.2));
        CHECK_NEAR (macro (*e, 1), alsoX ? 0.75 : 0.0, 1.0e-4);
        (void) play (*e, { sbtest::pitchBend (0, -1.0f, 1) }, frames (0.1));
        CHECK_NEAR (macro (*e, 1), alsoX ? 0.0 : 0.0, 1.0e-4);
    }
    // the manager channel's wheel is not an axis; with MPE off the wheel is the plain wheel
    auto m = quiet();
    setByKey (*m, "master.mpe.on", 1.0f);
    setByKey (*m, "master.mpe.xyz_macro", 1.0f);
    setByKey (*m, "master.mpe.bend_to_x", 1.0f);
    (void) play (*m, { noteOn (0, 60, 1.0f, 0), sbtest::pitchBend (100, 0.5f, 0) }, frames (0.2));
    CHECK_NEAR (macro (*m, 1), 0.0, 1.0e-6);
    // the per-note bend still bends the pitch (the mapping is an addition, not a replacement)
    auto p = makeVital();
    setByKey (*p, "osc1.wavetable", 0.0f);
    setByKey (*p, "osc1.level", 100.0f);
    setByKey (*p, "osc1.unison.voices", 1.0f);
    setByKey (*p, "filter1.on", 0.0f);
    setByKey (*p, "master.mpe.on", 1.0f);
    setByKey (*p, "master.mpe.bend_range", 12.0f);
    setByKey (*p, "master.mpe.xyz_macro", 1.0f);
    setByKey (*p, "master.mpe.bend_to_x", 1.0f);
    const auto out = play (*p, { noteOn (0, 69, 1.0f, 1), sbtest::pitchBend (frames (0.1), 0.5f, 1) }, frames (1.0));
    const auto spectrum = sbtest::powerSpectrum (out.left, static_cast<size_t> (0.4 * kSr), 32768, 131072);
    CHECK_NEAR (sbtest::peakFrequency (spectrum, 131072, kSr, 440.0 * std::pow (2.0, 6.0 / 12.0)), 440.0 * std::pow (2.0, 6.0 / 12.0), 1.0);
}
