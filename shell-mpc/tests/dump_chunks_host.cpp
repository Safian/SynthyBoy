// SynthyBoy - dump_chunks_host: dumps factory preset chunks from synthyboy_mpc.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: dump_chunks_host <plugin library> <content dir> <output dir>
//   Loads synthyboy_mpc through the VST2 C ABI, selects every program,
//   and writes the JSON chunk from effGetChunk to <output dir>/program_NNN.chunk
//   along with a manifest.json.

#include "mpc_vst2_abi.h"
#include "program_list.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

static std::string escapeJsonString (const std::string& s)
{
    std::string out;
    out.reserve (s.size() + 8);
    for (unsigned char c : s)
    {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20)
        {
            char buf[8];
            std::snprintf (buf, sizeof (buf), "\\u%04x", c);
            out += buf;
        }
        else out += static_cast<char> (c);
    }
    return out;
}

int main (int argc, char** argv)
{
    std::string pluginPath;
    std::string contentDir;
    std::string outDir;

    // Parse options or positional arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--plugin" || arg == "-p") && i + 1 < argc)
        {
            pluginPath = argv[++i];
        }
        else if ((arg == "--content" || arg == "-c") && i + 1 < argc)
        {
            contentDir = argv[++i];
        }
        else if ((arg == "--out" || arg == "-o") && i + 1 < argc)
        {
            outDir = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::printf ("Usage: dump_chunks_host <plugin library> <content dir> <output dir>\n");
            std::printf ("   or: dump_chunks_host --plugin <path> --content <dir> --out <dir>\n");
            return 0;
        }
        else if (arg[0] != '-')
        {
            if (pluginPath.empty()) pluginPath = arg;
            else if (contentDir.empty()) contentDir = arg;
            else if (outDir.empty()) outDir = arg;
        }
    }

    if (pluginPath.empty() || contentDir.empty() || outDir.empty())
    {
        std::fprintf (stderr, "Error: Missing required arguments.\n");
        std::fprintf (stderr, "Usage: dump_chunks_host <plugin library> <content dir> <output dir>\n");
        return 2;
    }

    std::error_code ec;
    fs::path pPlugin = fs::absolute (pluginPath, ec);
    fs::path pContent = fs::absolute (contentDir, ec);
    fs::path pOut = fs::absolute (outDir, ec);

    if (! fs::is_regular_file (pPlugin, ec) || ec)
    {
        std::fprintf (stderr, "Error: Plugin library not found: %s\n", pPlugin.string().c_str());
        return 2;
    }
    if (! fs::is_directory (pContent, ec) || ec)
    {
        std::fprintf (stderr, "Error: Content directory not found: %s\n", pContent.string().c_str());
        return 2;
    }

    // Determine presets source folder (either pContent/presets or pContent directly)
    fs::path pPresets = pContent;
    if (fs::is_directory (pContent / "presets", ec) && ! ec)
    {
        pPresets = pContent / "presets";
    }

    // Check if plugin is already inside a valid package layout:
    // walk up to find plugin.json, and check if <package>/Content/presets exists.
    bool needsStaging = true;
    fs::path dir = pPlugin.parent_path();
    while (! dir.empty())
    {
        if (fs::is_regular_file (dir / "plugin.json", ec) && ! ec)
        {
            if (fs::is_directory (dir / "Content" / "presets", ec) && ! ec)
            {
                needsStaging = false;
            }
            break;
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = std::move (parent);
    }

    fs::path stageDir;
    fs::path loadPath = pPlugin;

    if (needsStaging)
    {
        auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        stageDir = fs::temp_directory_path (ec) / ("synthyboy_dump_stage_" + std::to_string (timestamp));
        fs::create_directories (stageDir / "VST", ec);
        fs::create_directories (stageDir / "Content", ec);

        // Minimal plugin.json so pluginFolderFor finds stageDir
        std::ofstream pjf ((stageDir / "plugin.json").string());
        pjf << "{}\n";
        pjf.close();

        // Copy binary into stageDir/VST
        fs::path stagedLib = stageDir / "VST" / pPlugin.filename();
        fs::copy_file (pPlugin, stagedLib, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            std::fprintf (stderr, "Error creating staged plugin copy: %s\n", ec.message().c_str());
            return 1;
        }

        // Copy presets into stageDir/Content/presets
        fs::path stagedPresets = stageDir / "Content" / "presets";
        fs::copy (pPresets, stagedPresets, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            std::fprintf (stderr, "Error copying presets to staging: %s\n", ec.message().c_str());
            fs::remove_all (stageDir, ec);
            return 1;
        }

        loadPath = stagedLib;
    }

    // Load plugin
    void* lib = dlopen (loadPath.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (! lib)
    {
        std::fprintf (stderr, "dlopen failed for %s: %s\n", loadPath.string().c_str(), dlerror());
        if (! stageDir.empty()) fs::remove_all (stageDir, ec);
        return 1;
    }

    using MainProc = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<MainProc> (dlsym (lib, "VSTPluginMain"));
    if (! entry)
    {
        std::fprintf (stderr, "no VSTPluginMain in %s\n", loadPath.string().c_str());
        dlclose (lib);
        if (! stageDir.empty()) fs::remove_all (stageDir, ec);
        return 1;
    }

    AEffect* e = entry (hostCallback);
    if (! e || e->magic != kEffectMagic)
    {
        std::fprintf (stderr, "invalid AEffect from plugin\n");
        dlclose (lib);
        if (! stageDir.empty()) fs::remove_all (stageDir, ec);
        return 1;
    }

    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    const int numProgs = e->numPrograms;
    if (numProgs <= 1)
    {
        std::fprintf (stderr, "Error: Plugin reported only %d programs (expected > 1 factory presets)\n", numProgs);
        e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0.0f);
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        dlclose (lib);
        if (! stageDir.empty()) fs::remove_all (stageDir, ec);
        return 1;
    }

    fs::create_directories (pOut, ec);

    std::string manifestJson = "[\n";
    int exportedCount = 0;

    for (int i = 0; i < numProgs; ++i)
    {
        // Select program
        e->dispatcher (e, effSetProgram, 0, i, nullptr, 0.0f);

        // A header entry of a grouped program list (mode "headers") loads another program: the plugin's current program is then not `i`; it is no preset, skip it
        if (e->dispatcher (e, effGetProgram, 0, 0, nullptr, 0.0f) != i)
            continue;

        // Get chunk
        void* data = nullptr;
        const VstIntPtr n = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);

        // The program's own name is the one in the chunk (program_list.h programNameFromChunk), not the label of the host's list (the prefix modes change that)
        char nameBuf[256] = {};
        e->dispatcher (e, effGetProgramNameIndexed, i, 0, nameBuf, 0.0f);
        std::string progName = nameBuf;
        if (data != nullptr && n > 0)
        {
            std::string own;
            if (synthyboy_mpc::programNameFromChunk (data, static_cast<size_t> (n), own))
                progName = own;
        }
        if (progName.empty() && i == 0) progName = "Init";
        if (data == nullptr || n <= 0)
        {
            std::fprintf (stderr, "Warning: Program %d ('%s') returned empty chunk\n", i, progName.c_str());
            continue;
        }

        char numBuf[16];
        std::snprintf (numBuf, sizeof (numBuf), "%03d", i);
        std::string chunkFilename = "program_" + std::string (numBuf) + ".chunk";
        std::string nameFilename = "program_" + std::string (numBuf) + ".name";

        // Write raw chunk
        std::ofstream cf ((pOut / chunkFilename).string(), std::ios::binary);
        cf.write (static_cast<const char*> (data), n);
        cf.close();

        // Write companion name file
        std::ofstream nf ((pOut / nameFilename).string());
        nf << progName << "\n";
        nf.close();

        // Append to manifest
        if (exportedCount > 0) manifestJson += ",\n";
        manifestJson += "  {\n";
        manifestJson += "    \"index\": " + std::to_string (i) + ",\n";
        manifestJson += "    \"name\": \"" + escapeJsonString (progName) + "\",\n";
        manifestJson += "    \"chunk_file\": \"" + chunkFilename + "\",\n";
        manifestJson += "    \"size\": " + std::to_string (n) + "\n";
        manifestJson += "  }";

        ++exportedCount;
    }

    manifestJson += "\n]\n";
    std::ofstream mf ((pOut / "manifest.json").string());
    mf << manifestJson;
    mf.close();

    // Clean up plugin
    e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    dlclose (lib);

    if (! stageDir.empty())
    {
        fs::remove_all (stageDir, ec);
    }

    std::printf ("dump_chunks_host: successfully dumped %d program chunks to %s\n", exportedCount, pOut.string().c_str());
    return 0;
}
