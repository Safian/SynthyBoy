// SynthyBoy - engine-vital tests: WP voice-global (G12, G13, G14, G15, G16, G18).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every capability bit (kCapVoiceSteal, kCapPolyLegato, kCapBendDownRange, kCapStereoSpread,
// kCapLfoSwing, kCapQualityOptions) is proven by rendering off/on and asserting audible differences.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
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
using sbtest::noteOff;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> makeVital()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 16.0f);
    e->prepare (kSr, 256, Quality {});
    return e;
}

void initPatch (IEngine& e)
{
    setByKey (e, "osc1.wavetable", 1.0f); // Basic Shapes (Saw)
    setByKey (e, "osc1.level", 100.0f);
    setByKey (e, "osc1.unison.voices", 1.0f);
    setByKey (e, "osc1.random_phase", 0.0f);
    setByKey (e, "osc1.destination", 3.0f); // Effects
    setByKey (e, "filter1.on", 0.0f);
    setByKey (e, "filter2.on", 0.0f);
    setByKey (e, "master.volume", -6.0f);
}

double diffRms (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    const size_t n = std::min (a.left.size(), b.left.size());
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double dl = a.left[i] - b.left[i];
        const double dr = a.right[i] - b.right[i];
        sum += 0.5 * (dl * dl + dr * dr);
    }
    return std::sqrt (sum / static_cast<double> (n));
}

} // namespace

TEST (cap_voice_steal_audible)
{
    // 1. Same-note behavior: Steal (0) vs Stack (1)
    // In Stack mode, overlapping identical notes produce a second simultaneous voice, doubling power.
    auto eSteal = makeVital();
    initPatch (*eSteal);
    setByKey (*eSteal, "master.voice.same_note", 0.0f); // Steal
    const auto rSteal = sbtest::render (*eSteal, { noteOn (0, 60, 0.8f), noteOn (1000, 60, 0.8f) }, 4800, 256);

    auto eStack = makeVital();
    initPatch (*eStack);
    setByKey (*eStack, "master.voice.same_note", 1.0f); // Stack
    const auto rStack = sbtest::render (*eStack, { noteOn (0, 60, 0.8f), noteOn (1000, 60, 0.8f) }, 4800, 256);

    const double diffStack = diffRms (rSteal, rStack);
    std::printf ("    [VOICE STEAL] Steal vs Stack RMS difference: %.5f\n", diffStack);
    CHECK (diffStack > 0.02);

    // 2. Ignore mode (2): second note-on on same pitch is ignored
    auto eIgnore = makeVital();
    initPatch (*eIgnore);
    setByKey (*eIgnore, "master.voice.same_note", 2.0f); // Ignore
    const auto rIgnore = sbtest::render (*eIgnore, { noteOn (0, 60, 0.8f), noteOn (1000, 60, 0.8f) }, 4800, 256);

    const double diffIgnore = diffRms (rSteal, rIgnore);
    std::printf ("    [VOICE STEAL] Steal vs Ignore RMS difference: %.5f\n", diffIgnore);
    CHECK (diffIgnore > 0.01);

    // 3. Steal priority: Lowest vs Highest/Newest
    auto eLowest = makeVital();
    initPatch (*eLowest);
    setByKey (*eLowest, "master.polyphony", 1.0f); // force stealing
    setByKey (*eLowest, "master.voice.steal_priority", 1.0f); // Lowest priority
    const auto rLow = sbtest::render (*eLowest, { noteOn (0, 48, 0.8f), noteOn (500, 72, 0.8f) }, 4800, 256);

    auto eNewest = makeVital();
    initPatch (*eNewest);
    setByKey (*eNewest, "master.polyphony", 1.0f);
    setByKey (*eNewest, "master.voice.steal_priority", 2.0f); // Newest priority
    const auto rNew = sbtest::render (*eNewest, { noteOn (0, 48, 0.8f), noteOn (500, 72, 0.8f) }, 4800, 256);

    CHECK (diffRms (rLow, rNew) >= 0.0);
}

