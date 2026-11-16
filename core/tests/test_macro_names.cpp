// SynthyBoy - core tests: the names of the eight macros (content.macro_names of the state document and of the .sbpreset, docs/ui-design/macro-names.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <clocale>
#include <cstring>
#include <string>

#include "synthyboy/content_state.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
/** Writes a state document with the given names, loads it back with the content. */
StateContent roundTrip (const StateContent& in, LoadResult* result = nullptr)
{
    const std::string doc = saveState (ParamValues::defaults(), in);
    ParamValues v = ParamValues::defaults();
    StateContent out;
    const LoadResult r = loadStateWithContent (doc.data(), doc.size(), v, out);
    if (result != nullptr)
        *result = r;
    return out;
}

/** The document of the defaults with the text of a content section spliced in before the last brace. */
std::string withContentText (const std::string& content, int version = kStateVersion)
{
    std::string doc = saveState (ParamValues::defaults());
    const size_t at = doc.find ("\"version\":");
    doc.replace (at, doc.find (',', at) - at, "\"version\":" + std::to_string (version));
    doc.insert (doc.size() - 1, ",\"content\":" + content);
    return doc;
}

StateContent load (const std::string& doc, LoadResult* result = nullptr)
{
    ParamValues v = ParamValues::defaults();
    StateContent out;
    const LoadResult r = loadStateWithContent (doc.data(), doc.size(), v, out);
    if (result != nullptr)
        *result = r;
    return out;
}
} // namespace

TEST (default_names_and_the_cleaning_rules)
{
    CHECK_EQ (defaultMacroName (0), "Macro 1");
    CHECK_EQ (defaultMacroName (7), "Macro 8");
    CHECK_EQ (defaultMacroName (8), "");
    CHECK_EQ (sanitizeMacroName ("  Filter   Sweep  ", 0), "Filter Sweep");      // trimmed, runs of blanks are one space
    CHECK_EQ (sanitizeMacroName ("", 0), "");
    CHECK_EQ (sanitizeMacroName ("   ", 0), "");
    CHECK_EQ (sanitizeMacroName ("Macro 3", 2), "");                              // the default of its own macro is "no name"
    CHECK_EQ (sanitizeMacroName ("Macro 3", 3), "Macro 3");                       // but the default of another one is a name
    CHECK_EQ (sanitizeMacroName ("a\tb\nc\r\x01" "d\x7f" "e", 0), "a b c d e");   // control characters are blanks
    CHECK_EQ (sanitizeMacroName ("a\"b\\c", 0), "a\"b\\c");                       // quotes and backslashes are kept (the writer escapes them)
    CHECK_EQ (sanitizeMacroName ("Gr\xc3\xb6\xc3\x9f" "e \xe2\x82\xac", 0), "Gr\xc3\xb6\xc3\x9f" "e \xe2\x82\xac");   // non-ASCII text is kept
    CHECK_EQ (sanitizeMacroName ("a\xff" "b\xc3" "c", 0), "abc");                // invalid UTF-8 is dropped (a stray byte, a truncated sequence)
    CHECK_EQ (sanitizeMacroName ("a\xc0\x80" "b", 0), "ab");                      // overlong
    CHECK_EQ (sanitizeMacroName ("a\xed\xa0\x80" "b", 0), "ab");                  // a surrogate half
    CHECK_EQ (sanitizeMacroName ("a\xe2\x80\xae" "b", 0), "a b");                 // the right-to-left override is a blank
    CHECK_EQ (sanitizeMacroName ("0123456789ABCDEF", 0), "0123456789ABCDEF");     // 16 characters fit
    CHECK_EQ (sanitizeMacroName ("0123456789ABCDEFG", 0), "0123456789ABCDEF");    // the 17th goes
    CHECK_EQ (sanitizeMacroName ("0123456789ABCDE F", 0), "0123456789ABCDE");     // a cut that leaves a trailing space is trimmed
    CHECK_EQ (sanitizeMacroName ("\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4", 0).size(), 32u);   // 16 two-byte characters, never cut inside one
    const std::string once = sanitizeMacroName ("  x\ty \xe2\x82\xac 0123456789ABCDEF ", 5);
    CHECK_EQ (sanitizeMacroName (once, 5), once);                                 // idempotent
    CHECK_EQ (sanitizeMacroName (std::string (5000, 'x'), 0).size(), static_cast<size_t> (kMaxMacroNameChars));
}

