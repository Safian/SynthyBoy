// SynthyBoy - core/serum-import: oscillators, noise, sub, routing, filters, envelopes and LFOs of a Serum 2 preset (13-new-keys.md 4.26).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

#include "serum_importer.h"
#include "synthyboy/filters/filter_types.h"
#include "vital_import_internal.h"

namespace synthyboy
{
namespace serumimport
{
namespace
{
double clampd (double v, double lo, double hi) { return std::max (lo, std::min (hi, v)); }
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------
// the granular oscillator (GROUPC WP-11, assumptions A412 .. A414): density and grain length are a value plus a mode
// ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
/** Beats of an entry of kGrainDivisions (3 per division: straight, triplet, dotted; 1/1 .. 1/128), the list of oscN.gran.density_sync / length_sync. */
double divisionBeatsOf (int index)
{
    const double beats = 4.0 / static_cast<double> (1 << (index / 3));
    return index % 3 == 0 ? beats : (index % 3 == 1 ? beats * 2.0 / 3.0 : beats * 1.5);
}

int nearestDivision (double beats)
{
    int best = 12;
    double bestError = 1.0e9;
    for (int i = 0; i < 24; ++i)
    {
        const double e = std::fabs (std::log2 (std::max (1.0e-6, beats) / divisionBeatsOf (i)));
        if (e < bestError)
        {
            bestError = e;
            best = i;
        }
    }
    return best;
}
} // namespace

void Importer::granularRates (int n, int plain, const std::string& area, const std::string& path)
{
    const auto key = [&] (const char* tail) { return area + "." + tail; };
    // 0 free, 1 BPM, 2 grains (density), 3 percent (length); the node is reported once the key is stored
    int modeNode[2] = { -1, -1 };
    const auto modeOf = [&] (const char* param, int slot, int& mode, bool& present)
    {
        mode = -1;
        present = false;
        const int node = get (plain, param);
        if (node < 0 || ! isText (node))
            return;
        present = true;
        modeNode[slot] = node;
        const std::string name = str (node);
        mode = name.find ("BPM") != std::string::npos ? 1 : (name.find ("Grain") != std::string::npos ? 2 : (name.find ("Percent") != std::string::npos ? 3 : 0));
    };
    (void) n;

    // density: Free = Hz, BPM = the beats between two grains (nearest division), Grains = the number of grains that play at the same time
    int dMode = 0;
    bool dPresent = false;
    modeOf ("kParamDensityMode", 0, dMode, dPresent);
    if (dMode < 0)
        dMode = 0;
    const int dn = get (plain, "kParamDensity");
    if (dPresent || dn >= 0)
    {
        store (key ("gran.density_mode"), dMode);
        setByFile[static_cast<size_t> (indexOf (key ("gran.density_mode")))] = 1;
        if (modeNode[0] >= 0)
        {
            static const char* const names[3] = { "Free", "BPM Sync", "Grains" };
            entry (ItemKind::imported, Reason::exact, area, path + "/kParamDensityMode", valueText (modeNode[0]), key ("gran.density_mode"), names[dMode], "granular_density_mode");
            consume (modeNode[0]);
        }
    }
    if (dn >= 0)
    {
        const double v = num (dn);
        bool cl = false;
        const char* target = dMode == 1 ? "gran.density_sync" : (dMode == 2 ? "gran.density_grains" : "gran.density");
        const double st = store (key (target), dMode == 1 ? nearestDivision (v) : (dMode == 2 ? std::round (v) : v), &cl);
        setByFile[static_cast<size_t> (indexOf (key (target)))] = 1;
        entry (cl || dMode == 1 ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : (dMode == 1 ? Reason::enumRemapped : Reason::exact), area, path + "/kParamDensity", valueText (dn),
               key (target), fmt (st), dMode == 1 ? "granular_density_beats_to_the_nearest_division" : (dMode == 2 ? "granular_density_grains" : "granular_density_in_hz_assumed"));
        consume (dn);
    }

    // grain length: Free = seconds, BPM = beats (nearest division), Percent = a multiple of the density period; no mode in the file = Percent (A413)
    int lMode = 3;
    bool lPresent = false;
    modeOf ("kParamLengthMode", 1, lMode, lPresent);
    if (lMode < 0)
        lMode = 3;
    const int ln = get (plain, "kParamGrainLength");
    if (lPresent || ln >= 0)
    {
        const int stored = lMode == 3 ? 2 : lMode == 1 ? 1 : 0;
        store (key ("gran.length_mode"), stored);
        setByFile[static_cast<size_t> (indexOf (key ("gran.length_mode")))] = 1;
        if (modeNode[1] >= 0)
        {
            static const char* const names[3] = { "Free", "BPM Sync", "Percent" };
            entry (ItemKind::imported, Reason::exact, area, path + "/kParamLengthMode", valueText (modeNode[1]), key ("gran.length_mode"), names[stored], "granular_length_mode");
            consume (modeNode[1]);
        }
    }
    if (ln >= 0)
    {
        const double v = num (ln);
        bool cl = false;
        const char* target = lMode == 1 ? "gran.length_sync" : (lMode == 3 ? "gran.length_percent" : "gran.length");
        const double raw = lMode == 1 ? nearestDivision (v) : (lMode == 3 ? v * 100.0 : v * 1000.0);
        const double st = store (key (target), raw, &cl);
        setByFile[static_cast<size_t> (indexOf (key (target)))] = 1;
        entry (cl || lMode == 1 || lMode == 3 ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : (lMode == 1 || lMode == 3 ? Reason::enumRemapped : Reason::exact), area,
               path + "/kParamGrainLength", valueText (ln), key (target), fmt (st),
               lMode == 1 ? "granular_length_beats_to_the_nearest_division" : (lMode == 3 ? "granular_length_as_a_multiple_of_the_density_period_assumed" : "granular_length_in_seconds"));
        consume (ln);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// oscillators
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::oscillator (int i)
{
    const int n = i + 1;
    const std::string on = "Oscillator" + std::to_string (i), area = "osc" + std::to_string (n);
    const auto key = [&] (const char* tail) { return area + "." + tail; };
    store (key ("destination"), i == 0 ? 5 : 3); // routing() overwrites it from the RoutingSlot
    const int O = get (0, on.c_str());
    if (O < 0)
    {
        store (key ("on"), i == 0 ? 1 : 0);
        return;
    }
    const int pp = get (O, "plainParams");
    std::string typeName = "kOsc_WT";
    const int tn = get (pp, "kParamType");
    if (isText (tn)) typeName = str (tn);
    int kind = 0; // 0 WT, 1 multisample, 2 sample, 3 granular, 4 spectral
    if (typeName == "kOsc_MultiSample") kind = 1;
    else if (typeName == "kOsc_Sample") kind = 2;
    else if (typeName == "kOsc_Granular") kind = 3;
    else if (typeName == "kOsc_Spectral") kind = 4;
    if (tn >= 0)
    {
        const bool known = kind != 0 || typeName == "kOsc_WT";
        store (key ("type"), kind == 0 ? 0 : (kind == 3 && opt.granularOsc ? kOscTypeGranular : (kind == 4 && opt.spectralOsc ? kOscTypeSpectral : (kind == 1 && opt.multisampleOsc ? kOscTypeMultisample : 1)))); // GROUPC: Granular and Spectral are their own types (a sample oscillator on an engine without them)
        setByFile[static_cast<size_t> (indexOf (key ("type")))] = 1;
        entry (! known ? ItemKind::dropped : (kind <= 0 || kind == 2 || kind == 3 || kind == 4 || (kind == 1 && opt.multisampleOsc) ? ItemKind::imported : ItemKind::approximated), known ? Reason::exact : Reason::enumRemapped, area, on + "/plainParams/kParamType",
               typeName, key ("type"), kind == 0 ? "Wavetable" : (kind == 3 && opt.granularOsc ? "Granular" : (kind == 4 && opt.spectralOsc ? "Spectral" : (kind == 1 && opt.multisampleOsc ? "Multisample" : "Sample"))),
               kind == 1 && ! opt.multisampleOsc ? "multisample_reduced_to_one_zone" : (kind == 4 && ! opt.spectralOsc ? "spectral_played_as_sample" : (kind == 3 && ! opt.granularOsc ? "granular_played_as_sample" : "")));
        consume (tn);
    }
    else store (key ("type"), 0);
    const bool sampleLike = kind != 0;

    std::vector<Unmapped> um;
    applyScope ({ "Osc" }, pp, n, area, on + "/plainParams", um, true,
                { "kParamType", "kParamOctave", "kParamPitch", "kParamPitchRatio", "kParamScanRate", "kParamLoopCrossfade", "kParamEnable" });

    {
        // the unison of Serum 2.1.5: the level of the centre and the outer voices by blend and voice count, the stereo spread and the evenly spaced voices are the engine's Serum law
        // (oscN.unison.law, 13-new-keys.md 4.47); the detune of the voices is linear (no Vital curve)
        store (key ("unison.law"), 1);
        store (key ("unison.detune_curve"), 0);
    }

    {
        // A517 (13-new-keys.md 4.48): a sample, a granular and a spectral oscillator of ours plays quieter than the Serum one at the same volume (controlled experiments: the same sample in both, C3, the
        // default volume: -3.0 dB for 8 of 8 samples of the sample oscillator, -4.9 dB median for the granular one (-4.6 .. -5.2 for 6 of 8), -14.6 dB median for the spectral one (-12.0 .. -18.3, a constant of the engine: it does not fit the
        // range of the level); the multisample instrument has its own trim in the zones). The level of the oscillator is raised by it as far as its range goes (100 %)
        if (kind >= 2)
        {
            const double trimDb = kind == 2 ? 3.0 : (kind == 3 ? 4.9 : 0.0);   // the spectral oscillator carries its 14.6 dB in the engine
            const int li = indexOf (key ("level"));
            if (li >= 0) store (key ("level"), std::min (100.0, static_cast<double> (R.values.plain[static_cast<size_t> (li)]) * std::pow (10.0, trimDb / 20.0)));
        }
    }

    // on / off (OSC A is on in the init state)
    {
        const int en = get (pp, "kParamEnable");
        const double v = en >= 0 ? num (en) : (i == 0 ? 1.0 : 0.0);
        store (key ("on"), v >= 0.5 ? 1 : 0);
        if (en >= 0)
        {
            setByFile[static_cast<size_t> (indexOf (key ("on")))] = 1;
            entry (ItemKind::imported, Reason::exact, area, on + "/plainParams/kParamEnable", valueText (en), key ("on"), v >= 0.5 ? "On" : "Off", "");
            consume (en);
        }
        else ++R.report.numDefaulted;
    }
    // octave + semitone -> transpose
    {
        const int oc = get (pp, "kParamOctave"), pi = get (pp, "kParamPitch");
        const double oct = oc >= 0 ? num (oc) : 0.0, semi = pi >= 0 ? num (pi) : 0.0;
        bool cl = false;
        const double st = store (key ("transpose"), oct * 12.0 + semi, &cl);
        if (oc >= 0 || pi >= 0) setByFile[static_cast<size_t> (indexOf (key ("transpose")))] = 1;
        char text[32];
        std::snprintf (text, sizeof text, "%.0f", st);
        if (oc >= 0) { entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, on + "/plainParams/kParamOctave", valueText (oc), key ("transpose"), text, "octave_times_12_plus_semitones"); consume (oc); }
        if (pi >= 0) { entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, on + "/plainParams/kParamPitch", valueText (pi), key ("transpose"), text, "octave_times_12_plus_semitones"); consume (pi); }
    }
    // pitch ratio / harmonics: the ratio travels in the coarse slot (4.16)
    {
        const int pr = get (pp, "kParamPitchRatio");
        if (pr >= 0)
        {
            const int mode = static_cast<int> (R.values.plain[static_cast<size_t> (indexOf (key ("pitch_mode")))]);
            if (mode >= 1)
            {
                bool cl = false;
                const double st = store (key ("pitch_coarse"), num (pr), &cl);
                entry (ItemKind::approximated, cl ? Reason::clamped : Reason::enumRemapped, area, on + "/plainParams/kParamPitchRatio", valueText (pr), key ("pitch_coarse"), fmt (st), "pitch_ratio_in_coarse_slot");
            }
            else
                entry (ItemKind::dropped, Reason::noEngineEquivalent, area, on + "/plainParams/kParamPitchRatio", valueText (pr), "", "", "pitch_ratio_without_ratio_mode");
            consume (pr);
        }
    }
    // the sample player scan rate (percent of the original speed) -> our scan within the smallest range that holds it
    {
        const int sr = get (pp, "kParamScanRate");
        if (sr >= 0)
        {
            const double rate = num (sr);
            const double mag = std::fabs (rate);
            const int range = mag <= 100.0 ? 0 : mag <= 200.0 ? 1 : mag <= 400.0 ? 2 : 3;
            const double span = 100.0 * std::pow (2.0, range);
            store (key ("sample.scan_range"), range);
            const double st = store (key ("sample.scan"), clampd (rate / span * 100.0, -100.0, 100.0));
            setByFile[static_cast<size_t> (indexOf (key ("sample.scan")))] = 1;
            entry (ItemKind::imported, Reason::exact, area, on + "/plainParams/kParamScanRate", valueText (sr), key ("sample.scan"), fmt (st), "scan_range_chosen_to_fit");
            consume (sr);
        }
    }
    // everything the common block still holds
    for (const Unmapped& u : um)
    {
        if (used[static_cast<size_t> (u.node)]) continue;
        const std::string p = on + "/plainParams/" + u.name;
        if (u.name == "kParamLoopCrossfade") continue; // after the sample is known
        if (u.name == "kParamPitchSource")
        {
            // GROUPB WP-05 (A398): the ratio source of the Ratio pitch mode: 0 .. 2 = Osc A .. C, 3 = Noise, 4 = Sub (the enumeration is read from the two
            // factory presets that hold it); an oscillator that names itself plays the note of the voice
            const double v = num (u.node);
            const int src = static_cast<int> (std::lround (v));
            const int mode = static_cast<int> (R.values.plain[static_cast<size_t> (indexOf (key ("pitch_mode")))]);
            if (mode != 2)
                entry (ItemKind::ignored, Reason::exact, area, p, valueText (u.node), "", "", "ratio_source_outside_ratio_mode");
            else if (src == 3)
                entry (ItemKind::dropped, Reason::noEngineEquivalent, area, p, valueText (u.node), key ("ratio_src"), "Base note", "ratio_source_noise_not_modelled");
            else
            {
                const int ours = src == 4 ? 4 : (src >= 0 && src <= 2 && src != i ? src + 1 : 0);
                store (key ("ratio_src"), ours);
                setByFile[static_cast<size_t> (indexOf (key ("ratio_src")))] = 1;
                static const char* const names[5] = { "Base note", "Osc 1", "Osc 2", "Osc 3", "Sub" };
                entry (src == i ? ItemKind::approximated : ItemKind::imported, src == i ? Reason::enumRemapped : Reason::exact, area, p, valueText (u.node), key ("ratio_src"), names[ours],
                       src == i ? "ratio_source_is_the_oscillator_itself_note_used" : "ratio_source");
            }
            consume (u.node);
            continue;
        }
        if ((kind == 3 || kind == 4) && (u.name == "kParamManualPositionMode" || u.name == "kParamPosition" || (kind == 4 && u.name == "kParamHzOffset")))
        {
            // GROUPC WP-11 / WP-12: the play head of the granular and the spectral oscillator: Manual places it at Position (0 .. 100 % of the region); the spectral Hz offset is the
            // frequency shift
            const char* prefix = kind == 3 ? "gran." : "spec.";
            const std::string target = u.name == "kParamHzOffset" ? "spec.freq_shift" : std::string (prefix) + (u.name == "kParamManualPositionMode" ? "manual" : "position");
            const double v = num (u.node);
            bool cl = false;
            const double st = store (key (target.c_str()), u.name == "kParamManualPositionMode" ? (v >= 0.5 ? 1.0 : 0.0) : v, &cl);
            setByFile[static_cast<size_t> (indexOf (key (target.c_str())))] = 1;
            entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, p, valueText (u.node), key (target.c_str()), fmt (st),
                   u.name == "kParamHzOffset" ? "spectral_hz_offset_is_the_frequency_shift" : (u.name == "kParamManualPositionMode" ? "manual_position_mode" : "manual_position"));
            consume (u.node);
            continue;
        }
        const char* why = "serum_feature_not_modelled";
        if (u.name == "kParamPosition" || u.name == "kParamHzOffset" || u.name == "kParamBaseTempo" || u.name == "kParamScanBPMRate" || u.name == "kParamScanBPMDivide"
            || u.name == "kParamScanRange" || u.name == "kParamManualPositionMode" || u.name == "kParamLoopStartLink" || u.name == "kParamPitchRatioModMode")
            why = "sample_or_pitch_feature_not_modelled";
        entry (ItemKind::dropped, Reason::noEngineEquivalent, area, p, valueText (u.node), "", "", why);
        consume (u.node);
    }

