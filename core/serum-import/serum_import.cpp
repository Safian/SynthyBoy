// SynthyBoy - core/serum-import: the importer for a .SerumPreset (docs/ui-design/19-serum-preset-format.md, 13-new-keys.md 4.26).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One pass over the value tree of the file. Every leaf is accounted exactly once: imported (a row of the tables of serum_map.cpp or a special rule),
// approximated (a nearest feature, a clamped range), dropped (no home in our model), or ignored (interface state, derived data, the "default" blocks).
// The final sweep reports whatever no rule consumed, so the numbers of the report always add up to the leaves of the file.
#include "serum_importer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "sha256.h"
#include "synthyboy/filters/filter_types.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"
#include "vital_import_internal.h"

namespace synthyboy
{
namespace serumimport
{
namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string trimNumber (double v)
{
    char buf[48];
    if (std::fabs (v) >= 1e15) std::snprintf (buf, sizeof buf, "%.6g", v);
    else if (v == std::floor (v) && std::fabs (v) < 1e9) std::snprintf (buf, sizeof buf, "%.0f", v);
    else std::snprintf (buf, sizeof buf, "%.9g", v);
    return buf;
}

} // namespace

int leavesUnder (const cbor::Doc& d, int node)
{
    const cbor::Node& n = d.nodes[static_cast<size_t> (node)];
    switch (n.type)
    {
        case cbor::Type::map:
        {
            int total = 0;
            for (uint32_t k = 0; k < n.count; ++k)
                total += leavesUnder (d, static_cast<int> (d.kids[n.first + 2 * k + 1]));
            return total;
        }
        case cbor::Type::array:
        {
            if (n.floatArray) return static_cast<int> (n.count);
            int total = 0;
            for (uint32_t k = 0; k < n.count; ++k)
                total += leavesUnder (d, static_cast<int> (d.kids[n.first + k]));
            return total;
        }
        default: return 1;
    }
}

std::string jsonEscapeString (const std::string& s)
{
    std::string out = "\"";
    for (const unsigned char c : s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); out += b; }
                else out += static_cast<char> (c);
        }
    }
    out += '"';
    return out;
}

Importer::Importer (const Container& c, const SerumImportOptions& o, SerumImportResult& r, const uint8_t*, size_t)
    : d (c.body), con (c), opt (o), R (r)
{
    used.assign (d.nodes.size(), 0);
    touched.assign (paramCount(), 0);
    setByFile.assign (paramCount(), 0);
    for (int& v : randomLfoOf) v = 0;
    for (int& v : lfoOrigPoints) v = 0;
    for (int& v : lfoPointOffset) v = 0;
    startMs = nowMs();
    leafCount = d.nodes.empty() ? 0 : leavesUnder (d, 0);
    R.report.leaves = leafCount;
}

bool Importer::cancelled()
{
    if (opt.cancel && opt.cancel()) return true;
    if (opt.limits.deadlineMs != 0 && nowMs() - startMs > static_cast<int64_t> (opt.limits.deadlineMs)) { deadlineHit = 1; return true; }
    return false;
}

double Importer::num (int i, double fb) const
{
    if (i < 0) return fb;
    const cbor::Node& n = N (i);
    if (n.type == cbor::Type::boolean) return n.b ? 1.0 : 0.0;
    if (n.type == cbor::Type::integer || n.type == cbor::Type::real) return std::isfinite (n.d) ? n.d : fb;
    return fb;
}

std::string Importer::fmt (double v) const { return trimNumber (v); }

std::string Importer::valueText (int node) const
{
    if (node < 0) return {};
    const cbor::Node& n = N (node);
    switch (n.type)
    {
        case cbor::Type::text: return vitalimport::cleanText (d.text (static_cast<uint32_t> (node)), 64, true);
        case cbor::Type::boolean: return n.b ? "true" : "false";
        case cbor::Type::integer: case cbor::Type::real: return trimNumber (n.d);
        case cbor::Type::null: return "null";
        case cbor::Type::bytes: return "bytes[" + std::to_string (n.len) + "]";
        case cbor::Type::array: return "array[" + std::to_string (n.count) + "]";
        case cbor::Type::map: return "map[" + std::to_string (n.count) + "]";
    }
    return {};
}