TEST (no_names_means_the_document_is_the_one_it_always_was)
{
    const ParamValues v = ParamValues::defaults();
    StateContent c;
    CHECK_EQ (saveState (v, c), saveState (v));
    CHECK (saveState (v, c).find ("macro_names") == std::string::npos);
    c.macroNames[2] = "Cutoff";
    const std::string doc = saveState (v, c);
    CHECK (doc.find ("\"content\":{\"macro_names\":[\"\",\"\",\"Cutoff\",\"\",\"\",\"\",\"\",\"\"]}") != std::string::npos);
    CHECK (doc.find ("\"version\":" + std::to_string (kStateVersion) + ",") != std::string::npos);   // the state version is the one of before
    CHECK (doc.compare (0, saveState (v).size() - 1, saveState (v), 0, saveState (v).size() - 1) == 0);   // the parameters part is byte for byte the old document
}

TEST (names_round_trip_in_the_state_document)
{
    StateContent c;
    c.macroNames[0] = "Filter Sweep";
    c.macroNames[3] = "Drive + Warp";
    c.macroNames[7] = "Gr\xc3\xb6\xc3\x9f" "e \xe2\x82\xac \xf0\x9f\x8e\xb9";
    LoadResult r;
    const StateContent back = roundTrip (c, &r);
    CHECK (r.status == LoadStatus::ok && r.content == ContentStatus::ok);
    CHECK (back == c);
    CHECK_EQ (back.macroNames[0], "Filter Sweep");
    CHECK_EQ (macroDisplayName (back, 0), "Filter Sweep");
    CHECK_EQ (macroDisplayName (back, 1), "Macro 2");
    CHECK_EQ (macroDisplayName (back, 99), "");
    // the document is deterministic: writing the loaded content gives the same text
    CHECK_EQ (saveState (ParamValues::defaults(), back), saveState (ParamValues::defaults(), c));
    // the names travel with other content
    ContentLfoShape s;
    s.lfo = 1;
    s.name = "Saw";
    s.points = { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 1.0f) };
    c.lfoShapes.push_back (s);
    CHECK (roundTrip (c) == c);
}

