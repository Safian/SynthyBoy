// SynthyBoy - core: Vital import inside the plugin (vital_plugin_import.h) and the `.lfoshape.json` loader.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/vital_plugin_import.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>

#include "vital_import_internal.h"

namespace synthyboy
{
namespace fs = std::filesystem;

namespace
{
std::string stemOfName (const std::string& name)
{
    return fileStemOf (name);
}

bool writeTextFile (const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories (path.parent_path(), ec);
    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    if (! out)
        return false;
    out.write (text.data(), static_cast<std::streamsize> (text.size()));
    return out.good();
}

/** PresetStore on the user preset directory: "Bank/Name.sbpreset" and the report next to it as "Name.report.json". */
class FilePresetStore final : public PresetStore
{
public:
    explicit FilePresetStore (fs::path rootDir) : root (std::move (rootDir)) {}

    bool exists (const std::string& name, const std::string& srcTag) override
    {
        const fs::path file = root / (name + ".sbpreset");
        std::error_code ec;
        if (! fs::is_regular_file (file, ec))
            return false;
        if (srcTag.empty())
            return true;
        std::ifstream in (file, std::ios::binary);
        const std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        return text.find (srcTag) != std::string::npos;
    }

    bool write (const std::string& relPath, const std::string& text, const std::string& reportJson) override
    {
        const fs::path file = root / relPath;
        if (! writeTextFile (file, text))
            return false;
        if (reportJson.empty())
            return true;
        fs::path report = file;
        report.replace_extension (".report.json");
        return writeTextFile (report, reportJson);
    }

private:
    fs::path root;
};
} // namespace

ImportCaps importCapsFor (EngineCapsMask caps) noexcept
{
    ImportCaps c;
    c.customWavetables = (caps & kCapUserWavetable) != 0;
    c.customLfoShapes = (caps & kCapCustomLfoShape) != 0;
    c.sampleOsc = (caps & kCapSampleOsc) != 0;
    c.noiseSample = (caps & kCapNoiseSample) != 0;
    return c;
}

std::vector<std::string> reportLines (const ImportReport& report, size_t maxLines)
{
    static const char* const kinds[] = { "imported", "approximated", "dropped", "ignored" };
    std::vector<std::string> lines;
    for (const ReportItem& item : report.items)
    {
        if (lines.size() >= maxLines)
            break;
        std::string line = kinds[std::min<size_t> (static_cast<size_t> (item.kind), 3)];
        line += ": ";
        line += item.ourKey.empty() ? item.vitalName : item.ourKey;
        if (! item.vitalValue.empty())
            line += " (" + item.vitalValue + (item.ourValue.empty() ? "" : " -> " + item.ourValue) + ")";
        if (! item.detail.empty())
            line += "  " + item.detail;
        lines.push_back (std::move (line));
    }
    return lines;
}

VitalPluginImport importVitalFileForPlugin (const std::string& fileName, const std::vector<uint8_t>& bytes, UserContentStore& store,
                                            const std::string& userDir, EngineCapsMask caps)
{
    VitalPluginImport out;
    try
    {
        if (store.rootDirectory().empty())
        {
            out.error = "no user directory";
            return out;
        }
        const std::string stem = stemOfName (fileName);
        ImportOptions options;
        options.caps = importCapsFor (caps);
        options.fallbackName = stem.c_str();
        UserWavetableStore pack ((fs::path (store.rootDirectory()) / kPackImportedVital).string());

        if (isVitalBankName (fileName))
        {
            out.bank = true;
            out.name = stem;
            out.presetDirectory = (fs::path (userDir) / "presets").string();
            FilePresetStore presets (out.presetDirectory);
            BatchOptions batch;
            batch.duplicate = BatchOptions::skipSame;
            out.summary = importVitalBank (bytes.data(), bytes.size(), fileName, options, batch, pack, presets);
            out.ok = out.summary.zip == ZipStatus::ok && out.summary.written + out.summary.skippedSame > 0;
            if (! out.ok)
                out.error = out.summary.error.empty() ? std::string ("the bank holds no Vital preset") : out.summary.error;
            return out;
        }

        out.kind = vitalFileKindFromName (fileName);
        if (out.kind == VitalFileKind::unknown)
        {
            // a file without a Vital extension: the content decides (a renamed .vital)
            if (looksLikeVitalPreset (bytes.data(), bytes.size()))
                out.kind = VitalFileKind::preset;
            else
            {
                out.error = "not a Vital file";
                return out;
            }
        }

        if (out.kind == VitalFileKind::wavetable)
        {
            const ImportResult r = importVitalWavetableFile (bytes.data(), bytes.size(), options);
            if (r.status != ImportStatus::ok || r.wavetables.empty())
            {
                out.error = r.error.empty() ? std::string ("the table did not import") : r.error;
                return out;
            }
            const ImportedWavetable& wt = r.wavetables.front();
            out.name = wt.name.empty() ? stem : wt.name;
            // the user library keeps the table as a 16-bit WAV like every saved table
            std::vector<uint8_t> wav = wt.wavBytes;
            if (wav.empty())
            {
                out.error = "the table has no frames";
                return out;
            }
            if (! store.addFile (ContentKind::wavetable, out.name, wav, out.tableRef))
            {
                out.error = "the content store refused the table";
                return out;
            }
            out.tableFrames = wt.numFrames;
            out.tableAuthor = wt.author;
            out.ok = true;
            return out;
        }

        if (out.kind == VitalFileKind::lfo)
        {
            const ImportResult r = importVitalLfoFile (bytes.data(), bytes.size(), options);
            if (r.status != ImportStatus::ok || r.lfoShapes.empty())
            {
                out.error = r.error.empty() ? std::string ("the shape did not import") : r.error;
                return out;
            }
            const ImportedLfoShape& shape = r.lfoShapes.front();
            out.lfoShape.name = shape.name.empty() ? stem : shape.name;
            out.lfoShape.smooth = shape.smooth;
            for (const auto& p : shape.points)
                out.lfoShape.points.emplace_back (p[0], p[1], std::max (-20.0f, std::min (20.0f, p[2])));
            out.name = out.lfoShape.name;
            out.ok = out.lfoShape.points.size() >= 2;
            if (! out.ok)
                out.error = "the shape has fewer than two points";
            return out;
        }

        // a preset
        ImportResult r = importVitalPreset (bytes.data(), bytes.size(), options);
        out.report = r.report;
        out.reportJson = r.report.toJson();
        if (r.status != ImportStatus::ok)
        {
            out.error = r.error.empty() ? std::string ("the preset did not import") : r.error;
            return out;
        }
        PresetMeta meta = r.meta;
        if (meta.name.empty())
            meta.name = stem;
        out.name = meta.name;
        ContentRefs refs;
        for (const auto& wt : r.wavetables)
        {
            std::string packId, file;
            if (! pack.addWavetable (wt, packId, file))
            {
                out.error = "the content store refused a wavetable";
                return out;
            }
            refs.wavetables.push_back ({ wt.osc, packId, file });
        }
        for (const auto& smp : r.samples)
        {
            std::string packId, file;
            if (! pack.addSample (smp, packId, file))
            {
                out.error = "the content store refused a sample";
                return out;
            }
            refs.samples.push_back ({ smp.osc, packId, file });
        }
        const std::string text = serializeImportedPreset (r, meta, refs);
        PresetMeta loadedMeta;
        if (text.empty() || loadPreset (text.data(), text.size(), loadedMeta, out.values, out.content).status != LoadStatus::ok)
        {
            out.error = "the preset did not serialise";
            return out;
        }
        out.meta = loadedMeta;
        for (const auto& n : r.macroNames)
            out.macroNames.push_back (n);
        out.ok = true;
    }
    catch (const std::bad_alloc&)
    {
        out.ok = false;
        out.error = "out of memory";
    }
    catch (...)
    {
        out.ok = false;
        out.error = "internal error";
    }
    return out;
}

bool parseLfoShapeFile (const std::string& text, LfoShapeFile& out)
{
    using namespace vitalimport;
    json::Doc doc;
    if (text.empty() || text.size() > 1u << 20 || json::parse (text.data(), text.size(), 8, 8u << 20, doc) != json::Status::ok || doc.nodes.empty()
        || doc.root().type != json::Type::object)
        return false;
    const int format = doc.find (0, "format");
    if (format < 0 || doc.nodes[static_cast<size_t> (format)].type != json::Type::string || doc.text (doc.nodes[static_cast<size_t> (format)].str) != "synthyboy-lfo-shape")
        return false;
    LfoShapeFile shape;
    const int name = doc.find (0, "name");
    if (name >= 0 && doc.nodes[static_cast<size_t> (name)].type == json::Type::string)
        shape.name = cleanText (doc.text (doc.nodes[static_cast<size_t> (name)].str), kMaxContentStringBytes, true);
    const int smooth = doc.find (0, "smooth");
    shape.smooth = smooth >= 0 && doc.nodes[static_cast<size_t> (smooth)].type == json::Type::boolean && doc.nodes[static_cast<size_t> (smooth)].boolean;
    const int points = doc.find (0, "points");
    if (points < 0 || doc.nodes[static_cast<size_t> (points)].type != json::Type::array)
        return false;
    const auto number = [&] (int obj, const char* key, double def)
    {
        const int n = doc.find (obj, key);
        return n >= 0 && doc.nodes[static_cast<size_t> (n)].type == json::Type::number && std::isfinite (doc.nodes[static_cast<size_t> (n)].number)
                   ? doc.nodes[static_cast<size_t> (n)].number : def;
    };
    for (int e = doc.nodes[static_cast<size_t> (points)].first; e >= 0; e = doc.nodes[static_cast<size_t> (e)].next)
    {
        if (doc.nodes[static_cast<size_t> (e)].type != json::Type::object || static_cast<int> (shape.points.size()) >= kMaxLfoShapePoints)
            return false;
        ContentPoint p;
        p.x = static_cast<float> (std::max (0.0, std::min (1.0, number (e, "x", 0.0))));
        p.y = static_cast<float> (std::max (0.0, std::min (1.0, number (e, "y", 0.0))));
        p.power = static_cast<float> (std::max (-kMaxPointPower, std::min (kMaxPointPower, number (e, "power", 0.0))));
        if (! shape.points.empty() && p.x < shape.points.back().x)
            return false;
        shape.points.push_back (p);
    }
    if (shape.points.size() < 2)
        return false;
    out = std::move (shape);
    return true;
}
} // namespace synthyboy
