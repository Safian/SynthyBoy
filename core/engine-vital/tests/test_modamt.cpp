// SynthyBoy - engine-vital tests: Modulation of modulation amount, per-connection curve, stereo source, stereo routing (MODAMT).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/engine_caps.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> makeVital (const Quality& q = Quality {})
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, q);
    return e;
}

/** Basic saw oscillator on osc 1 through filter 1. */
void baseSawWithFilter (IEngine& e)
{
    setByKey (e, "osc1.on", 1.0f);
    setByKey (e, "osc1.wavetable", 1.0f); // basic saw
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 0.0f); // Filter 1
    setByKey (e, "filter1.on", 1.0f);
    setByKey (e, "filter1.cutoff", 40.0f); // low cutoff (audible modulation)
    setByKey (e, "filter1.resonance", 50.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}
} // namespace

TEST (mod_amount_capability_bit)
{
    auto e = makeVital();
    CHECK ((e->capabilities() & kCapModAmount) != 0);
}

TEST (mod_amount_modulation_audible_difference)
{
    // Slot 1: LFO 1 -> Filter 1 Cutoff, amount = 0.0
    // Slot 2: Macro 1 -> mod01.amount, amount = 100.0
    // When Macro 1 = 0.0, Slot 1 amount stays 0.0 (no cutoff modulation)
    // When Macro 1 = 100.0, Slot 1 amount is modulated (cutoff sweeps with LFO 1)
    const auto cutoffChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (kParamFilter1Cutoff)));
    const auto mod01AmtChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("mod01.amount")->id)));
    CHECK (mod01AmtChoice > 0);

    const auto runWithMacro = [&] (float macroVal)
    {
        auto e = makeVital();
        baseSawWithFilter (*e);

        // LFO 1 at 4 Hz
        setByKey (*e, "lfo1.rate", 4.0f);
        setByKey (*e, "lfo1.sync", 0.0f); // Seconds

        // Slot 1: LFO 1 (choice 7) -> Filter 1 Cutoff
        setByKey (*e, "mod01.source", 7.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (cutoffChoice));
        setByKey (*e, "mod01.amount", 0.0f); // default 0

        // Slot 2: Macro 1 (choice 19) -> mod01.amount
        setByKey (*e, "mod02.source", 19.0f);
        setByKey (*e, "mod02.destination", static_cast<float> (mod01AmtChoice));
        setByKey (*e, "mod02.amount", 100.0f);

        setByKey (*e, "macro.1", macroVal);

        return sbtest::render (*e, { noteOn (0, 48, 0.9f), noteOff (24000, 48) }, 32000, 256);
    };

    const auto offOut = runWithMacro (0.0f);
    const auto onOut = runWithMacro (100.0f);

    std::vector<float> diff (offOut.left.size());
    for (size_t i = 0; i < diff.size(); ++i)
        diff[i] = onOut.left[i] - offOut.left[i];

    const double diffRms = sbtest::rmsOf (diff, 0, diff.size());
    CHECK (diffRms > 0.01);
}

TEST (mod_self_modulation_and_cycles_rejected)
{
    auto e = makeVital();

    const auto mod01AmtChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("mod01.amount")->id)));
    const auto mod02AmtChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("mod02.amount")->id)));

    // 1) Direct self-loop: Slot 1 has source Macro 1, destination mod01.amount, but wait:
    // If slot 1 has source = mod01.amount? No, source cannot be a param.
    // Self-loop is when slot 1 drives mod01.amount:
    // Slot 1 modulates its own amount -> wouldCloseSlotLoop detects slot == targetSlot (slot 0 == target 0)
    setByKey (*e, "mod01.source", 1.0f); // Macro 1
    setByKey (*e, "mod01.destination", static_cast<float> (mod01AmtChoice));
    setByKey (*e, "mod01.amount", 50.0f);

    // Apply and check: slot 1 modulating its own amount should NOT be connected!
    sbtest::render (*e, { noteOn (0, 60, 0.8f) }, 256, 256);
    CHECK_EQ (vitalConnectedSlots (*e), 0);

    // 2) 2-slot cycle: Slot 1 modulates mod02.amount, Slot 2 modulates mod01.amount
    // Slot 1: LFO 1 -> mod02.amount
    // Slot 2: LFO 2 -> mod01.amount
    // Wait, Slot 1 output modulates Slot 2 amount, which modulates Slot 1 amount.
    // wouldCloseSlotLoop will detect that wiring slot 2 to mod01.amount closes a cycle because slot 1 already modulates slot 2!
    setByKey (*e, "mod01.source", 7.0f); // LFO 1
    setByKey (*e, "mod01.destination", static_cast<float> (mod02AmtChoice));
    setByKey (*e, "mod01.amount", 50.0f);

    setByKey (*e, "mod02.source", 8.0f); // LFO 2
    setByKey (*e, "mod02.destination", static_cast<float> (mod01AmtChoice));
    setByKey (*e, "mod02.amount", 50.0f);

    sbtest::render (*e, { noteOn (0, 60, 0.8f) }, 256, 256);
    // Exactly 1 slot should be connected (slot 1), slot 2 should be rejected!
    CHECK_EQ (vitalConnectedSlots (*e), 1);
}

