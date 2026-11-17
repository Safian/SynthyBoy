// SynthyBoy - core tests: the grid of the LFO shape editor stored with the LFO (state version 5, format change F2 of the pre-release list): "grid_x" / "grid_y" in an entry of content.lfo_shapes.
// Round trip, the exact text, a document of version 4 (no grid: loads with the grid 0 = the editor's default), values that are no grid, a document of a LATER version with fields this reader does not know
// (what a version 4 reader does with "grid_x" / "grid_y": it skips them like any unknown key, the shape is read in full), the same through an .sbpreset.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <string>

#include "synthyboy/content_state.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
StateContent contentWithGrid (int gx, int gy)
{
    StateContent c;
    ContentLfoShape l;
    l.lfo = 3;
    l.name = "Drawn";
    l.smooth = true;
    l.loopPoint = 1;
    l.gridX = gx;
    l.gridY = gy;
    l.points = { ContentPoint (0.0f, 0.0f, 0.0f), ContentPoint (0.5f, 1.0f, 2.0f), ContentPoint (1.0f, 0.0f, 0.0f) };
    c.lfoShapes.push_back (l);
    return c;
}
std::string replaceAll (std::string s, const std::string& from, const std::string& to)
{
    for (size_t at = s.find (from); at != std::string::npos; at = s.find (from, at + to.size()))
        s.replace (at, from.size(), to);
    return s;
}
}

TEST (lfo_grid_version_is_five)
{
    CHECK_EQ (kStateVersion, 5);
}

TEST (lfo_grid_round_trip_and_exact_text)
{
    const ParamValues values = ParamValues::defaults();
    const std::string text = saveState (values, contentWithGrid (16, 4));
    // the stored form: the two keys sit between loop_point and points, nothing else of the entry changed
    CHECK (text.find ("\"version\":5") != std::string::npos);
    CHECK (text.find ("\"lfo_shapes\":[{\"lfo\":3,\"name\":\"Drawn\",\"smooth\":true,\"loop_point\":1,\"grid_x\":16,\"grid_y\":4,\"points\":[") != std::string::npos);
    ParamValues out = values;
    StateContent c;
    const LoadResult r = loadStateWithContent (text.data(), text.size(), out, c);
    CHECK (r.status == LoadStatus::ok);
    CHECK_EQ (c.lfoShapes.size(), static_cast<size_t> (1));
    CHECK_EQ (c.lfoShapes[0].gridX, 16);
    CHECK_EQ (c.lfoShapes[0].gridY, 4);
    CHECK (c == contentWithGrid (16, 4));
    // the horizontal and the vertical grid are two values
    CHECK (! (contentWithGrid (16, 4) == contentWithGrid (16, 8)));
    CHECK (! (contentWithGrid (16, 4) == contentWithGrid (8, 4)));
    // saving again gives the same text (the grid is not lost on a second pass)
    CHECK (saveState (out, c) == text);
}

TEST (lfo_grid_not_stored_when_zero)
{
    const std::string text = saveState (ParamValues::defaults(), contentWithGrid (0, 0));
    CHECK (text.find ("grid_x") == std::string::npos);
    CHECK (text.find ("grid_y") == std::string::npos);
    CHECK (text.find ("\"loop_point\":1,\"points\":[") != std::string::npos);
}

TEST (lfo_grid_old_document_loads_with_the_default_grid)
{
    // a version 4 document: what the plugin wrote before this change (no grid keys, "version":4)
    const std::string v5 = saveState (ParamValues::defaults(), contentWithGrid (16, 4));
    std::string v4 = replaceAll (v5, "\"version\":5", "\"version\":4");
    v4 = replaceAll (v4, ",\"grid_x\":16,\"grid_y\":4", "");
    CHECK (v4.find ("grid_") == std::string::npos);
    ParamValues out = ParamValues::defaults();
    StateContent c;
    const LoadResult r = loadStateWithContent (v4.data(), v4.size(), out, c);
    CHECK (r.status == LoadStatus::ok);
    CHECK_EQ (r.version, 4);
    CHECK_EQ (c.lfoShapes.size(), static_cast<size_t> (1));
    CHECK_EQ (c.lfoShapes[0].gridX, 0);   // 0 = not stored: the editor takes its default grid (the grid the points lie on, else 8)
    CHECK_EQ (c.lfoShapes[0].gridY, 0);
    CHECK_EQ (c.lfoShapes[0].points.size(), static_cast<size_t> (3));
    CHECK_EQ (c.lfoShapes[0].loopPoint, 1);
    CHECK (c.lfoShapes[0].smooth);
}

