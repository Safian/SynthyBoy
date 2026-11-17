// SynthyBoy - chunk_load_host: loads a program or a raw chunk into synthyboy_mpc and prints every parameter.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: chunk_load_host <plugin library inside a package layout> (--program <name> | --chunk <file>)
//   The library must sit in <package>/VST/ next to <package>/plugin.json and <package>/Content/presets (as dump_chunks_host stages it).
//   --program selects the factory program of that name (effSetProgram); --chunk sends the file's bytes through effSetChunk(index 0, the bank
//   chunk the host sends after it restores a saved preset). Output: one line "<index>\t<name>\t<value %.9g>" per parameter, then "chunk <size>" and the
//   chunk text the plugin gives back (effGetChunk), so a caller can compare the sound and the state.
#include "mpc_vst2_abi.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

int main (int argc, char** argv)
{
    if (argc != 4 || (std::strcmp (argv[2], "--program") != 0 && std::strcmp (argv[2], "--chunk") != 0))
    {
        std::fprintf (stderr, "Usage: chunk_load_host <plugin library> (--program <name> | --chunk <file>)\n");
        return 2;
    }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 1; }
    auto entry = reinterpret_cast<AEffect* (*) (audioMasterCallback)> (dlsym (lib, "VSTPluginMain"));
    AEffect* e = entry ? entry (hostCallback) : nullptr;
    if (! e || e->magic != kEffectMagic) { std::fprintf (stderr, "no effect\n"); return 1; }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    if (std::strcmp (argv[2], "--program") == 0)
    {
        int found = -1;
        for (int i = 0; i < e->numPrograms; ++i)
        {
            char nm[256] = {};
            e->dispatcher (e, effGetProgramNameIndexed, i, 0, nm, 0.0f);
            if (std::strcmp (nm, argv[3]) == 0) { found = i; break; }
        }
        if (found < 0) { std::fprintf (stderr, "no program '%s'\n", argv[3]); return 1; }
        e->dispatcher (e, effSetProgram, 0, found, nullptr, 0.0f);
    }
    else
    {
        std::ifstream f (argv[3], std::ios::binary);
        std::vector<char> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        const VstIntPtr r = e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (bytes.size()), bytes.data(), 0.0f);
        std::fprintf (stderr, "effSetChunk(%zu bytes) -> %ld\n", bytes.size(), static_cast<long> (r));
    }

    for (int i = 0; i < e->numParams; ++i)
    {
        char nm[256] = {};
        e->dispatcher (e, effGetParamName, i, 0, nm, 0.0f);
        std::printf ("%d\t%s\t%.9g\n", i, nm, static_cast<double> (e->getParameter (e, i)));
    }
    void* data = nullptr;
    const VstIntPtr n = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);
    std::printf ("chunk %ld\n", static_cast<long> (n));
    if (data && n > 0) std::fwrite (data, 1, static_cast<size_t> (n), stdout);
    std::printf ("\n");
    e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    return 0;
}
