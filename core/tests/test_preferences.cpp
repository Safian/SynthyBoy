// SynthyBoy - core tests: GROUPA2 WP-02: the preferences file, the MIDI CC map files, the transport stop watch and the locks of the global settings
// (Lock Matrix, quality, tuning). 13-new-keys.md 4.31.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "synthyboy/preferences.h"
#include "synthyboy/shell_features.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
int idx (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? findParamIndex (p->id) : -1;
}

std::string tempDir (const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / (std::string ("sb_prefs_") + name + "_" + std::to_string (static_cast<long> (std::rand())));
    std::filesystem::remove_all (dir);
    return dir.string();
}
}

TEST (preferences_defaults_and_table)
{
    const Preferences p;
    CHECK (p.valueBubbles && p.doubleClickResets && p.mouseWheel && p.keyboardShortcuts && p.silenceOnStop);
    CHECK (! p.waveform3d && ! p.freqSemitones && ! p.mpeByDefault && ! p.mpeBendToX && ! p.mpeYBipolar);
    CHECK (! p.limitModDepth && ! p.modWheelToWtPos && ! p.loadMidiMapFromPresets && ! p.ultraRender);
    CHECK_EQ (preferenceCount(), size_t { 14 });
    std::set<std::string> keys;
    for (size_t i = 0; i < preferenceCount(); ++i)
        CHECK (keys.insert (preferenceTable()[i].key).second && std::string (preferenceTable()[i].label).size() > 5);
    // every field of the struct is in the table: flipping each one makes a difference
    for (size_t i = 0; i < preferenceCount(); ++i)
    {
        Preferences q;
        q.*(preferenceTable()[i].field) = ! (q.*(preferenceTable()[i].field));
        CHECK (q != p);
        CHECK (! (q == p));
    }
}

TEST (preferences_file_round_trips_and_survives_damage)
{
    const std::string dir = tempDir ("file");
    CHECK (loadPreferences (dir) == Preferences {});   // no file: the defaults
    Preferences p;
    for (size_t i = 0; i < preferenceCount(); ++i)
        p.*(preferenceTable()[i].field) = (i % 2) == 0 ? ! (p.*(preferenceTable()[i].field)) : p.*(preferenceTable()[i].field);
    CHECK (p != Preferences {});
    CHECK (savePreferences (dir, p));
    CHECK (loadPreferences (dir) == p);
    CHECK (! std::filesystem::exists (preferencesPath (dir) + ".tmp"));
    // deterministic text
    CHECK_EQ (serializePreferences (p), serializePreferences (loadPreferences (dir)));

    // a member that is missing or has another type keeps its default; unknown members and nested values are skipped
    Preferences q;
    const std::string text = "{\"format\":\"synthyboy-preferences\",\"version\":3,\"future\":{\"a\":[1,2,{\"b\":\"c\"}]},\"mouseWheel\":false,\"valueBubbles\":\"no\",\"freqSemitones\":true}";
    CHECK (parsePreferences (text.data(), text.size(), q));
    CHECK (! q.mouseWheel && q.freqSemitones && q.valueBubbles && q.doubleClickResets);

    // refusals leave the output alone
    Preferences keep;
    keep.waveform3d = true;
    for (const char* bad : { "", "garbage", "{}", "{\"format\":\"other\",\"waveform3d\":false}", "{\"format\":\"synthyboy-preferences\",\"waveform3d\":", "[1,2]",
                             "{\"format\":\"synthyboy-preferences\",\"waveform3d\":false" })
    {
        Preferences r = keep;
        CHECK (! parsePreferences (bad, std::string (bad).size(), r));
        CHECK (r == keep);
    }
    // a damaged file on disk: the defaults, never a crash
    {
        std::ofstream out (preferencesPath (dir), std::ios::binary | std::ios::trunc);
        out << "{\"format\":\"synthyboy-preferences\",\"mouseWheel\":fal";
    }
    CHECK (loadPreferences (dir) == Preferences {});
    CHECK (! savePreferences ("", p));
    // every prefix of a good document is refused or parses to a subset of it, without a crash
    const std::string good = serializePreferences (p);
    for (size_t n = 0; n < good.size(); ++n)
    {
        Preferences r;
        (void) parsePreferences (good.data(), n, r);
    }
    std::filesystem::remove_all (dir);
}

