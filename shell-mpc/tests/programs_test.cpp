// SynthyBoy - shell-mpc acceptance: shell-mpc/program_list.h (program list from a presets directory, program number in the chunk).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: programs_test <repo>/content/presets. Links the core.
#include "../program_list.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "../startup_timing.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
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
    {
        check (synthyboy_mpc::stripModifiedMark ("Acid Saw Reso *") == "Acid Saw Reso" && synthyboy_mpc::stripModifiedMark ("Acid Saw Reso*") == "Acid Saw Reso" && synthyboy_mpc::stripModifiedMark ("Acid Saw Reso  **") == "Acid Saw Reso"
                   && synthyboy_mpc::stripModifiedMark ("Acid Saw Reso") == "Acid Saw Reso" && synthyboy_mpc::stripModifiedMark ("A*B") == "A*B" && synthyboy_mpc::stripModifiedMark ("") == "" && synthyboy_mpc::stripModifiedMark ("Init *") == "Init",
               "stripModifiedMark cuts a trailing \" *\" / \"*\" and nothing else");
        const std::string marked = "{\"mpcProgram\":20,\"mpcProgramName\":\"Acid Saw Reso *\",\"x\":1}";
        std::string got;
        check (synthyboy_mpc::programNameFromChunk (marked.data(), marked.size(), got) && got == "Acid Saw Reso", "a chunk saved with \"Name *\" is read as \"Name\"");
    }
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
        std::vector<ProgramEntry> l = { { "Alpha", "A", "a" }, { "Beta", "A", "b" }, { "Twin", "B", "c" }, { "Twin", "C", "d" } };
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

    // ---- the program list: one plain list (docs/MPC.md "Preset list of a plugin track") ----
    {
        using namespace synthyboy_mpc;
        // truncation
        check (truncateUtf8 ("abcdef", 3) == "abc" && truncateUtf8 ("ab cd", 3) == "ab" && truncateUtf8 ("abc", 3) == "abc", "truncateUtf8 cuts to the size and drops a trailing blank");
        check (truncateUtf8 ("ab\xc3\xa9", 3) == "ab" && truncateUtf8 ("a\xc3\xa9", 3) == "a\xc3\xa9", "truncateUtf8 never cuts inside a UTF-8 sequence");

        // the longest name (our longest is 22 characters)
        size_t longest = 0;
        for (size_t i = 1; i < list.size(); ++i) if (list[i].name.size() > list[longest].name.size()) longest = i;
        check (list[longest].name.size() <= kProgramNameMax, "our longest preset name fits the host's buffer");

        // the "modified" mark is gone by design: no name the host reads ends with "*"; a project saved with the old " *" still finds its preset
        bool noStar = true, markLoads = true;
        const int N = static_cast<int> (list.size());
        for (int i = 0; i < N; ++i)
        {
            const std::string& nm2 = list[static_cast<size_t> (i)].name;
            noStar = noStar && ! nm2.empty() && nm2.back() != '*';
            const int plain = resolveProgram (list, i + 1, true, nm2);
            markLoads = markLoads && resolveProgram (list, i + 1, true, stripModifiedMark (nm2 + " *")) == plain && resolveProgram (list, 9999, true, stripModifiedMark (nm2 + "*")) == plain;
        }
        check (noStar, "no preset name ends with \"*\"");
        check (markLoads, "a save with \"Name *\" / \"Name*\" resolves to the preset \"Name\"");
        // the host-saved fixture (scripts/tests/data/synthyboy-host-saved-Space-Wobble-Bass.xpl: {"mpcProgram":20,"mpcProgramName":"Space Wobble Bass",...})
        {
            const int r = resolveProgram (list, 20, true, "Space Wobble Bass");
            check (r > 0 && list[static_cast<size_t> (r - 1)].name == "Space Wobble Bass", "the host-saved fixture (index 20, \"Space Wobble Bass\") resolves to that preset");
        }

        // the status counters
        {
            StartupTimes t;
            t.setProgramCount (176);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, -1);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, 0);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, 0);
            t.noteProgramCall (StartupTimes::kPcGetProgramNameIndexed, 99);
            t.noteProgramCall (StartupTimes::kPcNumCategories, 0);
            t.noteProgramCall (StartupTimes::kPcSetProgram, 5);
            const std::string d = t.describe();
            auto has = [&] (const char* x) { return d.find (x) != std::string::npos; };
            check (has ("program_list_length: 176\n") && has ("program_calls_effGetProgramNameIndexed: 4\n") && has ("program_calls_effGetProgramNameIndexed_category_-1: 1\n")
                       && has ("program_calls_effGetProgramNameIndexed_category_0: 2\n") && has ("program_calls_effGetProgramNameIndexed_category_other: 1\n") && has ("program_calls_effGetNumProgramCategories: 1\n")
                       && has ("program_calls_effGetNumProgramCategories_category_0: 1\n") && has ("program_calls_effSetProgram: 1\n") && ! has ("program_calls_effSetProgram_category"),
                   "the status text counts the program calls by opcode and category value");
        }
    }

    std::printf ("programs_test: %s\n", fails == 0 ? "OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
