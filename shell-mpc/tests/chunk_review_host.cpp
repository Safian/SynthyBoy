// Robustness review of effSetChunk (review of 2026-11-16): the whole chunk path of the shell on hostile input, every case in a CHILD process so that a crash is a result and not the end of the test.
// The chunk is what the host hands to effSetChunk (the project file of the MPC): truncated at every length, bit flips, nesting a million deep, 8 MB of garbage, a huge number of members, a newer state version.
// Each child runs the same calls as synthyboy_mpc.cpp's effSetChunk: loadStateWithContent, programFromChunk, programNameFromChunk, playFromChunk, padPressureFromChunk.
// Expected: no case crashes or hangs (exit code 0 in the child). A failing case would be a finding.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "play_params.h"
#include "pad_pressure.h"
#include "program_list.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"

static int fails = 0;

static void everything (const std::string& chunk)
{
    synthyboy::ParamValues v = synthyboy::ParamValues::defaults();
    synthyboy::StateContent c;
    (void) synthyboy::loadStateWithContent (chunk.data(), chunk.size(), v, c);
    (void) synthyboy_mpc::programFromChunk (chunk.data(), chunk.size());
    std::string name;
    (void) synthyboy_mpc::programNameFromChunk (chunk.data(), chunk.size(), name);
    int sel[3] = { 1, 1, 1 };
    (void) synthyboy_mpc::play::playFromChunk (chunk.data(), chunk.size(), sel);
    bool on = false;
    float pc = 0.0f;
    (void) synthyboy_mpc::padPressureFromChunk (chunk.data(), chunk.size(), on, pc);
}

/** Runs `body` in a child with a 120 s alarm and 3 GB of address space; true: it ended normally. */
template <class F>
static bool inChild (const char* what, F&& body)
{
    std::fflush (stdout);
    const pid_t pid = fork();
    if (pid == 0)
    {
        alarm (120);
#if !defined(__SANITIZE_ADDRESS__) && !(defined(__has_feature) && __has_feature(address_sanitizer))
        rlimit rl { 2ull << 30, 2ull << 30 };   // (not under AddressSanitizer: its shadow memory needs the whole address space)
        setrlimit (RLIMIT_AS, &rl);
#endif
        body();
        _exit (0);
    }
    int st = 0;
    waitpid (pid, &st, 0);
    const bool ok = WIFEXITED (st) && WEXITSTATUS (st) == 0;
    std::printf ("%s  %s", ok ? "PASS" : "FAIL", what);
    if (! ok)
    {
        if (WIFSIGNALED (st))
            std::printf ("  (killed by signal %d)", WTERMSIG (st));
        else
            std::printf ("  (exit %d)", WEXITSTATUS (st));
        ++fails;
    }
    std::printf ("\n");
    return ok;
}

int main()
{
    synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
    const std::string state = synthyboy::saveState (values, synthyboy::StateContent {});
    const std::string chunk = synthyboy_mpc::chunkWithProgram (state, 3, "Some \"quoted\" name");
    std::printf ("INFO  a valid chunk is %zu bytes\n", chunk.size());

    inChild ("the valid chunk loads", [&] { everything (chunk); });
    inChild ("truncations of a valid chunk (every length in the first and the last 400 bytes, every 53rd between)", [&] { for (size_t n = 0; n <= chunk.size(); n += (n < 400 || n + 400 > chunk.size()) ? 1 : 53) everything (chunk.substr (0, n)); everything (chunk); });
    inChild ("single bytes (every 101st position) replaced by 0x00 / 0xff / quote / { / [", [&]
    {
        const char rep[5] = { 0, static_cast<char> (0xff), '"', '{', '[' };
        for (size_t i = 0; i < chunk.size(); i += 101)
            for (char r : rep)
            {
                std::string c = chunk;
                c[i] = r;
                everything (c);
            }
    });
    inChild ("3000 random multi-byte corruptions", [&]
    {
        std::mt19937 rng (12345);
        for (int k = 0; k < 3000; ++k)
        {
            std::string c = chunk;
            const int n = 1 + static_cast<int> (rng() % 8);
            for (int j = 0; j < n; ++j)
                c[rng() % c.size()] = static_cast<char> (rng());
            if (rng() % 4 == 0)
                c.resize (rng() % c.size());
            everything (c);
        }
    });
    inChild ("1 000 000 nested arrays inside a valid member", [&]
    {
        std::string c = chunk;
        c.insert (c.rfind ('}'), ",\"x\":" + std::string (1000000, '[') + std::string (1000000, ']'));
        everything (c);
    });
    inChild ("1 000 000 nested objects at the very start", [&]
    {
        std::string c = "{\"mpcProgram\":1,\"mpcProgramName\":\"a\",";
        for (int i = 0; i < 1000000; ++i)
            c += "\"a\":{";
        everything (c);
    });
    inChild ("8 MB of 0xff", [&] { everything (std::string (8u << 20, static_cast<char> (0xff))); });
    inChild ("8 MB of '{'", [&] { everything (std::string (8u << 20, '{')); });
    inChild ("a valid chunk followed by 8 MB of members", [&]
    {
        std::string c = chunk;
        std::string filler;
        filler.reserve (8u << 20);
        for (size_t i = 0; filler.size() < (8u << 20); ++i)
            filler += ",\"k" + std::to_string (i) + "\":1";
        c.insert (c.rfind ('}'), filler);
        everything (c);
    });
    inChild ("a name of 6 MB in mpcProgramName", [&]
    {
        std::string c = "{\"mpcProgram\":1,\"mpcProgramName\":\"" + std::string (6u << 20, 'a') + "\"," + state.substr (1);
        everything (c);
    });
    inChild ("a chunk from a NEWER state version (version 999) with unknown members", [&]
    {
        std::string c = chunk;
        const size_t at = c.find ("\"version\":");
        if (at != std::string::npos)
        {
            const size_t e = c.find_first_of (",}", at);
            c.replace (at, e - at, "\"version\":999");
        }
        c.insert (c.rfind ('}'), ",\"futureThing\":{\"a\":[1,2,3],\"b\":\"x\"}");
        everything (c);
    });

    std::printf (fails == 0 ? "OK\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
