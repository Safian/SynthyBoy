// The preset index (committed content/presets/index.json, shell-mpc/program_list.h loadPrograms): the programs and their ORDER from the index are exactly those of the scan, the index is used when current,
// and a stale, damaged or missing index falls back to the scan (same list again).
// Usage: preset_index_host <repo>/content/presets
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "program_list.h"

namespace fs = std::filesystem;
using namespace synthyboy_mpc;

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } }
static bool same (const std::vector<ProgramEntry>& a, const std::vector<ProgramEntry>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].category != b[i].category || a[i].path != b[i].path)
            return false;
    return true;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: preset_index_host <presets dir with the committed index.json>\n"); return 2; }
    const fs::path tmp = fs::temp_directory_path() / ("sb-preset-index-" + std::to_string (static_cast<long> (getpid())));
    std::error_code ec;
    fs::remove_all (tmp, ec);
    fs::create_directories (tmp, ec);
    const std::string dir = (tmp / "presets").string();
    fs::copy (argv[1], dir, fs::copy_options::recursive, ec);
    check (! ec, "the presets were copied");
    check (fs::exists (fs::path (dir) / "index.json"), "the committed index.json was copied");

    const auto scanned = scanPrograms (dir);
    check (scanned.size() == 184, "the scan finds the 184 factory presets");
    std::vector<ProgramEntry> fromIndex;
    std::string why;
    check (loadPresetIndex (dir, fromIndex, why) && why == "index", "a current index is used");
    check (same (scanned, fromIndex), "program order, names, categories and paths are exactly those of the scan");
    std::string source;
    check (same (loadPrograms (dir, &source), scanned) && source == "index", "loadPrograms takes the index");

    // a preset file added: the listing no longer matches, the scan decides (and finds the new one)
    fs::copy_file (fs::path (dir) / "Bass" / "808_boom.sbpreset", fs::path (dir) / "Bass" / "extra_copy.sbpreset", ec);
    const auto withExtra = loadPrograms (dir, &source);
    check (source.rfind ("scan", 0) == 0 && withExtra.size() == 161 && same (withExtra, scanPrograms (dir)), "a new file makes the index stale: the scan is used");
    fs::remove (fs::path (dir) / "Bass" / "extra_copy.sbpreset", ec);
    check (loadPresetIndex (dir, fromIndex, why), "the index is current again");

    // a changed size (a preset edited in place)
    {
        std::ofstream f ((fs::path (dir) / "Bass" / "808_boom.sbpreset").string(), std::ios::binary | std::ios::app);
        f << " ";
    }
    check (! loadPresetIndex (dir, fromIndex, why) && source.size() > 0, "a changed size makes the index stale");
    check (same (loadPrograms (dir, &source), scanPrograms (dir)) && source.rfind ("scan", 0) == 0, "... and the scan is used");

    // damaged and missing index
    const fs::path indexPath = fs::path (dir) / "index.json";
    {
        std::ofstream f (indexPath.string(), std::ios::binary | std::ios::trunc);
        f << "{\"index_version\":1,\"count\":2,\"listing_hash\":\"0000000000000000\",\"presets\":[\nnot json";
    }
    check (! loadPresetIndex (dir, fromIndex, why), "a damaged index is refused");
    check (same (loadPrograms (dir, &source), scanPrograms (dir)) && source.rfind ("scan", 0) == 0, "... and the scan is used");
    fs::remove (indexPath, ec);
    check (! loadPresetIndex (dir, fromIndex, why) && why == "no index", "no index: refused");
    check (same (loadPrograms (dir, &source), scanPrograms (dir)), "... and the scan is used");

    fs::remove_all (tmp, ec);
    if (fails == 0)
        std::printf ("preset_index_host: all passed\n");
    return fails == 0 ? 0 : 1;
}
