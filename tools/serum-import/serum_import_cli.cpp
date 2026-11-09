// SynthyBoy - serum-import: imports Serum 2 files into .sbpreset files plus a <name>.report.json each.
//
//   synthyboy_serum_import --in FILE_OR_DIR [--out DIR] [--serum-root DIR] [--content-dir DIR] [--report] [--summary FILE.json] [--only EXT]
//
//   --in           a .SerumPreset, .SerumFX / .SerumFXRack (an effect chain), .XferShape (an LFO shape or a curve), a .flac / .wav of an Impulses folder, any
//                  other Serum 2 file (reported as not modelled), or a directory (searched recursively for all of these)
//   --out          output directory (default ./serum-import-out): <name>.sbpreset + <name>.report.json for a preset or chain (the folder structure of a
//                  directory input is kept), <name>.lfoshape.json for a shape
//   --serum-root   the Serum content folder (the parent of Tables / Samples / Multisamples / Impulses) the files of a preset are read from; default: the
//                  nearest ancestor of the input file that has a Tables or Samples folder
//   --content-dir  user content directory for the imported tables, samples and impulses (default OUT/content)
//   --report       additionally print every dropped / approximated entry of each file
//   --summary      write the aggregated report (counts per class, top dropped parameters, failures) as JSON
//   --only         import only files with this extension (e.g. SerumPreset)
//
// Exit code: 0 when every file imported, 1 when one failed, 2 on a usage error.
//
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "synthyboy/engine_caps.h"
#include "synthyboy/serum_plugin_import.h"

namespace fs = std::filesystem;
using namespace synthyboy;

namespace
{
bool readFile (const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream in (p, std::ios::binary);
    if (! in) return false;
    out.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile (const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories (p.parent_path(), ec);
    std::ofstream out (p, std::ios::binary | std::ios::trunc);
    out.write (text.data(), static_cast<std::streamsize> (text.size()));
    return static_cast<bool> (out);
}

const char* kindName (SerumFileKind k)
{
    switch (k)
    {
        case SerumFileKind::preset: return "preset";
        case SerumFileKind::fxModule: return "fx";
        case SerumFileKind::fxRack: return "fxrack";
        case SerumFileKind::shape: return "shape";
        case SerumFileKind::path: return "path";
        case SerumFileKind::clip: return "clip";
        case SerumFileKind::clipBank: return "clipbank";
        case SerumFileKind::arp: return "arp";
        case SerumFileKind::arpBank: return "arpbank";
        case SerumFileKind::style: return "style";
        case SerumFileKind::filterPz: return "pzfilter";
        case SerumFileKind::midiMap: return "midimap";
        case SerumFileKind::impulse: return "impulse";
        case SerumFileKind::audio: return "audio";
        case SerumFileKind::unknown: break;
    }
    return "-";
}

const char* fidelityName (Fidelity f) { return f == Fidelity::exact ? "exact" : f == Fidelity::close ? "close" : "degraded"; }

std::string jsonString (const std::string& s)
{
    std::string out = "\"";
    for (const unsigned char c : s)
    {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); out += b; }
        else out += static_cast<char> (c);
    }
    return out + "\"";
}

std::string normalise (const std::string& path)
{
    std::string out;
    bool inDigits = false;
    for (const char c : path)
    {
        if (std::isdigit (static_cast<unsigned char> (c))) { if (! inDigits) out += '#'; inDigits = true; }
        else { out += c; inDigits = false; }
    }
    return out;
}

struct Totals
{
    int files = 0, ok = 0, failed = 0, notModelled = 0;
    std::map<std::string, int> kinds, fidelity;
    long long imported = 0, approximated = 0, dropped = 0, ignored = 0, defaulted = 0, leaves = 0, unaccounted = 0;
    int missing = 0, stored = 0;
    double maxMs = 0, totalMs = 0;
    std::string slowest;
    std::map<std::string, long long> droppedBy, approxBy, droppedFiles, approxFiles;
    std::vector<std::string> failures;
};

void collect (const fs::path& dir, std::vector<fs::path>& files, const std::string& only)
{
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator (dir, fs::directory_options::skip_permission_denied, ec))
    {
        if (! e.is_regular_file()) continue;
        const std::string name = e.path().filename().string();
        const SerumFileKind k = serumFileKindFromName (name);
        if (k == SerumFileKind::unknown || k == SerumFileKind::audio) continue; // audio files are imported by naming them (an Impulses folder is walked below)
        if (! only.empty() && e.path().extension().string() != "." + only) continue;
        files.push_back (e.path());
    }
    std::sort (files.begin(), files.end());
}
} // namespace

