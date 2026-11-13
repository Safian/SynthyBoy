// SynthyBoy - shell-mpc acceptance: shell-mpc/program_list.h (program list from a presets directory, program number in the chunk).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: programs_test <repo>/content/presets. Links the core.
#include "../program_list.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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
    const std::string c = synthyboy_mpc::chunkWithProgram (state, 37);
    check (c == "{\"mpcProgram\":37," + state.substr (1), "chunkWithProgram puts {\"mpcProgram\":N, in front of the state document's members");
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

    std::printf ("programs_test: %s\n", fails == 0 ? "OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
