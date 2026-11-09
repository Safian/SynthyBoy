// SynthyBoy - core/serum-import: effect racks, modulation matrix, curves and the final sweep of the Serum 2 importer (13-new-keys.md 4.26).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "serum_importer.h"
#include "sha256.h"
#include "synthyboy/filters/filter_types.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"
#include "synthyboy/user_content.h"
#include "vital_import_internal.h"

namespace synthyboy
{
namespace serumimport
{
namespace
{
double clampd (double v, double lo, double hi) { return std::max (lo, std::min (hi, v)); }
constexpr double kSerumLimitRatio = 1.0e5; // a Serum compressor RATIO from here on is the LIMIT detent (the file stores 1,000,000)

std::string modPrefix (int slot0)
{
    char b[16];
    std::snprintf (b, sizeof b, "mod%02d", slot0 + 1);
    return b;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------
// effects
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::fxRack (int rack, bool fileIsRack)
{
    const std::string rname = fileIsRack ? "FXRack" : "FXRack" + std::to_string (rack);
    const int RK = get (0, rname.c_str());
    if (RK < 0) return;
    const int list = get (RK, "FX");
    const int target = fileIsRack ? std::max (0, std::min (2, opt.fileRack)) : rack;
    if (list >= 0 && N (list).type == cbor::Type::array)
        for (uint32_t k = 0; k < N (list).count && ! cancelled(); ++k)
            fxEntry (target, static_cast<int> (k), static_cast<int> (d.kids[N (list).first + k]), rname + "/FX/" + std::to_string (k));
    if (list >= 0) consume (list);
    const int dn = get (RK, "displayName");
    if (dn >= 0) { entry (ItemKind::ignored, Reason::exact, "fx", rname + "/displayName", valueText (dn), "", "", "rack_display_name"); consume (dn); }
    const int pp = get (RK, "plainParams");
    if (pp >= 0) noteDefault (pp);
    const int ex = get (RK, "kUIParamSelectedBus");
    (void) ex;
}

void Importer::convolveImpulse (int fx, const std::string& path)
{
    // the IR of the first convolution reverb: stored in the library, referenced by content.impulses[0] (kMaxContentImpulses = 1)
    if (! R.impulses.empty())
    {
        entry (ItemKind::dropped, Reason::limitExceeded, "fx", path, "", "convolve.ir", "", "only_one_impulse_response_per_patch", 0);
        return;
    }
    const int emb = get (fx, "embeddedIR"), rel = get (fx, "relativePathToIR");
    const int nch = get (fx, "numChannels"), sr = get (fx, "sampleRate");
    SerumImportedImpulse ir;
    bool ok = false;
    if (emb >= 0 && N (emb).type == cbor::Type::array && N (emb).count > 0)
    {
        const int ch = nch >= 0 ? std::max (1, std::min (2, static_cast<int> (num (nch)))) : 1;
        const size_t total = N (emb).count, frames = total / static_cast<size_t> (ch);
        std::vector<float> l (frames), r (ch == 2 ? frames : 0);
        for (size_t i = 0; i < frames; ++i)
        {
            l[i] = static_cast<float> (d.arrayNumber (static_cast<uint32_t> (emb), static_cast<uint32_t> (i * static_cast<size_t> (ch))));
            if (ch == 2) r[i] = static_cast<float> (d.arrayNumber (static_cast<uint32_t> (emb), static_cast<uint32_t> (i * 2 + 1)));
        }
        const int rate = sr >= 0 ? static_cast<int> (num (sr)) : 44100;
        ir.wavBytes = encodeWav (l.data(), ch == 2 ? r.data() : nullptr, frames, rate > 0 ? rate : 44100, 24);
        ir.name = "Serum embedded IR";
        ir.sampleRate = rate > 0 ? rate : 44100;
        ir.channels = ch;
        ir.numFrames = static_cast<int> (frames);
        ok = ! ir.wavBytes.empty();
        if (ok) acctRest (emb, ItemKind::imported, Reason::exact, "fx", path + "/embeddedIR", "convolve.ir", "Preset IR", "embedded_impulse_response_stored_in_the_library");
    }
    else if (isText (rel))
    {
        std::vector<uint8_t> bytes;
        int rate = 0, ch = 0;
        size_t frames = 0;
        if (readContent ("Impulses", str (rel), bytes))
        {
            ir.wavBytes = serumAudioToWav (bytes.data(), bytes.size(), 24, opt.limits.maxAudioFrames, &rate, &ch, &frames);
            ir.name = str (rel).substr (str (rel).find_last_of ("/\\") == std::string::npos ? 0 : str (rel).find_last_of ("/\\") + 1);
            const size_t dot = ir.name.find_last_of ('.');
            if (dot != std::string::npos) ir.name = ir.name.substr (0, dot);
            ir.sampleRate = rate;
            ir.channels = ch;
            ir.numFrames = static_cast<int> (frames);
            ok = ! ir.wavBytes.empty();
        }
        if (ok) entry (ItemKind::imported, Reason::exact, "fx", path + "/relativePathToIR", str (rel), "convolve.ir", "Preset IR", "impulse_file_stored_in_the_library");
        else
        {
            ++R.missingFiles;
            entry (ItemKind::dropped, Reason::renderFallback, "fx", path + "/relativePathToIR", str (rel), "convolve.ir", "None", "impulse_file_not_found_convolution_without_ir");
        }
        consume (rel);
    }
    if (ok)
    {
        ir.sha256 = Sha256::hashBufferHex (ir.wavBytes.data(), ir.wavBytes.size());
        R.impulses.push_back (std::move (ir));
        store ("convolve.ir", 1);
    }
}

void Importer::fxEntry (int rack, int index, int node, const std::string& path)
{
    // the module key: the member whose name starts with "FX" and whose value is a map
    std::string fxName;
    int fxMap = -1;
    forMap (node, [&] (const std::string& nm, int v, int) { if (nm.rfind ("FX", 0) == 0 && isMap (v)) { fxName = nm; fxMap = v; } });
    if (fxMap < 0)
    {
        acctRest (node, ItemKind::dropped, Reason::unsupportedComponent, "fx", path, "", "", "effect_module_without_a_name");
        placed[rack].push_back ({});
        return;
    }
    const std::string area = "fx.rack" + std::to_string (rack), mp = path + "/" + fxName;
    PlacedFx pf;
    pf.serumName = fxName;
    const int enable = get (get (fxMap, "plainParams"), "kParamEnable");
    const bool on = enable < 0 || num (enable) >= 0.5;

    // splitters: instance 0 modules, the bands are flattened into the rack (the pool cannot sit in a band)
    if (fxName == "FXSplit" || fxName == "FXSplit3" || fxName == "FXSplitMS")
    {
        const int module = fxName == "FXSplit" ? 13 : fxName == "FXSplit3" ? 14 : 15;
        const char* prefix = module == 13 ? "splitter_lh" : module == 14 ? "splitter_lmh" : "splitter_ms";
        store (std::string (prefix) + ".on", on ? 1 : 0);
        std::vector<FxWrite> w;
        fxMove (R.values.plain, { false, module }, rack, INT_MAX, w);
        fxApply (R.values.plain, w);
        for (const FxWrite& x : w) touched[static_cast<size_t> (x.param)] = 1;
        const int pp = get (fxMap, "plainParams");
        forMap (pp, [&] (const std::string& nm, int v, int)
        {
            const std::string p = mp + "/plainParams/" + nm;
            if (nm == "kParamEnable")
            {
                entry (ItemKind::imported, Reason::exact, area, p, valueText (v), std::string (prefix) + ".on", on ? "On" : "Off", "");
                consume (v);
            }
            else if (nm == "kParamFreq" || nm == "kParamFreq2")
            {
                const std::string key = module == 13 ? "splitter_lh.freq" : nm == "kParamFreq" ? "splitter_lmh.freq_low" : "splitter_lmh.freq_high";
                bool cl = false;
                const double st = store (key, num (v), &cl);
                entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, p, valueText (v), key, fmt (st), "");
                consume (v);
            }
            else if (nm.rfind ("kParamModuleCount", 0) == 0)
            {
                entry (ItemKind::approximated, Reason::tierNote, area, p, valueText (v), "", "", "splitter_bands_flattened_into_the_rack");
                consume (v);
            }
            else if (nm.find ("Bypass") != std::string::npos)
            {
                const std::string key = std::string (prefix) + (nm == "kParamBand1Bypass" ? ".low_bypass" : nm == "kParamBand2Bypass" ? (module == 13 ? ".high_bypass" : ".mid_bypass") : ".high_bypass");
                store (key, num (v) >= 0.5 ? 1 : 0);
                entry (ItemKind::imported, Reason::exact, area, p, valueText (v), key, num (v) >= 0.5 ? "On" : "Off", "");
                consume (v);
            }
        });
        placed[rack].push_back (pf);
        acctRest (node == 0 ? fxMap : node, ItemKind::dropped, Reason::noEngineEquivalent, area, path, "", "", "splitter_detail_not_modelled");
        return;
    }
    const int type = fxTypeOf (fxName);
    if (type == 0)
    {
        acctRest (node, ItemKind::dropped, Reason::unsupportedComponent, area, path, "", "", "unknown_effect_module");
        placed[rack].push_back (pf);
        return;
    }
    pf.type = type;
    const bool instanceZero = type == kFxTypeConvolve && R.impulses.empty() && (get (fxMap, "embeddedIR") >= 0 || get (fxMap, "relativePathToIR") >= 0);
    int slot = -1;
    const FxTypeDescriptor& desc = fxTypeDescriptor (type);
    if (instanceZero)
    {
        pf.prefix = "convolve";
        std::vector<FxWrite> w;
        store ("convolve.on", on ? 1 : 0);
        fxMove (R.values.plain, { false, 12 }, rack, INT_MAX, w);
        fxApply (R.values.plain, w);
        for (const FxWrite& x : w) touched[static_cast<size_t> (x.param)] = 1;
        convolveImpulse (fxMap, mp);
    }
    else
    {
        std::vector<FxWrite> w;
        slot = fxAddModule (R.values.plain, type, rack, w);
        if (slot < 0)
        {
            acctRest (node, ItemKind::dropped, Reason::limitExceeded, area, path, "", "", "effect_pool_is_full");
            placed[rack].push_back (pf);
            return;
        }
        fxApply (R.values.plain, w);
        for (const FxWrite& x : w) touched[static_cast<size_t> (x.param)] = 1;
        pf.slot = slot;
        const FxInstanceKeys& k = fxInstanceKeys (slot);
        store (paramTable()[k.on].key, on ? 1 : 0);
    }
    if (enable >= 0)
    {
        entry (ItemKind::imported, Reason::exact, area, mp + "/plainParams/kParamEnable", valueText (enable), instanceZero ? "convolve.on" : paramTable()[fxInstanceKeys (slot).on].key, on ? "On" : "Off", "");
        consume (enable);
    }
    else ++R.report.numDefaulted;
    placed[rack].push_back (pf);

    // parameters
    size_t count = 0;
    const Rule* rows = fxRulesOf (type, count);
    // writes a value of OUR key into the module: directly (instance 0) or through the slot's normalised parameter
    const auto put = [&] (const std::string& ourKey, double v, bool* clamped) -> double
    {
        if (instanceZero) return store (ourKey, v, clamped);
        const int src = indexOf (ourKey);
        int number = -1;
        for (int q = 0; q < desc.count; ++q)
            if (desc.source[q] == src) number = q;
        if (src < 0 || number < 0) return v;
        const ParamInfo& info = paramTable()[src];
        const float plain = constrainValue (info, static_cast<float> (v));
        if (clamped) { const double tol = info.interval > 0 ? 0.5 * info.interval + 1e-6 : 1e-4 * std::max (1.0, std::fabs (v)); *clamped = std::fabs (static_cast<double> (plain) - v) > tol; }
        const int target = fxInstanceKeys (slot).p[number];
        R.values.plain[static_cast<size_t> (target)] = fxInstanceNormalise (type, number, plain);
        touched[static_cast<size_t> (target)] = 1;
        return plain;
    };
    const auto choiceName = [&] (const std::string& key, double v)
    {
        char text[64];
        const int idx = indexOf (key);
        if (idx >= 0) valueToText (paramTable()[idx], static_cast<float> (v), text, sizeof text); else text[0] = 0;
        return std::string (text);
    };
    const int pp = get (fxMap, "plainParams");
    std::map<std::string, int> seen;
    const auto find = [&] (const char* name) { return get (pp, name); };
    // the bands of the Serum EQ: the left band is our low band, the right band our high band; a band set to Peak uses the mid band (the first peak), a second
    // peak falls back to the shelf of its side
    std::string eqGroup[2];
    bool eqShelfForPeak[2] = { false, false };
    if (type == kFxTypeEq)
    {
        const int t1 = find ("kParamType1") >= 0 ? static_cast<int> (num (find ("kParamType1"))) : 0, t2 = find ("kParamType2") >= 0 ? static_cast<int> (num (find ("kParamType2"))) : 0;
        bool midTaken = false;
        const int types[2] = { t1, t2 };
        for (int b = 0; b < 2; ++b)
        {
            const bool peak = types[b] == 1;
            if (peak && ! midTaken) { eqGroup[b] = "mid"; midTaken = true; }
            else { eqGroup[b] = b == 0 ? "low" : "high"; eqShelfForPeak[b] = peak; }
        }
        placed[rack].back().eq[0] = eqGroup[0];
        placed[rack].back().eq[1] = eqGroup[1];
    }
    if (isMap (pp))
        forMap (pp, [&] (const std::string& nm, int v, int)
        {
            if (used[static_cast<size_t> (v)]) return;
            const std::string p = mp + "/plainParams/" + nm;
            bool handled = false;
            for (size_t r = 0; r < count; ++r)
            {
                const Rule& rule = rows[r];
                if (nm != rule.param) continue;
                if (handled) { if (rule.conv != Conv::enumName && isNumber (v)) put (rule.key, convert (rule, num (v)), nullptr); continue; }
                // A347: a Serum compressor RATIO at its maximum (1,000,000) is the LIMIT detent, a true peak limiter (manual p.163): limit mode, the
                // makeup gain drives into the limiter
                const bool limitDetent = type == kFxTypeCompressor && nm == "kParamRatio" && isNumber (v) && num (v) >= kSerumLimitRatio;
                if (rule.conv == Conv::enumName)
                {
                    if (! isText (v)) continue;
                    const std::string en = str (v);
                    const EnumEntry* hit = nullptr;
                    for (int q = 0; q < rule.en->count; ++q) if (en == rule.en->entries[q].name) hit = &rule.en->entries[q];
                    if (! hit) { entry (ItemKind::dropped, Reason::enumRemapped, area, p, en, rule.key, "", "enum_value_unknown"); consume (v); handled = true; continue; }
                    put (rule.key, hit->value, nullptr);
                    entry (hit->fid == Fid::approx ? ItemKind::approximated : ItemKind::imported, hit->fid == Fid::approx ? Reason::enumRemapped : Reason::exact, area, p, en, rule.key,
                           choiceName (rule.key, hit->value), hit->fid == Fid::approx ? std::string ("nearest_") + rule.en->area : "");
                    consume (v);
                    handled = true;
                    continue;
                }
                if (! isNumber (v)) { entry (ItemKind::dropped, Reason::typeMismatch, area, p, valueText (v), rule.key, "", "number_expected"); consume (v); handled = true; continue; }
                bool cl = false;
                // GROUPA1 (WP-10, A352): the FEEDBACK of the two reverbs that have one (Space = Nitrous, Abyss = Basin) is reverb.feedback (a percent); the other
                // types carry their decay in the same parameter (reverb.decay, a log scale)
                const int reverbTypeNode = find ("kParamType");
                const bool networkFeedback = type == kFxTypeReverb && rule.conv == Conv::logPercent && std::strcmp (rule.key, "reverb.decay") == 0 && isText (reverbTypeNode)
                                             && (str (reverbTypeNode) == "kSpace" || str (reverbTypeNode) == "kAbyss");
                const char* targetKey = networkFeedback ? "reverb.feedback" : rule.key;
                const double ours = put (targetKey, networkFeedback ? num (v) : convert (rule, num (v)), &cl);
                ItemKind kind = rule.fid == Fid::approx || networkFeedback ? ItemKind::approximated : ItemKind::imported;
                Reason reason = Reason::exact;
                std::string detail = networkFeedback ? std::string ("reverb_feedback_of_nitrous_and_basin") : rule.note;
                if (cl) { kind = ItemKind::approximated; reason = Reason::clamped; if (detail.empty()) detail = "clamped_to_range"; }
                if (limitDetent) { put ("compressor.limit", 1, nullptr); kind = ItemKind::approximated; detail = "compressor_ratio_max_is_limit_mode"; }
                entry (kind, reason, area, p, valueText (v), targetKey, fmt (ours), detail);
                consume (v);
                handled = true;
            }
            if (handled) return;
            // the specials
            if (nm == "kParamBeatSync" || nm == "kParamSwapAB" || nm == "kParamLink" || nm == "kParamFiltMode" || nm == "kParamCompensatedWetDry")
            {
                entry (ItemKind::approximated, Reason::tierNote, area, p, valueText (v), "", "", nm == "kParamBeatSync" ? "tempo_sync_flattened_to_time" : "effect_option_not_modelled");
                consume (v);
            }
            else if (type == kFxTypeFilter && nm == "kParamType" && isText (v))
            {
                int m = 0, s2 = 0;
                Fid fid = Fid::exact;
                if (filterTypeFromName (str (v), m, s2, fid))
                {
                    put ("fxfilter.model", m, nullptr);
                    put ("fxfilter.style", s2, nullptr);
                    entry (fid == Fid::exact ? ItemKind::imported : ItemKind::approximated, fid == Fid::exact ? Reason::exact : Reason::enumRemapped, area, p, str (v), "fxfilter.model",
                           choiceName ("fxfilter.model", m), fid == Fid::exact ? "" : "filter_type_nearest");
                }
                else entry (ItemKind::dropped, Reason::enumRemapped, area, p, str (v), "fxfilter.model", "", "filter_type_unknown");
                consume (v);
            }
            else if (type == kFxTypeBode && nm == "kParamShift" && isNumber (v))
            {
                bool cl = false;
                put ("bode.shift", std::fabs (num (v)), &cl);
                put ("bode.dir", num (v) < 0 ? -100.0 : 100.0, nullptr);
                entry (ItemKind::approximated, Reason::tierNote, area, p, valueText (v), "bode.shift", fmt (std::fabs (num (v))), "shift_sign_to_direction");
                consume (v);
            }
            else if (type == kFxTypeEq && isNumber (v) && (nm.rfind ("kParamFreq", 0) == 0 || nm.rfind ("kParamGain", 0) == 0 || nm.rfind ("kParamReso", 0) == 0 || nm.rfind ("kParamType", 0) == 0))
            {
                const int band = nm.back() == '2' ? 2 : 1;
                const int t = band == 1 ? (find ("kParamType1") >= 0 ? static_cast<int> (num (find ("kParamType1"))) : 0) : (find ("kParamType2") >= 0 ? static_cast<int> (num (find ("kParamType2"))) : 0);
                const std::string& group = eqGroup[band - 1];
                const bool asMid = group == "mid", shelfForPeak = eqShelfForPeak[band - 1];
                std::string key;
                double value = num (v);
                if (nm.rfind ("kParamFreq", 0) == 0) key = "eq." + group + ".cutoff";
                else if (nm.rfind ("kParamGain", 0) == 0) key = "eq." + group + ".gain";
                else if (nm.rfind ("kParamReso", 0) == 0) key = "eq." + group + ".resonance";
                else
                {
                    key = "eq." + group + ".mode";
                    value = asMid ? 0.0 : (t == 2 ? 1.0 : 0.0); // low: Shelf | High pass, high: Shelf | Low pass, mid: Peak | Notch
                }
                bool cl = false;
                const double st = put (key, value, &cl);
                entry (asMid || shelfForPeak || cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, p, valueText (v), key, fmt (st),
                       asMid ? "eq_peak_band_on_the_mid_band" : shelfForPeak ? "eq_second_peak_as_shelf" : (cl ? "clamped_to_range" : ""));
                consume (v);
            }
        });
    // Serum defaults of enumerations the file leaves out
    if (type == kFxTypeDistortion && find ("kParamMode") < 0) put ("distortion.type", 6, nullptr);
    if (type == kFxTypeReverb && find ("kParamType") < 0) put ("reverb.type", 1, nullptr);
    if (type == kFxTypeEq)
    {
        // an EQ band at Shelf with no gain is neutral; the Serum EQ default has no gain either
    }
    // everything else of the module
    forMap (fxMap, [&] (const std::string& nm, int v, int)
    {
        if (used[static_cast<size_t> (v)]) return;
        const std::string p = mp + "/" + nm;
        if (nm == "plainParams")
        {
            if (isDefaultBlock (v)) { noteDefault (v); return; }
            forMap (v, [&] (const std::string& q, int w, int)
            {
                if (used[static_cast<size_t> (w)]) return;
                const bool convParam = type == kFxTypeConvolve && (q == "kParamLevelOut");
                (void) convParam;
                entry (ItemKind::dropped, Reason::noEngineEquivalent, area, p + "/" + q, valueText (w), "", "", "effect_parameter_not_modelled");
                consume (w);
            });
            consume (v);
        }
        else if (nm == "lfophasor" || nm == "lfo") acctRest (v, ItemKind::ignored, Reason::exact, area, p, "", "", "effect_lfo_runtime_state");
        else if (nm == "PZs") acctRest (v, ItemKind::dropped, Reason::noEngineEquivalent, area, p, "", "", "pz_svf_poles_and_zeros_not_imported");
        else if (nm == "numChannels" || nm == "numFrames" || nm == "sampleRate" || nm == "sourceRate") acctRest (v, ItemKind::ignored, Reason::exact, area, p, "", "", "derived_from_the_impulse_data");
        else if (nm == "embeddedIR" || nm == "relativePathToIR") acctRest (v, ItemKind::dropped, Reason::limitExceeded, area, p, "", "", "only_one_impulse_response_per_patch");
    });
    // the drawn X-Shaper curves of the distortion
    const int flex = get (node, "flex");
    if (flex >= 0 && N (flex).type == cbor::Type::array)
    {
        const bool shaper = type == kFxTypeDistortion;
        for (uint32_t q = 0; q < N (flex).count && q < 2; ++q)
        {
            const int c = static_cast<int> (d.kids[N (flex).first + q]);
            if (! isMap (c) || N (c).count == 0) continue;
            std::vector<ContentPoint> pts;
            if (shaper && pointsFromCurve (c, pts, true))
            {
                (q == 0 ? R.shaperA : R.shaperB) = reduce (pts, kMaxCurvePoints);
                acctRest (c, ItemKind::approximated, Reason::tierNote, area, path + "/flex/" + std::to_string (q), "", "", "x_shaper_curve_as_shaper_curve");
            }
        }
        acctRest (flex, ItemKind::ignored, Reason::exact, area, path + "/flex", "", "", "effect_curve_slots_empty");
    }
    const int ty = get (node, "type");
    if (ty >= 0) { entry (ItemKind::ignored, Reason::exact, area, path + "/type", valueText (ty), "", "", "effect_type_code"); consume (ty); }
    const int ui = get (node, "kUIParamMixOrGain");
    if (ui >= 0) { entry (ItemKind::ignored, Reason::exact, area, path + "/kUIParamMixOrGain", valueText (ui), "", "", "interface_state"); consume (ui); }
    acctRest (node == 0 ? fxMap : node, ItemKind::dropped, Reason::noEngineEquivalent, area, path, "", "", "effect_detail_not_modelled");
    (void) index;
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// modulation destinations
// ---------------------------------------------------------------------------------------------------------------------------------------
Importer::Dest Importer::destOf (const std::string& type, int id, const std::string& param)
{
    Dest out;
    const auto fromRules = [&] (std::initializer_list<const char*> scopes, int n) -> bool
    {
        for (const char* scope : scopes)
        {
            size_t count = 0;
            const Rule* rows = rulesOf (scope, count);
            for (size_t i = 0; i < count; ++i)
                if (std::strcmp (rows[i].scope, scope) == 0 && param == rows[i].param && rows[i].key[0])
                {
                    out.key = keyOf (rows[i].key, n);
                    out.fid = rows[i].fid;
                    out.note = rows[i].note;
                    out.ok = true;
                    out.dbLaw = rows[i].conv == Conv::dbFromLin;
                    out.srcKey = out.key;
                    return true;
                }
        }
        return false;
    };
    const int n = id + 1;
    if (type == "Oscillator")
    {
        if (id >= 0 && id <= 2)
        {
            if (param == "kParamOctave" || param == "kParamPitch") { out.key = keyOf ("osc#.transpose", n); out.ok = true; out.fid = Fid::close; out.note = "octave_and_semitones_share_the_transpose_range"; return out; }
            if (param == "kParamScanRate") { out.key = keyOf ("osc#.sample.scan", n); out.ok = true; return out; }
            if (param == "kParamPitchRatio") { out.key = keyOf ("osc#.pitch_coarse", n); out.ok = true; out.fid = Fid::approx; return out; }
            if (fromRules ({ "Osc" }, n)) return out;
        }
        else if (id == 3) { if (param == "kParamPitch") { out.key = "noise.pitch"; out.ok = true; return out; } if (fromRules ({ "Noise" }, 1)) return out; }
        else if (id == 4) { if (param == "kParamFine") { out.key = "sub.tune"; out.ok = true; out.fid = Fid::approx; return out; } if (fromRules ({ "Sub" }, 1)) return out; }
    }
    else if (type == "WTOsc" || type == "SampleOsc" || type == "MultiSampleOsc" || type == "GranularOsc" || type == "SpectralOsc")
    {
        if (param == "kParamTablePos" && type == "WTOsc") { out.key = keyOf ("osc#.frame", n); out.ok = true; return out; }
        if (type == "WTOsc" ? fromRules ({ "WT", "Warp" }, n) : type == "MultiSampleOsc" ? fromRules ({ "Multi", "Warp" }, n) : fromRules ({ "Warp" }, n)) return out;
    }
    else if (type == "NoiseOsc") { if (fromRules ({ "NoiseOsc" }, 1)) return out; }
    else if (type == "SubOsc") { if (fromRules ({ "SubOsc" }, 1)) return out; }
    else if (type == "VoiceFilter") { if (fromRules ({ "Filter" }, n)) return out; }
    else if (type == "Env") { if (fromRules ({ "Env" }, n)) return out; }
    else if (type == "Macro") { if (fromRules ({ "Macro" }, n)) return out; }
    else if (type == "Global")
    {
        if (param == "kParamVoiceAmp") { out.key = "master.volume"; out.ok = true; out.fid = Fid::approx; out.note = "voice_amp_as_master_volume"; out.dbLaw = true; out.srcKey = "master.volume"; return out; }
        if (fromRules ({ "Global" }, 1)) return out;
    }
    else if (type == "LFO")
    {
        if (id >= 0 && id < 10)
        {
            const int rj = randomLfoOf[id];
            if (rj < 0) { out.whyText = "path_or_surplus_random_lfo"; return out; }
            const std::string pfx = rj > 0 ? "rlfo" + std::to_string (rj) : "lfo" + std::to_string (n);
            if (param == "kParamRate") { out.key = pfx + ".rate"; out.ok = true; out.fid = Fid::close; out.note = "tempo_rate_modulated_as_hz"; return out; }
            if (rj == 0 && param == "kParamPhase") { out.key = pfx + ".phase"; out.ok = true; return out; }
            if (rj == 0 && param == "kParamRise") { out.key = pfx + ".fade_in"; out.ok = true; return out; }
            if (rj == 0 && param == "kParamDelay") { out.key = pfx + ".delay"; out.ok = true; return out; }
            if (rj == 0 && param == "kParamSmooth") { out.key = pfx + ".smooth_time"; out.ok = true; out.fid = Fid::approx; return out; }
        }
    }
    else if (type == "RoutingSlot")
    {
        static const char* const prefix[5] = { "osc1", "osc2", "osc3", "noise", "sub" };
        if (id >= 0 && id <= 4)
        {
            if (param == "kParamFXBus1Level") { out.key = std::string (prefix[id]) + ".bus1_send"; out.ok = true; return out; }
            if (param == "kParamFXBus2Level") { out.key = std::string (prefix[id]) + ".bus2_send"; out.ok = true; return out; }
            if (param == "kParamFilterBalance") { out.key = std::string (prefix[id]) + ".filter_blend"; out.ok = true; return out; }
        }
        else if (id == 5 || id == 6)
        {
            if (param == "kParamFXBus1Level") { out.key = "filter" + std::to_string (id - 4) + ".bus1_send"; out.ok = true; return out; }
            if (param == "kParamFXBus2Level") { out.key = "filter" + std::to_string (id - 4) + ".bus2_send"; out.ok = true; return out; }
        }
    }
    else if (type.rfind ("FX", 0) == 0 && type != "FXRack")
    {
        const int rack = id / 100, pos = id % 100;
        if (rack >= 0 && rack < 3 && pos >= 0 && pos < static_cast<int> (placed[rack].size()))
        {
            const PlacedFx& pf = placed[rack][static_cast<size_t> (pos)];
            if (pf.type != 0 && pf.serumName == type)
            {
                size_t count = 0;
                const Rule* rows = fxRulesOf (pf.type, count);
                std::string eqKey;
                if (pf.type == kFxTypeEq && param.size() > 6 && param.rfind ("kParam", 0) == 0 && (param.back() == '1' || param.back() == '2'))
                {
                    const std::string& group = pf.eq[param.back() - '1'];
                    const std::string base = param.substr (6, param.size() - 7);
                    if (! group.empty() && (base == "Freq" || base == "Gain" || base == "Reso"))
                        eqKey = "eq." + group + (base == "Freq" ? ".cutoff" : base == "Gain" ? ".gain" : ".resonance");
                }
                if (! eqKey.empty())
                {
                    const FxTypeDescriptor& desc = fxTypeDescriptor (pf.type);
                    const int src = indexOf (eqKey);
                    for (int q = 0; q < desc.count; ++q)
                        if (desc.source[q] == src)
                        {
                            out.key = pf.slot < 0 ? eqKey : paramTable()[fxInstanceKeys (pf.slot).p[q]].key;
                            out.ok = true;
                            out.fid = Fid::close;
                            out.note = "eq_band_modulated_on_its_group";
                            return out;
                        }
                }
                for (size_t i = 0; i < count; ++i)
                    if (param == rows[i].param)
                    {
                        out.dbLaw = rows[i].conv == Conv::dbFromLin;
                        out.srcKey = rows[i].key;
                        if (pf.slot < 0) { out.key = rows[i].key; out.ok = true; out.fid = rows[i].fid; return out; }
                        const FxTypeDescriptor& desc = fxTypeDescriptor (pf.type);
                        const int src = indexOf (rows[i].key);
                        for (int q = 0; q < desc.count; ++q)
                            if (desc.source[q] == src)
                            {
                                out.key = paramTable()[fxInstanceKeys (pf.slot).p[q]].key;
                                out.ok = true;
                                out.fid = rows[i].fid;
                                out.note = rows[i].note;
                                return out;
                            }
                    }
                out.whyText = "effect_parameter_not_modelled";
                return out;
            }
            out.whyText = "effect_not_imported";
            return out;
        }
        out.whyText = "effect_not_found";
        return out;
    }
    if (out.whyText.empty()) out.whyText = "destination_not_modelled";
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// the matrix
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::modSlots()
{
    struct Bus { bool used = false; std::string source; double amount = 0.0; };
    Bus buses[16];
    int slotsUsed = 0;
    for (int k = 0; k < 64 && ! cancelled(); ++k)
    {
        const std::string name = "ModSlot" + std::to_string (k);
        const int S = get (0, name.c_str());
        if (S < 0) continue;
        const std::string pfx = modPrefix (k), area = "mod" + std::to_string (k + 1);
        const int src = get (S, "source");
        const int pp = get (S, "plainParams");
        if (src < 0 || N (src).type != cbor::Type::array || N (src).count < 1 || d.arrayNumber (static_cast<uint32_t> (src), 0) == 0.0)
        {
            acctRest (S, ItemKind::ignored, Reason::defaulted, area, name, "", "", "empty_modulation_slot");
            continue;
        }
        const int a = static_cast<int> (d.arrayNumber (static_cast<uint32_t> (src), 0));
        const int b = N (src).count > 1 ? static_cast<int> (d.arrayNumber (static_cast<uint32_t> (src), 1)) : 0;
        const int typeNode = get (S, "destModuleTypeString"), idNode = get (S, "destModuleID"), paramNode = get (S, "destModuleParamName");
        const std::string destType = isText (typeNode) ? str (typeNode) : "", destParam = isText (paramNode) ? str (paramNode) : "";
        const int destId = idNode >= 0 ? static_cast<int> (num (idNode)) : 0;
        // the main source
        const char* sname = "";
        Fid sfid = Fid::exact;
        int mainChoice = sourceChoiceOf (a, sname, sfid);
        if (a >= 6 && a <= 15)
        {
            const int rj = randomLfoOf[a - 6];
            if (rj < 0) mainChoice = -1;
            else if (rj > 0) { sname = ""; mainChoice = -1; const ModSourceInfo* t = modSourceTable(); for (size_t q = 0; q < modSourceCount(); ++q) if (std::string (t[q].key) == "rlfo" + std::to_string (rj)) mainChoice = static_cast<int> (q) + 1; }
        }
        // a modulation of the point of an LFO shape: kept as the source of the point
        if (destType == "LFOPointModBus")
        {
            if (destId >= 0 && destId < 16 && mainChoice > 0)
            {
                buses[destId].used = true;
                buses[destId].source = modSourceTable()[mainChoice - 1].key;
                buses[destId].amount = clampd (num (get (pp, "kParamAmount"), 0.0) / 100.0, -kMaxPointModAmount, kMaxPointModAmount);
                acctRest (S, ItemKind::approximated, Reason::tierNote, area, name, "", "", "point_modulation_bus_kept_as_point_source");
            }
            else acctRest (S, ItemKind::dropped, Reason::modSourceUnsupported, area, name, "", "", "point_modulation_source_unsupported");
            continue;
        }
        if (mainChoice < 0)
        {
            acctRest (S, ItemKind::dropped, Reason::modSourceUnsupported, area, name, "", "", "modulation_source_not_modelled");
            if (! R.report.entries.empty()) R.report.entries.back().serumValue = destType + "#" + std::to_string (destId) + " " + destParam + " src=" + std::to_string (a) + "," + std::to_string (b);
            continue;
        }
        const Dest dest = destOf (destType, destId, destParam);
        const std::string desc = destType + "#" + std::to_string (destId) + " " + destParam + " src=" + std::to_string (a) + "," + std::to_string (b);
        int choice = 0;
        if (dest.ok)
        {
            const int idx = indexOf (dest.key);
            choice = idx >= 0 ? static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (idx))) : 0;
        }
        if (choice <= 0)
        {
            acctRest (S, ItemKind::dropped, Reason::modDestinationUnsupported, area, name, "", "", dest.ok ? "destination_is_not_modulatable" : dest.whyText);
            if (! R.report.entries.empty()) R.report.entries.back().serumValue = desc;
            continue;
        }
        ++slotsUsed;
        store (pfx + ".source", mainChoice);
        store (pfx + ".destination", choice);
        entry (sfid == Fid::approx ? ItemKind::approximated : ItemKind::imported, sfid == Fid::approx ? Reason::enumRemapped : Reason::exact, area, name + "/source", std::to_string (a) + "," + std::to_string (b),
               pfx + ".source", sname, sfid == Fid::approx ? "source_nearest" : "serum_source_id", 1);
        // the aux source: the second element of the array
        int auxLeaves = 1;
        if (b != 0)
        {
            const char* an = "";
            Fid afid = Fid::exact;
            int auxChoice = sourceChoiceOf (b, an, afid);
            if (b >= 6 && b <= 15)
            {
                const int rj = randomLfoOf[b - 6];
                if (rj < 0) auxChoice = -1;
                else if (rj > 0) { auxChoice = -1; const ModSourceInfo* t = modSourceTable(); for (size_t q = 0; q < modSourceCount(); ++q) if (std::string (t[q].key) == "rlfo" + std::to_string (rj)) auxChoice = static_cast<int> (q) + 1; }
            }
            if (auxChoice > 0)
            {
                store (pfx + ".aux_source", auxChoice);
                entry (afid == Fid::approx ? ItemKind::approximated : ItemKind::imported, afid == Fid::approx ? Reason::enumRemapped : Reason::exact, area, name + "/source[1]", std::to_string (b), pfx + ".aux_source", an, "");
            }
            else entry (ItemKind::dropped, Reason::modSourceUnsupported, area, name + "/source[1]", std::to_string (b), "", "", "aux_source_not_modelled");
        }
        else entry (ItemKind::ignored, Reason::exact, area, name + "/source[1]", "0", "", "", "no_aux_source");
        (void) auxLeaves;
        consume (src);
        // destination fields
        {
            entry (dest.fid == Fid::approx ? ItemKind::approximated : ItemKind::imported, dest.fid == Fid::approx ? Reason::enumRemapped : Reason::exact, area, name + "/destModule*", destType + "#" + std::to_string (destId) + " " + destParam,
                   pfx + ".destination", dest.key, dest.note.empty() ? "modulation_destination" : dest.note, 4);
            for (const char* q : { "destModuleTypeString", "destModuleID", "destModuleParamName", "destModuleParamID" })
            {
                const int nd = get (S, q);
                if (nd >= 0) used[static_cast<size_t> (nd)] = 1;
            }
        }
        forMap (S, [&] (const std::string& q, int, int kn) { if (q.rfind ("destModule", 0) == 0) used[static_cast<size_t> (kn)] = 1; });
        // the parameters of the slot
        if (isMap (pp))
        {
            const auto number = [&] (const char* nm, const char* tail, double scale, double lo, double hi, const char* detail, ItemKind okKind = ItemKind::imported)
            {
                const int v = get (pp, nm);
                if (v < 0) return;
                bool cl = false;
                const std::string key = pfx + "." + tail;
                const double st = store (key, clampd (num (v) * scale, lo, hi), &cl);
                setByFile[static_cast<size_t> (indexOf (key))] = 1;
                entry (cl ? ItemKind::approximated : okKind, cl ? Reason::clamped : Reason::exact, area, name + "/plainParams/" + nm, valueText (v), key, fmt (st), detail);
                consume (v);
            };
            {
                // a destination whose Serum value is a linear gain (0 .. 1) is a dB value here: the same relative depth would move it over the whole dB range, so the
                // depth is limited to 24 dB down / 12 dB up (A346)
                double scale = 1.0;
                const int amountNode = get (pp, "kParamAmount");
                if (dest.dbLaw && amountNode >= 0)
                {
                    const ParamInfo& di = paramTable()[static_cast<size_t> (indexOf (dest.srcKey.empty() ? dest.key : dest.srcKey))];
                    const double range = static_cast<double> (di.max - di.min);
                    scale = (num (amountNode) < 0 ? 24.0 : 12.0) / range;
                    if (scale > 1.0) scale = 1.0;
                }
                if (scale < 1.0 && amountNode >= 0)
                {
                    bool cl = false;
                    const double st = store (pfx + ".amount", clampd (num (amountNode) * scale, -100, 100), &cl);
                    setByFile[static_cast<size_t> (indexOf (pfx + ".amount"))] = 1;
                    entry (ItemKind::approximated, Reason::tierNote, area, name + "/plainParams/kParamAmount", valueText (amountNode), pfx + ".amount", fmt (st), "db_destination_depth_limited");
                    consume (amountNode);
                }
                else
                    number ("kParamAmount", "amount", 1.0, -100, 100, "");
            }
            number ("kParamBipolar", "bipolar", 1.0, 0, 1, "");
            number ("kParamBypass", "bypass", 1.0, 0, 1, "");
            number ("kParamAuxInverted", "aux_inv", 1.0, 0, 1, "");
            number ("kParamCurveIn", "power", 0.1, -10, 10, "mod_curve_knob_scale_assumed", ItemKind::approximated);
            number ("kParamAuxCurve", "aux_power", 0.1, -10, 10, "mod_curve_knob_scale_assumed", ItemKind::approximated);
            number ("kParamOut", "aux_output", 1.0, 0, 200, "");
        }
        // drawn curves
        const int flex = get (S, "flex");
        const int mainFlag = get (pp, "kParamMainCurveData"), auxFlag = get (pp, "kParamAuxCurveData");
        if (flex >= 0 && N (flex).type == cbor::Type::array)
        {
            for (uint32_t q = 0; q < N (flex).count; ++q)
            {
                const int c = static_cast<int> (d.kids[N (flex).first + q]);
                if (! isMap (c) || N (c).count == 0) { consume (c); continue; }
                std::vector<ContentPoint> pts;
                if (q == 0 && pointsFromCurve (c, pts, true))
                {
                    ContentModCurve mc;
                    mc.slot = k + 1;
                    mc.points = reduce (pts, kMaxModCurvePoints);
                    for (auto& p : mc.points) { p.modX = {}; p.modY = {}; }
                    R.modCurves.push_back (std::move (mc));
                    acctRest (c, ItemKind::imported, Reason::exact, area, name + "/flex/0", pfx + ".source", "curve", "modulation_curve_stored");
                }
                else acctRest (c, ItemKind::dropped, Reason::noEngineEquivalent, area, name + "/flex/" + std::to_string (q), "", "", q == 1 ? "aux_curve_not_modelled" : "modulation_curve_unreadable");
            }
            consume (flex);
        }
        if (mainFlag >= 0) { entry (ItemKind::ignored, Reason::exact, area, name + "/plainParams/kParamMainCurveData", valueText (mainFlag), "", "", "curve_flag"); consume (mainFlag); }
        if (auxFlag >= 0) { entry (ItemKind::ignored, Reason::exact, area, name + "/plainParams/kParamAuxCurveData", valueText (auxFlag), "", "", "curve_flag"); consume (auxFlag); }
        // what is left of the slot
        std::vector<Unmapped> um;
        forMap (pp, [&] (const std::string& nm, int v, int) { if (! used[static_cast<size_t> (v)]) um.push_back ({ nm, v }); });
        for (const Unmapped& u : um)
        {
            const char* why = u.name == "kParamCurveOut" ? "output_curve_display_not_modelled" : u.name.rfind ("kParamSmooth", 0) == 0 ? "modulation_slew_not_modelled" : u.name.rfind ("kParamDelay", 0) == 0 ? "modulation_delay_not_modelled" : "serum_feature_not_modelled";
            entry (ItemKind::dropped, Reason::noEngineEquivalent, area, name + "/plainParams/" + u.name, valueText (u.node), "", "", why);
            consume (u.node);
        }
        acctRest (S, ItemKind::dropped, Reason::noEngineEquivalent, area, name, "", "", "modulation_slot_detail_not_modelled");
    }
    // the assignments of bus values to LFO points
    const int as = get (0, "lfoPointModAssignments");
    if (as >= 0 && N (as).type == cbor::Type::array)
    {
        for (uint32_t q = 0; q < N (as).count; ++q)
        {
            const int e = static_cast<int> (d.kids[N (as).first + q]);
            const int bus = static_cast<int> (num (get (e, "busID"), -1)), lfoId = static_cast<int> (num (get (e, "lfoID"), -1)), type = static_cast<int> (num (get (e, "lfoType"), -1));
            const int point = static_cast<int> (num (get (e, "pointID"), -1)), target = static_cast<int> (num (get (e, "target"), -1));
            bool done = false;
            if (bus >= 0 && bus < 16 && buses[bus].used && type == 0 && lfoId >= 0 && lfoId < 10 && target >= 0 && target <= 1)
                for (ContentLfoShape& shape : R.lfoShapes)
                    if (shape.lfo == lfoId + 1 && lfoOrigPoints[lfoId] > 0 && static_cast<int> (shape.points.size()) == lfoOrigPoints[lfoId] + lfoPointOffset[lfoId])
                    {
                        const int at = point + lfoPointOffset[lfoId];
                        if (at < 0 || at >= static_cast<int> (shape.points.size())) break;
                        ContentModRef& m = target == 0 ? shape.points[static_cast<size_t> (at)].modX : shape.points[static_cast<size_t> (at)].modY;
                        m.present = true;
                        m.source = buses[bus].source;
                        m.amount = static_cast<float> (buses[bus].amount);
                        done = true;
                    }
            if (done) acctRest (e, ItemKind::approximated, Reason::tierNote, "lfo" + std::to_string (lfoId + 1), "lfoPointModAssignments/" + std::to_string (q), "", "", target == 0 ? "point_x_modulation" : "point_y_modulation");
            else acctRest (e, ItemKind::dropped, Reason::noEngineEquivalent, "lfo", "lfoPointModAssignments/" + std::to_string (q), "", "", target == 2 ? "point_curve_modulation_not_modelled" : "point_modulation_target_not_modelled");
        }
        consume (as);
    }
    (void) slotsUsed;
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// velocity / note curves
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::curves()
{
    const int sc = get (0, "scalars");
    if (sc < 0) return;
    for (const char* which : { "velo", "note" })
    {
        const int c = get (sc, which);
        if (c < 0) continue;
        const bool velo = std::strcmp (which, "velo") == 0;
        std::vector<ContentPoint> pts;
        bool identity = true;
        if (pointsFromCurve (c, pts, true))
        {
            for (const ContentPoint& p : pts) if (std::fabs (p.y - p.x) > 1e-4 || std::fabs (p.power) > 1e-4) identity = false;
            for (auto& p : pts) { p.modX = {}; p.modY = {}; }
            if (! identity) (velo ? R.velocityCurve : R.noteCurve) = reduce (pts, kMaxCurvePoints);
        }
        const int lg = get (c, "legato");
        if (lg >= 0)
        {
            const std::string key = velo ? "curve.velocity.legato" : "curve.note.legato";
            store (key, num (lg) >= 0.5 ? 1 : 0);
            entry (ItemKind::imported, Reason::exact, "voice", std::string ("scalars/") + which + "/legato", valueText (lg), key, num (lg) >= 0.5 ? "On" : "Off", "");
            consume (lg);
        }
        acctRest (c, identity ? ItemKind::ignored : ItemKind::imported, identity ? Reason::defaulted : Reason::exact, "voice", std::string ("scalars/") + which, velo ? "velocity curve" : "note curve", "",
                  identity ? "curve_is_the_identity" : "scalar_curve_points_and_bends");
    }
    consume (sc);
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// the sweep and the effect chain / shape files
// ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct Agg { int count = 0; bool ui = false; std::string detail; };

void sweepNode (const cbor::Doc& d, const std::vector<uint8_t>& used, int node, std::string path, int depth, std::map<std::string, Agg>& agg, int& defaults)
{
    if (used[static_cast<size_t> (node)]) return;
    const cbor::Node& n = d.nodes[static_cast<size_t> (node)];
    if (n.type == cbor::Type::map)
    {
        for (uint32_t k = 0; k < n.count; ++k)
        {
            const std::string key = d.text (d.kids[n.first + 2 * k]);
            const int child = static_cast<int> (d.kids[n.first + 2 * k + 1]);
            if (used[static_cast<size_t> (child)]) continue;
            sweepNode (d, used, child, depth < 3 ? (path.empty() ? key : path + "/" + key) : path, depth + 1, agg, defaults);
        }
        return;
    }
    if (n.type == cbor::Type::array)
    {
        if (n.floatArray) { agg[path].count += static_cast<int> (n.count); return; }
        for (uint32_t k = 0; k < n.count; ++k)
            sweepNode (d, used, static_cast<int> (d.kids[n.first + k]), path, depth + 1, agg, defaults);
        return;
    }
    if (n.type == cbor::Type::text && d.isText (static_cast<uint32_t> (node), "default")) { ++defaults; return; }
    agg[path].count += 1;
}
} // namespace

void Importer::finish()
{
    std::map<std::string, Agg> agg;
    int defaults = 0;
    if (! d.nodes.empty()) sweepNode (d, used, 0, "", 0, agg, defaults);
    defaults += defaultLeaves;
    if (defaults > 0) entry (ItemKind::ignored, Reason::defaulted, "voice", "", "default", "", "", "blocks_at_default", defaults);
    for (const auto& kv : agg)
    {
        const std::string& p = kv.first;
        const bool ui = p.find ("kUIParam") != std::string::npos || p.rfind ("SerumGUI", 0) == 0 || p.rfind ("ClipPlayer", 0) == 0;
        const char* detail = "serum_feature_not_modelled";
        if (p.rfind ("Arp", 0) == 0) detail = "arpeggiator_not_modelled";
        else if (p.rfind ("MidiClip", 0) == 0 || p.rfind ("ClipPlayer", 0) == 0 || p.rfind ("ArpClip", 0) == 0) detail = "clip_player_not_modelled";
        else if (p.rfind ("VoicePanel", 0) == 0) detail = "voice_panel_not_modelled";
        else if (p.rfind ("LFOPointModBus", 0) == 0) detail = "lfo_point_modulation_bus_state";
        else if (p.rfind ("midiMap", 0) == 0) detail = "midi_cc_map_not_imported";
        else if (p.rfind ("RetriggerState", 0) == 0) detail = "retrigger_state_not_modelled";
        entry (ui ? ItemKind::ignored : ItemKind::dropped, ui ? Reason::exact : Reason::noEngineEquivalent, "voice", p, "", "", "", ui ? "interface_state" : detail, kv.second.count);
    }
    SerumImportReport& rep = R.report;
    rep.unaccounted = rep.leaves - (rep.numImported + rep.numApproximated + rep.numDropped + rep.numIgnored);
    // fidelity: degraded when something that sounds was dropped, close when something was approximated, else exact. The clip player, the arpeggiator, the
    // retrigger state, the MIDI map and the interface state are part of the file but not of the sound of the patch.
    int soundDropped = 0;
    for (const SerumReportEntry& e : rep.entries)
        if (e.kind == ItemKind::dropped && e.detail != "clip_player_not_modelled" && e.detail != "arpeggiator_not_modelled" && e.detail != "retrigger_state_not_modelled"
            && e.detail != "midi_cc_map_not_imported" && e.detail != "lfo_point_modulation_bus_state" && e.serumPath != "mpeConfig" && e.detail != "interface_state")
            ++soundDropped;
    rep.fidelity = soundDropped > 0 ? Fidelity::degraded : (rep.numApproximated > 0 ? Fidelity::close : Fidelity::exact);
    for (size_t i = 0; i < touched.size(); ++i)
        if (touched[i]) R.touched.push_back (static_cast<int> (i));
}

void Importer::runFxFile()
{
    meta();
    const int fxMapNode = 0;
    (void) fxMapNode;
    if (con.fileType == "SerumFXRack")
        fxRack (0, true);
    else
    {
        // a single module: the body is {"FXPhaser": {...}, "fxType": ..., "type": n}
        std::string name;
        forMap (0, [&] (const std::string& nm, int v, int) { if (nm.rfind ("FX", 0) == 0 && nm != "fxType" && isMap (v)) name = nm; });
        if (! name.empty()) fxEntry (std::max (0, std::min (2, opt.fileRack)), 0, 0, "");
        const int ft = get (0, "fxType");
        if (ft >= 0 && ! used[static_cast<size_t> (ft)]) { entry (ItemKind::imported, Reason::exact, "fx", "fxType", valueText (ft), "", "", "effect_type_name"); consume (ft); }
    }
    unmappedTopLevel();
}

void Importer::runShape()
{
    meta();
    const int lfo = get (0, "LFO");
    std::vector<ContentPoint> pts;
    int loop = -1;
    if (lfo >= 0 && pointsFromCurve (lfo, pts, true, &loop))
    {
        R.shape.name = R.meta.name;
        R.shape.smooth = false;
        const bool reduced = pts.size() > static_cast<size_t> (kMaxLfoShapePoints);
        R.shape.points = reduce (pts, kMaxLfoShapePoints);
        for (auto& p : R.shape.points) { p.modX = {}; p.modY = {}; }
        acctRest (lfo, reduced ? ItemKind::approximated : ItemKind::imported, reduced ? Reason::truncated : Reason::exact, "shape", "LFO", "", "", reduced ? "shape_resampled_to_128_points" : "shape_points_and_curves");
    }
    else if (lfo >= 0) acctRest (lfo, ItemKind::dropped, Reason::noEngineEquivalent, "shape", "LFO", "", "", "shape_unreadable");
    unmappedTopLevel();
}

void Importer::runUnmapped (const char* what)
{
    meta();
    unmappedTopLevel();
    (void) what;
}
} // namespace serumimport
} // namespace synthyboy
