// SynthyBoy - core: Vital preset (.vital) importer (docs/ui-design/11-vital-preset-import.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pipeline: bytes -> bounded JSON tree -> version -> table driven mapping of the 730 one-to-one settings (vital_map.cpp) + the special
// bindings (modulations, effect order, routing, destinations, bend range, wavetables, LFO shapes, noise) -> ParamValues + PresetMeta
// + ImportReport. Pure memory, no I/O.

#include "synthyboy/vital_import.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <unordered_map>
#include <unordered_set>

#include "number.h"
#include "sha256.h"
#include "synthyboy/modulation.h"
#include "synthyboy/wavetable_render.h"
#include "vital_content.h"
#include "vital_import_internal.h"

namespace synthyboy
{

// Last valid index of a choice list before the WP0 appends, or -1 when the key's list has no appended entries.
int legacyChoiceMax (const char* key)
{
    const std::string k (key);
    const auto endsWith = [&k] (const char* suffix) { const std::string s (suffix); return k.size() >= s.size() && k.compare (k.size() - s.size(), s.size(), s) == 0; };
    if ((k.rfind ("filter", 0) == 0 || k.rfind ("fxfilter", 0) == 0) && endsWith (".style")) return 5;
    if ((k.rfind ("filter", 0) == 0 || k.rfind ("fxfilter", 0) == 0) && endsWith (".model")) return 7;
    if (k.rfind ("lfo", 0) == 0 && endsWith (".shape")) return 11;
    if (k.rfind ("rlfo", 0) == 0 && endsWith (".style")) return 3;
    if (k.rfind ("osc", 0) == 0 && endsWith (".wavetable")) return 12;
    if ((k.rfind ("osc", 0) == 0 || k.rfind ("noise", 0) == 0) && endsWith (".destination")) return 4;
    return -1;
}

namespace vitalimport
{
std::string cleanText (const std::string& in, size_t maxBytes, bool newlineToSpace, bool* cut)
{
    std::string out;
    out.reserve (std::min (in.size(), maxBytes) + 4);
    const auto* s = reinterpret_cast<const unsigned char*> (in.data());
    const size_t n = in.size();
    bool shortened = false;
    for (size_t i = 0; i < n;)
    {
        uint32_t cp;
        size_t len;
        const unsigned char c = s[i];
        if (c < 0x80)
        {
            cp = c;
            len = 1;
        }
        else
        {
            const int extra = (c >= 0xC2 && c <= 0xDF) ? 1 : ((c & 0xF0) == 0xE0 ? 2 : ((c >= 0xF0 && c <= 0xF4) ? 3 : -1));
            bool ok = extra > 0 && i + static_cast<size_t> (extra) < n;
            cp = extra == 1 ? (c & 0x1Fu) : (extra == 2 ? (c & 0x0Fu) : (c & 0x07u));
            if (ok)
                for (int k = 1; k <= extra; ++k)
                {
                    const unsigned char cc = s[i + static_cast<size_t> (k)];
                    if ((cc & 0xC0) != 0x80)
                    {
                        ok = false;
                        break;
                    }
                    cp = (cp << 6) | (cc & 0x3Fu);
                }
            if (ok && ((extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF))))
                ok = false; // overlong, surrogate, out of range
            if (ok)
                len = static_cast<size_t> (extra) + 1;
            else
            {
                cp = 0xFFFD;
                len = 1;
            }
        }
        i += len;
        if (cp < 0x20 || cp == 0x7F)
        {
            if (! newlineToSpace || (cp != '\n' && cp != '\r' && cp != '\t'))
                continue; // control characters are removed
            cp = ' ';
        }
        char enc[4];
        size_t el;
        if (cp < 0x80) { enc[0] = static_cast<char> (cp); el = 1; }
        else if (cp < 0x800) { enc[0] = static_cast<char> (0xC0 | (cp >> 6)); enc[1] = static_cast<char> (0x80 | (cp & 0x3F)); el = 2; }
        else if (cp < 0x10000) { enc[0] = static_cast<char> (0xE0 | (cp >> 12)); enc[1] = static_cast<char> (0x80 | ((cp >> 6) & 0x3F)); enc[2] = static_cast<char> (0x80 | (cp & 0x3F)); el = 3; }
        else { enc[0] = static_cast<char> (0xF0 | (cp >> 18)); enc[1] = static_cast<char> (0x80 | ((cp >> 12) & 0x3F)); enc[2] = static_cast<char> (0x80 | ((cp >> 6) & 0x3F)); enc[3] = static_cast<char> (0x80 | (cp & 0x3F)); el = 4; }
        if (out.size() + el > maxBytes)
        {
            shortened = true;
            break;
        }
        out.append (enc, el);
    }
    if (cut != nullptr)
        *cut = shortened;
    return out;
}
} // namespace vitalimport

namespace
{
using namespace vitalimport;
using json::Doc;
using json::Type;

// ---------------------------------------------------------------------------------------------------------------------
// static tables
// ---------------------------------------------------------------------------------------------------------------------
enum class RowKind : uint8_t { plain, frame, frameSpread, destination, warp, sampleOn, modSlot };

struct RowInfo
{
    int param = -1;
    RowKind kind = RowKind::plain;
    int osc = 0;   // 1..3 for frame rows
    int slot = -1; // 0..63 for modNN rows
    bool isOnSwitch = false;
    int destOsc = 0; // 1..3 for oscN destination, 4 for the noise destination (default routing)
};

struct Tables
{
    const VitalRow* rows = nullptr;
    size_t count = 0;
    std::vector<RowInfo> info;
    std::unordered_map<std::string, int> byControl;
    std::unordered_map<std::string, int> byKey;
    std::unordered_set<std::string> auxControls; // known controls without a row (ignored / unmappable / special)
    int volumeRow = -1;