    // the type specific blocks
    static const char* const blocks[5] = { "WTOsc", "MultiSampleOsc", "SampleOsc", "GranularOsc", "SpectralOsc" };
    for (int b = 0; b < 5; ++b)
    {
        const int B = get (O, (blocks[b] + std::to_string (i)).c_str());
        if (B < 0) continue;
        const std::string bp = on + "/" + blocks[b] + std::to_string (i);
        if (b != kind)
        {
            acctRest (B, ItemKind::ignored, Reason::exact, area, bp, "", "", "inactive_oscillator_type_block");
            continue;
        }
        const int bplain = get (B, "plainParams");
        std::vector<Unmapped> bum;
        if (isDefaultBlock (bplain)) noteDefault (bplain);
        else applyScope (kind == 0 ? std::initializer_list<const char*> { "Warp", "WT" } : kind == 1 ? std::initializer_list<const char*> { "Warp", "Multi" }
                                                                                   : kind == 3 ? std::initializer_list<const char*> { "Warp", "Granular" }
                                                                                   : kind == 4 ? std::initializer_list<const char*> { "Warp", "Spectral" } : std::initializer_list<const char*> { "Warp" },
                         bplain, n, area, bp + "/plainParams", bum, true,
                         { "kParamTablePos", "kParamDensity", "kParamDensityMode", "kParamGrainLength", "kParamLengthMode" });
        if (kind == 3)
            granularRates (n, bplain, area, bp + "/plainParams");
        if (kind == 4)
        {
            // the drawn mask of the spectral filter (flex): x = position on the log frequency axis, y = 0 .. 1 with 0.5 unity (A422); a flat 0.5 curve is not stored
            const int fx = get (B, "flex");
            if (isMap (fx) && N (fx).count > 0)
            {
                std::vector<ContentPoint> pts;
                bool flat = true;
                if (pointsFromCurve (fx, pts, true))
                {
                    for (const ContentPoint& pt : pts) if (std::fabs (pt.y - 0.5) > 1e-4) flat = false;
                    if (! flat && opt.spectralOsc)
                    {
                        ContentRemap rm;
                        rm.osc = n;
                        rm.slot = 1;
                        rm.name = "Serum filter";
                        rm.points = reduce (pts, kMaxRemapPoints);
                        R.spectralFilters.push_back (std::move (rm));
                        acctRest (fx, ItemKind::imported, Reason::exact, area, bp + "/flex", "content.spectral_filters", "Serum filter", "spectral_filter_curve");
                    }
                    else
                        acctRest (fx, flat ? ItemKind::imported : ItemKind::dropped, flat ? Reason::exact : Reason::noEngineEquivalent, area, bp + "/flex", "", "",
                                  flat ? "spectral_filter_curve_is_flat" : "spectral_filter_curve_without_the_spectral_oscillator");
                }
                else acctRest (fx, ItemKind::dropped, Reason::noEngineEquivalent, area, bp + "/flex", "", "", "spectral_filter_curve_unreadable");
            }
            else if (fx >= 0) acctRest (fx, ItemKind::ignored, Reason::exact, area, bp + "/flex", "", "", "spectral_filter_curve_is_empty");
        }
        if (kind == 0)
        {
            const int tp = get (bplain, "kParamTablePos");
            const double pos = tp >= 0 ? num (tp) : 1.0;
            bool cl = false;
            const double st = store (key ("frame"), pos / 256.0 * 100.0, &cl);   // measured against Serum 2.1.5 (tools/ref-compare): position / 256 of the table, not (position - 1) / 255
            if (tp >= 0)
            {
                setByFile[static_cast<size_t> (indexOf (key ("frame")))] = 1;
                entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, bp + "/plainParams/kParamTablePos", valueText (tp), key ("frame"), fmt (st),
                       "table_position_1_to_256");
                consume (tp);
            }
        }
        for (const Unmapped& u : bum)
        {
            if (used[static_cast<size_t> (u.node)]) continue;
            const std::string p = bp + "/plainParams/" + u.name;
            const bool warpish = u.name.find ("Warp") != std::string::npos;
            const char* why = kind == 1 ? "multisample_feature_not_modelled" : kind == 3 ? "granular_feature_not_modelled" : kind == 4 ? "spectral_feature_not_modelled" : "serum_feature_not_modelled";
            if (u.name == "kParamXfadeMode" && kind == 0)
            {
                // GROUPB WP-05 (A399): the crossfade mode of the table is the Smooth Interpretation of WT POS (1 = on); 0 / absent = off
                const double v = num (u.node);
                store (key ("smooth_interp"), v >= 0.5 ? 1 : 0);
                setByFile[static_cast<size_t> (indexOf (key ("smooth_interp")))] = 1;
                entry (ItemKind::approximated, Reason::enumRemapped, area, p, valueText (u.node), key ("smooth_interp"), v >= 0.5 ? "On" : "Off", "table_crossfade_mode_read_as_smooth_interpretation");
                consume (u.node);
                continue;
            }
            entry (ItemKind::dropped, Reason::noEngineEquivalent, area, p, valueText (u.node), "", "", warpish ? "warp_parameter_not_modelled" : why);
            consume (u.node);
        }
        // the warp types that came from a spectral / granular menu are not in the list: leave the warp off (reported as an unknown enum value above)
        // content
        if (kind == 0)
        {
            const int emb = get (B, "embeddedWTData"), rel = get (B, "relativePathToWT");
            const int ch = get (B, "numChannels");
            Table t;
            bool ok = false;
            std::string source;
            if (emb >= 0 && N (emb).type == cbor::Type::array)
            {
                ok = tableFromEmbedded (emb, ch >= 0 ? static_cast<int> (num (ch)) : 1, t);
                source = "embedded";
                if (ok) acctRest (emb, ItemKind::imported, Reason::exact, "content", bp + "/embeddedWTData", key ("wavetable"), "Preset table", "embedded_table_stored_in_the_library");
            }
            else if (isText (rel))
            {
                ok = loadTableFile (str (rel), t);
                source = str (rel);
                if (ok) entry (ItemKind::imported, Reason::exact, "content", bp + "/relativePathToWT", source, key ("wavetable"), "Preset table", "table_file_stored_in_the_library");
                else
                {
                    ++R.missingFiles;
                    entry (ItemKind::dropped, Reason::renderFallback, "content", bp + "/relativePathToWT", source, key ("wavetable"), "Saw", "table_file_not_found_saw_used");
                }
                consume (rel);
            }
            const int dn = get (B, "tableDisplayName");
            if (isText (dn)) { if (t.name.empty() && str (dn) != "Custom") t.name = str (dn); entry (ItemKind::ignored, Reason::exact, "content", bp + "/tableDisplayName", str (dn), "", "", "display_name"); consume (dn); }
            if (ok) attachTable (n, t, area, bp, emb >= 0);
            // table bookkeeping that is derived from the data
            for (const char* meta : { "numFrames", "numChannels", "sampleRate", "interpolateAfterLoad", "storedPhasePos" })
            {
                const int m = get (B, meta);
                if (m >= 0 && ! used[static_cast<size_t> (m)])
                {
                    const bool interp = std::strcmp (meta, "interpolateAfterLoad") == 0;
                    entry (interp ? ItemKind::approximated : ItemKind::ignored, interp ? Reason::tierNote : Reason::exact, "content", bp + "/" + meta, valueText (m), "", "",
                           interp ? "table_keyframes_only_no_spectral_interpolation" : "derived_from_the_table_data", leavesUnder (d, m));
                    consume (m);
                }
            }
            // flex: the drawn remap curve of the table position
            const int fx = get (B, "flex");
            if (isMap (fx) && N (fx).count > 0)
            {
                std::vector<ContentPoint> pts;
                bool identity = true;
                if (pointsFromCurve (fx, pts, true))
                {
                    for (const ContentPoint& p : pts) if (std::fabs (p.y - p.x) > 1e-4 || std::fabs (p.power) > 1e-4) identity = false;
                    if (! identity)
                    {
                        ContentRemap rm;
                        rm.osc = n;
                        rm.slot = 1;
                        rm.name = "Serum flex";
                        rm.points = reduce (pts, kMaxRemapPoints);
                        R.remaps.push_back (std::move (rm));
                        store (key ("remap.slot"), 0);
                        acctRest (fx, ItemKind::approximated, Reason::tierNote, area, bp + "/flex", key ("remap.slot"), "Curve 1", "flex_curve_as_remap_curve_1");
                    }
                    else acctRest (fx, ItemKind::imported, Reason::exact, area, bp + "/flex", "", "", "flex_curve_is_the_identity");
                }
                else acctRest (fx, ItemKind::dropped, Reason::noEngineEquivalent, area, bp + "/flex", "", "", "flex_curve_unreadable");
            }
            else if (fx >= 0) acctRest (fx, ItemKind::ignored, Reason::exact, area, bp + "/flex", "", "", "flex_curve_is_empty");
        }
        else if (kind == 1)
        {
            int rootNote = -1;
            applyMultisample (n, B, bp, &rootNote);
        }
        else
        {
            const int rel = get (B, "samplePathRelative");
            if (isText (rel))
            {
                const int bn = get (B, "baseNote");
                const int base = bn >= 0 ? static_cast<int> (num (bn)) : 60;
                int frames = 0, rate = 0;
                std::vector<float> slices;
                const int sm = get (O, "sliceMarkers");
                const bool loaded = loadSample ("Samples", str (rel), "", n, bp + "/samplePathRelative", base, &frames, &rate, nullptr);
                entry (loaded ? ItemKind::imported : ItemKind::dropped, loaded ? Reason::exact : Reason::renderFallback, area, bp + "/samplePathRelative", str (rel), key ("type"),
                       loaded ? "Sample" : "Wavetable", loaded ? (kind == 2 ? "sample_path" : "sample_path_played_by_a_plain_sample_oscillator") : "sample_file_not_found");
                consume (rel);
                if (bn >= 0) { entry (ItemKind::imported, Reason::exact, area, bp + "/baseNote", valueText (bn), key ("sample.root"), fmt (base), "sample_root_note"); consume (bn); }
                (void) sm;
                // REMAINDER item 4 (S12, A458): trimHead / trimTail are the first and the last frame of the part of the file that Serum plays; START / END of the oscillator run inside it
                {
                    const int head = get (B, "trimHead"), tail = get (B, "trimTail"), numFramesNode = get (B, "numFrames");
                    const double total = numFramesNode >= 0 ? num (numFramesNode) : static_cast<double> (frames);
                    if ((head >= 0 || tail >= 0) && total > 0.0 && loaded)
                    {
                        const double lo = head >= 0 ? std::min (1.0, std::max (0.0, num (head) / total)) : 0.0, hi = tail >= 0 ? std::min (1.0, std::max (0.0, num (tail) / total)) : 1.0;
                        if (hi > lo)
                        {
                            const double start = R.values.plain[static_cast<size_t> (indexOf (key ("sample.start")))] * 0.01, end = R.values.plain[static_cast<size_t> (indexOf (key ("sample.end")))] * 0.01;
                            const double newStart = store (key ("sample.start"), 100.0 * (lo + start * (hi - lo))), newEnd = store (key ("sample.end"), 100.0 * (lo + end * (hi - lo)));
                            setByFile[static_cast<size_t> (indexOf (key ("sample.start")))] = 1;
                            setByFile[static_cast<size_t> (indexOf (key ("sample.end")))] = 1;
                            if (head >= 0) { entry (ItemKind::approximated, Reason::tierNote, area, bp + "/trimHead", valueText (head), key ("sample.start"), fmt (newStart), "sample_trim_folded_into_start_and_end"); consume (head); }
                            if (tail >= 0) { entry (ItemKind::approximated, Reason::tierNote, area, bp + "/trimTail", valueText (tail), key ("sample.end"), fmt (newEnd), "sample_trim_folded_into_start_and_end"); consume (tail); }
                        }
                    }
                }
            }
            for (const char* meta : { "numFrames", "numChannels", "sampleRate" })
            {
                const int m = get (B, meta);
                if (m >= 0 && ! used[static_cast<size_t> (m)]) { entry (ItemKind::ignored, Reason::exact, "content", bp + "/" + meta, valueText (m), "", "", "derived_from_the_audio_file"); consume (m); }
            }
        }
        // sample markers (manual slices) and the other leftovers of the block are reported by the sweep
    }
    // slice markers
    {
        const int sm = get (O, "sliceMarkers");
        if (sm >= 0 && N (sm).type == cbor::Type::array && sampleLike)
        {
            for (ImportedSample& s : R.samples)
                if (s.osc == n)
                {
                    s.slices.clear();
                    const uint32_t cnt = std::min<uint32_t> (N (sm).count, kMaxSampleSlices);
                    for (uint32_t k = 0; k < cnt; ++k)
                    {
                        const double v = d.arrayNumber (static_cast<uint32_t> (sm), k);
                        s.slices.push_back (static_cast<float> (clampd (v > 1.0 ? v / std::max (1, s.numFrames) : v, 0.0, 1.0)));
                    }
                    std::sort (s.slices.begin(), s.slices.end());
                }
            acctRest (sm, ItemKind::approximated, Reason::tierNote, area, on + "/sliceMarkers", key ("sample.slice_mode"), "Manual", "slice_markers_assumed_in_fractions_or_frames");
        }
    }
    // loop crossfade needs the sample length
    {
        const int lx = get (pp, "kParamLoopCrossfade");
        if (lx >= 0)
        {
            double seconds = 0.0;
            for (const ImportedSample& s : R.samples)
                if (s.osc == n && s.sampleRate > 0) seconds = static_cast<double>(s.numFrames) / s.sampleRate;
            const double loopFrac = clampd ((R.values.plain[static_cast<size_t> (indexOf (key ("sample.loop_end")))] - R.values.plain[static_cast<size_t> (indexOf (key ("sample.loop_start")))]) / 100.0, 0.0, 1.0);
            if (sampleLike && seconds > 0.0)
            {
                bool cl = false;
                const double st = store (key ("sample.loop_xfade"), num (lx) / 100.0 * loopFrac * seconds * 1000.0, &cl);
                setByFile[static_cast<size_t> (indexOf (key ("sample.loop_xfade")))] = 1;
                entry (ItemKind::approximated, cl ? Reason::clamped : Reason::tierNote, area, on + "/plainParams/kParamLoopCrossfade", valueText (lx), key ("sample.loop_xfade"), fmt (st), "loop_crossfade_percent_of_loop_to_ms");
            }
            else
                entry (ItemKind::dropped, Reason::noEngineEquivalent, area, on + "/plainParams/kParamLoopCrossfade", valueText (lx), "", "", "loop_crossfade_without_sample");
            consume (lx);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// noise and sub
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::noiseOsc()
{
    const int O = get (0, "Oscillator3");
    store ("noise.destination", 3);
    store ("noise.on", 0);
    if (O < 0) return;
    const std::string on = "Oscillator3", area = "noise";
    const int pp = get (O, "plainParams");
    std::vector<Unmapped> um;
    applyScope ({ "Noise" }, pp, 1, area, on + "/plainParams", um, true, { "kParamOctave" });
    {
        const int oc = get (pp, "kParamOctave");
        if (oc >= 0)
        {
            bool cl = false;
            const double st = store ("noise.pitch", num (oc) * 12.0 + (indexOf ("noise.pitch") >= 0 ? R.values.plain[static_cast<size_t> (indexOf ("noise.pitch"))] : 0.0), &cl);
            entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, on + "/plainParams/kParamOctave", valueText (oc), "noise.pitch", fmt (st), "octave_times_12_plus_semitones");
            consume (oc);
        }
    }
    for (const Unmapped& u : um)
    {
        if (used[static_cast<size_t> (u.node)]) continue;
        entry (ItemKind::dropped, Reason::noEngineEquivalent, area, on + "/plainParams/" + u.name, valueText (u.node), "", "", "serum_feature_not_modelled");
        consume (u.node);
    }
    const int B = get (O, "NoiseOsc3");
    if (B >= 0)
    {
        const std::string bp = on + "/NoiseOsc3";
        const int bplain = get (B, "plainParams");
        std::vector<Unmapped> bum;
        if (isDefaultBlock (bplain)) noteDefault (bplain);
        else applyScope ({ "NoiseOsc" }, bplain, 1, area, bp + "/plainParams", bum, true, { "kParamNoiseType", "kParamColor" });
        const int nt = get (bplain, "kParamNoiseType");
        const int rel = get (B, "relativePathToNoiseSample");
        int type = -1;
        std::string how;
        if (isText (nt))
        {
            for (int k = 0; k < kNoiseTypeEnum.count; ++k)
                if (str (nt) == kNoiseTypeEnum.entries[k].name) type = kNoiseTypeEnum.entries[k].value;
            if (type >= 0)
            {
                store ("noise.sample", type);
                entry (ItemKind::imported, Reason::exact, area, bp + "/plainParams/kParamNoiseType", str (nt), "noise.sample", str (nt), "");
            }
            else entry (ItemKind::dropped, Reason::enumRemapped, area, bp + "/plainParams/kParamNoiseType", str (nt), "", "", "enum_value_unknown");
            consume (nt);
        }
        // GROUPA1 (WP-04): the noise source plays the sample of the preset: the data embedded in it, else the file of the Noises folder. A colour
        // (kParamNoiseType) is a generated noise: it stays the generator, and the file name it carries is ignored
        const int emb = get (B, "embeddedNoiseData");
        bool sampleStored = false;
        if (type < 0 && opt.noiseSample && opt.customSamples)
        {
            if (emb >= 0 && N (emb).type == cbor::Type::array && N (emb).count > 0)
                sampleStored = noiseSampleFromEmbedded (emb, B, bp);
            if (! sampleStored && isText (rel))
            {
                sampleStored = noiseSampleFromFile (str (rel), bp + "/relativePathToNoiseSample");
                if (sampleStored) consume (rel);
            }
            else if (sampleStored && isText (rel))
            {
                entry (ItemKind::ignored, Reason::exact, area, bp + "/relativePathToNoiseSample", str (rel), "", "", "noise_file_replaced_by_the_embedded_data");
                consume (rel);
            }
        }
        if (isText (rel) && ! used[static_cast<size_t> (rel)])
        {
            // no sample can be played (a colour, a missing file, an engine without the noise sample): the name of the file picks the nearest generator
            std::string name = str (rel);
            std::string lower;
            for (const char c : name) lower += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
            const struct { const char* word; int value; const char* label; } words[] = {
                { "white", 0, "White Noise" }, { "pink", 1, "Pink Noise" }, { "brown", 2, "Brown Noise" }, { "geiger", 3, "Geiger" }, { "hiss", 4, "Tape Hiss" },
                { "crackle", 5, "Vinyl Clicks" }, { "vinyl", 5, "Vinyl Clicks" }, { "hash", 6, "Digital Hash" }, { "velvet", 7, "Velvet Noise" }, { "atmos", 8, "Atmosphere" } };
            int pick = -1;
            const char* label = "";
            for (const auto& w : words)
                if (lower.find (w.word) != std::string::npos) { pick = w.value; label = w.label; break; }
            if (type < 0 && pick >= 0)
            {
                store ("noise.sample", pick);
                entry (ItemKind::approximated, Reason::nearestWavetable, area, bp + "/relativePathToNoiseSample", name, "noise.sample", label, "noise_file_matched_by_name");
            }
            else if (type < 0)
                entry (ItemKind::dropped, Reason::noEngineEquivalent, area, bp + "/relativePathToNoiseSample", name, "noise.sample", "White Noise", "noise_sample_file_not_representable");
            else
                entry (ItemKind::ignored, Reason::exact, area, bp + "/relativePathToNoiseSample", name, "", "", "noise_type_overrides_the_file");
            consume (rel);
        }
        const int col = get (bplain, "kParamColor");
        if (col >= 0)
        {
            if (isNumber (col) && type >= 0 && type <= 3)
            {
                // A348: kParamColor of a colour noise is the FILTER knob, 0 .. 1 with 0.5 in the middle (off), below it a low pass, above a high pass
                bool cl = false;
                const double st = store ("noise.filter", (num (col) - 0.5) * 200.0, &cl);
                entry (ItemKind::approximated, cl ? Reason::clamped : Reason::tierNote, area, bp + "/plainParams/kParamColor", valueText (col), "noise.filter", fmt (st), "noise_color_is_the_filter_knob_assumed");
            }
            else
                entry (ItemKind::dropped, Reason::noEngineEquivalent, area, bp + "/plainParams/kParamColor", valueText (col), "", "", "noise_filter_acts_on_the_colour_samples_only");
            consume (col);
        }
        for (const Unmapped& u : bum)
        {
            if (used[static_cast<size_t> (u.node)]) continue;
            entry (ItemKind::dropped, Reason::noEngineEquivalent, area, bp + "/plainParams/" + u.name, valueText (u.node), "", "", "serum_feature_not_modelled");
            consume (u.node);
        }
        for (const char* meta : { "numFrames", "numChannels", "sampleRate", "detuneFactor", "boundary", "loopback", "sampleFromAudioInput" })
        {
            const int m = get (B, meta);
            if (m >= 0 && ! used[static_cast<size_t> (m)]) { entry (ItemKind::ignored, Reason::exact, "content", bp + "/" + meta, valueText (m), "", "", "derived_from_the_noise_data", leavesUnder (d, m)); consume (m); }
        }
        if (emb >= 0 && ! used[static_cast<size_t> (emb)]) acctRest (emb, ItemKind::dropped, Reason::noEngineEquivalent, "content", bp + "/embeddedNoiseData", "", "", "embedded_noise_sample_not_representable");
    }
    // the noise osc is audible only when switched on; the on-switch of the common block was applied by the rules
}

void Importer::subOsc()
{
    const int O = get (0, "Oscillator4");
    store ("sub.destination", 3);
    store ("sub.on", 0);
    if (O < 0) return;
    const std::string on = "Oscillator4", area = "sub";
    const int pp = get (O, "plainParams");
    std::vector<Unmapped> um;
    applyScope ({ "Sub" }, pp, 1, area, on + "/plainParams", um, true, { "kParamFine" });
    {
        const int fi = get (pp, "kParamFine");
        if (fi >= 0)
        {
            const int tune = indexOf ("sub.tune");
            bool cl = false;
            const double st = store ("sub.tune", R.values.plain[static_cast<size_t> (tune)] + num (fi) / 100.0, &cl);
            entry (ItemKind::approximated, cl ? Reason::clamped : Reason::tierNote, area, on + "/plainParams/kParamFine", valueText (fi), "sub.tune", fmt (st), "fine_cents_added_to_semitone_tune");
            consume (fi);
        }
    }
    for (const Unmapped& u : um)
    {
        if (used[static_cast<size_t> (u.node)]) continue;
        entry (ItemKind::dropped, Reason::noEngineEquivalent, area, on + "/plainParams/" + u.name, valueText (u.node), "", "", "serum_feature_not_modelled");
        consume (u.node);
    }
    const int B = get (O, "SubOsc4");
    if (B >= 0)
    {
        const int bplain = get (B, "plainParams");
        std::vector<Unmapped> bum;
        if (isDefaultBlock (bplain)) noteDefault (bplain);
        else applyScope ({ "SubOsc" }, bplain, 1, area, on + "/SubOsc4/plainParams", bum);
        for (const Unmapped& u : bum)
        {
            if (used[static_cast<size_t> (u.node)]) continue;
            entry (ItemKind::dropped, Reason::noEngineEquivalent, area, on + "/SubOsc4/plainParams/" + u.name, valueText (u.node), "", "", "sub_phase_not_modelled");
            consume (u.node);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// routing: the channel menus and the bus sends
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::routing()
{
    static const char* const prefix[5] = { "osc1", "osc2", "osc3", "noise", "sub" };
    for (int k = 0; k < 7; ++k)
    {
        const std::string name = "RoutingSlot" + std::to_string (k);
        const int S = get (0, name.c_str());
        const bool filterSlot = k >= 5;
        const std::string p = filterSlot ? "filter" + std::to_string (k - 4) : prefix[k];
        const std::string area = filterSlot ? p : p;
        if (S < 0) { if (! filterSlot) store (p + ".destination", k == 0 ? 5 : 3); continue; }
        const int pp = get (S, "plainParams");
        if (isDefaultBlock (pp) || ! isMap (pp))
        {
            if (! filterSlot) { store (p + ".destination", k == 0 ? 5 : 3); store (p + ".filter_blend", 0); }
            noteDefault (pp);
            acctRest (S, ItemKind::ignored, Reason::defaulted, area, name, "", "", "routing_slot_at_default");
            continue;
        }
        const std::string base = name + "/plainParams/";
        std::string dest = k == 0 ? "kRoutingDestFilter" : "kRoutingDestMaster";
        const int rd = get (pp, "kParamRoutingDest");
        if (isText (rd)) dest = str (rd);
        // destination
        if (! filterSlot)
        {
            int v = -1;
            for (int e = 0; e < kRoutingDestEnum.count; ++e)
                if (dest == kRoutingDestEnum.entries[e].name) v = kRoutingDestEnum.entries[e].value;
            if (v < 0) v = k == 0 ? 5 : 3;
            store (p + ".destination", v);
            if (rd >= 0)
            {
                setByFile[static_cast<size_t> (indexOf (p + ".destination"))] = 1;
                entry (ItemKind::imported, Reason::exact, area, base + "kParamRoutingDest", dest, p + ".destination", dest == "kRoutingDestFilter" ? "Filter blend" : dest == "kRoutingDestMaster" ? "Effects only" : dest == "kRoutingDestDirect" ? "Direct out" : "None", "");
                consume (rd);
            }
            const int fb = get (pp, "kParamFilterBalance");
            const double bal = fb >= 0 ? num (fb) : -100.0;
            const double st = store (p + ".filter_blend", (bal + 100.0) / 2.0);
            if (fb >= 0)
            {
                setByFile[static_cast<size_t> (indexOf (p + ".filter_blend"))] = 1;
                entry (ItemKind::imported, Reason::exact, area, base + "kParamFilterBalance", valueText (fb), p + ".filter_blend", fmt (st), "balance_minus100_is_filter_1");
                consume (fb);
            }
            const int ve = get (pp, "kParamViaEnv1");
            if (ve >= 0)
            {
                store (p + ".env1", num (ve) >= 0.5 ? 1 : 0);
                entry (ItemKind::imported, Reason::exact, area, base + "kParamViaEnv1", valueText (ve), p + ".env1", num (ve) >= 0.5 ? "On" : "Off", "");
                consume (ve);
            }
            else store (p + ".env1", 1);
        }
        else
        {
            int v = 0;
            if (dest == "kRoutingDestDirect") v = 1;
            else if (dest == "kRoutingDestNone") v = 2;
            store (p + ".destination", v);
            if (rd >= 0)
            {
                const bool series = dest == "kRoutingDestFilter";
                if (series) store ("filter.routing", k == 5 ? 1 : 2);
                entry (series ? ItemKind::approximated : ItemKind::imported, Reason::exact, area, base + "kParamRoutingDest", dest, series ? "filter.routing" : p + ".destination",
                       series ? (k == 5 ? "Series 1 to 2" : "Series 2 to 1") : (v == 0 ? "Effects only" : v == 1 ? "Direct out" : "None"), series ? "filter_to_filter_as_series_routing" : "");
                consume (rd);
            }
            const int fb = get (pp, "kParamFilterBalance");
            if (fb >= 0) { entry (ItemKind::dropped, Reason::noEngineEquivalent, area, base + "kParamFilterBalance", valueText (fb), "", "", "filter_balance_of_a_filter_channel"); consume (fb); }
            const int ve = get (pp, "kParamViaEnv1");
            if (ve >= 0)
            {
                store (p + ".env1", num (ve) >= 0.5 ? 1 : 0);
                entry (ItemKind::imported, Reason::exact, area, base + "kParamViaEnv1", valueText (ve), p + ".env1", num (ve) >= 0.5 ? "On" : "Off", "");
                consume (ve);
            }
        }
        for (int b = 1; b <= 2; ++b)
        {
            const std::string pn = "kParamFXBus" + std::to_string (b) + "Level";
            const int bl = get (pp, pn.c_str());
            const std::string key = p + ".bus" + std::to_string (b) + "_send";
            if (bl >= 0)
            {
                bool cl = false;
                const double x = std::clamp (num (bl), 0.0, 100.0) / 100.0;   // the send law of Serum is cubic (measured against Serum 2.1.5: 50 % adds 1.02 dB)
                const double st = store (key, 100.0 * x * x * x, &cl);
                setByFile[static_cast<size_t> (indexOf (key))] = 1;
                entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, base + pn, valueText (bl), key, fmt (st), "");
                consume (bl);
            }
            else store (key, 0);
        }
        std::vector<Unmapped> um;
        forMap (pp, [&] (const std::string& nm, int v, int) { if (! used[static_cast<size_t> (v)]) um.push_back ({ nm, v }); });
        dropUnmapped (um, area, name + "/plainParams", "serum_feature_not_modelled");
        acctRest (S, ItemKind::dropped, Reason::noEngineEquivalent, area, name, "", "", "serum_feature_not_modelled");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// filters
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::voiceFilter (int i)
{
    const int n = i + 1;
    const std::string name = "VoiceFilter" + std::to_string (i), area = "filter" + std::to_string (n);
    const int V = get (0, name.c_str());
    int model = 9, style = 0; // MG Low 6, the type of the Init preset (manual p.142)
    {
        Fid fid;
        filterTypeFromName ("MgL6", model, style, fid);
    }
    store (area + ".model", model);
    store (area + ".style", style);
    store (area + ".on", 0);
    if (V < 0) return;
    const int pp = get (V, "plainParams");
    std::vector<Unmapped> um;
    if (isDefaultBlock (pp)) noteDefault (pp);
    applyScope ({ "Filter" }, pp, n, area, name + "/plainParams", um, true, { "kParamType" });   // also for "default": the Serum defaults of the rows apply
    const int tp = get (pp, "kParamType");
    if (isText (tp))
    {
        Fid fid = Fid::exact;
        int m = 0, s = 0;
        if (filterTypeFromName (str (tp), m, s, fid))
        {
            store (area + ".model", m);
            store (area + ".style", s);
            setByFile[static_cast<size_t> (indexOf (area + ".model"))] = 1;
            char text[64];
            valueToText (paramTable()[indexOf (area + ".model")], static_cast<float> (m), text, sizeof text);
            entry (fid == Fid::exact ? ItemKind::imported : ItemKind::approximated, fid == Fid::exact ? Reason::exact : Reason::enumRemapped, area, name + "/plainParams/kParamType", str (tp),
                   area + ".model", text, fid == Fid::exact ? "" : "filter_type_nearest");
        }
        else entry (ItemKind::dropped, Reason::enumRemapped, area, name + "/plainParams/kParamType", str (tp), area + ".model", "", "filter_type_unknown");
        consume (tp);
    }
    else
    {
        // no type in the file = the Serum default "MG Low 12" (Serum 2.1.5, the type of a filter that was only switched on)
        Fid fid = Fid::exact;
        int m = 0, s = 0;
        if (filterTypeFromName ("MgL12", m, s, fid))
        {
            store (area + ".model", m);
            store (area + ".style", s);
        }
    }
    {
        // the Serum drive stage is also louder (measured against Serum 2.1.5): the makeup goes into the level of the filter
        const int dn = get (pp, "kParamDrive");
        const int li = indexOf (area + ".level");
        if (dn >= 0 && isNumber (dn) && li >= 0)
        {
            const double mk = filterDriveMakeupDb (num (dn));
            const double cur = static_cast<double> (R.values.plain[static_cast<size_t> (li)]);
            if (mk > 0.0 && cur > -59.99) store (area + ".level", cur + mk);   // a muted filter (level 0) stays muted
        }
    }
    {
        // the resonance of the Serum filter types takes or adds level (measured against Serum 2.1.5, per type, cutoff and resonance): the difference goes into the level of the filter
        const int fn = get (pp, "kParamFreq"), rn = get (pp, "kParamReso");
        const int li = indexOf (area + ".level");
        const std::string typeName = isText (tp) ? str (tp) : std::string ("MgL12");
        if (li >= 0)
        {
            const int dn2 = get (pp, "kParamDrive");
            const double resoV = rn >= 0 && isNumber (rn) ? num (rn) : 0.0;
            const double mk = filterResoLevelDb (typeName, fn >= 0 && isNumber (fn) ? num (fn) : 0.5, resoV)
                              + filterDriveLevelDb (typeName, dn2 >= 0 && isNumber (dn2) ? num (dn2) : 0.0, resoV);
            const double cur = static_cast<double> (R.values.plain[static_cast<size_t> (li)]);
            if (mk != 0.0 && cur > -59.99) store (area + ".level", cur + mk);
        }
    }
    for (const Unmapped& u : um)
    {
        if (used[static_cast<size_t> (u.node)]) continue;
        if (u.name == "kParamEnable") continue;
        entry (ItemKind::dropped, Reason::noEngineEquivalent, area, name + "/plainParams/" + u.name, valueText (u.node), "", "", "serum_feature_not_modelled");
        consume (u.node);
    }
    const int pz = get (V, "PZs");
    if (pz >= 0) pzFilter (pz, pp, area, isText (tp) && str (tp) == "PZ_SVF", area, name + "/PZs");
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// PZ SVF: poles and zeros -> the response curve (REMAINDER item 4, S4; 13-new-keys.md 4.43, A452 .. A455)
// ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct PzSet
{
    double fp[8] = {}, fz[8] = {}, qp[8] = {}, qz[8] = {};
    int type[8] = {};
    double gain = 0.5;
};

constexpr double kPzLow = 20.0, kPzRatio = 1000.0;   // the frequency axis of the curve: 20 Hz .. 20 kHz, logarithmic

double pzFrequency (double x) { return kPzLow * std::pow (kPzRatio, std::min (1.0, std::max (0.0, x))); }
double pzQuality (double q) { return 0.5 * std::pow (100.0, std::min (1.0, std::max (0.0, q))); }   // 0 .. 1 -> Q 0.5 .. 50

/** The magnitude (dB) of one set at `f` Hz. The pair i is a second order section from the pole (fp, qp) and the zero (fz, qz); the type picks the shape: 0 high pass, 1 low pass, 2 off,
    3 all pass (flat magnitude), 4 band pass, 5 notch (zero on fz, DC gain 1), 6 the general pole / zero pair (zero and pole as given, so fp = fz with qp > qz is a peak of qp / qz). */
double pzSetDb (const PzSet& set, double f)
{
    using C = std::complex<double>;
    const C s (0.0, f);
    double db = (set.gain - 0.5) * 24.0;   // the set gain 0 .. 1: -12 .. +12 dB
    for (int i = 0; i < 8; ++i)
    {
        const double wp = pzFrequency (set.fp[i]), wz = pzFrequency (set.fz[i]), qp = pzQuality (set.qp[i]), qz = pzQuality (set.qz[i]);
        const C pole = s * s + s * (wp / qp) + wp * wp;
        C h (1.0, 0.0);
        switch (set.type[i])
        {
            case 0: h = s * s / pole; break;
            case 1: h = C (wp * wp, 0.0) / pole; break;
            case 3: h = std::conj (pole) / pole; break;
            case 4: h = s * (wp / qp) / pole; break;
            case 5: h = (s * s + wz * wz) / pole * (wp * wp / (wz * wz)); break;
            case 6: h = (s * s + s * (wz / qz) + wz * wz) / pole; break;
            default: break;
        }
        db += 20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
    }
    return db;
}

/** Ramer - Douglas - Peucker on (x, dB): the fewest points (at most `maxPoints`) that follow the curve. */
void pzSimplify (const std::vector<ContentFilterCurvePoint>& in, size_t first, size_t last, double tolerance, std::vector<char>& keep)
{
    if (last <= first + 1) return;
    double worst = 0.0;
    size_t at = first;
    for (size_t i = first + 1; i < last; ++i)
    {
        const double t = (in[i].x - in[first].x) / std::max (1.0e-9, static_cast<double> (in[last].x - in[first].x));
        const double line = in[first].y + t * (in[last].y - in[first].y);
        const double error = std::fabs (in[i].y - line);
        if (error > worst) { worst = error; at = i; }
    }
    if (worst > tolerance)
    {
        keep[at] = 1;
        pzSimplify (in, first, at, tolerance, keep);
        pzSimplify (in, at, last, tolerance, keep);
    }
}

std::vector<ContentFilterCurvePoint> pzReduce (const std::vector<ContentFilterCurvePoint>& in, size_t maxPoints)
{
    std::vector<ContentFilterCurvePoint> out;
    for (double tolerance = 0.25; tolerance < 200.0; tolerance *= 1.4)
    {
        std::vector<char> keep (in.size(), 0);
        keep.front() = keep.back() = 1;
        pzSimplify (in, 0, in.size() - 1, tolerance, keep);
        out.clear();
        for (size_t i = 0; i < in.size(); ++i)
            if (keep[i]) out.push_back (in[i]);
        if (out.size() <= maxPoints) break;
    }
    return out;
}
} // namespace

void Importer::pzFilter (int pzNode, int params, const std::string& filter, bool active, const std::string& area, const std::string& path)
{
    if (! active)
    {
        acctRest (pzNode, ItemKind::ignored, Reason::exact, area, path, "", "", "pz_set_of_a_filter_that_is_not_a_pz_svf");
        return;
    }
    // the sets: the array of four (the corners of the X | Y control) or the one set of a Single file
    std::vector<PzSet> sets;
    const auto readSet = [&] (int node, PzSet& out) -> bool
    {
        if (! isMap (node)) return false;
        const int fp = get (node, "fp"), fz = get (node, "fz"), qp = get (node, "qp"), qz = get (node, "qz"), ty = get (node, "types");
        if (fp < 0 || fz < 0 || qp < 0 || qz < 0 || ty < 0) return false;
        const auto fill = [&] (int arr, double* dest) -> bool
        {
            if (N (arr).type != cbor::Type::array || N (arr).count < 8) return false;
            for (uint32_t i = 0; i < 8; ++i) dest[i] = d.arrayNumber (static_cast<uint32_t> (arr), i);
            return true;
        };
        double types[8] = {};
        if (! fill (fp, out.fp) || ! fill (fz, out.fz) || ! fill (qp, out.qp) || ! fill (qz, out.qz) || ! fill (ty, types)) return false;
        for (int i = 0; i < 8; ++i) out.type[i] = static_cast<int> (std::lround (types[i]));
        out.gain = num (get (node, "gain"), 0.5);
        return true;
    };
    if (N (pzNode).type == cbor::Type::array)
    {
        for (uint32_t q = 0; q < N (pzNode).count && q < 4; ++q)
        {
            PzSet set;
            if (readSet (static_cast<int> (d.kids[N (pzNode).first + q]), set)) sets.push_back (set);
        }
    }
    else
    {
        PzSet set;
        if (readSet (pzNode, set)) sets.push_back (set);
    }
    if (sets.empty())
    {
        acctRest (pzNode, ItemKind::dropped, Reason::noEngineEquivalent, area, path, "", "", "pz_set_unreadable");
        return;
    }
    // blend: four sets are the corners of the X | Y square in the order (0,0) (1,0) (0,1) (1,1); a file without X and Y (a Multi file) takes the middle
    const double x = std::min (1.0, std::max (0.0, num (get (params, "kParamX"), 0.5))), y = std::min (1.0, std::max (0.0, num (get (params, "kParamY"), 0.5)));
    std::vector<double> weight (sets.size(), 1.0 / static_cast<double>(sets.size()));
    if (sets.size() == 4)
        weight = { (1.0 - x) * (1.0 - y), x * (1.0 - y), (1.0 - x) * y, x * y };
    std::vector<ContentFilterCurvePoint> curve;
    constexpr int kSamples = 129;
    for (int k = 0; k < kSamples; ++k)
    {
        const double at = static_cast<double> (k) / (kSamples - 1);
        double db = 0.0;
        for (size_t s = 0; s < sets.size(); ++s)
            db += weight[s] * pzSetDb (sets[s], pzFrequency (at));
        ContentFilterCurvePoint p;
        p.x = static_cast<float> (at);
        p.y = static_cast<float> (std::min (60.0, std::max (-60.0, db)));
        curve.push_back (p);
    }
    ContentFilterCurve result;
    result.filter = filter;
    result.points = pzReduce (curve, static_cast<size_t> (kMaxFilterCurvePoints));
    if (filter.empty()) R.pzCurve = result;
    else
    {
        R.filterCurves.erase (std::remove_if (R.filterCurves.begin(), R.filterCurves.end(), [&] (const ContentFilterCurve& c) { return c.filter == filter; }), R.filterCurves.end());
        R.filterCurves.push_back (result);
    }
    acctRest (pzNode, ItemKind::approximated, Reason::tierNote, area, path, filter.empty() ? "" : filter + ".model", filter.empty() ? "" : "response curve",
              sets.size() == 4 ? "pz_svf_four_sets_blended_as_a_response_curve" : "pz_svf_poles_and_zeros_as_a_response_curve");
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// envelopes
// ---------------------------------------------------------------------------------------------------------------------------------------
void Importer::envelope (int i)
{
    const int n = i + 1;
    const std::string name = "Env" + std::to_string (i), area = "env" + std::to_string (n);
    const int E = get (0, name.c_str());
    if (E < 0) return;
    const int pp = get (E, "plainParams");
    std::vector<Unmapped> um;
    if (isDefaultBlock (pp)) noteDefault (pp);
    applyScope ({ "Env" }, pp, n, area, name + "/plainParams", um);   // also for "default": the Serum defaults of the rows apply
    if (n > 1)
    {
        // envelopes 2 .. 4 modulate: their curve knobs map through the table measured on a swept filter, not the one of the amplitude envelope (the rows hold that one)
        const char* names[3] = { "kParamCurve1", "kParamCurve2", "kParamCurve3" };
        const char* keys[3] = { "env#.attack_curve", "env#.decay_curve", "env#.release_curve" };
        const double defaults[3] = { 50.0, 66.6, 66.6 };
        for (int c = 0; c < 3; ++c)
        {
            const int node = get (pp, names[c]);
            const double v = node >= 0 && isNumber (node) ? num (node) : defaults[c];
            store (keyOf (keys[c], n), envCurveOurs (c == 0, true, v));
        }
    }
    {
        // Serum 2.1.5: the attack of envelope 1 is 1 ms, of envelopes 2 .. 4 0.5 ms (the row default is the first)
        const int a = indexOf (keyOf ("env#.attack", n));
        if (n > 1 && a >= 0 && ! setByFile[static_cast<size_t> (a)]) store (keyOf ("env#.attack", n), 0.5);
    }
    for (const Unmapped& u : um)
    {
        if (used[static_cast<size_t> (u.node)]) continue;
        const std::string p = name + "/plainParams/" + u.name;
        if (u.name == "kParamBeatSync")
        {
            entry (ItemKind::approximated, Reason::tierNote, area, p, valueText (u.node), "", "", "env_beat_sync_kept_as_seconds");
            consume (u.node);
            continue;
        }
        const char* why = u.name == "kParamStart" || u.name == "kParamEnd" ? "env_start_end_levels_not_modelled" : u.name == "kParamVoiceStealRestart" ? "voice_steal_restart_not_modelled" : "serum_feature_not_modelled";
        entry (ItemKind::dropped, Reason::noEngineEquivalent, area, p, valueText (u.node), "", "", why);
        consume (u.node);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// LFOs
// ---------------------------------------------------------------------------------------------------------------------------------------
double Importer::powerOfCurve (double c)
{
    return clampd ((c - 0.5) * 11.0, -kMaxPointPower, kMaxPointPower); // A-number: the Serum bend 0 .. 1 (0.5 straight) to our power; a quarter sine is about -2.2 at 0.3
}

std::vector<ContentPoint> Importer::reduce (std::vector<ContentPoint> p, size_t maxPoints)
{
    if (p.size() <= maxPoints || maxPoints < 2) return p;
    std::vector<ContentPoint> out;
    out.reserve (maxPoints);
    for (size_t k = 0; k < maxPoints; ++k)
    {
        const double pos = static_cast<double> (k) * static_cast<double> (p.size() - 1) / static_cast<double> (maxPoints - 1);
        out.push_back (p[static_cast<size_t> (std::llround (pos))]);
    }
    return out;
}

bool Importer::pointsFromCurve (int curve, std::vector<ContentPoint>& out, bool, int* loopback)
{
    out.clear();
    if (! isMap (curve)) return false;
    const int xv = get (curve, "xVals"), yv = get (curve, "yVals"), cv = get (curve, "curveVals");
    if (xv < 0 || yv < 0 || N (xv).type != cbor::Type::array || N (yv).type != cbor::Type::array) return false;
    const uint32_t cnt = std::min (N (xv).count, N (yv).count);
    if (cnt < 2) return false;
    const uint32_t cc = cv >= 0 && N (cv).type == cbor::Type::array ? N (cv).count : 0;
    for (uint32_t k = 0; k < cnt; ++k)
    {
        ContentPoint p;
        p.x = static_cast<float> (clampd (d.arrayNumber (static_cast<uint32_t> (xv), k), 0.0, 1.0));
        p.y = static_cast<float> (clampd (1.0 - d.arrayNumber (static_cast<uint32_t> (yv), k), 0.0, 1.0)); // the file stores the screen y: up is 0
        p.power = k < cc ? static_cast<float> (powerOfCurve (d.arrayNumber (static_cast<uint32_t> (cv), k))) : 0.0f;
        if (! out.empty() && p.x < out.back().x) p.x = out.back().x;
        out.push_back (p);
    }
    if (out.front().x > 1e-6f) { ContentPoint p = out.front(); p.x = 0.0f; out.insert (out.begin(), p); }
    if (out.back().x < 1.0f - 1e-6f) { ContentPoint p = out.back(); p.x = 1.0f; p.power = 0.0f; out.push_back (p); }
    if (loopback)
    {
        const int lb = get (curve, "loopbackPointNum");
        *loopback = lb >= 0 ? static_cast<int> (num (lb)) : -1;
    }
    return out.size() >= 2;
}

void Importer::lfoShapeOf (int i, int L)
{
    const int n = i + 1;
    const std::string name = "LFO" + std::to_string (i), area = "lfo" + std::to_string (n);
    const int cd = get (L, "curveData");
    const int cn = get (L, "curveDisplayName");
    std::string shapeName;
    if (isText (cn)) { shapeName = vitalimport::cleanText (str (cn), 64, true); entry (ItemKind::ignored, Reason::exact, area, name + "/curveDisplayName", shapeName, "", "", "display_name"); consume (cn); }
    std::vector<ContentPoint> pts;
    int loop = -1;
    if (isMap (cd) && N (cd).count > 0 && pointsFromCurve (cd, pts, true, &loop))
    {
        ContentLfoShape shape;
        shape.lfo = n;
        shape.name = shapeName.empty() || shapeName == "Custom" ? "Serum LFO " + std::to_string (n) : shapeName;
        const bool tooMany = pts.size() > static_cast<size_t> (kMaxLfoShapePoints);
        lfoOrigPoints[i] = tooMany ? 0 : static_cast<int> (std::min<size_t> (pts.size(), N (get (cd, "xVals")).count));
        shape.points = reduce (pts, kMaxLfoShapePoints);
        shape.loopPoint = loop > 0 && loop < static_cast<int> (shape.points.size()) && ! tooMany ? loop : -1;
        const bool match = false;
        (void) match;
        store ("lfo" + std::to_string (n) + ".shape", kLfoShapeCustom);
        R.lfoShapes.push_back (std::move (shape));
        acctRest (cd, tooMany ? ItemKind::approximated : ItemKind::imported, tooMany ? Reason::truncated : Reason::exact, area, name + "/curveData", "lfo" + std::to_string (n) + ".shape", "Custom",
                  tooMany ? "lfo_shape_resampled_to_128_points" : "lfo_shape_points_and_curves");
    }
    else
    {
        store ("lfo" + std::to_string (n) + ".shape", 1); // Triangle: the shape of the Init preset
        if (cd >= 0) acctRest (cd, ItemKind::ignored, Reason::defaulted, area, name + "/curveData", "lfo" + std::to_string (n) + ".shape", "Triangle", "default_lfo_shape");
        ++R.report.numDefaulted;
    }
}

void Importer::lfo (int i)
{
    const int n = i + 1;
    const std::string name = "LFO" + std::to_string (i), area = "lfo" + std::to_string (n);
    const int L = get (0, name.c_str());
    if (L < 0) return;
    const int pp = get (L, "plainParams");
    const std::string base = name + "/plainParams/";
    const bool plainMap = isMap (pp);
    const auto field = [&] (const char* nm) -> int { return plainMap ? get (pp, nm) : -1; };
    std::string type = "None", mode;
    if (isText (field ("kParamType"))) type = str (field ("kParamType"));
    if (isText (field ("kParamMode"))) mode = str (field ("kParamMode"));
    const bool random = type == "RandomSH" || type == "Lorenz" || type == "Rossler";
    std::string prefix = "lfo" + std::to_string (n);
    int rj = 0;
    if (random)
    {
        if (usedRandomLfos < 4) { rj = ++usedRandomLfos; randomLfoOf[i] = rj; prefix = "rlfo" + std::to_string (rj); }
        else
        {
            entry (ItemKind::dropped, Reason::limitExceeded, area, name + "/plainParams/kParamType", type, "", "", "more_than_four_random_lfos");
            if (plainMap) consume (field ("kParamType"));
            randomLfoOf[i] = -1;
        }
    }
    const bool tempo = [&] { const int bs = field ("kParamBeatSync"); return bs < 0 || num (bs) >= 0.5; }();
    const bool dotted = field ("kParamDotted") >= 0 && num (field ("kParamDotted")) >= 0.5;
    const bool triplet = field ("kParamTriplets") >= 0 && num (field ("kParamTriplets")) >= 0.5;
    const bool ten = field ("kParamRate10x") >= 0 && num (field ("kParamRate10x")) >= 0.5;
    const int rateNode = field ("kParamRate");
    const bool haveRate = rateNode >= 0;
    const double rate = haveRate ? num (rateNode) : 0.0;
    auto setKey = [&] (const std::string& k, double v, bool* cl = nullptr) { return store (prefix + "." + k, v, cl); };

    if (type == "Path")
    {
        entry (ItemKind::dropped, Reason::noEngineEquivalent, area, name + "/plainParams/kParamType", type, "", "", "path_lfo_not_modelled");
        if (field ("kParamType") >= 0) consume (field ("kParamType"));
        randomLfoOf[i] = -1;
    }
    if (! random || rj > 0)
    {
        if (random)
        {
            const int style = type == "RandomSH" ? 1 : type == "Lorenz" ? 3 : 4;
            setKey ("style", style);
            entry (ItemKind::approximated, Reason::enumRemapped, area, base + "kParamType", type, prefix + ".style", type == "RandomSH" ? "Sample and hold" : type == "Lorenz" ? "Lorenz attractor" : "Rossler attractor",
                   "random_lfo_slot_allocated");
            consume (field ("kParamType"));
        }
        else if (field ("kParamType") >= 0 && type != "Path")
        {
            entry (ItemKind::imported, Reason::exact, area, base + "kParamType", type, "", "", "shape_lfo");
            consume (field ("kParamType"));
        }
        // rate
        if (! tempo)
        {
            bool cl = false;
            if (haveRate)
            {
                const double st = setKey ("rate", rate * (ten ? 10.0 : 1.0), &cl);
                entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, base + "kParamRate", valueText (rateNode), prefix + ".rate", fmt (st), ten ? "rate_hz_times_ten" : "");
                consume (rateNode);
            }
            setKey ("sync", 0);
        }
        else
        {
            // tempo mode: the plain value is read as Hz at 120 BPM (A-number), the nearest division of the sync list keeps the period
            static const double beats[13] = { 0, 128, 64, 32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 0.0625 };
            const int syncMode = (dotted && ! triplet) ? 2 : (triplet && ! dotted) ? 3 : 1;
            const double factor = syncMode == 2 ? 1.5 : syncMode == 3 ? 2.0 / 3.0 : 1.0;
            int best = 8;
            if (haveRate)
            {
                if (rate <= 1e-4) best = 0;
                else
                {
                    const double period = 2.0 / rate;
                    double bestErr = 1e9;
                    for (int dvn = 1; dvn <= 12; ++dvn)
                    {
                        const double err = std::fabs (std::log (period / (beats[dvn] * factor)));
                        if (err < bestErr) { bestErr = err; best = dvn; }
                    }
                }
            }
            setKey ("sync", syncMode);
            setKey ("division", best);
            if (haveRate)
            {
                entry (ItemKind::approximated, Reason::rounded, area, base + "kParamRate", valueText (rateNode), prefix + ".division", std::to_string (best), "tempo_rate_read_as_hz_at_120_bpm");
                consume (rateNode);
            }
            else ++R.report.numDefaulted;
        }
        for (const char* flag : { "kParamBeatSync", "kParamDotted", "kParamTriplets", "kParamRate10x" })
        {
            const int f = field (flag);
            if (f >= 0 && ! used[static_cast<size_t> (f)]) { entry (ItemKind::imported, Reason::exact, area, base + flag, valueText (f), prefix + ".sync", "", "rate_mode_flag"); consume (f); }
        }
        // phase, rise, delay, smooth, mono, direction, host
        const auto numeric = [&] (const char* nm, const char* key, double scale, double def, const char* detail)
        {
            const int f = field (nm);
            if (f < 0) { if (std::isfinite (def)) setKey (key, def); return; }
            bool cl = false;
            const double st = setKey (key, num (f) * scale, &cl);
            entry (cl ? ItemKind::approximated : ItemKind::imported, cl ? Reason::clamped : Reason::exact, area, base + nm, valueText (f), prefix + "." + key, fmt (st), detail);
            consume (f);
        };
        if (! random)
        {
            numeric ("kParamPhase", "phase", 1.0, std::nan (""), "");
            numeric ("kParamRise", "fade_in", 1000.0, std::nan (""), "");
            numeric ("kParamDelay", "delay", 1000.0, std::nan (""), "");
            numeric ("kParamMono", "mono", 1.0, std::nan (""), "");
            const int dr = field ("kParamDirection");
            if (dr >= 0) { setKey ("direction", std::min (2.0, std::max (0.0, num (dr)))); entry (ItemKind::approximated, Reason::enumRemapped, area, base + "kParamDirection", valueText (dr), prefix + ".direction", "", "direction_index_assumed"); consume (dr); }
            const int sm = field ("kParamSmooth");
            if (sm >= 0)
            {
                setKey ("smooth", 1);
                const double ms = std::max (1.0, 1000.0 * std::pow (num (sm) / 100.0, 2.0));
                const double st = setKey ("smooth_time", ms);
                entry (ItemKind::approximated, Reason::tierNote, area, base + "kParamSmooth", valueText (sm), prefix + ".smooth_time", fmt (st), "lfo_smooth_percent_to_time_assumed");
                consume (sm);
            }
            else setKey ("smooth", 0);
            const int an = field ("kParamAnchored");
            setKey ("host", an >= 0 ? (num (an) >= 0.5 ? 1 : 0) : 1);
            if (an >= 0) { entry (ItemKind::imported, Reason::exact, area, base + "kParamAnchored", valueText (an), prefix + ".host", num (an) >= 0.5 ? "On" : "Off", "anchored_is_host_sync"); consume (an); }
            // mode
            int sync = 0;
            if (mode == "Free") sync = 1;
            else if (mode == "Envelope") sync = 2;
            if (isText (field ("kParamMode")))
            {
                entry (ItemKind::imported, Reason::exact, area, base + "kParamMode", mode, prefix + ".sync_type", sync == 1 ? "Free-run" : sync == 2 ? "Envelope" : "Retrigger", "");
                consume (field ("kParamMode"));
            }
            setKey ("sync_type", sync);
        }
        else
        {
            const int phaseIdx = 0;
            (void) phaseIdx;
            int sync = mode == "Free" ? 1 : 0;
            setKey ("sync_type", sync);
            if (isText (field ("kParamMode")))
            {
                entry (mode == "Envelope" ? ItemKind::approximated : ItemKind::imported, mode == "Envelope" ? Reason::enumRemapped : Reason::exact, area, base + "kParamMode", mode, prefix + ".sync_type",
                       sync == 1 ? "Free-run" : "Retrigger", mode == "Envelope" ? "random_lfo_has_no_envelope_mode" : "");
                consume (field ("kParamMode"));
            }
        }
        if (! random) lfoShapeOf (i, L);
        else
        {
            // a random LFO has no drawn shape
            const int cd = get (L, "curveData"), cn = get (L, "curveDisplayName");
            if (cd >= 0) acctRest (cd, ItemKind::ignored, Reason::exact, area, name + "/curveData", "", "", "random_lfo_has_no_shape");
            if (cn >= 0) acctRest (cn, ItemKind::ignored, Reason::exact, area, name + "/curveDisplayName", "", "", "display_name");
        }
    }
    else
    {
        // a random LFO beyond the fourth: its rate and the shape stay unreported until the sweep
    }
    const int pd = get (L, "pathData");
    if (pd >= 0) acctRest (pd, ItemKind::dropped, Reason::noEngineEquivalent, area, name + "/pathData", "", "", "path_lfo_not_modelled");
    if (plainMap)
        forMap (pp, [&] (const std::string& nm, int v, int)
        {
            if (used[static_cast<size_t> (v)]) return;
            const char* ui[] = { "kParamGridX", "kParamGridY", "kParamDefaultMode", "kParamPhaseSnap", "kParamTimingScale", "kParamTimingScaleSnap", "kParamLoopbackCompat" };
            bool isUi = false;
            for (const char* u : ui) if (nm == u) isUi = true;
            if (isUi) entry (ItemKind::ignored, Reason::exact, area, base + nm, valueText (v), "", "", "interface_state");
            else if (nm == "kParamSwing" && ! random && isNumber (v))
            {
                // REMAINDER item 4 (S13, A456): the flag says that the rate takes the global SWING (50 = straight .. 75 = full); ours is the swing of the LFO, 0 .. 100 %
                const double global = num (get (get (get (0, "Global0"), "plainParams"), "kParamSwing"), 50.0);
                if (num (v) >= 0.5)
                {
                    const std::string key = prefix + ".swing";
                    bool cl = false;
                    const double st = store (key, std::min (100.0, std::max (0.0, (global - 50.0) * 4.0)), &cl);
                    setByFile[static_cast<size_t> (indexOf (key))] = 1;
                    entry (ItemKind::approximated, Reason::tierNote, area, base + nm, valueText (v), key, fmt (st), "lfo_swing_flag_takes_the_global_swing");
                }
                else entry (ItemKind::ignored, Reason::exact, area, base + nm, valueText (v), "", "", "lfo_swing_flag_off");
            }
            else if (nm == "kParamSwing") entry (ItemKind::dropped, Reason::noEngineEquivalent, area, base + nm, valueText (v), "", "", "random_lfo_parameter_without_key");
            else if (nm == "kParamSmooth" || nm == "kParamPhase" || nm == "kParamRise" || nm == "kParamDelay" || nm == "kParamMono" || nm == "kParamAnchored" || nm == "kParamDirection")
                entry (ItemKind::dropped, Reason::limitExceeded, area, base + nm, valueText (v), "", "", "random_lfo_parameter_without_key");
            else entry (ItemKind::dropped, Reason::noEngineEquivalent, area, base + nm, valueText (v), "", "", "serum_feature_not_modelled");
            consume (v);
        });
    else if (pp >= 0) noteDefault (pp);
}

} // namespace serumimport
} // namespace synthyboy