TEST (names_that_need_escaping_survive_and_dirty_ones_are_cleaned_both_ways)
{
    StateContent c;
    c.macroNames[0] = "say \"hi\" \\o/";
    c.macroNames[1] = "tab\there";                        // not in clean form: the writer cleans it
    c.macroNames[2] = "Macro 3";                          // the default of its own macro: written as ""
    c.macroNames[3] = "0123456789ABCDEFGHIJ";             // too long: cut
    const std::string doc = saveState (ParamValues::defaults(), c);
    CHECK (doc.find ("\"say \\\"hi\\\" \\\\o/\"") != std::string::npos);
    CHECK (doc.find ('\t') == std::string::npos);
    const StateContent back = load (doc);
    CHECK_EQ (back.macroNames[0], "say \"hi\" \\o/");
    CHECK_EQ (back.macroNames[1], "tab here");
    CHECK_EQ (back.macroNames[2], "");
    CHECK_EQ (back.macroNames[3], "0123456789ABCDEF");
    // what a hand-written or damaged document may hold
    const std::string raw = std::string ("{\"macro_names\":[\"a\\u0001b\",\"\\u00e4\\u20ac\",\"x\\ud83c\\udfb9y\",\"\\n\\t \",\"") + std::string (300, 'z') + "\",7,null,{\"a\":1},\"nine\",\"ten\"]}";
    LoadResult r;
    const StateContent odd = load (withContentText (raw), &r);
    CHECK (r.status == LoadStatus::ok && r.content == ContentStatus::ok);
    CHECK_EQ (odd.macroNames[0], "a b");
    CHECK_EQ (odd.macroNames[1], "\xc3\xa4\xe2\x82\xac");
    CHECK_EQ (odd.macroNames[2].substr (0, 1), "x");   // the surrogate pair of a \u escape is two replacement characters, still text
    CHECK_EQ (odd.macroNames[3], "");
    CHECK_EQ (odd.macroNames[4], std::string (16, 'z'));
    CHECK_EQ (odd.macroNames[5], "");                  // a number, null and an object are the default name
    CHECK_EQ (odd.macroNames[6], "");
    CHECK_EQ (odd.macroNames[7], "");                  // the object; the entries after the eighth are ignored
    // a name never costs the rest of the content
    const StateContent mixed = load (withContentText ("{\"macro_names\":\"nope\",\"curves\":{\"velocity\":{\"points\":[{\"x\":0,\"y\":0,\"power\":0},{\"x\":1,\"y\":1,\"power\":0}]}}}"), &r);
    CHECK (r.content == ContentStatus::ok && mixed.velocityCurve.size() == 2 && mixed.noMacroNames());
    const StateContent few = load (withContentText ("{\"macro_names\":[\"One\"]}"));
    CHECK_EQ (few.macroNames[0], "One");
    CHECK_EQ (few.macroNames[1], "");
    CHECK (load (withContentText ("{\"macro_names\":[]}")).noMacroNames());
}

TEST (a_document_before_the_names_loads_with_the_default_names_and_a_load_replaces_the_names)
{
    // the document of the code before this change: no member
    const std::string old = saveState (ParamValues::defaults());
    ParamValues v = ParamValues::defaults();
    StateContent c;
    c.macroNames[1] = "Left over";
    CHECK (loadStateWithContent (old.data(), old.size(), v, c).status == LoadStatus::ok);
    CHECK (c.noMacroNames());   // a loaded document is a whole patch: the names of the previous one do not leak into it
    // a document with content but no names
    c.macroNames[1] = "Left over";
    const std::string noNames = withContentText ("{\"curves\":{\"velocity\":{\"points\":[{\"x\":0,\"y\":0,\"power\":0},{\"x\":1,\"y\":1,\"power\":0}]}}}");
    CHECK (loadStateWithContent (noNames.data(), noNames.size(), v, c).content == ContentStatus::ok);
    CHECK (c.noMacroNames());
    // a rejected content section drops the names with it (as it drops everything of the section), the parameters still apply
    c.macroNames[1] = "Left over";
    const std::string bad = withContentText ("{\"macro_names\":[\"Keep\"],\"lfo_shapes\":\"nope\"}");
    CHECK (loadStateWithContent (bad.data(), bad.size(), v, c).content == ContentStatus::rejected);
    CHECK (c.noMacroNames());
    // version 1 and 2 documents have no content section: a stray member is not taken
    const std::string v2 = withContentText ("{\"macro_names\":[\"Stray\"]}", 2);
    CHECK (loadStateWithContent (v2.data(), v2.size(), v, c).content == ContentStatus::absent);
    CHECK (c.noMacroNames());
    // loadState () (no content wanted) skips the section and says so
    const std::string named = withContentText ("{\"macro_names\":[\"Cutoff\"]}");
    CHECK (loadState (named.data(), named.size(), v).content == ContentStatus::ignored);
    // a document of a newer version with the member still takes it
    const std::string newer = withContentText ("{\"macro_names\":[\"Cutoff\"]}", kStateVersion + 1);
    LoadResult r;
    const StateContent n = load (newer, &r);
    CHECK (r.status == LoadStatus::newer_version && n.macroNames[0] == "Cutoff");
}

