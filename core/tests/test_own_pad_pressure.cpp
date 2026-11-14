// SynthyBoy - own engine tests: pad pressure to Filter 1 cutoff (MPC shell layer, setOwnPadPressureToCutoff).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "voice/param_index.h"
#include "voice/voice.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

Event pressureEvent (int frame, int key, float pressure)
{
    Event e;
    e.frame = frame;
    e.type = Event::NoteExpression;
    e.expression = Event::Pressure;
    e.channel = 0;
    e.key = static_cast<int16_t> (key);
    e.value = pressure;
    return e;
}

void setByKey (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

std::unique_ptr<IEngine> makeEngine()
{
    auto e = createOwnEngine();
    e->prepare (kSr, kBlock, qualityForTier (Tier::low));
    setByKey (*e, "filter1.on", 1.0f);
    setByKey (*e, "filter1.cutoff", 400.0f);
    return e;
}

/** Plays key 60 and a pressure stream of ~170 values a second (every 282 frames) at `pressure`; returns the brightness (first difference energy / energy) of the last blocks. */
double renderBrightness (IEngine& e, float pressure, std::vector<float>* keep = nullptr)
{
    std::vector<float> l (kBlock), r (kBlock);
    double en = 0.0, dn = 0.0;
    int nextPressure = 0;
    for (int b = 0; b < 200; ++b)
    {
        std::vector<Event> ev;
        if (b == 0)
            ev.push_back (sbtest::noteOn (0, 60, 1.0f));
        for (int f = 0; f < kBlock; ++f)
            if (b * kBlock + f == nextPressure)
            {
                ev.push_back (pressureEvent (f, 60, pressure));
                nextPressure += 282;
            }
        e.process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), kBlock);
        if (keep != nullptr)
            keep->insert (keep->end(), l.begin(), l.end());
        if (b >= 150)
            for (int i = 1; i < kBlock; ++i)
            {
                en += static_cast<double> (l[i]) * l[i];
                const double d = static_cast<double> (l[i]) - l[i - 1];
                dn += d * d;
            }
    }
    return en > 0.0 ? dn / en : 0.0;
}

struct VoiceRig
{
    Voice v;
    std::vector<float> base;
    ModMatrix matrix;
    TickGlobals g;
    int cutIdx = 0;
    float baseCutoff = 1000.0f;

    explicit VoiceRig (int key, float depth)
    {
        v.prepare (kSr);
        NoteStart ns;
        ns.key = key;
        ns.tunedNote = static_cast<float> (key);
        ns.velocity = 1.0f;
        v.start (ns, 0.0f, 0.0f);
        base.assign (kMaxParams, 0.0f);
        for (size_t i = 0; i < paramCount(); ++i)
            base[i] = paramTable()[i].def;
        cutIdx = paramIndex().filter[0].cutoff;
        base[static_cast<size_t> (cutIdx)] = baseCutoff; // +36 semitones (8000 Hz) stays inside the range of the cutoff law
        matrix.compile (base.data());
        g.base = base.data();
        g.matrix = &matrix;
        g.sampleRate = kSr;
        g.bpm = 120.0;
        g.padPressureToCutoff = depth;
    }
    float semitones() const { return 12.0f * std::log2 (v.modulatedParam (cutIdx) / baseCutoff); }
};
} // namespace

TEST (pad_pressure_off_ignores_pressure_events)
{
    auto a = makeEngine();
    auto b = makeEngine();
    CHECK (setOwnPadPressureToCutoff (*b, 0.0f));
    std::vector<float> outA, outB;
    renderBrightness (*a, 0.0f, &outA);   // default: off, a pressure stream of 0
    renderBrightness (*b, 1.0f, &outB);   // explicitly off, full pressure
    CHECK_EQ (outA.size(), outB.size());
    CHECK_EQ (std::memcmp (outA.data(), outB.data(), outA.size() * sizeof (float)), 0);
}

TEST (pad_pressure_opens_the_filter)
{
    auto off = makeEngine();
    auto on = makeEngine();
    auto light = makeEngine();
    CHECK (setOwnPadPressureToCutoff (*on, 1.0f));
    CHECK (setOwnPadPressureToCutoff (*light, 0.35f));
    const double bOff = renderBrightness (*off, 1.0f);
    const double bLight = renderBrightness (*light, 1.0f);
    const double bOn = renderBrightness (*on, 1.0f);
    CHECK (bLight > bOff * 1.5);
    CHECK (bOn > bLight * 1.5);
    // no pressure: the layer adds nothing, whatever the depth
    auto idle = makeEngine();
    CHECK (setOwnPadPressureToCutoff (*idle, 1.0f));
    CHECK (renderBrightness (*idle, 0.0f) == renderBrightness (*makeEngine(), 0.0f));
}

