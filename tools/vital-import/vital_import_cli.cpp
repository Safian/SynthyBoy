// SynthyBoy - vital-import: imports Vital presets (.vital) into .sbpreset files plus a <name>.report.json each.
//
//   synthyboy_vital_import --in FILE_OR_DIR [--out DIR] [--content-dir DIR] [--report]
//
//   --in           a .vital file, or a directory (searched recursively for .vital files)
//   --out          output directory for <name>.sbpreset and <name>.report.json (default ./vital-import-out)
//   --content-dir  ContentPack directory for the imported wavetables / samples (default OUT/content)
//   --report       additionally print every dropped / approximated item of each preset
//
// A .vitalbank is a zip archive and there is no zip reader in the tree: unzip banks first (unzip BANK -d DIR) and pass DIR.
// Exit code: 0 when every preset imported, 1 when one failed, 2 on a usage error.
//
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "synthyboy/vital_import.h"

namespace fs = std::filesystem;
using namespace synthyboy;

namespace
{
const char* statusName (ImportStatus s)
{
    switch (s)
    {
        case ImportStatus::ok: return "ok";
        case ImportStatus::notVital: return "not-vital";
        case ImportStatus::malformed: return "malformed";
        case ImportStatus::tooLarge: return "too-large";
        case ImportStatus::cancelled: return "cancelled";
        case ImportStatus::budgetExceeded: return "budget-exceeded";
    }
    return "?";
}

const char* fidelityName (Fidelity f)
{
    switch (f)
    {
        case Fidelity::exact: return "exact";
        case Fidelity::close: return "close";
        case Fidelity::degraded: return "degraded";
    }
    return "?";
}

const char* kindName (ItemKind k)
{
    switch (k)
    {
        case ItemKind::imported: return "imported";
        case ItemKind::approximated: return "approximated";
        case ItemKind::dropped: return "dropped";
        case ItemKind::ignored: return "ignored";
    }
    return "?";
}

bool readFile (const fs::path& p, std::vector<char>& out)
{
    std::ifstream in (p, std::ios::binary);
    if (! in)
        return false;
    out.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile (const fs::path& p, const std::string& text)
{
    std::ofstream out (p, std::ios::binary | std::ios::trunc);
    out.write (text.data(), static_cast<std::streamsize> (text.size()));
    return static_cast<bool> (out);
}

std::string safeStem (const fs::path& p)
{
    std::string s = p.stem().string();
    for (auto& c : s)
        if (! (std::isalnum (static_cast<unsigned char> (c)) || c == '-' || c == '_' || c == '.'))
            c = '_';
    return s.empty() ? std::string ("preset") : s;
}

struct Options
{
    fs::path in, out = "vital-import-out", contentDir;
    bool report = false;
};

/** Imports one file; returns true when the preset was imported and written. */
bool importOne (const fs::path& file, const Options& o, UserWavetableStore& store, std::set<std::string>& usedNames)
{
    const std::string fileName = file.filename().string();
    std::vector<char> bytes;
    if (! readFile (file, bytes))
    {
        std::printf ("%-40s status=unreadable\n", fileName.c_str());
        return false;
    }

    const std::string stem = safeStem (file);
    ImportOptions opt;
    opt.caps.customWavetables = true;
    opt.caps.customLfoShapes = true;
    opt.caps.sampleOsc = true;
    opt.fallbackName = stem.c_str();
    const ImportResult r = importVitalPreset (bytes.data(), bytes.size(), opt);

    if (r.status != ImportStatus::ok)
    {
        std::printf ("%-40s status=%s error=\"%s\"\n", fileName.c_str(), statusName (r.status), r.error.c_str());
        return false;
    }

    ContentRefs refs;
    bool stored = true;
    for (const auto& wt : r.wavetables)
    {
        std::string pack, name;
        if (store.addWavetable (wt, pack, name))
            refs.wavetables.push_back ({ wt.osc, pack, name });
        else
            stored = false;
    }
    for (const auto& s : r.samples)
    {
        std::string pack, name;
        if (store.addSample (s, pack, name))
            refs.samples.push_back ({ s.osc, pack, name });
        else
            stored = false;
    }

    std::string base = stem;
    for (int n = 2; ! usedNames.insert (base).second; ++n)
        base = stem + "_" + std::to_string (n);

    const bool wrote = writeFile (o.out / (base + ".sbpreset"), serializeImportedPreset (r, r.meta, refs))
                       && writeFile (o.out / (base + ".report.json"), r.report.toJson());

    std::string unknown;
    for (const auto& k : r.unknownKeys)
        unknown += (unknown.empty() ? "" : ",") + k;

    std::printf ("%-40s status=%s fidelity=%s imported=%d approximated=%d dropped=%d ignored=%d unknown_keys=%d wavetables=%zu samples=%zu",
                 fileName.c_str(), wrote && stored ? "ok" : "write-failed", fidelityName (r.report.fidelity), r.report.numImported,
                 r.report.numApproximated, r.report.numDropped, r.report.numIgnored, r.report.vitalUnknownKeys, r.wavetables.size(),
                 r.samples.size());
    if (! unknown.empty())
        std::printf (" unknown=%s", unknown.c_str());
    std::printf ("\n");

    if (o.report)
        for (const auto& it : r.report.items)
            if (it.kind == ItemKind::approximated || it.kind == ItemKind::dropped)
                std::printf ("    %-12s reason=%-2d %-8s %s = %s -> %s %s [%s]\n", kindName (it.kind), static_cast<int> (it.reason),
                             it.area.c_str(), it.vitalName.c_str(), it.vitalValue.c_str(), it.ourKey.c_str(), it.ourValue.c_str(),
                             it.detail.c_str());

    return wrote && stored;
}
} // namespace

int main (int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--in") o.in = next();
        else if (a == "--out") o.out = next();
        else if (a == "--content-dir") o.contentDir = next();
        else if (a == "--report") o.report = true;
        else
        {
            o.in.clear();
            break;
        }
    }
    if (o.in.empty())
    {
        std::fprintf (stderr, "usage: synthyboy_vital_import --in FILE_OR_DIR [--out DIR] [--content-dir DIR] [--report]\n"
                              "  (a .vitalbank is a zip: unzip it first and pass the directory)\n");
        return 2;
    }
    if (o.contentDir.empty())
        o.contentDir = o.out / "content";

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory (o.in, ec))
    {
        for (const auto& e : fs::recursive_directory_iterator (o.in, ec))
            if (e.is_regular_file() && e.path().extension() == ".vital")
                files.push_back (e.path());
        std::sort (files.begin(), files.end());
    }
    else if (fs::is_regular_file (o.in, ec))
    {
        if (o.in.extension() == ".vitalbank")
        {
            std::fprintf (stderr, "%s is a zip archive; no zip reader is built in. Unzip it and pass the directory.\n", o.in.string().c_str());
            return 2;
        }
        files.push_back (o.in);
    }
    else
    {
        std::fprintf (stderr, "not found: %s\n", o.in.string().c_str());
        return 2;
    }

    fs::create_directories (o.out, ec);
    fs::create_directories (o.contentDir, ec);
    UserWavetableStore store (o.contentDir.string());
    std::set<std::string> usedNames;
    int failed = 0;
    for (const auto& f : files)
        if (! importOne (f, o, store, usedNames))
            ++failed;

    std::printf ("%zu presets, %d failed\n", files.size(), failed);
    return failed ? 1 : 0;
}
