// SynthyBoy - core tests: shell features (docs/ui-design/15-shell-features.md): MIDI learn table (G07), locks (G08), the additive `shell`
// state section and the user default patch (G02).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "synthyboy/shell_features.h"
#include "test_runner.h"
#include "test_util.h"

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
    const auto dir = std::filesystem::temp_directory_path() / (std::string ("sb_shellfeat_") + name + "_" + std::to_string (static_cast<long> (std::rand())));
    std::filesystem::remove_all (dir);
    return dir.string();
}
} // namespace

TEST (shell_midi_map_assign_and_scale)
{
    MidiCcMap map;
    const int cutoff = idx ("filter1.cutoff");
    CHECK (cutoff >= 0);
    CHECK (map.assignKey (21, "filter1.cutoff"));
    CHECK_EQ (map.paramForCc (21), cutoff);
    CHECK_EQ (map.ccForParam (cutoff), 21);
    CHECK_EQ (map.numMappings(), 1);

    const ParamInfo& info = paramTable()[cutoff];
    auto lo = map.handleCc (21, 0);
    auto hi = map.handleCc (21, 127);
    CHECK (lo.apply && hi.apply);
    CHECK_EQ (lo.paramIndex, cutoff);
    CHECK_NEAR (lo.plain, info.min, 1.0e-4);
    CHECK_NEAR (hi.plain, info.max, 1.0e-3);
    const auto mid = map.handleCc (21, 64);
    CHECK_NEAR (mid.plain, constrainValue (info, fromNormalised (info, 64.0f / 127.0f)), 1.0e-4);
    CHECK (! map.handleCc (22, 10).apply); // unmapped
    CHECK (! map.handleCc (500, 10).apply);

    // a stepped parameter snaps
    const int model = idx ("filter1.model");
    CHECK (model >= 0);
    CHECK (map.assign (23, model));
    const auto step = map.handleCc (23, 127);
    CHECK (step.apply);
    CHECK_EQ (step.plain, constrainValue (paramTable()[model], step.plain));

    // one CC per parameter, one parameter per CC: the older mapping moves away
    CHECK (map.assign (30, cutoff));
    CHECK_EQ (map.paramForCc (21), -1);
    CHECK_EQ (map.ccForParam (cutoff), 30);
    CHECK (map.assign (30, idx ("master.volume")));
    CHECK_EQ (map.ccForParam (cutoff), -1);

    map.clearCc (30);
    CHECK_EQ (map.paramForCc (30), -1);
    map.clearParam (model);
    CHECK_EQ (map.numMappings(), 0);
}

TEST (shell_midi_map_learn)
{
    MidiCcMap map;
    const int cutoff = idx ("filter1.cutoff");
    const uint32_t rev0 = map.revision();
    map.armLearn (cutoff);
    CHECK_EQ (map.armedParam(), cutoff);
    CHECK (map.revision() != rev0);

    // reserved CCs are not learned and keep the learn mode armed
    for (const int cc : { 0, 32, 64, 120, 123, 127 })
    {
        const auto h = map.handleCc (cc, 100);
        CHECK (! h.learned && ! h.apply);
        CHECK_EQ (map.armedParam(), cutoff);
        CHECK (! MidiCcMap::isLearnableCc (cc));
    }
    const auto learned = map.handleCc (74, 90);
    CHECK (learned.learned);
    CHECK (! learned.apply); // the knob stays where it is
    CHECK_EQ (learned.paramIndex, cutoff);
    CHECK_EQ (map.armedParam(), -1);
    CHECK_EQ (map.paramForCc (74), cutoff);
    CHECK (map.handleCc (74, 90).apply);

    map.armLearn (-1);
    CHECK_EQ (map.armedParam(), -1);
    map.armLearn (1 << 20);
    CHECK_EQ (map.armedParam(), -1);
    CHECK (! map.assign (1000, cutoff));
    CHECK (! map.assign (21, 1 << 20));
}

TEST (shell_midi_map_bindings_round_trip)
{
    MidiCcMap map;
    map.assignKey (21, "filter1.cutoff");
    map.assignKey (22, "osc1.level");
    const auto list = map.bindings();
    CHECK_EQ (list.size(), static_cast<size_t> (2));
    CHECK_EQ (list[0].first, 21);
    CHECK (list[0].second == "filter1.cutoff");

    MidiCcMap other;
    other.assignKey (50, "master.volume");
    std::vector<std::pair<int, std::string>> in = list;
    in.emplace_back (60, "no.such.key");
    in.emplace_back (64, "osc2.level"); // sustain: not learnable
    CHECK_EQ (other.setBindings (in), 2);
    CHECK (other.bindings() == list);
}