void Importer::consume (int node)
{
    if (node < 0) return;
    const cbor::Node& n = N (node);
    used[static_cast<size_t> (node)] = 1;
    if (n.type == cbor::Type::map)
        for (uint32_t k = 0; k < n.count; ++k)
        {
            used[d.kids[n.first + 2 * k]] = 1;
            consume (static_cast<int> (d.kids[n.first + 2 * k + 1]));
        }
    else if (n.type == cbor::Type::array && ! n.floatArray)
        for (uint32_t k = 0; k < n.count; ++k)
            consume (static_cast<int> (d.kids[n.first + k]));
}

int Importer::unusedLeaves (int node) const
{
    if (node < 0 || used[static_cast<size_t> (node)]) return 0;
    const cbor::Node& n = N (node);
    if (n.type == cbor::Type::map)
    {
        int total = 0;
        for (uint32_t k = 0; k < n.count; ++k)
            total += unusedLeaves (static_cast<int> (d.kids[n.first + 2 * k + 1]));
        return total;
    }
    if (n.type == cbor::Type::array)
    {
        if (n.floatArray) return static_cast<int> (n.count);
        int total = 0;
        for (uint32_t k = 0; k < n.count; ++k)
            total += unusedLeaves (static_cast<int> (d.kids[n.first + k]));
        return total;
    }
    return 1;
}

void Importer::acctRest (int node, ItemKind kind, Reason reason, const std::string& area, const std::string& path, const std::string& key, const std::string& ours, const std::string& detail)
{
    if (node < 0) return;
    const int left = unusedLeaves (node);
    if (left > 0) entry (kind, reason, area, path, valueText (node), key, ours, detail, left);
    consume (node);
}

void Importer::noteDefault (int node)
{
    if (node < 0 || used[static_cast<size_t> (node)]) return;
    used[static_cast<size_t> (node)] = 1;
    ++defaultLeaves;
}

bool Importer::isDefaultBlock (int plain) const { return plain >= 0 && N (plain).type == cbor::Type::text && d.isText (static_cast<uint32_t> (plain), "default"); }

void Importer::entry (ItemKind kind, Reason reason, const std::string& area, const std::string& path, const std::string& value, const std::string& key, const std::string& ours,
                      const std::string& detail, int count)
{
    SerumImportReport& rep = R.report;
    switch (kind)
    {
        case ItemKind::imported: rep.numImported += count; break;
        case ItemKind::approximated: rep.numApproximated += count; break;
        case ItemKind::dropped: rep.numDropped += count; break;
        case ItemKind::ignored: rep.numIgnored += count; break;
    }
    if (static_cast<int> (rep.entries.size()) >= opt.limits.maxReportEntries)
    {
        rep.truncated = true;
        return;
    }
    SerumReportEntry e;
    e.kind = kind;
    e.reason = reason;
    e.area = area;
    e.serumPath = path;
    e.serumValue = value;
    e.ourKey = key;
    e.ourValue = ours;
    e.detail = detail;
    e.count = count;
    rep.entries.push_back (std::move (e));
}

int Importer::indexOf (const std::string& key) const { return findParamIndex (hashKey (key.c_str())); }

std::string Importer::keyOf (const char* pattern, int n)
{
    std::string s = pattern;
    const size_t p = s.find ('#');
    if (p != std::string::npos) s.replace (p, 1, std::to_string (n));
    return s;
}

double Importer::store (const std::string& key, double v, bool* clamped)
{
    const int idx = indexOf (key);
    if (idx < 0) return v;
    const ParamInfo& info = paramTable()[idx];
    const float stored = constrainValue (info, static_cast<float> (v));
    R.values.plain[idx] = stored;
    touched[static_cast<size_t> (idx)] = 1;
    if (clamped)
    {
        const double tol = info.interval > 0 ? 0.5 * info.interval + 1e-6 : 1e-4 * std::max (1.0, std::fabs (v));
        *clamped = std::fabs (static_cast<double> (stored) - v) > tol;
    }
    return stored;
}