TEST (midi_map_files_default_map_and_serum_ids)
{
    const std::string dir = tempDir ("map");
    std::vector<std::pair<int, std::string>> map { { 21, "filter1.cutoff" }, { 22, "macro.2" }, { 74, "osc1.level" } };
    const std::string text = serializeMidiMap (map);
    std::vector<std::pair<int, std::string>> back;
    CHECK_EQ (parseMidiMap (text.data(), text.size(), back), 3);
    CHECK (back == map);
    // entries with an unknown key or a reserved CC are skipped, the others are taken
    const std::string mixed = "{\"format\":\"synthyboy-midi-map\",\"version\":1,\"midi_cc\":[{\"cc\":21,\"key\":\"nope.nope\"},{\"cc\":64,\"key\":\"macro.1\"},{\"cc\":30,\"key\":\"macro.1\"},{\"cc\":200,\"key\":\"macro.2\"}]}";
    CHECK_EQ (parseMidiMap (mixed.data(), mixed.size(), back), 1);
    CHECK_EQ (back.size(), size_t { 1 });
    CHECK_EQ (back[0].first, 30);
    // not a map: the output stays
    back = map;
    CHECK_EQ (parseMidiMap ("{\"format\":\"synthyboy-preferences\"}", 34, back), -1);
    CHECK_EQ (parseMidiMap ("[", 1, back), -1);
    CHECK_EQ (parseMidiMap (nullptr, 0, back), -1);
    CHECK (back == map);
    // an empty map is a valid map
    const std::string empty = serializeMidiMap ({});
    CHECK_EQ (parseMidiMap (empty.data(), empty.size(), back), 0);
    CHECK (back.empty());

    // the default map file
    CHECK (! hasDefaultMidiMap (dir));
    std::vector<std::pair<int, std::string>> loaded;
    CHECK (! loadDefaultMidiMap (dir, loaded));
    CHECK (saveDefaultMidiMap (dir, map));
    CHECK (hasDefaultMidiMap (dir));
    CHECK (loadDefaultMidiMap (dir, loaded) && loaded == map);
    CHECK (clearDefaultMidiMap (dir));
    CHECK (! hasDefaultMidiMap (dir));
    CHECK (clearDefaultMidiMap (dir));   // nothing to remove is fine

    // the Serum 2 ids of "Macros CC14-21"
    for (int m = 0; m < 8; ++m)
        CHECK_EQ (serumMidiParamKey (7000000LL + m * 1000LL), "macro." + std::to_string (m + 1));
    CHECK_EQ (serumMidiParamKey (7008000LL), std::string());   // no macro 9
    CHECK_EQ (serumMidiParamKey (7000001LL), std::string());   // another parameter of the module
    CHECK_EQ (serumMidiParamKey (3001000LL), std::string());   // another module: not decoded
    CHECK_EQ (serumMidiParamKey (-5), std::string());
    std::filesystem::remove_all (dir);
}

TEST (transport_stop_watch_fires_once_per_stop)
{
    TransportStopWatch w;
    CHECK (! w.update (false, false));                 // a host without transport information never stops
    CHECK (! w.update (true, false));                  // stopped from the start: not a stop
    CHECK (! w.update (true, true));                   // starts
    CHECK (! w.update (true, true));
    CHECK (w.update (true, false));                    // stops: once
    CHECK (! w.update (true, false));
    CHECK (! w.update (true, false));
    CHECK (! w.update (true, true));
    CHECK (! w.update (false, true));                  // the host stops giving information while running: no stop is invented
    CHECK (! w.update (true, false));
    CHECK (! w.update (true, true));
    w.reset();
    CHECK (! w.update (true, false));                  // a reset forgets that it was running
}

