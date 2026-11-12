// SynthyBoy - engine-vital tests: GROUPA2 WP-17: the live read-out (IEngine::setReadoutTargets / copyReadout): the values of the modulation sources, the phases of the
// LFOs and the stages of the envelopes, and the modulated value of the knobs the display subscribes to. 13-new-keys.md 4.32.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/readout.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_bindings.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;

std::unique_ptr<IEngine> make()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 16.0f);
    e->prepare (kSr, 256, Quality {});
    return e;
}

/** Renders `seconds` in blocks of 256, with the events of the first block, and returns the readout after the last block. */
LiveValues renderRead (IEngine& e, const std::vector<Event>& events, double seconds)
{
    const auto r = sbtest::render (e, events, static_cast<int> (seconds * kSr), 256);
    (void) r;
    LiveValues v;
    CHECK (e.copyReadout (v));
    return v;
}

int sourceIndex (const char* key) { return static_cast<int> (sbtest::sourceChoice (key)) - 1; }

int paramIndexOf (const char* key) { return findParamIndex (findParamByKey (key)->id); }

void route (IEngine& e, int slot, const char* source, const char* destinationKey, float amountPercent)
{
    char key[32];
    std::snprintf (key, sizeof key, "mod%02d.source", slot);
    setByKey (e, key, sbtest::sourceChoice (source));
    std::snprintf (key, sizeof key, "mod%02d.destination", slot);
    const ParamInfo* destination = findParamByKey (destinationKey);
    setByKey (e, key, static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))));
    std::snprintf (key, sizeof key, "mod%02d.amount", slot);
    setByKey (e, key, amountPercent);
}
}

TEST (readout_is_empty_before_the_first_block_and_silent_engines_have_none)
{
    auto e = make();
    LiveValues v;
    v.block = 77;
    CHECK (! e->copyReadout (v));
    (void) sbtest::render (*e, {}, 256, 256);
    CHECK (e->copyReadout (v));
    CHECK (v.block >= 1);
    CHECK_EQ (v.activeVoices, 0);
    CHECK_EQ (v.numTargets, 0);
    for (int n = 0; n < LiveValues::kNumLfos; ++n)
        CHECK_NEAR (v.lfoPhase[n], -1.0f, 1.0e-6f);          // no voice: no phase
    auto own = createEngine (EngineKind::own);
    own->copyReadout (v); // the own engine reports the read-out of the sample type oscillators (and what its live bus adds); the call is safe on any block count
    own->setReadoutTargets (nullptr, 0);
}