TEST (shell_locks_values)
{
    auto patch = ParamValues::defaults();
    auto current = ParamValues::defaults();
    current.plain[idx ("osc1.level")] = 0.25f;
    current.plain[idx ("osc1.tune")] = 7.0f;
    current.plain[idx ("osc2.level")] = 0.5f;
    current.plain[idx ("master.volume")] = -20.0f;
    patch.plain[idx ("osc1.level")] = 1.0f;

    std::vector<std::string> locks;
    CHECK (setLocked (locks, "osc1", true));
    CHECK (setLocked (locks, "master.volume", true));
    CHECK (setLocked (locks, "osc1", true)); // no duplicate
    CHECK_EQ (locks.size(), static_cast<size_t> (2));
    CHECK (isLocked (locks, "osc1"));
    CHECK (! setLocked (locks, "Bad Name", true));
    CHECK (! setLocked (locks, "", true));

    const int restored = keepLockedValues (patch, current, locks);
    CHECK (restored >= 3);
    CHECK_EQ (patch.plain[idx ("osc1.level")], 0.25f);
    CHECK_EQ (patch.plain[idx ("osc1.tune")], 7.0f);
    CHECK_EQ (patch.plain[idx ("master.volume")], -20.0f);
    CHECK_EQ (patch.plain[idx ("osc2.level")], ParamValues::defaults().plain[idx ("osc2.level")]); // not locked: the preset's value
    // "osc1" must not cover "osc10" style prefixes of other modules
    CHECK (lockCoversKey ("osc1", "osc1.level"));
    CHECK (! lockCoversKey ("osc1", "osc10.level"));
    CHECK (! lockCoversKey ("osc1", "osc2.level"));
    CHECK (lockCoversKey ("master.volume", "master.volume"));

    CHECK (setLocked (locks, "osc1", false));
    CHECK (! isLocked (locks, "osc1"));
    auto patch2 = ParamValues::defaults();
    keepLockedValues (patch2, current, locks);
    CHECK_EQ (patch2.plain[idx ("osc1.level")], ParamValues::defaults().plain[idx ("osc1.level")]);

    for (int i = 0; i < 100; ++i)
        setLocked (locks, "fx" + std::to_string (i), true);
    CHECK (locks.size() <= kMaxLocks);
}

TEST (shell_locks_content)
{
    StateContent current, patch;
    current.lfoShapes.push_back ({ 2, "mine", false, -1, { { 0, 0 }, { 1, 1 } } });
    current.lfoShapes.push_back ({ 3, "other", false, -1, { { 0, 1 }, { 1, 0 } } });
    patch.lfoShapes.push_back ({ 2, "preset2", false, -1, { { 0, 0 }, { 1, 0 } } });
    patch.lfoShapes.push_back ({ 4, "preset4", false, -1, { { 0, 0 }, { 1, 1 } } });

    std::vector<std::string> locks;
    CHECK (! keepLockedContent (patch, current, locks));
    setLocked (locks, "lfo2", true);
    CHECK (keepLockedContent (patch, current, locks));
    CHECK_EQ (patch.lfoShapes.size(), static_cast<size_t> (2));
    CHECK_EQ (patch.lfoShapes[0].lfo, 2);
    CHECK (patch.lfoShapes[0].name == "mine");
    CHECK_EQ (patch.lfoShapes[1].lfo, 4);
    CHECK (validateContent (patch));

    // a locked module whose current patch has no custom shape clears the preset's one
    StateContent empty;
    StateContent patch2;
    patch2.lfoShapes.push_back ({ 2, "preset2", false, -1, { { 0, 0 }, { 1, 0 } } });
    CHECK (keepLockedContent (patch2, empty, locks));
    CHECK (patch2.lfoShapes.empty());
}

TEST (shell_section_round_trip)
{
    ShellState shell;
    shell.midiCc = { { 21, "filter1.cutoff" }, { 22, "osc1.level" } };
    setLocked (shell.locks, "osc2", true);
    setLocked (shell.locks, "filter1", true);

    auto values = ParamValues::defaults();
    values.plain[idx ("filter1.cutoff")] = 66.0f;
    const std::string doc = withShellSection (saveState (values), shell);

    // the document is still a valid state document, the shell section is invisible to the parameter loader
    auto loaded = ParamValues::defaults();
    const auto res = loadState (doc.data(), doc.size(), loaded);
    CHECK (res.status == LoadStatus::ok);
    CHECK_NEAR (loaded.plain[idx ("filter1.cutoff")], 66.0f, 1.0e-4);

    ShellState back;
    CHECK (loadShellSection (doc.data(), doc.size(), back) == ShellStatus::ok);
    CHECK (back == shell);

    // with a content section in front
    StateContent content;
    content.lfoShapes.push_back ({ 1, "a \"quoted\" shape", true, -1, { { 0, 0 }, { 1, 1 } } });
    const std::string doc2 = withShellSection (saveState (values, content), shell);
    ShellState back2;
    CHECK (loadShellSection (doc2.data(), doc2.size(), back2) == ShellStatus::ok);
    CHECK (back2 == shell);
    StateContent loadedContent;
    CHECK (loadStateWithContent (doc2.data(), doc2.size(), loaded, loadedContent).status == LoadStatus::ok);
    CHECK (loadedContent == content);

    // an empty section is written and read back as present-but-empty
    const std::string doc3 = withShellSection (saveState (values), ShellState {});
    ShellState pre;
    pre.locks = { "osc1" };
    CHECK (loadShellSection (doc3.data(), doc3.size(), pre) == ShellStatus::ok);
    CHECK (pre.empty());
}

