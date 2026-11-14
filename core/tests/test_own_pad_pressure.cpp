// SynthyBoy - own engine tests: pad pressure to Filter 1 cutoff (MPC shell layer, setOwnPadPressureToCutoff).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/params.h"
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