TEST (readout_sources_envelopes_and_lfo_phases_follow_the_voice)
{
    auto e = make();
    setByKey (*e, "env1.attack", 400.0f);
    setByKey (*e, "env1.decay", 300.0f);
    setByKey (*e, "env1.sustain", 60.0f);
    setByKey (*e, "env1.release", 500.0f);
    setByKey (*e, "lfo1.rate", 2.0f);
    setByKey (*e, "lfo1.sync", 0.0f);
    setByKey (*e, "lfo1.sync_type", 0.0f);   // retrigger: the phase starts with the note
    setByKey (*e, "macro.3", 40.0f);
    route (*e, 1, "lfo1", "osc1.level", 10.0f);   // an LFO that nothing uses is switched off by the engine and has no phase
    route (*e, 2, "env2", "osc1.pan", 10.0f);
    std::vector<Event> events { noteOn (0, 60, 0.5f) };
    float lastPhase = -1.0f;
    bool sawAttack = false, sawDecay = false, sawRelease = false;
    double maxEnv = 0.0, sustainLevel = -1.0;
    LiveValues v;
    for (int block = 0; block < 400; ++block)
    {
        std::vector<float> l (256), r (256);
        const std::vector<Event> now = block == 0 ? events : (block == 200 ? std::vector<Event> { noteOff (0, 60) } : std::vector<Event> {});
        e->process (now.data(), static_cast<int> (now.size()), l.data(), r.data(), 256);
        CHECK (e->copyReadout (v));
        const double t = (block + 1) * 256.0 / kSr;
        if (block < 200)
        {
            CHECK_EQ (v.activeVoices, 1);
            // the LFO phase of the heard voice: 2 Hz from the note on
            const double expected = std::fmod (2.0 * t, 1.0);
            const double got = v.lfoPhase[0];
            const double diff = std::min (std::fabs (got - expected), 1.0 - std::fabs (got - expected));
            CHECK (diff < 0.03);
            if (lastPhase >= 0.0f && block % 50 == 0)
                CHECK (got != lastPhase);
            lastPhase = static_cast<float>(got);
            CHECK (v.source[sourceIndex ("lfo1")] >= -1.001f && v.source[sourceIndex ("lfo1")] <= 1.001f);
        }
        const int stage = static_cast<int> (v.envStage[0]);
        sawAttack = sawAttack || stage == 1;
        sawDecay = sawDecay || stage == 3;
        sawRelease = sawRelease || stage == 4;
        maxEnv = std::max (maxEnv, static_cast<double> (v.source[sourceIndex ("env1")]));
        if (block == 190)
            sustainLevel = v.source[sourceIndex ("env1")];
        CHECK (v.envPosition[0] >= 0.0f && v.envPosition[0] <= 1.0f);
        if (block == 3)
            CHECK_EQ (stage, 1);              // 0.04 s into a 0.4 s attack
    }
    CHECK (sawAttack && sawDecay && sawRelease);
    CHECK (maxEnv > 0.95);
    CHECK_NEAR (sustainLevel, 0.6, 0.03);
    CHECK_NEAR (v.source[sourceIndex ("macro3")], 0.4, 1.0e-3);
    // the note off 2.1 s ago is long over: the voice is gone and the readout says so
    CHECK_EQ (v.activeVoices, 0);
    CHECK_NEAR (v.lfoPhase[0], -1.0f, 1.0e-6f);

    // the other sources: velocity, mod wheel, the voice index of the voice control
    auto f = make();
    setByKey (*f, "voicectl.length", 4.0f);
    std::vector<Event> held { noteOn (0, 60, 0.25f), noteOn (0, 64, 0.8f) };
    const LiveValues w = renderRead (*f, held, 0.1);
    CHECK_EQ (w.activeVoices, 2);
    CHECK (w.source[sourceIndex ("velocity")] > 0.2f);           // the voice that was played last (or the first of the pair): a velocity of one of the two notes
    CHECK (w.source[sourceIndex ("note")] > 0.0f);
    Event wheel = noteOn (0, 0, 0.7f, 0);
    wheel.type = Event::ModWheel;
    const LiveValues x = renderRead (*f, { wheel }, 0.05);
    CHECK_NEAR (x.source[sourceIndex ("mod_wheel")], 0.7f, 1.0e-3f);
}

