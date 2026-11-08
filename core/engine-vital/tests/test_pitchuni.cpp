// SynthyBoy - engine-vital tests: Pitch modes, Transpose snap, Unison laws and Phase legato (Block B14, PITCHUNI).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/engine_caps.h"
#include "synthyboy/content_state.h"
#include <functional>
#include <string>
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::choiceValue;
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

/** Basic saw oscillator on osc 1, neutral defaults. */
void baseSaw (IEngine& e)
{
    setByKey (e, "osc1.on", 1.0f);
    setByKey (e, "osc1.wavetable", 1.0f); // basic saw
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f); // Effects only
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

/** 7-voice unison saw on osc 1. */
void unisonSaw (IEngine& e)
{
    baseSaw (e);
    setByKey (e, "osc1.unison.voices", 7.0f);
    setByKey (e, "osc1.unison.detune", 30.0f);
    setByKey (e, "osc1.unison.spread", 100.0f);
}

sbtest::Rendered play (IEngine& e, double seconds = 0.5, int key = 48)
{
    return sbtest::render (e, { noteOn (0, key) }, static_cast<int> (seconds * kSr), 256);
}

double relativeDifference (const sbtest::Rendered& a, const sbtest::Rendered& b, double t0 = 0.1, double t1 = 0.4)
{
    double d = 0.0, n = 0.0;
    const size_t start = static_cast<size_t> (t0 * kSr);
    const size_t end = static_cast<size_t> (t1 * kSr);
    for (size_t i = start; i < end && i < a.left.size() && i < b.left.size(); ++i)
    {
        const double x = static_cast<double> (a.left[i]) - b.left[i];
        d += x * x;
        n += static_cast<double> (a.left[i]) * a.left[i];
    }
    return n > 0.0 ? std::sqrt (d / n) : (d > 0.0 ? 1.0e9 : 0.0);
}
} // namespace

TEST (pitch_unison_capabilities_reported)
{
    auto e = makeVital();
    CHECK (capsCover (e->capabilities(), kCapPitchUnison));
}

TEST (pitch_coarse_and_modes_render_distinct_audio)
{
    // 1. Semitones mode (0): pitch_coarse 0 vs 7 st
    auto e1 = makeVital();
    baseSaw (*e1);
    const auto rDefault = play (*e1);

    auto e2 = makeVital();
    baseSaw (*e2);
    setByKey (*e2, "osc1.pitch_coarse", 7.0f);
    const auto rCoarse7 = play (*e2);

    const double diffCoarse = relativeDifference (rDefault, rCoarse7);
    CHECK (diffCoarse > 0.2);

    // 2. Harmonics mode (1): coarse = 1 (2nd harmonic = octave up)
    auto e3 = makeVital();
    baseSaw (*e3);
    setByKey (*e3, "osc1.pitch_mode", 1.0f);
    setByKey (*e3, "osc1.pitch_coarse", 1.0f);
    const auto rHarmonic = play (*e3);
    const double diffHarmonic = relativeDifference (rDefault, rHarmonic);
    CHECK (diffHarmonic > 0.2);

    // 3. Ratio mode (2): coarse = 0.5 (1.5x ratio = perfect fifth)
    auto e4 = makeVital();
    baseSaw (*e4);
    setByKey (*e4, "osc1.pitch_mode", 2.0f);
    setByKey (*e4, "osc1.pitch_coarse", 0.5f);
    const auto rRatio = play (*e4);
    const double diffRatio = relativeDifference (rDefault, rRatio);
    CHECK (diffRatio > 0.2);

    // 4. Step mode (3): coarse = 7 (7 scale steps)
    auto e5 = makeVital();
    baseSaw (*e5);
    setByKey (*e5, "osc1.pitch_mode", 3.0f);
    setByKey (*e5, "osc1.pitch_coarse", 7.0f);
    const auto rStep = play (*e5);
    const double diffStep = relativeDifference (rDefault, rStep);
    CHECK (diffStep > 0.2);
}