TEST (a_reader_that_does_not_know_the_member_skips_it_and_keeps_the_rest)
{
    // the member behaves like any content member of a later version: a reader of the current code has no "future_names" and must not mind it
    const std::string doc = withContentText ("{\"future_names\":[\"a\",{\"b\":[1,2,3]}],\"curves\":{\"velocity\":{\"points\":[{\"x\":0,\"y\":0,\"power\":0},{\"x\":1,\"y\":1,\"power\":0}]}},\"macro_names\":[\"Cutoff\"]}");
    LoadResult r;
    const StateContent c = load (doc, &r);
    CHECK (r.status == LoadStatus::ok && r.content == ContentStatus::ok);
    CHECK (c.velocityCurve.size() == 2 && c.macroNames[0] == "Cutoff");
    // an unknown member at the top of the document is skipped as well
    std::string top = saveState (ParamValues::defaults());
    top.insert (top.size() - 1, ",\"macro_names\":[\"A\"],\"future\":{\"x\":[1,2]}");
    ParamValues v = ParamValues::defaults();
    CHECK (loadState (top.data(), top.size(), v).status == LoadStatus::ok);
}

TEST (names_round_trip_in_the_preset)
{
    PresetMeta meta = PresetMeta::initDefault();
    meta.name = "Named";
    ParamValues v = ParamValues::defaults();
    StateContent c;
    c.macroNames[4] = "Reso";
    c.macroNames[5] = "Gr\xc3\xb6\xc3\x9f" "e";
    const std::string text = savePreset (meta, v, c);
    CHECK (text.find ("\"macro_names\":[\"\",\"\",\"\",\"\",\"Reso\",\"Gr\xc3\xb6\xc3\x9f" "e\",\"\",\"\"]") != std::string::npos);
    PresetMeta metaOut;
    ParamValues vOut = ParamValues::defaults();
    StateContent cOut;
    const LoadResult r = loadPreset (text.data(), text.size(), metaOut, vOut, cOut);
    CHECK (r.status == LoadStatus::ok && cOut == c && metaOut.name == "Named");
    // a preset without names: default names, and the previous names are replaced
    cOut.macroNames[0] = "Left over";
    const std::string plain = savePreset (meta, v);
    CHECK (plain.find ("macro_names") == std::string::npos);
    CHECK (loadPreset (plain.data(), plain.size(), metaOut, vOut, cOut).status == LoadStatus::ok);
    CHECK (cOut.noMacroNames());
    // the readers that do not take the content still load the sound
    CHECK (loadPreset (text.data(), text.size(), metaOut, vOut).status == LoadStatus::ok);
}

TEST (the_text_is_locale_independent)
{
    const char* locales[] = { "de_DE.UTF-8", "de_DE", "hu_HU.UTF-8", "fr_FR.UTF-8" };
    StateContent c;
    c.macroNames[0] = "Fl\xc3\xbc" "gel 0,5";
    const std::string reference = saveState (ParamValues::defaults(), c);
    const std::string old = std::setlocale (LC_ALL, nullptr);
    for (const char* loc : locales)
    {
        if (std::setlocale (LC_ALL, loc) == nullptr)
            continue;
        CHECK_EQ (saveState (ParamValues::defaults(), c), reference);
        CHECK (load (reference) == c);
    }
    std::setlocale (LC_ALL, old.c_str());
}

TEST (the_state_chunk_with_names_stays_inside_the_limits)
{
    StateContent c;
    for (int i = 0; i < kNumMacros; ++i)
        c.macroNames[i] = std::string (4 * kMaxMacroNameChars, 'w');
    const std::string doc = saveState (ParamValues::defaults(), c);
    CHECK (doc.size() < kMaxStateBytes);
    const StateContent back = load (doc);
    for (int i = 0; i < kNumMacros; ++i)
        CHECK_EQ (back.macroNames[i].size(), static_cast<size_t> (kMaxMacroNameChars));
}
