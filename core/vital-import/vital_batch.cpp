// SynthyBoy - core: batch import of Vital files and .vitalbank archives (docs/ui-design/11-vital-preset-import.md 2.6).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The core never touches the file system here: the host hands over bytes and implements ContentStore / PresetStore.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <new>

#include "synthyboy/vital_import.h"

namespace synthyboy
{
namespace
{
std::string lowerExtension (const std::string& name)
{
    // the extension after the last '.' of the last path segment
    const size_t slash = name.find_last_of ("/\\");
    const size_t dot = name.find_last_of ('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::string ext = name.substr (dot);
    for (auto& c : ext)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return ext;
}

std::string stemOf (const std::string& name)
{
    const size_t slash = name.find_last_of ("/\\");
    std::string leaf = slash == std::string::npos ? name : name.substr (slash + 1);
    const size_t dot = leaf.find_last_of ('.');
    return dot == std::string::npos || dot == 0 ? leaf : leaf.substr (0, dot);
}

/** A file name from free text: letters, digits, - _ . ( ) and space survive, anything else becomes '_' ; no leading dot, at most 80 bytes. */
std::string safeFileName (const std::string& text, const std::string& fallback)
{
    std::string s;
    for (const char c : text)
    {
        const auto u = static_cast<unsigned char> (c);
        if (std::isalnum (u) || c == '-' || c == '_' || c == '.' || c == '(' || c == ')' || c == ' ')
            s += c;
        else if (u >= 0x80)
            s += c; // UTF-8 bytes of a name are kept
        else
            s += '_';
        if (s.size() >= 80)
            break;
    }
    while (! s.empty() && (s.front() == '.' || s.front() == ' '))
        s.erase (s.begin());
    while (! s.empty() && (s.back() == '.' || s.back() == ' '))
        s.pop_back();
    return s.empty() ? (fallback.empty() ? std::string ("preset") : fallback) : s;
}

void escapeJson (const std::string& s, std::string& out)
{
    out += '"';
    for (const char c : s)
    {
        const auto u = static_cast<unsigned char> (c);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (u < 0x20)
        {
            char buf[8];
            std::snprintf (buf, sizeof buf, "\\u%04x", u);
            out += buf;
        }
        else
            out += c;
    }
    out += '"';
}

void fillCounts (BatchRow& row, const ImportResult& r)
{
    row.status = r.status;
    row.fidelity = r.report.fidelity;
    row.imported = r.report.numImported;
    row.approximated = r.report.numApproximated;
    row.dropped = r.report.numDropped;
    row.ignoredItems = r.report.numIgnored;
    row.wavetables = static_cast<int> (r.wavetables.size());
    row.samples = static_cast<int> (r.samples.size());
    row.lfoShapes = static_cast<int> (r.lfoShapes.size());
}

std::string joinPath (const std::string& folder, const std::string& leaf)
{
    return folder.empty() ? leaf : folder + "/" + leaf;
}
} // namespace

VitalFileKind vitalFileKindFromName (const std::string& fileName) noexcept
{
    try
    {
        const std::string ext = lowerExtension (fileName);
        if (ext == ".vital") return VitalFileKind::preset;
        if (ext == ".vitaltable") return VitalFileKind::wavetable;
        if (ext == ".vitallfo") return VitalFileKind::lfo;
    }
    catch (...) {}
    return VitalFileKind::unknown;
}

bool isVitalBankName (const std::string& fileName) noexcept
{
    try { return lowerExtension (fileName) == ".vitalbank"; }
    catch (...) { return false; }
}

std::string serializeImportedLfoShape (const ImportedLfoShape& shape)
{
    std::string s = "{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":";
    escapeJson (shape.name, s);
    s += ",\"smooth\":";
    s += shape.smooth ? "true" : "false";
    s += ",\"points\":[";
    for (size_t i = 0; i < shape.points.size(); ++i)
    {
        char buf[96];
        const float power = std::max (-20.0f, std::min (20.0f, shape.points[i][2]));
        std::snprintf (buf, sizeof buf, "%s{\"x\":%.9g,\"y\":%.9g,\"power\":%.9g}", i > 0 ? "," : "", static_cast<double> (shape.points[i][0]),
                       static_cast<double> (shape.points[i][1]), static_cast<double> (power));
        s += buf;
    }
    s += "]}";
    return s;
}

BatchRow importVitalDocument (const std::string& source, const std::string& fileName, const std::string& folder, const void* data, size_t size,
                              const ImportOptions& options, const BatchOptions& batch, ContentStore& content, PresetStore& presets) noexcept
{
    BatchRow row;
    try
    {
        row.source = source;
        row.kind = vitalFileKindFromName (fileName);
        if (row.kind == VitalFileKind::unknown)
        {
            row.outcome = BatchOutcome::ignored;
            return row;
        }

        const std::string stem = stemOf (fileName);
        ImportOptions opt = options;
        opt.fallbackName = stem.c_str();

        if (row.kind == VitalFileKind::wavetable)
        {
            const ImportResult r = importVitalWavetableFile (data, size, opt);
            fillCounts (row, r);
            if (r.status != ImportStatus::ok || r.wavetables.empty())
            {
                row.error = r.error;
                return row;
            }
            const ImportedWavetable& wt = r.wavetables.front();
            row.name = wt.name.empty() ? stem : wt.name;
            std::string pack, file;
            if (! content.addWavetable (wt, pack, file))
            {
                row.error = "the content store refused the wavetable";
                return row;
            }
            row.relPath = pack + "/" + file;
            row.outcome = BatchOutcome::written;
            return row;
        }

        if (row.kind == VitalFileKind::lfo)
        {
            const ImportResult r = importVitalLfoFile (data, size, opt);
            fillCounts (row, r);
            if (r.status != ImportStatus::ok || r.lfoShapes.empty())
            {
                row.error = r.error;
                return row;
            }
            ImportedLfoShape shape = r.lfoShapes.front();
            if (shape.name.empty())
                shape.name = stem;
            row.name = shape.name;
            row.relPath = joinPath (folder, safeFileName (shape.name, stem) + ".lfoshape.json");
            if (! presets.write (row.relPath, serializeImportedLfoShape (shape), {}))
            {
                row.error = "the store refused the LFO shape";
                return row;
            }
            row.outcome = BatchOutcome::written;
            return row;
        }

        // a preset
        ImportResult r = importVitalPreset (data, size, opt);
        fillCounts (row, r);
        if (r.status != ImportStatus::ok)
        {
            row.error = r.error;
            return row;
        }

        PresetMeta meta = r.meta;
        if (meta.name.empty())
            meta.name = stem;
        if (! batch.useVitalStyle || r.categoryNeedsChoice)
            meta.category = batch.unknownStyleCategory;
        row.name = meta.name;

        const std::string srcTag = "src:" + r.sourceSha256.substr (0, 12);
        std::string fileStem = safeFileName (meta.name, stem);

        // the store is asked with the path inside the store, without the extension ("Bank/Name"), so two banks may hold the same name
        const auto taken = [&] (const std::string& stem, const std::string& tag) { return presets.exists (joinPath (folder, stem), tag); };
        if (taken (fileStem, srcTag) && batch.duplicate != BatchOptions::replace)
        {
            row.outcome = BatchOutcome::skippedSame;
            row.relPath = joinPath (folder, fileStem + ".sbpreset");
            return row;
        }
        if (taken (fileStem, {}) && ! taken (fileStem, srcTag))
        {
            if (batch.duplicate == BatchOptions::ask)
            {
                row.outcome = BatchOutcome::needsDecision;
                row.relPath = joinPath (folder, fileStem + ".sbpreset");
                return row;
            }
            if (batch.duplicate != BatchOptions::replace) // skipSame and keepBoth: nothing of the other preset is lost
            {
                const std::string base = fileStem;
                for (int n = 2; n < 10000 && taken (fileStem, {}); ++n)
                    fileStem = base + " (" + std::to_string (n) + ")";
            }
        }

        // the wavetables / samples first (never stored twice: the stores key them by SHA-256)
        ContentRefs refs;
        for (const auto& wt : r.wavetables)
        {
            std::string pack, file;
            if (! content.addWavetable (wt, pack, file)) // the store keys the tables by the SHA-256 of the file: an identical one is answered, not stored twice
            {
                row.error = "the content store refused a wavetable";
                return row;
            }
            refs.wavetables.push_back ({ wt.osc, pack, file });
        }
        for (const auto& smp : r.samples)
        {
            std::string pack, file;
            if (! content.addSample (smp, pack, file))
            {
                row.error = "the content store refused a sample";
                return row;
            }
            refs.samples.push_back ({ smp.osc, pack, file });
        }

        const std::string text = serializeImportedPreset (r, meta, refs);
        if (text.empty())
        {
            row.error = "the preset did not serialise";
            return row;
        }
        row.relPath = joinPath (folder, fileStem + ".sbpreset");
        if (! presets.write (row.relPath, text, r.report.toJson()))
        {
            row.error = "the store refused the preset";
            return row;
        }
        row.outcome = BatchOutcome::written;
    }
    catch (const std::bad_alloc&)
    {
        row.outcome = BatchOutcome::failed;
        row.error = "out of memory";
    }
    catch (...)
    {
        row.outcome = BatchOutcome::failed;
        row.error = "internal error";
    }
    return row;
}

BatchSummary importVitalBank (const void* data, size_t size, const std::string& bankName, const ImportOptions& options, const BatchOptions& batch,
                              ContentStore& content, PresetStore& presets, const ZipLimits& limits) noexcept
{
    BatchSummary sum;
    try
    {
        ZipArchive zip;
        sum.zip = zip.open (data, size, limits);
        if (sum.zip != ZipStatus::ok)
        {
            sum.error = std::string ("not a readable archive: ") + zipStatusName (sum.zip);
            return sum;
        }

        const std::string folder = safeFileName (stemOf (bankName), "bank");
        std::vector<uint8_t> bytes;
        const auto& entries = zip.entries();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const ZipEntry& e = entries[i];
            const std::string source = bankName + "/" + e.name;
            BatchRow row;
            if (e.directory || ! e.safePath || vitalFileKindFromName (e.name) == VitalFileKind::unknown)
            {
                row.source = source;
                row.outcome = BatchOutcome::ignored;
                if (! e.directory && ! e.safePath)
                    row.error = "unsafe path in the archive";
            }
            else if (const ZipStatus zs = zip.extract (i, bytes); zs != ZipStatus::ok)
            {
                row.source = source;
                row.kind = vitalFileKindFromName (e.name);
                row.outcome = BatchOutcome::failed;
                row.zip = zs;
                row.error = std::string ("cannot read the entry: ") + zipStatusName (zs);
            }
            else
            {
                row = importVitalDocument (source, e.name, folder, bytes.data(), bytes.size(), options, batch, content, presets);
            }

            switch (row.outcome)
            {
                case BatchOutcome::written: ++sum.written; break;
                case BatchOutcome::skippedSame: ++sum.skippedSame; break;
                case BatchOutcome::needsDecision: ++sum.needsDecision; break;
                case BatchOutcome::failed: ++sum.failed; break;
                case BatchOutcome::ignored: ++sum.ignored; break;
            }
            sum.rows.push_back (std::move (row));
        }
    }
    catch (...)
    {
        sum.error = "internal error";
        ++sum.failed;
    }
    return sum;
}
} // namespace synthyboy
