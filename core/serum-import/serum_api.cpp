// SynthyBoy - core/serum-import: the public entry points, the preset text and the report of the Serum 2 importer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "serum_importer.h"
#include "sha256.h"
#include "synthyboy/serum_import.h"
#include "vital_import_internal.h"

namespace synthyboy
{
namespace
{
std::string lower (std::string s)
{
    for (char& c : s) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

bool endsWith (const std::string& s, const char* suffix)
{
    const size_t n = std::strlen (suffix);
    return s.size() >= n && lower (s.substr (s.size() - n)) == suffix;
}

const char* kindName (ItemKind k)
{
    switch (k) { case ItemKind::imported: return "imported"; case ItemKind::approximated: return "approximated"; case ItemKind::dropped: return "dropped"; case ItemKind::ignored: return "ignored"; }
    return "ignored";
}

const char* reasonName (Reason r)
{
    static const char* const names[] = { "exact", "clamped", "rounded", "enumRemapped", "legacyMigration", "defaulted", "typeMismatch", "truncated", "nearestWavetable", "nearestLfoShape",
                                         "unsupportedComponent", "renderFallback", "noEngineEquivalent", "customSample", "subOscillator", "tuningUnsupported", "modSourceUnsupported",
                                         "modDestinationUnsupported", "effectOrderInvalid", "limitExceeded", "tierNote" };
    return names[std::min<size_t> (static_cast<size_t> (r), sizeof names / sizeof names[0] - 1)];
}

const char* categoryOf (const std::string& hint, const std::string& presetName)
{
    static const struct { const char* from; const char* to; } map[] = {
        { "bass", "Bass" }, { "pad", "Pad" }, { "lead", "Lead" }, { "pluck", "Pluck" }, { "synth", "Lead" }, { "sfx", "FX" }, { "seq", "Lead" }, { "keyboard", "Keys" },
        { "drum", "Percussion" }, { "drumkit", "Percussion" }, { "arp", "Pluck" }, { "guitar", "Pluck" }, { "loop", "FX" }, { "organ", "Keys" }, { "e piano", "Keys" },
        { "soundscape", "Pad" }, { "string", "Pad" }, { "brass", "Lead" }, { "piano", "Keys" }, { "vox", "Pad" }, { "chord", "Keys" }, { "orchestral", "Pad" }, { "mallet", "Pluck" },
        { "bell", "Pluck" }, { "woodwind", "Lead" }, { "hit", "FX" }, { "instrument", "Keys" }, { "hoover", "Lead" },
        { "ba", "Bass" }, { "pd", "Pad" }, { "ld", "Lead" }, { "pl", "Pluck" }, { "sy", "Lead" }, { "fx", "FX" }, { "ky", "Keys" }, { "dr", "Percussion" }, { "str", "Pad" }, { "vox ", "Pad" } };
    const std::string h = lower (hint);
    for (const auto& m : map)
        if (h == m.from) return m.to;
    std::string prefix;
    for (const char c : presetName) { if (! std::isalpha (static_cast<unsigned char> (c))) break; prefix += static_cast<char> (std::tolower (static_cast<unsigned char> (c))); }
    for (const auto& m : map)
        if (prefix == m.from && std::strlen (m.from) <= 3) return m.to;
    return nullptr;
}
} // namespace

SerumFileKind serumFileKindFromName (const std::string& n) noexcept
{
    if (endsWith (n, ".serumpreset")) return SerumFileKind::preset;
    if (endsWith (n, ".serumfx")) return SerumFileKind::fxModule;
    if (endsWith (n, ".serumfxrack")) return SerumFileKind::fxRack;
    if (endsWith (n, ".xfershape")) return SerumFileKind::shape;
    if (endsWith (n, ".xferpath")) return SerumFileKind::path;
    if (endsWith (n, ".xferclip")) return SerumFileKind::clip;
    if (endsWith (n, ".xferclipbank")) return SerumFileKind::clipBank;
    if (endsWith (n, ".xferarp")) return SerumFileKind::arp;
    if (endsWith (n, ".xferarpbank")) return SerumFileKind::arpBank;
    if (endsWith (n, ".serumstyle")) return SerumFileKind::style;
    if (endsWith (n, ".xferfilterpzsingle") || endsWith (n, ".xferfilterpzmulti")) return SerumFileKind::filterPz;
    if (endsWith (n, ".serummidimap")) return SerumFileKind::midiMap;
    if (endsWith (n, ".flac") || endsWith (n, ".wav")) return SerumFileKind::audio;
    return SerumFileKind::unknown;
}

bool looksLikeSerumFile (const void* data, size_t size) noexcept { return size >= 17 && std::memcmp (data, "XferJson\0", 9) == 0; }

SerumImportResult importSerumFile (const void* data, size_t size, const SerumImportOptions& options) noexcept
{
    SerumImportResult r;
    try
    {
        r.values = ParamValues::defaults();
        r.sourceSha256 = Sha256::hashBufferHex (data, size);
        if (! looksLikeSerumFile (data, size)) { r.status = ImportStatus::notVital; r.error = "not a Serum file"; return r; }
        serumimport::ContainerLimits lim;
        lim.maxFileBytes = options.limits.maxFileBytes;
        lim.maxBodyBytes = options.limits.maxBodyBytes;
        lim.maxWorkingBytes = options.limits.maxWorkingBytes;
        lim.maxDepth = options.limits.maxDepth;
        serumimport::Container con;
        const serumimport::ContainerStatus cs = serumimport::openContainer (static_cast<const uint8_t*> (data), size, lim, con);
        if (cs != serumimport::ContainerStatus::ok)
        {
            r.error = serumimport::containerStatusText (cs);
            r.status = cs == serumimport::ContainerStatus::notSerum ? ImportStatus::notVital
                       : (cs == serumimport::ContainerStatus::tooLarge) ? ImportStatus::tooLarge
                       : (cs == serumimport::ContainerStatus::budget) ? ImportStatus::budgetExceeded : ImportStatus::malformed;
            return r;
        }
        const std::string& ft = con.fileType;
        serumimport::Importer imp (con, options, r, static_cast<const uint8_t*> (data), size);
        if (ft == "SerumPreset") { r.kind = SerumFileKind::preset; imp.runPreset(); }
        else if (ft == "SerumFX") { r.kind = SerumFileKind::fxModule; imp.runFxFile(); }
        else if (ft == "SerumFXRack") { r.kind = SerumFileKind::fxRack; imp.runFxFile(); }
        else if (ft == "XferShape") { r.kind = SerumFileKind::shape; imp.runShape(); }
        else if (ft == "SerumStyle") { r.kind = SerumFileKind::style; imp.runStyle(); }
        else
        {
            r.kind = ft == "XferPath" ? SerumFileKind::path : ft == "XferClip" ? SerumFileKind::clip : ft == "XferClipBank" ? SerumFileKind::clipBank : ft == "XferArp" ? SerumFileKind::arp
                     : ft == "XferArpBank" ? SerumFileKind::arpBank : ft.rfind ("XferFilterPZ", 0) == 0 ? SerumFileKind::filterPz
                     : ft == "SerumMIDIMap" ? SerumFileKind::midiMap : SerumFileKind::unknown;
            imp.runUnmapped (ft.c_str());
        }
        if (r.kind == SerumFileKind::preset)
        {
            const char* cat = categoryOf (options.categoryHint ? options.categoryHint : "", r.meta.name);
            if (cat) r.meta.category = cat;
        }
        imp.finish();
        if (imp.deadlineHit) { r.status = ImportStatus::cancelled; r.error = "time limit reached"; return r; }
        if (options.cancel && options.cancel()) { r.status = ImportStatus::cancelled; r.error = "cancelled"; return r; }
        r.report.serumVersion = r.report.serumVersion.empty() ? "" : r.report.serumVersion;
        r.status = ImportStatus::ok;
    }
    catch (const std::bad_alloc&)
    {
        r.status = ImportStatus::budgetExceeded;
        r.error = "out of memory";
    }
    catch (...)
    {
        r.status = ImportStatus::malformed;
        r.error = "internal error";
    }
    return r;
}

std::string SerumImportReport::toJson() const
{
    std::string s = "{\"format\":\"synthyboy-serum-import-report\",\"version\":1";
    s += ",\"serum_version\":" + serumimport::jsonEscapeString (serumVersion);
    s += ",\"file_version\":" + std::to_string (fileVersion);
    s += ",\"counts\":{\"imported\":" + std::to_string (numImported) + ",\"approximated\":" + std::to_string (numApproximated) + ",\"dropped\":" + std::to_string (numDropped)
         + ",\"ignored\":" + std::to_string (numIgnored) + ",\"defaulted\":" + std::to_string (numDefaulted) + "}";
    s += ",\"leaves\":" + std::to_string (leaves) + ",\"unaccounted\":" + std::to_string (unaccounted);
    s += std::string (",\"truncated\":") + (truncated ? "true" : "false");
    s += ",\"fidelity\":\"" + std::string (fidelity == Fidelity::exact ? "exact" : fidelity == Fidelity::close ? "close" : "degraded") + "\"";
    s += ",\"entries\":[";
    bool first = true;
    for (const SerumReportEntry& e : entries)
    {
        if (! first) s += ',';
        first = false;
        s += "{\"kind\":\"" + std::string (kindName (e.kind)) + "\",\"reason\":\"" + reasonName (e.reason) + "\",\"area\":" + serumimport::jsonEscapeString (e.area);
        s += ",\"path\":" + serumimport::jsonEscapeString (e.serumPath) + ",\"value\":" + serumimport::jsonEscapeString (e.serumValue);
        s += ",\"key\":" + serumimport::jsonEscapeString (e.ourKey) + ",\"ours\":" + serumimport::jsonEscapeString (e.ourValue);
        s += ",\"detail\":" + serumimport::jsonEscapeString (e.detail) + ",\"count\":" + std::to_string (e.count) + "}";
    }
    s += "]}";
    return s;
}

std::vector<std::string> serumReportLines (const SerumImportReport& report, size_t maxLines)
{
    static const char* const kinds[] = { "imported", "approximated", "dropped", "ignored" };
    std::vector<std::string> lines;
    for (const SerumReportEntry& e : report.entries)
    {
        if (lines.size() >= maxLines) break;
        if (e.kind == ItemKind::imported || e.kind == ItemKind::ignored) continue; // the sheet lists what was not taken over as is
        std::string line = kinds[std::min<size_t> (static_cast<size_t> (e.kind), 3)];
        line += ": ";
        line += e.ourKey.empty() ? e.serumPath : e.ourKey;
        if (! e.serumValue.empty()) line += " (" + e.serumValue + (e.ourValue.empty() ? "" : " -> " + e.ourValue) + ")";
        if (! e.detail.empty()) line += "  " + e.detail;
        if (e.count > 1) line += "  [" + std::to_string (e.count) + " values]";
        lines.push_back (std::move (line));
    }
    return lines;
}

std::string serializeSerumPreset (const SerumImportResult& result, const PresetMeta& finalMeta, const ContentRefs& contentRefs, const ContentFileRef* impulseRef)
{
    if (result.status != ImportStatus::ok || result.kind != SerumFileKind::preset) return {};
    StateContent content;
    for (const auto& iw : result.wavetables)
    {
        ContentWavetable cw;
        cw.osc = iw.osc;
        cw.ref.sha256 = iw.sha256;
        cw.ref.name = iw.name;
        cw.author = iw.author;
        cw.frames = iw.numFrames;
        if (const auto* ref = contentRefs.findWavetable (iw.osc)) { cw.ref.pack = ref->pack; cw.ref.file = ref->file; }
        content.wavetables.push_back (std::move (cw));
    }
    for (const auto& is : result.samples)
    {
        ContentSample cs;
        cs.osc = is.osc;
        cs.ref.sha256 = is.sha256;
        cs.ref.name = is.name;
        cs.sampleRate = is.sampleRate;
        cs.frames = is.numFrames;
        cs.channels = is.channels;
        cs.slices = is.slices;
        if (const auto* ref = contentRefs.findSample (is.osc)) { cs.ref.pack = ref->pack; cs.ref.file = ref->file; }
        content.samples.push_back (std::move (cs));
    }
    content.lfoShapes = result.lfoShapes;
    content.remaps = result.remaps;
    content.modCurves = result.modCurves;
    content.velocityCurve = result.velocityCurve;
    content.noteCurve = result.noteCurve;
    content.shaperA = result.shaperA;
    content.shaperB = result.shaperB;
    if (result.tuning.present) content.tuning = result.tuning;
    if (impulseRef && ! result.impulses.empty()) content.impulses.push_back (*impulseRef);
    // keep only the parts that validate (a part the engine would reject must not take the other parts with it)
    if (! validateContent (content))
    {
        StateContent keep;
        const auto ok = [] (StateContent c) { return validateContent (c); };
        for (const auto& w : content.wavetables) { StateContent t; t.wavetables.push_back (w); if (ok (t)) keep.wavetables.push_back (w); }
        for (const auto& w : content.samples) { StateContent t; t.samples.push_back (w); if (ok (t)) keep.samples.push_back (w); }
        for (const auto& w : content.lfoShapes) { StateContent t; t.lfoShapes.push_back (w); if (ok (t)) keep.lfoShapes.push_back (w); }
        for (const auto& w : content.remaps) { StateContent t; t.remaps.push_back (w); if (ok (t)) keep.remaps.push_back (w); }
        for (const auto& w : content.modCurves) { StateContent t; t.modCurves.push_back (w); if (ok (t)) keep.modCurves.push_back (w); }
        { StateContent t; t.velocityCurve = content.velocityCurve; if (ok (t)) keep.velocityCurve = content.velocityCurve; }
        { StateContent t; t.noteCurve = content.noteCurve; if (ok (t)) keep.noteCurve = content.noteCurve; }
        { StateContent t; t.shaperA = content.shaperA; if (ok (t)) keep.shaperA = content.shaperA; }
        { StateContent t; t.shaperB = content.shaperB; if (ok (t)) keep.shaperB = content.shaperB; }
        keep.tuning = content.tuning;
        keep.impulses = content.impulses;
        content = std::move (keep);
    }
    std::string s = savePreset (finalMeta, result.values, content, true);
    if (s.empty() || s.back() != '}') return {};
    s.pop_back();
    s += ",\"import\":{\"source\":{\"format\":\"serum\",\"version\":" + serumimport::jsonEscapeString (result.report.serumVersion);
    s += ",\"sha256\":" + serumimport::jsonEscapeString (result.sourceSha256);
    s += ",\"preset_name\":" + serumimport::jsonEscapeString (finalMeta.name.empty() ? result.meta.name : finalMeta.name);
    s += ",\"macro_names\":[";
    for (int i = 0; i < 8; ++i) { if (i) s += ','; s += serumimport::jsonEscapeString (result.macroNames[i]); }
    s += "]},\"approximated\":[";
    std::vector<std::string> keys;
    for (const SerumReportEntry& e : result.report.entries)
        if (e.kind == ItemKind::approximated && ! e.ourKey.empty() && std::find (keys.begin(), keys.end(), e.ourKey) == keys.end() && keys.size() < 64) keys.push_back (e.ourKey);
    for (size_t i = 0; i < keys.size(); ++i) { if (i) s += ','; s += serumimport::jsonEscapeString (keys[i]); }
    s += "]}}";
    return s;
}

size_t serumRuleCount() noexcept
{
    size_t total = 0;
    for (const char* scope : { "Osc", "Warp", "WT", "Multi", "Noise", "Sub", "Filter", "Env", "Macro", "Global", "Routing" })
    {
        size_t n = 0;
        serumimport::rulesOf (scope, n);
        total += n;
    }
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
    {
        size_t n = 0;
        serumimport::fxRulesOf (t, n);
        total += n;
    }
    return total;
}

std::string serumRuleDescription (size_t row)
{
    for (const char* scope : { "Osc", "Warp", "WT", "Multi", "Noise", "Sub", "Filter", "Env", "Macro", "Global", "Routing" })
    {
        size_t n = 0;
        const serumimport::Rule* r = serumimport::rulesOf (scope, n);
        if (row < n) return std::string (r[row].scope) + "/" + r[row].param + " -> " + r[row].key;
        row -= n;
    }
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
    {
        size_t n = 0;
        const serumimport::Rule* r = serumimport::fxRulesOf (t, n);
        if (row < n) return std::string (fxTypeName (t)) + "/" + r[row].param + " -> " + r[row].key;
        row -= n;
    }
    return {};
}
} // namespace synthyboy
