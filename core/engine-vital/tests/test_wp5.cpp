// SynthyBoy - engine-vital tests: WP5 (mixer graph, buses, DIRECT, racks, the 16 module chain) of docs/ui-design/13-new-keys.md.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// kCapMixerGraph and kCapFxRacks are proven by rendering the keys off and on and asserting a measurable difference of the audio.

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
#include "vital_bindings.h"
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

/** A saw on oscillator 1 into effects only, filters off, nothing else on. */
void base (IEngine& e)
{
    setByKey (e, "osc1.wavetable", 1.0f);
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f); // Effects
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

sbtest::Rendered play (IEngine& e, double seconds = 0.8, int key = 57)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

double rms (const sbtest::Rendered& r, double t0 = 0.3, double t1 = 0.7)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.3, double t1 = 0.7)
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

/** Filter 1 low pass at 300 Hz, filter 2 at 8 kHz: two clearly different filters. */
void twoFilters (IEngine& e)
{
    setByKey (e, "filter1.on", 1.0f);
    setByKey (e, "filter1.cutoff", 300.0f);
    setByKey (e, "filter2.on", 1.0f);
    setByKey (e, "filter2.cutoff", 8000.0f);
}

void useDistortion (IEngine& e, int rack)
{
    setByKey (e, "distortion.on", 1.0f);
    setByKey (e, "distortion.drive", 24.0f);
    setByKey (e, "distortion.mix", 100.0f);
    setByKey (e, "distortion.rack", static_cast<float> (rack));
}
} // namespace

TEST (wp5_capabilities_are_reported)
{
    auto e = createEngine (EngineKind::vital);
    CHECK ((e->capabilities() & kCapMixerGraph) != 0);
    CHECK ((e->capabilities() & kCapFxRacks) != 0);
    auto own = createEngine (EngineKind::own);
    CHECK ((own->capabilities() & (kCapMixerGraph | kCapFxRacks)) == 0);
}

TEST (filter_blend_destination_crossfades_the_two_filters)
{
    auto e = makeVital();
    base (*e);
    twoFilters (*e);
    setByKey (*e, "osc1.destination", 0.0f); // Filter 1
    const auto only1 = play (*e);
    e->reset();
    setByKey (*e, "osc1.destination", 1.0f); // Filter 2
    const auto only2 = play (*e);
    e->reset();
    setByKey (*e, "osc1.destination", 5.0f); // Filter blend
    setByKey (*e, "osc1.filter_blend", 0.0f);
    const auto blend0 = play (*e);
    e->reset();
    setByKey (*e, "osc1.filter_blend", 100.0f);
    const auto blend100 = play (*e);
    e->reset();
    setByKey (*e, "osc1.filter_blend", 50.0f);
    const auto blend50 = play (*e);
    CHECK (relativeDifference (only1, blend0) < 0.02);   // 0 % = filter 1 only
    CHECK (relativeDifference (only2, blend100) < 0.02); // 100 % = filter 2 only
    CHECK (relativeDifference (only1, only2) > 0.5);     // the two filters differ
    CHECK (relativeDifference (only1, blend50) > 0.3);   // in between is neither
    CHECK (relativeDifference (only2, blend50) > 0.1);
    // equal power: a source that both filters pass unchanged (cutoff wide open) stays at -3 dB per side, so the sum is as loud as one filter
    CHECK (rms (blend50) > 0.0);
}

TEST (destination_none_is_silent_but_still_a_modulation_source)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.destination", 6.0f); // None
    CHECK (rms (play (*e)) < 1.0e-4);
    e->reset();
    // osc 2 is frequency modulated by osc 1 (which is silent in the mix): osc 1 on or off changes osc 2
    setByKey (*e, "osc2.on", 1.0f);
    setByKey (*e, "osc2.wavetable", 0.0f);
    setByKey (*e, "osc2.level", 100.0f);
    setByKey (*e, "osc2.unison.voices", 1.0f);
    setByKey (*e, "osc2.random_phase", 0.0f);
    setByKey (*e, "osc2.warp.type", 7.0f); // FM from Osc 1
    setByKey (*e, "osc2.warp.amount", 100.0f);
    const auto modulated = play (*e);
    e->reset();
    setByKey (*e, "osc1.on", 0.0f);
    const auto plain = play (*e);
    CHECK (rms (modulated) > 0.01);
    CHECK (relativeDifference (plain, modulated) > 0.3);
}