TEST (locks_of_the_global_settings_matrix_quality_tuning)
{
    CHECK (isValidLockName (kMatrixLock) && isValidLockName (kQualityLock) && isValidLockName (kTuningLock));
    CHECK (lockCoversKey (kMatrixLock, "mod01.source") && lockCoversKey (kMatrixLock, "mod64.amount") && lockCoversKey (kMatrixLock, "mod09.aux_source")
           && lockCoversKey (kMatrixLock, "mod33.bypass"));
    CHECK (! lockCoversKey (kMatrixLock, "mod_wheel") && ! lockCoversKey (kMatrixLock, "master.volume") && ! lockCoversKey (kMatrixLock, "macro.1")
           && ! lockCoversKey (kMatrixLock, "mod1.source") && ! lockCoversKey (kMatrixLock, "mod"));
    CHECK (lockCoversKey (kQualityLock, "quality.oversample_4x") && lockCoversKey (kQualityLock, "quality.no_smoothing"));
    CHECK (lockCoversKey (kTuningLock, "master.tuning.a4") && lockCoversKey (kTuningLock, "master.tuning.scale") && ! lockCoversKey (kTuningLock, "master.tune"));

    // a preset change keeps the locked matrix: 64 slots and the curves, nothing else
    ParamValues current = ParamValues::defaults();
    current.plain[static_cast<size_t> (idx ("mod01.source"))] = 5.0f;
    current.plain[static_cast<size_t> (idx ("mod07.amount"))] = 42.0f;
    current.plain[static_cast<size_t> (idx ("mod64.aux_source"))] = 3.0f;
    current.plain[static_cast<size_t> (idx ("quality.oversample_4x"))] = 1.0f;
    current.plain[static_cast<size_t> (idx ("master.tuning.a4"))] = 432.0f;
    current.plain[static_cast<size_t> (idx ("osc1.level"))] = 11.0f;
    ParamValues patch = ParamValues::defaults();
    patch.plain[static_cast<size_t> (idx ("mod01.source"))] = 9.0f;
    patch.plain[static_cast<size_t> (idx ("osc1.level"))] = 88.0f;
    patch.plain[static_cast<size_t> (idx ("quality.oversample_4x"))] = 0.0f;
    patch.plain[static_cast<size_t> (idx ("master.tuning.a4"))] = 415.0f;

    ParamValues a = patch;
    CHECK (keepLockedValues (a, current, { kMatrixLock }) >= 3);
    CHECK_EQ (a.plain[static_cast<size_t> (idx ("mod01.source"))], 5.0f);
    CHECK_EQ (a.plain[static_cast<size_t> (idx ("mod07.amount"))], 42.0f);
    CHECK_EQ (a.plain[static_cast<size_t> (idx ("mod64.aux_source"))], 3.0f);
    CHECK_EQ (a.plain[static_cast<size_t> (idx ("osc1.level"))], 88.0f);                // not part of the matrix
    CHECK_EQ (a.plain[static_cast<size_t> (idx ("quality.oversample_4x"))], 0.0f);
    ParamValues b = patch;
    CHECK_EQ (keepLockedValues (b, current, { kQualityLock }), 1);
    CHECK_EQ (b.plain[static_cast<size_t> (idx ("quality.oversample_4x"))], 1.0f);
    CHECK_EQ (b.plain[static_cast<size_t> (idx ("master.tuning.a4"))], 415.0f);
    ParamValues c = patch;
    CHECK_EQ (keepLockedValues (c, current, { kTuningLock }), 1);
    CHECK_EQ (c.plain[static_cast<size_t> (idx ("master.tuning.a4"))], 432.0f);

    // and the content: the drawn per-connection curves (Lock Matrix) and the tuning table (tuning lock)
    StateContent now, incoming;
    ContentModCurve curve;
    curve.slot = 3;
    curve.points = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    now.modCurves.push_back (curve);
    ContentModCurve other;
    other.slot = 5;
    other.points = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
    incoming.modCurves.push_back (other);
    now.tuning.present = true;
    now.tuning.scl = "just";
    incoming.tuning.present = true;
    incoming.tuning.scl = "other";
    {
        StateContent kept = incoming;
        CHECK (keepLockedContent (kept, now, { kMatrixLock }));
        CHECK_EQ (kept.modCurves.size(), size_t { 1 });
        CHECK_EQ (kept.modCurves[0].slot, 3);
        CHECK_EQ (kept.tuning.scl, std::string ("other"));
    }
    {
        StateContent kept = incoming;
        CHECK (keepLockedContent (kept, now, { kTuningLock }));
        CHECK_EQ (kept.tuning.scl, std::string ("just"));
        CHECK_EQ (kept.modCurves.size(), size_t { 1 });
        CHECK_EQ (kept.modCurves[0].slot, 5);
    }
    {
        StateContent kept = incoming;
        CHECK (! keepLockedContent (kept, now, { kQualityLock }));   // quality has no content
        CHECK (kept == incoming);
    }
    // the lock names are saved with the session
    ShellState shell;
    CHECK (setLocked (shell.locks, kMatrixLock, true) && setLocked (shell.locks, kTuningLock, true) && setLocked (shell.locks, kQualityLock, true));
    const std::string doc = withShellSection ("{\"v\":1}", shell);
    ShellState back;
    CHECK (loadShellSection (doc.data(), doc.size(), back) == ShellStatus::ok);
    CHECK (isLocked (back.locks, kMatrixLock) && isLocked (back.locks, kTuningLock) && isLocked (back.locks, kQualityLock));
}
