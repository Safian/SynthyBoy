// SynthyBoy - engine-vital tests: Modulation of modulation amount, per-connection curve, stereo source, stereo routing (MODAMT).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "alloc_hook.h"
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
    CHECK_EQ (stereoChoice, 453u);
    setByKey (*e, "mod01.source", 1.0f); // Macro 1
    setByKey (*e, "mod01.destination", static_cast<float> (stereoChoice));
    setByKey (*e, "mod01.amount", 100.0f);
    sbtest::render (*e, { noteOn (0, 60, 0.9f) }, 256, 256);
    CHECK_EQ (vitalConnectedSlots (*e), 1);
}

namespace
{
double rmsDiff (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    const size_t n = std::min (a.left.size(), b.left.size());
    std::vector<float> d (n);
    for (size_t i = 0; i < n; ++i)
        d[i] = a.left[i] - b.left[i];
    return sbtest::rmsOf (d, 0, d.size());
}

size_t choiceOfKey (const char* key)
{
    return modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey (key)->id)));
}

/** process () with the allocations of the audio thread counted into `allocs`. */
sbtest::Rendered renderCounted (IEngine& engine, const std::vector<Event>& events, int totalFrames, int blockSize, unsigned long& allocs)
{
    sbtest::Rendered out;
    out.left.assign (static_cast<size_t> (totalFrames), 0.0f);
    out.right.assign (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<Event> local;
    size_t idx = 0;
    for (int start = 0; start < totalFrames; start += blockSize)
    {
        const int n = std::min (blockSize, totalFrames - start);
        local.clear();
        while (idx < events.size() && events[idx].frame < start + n)
        {
            auto e = events[idx++];
            e.frame -= start;
            local.push_back (e);
        }
        const auto before = g_allocations.load();
        engine.process (local.data(), static_cast<int> (local.size()), out.left.data() + start, out.right.data() + start, n);
        allocs += g_allocations.load() - before;
    }
    return out;
}
} // namespace

TEST (mod_amount_destination_numbers_are_the_documented_ones)
{
    CHECK_EQ (choiceOfKey ("master.stereo_routing"), 453u);
    CHECK_EQ (choiceOfKey ("mod01.amount"), 454u);
    CHECK_EQ (choiceOfKey ("mod64.amount"), 517u);
    CHECK_EQ (choiceOfKey ("master.portamento.time"), 518u);
    CHECK_EQ (choiceOfKey ("master.polyphony"), 519u);
}

TEST (mod_amount_driven_by_the_note_follows_the_key)
{
    // key track (source "note", choice 24) drives the amount of an LFO -> cutoff connection: a low and a high note get different sweeps,
    // and two notes played together keep their own amounts (a global amount would make both voices equal)
    const auto cutoff = choiceOfKey ("filter1.cutoff");
    const auto amt = choiceOfKey ("mod01.amount");
    const auto run = [&] (const std::vector<Event>& ev)
    {
        auto e = makeVital();
        baseSawWithFilter (*e);
        setByKey (*e, "lfo1.rate", 4.0f);
        setByKey (*e, "lfo1.sync", 0.0f);
        setByKey (*e, "mod01.source", 7.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (cutoff));
        setByKey (*e, "mod01.amount", 0.0f);
        setByKey (*e, "mod02.source", 24.0f); // note
        setByKey (*e, "mod02.destination", static_cast<float> (amt));
        setByKey (*e, "mod02.amount", 100.0f);
        return sbtest::render (*e, ev, 24000, 256);
    };
    const auto lo = run ({ noteOn (0, 36, 0.9f) });
    const auto hi = run ({ noteOn (0, 96, 0.9f) });
    // the same two notes without the amount connection: with it the two voices must differ from that plain mix
    auto plain = makeVital();
    baseSawWithFilter (*plain);
    setByKey (*plain, "lfo1.rate", 4.0f);
    setByKey (*plain, "lfo1.sync", 0.0f);
    setByKey (*plain, "mod01.source", 7.0f);
    setByKey (*plain, "mod01.destination", static_cast<float> (cutoff));
    setByKey (*plain, "mod01.amount", 0.0f);
    const auto plainLo = sbtest::render (*plain, { noteOn (0, 36, 0.9f) }, 24000, 256);
    CHECK (rmsDiff (lo, plainLo) > 0.001); // key 36 sits above the centre of the note source: its amount is not 0
    CHECK (rmsDiff (lo, hi) > 0.01);
    CHECK (sbtest::allFinite (lo.left) && sbtest::allFinite (hi.left));
}

