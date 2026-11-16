// SynthyBoy - shell-mpc acceptance: the names of the macros in the plugin built with SYNTHYBOY_MPC_OVERLAY (docs/ui-design/macro-names.md): the chunk carries them (content.macro_names) and a chunk of before
// the names loads with the default ones, a program change or a load replaces them, the Q proxy whose target is a macro reports the name (the host is told by audioMasterUpdateDisplay when it changes),
// a rename of the Macros page (the debug file SYNTHYBOY_MPC_MACRO_NAME_FILE stands for the page) reaches the chunk, dirty names are cleaned, the parameter list is the one of before (2719).
//
// Usage: macro_names_chunk_host <plugin library with the overlay>. Run under ThreadSanitizer too.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpc_vst2_abi.h"
#include "macro_names.h"
#include "q_router.h"
#include "synthyboy/state.h"

#include <atomic>
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

static std::mutex mu;
static std::map<AEffect*, int> updateDisplays;
static VstIntPtr hostCallback (AEffect* e, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterUpdateDisplay)
    {
        std::lock_guard<std::mutex> g (mu);
        ++updateDisplays[e];
    }
    return opcode == audioMasterVersion ? 2400 : (opcode == audioMasterAutomate ? 1 : 0);
}
static int updates (AEffect* e) { std::lock_guard<std::mutex> g (mu); return updateDisplays[e]; }

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    if (! ok || std::getenv ("MACRO_NAMES_CHUNK_HOST_VERBOSE"))
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
}
static bool waitFor (int ms, const std::function<bool()>& cond)
{
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (ms))
    {
        if (cond())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return cond();
}
static std::string getChunk (AEffect* e)
{
    void* data = nullptr;
    const VstIntPtr n = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);
    return (data != nullptr && n > 0) ? std::string (static_cast<const char*> (data), static_cast<size_t> (n)) : std::string();
}
static bool setChunk (AEffect* e, const std::string& c)
{
    return e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (c.size()), const_cast<char*> (c.data()), 0.0f) != 0;
}
static std::string text (AEffect* e, VstInt32 opcode, int index)
{
    char buf[256];
    std::memset (buf, 0, sizeof (buf));
    e->dispatcher (e, opcode, index, 0, buf, 0.0f);
    return buf;
}

using Main = AEffect* (*) (audioMasterCallback);
static Main entry = nullptr;
static AEffect* make()
{
    AEffect* e = entry (hostCallback);
    if (! e) return nullptr;
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    return e;
}