TEST (pitch_track_and_fixed_pitch_render_distinct_audio)
{
    // When pitch_track is 1 (on, default), note 48 vs note 60 differ
    auto e = makeVital();
    baseSaw (*e);
    const auto r48 = play (*e, 0.4, 48);
    const auto r60 = play (*e, 0.4, 60);
    CHECK (relativeDifference (r48, r60) > 0.2);

    // When pitch_track is 0 (off), note 48 and note 60 render identical fixed pitch (48)
    auto eFixed1 = makeVital();
    baseSaw (*eFixed1);
    setByKey (*eFixed1, "osc1.pitch_track", 0.0f);
    const auto rFixed48 = play (*eFixed1, 0.4, 48);

    auto eFixed2 = makeVital();
    baseSaw (*eFixed2);
    setByKey (*eFixed2, "osc1.pitch_track", 0.0f);
    const auto rFixed60 = play (*eFixed2, 0.4, 60);
    const double diffSameFixed = relativeDifference (rFixed48, rFixed60);
    std::printf ("    fixed pitch diff between note 48 and 60: %g\n", diffSameFixed);
    CHECK (diffSameFixed < 1.0e-3);

    // Changing fixed_pitch from 48 to 60 produces audible difference
    setByKey (*eFixed1, "osc1.fixed_pitch", 60.0f);
    const auto rFixed60New = play (*eFixed1, 0.4, 48);
    CHECK (relativeDifference (rFixed48, rFixed60New) > 0.2);
}

TEST (transpose_snap_quantizes_transpose)
{
    // Transpose 1 semitone up (C#):
    // Without snap: pitch is C#
    auto eNormal = makeVital();
    baseSaw (*eNormal);
    setByKey (*eNormal, "osc1.transpose", 1.0f);
    const auto rCsharp = play (*eNormal, 0.4, 48);

    // With transpose_snap = 1 (only bit 0 = C allowed):
    // 1 semitone snaps to 0 semitones (C)
    auto eSnap = makeVital();
    baseSaw (*eSnap);
    setByKey (*eSnap, "osc1.transpose", 1.0f);
    setByKey (*eSnap, "osc1.transpose_snap", 1.0f); // bit 0 = C
    const auto rSnapped = play (*eSnap, 0.4, 48);

    CHECK (relativeDifference (rCsharp, rSnapped) > 0.1);
}

TEST (unison_modes_and_blend_shape_render_distinct_audio)
{
    // Linear (0) vs Super (1), Exp (2), Inv (3), Random (4)
    auto e0 = makeVital();
    unisonSaw (*e0);
    const auto rLinear = play (*e0);

    auto e1 = makeVital();
    unisonSaw (*e1);
    setByKey (*e1, "osc1.unison.mode", 1.0f); // Super
    const auto rSuper = play (*e1);
    CHECK (relativeDifference (rLinear, rSuper) > 0.05);

    auto e2 = makeVital();
    unisonSaw (*e2);
    setByKey (*e2, "osc1.unison.mode", 2.0f); // Exp
    const auto rExp = play (*e2);
    CHECK (relativeDifference (rLinear, rExp) > 0.05);

    auto e3 = makeVital();
    unisonSaw (*e3);
    setByKey (*e3, "osc1.unison.mode", 3.0f); // Inv
    const auto rInv = play (*e3);
    CHECK (relativeDifference (rLinear, rInv) > 0.05);

    auto e4 = makeVital();
    unisonSaw (*e4);
    setByKey (*e4, "osc1.unison.mode", 4.0f); // Random
    const auto rRandom = play (*e4);
    CHECK (relativeDifference (rLinear, rRandom) > 0.05);

    // Unison Blend Shape: 0% vs 100% vs -100%
    auto eBlendPos = makeVital();
    unisonSaw (*eBlendPos);
    setByKey (*eBlendPos, "osc1.unison.blend_shape", 100.0f);
    const auto rBlendPos = play (*eBlendPos);
    CHECK (relativeDifference (rLinear, rBlendPos) > 0.05);

    auto eBlendNeg = makeVital();
    unisonSaw (*eBlendNeg);
    setByKey (*eBlendNeg, "osc1.unison.blend_shape", -100.0f);
    const auto rBlendNeg = play (*eBlendNeg);
    CHECK (relativeDifference (rLinear, rBlendNeg) > 0.05);

    // Unison Stack Repeat: stack style 3 (octaves), 1x vs 2x
    auto eStack1 = makeVital();
    unisonSaw (*eStack1);
    setByKey (*eStack1, "osc1.unison.stack", 3.0f); // Octave stack
    const auto rStack1 = play (*eStack1);

    auto eStack2 = makeVital();
    unisonSaw (*eStack2);
    setByKey (*eStack2, "osc1.unison.stack", 3.0f);
    setByKey (*eStack2, "osc1.unison.stack_repeat", 1.0f); // 2x repeat
    const auto rStack2 = play (*eStack2);
    CHECK (relativeDifference (rStack1, rStack2) > 0.05);

    // Serum 2 manual p.47: "12 (1-3x)" / "12+7 (1-3x)": the repeat is the number of octaves above the original pitch (1x / 2x / 3x), for the
    // Octaves (3) and Power Chord (5) stacks; the other stacks ignore it
    const auto stacked = [&] (float style, float repeat)
    {
        auto e = makeVital();
        unisonSaw (*e);
        setByKey (*e, "osc1.unison.voices", 16.0f); // eight voice pairs: the 3x stack has seven steps
        setByKey (*e, "osc1.unison.stack", style);
        setByKey (*e, "osc1.unison.stack_repeat", repeat);
        return play (*e);
    };
    CHECK (relativeDifference (stacked (3.0f, 1.0f), stacked (3.0f, 2.0f)) > 0.05);
    CHECK (relativeDifference (stacked (5.0f, 0.0f), stacked (5.0f, 1.0f)) > 0.05);
    CHECK (relativeDifference (stacked (5.0f, 1.0f), stacked (5.0f, 2.0f)) > 0.05);
    CHECK (relativeDifference (stacked (1.0f, 0.0f), stacked (1.0f, 2.0f)) < 1.0e-6); // Center Drop 12: no repeat
}

