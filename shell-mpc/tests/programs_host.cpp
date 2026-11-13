// SynthyBoy - shell-mpc acceptance: factory presets as VST programs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: programs_host <plugin library> <presets dir | none>
//   <presets dir>: the library sits in a plugin folder whose Content/presets is a copy of this directory;
//   none:          the library is outside any plugin folder: exactly one program, "Init".
// Links the core to read the preset files itself. Run the ThreadSanitizer build of it too.
#include "mpc_vst2_abi.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::atomic<int> displayUpdates { 0 };
static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterUpdateDisplay) { displayUpdates.fetch_add (1); return 1; }
    return opcode == audioMasterVersion ? 2400 : 0;
}

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}
static std::string lower (std::string s) { for (char& c : s) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c))); return s; }
static std::string getChunk (AEffect* e)
{
    void* data = nullptr;
    const VstIntPtr n = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);
    return (data != nullptr && n > 0) ? std::string (static_cast<const char*> (data), static_cast<size_t> (n)) : std::string();
}
static std::string nameOf (AEffect* e, int index)
{
    char buf[256]; std::memset (buf, 0x7f, sizeof (buf)); buf[255] = 0;
    const VstIntPtr ok = e->dispatcher (e, effGetProgramNameIndexed, index, 0, buf, 0.0f);
    if (ok == 0) return "<not named>";
    if (std::memchr (buf, 0, kVstMaxProgNameLen + 1) == nullptr) return "<not terminated within kVstMaxProgNameLen+1>";
    return buf;
}
static int program (AEffect* e) { return static_cast<int> (e->dispatcher (e, effGetProgram, 0, 0, nullptr, 0.0f)); }
static void select (AEffect* e, int i) { e->dispatcher (e, effSetProgram, 0, i, nullptr, 0.0f); }