    Tables()
    {
        rows = vitalRows (count);
        info.resize (count);
        for (size_t i = 0; i < count; ++i)
        {
            const VitalRow& r = rows[i];
            RowInfo& ri = info[i];
            const ParamInfo* p = findParamByKey (r.key);
            ri.param = p != nullptr ? findParamIndex (p->id) : -1;
            byControl[r.control] = static_cast<int> (i);
            byKey[r.key] = static_cast<int> (i);
            const std::string c = r.control, k = r.key;
            if (c.size() > 3 && c.compare (c.size() - 3, 3, "_on") == 0)
                ri.isOnSwitch = true;
            if (c.compare (0, 4, "osc_") == 0 && c.size() > 6)
            {
                const int n = c[4] - '0';
                const std::string tail = c.substr (6);
                if (tail == "wave_frame") { ri.kind = RowKind::frame; ri.osc = n; }
                else if (tail == "frame_spread") { ri.kind = RowKind::frameSpread; ri.osc = n; }
                else if (tail == "destination") { ri.kind = RowKind::destination; ri.destOsc = n; }
                else if (tail == "distortion_type") ri.kind = RowKind::warp;
            }
            if (c == "sample_destination") { ri.kind = RowKind::destination; ri.destOsc = 4; }
            if (c == "sample_on") ri.kind = RowKind::sampleOn;
            if (c == "volume") volumeRow = static_cast<int> (i);
            if (k.size() > 5 && k.compare (0, 3, "mod") == 0 && k[3] >= '0' && k[3] <= '9' && k[4] >= '0' && k[4] <= '9')
            {
                ri.kind = RowKind::modSlot;
                ri.slot = (k[3] - '0') * 10 + (k[4] - '0') - 1;
            }
        }
        for (const char* s : { "sub_on", "sub_direct_out", "sub_transpose", "sub_transpose_quantize", "sub_tune", "sub_level", "sub_pan", "sub_waveform",
                               "sample_random_phase", "sample_keytrack", "sample_loop", "sample_bounce", "sample_transpose", "sample_tune",
                               "sample_transpose_quantize", "voice_amplitude", "delay_aux_sync", "voice_priority", "voice_override", "oversampling",
                               "stereo_mode", "stereo_routing", "mpe_enabled", "bypass", "beats_per_minute", "view_spectrogram", "pitch_wheel",
                               "mod_wheel", "compressor_low_band_unused", "pitch_bend_range", "effect_chain_order", "filter_1_filter_input",
                               "filter_2_filter_input", "filter_fx_filter_input", "modulations", "wavetables", "sample", "lfos", "tuning" })
            auxControls.insert (s);
        for (int n = 1; n <= 3; ++n)
            for (const char* t : { "detune_range", "midi_track", "spectral_unison", "transpose_quantize", "smooth_interpolation", "view_2d" })
                auxControls.insert ("osc_" + std::to_string (n) + "_" + t);
        for (const char* f : { "filter_1_", "filter_2_", "filter_fx_" })
            for (const char* t : { "osc1_input", "osc2_input", "osc3_input", "sample_input" })
                auxControls.insert (std::string (f) + t);
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

// ---------------------------------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------------------------------
std::string num (double v)
{
    char buf[48];
    if (! std::isfinite (v))
        return v < 0 ? "-inf" : "inf";
    number::formatG9 (static_cast<float> (v), buf, sizeof buf);
    return buf;
}

std::string valueText (const ParamInfo& info, double v)
{
    char buf[96];
    valueToText (info, static_cast<float> (v), buf, sizeof buf);
    return buf;
}

std::string areaOf (const char* key)
{
    const char* dot = std::strchr (key, '.');
    const std::string prefix (key, dot != nullptr ? static_cast<size_t> (dot - key) : std::strlen (key));
    for (const char* fx : { "chorus", "compressor", "delay", "distortion", "eq", "flanger", "phaser", "reverb" })
        if (prefix == fx)
            return "fx." + prefix;
    if (prefix == "master")
        return "voice";
    if (prefix == "filter")
        return "filters";
    return prefix;
}

int parseVersion (const std::string& s)
{
    int part[3] = { 0, 0, 0 };
    int idx = 0;
    bool digits = false;
    for (char c : s)
    {
        if (c >= '0' && c <= '9')
        {
            part[idx] = std::min (part[idx] * 10 + (c - '0'), 255);
            digits = true;
        }
        else if (c == '.' && digits && idx < 2)
        {
            ++idx;
            digits = false;
        }
        else
            return -1;
    }
    if (! digits || idx != 2)
        return -1;
    return (part[0] << 16) | (part[1] << 8) | part[2];
}

struct Ctx
{
    const ImportOptions& opt;
    ImportResult& res;
    Doc doc;
    int settings = -1;
    std::unordered_map<std::string, int> members; // settings key -> node (the last one wins)
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool deadlineHit = false;
    bool cancelled = false;

    Ctx (const ImportOptions& o, ImportResult& r) : opt (o), res (r) {}

    bool shouldStop()
    {
        if (opt.cancel && opt.cancel())
        {
            cancelled = true;
            return true;
        }
        if (opt.limits.deadlineMs != 0
            && std::chrono::steady_clock::now() - start > std::chrono::milliseconds (opt.limits.deadlineMs))
        {
            deadlineHit = true;
            return true;
        }
        return false;
    }

    void progress (int percent, const char* step)
    {
        if (opt.progress)
            opt.progress (percent, step);
    }

    void item (ItemKind kind, Reason reason, std::string area, std::string vitalName, std::string vitalValue, std::string ourKey,
               std::string ourValue, std::string detail)
    {
        ImportReport& rep = res.report;
        switch (kind)
        {
            case ItemKind::approximated: ++rep.numApproximated; break;
            case ItemKind::dropped: ++rep.numDropped; break;
            case ItemKind::ignored: ++rep.numIgnored; break;
            case ItemKind::imported: return;
        }
        if (static_cast<int> (rep.items.size()) >= opt.limits.maxReportItems)
        {
            rep.truncated = true;
            return;
        }
        rep.items.push_back ({ kind, reason, std::move (area), std::move (vitalName), std::move (vitalValue), std::move (ourKey),
                               std::move (ourValue), std::move (detail) });
    }

    /** Numeric value of a settings member. `mismatch`: it was a string or a bool. */
    bool number (int node, double& out, bool& mismatch) const
    {
        mismatch = false;
        if (node < 0)
            return false;
        const json::Node& n = doc.nodes[static_cast<size_t> (node)];
        double v;
        switch (n.type)
        {
            case Type::number: v = n.number; break;
            case Type::boolean: v = n.boolean ? 1.0 : 0.0; mismatch = true; break;
            case Type::string:
            {
                const std::string& s = doc.text (n.str);
                const char* next = nullptr;
                size_t a = 0, b = s.size();
                while (a < b && (s[a] == ' ' || s[a] == '\t'))
                    ++a;
                while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
                    --b;
                if (a >= b || ! synthyboy::number::parseDecimal (s.data() + a, s.data() + b, false, v, next) || next != s.data() + b)
                    return false;
                mismatch = true;
                break;
            }
            default: return false;
        }
        if (! std::isfinite (v))
            return false;
        out = v;
        return true;
    }

    int member (const char* name) const
    {
        const auto it = members.find (name);
        return it == members.end() ? -1 : it->second;
    }

    /** Value of a setting by control name; reports a type mismatch. */
    bool setting (const char* name, double& out)
    {
        bool mismatch;
        const int n = member (name);
        if (! number (n, out, mismatch))
            return false;
        if (mismatch)
            item (ItemKind::approximated, Reason::typeMismatch, "meta", name, num (out), "", "", "number_expected");
        return true;
    }
};

double fromFile (const VitalRow& r, double x, bool portamentoTime, bool& unmappable)
{
    switch (r.conv)
    {
        case Conv::linear: return (x - r.b) / r.a;
        case Conv::sqrtScaled: return std::max (x, 0.0) * std::max (x, 0.0) / r.a;
        case Conv::quarticMs: return std::pow (std::max (x, 0.0), 4.0) * 1000.0;
        case Conv::log2Ms:
            if (portamentoTime && x <= r.a + 1.0e-4)
                return 0.0; // the floor of the portamento time is "off"
            return std::exp2 (std::min (x, 1000.0)) * 1000.0;
        case Conv::log2Hz: return std::exp2 (std::min (x, 1000.0));
        case Conv::log2PeriodMs: return 1000.0 / std::exp2 (std::min (x, 1000.0));
        case Conv::noteFromHz: return 8.1757989156437073 * std::exp2 (std::min (x, 12000.0) / 12.0);
        case Conv::index: return std::floor (x + 0.5) - r.b;
        case Conv::indexMap:
        {
            const double e = std::floor (x + 0.5);
            if (e >= 0.0 && e <= 8.0) return e;
            if (e == 10.0) return 9.0;
            if (e == 11.0) return 10.0;
            if (e == 9.0 || e == 12.0) { unmappable = true; return 0.0; }
            return e < 0.0 ? e : 100.0;
        }
        case Conv::volumeDb:
        {
            const double dB = std::sqrt (std::max (x, 0.0)) - 80.0;
            return dB <= -60.0 ? -60.0 : dB;
        }
    }
    return x;
}

const char* const kStyleFrom[] = { "Bass", "Lead", "Pad", "Keys", "Percussion", "Sequence", "Experimental", "SFX" };
const char* const kStyleTo[] = { "Bass", "Lead", "Pad", "Keys", "Pluck", "Lead", "FX", "FX" };

void jsonEscape (const std::string& s, std::string& out)
{
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char b[8];
                    std::snprintf (b, sizeof b, "\\u%04x", c);
                    out += b;
                }
                else
                    out += static_cast<char> (c);
        }
    }
    out += '"';
}