TEST (shell_section_absent_and_bad)
{
    const std::string plain = saveState (ParamValues::defaults());
    ShellState s;
    s.locks = { "osc1" };
    CHECK (loadShellSection (plain.data(), plain.size(), s) == ShellStatus::absent);
    CHECK_EQ (s.locks.size(), static_cast<size_t> (1)); // untouched

    CHECK (loadShellSection (nullptr, 0, s) == ShellStatus::invalid);
    const std::string junk = "not json";
    CHECK (loadShellSection (junk.data(), junk.size(), s) == ShellStatus::invalid);

    // too many entries: dropped as a whole
    std::string big = "{\"format\":\"synthyboy-state\",\"version\":3,\"params\":{},\"shell\":{\"midi_cc\":[";
    for (int i = 0; i < 200; ++i)
        big += std::string (i ? "," : "") + "{\"cc\":21,\"key\":\"filter1.cutoff\"}";
    big += "],\"locks\":[]}}";
    CHECK (loadShellSection (big.data(), big.size(), s) == ShellStatus::rejected);
    CHECK (s.empty());

    // entries of unknown shape or name: skipped, the rest is taken
    const std::string mixed = "{\"shell\":{\"extra\":[1,{\"a\":\"}\"}],\"midi_cc\":[{\"cc\":300,\"key\":\"x\"},{\"key\":\"y\"},{\"cc\":5,\"key\":\"osc1.level\",\"more\":1}],"
                              "\"locks\":[\"osc1\",\"BAD NAME\"]}}";
    ShellState m;
    const auto st = loadShellSection (mixed.data(), mixed.size(), m);
    CHECK (st == ShellStatus::rejected); // cc 300 is outside 0..127: the section is not trusted
    const std::string mixed2 = "{\"shell\":{\"extra\":[1,{\"a\":\"}\"}],\"midi_cc\":[{\"key\":\"y\"},{\"cc\":5,\"key\":\"osc1.level\",\"more\":1}],"
                               "\"locks\":[\"osc1\",\"BAD NAME\"]}}";
    CHECK (loadShellSection (mixed2.data(), mixed2.size(), m) == ShellStatus::ok);
    CHECK_EQ (m.midiCc.size(), static_cast<size_t> (1));
    CHECK_EQ (m.locks.size(), static_cast<size_t> (1));

    // truncations of a real document never crash and never report ok for a cut section
    ShellState shell;
    shell.midiCc = { { 21, "filter1.cutoff" } };
    shell.locks = { "osc1" };
    const std::string doc = withShellSection (plain, shell);
    for (size_t n = 0; n < doc.size(); n += 7)
    {
        ShellState t;
        const auto r = loadShellSection (doc.data(), n, t);
        CHECK (r != ShellStatus::ok || n >= doc.size() - 1);
    }
}

TEST (shell_user_default_round_trip)
{
    const std::string dir = tempDir ("default");
    CHECK (! hasUserDefault (dir));
    ParamValues v = ParamValues::defaults();
    StateContent c;
    CHECK (! loadUserDefault (dir, v, c));

    auto custom = ParamValues::defaults();
    custom.plain[idx ("filter1.cutoff")] = 77.0f;
    StateContent content;
    content.lfoShapes.push_back ({ 1, "kept", false, -1, { { 0, 0 }, { 1, 1 } } });
    CHECK (saveUserDefault (dir, saveState (custom, content)));
    CHECK (hasUserDefault (dir));

    auto got = ParamValues::defaults();
    StateContent gotContent;
    CHECK (loadUserDefault (dir, got, gotContent));
    CHECK_NEAR (got.plain[idx ("filter1.cutoff")], 77.0f, 1.0e-4);
    CHECK (gotContent == content);

    // saving again replaces it
    custom.plain[idx ("filter1.cutoff")] = 55.0f;
    CHECK (saveUserDefault (dir, saveState (custom)));
    CHECK (loadUserDefault (dir, got, gotContent));
    CHECK_NEAR (got.plain[idx ("filter1.cutoff")], 55.0f, 1.0e-4);
    CHECK (gotContent.empty());

    // a damaged file does not load and leaves the outputs alone
    {
        std::ofstream out (userDefaultPath (dir), std::ios::binary | std::ios::trunc);
        out << "garbage";
    }
    auto untouched = ParamValues::defaults();
    untouched.plain[idx ("filter1.cutoff")] = 12.0f;
    StateContent untouchedContent;
    CHECK (! loadUserDefault (dir, untouched, untouchedContent));
    CHECK_NEAR (untouched.plain[idx ("filter1.cutoff")], 12.0f, 1.0e-6);

    CHECK (clearUserDefault (dir));
    CHECK (! hasUserDefault (dir));
    CHECK (! saveUserDefault ("", "{}"));
    CHECK (! saveUserDefault (dir, ""));
    std::filesystem::remove_all (dir);
}

TEST (shell_user_data_directory)
{
    const std::string dir = userDataDirectory();
    CHECK (! dir.empty());
    CHECK (dir.find ("SynthyBoy") != std::string::npos || std::getenv ("SYNTHYBOY_USER_DIR") != nullptr);
}