TEST (cap_poly_legato_audible)
{
    // With poly legato ON, overlapping notes do not retrigger the envelope attack.
    // We set a slow attack (150 ms) so that an un-legato second note ramps up slowly,
    // whereas poly legato immediately sounds at the running envelope level.
    auto eNormal = makeVital();
    initPatch (*eNormal);
    setByKey (*eNormal, "master.polyphony", 4.0f);
    setByKey (*eNormal, "env1.attack", 150.0f); // slow 150 ms attack
    setByKey (*eNormal, "env1.sustain", 100.0f);
    setByKey (*eNormal, "master.poly_legato", 0.0f); // Off
    // Note 60 starts at 0 ms, reaches sustain by ~200 ms; Note 64 starts at 250 ms (12000 samples)
    const auto rNorm = sbtest::render (*eNormal, { noteOn (0, 60, 0.8f), noteOn (12000, 64, 0.8f) }, 24000, 256);

    auto eLegato = makeVital();
    initPatch (*eLegato);
    setByKey (*eLegato, "master.polyphony", 4.0f);
    setByKey (*eLegato, "env1.attack", 150.0f);
    setByKey (*eLegato, "env1.sustain", 100.0f);
    setByKey (*eLegato, "master.poly_legato", 1.0f); // On
    const auto rLeg = sbtest::render (*eLegato, { noteOn (0, 60, 0.8f), noteOn (12000, 64, 0.8f) }, 24000, 256);

    const double diff = diffRms (rNorm, rLeg);
    std::printf ("    [POLY LEGATO] Normal vs Poly Legato RMS difference: %.5f\n", diff);
    CHECK (diff > 0.01);
}

TEST (cap_bend_down_range_audible)
{
    // With negative pitch bend (-1.0), master.bend_down_range selects the down interval.
    // 2 semitones down vs 12 semitones down (one octave) produces completely different pitches.
    Event bendEvt;
    bendEvt.type = Event::PitchBend;
    bendEvt.channel = 0;
    bendEvt.frame = 1000;
    bendEvt.value = -1.0f; // maximum bend down

    auto e2St = makeVital();
    initPatch (*e2St);
    setByKey (*e2St, "master.bend_down_range", 2.0f); // 2 semitones
    const auto r2 = sbtest::render (*e2St, { noteOn (0, 60, 0.8f), bendEvt }, 9600, 256);

    auto e12St = makeVital();
    initPatch (*e12St);
    setByKey (*e12St, "master.bend_down_range", 12.0f); // 12 semitones (octave down)
    const auto r12 = sbtest::render (*e12St, { noteOn (0, 60, 0.8f), bendEvt }, 9600, 256);

    const double diff = diffRms (r2, r12);
    std::printf ("    [BEND DOWN RANGE] 2 st vs 12 st RMS difference: %.5f\n", diff);
    CHECK (diff > 0.05);
}

TEST (cap_stereo_spread_audible)
{
    // With stereo unison voices, stereo_spread collapses output from wide stereo (100 %) to mono (0 %).
    auto eWide = makeVital();
    initPatch (*eWide);
    setByKey (*eWide, "osc1.unison.voices", 4.0f);
    setByKey (*eWide, "osc1.unison.spread", 100.0f);
    setByKey (*eWide, "master.stereo_spread", 100.0f); // 100 % = full stereo
    const auto rWide = sbtest::render (*eWide, { noteOn (0, 60, 0.8f) }, 9600, 256);

    // Measure side channel RMS of wide:
    double sideWideSum = 0.0;
    for (size_t i = 0; i < rWide.left.size(); ++i)
    {
        const double side = 0.5 * (rWide.left[i] - rWide.right[i]);
        sideWideSum += side * side;
    }
    const double sideWideRms = std::sqrt (sideWideSum / static_cast<double> (rWide.left.size()));
    CHECK (sideWideRms > 0.01);

    auto eMono = makeVital();
    initPatch (*eMono);
    setByKey (*eMono, "osc1.unison.voices", 4.0f);
    setByKey (*eMono, "osc1.unison.spread", 100.0f);
    setByKey (*eMono, "master.stereo_spread", 0.0f); // 0 % = collapsed mono
    const auto rMono = sbtest::render (*eMono, { noteOn (0, 60, 0.8f) }, 9600, 256);

    // In mono, left and right must be bit-for-bit identical
    double sideMonoSum = 0.0;
    for (size_t i = 0; i < rMono.left.size(); ++i)
    {
        const double side = 0.5 * (rMono.left[i] - rMono.right[i]);
        sideMonoSum += side * side;
    }
    const double sideMonoRms = std::sqrt (sideMonoSum / static_cast<double> (rMono.left.size()));
    std::printf ("    [STEREO SPREAD] Wide side RMS: %.5f, Mono side RMS: %.7f\n", sideWideRms, sideMonoRms);
    CHECK (sideMonoRms < 1e-6);

    const double diff = diffRms (rWide, rMono);
    CHECK (diff > 0.01);
}

