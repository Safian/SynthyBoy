// SynthyBoy - engine-vital tests: a wavetable as the filter of the spectral oscillator (REMAINDER item 2, block B32, 13-new-keys.md 4.42; Serum 2 manual p.120).
// kCap2SpectralFilterTable is proven here: the switch, the table and the frame are each rendered off and on.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>

#include "groupc_util.h"
#include "synthyboy/state.h"
#include "vital_tables.h"

using namespace groupc;

namespace
{
void spectralPatch (IEngine& e, Content& c)
{
    basePatch (e, c, kOscTypeSpectral);
    setByKey (e, "osc1.sample.scan", 0.0f);
    setByKey (e, "osc1.spec.manual", 1.0f);
    setByKey (e, "osc1.spec.position", 50.0f);
}

/** A table of `frames` cycles for oscillator 1 in the pack "test": frame f is constant `level(f)` (the filter curve of a constant frame is flat at gain level + 1), with a notch at
    the quarter of the cycle when `notch(f)`. */
void table (Content& c, int osc, const std::vector<std::vector<float>>& cycles)
{
    std::vector<float> all;
    for (const auto& cycle : cycles)
        all.insert (all.end(), cycle.begin(), cycle.end());
    c.store.add ("test", "table.wav", wav16 (all));
    ContentWavetable w;
    w.osc = osc;
    w.ref.pack = "test";
    w.ref.file = "table.wav";
    w.ref.name = "filter table";
    w.frames = static_cast<int> (cycles.size());
    c.state.wavetables.push_back (w);
}

std::vector<float> constantFrame (float v) { return std::vector<float> (2048, v); }

/** A frame that is -1 (no gain) in the middle of the curve and 0 elsewhere. */
std::vector<float> notchFrame()
{
    std::vector<float> f (2048, 0.0f);
    for (int i = 0; i < 2048; ++i)
        if (std::abs (i - 1024) < 90)   // the frequency axis is log: the middle of the cycle is 632 Hz
            f[static_cast<size_t> (i)] = -1.0f;
    return f;
}
} // namespace

TEST (spectral_filter_table_keys_capability_and_curve_of_a_frame)
{
    auto e = makeVital();
    CHECK ((e->capabilities2() & kCap2SpectralFilterTable) != 0);
    for (const char* key : { "osc1.spec.flt_use", "osc2.spec.flt_table", "osc3.spec.flt_frame" })
        CHECK_EQ (requiredCaps2 (static_cast<size_t> (findParamIndex (findParamByKey (key)->id))), static_cast<EngineCaps2Mask> (kCap2Spectral | kCap2SpectralFilterTable));
    CHECK ((findParamByKey ("osc1.spec.flt_frame")->flags & kParamModulatable) != 0);
    CHECK ((findParamByKey ("osc1.spec.flt_use")->flags & kParamModulatable) == 0);
    CHECK_EQ (static_cast<int> (choiceCount (*findParamByKey ("osc1.spec.flt_table"))), kNumWavetableChoices);
    CHECK_EQ (std::string (capName2 (kCap2SpectralFilterTable)), "spectral_filter_table");
    // the curve of a frame: y = (v + 1) / 2, so the gain 2 y is v + 1
    std::vector<float> frame (2048);
    for (int i = 0; i < 2048; ++i)
        frame[static_cast<size_t> (i)] = i < 1024 ? -1.0f : 1.0f;
    float curve[synthyboy::vitaltables::kFilterCurvePoints];
    synthyboy::vitaltables::frameToFilterCurve (frame.data(), curve);
    CHECK_NEAR (curve[10], 0.0f, 1.0e-6f);
    CHECK_NEAR (curve[250], 1.0f, 1.0e-6f);
    CHECK_NEAR (curve[128], 0.5f, 0.5f);   // the edge
}

