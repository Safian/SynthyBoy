// SynthyBoy - shell-mpc acceptance: shell-mpc/program_list.h (program list from a presets directory, program number in the chunk).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: programs_test <repo>/content/presets [<factory-preset-categories.json>]. Links the core. The categories file defaults to <presets>/../../scripts/factory-preset-categories.json.
#include "../program_list.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "startup_timing.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <vector>

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}
static std::string lower (std::string s) { for (char& c : s) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c))); return s; }

/** groups list and {preset name: group} of scripts/factory-preset-categories.json (the file is written by a script with a fixed layout: "groups": [ ... ] and "<name>": { "group": "<g>", ... }). */
static bool loadCategories (const std::string& path, std::vector<std::string>& groups, std::map<std::string, std::string>& group)
{
    std::ifstream in (path);
    if (! in) return false;
    std::string line, name;
    bool inGroups = false;
    auto quoted = [] (const std::string& l, size_t from, std::string& out) -> size_t
    {
        const size_t a = l.find ('"', from);
        if (a == std::string::npos) return std::string::npos;
        const size_t b = l.find ('"', a + 1);
        if (b == std::string::npos) return std::string::npos;
        out = l.substr (a + 1, b - a - 1);
        return b + 1;
    };
    while (std::getline (in, line))
    {
        if (line.find ("\"groups\": [") != std::string::npos) { inGroups = true; continue; }
        if (inGroups)
        {
            if (line.find (']') != std::string::npos) { inGroups = false; continue; }
            std::string g;
            if (quoted (line, 0, g) != std::string::npos) groups.push_back (g);
            continue;
        }
        const size_t k = line.find ("\"group\": \"");
        if (k != std::string::npos && ! name.empty())
        {
            std::string g;
            quoted (line, k + 8, g);
            group[name] = g;
            continue;
        }
        if (line.size() > 4 && line.compare (0, 4, "    ") == 0 && line[4] == '"' && line.find ("\": {") != std::string::npos)
            quoted (line, 4, name);
    }
    return ! groups.empty() && ! group.empty();
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: programs_test <presets dir>\n"); return 2; }
    using synthyboy_mpc::ProgramEntry;

    const std::vector<ProgramEntry> list = synthyboy_mpc::scanPrograms (argv[1]);
    const auto raw = synthyboy::scanPresetDirectory (argv[1]);
    check (! raw.empty() && list.size() == raw.size(), "scanPrograms returns one entry per preset file");
    bool filled = true, sorted = true;
    for (size_t i = 0; i < list.size(); ++i)
    {
        filled = filled && ! list[i].name.empty() && ! list[i].category.empty() && ! list[i].path.empty();
        if (i > 0)
        {
            const ProgramEntry& a = list[i - 1]; const ProgramEntry& b = list[i];
            const bool le = a.category < b.category
                            || (a.category == b.category && (lower (a.name) < lower (b.name) || (lower (a.name) == lower (b.name) && a.path <= b.path)));
            sorted = sorted && le;
        }
    }
    check (filled, "every entry has name, category and path");
    check (sorted, "entries are ordered by category, then name (case-insensitive), then path");
    bool same = true;
    for (const auto& r : raw)
        same = same && std::any_of (list.begin(), list.end(), [&] (const ProgramEntry& p) { return p.path == r.path && p.name == r.meta.name && p.category == r.meta.category; });
    check (same, "name / category / path are those of scanPresetDirectory");
    check (synthyboy_mpc::scanPrograms (std::string (argv[1]) + "/no-such-directory").empty(), "a missing directory gives an empty list");
    check (synthyboy_mpc::scanPrograms ("").empty(), "an empty path gives an empty list");
    check (synthyboy_mpc::scanPrograms (std::string (argv[0])).empty(), "a path that is a file gives an empty list");
    static_assert (noexcept (synthyboy_mpc::scanPrograms (std::string())), "scanPrograms must be noexcept");

    const std::string state = synthyboy::saveState (synthyboy::ParamValues::defaults());
    const std::string c = synthyboy_mpc::chunkWithProgram (state, 37, "Acid \"Saw\" \\ Reso");
    check (c == "{\"mpcProgram\":37,\"mpcProgramName\":\"Acid \\\"Saw\\\" \\\\ Reso\"," + state.substr (1),
           "chunkWithProgram puts {\"mpcProgram\":N,\"mpcProgramName\":\"...\", in front of the state document's members (quote and backslash escaped)");
    std::string nm;
    check (synthyboy_mpc::programNameFromChunk (c.data(), c.size(), nm) && nm == "Acid \"Saw\" \\ Reso", "programNameFromChunk reads the name back (unescaped)");
    const std::string ctl = synthyboy_mpc::chunkWithProgram (state, 1, std::string ("a\tb\x01") + "\xc3\xa9");
    check (ctl.find ("\"mpcProgramName\":\"a\\u0009b\\u0001\xc3\xa9\",") != std::string::npos, "control characters are written as \\u00XX, UTF-8 bytes stay as they are");
    check (synthyboy_mpc::programNameFromChunk (ctl.data(), ctl.size(), nm) && nm == std::string ("a\tb\x01") + "\xc3\xa9", "and they are read back");
    const std::string old = "{\"mpcProgram\":5," + state.substr (1);
    nm = "untouched";
    check (! synthyboy_mpc::programNameFromChunk (old.data(), old.size(), nm), "a chunk with only the number has no name (false)");
    check (synthyboy_mpc::programFromChunk (old.data(), old.size()) == 5, "and its number is still read");
    check (! synthyboy_mpc::programNameFromChunk (state.data(), state.size(), nm) && ! synthyboy_mpc::programNameFromChunk (nullptr, 0, nm), "plain state / null chunk: no name");
    const std::string cut = "{\"mpcProgram\":5,\"mpcProgramName\":\"never ends";
    check (! synthyboy_mpc::programNameFromChunk (cut.data(), cut.size(), nm), "an unterminated name gives false (no reading beyond the size)");
    const std::string longName = "{\"mpcProgram\":5,\"mpcProgramName\":\"" + std::string (300, 'x') + "\",\"format\":1}";
    check (! synthyboy_mpc::programNameFromChunk (longName.data(), longName.size(), nm), "a name longer than 256 bytes gives false");

    // index resolution: the name decides, the stored index only breaks ties
    {
        using synthyboy_mpc::resolveProgram;
        std::vector<ProgramEntry> l = { { "Alpha", "A", "a", "" }, { "Beta", "A", "b", "" }, { "Twin", "B", "c", "" }, { "Twin", "C", "d", "" } };
        check (resolveProgram (l, 2, true, "Beta") == 2, "name at the stored index: that index");
        check (resolveProgram (l, 1, true, "Beta") == 2, "name elsewhere in the list: its index");
        check (resolveProgram (l, 9, true, "Alpha") == 1, "stored index beyond the list, name known: its index");
        check (resolveProgram (l, 4, true, "Twin") == 4 && resolveProgram (l, 1, true, "Twin") == 3, "two programs of one name: the stored index if it has that name, else the first");
        check (resolveProgram (l, 0, true, "Init") == 0, "index 0 named Init is program 0");
        check (resolveProgram (l, 2, true, "Gone") == -1 && resolveProgram (l, 0, true, "Gone") == -1, "an unknown name is no program (-1)");
        check (resolveProgram (l, 3, false, "") == 3 && resolveProgram (l, 0, false, "") == 0, "no name (old chunk): the stored index if it is in the list");
        check (resolveProgram (l, 5, false, "") == -1 && resolveProgram (l, -1, false, "") == -1, "no name and an index outside the list: no program (-1)");
    }
    check (synthyboy_mpc::programFromChunk (c.data(), c.size()) == 37, "programFromChunk reads it back");
    check (synthyboy_mpc::programFromChunk (state.data(), state.size()) == -1, "a plain state document has no program number (-1)");
    check (synthyboy_mpc::programFromChunk (nullptr, 0) == -1, "null / empty chunk gives -1");
    const std::string junk = "{\"mpcProgram\":";
    check (synthyboy_mpc::programFromChunk (junk.data(), junk.size()) == -1, "a truncated prefix gives -1");
    const std::string neg = "{\"mpcProgram\":-4,\"format\":\"x\"}";
    check (synthyboy_mpc::programFromChunk (neg.data(), neg.size()) == -1, "a negative number gives -1");
    const std::string big = "{\"mpcProgram\":99999999999999999999,\"format\":\"x\"}";
    check (synthyboy_mpc::programFromChunk (big.data(), big.size()) == -1, "an absurdly long number gives -1 (no overflow)");
    synthyboy::ParamValues v = synthyboy::ParamValues::defaults(); synthyboy::StateContent content;
    const synthyboy::LoadResult r = synthyboy::loadStateWithContent (c.data(), c.size(), v, content);
    check (r.status == synthyboy::LoadStatus::ok && synthyboy::saveState (v) == state, "the core still loads a chunk that carries the program number");

    // ---- the program list in its modes (docs/MPC.md "Preset list of a plugin track") ----
    {
        using namespace synthyboy_mpc;
        std::vector<std::string> jsonGroups;
        std::map<std::string, std::string> jsonGroupOf;
        const std::string catPath = argc > 2 ? argv[2] : std::string (argv[1]) + "/../../scripts/factory-preset-categories.json";
        check (loadCategories (catPath, jsonGroups, jsonGroupOf), "factory-preset-categories.json is read (groups and per-preset groups)");
        bool same15 = jsonGroups.size() == static_cast<size_t> (kFactoryGroupCount);
        for (size_t i = 0; same15 && i < jsonGroups.size(); ++i) same15 = jsonGroups[i] == kFactoryGroups[i];
        check (same15, "kFactoryGroups is the group list of factory-preset-categories.json, in its order");

        std::vector<ProgramEntry> presets = list;   // the order of loadPrograms; the groups as the index carries them
        bool allGrouped = true;
        for (auto& p : presets)
        {
            auto it = jsonGroupOf.find (p.name);
            allGrouped = allGrouped && it != jsonGroupOf.end();
            if (it != jsonGroupOf.end()) p.group = it->second;
        }
        check (allGrouped, "every factory preset has a browser group");
        std::map<std::string, int> perGroup;
        for (const auto& p : presets) ++perGroup[p.group];
        const int N = static_cast<int> (presets.size());

        // mode names
        {
            GroupMode m = GroupMode::headers;
            check (parseGroupMode ("off", m) && m == GroupMode::off && parseGroupMode ("prefix-short", m) && m == GroupMode::prefixShort && parseGroupMode ("prefix-group", m) && m == GroupMode::prefixGroup
                   && parseGroupMode ("headers", m) && m == GroupMode::headers && parseGroupMode ("vst-categories", m) && m == GroupMode::vstCategories && ! parseGroupMode ("Off", m) && ! parseGroupMode ("", m),
                   "the five mode names parse (and nothing else)");
            check (std::string (groupModeName (GroupMode::vstCategories)) == "vst-categories" && std::string (groupModeName (GroupMode::off)) == "off", "mode names round trip");
        }
        // truncation
        check (truncateUtf8 ("abcdef", 3) == "abc" && truncateUtf8 ("ab cd", 3) == "ab" && truncateUtf8 ("abc", 3) == "abc", "truncateUtf8 cuts to the size and drops a trailing blank");
        check (truncateUtf8 ("ab\xc3\xa9", 3) == "ab" && truncateUtf8 ("a\xc3\xa9", 3) == "a\xc3\xa9", "truncateUtf8 never cuts inside a UTF-8 sequence");

        // the longest name (our longest is 22 characters)
        size_t longest = 0;
        for (size_t i = 1; i < presets.size(); ++i) if (presets[i].name.size() > presets[longest].name.size()) longest = i;
        check (presets[longest].name.size() <= kProgramNameMax, "our longest preset name fits the host's buffer");

        for (int mi = 0; mi <= 4; ++mi)
        {
            const GroupMode mode = static_cast<GroupMode> (mi);
            const std::string tag = std::string ("[") + groupModeName (mode) + "] ";
            const ProgramView v = buildProgramView (presets, mode);
            const int headers = mode == GroupMode::headers ? static_cast<int> (perGroup.size()) : 0;
            check (v.numPrograms() == N + 1 + headers, (tag + "list length: Init + presets (+ one header per group)").c_str());
            check (v.items[0].baseName == "Init" && v.items[0].label == "Init" && ! v.items[0].header && v.items[0].preset == -1, (tag + "program 0 is Init").c_str());

            std::vector<int> seen (presets.size(), 0);
            bool names = true, caps = true, legacy = true, rt = true;
            for (int n = 1; n < v.numPrograms(); ++n)
            {
                const auto& it = v.items[static_cast<size_t> (n)];
                if (it.header) continue;
                if (it.preset < 0 || it.preset >= N) { names = false; continue; }
                ++seen[static_cast<size_t> (it.preset)];
                names = names && it.baseName == presets[static_cast<size_t> (it.preset)].name;
                if (mode == GroupMode::off || mode == GroupMode::headers || mode == GroupMode::vstCategories) names = names && it.label == it.baseName;
                if (mode == GroupMode::prefixShort || mode == GroupMode::headers) caps = caps && it.label.size() <= kProgramNameMax;
                rt = rt && v.legacyToNumber[static_cast<size_t> (it.preset) + 1] == n;
            }
            for (int i = 0; i < N; ++i) legacy = legacy && seen[static_cast<size_t> (i)] == 1;
            check (legacy, (tag + "every preset is in the list exactly once").c_str());
            check (names, (tag + "base names are the preset names; labels as the mode says").c_str());
            check (caps, (tag + "labels that are not allowed beyond 24 characters are within 24").c_str());
            check (rt, (tag + "round trip: old number -> new number -> the same preset").c_str());

            // order and membership
            if (mode == GroupMode::off)
            {
                bool flat = v.groupNames.empty() && numProgramCategories (v) == 0;
                for (int i = 0; i < N; ++i) flat = flat && v.items[static_cast<size_t> (i) + 1].preset == i && v.legacyToNumber[static_cast<size_t> (i) + 1] == i + 1 && v.items[static_cast<size_t> (i) + 1].label == presets[static_cast<size_t> (i)].name;
                check (flat, (tag + "today's list: Init, then the presets in the order of sortPrograms under their own names; no categories").c_str());
            }
            else
            {
                bool inOrder = v.groupNames.size() == perGroup.size();
                int gi = 0, pos = 0;
                for (size_t g = 0; inOrder && g < v.groupNames.size(); ++g)
                {
                    inOrder = v.groupNames[g] == kFactoryGroups[g] && v.groupCount[g] == perGroup[v.groupNames[g]];
                    gi = static_cast<int> (g);
                    pos = v.groupFirst[g];
                    for (int k = 0; inOrder && k < v.groupCount[g]; ++k)
                    {
                        const auto& it = v.items[static_cast<size_t> (pos + k)];
                        inOrder = ! it.header && it.group == gi && presets[static_cast<size_t> (it.preset)].group == v.groupNames[g];
                        if (k > 0) inOrder = inOrder && lower (presets[static_cast<size_t> (v.items[static_cast<size_t> (pos + k - 1)].preset)].name) <= lower (it.baseName);
                    }
                    if (mode == GroupMode::headers)
                    {
                        std::string u;
                        for (char ch : std::string (kFactoryGroups[g])) u += static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
                        inOrder = inOrder && v.items[static_cast<size_t> (pos - 1)].header && v.items[static_cast<size_t> (pos - 1)].baseName == "-- " + u + " --" && v.items[static_cast<size_t> (pos - 1)].label == v.items[static_cast<size_t> (pos - 1)].baseName;
                    }
                }
                check (inOrder, (tag + "groups in the order of the Sounds browser files, names alphabetical inside, group sizes as in factory-preset-categories.json").c_str());
                // the same numbering as the .xpl file names: the k-th preset of the list is the k-th file (scripts/gen-xpl-presets.py: group order, then name.lower())
                std::vector<std::string> xplOrder;
                for (int g = 0; g < kFactoryGroupCount; ++g)
                {
                    std::vector<std::string> n;
                    for (const auto& p : presets) if (p.group == kFactoryGroups[g]) n.push_back (p.name);
                    std::sort (n.begin(), n.end(), [] (const std::string& a, const std::string& b) { return lower (a) < lower (b); });
                    xplOrder.insert (xplOrder.end(), n.begin(), n.end());
                }
                std::vector<std::string> ours;
                for (int n = 1; n < v.numPrograms(); ++n) if (! v.isHeader (n)) ours.push_back (v.items[static_cast<size_t> (n)].baseName);
                check (ours == xplOrder, (tag + "the preset order is the order of the .xpl file numbers (group order, then name.lower())").c_str());
            }
            check (numProgramCategories (v) == (mode == GroupMode::vstCategories ? kFactoryGroupCount : 0), (tag + "number of VST categories: the groups in mode vst-categories, else 0").c_str());

            // examples (three presets, the longest included), printed for the report
            std::vector<size_t> pick = { 0, presets.size() / 2, longest };
            for (size_t i : pick)
            {
                const int n = v.legacyToNumber[i + 1];
                std::printf ("      %s old %zu -> %d: \"%s\" (%zu) as written \"%s\"\n", groupModeName (mode), i + 1, n, v.label (n).c_str(), v.label (n).size(), truncateUtf8 (v.label (n), kProgramNameMax).c_str());
            }
            if (mode == GroupMode::headers)
                std::printf ("      headers: \"%s\" ... \"%s\"\n", v.items[1].label.c_str(), v.items[static_cast<size_t> (v.groupFirst.back()) - 1].label.c_str());

            // old saves: every preset by (old number, name), by name alone, and by the old number alone
            bool oldOk = true, nameOnly = true, numOnly = true, staleIndex = true;
            for (int i = 0; i < N; ++i)
            {
                const std::string& nm2 = presets[static_cast<size_t> (i)].name;
                const int a = resolveProgramInView (v, presets, i + 1, true, nm2);
                const int b = resolveProgramInView (v, presets, 9999, true, nm2);
                const int c2 = resolveProgramInView (v, presets, i + 1, false, "");
                const int d = resolveProgramInView (v, presets, (i + 7) % N + 1, true, nm2);
                oldOk = oldOk && a > 0 && ! v.isHeader (a) && v.items[static_cast<size_t> (a)].preset == i;
                nameOnly = nameOnly && b == a;
                numOnly = numOnly && c2 == a;
                staleIndex = staleIndex && d == a;
            }
            check (oldOk, (tag + "a save with the old number and the name finds its preset").c_str());
            check (nameOnly && staleIndex, (tag + "a save whose number no longer fits (other mode, other package) is found by its name").c_str());
            check (numOnly, (tag + "a save with only the old number (before names were stored) finds its preset through the old numbering").c_str());
            check (resolveProgramInView (v, presets, 0, true, "Init") == 0 && resolveProgramInView (v, presets, 3, true, "No Such Preset") == -1 && resolveProgramInView (v, presets, N + 5, false, "") == -1
                       && resolveProgramInView (v, presets, 0, false, "") == 0,
                   (tag + "Init, unknown name and out-of-range number as before").c_str());
            // the host-saved fixture (scripts/tests/data/synthyboy-host-saved-Space-Wobble-Bass.xpl: {"mpcProgram":20,"mpcProgramName":"Space Wobble Bass",...}; scripts/tests/test_program_groups_fixture.py checks the file still says so)
            {
                const int r = resolveProgramInView (v, presets, 20, true, "Space Wobble Bass");
                check (r > 0 && v.items[static_cast<size_t> (r)].baseName == "Space Wobble Bass" && ! v.isHeader (r), (tag + "the host-saved fixture (index 20, \"Space Wobble Bass\") resolves to that preset").c_str());
            }
        }

        // headers: Previous / Next
        {
            const ProgramView v = buildProgramView (presets, GroupMode::headers);
            const int L = v.numPrograms();
            bool sel = true;
            for (int h = 1; h < L; ++h)
            {
                if (! v.isHeader (h)) continue;
                sel = sel && effectiveProgram (v, h, 0) == h + 1 && effectiveProgram (v, h, h + 1) == h - 1 && ! v.isHeader (h - 1) && ! v.isHeader (h + 1);
            }
            check (sel && effectiveProgram (v, 5, 5) == 5 && effectiveProgram (v, -1, 0) == -1 && effectiveProgram (v, L, 0) == -1,
                   "[headers] a header loads the first preset of its group; selected while that preset is current it steps back (last preset before, or Init); presets load as they are");
            // host that asks for the current program after every step (effGetProgram)
            int cur = 0; std::vector<int> fwd;
            for (int step = 0; step < 4 * L && cur < L - 1; ++step) { const int t = effectiveProgram (v, cur + 1, cur); if (t < 0) break; cur = t; fwd.push_back (cur); }
            bool stepsFwd = cur == L - 1;
            std::set<int> uniq (fwd.begin(), fwd.end());
            check (stepsFwd && static_cast<int> (fwd.size()) == N && static_cast<int> (uniq.size()) == N, "[headers] Next from Init visits every preset once, in list order, and crosses every header (host reads effGetProgram)");
            std::vector<int> back;
            for (int step = 0; step < 4 * L && cur > 0; ++step) { const int t = effectiveProgram (v, cur - 1, cur); if (t < 0) break; cur = t; back.push_back (cur); }
            check (cur == 0 && static_cast<int> (back.size()) == N, "[headers] Previous from the last preset visits every preset once and crosses every header down to Init");
            // host that keeps its own counter (the header index stays in its counter)
            int c = 0; cur = 0; int steps = 0;
            while (cur != L - 1 && steps < 4 * L) { c = c + 1; const int t = effectiveProgram (v, c, cur); if (t >= 0) { cur = t; } ++steps; }
            check (cur == L - 1 && steps <= N + static_cast<int> (perGroup.size()) + 2, "[headers] Next with the host's own counter reaches the end (one repeated first preset per header at most)");
            steps = 0;
            while (cur != 0 && steps < 4 * L) { c = c - 1; if (c < 0) break; const int t = effectiveProgram (v, c, cur); if (t >= 0) cur = t; ++steps; }
            check (cur == 0 && steps <= N + static_cast<int> (perGroup.size()) + 2, "[headers] Previous with the host's own counter reaches Init (one repeated preset per header at most)");
        }

        // vst-categories: the category call
        {
            const ProgramView v = buildProgramView (presets, GroupMode::vstCategories);
            bool flat = true, cat = true;
            for (int n = 0; n < v.numPrograms(); ++n) flat = flat && programOfIndexed (v, n, -1, true) == n && programOfIndexed (v, n, 0, false) == n;
            check (flat && programOfIndexed (v, v.numPrograms(), -1, true) == -1 && programOfIndexed (v, -1, -1, true) == -1, "[vst-categories] category -1, or a host that never asked for the categories: the flat list");
            int total = 0;
            for (int g = 0; g < numProgramCategories (v); ++g)
            {
                int k = 0;
                while (programOfIndexed (v, k, g, true) >= 0)
                {
                    const int n = programOfIndexed (v, k, g, true);
                    cat = cat && v.items[static_cast<size_t> (n)].group == g;
                    ++k;
                }
                cat = cat && k == v.groupCount[static_cast<size_t> (g)];
                total += k;
            }
            check (cat && total == N && programOfIndexed (v, 0, numProgramCategories (v), true) == -1, "[vst-categories] category c lists exactly the presets of group c (Init is in none), together all presets once");
            const ProgramView off = buildProgramView (presets, GroupMode::off);
            check (programOfIndexed (off, 3, 2, true) == 3 && programOfIndexed (off, 0, 5, true) == 0, "[off] the category argument is ignored");
        }

        // user presets / no index: groups that are not factory groups sort after them, by folder
        {
            std::vector<ProgramEntry> mix = { { "Zed", "Pad", "z", "" }, { "Alpha", "Bass", "a", "Bass Sub+808" }, { "Mine", "Keys", "m", "" }, { "Beta", "FX", "b", "FX" }, { "My Lead", "Lead", "l", "" } };
            const ProgramView v = buildProgramView (mix, GroupMode::headers);
            std::vector<std::string> seq;
            for (int n = 1; n < v.numPrograms(); ++n) seq.push_back (v.items[static_cast<size_t> (n)].baseName);
            const std::vector<std::string> want = { "-- BASS SUB+808 --", "Alpha", "-- FX --", "Beta", "-- KEYS --", "Mine", "-- LEAD --", "My Lead", "-- PAD --", "Zed" };
            check (seq == want, "[headers] entries without a browser group sort after the factory groups, by folder category, under their folder as the group");
            const ProgramView ps = buildProgramView (mix, GroupMode::prefixShort);
            check (ps.label (ps.legacyToNumber[1]) == "Pad: Zed" && ps.label (ps.legacyToNumber[2]) == "Bass: Alpha", "[prefix-short] the prefix is the folder category");
            ProgramEntry big; big.name = "Bowed Pressure Strings"; big.category = "Pluck"; big.path = "p"; big.group = "Strings+Brass";
            const ProgramView a = buildProgramView ({ big }, GroupMode::prefixShort), b = buildProgramView ({ big }, GroupMode::prefixGroup);
            check (a.label (1) == "Pluck: Bowed Pressure St" && a.label (1).size() == 24, "[prefix-short] 22-character name behind \"Pluck: \": cut to 24");
            check (b.label (1) == "Strings+Brass: Bowed Pressure Strings" && truncateUtf8 (b.label (1), kProgramNameMax) == "Strings+Brass: Bowed Pre", "[prefix-group] the label is whole, the write cuts it to 24");
            check (buildProgramView ({}, GroupMode::headers).numPrograms() == 1 && numProgramCategories (buildProgramView ({}, GroupMode::vstCategories)) == 0, "an empty list has only Init in every mode");
        }

        // the status counters
        {
            StartupTimes t;
            t.setProgramMode ("headers", 176);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, -1);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, 0);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, 0);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, 99);
            t.noteProgramCall (StartupTimes::kPcNumCategories, 0);
            t.noteProgramCall (StartupTimes::kPcSetProgram, 5);
            const std::string d = t.describe();
            auto has = [&] (const char* x) { return d.find (x) != std::string::npos; };
            check (has ("program_groups_mode: headers\n") && has ("program_list_length: 176\n") && has ("program_calls_effGetProgramNameIndexed: 4\n") && has ("program_calls_effGetProgramNameIndexed_category_-1: 1\n")
                       && has ("program_calls_effGetProgramNameIndexed_category_0: 2\n") && has ("program_calls_effGetProgramNameIndexed_category_other: 1\n") && has ("program_calls_effGetNumProgramCategories: 1\n")
                       && has ("program_calls_effGetNumProgramCategories_category_0: 1\n") && has ("program_calls_effSetProgram: 1\n") && ! has ("program_calls_effSetProgram_category"),
                   "the status text counts the program calls by opcode and category value");
        }
    }

    std::printf ("programs_test: %s\n", fails == 0 ? "OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