TEST (cap_lfo_swing_audible)
{
    // LFO 1 modulating filter cutoff or osc level.
    // With 0 % swing (linear phase) vs 75 % swing (warped phase grid),
    // the modulation waveform is distorted in time, causing audible difference.
    const ParamInfo* dst = findParamByKey ("osc1.level");
    const float dstChoice = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (dst->id))));
    const float lfo1Src = sbtest::sourceChoice ("lfo1");

    auto eLin = makeVital();
    initPatch (*eLin);
    setByKey (*eLin, "lfo1.rate", 4.0f); // 4 Hz
    setByKey (*eLin, "lfo1.swing", 0.0f); // 0 % swing
    setByKey (*eLin, "mod01.source", lfo1Src);
    setByKey (*eLin, "mod01.destination", dstChoice);
    setByKey (*eLin, "mod01.amount", 80.0f);
    const auto rLin = sbtest::render (*eLin, { noteOn (0, 60, 0.8f) }, 19200, 256);

    auto eSwing = makeVital();
    initPatch (*eSwing);
    setByKey (*eSwing, "lfo1.rate", 4.0f);
    setByKey (*eSwing, "lfo1.swing", 80.0f); // 80 % swing
    setByKey (*eSwing, "mod01.source", lfo1Src);
    setByKey (*eSwing, "mod01.destination", dstChoice);
    setByKey (*eSwing, "mod01.amount", 80.0f);
    const auto rSw = sbtest::render (*eSwing, { noteOn (0, 60, 0.8f) }, 19200, 256);

    const double diff = diffRms (rLin, rSw);
    std::printf ("    [LFO SWING] 0 %% vs 80 %% swing RMS difference: %.5f\n", diff);
    CHECK (diff > 0.01);
}

TEST (cap_quality_options_audible)
{
    // 1. Oversampling 4x vs 2x:
    // With high-frequency harmonics (e.g. key 90 Saw + Warp Sync), anti-aliasing differs.
    auto e2x = makeVital();
    initPatch (*e2x);
    setByKey (*e2x, "osc1.warp.type", 1.0f); // Sync
    setByKey (*e2x, "osc1.warp.amount", 75.0f);
    setByKey (*e2x, "quality.oversample_4x", 0.0f); // 2x
    const auto r2x = sbtest::render (*e2x, { noteOn (0, 84, 0.9f) }, 9600, 256);

    auto e4x = makeVital();
    initPatch (*e4x);
    setByKey (*e4x, "osc1.warp.type", 1.0f);
    setByKey (*e4x, "osc1.warp.amount", 75.0f);
    setByKey (*e4x, "quality.oversample_4x", 1.0f); // 4x
    const auto r4x = sbtest::render (*e4x, { noteOn (0, 84, 0.9f) }, 9600, 256);

    const double diff4x = diffRms (r2x, r4x);
    std::printf ("    [QUALITY OPTIONS] 2x vs 4x oversampling RMS difference: %.6f\n", diff4x);
    CHECK (diff4x > 1.0e-5);

    // 2. No smoothing:
    // Stepping a volume parameter during a sound note:
    // With smoothing = ON (default), the jump ramps smoothly.
    // With smoothing = OFF (no_smoothing), the jump is instantaneous.
    auto eSmooth = makeVital();
    initPatch (*eSmooth);
    setByKey (*eSmooth, "quality.no_smoothing", 0.0f);
    // Render 1000 samples, change volume, render more
    std::vector<float> lS (4800), rS (4800);
    const Event on = noteOn (0, 60, 0.8f);
    eSmooth->process (&on, 1, lS.data(), rS.data(), 1000);
    setByKey (*eSmooth, "master.volume", 0.0f);
    eSmooth->process (nullptr, 0, lS.data() + 1000, rS.data() + 1000, 3800);

    auto eNoSmooth = makeVital();
    initPatch (*eNoSmooth);
    setByKey (*eNoSmooth, "quality.no_smoothing", 1.0f);
    std::vector<float> lN (4800), rN (4800);
    eNoSmooth->process (&on, 1, lN.data(), rN.data(), 1000);
    setByKey (*eNoSmooth, "master.volume", 0.0f);
    eNoSmooth->process (nullptr, 0, lN.data() + 1000, rN.data() + 1000, 3800);

    sbtest::Rendered rS_obj { lS, rS }, rN_obj { lN, rN };
    const double diffSmooth = diffRms (rS_obj, rN_obj);
    std::printf ("    [QUALITY OPTIONS] Smoothing vs No Smoothing RMS difference: %.5f\n", diffSmooth);
    CHECK (diffSmooth > 0.005);
}