TEST (lfo_grid_values_that_are_no_grid)
{
    const std::string v5 = saveState (ParamValues::defaults(), contentWithGrid (16, 4));
    ParamValues out = ParamValues::defaults();
    StateContent c;
    std::string text = replaceAll (v5, "\"grid_x\":16", "\"grid_x\":99");   // too big: read as "not stored", the section stays valid
    text = replaceAll (text, "\"grid_y\":4", "\"grid_y\":1");
    LoadResult r = loadStateWithContent (text.data(), text.size(), out, c);
    CHECK (r.status == LoadStatus::ok);
    CHECK (r.content == ContentStatus::ok);
    CHECK_EQ (c.lfoShapes.size(), static_cast<size_t> (1));
    CHECK_EQ (c.lfoShapes[0].gridX, 0);
    CHECK_EQ (c.lfoShapes[0].gridY, 0);
    // the writer never writes one: a content that holds one is not valid and is left out of the state
    CHECK (! validateContent (contentWithGrid (1, 8)));
    CHECK (! validateContent (contentWithGrid (8, 33)));
    CHECK (validateContent (contentWithGrid (2, 32)));
    CHECK (saveState (ParamValues::defaults(), contentWithGrid (1, 8)).find ("lfo_shapes") == std::string::npos);
}

TEST (lfo_grid_later_version_with_unknown_fields_is_read_soft)
{
    // a document of version 6 that has fields this reader does not know, in the entry and around it: the status is newer_version, what is known is read (the shape, the grid), the rest is skipped. A
    // version 4 reader meets the same thing with "grid_x" / "grid_y" (unknown to it): it skips the two keys the way this one skips "grid_z".
    const std::string v5 = saveState (ParamValues::defaults(), contentWithGrid (16, 4));
    std::string text = replaceAll (v5, "\"version\":5", "\"version\":6");
    text = replaceAll (text, "\"grid_y\":4,", "\"grid_y\":4,\"grid_z\":7,\"future\":{\"a\":[1,2,3]},");
    ParamValues out = ParamValues::defaults();
    StateContent c;
    const LoadResult r = loadStateWithContent (text.data(), text.size(), out, c);
    CHECK (r.status == LoadStatus::newer_version);
    CHECK_EQ (c.lfoShapes.size(), static_cast<size_t> (1));
    CHECK_EQ (c.lfoShapes[0].gridX, 16);
    CHECK_EQ (c.lfoShapes[0].points.size(), static_cast<size_t> (3));
    // and without the grid keys at all, in the same later version
    std::string bare = replaceAll (text, ",\"grid_x\":16,\"grid_y\":4,\"grid_z\":7,\"future\":{\"a\":[1,2,3]}", "");
    StateContent c2;
    const LoadResult r2 = loadStateWithContent (bare.data(), bare.size(), out, c2);
    CHECK (r2.status == LoadStatus::newer_version);
    CHECK_EQ (c2.lfoShapes[0].gridX, 0);
    CHECK_EQ (c2.lfoShapes[0].points.size(), static_cast<size_t> (3));
}

TEST (lfo_grid_in_a_preset)
{
    PresetMeta meta;
    meta.name = "Grid test";
    const std::string text = savePreset (meta, ParamValues::defaults(), contentWithGrid (12, 6));
    PresetMeta metaOut;
    ParamValues values;
    StateContent c;
    const LoadResult r = loadPreset (text.data(), text.size(), metaOut, values, c);
    CHECK (r.status == LoadStatus::ok);
    CHECK_EQ (c.lfoShapes.size(), static_cast<size_t> (1));
    CHECK_EQ (c.lfoShapes[0].gridX, 12);
    CHECK_EQ (c.lfoShapes[0].gridY, 6);
    // a preset of the first release candidates (state version 4) has none
    std::string old = replaceAll (text, "\"version\":5", "\"version\":4");
    old = replaceAll (old, ",\"grid_x\":12,\"grid_y\":6", "");
    StateContent c2;
    const LoadResult r2 = loadPreset (old.data(), old.size(), metaOut, values, c2);
    CHECK (r2.status == LoadStatus::ok);
    CHECK_EQ (c2.lfoShapes[0].gridX, 0);
}
