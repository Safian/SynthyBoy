// SynthyBoy - vital-import: imports Vital files into .sbpreset files plus a <name>.report.json each.
//
//   synthyboy_vital_import --in FILE_OR_DIR [--out DIR] [--content-dir DIR] [--report] [--duplicates skip|replace|keep]
//
//   --in           a .vital preset, a .vitalbank (a zip of .vital files, read directly), a stand-alone .vitaltable (wavetable) or .vitallfo
//                  (LFO shape), or a directory (searched recursively for all of these)
//   --out          output directory (default ./vital-import-out): <name>.sbpreset + <name>.report.json for a preset, <Bank>/<name>.sbpreset for a
//                  bank, <name>.lfoshape.json for a shape
//   --content-dir  ContentPack directory for the imported wavetables / samples (default OUT/content)
//   --report       additionally print every dropped / approximated item of each preset
//   --duplicates   a name that is taken by a different source: skip (default, keeps both as "Name (2)"), replace, keep (same as skip)
//
// Exit code: 0 when every file imported, 1 when one failed, 2 on a usage error.
//
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

const char* kindName (VitalFileKind k)
{
    switch (k)
    {
        case VitalFileKind::preset: return "preset";
        case VitalFileKind::wavetable: return "wavetable";
        case VitalFileKind::lfo: return "lfo";
        case VitalFileKind::unknown: break;
    }
    return "-";
}