TEST (pad_pressure_is_per_voice_and_only_the_cutoff)
{
    VoiceRig pressed (60, 1.0f), free (72, 1.0f);
    pressed.v.setPressure (1.0f);
    free.v.setPressure (0.0f);
    for (int t = 0; t < 400; ++t)
    {
        pressed.v.tick (pressed.g, 0);
        free.v.tick (free.g, 1);
    }
    CHECK (std::fabs (free.semitones()) < 1.0e-3f);
    CHECK (std::fabs (pressed.semitones() - kPadPressureSemitones) < 0.1f);
    // depth scales the range, half pressure at half depth = a quarter of it
    VoiceRig half (60, 0.5f);
    half.v.setPressure (0.5f);
    for (int t = 0; t < 400; ++t)
        half.v.tick (half.g, 0);
    CHECK (std::fabs (half.semitones() - 0.25f * kPadPressureSemitones) < 0.1f);
    // the second filter is not touched
    const int cut2 = paramIndex().filter[1].cutoff;
    CHECK (pressed.v.modulatedParam (cut2) == pressed.base[static_cast<size_t> (cut2)]);
}

TEST (pad_pressure_smoothing_step_response)
{
    VoiceRig rig (60, 1.0f);
    rig.v.setPressure (0.0f);
    rig.v.tick (rig.g, 0);
    rig.v.setPressure (1.0f);
    // one time constant (12 ms = 18 control periods of 32 samples at 48 kHz) = 63 % of the step
    for (int t = 0; t < 18; ++t)
        rig.v.tick (rig.g, 0);
    const float fraction = rig.semitones() / kPadPressureSemitones;
    CHECK (std::fabs (fraction - 0.632f) < 0.04f);
}

TEST (pad_pressure_stream_has_no_zipper)
{
    // the pad: 1/127 steps arriving every 5.9 ms; the cutoff moves in far finer steps than the pressure steps
    VoiceRig rig (60, 1.0f);
    float prev = 0.0f, maxStep = 0.0f;
    int step = 0;
    rig.v.tick (rig.g, 0); // the first period of a note takes the pressure as it is, the stream starts after it
    for (int t = 0; t < 4000; ++t)
    {
        if (t % 9 == 0 && step < 100)
            rig.v.setPressure (static_cast<float> (++step) / 127.0f);
        rig.v.tick (rig.g, 0);
        const float s = rig.semitones();
        maxStep = std::max (maxStep, std::fabs (s - prev));
        prev = s;
    }
    const float stepSemis = kPadPressureSemitones / 127.0f; // the jump of one pressure step, unsmoothed
    CHECK (maxStep < stepSemis * 0.2f);
    CHECK (prev > 0.0f);
}

TEST (pad_pressure_released_voice_returns_to_the_knob)
{
    VoiceRig rig (60, 1.0f);
    rig.v.setPressure (1.0f);
    for (int t = 0; t < 400; ++t)
        rig.v.tick (rig.g, 0);
    rig.v.setPressure (0.0f);
    for (int t = 0; t < 1000; ++t)
        rig.v.tick (rig.g, 0);
    CHECK (std::fabs (rig.semitones()) < 0.01f);
    rig.g.padPressureToCutoff = 0.0f;
    rig.v.tick (rig.g, 0);
    CHECK (rig.v.modulatedParam (rig.cutIdx) == rig.baseCutoff);
}

TEST (pad_pressure_voice_note_off_clears_pressure_and_returns_to_knob)
{
    // Voice::release() (NoteOff) must clear polyPressure so a decaying voice
    // does not keep its filter opened.
    VoiceRig rig (60, 1.0f);
    rig.v.setPressure (1.0f);
    for (int t = 0; t < 400; ++t)
        rig.v.tick (rig.g, 0);
    CHECK (rig.semitones() > 30.0f);
    // Real NoteOff release (not manual setPressure(0))
    rig.v.release (0.0f);
    for (int t = 0; t < 1000; ++t)
        rig.v.tick (rig.g, 0);
    CHECK (std::fabs (rig.semitones()) < 0.01f);
    rig.g.padPressureToCutoff = 0.0f;
    rig.v.tick (rig.g, 0);
    CHECK (rig.v.modulatedParam (rig.cutIdx) == rig.baseCutoff);
}

