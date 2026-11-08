// SynthyBoy - vital-roundtrip: loads imported .sbpreset files into the Vital-derived engine the way the shell does and dumps what the
// engine holds, for the audit (scripts/audit-vital-import.py).
//
//   synthyboy_vital_roundtrip --presets DIR --content-dir DIR [--out FILE.json]
//
// Per preset (a fresh engine, Tier::high, 48 kHz): every parameter is set, the content and the content store are handed over, one
// block of silence is rendered (the parameters are copied into the engine at the start of a block), then
//   - getParam () of every key is compared with the value of the file (the state round trip),
//   - the value of every engine control the importer's table addresses is dumped (vitalControlValue), so the audit can compare it with the
//     raw number of the original .vital file,
//   - the number of connected modulation slots is dumped.
// Output: one JSON document {"presets":[{"file","param_mismatches":[{key,set,got}],"controls":{name:value},"connected_slots":n}]}.
// Exit code 0 when every parameter reads back, 1 otherwise.
//
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"
#include "synthyboy/vital_import.h"
#include "vital_engine.h"

using namespace synthyboy;

namespace
{
std::string readFile (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

std::string num (double v)
{
    if (! std::isfinite (v))
        return "null";
    char b[40];
    std::snprintf (b, sizeof b, "%.9g", v);
    return b;
}

void esc (const std::string& s, std::string& out)
{
    out += '"';
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '"';
}
} // namespace

int main (int argc, char** argv)
{
    std::string presets, contentDir, outFile;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--presets") presets = next();
        else if (a == "--content-dir") contentDir = next();
        else if (a == "--out") outFile = next();
        else
        {
            presets.clear();
            break;
        }
    }
    if (presets.empty() || ! isEngineAvailable (EngineKind::vital))
    {
        std::fprintf (stderr, "usage: synthyboy_vital_roundtrip --presets DIR [--content-dir DIR] [--out FILE.json]  (needs the Vital engine)\n");
        return 2;
    }
    if (contentDir.empty())
        contentDir = (std::filesystem::path (presets) / "content").string();
    const DirectoryContentStore store (contentDir);

    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator (presets, ec))
        if (e.is_regular_file() && e.path().extension() == ".sbpreset")
            files.push_back (e.path());
    std::sort (files.begin(), files.end());

    // the Vital controls the audit looks at: every row of the importer's table plus the special settings
    std::vector<std::string> controls;
    for (size_t r = 0; r < vitalRowCount(); ++r)
        controls.push_back (vitalRowControl (r));
    for (const char* extra : { "pitch_bend_range", "voice_amplitude", "filter_1_filter_input", "filter_2_filter_input", "effect_chain_order" })
        controls.push_back (extra);

    std::string json = "{\"presets\":[";
    int bad = 0;
    bool firstPreset = true;
    for (const auto& f : files)
    {
        const std::string text = readFile (f);
        PresetMeta meta;
        ParamValues values = ParamValues::defaults();
        StateContent content;
        if (loadPreset (text.data(), text.size(), meta, values, content).status != LoadStatus::ok)
        {
            std::printf ("FAIL %s: does not load\n", f.filename().string().c_str());
            ++bad;
            continue;
        }

        auto engine = createEngine (EngineKind::vital);
        engine->prepare (48000.0, 256, qualityForTier (Tier::high));
        engine->setContentStore (&store);
        for (size_t i = 0; i < paramCount(); ++i)
            engine->setParam (paramTable()[i].id, values.plain[i]);
        engine->setContent (content);
        std::vector<float> l (64), r (64);
        engine->process (nullptr, 0, l.data(), r.data(), 64);

        std::string entry = firstPreset ? "" : ",";
        firstPreset = false;
        entry += "{\"file\":";
        esc (f.filename().string(), entry);
        entry += ",\"param_mismatches\":[";
        int mism = 0;
        for (size_t i = 0; i < paramCount(); ++i)
        {
            const float got = engine->getParam (paramTable()[i].id);
            if (got != values.plain[i])
            {
                entry += (mism++ ? "," : "");
                entry += "{\"key\":";
                esc (paramTable()[i].key, entry);
                entry += ",\"set\":" + num (values.plain[i]) + ",\"got\":" + num (got) + "}";
            }
        }
        bad += mism;
        entry += "],\"connected_slots\":" + std::to_string (vitalConnectedSlots (*engine)) + ",\"controls\":{";
        bool firstCtl = true;
        for (const auto& c : controls)
        {
            if (! firstCtl)
                entry += ",";
            firstCtl = false;
            esc (c, entry);
            entry += ":" + num (vitalControlValue (*engine, c.c_str()));
        }
        entry += "}}";
        json += entry;
        std::printf ("%-44s param read-back mismatches: %d  connected modulation slots: %d\n", f.filename().string().c_str(), mism,
                     vitalConnectedSlots (*engine));
    }
    json += "]}\n";
    if (! outFile.empty())
    {
        std::ofstream out (outFile, std::ios::binary | std::ios::trunc);
        out << json;
    }
    std::printf ("%zu presets, %d mismatch(es)\n", files.size(), bad);
    return bad ? 1 : 0;
}