TEST (spectral_filter_from_a_wavetable_off_and_on_changes_the_sound_kCap2SpectralFilterTable)
{
    Content content;
    content.sample (richTone (220.0, 2.0));
    table (content, 1, { constantFrame (-1.0f), notchFrame() });   // frame 0 is silence (gain 0 everywhere), frame 1 is gain 1 with a hole
    const auto render = [&] (const std::vector<std::pair<const char*, float>>& keys)
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        for (const auto& k : keys)
            setByKey (*e, k.first, k.second);
        return play (*e, 57, 0.8);
    };
    const auto off = render ({});
    CHECK (rmsBetween (off, 0.2, 0.7) > 0.02);
    // the built-in Saw as the filter (a rising curve): the sound changes against no filter
    const auto saw = render ({ { "osc1.spec.flt_use", 1.0f }, { "osc1.spec.flt_table", 2.0f } });
    CHECK (relativeDifference (off, saw, 0.2, 0.7) > 0.05 && sbtest::allFinite (saw.left));
    // another built-in table (the Square) differs from the Saw
    const auto square = render ({ { "osc1.spec.flt_use", 1.0f }, { "osc1.spec.flt_table", 3.0f } });
    CHECK (relativeDifference (saw, square, 0.2, 0.7) > 0.02);
    // the switch off again: bit identical to the render without the keys (the curve decides, and there is none)
    const auto offAgain = render ({ { "osc1.spec.flt_use", 0.0f }, { "osc1.spec.flt_table", 3.0f }, { "osc1.spec.flt_frame", 80.0f } });
    CHECK (identical (off, offAgain));
    // the Preset table: frame 0 mutes the oscillator (gain 0), frame 1 lets it through except the hole
    const auto muted = render ({ { "osc1.spec.flt_use", 1.0f }, { "osc1.spec.flt_table", 13.0f }, { "osc1.spec.flt_frame", 0.0f } });
    const auto hole = render ({ { "osc1.spec.flt_use", 1.0f }, { "osc1.spec.flt_table", 13.0f }, { "osc1.spec.flt_frame", 100.0f } });
    CHECK (rmsBetween (muted, 0.2, 0.7) < 0.2 * rmsBetween (off, 0.2, 0.7));
    CHECK (rmsBetween (hole, 0.2, 0.7) > 3.0 * rmsBetween (muted, 0.2, 0.7));
    // the frame blends: halfway is neither
    const auto half = render ({ { "osc1.spec.flt_use", 1.0f }, { "osc1.spec.flt_table", 13.0f }, { "osc1.spec.flt_frame", 50.0f } });
    CHECK (relativeDifference (muted, half, 0.2, 0.7) > 0.05 && relativeDifference (hole, half, 0.2, 0.7) > 0.02);
    // a Preset table choice without a table of the oscillator: the drawn curve (none) answers, the sound is the unfiltered one
    Content nothing;
    nothing.sample (richTone (220.0, 2.0));
    auto e = makeVital();
    spectralPatch (*e, nothing);
    setByKey (*e, "osc1.spec.flt_use", 1.0f);
    setByKey (*e, "osc1.spec.flt_table", 13.0f);
    CHECK (identical (off, play (*e, 57, 0.8)));
    // the other spectral keys keep working with it: CUT shifts the curve, MIX 0 is the dry sound
    const auto dry = render ({ { "osc1.spec.flt_use", 1.0f }, { "osc1.spec.flt_table", 13.0f }, { "osc1.spec.flt_frame", 0.0f }, { "osc1.spec.mix", 0.0f } });
    CHECK (identical (off, dry) || relativeDifference (off, dry, 0.2, 0.7) < 0.01);
}

TEST (spectral_filter_frame_is_modulatable)
{
    Content content;
    content.sample (richTone (220.0, 2.0));
    table (content, 1, { constantFrame (-1.0f), notchFrame() });
    const auto render = [&] (float macro)
    {
        auto e = makeVital();
        spectralPatch (*e, content);
        setByKey (*e, "osc1.spec.flt_use", 1.0f);
        setByKey (*e, "osc1.spec.flt_table", 13.0f);
        setByKey (*e, "mod01.source", sbtest::sourceChoice ("macro1"));
        setByKey (*e, "mod01.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("osc1.spec.flt_frame")->id)))));
        setByKey (*e, "mod01.amount", 100.0f);
        setByKey (*e, "macro.1", macro);
        return play (*e, 57, 0.8);
    };
    const auto low = render (0.0f), high = render (100.0f);
    CHECK (rmsBetween (high, 0.2, 0.7) > 3.0 * rmsBetween (low, 0.2, 0.7));
    CHECK (sbtest::allFinite (low.left) && sbtest::allFinite (high.left));
}