const char* kindName (ItemKind k)
{
    switch (k) { case ItemKind::imported: return "imported"; case ItemKind::approximated: return "approximated"; case ItemKind::dropped: return "dropped"; default: return "ignored"; }
}

const char* reasonName (Reason r)
{
    static const char* const names[] = { "exact", "clamped", "rounded", "enumRemapped", "legacyMigration", "defaulted", "typeMismatch", "truncated",
                                         "nearestWavetable", "nearestLfoShape", "unsupportedComponent", "renderFallback", "noEngineEquivalent",
                                         "customSample", "subOscillator", "tuningUnsupported", "modSourceUnsupported", "modDestinationUnsupported",
                                         "effectOrderInvalid", "limitExceeded", "tierNote" };
    return names[static_cast<int> (r)];
}

// ---------------------------------------------------------------------------------------------------------------------
// content
// ---------------------------------------------------------------------------------------------------------------------
void addContentNotes (Ctx& c, const ContentNotes& notes, const std::string& where)
{
    for (const auto& t : notes.unsupportedTypes)
        c.item (ItemKind::dropped, Reason::unsupportedComponent, "content", where + ".type", cleanText (t, 64, true), "", "", "unsupported_component");
    if (notes.limitExceeded)
        c.item (ItemKind::approximated, Reason::limitExceeded, "content", where, "", "", "", "content_limit_exceeded");
    if (notes.badFields > 0)
        c.item (ItemKind::approximated, Reason::typeMismatch, "content", where, std::to_string (notes.badFields), "", "", "content_field_dropped");
}

std::vector<int16_t> toInt16 (const std::vector<float>& frames)
{
    std::vector<int16_t> out (frames.size());
    for (size_t i = 0; i < frames.size(); ++i)
        out[i] = static_cast<int16_t> (std::lround (std::max (-1.0f, std::min (1.0f, frames[i])) * 32767.0f));
    return out;
}