TEST (pad_pressure_channel_aftertouch_0xD0_ignored_even_in_mpe)
{
    // The pad pressure layer reacts strictly to poly aftertouch (0xA0, Event::Pressure).
    // Channel aftertouch (0xD0, Event::Aftertouch) must not open Filter 1 through the layer,
    // neither on standard channel 0 nor on MPE member channels (channel > 0).
    auto testChannel = [] (int chan, bool mpe)
    {
        auto makeTestEngine = [mpe] (float layerDepth)
        {
            auto e = createOwnEngine();
            e->prepare (kSr, kBlock, qualityForTier (Tier::low));
            setByKey (*e, "filter1.on", 1.0f);
            setByKey (*e, "filter1.cutoff", 400.0f);
            if (mpe)
                setByKey (*e, "master.mpe.on", 1.0f);
            CHECK (setOwnPadPressureToCutoff (*e, layerDepth));
            return e;
        };

        // Compare layer ON vs layer OFF under 0xD0 Channel Aftertouch:
        // Because the pad pressure layer ignores 0xD0, layer ON must be BIT-IDENTICAL to layer OFF!
        auto eOn = makeTestEngine (1.0f);
        auto eOff = makeTestEngine (0.0f);

        std::vector<float> l (kBlock), r (kBlock);
        Event on = sbtest::noteOn (0, 60, 1.0f);
        on.channel = static_cast<int16_t> (chan);

        Event at {};
        at.frame = 10;
        at.type = Event::Aftertouch;
        at.channel = static_cast<int16_t> (chan);
        at.value = 1.0f;

        const Event evs[2] = { on, at };
        eOn->process (evs, 2, l.data(), r.data(), kBlock);
        std::vector<float> outOn (l.begin(), l.end());

        eOff->process (evs, 2, l.data(), r.data(), kBlock);
        std::vector<float> outOff (l.begin(), l.end());

        for (int b = 0; b < 20; ++b)
        {
            eOn->process (nullptr, 0, l.data(), r.data(), kBlock);
            outOn.insert (outOn.end(), l.begin(), l.end());

            eOff->process (nullptr, 0, l.data(), r.data(), kBlock);
            outOff.insert (outOff.end(), l.begin(), l.end());
        }

        CHECK_EQ (outOn.size(), outOff.size());
        CHECK_EQ (std::memcmp (outOn.data(), outOff.data(), outOn.size() * sizeof (float)), 0);

        // In contrast, Poly Aftertouch (0xA0, Event::Pressure) DOES engage the layer and opens the filter:
        auto ePolyOn = makeTestEngine (1.0f);
        auto ePolyOff = makeTestEngine (0.0f);

        Event poly = pressureEvent (10, 60, 1.0f);
        poly.channel = static_cast<int16_t> (chan);
        const Event polyEvs[2] = { on, poly };

        ePolyOn->process (polyEvs, 2, l.data(), r.data(), kBlock);
        std::vector<float> outPolyOn (l.begin(), l.end());

        ePolyOff->process (polyEvs, 2, l.data(), r.data(), kBlock);
        std::vector<float> outPolyOff (l.begin(), l.end());

        for (int b = 0; b < 20; ++b)
        {
            ePolyOn->process (nullptr, 0, l.data(), r.data(), kBlock);
            outPolyOn.insert (outPolyOn.end(), l.begin(), l.end());

            ePolyOff->process (nullptr, 0, l.data(), r.data(), kBlock);
            outPolyOff.insert (outPolyOff.end(), l.begin(), l.end());
        }

        CHECK_EQ (outPolyOn.size(), outPolyOff.size());
        CHECK (std::memcmp (outPolyOn.data(), outPolyOff.data(), outPolyOn.size() * sizeof (float)) != 0);
    };

    testChannel (0, false); // Standard channel 0, MPE off
    testChannel (1, true);  // MPE member channel 1, MPE on
}

