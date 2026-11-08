// SynthyBoy - engine-vital tests: per-voice point modulation of a custom LFO shape and lfoN.mono for the envelope / loop modes
// (docs/ui-design/13-new-keys.md Q17 / G65, assumption A45; docs/ui-design/LFOVOICE-REPORT.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The point modulation of a custom shape reads its sources per voice: velocity, envelopes, ... of THAT voice. Every test renders notes
// that differ in such a source and asserts different LFO output; the level of the oscillator is the LFO, so a note's loudness is its
// own LFO value.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "alloc_hook.h"
#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"
#include "vital_point_mod.h"

using namespace synthyboy;
using sbtest::choiceValue;
using sbtest::noteOff;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;
unsigned long g_allocationsInProcess = 0;

int frames (double seconds)
{
    return static_cast<int> (seconds * kSr);
}

std::unique_ptr<IEngine> makeVital (const Quality& q = qualityForTier (Tier::high))
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 32.0f);
    // a plain sine whose level is the LFO (osc level 0 plus lfo1 x 100 %)
    setByKey (*e, "osc1.wavetable", 0.0f);
    setByKey (*e, "osc1.level", 0.0f);
    setByKey (*e, "osc1.unison.voices", 1.0f);
    setByKey (*e, "osc1.random_phase", 0.0f);
    setByKey (*e, "filter1.on", 0.0f);
    setByKey (*e, "master.volume", -6.0f);
    setByKey (*e, "lfo1.shape", 12.0f); // Custom
    setByKey (*e, "lfo1.rate", 0.01f);
    setByKey (*e, "lfo1.smooth", 0.0f);
    setByKey (*e, "mod01.source", sbtest::sourceChoice ("lfo1"));
    const ParamInfo* destination = findParamByKey ("osc1.level");
    setByKey (*e, "mod01.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))));
    setByKey (*e, "mod01.amount", 100.0f);
    e->prepare (kSr, kBlock, q);
    return e;
}

ContentPoint point (float x, float y)
{
    return ContentPoint (x, y, 0.0f);
}

/** A constant shape of height `y` whose height follows `source` (both points, Y), scaled by `amount`. */
StateContent constantShape (float y, const char* source, float amount, int lfo = 1)
{
    StateContent content;
    ContentLfoShape shape;
    shape.lfo = lfo;
    shape.points = { point (0.0f, y), point (1.0f, y) };
    shape.points[0].modY = { true, source, amount };
    shape.points[1].modY = { true, source, amount };
    content.lfoShapes.push_back (shape);
    return content;
}

sbtest::Rendered render (IEngine& e, const std::vector<Event>& events, int totalFrames)
{
    sbtest::Rendered out;
    out.left.assign (static_cast<size_t> (totalFrames), 0.0f);
    out.right.assign (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<Event> local;
    size_t idx = 0;
    for (int start = 0; start < totalFrames; start += kBlock)
    {
        const int n = std::min (kBlock, totalFrames - start);
        local.clear();
        while (idx < events.size() && events[idx].frame < start + n)
        {
            auto ev = events[idx++];
            ev.frame -= start;
            local.push_back (ev);
        }
        const auto before = g_allocations.load();
        e.process (local.data(), static_cast<int> (local.size()), out.left.data() + start, out.right.data() + start, n);
        g_allocationsInProcess += g_allocations.load() - before;
    }
    return out;
}

/** Amplitude of the spectral line of a sine near `hz` in left[from, from + n). */
double lineAt (const sbtest::Rendered& r, double hz, double from, size_t n = 4800)
{
    const size_t fft = 16384;
    const auto power = sbtest::powerSpectrum (r.left, static_cast<size_t> (from * kSr), n, fft);
    const double bin = kSr / static_cast<double> (fft);
    const size_t centre = static_cast<size_t> (hz / bin + 0.5);
    double best = 0.0;
    for (size_t k = centre - 4; k <= centre + 4; ++k)
        best = std::max (best, power[k]);
    return std::sqrt (best);
}

double hzOf (int key)
{
    return 440.0 * std::pow (2.0, (key - 69) / 12.0);
}

double rmsBetween (const sbtest::Rendered& r, double t0, double t1)
{
    return sbtest::rmsOf (r.left, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr));
}
} // namespace

