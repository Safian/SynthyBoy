// SynthyBoy - core/serum-import: effect racks, modulation matrix, curves and the final sweep of the Serum 2 importer (13-new-keys.md 4.26).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
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
constexpr double kSerumEqFreq1 = 209.6, kSerumEqFreq2 = 2040.7, kSerumEqReso = 60.0; // the Serum 2.1.5 defaults of the equalizer bands (measured: tools/ref-compare/eqdef.py)
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
    const auto fileOrZero = [&] (const char* name) { const int n = find (name); return n >= 0 && isNumber (n) ? num (n) : 0.0; };
    // the bands of the Serum EQ (A508, 13-new-keys.md 4.48): the left band (Shelf | Peak | High pass) is our low band, the right band (Shelf | Peak | Low pass) our high band; a band set to Peak
    // takes the mid band (the first peak) or the second peak band eq.mid2 (the second). The engine runs the EQ law of Serum (eq.law = Serum): RBJ biquads, the Q laws of the plug-in.
    std::string eqGroup[2];
    int eqType[2] = { 0, 0 };
    if (type == kFxTypeEq)
    {
        eqType[0] = find ("kParamType1") >= 0 ? static_cast<int> (num (find ("kParamType1"))) : 0;
        eqType[1] = find ("kParamType2") >= 0 ? static_cast<int> (num (find ("kParamType2"))) : 0;
        int peaks = 0;
        for (int b = 0; b < 2; ++b)
        {
            if (eqType[b] == 1) eqGroup[b] = peaks++ == 0 ? "mid" : "mid2";
            else eqGroup[b] = b == 0 ? "low" : "high";
        }
        placed[rack].back().eq[0] = eqGroup[0];
        placed[rack].back().eq[1] = eqGroup[1];
        put ("eq.law", 1, nullptr);
        for (int b = 0; b < 2; ++b)
        {
            // the Serum defaults of the parameters the file leaves out (measured, tools/ref-compare/eqdef.py): the left band shelf at 209.6 Hz, the right band shelf at 2040.7 Hz, Q 60 %, no gain
            const std::string g = "eq." + eqGroup[b] + ".";
            const std::string n = std::to_string (b + 1);
            if (find (("kParamFreq" + n).c_str()) < 0) put (g + "cutoff", b == 0 ? kSerumEqFreq1 : kSerumEqFreq2, nullptr);
            if (find (("kParamReso" + n).c_str()) < 0) put (g + "resonance", kSerumEqReso, nullptr);
            if (find (("kParamGain" + n).c_str()) < 0) put (g + "gain", 0.0, nullptr);
            put (g + "mode", eqGroup[b] == "mid2" ? 1.0 : eqGroup[b] == "mid" ? 0.0 : (eqType[b] == 2 ? 1.0 : 0.0), nullptr);   // low: Shelf | High pass, high: Shelf | Low pass, mid: Peak, mid2: Peak (1)
        }
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
                const bool limitedModule = type == kFxTypeCompressor && find ("kParamRatio") >= 0 && isNumber (find ("kParamRatio")) && num (find ("kParamRatio")) >= kSerumLimitRatio;
                // A512: the makeup of the limiter of Serum is 36 ln (2 m - 1) / ln 61 dB (the knob range 1 .. 31 is 0 .. 36 dB; the compressor has 20 log10 m), applied after the limiter
                const auto makeupLaw = [&] (const Rule& r, double value) { return limitedModule && nm == "kParamMakeup" ? 36.0 * std::log (std::max (1.0, 2.0 * value - 1.0)) / std::log (61.0) : convert (r, value); };
                if (handled) { if (rule.conv != Conv::enumName && isNumber (v)) put (rule.key, makeupLaw (rule, num (v)), nullptr); continue; }
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
                const double ours = put (targetKey, networkFeedback ? num (v) : makeupLaw (rule, num (v)), &cl);
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
                const std::string& group = eqGroup[band - 1];
                std::string key;
                if (nm.rfind ("kParamFreq", 0) == 0) key = "eq." + group + ".cutoff";
                else if (nm.rfind ("kParamGain", 0) == 0) key = "eq." + group + ".gain";
                else if (nm.rfind ("kParamReso", 0) == 0) key = "eq." + group + ".resonance";
                if (key.empty())
                {
                    // the band type is the mode of its group, written above
                    entry (ItemKind::imported, Reason::exact, area, p, valueText (v), "eq." + group + ".mode", fmt (group == "mid2" ? 1.0 : group == "mid" ? 0.0 : (eqType[band - 1] == 2 ? 1.0 : 0.0)), "");
                }
                else
                {
                    bool cl = false;
                    const double st = put (key, num (v), &cl);
                    entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, p, valueText (v), key, fmt (st), cl ? "clamped_to_range" : "");
                }
                consume (v);
            }
        });
    // A508 (13-new-keys.md 4.48): a Serum file stores only the parameters that differ from the defaults of the module, so a parameter that is not in the file has the Serum default, not ours
    // (measured on the real plug-in 2.1.5: tools/ref-compare/findef.py renders the module with the parameter absent and with candidate values and takes the value that gives the same render)
    for (size_t r = 0; r < count; ++r)
    {
        const Rule& rule = rows[r];
        if (! std::isfinite (rule.serumDefault) || rule.conv == Conv::enumName || find (rule.param) >= 0)
            continue;
        bool keyGivenByTheFile = false;   // another parameter of the file writes the same key (reverb: PreDelay and Delay): the default of the absent one must not overwrite it
        for (size_t q = 0; q < count; ++q)
            if (q != r && std::strcmp (rows[q].key, rule.key) == 0 && find (rows[q].param) >= 0) keyGivenByTheFile = true;
        if (keyGivenByTheFile)
            continue;
        const bool networkFeedback = type == kFxTypeReverb && rule.conv == Conv::logPercent && std::strcmp (rule.key, "reverb.decay") == 0 && find ("kParamType") >= 0
                                     && isText (find ("kParamType")) && (str (find ("kParamType")) == "kSpace" || str (find ("kParamType")) == "kAbyss");
        put (networkFeedback ? "reverb.feedback" : rule.key, networkFeedback ? rule.serumDefault : convert (rule, rule.serumDefault), nullptr);
    }
    // A510 (13-new-keys.md 4.48): the delay time of Serum with BeatSync on (the default of the module): the file value 0 .. 0.34 is a position on a scale of note values, not a time (measured on the
    // plug-in: the onset of the echo of a delay at 100 % wet, 120 BPM); the steps are 15.5 ms, 57.6 ms, 1/16, 1/8, 1/4, 1/2, 1 bar and 2 bars. The divisions of ours follow the tempo of the host like
    // Serum does; the two short steps and the zero have no division in ours and take the time in milliseconds
    if (type == kFxTypeDelay)
    {
        const auto position = [&] (const char* param, double fallback)
        {
            const int n = find (param);
            return n >= 0 && isNumber (n) ? num (n) : fallback;
        };
        const bool beatSync = position ("kParamBeatSync", 1.0) >= 0.5;
        if (! beatSync) put ("delay.sync", 0, nullptr);
        else
        {
            struct Step { double upTo, ms; int division; };   // division: index in the list of delay.division, -1 = none
            static const Step kSteps[] = { { 0.0023, 0.2, -1 }, { 0.0062, 15.5, -1 }, { 0.015, 57.6, -1 }, { 0.0322, 125.0, 6 }, { 0.062, 250.0, 5 }, { 0.110, 500.0, 4 }, { 0.179, 1000.0, 3 },
                                           { 0.2775, 2000.0, 2 }, { 1.0e9, 4000.0, 1 } };
            const auto stepOf = [&] (double x) -> const Step& { for (const Step& s : kSteps) if (x < s.upTo) return s; return kSteps[8]; };
            const Step& l = stepOf (position ("kParamTimeL", 0.06236));
            const Step& r = stepOf (position ("kParamTimeR", 0.07642));
            if (l.division >= 0 && r.division >= 0)
            {
                put ("delay.sync", 1, nullptr);
                put ("delay.division", l.division, nullptr);
                put ("delay.division2", r.division, nullptr);
            }
            else
            {
                put ("delay.sync", 0, nullptr);
                put ("delay.time", l.ms, nullptr);
                put ("delay.time2", r.ms, nullptr);
            }
        }
    }
    // A513 (13-new-keys.md 4.48): the decay of the Serum reverbs (measured: the energy decay of the wet signal of an impulse, T60 by the slope between -4 and -25 dB): Plate follows the SIZE knob (0.36 s up to
    // size 8, 1.0 s at 20, 2.2 s at 30, 4.6 s at 40, 8.4 s at 50, 11 s at 70, 12 s from 80; FEEDBACK has no effect), Hall is 3.2 s whatever the size, Vintage follows VintageScale (0.93 s at 0, 2.0 s at 50,
    // 3.9 s at 100); Space and Abyss keep their feedback. The earlier mapping took the FEEDBACK knob of the Plate for the decay (0.12 s at its default)
    if (type == kFxTypeReverb)
    {
        const int tn = find ("kParamType");
        const std::string typeName = tn >= 0 && isText (tn) ? str (tn) : std::string ("kPlate");
        const auto position = [&] (const char* param, double fallback) { const int n = find (param); return n >= 0 && isNumber (n) ? num (n) : fallback; };
        const auto lerp = [] (const double* xs, const double* ys, int n, double v) { int i = 0; while (i + 2 < n && v > xs[i + 1]) ++i; const double t = std::max (0.0, std::min (1.0, (v - xs[i]) / (xs[i + 1] - xs[i]))); return ys[i] + (ys[i + 1] - ys[i]) * t; };
        double seconds = -1.0;
        if (typeName == "kPlate")
        {
            static const double kSize[] = { 0, 8, 10, 15, 18, 20, 25, 28, 30, 35, 40, 50, 60, 70, 80 };
            static const double kT60[] = { 0.36, 0.30, 0.48, 0.46, 0.77, 1.0, 1.6, 2.1, 2.2, 3.5, 4.6, 8.4, 10.6, 11.1, 12.0 };
            seconds = lerp (kSize, kT60, 15, position ("kParamSize", 35.0));
        }
        else if (typeName == "kHall") seconds = 3.2;
        else if (typeName == "kVintage")
        {
            static const double kScale[] = { 0, 50, 100 };
            static const double kT60[] = { 0.93, 2.04, 3.9 };
            seconds = lerp (kScale, kT60, 3, position ("kParamVintageScale", 0.0));
        }
        if (seconds > 0.0) put ("reverb.decay", seconds * 1000.0, nullptr);
    }
    // A509 (13-new-keys.md 4.48): the wet signal of a Serum module has another level than ours (measured, tools/ref-compare/make_fx_laws.py: a C3 saw at 100 % wet, Serum minus ours); the dry signal
    // stays at unity: the mix and the level of our module are remapped so that dry * (1 - w) + wet * w * r comes out of it (L = (1 - w) + w r, mix = w r / L, level + 20 log10 L). The distortion
    // also takes its drive from the measured law of the mode (the harmonic shapes of the modes follow ours, the drive scale does not)
    {
        const auto fileOrDefault = [&] (const char* param, double fallback)
        {
            const int n = find (param);
            return n >= 0 && isNumber (n) ? num (n) : fallback;
        };
        const Rule* wetRule = nullptr;
        const Rule* levelRule = nullptr;
        for (size_t r = 0; r < count; ++r)
        {
            if (std::strcmp (rows[r].param, "kParamWet") == 0) wetRule = &rows[r];
            if (std::strcmp (rows[r].param, "kParamLevelOut") == 0) levelRule = &rows[r];
        }
        double wetGain = 0.0;
        if (type == kFxTypeDistortion)
        {
            const int mn = find ("kParamMode");
            const std::string mode = mn >= 0 && isText (mn) ? str (mn) : std::string ("kTube");
            double ourDrive = 0.0, gain = 0.0;
            if (fxDistortionLaw (mode, fileOrDefault ("kParamDrive", 25.0), ourDrive, gain))
            {
                put ("distortion.drive", ourDrive, nullptr);
                wetGain = gain;
            }
        }
        else if (type == kFxTypeHyperDimension)
        {
            const double unison = fileOrDefault ("kParamUnison", 4.0);
            wetGain = hyperWetGainDb (unison);
            if (unison < 0.5) put ("hyperdim.mix", 0.0, nullptr);   // no voices: the module passes the dry signal
        }
        else if (type == kFxTypeReverb)
        {
            // the level of the wet signal by reverb type, measured on the default parameters of each type (Serum minus ours; the Vintage level varies with VintageScale by 4 dB around this)
            const int tn = find ("kParamType");
            const std::string typeName = tn >= 0 && isText (tn) ? str (tn) : std::string ("kPlate");
            wetGain = typeName == "kHall" ? -6.7 : typeName == "kVintage" ? -5.0 : typeName == "kSpace" ? -13.0 : typeName == "kAbyss" ? 4.5 : 0.8;
        }
        else wetGain = fxWetGainDb (type);
        double probeWet = 0.0, probeLevel = 0.0;
        const bool haveLaw = fxMixRemap (type, 0.5, 0.0, probeWet, probeLevel);
        if ((std::fabs (wetGain) > 0.05 || haveLaw) && wetRule != nullptr && levelRule != nullptr && ! (type == kFxTypeHyperDimension && fileOrDefault ("kParamUnison", 4.0) < 0.5))
        {
            const double w = std::max (0.0, std::min (1.0, fileOrDefault ("kParamWet", wetRule->serumDefault) * 0.01));
            const double levelDb = convert (*levelRule, fileOrDefault ("kParamLevelOut", levelRule->serumDefault));
            double oursWet = 0.0, extraDb = 0.0;
            if (! fxMixRemap (type, w, wetGain, oursWet, extraDb))
            {
                const double r = std::pow (10.0, wetGain / 20.0);
                const double L = (1.0 - w) + w * r;
                oursWet = w * r / L;
                extraDb = 20.0 * std::log10 (L);
            }
            put (wetRule->key, 100.0 * oursWet, nullptr);
            put (levelRule->key, levelDb + extraDb, nullptr);
        }
    }
    // A511 (13-new-keys.md 4.48): the multiband mode of the Serum compressor (measured on a sine in a band and on a saw): three bands (the crossovers of the module), each with a threshold (the UD knob 0 .. 200:
    // the knee is the peak ceiling -51 / -33.6 / -20.3 / -7.9 dB at 0 / 50 / 100 / 150, +0.25 dB per step beyond; the global THRESH shifts all the knees: +9 dB at 0.42, 0 at 0.5, -15 at 0.6, -28 at 0.7, -41 at 0.8,
    // no compression above +20 dB under 0.3), a ratio knob 0 .. 1 that goes from 1:1 to the global RATIO (the amplitude exponent is r ^ 0.9 (1 - 1 / R), at most 0.8, 0.78 for the limit detent: the slopes above the knee 0.93 / 0.64 /
    // 0.28 at 0.1 / 0.5 / 1 for R = 4) and a gain after the compression; the makeup of the module comes on top of the gains. The global threshold and ratio do not compress by themselves in this mode
    if (type == kFxTypeCompressor && fileOrZero ("kParamMultiband") >= 0.5)
    {
        static const double kUdKnee[] = { -51.0, -33.6, -20.3, -7.9, 4.5 };   // at UD 0, 50, 100, 150, 200 (measured at the output with the master 1.0 of +2.98 dB, taken off)
        static const double kUdDefault[3] = { 72.533, 100.0, 100.0 };
        static const double kThreshAt[] = { 0.0, 0.3, 0.42, 0.5, 0.6, 0.7, 0.8, 1.0 };
        static const double kShiftDb[] = { 30.0, 22.0, 9.1, 0.0, -15.1, -27.8, -41.0, -70.0 };
        static const char* const kBand[3] = { "low", "mid", "high" };
        const auto fileOr = [&] (const char* param, double fallback) { const int n = find (param); return n >= 0 && isNumber (n) ? num (n) : fallback; };
        const double makeupDb = 20.0 * std::log10 (std::max (1.0, fileOr ("kParamMakeup", 1.0)));
        const double thresh = std::max (0.0, std::min (1.0, fileOr ("kParamThresh", 0.5)));
        int ti = 0;
        while (ti + 2 < 8 && thresh > kThreshAt[ti + 1]) ++ti;
        const double shift = kShiftDb[ti] + (kShiftDb[ti + 1] - kShiftDb[ti]) * (thresh - kThreshAt[ti]) / (kThreshAt[ti + 1] - kThreshAt[ti]);
        const double globalRatio = std::max (1.0, fileOr ("kParamRatio", 4.0));
        const double strength = globalRatio >= 1.0e5 ? 0.78 : std::min (0.8, (1.0 - 1.0 / globalRatio) * 1.0);
        for (int b = 0; b < 3; ++b)
        {
            const std::string n = std::to_string (b);
            const double ud = std::max (0.0, std::min (200.0, fileOr (("kParamThreshUD" + n).c_str(), kUdDefault[b])));
            const int seg = std::min (3, static_cast<int> (ud / 50.0));
            const double knee = kUdKnee[seg] + (kUdKnee[seg + 1] - kUdKnee[seg]) * (ud - 50.0 * seg) / 50.0 + shift;
            const double ratio = std::max (0.0, std::min (1.0, fileOr (("kParamRatio" + n).c_str(), 1.0)));
            const std::string g = std::string ("compressor.") + kBand[b] + ".";
            put (g + "upper_threshold", std::max (-120.0, knee), nullptr);
            put (g + "upper_ratio", 100.0 * strength * std::pow (ratio, 0.9), nullptr);
            put (g + "gain", makeupDb + fileOr (("kParamGain" + n).c_str(), 0.0), nullptr);
        }
    }
    // Serum defaults of enumerations the file leaves out
    if (type == kFxTypeDistortion && find ("kParamMode") < 0) put ("distortion.type", 6, nullptr);
    if (type == kFxTypeReverb && find ("kParamType") < 0) put ("reverb.type", 1, nullptr);
    if (type == kFxTypeFilter && find ("kParamType") < 0)
    {
        int m = 0, s2 = 0;
        Fid fid = Fid::exact;
        if (filterTypeFromName ("MgL6", m, s2, fid)) { put ("fxfilter.model", m, nullptr); put ("fxfilter.style", s2, nullptr); }
    }
    if (type == kFxTypeCompressor)
    {
        // the compressor of Serum 2.1.5 is the peak compressor of the engine (compressor.limit = Serum); a ratio at the LIMIT detent is the look-ahead limiter
        const int rn = find ("kParamRatio");
        put ("compressor.limit", rn >= 0 && isNumber (rn) && num (rn) >= kSerumLimitRatio && fileOrZero ("kParamMultiband") < 0.5 ? 1 : 2, nullptr);   // in the multiband mode the ratio at the limit detent is the ceiling of the band ratios
    }
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
        else if (nm == "PZs")
        {
            // REMAINDER item 4 (S4): the response curve of the FX filter (the main FX filter only: the curve of the content belongs to fxfilter)
            const int typeNode = find ("kParamType");
            const bool pzFilterType = instanceZero && type == kFxTypeFilter && isText (typeNode) && str (typeNode) == "PZ_SVF";
            pzFilter (v, pp, instanceZero && type == kFxTypeFilter ? "fxfilter" : "", pzFilterType, area, p);
        }
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
                    out.dbLaw = rows[i].conv == Conv::dbFromLin || rows[i].conv == Conv::compThresh;   // the measured threshold is a dB value too
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
            // GROUPB WP-05 (A399): the single slice destination: the depth is a share of the slice list (+-100 % = every slice)
            if (param == "kParamSlicingSingleSlice") { out.key = keyOf ("osc#.sample.slice", n); out.ok = true; out.fid = Fid::approx; out.note = "single_slice_depth_is_a_share_of_the_slice_list"; return out; }
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
    else if (type == "ModSlot")
    {
        // REMAINDER item 1 (A449): the RISE / FALL / DELAY of another slot are modulatable keys now (backlog S8); the depth is a share of the key's range (RISE / FALL: the Serum knob is
        // quadratic in time, so the depth is approximate; DELAY: the destinations seen are tempo synced, so the beats key)
        if (id >= 0 && id < kNumModSlots)
        {
            const std::string pfx = modPrefix (id);
            if (param == "kParamSmoothRise") { out.key = pfx + ".rise"; out.ok = true; out.fid = Fid::approx; out.note = "slot_rise_destination_depth_is_a_share_of_the_range"; return out; }
            if (param == "kParamSmoothFall") { out.key = pfx + ".fall"; out.ok = true; out.fid = Fid::approx; out.note = "slot_fall_destination_depth_is_a_share_of_the_range"; return out; }
            if (param == "kParamDelayOffset") { out.key = pfx + ".delay_beats"; out.ok = true; out.fid = Fid::approx; out.note = "slot_delay_destination_depth_is_a_share_of_the_range"; return out; }
        }
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
                        out.dbLaw = rows[i].conv == Conv::dbFromLin || rows[i].conv == Conv::compThresh;   // the measured threshold is a dB value too
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
                    for (auto& p : mc.points) { p.modX = {}; p.modY = {}; p.modCurve = {}; }
                    R.modCurves.push_back (std::move (mc));
                    acctRest (c, ItemKind::imported, Reason::exact, area, name + "/flex/0", pfx + ".source", "curve", "modulation_curve_stored");
                }
                else acctRest (c, ItemKind::dropped, Reason::noEngineEquivalent, area, name + "/flex/" + std::to_string (q), "", "", q == 1 ? "aux_curve_not_modelled" : "modulation_curve_unreadable");
            }
            consume (flex);
        }
        // RISE / FALL of the slot (GROUPB WP-08, A408; the keys modNN.rise / fall since REMAINDER item 1, A449): kParamSmoothRise / Fall are 0 .. 100 of the knob, read as
        // 2000 ms x (v / 100)^2; kParamSmoothLink (absent: linked) makes a missing one follow the stored one
        {
            const int riseNode = get (pp, "kParamSmoothRise"), fallNode = get (pp, "kParamSmoothFall"), linkNode = get (pp, "kParamSmoothLink");
            if (riseNode >= 0 || fallNode >= 0)
            {
                double rise = riseNode >= 0 ? num (riseNode) : 0.0, fall = fallNode >= 0 ? num (fallNode) : 0.0;
                const bool linked = linkNode >= 0 ? num (linkNode) >= 0.5 : true;
                if (linked && riseNode >= 0 && fallNode < 0) fall = rise;
                if (linked && fallNode >= 0 && riseNode < 0) rise = fall;
                const auto toMs = [] (double v) { const double u = clampd (v, 0.0, 100.0) / 100.0; return std::min<double> (kMaxModSlewMs, 2000.0 * u * u); };
                const double riseMs = toMs (rise), fallMs = toMs (fall);
                store (pfx + ".rise", riseMs);
                store (pfx + ".fall", fallMs);
                setByFile[static_cast<size_t> (indexOf (pfx + ".rise"))] = 1;
                setByFile[static_cast<size_t> (indexOf (pfx + ".fall"))] = 1;
                if (riseNode >= 0) { entry (ItemKind::approximated, Reason::enumRemapped, area, name + "/plainParams/kParamSmoothRise", valueText (riseNode), pfx + ".rise", fmt (riseMs), "modulation_rise_knob_law_assumed"); consume (riseNode); }
                if (fallNode >= 0) { entry (ItemKind::approximated, Reason::enumRemapped, area, name + "/plainParams/kParamSmoothFall", valueText (fallNode), pfx + ".fall", fmt (fallMs), "modulation_fall_knob_law_assumed"); consume (fallNode); }
                if (linkNode >= 0) { entry (ItemKind::ignored, Reason::exact, area, name + "/plainParams/kParamSmoothLink", valueText (linkNode), "", "", "rise_fall_link_flag"); consume (linkNode); }
            }
        }
        {
            // the link flag of a slot that has no RISE / FALL: nothing to link
            const int linkLeft = get (pp, "kParamSmoothLink");
            if (linkLeft >= 0 && ! used[static_cast<size_t> (linkLeft)]) { entry (ItemKind::ignored, Reason::exact, area, name + "/plainParams/kParamSmoothLink", valueText (linkLeft), "", "", "rise_fall_link_flag_without_a_time"); consume (linkLeft); }
        }
        // DELAY of the slot (REMAINDER item 1, A450): kParamDelayOffset 0 .. 1 with kParamDelayBeatSync: tempo synced one bar (4 beats) x the value, otherwise 4000 ms x the value
        {
            const int delayNode = get (pp, "kParamDelayOffset"), syncNode = get (pp, "kParamDelayBeatSync");
            if (delayNode >= 0)
            {
                const double v = clampd (num (delayNode), 0.0, 1.0);
                const bool sync = syncNode >= 0 && num (syncNode) >= 0.5;
                const std::string key = pfx + (sync ? ".delay_beats" : ".delay");
                bool cl = false;
                const double st = store (key, sync ? 4.0 * v : 4000.0 * v, &cl);
                setByFile[static_cast<size_t> (indexOf (key))] = 1;
                if (sync)
                {
                    store (pfx + ".delay_sync", 1);
                    setByFile[static_cast<size_t> (indexOf (pfx + ".delay_sync"))] = 1;
                }
                entry (ItemKind::approximated, Reason::enumRemapped, area, name + "/plainParams/kParamDelayOffset", valueText (delayNode), key, fmt (st), "modulation_delay_scale_assumed");
                consume (delayNode);
            }
            if (syncNode >= 0)
            {
                if (delayNode >= 0) entry (ItemKind::imported, Reason::exact, area, name + "/plainParams/kParamDelayBeatSync", valueText (syncNode), pfx + ".delay_sync", "", "modulation_delay_tempo_sync");
                else entry (ItemKind::ignored, Reason::exact, area, name + "/plainParams/kParamDelayBeatSync", valueText (syncNode), "", "", "delay_sync_without_delay");
                consume (syncNode);
            }
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
            if (bus >= 0 && bus < 16 && buses[bus].used && type == 0 && lfoId >= 0 && lfoId < 10 && target >= 0 && target <= 2)
                for (ContentLfoShape& shape : R.lfoShapes)
                    if (shape.lfo == lfoId + 1 && lfoOrigPoints[lfoId] > 0 && static_cast<int> (shape.points.size()) == lfoOrigPoints[lfoId] + lfoPointOffset[lfoId])
                    {
                        const int at = point + lfoPointOffset[lfoId];
                        if (at < 0 || at >= static_cast<int> (shape.points.size())) break;
                        ContentModRef& m = target == 0 ? shape.points[static_cast<size_t> (at)].modX : target == 1 ? shape.points[static_cast<size_t> (at)].modY : shape.points[static_cast<size_t> (at)].modCurve;
                        m.present = true;
                        m.source = buses[bus].source;
                        m.amount = static_cast<float> (buses[bus].amount);
                        done = true;
                    }
            if (done) acctRest (e, ItemKind::approximated, Reason::tierNote, "lfo" + std::to_string (lfoId + 1), "lfoPointModAssignments/" + std::to_string (q), "", "", target == 0 ? "point_x_modulation" : target == 1 ? "point_y_modulation" : "point_curve_modulation");
            else acctRest (e, ItemKind::dropped, Reason::noEngineEquivalent, "lfo", "lfoPointModAssignments/" + std::to_string (q), "", "", "point_modulation_target_not_modelled");
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
            for (auto& p : pts) { p.modX = {}; p.modY = {}; p.modCurve = {}; }
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
            && e.detail != "midi_cc_map_not_imported" && e.detail != "midi_map_parameter_not_decoded" && e.detail != "midi_map_cc_not_learnable" && e.detail != "lfo_point_modulation_bus_state" && e.serumPath != "mpeConfig" && e.detail != "interface_state")
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
        for (auto& p : R.shape.points) { p.modX = {}; p.modY = {}; p.modCurve = {}; }
        acctRest (lfo, reduced ? ItemKind::approximated : ItemKind::imported, reduced ? Reason::truncated : Reason::exact, "shape", "LFO", "", "", reduced ? "shape_resampled_to_128_points" : "shape_points_and_curves");
    }
    else if (lfo >= 0) acctRest (lfo, ItemKind::dropped, Reason::noEngineEquivalent, "shape", "LFO", "", "", "shape_unreadable");
    unmappedTopLevel();
}

void Importer::runMidiMap()
{
    meta();
    midiMapSection (get (0, "midiMap"), "midiMap");
    unmappedTopLevel();
}

void Importer::runStyle()
{
    meta();
    voicePanel (true);
    unmappedTopLevel();
}

void Importer::runPzFile()
{
    meta();
    const int single = get (0, "PZ"), multi = get (0, "PZs");
    if (single >= 0) pzFilter (single, -1, "", true, "filter", "PZ");
    else if (multi >= 0) pzFilter (multi, -1, "", true, "filter", "PZs");
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