static std::string stateWith (std::initializer_list<std::pair<int, const char*>> names)
{
    synthyboy::StateContent c;
    for (const auto& n : names)
        c.macroNames[n.first] = n.second;
    return synthyboy::saveState (synthyboy::ParamValues::defaults(), c);
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: macro_names_chunk_host <plugin>\n"); return 2; }
    const std::string qFake = "/tmp/synthyboy-q-fake-group-macronames-" + std::to_string (static_cast<long> (getpid()));
    const std::string nameFile = "/tmp/synthyboy-macro-name-macronames-" + std::to_string (static_cast<long> (getpid()));
    setenv ("SYNTHYBOY_MPC_Q_FAKE_FILE", qFake.c_str(), 1);
    setenv ("SYNTHYBOY_MPC_MACRO_NAME_FILE", nameFile.c_str(), 1);
    std::remove (qFake.c_str());
    std::remove (nameFile.c_str());
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    entry = reinterpret_cast<Main> (dlsym (lib, "VSTPluginMain"));
    if (! entry) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }

    int macroIdx[8];
    for (int m = 0; m < 8; ++m)
    {
        const synthyboy::ParamInfo* p = synthyboy::findParamByKey (("macro." + std::to_string (m + 1)).c_str());
        macroIdx[m] = p != nullptr ? static_cast<int>(p - synthyboy::paramTable()) : -1;
    }
    const int q0 = 2703;
    auto writeGroup = [&] (const std::string& line) { if (FILE* f = std::fopen (qFake.c_str(), "w")) { std::fputs ((line + "\n").c_str(), f); std::fclose (f); } };
    auto rename = [&] (const std::string& line) { if (FILE* f = std::fopen (nameFile.c_str(), "w")) { std::fputs ((line + "\n").c_str(), f); std::fclose (f); } };

    AEffect* a = make();
    if (! a) { std::fprintf (stderr, "plugin did not load\n"); return 2; }

    // ---- the parameter list is the one of before: names are state, not parameters
    check (a->numParams == 2719 && synthyboy::paramCount() == 2637 && text (a, effGetParamName, macroIdx[0]) == "Macro 1" && text (a, effGetParamName, 2718) == "Q 16", "the parameter list did not change: 2637 engine parameters, 2719 in the shell; the macro parameters keep their table names");

    // ---- a chunk of before the names: no member, default names
    const std::string fresh = getChunk (a);
    check (! fresh.empty() && fresh.find ("macro_names") == std::string::npos, "the chunk of a plugin without names has no macro_names member (byte for byte the chunk of before)");
    writeGroup (std::to_string (macroIdx[0]) + " " + std::to_string (macroIdx[1]) + " " + std::to_string (macroIdx[2]) + " -1");
    check (waitFor (2000, [&] { return text (a, effGetParamName, q0) == "Macro 1"; }) && text (a, effGetParamName, q0 + 1) == "Macro 2" && text (a, effGetParamName, q0 + 3) == "Q 4", "the Q proxy of a macro without a name reports Macro n");

    // ---- a chunk with names: the Q proxy reports them, the host is told
    const int ud0 = updates (a);
    check (setChunk (a, stateWith ({ { 0, "Filter Sweep" }, { 2, "Gr\xc3\xb6\xc3\x9f" "e" } })), "a chunk with names loads");
    check (waitFor (2000, [&] { return updates (a) > ud0; }), "the host is told (audioMasterUpdateDisplay) that the names changed");
    check (text (a, effGetParamName, q0) == "Filter Sweep" && text (a, effGetParamName, q0 + 1) == "Macro 2" && text (a, effGetParamName, q0 + 2) == "Gr\xc3\xb6\xc3\x9f" "e", "the Q proxy reports the name of its macro: Filter Sweep, Macro 2, Gr\xc3\xb6\xc3\x9f" "e");
    check (text (a, effGetParamName, macroIdx[0]) == "Macro 1", "the macro's own parameter keeps its table name");
    const std::string withNames = getChunk (a);
    check (withNames.find ("\"macro_names\":[\"Filter Sweep\",\"\",\"Gr\xc3\xb6\xc3\x9f" "e\",\"\",\"\",\"\",\"\",\"\"]") != std::string::npos, "the chunk carries the names (content.macro_names)");
    // the whole chunk round trip: another instance restores the names from it
    AEffect* b = make();
    check (setChunk (b, withNames) && getChunk (b) == withNames, "a chunk with names restores into another instance and writes back byte for byte");
    writeGroup (std::to_string (macroIdx[0]) + " -1 -1 -1");   // the debug file of the second instance is the same path: both follow it
    check (waitFor (2000, [&] { return text (b, effGetParamName, q0) == "Filter Sweep"; }), "... and its Q proxy reports the name");
    // a chunk of before the names replaces them with the defaults
    const int ud1 = updates (b);
    check (setChunk (b, fresh) && getChunk (b).find ("macro_names") == std::string::npos, "a chunk without names: default names, no member");
    check (waitFor (2000, [&] { return updates (b) > ud1 && text (b, effGetParamName, q0) == "Macro 1"; }), "... the Q proxy says Macro 1 again and the host is told");
    // a program change replaces the names too (the init program has none)
    const int ud2 = updates (a);
    a->dispatcher (a, effSetProgram, 0, 0, nullptr, 0.0f);
    check (getChunk (a).find ("macro_names") == std::string::npos && waitFor (2000, [&] { return updates (a) > ud2 && text (a, effGetParamName, q0) == "Macro 1"; }), "a program change (Init) resets the names");

    // ---- a rename of the page: through the display thread into the content and the chunk
    writeGroup (std::to_string (macroIdx[0]) + " " + std::to_string (macroIdx[1]) + " " + std::to_string (macroIdx[2]) + " " + std::to_string (macroIdx[3]));
    waitFor (2000, [&] { return text (a, effGetParamName, q0 + 3) == "Macro 4"; });
    const int ud3 = updates (a);
    rename ("2 Drive + Warp");
    check (waitFor (2000, [&] { return text (a, effGetParamName, q0 + 1) == "Drive + Warp"; }), "a rename of the Macros page: the Q proxy of macro 2 reports it");
    check (updates (a) > ud3, "... and the host is told");
    check (getChunk (a).find ("\"macro_names\":[\"\",\"Drive + Warp\",\"\",") != std::string::npos, "... and the chunk carries it");
    rename ("1   \t\"Quo\\ted\"  a very long name that is cut");
    check (waitFor (2000, [&] { return text (a, effGetParamName, q0) != "Macro 1"; }), "a second rename arrives");
    const std::string cleaned = text (a, effGetParamName, q0);
    check (cleaned == "\"Quo\\ted\" a very " || cleaned == "\"Quo\\ted\" a very", "dirty text is cleaned (tabs and runs of blanks), cut at 16 characters and trimmed: [" + cleaned + "]");
    check (getChunk (a).find ("\\\"Quo\\\\ted\\\" a very") != std::string::npos, "the chunk has quotes and backslashes escaped");
    AEffect* c = make();
    check (setChunk (c, getChunk (a)) && getChunk (c) == getChunk (a), "the chunk with the escaped name round trips");
    const int ud4 = updates (a);
    rename ("1 ");
    check (waitFor (2000, [&] { return text (a, effGetParamName, q0) == "Macro 1"; }) && updates (a) > ud4, "an empty name is the default again");
    // the same rename twice does not tell the host twice
    rename ("4 Same");
    waitFor (2000, [&] { return getChunk (a).find ("Same") != std::string::npos; });
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    const int ud5 = updates (a);
    rename ("4 Same ");
    std::this_thread::sleep_for (std::chrono::milliseconds (500));
    check (updates (a) == ud5, "a rename to the name it has changes nothing and tells nobody");

    // ---- a name beyond what the host's buffer takes
    {
        std::string wide = "{\"format\":\"synthyboy-state\",\"version\":4,\"params\":{},\"content\":{\"macro_names\":[\"";
        for (int i = 0; i < 20; ++i)
            wide += "\xe2\x82\xac";
        wide += "\"]}}";
        check (setChunk (a, wide), "a chunk with a name of 20 three byte characters loads");
        writeGroup (std::to_string (macroIdx[0]) + " -1 -1 -1");
        check (waitFor (2000, [&] { return text (a, effGetParamName, q0).size() == 48; }), "the name is cut to 16 characters (48 bytes), whole characters, inside the 64 byte buffer of the host");
    }

    a->dispatcher (a, effClose, 0, 0, nullptr, 0.0f);
    b->dispatcher (b, effClose, 0, 0, nullptr, 0.0f);
    c->dispatcher (c, effClose, 0, 0, nullptr, 0.0f);
    std::remove (qFake.c_str());
    std::remove (nameFile.c_str());
    std::printf (fails == 0 ? "macro_names_chunk_host: OK\n" : "macro_names_chunk_host: FAILED (%d failures)\n", fails);
    return fails == 0 ? 0 : 1;
}