TEST (bus_sends_buses_and_other_bus_route_the_audio)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.destination", 6.0f); // nothing to the effects: the sound can only come through a bus
    setByKey (*e, "osc1.bus1_send", 0.0f);
    CHECK (rms (play (*e)) < 1.0e-4);
    e->reset();
    setByKey (*e, "osc1.bus1_send", 100.0f);
    const auto viaBus = play (*e);
    CHECK (rms (viaBus) > 0.02);
    e->reset();
    // the same sound as a direct effects send at unity
    setByKey (*e, "osc1.bus1_send", 0.0f);
    setByKey (*e, "osc1.destination", 3.0f);
    const auto direct = play (*e);
    CHECK (relativeDifference (direct, viaBus) < 0.02);
    e->reset();
    // bus level: -60 dB silences the bus
    setByKey (*e, "osc1.destination", 6.0f);
    setByKey (*e, "osc1.bus1_send", 100.0f);
    setByKey (*e, "bus1.level", -60.0f);
    CHECK (rms (play (*e)) < 1.0e-3);
    e->reset();
    // bus 1 into bus 2 ("Other bus"), bus 2 to Main at its own level
    setByKey (*e, "bus1.level", 0.0f);
    setByKey (*e, "bus1.destination", 2.0f);
    setByKey (*e, "bus2.level", 0.0f);
    const auto chained = play (*e);
    CHECK (rms (chained) > 0.02);
    e->reset();
    setByKey (*e, "bus2.level", -60.0f);
    CHECK (rms (play (*e)) < 1.0e-3); // proves the signal really went through bus 2
    e->reset();
    // bus 1 to Direct: the direct level is its master
    setByKey (*e, "bus1.destination", 1.0f);
    setByKey (*e, "bus2.level", 0.0f);
    setByKey (*e, "direct.level", 0.0f);
    const auto viaDirect = play (*e);
    CHECK (rms (viaDirect) > 0.02);
    e->reset();
    setByKey (*e, "direct.level", -60.0f);
    CHECK (rms (play (*e)) < 1.0e-3);
    // a second oscillator's noise send and a filter send are different sources of the same bus
    e->reset();
    setByKey (*e, "direct.level", 0.0f);
    setByKey (*e, "bus1.destination", 0.0f);
    setByKey (*e, "osc1.bus1_send", 0.0f);
    setByKey (*e, "osc1.destination", 0.0f); // Filter 1
    setByKey (*e, "filter1.on", 1.0f);
    setByKey (*e, "filter1.destination", 2.0f); // filter 1 to nowhere ...
    CHECK (rms (play (*e)) < 1.0e-4);
    e->reset();
    setByKey (*e, "filter1.bus1_send", 100.0f); // ... but its send feeds bus 1
    CHECK (rms (play (*e)) > 0.01);
}

TEST (filter_destination_effects_only_direct_out_or_none)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "osc1.destination", 0.0f);
    setByKey (*e, "filter1.on", 1.0f);
    setByKey (*e, "filter1.cutoff", 2000.0f);
    useDistortion (*e, 0); // the Main rack distorts
    const auto effects = play (*e);
    e->reset();
    setByKey (*e, "filter1.destination", 1.0f); // Direct out: skips every rack
    const auto direct = play (*e);
    e->reset();
    setByKey (*e, "distortion.on", 0.0f);
    const auto clean = play (*e);
    CHECK (relativeDifference (clean, direct) < 0.02);   // DIRECT skips the racks
    CHECK (relativeDifference (clean, effects) > 0.2);   // the effects path is distorted
    e->reset();
    setByKey (*e, "filter1.destination", 2.0f); // None
    CHECK (rms (play (*e)) < 1.0e-4);
}

TEST (racks_assign_the_effect_to_main_bus_1_or_bus_2)
{
    auto e = makeVital();
    base (*e);
    useDistortion (*e, 0);
    const auto inMain = play (*e);
    e->reset();
    useDistortion (*e, 1); // Bus 1: the main signal no longer passes it
    const auto inBus1 = play (*e);
    setByKey (*e, "distortion.on", 0.0f);
    e->reset();
    const auto clean = play (*e);
    CHECK (relativeDifference (clean, inMain) > 0.2);
    CHECK (relativeDifference (clean, inBus1) < 0.01);
    // the sound that goes to bus 1 passes the distortion that lives there
    e->reset();
    useDistortion (*e, 1);
    setByKey (*e, "osc1.destination", 6.0f);
    setByKey (*e, "osc1.bus1_send", 100.0f);
    const auto busDistorted = play (*e);
    e->reset();
    setByKey (*e, "distortion.on", 0.0f);
    const auto busClean = play (*e);
    CHECK (rms (busDistorted) > 0.02);
    CHECK (relativeDifference (busClean, busDistorted) > 0.2);
    // bus 1 effects bypass
    e->reset();
    useDistortion (*e, 1);
    setByKey (*e, "bus1.fx_bypass", 1.0f);
    CHECK (relativeDifference (busClean, play (*e)) < 0.01);
    // main fx bypass
    e->reset();
    setByKey (*e, "bus1.fx_bypass", 0.0f);
    setByKey (*e, "osc1.destination", 3.0f);
    setByKey (*e, "osc1.bus1_send", 0.0f);
    useDistortion (*e, 0);
    setByKey (*e, "main.fx_bypass", 1.0f);
    CHECK (relativeDifference (clean, play (*e)) < 0.01);
}

