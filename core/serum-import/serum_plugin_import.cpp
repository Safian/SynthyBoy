// SynthyBoy - core: Serum 2 import inside the plugin (serum_plugin_import.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/serum_plugin_import.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>

namespace synthyboy
{
namespace fs = std::filesystem;

bool SerumFolderResolver::read (const std::string& area, const std::string& relPath, std::vector<uint8_t>& bytes)
{
    std::string sub;
    if (area == "Tables") sub = "Tables";
    else if (area == "Samples") sub = "Samples";
    else if (area == "Multisamples") sub = "Multisamples";
    else if (area == "Impulses") sub = "Impulses";
    else if (area == "Noises") sub = "Samples/Factory Non-Tonal/Noises";
    else return false;
    // the path of the preset is relative to the folder of its area and may climb ("Factory/Drums/../../../Samples/..."): it is resolved lexically
    // and must stay inside the Serum content folder
    std::vector<std::string> parts;
    {
        std::string comp;
        const auto push = [&] (const std::string& c)
        {
            if (c.empty() || c == ".") return true;
            if (c == "..") { if (parts.empty()) return false; parts.pop_back(); return true; }
            parts.push_back (c);
            return true;
        };
        std::string base = sub + "/" + relPath;
        for (size_t i = 0; i <= base.size(); ++i)
        {
            if (i == base.size() || base[i] == '/' || base[i] == '\\')
            {
                if (! push (comp)) return false;
                comp.clear();
            }
            else comp += base[i];
        }
    }
    fs::path rel;
    for (const std::string& c : parts) rel /= c;
    if (rel.empty()) return false;
    std::error_code ec;
    const fs::path file = fs::path (root_) / rel;
    if (! fs::is_regular_file (file, ec)) return false;
    if (fs::file_size (file, ec) > (512u << 20)) return false;
    std::ifstream in (file, std::ios::binary);
    if (! in) return false;
    bytes.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
    return ! bytes.empty();
}

std::string findSerumContentRoot (const std::string& filePath)
{
    std::error_code ec;
    fs::path p = fs::absolute (filePath, ec).parent_path();
    while (! p.empty() && p != p.root_path())
    {
        if (fs::is_directory (p / "Tables", ec) || fs::is_directory (p / "Samples", ec)) return p.string();
        p = p.parent_path();
    }
    return {};
}

SerumPluginImport importSerumFileForPlugin (const std::string& fileName, const std::vector<uint8_t>& bytes, UserContentStore& store, EngineCapsMask caps,
                                            SerumContentResolver* resolver, const std::string& categoryHint)
{
    SerumPluginImport out;
    try
    {
        if (store.rootDirectory().empty()) { out.error = "no user directory"; return out; }
        const std::string stem = fileStemOf (fileName);
        SerumImportOptions options;
        options.resolver = resolver;
        options.fallbackName = stem.c_str();
        options.customWavetables = (caps & kCapUserWavetable) != 0;
        options.customSamples = (caps & kCapSampleOsc) != 0;
        options.noiseSample = (caps & kCapNoiseSample) != 0;
        options.customLfoShapes = (caps & kCapCustomLfoShape) != 0;
        options.categoryHint = categoryHint.empty() ? nullptr : categoryHint.c_str();

        SerumImportResult r;
        const SerumFileKind byName = serumFileKindFromName (fileName);
        if (! looksLikeSerumFile (bytes.data(), bytes.size()))
        {
            if (byName != SerumFileKind::audio) { out.error = "not a Serum file"; return out; }
            r = importSerumImpulse (bytes.data(), bytes.size(), stem, options);
        }
        else
            r = importSerumFile (bytes.data(), bytes.size(), options);
        out.kind = r.kind;
        out.report = r.report;
        out.reportJson = r.report.toJson();
        out.missingFiles = r.missingFiles;
        if (r.status != ImportStatus::ok)
        {
            out.error = r.error.empty() ? std::string ("the file did not import") : r.error;
            return out;
        }
        out.name = r.meta.name.empty() ? stem : r.meta.name;

        if (r.kind == SerumFileKind::impulse)
        {
            if (r.impulses.empty()) { out.error = "the impulse did not decode"; return out; }
            if (! store.addFile (ContentKind::impulse, stem, r.impulses.front().wavBytes, out.impulseRef)) { out.error = "the content store refused the impulse"; return out; }
            out.name = stem;
            ++out.storedFiles;
            out.ok = true;
            return out;
        }
        if (r.kind == SerumFileKind::shape)
        {
            out.shape = r.shape;
            out.shape.name = stem;
            out.name = stem;
            out.ok = out.shape.points.size() >= 2;
            if (! out.ok) out.error = "the shape has fewer than two points";
            return out;
        }
        if (r.kind == SerumFileKind::midiMap)
        {
            out.midiMap = r.midiMap;
            out.ok = true;
            return out;
        }
        if (r.kind != SerumFileKind::preset && r.kind != SerumFileKind::fxModule && r.kind != SerumFileKind::fxRack
            && r.kind != SerumFileKind::style)
        {
            out.ok = true; // the family has no equivalent: the report lists what the file holds
            out.error = "no equivalent in this synthesizer";
            return out;
        }

        // content
        ContentRefs refs;
        ContentFileRef impulseRef;
        for (const auto& wt : r.wavetables)
        {
            ContentFileRef ref;
            if (! store.addFile (ContentKind::wavetable, wt.name, wt.wavBytes, ref)) { out.error = "the content store refused a wavetable"; return out; }
            refs.wavetables.push_back ({ wt.osc, ref.pack, ref.file });
            ++out.storedFiles;
        }
        for (const auto& smp : r.samples)
        {
            ContentFileRef ref;
            if (! store.addFile (ContentKind::sample, smp.name, smp.wavBytes, ref)) { out.error = "the content store refused a sample"; return out; }
            refs.samples.push_back ({ smp.osc, ref.pack, ref.file });
            ++out.storedFiles;
        }
        for (const auto& ir : r.impulses)
        {
            if (! store.addFile (ContentKind::impulse, ir.name, ir.wavBytes, impulseRef)) { out.error = "the content store refused an impulse"; return out; }
            ++out.storedFiles;
            break;
        }
        PresetMeta meta = r.meta;
        if (meta.name.empty()) meta.name = stem;
        // the text goes through the same writer as every preset; the patch is read back from it, so what the plugin loads is what the file holds
        SerumImportResult asPreset = r;
        asPreset.kind = SerumFileKind::preset;
        const std::string text = serializeSerumPreset (asPreset, meta, refs, r.impulses.empty() ? nullptr : &impulseRef);
        PresetMeta loadedMeta;
        if (text.empty() || loadPreset (text.data(), text.size(), loadedMeta, out.values, out.content).status != LoadStatus::ok)
        {
            out.error = "the preset did not serialise";
            return out;
        }
        out.meta = loadedMeta;
        out.presetText = text;
        out.touched = r.touched;
        out.midiMap = r.midiMap;
        for (const auto& n : r.macroNames) out.macroNames.push_back (n);
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
} // namespace synthyboy