TEST (per_voice_point_modulation_sets_the_capability)
{
    CHECK (capsCover (makeVital()->capabilities(), kCapCustomLfoShape));
}

TEST (velocity_moves_a_point_per_voice)
{
    // two notes sound together (the same voice pair, and in different pairs): the loud one is the high velocity, whatever the key
    const auto play = [] (float velocityLow, float velocityHigh)
    {
        auto e = makeVital();
        e->setContent (constantShape (0.0f, "velocity", 1.0f));
        const std::vector<Event> notes = { noteOn (0, 60, velocityLow), noteOn (0, 72, velocityHigh), noteOn (0, 48, velocityLow), noteOn (0, 84, velocityHigh) };
        return render (*e, notes, frames (0.6));
    };
    const auto a = play (0.2f, 1.0f);
    const double lowA = lineAt (a, hzOf (60), 0.3), highA = lineAt (a, hzOf (72), 0.3);
    std::printf ("    key 60 at velocity 0.2: %.2f, key 72 at velocity 1.0: %.2f\n", lowA, highA);
    CHECK (highA > 0.5);
    CHECK (highA > 3.0 * lowA);
    CHECK (lineAt (a, hzOf (84), 0.3) > 3.0 * lineAt (a, hzOf (48), 0.3));

    // the same notes with the velocities swapped: the loudness follows the velocity, not the key or the voice
    const auto b = play (1.0f, 0.2f);
    const double lowB = lineAt (b, hzOf (60), 0.3), highB = lineAt (b, hzOf (72), 0.3);
    std::printf ("    swapped: key 60 at velocity 1.0: %.2f, key 72 at velocity 0.2: %.2f\n", lowB, highB);
    CHECK (lowB > 3.0 * highB);
}

TEST (two_notes_in_turn_differ_by_velocity)
{
    auto e = makeVital();
    e->setContent (constantShape (0.0f, "velocity", 1.0f));
    const auto out = render (*e, { noteOn (0, 69, 0.25f), noteOff (frames (0.4), 69), noteOn (frames (0.5), 69, 1.0f) }, frames (0.9));
    const double first = rmsBetween (out, 0.2, 0.38), second = rmsBetween (out, 0.7, 0.88);
    std::printf ("    the same key at velocity 0.25 then 1.0: rms %.4f then %.4f\n", first, second);
    CHECK (first > 0.001);
    CHECK (second > 2.5 * first);
}

TEST (envelope_state_moves_a_point_per_voice)
{
    // env 2 rises for a second; the note that started 0.6 s earlier is further up its envelope than the new one
    const auto play = [] ()
    {
        auto e = makeVital();
        setByKey (*e, "env2.attack", 1000.0f);
        setByKey (*e, "env2.decay", 10.0f);
        setByKey (*e, "env2.sustain", 100.0f);
        setByKey (*e, "env1.sustain", 100.0f);
        StateContent content = constantShape (0.0f, "env2", 1.0f);
        e->setContent (content);
        return render (*e, { noteOn (0, 60, 1.0f), noteOn (frames (0.6), 72, 1.0f) }, frames (0.8));
    };
    const auto out = play();
    const double older = lineAt (out, hzOf (60), 0.7, 3600), newer = lineAt (out, hzOf (72), 0.7, 3600);
    std::printf ("    env 2 of the older note %.2f, of the newer note %.2f (same time)\n", older, newer);
    CHECK (older > 3.0 * newer);
    CHECK (newer > 0.01);
}