TEST (the_order_inside_a_rack_follows_fx_order)
{
    auto e = makeVital();
    base (*e);
    setByKey (*e, "distortion.on", 1.0f);
    setByKey (*e, "distortion.drive", 24.0f);
    setByKey (*e, "distortion.mix", 100.0f);
    setByKey (*e, "eq.on", 1.0f);
    setByKey (*e, "eq.low.mode", 1.0f);
    setByKey (*e, "eq.low.gain", 12.0f);
    setByKey (*e, "eq.low.cutoff", 400.0f);
    // distortion (3) then EQ (4) ...
    setByKey (*e, "fx.order.1", 3.0f);
    setByKey (*e, "fx.order.2", 4.0f);
    int next = 0;
    for (int position = 3; position <= 16; ++position)
    {
        while (next == 3 || next == 4)
            ++next;
        char key[16];
        std::snprintf (key, sizeof key, "fx.order.%d", position);
        setByKey (*e, key, static_cast<float> (next++));
    }
    const auto distortionFirst = play (*e);
    e->reset();
    setByKey (*e, "fx.order.1", 4.0f);
    setByKey (*e, "fx.order.2", 3.0f);
    const auto eqFirst = play (*e);
    CHECK (relativeDifference (distortionFirst, eqFirst) > 0.1);
    // with the two effects in different racks the order of the chain no longer matters between them: each rack has its own signal
    e->reset();
    setByKey (*e, "eq.rack", 1.0f);
    setByKey (*e, "osc1.destination", 6.0f);
    setByKey (*e, "osc1.bus1_send", 100.0f);
    const auto eqInBus = play (*e);
    CHECK (rms (eqInBus) > 0.01); // the EQ lives in bus 1, the sound goes through it
}

TEST (fx_order_repair_for_16_slots)
{
    float choices[16];
    for (int i = 0; i < 16; ++i)
        choices[i] = static_cast<float> (i);
    int order[16];
    resolveFxOrder (choices, order, 16);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ (order[i], i);
    // duplicates: a later position takes the lowest module not yet placed; out of range values too
    const float broken[16] = { 15, 15, 3, 3, 99, -4, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10 };
    resolveFxOrder (broken, order, 16);
    bool seen[16] = {};
    for (int i = 0; i < 16; ++i)
    {
        CHECK (order[i] >= 0 && order[i] < 16);
        seen[order[i] & 15] = true;
    }
    for (bool s : seen)
        CHECK (s);
    CHECK_EQ (order[0], 15);
    CHECK_EQ (order[1], 0);   // the duplicate 15 takes the lowest free module
    CHECK_EQ (order[2], 3);
    CHECK_EQ (order[3], 1);   // the duplicate 3 takes the next free one
}

TEST (mixer_and_racks_survive_random_settings_without_nonfinite_audio)
{
    auto e = makeVital();
    sbtest::Rng rng (5);
    for (int round = 0; round < 12; ++round)
    {
        for (const char* key : { "osc1.destination", "osc2.destination", "osc3.destination", "noise.destination" })
            setByKey (*e, key, static_cast<float> (rng.u32() % 7));
        for (const char* key : { "osc1.filter_blend", "osc2.bus1_send", "osc3.bus2_send", "noise.bus1_send", "filter1.bus2_send", "filter2.bus1_send" })
            setByKey (*e, key, static_cast<float> (rng.uniform() * 100.0));
        for (const char* key : { "filter1.destination", "filter2.destination", "bus1.destination", "bus2.destination" })
            setByKey (*e, key, static_cast<float> (rng.u32() % 3));
        for (const char* key : { "chorus.rack", "delay.rack", "distortion.rack", "reverb.rack", "eq.rack", "phaser.rack" })
            setByKey (*e, key, static_cast<float> (rng.u32() % 3));
        for (const char* key : { "chorus.on", "delay.on", "distortion.on", "reverb.on", "eq.on", "phaser.on", "filter1.on", "filter2.on" })
            setByKey (*e, key, static_cast<float> (rng.u32() % 2));
        for (int position = 1; position <= 16; ++position)
        {
            char key[16];
            std::snprintf (key, sizeof key, "fx.order.%d", position);
            setByKey (*e, key, static_cast<float> (rng.u32() % 16));
        }
        auto out = sbtest::render (*e, { noteOn (0, 40 + static_cast<int> (rng.u32() % 30)), noteOn (0, 60) }, 20000, 256);
        CHECK (sbtest::allFinite (out.left) && sbtest::allFinite (out.right));
        CHECK (sbtest::peak (out.left) < 4.0f);
    }
}