TEST (pad_pressure_preset_with_poly_aftertouch_to_cutoff_does_not_double)
{
    // Case 1: Mod matrix has Poly Aftertouch -> Filter 1 Cutoff route.
    // The pad pressure layer must NOT add its cutoff offset (no doubling).
    VoiceRig rigWithRoute (60, 1.0f);
    const int cutChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (rigWithRoute.cutIdx)));
    const auto& slot0 = paramIndex().slot[0];
    rigWithRoute.base[static_cast<size_t> (slot0.source)] = static_cast<float> (kSrcPolyAftertouch);
    rigWithRoute.base[static_cast<size_t> (slot0.destination)] = static_cast<float> (cutChoice);
    rigWithRoute.base[static_cast<size_t> (slot0.amount)] = 50.0f; // +50 %
    rigWithRoute.matrix.compile (rigWithRoute.base.data());
    CHECK (rigWithRoute.matrix.hasPolyAftertouchToFilter1Cutoff());

    // Run voice with padPressureToCutoff = 1.0f and pressure = 1.0f
    rigWithRoute.v.setPressure (1.0f);
    for (int t = 0; t < 400; ++t)
        rigWithRoute.v.tick (rigWithRoute.g, 0);
    const float semisWithLayerOn = rigWithRoute.semitones();

    // Now run with padPressureToCutoff = 0.0f (layer disabled)
    VoiceRig rigLayerOff (60, 0.0f);
    rigLayerOff.base[static_cast<size_t> (slot0.source)] = static_cast<float> (kSrcPolyAftertouch);
    rigLayerOff.base[static_cast<size_t> (slot0.destination)] = static_cast<float> (cutChoice);
    rigLayerOff.base[static_cast<size_t> (slot0.amount)] = 50.0f;
    rigLayerOff.matrix.compile (rigLayerOff.base.data());
    rigLayerOff.v.setPressure (1.0f);
    for (int t = 0; t < 400; ++t)
        rigLayerOff.v.tick (rigLayerOff.g, 0);
    const float semisWithLayerOff = rigLayerOff.semitones();

    // Semitones must be strictly positive (the matrix modulated cutoff) and IDENTICAL (layer added 0)
    CHECK (semisWithLayerOff > 1.0f);
    CHECK (std::fabs (semisWithLayerOn - semisWithLayerOff) < 1.0e-4f);

    // Case 2: Mod matrix does NOT route Poly Aftertouch to Filter 1 Cutoff (e.g. routes to resonance instead).
    // The pad pressure layer MUST add its cutoff offset normally.
    const ParamInfo* resInfo = findParamByKey ("filter1.resonance");
    CHECK (resInfo != nullptr);
    const int resChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (resInfo->id))));
    VoiceRig rigOtherDest (60, 1.0f);
    rigOtherDest.base[static_cast<size_t> (slot0.source)] = static_cast<float> (kSrcPolyAftertouch);
    rigOtherDest.base[static_cast<size_t> (slot0.destination)] = static_cast<float> (resChoice);
    rigOtherDest.base[static_cast<size_t> (slot0.amount)] = 50.0f;
    rigOtherDest.matrix.compile (rigOtherDest.base.data());
    CHECK (! rigOtherDest.matrix.hasPolyAftertouchToFilter1Cutoff());

    rigOtherDest.v.setPressure (1.0f);
    for (int t = 0; t < 400; ++t)
        rigOtherDest.v.tick (rigOtherDest.g, 0);
    // Pad pressure layer opened cutoff by kPadPressureSemitones (36 semitones)
    CHECK (std::fabs (rigOtherDest.semitones() - kPadPressureSemitones) < 0.1f);
}

TEST (pad_pressure_mpe_pressure_lead_preset_audio_does_not_double)
{
    // Loads content/presets/Lead/mpe_pressure_lead.sbpreset
    // When rendered with padPressureToCutoff = 1.0f vs 0.0f, the output must be identical
    // because the preset already has Poly Aftertouch -> Filter 1 Cutoff in its mod matrix.
    std::ifstream in (std::string (SYNTHYBOY_TEST_SOURCE_DIR) + "/content/presets/Lead/mpe_pressure_lead.sbpreset", std::ios::binary);
    CHECK (in.is_open());
    if (! in)
        return;
    std::string data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    PresetMeta meta;
    ParamValues vals = ParamValues::defaults();
    StateContent content;
    LoadResult r = loadPreset (data.data(), data.size(), meta, vals, content);
    CHECK (r.status == LoadStatus::ok);

    auto engineOn = createOwnEngine();
    engineOn->prepare (kSr, kBlock, qualityForTier (Tier::low));
    for (size_t i = 0; i < paramCount(); ++i)
        engineOn->setParam (paramTable()[i].id, vals.plain[i]);
    engineOn->setContent (content);
    CHECK (setOwnPadPressureToCutoff (*engineOn, 1.0f));

    auto engineOff = createOwnEngine();
    engineOff->prepare (kSr, kBlock, qualityForTier (Tier::low));
    for (size_t i = 0; i < paramCount(); ++i)
        engineOff->setParam (paramTable()[i].id, vals.plain[i]);
    engineOff->setContent (content);
    CHECK (setOwnPadPressureToCutoff (*engineOff, 0.0f));

    std::vector<float> outOn, outOff;
    const double bOn = renderBrightness (*engineOn, 1.0f, &outOn);
    const double bOff = renderBrightness (*engineOff, 1.0f, &outOff);
    CHECK (bOn > 0.0);
    CHECK_EQ (bOn, bOff);
    CHECK_EQ (outOn.size(), outOff.size());
    CHECK_EQ (std::memcmp (outOn.data(), outOff.data(), outOn.size() * sizeof (float)), 0);
}
