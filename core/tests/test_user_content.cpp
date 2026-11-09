// SynthyBoy - tests of the user content library (WP-01): the content store, references with SHA-256, the Missing Files logic, root notes, WAV helpers,
// the sample preview, the JSON item library, the library item formats and the Vital import inside the plugin (synthetic fixtures only).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fixtures.h"
#include "synthyboy/formula.h"
#include "synthyboy/library_items.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/user_library.h"
#include "synthyboy/vital_plugin_import.h"
#include "test_runner.h"

using namespace synthyboy;
namespace fs = std::filesystem;

namespace
{
constexpr double kPi = 3.14159265358979323846;

/** A fresh temp directory per call. */
std::string tempDir (const char* name)
{
    const fs::path p = fs::temp_directory_path() / (std::string ("sb_test_user_content_") + name);
    std::error_code ec;
    fs::remove_all (p, ec);
    fs::create_directories (p, ec);
    return p.string();
}

std::vector<float> sine (double freq, double seconds, int sr = 48000)
{
    std::vector<float> v (static_cast<size_t> (seconds * sr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = 0.8f * static_cast<float> (std::sin (2.0 * kPi * freq * static_cast<double> (i) / sr));
    return v;
}

std::vector<uint8_t> wav16 (const std::vector<float>& v, int sr = 48000) { return encodeWav (v.data(), nullptr, v.size(), sr, 16); }

/** Clicks at the given positions (fractions) over silence: onsets for the Auto slices. */
std::vector<float> clicks (size_t n, std::initializer_list<double> at)
{
    std::vector<float> v (n, 0.0f);
    for (double a : at)
        for (size_t k = 0; k < 2400 && static_cast<size_t> (a * static_cast<double> (n)) + k < n; ++k)
            v[static_cast<size_t> (a * static_cast<double> (n)) + k] = 0.9f * static_cast<float> (std::sin (0.2 * static_cast<double> (k))) * (1.0f - static_cast<float> (k) / 2400.0f);
    return v;
}
} // namespace

TEST (content_store_writes_lists_reads_and_removes_with_sha_references)
{
    UserContentStore store (tempDir ("store"));
    const auto bytes = wav16 (sine (440.0, 0.1));
    ContentFileRef ref;
    CHECK (store.addFile (ContentKind::sample, "Pad C3.flac", bytes, ref));
    CHECK_EQ (ref.pack, std::string (kPackSamples));
    CHECK_EQ (ref.name, std::string ("Pad C3"));
    CHECK_EQ (ref.sha256.size(), static_cast<size_t> (64));
    CHECK (ref.file.size() > 12 && ref.file[12] == '/' && ref.file.find ("Pad C3.wav") != std::string::npos); // <sha12>/<stem>.wav keeps the note suffix

    std::vector<uint8_t> back;
    CHECK (store.readFile (ref, back));
    CHECK (back == bytes);

    // the same bytes are stored once
    ContentFileRef again;
    CHECK (store.addFile (ContentKind::sample, "Other name.wav", bytes, again));
    CHECK_EQ (again.file.substr (0, 12), ref.file.substr (0, 12));
    CHECK_EQ (store.list (ContentKind::sample).size(), static_cast<size_t> (2)); // two names, the same hash, two files

    // the SHA-256 of the reference is verified: a flipped byte on disk is refused
    const fs::path onDisk = fs::path (store.rootDirectory()) / "samples" / ref.file;
    {
        std::fstream f (onDisk, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp (60);
        f.put (static_cast<char> (0x55));
    }
    CHECK (! store.readFile (ref, back));
    ContentFileRef noHash = ref;
    noHash.sha256.clear();
    CHECK (store.readFile (noHash, back)); // without a hash the file is read as it is

    // references cannot leave the store
    ContentFileRef bad = ref;
    bad.file = "../../etc/passwd";
    CHECK (! store.readFile (bad, back));
    bad.pack = "..";
    bad.file = "x.wav";
    CHECK (! store.readFile (bad, back));

    // the listing carries the verified hash of each file; removal deletes the file and its hash directory
    ContentFileRef wt;
    CHECK (store.addFile (ContentKind::wavetable, "My Table", wav16 (sine (100.0, 0.05)), wt));
    CHECK_EQ (store.list (ContentKind::wavetable).size(), static_cast<size_t> (1));
    CHECK (store.list (ContentKind::wavetable)[0].ref.sha256 == wt.sha256);
    CHECK (store.remove (wt));
    CHECK (store.list (ContentKind::wavetable).empty());
    CHECK (store.remove (wt)); // removing again leaves it gone
    ContentFileRef installed;
    installed.pack = "some.installed.pack";
    installed.file = "a.wav";
    CHECK (! store.remove (installed)); // an installed pack is never deleted from here

    // an embedded file is stored in its own pack and is not part of the library list
    ContentFileRef embedded;
    CHECK (store.addFile (ContentKind::wavetable, "Embedded", wav16 (sine (200.0, 0.05)), embedded, true));
    CHECK_EQ (embedded.pack, std::string (kPackEmbedded));
    CHECK (store.list (ContentKind::wavetable).empty());
    CHECK (store.readFile (embedded, back));
}

TEST (state_keeps_the_references_and_the_missing_files_logic_resolves_them)
{
    UserContentStore store (tempDir ("missing"));
    ContentFileRef tableRef, sampleRef, irRef;
    const auto table = wav16 (std::vector<float> (2048 * 3, 0.25f));
    CHECK (store.addFile (ContentKind::wavetable, "Three", table, tableRef));
    CHECK (store.addFile (ContentKind::sample, "Hit F2.wav", wav16 (sine (220.0, 0.2)), sampleRef));
    CHECK (store.addFile (ContentKind::impulse, "Room", wav16 (sine (50.0, 0.2)), irRef));

    StateContent content;
    CHECK (attachStoredWavetable (content, 2, tableRef, store, "Me"));
    ParamValues values = ParamValues::defaults();
    CHECK (attachStoredSample (content, &values, 3, sampleRef, store));
    attachImpulse (content, irRef);
    CHECK_EQ (content.wavetables[0].frames, 3);
    CHECK_EQ (content.samples[0].frames, static_cast<int> (0.2 * 48000));
    const ParamInfo* root = findParamByKey ("osc3.sample.root");
    CHECK (root != nullptr);
    CHECK_EQ (values.plain[static_cast<size_t> (findParamIndex (root->id))], 53.0f); // F2 = 53 (C3 = 60)
    CHECK (validateContent (content));

    // save -> load keeps the references, nothing is missing
    const std::string text = saveState (values, content);
    ParamValues loaded = ParamValues::defaults();
    StateContent loadedContent;
    CHECK (loadStateWithContent (text.data(), text.size(), loaded, loadedContent).status == LoadStatus::ok);
    CHECK (loadedContent == content);
    CHECK (findMissingContent (loadedContent, store).empty());

    // the file of the sample disappears, the table changes on disk
    CHECK (store.remove (sampleRef));
    {
        std::fstream f (fs::path (store.rootDirectory()) / "wavetables" / tableRef.file, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp (80);
        f.put (static_cast<char> (0x33));
    }
    const auto missing = findMissingContent (loadedContent, store);
    CHECK_EQ (missing.size(), static_cast<size_t> (2));
    for (const auto& m : missing)
    {
        if (m.kind == ContentKind::sample)
        {
            CHECK_EQ (m.reason, MissingReason::notFound);
            CHECK_EQ (m.osc, 3);
        }
        if (m.kind == ContentKind::wavetable)
            CHECK_EQ (m.reason, MissingReason::hashMismatch);
    }

    // locating the wrong file is refused (the preset has a hash), the right one relinks, "accept" takes another
    const auto sampleItem = std::find_if (missing.begin(), missing.end(), [] (const MissingContent& m) { return m.kind == ContentKind::sample; });
    CHECK (sampleItem != missing.end());
    CHECK (! relinkMissing (loadedContent, *sampleItem, wav16 (sine (330.0, 0.2)), "Wrong.wav", store, false));
    CHECK (relinkMissing (loadedContent, *sampleItem, wav16 (sine (220.0, 0.2)), "Hit F2.wav", store, false));
    CHECK (relinkMissing (loadedContent, *std::find_if (missing.begin(), missing.end(), [] (const MissingContent& m) { return m.kind == ContentKind::wavetable; }),
                          wav16 (std::vector<float> (2048, 0.1f)), "Other.wav", store, true));
    CHECK (findMissingContent (loadedContent, store).empty());

    // "remove" drops the reference
    store.remove (loadedContent.impulses[0]);
    const auto gone = findMissingContent (loadedContent, store);
    CHECK_EQ (gone.size(), static_cast<size_t> (1));
    dropMissing (loadedContent, gone[0]);
    CHECK (loadedContent.impulses.empty());
    CHECK (findMissingContent (loadedContent, store).empty());
}

TEST (root_note_comes_from_the_file_name_or_the_wav_chunk)
{
    SampleFacts facts;
    const auto plain = wav16 (sine (220.0, 0.05));
    CHECK (probeSampleWav (plain, "Name F2.flac", facts));
    CHECK_EQ (facts.rootNote, 53);
    CHECK (facts.rootFromName);
    CHECK (probeSampleWav (plain, "Piano Eb3.wav", facts));
    CHECK_EQ (facts.rootNote, 63);
    CHECK (probeSampleWav (plain, "Kick_C#4.wav", facts));
    CHECK_EQ (facts.rootNote, 73);
    CHECK (probeSampleWav (plain, "no note here.wav", facts));
    CHECK_EQ (facts.rootNote, -1);

    // the 'smpl' chunk (unity note 57) and the 'inst' chunk (unshifted note 52); the file name wins when both exist
    const auto withChunk = [&] (const char* tag, const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> b = plain;
        b.insert (b.end(), tag, tag + 4);
        const uint32_t size = static_cast<uint32_t> (payload.size());
        for (int i = 0; i < 4; ++i)
            b.push_back (static_cast<uint8_t> (size >> (8 * i)));
        b.insert (b.end(), payload.begin(), payload.end());
        const uint32_t riff = static_cast<uint32_t> (b.size() - 8);
        for (int i = 0; i < 4; ++i)
            b[4 + static_cast<size_t> (i)] = static_cast<uint8_t> (riff >> (8 * i));
        return b;
    };
    std::vector<uint8_t> smpl (36, 0);
    smpl[12] = 57;
    CHECK (probeSampleWav (withChunk ("smpl", smpl), "loop.wav", facts));
    CHECK_EQ (facts.rootNote, 57);
    CHECK (! facts.rootFromName);
    CHECK (probeSampleWav (withChunk ("smpl", smpl), "loop A3.wav", facts));
    CHECK_EQ (facts.rootNote, 69);
    std::vector<uint8_t> inst (8, 0);
    inst[0] = 52;
    CHECK (probeSampleWav (withChunk ("inst", inst), "x.wav", facts));
    CHECK_EQ (facts.rootNote, 52);
}

TEST (wav_helpers_encode_trim_and_table_round_trip)
{
    const auto v = sine (100.0, 1.0);
    for (int bits : { 16, 24, 32 })
    {
        const auto wav = encodeWav (v.data(), nullptr, v.size(), 48000, bits);
        std::vector<float> l, r;
        sampleosc::WavInfo info;
        CHECK (sampleosc::readWav (wav.data(), wav.size(), l, r, info));
        CHECK_EQ (l.size(), v.size());
        CHECK_NEAR (l[1000], v[1000], bits == 16 ? 1.0e-3 : 1.0e-5);
    }
    CHECK (encodeWav (v.data(), nullptr, 0, 48000, 16).empty());
    CHECK (encodeWav (v.data(), nullptr, v.size(), 48000, 12).empty());

    // trim keeps rate and channels and the part between START and END
    const auto stereo = encodeWav (v.data(), v.data(), v.size(), 48000, 24);
    const auto trimmed = trimSampleWav (stereo, 0.25f, 0.5f);
    SampleFacts facts;
    CHECK (probeSampleWav (trimmed, "t.wav", facts));
    CHECK_EQ (facts.channels, 2);
    CHECK_EQ (facts.sampleRate, 48000);
    CHECK_NEAR (static_cast<double> (facts.frames), 12000.0, 2.0);
    CHECK (trimSampleWav (stereo, 0.5f, 0.5f).empty());

    // a table written as a 16-bit WAV reads back as the same frames; a single cycle of another length is resampled to 2048
    wtedit::Wavetable table ("Round");
    table.clear();
    for (size_t f = 0; f < 4; ++f)
    {
        wtedit::Frame frame;
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
            frame[i] = 0.5f * static_cast<float> (std::sin (2.0 * kPi * static_cast<double> ((f + 1) * i) / static_cast<double> (wtedit::kWaveformSize)));
        if (f == 0)
            table.setFrame (0, frame);
        else
            table.insertFrame (f, frame);
    }
    const auto bytes = wavetableToWav (table);
    CHECK (! bytes.empty());
    wtedit::Wavetable back;
    CHECK (wavetableFromWav (bytes, "Round", back));
    CHECK_EQ (back.numFrames(), static_cast<size_t> (4));
    CHECK_NEAR (back.getFrame (2)[100], table.getFrame (2)[100], 1.0e-3);
    wtedit::Wavetable cycle;
    CHECK (wavetableFromWav (wav16 (sine (48000.0 / 1000.0, 1000.0 / 48000.0)), "Cycle", cycle));
    CHECK_EQ (cycle.numFrames(), static_cast<size_t> (1));
}

TEST (sample_preview_slices_follow_the_engine_rule_and_the_markers_snap)
{
    const auto audio = clicks (96000, { 0.0, 0.25, 0.5, 0.75 });
    const auto wav = encodeWav (audio.data(), nullptr, audio.size(), 48000, 24);
    const auto preview = SamplePreview::create (wav, "clicks.wav");
    CHECK (preview != nullptr);
    if (! preview)
        return;
    CHECK_EQ (preview->frames(), static_cast<int64_t> (audio.size()));
    CHECK_NEAR (preview->seconds(), 2.0, 1.0e-6);

    // the Auto slices are the ones SampleData::detectSlices would play (first = 0, the onsets above the threshold)
    sampleosc::SampleData data;
    CHECK (data.loadFromFrames (audio.data(), nullptr, static_cast<int64_t> (audio.size()), 48000));
    // note: 24-bit quantisation of the file does not move the onsets
    const auto engineSlices = data.detectSlices (0.5f);
    const auto previewSlices = preview->autoSlices (0.5f);
    CHECK_EQ (previewSlices.size(), engineSlices.size());
    if (previewSlices.size() == engineSlices.size())
        for (size_t i = 0; i < previewSlices.size(); ++i)
            CHECK_NEAR (previewSlices[i], engineSlices[i], 0.002);
    CHECK (previewSlices.size() >= 3);
    CHECK (preview->autoSlices (0.0f).size() <= preview->autoSlices (1.0f).size()); // a lower threshold keeps fewer slices

    // the waveform columns: min <= max, and the click shows
    std::vector<float> lo, hi;
    preview->peaks (0.0f, 1.0f, 64, lo, hi);
    CHECK_EQ (lo.size(), static_cast<size_t> (64));
    bool ordered = true;
    for (size_t i = 0; i < lo.size(); ++i)
        ordered = ordered && lo[i] <= hi[i];
    CHECK (ordered);
    CHECK (hi[16] > 0.3f); // the click at 25 %

    // snap: zero crossing, beat grid, loop points
    const auto tone = sine (100.0, 1.0);
    const auto tonePreview = SamplePreview::create (encodeWav (tone.data(), nullptr, tone.size(), 48000, 24), "tone.wav");
    const float snapped = snapMarker (SnapMode::zero, 0.2503f, tonePreview.get(), 0, 0, 1);
    CHECK (std::abs (tonePreview->valueAt (snapped)) < 0.1f);
    CHECK (std::abs (snapped - 0.2503f) < 0.01f);
    CHECK_NEAR (snapMarker (SnapMode::beats, 0.33f, nullptr, 0.125f, 0, 1), 0.375, 1.0e-6);
    CHECK_NEAR (snapMarker (SnapMode::loop, 0.49f, nullptr, 0, 0.5f, 0.9f), 0.5, 1.0e-6);
    CHECK_NEAR (snapMarker (SnapMode::loop, 0.2f, nullptr, 0, 0.5f, 0.9f), 0.2, 1.0e-6); // too far from a loop point: untouched
    CHECK_NEAR (snapMarker (SnapMode::off, 0.2f, nullptr, 0.125f, 0.5f, 0.9f), 0.2, 1.0e-6);
}

TEST (user_library_round_trips_items_favourites_and_refusals)
{
    UserLibrary lib (tempDir ("library"));
    CHECK (lib.list (kLibLfoShapes).empty());
    CHECK (lib.save (kLibLfoShapes, "Wobble", "{\"a\":1}"));
    CHECK (lib.save (kLibLfoShapes, "alpha", "{\"a\":2}"));
    CHECK (! lib.save (kLibLfoShapes, "Wobble", "{\"a\":3}", false)); // no overwrite
    CHECK (lib.save (kLibLfoShapes, "Wobble", "{\"a\":3}", true));
    std::string json;
    CHECK (lib.load (kLibLfoShapes, "Wobble", json));
    CHECK_EQ (json, std::string ("{\"a\":3}"));
    CHECK (lib.exists (kLibLfoShapes, "alpha"));
    CHECK (! lib.exists (kLibFormulas, "alpha")); // categories are separate

    auto list = lib.list (kLibLfoShapes);
    CHECK_EQ (list.size(), static_cast<size_t> (2));
    CHECK_EQ (list[0].name, std::string ("alpha")); // by name, case insensitive
    CHECK (lib.setFavourite (kLibLfoShapes, "Wobble", true));
    CHECK (lib.isFavourite (kLibLfoShapes, "Wobble"));
    list = lib.list (kLibLfoShapes);
    CHECK_EQ (list[0].name, std::string ("Wobble")); // favourites first
    CHECK (list[0].favourite);
    CHECK (lib.setFavourite (kLibLfoShapes, "Wobble", false));
    CHECK (! lib.isFavourite (kLibLfoShapes, "Wobble"));

    // a name is made safe for a file, a path cannot be smuggled in
    CHECK (lib.save (kLibFxRacks, "../../evil/name", "{}"));
    CHECK_EQ (lib.list (kLibFxRacks).size(), static_cast<size_t> (1));
    CHECK (lib.list (kLibFxRacks)[0].name.find ('/') == std::string::npos);
    CHECK (! lib.save ("nonsense", "x", "{}"));
    CHECK (! lib.save (kLibFormulas, "", "{}"));
    CHECK (! lib.save (kLibFormulas, "empty", ""));
    CHECK (! lib.save (kLibFormulas, "huge", std::string (kMaxLibraryItemBytes + 1, 'x')));

    CHECK (lib.setFavourite (kLibLfoShapes, "alpha", true));
    CHECK (lib.remove (kLibLfoShapes, "alpha"));
    CHECK (! lib.exists (kLibLfoShapes, "alpha"));
    CHECK (! lib.isFavourite (kLibLfoShapes, "alpha")); // the favourite goes with the item
    CHECK (! lib.load (kLibLfoShapes, "alpha", json));
}

TEST (library_item_formats_formula_examples_fx_rack_and_lfo_shape)
{
    // every example of the formula menu compiles
    for (const auto& example : formulaExamples())
    {
        formula::Formula f;
        const auto err = formula::Formula::compile (example.expression, f);
        CHECK (err.ok);
        if (! err.ok)
            std::printf ("  example \"%s\": %s at %zu\n", example.name, err.message, err.position);
    }
    std::string name, expression;
    CHECK (parseFormulaItem (serializeFormulaItem ("My \"saw\"", "1 - 2*w"), name, expression));
    CHECK_EQ (name, std::string ("My \"saw\""));
    CHECK_EQ (expression, std::string ("1 - 2*w"));
    CHECK (! parseFormulaItem ("{\"format\":\"other\",\"expression\":\"x\"}", name, expression));
    CHECK (! parseFormulaItem ("garbage", name, expression));

    // the FX rack item holds only FX chain keys; applying it restores them and leaves the rest alone
    ParamValues a = ParamValues::defaults();
    const auto setKey = [] (ParamValues& v, const char* key, float x) { v.plain[static_cast<size_t> (findParamIndex (findParamByKey (key)->id))] = x; };
    const auto getKey = [] (const ParamValues& v, const char* key) { return v.plain[static_cast<size_t> (findParamIndex (findParamByKey (key)->id))]; };
    setKey (a, "reverb.on", 1.0f);
    setKey (a, "reverb.mix", 77.0f);
    setKey (a, "fx.order.1", 4.0f);
    setKey (a, "master.volume", -20.0f);
    const std::string item = serializeFxRackPreset ("Hall", a);
    CHECK (item.find ("\"reverb.on\"") != std::string::npos);
    CHECK (item.find ("master.volume") == std::string::npos);
    CHECK (isFxChainKey ("fxi03.p05") && isFxChainKey ("splitter_lh.on") && ! isFxChainKey ("osc1.level"));
    ParamValues b = ParamValues::defaults();
    setKey (b, "master.volume", -3.0f);
    CHECK (applyFxRackPreset (item, b) > 20);
    CHECK_EQ (getKey (b, "reverb.on"), 1.0f);
    CHECK_NEAR (getKey (b, "reverb.mix"), getKey (a, "reverb.mix"), 1.0e-4);
    CHECK_EQ (getKey (b, "fx.order.1"), 4.0f);
    CHECK_EQ (getKey (b, "master.volume"), -3.0f);
    CHECK_EQ (applyFxRackPreset ("{\"format\":\"nope\"}", b), 0);
    // a foreign key and an out of range value in an item: skipped / clamped
    ParamValues c = ParamValues::defaults();
    CHECK_EQ (applyFxRackPreset ("{\"format\":\"synthyboy-fx-rack\",\"version\":1,\"values\":{\"osc1.level\":5,\"reverb.mix\":1e9,\"nonsense.key\":1}}", c), 1);
    CHECK_EQ (getKey (c, "reverb.mix"), findParamByKey ("reverb.mix")->max);
    CHECK_EQ (getKey (c, "osc1.level"), getKey (ParamValues::defaults(), "osc1.level"));

    // the .lfoshape.json of tools/vital-import
    LfoShapeFile shape;
    shape.name = "Ramp";
    shape.smooth = true;
    shape.points = { ContentPoint (0.0f, 0.0f, 0.0f), ContentPoint (0.5f, 1.0f, -2.0f), ContentPoint (1.0f, 0.25f, 0.0f) };
    LfoShapeFile parsed;
    CHECK (parseLfoShapeFile (serializeLfoShapeFile (shape), parsed));
    CHECK_EQ (parsed.name, std::string ("Ramp"));
    CHECK (parsed.smooth);
    CHECK_EQ (parsed.points.size(), static_cast<size_t> (3));
    CHECK_NEAR (parsed.points[1].power, -2.0, 1.0e-6);
    CHECK_NEAR (parsed.points[2].y, 0.25, 1.0e-6);
    CHECK (! parseLfoShapeFile ("{\"format\":\"synthyboy-lfo-shape\",\"points\":[{\"x\":0.5,\"y\":0}]}", parsed)); // fewer than two points
    CHECK (! parseLfoShapeFile ("{\"format\":\"synthyboy-lfo-shape\",\"points\":[{\"x\":0.9,\"y\":0},{\"x\":0.1,\"y\":1}]}", parsed)); // x not ascending
    CHECK (! parseLfoShapeFile ("{\"format\":\"something\",\"points\":[]}", parsed));
}

namespace
{
std::vector<uint8_t> bytesOf (const std::string& s) { return std::vector<uint8_t> (s.begin(), s.end()); }

std::string presetWithTableAndShape()
{
    sbfx::Fixture f = sbfx::defaultPatch();
    f.name = "Fixture Pad";
    f.set ("filter_1_cutoff", 70.0);
    f.wavetables = "[" + sbfx::wavetable ("Fixture Table",
        { sbfx::component ("Wave Source", "\"interpolation\":0", { sbfx::waveKeyframe (0, sbfx::sineWave (1)), sbfx::waveKeyframe (255, sbfx::sineWave (5)) }) }) + "]";
    f.lfos = "[" + sbfx::lineObject ({ { 0.0, 1.0 }, { 0.5, 0.0 }, { 1.0, 1.0 } }, { 0.0, 0.0, 0.0 }, true) + "]";
    return f.json();
}
} // namespace

TEST (vital_import_inside_the_plugin_with_synthetic_fixtures)
{
    const std::string user = tempDir ("vital");
    UserContentStore store (user);

    // a .vital preset: the patch is returned, its table is in the library and the references resolve
    const std::string preset = presetWithTableAndShape();
    const auto imported = importVitalFileForPlugin ("Fixture Pad.vital", bytesOf (preset), store, user, kCapAllKnown);
    CHECK (imported.ok);
    CHECK (imported.kind == VitalFileKind::preset);
    CHECK_EQ (imported.name, std::string ("Fixture Pad"));
    CHECK (imported.report.numImported > 100);
    CHECK (! imported.report.toJson().empty());
    CHECK (! imported.content.wavetables.empty());
    CHECK (validateContent (imported.content));
    CHECK (findMissingContent (imported.content, store).empty());
    CHECK_EQ (imported.content.wavetables[0].ref.pack, std::string (kPackImportedVital));
    wtedit::Wavetable table;
    std::vector<uint8_t> tableBytes;
    CHECK (store.readFile (imported.content.wavetables[0].ref, tableBytes));
    CHECK (wavetableFromWav (tableBytes, "x", table));
    CHECK (table.numFrames() > 1);
    // the imported tables are listed in the library
    CHECK (! store.list (ContentKind::wavetable).empty());

    // the engine capabilities decide what is kept: without user tables the table is rendered / approximated, nothing dangling
    UserContentStore store2 (tempDir ("vital2"));
    const auto bare = importVitalFileForPlugin ("Fixture Pad.vital", bytesOf (preset), store2, tempDir ("vital2u"), kCapNone);
    CHECK (bare.ok);
    CHECK (bare.content.wavetables.empty());

    // a stand-alone table lands in the library
    const std::string tableOnly = sbfx::wavetable ("Alone", { sbfx::component ("Wave Source", "\"interpolation\":0", { sbfx::waveKeyframe (0, sbfx::sineWave (1)), sbfx::waveKeyframe (255, sbfx::sineWave (3)) }) });
    const auto t = importVitalFileForPlugin ("Alone.vitaltable", bytesOf (tableOnly), store, user, kCapAllKnown);
    CHECK (t.ok);
    CHECK (t.kind == VitalFileKind::wavetable);
    CHECK (t.tableFrames > 1);
    CHECK (store.contains (t.tableRef));

    // a stand-alone LFO shape comes back as points for a slot
    const std::string lfo = sbfx::lineObject ({ { 0.0, 1.0 }, { 0.5, 0.0 }, { 1.0, 1.0 } }, { 0.0, 0.0, 0.0 }, true);
    const auto l = importVitalFileForPlugin ("Wave.vitallfo", bytesOf (lfo), store, user, kCapAllKnown);
    CHECK (l.ok);
    CHECK (l.kind == VitalFileKind::lfo);
    CHECK_EQ (l.lfoShape.points.size(), static_cast<size_t> (3));

    // garbage and unknown files fail with a reason instead of changing anything
    CHECK (! importVitalFileForPlugin ("x.vital", bytesOf ("{\"nothing\":1}"), store, user, kCapAllKnown).ok);
    CHECK (! importVitalFileForPlugin ("x.txt", bytesOf ("hello"), store, user, kCapAllKnown).ok);
    CHECK (! importVitalFileForPlugin ("x.vitaltable", bytesOf ("{}"), store, user, kCapAllKnown).error.empty());
}

// GROUPA2 (WP-07): the voice control as an item of the user library
TEST (library_item_voice_control_holds_the_62_keys_and_applies_a_complete_configuration)
{
    const auto setKey = [] (ParamValues& v, const char* key, float x) { v.plain[static_cast<size_t> (findParamIndex (findParamByKey (key)->id))] = x; };
    const auto getKey = [] (const ParamValues& v, const char* key) { return v.plain[static_cast<size_t> (findParamIndex (findParamByKey (key)->id))]; };
    ParamValues a = ParamValues::defaults();
    setKey (a, "voicectl.length", 5.0f);
    setKey (a, "voicectl.s3.detune", -12.5f);
    setKey (a, "voicectl.s8.mod2", 100.0f);
    setKey (a, "voicectl.rand.detune10x", 1.0f);
    setKey (a, "voicectl.scale.lfo", 150.0f);
    setKey (a, "voicectl.osc.sub", 0.0f);
    setKey (a, "master.volume", -20.0f);
    const std::string item = serializeVoiceControlPreset ("Wide \"drift\"", a);
    int keys = 0;
    for (size_t pos = item.find ("\"voicectl."); pos != std::string::npos; pos = item.find ("\"voicectl.", pos + 1)) ++keys;
    CHECK_EQ (keys, 62);
    CHECK (item.find ("master.volume") == std::string::npos);
    CHECK (isVoiceControlKey ("voicectl.s1.pan") && ! isVoiceControlKey ("voice.phase_legato") && ! isVoiceControlKey (nullptr));

    // applying: the voice control is replaced as a whole (what the item does not mention is the default), everything else stays
    ParamValues b = ParamValues::defaults();
    setKey (b, "voicectl.s1.pan", 77.0f);
    setKey (b, "master.volume", -3.0f);
    CHECK_EQ (applyVoiceControlPreset (item, b), 62);
    CHECK_EQ (getKey (b, "voicectl.length"), 5.0f);
    CHECK_NEAR (getKey (b, "voicectl.s3.detune"), -12.5f, 1.0e-4f);
    CHECK_EQ (getKey (b, "voicectl.s8.mod2"), 100.0f);
    CHECK_EQ (getKey (b, "voicectl.rand.detune10x"), 1.0f);
    CHECK_NEAR (getKey (b, "voicectl.scale.lfo"), 150.0f, 1.0e-3f);
    CHECK_EQ (getKey (b, "voicectl.osc.sub"), 0.0f);
    CHECK_EQ (getKey (b, "voicectl.s1.pan"), 0.0f);
    CHECK_EQ (getKey (b, "master.volume"), -3.0f);
    CHECK_EQ (applyVoiceControlPreset ("{\"format\":\"synthyboy-fx-rack\"}", b), 0);
    CHECK_EQ (applyVoiceControlPreset ("garbage", b), 0);
    // a foreign key and an out of range value: skipped / clamped
    ParamValues c = ParamValues::defaults();
    CHECK_EQ (applyVoiceControlPreset ("{\"format\":\"synthyboy-voice-control\",\"version\":1,\"values\":{\"osc1.level\":5,\"voicectl.s1.pan\":1e9,\"voicectl.length\":0,\"nonsense\":1}}", c), 2);
    CHECK_EQ (getKey (c, "voicectl.s1.pan"), 100.0f);
    CHECK_EQ (getKey (c, "voicectl.length"), 1.0f);
    CHECK_EQ (getKey (c, "osc1.level"), ParamValues::defaults().plain[static_cast<size_t> (findParamIndex (findParamByKey ("osc1.level")->id))]);
}