TEST (mod_curve_is_applied_to_the_source_before_the_amount)
{
    const auto cutoff = choiceOfKey ("filter1.cutoff");
    const auto run = [&] (float macro, float amount, const std::vector<ContentPoint>& pts)
    {
        auto e = makeVital();
        StateContent c;
        if (! pts.empty())
        {
            ContentModCurve mc;
            mc.slot = 1;
            mc.points = pts;
            c.modCurves.push_back (mc);
        }
        e->setContent (c);
        baseSawWithFilter (*e);
        setByKey (*e, "mod01.source", 19.0f); // macro 1
        setByKey (*e, "mod01.destination", static_cast<float> (cutoff));
        setByKey (*e, "mod01.amount", amount);
        setByKey (*e, "macro.1", macro * 100.0f);
        return sbtest::render (*e, { noteOn (0, 48, 0.9f) }, 16000, 256);
    };
    const auto none = run (0.5f, 0.0f, {});
    const auto linear = run (0.5f, 80.0f, {});
    CHECK (rmsDiff (none, linear) > 0.01); // the connection itself is audible
    // a curve that is 0 everywhere: the source never reaches the amount, the render equals the one without modulation
    const auto flat0 = run (0.5f, 80.0f, { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 0.0f) });
    CHECK (rmsDiff (none, flat0) < 0.002);
    // a curve that is 1 everywhere with the source at 0.5 equals the straight line with the source at 1
    const auto linearFull = run (1.0f, 80.0f, {});
    const auto flat1 = run (0.5f, 80.0f, { ContentPoint (0.0f, 1.0f), ContentPoint (1.0f, 1.0f) });
    CHECK (rmsDiff (linearFull, flat1) < 0.002);
    CHECK (rmsDiff (linear, linearFull) > 0.005);
}

TEST (mod_curve_of_another_slot_does_not_leak)
{
    const auto cutoff = choiceOfKey ("filter1.cutoff");
    const auto run = [&] (int curveSlot)
    {
        auto e = makeVital();
        StateContent c;
        ContentModCurve mc;
        mc.slot = curveSlot;
        mc.points = { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 0.0f) };
        c.modCurves.push_back (mc);
        e->setContent (c);
        baseSawWithFilter (*e);
        setByKey (*e, "mod01.source", 19.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (cutoff));
        setByKey (*e, "mod01.amount", 80.0f);
        setByKey (*e, "macro.1", 70.0f);
        return sbtest::render (*e, { noteOn (0, 48, 0.9f) }, 12000, 256);
    };
    CHECK (rmsDiff (run (1), run (2)) > 0.01);
}

TEST (mod_cycles_are_neutral_and_recover)
{
    auto e = makeVital();
    baseSawWithFilter (*e);
    const auto a1 = choiceOfKey ("mod01.amount");
    const auto a2 = choiceOfKey ("mod02.amount");
    const auto a3 = choiceOfKey ("mod03.amount");
    const auto cutoff = choiceOfKey ("filter1.cutoff");
    unsigned long allocs = 0;
    const auto step = [&] { renderCounted (*e, { noteOn (0, 60, 0.8f) }, 512, 256, allocs); };

    // self: rejected
    setByKey (*e, "mod01.source", 19.0f);
    setByKey (*e, "mod01.destination", static_cast<float> (a1));
    setByKey (*e, "mod01.amount", 50.0f);
    step();
    CHECK_EQ (vitalConnectedSlots (*e), 0);

    // chain 1 -> 2 -> 3 is fine (3 drives the cutoff)
    setByKey (*e, "mod01.destination", static_cast<float> (a2));
    setByKey (*e, "mod02.source", 20.0f);
    setByKey (*e, "mod02.destination", static_cast<float> (a3));
    setByKey (*e, "mod02.amount", 50.0f);
    setByKey (*e, "mod03.source", 7.0f);
    setByKey (*e, "mod03.destination", static_cast<float> (cutoff));
    setByKey (*e, "mod03.amount", 50.0f);
    step();
    CHECK_EQ (vitalConnectedSlots (*e), 3);

    // 3 -> 1.amount closes the loop 1 -> 2 -> 3 -> 1: one of the three stays unwired, the engine stays finite
    setByKey (*e, "mod03.destination", static_cast<float> (a1));
    const auto out = renderCounted (*e, { noteOn (0, 62, 0.8f) }, 4096, 256, allocs);
    CHECK (sbtest::allFinite (out.left));
    CHECK (vitalConnectedSlots (*e) <= 2);

    // opening the loop again wires everything
    setByKey (*e, "mod03.destination", static_cast<float> (cutoff));
    step();
    step();
    CHECK_EQ (vitalConnectedSlots (*e), 3);
    CHECK_EQ (allocs, 0ul);
}