int main (int argc, char** argv)
{
    fs::path in, out = "serum-import-out", contentDir, serumRoot, summary;
    bool report = false;
    std::string only;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--in") in = next();
        else if (a == "--out") out = next();
        else if (a == "--serum-root") serumRoot = next();
        else if (a == "--content-dir") contentDir = next();
        else if (a == "--summary") summary = next();
        else if (a == "--only") only = next();
        else if (a == "--report") report = true;
        else { in.clear(); break; }
    }
    if (in.empty())
    {
        std::fprintf (stderr, "usage: synthyboy_serum_import --in FILE_OR_DIR [--out DIR] [--serum-root DIR] [--content-dir DIR] [--report] [--summary FILE.json] [--only EXT]\n"
                              "  FILE_OR_DIR: .SerumPreset, .SerumFX, .SerumFXRack, .XferShape, an impulse .flac / .wav, any other Serum 2 file, or a directory of them\n");
        return 2;
    }
    if (contentDir.empty()) contentDir = out / "content";
    std::vector<fs::path> files;
    std::error_code ec;
    const bool isDir = fs::is_directory (in, ec);
    if (isDir) collect (in, files, only);
    else if (fs::is_regular_file (in, ec)) files.push_back (in);
    else { std::fprintf (stderr, "not found: %s\n", in.string().c_str()); return 2; }
    // impulses: audio files below an "Impulses" folder of a directory input
    if (isDir && (only.empty() || only == "flac"))
        for (const auto& e : fs::recursive_directory_iterator (in, fs::directory_options::skip_permission_denied, ec))
            if (e.is_regular_file() && serumFileKindFromName (e.path().filename().string()) == SerumFileKind::audio)
            {
                bool inImpulses = false;
                for (const auto& part : e.path()) if (part == "Impulses") inImpulses = true;
                if (inImpulses) files.push_back (e.path());
            }

    fs::create_directories (out, ec);
    fs::create_directories (contentDir, ec);
    UserContentStore store (contentDir.parent_path().string());
    // UserContentStore keeps its files below <dir>/content
    const std::string storeRoot = store.rootDirectory();
    (void) storeRoot;
    const EngineCapsMask caps = kCapUserWavetable | kCapSampleOsc | kCapCustomLfoShape;

    Totals t;
    for (const fs::path& f : files)
    {
        const std::string fileName = f.filename().string();
        ++t.files;
        std::vector<uint8_t> bytes;
        if (! readFile (f, bytes)) { std::printf ("%-60s status=unreadable\n", fileName.c_str()); ++t.failed; t.failures.push_back (fileName + ": unreadable"); continue; }
        const std::string root = serumRoot.empty() ? findSerumContentRoot (f.string()) : serumRoot.string();
        SerumFolderResolver resolver (root);
        std::string hint;
        {
            // the folder below Presets/Factory (Bass, Pad, ...) is the category
            fs::path p = f.parent_path();
            for (fs::path q = p; ! q.empty() && q != q.root_path(); q = q.parent_path())
                if (q.parent_path().filename() == "Factory" && q.parent_path().parent_path().filename() == "Presets") { hint = q.filename().string(); break; }
        }
        const auto t0 = std::chrono::steady_clock::now();
        const SerumPluginImport r = importSerumFileForPlugin (fileName, bytes, store, caps, root.empty() ? nullptr : &resolver, hint);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        t.totalMs += ms;
        if (ms > t.maxMs) { t.maxMs = ms; t.slowest = fileName; }
        const fs::path rel = isDir ? fs::relative (f, in, ec) : fs::path (fileName);
        fs::path base = out / rel;
        base.replace_extension();
        t.kinds[kindName (r.kind)]++;
        if (! r.ok && r.error != "no equivalent in this synthesizer")
        {
            std::printf ("%-60s kind=%s status=FAILED error=\"%s\"\n", fileName.c_str(), kindName (r.kind), r.error.c_str());
            ++t.failed;
            t.failures.push_back (fileName + ": " + r.error);
            continue;
        }
        const bool notModelled = r.kind != SerumFileKind::preset && r.kind != SerumFileKind::fxModule && r.kind != SerumFileKind::fxRack && r.kind != SerumFileKind::shape
                                 && r.kind != SerumFileKind::impulse;
        if (notModelled) ++t.notModelled;
        ++t.ok;
        if (! r.presetText.empty()) writeFile (fs::path (base).concat (".sbpreset"), r.presetText);
        if (r.kind == SerumFileKind::shape) writeFile (fs::path (base).concat (".lfoshape.json"), serializeLfoShapeFile (r.shape));
        if (! r.reportJson.empty()) writeFile (fs::path (base).concat (".report.json"), r.reportJson);
        const SerumImportReport& rep = r.report;
        t.fidelity[fidelityName (rep.fidelity)]++;
        t.imported += rep.numImported;
        t.approximated += rep.numApproximated;
        t.dropped += rep.numDropped;
        t.ignored += rep.numIgnored;
        t.defaulted += rep.numDefaulted;
        t.leaves += rep.leaves;
        t.unaccounted += rep.unaccounted;
        t.missing += r.missingFiles;
        t.stored += r.storedFiles;
        {
            std::set<std::string> seenDropped, seenApprox; // a file counts once per name in the "files" column
            for (const SerumReportEntry& e : rep.entries)
            {
                if (e.kind == ItemKind::dropped)
                {
                    const std::string name = normalise (e.serumPath) + " [" + e.detail + "]";
                    t.droppedBy[name] += e.count;
                    if (seenDropped.insert (name).second) t.droppedFiles[name] += 1;
                }
                if (e.kind == ItemKind::approximated)
                {
                    const std::string name = (e.ourKey.empty() ? normalise (e.serumPath) : normalise (e.ourKey)) + " [" + e.detail + "]";
                    t.approxBy[name] += e.count;
                    if (seenApprox.insert (name).second) t.approxFiles[name] += 1;
                }
            }
        }
        std::printf ("%-60s kind=%s status=ok fidelity=%s imported=%d approximated=%d dropped=%d ignored=%d defaulted=%d unaccounted=%d stored=%d missing=%d %.0fms\n", fileName.c_str(), kindName (r.kind),
                     fidelityName (rep.fidelity), rep.numImported, rep.numApproximated, rep.numDropped, rep.numIgnored, rep.numDefaulted, rep.unaccounted, r.storedFiles, r.missingFiles, ms);
        if (report)
            for (const SerumReportEntry& e : rep.entries)
                if (e.kind == ItemKind::approximated || e.kind == ItemKind::dropped)
                    std::printf ("    %-12s %-10s %s = %s -> %s %s [%s]%s\n", e.kind == ItemKind::dropped ? "dropped" : "approximated", e.area.c_str(), e.serumPath.c_str(), e.serumValue.c_str(),
                                 e.ourKey.c_str(), e.ourValue.c_str(), e.detail.c_str(), e.count > 1 ? (" x" + std::to_string (e.count)).c_str() : "");
    }

    std::printf ("summary: %d files: %d ok (%d without an equivalent), %d failed; leaves=%lld imported=%lld approximated=%lld dropped=%lld ignored=%lld unaccounted=%lld; stored=%d missing=%d; slowest %.0f ms (%s)\n",
                 t.files, t.ok, t.notModelled, t.failed, t.leaves, t.imported, t.approximated, t.dropped, t.ignored, t.unaccounted, t.stored, t.missing, t.maxMs, t.slowest.c_str());
    if (! summary.empty())
    {
        const auto top = [] (const std::map<std::string, long long>& m, const std::map<std::string, long long>& files, size_t n)
        {
            std::vector<std::pair<std::string, long long>> v (m.begin(), m.end());
            std::sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
            std::string s = "[";
            for (size_t i = 0; i < v.size() && i < n; ++i)
            {
                if (i) s += ',';
                const auto it = files.find (v[i].first);
                s += "{\"name\":" + jsonString (v[i].first) + ",\"values\":" + std::to_string (v[i].second) + ",\"files\":" + std::to_string (it == files.end() ? 0 : it->second) + "}";
            }
            return s + "]";
        };
        std::string s = "{\"format\":\"synthyboy-serum-import-summary\",\"version\":1";
        s += ",\"files\":" + std::to_string (t.files) + ",\"ok\":" + std::to_string (t.ok) + ",\"failed\":" + std::to_string (t.failed) + ",\"without_equivalent\":" + std::to_string (t.notModelled);
        s += ",\"kinds\":{";
        bool first = true;
        for (const auto& kv : t.kinds) { s += (first ? "" : ",") + jsonString (kv.first) + ":" + std::to_string (kv.second); first = false; }
        s += "},\"fidelity\":{";
        first = true;
        for (const auto& kv : t.fidelity) { s += (first ? "" : ",") + jsonString (kv.first) + ":" + std::to_string (kv.second); first = false; }
        s += "},\"leaves\":" + std::to_string (t.leaves) + ",\"imported\":" + std::to_string (t.imported) + ",\"approximated\":" + std::to_string (t.approximated) + ",\"dropped\":" + std::to_string (t.dropped)
             + ",\"ignored\":" + std::to_string (t.ignored) + ",\"defaulted\":" + std::to_string (t.defaulted) + ",\"unaccounted\":" + std::to_string (t.unaccounted);
        s += ",\"stored_files\":" + std::to_string (t.stored) + ",\"missing_files\":" + std::to_string (t.missing);
        s += ",\"total_ms\":" + std::to_string (static_cast<long long> (t.totalMs)) + ",\"max_ms\":" + std::to_string (static_cast<long long> (t.maxMs)) + ",\"slowest\":" + jsonString (t.slowest);
        s += ",\"top_dropped\":" + top (t.droppedBy, t.droppedFiles, 60) + ",\"top_approximated\":" + top (t.approxBy, t.approxFiles, 60);
        s += ",\"failures\":[";
        for (size_t i = 0; i < t.failures.size(); ++i) s += (i ? "," : "") + jsonString (t.failures[i]);
        s += "]}";
        writeFile (summary, s);
    }
    return t.failed ? 1 : 0;
}
