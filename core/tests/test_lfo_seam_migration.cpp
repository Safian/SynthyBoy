// SynthyBoy - legacy drawn LFO seam migration and current-format opt-outs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/state.h"
#include "synthyboy/preset.h"
#include "synthyboy/shapes/shape.h"
#include "test_runner.h"
#include "../engine-own/env_lfo/lfo_eval.h"
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace synthyboy;

namespace
{
void set (ParamValues& v, const char* key, float x)
{
    v.plain[findParamIndex (findParamByKey (key)->id)] = x;
}
ContentLfoShape authored()
{
    ContentLfoShape s;
    s.lfo = 1;
    s.name = "Legacy";
    s.smooth = true;
    s.loopPoint = 1;
    s.gridX = 8;
    s.gridY = 16;
    s.points = {{0, .2f}, {.5f, .8f, 2.0f}, {1, .6f}};
    return s;
}
ContentLfoShape load (const ContentLfoShape& s, int mode, float phase, int version, int direction = 0, float swing = 0)
{
    auto v = ParamValues::defaults();
    set (v, "lfo1.shape", kLfoShapeCustom);
    set (v, "lfo1.sync_type", mode);
    set (v, "lfo1.phase", phase);
    set (v, "lfo1.direction", direction);
    set (v, "lfo1.swing", swing);
    StateContent c;
    c.lfoShapes.push_back (s);
    auto doc = saveState (v, c);
    auto tag = "\"version\":" + std::to_string (kStateVersion);
    doc.replace (doc.find (tag), tag.size(), "\"version\":" + std::to_string (version));
    const auto r = loadStateWithContent (doc.data(), doc.size(), v, c);
    CHECK (r.status == LoadStatus::ok && c.lfoShapes.size() == 1);
    return c.lfoShapes.at (0);
}
float yAt (const ContentLfoShape& s, float x)
{
    shapes::ShapeData d;
    d.numPoints = s.points.size();
    for (size_t i = 0; i < s.points.size(); ++i)
    {
        d.points[i].x = s.points[i].x;
        d.points[i].y = s.points[i].y;
        d.points[i].curve = s.points[i].power;
        d.points[i].smooth = s.smooth;
    }
    d.sanitize();
    return d.evaluate (x);
}
}

TEST (legacy_lfo_periodic_shape_closes_on_state_load)
{
    const std::string old = R"({"format":"synthyboy-state","version":5,"params":{"lfo1.shape":12},"content":{"lfo_shapes":[{"lfo":1,"points":[{"x":0,"y":0.25},{"x":0.5,"y":1},{"x":1,"y":0.75}]}]}})";
    auto values = ParamValues::defaults();
    StateContent content;
    const auto r = loadStateWithContent (old.data(), old.size(), values, content);
    CHECK (r.status == LoadStatus::ok && content.lfoShapes.size() == 1);
    if (!content.lfoShapes.empty())
        CHECK_EQ (content.lfoShapes.front().points.back().y, 0.25f);
}

TEST (legacy_lfo_actual_transport_intervals)
{
    const auto s = authored();
    for (int mode : {0, 1})
    {
        const auto r = load (s, mode, 90, 5);
        CHECK (r.linkCycleEnds && !r.linkLoopEnds);
        CHECK_EQ (yAt (r, 1), yAt (s, 0));
        CHECK_EQ (r.loopPoint, s.loopPoint);
        CHECK_EQ (r.points[1].power, s.points[1].power);
        CHECK_EQ (r.gridY, 16);
    }
    for (int mode : {2, 3})
    {
        auto r = load (s, mode, 90, 5);
        CHECK (!r.linkCycleEnds && !r.linkLoopEnds);
        CHECK_EQ (yAt (r, 0), .2f);
        CHECK_EQ (yAt (r, 1), .6f);
        CHECK_EQ (r.points.size(), s.points.size());
    }
    auto tail = load (s, 4, 270, 5);
    CHECK (tail.linkLoopEnds && !tail.linkCycleEnds);
    CHECK_EQ (yAt (tail, 1), yAt (s, .75f));
    CHECK_EQ (yAt (tail, .75f), yAt (s, .75f));
    CHECK_EQ (tail.points.front().y, .2f);
    auto held = load (s, 5, 90, 5);
    CHECK_EQ (yAt (held, .25f), yAt (s, 0));
    CHECK_EQ (held.loopPoint, 2); // inserted before the original editor loop marker
    CHECK_EQ (held.points.back().y, .6f); // release tail survives
    CHECK_EQ (load (s, 4, 360, 5).points.back().y, .6f);
    CHECK_EQ (load (s, 5, 0, 5).points.back().y, .6f);
    auto backwards = load (s, 4, 90, 5, 1);
    CHECK_EQ (yAt (backwards, 0), yAt (s, .75f));
    auto bounced = load (s, 0, 42, 5, 2);
    CHECK_EQ (bounced.points.back().y, .6f);
    auto swung = load (s, 4, 45, 5, 0, 100);
    CHECK (std::abs (yAt (swung, 1) - yAt (s, 1.0f / 12.0f)) < .00001f);
}