wtrender::Status renderParsed (Ctx& c, const ParsedWavetable& pw, wtrender::Result& result)
{
    wtrender::Options o;
    o.cancel = [&c] { return c.shouldStop(); };
    return wtrender::render (pw.desc, o, result);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// public: small functions
// ---------------------------------------------------------------------------------------------------------------------
size_t vitalRowCount() noexcept
{
    return tables().count;
}
const char* vitalRowKey (size_t row) noexcept
{
    return row < tables().count ? tables().rows[row].key : "";
}
const char* vitalRowControl (size_t row) noexcept
{
    return row < tables().count ? tables().rows[row].control : "";
}

bool decodeVitalEffectOrder (double value, int order[9]) noexcept
{
    for (int i = 0; i < 9; ++i)
        order[i] = i;
    if (! std::isfinite (value) || value < 0.0 || value >= 362880.0 || value != std::floor (value))
        return false;
    long code = static_cast<long> (value);
    int work[9];
    for (int i = 0; i < 9; ++i)
        work[i] = i;
    for (int i = 0; i < 9; ++i)
    {
        const int r = 9 - i;
        const int inv = static_cast<int> (code % r);
        code /= r;
        const int idx = r - 1 - inv;
        const int taken = work[idx];
        for (int j = idx; j < r - 1; ++j)
            work[j] = work[j + 1];
        work[r - 1] = taken;
    }
    for (int i = 0; i < 9; ++i)
        order[i] = work[i];
    return true;
}

std::string ImportReport::toJson() const
{
    std::string s;
    s.reserve (512 + items.size() * 220);
    s += "{\"format\":\"synthyboy-import-report\",\"version\":1,\"vital_version\":";
    jsonEscape (vitalVersion, s);
    s += ",\"fidelity\":\"";
    s += fidelity == Fidelity::exact ? "exact" : (fidelity == Fidelity::close ? "close" : "degraded");
    s += "\",\"counts\":{\"imported\":" + std::to_string (numImported) + ",\"approximated\":" + std::to_string (numApproximated)
       + ",\"dropped\":" + std::to_string (numDropped) + ",\"ignored\":" + std::to_string (numIgnored) + ",\"defaulted\":" + std::to_string (numDefaulted)
       + ",\"settings_seen\":" + std::to_string (vitalSettingsSeen) + ",\"unknown_keys\":" + std::to_string (vitalUnknownKeys) + "}";
    s += std::string (",\"truncated\":") + (truncated ? "true" : "false") + ",\"items\":[";
    bool first = true;
    for (const auto& it : items)
    {
        if (! first)
            s += ',';
        first = false;
        s += "{\"kind\":\"";
        s += kindName (it.kind);
        s += "\",\"reason\":\"";
        s += reasonName (it.reason);
        s += "\",\"area\":";
        jsonEscape (it.area, s);
        s += ",\"vital_name\":";
        jsonEscape (it.vitalName, s);
        s += ",\"vital_value\":";
        jsonEscape (it.vitalValue, s);
        s += ",\"our_key\":";
        jsonEscape (it.ourKey, s);
        s += ",\"our_value\":";
        jsonEscape (it.ourValue, s);
        s += ",\"detail\":";
        jsonEscape (it.detail, s);
        s += '}';
    }
    s += "]}";
    return s;
}

bool looksLikeVitalPreset (const void* data, size_t size) noexcept
{
    if (data == nullptr || size < 2)
        return false;
    const char* p = static_cast<const char*> (data);
    const size_t n = std::min<size_t> (size, 64u << 10);
    size_t i = 0;
    if (n >= 3 && static_cast<unsigned char> (p[0]) == 0xEF && static_cast<unsigned char> (p[1]) == 0xBB && static_cast<unsigned char> (p[2]) == 0xBF)
        i = 3;
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r'))
        ++i;
    if (i >= n || p[i] != '{')
        return false;
    static const char kNeedle[] = "\"settings\"";
    for (; i + sizeof kNeedle - 1 <= n; ++i)
        if (std::memcmp (p + i, kNeedle, sizeof kNeedle - 1) == 0)
            return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// the preset import
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
ImportStatus importPresetImpl (Ctx& c, const void* data, size_t size, const char* fallbackName)
{
    ImportResult& res = c.res;
    const ImportOptions& opt = c.opt;
    const Tables& T = tables();
    ImportReport& rep = res.report;

    c.progress (0, "Reading");
    const json::Status js = json::parse (static_cast<const char*> (data), size, opt.limits.maxDepth, opt.limits.maxWorkingBytes, c.doc);
    if (js != json::Status::ok)
    {
        res.error = js == json::Status::budget ? "working memory budget exceeded" : (js == json::Status::tooDeep ? "JSON nesting too deep" : "malformed JSON");
        res.error += " at byte " + std::to_string (c.doc.errorOffset);
        return js == json::Status::budget ? ImportStatus::budgetExceeded : ImportStatus::malformed;
    }
    const json::Node& root = c.doc.root();
    if (root.type != Type::object)
    {
        res.error = "root is not an object";
        return ImportStatus::notVital;
    }
    c.settings = c.doc.find (0, "settings");
    if (c.settings < 0 || c.doc.nodes[static_cast<size_t> (c.settings)].type != Type::object)
    {
        res.error = (c.doc.find (0, "groups") >= 0 || c.doc.find (0, "num_points") >= 0) ? "stand-alone wavetable / LFO file, not a preset" : "no settings object";
        return ImportStatus::notVital;
    }

    res.sourceSha256 = Sha256::hashBufferHex (data, size);
    res.values = ParamValues::defaults();
    ParamValues& V = res.values;
    const ParamValues defaults = ParamValues::defaults();

    // ---- settings members (the last duplicate wins) -----------------------------------------------------------------------
    const json::Node& sn = c.doc.nodes[static_cast<size_t> (c.settings)];
    for (int m = sn.first; m >= 0; m = c.doc.nodes[static_cast<size_t> (m)].next)
    {
        const json::Node& mn = c.doc.nodes[static_cast<size_t> (m)];
        if (mn.key < 0)
            continue;
        c.members[c.doc.text (mn.key)] = m;
    }
    rep.vitalSettingsSeen = static_cast<int> (c.members.size());
    for (int m = sn.first; m >= 0; m = c.doc.nodes[static_cast<size_t> (m)].next) // file order, for the unknown names
    {
        const json::Node& mn = c.doc.nodes[static_cast<size_t> (m)];
        if (mn.key < 0)
            continue;
        const std::string& name = c.doc.text (mn.key);
        if (c.members[name] != m)
            continue; // an earlier duplicate
        if (T.byControl.count (name) != 0 || T.auxControls.count (name) != 0)
            continue;
        ++rep.vitalUnknownKeys;
        if (res.unknownKeys.size() < 32)
            res.unknownKeys.push_back (cleanText (name, 96, true));
    }

    // ---- top level metadata and version -------------------------------------------------------------------------------------
    const auto topString = [&] (const char* key, size_t maxBytes, bool newlineToSpace, bool* cut = nullptr)
    {
        const int n = c.doc.find (0, key);
        if (n < 0 || c.doc.nodes[static_cast<size_t> (n)].type != Type::string)
            return std::string();
        return cleanText (c.doc.text (c.doc.nodes[static_cast<size_t> (n)].str), maxBytes, newlineToSpace, cut);
    };
    {
        const int vn = c.doc.find (0, "synth_version");
        std::string vs;
        if (vn >= 0 && c.doc.nodes[static_cast<size_t> (vn)].type == Type::string)
            vs = cleanText (c.doc.text (c.doc.nodes[static_cast<size_t> (vn)].str), 32, true);
        rep.vitalVersion = vs;
        int version = parseVersion (vs);
        if (version < 0)
        {
            version = 0x010000;
            c.item (ItemKind::ignored, Reason::defaulted, "meta", "synth_version", vs, "", "", "version_unknown");
        }
        else if (version > 0x010505)
            c.item (ItemKind::ignored, Reason::defaulted, "meta", "synth_version", vs, "", "", "version_newer");
        // < 0.5.0: the migrations below apply per setting (a missing osc_3_*, a missing osc_N_destination)
        (void) version;
    }

    bool cutName = false, cutAuthor = false, cutComments = false;
    std::string name = topString ("preset_name", 255, false, &cutName);
    if (name.empty())
        name = (fallbackName != nullptr && fallbackName[0] != '\0') ? cleanText (fallbackName, 255, false) : std::string ("Imported");
    res.meta.name = name;
    res.meta.author = topString ("author", 255, false, &cutAuthor);
    std::string comments = topString ("comments", 100000, true);
    {
        const std::string desc = cleanText (comments, 250, true, &cutComments);
        res.meta.description = cutComments ? desc + "\xE2\x80\xA6" : desc;
    }
    for (int i = 0; i < 4; ++i)
        res.macroNames[i] = topString (("macro" + std::to_string (i + 1)).c_str(), 32, true);
    if (cutName || cutAuthor || cutComments)
        c.item (ItemKind::approximated, Reason::truncated, "meta", cutName ? "preset_name" : (cutAuthor ? "author" : "comments"), "", "", "", "text_truncated");
    res.style = topString ("preset_style", 60, true);
    res.meta.category = "Lead";
    res.categoryNeedsChoice = true;
    for (size_t i = 0; i < sizeof kStyleFrom / sizeof kStyleFrom[0]; ++i)
        if (res.style == kStyleFrom[i])
        {
            res.meta.category = kStyleTo[i];
            res.categoryNeedsChoice = false;
            if (res.style != kStyleTo[i])
                c.item (ItemKind::approximated, Reason::enumRemapped, "meta", "preset_style", res.style, "", kStyleTo[i], "style_mapped");
        }
    res.meta.license = "Imported, user-supplied; original terms apply";
    {
        std::string style = res.style;
        style.erase (std::remove (style.begin(), style.end(), ','), style.end());
        res.meta.tags = "imported,vital," + (style.empty() ? std::string() : "vital-style:" + style + ",") + "src:" + res.sourceSha256.substr (0, 12);
        if (res.meta.tags.size() > 255)
            res.meta.tags = "imported,vital,src:" + res.sourceSha256.substr (0, 12);
    }

    // ---- which LFOs are used by a modulation connection -------------------------------------------------------------------
    bool lfoUsed[8] = {};
    const int modsNode = c.member ("modulations");
    const bool modsIsArray = modsNode >= 0 && c.doc.nodes[static_cast<size_t> (modsNode)].type == Type::array;
    if (modsIsArray)
        for (int e = c.doc.nodes[static_cast<size_t> (modsNode)].first; e >= 0; e = c.doc.nodes[static_cast<size_t> (e)].next)
        {
            const int sn2 = c.doc.find (e, "source");
            if (sn2 < 0 || c.doc.nodes[static_cast<size_t> (sn2)].type != Type::string)
                continue;
            const std::string& s = c.doc.text (c.doc.nodes[static_cast<size_t> (sn2)].str);
            if (s.size() == 5 && s.compare (0, 4, "lfo_") == 0 && s[4] >= '1' && s[4] <= '8')
                lfoUsed[s[4] - '1'] = true;
        }

    // ---- embedded wavetables --------------------------------------------------------------------------------------------------
    double frameScaleFrames[3] = { 257.0, 257.0, 257.0 };
    ContentNotes notes;
    const int wtNode = c.member ("wavetables");
    if (opt.renderWavetables && wtNode >= 0 && c.doc.nodes[static_cast<size_t> (wtNode)].type == Type::array)
    {
        int osc = 0;
        for (int w = c.doc.nodes[static_cast<size_t> (wtNode)].first; w >= 0; w = c.doc.nodes[static_cast<size_t> (w)].next, ++osc)
        {
            if (osc >= 3)
            {
                c.item (ItemKind::approximated, Reason::limitExceeded, "content", "wavetables", std::to_string (c.doc.nodes[static_cast<size_t> (wtNode)].count), "", "", "wavetables_beyond_3_ignored");
                break;
            }
            c.progress (10 + osc * 25, "Rendering wavetables");
            const std::string where = "wavetables[" + std::to_string (osc) + "]";
            const std::string oscKey = "osc" + std::to_string (osc + 1) + ".wavetable";
            const ParamInfo* pinfo = findParamByKey (oscKey.c_str());
            ParsedWavetable pw;
            ContentNotes wn;
            wn.decodedBytes = notes.decodedBytes;
            if (! parseWavetable (c.doc, w, pw, wn))
            {
                c.item (ItemKind::dropped, Reason::renderFallback, "content", where, "", oscKey, "", "wavetable_invalid");
                continue;
            }
            notes.decodedBytes = wn.decodedBytes;
            addContentNotes (c, wn, where);
            wtrender::Result r;
            const wtrender::Status st = renderParsed (c, pw, r);
            if (st == wtrender::Status::cancelled)
                return c.deadlineHit ? ImportStatus::budgetExceeded : ImportStatus::cancelled;
            if (st == wtrender::Status::budgetExceeded)
                return ImportStatus::budgetExceeded;
            const size_t pidx = static_cast<size_t> (findParamIndex (pinfo->id));
            const int shortcut = matchBuiltinSingleCycle (r.frames.data(), r.numFrames);
            if (shortcut >= 0)
            {
                V.plain[pidx] = static_cast<float> (shortcut);
                continue; // exact built-in: nothing stored, frame fraction as for a built-in
            }
            frameScaleFrames[osc] = r.numFrames;
            if (opt.caps.customWavetables)
            {
                V.plain[pidx] = static_cast<float> (kWavetablePresetTable);
                ImportedWavetable iw;
                iw.osc = osc + 1;
                iw.name = pw.name;
                iw.author = pw.author;
                iw.numFrames = r.numFrames;
                iw.frames = toInt16 (r.frames);
                iw.sha256 = Sha256::hashBufferHex (iw.frames.data(), iw.frames.size() * sizeof (int16_t));
                res.wavetables.push_back (std::move (iw));
            }
            else
            {
                double distance = 0.0;
                const int nearest = nearestBuiltinWavetable (r.frames.data(), r.numFrames, distance);
                V.plain[pidx] = static_cast<float> (nearest);
                char dist[32];
                std::snprintf (dist, sizeof dist, "%.1f dB", distance);
                c.item (ItemKind::approximated, Reason::nearestWavetable, "content", where, pw.name, oscKey, valueText (*pinfo, nearest),
                        std::string ("nearest_builtin; distance ") + dist);
            }
        }
    }

    // ---- embedded LFO shapes ------------------------------------------------------------------------------------------------------
    const int lfoNode = c.member ("lfos");
    if (lfoNode >= 0 && c.doc.nodes[static_cast<size_t> (lfoNode)].type == Type::array)
    {
        int n = 0;
        for (int l = c.doc.nodes[static_cast<size_t> (lfoNode)].first; l >= 0; l = c.doc.nodes[static_cast<size_t> (l)].next, ++n)
        {
            if (n >= 8)
            {
                c.item (ItemKind::approximated, Reason::limitExceeded, "content", "lfos", std::to_string (c.doc.nodes[static_cast<size_t> (lfoNode)].count), "", "", "lfos_beyond_8_ignored");
                break;
            }
            const std::string where = "lfos[" + std::to_string (n) + "]";
            const std::string key = "lfo" + std::to_string (n + 1) + ".shape";
            const ParamInfo* pinfo = findParamByKey (key.c_str());
            ParsedLfo pl;
            ContentNotes ln;
            if (! parseLfoLine (c.doc, l, pl, ln))
            {
                c.item (lfoUsed[n] ? ItemKind::dropped : ItemKind::ignored, Reason::typeMismatch, "content", where, "", key, "", "lfo_shape_invalid");
                continue;
            }
            float samples[kLfoRefSamples];
            wtrender::renderLine (pl.line, samples, kLfoRefSamples);
            int nearest = 0;
            double err = 0.0;
            const int match = matchBuiltinLfoShape (samples, nearest, err);
            const size_t pidx = static_cast<size_t> (findParamIndex (pinfo->id));
            if (match >= 0)
            {
                V.plain[pidx] = static_cast<float> (nearest);
                continue;
            }
            if (opt.caps.customLfoShapes)
            {
                V.plain[pidx] = static_cast<float> (kLfoShapeCustom);
                ImportedLfoShape s;
                s.lfo = n + 1;
                s.name = pl.name;
                s.smooth = pl.line.smooth;
                for (size_t i = 0; i < pl.line.x.size(); ++i)
                    s.points.push_back ({ pl.line.x[i], pl.line.y[i], pl.line.power[i] });
                res.lfoShapes.push_back (std::move (s));
            }
            else
            {
                V.plain[pidx] = static_cast<float> (nearest);
                if (lfoUsed[n])
                {
                    char e[32];
                    std::snprintf (e, sizeof e, "%.3f", err);
                    c.item (ItemKind::approximated, Reason::nearestLfoShape, "content", where, pl.name, key, valueText (*pinfo, nearest),
                            std::string ("nearest_builtin; rms error ") + e);
                }
            }
        }
    }

    // ---- sample (noise) ------------------------------------------------------------------------------------------------------------
    bool customSample = false;
    {
        const int sNode = c.member ("sample");
        if (sNode >= 0 && c.doc.nodes[static_cast<size_t> (sNode)].type == Type::object)
        {
            const int nn = c.doc.find (sNode, "name");
            const std::string sname = nn >= 0 && c.doc.nodes[static_cast<size_t> (nn)].type == Type::string
                ? cleanText (c.doc.text (c.doc.nodes[static_cast<size_t> (nn)].str), 255, true) : std::string();
            customSample = ! sname.empty() && sname != "White Noise";
            if (customSample)
            {
                double on = 0.0;
                bool mm;
                c.number (c.member ("sample_on"), on, mm);
                const int ln = c.doc.find (sNode, "length"), rn = c.doc.find (sNode, "sample_rate");
                double len = 0.0, sr = 0.0;
                c.number (ln, len, mm);
                c.number (rn, sr, mm);
                c.item (on != 0.0 ? ItemKind::dropped : ItemKind::ignored, Reason::customSample, "noise", "sample", sname + " (" + num (len) + " samples, " + num (sr) + " Hz)",
                        "noise.on", "Off", "custom_sample_unsupported");
            }
        }
    }

    // ---- modulation connections (resolved before the table loop: it needs to know the active slots) -----------------------
    bool slotActive[kNumModSlots] = {};
    std::string slotSource[kNumModSlots], slotDestination[kNumModSlots];
    int slotSourceChoice[kNumModSlots] = {}, slotDestChoice[kNumModSlots] = {};
    int modCount = 0;
    if (modsIsArray)
    {
        int i = 0;
        for (int e = c.doc.nodes[static_cast<size_t> (modsNode)].first; e >= 0; e = c.doc.nodes[static_cast<size_t> (e)].next, ++i)
        {
            if (i >= kNumModSlots)
            {
                c.item (ItemKind::approximated, Reason::limitExceeded, "mod", "modulations", std::to_string (c.doc.nodes[static_cast<size_t> (modsNode)].count), "", "", "modulations_beyond_64_ignored");
                break;
            }
            ++modCount;
            char area[8];
            std::snprintf (area, sizeof area, "mod%02d", i + 1);
            const std::string where = "modulations[" + std::to_string (i) + "]";
            const int sn2 = c.doc.find (e, "source"), dn = c.doc.find (e, "destination");
            if (sn2 < 0 || dn < 0 || c.doc.nodes[static_cast<size_t> (sn2)].type != Type::string || c.doc.nodes[static_cast<size_t> (dn)].type != Type::string)
            {
                c.item (ItemKind::dropped, Reason::typeMismatch, area, where, "", "", "", "modulation_entry_invalid");
                continue;
            }
            const std::string src = cleanText (c.doc.text (c.doc.nodes[static_cast<size_t> (sn2)].str), 96, true);
            const std::string dst = cleanText (c.doc.text (c.doc.nodes[static_cast<size_t> (dn)].str), 96, true);
            slotSource[i] = src;
            slotDestination[i] = dst;

            // source: the Vital name -> our source key
            std::string ours;
            if (src.compare (0, 4, "env_") == 0) ours = "env" + src.substr (4);
            else if (src.compare (0, 4, "lfo_") == 0) ours = "lfo" + src.substr (4);
            else if (src.compare (0, 7, "random_") == 0) ours = "rlfo" + src.substr (7);
            else if (src.compare (0, 14, "macro_control_") == 0) ours = "macro" + src.substr (14);
            else ours = src;
            int choice = 0;
            for (size_t s = 0; s < modSourceCount(); ++s)
                if (ours == modSourceTable()[s].key)
                    choice = static_cast<int> (s) + 1;
            double amount = 0.0;
            bool mm;
            c.number (c.member (("modulation_" + std::to_string (i + 1) + "_amount").c_str()), amount, mm);
            if (choice == 0)
            {
                c.item (ItemKind::dropped, Reason::modSourceUnsupported, area, where, src + " -> " + dst + " amount " + num (amount), "", "", "source_not_exposed");
                continue;
            }
            const auto di = T.byControl.find (dst);
            int destChoice = 0;
            if (di != T.byControl.end() && T.info[static_cast<size_t> (di->second)].param >= 0)
                destChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (T.info[static_cast<size_t> (di->second)].param)));
            if (destChoice == 0)
            {
                c.item (ItemKind::dropped, Reason::modDestinationUnsupported, area, where, src + " -> " + dst + " amount " + num (amount), "", "", "destination_not_modulatable");
                continue;
            }
            slotSourceChoice[i] = choice;
            slotDestChoice[i] = destChoice;
            slotActive[i] = true;
            if (c.doc.find (e, "line_mapping") >= 0)
                c.item (ItemKind::approximated, Reason::noEngineEquivalent, area, where + ".line_mapping", src + " -> " + dst, "", "", "line_map_curve_dropped");
            const ModSlotParams sp = modSlotParams (i);
            V.plain[sp.source] = static_cast<float> (choice);
            V.plain[sp.destination] = static_cast<float> (destChoice);
        }
    }

    // ---- the table loop ---------------------------------------------------------------------------------------------------------------
    c.progress (60, "Mapping");
    if (c.shouldStop())
        return c.deadlineHit ? ImportStatus::budgetExceeded : ImportStatus::cancelled;
    const bool* active = slotActive;
    for (size_t ri = 0; ri < T.count; ++ri)
    {
        const VitalRow& row = T.rows[ri];
        const RowInfo& info = T.info[ri];
        if (info.param < 0)
            continue;
        if (info.kind == RowKind::modSlot && ! active[info.slot])
            continue; // unused slot: its values are ignored (design 1.1)
        const ParamInfo& pinfo = paramTable()[info.param];
        const std::string area = areaOf (pinfo.key);
        bool mismatch = false;
        double x = 0.0;
        const int node = c.member (row.control);
        if (! c.number (node, x, mismatch))
        {
            // missing setting (design 1.1): our default, except the switches and the oscillator destinations (Vital's defaults)
            float def = defaults.plain[info.param];
            if (info.isOnSwitch)
                def = std::strcmp (row.control, "osc_1_on") == 0 ? 1.0f : 0.0f;
            else if (info.kind == RowKind::destination)
            {
                static const float kDest[3] = { 0.0f, 1.0f, 3.0f };
                if (info.destOsc <= 3)
                    def = kDest[info.destOsc - 1];
                // legacy (< 0.5.0) routing from filter_N_oscM_input / filter_N_sample_input
                double f1 = 0.0, f2 = 0.0;
                bool m1, m2;
                const std::string part = info.destOsc == 4 ? std::string ("sample") : "osc" + std::to_string (info.destOsc);
                const std::string a = "filter_1_" + part + "_input", b = "filter_2_" + part + "_input";
                const bool h1 = c.number (c.member (a.c_str()), f1, m1), h2 = c.number (c.member (b.c_str()), f2, m2);
                if (h1 || h2)
                {
                    const bool on1 = f1 != 0.0, on2 = f2 != 0.0;
                    def = on1 && on2 ? 2.0f : (on1 ? 0.0f : (on2 ? 1.0f : 3.0f));
                    c.item (ItemKind::approximated, Reason::legacyMigration, area, row.control, num (f1) + "," + num (f2), pinfo.key, valueText (pinfo, def), "routing_migrated");
                }
            }
            V.plain[info.param] = def;
            ++rep.numDefaulted;
            continue;
        }
        if (mismatch)
            c.item (ItemKind::approximated, Reason::typeMismatch, area, row.control, num (x), pinfo.key, "", "number_expected");

        bool unmappable = false;
        double want;
        Reason reason = Reason::clamped;
        bool remapped = false;
        if (info.kind == RowKind::frame || info.kind == RowKind::frameSpread)
        {
            want = x / std::max(1.0, frameScaleFrames[info.osc - 1] - 1.0) * 100.0;
        }
        else if (info.kind == RowKind::destination)
        {
            const double e = std::floor (x + 0.5);
            if (e >= 0.0 && e <= 4.0) want = e;
            else if (e > 4.0 && e <= 14.0) { want = 3.0; remapped = true; reason = Reason::enumRemapped; } // send to one effect -> Effects only
            else want = e;
        }
        else if (info.kind == RowKind::sampleOn)
        {
            want = customSample ? 0.0 : std::floor (x + 0.5);
        }
        else
        {
            want = fromFile (row, x, std::strcmp (pinfo.key, "master.portamento.time") == 0, unmappable);
            if (unmappable) { remapped = true; reason = Reason::enumRemapped; }
        }
        // Choice lists that got values appended (13-new-keys.md section 3): a Vital file only knows the original range, so
        // an out-of-range index must clamp to the original last entry, never land on a new Serum-only entry.
        bool legacyCapped = false;
        if (const int legacyMax = legacyChoiceMax (pinfo.key); legacyMax >= 0 && want > legacyMax)
        {
            want = legacyMax;
            legacyCapped = true;
        }
        if (pinfo.interval > 0.0f)
            want = std::floor (want / pinfo.interval + 0.5) * pinfo.interval;
        const float got = constrainValue (pinfo, static_cast<float> (std::max (-1.0e30, std::min (1.0e30, want))));
        V.plain[info.param] = got;
        const bool clamped = (pinfo.flags & kParamStepped) != 0 ? std::fabs (want - got) > 0.5
                                                               : std::fabs (want - got) > 1.0e-4 * (pinfo.max - pinfo.min);
        if (remapped || clamped || legacyCapped)
        {
            const bool audibleOff = info.kind == RowKind::warp && reason == Reason::enumRemapped;
            (void) audibleOff;
            c.item (ItemKind::approximated, remapped ? reason : Reason::clamped, area, row.control, num (x), pinfo.key, valueText (pinfo, got),
                    unmappable ? "warp_from_sample_unsupported" : (remapped ? "send_to_effect_mapped_to_effects_only" : "outside_our_range"));
        }
        else
        {
            ++rep.numImported;
            rep.imported.push_back ({ static_cast<uint16_t> (ri), static_cast<float> (x), got });
        }
    }

    // ---- special bindings ---------------------------------------------------------------------------------------------------------
    // modulation slot values of connections that exist are handled by the loop above; bend range, voice amplitude
    {
        double v;
        const ParamInfo* bend = findParamByKey ("master.bend_range");
        if (c.setting ("pitch_bend_range", v))
        {
            const size_t bi = static_cast<size_t> (findParamIndex (bend->id));
            const double want = std::floor (v + 0.5);
            const float got = constrainValue (*bend, static_cast<float> (want));
            V.plain[bi] = got;
            if (want != got)
                c.item (ItemKind::approximated, Reason::clamped, "voice", "pitch_bend_range", num (v), bend->key, valueText (*bend, got), "outside_our_range");
            else
            {
                ++rep.numImported;
                rep.imported.push_back ({ 0xFFFF, static_cast<float> (v), got });
            }
        }
        else
            ++rep.numDefaulted;

        if (c.setting ("voice_amplitude", v) && std::fabs (v - 1.0) > 1.0e-6)
        {
            const ParamInfo* vol = findParamByKey ("master.volume");
            const size_t vi = static_cast<size_t> (findParamIndex (vol->id));
            const double dB = V.plain[vi] + 20.0 * std::log10 (std::max (v, 1.0e-6));
            V.plain[vi] = constrainValue (*vol, static_cast<float> (std::max (dB, -60.0)));
            c.item (ItemKind::approximated, Reason::noEngineEquivalent, "voice", "voice_amplitude", num (v), vol->key, valueText (*vol, V.plain[vi]), "folded_into_master_volume");
        }
        else if (c.member ("voice_amplitude") >= 0)
            c.item (ItemKind::ignored, Reason::noEngineEquivalent, "voice", "voice_amplitude", "1", "", "", "unity_gain");
    }

    // filter routing
    {
        double a = 0.0, b = 0.0;
        const bool ha = c.setting ("filter_1_filter_input", a), hb = c.setting ("filter_2_filter_input", b);
        if (ha || hb)
        {
            const ParamInfo* p = findParamByKey ("filter.routing");
            const size_t pi = static_cast<size_t> (findParamIndex (p->id));
            const bool f1 = a != 0.0, f2 = b != 0.0;
            // (0,0) parallel, (0,1) series 1 -> 2, (1,0) series 2 -> 1, (1,1) would be a loop: parallel + report
            const float v = f1 && f2 ? 0.0f : (f2 ? 1.0f : (f1 ? 2.0f : 0.0f));
            V.plain[pi] = v;
            if (f1 && f2)
                c.item (ItemKind::approximated, Reason::enumRemapped, "filters", "filter_1_filter_input,filter_2_filter_input", "1,1", p->key, valueText (*p, v), "filter_loop_to_parallel");
            else
            {
                ++rep.numImported;
                rep.imported.push_back ({ 0xFFFF, static_cast<float> (a * 2 + b), v });
            }
        }
        else
            ++rep.numDefaulted;
    }

    // effect order
    {
        double code;
        if (c.setting ("effect_chain_order", code))
        {
            int order[9];
            const bool ok = decodeVitalEffectOrder (code, order);
            for (int i = 0; i < 9; ++i)
            {
                const std::string key = "fx.order." + std::to_string (i + 1);
                V.plain[findParamIndex (findParamByKey (key.c_str())->id)] = static_cast<float> (order[i]);
            }
            if (! ok)
                c.item (ItemKind::approximated, Reason::effectOrderInvalid, "fx", "effect_chain_order", num (code), "fx.order.1..9", "identity", "effect_order_invalid");
            else
            {
                ++rep.numImported;
                rep.imported.push_back ({ 0xFFFF, static_cast<float> (code), static_cast<float> (order[0]) });
            }
        }
        else
            ++rep.numDefaulted;
    }

    // oscillator / sample / sub / tuning items that have no counterpart
    {
        const auto isOn = [&] (const char* control) { double v = 0.0; bool m; return c.number (c.member (control), v, m) && v != 0.0; };
        for (int n = 1; n <= 3; ++n)
        {
            const std::string p = "osc_" + std::to_string (n) + "_";
            const bool on = isOn ((p + "on").c_str());
            const std::string area = "osc" + std::to_string (n);
            struct U { const char* tail; double expected; };
            for (const U& u : { U { "detune_range", 2.0 }, U { "midi_track", 1.0 }, U { "spectral_unison", 1.0 }, U { "transpose_quantize", 0.0 } })
            {
                double v;
                bool m;
                const std::string name = p + u.tail;
                if (! c.number (c.member (name.c_str()), v, m))
                    continue;
                if (v != u.expected)
                    c.item (on ? ItemKind::dropped : ItemKind::ignored, Reason::noEngineEquivalent, area, name, num (v), "", "", "no_engine_equivalent");
                else
                    c.item (ItemKind::ignored, Reason::noEngineEquivalent, area, name, num (v), "", "", "engine_fixed_value");
            }
            for (const char* tail : { "smooth_interpolation", "view_2d" })
            {
                const std::string name = p + tail;
                double v;
                bool m;
                if (c.number (c.member (name.c_str()), v, m))
                    c.item (ItemKind::ignored, Reason::noEngineEquivalent, area, name, num (v), "", "", "quality_tier_or_ui");
            }
        }
        // sub oscillator: one item
        if (isOn ("sub_on"))
        {
            double lvl = 0.0, wave = 0.0, tr = 0.0;
            bool m;
            c.number (c.member ("sub_level"), lvl, m);
            c.number (c.member ("sub_waveform"), wave, m);
            c.number (c.member ("sub_transpose"), tr, m);
            c.item (ItemKind::dropped, Reason::subOscillator, "sub", "sub_on", "level " + num (lvl) + " waveform " + num (wave) + " transpose " + num (tr), "", "", "no_sub_oscillator");
        }
        else if (c.member ("sub_on") >= 0)
            c.item (ItemKind::ignored, Reason::noEngineEquivalent, "sub", "sub_on", "0", "", "", "not_part_of_our_model");
        for (const char* name : { "sub_direct_out", "sub_transpose", "sub_transpose_quantize", "sub_tune", "sub_level", "sub_pan", "sub_waveform",
                                  "sample_random_phase", "sample_keytrack", "sample_loop", "sample_bounce", "sample_transpose", "sample_tune",
                                  "sample_transpose_quantize", "voice_priority", "voice_override", "oversampling", "stereo_mode", "stereo_routing",
                                  "mpe_enabled", "bypass", "beats_per_minute", "view_spectrogram", "pitch_wheel", "mod_wheel", "compressor_low_band_unused",
                                  "filter_fx_filter_input" })
        {
            double v;
            bool m;
            if (c.number (c.member (name), v, m))
                c.item (ItemKind::ignored, Reason::noEngineEquivalent, std::strncmp (name, "sub_", 4) == 0 ? "sub" : "voice", name, num (v), "", "", "not_part_of_our_model");
        }
        for (const char* f : { "filter_1_", "filter_2_", "filter_fx_" })
            for (const char* t : { "osc1_input", "osc2_input", "osc3_input", "sample_input" })
            {
                const std::string name = std::string (f) + t;
                double v;
                bool m;
                if (c.number (c.member (name.c_str()), v, m))
                    c.item (ItemKind::ignored, Reason::legacyMigration, "filters", name, num (v), "", "", "legacy_routing");
            }
        double a, b;
        if (c.setting ("delay_aux_sync", a) && c.setting ("delay_sync", b) && a != b)
            c.item (ItemKind::approximated, Reason::noEngineEquivalent, "fx.delay", "delay_aux_sync", num (a), "delay.sync", "", "aux_sync_follows_delay_sync");
        else if (c.member ("delay_aux_sync") >= 0)
            c.item (ItemKind::ignored, Reason::noEngineEquivalent, "fx.delay", "delay_aux_sync", "", "", "", "same_as_delay_sync");

        const int tn = c.member ("tuning");
        if (tn >= 0 && c.doc.nodes[static_cast<size_t> (tn)].type == Type::object && c.doc.nodes[static_cast<size_t> (tn)].count > 0)
        {
            std::string scale;
            for (const char* k : { "tuning_name", "name", "scale_name" })
            {
                const int nn = c.doc.find (tn, k);
                if (nn >= 0 && c.doc.nodes[static_cast<size_t> (nn)].type == Type::string)
                {
                    scale = cleanText (c.doc.text (c.doc.nodes[static_cast<size_t> (nn)].str), 96, true);
                    break;
                }
            }
            c.item (ItemKind::dropped, Reason::tuningUnsupported, "voice", "tuning", scale, "", "", "tuning_unsupported");
        }
    }

    // ---- fidelity -----------------------------------------------------------------------------------------------------------------------
    rep.fidelity = rep.numDropped > 0 ? Fidelity::degraded : (rep.numApproximated > 0 ? Fidelity::close : Fidelity::exact);
    c.progress (100, "Finishing");
    (void) modCount;
    return ImportStatus::ok;
}