TEST (pitch_bend_tracking_off_ignores_the_wheel)
{
    const std::vector<Event> bent = { noteOn (0, 48), sbtest::pitchBend (2400, 1.0f) };
    const auto renderBend = [&] (const std::function<void (IEngine&)>& setup, const std::vector<Event>& events)
    {
        auto e = makeVital();
        baseSaw (*e);
        setup (*e);
        return sbtest::render (*e, events, static_cast<int> (0.5 * kSr), 256);
    };
    const auto none = renderBend ([] (IEngine&) {}, { noteOn (0, 48) });
    const auto tracked = renderBend ([] (IEngine&) {}, bent);
    const auto untracked = renderBend ([] (IEngine& e) { setByKey (e, "osc1.pitch_bend_track", 0.0f); }, bent);
    CHECK (relativeDifference (none, tracked) > 0.2);   // On (default): the wheel moves the pitch
    // Serum 2 manual p.36: a wavetable oscillator always follows the wheel, the switch is for the sample types (test_wp4.cpp)
    CHECK (relativeDifference (tracked, untracked) < 1.0e-3);

    // a fixed pitch oscillator: wheel tracking on / off
    const auto fixedBent = renderBend ([] (IEngine& e) { setByKey (e, "osc1.pitch_track", 0.0f); }, bent);
    const auto fixedFlat = renderBend ([] (IEngine& e) { setByKey (e, "osc1.pitch_track", 0.0f); setByKey (e, "osc1.pitch_bend_track", 0.0f); }, bent);
    const auto fixedNone = renderBend ([] (IEngine& e) { setByKey (e, "osc1.pitch_track", 0.0f); }, { noteOn (0, 48) });
    CHECK (relativeDifference (fixedNone, fixedBent) > 0.2);
    CHECK (relativeDifference (fixedBent, fixedFlat) < 1.0e-3); // the wavetable oscillator ignores the switch
}