TEST (lfo_new_state_keeps_intentional_break_and_link_choices)
{
    auto s = authored();
    for (int flags = 0; flags < 4; ++flags)
    {
        s.linkCycleEnds = (flags & 1) != 0;
        s.linkLoopEnds = (flags & 2) != 0;
        auto r = load (s, 0, 0, kStateVersion);
        CHECK_EQ (r.linkCycleEnds, s.linkCycleEnds);
        CHECK_EQ (r.linkLoopEnds, s.linkLoopEnds);
        CHECK_EQ (yAt (r, 1), .6f); // current documents are never migrated
    }
    auto migrated = load (s, 4, 240, 4);
    auto again = load (migrated, 4, 240, kStateVersion);
    CHECK_EQ (again.points.size(), migrated.points.size());
    CHECK_EQ (yAt (again, 1), yAt (migrated, 1));
}

TEST (lfo_legacy_factory_presets_load_through_preset_api)
{
    const auto root = std::filesystem::path (__FILE__).parent_path().parent_path().parent_path() / "content/presets";
    for (const char* name : {"Pluck/pentatonic_step_pluck.sbpreset", "Bass/custom_shape_wobble.sbpreset", "Keys/tempo_gate_chords.sbpreset"})
    {
        std::ifstream in (root / name);
        CHECK (in.good());
        if (!in.good()) continue;
        std::string doc {std::istreambuf_iterator<char> (in), {}};
        auto v = ParamValues::defaults();
        PresetMeta meta;
        StateContent c;
        const auto r = loadPreset (doc.data(), doc.size(), meta, v, c);
        CHECK (r.status == LoadStatus::ok && c.lfoShapes.size() == 1);
        if (!c.lfoShapes.empty())
        {
            const auto& s = c.lfoShapes.front();
            CHECK (s.linkCycleEnds);
            CHECK_EQ (s.points.front().y, s.points.back().y);
            // The same fixture tagged as current provides the untouched starting
            // shape. The old Pluck needs exactly one ordinate changed; the other
            // two factory curves need none.
            const std::string oldVersion = "\"version\":4";
            doc.replace (doc.find (oldVersion), oldVersion.size(), "\"version\":" + std::to_string (kStateVersion));
            StateContent original;
            CHECK (loadPreset (doc.data(), doc.size(), meta, v, original).status == LoadStatus::ok);
            const auto& before = original.lfoShapes.at (0);
            CHECK_EQ (s.points.size(), before.points.size());
            int changed = 0;
            for (size_t i = 0; i < s.points.size(); ++i)
            {
                CHECK_EQ (s.points[i].x, before.points[i].x);
                CHECK_EQ (s.points[i].power, before.points[i].power);
                if (s.points[i].y != before.points[i].y)
                {
                    CHECK_EQ (i + 1, s.points.size());
                    ++changed;
                }
            }
            CHECK_EQ (changed, meta.name == "Pentatonic Step Pluck" ? 1 : 0);
        }
    }
}

TEST (legacy_lfo_preserves_capacity_metadata_and_vertical_steps)
{
    auto s = authored();
    s.points.clear();
    for (int i = 0; i < kMaxLfoShapePoints; ++i)
        s.points.emplace_back (float (i) / float (kMaxLfoShapePoints - 1), float (i % 7) / 7.f);
    s.loopPoint = 7;
    s.points[7].modY.present = true;
    s.points[7].modY.source = "velocity";
    s.points[7].modY.amount = .23f;
    auto end = load (s, 4, 359, 5);
    CHECK_EQ (end.points.size(), s.points.size());
    CHECK (std::abs (yAt (end, 1) - yAt (end, 359.f / 360.f)) < .00001f);
    CHECK_EQ (end.loopPoint, s.loopPoint);
    CHECK_EQ (end.points[7].modY.source, s.points[7].modY.source);
    auto middle = load (s, 5, 90, 5);
    CHECK_EQ (middle.points.size(), s.points.size());
    CHECK_EQ (yAt (middle, .25f), yAt (middle, 0));
    s = authored();
    s.points = {{0, .1f}, {0, .6f}, {.5f, .3f}, {1, .9f}, {1, .2f}};
    const auto stepped = load (s, 0, 0, 5);
    CHECK_EQ (stepped.points.front().y, .1f);
    CHECK_EQ (stepped.points[3].y, .6f);
    CHECK_EQ (stepped.points[4].y, .6f);
    s = authored();
    s.points.front().x = .1f;
    s.points.back().x = .95f;
    const auto inset = load (s, 0, 0, 5);
    CHECK_EQ (yAt (inset, 0), yAt (inset, 1));
}


TEST (legacy_loop_reaches_first_engine_wrap_without_editor)
{
    // The voice reads its compiled shape at LfoCore::position; use that same
    // transport and evaluator on either side of the first boundary.
    const auto source = authored();
    for (int mode : {0, 4, 5})
    {
        const auto shape = load (source, mode, 270, 5);
        own::CompiledShape compiled;
        own::ContentHub::compileLfoShape (shape, compiled);
        own::LfoCore core;
        const float knob = .75f;
        const float limit = mode == 5 ? knob : 1.f;
        // Retrigger adds the phase knob to the position; begin just before
        // the wrap of the *read position*, not its internal offset.
        core.offset = (mode == 0 ? 1.f - knob : limit) - .0001f;
        shapes::ShapeData scratch;
        const auto read = [&] {
            return own::lfoShapeAt (kLfoShapeCustom, core.position (mode, knob), mode <= 1,
                                    0, 0, &compiled, nullptr, scratch, 0);
        };
        const auto before = read();
        core.advance (mode, knob, 1, 0, .0002f, true);
        CHECK (std::abs (before - read()) < .001f);
    }
}