const char* outcomeName (BatchOutcome o)
{
    switch (o)
    {
        case BatchOutcome::written: return "ok";
        case BatchOutcome::skippedSame: return "skipped-same";
        case BatchOutcome::needsDecision: return "needs-decision";
        case BatchOutcome::failed: return "failed";
        case BatchOutcome::ignored: return "ignored";
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
    std::error_code ec;
    fs::create_directories (p.parent_path(), ec);
    std::ofstream out (p, std::ios::binary | std::ios::trunc);
    out.write (text.data(), static_cast<std::streamsize> (text.size()));
    return static_cast<bool> (out);
}

/** PresetStore on a directory: relPath "Bank/Name.sbpreset" is <root>/Bank/Name.sbpreset, the report goes next to it as Name.report.json. */
class DirectoryPresetStore final : public PresetStore
{
public:
    explicit DirectoryPresetStore (fs::path rootDir) : root (std::move (rootDir)) {}

    bool exists (const std::string& name, const std::string& srcTag) override
    {
        const fs::path file = root / (name + ".sbpreset");
        std::error_code ec;
        if (! fs::is_regular_file (file, ec))
            return false;
        if (srcTag.empty())
            return true;
        std::vector<char> bytes;
        return readFile (file, bytes) && std::search (bytes.begin(), bytes.end(), srcTag.begin(), srcTag.end()) != bytes.end();
    }

    bool write (const std::string& relPath, const std::string& text, const std::string& reportJson) override
    {
        const fs::path file = root / relPath;
        if (! writeFile (file, text))
            return false;
        if (reportJson.empty())
            return true;
        fs::path report = file;
        report.replace_extension (".report.json");
        return writeFile (report, reportJson);
    }

private:
    fs::path root;
};

struct Options
{
    fs::path in, out = "vital-import-out", contentDir;
    bool report = false;
    BatchOptions::Duplicate duplicate = BatchOptions::skipSame;
};

struct Totals
{
    int files = 0, written = 0, skipped = 0, needsDecision = 0, failed = 0, ignored = 0;
    int presets = 0, tables = 0, shapes = 0, banks = 0;
    int exact = 0, close = 0, degraded = 0;
    int imported = 0, approximated = 0, dropped = 0, wavetables = 0, samples = 0;
};

void printRow (const BatchRow& r, const Options& o, Totals& t)
{
    ++t.files;
    switch (r.outcome)
    {
        case BatchOutcome::written: ++t.written; break;
        case BatchOutcome::skippedSame: ++t.skipped; break;
        case BatchOutcome::needsDecision: ++t.needsDecision; break;
        case BatchOutcome::failed: ++t.failed; break;
        case BatchOutcome::ignored: ++t.ignored; break;
    }
    if (r.outcome == BatchOutcome::ignored)
    {
        if (! r.error.empty())
            std::printf ("%-60s status=ignored error=\"%s\"\n", r.source.c_str(), r.error.c_str());
        return;
    }
    if (r.outcome == BatchOutcome::written || r.outcome == BatchOutcome::skippedSame)
    {
        if (r.kind == VitalFileKind::preset) ++t.presets;
        else if (r.kind == VitalFileKind::wavetable) ++t.tables;
        else if (r.kind == VitalFileKind::lfo) ++t.shapes;
    }
    if (r.kind == VitalFileKind::preset && r.status == ImportStatus::ok)
    {
        (r.fidelity == Fidelity::exact ? t.exact : r.fidelity == Fidelity::close ? t.close : t.degraded)++;
        t.imported += r.imported;
        t.approximated += r.approximated;
        t.dropped += r.dropped;
        t.wavetables += r.wavetables;
        t.samples += r.samples;
    }

    if (r.outcome == BatchOutcome::failed)
    {
        std::printf ("%-60s kind=%s status=%s error=\"%s\"\n", r.source.c_str(), kindName (r.kind),
                     r.zip != ZipStatus::ok ? zipStatusName (r.zip) : statusName (r.status), r.error.c_str());
        return;
    }
    std::printf ("%-60s kind=%s status=%s", r.source.c_str(), kindName (r.kind), outcomeName (r.outcome));
    if (r.kind == VitalFileKind::preset)
        std::printf (" fidelity=%s imported=%d approximated=%d dropped=%d ignored=%d wavetables=%d samples=%d", fidelityName (r.fidelity),
                     r.imported, r.approximated, r.dropped, r.ignoredItems, r.wavetables, r.samples);
    else if (r.kind == VitalFileKind::wavetable)
        std::printf (" name=\"%s\"", r.name.c_str());
    else if (r.kind == VitalFileKind::lfo)
        std::printf (" name=\"%s\"", r.name.c_str());
    std::printf ("\n");
    (void) o;
}

void collect (const fs::path& dir, std::vector<fs::path>& files)
{
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator (dir, ec))
        if (e.is_regular_file() && (vitalFileKindFromName (e.path().filename().string()) != VitalFileKind::unknown
                                    || isVitalBankName (e.path().filename().string())))
            files.push_back (e.path());
    std::sort (files.begin(), files.end());
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
        else if (a == "--duplicates")
        {
            const std::string v = next();
            if (v == "skip" || v == "keep") o.duplicate = BatchOptions::skipSame;
            else if (v == "replace") o.duplicate = BatchOptions::replace;
            else
            {
                o.in.clear();
                break;
            }
        }
        else
        {
            o.in.clear();
            break;
        }
    }
    if (o.in.empty())
    {
        std::fprintf (stderr, "usage: synthyboy_vital_import --in FILE_OR_DIR [--out DIR] [--content-dir DIR] [--report] [--duplicates skip|replace|keep]\n"
                              "  FILE_OR_DIR: .vital, .vitalbank, .vitaltable, .vitallfo, or a directory of them\n");
        return 2;
    }
    if (o.contentDir.empty())
        o.contentDir = o.out / "content";

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory (o.in, ec))
        collect (o.in, files);
    else if (fs::is_regular_file (o.in, ec))
        files.push_back (o.in);
    else
    {
        std::fprintf (stderr, "not found: %s\n", o.in.string().c_str());
        return 2;
    }

    fs::create_directories (o.out, ec);
    fs::create_directories (o.contentDir, ec);
    UserWavetableStore content (o.contentDir.string());
    DirectoryPresetStore presets (o.out);

    ImportOptions opt;
    opt.caps.customWavetables = true;
    opt.caps.customLfoShapes = true;
    opt.caps.sampleOsc = true;
    opt.caps.noiseSample = true;
    BatchOptions batch;
    batch.duplicate = o.duplicate;

    Totals t;
    for (const auto& f : files)
    {
        const std::string fileName = f.filename().string();
        std::vector<char> bytes;
        if (! readFile (f, bytes))
        {
            std::printf ("%-60s status=unreadable\n", fileName.c_str());
            ++t.failed;
            ++t.files;
            continue;
        }

        if (isVitalBankName (fileName))
        {
            ++t.banks;
            const BatchSummary sum = importVitalBank (bytes.data(), bytes.size(), fileName, opt, batch, content, presets);
            if (sum.zip != ZipStatus::ok)
            {
                std::printf ("%-60s kind=bank status=%s error=\"%s\"\n", fileName.c_str(), zipStatusName (sum.zip), sum.error.c_str());
                ++t.failed;
                ++t.files;
                continue;
            }
            std::printf ("%s: bank, %zu entries\n", fileName.c_str(), sum.rows.size());
            for (const auto& row : sum.rows)
                printRow (row, o, t);
            continue;
        }

        const BatchRow row = importVitalDocument (fileName, fileName, {}, bytes.data(), bytes.size(), opt, batch, content, presets);
        printRow (row, o, t);
        if (o.report && row.kind == VitalFileKind::preset && row.status == ImportStatus::ok)
        {
            // the full report is next to the preset (<name>.report.json); the items are printed from a second pass over the same bytes
            ImportOptions again = opt;
            const ImportResult r = importVitalPreset (bytes.data(), bytes.size(), again);
            for (const auto& it : r.report.items)
                if (it.kind == ItemKind::approximated || it.kind == ItemKind::dropped)
                    std::printf ("    %-12s reason=%-2d %-8s %s = %s -> %s %s [%s]\n", it.kind == ItemKind::dropped ? "dropped" : "approximated",
                                 static_cast<int>(it.reason), it.area.c_str(), it.vitalName.c_str(), it.vitalValue.c_str(), it.ourKey.c_str(),
                                 it.ourValue.c_str(), it.detail.c_str());
        }
    }

    std::printf ("summary: %d input files (%d banks), %d entries: %d presets, %d wavetables, %d lfo shapes imported; %d skipped (same source), "
                 "%d need a decision, %d failed, %d ignored entries\n",
                 static_cast<int> (files.size()), t.banks, t.files, t.presets, t.tables, t.shapes, t.skipped, t.needsDecision, t.failed, t.ignored);
    if (t.presets > 0)
        std::printf ("presets: fidelity exact=%d close=%d degraded=%d; items imported=%d approximated=%d dropped=%d; wavetables=%d samples=%d\n",
                     t.exact, t.close, t.degraded, t.imported, t.approximated, t.dropped, t.wavetables, t.samples);
    return t.failed ? 1 : 0;
}