TEST (per_voice_x_and_y_modulation_and_global_sources_mix)
{
    // a triangle whose peak height follows velocity and whose peak position follows macro 1 (global)
    const auto play = [] (float macro, float velocity)
    {
        auto e = makeVital();
        setByKey (*e, "lfo1.rate", 4.0f);
        setByKey (*e, "macro.1", macro);
        StateContent content;
        ContentLfoShape shape;
        shape.lfo = 1;
        shape.points = { point (0.0f, 0.0f), point (0.5f, 0.2f), point (1.0f, 0.0f) };
        shape.points[1].modY = { true, "velocity", 0.8f };
        shape.points[1].modX = { true, "macro1", 0.4f };
        content.lfoShapes.push_back (shape);
        e->setContent (content);
        return render (*e, { noteOn (0, 69, velocity) }, frames (0.8));
    };
    const auto lowVelocity = play (0.0f, 0.1f), highVelocity = play (0.0f, 1.0f), moved = play (100.0f, 1.0f);
    // 4 Hz: the window is phase 0.40 .. 0.50 of the second cycle, just before the peak at x = 0.5 (macro 1 = 0) or far below the peak at x = 0.9
    const double lo = rmsBetween (lowVelocity, 0.35, 0.375), hi = rmsBetween (highVelocity, 0.35, 0.375), mo = rmsBetween (moved, 0.35, 0.375);
    std::printf ("    peak height by velocity: rms %.4f (0.1) -> %.4f (1.0); peak moved by macro 1 at velocity 1.0: %.4f\n", lo, hi, mo);
    CHECK (hi > 1.5 * lo);
    CHECK (mo < 0.8 * hi); // the global source still acts on top of the per-voice one
}

TEST (point_modulation_is_deterministic)
{
    const auto play = [] ()
    {
        auto e = makeVital();
        setByKey (*e, "env2.attack", 300.0f);
        e->setContent (constantShape (0.0f, "env2", 1.0f));
        return render (*e, { noteOn (0, 60, 0.4f), noteOn (frames (0.2), 64, 0.9f), noteOn (frames (0.3), 67, 0.6f) }, frames (0.7));
    };
    const auto a = play(), b = play();
    CHECK (a.left == b.left);
    CHECK (a.right == b.right);
    CHECK (sbtest::allFinite (a.left));
}

TEST (unmodulated_voice_tables_equal_the_shared_table)
{
    // a source at 0 moves no point: the per-voice table is the shared one, so the output equals a shape without point modulation
    const auto play = [] (bool withMod)
    {
        auto e = makeVital();
        StateContent content = constantShape (0.6f, "lift", 1.0f); // lift stays 0 (no note-off yet)
        if (! withMod)
        {
            content.lfoShapes[0].points[0].modY = {};
            content.lfoShapes[0].points[1].modY = {};
        }
        e->setContent (content);
        return render (*e, { noteOn (0, 69, 0.8f) }, frames (0.4));
    };
    const auto a = play (true), b = play (false);
    CHECK (a.left == b.left);
}

TEST (incremental_bake_equals_a_full_bake)
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    for (int round = 0; round < 40; ++round)
    {
        shapes::ShapeData shape;
        shape.numPoints = 2 + rng() % 40;
        for (size_t i = 0; i < shape.numPoints; ++i)
        {
            shape.points[i].x = static_cast<float> (i) / static_cast<float> (shape.numPoints - 1);
            shape.points[i].y = unit (rng);
            shape.points[i].curve = (unit (rng) - 0.5f) * 12.0f;
            shape.points[i].smooth = (rng() % 3) == 0;
            shape.points[i].step = (rng() % 7) == 0;
        }
        shape.sanitize();
        std::vector<PointMod> mods;
        for (size_t i = 0; i < shape.numPoints; ++i)
        {
            if ((rng() % 3) != 0)
                continue;
            if (i > 0 && i + 1 < shape.numPoints)
                mods.push_back ({ static_cast<int> (i), true, static_cast<int> (rng() % 38), unit (rng) * 2.0f - 1.0f });
            mods.push_back ({ static_cast<int> (i), false, static_cast<int> (rng() % 38), unit (rng) * 2.0f - 1.0f });
        }
        const CustomShapeModulation mod (shape, mods);
        for (const int resolution : { 2048, 512, 1000 })
        {
            float values[kPointSources];
            for (float& v : values)
                v = unit (rng) * 2.0f - 1.0f;
            std::vector<float> incremental (static_cast<size_t> (resolution)), full (static_cast<size_t> (resolution));
            mod.bake (values, resolution, incremental.data());
            shapes::ShapeData moved;
            mod.apply (values, moved);
            moved.bake (full.data(), static_cast<size_t> (resolution));
            CHECK (incremental == full);
        }
    }
}