bool Importer::applyRuleValue (const Rule& r, int node, int n, const std::string& area, const std::string& path, double* storedOut)
{
    if (r.key[0] == '\0') return false;
    const std::string key = keyOf (r.key, n);
    const int idx = indexOf (key);
    if (idx < 0) return false;
    double ours = 0.0;
    Fid fid = r.fid;
    std::string detail = r.note;
    Reason reason = Reason::exact;
    if (r.conv == Conv::enumName)
    {
        if (! isText (node))
        {
            entry (ItemKind::dropped, Reason::typeMismatch, area, path, valueText (node), key, "", "enum_expected_text");
            consume (node);
            return true;
        }
        const std::string name = str (node);
        const EnumEntry* hit = nullptr;
        for (int k = 0; k < r.en->count; ++k)
            if (name == r.en->entries[k].name) { hit = &r.en->entries[k]; break; }
        if (! hit)
        {
            const bool source = name.find ("_NOISE") != std::string::npos || name.find ("_SUB") != std::string::npos || name.find ("_FILT") != std::string::npos;
            entry (ItemKind::dropped, source ? Reason::noEngineEquivalent : Reason::enumRemapped, area, path, name, key, "", source ? "warp_source_unsupported" : "enum_value_unknown");
            consume (node);
            return true;
        }
        ours = hit->value;
        if (hit->fid == Fid::approx || (hit->fid == Fid::close && fid == Fid::exact)) fid = hit->fid == Fid::approx ? Fid::approx : Fid::close;
        if (hit->fid == Fid::approx) { reason = Reason::enumRemapped; if (detail.empty()) detail = std::string ("nearest_") + r.en->area; }
    }
    else
    {
        if (! isNumber (node))
        {
            entry (ItemKind::dropped, Reason::typeMismatch, area, path, valueText (node), key, "", "number_expected");
            consume (node);
            return true;
        }
        ours = convert (r, num (node));
    }
    bool clamped = false;
    const double stored = store (key, ours, &clamped);
    setByFile[static_cast<size_t> (idx)] = 1;
    if (storedOut) *storedOut = stored;
    ItemKind kind = fid == Fid::approx ? ItemKind::approximated : ItemKind::imported;
    if (clamped) { kind = ItemKind::approximated; reason = Reason::clamped; if (detail.empty()) detail = "clamped_to_range"; }
    char text[64];
    valueToText (paramTable()[idx], static_cast<float> (stored), text, sizeof text);
    entry (kind, reason, area, path, valueText (node), key, text, detail);
    consume (node);
    return true;
}

void Importer::applyScope (std::initializer_list<const char*> scopes, int plain, int n, const std::string& area, const std::string& path, std::vector<Unmapped>& unmapped,
                           bool applyDefaults, const std::vector<const char*>& skip)
{
    std::vector<const Rule*> rows;
    for (const char* scope : scopes)
    {
        size_t count = 0;
        const Rule* rules = rulesOf (scope, count);
        for (size_t i = 0; i < count; ++i)
            if (std::strcmp (rules[i].scope, scope) == 0)
                rows.push_back (rules + i);
    }
    if (isMap (plain))
        forMap (plain, [&] (const std::string& name, int value, int keyNode)
        {
            for (const char* s : skip)
                if (name == s) return;
            bool handled = false;
            for (const Rule* r : rows)
                if (name == r->param)
                {
                    // several rows can share one parameter (the compressor writes three bands); the first one marks the leaf, the others store silently
                    if (! handled)
                    {
                        handled = applyRuleValue (*r, value, n, area, path + "/" + name);
                        if (handled) used[static_cast<size_t> (keyNode)] = 1;
                    }
                    else if (r->conv != Conv::enumName)
                        store (keyOf (r->key, n), convert (*r, num (value)));
                }
            if (! handled)
                unmapped.push_back ({ name, value });
        });
    if (applyDefaults)
        for (const Rule* r : rows)
        {
            if (! std::isfinite (r->serumDefault) || r->key[0] == '\0' || r->conv == Conv::enumName) continue;
            const int idx = indexOf (keyOf (r->key, n));
            if (idx < 0 || setByFile[static_cast<size_t> (idx)]) continue;
            store (keyOf (r->key, n), convert (*r, r->serumDefault));
            ++R.report.numDefaulted;
        }
}

