// SynthyBoy - engine-vital tests: GROUPA2 WP-02: IEngine::silence () (the host transport stopped: notes end and the tails of the effects are flushed) and
// IEngine::setUltraRender () (a host bounce runs at 4x oversampling). 13-new-keys.md 4.31.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synthyboy/engine.h"
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

std::unique_ptr<IEngine> make()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 16.0f);
    e->prepare (kSr, 256, Quality {});
    // a long tail on purpose: a reverb with a long decay and a delay with feedback behind a note that is still held
    setByKey (*e, "reverb.on", 1.0f);
    setByKey (*e, "reverb.mix", 100.0f);
    setByKey (*e, "reverb.decay", 6000.0f);
    setByKey (*e, "delay.on", 1.0f);
    setByKey (*e, "delay.mix", 60.0f);
    setByKey (*e, "delay.feedback", 70.0f);
    setByKey (*e, "env1.release", 4000.0f);
    return e;
}

/** Renders `seconds` of blocks of 256 and returns the peak of the last `tailSeconds`. */
float renderPeak (IEngine& e, const std::vector<Event>& events, double seconds, double tailSeconds)
{
    const auto r = sbtest::render (e, events, static_cast<int> (seconds * kSr), 256);
    return std::max (sbtest::peak (r.left, static_cast<size_t> ((seconds - tailSeconds) * kSr)), sbtest::peak (r.right, static_cast<size_t> ((seconds - tailSeconds) * kSr)));
}
}

TEST (silence_ends_the_notes_and_flushes_the_tails_at_once)
{
    // control: the note is released and the tails ring on
    auto control = make();
    const std::vector<Event> notes { noteOn (0, 60), noteOn (0, 67), noteOff (static_cast<int> (0.5 * kSr), 60), noteOff (static_cast<int> (0.5 * kSr), 67) };
    (void) sbtest::render (*control, notes, static_cast<int> (0.6 * kSr), 256);
    CHECK (control->activeVoices() > 0);
    const float ringing = renderPeak (*control, {}, 0.4, 0.2);
    CHECK (ringing > 1.0e-3f);

    // silence () between two blocks: nothing is left, not even the tails
    auto e = make();
    (void) sbtest::render (*e, { noteOn (0, 60), noteOn (0, 67) }, static_cast<int> (0.6 * kSr), 256); // the notes are still held
    CHECK (e->activeVoices() > 0);
    e->silence();
    CHECK_EQ (e->activeVoices(), 0);
    const auto after = sbtest::render (*e, {}, static_cast<int> (0.4 * kSr), 256);
    CHECK (sbtest::allFinite (after.left) && sbtest::allFinite (after.right));
    // the filters of the output stage may still let a few thousandths of a percent through in the first block (-69 dBFS measured, dead after two blocks)
    for (const auto* channel : { &after.left, &after.right })
    {
        CHECK (sbtest::peak (*channel, 0, 256) < 1.0e-3f);
        CHECK (sbtest::peak (*channel, 512) < 1.0e-6f);
    }
    CHECK (sbtest::peak (after.left, 512) < 0.001f * ringing);

    // the engine is fully alive afterwards: the parameters stayed, a new note sounds, the note off of the old key does not disturb it
    const auto again = sbtest::render (*e, { noteOff (0, 60), noteOn (10, 60) }, static_cast<int> (0.4 * kSr), 256);
    CHECK (sbtest::rmsOf (again.left, static_cast<size_t> (0.2 * kSr), static_cast<size_t> (0.4 * kSr)) > 0.01);
    CHECK_NEAR (e->getParam (findParamByKey ("reverb.mix")->id), 100.0f, 1.0e-3f);
    // and a second silence () with nothing sounding is harmless
    e->silence();
    e->silence();
    CHECK_EQ (e->activeVoices(), 0);
}

TEST (silence_leaves_the_controllers_alone)
{
    auto e = make();
    e->silence(); // before anything sounded
    const Event bend = sbtest::pitchBend (0, 1.0f);
    const auto r = sbtest::render (*e, { bend, noteOn (100, 60) }, static_cast<int> (0.3 * kSr), 256);
    e->silence();
    // the bend wheel stays where it was: the next note is bent up by the range (2 semitones) without a new bend message
    const auto next = sbtest::render (*e, { noteOn (0, 60) }, static_cast<int> (0.5 * kSr), 256);
    const auto p = sbtest::powerSpectrum (next.left, static_cast<size_t> (0.2 * kSr), 16384, 16384, sbtest::Window::blackmanHarris);
    const double f = sbtest::peakFrequency (p, 16384, kSr, 261.6256 * std::pow (2.0, 2.0 / 12.0));
    CHECK_NEAR (1200.0 * std::log2 (f / 261.6256), 200.0, 15.0);
    (void) r;
}

TEST (ultra_render_is_the_4x_oversampling_of_the_quality_option)
{
    const auto render = [] (bool ultra, bool key)
    {
        auto e = createEngine (EngineKind::vital);
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->setUltraRender (ultra);
        e->prepare (kSr, 256, Quality {});
        if (key)
        {
            setByKey (*e, "quality.oversample_4x", 1.0f);
            CHECK (e->needsPrepare());   // the key is read by prepare () only
            e->prepare (kSr, 256, Quality {});
        }
        CHECK (! e->needsPrepare());
        setByKey (*e, "osc1.wavetable", 1.0f);
        setByKey (*e, "filter1.on", 0.0f);
        return sbtest::render (*e, { noteOn (0, 96) }, static_cast<int> (0.5 * kSr), 256);
    };
    const auto plain = render (false, false);
    const auto ultra = render (true, false);
    const auto asKey = render (false, true);
    CHECK (plain.left != ultra.left);                       // the option changes the render (the aliasing of a high saw)
    CHECK (ultra.left == asKey.left && ultra.right == asKey.right); // and it is exactly the 4x oversampling of the quality option
    // a change after prepare () is reported, not applied behind the back of the buffers
    auto e = createEngine (EngineKind::vital);
    e->prepare (kSr, 256, Quality {});
    CHECK (! e->needsPrepare());
    e->setUltraRender (true);
    CHECK (e->needsPrepare());
    e->prepare (kSr, 256, Quality {});
    CHECK (! e->needsPrepare());
    e->setUltraRender (false);
    CHECK (e->needsPrepare());
    // an engine without the option ignores it
    auto own = createEngine (EngineKind::own);
    own->setUltraRender (true);
    own->silence();
    CHECK (! own->needsPrepare());
}