TEST (readout_targets_give_the_modulated_value_of_a_knob)
{
    auto e = make();
    setByKey (*e, "filter1.on", 1.0f);
    setByKey (*e, "filter1.cutoff", 1000.0f);
    setByKey (*e, "osc1.level", 60.0f);
    const int targets[] = { paramIndexOf ("filter1.cutoff"), paramIndexOf ("osc1.level"), paramIndexOf ("filter1.resonance") };
    e->setReadoutTargets (targets, 3);
    // nothing modulates: the live value is the plain value, exactly
    LiveValues v = renderRead (*e, { noteOn (0, 60, 1.0f) }, 0.1);
    CHECK_EQ (v.numTargets, 3);
    CHECK_NEAR (v.target[0], 1000.0f, 1.0e-3f);
    CHECK_NEAR (v.target[1], 60.0f, 1.0e-3f);
    CHECK_NEAR (v.target[2], e->getParam (findParamByKey ("filter1.resonance")->id), 1.0e-3f);

    // a macro modulates the cutoff: more macro, a higher live value; a negative amount lowers it; the other knobs stay
    route (*e, 1, "macro1", "filter1.cutoff", 40.0f);
    float previous = 0.0f;
    for (const float macro : { 0.0f, 25.0f, 50.0f, 100.0f })
    {
        setByKey (*e, "macro.1", macro);
        v = renderRead (*e, {}, 0.1);
        if (macro == 0.0f)
            CHECK_NEAR (v.target[0], 1000.0f, 1.0f);
        else
            CHECK (v.target[0] > previous);
        previous = v.target[0];
        CHECK_NEAR (v.target[1], 60.0f, 1.0e-3f);
    }
    CHECK (previous > 3000.0f);                 // 40 % of the range of the cutoff above 1000 Hz
    setByKey (*e, "mod01.amount", -40.0f);
    setByKey (*e, "macro.1", 100.0f);
    v = renderRead (*e, {}, 0.1);
    CHECK (v.target[0] < 1000.0f && v.target[0] >= findParamByKey ("filter1.cutoff")->min);

    // an LFO makes the live value move from block to block, a per voice source (velocity) gives each note its own value
    route (*e, 2, "lfo1", "osc1.level", 50.0f);
    setByKey (*e, "mod01.amount", 0.0f);
    setByKey (*e, "lfo1.rate", 3.0f);
    setByKey (*e, "lfo1.shape", 1.0f);
    std::set<int> seen;
    for (int block = 0; block < 60; ++block)
    {
        std::vector<float> l (256), r (256);
        e->process (nullptr, 0, l.data(), r.data(), 256);
        e->copyReadout (v);
        seen.insert (static_cast<int> (std::lround (v.target[1] * 10.0f)));
    }
    CHECK (seen.size() > 8);
    CHECK (*seen.begin() >= 600 - 1 && *seen.rbegin() > 600 + 100);       // the unipolar slot adds up to 50 % of the range to the plain value 60

    // a new target list takes over; more than the capacity is cut
    std::vector<int> many (LiveValues::kMaxTargets + 20, targets[0]);
    e->setReadoutTargets (many.data(), static_cast<int> (many.size()));
    v = renderRead (*e, {}, 0.05);
    CHECK_EQ (v.numTargets, LiveValues::kMaxTargets);
    e->setReadoutTargets (targets, 1);
    v = renderRead (*e, {}, 0.05);
    CHECK_EQ (v.numTargets, 1);
    // an index outside the table, and a choice (no continuous value): the plain value or 0, never a crash
    const int odd[] = { -5, 1000000, paramIndexOf ("filter1.model") };
    e->setReadoutTargets (odd, 3);
    v = renderRead (*e, {}, 0.05);
    CHECK_EQ (v.numTargets, 3);
    CHECK_NEAR (v.target[0], 0.0f, 1.0e-6f);
    CHECK_NEAR (v.target[2], e->getParam (findParamByKey ("filter1.model")->id), 1.0e-6f);
}

TEST (invert_param_undoes_convert_param_for_every_continuous_binding)
{
    size_t checked = 0;
    for (const Binding& b : vitalBindings())
    {
        if (b.conv == Conv::index || b.conv == Conv::indexMap)
        {
            CHECK (std::isnan (invertParam (b, 1.0, 48000.0)));
            continue;
        }
        const ParamInfo& info = paramTable()[b.param];
        for (const double x : { static_cast<double> (info.min), static_cast<double> (info.def), 0.5 * (info.min + info.max), static_cast<double> (info.max) })
        {
            // the converters that clamp (a cutoff below 20 Hz, a time at the floor) are inverted only inside what they keep
            if ((b.conv == Conv::noteFromHz && (x < 20.0 || x > 20000.0)) || (b.conv == Conv::log2Ms && x <= 0.0) || (b.conv == Conv::log2Hz && x <= 0.0)
                || (b.conv == Conv::log2PeriodMs && x <= 0.0) || (b.conv == Conv::volumeDb && (x <= -60.0 || x > 6.0)) || (b.conv == Conv::sqrtScaled && x < 0.0)
                || ((b.conv == Conv::quarticMs || b.conv == Conv::quarticBeats) && x < 0.0))
                continue;
            const double raw = convertParam (b, x, 48000.0, 120.0);
            const double back = invertParam (b, raw, 48000.0, 120.0);
            if (std::isnan (back))
                continue;   // linear with a = 0
            CHECK_NEAR (back, x, 1.0e-4 * std::max (1.0, std::fabs (x)));
            ++checked;
        }
    }
    CHECK (checked > 1500);
}