TEST (master_voice_override_kill_and_steal_sound_different)
{
    const auto run = [&] (float override)
    {
        auto e = makeVital();
        e->setParam (kParamMasterPolyphony, 2.0f);
        setByKey (*e, "osc1.on", 1.0f);
        setByKey (*e, "osc1.wavetable", 1.0f);
        setByKey (*e, "osc1.level", 100.0f);
        setByKey (*e, "osc1.unison.voices", 1.0f);
        setByKey (*e, "osc1.random_phase", 0.0f);
        setByKey (*e, "filter1.on", 0.0f);
        setByKey (*e, "master.volume", -6.0f);
        setByKey (*e, "master.voice_override", override);
        return sbtest::render (*e, { noteOn (0, 48, 0.9f), noteOn (2000, 55, 0.9f), noteOn (4000, 60, 0.9f), noteOn (6000, 64, 0.9f) }, 16000, 256);
    };
    CHECK (rmsDiff (run (0.0f), run (1.0f)) > 0.005);
}

TEST (portamento_time_is_a_working_modulation_destination)
{
    const auto porta = choiceOfKey ("master.portamento.time");
    const auto run = [&] (float macro)
    {
        auto e = makeVital();
        setByKey (*e, "osc1.on", 1.0f);
        setByKey (*e, "osc1.wavetable", 0.0f); // sine
        setByKey (*e, "osc1.level", 100.0f);
        setByKey (*e, "osc1.unison.voices", 1.0f);
        setByKey (*e, "osc1.random_phase", 0.0f);
        setByKey (*e, "filter1.on", 0.0f);
        setByKey (*e, "master.volume", -6.0f);
        setByKey (*e, "master.legato", 1.0f);
        setByKey (*e, "master.portamento.always", 1.0f);
        setByKey (*e, "master.portamento.time", 0.0f); // the shortest
        setByKey (*e, "mod01.source", 19.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (porta));
        setByKey (*e, "mod01.amount", 100.0f);
        setByKey (*e, "macro.1", macro * 100.0f);
        return sbtest::render (*e, { noteOn (0, 48, 0.9f), noteOn (6000, 72, 0.9f) }, 20000, 256);
    };
    const auto fast = run (0.0f);
    const auto slow = run (1.0f);
    CHECK (rmsDiff (fast, slow) > 0.05);
}

TEST (polyphony_is_a_working_modulation_destination)
{
    const auto poly = choiceOfKey ("master.polyphony");
    const auto run = [&] (float macro)
    {
        auto e = makeVital();
        e->setParam (kParamMasterPolyphony, 8.0f);
        setByKey (*e, "osc1.on", 1.0f);
        setByKey (*e, "osc1.wavetable", 1.0f);
        setByKey (*e, "osc1.level", 100.0f);
        setByKey (*e, "osc1.unison.voices", 1.0f);
        setByKey (*e, "filter1.on", 0.0f);
        setByKey (*e, "master.volume", -12.0f);
        setByKey (*e, "mod01.source", 19.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (poly));
        setByKey (*e, "mod01.amount", -100.0f);
        setByKey (*e, "macro.1", macro * 100.0f);
        return sbtest::render (*e, { noteOn (0, 48, 0.9f), noteOn (1000, 52, 0.9f), noteOn (2000, 55, 0.9f), noteOn (3000, 60, 0.9f) }, 14000, 256);
    };
    const auto many = run (0.0f);
    const auto one = run (1.0f);
    CHECK (rmsDiff (many, one) > 0.02);
}

TEST (stereo_routing_is_a_working_modulation_destination)
{
    const auto routing = choiceOfKey ("master.stereo_routing");
    const auto run = [&] (float macro)
    {
        auto e = makeVital();
        setByKey (*e, "osc1.on", 1.0f);
        setByKey (*e, "osc1.wavetable", 1.0f);
        setByKey (*e, "osc1.level", 100.0f);
        setByKey (*e, "osc1.unison.voices", 7.0f);
        setByKey (*e, "osc1.unison.detune", 30.0f);
        setByKey (*e, "osc1.unison.spread", 100.0f);
        setByKey (*e, "osc1.destination", 3.0f);
        setByKey (*e, "master.volume", -6.0f);
        setByKey (*e, "mod01.source", 19.0f);
        setByKey (*e, "mod01.destination", static_cast<float> (routing));
        setByKey (*e, "mod01.amount", -100.0f);
        setByKey (*e, "macro.1", macro * 100.0f);
        const auto r = sbtest::render (*e, { noteOn (0, 60, 0.9f) }, 16000, 256);
        double side = 0.0;
        for (size_t i = 0; i < r.left.size(); ++i)
        {
            const double d = r.left[i] - r.right[i];
            side += d * d;
        }
        return side;
    };
    const double wide = run (0.0f), narrow = run (1.0f);
    CHECK (wide > 1.0);
    CHECK (narrow < wide * 0.05);
}