ImportResult makeResult()
{
    ImportResult r;
    r.status = ImportStatus::malformed;
    return r;
}
} // namespace

ImportResult importVitalPreset (const void* data, size_t size, const ImportOptions& options) noexcept
{
    ImportResult res = makeResult();
    try
    {
        if (data == nullptr || size == 0)
        {
            res.status = ImportStatus::malformed;
            res.error = "empty input";
            return res;
        }
        if (size > options.limits.maxFileBytes)
        {
            res.status = ImportStatus::tooLarge;
            res.error = "file larger than " + std::to_string (options.limits.maxFileBytes) + " bytes";
            return res;
        }
        Ctx c (options, res);
        res.status = importPresetImpl (c, data, size, options.fallbackName);
        if (res.status != ImportStatus::ok)
        {
            // partial results are never returned
            const std::string err = res.error;
            const ImportStatus st = res.status;
            res = makeResult();
            res.status = st;
            res.error = err;
            return res;
        }
    }
    catch (const std::bad_alloc&)
    {
        res = makeResult();
        res.status = ImportStatus::budgetExceeded;
        res.error = "out of memory";
    }
    catch (...)
    {
        res = makeResult();
        res.status = ImportStatus::malformed;
        res.error = "internal error";
    }
    return res;
}

namespace
{
ImportResult standalone (const void* data, size_t size, const ImportOptions& options, bool wavetable) noexcept
{
    ImportResult res = makeResult();
    try
    {
        if (data == nullptr || size == 0)
        {
            res.error = "empty input";
            return res;
        }
        if (size > options.limits.maxFileBytes)
        {
            res.status = ImportStatus::tooLarge;
            return res;
        }
        Ctx c (options, res);
        const json::Status js = json::parse (static_cast<const char*> (data), size, options.limits.maxDepth, options.limits.maxWorkingBytes, c.doc);
        if (js != json::Status::ok)
        {
            res.status = js == json::Status::budget ? ImportStatus::budgetExceeded : ImportStatus::malformed;
            res.error = "malformed JSON";
            return res;
        }
        ContentNotes notes;
        if (wavetable)
        {
            ParsedWavetable pw;
            if (! parseWavetable (c.doc, 0, pw, notes))
            {
                res.status = ImportStatus::notVital;
                res.error = "not a wavetable object";
                return res;
            }
            addContentNotes (c, notes, "wavetable");
            wtrender::Result r;
            const wtrender::Status st = renderParsed (c, pw, r);
            if (st != wtrender::Status::ok)
            {
                res.status = st == wtrender::Status::cancelled && ! c.deadlineHit ? ImportStatus::cancelled : ImportStatus::budgetExceeded;
                return res;
            }
            ImportedWavetable iw;
            iw.name = pw.name;
            iw.author = pw.author;
            iw.numFrames = r.numFrames;
            iw.frames = toInt16 (r.frames);
            iw.sha256 = Sha256::hashBufferHex (iw.frames.data(), iw.frames.size() * sizeof (int16_t));
            res.wavetables.push_back (std::move (iw));
        }
        else
        {
            ParsedLfo pl;
            if (! parseLfoLine (c.doc, 0, pl, notes))
            {
                res.status = ImportStatus::notVital;
                res.error = "not an LFO line object";
                return res;
            }
            addContentNotes (c, notes, "lfo");
            ImportedLfoShape s;
            s.name = pl.name;
            s.smooth = pl.line.smooth;
            for (size_t i = 0; i < pl.line.x.size(); ++i)
                s.points.push_back ({ pl.line.x[i], pl.line.y[i], pl.line.power[i] });
            res.lfoShapes.push_back (std::move (s));
        }
        res.sourceSha256 = Sha256::hashBufferHex (data, size);
        res.report.fidelity = res.report.numDropped > 0 ? Fidelity::degraded : (res.report.numApproximated > 0 ? Fidelity::close : Fidelity::exact);
        res.status = ImportStatus::ok;
    }
    catch (const std::bad_alloc&)
    {
        res = makeResult();
        res.status = ImportStatus::budgetExceeded;
    }
    catch (...)
    {
        res = makeResult();
    }
    return res;
}
} // namespace