/** True when every parameter of the plugin equals `want` (compared as host values). */
static bool matches (AEffect* e, const synthyboy::ParamValues& want, const char* label)
{
    const synthyboy::ParamInfo* table = synthyboy::paramTable();
    const size_t count = synthyboy::paramCount();
    for (size_t i = 0; i < count; ++i)
    {
        const float expect = synthyboy::toNormalised (table[i], want.plain[i]);
        const float got = e->getParameter (e, static_cast<VstInt32> (i));
        if (std::fabs (expect - got) > 1.0e-5f)
        {
            std::printf ("      %s: parameter %zu (%s): expected %.6f, plugin has %.6f\n", label, i, table[i].name, static_cast<double> (expect), static_cast<double> (got));
            return false;
        }
    }
    return true;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: programs_host <plugin> <presets dir | none>\n"); return 2; }
    const bool packaged = std::string (argv[2]) != "none";
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    using MainProc = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<MainProc> (dlsym (lib, "VSTPluginMain"));
    if (entry == nullptr) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    AEffect* e = entry (hostCallback);
    if (e == nullptr || e->magic != kEffectMagic) { std::fprintf (stderr, "bad AEffect\n"); return 2; }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 48000.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    const synthyboy::ParamValues defaults = synthyboy::ParamValues::defaults();
    check (nameOf (e, 0) == "Init", "program 0 is named Init");
    check (program (e) == 0, "program 0 is current after loading");

    if (! packaged)
    {
        check (e->numPrograms == 1, "outside a plugin folder there is exactly one program");
        select (e, 0);
        check (matches (e, defaults, "Init"), "program 0 holds the default values");
        select (e, 5);
        check (program (e) == 0, "an out-of-range program number is ignored");
        const std::string c = getChunk (e);
        check (c.rfind ("{\"mpcProgram\":0,", 0) == 0, "the chunk starts with {\"mpcProgram\":0,");
    }
    else
    {
        // the expected list, built independently: category, then name (case-insensitive), then path
        std::vector<synthyboy::PresetEntry> want = synthyboy::scanPresetDirectory (argv[2]);
        std::sort (want.begin(), want.end(), [] (const synthyboy::PresetEntry& a, const synthyboy::PresetEntry& b)
        {
            if (a.meta.category != b.meta.category) return a.meta.category < b.meta.category;
            if (lower (a.meta.name) != lower (b.meta.name)) return lower (a.meta.name) < lower (b.meta.name);
            return a.path < b.path;
        });
        const int count = static_cast<int> (want.size());
        check (count > 0 && e->numPrograms == 1 + count, "numPrograms is 1 + the number of preset files");
        bool names = true;
        for (int i = 0; i < count && i + 1 < e->numPrograms; ++i)
        {
            const std::string expect = want[static_cast<size_t> (i)].meta.name.substr (0, kVstMaxProgNameLen);
            const std::string got = nameOf (e, i + 1);
            if (got != expect) { names = false; std::printf ("      program %d: expected '%s', plugin says '%s'\n", i + 1, expect.c_str(), got.c_str()); break; }
        }
        check (names, "effGetProgramNameIndexed gives the preset names in category / name order (at most kVstMaxProgNameLen characters)");
        check (e->dispatcher (e, effGetProgramNameIndexed, e->numPrograms, 0, nullptr, 0.0f) == 0, "effGetProgramNameIndexed beyond the list returns 0 (and tolerates a null pointer)");

        auto load = [&] (int index, synthyboy::ParamValues& values)
        {
            std::ifstream in (want[static_cast<size_t> (index - 1)].path, std::ios::binary);
            std::stringstream ss; ss << in.rdbuf(); const std::string text = ss.str();
            synthyboy::PresetMeta meta; synthyboy::StateContent content; values = synthyboy::ParamValues::defaults();
            const synthyboy::LoadResult r = synthyboy::loadPreset (text.data(), text.size(), meta, values, content);
            return r.status == synthyboy::LoadStatus::ok || r.status == synthyboy::LoadStatus::newer_version;
        };

        // selecting a program loads the preset and asks for ONE display update
        bool loaded = true, once = true, current = true, named = true;
        for (int index : { 1, count / 2, count })
        {
            synthyboy::ParamValues values;
            if (! load (index, values)) { std::printf ("      the test could not read preset %d\n", index); loaded = false; continue; }
            const int before = displayUpdates.load();
            select (e, index);
            once = once && displayUpdates.load() == before + 1;
            current = current && program (e) == index;
            loaded = loaded && matches (e, values, want[static_cast<size_t> (index - 1)].meta.name.c_str());
            char buf[64] = {}; e->dispatcher (e, effGetProgramName, 0, 0, buf, 0.0f);
            named = named && std::string (buf) == want[static_cast<size_t> (index - 1)].meta.name.substr (0, kVstMaxProgNameLen);
        }
        check (loaded, "effSetProgram loads the values of the preset file");
        check (once, "effSetProgram calls audioMasterUpdateDisplay exactly once");
        check (current, "effGetProgram returns the selected program");
        check (named, "effGetProgramName returns the current program's name");

        const int updates = displayUpdates.load();
        select (e, -1); select (e, e->numPrograms);
        check (program (e) == count && displayUpdates.load() == updates, "out-of-range program numbers are ignored (no load, no display update)");

        select (e, 0);
        check (program (e) == 0 && matches (e, defaults, "Init"), "program 0 (Init) restores the default values");

        // the chunk carries the program number and wins over the program selection that follows it
        const int pa = std::min (5, count), pb = std::min (9, count);
        synthyboy::ParamValues va; load (pa, va);
        select (e, pa);
        const float edited = e->getParameter (e, 0) > 0.5f ? 0.125f : 0.875f;
        e->setParameter (e, 0, edited);
        const std::string saved = getChunk (e);
        check (saved.rfind ("{\"mpcProgram\":" + std::to_string (pa) + ",", 0) == 0, "the chunk starts with {\"mpcProgram\":<current program>,");
        select (e, pb);
        e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (saved.size()), const_cast<char*> (saved.data()), 0.0f);
        check (program (e) == pa, "effSetChunk restores the program number stored in the chunk");
        const float afterChunk = e->getParameter (e, 0);
        check (std::fabs (afterChunk - edited) < 1.0e-3f, "effSetChunk restores the edited value");
        const int u2 = displayUpdates.load();
        select (e, pa); // the host selecting the saved program right after restoring the chunk
        check (std::fabs (e->getParameter (e, 0) - afterChunk) < 1.0e-6f && displayUpdates.load() == u2, "effSetProgram(saved program) directly after effSetChunk keeps the restored state");
        select (e, pa); // the user picking the same program again
        check (matches (e, va, "second selection"), "the second effSetProgram of the same program loads the preset again");

        e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (saved.size()), const_cast<char*> (saved.data()), 0.0f);
        synthyboy::ParamValues vb; load (pb, vb);
        select (e, pb); // a different program after a chunk is a real selection
        check (program (e) == pb && matches (e, vb, "other program after chunk"), "effSetProgram(another program) after effSetChunk loads that program");

        e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (saved.size()), const_cast<char*> (saved.data()), 0.0f);
        e->setParameter (e, 1, e->getParameter (e, 1) > 0.5f ? 0.25f : 0.75f); // an edit ends the protection
        select (e, pa);
        check (matches (e, va, "after an edit"), "after a parameter edit, effSetProgram(saved program) loads the preset");

        const std::string plain = synthyboy::saveState (defaults);
        select (e, pa);
        e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (plain.size()), const_cast<char*> (plain.data()), 0.0f);
        check (program (e) == pa && matches (e, defaults, "plain chunk"), "a chunk without a program number loads its values and keeps the current program number");
        const std::string wild = "{\"mpcProgram\":100000," + plain.substr (1);
        e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (wild.size()), const_cast<char*> (wild.data()), 0.0f);
        check (program (e) == pa, "a chunk with a program number beyond the list keeps the current program number");

        // program changes from the UI thread while the audio thread processes
        std::vector<float> left (256), right (256);
        float* outs[2] = { left.data(), right.data() };
        std::atomic<bool> run { true };
        std::thread ui ([&] { int i = 0; while (run.load()) { select (e, 1 + (i * 7) % count); ++i; (void) getChunk (e); } });
        VstMidiEvent on; std::memset (&on, 0, sizeof (on));
        on.type = kVstMidiType; on.byteSize = sizeof (VstMidiEvent); on.midiData[0] = static_cast<char> (0x90); on.midiData[1] = 60; on.midiData[2] = 100;
        VstEvents events; std::memset (&events, 0, sizeof (events)); events.numEvents = 1; events.events[0] = reinterpret_cast<VstEvent*> (&on);
        bool finite = true;
        for (int block = 0; block < 600; ++block)
        {
            if (block % 40 == 0) { on.midiData[1] = static_cast<char> (48 + (block / 40) % 24); e->dispatcher (e, effProcessEvents, 0, 0, &events, 0.0f); }
            e->processReplacing (e, nullptr, outs, 256);
            for (int i = 0; i < 256; ++i) finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
        }
        run.store (false); ui.join();
        check (finite, "audio stayed finite while the UI thread changed programs");
    }

    e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    std::printf ("programs_host: %s\n", fails == 0 ? "OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