TEST (per_connection_modulation_curve_audible_difference)
{
    const auto cutoffChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (kParamFilter1Cutoff)));

    const auto renderWithContent = [&] (const StateContent& content)
    {
        auto e = makeVital();
        e->setContent (content);
        baseSawWithFilter (*e);

        // Slow LFO 1 at 2 Hz
        setByKey (*e, "lfo1.rate", 2.0f);
        setByKey (*e, "lfo1.sync", 0.0f); // Seconds

        // Slot 1: LFO 1 -> Filter 1 Cutoff
        setByKey (*e, "mod01.source", 7.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (cutoffChoice));
        setByKey (*e, "mod01.amount", 80.0f);

        return sbtest::render (*e, { noteOn (0, 48, 0.9f), noteOff (24000, 48) }, 32000, 256);
    };

    // Case A: empty content (default linear curve)
    StateContent contentA;
    const auto outA = renderWithContent (contentA);

    // Case B: custom curve on slot 1 (sharp step / inverted)
    StateContent contentB;
    ContentModCurve mc;
    mc.slot = 1;
    mc.points = { ContentPoint (0.0f, 1.0f), ContentPoint (0.5f, 0.0f), ContentPoint (1.0f, 1.0f) };
    contentB.modCurves.push_back (mc);
    const auto outB = renderWithContent (contentB);

    std::vector<float> diff (outA.left.size());
    for (size_t i = 0; i < diff.size(); ++i)
        diff[i] = outB.left[i] - outA.left[i];

    const double diffRms = sbtest::rmsOf (diff, 0, diff.size());
    CHECK (diffRms > 0.005);
}

TEST (stereo_modulation_source_produces_stereo_difference)
{
    auto e = makeVital();
    baseSawWithFilter (*e);

    // Oscillator 1 is centered, filter 1 cutoff is at 40
    // Slot 1: Stereo source (choice 39) -> Filter 1 Cutoff, amount = 60
    // The stereo source is +1 on left / -1 on right, so cutoffs on left and right diverge
    const auto cutoffChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (kParamFilter1Cutoff)));
    setByKey (*e, "mod01.source", 39.0f); // stereo source
    setByKey (*e, "mod01.destination", static_cast<float> (cutoffChoice));
    setByKey (*e, "mod01.amount", 60.0f);

    const auto out = sbtest::render (*e, { noteOn (0, 48, 0.9f) }, 24000, 256);

    std::vector<float> diff (out.left.size());
    for (size_t i = 0; i < diff.size(); ++i)
        diff[i] = out.left[i] - out.right[i];

    const double diffRms = sbtest::rmsOf (diff, 0, diff.size());
    CHECK (diffRms > 0.02);
}

TEST (master_stereo_routing_control_and_modulation)
{
    auto e = makeVital();

    // 7-voice unison saw with 100% spread
    setByKey (*e, "osc1.on", 1.0f);
    setByKey (*e, "osc1.wavetable", 1.0f);
    setByKey (*e, "osc1.level", 100.0f);
    setByKey (*e, "osc1.unison.voices", 7.0f);
    setByKey (*e, "osc1.unison.detune", 30.0f);
    setByKey (*e, "osc1.unison.spread", 100.0f);
    setByKey (*e, "osc1.destination", 3.0f); // Direct to effects/out
    setByKey (*e, "master.volume", -6.0f);

    // At 100% stereo routing: wide stereo
    setByKey (*e, "master.stereo_routing", 100.0f);
    const auto wide = sbtest::render (*e, { noteOn (0, 60, 0.9f) }, 16000, 256);
    double wideSide = 0.0;
    for (size_t i = 0; i < wide.left.size(); ++i)
    {
        const double d = wide.left[i] - wide.right[i];
        wideSide += d * d;
    }

    // At 0% stereo routing: collapsed to mono
    setByKey (*e, "master.stereo_routing", 0.0f);
    const auto mono = sbtest::render (*e, { noteOn (0, 60, 0.9f) }, 16000, 256);
    double monoSide = 0.0;
    for (size_t i = 0; i < mono.left.size(); ++i)
    {
        const double d = mono.left[i] - mono.right[i];
        monoSide += d * d;
    }

    CHECK (wideSide > 1.0);
    CHECK (monoSide < wideSide * 0.01);

    // Verify modulating master.stereo_routing works as destination
    const auto stereoChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("master.stereo_routing")->id)));
    CHECK_EQ (stereoChoice, 442u);
    setByKey (*e, "mod01.source", 1.0f); // Macro 1
    setByKey (*e, "mod01.destination", static_cast<float> (stereoChoice));
    setByKey (*e, "mod01.amount", 100.0f);
    sbtest::render (*e, { noteOn (0, 60, 0.9f) }, 256, 256);
    CHECK_EQ (vitalConnectedSlots (*e), 1);
}

TEST (master_voice_override_control)
{
    auto e = makeVital();
    setByKey (*e, "master.voice_override", 0.0f); // Kill
    CHECK_EQ (e->getParam (findParamByKey ("master.voice_override")->id), 0.0f);
    setByKey (*e, "master.voice_override", 1.0f); // Steal
    CHECK_EQ (e->getParam (findParamByKey ("master.voice_override")->id), 1.0f);
}

TEST (portamento_and_polyphony_modulation_destinations)
{
    auto e = makeVital();

    const auto portaChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("master.portamento.time")->id)));
    const auto polyChoice = modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("master.polyphony")->id)));

    CHECK_EQ (portaChoice, 507u);
    CHECK_EQ (polyChoice, 508u);

    // Wire them in matrix
    setByKey (*e, "mod01.source", 1.0f); // Macro 1
    setByKey (*e, "mod01.destination", static_cast<float> (portaChoice));
    setByKey (*e, "mod01.amount", 50.0f);

    setByKey (*e, "mod02.source", 2.0f); // Macro 2
    setByKey (*e, "mod02.destination", static_cast<float> (polyChoice));
    setByKey (*e, "mod02.amount", 50.0f);

    sbtest::render (*e, { noteOn (0, 60, 0.9f) }, 256, 256);
    CHECK_EQ (vitalConnectedSlots (*e), 2);
}