ImportResult importVitalWavetableFile (const void* data, size_t size, const ImportOptions& options) noexcept
{
    return standalone (data, size, options, true);
}

ImportResult importVitalLfoFile (const void* data, size_t size, const ImportOptions& options) noexcept
{
    return standalone (data, size, options, false);
}

std::string serializeImportedPreset (const ImportResult& result, const PresetMeta& finalMeta, const ContentRefs& contentRefs)
{
    if (result.status != ImportStatus::ok)
        return {};

    StateContent content;
    for (const auto& iw : result.wavetables)
    {
        ContentWavetable cw;
        cw.osc = iw.osc;
        cw.ref.sha256 = iw.sha256;
        cw.ref.name = iw.name;
        cw.author = iw.author;
        cw.frames = iw.numFrames;
        if (const auto* ref = contentRefs.findWavetable (iw.osc))
        {
            cw.ref.pack = ref->pack;
            cw.ref.file = ref->file;
        }
        content.wavetables.push_back (std::move (cw));
    }

    for (const auto& is : result.lfoShapes)
    {
        ContentLfoShape cls;
        cls.lfo = is.lfo;
        cls.name = is.name;
        cls.smooth = is.smooth;
        cls.loopPoint = -1;
        for (const auto& pt : is.points)
        {
            ContentPoint cp;
            cp.x = pt[0];
            cp.y = pt[1];
            cp.power = std::max (-20.0f, std::min (20.0f, pt[2]));
            cls.points.push_back (cp);
        }
        content.lfoShapes.push_back (std::move (cls));
    }

    std::string s = savePreset (finalMeta, result.values, content, true);
    if (s.empty() || s.back() != '}')
        return {};

    s.pop_back();

    s += ",\"import\":{\"source\":{";
    s += "\"format\":\"vital\"";
    s += ",\"version\":";
    jsonEscape (result.report.vitalVersion, s);
    s += ",\"sha256\":";
    jsonEscape (result.sourceSha256, s);
    s += ",\"preset_name\":";
    jsonEscape (finalMeta.name.empty() ? result.meta.name : finalMeta.name, s);
    s += ",\"comments\":";
    jsonEscape (finalMeta.description.empty() ? result.meta.description : finalMeta.description, s);
    s += ",\"macro_names\":[";
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            s += ',';
        jsonEscape (result.macroNames[i], s);
    }
    s += "]},\"approximated\":[";

    std::vector<std::string> approxKeys;
    for (const auto& it : result.report.items)
    {
        if (it.kind == ItemKind::approximated && ! it.ourKey.empty())
        {
            if (std::find (approxKeys.begin(), approxKeys.end(), it.ourKey) == approxKeys.end())
            {
                if (approxKeys.size() < 64)
                    approxKeys.push_back (it.ourKey);
            }
        }
    }
    for (size_t i = 0; i < approxKeys.size(); ++i)
    {
        if (i > 0)
            s += ',';
        jsonEscape (approxKeys[i], s);
    }
    s += "]}}";

    if (s.size() > (1u << 20))
        return {};

    return s;
}
} // namespace synthyboy