void Importer::dropUnmapped (const std::vector<Unmapped>& list, const std::string& area, const std::string& path, const char* detail, Reason code)
{
    for (const Unmapped& u : list)
    {
        if (used[static_cast<size_t> (u.node)]) continue;
        const int leaves = leavesUnder (d, u.node);
        const bool ui = u.name.rfind ("kUIParam", 0) == 0;
        entry (ui ? ItemKind::ignored : ItemKind::dropped, ui ? Reason::exact : code, area, path + "/" + u.name, valueText (u.node), "", "", ui ? "interface_state" : detail, leaves);
        consume (u.node);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// text and tuning of the preset
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::meta()
{
    using namespace vitalimport;
    json::Doc hdr;
    std::string name, author, description, comment;
    std::vector<std::string> tags;
    if (json::parse (con.headerText.data(), con.headerText.size(), 6, 1u << 20, hdr) == json::Status::ok && ! hdr.nodes.empty())
    {
        const auto text = [&] (const char* key) -> std::string
        {
            const int n = hdr.find (0, key);
            return n >= 0 && hdr.nodes[static_cast<size_t> (n)].type == json::Type::string ? hdr.text (hdr.nodes[static_cast<size_t> (n)].str) : std::string();
        };
        name = text ("presetName");
        author = text ("presetAuthor");
        description = text ("presetDescription");
        comment = text ("presetComment");
        R.report.serumVersion = text ("productVersion");
        const int ver = hdr.find (0, "version");
        if (ver >= 0 && hdr.nodes[static_cast<size_t> (ver)].type == json::Type::number) R.report.fileVersion = static_cast<uint32_t> (hdr.nodes[static_cast<size_t> (ver)].number);
        const int tg = hdr.find (0, "tags");
        if (tg >= 0 && hdr.nodes[static_cast<size_t> (tg)].type == json::Type::array)
            for (int e = hdr.nodes[static_cast<size_t> (tg)].first; e >= 0; e = hdr.nodes[static_cast<size_t> (e)].next)
                if (hdr.nodes[static_cast<size_t> (e)].type == json::Type::string) tags.push_back (cleanText (hdr.text (hdr.nodes[static_cast<size_t> (e)].str), 40, true));
    }
    bool cutName = false, cutAuthor = false, cutDesc = false;
    R.meta.name = cleanText (name, 255, true, &cutName);
    if (R.meta.name.empty() && opt.fallbackName && opt.fallbackName[0]) R.meta.name = cleanText (opt.fallbackName, 255, false);
    if (R.meta.name.empty()) R.meta.name = "Imported";
    R.meta.author = cleanText (author, 255, true, &cutAuthor);
    std::string desc = description;
    if (! comment.empty()) desc += (desc.empty() ? "" : " - ") + comment;
    const std::string cleaned = cleanText (desc, 250, true, &cutDesc);
    R.meta.description = cutDesc ? cleaned + "\xE2\x80\xA6" : cleaned;
    R.meta.license = "Imported, user-supplied; original terms apply";
    R.meta.category = "Lead";
    const std::string src = R.sourceSha256.substr (0, 12);
    std::string tagText = "imported,serum2";
    for (const std::string& t : tags) tagText += "," + t;
    tagText += ",src:" + src;
    if (tagText.size() > 255) tagText = "imported,serum2,src:" + src;
    R.meta.tags = tagText;
    if (cutName || cutAuthor || cutDesc)
        entry (ItemKind::approximated, Reason::truncated, "meta", "header", "", "", "", "text_truncated", 0);
    // the header strings are leaves of the file but not of the value tree (they sit in the JSON header): they are not part of `leaves`
    // the same keys appear again in the body (presetName, presetAuthor, ...): they are consumed in unmappedTopLevel ()
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// the preset
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::runPreset()
{
    meta();
    if (! isMap (0)) return;
    // the performance parameters of the init state are Serum's, not ours: start from our defaults and overwrite what the file (and the Serum defaults) say
    for (int i = 0; i < 3 && ! cancelled(); ++i) oscillator (i);
    noiseOsc();
    subOsc();
    routing();
    for (int i = 0; i < 2 && ! cancelled(); ++i) voiceFilter (i);
    for (int i = 0; i < 4 && ! cancelled(); ++i) envelope (i);
    for (int i = 0; i < 10 && ! cancelled(); ++i) lfo (i);
    for (int i = 0; i < 8; ++i) macro (i);
    global();
    for (int r = 0; r < 3 && ! cancelled(); ++r) fxRack (r, false);
    modSlots();
    curves();
    unmappedTopLevel();
}

void Importer::macro (int i)
{
    const std::string name = "Macro" + std::to_string (i);
    const int m = get (0, name.c_str());
    const int n = i + 1;
    const std::string area = "macro" + std::to_string (n);
    if (m < 0) return;
    const int nm = get (m, "name");
    if (isText (nm))
    {
        R.macroNames[i] = vitalimport::cleanText (str (nm), 32, true);
        entry (ItemKind::imported, Reason::exact, area, name + "/name", R.macroNames[i], "", "", "macro_name");
        consume (nm);
    }
    const int pp = get (m, "plainParams");
    std::vector<Unmapped> um;
    if (isDefaultBlock (pp)) noteDefault (pp);
    else applyScope ({ "Macro" }, pp, n, area, name + "/plainParams", um, false);
    dropUnmapped (um, area, name + "/plainParams", "serum_feature_not_modelled");
}

void Importer::global()
{
    const int g = get (0, "Global0");
    if (g < 0) return;
    const int pp = get (g, "plainParams");
    const std::string path = "Global0/plainParams";
    std::vector<Unmapped> um;
    if (isDefaultBlock (pp)) noteDefault (pp);
    else
    {
        applyScope ({ "Global" }, pp, 1, "voice", path, um, false);
        // the specials
        for (const Unmapped& u : um)
        {
            if (used[static_cast<size_t> (u.node)]) continue;
            const std::string p = path + "/" + u.name;
            const double v = num (u.node);
            const auto done = [&] (ItemKind k, Reason r, const std::string& key, const std::string& ours, const std::string& detail)
            {
                entry (k, r, "voice", p, valueText (u.node), key, ours, detail);
                consume (u.node);
            };
            if (u.name == "kParamBendRangeDn")
            {
                bool cl = false;
                const double st = store ("master.bend_down_range", -v, &cl);
                done (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, "master.bend_down_range", fmt (st), "");
            }
            else if (u.name == "kParamMonoToggle")
            {
                if (v >= 0.5) store ("master.polyphony", 1);
                done (ItemKind::approximated, Reason::enumRemapped, "master.polyphony", v >= 0.5 ? "1" : "", "mono_as_polyphony_one");
            }
            else if (u.name == "kParamLimitSameNotePolyphony")
            {
                store ("master.voice.same_note", v >= 0.5 ? 0 : 1);
                done (ItemKind::approximated, Reason::enumRemapped, "master.voice.same_note", v >= 0.5 ? "Steal" : "Stack", "same_note_limit_as_steal");
            }
            else if (u.name == "kParamOversampling")
            {
                store ("quality.oversample_4x", v >= 2.0 ? 1 : 0);
                done (ItemKind::approximated, Reason::enumRemapped, "quality.oversample_4x", v >= 2.0 ? "On" : "Off", "oversampling_to_4x_switch");
            }
            else if (u.name == "kParamVoicePriority")
            {
                const std::string s = str (u.node);
                const int idx = s == "Low" ? 3 : s == "High" ? 2 : s == "Last" ? 0 : s == "First" ? 1 : -1;
                if (idx >= 0) { store ("master.voice.steal_priority", idx); done (ItemKind::imported, Reason::exact, "master.voice.steal_priority", s, ""); }
                else done (ItemKind::dropped, Reason::enumRemapped, "", "", "enum_value_unknown");
            }
            else if (u.name == "kParamMasterTuning" || u.name == "kParamVoiceAmp") // normally rules; reached only if the type was not a number
                done (ItemKind::dropped, Reason::typeMismatch, "", "", "number_expected");
            else if (u.name == "kParamModWheel" || u.name == "kParamProgram" || u.name == "kParamMidiOut" || u.name == "kParamNoteLatch" || u.name == "kParamPitchBendAuto"
                     || u.name == "kParamBypass")
                done (ItemKind::ignored, Reason::exact, "", "", "performance_state_not_part_of_a_patch");
            else if (u.name == "kParamS1Compatibility" || u.name == "kParamUseUltraOnRender" || u.name == "kParamSmoothingDisable")
                done (ItemKind::dropped, Reason::noEngineEquivalent, "", "", "serum_mode_not_modelled");
            else if (u.name == "kParamSwing" || u.name == "kParamSwingDiv")
                done (ItemKind::dropped, Reason::noEngineEquivalent, "", "", "global_swing_not_modelled");
            else if (u.name == "kParamPolyCount") {}
        }
    }
    // absent Serum defaults of the voice settings
    if (! setByFile[static_cast<size_t> (indexOf ("master.bend_range"))]) store ("master.bend_range", 2), store ("master.bend_down_range", 2);
    store ("master.bend_down_link", [&] { const int up = indexOf ("master.bend_range"), dn = indexOf ("master.bend_down_range"); return R.values.plain[up] == R.values.plain[dn] ? 1 : 0; }());
    dropUnmapped (um, "voice", path, "serum_feature_not_modelled");
    // MPE and tuning at the top level
    const int mpe = get (0, "mpeEnabled");
    if (mpe >= 0) { store ("master.mpe.on", num (mpe)); entry (ItemKind::imported, Reason::exact, "voice", "mpeEnabled", valueText (mpe), "master.mpe.on", num (mpe) >= 0.5 ? "On" : "Off", ""); consume (mpe); }
    const int pbr = get (0, "mpePitchBendRange");
    if (pbr >= 0)
    {
        bool cl = false;
        const double st = store ("master.mpe.bend_range", num (pbr), &cl);
        entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, "voice", "mpePitchBendRange", valueText (pbr), "master.mpe.bend_range", fmt (st), "");
        consume (pbr);
    }
    // GROUPA1 (WP-09, A350): mpeConfig is the number of the main menu mappings (XYZ -> Macro ..., Y -> Mod Wheel). 0 (all of the 656 presets of the installation) is
    // "no mapping": the keys stay Off. A number we have never seen is not decoded: the mappings stay Off and the value is reported
    const int mpeConfig = get (0, "mpeConfig");
    if (mpeConfig >= 0)
    {
        if (isNumber (mpeConfig) && num (mpeConfig) == 0.0)
        {
            store ("master.mpe.xyz_macro", 0);
            entry (ItemKind::imported, Reason::exact, "voice", "mpeConfig", valueText (mpeConfig), "master.mpe.xyz_macro", "Off", "mpe_config_no_mapping");
        }
        else
            entry (ItemKind::dropped, Reason::noEngineEquivalent, "voice", "mpeConfig", valueText (mpeConfig), "master.mpe.xyz_macro", "Off", "mpe_config_value_not_decoded");
        consume (mpeConfig);
    }
    const int pq = get (0, "PitchQuantizer0");
    if (pq >= 0)
    {
        const int sn = get (pq, "scaleName"), pp2 = get (pq, "plainParams");
        const std::string scaleName = isText (sn) ? str (sn) : std::string();
        static const struct { const char* name; int type; } scales[] = { { "Major", 1 }, { "Minor", 2 }, { "Natural Minor", 2 }, { "Minor (Harmonic)", 3 }, { "Harmonic Minor", 3 },
                                                                          { "Minor (Melodic)", 4 }, { "Melodic Minor", 4 }, { "Dorian", 5 }, { "Phrygian", 6 }, { "Lydian", 7 },
                                                                          { "Mixolydian", 8 }, { "Locrian", 9 }, { "Blues", 12 }, { "Whole Tone", 13 } };
        int type = -1;
        for (const auto& s : scales) if (scaleName == s.name) type = s.type;
        bool active = false;
        if (isMap (pp2))
        {
            const int sc = get (pp2, "kParamScale"), ky = get (pp2, "kParamKey");
            active = sc >= 0;
            if (active && type > 0)
            {
                store ("master.scale.type", type);
                if (ky >= 0) store ("master.scale.key", std::fmod (std::max (0.0, num (ky)), 12.0));
                entry (ItemKind::approximated, Reason::enumRemapped, "voice", "PitchQuantizer0", scaleName, "master.scale.type", scaleName, "scale_matched_by_name", 0);
                if (ky >= 0) { entry (ItemKind::approximated, Reason::enumRemapped, "voice", "PitchQuantizer0/plainParams/kParamKey", valueText (ky), "master.scale.key", "", "scale_key_index_assumed"); consume (ky); }
                entry (ItemKind::approximated, Reason::enumRemapped, "voice", "PitchQuantizer0/plainParams/kParamScale", valueText (sc), "master.scale.type", scaleName, "scale_matched_by_name");
                consume (sc);
            }
        }
        // the 12 note scale list and the name are the data behind the name
        acctRest (pq, ItemKind::ignored, Reason::exact, "voice", "PitchQuantizer0", "", "", active && type > 0 ? "scale_notes_derived_from_name" : "pitch_quantizer_at_default_or_unknown_scale");
    }
    // the tuning table
    const int tn = get (0, "tuningName"), td = get (0, "tuningData");
    if (td >= 0)
    {
        entry (ItemKind::dropped, Reason::tuningUnsupported, "voice", "tuningData", isText (tn) ? str (tn) : "", "", "", "serum_tuning_table_not_imported", leavesUnder (d, td));
        consume (td);
    }
    if (tn >= 0 && ! used[static_cast<size_t> (tn)])
    {
        entry (ItemKind::dropped, Reason::tuningUnsupported, "voice", "tuningName", valueText (tn), "", "", "serum_tuning_table_not_imported");
        consume (tn);
    }
}

void Importer::unmappedTopLevel()
{
    // the strings of the preset header that the value tree repeats
    static const char* const metaKeys[] = { "presetName", "presetAuthor", "presetDescription", "presetComment", "tags", "fileType", "product", "productVersion", "url", "vendor", "version" };
    forMap (0, [&] (const std::string& name, int value, int keyNode)
    {
        if (used[static_cast<size_t> (value)]) { used[static_cast<size_t> (keyNode)] = 1; return; }
        for (const char* k : metaKeys)
            if (name == k)
            {
                entry (ItemKind::imported, Reason::exact, "meta", name, valueText (value), "", "", "preset_meta", leavesUnder (d, value));
                consume (value);
                return;
            }
        if (name == "lockOversampling" || name == "lockTuning" || name == "SerumGUI" || name == "ClipPlayer" || name == "Osc" || name == "WTOsc" || name == "SpectralOsc"
            || name == "GranularOsc" || name == "MultiSampleOsc" || name == "Filter" || name == "arpBankDisplayName" || name == "clipBankDisplayName")
        {
            entry (ItemKind::ignored, Reason::exact, "ui", name, "", "", "", "interface_state", leavesUnder (d, value));
            consume (value);
        }
    });
}

} // namespace serumimport
} // namespace synthyboy