TEST (point_modulation_does_not_allocate_and_stays_within_the_tiers)
{
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
    {
        auto e = makeVital (qualityForTier (tier));
        setByKey (*e, "env2.attack", 200.0f);
        setByKey (*e, "env2.release", 200.0f);
        StateContent content;
        ContentLfoShape shape;
        shape.lfo = 1;
        shape.points = { point (0.0f, 0.2f), point (0.3f, 0.8f), point (0.6f, 0.1f), point (1.0f, 0.5f) };
        shape.points[1].modY = { true, "env2", -0.5f };
        shape.points[2].modX = { true, "velocity", 0.3f };
        shape.points[2].modY = { true, "poly_aftertouch", 0.5f };
        shape.points[3].modY = { true, "lfo2", 0.2f }; // an LFO as a source
        content.lfoShapes.push_back (shape);
        e->setContent (content);
        std::vector<Event> events;
        for (int i = 0; i < 24; ++i)
        {
            events.push_back (noteOn (frames (0.01 * i), 36 + 2 * i, 0.3f + 0.03f * static_cast<float> (i)));
            events.push_back (noteOff (frames (0.4 + 0.01 * i), 36 + 2 * i));
        }
        g_allocationsInProcess = 0;
        const auto out = render (*e, events, frames (1.0));
        CHECK (sbtest::allFinite (out.left));
        std::printf ("    tier %d: allocations in process() %lu\n", static_cast<int> (tier), g_allocationsInProcess);
        CHECK (g_allocationsInProcess == 0);
    }
}

TEST (an_lfo_as_its_own_point_source_is_ignored_and_does_not_allocate)
{
    auto e = makeVital();
    e->setContent (constantShape (0.3f, "lfo1", 1.0f)); // would close a loop
    g_allocationsInProcess = 0;
    const auto out = render (*e, { noteOn (0, 60, 0.8f) }, frames (0.3));
    CHECK (sbtest::allFinite (out.left));
    CHECK (g_allocationsInProcess == 0);
    CHECK (rmsBetween (out, 0.1, 0.3) > 0.001); // the constant shape of height 0.3 sounds
}

TEST (content_swaps_while_notes_sound_are_safe)
{
    auto e = makeVital();
    setByKey (*e, "env2.attack", 100.0f);
    std::vector<Event> events = { noteOn (0, 60, 0.5f), noteOn (frames (0.1), 67, 0.9f) };
    sbtest::Rendered out;
    out.left.assign (static_cast<size_t> (frames (0.8)), 0.0f);
    out.right = out.left;
    for (int start = 0, step = 0; start < frames (0.8); start += kBlock, ++step)
    {
        if (step % 5 == 0)
            e->setContent (step % 10 == 0 ? constantShape (0.0f, "env2", 1.0f) : constantShape (0.2f, "velocity", 0.7f));
        std::vector<Event> local;
        for (const Event& ev : events)
            if (ev.frame >= start && ev.frame < start + kBlock)
            {
                Event copy = ev;
                copy.frame -= start;
                local.push_back (copy);
            }
        e->process (local.data(), static_cast<int> (local.size()), out.left.data() + start, out.right.data() + start, std::min (kBlock, frames (0.8) - start));
    }
    CHECK (sbtest::allFinite (out.left));
}