TEST (step_mode_follows_the_active_tuning)
{
    // 24 equal divisions of the octave: one Step is one key = 50 cents, one Semitone is 100 cents
    const auto stepped = [] (float mode)
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 32.0f);
        baseSaw (*e);
        setByKey (*e, "master.tuning.scale", choiceValue ("master.tuning.scale", "User file"));
        StateContent content;
        content.tuning.present = true;
        content.tuning.scl = "! 24edo.scl\n24 tone equal temperament\n 24\n";
        for (int i = 1; i <= 24; ++i)
            content.tuning.scl += " " + std::to_string (50 * i) + ".0\n";
        e->setContent (content);
        setByKey (*e, "osc1.pitch_mode", mode);
        setByKey (*e, "osc1.pitch_coarse", 2.0f);
        e->prepare (kSr, 256, Quality {});
        return play (*e, 0.5, 60);
    };
    CHECK (relativeDifference (stepped (0.0f), stepped (3.0f)) > 0.2);

    // with the equal tempered keys a Step is a semitone
    auto semi = makeVital();
    baseSaw (*semi);
    setByKey (*semi, "osc1.pitch_coarse", 2.0f);
    auto step = makeVital();
    baseSaw (*step);
    setByKey (*step, "osc1.pitch_mode", 3.0f);
    setByKey (*step, "osc1.pitch_coarse", 2.0f);
    CHECK (relativeDifference (play (*semi), play (*step)) < 1.0e-3);
}

TEST (unison_range_scales_the_detune_span)
{
    auto narrow = makeVital();
    unisonSaw (*narrow);
    setByKey (*narrow, "osc1.unison.range", 2.0f);
    auto wide = makeVital();
    unisonSaw (*wide);
    setByKey (*wide, "osc1.unison.range", 12.0f);
    CHECK (relativeDifference (play (*narrow), play (*wide)) > 0.05);
}

namespace
{
/** Two overlapping notes (the second while the first is held) and a third after a release: the legato situations. */
sbtest::Rendered legatoRender (const std::function<void (IEngine&)>& setup)
{
    auto e = makeVital();
    unisonSaw (*e);
    setByKey (*e, "osc1.unison.voices", 7.0f);
    setup (*e);
    const std::vector<Event> events = { noteOn (0, 48), noteOn (4800, 52), noteOff (9600, 52), noteOn (14400, 55) };
    return sbtest::render (*e, events, static_cast<int> (0.6 * kSr), 256);
}
} // namespace

TEST (phase_legato_changes_the_start_phase_of_overlapping_notes)
{
    const auto off = legatoRender ([] (IEngine&) {});
    const auto oscOn = legatoRender ([] (IEngine& e) { setByKey (e, "osc1.phase_legato", 1.0f); });
    const auto voiceOn = legatoRender ([] (IEngine& e) { setByKey (e, "voice.phase_legato", 1.0f); });
    CHECK (sbtest::allFinite (oscOn.left) && sbtest::allFinite (oscOn.right));
    CHECK (sbtest::rmsOf (oscOn.left, 0, oscOn.left.size()) > 0.01);
    CHECK (relativeDifference (off, oscOn, 0.1, 0.5) > 0.02);
    CHECK (relativeDifference (off, voiceOn, 0.1, 0.5) > 0.02);
    // both switches act the same way
    CHECK (relativeDifference (oscOn, voiceOn, 0.1, 0.5) < 1.0e-3);

    // the memory modes differ from each other
    const auto memory = [] (float mode)
    {
        return legatoRender ([mode] (IEngine& e)
        {
            setByKey (e, "osc1.phase_legato", 1.0f);
            setByKey (e, "osc1.phase_memory", mode);
        });
    };
    const auto all = memory (0.0f), contiguous = memory (1.0f), perVoice = memory (2.0f);
    CHECK (relativeDifference (all, contiguous, 0.1, 0.5) > 0.02);
    CHECK (relativeDifference (all, perVoice, 0.1, 0.5) > 0.02);
    CHECK (relativeDifference (contiguous, perVoice, 0.1, 0.5) > 0.02);
}

TEST (phase_legato_preserves_continuity_and_stays_finite)
{
    auto e = makeVital();
    baseSaw (*e);
    setByKey (*e, "osc1.phase_legato", 1.0f);
    setByKey (*e, "voice.phase_legato", 1.0f);

    // Play overlapping notes (legato transition)
    const std::vector<Event> events = {
        noteOn (0, 48),
        noteOn (4800, 52), // note on while 48 is held
        noteOff (9600, 48),
        noteOff (14400, 52)
    };
    const auto r = sbtest::render (*e, events, static_cast<int> (0.5 * kSr), 256);
    CHECK (sbtest::allFinite (r.left) && sbtest::allFinite (r.right));
    CHECK (sbtest::rmsOf (r.left, 0, r.left.size()) > 0.01);
}
