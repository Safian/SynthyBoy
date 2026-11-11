// SynthyBoy - tests of the Serum 2 importer: hand-built fixtures for every mapping class, the accounting identity, content, effects, robustness; a gated run
// over the real presets (tests/serum-presets, never committed) when the folder exists.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "serum_fixtures.h"
#include "synthyboy/multisample/multisample.h"
#include "serum_map.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/serum_plugin_import.h"
#include "test_runner.h"

// the wall time check is meaningless under a sanitizer (ThreadSanitizer: 15 to 30 times slower; found by the WP-20 TSan run)
#if defined (__SANITIZE_THREAD__) || defined (__SANITIZE_ADDRESS__)
 #define SBTEST_INSTRUMENTED 1
#elif defined (__has_feature)
 #if __has_feature (thread_sanitizer) || __has_feature (address_sanitizer)
  #define SBTEST_INSTRUMENTED 1
 #endif
#endif
#ifndef SBTEST_INSTRUMENTED
 #define SBTEST_INSTRUMENTED 0
#endif

using namespace synthyboy;
using namespace sbserum;

namespace
{
std::vector<double> ramp (size_t n, double scale = 1.0)
{
    std::vector<double> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = scale * (-1.0 + 2.0 * static_cast<double> (i % 2048) / 2048.0);
    return v;
}

Map withText (std::vector<std::pair<std::string, double>> numbers, const std::vector<std::pair<std::string, std::string>>& texts)
{
    Map m;
    for (const auto& kv : numbers) m.push_back ({ kv.first, num (kv.second) });
    for (const auto& kv : texts) m.push_back ({ kv.first, text (kv.second) });
    return m;
}

Bytes curveMap (const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& c)
{
    return map ({ { "curveVals", nums (c) }, { "numPoints", num (static_cast<double> (x.size()) - 1) }, { "xVals", nums (x) }, { "yVals", nums (y) } });
}

float norm (const SerumImportResult& r, const char* key) { return value (r, key); }

/** Number of the first report entry with this detail (-1 if none). */
int entryWith (const SerumImportResult& r, const std::string& detail)
{
    for (size_t i = 0; i < r.report.entries.size(); ++i)
        if (r.report.entries[i].detail == detail) return static_cast<int> (i);
    return -1;
}

int countKind (const SerumImportResult& r, ItemKind k, const std::string& pathPrefix)
{
    int n = 0;
    for (const auto& e : r.report.entries)
        if (e.kind == k && e.serumPath.rfind (pathPrefix, 0) == 0) n += e.count;
    return n;
}
} // namespace

TEST (serum_oscillator_values_and_the_accounting_identity)
{
    Preset p;
    Map wt = withText ({ { "kParamTablePos", 129 }, { "kParamWarp", 0.4 }, { "kParamInitialPhase", 90 }, { "kParamRandomPhase", 50 } }, { { "kParamWarpMenu", "kSync" } });
    Map osc = withText ({ { "kParamVolume", 0.5 }, { "kParamPan", 25 }, { "kParamOctave", -1 }, { "kParamPitch", 3 }, { "kParamFine", 12 }, { "kParamUnison", 5 },
                          { "kParamDetune", 0.25 }, { "kParamDetuneWid", 60 }, { "kParamUnisonStereo", 80 }, { "kParamPitchTrack", 0 } },
                        { { "kParamUnisonStack", "kOctave1" }, { "kParamDetuneMode", "kDetuneSuper" } });
    p.set ("Oscillator0", map ({ { "plainParams", map (osc) },
                                 { "WTOsc0", map ({ { "embeddedWTData", nums (ramp (4096)) }, { "numFrames", num (4096) }, { "numChannels", num (1) }, { "plainParams", map (wt) } }) } }));
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok && r.kind == SerumFileKind::preset);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK (r.report.leaves > 20);
    CHECK_EQ (norm (r, "osc1.on"), 1.0f);
    CHECK_NEAR (norm (r, "osc1.level"), 50.0, 1e-2);   // the unison level is the engine's Serum law (oscN.unison.law), not a correction of the level
    CHECK_EQ (norm (r, "osc1.unison.law"), 1.0f);
    CHECK_EQ (norm (r, "osc1.unison.detune_curve"), 0.0f);
    CHECK_NEAR (norm (r, "osc1.pan"), 50.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.transpose"), -9.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.tune"), 12.0, 1e-3);
    CHECK_EQ (norm (r, "osc1.unison.voices"), 5.0f);
    CHECK_NEAR (norm (r, "osc1.unison.detune"), 100.0 * std::pow (0.25, 1.25), 1e-3);   // the detune law of Serum 2.1.5: 200 * d ^ 1.25 cents at the outer voices (file value 0.25 here)
    CHECK_NEAR (norm (r, "osc1.unison.blend"), 60.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.unison.spread"), 80.0, 1e-3);
    CHECK_EQ (norm (r, "osc1.pitch_track"), 0.0f);
    CHECK_EQ (norm (r, "osc1.unison.stack"), 3.0f); // Octaves
    CHECK_EQ (norm (r, "osc1.unison.mode"), 1.0f);  // Super
    CHECK_NEAR (norm (r, "osc1.frame"), 129.0 / 256.0 * 100.0, 1e-2);   // position / 256 (measured against Serum 2.1.5)
    CHECK_NEAR (norm (r, "osc1.warp.amount"), 40.0, 1e-3);
    CHECK_EQ (norm (r, "osc1.warp.type"), 1.0f); // Sync
    CHECK_NEAR (norm (r, "osc1.phase"), 90.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.random_phase"), 50.0, 1e-3);
    CHECK_EQ (norm (r, "osc1.wavetable"), static_cast<float> (kWavetablePresetTable));
    CHECK_EQ (r.wavetables.size(), size_t { 1 });
    CHECK_EQ (r.wavetables[0].numFrames, 2);
    CHECK_EQ (r.wavetables[0].osc, 1);
    // OSC B and C are off in the init state, the noise and sub too; filter 1 and 2 are off
    CHECK_EQ (norm (r, "osc2.on"), 0.0f);
    CHECK_EQ (norm (r, "osc3.on"), 0.0f);
    CHECK_EQ (norm (r, "noise.on"), 0.0f);
    CHECK_EQ (norm (r, "sub.on"), 0.0f);
    CHECK_EQ (norm (r, "filter1.on"), 0.0f);
    CHECK_EQ (norm (r, "filter2.on"), 0.0f);
    // the routing defaults: OSC A through the filter, the others to the main output
    CHECK_EQ (norm (r, "osc1.destination"), 5.0f);
    CHECK_EQ (norm (r, "osc2.destination"), 3.0f);
    CHECK (! r.touched.empty());
    CHECK (r.report.numImported > 15);
}

TEST (serum_defaults_of_an_empty_patch)
{
    Preset p;
    const SerumImportResult r = run (p.file ("Empty"));
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (r.meta.name, std::string ("Empty"));
    CHECK_EQ (r.meta.author, std::string ("Tester"));
    CHECK (r.meta.tags.find ("src:") != std::string::npos);
    CHECK_EQ (norm (r, "osc1.on"), 1.0f);
    CHECK_EQ (norm (r, "master.bend_range"), 2.0f);
    CHECK_EQ (norm (r, "master.bend_down_link"), 1.0f);
    // a patch without a filter module keeps the defaults of our engine (the Serum default type of a filter that is switched on is tested below)
    CHECK_EQ (norm (r, "filter1.model"), 9.0f);
    CHECK_EQ (norm (r, "filter1.style"), 0.0f);
    // an LFO without a shape is a triangle
    CHECK_EQ (norm (r, "lfo1.shape"), 0.0f);
}

TEST (serum_filter_envelope_lfo_macro_values)
{
    Preset p;
    p.set ("VoiceFilter0", map ({ { "plainParams", map (withText ({ { "kParamEnable", 1 }, { "kParamFreq", 0.5 }, { "kParamReso", 40 }, { "kParamDrive", 50 }, { "kParamVar", 30 }, { "kParamWet", 80 },
                                                                    { "kParamKeyTrack", 1 }, { "kParamLevelOut", 0.5 }, { "kParamStereo", 60 } }, { { "kParamType", "LadderMg" } })) } }));
    p.set ("Env0", map ({ { "plainParams", plain ({ { "kParamAttack", 0.01 }, { "kParamHold", 0.1 }, { "kParamDecay", 1.5 }, { "kParamSustain", 0.5 }, { "kParamRelease", 2 },
                                                    { "kParamCurve1", 50 }, { "kParamCurve2", 66.6 }, { "kParamCurve3", 40 } }) } }));
    p.set ("Macro2", map ({ { "name", text ("CUTOFF") }, { "plainParams", plain ({ { "kParamValue", 42 } }) } }));
    p.set ("LFO0", map ({ { "curveData", curveMap ({ 0, 0.5, 1 }, { 1, 0, 1 }, { 0.5, 0.5, 0.5 }) }, { "curveDisplayName", text ("mytri") },
                         { "plainParams", map (withText ({ { "kParamBeatSync", 0 }, { "kParamRate", 4 }, { "kParamRise", 0.5 }, { "kParamDelay", 0.2 }, { "kParamPhase", 90 }, { "kParamMono", 1 },
                                                         { "kParamSmooth", 50 } }, { { "kParamMode", "Free" } })) } }));
    p.set ("LFO1", map ({ { "plainParams", map (withText ({ { "kParamBeatSync", 0 }, { "kParamRate", 2 } }, { { "kParamType", "Lorenz" } })) } }));
    p.set ("LFO2", map ({ { "plainParams", plain ({ { "kParamRate", 2 } }) } })); // tempo mode (the default), 2 Hz at 120 BPM = 1 beat = 1/4 note
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (norm (r, "filter1.on"), 1.0f);
    CHECK_NEAR (norm (r, "filter1.cutoff"), 440.0 * std::pow (2.0, (135.0 * 0.5 - 69.0 + 2.0) / 12.0), 0.5); // measured against Serum 2.1.5: +2 semitones on the 135 note scale
    CHECK_NEAR (norm (r, "filter1.resonance"), 40.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.drive"), 2.0, 1e-3);   // Serum drive 50: the drive of our filter that gives the same harmonic shape (measured), the makeup goes to the level
    CHECK_NEAR (norm (r, "filter1.var"), 30.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.mix"), 80.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.keytrack"), 100.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.level"), std::min (12.0, 40.0 * std::log10 (2.0 * 0.5) + serumimport::filterDriveMakeupDb (50.0) + serumimport::filterResoLevelDb ("LadderMg", 0.5, 40.0) + serumimport::filterDriveLevelDb ("LadderMg", 50.0, 40.0)), 1e-2);   // the drive makeup and the resonance / drive levels of the filter type (measured against Serum 2.1.5)
    CHECK (serumimport::filterResoLevelDb ("LadderMg", 0.5, 40.0) > 5.0);   // a Serum ladder keeps its level with the resonance, ours loses it
    CHECK (serumimport::filterResoLevelDb ("H12", 0.5, 60.0) < -3.0);   // a Serum state variable filter takes level with the resonance
    CHECK_EQ (serumimport::filterResoLevelDb ("NoSuchType", 0.5, 60.0), 0.0);
    CHECK_NEAR (norm (r, "filter1.pan"), 20.0, 1e-3);
    CHECK_EQ (norm (r, "filter1.model"), 26.0f); // New: MG Ladder is style 0
    CHECK_EQ (norm (r, "filter1.style"), 0.0f);
    CHECK_NEAR (norm (r, "env1.attack"), 10.0, 1e-3);
    CHECK_NEAR (norm (r, "env1.hold"), 100.0, 1e-3);
    CHECK_NEAR (norm (r, "env1.decay"), 1500.0, 1e-2);
    CHECK_NEAR (norm (r, "env1.sustain"), 50.0, 1e-3);
    CHECK_NEAR (norm (r, "env1.release"), 2000.0, 1e-2);
    CHECK_NEAR (norm (r, "env1.attack_curve"), 2.5, 1e-3);   // curve 50: the measured envelope shape (Serum 50 is not our 0)
    CHECK_NEAR (norm (r, "env1.decay_curve"), -1.0, 0.02);
    CHECK_NEAR (norm (r, "env1.release_curve"), 5.5, 1e-3);
    CHECK_NEAR (norm (r, "macro.3"), 42.0, 1e-3);
    CHECK_EQ (r.macroNames[2], std::string ("CUTOFF"));
    // LFO 1: a drawn shape, Hz mode, free running
    CHECK_EQ (norm (r, "lfo1.shape"), static_cast<float> (kLfoShapeCustom));
    CHECK_EQ (r.lfoShapes.size(), size_t { 1 });
    CHECK_EQ (r.lfoShapes[0].lfo, 1);
    CHECK_EQ (r.lfoShapes[0].name, std::string ("mytri"));
    CHECK_EQ (r.lfoShapes[0].points.size(), size_t { 3 });
    CHECK_NEAR (r.lfoShapes[0].points[0].y, 0.0, 1e-6); // the file stores the screen y: 1 is the bottom
    CHECK_NEAR (r.lfoShapes[0].points[1].y, 1.0, 1e-6);
    CHECK_NEAR (norm (r, "lfo1.rate"), 4.0, 1e-3);
    CHECK_EQ (norm (r, "lfo1.sync"), 0.0f);
    CHECK_EQ (norm (r, "lfo1.sync_type"), 1.0f);
    CHECK_NEAR (norm (r, "lfo1.fade_in"), 500.0, 1e-2);
    CHECK_NEAR (norm (r, "lfo1.delay"), 200.0, 1e-2);
    CHECK_NEAR (norm (r, "lfo1.phase"), 90.0, 1e-3);
    CHECK_EQ (norm (r, "lfo1.mono"), 1.0f);
    CHECK_EQ (norm (r, "lfo1.smooth"), 1.0f);
    // LFO 2 is a Lorenz attractor: the first random LFO
    CHECK_EQ (norm (r, "rlfo1.style"), 3.0f);
    CHECK_NEAR (norm (r, "rlfo1.rate"), 2.0, 1e-3);
    // LFO 3: tempo mode, 1/4 note
    CHECK_EQ (norm (r, "lfo3.sync"), 1.0f);
    CHECK_EQ (norm (r, "lfo3.division"), 8.0f);
}

TEST (serum_modulation_matrix_sources_destinations_curves)
{
    Preset p;
    p.set ("LFO1", map ({ { "plainParams", map (withText ({ { "kParamBeatSync", 0 }, { "kParamRate", 2 } }, { { "kParamType", "Lorenz" } })) } }));
    const auto slot = [] (int a, int b, const std::string& type, int id, const std::string& param, const Bytes& pp, const Bytes& flex = {})
    {
        Map m = { { "source", nums ({ static_cast<double> (a), static_cast<double> (b) }) }, { "destModuleTypeString", text (type) }, { "destModuleID", num (id) },
                  { "destModuleParamName", text (param) }, { "destModuleParamID", num (0) }, { "plainParams", pp } };
        if (! flex.empty()) m.push_back ({ "flex", flex });
        return map (m);
    };
    p.set ("ModSlot0", slot (6, 25, "Oscillator", 0, "kParamVolume", plain ({ { "kParamAmount", 50 }, { "kParamBipolar", 1 } })));        // LFO 1, aux Macro 1
    p.set ("ModSlot1", slot (7, 0, "VoiceFilter", 0, "kParamFreq", plain ({ { "kParamAmount", -30 } })));                                 // LFO 2 (Lorenz -> rlfo1)
    p.set ("ModSlot2", slot (51, 0, "VoiceFilter", 0, "kParamFreq", plain ({ { "kParamAmount", 10 } })));                                // OSC C as a source (GROUPB WP-06: the output of osc 3)
    p.set ("ModSlot3", slot (1, 0, "Arp", 0, "kParamGate", plain ({ { "kParamAmount", 10 } })));                                         // an arpeggiator target: none
    p.set ("ModSlot4", slot (16, 0, "WTOsc", 1, "kParamTablePos", plain ({ { "kParamAmount", 20 }, { "kParamMainCurveData", 1 }, { "kParamCurveIn", 40 } }),
                             arr ({ curveMap ({ 0, 1 }, { 1, 0.2 }, { 0.5, 0.5 }), map ({}) })));                                          // Velo -> table position of osc B with a drawn curve
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    const ModSlotParams s0 = modSlotParams (0), s1 = modSlotParams (1), s4 = modSlotParams (4), s2 = modSlotParams (2);
    CHECK_EQ (r.values.plain[s0.source], 7.0f);                      // lfo1 is choice 7
    CHECK_EQ (r.values.plain[s0.auxSource], 19.0f);                  // macro1 is choice 19
    CHECK_EQ (r.values.plain[s0.destination], static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (hashKey ("osc1.level"))))));
    CHECK_NEAR (r.values.plain[s0.amount], 50.0, 1e-3);
    CHECK_EQ (r.values.plain[s0.bipolar], 1.0f);
    CHECK_EQ (r.values.plain[s1.source], 15.0f);                     // rlfo1 is choice 15
    CHECK_NEAR (r.values.plain[s1.amount], -30.0, 1e-3);
    CHECK_EQ (r.values.plain[s2.source], 45.0f);                     // the output of oscillator C (GROUPB WP-06)
    CHECK_NEAR (r.values.plain[s2.amount], 10.0, 1e-3);
    CHECK (countKind (r, ItemKind::dropped, "ModSlot3") >= 1);
    CHECK_EQ (r.values.plain[s4.source], 23.0f);                     // velocity
    CHECK_EQ (r.values.plain[s4.destination], static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (hashKey ("osc2.frame"))))));
    CHECK_NEAR (r.values.plain[s4.power], 4.0, 1e-3);
    CHECK_EQ (r.modCurves.size(), size_t { 1 });
    CHECK_EQ (r.modCurves[0].slot, 5);
    CHECK_NEAR (r.modCurves[0].points.back().y, 0.8, 1e-6);
}

TEST (serum_linear_gain_destinations_get_their_modulation_depth_limited)
{
    Preset p;
    const auto slot = [] (const std::string& type, int id, const std::string& param, double amount)
    {
        return map ({ { "source", nums ({ 16, 0 }) }, { "destModuleTypeString", text (type) }, { "destModuleID", num (id) }, { "destModuleParamName", text (param) },
                      { "destModuleParamID", num (0) }, { "plainParams", plain ({ { "kParamAmount", amount } }) } });
    };
    p.set ("ModSlot0", slot ("VoiceFilter", 0, "kParamLevelOut", 75));  // filter level -60 .. +12 dB: +75 % would be +54 dB; limited to +12 dB of 72 -> 12.5 %
    p.set ("ModSlot1", slot ("VoiceFilter", 0, "kParamLevelOut", -100)); // -24 dB of 72 -> -33.3 %
    p.set ("ModSlot2", slot ("VoiceFilter", 0, "kParamFreq", 75));       // not a gain: untouched
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_NEAR (r.values.plain[modSlotParams (0).amount], 75.0 * 12.0 / 72.0, 1e-3);
    CHECK_NEAR (r.values.plain[modSlotParams (1).amount], -100.0 * 24.0 / 72.0, 1e-3);
    CHECK_NEAR (r.values.plain[modSlotParams (2).amount], 75.0, 1e-3);
    CHECK (entryWith (r, "db_destination_depth_limited") >= 0);
}

// GROUPA1 (A347): the maximum compressor RATIO (1,000,000) is the Serum LIMIT detent: limit mode, the makeup gain drives into the limiter
TEST (serum_compressor_ratio_at_its_maximum_is_the_limit_mode)
{
    const auto fx = [] (const std::string& name, const Bytes& pp, int type) { return map ({ { name, map ({ { "plainParams", pp } }) }, { "type", num (type) } }); };
    const auto run1 = [&] (double ratio)
    {
        Preset p;
        p.set ("FXRack0", map ({ { "FX", arr ({ fx ("FXComp", plain ({ { "kParamRatio", ratio }, { "kParamThresh", 0.5 }, { "kParamMakeup", 3.5 }, { "kParamWet", 100 } }), 2) }) },
                                 { "displayName", text ("") }, { "plainParams", dflt() } }));
        return run (p.file());
    };
    int limitNumber = -1;
    const FxTypeDescriptor& cd = fxTypeDescriptor (kFxTypeCompressor);
    for (int q = 0; q < cd.count; ++q)
        if (cd.source[q] == findParamIndex (hashKey ("compressor.limit"))) limitNumber = q;
    CHECK (limitNumber >= 0);
    for (const double ratio : { 1.0e6, 4.0 })
    {
        const SerumImportResult r = run1 (ratio);
        CHECK (r.status == ImportStatus::ok);
        CHECK_EQ (r.report.unaccounted, 0);
        const FxRacks racks = fxRacks (r.values.plain);
        int slot = -1;
        for (const FxRackItem& it : racks.rack[0]) if (it.pool) slot = it.index;
        CHECK (slot >= 0);
        if (slot < 0) continue;
        const double limit = fxInstancePlain (kFxTypeCompressor, limitNumber, r.values.plain[fxInstanceKeys (slot).p[limitNumber]]);
        CHECK_NEAR (limit, ratio >= 1.0e5 ? 1.0 : 2.0, 1e-4);   // the ratio at its maximum is the look-ahead limiter (1), any other ratio the peak compressor of Serum (2, A508)
        CHECK ((entryWith (r, "compressor_ratio_max_is_limit_mode") >= 0) == (ratio >= 1.0e5));
    }
}

// GROUPA1 (WP-04, A348): the noise source plays the sample of the preset (the file of the Noises folder, or the data embedded in it); a colour stays the
// generator and its kParamColor is the FILTER knob
TEST (serum_noise_source_takes_the_sample_file_the_embedded_data_or_the_colour)
{
    const auto noisePreset = [] (const Map& block)
    {
        Preset p;
        p.set ("Oscillator3", map ({ { "plainParams", plain ({ { "kParamEnable", 1 } }) }, { "NoiseOsc3", map (block) } }));
        return p;
    };
    const auto meta = [] (int frames, int channels, int rate) -> Map
    {
        return { { "numFrames", num (frames) }, { "numChannels", num (channels) }, { "sampleRate", num (rate) }, { "detuneFactor", num (1) } };
    };

    // 1. a file of the Noises folder
    {
        MemResolver res;
        std::vector<float> noise (2000);
        for (size_t i = 0; i < noise.size(); ++i) noise[i] = 0.5f * static_cast<float> (std::sin (0.3 * static_cast<double> (i)));
        res.files["Noises/Organics/Hum.wav"] = wavFloat (noise);
        Map block = meta (2000, 1, 44100);
        block.push_back ({ "relativePathToNoiseSample", text ("Organics/Hum.wav") });
        block.push_back ({ "plainParams", plain ({ { "kParamColor", 0.25 }, { "kParamOneShot", 1 } }) });
        const SerumImportResult r = run (noisePreset (block).file(), &res);
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (value (r, "noise.type"), 1.0f);
        CHECK_EQ (value (r, "noise.loop"), 0.0f);          // one shot
        CHECK_EQ (value (r, "noise.filter"), 0.0f);        // a sample has no filter: kParamColor is dropped
        CHECK_EQ (r.samples.size(), size_t { 1 });
        CHECK_EQ (r.samples[0].osc, kNoiseSampleOsc);
        CHECK_EQ (r.samples[0].numFrames, 2000);
        CHECK (entryWith (r, "noise_sample_file_stored_in_the_library") >= 0);
        CHECK (entryWith (r, "noise_filter_acts_on_the_colour_samples_only") >= 0);
    }
    // 2. the data embedded in the preset wins over the file name beside it
    {
        MemResolver res;
        res.files["Noises/Organics/Hum.wav"] = wavFloat (std::vector<float> (300, 0.1f));
        std::vector<double> data (500);
        for (size_t i = 0; i < data.size(); ++i) data[i] = 0.25 * std::sin (0.1 * static_cast<double> (i));
        Map block = meta (500, 1, 22050);
        block.push_back ({ "embeddedNoiseData", arr ({ nums (data) }) });
        block.push_back ({ "relativePathToNoiseSample", text ("Organics/Hum.wav") });
        const SerumImportResult r = run (noisePreset (block).file(), &res);
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (value (r, "noise.type"), 1.0f);
        CHECK_EQ (r.samples.size(), size_t { 1 });
        CHECK_EQ (r.samples[0].numFrames, 500);
        CHECK_EQ (r.samples[0].sampleRate, 22050);
        CHECK (entryWith (r, "noise_file_replaced_by_the_embedded_data") >= 0);
        CHECK (entryWith (r, "embedded_noise_sample_stored_in_the_library") >= 0);
    }
    // 3. a colour is a generator; kParamColor is its FILTER knob (0.5 = off, below a low pass, above a high pass)
    for (const double color : { 0.25, 0.5, 1.0 })
    {
        Map block = meta (100, 1, 44100);
        block.push_back ({ "relativePathToNoiseSample", text ("Organics/Hum.wav") });
        block.push_back ({ "plainParams", map (withText ({ { "kParamColor", color } }, { { "kParamNoiseType", "Pink" } })) });
        const SerumImportResult r = run (noisePreset (block).file());
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (value (r, "noise.type"), 0.0f);
        CHECK_EQ (value (r, "noise.sample"), 1.0f);        // Pink
        CHECK_NEAR (value (r, "noise.filter"), (color - 0.5) * 200.0, 1e-3);
        CHECK (r.samples.empty());
    }
    // 4. a file that cannot be read: the nearest generator by its name, as before
    {
        Map block = meta (100, 1, 44100);
        block.push_back ({ "relativePathToNoiseSample", text ("Analog/ARP pink.wav") });
        const SerumImportResult r = run (noisePreset (block).file());
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (value (r, "noise.type"), 0.0f);
        CHECK_EQ (value (r, "noise.sample"), 1.0f);
        CHECK (entryWith (r, "noise_file_matched_by_name") >= 0);
    }
}

// GROUPA1 (WP-09, A350): mpeConfig 0 = no mapping; a value nobody has seen is reported, not guessed
TEST (serum_mpe_config_zero_is_no_mapping_and_other_values_are_reported)
{
    for (const double config : { 0.0, 3.0 })
    {
        Preset p;
        p.set ("Global0", map ({ { "plainParams", plain ({ { "kParamPolyCount", 8 } }) } }));
        p.set ("mpeConfig", num (config));
        const SerumImportResult r = run (p.file());
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (value (r, "master.mpe.xyz_macro"), 0.0f);
        CHECK_EQ (value (r, "master.mpe.y_modwheel"), 0.0f);
        CHECK ((entryWith (r, "mpe_config_no_mapping") >= 0) == (config == 0.0));
        CHECK ((entryWith (r, "mpe_config_value_not_decoded") >= 0) == (config != 0.0));
    }
}

// GROUPA1 (WP-10, A351 / A352): the delay offset of each channel, and the FEEDBACK of Space (Nitrous) and Abyss (Basin) as reverb.feedback
TEST (serum_delay_offsets_and_reverb_feedback_have_keys)
{
    const auto fx = [] (const std::string& name, const Bytes& pp, int type) { return map ({ { name, map ({ { "plainParams", pp } }) }, { "type", num (type) } }); };
    const auto number = [] (int type, const char* key)
    {
        const FxTypeDescriptor& d = fxTypeDescriptor (type);
        for (int q = 0; q < d.count; ++q)
            if (d.source[q] == findParamIndex (hashKey (key))) return q;
        return -1;
    };
    const auto slotOf = [] (const SerumImportResult& r)
    {
        const FxRacks racks = fxRacks (r.values.plain);
        for (const FxRackItem& it : racks.rack[0]) if (it.pool) return it.index;
        return -1;
    };
    {
        Preset p;
        p.set ("FXRack0", map ({ { "FX", arr ({ fx ("FXDelay", plain ({ { "kParamWet", 30 }, { "kParamOffsetL", 1.5 }, { "kParamOffsetR", 0.5 } }), 3) }) },
                                 { "displayName", text ("") }, { "plainParams", dflt() } }));
        const SerumImportResult r = run (p.file());
        CHECK_EQ (r.report.unaccounted, 0);
        const int slot = slotOf (r);
        CHECK (slot >= 0);
        if (slot >= 0)
        {
            const int left = number (kFxTypeDelay, "delay.offset_left"), right = number (kFxTypeDelay, "delay.offset");
            CHECK (left >= 0 && right >= 0);
            CHECK_NEAR (fxInstancePlain (kFxTypeDelay, left, r.values.plain[fxInstanceKeys (slot).p[left]]), 1.5, 1e-3);
            CHECK_NEAR (fxInstancePlain (kFxTypeDelay, right, r.values.plain[fxInstanceKeys (slot).p[right]]), 0.5, 1e-3);
        }
        CHECK (entryWith (r, "delay_offset_left_only") < 0);
    }
    for (const char* kind : { "kSpace", "kAbyss", "kHall" })
    {
        Preset p;
        p.set ("FXRack0", map ({ { "FX", arr ({ fx ("FXReverb", map (withText ({ { "kParamWet", 40 }, { "kParamFeedback", 70 } }, { { "kParamType", kind } })), 7) }) },
                                 { "displayName", text ("") }, { "plainParams", dflt() } }));
        const SerumImportResult r = run (p.file());
        CHECK_EQ (r.report.unaccounted, 0);
        const int slot = slotOf (r);
        CHECK (slot >= 0);
        if (slot < 0) continue;
        const int feedback = number (kFxTypeReverb, "reverb.feedback"), decay = number (kFxTypeReverb, "reverb.decay");
        CHECK (feedback >= 0 && decay >= 0);
        const double fb = fxInstancePlain (kFxTypeReverb, feedback, r.values.plain[fxInstanceKeys (slot).p[feedback]]);
        const double dc = fxInstancePlain (kFxTypeReverb, decay, r.values.plain[fxInstanceKeys (slot).p[decay]]);
        if (std::string (kind) == "kHall")
        {
            CHECK_NEAR (fb, 50.0, 1e-3);          // the default: Hall has no FEEDBACK, its decay is the parameter
            CHECK (std::fabs (dc - 1500.0) > 1.0);
        }
        else
        {
            CHECK_NEAR (fb, 70.0, 1e-3);          // Nitrous / Basin: the percent
            CHECK_NEAR (dc, 1500.0, 1.0);         // the decay time stays its default
        }
    }
}

TEST (serum_effect_racks_become_pool_slots_in_order)
{
    Preset p;
    const auto fx = [] (const std::string& name, const Bytes& pp, int type) { return map ({ { name, map ({ { "plainParams", pp } }) }, { "type", num (type) } }); };
    p.set ("FXRack0", map ({ { "FX", arr ({ fx ("FXReverb", map (withText ({ { "kParamWet", 40 }, { "kParamSize", 60 } }, { { "kParamType", "kHall" } })), 7),
                                            fx ("FXDelay", plain ({ { "kParamWet", 30 }, { "kParamTimeL", 0.25 }, { "kParamFeedback", 40 } }), 3) }) },
                            { "displayName", text ("") }, { "plainParams", dflt() } }));
    p.set ("FXRack1", map ({ { "FX", arr ({ fx ("FXDistortion", map (withText ({ { "kParamDrive", 50 }, { "kParamWet", 100 } }, { { "kParamMode", "kDiode1" } })), 0) }) },
                            { "displayName", text ("") }, { "plainParams", dflt() } }));
    p.set ("ModSlot0", map ({ { "source", nums ({ 6, 0 }) }, { "destModuleTypeString", text ("FXDelay") }, { "destModuleID", num (1) }, { "destModuleParamName", text ("kParamWet") },
                              { "destModuleParamID", num (1) }, { "plainParams", plain ({ { "kParamAmount", 40 } }) } }));
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    const FxRacks racks = fxRacks (r.values.plain);
    // instance 0 modules are always listed; the pool slots are the ones we added
    std::vector<FxRackItem> pool0, pool1;
    for (const FxRackItem& it : racks.rack[0]) if (it.pool) pool0.push_back (it);
    for (const FxRackItem& it : racks.rack[1]) if (it.pool) pool1.push_back (it);
    CHECK_EQ (pool0.size(), size_t { 2 });
    CHECK_EQ (pool1.size(), size_t { 1 });
    CHECK_EQ (fxItemType (r.values.plain, pool0[0]), kFxTypeReverb);
    CHECK_EQ (fxItemType (r.values.plain, pool0[1]), kFxTypeDelay);
    CHECK_EQ (fxItemType (r.values.plain, pool1[0]), kFxTypeDistortion);
    const FxInstanceKeys& k0 = fxInstanceKeys (pool0[0].index);
    // the reverb mix is p-parameter of the slot: normalised 0.4 of the 0 .. 100 % range
    const FxTypeDescriptor& rd = fxTypeDescriptor (kFxTypeReverb);
    int mixNumber = -1, typeNumber = -1;
    for (int q = 0; q < rd.count; ++q)
    {
        if (rd.source[q] == findParamIndex (hashKey ("reverb.mix"))) mixNumber = q;
        if (rd.source[q] == findParamIndex (hashKey ("reverb.type"))) typeNumber = q;
    }
    CHECK (mixNumber >= 0 && typeNumber >= 0);
    // A509: the mix knob of the module is the one that gives the dry and wet gain of the Serum law at 40 % (Hall: the wet signal 6.7 dB under ours)
    double expectedMix = 0.0, expectedLevel = 0.0;
    CHECK (serumimport::fxMixRemap (kFxTypeReverb, 0.4, -6.7, expectedMix, expectedLevel));
    CHECK_NEAR (fxInstancePlain (kFxTypeReverb, mixNumber, r.values.plain[k0.p[mixNumber]]), 100.0 * expectedMix, 0.05);
    CHECK_NEAR (fxInstancePlain (kFxTypeReverb, typeNumber, r.values.plain[k0.p[typeNumber]]), 2.0, 1e-4); // Hall
    // the delay time 0.25 s is 250 ms
    const FxTypeDescriptor& dd = fxTypeDescriptor (kFxTypeDelay);
    int timeNumber = -1;
    for (int q = 0; q < dd.count; ++q) if (dd.source[q] == findParamIndex (hashKey ("delay.time"))) timeNumber = q;
    CHECK_NEAR (fxInstancePlain (kFxTypeDelay, timeNumber, r.values.plain[fxInstanceKeys (pool0[1].index).p[timeNumber]]), 250.0, 0.5);
    // the modulation of the delay mix (rack 0, position 1) points at the pool parameter
    const ModSlotParams s0 = modSlotParams (0);
    CHECK (r.values.plain[s0.destination] > 0.0f);
    const int destIdx = modDestinationParamIndex (static_cast<size_t> (r.values.plain[s0.destination]));
    CHECK (destIdx >= 0);
    int wetNumber = -1;
    for (int q = 0; q < dd.count; ++q) if (dd.source[q] == findParamIndex (hashKey ("delay.mix"))) wetNumber = q;
    CHECK_EQ (destIdx, fxInstanceKeys (pool0[1].index).p[wetNumber]);
}

// REFCOMPARE round 3 (13-new-keys.md 4.48): the effect laws measured on the real Serum 2.1.5 (A508 .. A513)
namespace
{
/** One effect module in rack 0 (plainParams `pp`, Serum type code `type`) imported; returns the plain value of our key `key` in its pool slot (-9999 when the key is not one of the type's). */
struct OneFx
{
    SerumImportResult r;
    int slot = -1, type = 0;
    double operator() (const char* key) const
    {
        if (slot < 0) return -9999.0;
        const FxTypeDescriptor& d = fxTypeDescriptor (type);
        for (int q = 0; q < d.count; ++q)
            if (d.source[q] == findParamIndex (hashKey (key)))
                return static_cast<double> (fxInstancePlain (type, q, r.values.plain[fxInstanceKeys (slot).p[q]]));
        return -9999.0;
    }
};
OneFx importOne (const std::string& name, int serumType, int ourType, const Bytes& pp)
{
    Preset p;
    p.set ("FXRack0", map ({ { "FX", arr ({ map ({ { name, map ({ { "plainParams", pp } }) }, { "type", num (serumType) } }) }) }, { "displayName", text ("") }, { "plainParams", dflt() } }));
    OneFx o;
    o.r = run (p.file());
    o.type = ourType;
    const FxRacks racks = fxRacks (o.r.values.plain);
    for (const FxRackItem& it : racks.rack[0]) if (it.pool) o.slot = it.index;
    return o;
}
} // namespace

TEST (serum_absent_effect_parameters_take_the_serum_defaults)
{
    // a Serum file stores only what differs from the module defaults (measured: tools/ref-compare/findef.py)
    const OneFx chorus = importOne ("FXChorus", 3, kFxTypeChorus, plain ({ { "kParamWet", 50 } }));
    CHECK (chorus.slot >= 0);
    CHECK_NEAR (chorus ("chorus.rate"), 0.078125, 1e-4);
    CHECK_NEAR (chorus ("chorus.feedback"), 9.5, 1e-3);
    CHECK_NEAR (chorus ("chorus.filter_cutoff"), 1000.0, 1.0);
    const OneFx comp = importOne ("FXComp", 5, kFxTypeCompressor, plain ({ { "kParamWet", 100 } }));
    CHECK_NEAR (comp ("compressor.bands"), 3.0, 1e-4);              // single band: the Multiband switch is off by default
    CHECK_NEAR (comp ("compressor.high.upper_ratio"), 75.0, 0.05);   // 4:1
    CHECK_NEAR (comp ("compressor.high.upper_threshold"), serumimport::convert (*[] { size_t n = 0; const serumimport::Rule* rows = serumimport::fxRulesOf (kFxTypeCompressor, n); for (size_t i = 0; i < n; ++i) if (std::strcmp (rows[i].param, "kParamThresh") == 0) return rows + i; return rows; }(), 0.5), 0.01);
    CHECK_NEAR (comp ("compressor.limit"), 2.0, 1e-4);               // the peak compressor of Serum
}

TEST (serum_delay_with_beat_sync_reads_the_time_as_a_note_value)
{
    // BeatSync is on by default; the file value is a position on a scale of note values (measured: the onset of the echo at 120 BPM)
    const OneFx quarter = importOne ("FXDelay", 4, kFxTypeDelay, plain ({ { "kParamTimeL", 0.0624 }, { "kParamTimeR", 0.09 } }));
    CHECK_NEAR (quarter ("delay.sync"), 1.0, 1e-4);
    CHECK_NEAR (quarter ("delay.division"), 4.0, 1e-4);    // 1/4
    CHECK_NEAR (quarter ("delay.division2"), 4.0, 1e-4);
    const OneFx half = importOne ("FXDelay", 4, kFxTypeDelay, plain ({ { "kParamTimeL", 0.13 }, { "kParamTimeR", 0.15 } }));
    CHECK_NEAR (half ("delay.division"), 3.0, 1e-4);       // 1/2
    const OneFx free = importOne ("FXDelay", 4, kFxTypeDelay, plain ({ { "kParamBeatSync", 0 }, { "kParamTimeL", 0.2 }, { "kParamTimeR", 0.3 } }));
    CHECK_NEAR (free ("delay.sync"), 0.0, 1e-4);
    CHECK_NEAR (free ("delay.time"), 200.0, 0.5);
    CHECK_NEAR (free ("delay.time2"), 300.0, 0.5);
    const OneFx tiny = importOne ("FXDelay", 4, kFxTypeDelay, plain ({ { "kParamTimeL", 0.004 }, { "kParamTimeR", 0.004 } }));   // the 15.5 ms step has no division: free time
    CHECK_NEAR (tiny ("delay.sync"), 0.0, 1e-4);
    CHECK_NEAR (tiny ("delay.time"), 15.5, 0.5);
}

TEST (serum_wet_dry_law_and_wet_gain_of_the_modules)
{
    // the mix law of Serum (dry 1 - s^2, wet s = sin^2 (pi w / 2) for most modules): our module is driven so that the dry and the wet gain come out right
    for (const double w : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        for (const int type : { kFxTypeChorus, kFxTypeDistortion, kFxTypeReverb, kFxTypeFlanger, kFxTypeBode, kFxTypeHyperDimension })
        {
            double mix = -1.0, level = 0.0;
            CHECK (serumimport::fxMixRemap (type, w, 0.0, mix, level));
            CHECK (mix >= 0.0 && mix <= 1.0);
            if (w == 0.0) { CHECK_NEAR (mix, 0.0, 1e-9); CHECK_NEAR (level, 0.0, 1e-6); }
            if (w == 1.0) CHECK_NEAR (mix, 1.0, 1e-9);
        }
    double a = 0.0, la = 0.0, b = 0.0, lb = 0.0;
    CHECK (serumimport::fxMixRemap (kFxTypeDistortion, 0.5, 0.0, a, la));   // the Serum Distortion at 50 %: dry 0.75, wet 0.5; ours is linear (dry 1 - m, wet m): m = 0.4, level +2.5 dB
    CHECK_NEAR (a, 0.4, 0.02);
    CHECK_NEAR (la, 20.0 * std::log10 (1.25), 0.2);
    CHECK (serumimport::fxMixRemap (kFxTypeDistortion, 0.5, 6.0, b, lb));   // a wet signal 6 dB stronger than ours needs more mix
    CHECK (b > a);
    // the laws of the distortion modes: the Serum drive is mapped to ours with a level (kTube at drive 0 is 4.8 dB under ours at its minimum)
    double drive = 0.0, gain = 0.0;
    CHECK (serumimport::fxDistortionLaw ("kTube", 0.0, drive, gain));
    CHECK_NEAR (gain, -4.8, 0.05);
    CHECK (! serumimport::fxDistortionLaw ("kNoSuchMode", 10.0, drive, gain));
    CHECK_NEAR (serumimport::hyperWetGainDb (4.0), 9.2, 1e-9);        // four unison voices: the wet signal is sqrt (4) = 6 dB over the dry one in Serum, ours is normalised
    // end to end: a Chorus at 100 % wet has the wet gain of the table in its level (the dry part is gone, only the level moves)
    const OneFx chorus = importOne ("FXChorus", 3, kFxTypeChorus, plain ({ { "kParamWet", 100 } }));
    CHECK_NEAR (chorus ("chorus.mix"), 100.0, 0.05);
    CHECK_NEAR (chorus ("chorus.level"), serumimport::fxWetGainDb (kFxTypeChorus), 0.2);
}

TEST (serum_reverb_decay_follows_size_vintage_scale_and_type)
{
    const OneFx plate = importOne ("FXReverb", 6, kFxTypeReverb, plain ({ { "kParamSize", 30 }, { "kParamFeedback", 90 } }));
    CHECK_NEAR (plate ("reverb.decay"), 2200.0, 5.0);     // the Feedback knob has no effect on a Plate
    const OneFx big = importOne ("FXReverb", 6, kFxTypeReverb, plain ({ { "kParamSize", 50 } }));
    CHECK_NEAR (big ("reverb.decay"), 8400.0, 20.0);
    const OneFx vintage = importOne ("FXReverb", 6, kFxTypeReverb, map ({ { "kParamType", text ("kVintage") }, { "kParamVintageScale", num (50) } }));
    CHECK_NEAR (vintage ("reverb.decay"), 2040.0, 5.0);
    const OneFx hall = importOne ("FXReverb", 6, kFxTypeReverb, map ({ { "kParamType", text ("kHall") }, { "kParamSize", num (10) } }));
    CHECK_NEAR (hall ("reverb.decay"), 3200.0, 5.0);   // the Hall has 3.2 s whatever the size
}

TEST (serum_multiband_compressor_maps_the_bands_and_the_limiter_makeup)
{
    const OneFx mb = importOne ("FXComp", 5, kFxTypeCompressor, plain ({ { "kParamMultiband", 1 }, { "kParamThreshUD1", 50 }, { "kParamRatio1", 0.5 }, { "kParamGain1", 6 }, { "kParamMakeup", 2.0 } }));
    CHECK_NEAR (mb ("compressor.bands"), 0.0, 1e-4);                 // Multiband
    CHECK_NEAR (mb ("compressor.mid.upper_threshold"), -33.6, 0.05);   // UD 50
    CHECK_NEAR (mb ("compressor.mid.upper_ratio"), 100.0 * 0.75 * std::pow (0.5, 0.9), 0.05);   // the band ratio goes from 1:1 to the global ratio (4:1)
    CHECK_NEAR (mb ("compressor.mid.gain"), 6.0 + 20.0 * std::log10 (2.0), 0.05);   // the band gain and the makeup
    CHECK_NEAR (mb ("compressor.low.upper_threshold"), -33.6 + 13.3 * (72.533 - 50.0) / 50.0, 0.05);   // the low band has the default UD 72.5
    // the makeup of the limiter is 36 ln (2 m - 1) / ln 61 dB, after the limiter
    const OneFx lim = importOne ("FXComp", 5, kFxTypeCompressor, plain ({ { "kParamRatio", 1.0e6 }, { "kParamMakeup", 4.0 } }));
    CHECK_NEAR (lim ("compressor.high.gain"), 36.0 * std::log (7.0) / std::log (61.0), 0.05);   // 17.0 dB (20 log10 4 = 12 dB for the compressor)
    CHECK_NEAR (lim ("compressor.limit"), 1.0, 1e-4);
}

TEST (serum_eq_bands_and_unsupported_effects)
{
    Preset p;
    const auto fx = [] (const std::string& name, const Bytes& pp) { return map ({ { name, map ({ { "plainParams", pp } }) }, { "type", num (0) } }); };
    p.set ("FXRack0", map ({ { "FX", arr ({ fx ("FXEQ", plain ({ { "kParamFreq1", 200 }, { "kParamGain1", 6 }, { "kParamType1", 1 }, { "kParamFreq2", 5000 }, { "kParamGain2", -4 }, { "kParamType2", 1 } })),
                                            fx ("FXMystery", plain ({ { "kParamWet", 50 } })) }) },
                            { "plainParams", dflt() } }));
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK (entryWith (r, "unknown_effect_module") >= 0);
    // A508: two peak bands are the mid band and the second peak band, the law of the module is Serum, the Serum defaults fill what the file leaves out (Q 60 %)
    const FxRacks racks = fxRacks (r.values.plain);
    int slot = -1;
    for (const FxRackItem& it : racks.rack[0]) if (it.pool && fxTypeDescriptor (kFxTypeEq).count > 0) { slot = it.index; break; }
    CHECK (slot >= 0);
    if (slot >= 0)
    {
        const FxTypeDescriptor& d = fxTypeDescriptor (kFxTypeEq);
        const auto plain = [&] (const char* key)
        {
            for (int q = 0; q < d.count; ++q)
                if (d.source[q] == findParamIndex (hashKey (key)))
                    return static_cast<double> (fxInstancePlain (kFxTypeEq, q, r.values.plain[fxInstanceKeys (slot).p[q]]));
            return -9999.0;
        };
        CHECK_NEAR (plain ("eq.law"), 1.0, 1e-4);
        CHECK_NEAR (plain ("eq.mid.cutoff"), 200.0, 0.5);
        CHECK_NEAR (plain ("eq.mid.gain"), 6.0, 0.01);
        CHECK_NEAR (plain ("eq.mid.resonance"), 60.0, 0.01);
        CHECK_NEAR (plain ("eq.mid2.mode"), 1.0, 1e-4);
        CHECK_NEAR (plain ("eq.mid2.cutoff"), 5000.0, 2.0);
        CHECK_NEAR (plain ("eq.mid2.gain"), -4.0, 0.01);
        CHECK_NEAR (plain ("eq.low.gain"), 0.0, 1e-4);      // both bands of the file are peaks: the shelves of the module stay flat
        CHECK_NEAR (plain ("eq.high.gain"), 0.0, 1e-4);
    }
}

TEST (serum_content_tables_samples_multisamples_impulses)
{
    MemResolver res;
    std::vector<float> saw3 (2048 * 3);
    for (size_t i = 0; i < saw3.size(); ++i) saw3[i] = static_cast<float> ((i % 2048) / 1024.0 - 1.0) * (0.4f + 0.2f * static_cast<float> (i / 2048));
    res.files["Tables/S2 Tables/three.wav"] = wavFloat (saw3);
    std::vector<float> sine (2048);
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = static_cast<float> (std::sin (2.0 * 3.14159265358979 * static_cast<double> (i) / 2048.0));
    res.files["Tables/S2 Tables/sine.wav"] = wavFloat (sine);
    std::vector<float> tone (4410);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
    res.files["Samples/Factory/Pad/tone.wav"] = wavFloat (tone);
    res.files["Multisamples/Factory/Keys/Zone Samples/zone C3.wav"] = wavFloat (tone);
    std::vector<float> ir (3000, 0.0f);
    ir[0] = 1.0f;
    ir[500] = 0.5f;
    res.files["Impulses/Factory/Hall/room.wav"] = wavFloat (ir);
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", plain ({}) },
                                 { "WTOsc0", map ({ { "relativePathToWT", text ("S2 Tables/three.wav") }, { "numFrames", num (6144) } }) } }));
    p.set ("Oscillator1", map ({ { "plainParams", map (withText ({ { "kParamStart", 10 }, { "kParamEnd", 90 }, { "kParamLoopStart", 20 }, { "kParamLoopEnd", 80 }, { "kParamScanRate", 150 }, { "kParamEnable", 1 } },
                                                              { { "kParamType", "kOsc_Sample" }, { "kParamLoopMode", "kPingPong" } })) },
                                 { "SampleOsc1", map ({ { "samplePathRelative", text ("Factory/Pad/tone.wav") }, { "baseNote", num (48) }, { "numFrames", num (4410) }, { "sampleRate", num (44100) } }) } }));
    p.set ("Oscillator2", map ({ { "plainParams", map (withText ({ { "kParamEnable", 1 } }, { { "kParamType", "kOsc_MultiSample" } })) },
                                 { "MultiSampleOsc2", map ({ { "sfzPathRelative", text ("Factory/Keys/Zone.sfz") },
                                                              { "embedded_sfz", text ("// SFZ\n<group> amp_veltrack=0\n<region> hikey=40 pitch_keycenter=36 sample=Zone Samples/zone C1.wav\n"
                                                                                       "<region> lokey=41 hikey=72 pitch_keycenter=60 tune=-5 sample=Zone Samples/zone C3.wav\n") },
                                                              { "files", map ({ { "Zone Samples\\zone C3.wav", map ({ { "numChannels", num (1) } }) } }) } }) } }));
    p.set ("FXRack0", map ({ { "FX", arr ({ map ({ { "FXConv", map ({ { "plainParams", plain ({ { "kParamWet", 40 }, { "kParamSize", 500 } }) }, { "relativePathToIR", text ("Factory/Hall/room.wav") },
                                                                          { "numChannels", num (1) }, { "numFrames", num (3000) } }) }, { "type", num (9) } }) }) },
                            { "plainParams", dflt() } }));
    const SerumImportResult r = run (p.file(), &res, false); // an engine without the multisample oscillator: the reduced path
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (r.missingFiles, 0);
    // the 3 frame table is stored
    CHECK_EQ (norm (r, "osc1.wavetable"), static_cast<float> (kWavetablePresetTable));
    bool tableOk = false;
    for (const auto& w : r.wavetables) if (w.osc == 1 && w.numFrames == 3 && ! w.wavBytes.empty() && w.sha256.size() == 64) tableOk = true;
    CHECK (tableOk);
    // the sample oscillator
    CHECK_EQ (norm (r, "osc2.type"), 1.0f);
    CHECK_EQ (norm (r, "osc2.sample.root"), 48.0f);
    CHECK_NEAR (norm (r, "osc2.sample.start"), 10.0, 1e-3);
    CHECK_NEAR (norm (r, "osc2.sample.loop_end"), 80.0, 1e-3);
    CHECK_EQ (norm (r, "osc2.sample.loop_mode"), 3.0f);
    CHECK_EQ (norm (r, "osc2.sample.scan_range"), 1.0f); // 150 % needs the +-200 % range
    CHECK_NEAR (norm (r, "osc2.sample.scan"), 75.0, 1e-3);
    bool sampleOk = false;
    for (const auto& s : r.samples) if (s.osc == 2 && s.numFrames == 4410 && s.channels == 1 && s.sampleRate == 44100) sampleOk = true;
    CHECK (sampleOk);
    // the multisample is reduced to the zone that holds C3
    CHECK_EQ (norm (r, "osc3.type"), 1.0f);
    CHECK_EQ (norm (r, "osc3.sample.root"), 60.0f);
    CHECK_NEAR (norm (r, "osc3.tune"), -5.0, 1e-3);
    CHECK (entryWith (r, "multisample_reduced_to_one_zone") >= 0);
    // the impulse of the convolution reverb sits in instance 0
    CHECK_EQ (r.impulses.size(), size_t { 1 });
    CHECK_EQ (r.impulses[0].numFrames, 3000);
    CHECK_EQ (norm (r, "convolve.ir"), 1.0f);
    CHECK_EQ (norm (r, "convolve.on"), 1.0f);
    // the Wet knob 40 % goes through the measured mix law of the convolution (A518: dry 0.88 / wet 0.35 at 40 %), not 1:1
    double lawWet = 0.0, lawLevel = 0.0;
    CHECK (serumimport::fxMixRemap (kFxTypeConvolve, 0.4, 0.0, lawWet, lawLevel));
    CHECK_NEAR (norm (r, "convolve.mix"), 100.0 * lawWet, 1e-2);
    CHECK_EQ (norm (r, "convolve.law"), 1.0f);
}

// GROUPC (WP-13, A427 .. A434): the multisample oscillator keeps the instrument: the SFZ with all its zones, the samples as 24-bit WAV files, the envelope override, the velocity
// tracking and the timbre of the block; a sample the Serum content does not hold is counted, the zone stays in the SFZ
TEST (serum_multisample_oscillator_keeps_the_whole_instrument)
{
    MemResolver res;
    std::vector<float> tone (4410);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
    res.files["Multisamples/Factory/Keys/Zone Samples/zone C1.flac"] = wavFloat (tone); // (the decoder goes by the content, not by the name)
    res.files["Multisamples/Factory/Keys/Zone Samples/zone C3.wav"] = wavFloat (tone);
    res.files["Multisamples/Factory/Keys/Zone Samples/../../../Samples/Factory/shared.wav"] = wavFloat (tone); // (the resolver of this fixture does not normalise the path)
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", map (withText ({ { "kParamEnable", 1 } }, { { "kParamType", "kOsc_MultiSample" } })) },
                                 { "MultiSampleOsc0",
                                   map ({ { "sfzPathRelative", text ("Factory/Keys/Zone.sfz") },
                                          { "embedded_sfz", text ("// SFZ\n<control> default_path=Zone Samples\\\n<group> amp_veltrack=40 ampeg_attack=0.01\n"
                                                                   "<region> hikey=40 pitch_keycenter=36 sample=zone C1.flac\n"
                                                                   "<region> lokey=41 hikey=72 pitch_keycenter=60 tune=-5 sample=zone C3.wav\n"
                                                                   "<region> lokey=73 hikey=127 pitch_keycenter=84 sample=../../../Samples/Factory/shared.wav\n"
                                                                   "<region> lokey=0 hikey=0 sample=not there.flac\n") },
                                          { "plainParams", map (withText ({ { "kParamEnvOverride", 1 }, { "kParamEnvDelay", 0.01 }, { "kParamEnvAttack", 0.2 }, { "kParamEnvHold", 0.05 }, { "kParamEnvDecay", 0.4 },
                                                                            { "kParamEnvSustain", 0.5 }, { "kParamEnvRelease", 1.5 }, { "kParamVelTrack", 25 }, { "kParamVelTrackOverride", 1 },
                                                                            { "kParamTimbreShift", -0.5 }, { "kParamRandomPhase", 12 } }, {})) },
                                          { "files", map ({ { "zone C3.wav", map ({ { "numChannels", num (1) } }) } }) },
                                          { "defaultLoopMode", text ("no_loop") } }) } }));
    const SerumImportResult r = run (p.file(), &res);
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (value (r, "osc1.type"), 4.0f);
    CHECK_EQ (r.instruments.size(), static_cast<size_t> (1));
    CHECK_EQ (r.samples.size(), static_cast<size_t> (0)); // no single sample: the instrument is the content
    const ImportedInstrument& instrument = r.instruments[0];
    CHECK_EQ (instrument.osc, 1);
    CHECK_EQ (instrument.zones, 4);
    CHECK_EQ (instrument.files.size(), static_cast<size_t> (3));
    CHECK_EQ (instrument.missingFiles, 1);
    CHECK_EQ (r.missingFiles, 1);
    CHECK_EQ (instrument.files[0].first, std::string ("Zone Samples/zone C1.wav"));       // default_path applied, FLAC name becomes WAV
    CHECK_EQ (instrument.files[2].first, std::string ("Samples/Factory/shared.wav"));     // ../ never climbs out of the instrument folder
    CHECK (instrument.sfzText.find ("<region>") != std::string::npos && instrument.files[0].second.size() > 44);
    const auto sfz = multisample::parseSfz (instrument.sfzText);
    CHECK_EQ (sfz.regions.size(), static_cast<size_t> (4));
    CHECK_EQ (sfz.regions[0].sample, std::string ("Zone Samples/zone C1.wav"));
    CHECK_EQ (sfz.regions[1].center, 60);
    CHECK_NEAR (sfz.regions[1].tuneCents, -5.0, 1.0e-6);
    CHECK_NEAR (sfz.regions[1].velTrack, 40.0, 1.0e-6);
    CHECK_NEAR (sfz.regions[1].attack, 0.01, 1.0e-6);
    CHECK_EQ (sfz.regions[2].lokey, 73);
    // the keys of the block
    CHECK_EQ (value (r, "osc1.multi.override"), 1.0f);
    CHECK_NEAR (value (r, "osc1.multi.delay"), 10.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.multi.attack"), 200.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.multi.hold"), 50.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.multi.decay"), 400.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.multi.sustain"), 50.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.multi.release"), 1500.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.multi.vel_track"), 25.0, 1.0e-3);
    CHECK_EQ (value (r, "osc1.multi.vel_override"), 1.0f);
    CHECK_NEAR (value (r, "osc1.multi.timbre"), -50.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.random_phase"), 12.0, 1.0e-3);
    CHECK (entryWith (r, "multisample_instrument_stored_in_the_library") >= 0);
    CHECK (entryWith (r, "multisample_samples_not_found") >= 0);
}

TEST (serum_single_cycle_sine_table_is_the_builtin_sine_and_missing_files_fall_back)
{
    MemResolver res;
    std::vector<float> sine (2048);
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = static_cast<float> (std::sin (2.0 * 3.14159265358979 * static_cast<double> (i) / 2048.0));
    res.files["Tables/Analog/sine.wav"] = wavFloat (sine);
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", plain ({}) }, { "WTOsc0", map ({ { "relativePathToWT", text ("Analog/sine.wav") } }) } }));
    p.set ("Oscillator1", map ({ { "plainParams", plain ({ { "kParamEnable", 1 } }) }, { "WTOsc1", map ({ { "relativePathToWT", text ("Analog/gone.wav") } }) } }));
    p.set ("Oscillator2", map ({ { "plainParams", map (withText ({ { "kParamEnable", 1 } }, { { "kParamType", "kOsc_Sample" } })) },
                                 { "SampleOsc2", map ({ { "samplePathRelative", text ("Factory/gone.flac") } }) } }));
    const SerumImportResult r = run (p.file(), &res);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (norm (r, "osc1.wavetable"), 0.0f); // Sine
    CHECK (r.wavetables.empty());
    CHECK_EQ (norm (r, "osc2.wavetable"), 1.0f); // the Saw default
    CHECK_EQ (norm (r, "osc3.type"), 0.0f);      // no sample: back to the wavetable oscillator
    CHECK_EQ (r.missingFiles, 2);
    // without a resolver everything referenced is missing, nothing crashes
    const SerumImportResult none = run (p.file());
    CHECK_EQ (none.report.unaccounted, 0);
    CHECK_EQ (none.missingFiles, 3);
}

TEST (serum_routing_global_mpe_and_curves)
{
    Preset p;
    p.set ("RoutingSlot0", map ({ { "plainParams", map (withText ({ { "kParamFilterBalance", 0 }, { "kParamFXBus1Level", 25 } }, { { "kParamRoutingDest", "kRoutingDestFilter" } })) } }));
    p.set ("RoutingSlot1", map ({ { "plainParams", map (withText ({ { "kParamFXBus2Level", 60 } }, { { "kParamRoutingDest", "kRoutingDestDirect" } })) } }));
    p.set ("RoutingSlot5", map ({ { "plainParams", map (withText ({}, { { "kParamRoutingDest", "kRoutingDestNone" } })) } }));
    p.set ("Global0", map ({ { "plainParams", plain ({ { "kParamMasterVolume", 0.25 }, { "kParamPolyCount", 8 }, { "kParamBendRangeUp", 12 }, { "kParamBendRangeDn", -7 }, { "kParamMonoToggle", 1 },
                                                      { "kParamPortamentoTime", 0.5 }, { "kParamGlobalTuning", 432 } }) } }));
    p.set ("scalars", map ({ { "velo", map ({ { "curveVals", nums ({ 0.5, 0.5, 0.5 }) }, { "legato", boolean (true) }, { "numPoints", num (2) }, { "xVals", nums ({ 0, 0.5, 1 }) }, { "yVals", nums ({ 1, 0.8, 0 }) } }) },
                             { "note", map ({ { "curveVals", nums ({ 0.5, 0.5 }) }, { "legato", boolean (false) }, { "numPoints", num (1) }, { "xVals", nums ({ 0, 1 }) }, { "yVals", nums ({ 1, 0 }) } }) } }));
    p.set ("mpeEnabled", boolean (true));
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (norm (r, "osc1.destination"), 5.0f);
    CHECK_NEAR (norm (r, "osc1.filter_blend"), 50.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.bus1_send"), 100.0 * 0.25 * 0.25 * 0.25, 1e-3);   // the send law of Serum is cubic (measured against Serum 2.1.5)
    CHECK_EQ (norm (r, "osc2.destination"), 4.0f);
    CHECK_NEAR (norm (r, "osc2.bus2_send"), 100.0 * 0.6 * 0.6 * 0.6, 1e-3);
    CHECK_EQ (norm (r, "filter1.destination"), 2.0f);
    CHECK_NEAR (norm (r, "master.volume"), 20.0 * std::log10 (0.25) + 2.98, 1e-2); // the gain staging against Serum 2.1.5 (serum_map.cpp, kMasterTrimDb)
    CHECK_EQ (norm (r, "master.polyphony"), 1.0f); // mono toggle wins
    CHECK_EQ (norm (r, "master.bend_range"), 12.0f);
    CHECK_EQ (norm (r, "master.bend_down_range"), 7.0f);
    CHECK_EQ (norm (r, "master.bend_down_link"), 0.0f);
    CHECK_NEAR (norm (r, "master.portamento.time"), 500.0, 1e-2);
    CHECK_NEAR (norm (r, "master.tuning.a4"), 432.0, 1e-3);
    CHECK_EQ (norm (r, "master.mpe.on"), 1.0f);
    CHECK_EQ (norm (r, "curve.velocity.legato"), 1.0f);
    CHECK_EQ (r.velocityCurve.size(), size_t { 3 });
    CHECK_NEAR (r.velocityCurve[1].y, 0.2, 1e-6);
    CHECK (r.noteCurve.empty()); // the identity
}

TEST (serum_lfo_point_modulation_and_path_lfo)
{
    Preset p;
    p.set ("LFO0", map ({ { "curveData", curveMap ({ 0, 0.5, 1 }, { 1, 0.2, 1 }, { 0.5, 0.5, 0.5 }) }, { "plainParams", plain ({ { "kParamBeatSync", 0 }, { "kParamRate", 3 } }) } }));
    p.set ("LFO1", map ({ { "pathData", map ({ { "isOpen", boolean (true) }, { "xVals", nums ({ 0, 1 }) }, { "yVals", nums ({ 0, 1 }) }, { "curveVals", nums ({ 0.5, 0.5 }) }, { "numPoints", num (1) } }) },
                          { "plainParams", map (withText ({}, { { "kParamType", "Path" } })) } }));
    p.set ("LFOPointModBus0", map ({ { "plainParams", dflt() } }));
    p.set ("ModSlot0", map ({ { "source", nums ({ 25, 0 }) }, { "destModuleTypeString", text ("LFOPointModBus") }, { "destModuleID", num (0) }, { "destModuleParamName", text ("kParamValue") },
                              { "destModuleParamID", num (0) }, { "plainParams", plain ({ { "kParamAmount", 50 }, { "kParamBipolar", 1 } }) } }));
    p.set ("ModSlot1", map ({ { "source", nums ({ 7, 0 }) }, { "destModuleTypeString", text ("VoiceFilter") }, { "destModuleID", num (0) }, { "destModuleParamName", text ("kParamFreq") },
                              { "destModuleParamID", num (3) }, { "plainParams", plain ({ { "kParamAmount", 20 } }) } })); // LFO 2 is a path LFO: no source
    p.set ("lfoPointModAssignments", arr ({ map ({ { "busID", num (0) }, { "lfoID", num (0) }, { "lfoType", num (0) }, { "pointID", num (1) }, { "target", num (1) } }) }));
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (r.lfoShapes.size(), size_t { 1 });
    CHECK (r.lfoShapes[0].points[1].modY.present);
    CHECK_EQ (r.lfoShapes[0].points[1].modY.source, std::string ("macro1"));
    CHECK_NEAR (r.lfoShapes[0].points[1].modY.amount, 0.5, 1e-6);
    CHECK_EQ (r.values.plain[modSlotParams (1).source], 0.0f);
    CHECK (entryWith (r, "path_lfo_not_modelled") >= 0);
    CHECK (entryWith (r, "point_curve_modulation_not_modelled") < 0);
}

// REMAINDER item 4 (S13, A456 / A457): the curve target of the point modulation (target 2) and the swing flag of an LFO
TEST (serum_lfo_point_curve_modulation_and_swing_flag)
{
    Preset p;
    p.set ("LFO0", map ({ { "curveData", curveMap ({ 0, 0.5, 1 }, { 1, 0.2, 1 }, { 0.5, 0.5, 0.5 }) }, { "plainParams", plain ({ { "kParamBeatSync", 1 }, { "kParamRate", 3 }, { "kParamSwing", 1 } }) } }));
    p.set ("LFO1", map ({ { "plainParams", plain ({ { "kParamBeatSync", 1 }, { "kParamRate", 3 }, { "kParamSwing", 0 } }) } }));
    p.set ("Global0", map ({ { "plainParams", plain ({ { "kParamSwing", 62.5 } }) } }));
    p.set ("LFOPointModBus0", map ({ { "plainParams", dflt() } }));
    p.set ("ModSlot0", map ({ { "source", nums ({ 25, 0 }) }, { "destModuleTypeString", text ("LFOPointModBus") }, { "destModuleID", num (0) }, { "destModuleParamName", text ("kParamValue") },
                              { "destModuleParamID", num (0) }, { "plainParams", plain ({ { "kParamAmount", 50 }, { "kParamBipolar", 1 } }) } }));
    p.set ("lfoPointModAssignments", arr ({ map ({ { "busID", num (0) }, { "lfoID", num (0) }, { "lfoType", num (0) }, { "pointID", num (1) }, { "target", num (2) } }) }));
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (r.lfoShapes.size(), size_t { 1 });
    CHECK (r.lfoShapes[0].points[1].modCurve.present && ! r.lfoShapes[0].points[1].modY.present && ! r.lfoShapes[0].points[1].modX.present);
    CHECK_EQ (r.lfoShapes[0].points[1].modCurve.source, std::string ("macro1"));
    CHECK_NEAR (r.lfoShapes[0].points[1].modCurve.amount, 0.5, 1e-6);
    CHECK (entryWith (r, "point_curve_modulation") >= 0 && entryWith (r, "point_curve_modulation_not_modelled") < 0);
    // the flag takes the global swing: 62.5 of 50 .. 75 is half of the range, so 50 % here; LFO 2 has the flag off
    CHECK_NEAR (value (r, "lfo1.swing"), 50.0, 1e-3);
    CHECK_EQ (value (r, "lfo2.swing"), 0.0f);
    CHECK (entryWith (r, "lfo_swing_flag_takes_the_global_swing") >= 0 && entryWith (r, "lfo_swing_flag_not_modelled") < 0);
}

// REMAINDER item 4 (S4): the poles and zeros of a PZ SVF filter become its response curve
TEST (serum_pz_svf_poles_and_zeros_become_the_response_curve)
{
    const auto set = [] (double gain, double fp, double fz, double qp, double qz, double type)
    {
        std::vector<double> fps (8), fzs (8), qps (8), qzs (8), types (8, 2.0);
        for (int i = 0; i < 8; ++i) { fps[i] = fzs[i] = (i + 0.5) / 8.0; qps[i] = qzs[i] = 0.5; }
        fps[3] = fp; fzs[3] = fz; qps[3] = qp; qzs[3] = qz; types[3] = type;
        return map ({ { "fp", nums (fps) }, { "fz", nums (fzs) }, { "qp", nums (qps) }, { "qz", nums (qzs) }, { "gain", num (gain) }, { "types", nums (types) } });
    };
    const auto filter = [&] (const char* typeName, const Bytes& sets)
    {
        Map plainMap = withText ({ { "kParamX", 0.0 }, { "kParamY", 0.0 } }, { { "kParamType", typeName } });
        return map ({ { "plainParams", map (plainMap) }, { "PZs", sets } });
    };
    Preset p;
    const Bytes four = arr ({ set (0.5, 0.5, 0.5, 0.8, 0.2, 6), set (0.5, 0.5, 0.5, 0.5, 0.5, 2), set (0.5, 0.5, 0.5, 0.5, 0.5, 2), set (0.5, 0.5, 0.5, 0.5, 0.5, 2) });
    p.set ("VoiceFilter0", filter ("PZ_SVF", four));
    p.set ("VoiceFilter1", filter ("H24", four));   // the poles and zeros of a filter that is not a PZ SVF are not used
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (r.filterCurves.size(), size_t { 1 });
    if (r.filterCurves.size() == 1)
    {
        const auto& c = r.filterCurves[0];
        CHECK_EQ (c.filter, std::string ("filter1"));
        CHECK (c.points.size() >= 2 && c.points.size() <= static_cast<size_t> (kMaxFilterCurvePoints));
        float peak = -100.0f, at = 0.0f;
        for (const auto& pt : c.points)
            if (pt.y > peak) { peak = pt.y; at = pt.x; }
        CHECK (peak > 15.0f && peak < 40.0f);              // a pole at Q 20 over a zero at Q 1.3 on the same frequency: about +24 dB
        CHECK (std::fabs (at - 0.4375f) < 0.12f);          // the fourth pair sits at x = 3.5 / 8 of the log frequency axis
        CHECK (c.points.front().y > -1.0f && c.points.front().y < 1.0f && c.points.back().y > -1.0f && c.points.back().y < 1.0f);   // the other pairs are neutral
    }
    CHECK (entryWith (r, "pz_svf_four_sets_blended_as_a_response_curve") >= 0 && entryWith (r, "pz_set_of_a_filter_that_is_not_a_pz_svf") >= 0);
    CHECK (entryWith (r, "pz_svf_poles_and_zeros_not_imported") < 0);
    // a Single file: the curve is the result of the file; the sets that are all neutral give a flat curve
    const Bytes file = container ("XferFilterPZSingle", map ({ { "PZ", set (0.5, 0.5, 0.5, 0.2, 0.9, 5) } }), "Notch");
    const SerumImportResult single = run (file);
    CHECK (single.kind == SerumFileKind::filterPz);
    CHECK_EQ (single.report.unaccounted, 0);
    CHECK (single.pzCurve.points.size() >= 2);
    float low = 100.0f;
    for (const auto& pt : single.pzCurve.points)
        low = std::min (low, pt.y);
    CHECK (low < -10.0f);   // a notch pair (zero Q above the pole Q)
}

TEST (serum_report_json_lines_and_serialized_preset)
{
    MemResolver res;
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", plain ({ { "kParamVolume", 0.7 } }) },
                                 { "WTOsc0", map ({ { "embeddedWTData", nums (ramp (2048 * 2)) }, { "plainParams", plain ({ { "kParamTablePos", 100 } }) } }) } }));
    p.set ("LFO0", map ({ { "curveData", curveMap ({ 0, 0.5, 1 }, { 1, 0, 1 }, { 0.5, 0.5, 0.5 }) }, { "plainParams", plain ({ { "kParamBeatSync", 0 }, { "kParamRate", 1 } }) } }));
    p.set ("Macro0", map ({ { "name", text ("WIDTH") }, { "plainParams", plain ({ { "kParamValue", 25 } }) } }));
    const SerumImportResult r = run (p.file ("My Patch"), &res);
    const std::string json = r.report.toJson();
    CHECK (json.find ("\"format\":\"synthyboy-serum-import-report\"") != std::string::npos);
    CHECK (json.find ("\"unaccounted\":0") != std::string::npos);
    CHECK (! serumReportLines (r.report).empty() || r.report.numApproximated + r.report.numDropped == 0);
    ContentRefs refs;
    refs.wavetables.push_back ({ 1, "user.wavetables", "abc/Table.wav" });
    PresetMeta meta = r.meta;
    const std::string text = serializeSerumPreset (r, meta, refs, nullptr);
    CHECK (! text.empty());
    CHECK (text.find ("\"import\":{\"source\":{\"format\":\"serum\"") != std::string::npos);
    PresetMeta back;
    ParamValues values = ParamValues::defaults();
    StateContent content;
    CHECK (loadPreset (text.data(), text.size(), back, values, content).status == LoadStatus::ok);
    CHECK_EQ (back.name, std::string ("My Patch"));
    CHECK_EQ (content.wavetables.size(), size_t { 1 });
    CHECK_EQ (content.lfoShapes.size(), size_t { 1 });
    CHECK (validateContent (content));
    CHECK_NEAR (values.plain[static_cast<size_t> (findParamIndex (hashKey ("osc1.level")))], 70.0, 1e-3);
}

TEST (serum_other_file_kinds_and_the_old_format)
{
    // a shape
    const Bytes shape = container ("XferShape", map ({ { "LFO", curveMap ({ 0, 0.5, 1 }, { 1, 0, 1 }, { 0.5, 0.3, 0.5 }) } }), "tri");
    const SerumImportResult s = run (shape);
    CHECK (s.status == ImportStatus::ok && s.kind == SerumFileKind::shape);
    CHECK_EQ (s.report.unaccounted, 0);
    CHECK_EQ (s.shape.points.size(), size_t { 3 });
    CHECK_NEAR (s.shape.points[0].power, (0.5 - 0.5) * 11.0, 1e-6);
    CHECK_NEAR (s.shape.points[1].power, (0.3 - 0.5) * 11.0, 1e-5);
    // an effect module and a rack
    const Bytes module = container ("SerumFX", map ({ { "FXPhaser", map ({ { "lfophasor", num (0.5) }, { "plainParams", plain ({ { "kParamRate", 1.5 }, { "kParamWet", 60 } }) } }) },
                                                       { "fxType", text ("FXPhaser") }, { "type", num (2) } }), "Phaser");
    const SerumImportResult m = run (module);
    CHECK (m.status == ImportStatus::ok && m.kind == SerumFileKind::fxModule);
    CHECK_EQ (m.report.unaccounted, 0);
    CHECK (! m.touched.empty());
    CHECK_EQ (fxItemType (m.values.plain, fxRacks (m.values.plain).rack[0].back()), kFxTypePhaser);
    // families with no equivalent
    const Bytes arp = container ("XferArp", map ({ { "ArpClip", map ({ { "name", text ("x") }, { "plainParams", plain ({ { "kParamRate", 5 } }) } }) } }), "Arp");
    const SerumImportResult a = run (arp);
    CHECK (a.status == ImportStatus::ok && a.kind == SerumFileKind::arp);
    CHECK_EQ (a.report.unaccounted, 0);
    CHECK (a.report.numDropped >= 1);
    // the old format: header, size, format 0, a zlib stream (stored block) of a UBJSON body
    Bytes ub = { '{', 'i', 6, 'f', 'x', 'T', 'y', 'p', 'e', 'S', 'i', 7, 'F', 'X', 'D', 'e', 'l', 'a', 'y', 'i', 4, 't', 'y', 'p', 'e', 'i', 3 };
    const std::string key = "FXDelay";
    ub = { '{' };
    const auto addKey = [&] (const std::string& k) { ub.push_back ('i'); ub.push_back (static_cast<uint8_t> (k.size())); ub.insert (ub.end(), k.begin(), k.end()); };
    addKey ("FXDelay");
    ub.push_back ('{'); addKey ("plainParams"); ub.push_back ('{'); addKey ("kParamWet"); ub.push_back ('D');
    const double wet = 35.0;
    uint64_t u;
    std::memcpy (&u, &wet, 8);
    for (int i = 7; i >= 0; --i) ub.push_back (static_cast<uint8_t> (u >> (8 * i)));
    ub.push_back ('}'); ub.push_back ('}');
    addKey ("fxType"); ub.push_back ('S'); ub.push_back ('i'); ub.push_back (7); ub.insert (ub.end(), key.begin(), key.end());
    addKey ("type"); ub.push_back ('i'); ub.push_back (4);
    ub.push_back ('}');
    Bytes z = { 0x78, 0x01, 0x01, static_cast<uint8_t> (ub.size()), static_cast<uint8_t> (ub.size() >> 8), static_cast<uint8_t> (~ub.size()), static_cast<uint8_t> (~(ub.size() >> 8)) };
    z.insert (z.end(), ub.begin(), ub.end());
    z.insert (z.end(), { 0, 0, 0, 0 }); // adler32 is not checked
    std::string json = "{\"fileType\":\"SerumFX\",\"fxType\":\"FXDelay\",\"product\":\"Serum2FX\",\"version\":3.0}";
    Bytes old = { 'X', 'f', 'e', 'r', 'J', 's', 'o', 'n', 0 };
    const size_t jl = json.size() + 1; // the old files count a NUL
    for (int i = 0; i < 8; ++i) old.push_back (static_cast<uint8_t> (static_cast<uint64_t> (jl) >> (8 * i)));
    old.insert (old.end(), json.begin(), json.end());
    old.push_back (0);
    for (int i = 0; i < 4; ++i) old.push_back (static_cast<uint8_t> (ub.size() >> (8 * i)));
    old.insert (old.end(), { 0, 0, 0, 0 });
    old.insert (old.end(), z.begin(), z.end());
    const SerumImportResult o = run (old);
    CHECK (o.status == ImportStatus::ok && o.kind == SerumFileKind::fxModule);
    CHECK_EQ (o.report.unaccounted, 0);
    CHECK_EQ (fxItemType (o.values.plain, fxRacks (o.values.plain).rack[0].back()), kFxTypeDelay);
}

TEST (serum_rejects_foreign_cut_and_hostile_input_without_crashing)
{
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", plain ({ { "kParamVolume", 0.5 } }) } }));
    const Bytes good = p.file();
    CHECK (run (good).status == ImportStatus::ok);
    const char* junk = "this is not a serum file";
    CHECK (importSerumFile (junk, std::strlen (junk), {}).status == ImportStatus::notVital);
    CHECK (importSerumFile (junk, 0, {}).status == ImportStatus::notVital);
    // every prefix of a good file
    for (size_t n = 0; n < good.size(); n += 3)
    {
        const SerumImportResult r = importSerumFile (good.data(), n, {});
        CHECK (r.status != ImportStatus::ok || n == good.size());
    }
    // single byte flips never crash (the status is whatever it is)
    std::mt19937 rng (7);
    for (int i = 0; i < 400; ++i)
    {
        Bytes b = good;
        b[rng() % b.size()] ^= static_cast<uint8_t> (1u << (rng() % 8));
        const SerumImportResult r = run (b);
        CHECK (r.report.unaccounted == 0 || r.status != ImportStatus::ok);
    }
    // limits
    SerumImportOptions small;
    small.limits.maxFileBytes = 16;
    CHECK (importSerumFile (good.data(), good.size(), small).status == ImportStatus::tooLarge);
    SerumImportOptions cancel;
    cancel.cancel = [] { return true; };
    CHECK (importSerumFile (good.data(), good.size(), cancel).status == ImportStatus::cancelled);
    SerumImportOptions memory;
    memory.limits.maxWorkingBytes = 64;
    CHECK (importSerumFile (good.data(), good.size(), memory).status == ImportStatus::budgetExceeded);
    // a body that claims a huge array
    Bytes bomb = { 0xA1, 0x61, 'x', 0x9B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK (run (container ("SerumPreset", bomb)).status != ImportStatus::ok);
    // wrong types where numbers are expected are reported, not trusted
    Preset q;
    q.set ("Oscillator0", map ({ { "plainParams", map ({ { "kParamVolume", text ("loud") }, { "kParamUnison", boolean (true) } }) } }));
    const SerumImportResult t = run (q.file());
    CHECK (t.status == ImportStatus::ok);
    CHECK_EQ (t.report.unaccounted, 0);
    CHECK (entryWith (t, "number_expected") >= 0);
}

TEST (serum_rule_tables_point_at_real_keys)
{
    CHECK (serumRuleCount() > 150);
    CHECK (! serumRuleDescription (0).empty());
    int checked = 0;
    for (const char* scope : { "Osc", "Warp", "WT", "Multi", "Noise", "Sub", "Filter", "Env", "Macro", "Global", "Routing" })
    {
        size_t n = 0;
        const serumimport::Rule* rules = serumimport::rulesOf (scope, n);
        for (size_t i = 0; i < n; ++i)
        {
            std::string key = rules[i].key;
            const size_t hash = key.find ('#');
            if (hash != std::string::npos) key.replace (hash, 1, "1");
            const int idx = findParamIndex (hashKey (key.c_str()));
            if (idx < 0) std::printf ("  rule %s/%s -> %s: no such key\n", rules[i].scope, rules[i].param, key.c_str());
            CHECK (idx >= 0);
            ++checked;
            if (rules[i].conv == serumimport::Conv::enumName && idx >= 0)
                for (int e = 0; e < rules[i].en->count; ++e)
                {
                    const ParamInfo& info = paramTable()[static_cast<size_t> (idx)];
                    if (rules[i].en->entries[e].value > static_cast<int> (info.max) || rules[i].en->entries[e].value < static_cast<int> (info.min))
                        std::printf ("  enum %s %s -> %d outside %s\n", rules[i].en->entries[e].name, key.c_str(), rules[i].en->entries[e].value, key.c_str());
                    CHECK (rules[i].en->entries[e].value <= static_cast<int> (info.max) && rules[i].en->entries[e].value >= static_cast<int> (info.min));
                }
        }
    }
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
    {
        size_t n = 0;
        const serumimport::Rule* rules = serumimport::fxRulesOf (t, n);
        const FxTypeDescriptor& desc = fxTypeDescriptor (t);
        for (size_t i = 0; i < n; ++i)
        {
            const int idx = findParamIndex (hashKey (rules[i].key));
            bool inDescriptor = false;
            for (int q = 0; q < desc.count; ++q) if (desc.source[q] == idx) inDescriptor = true;
            if (! inDescriptor) std::printf ("  fx rule %s -> %s is not a parameter of the pool type %s\n", rules[i].param, rules[i].key, fxTypeName (t));
            CHECK (inDescriptor);
            ++checked;
        }
    }
    CHECK (checked > 150);
    // every filter name maps to a (model, style) pair
    int model = 0, style = 0;
    serumimport::Fid fid;
    CHECK (serumimport::filterTypeFromName ("MgL6", model, style, fid) && model == 9 && style == 0);
    CHECK (serumimport::filterTypeFromName ("PZ_SVF", model, style, fid) && model == 21);
    CHECK (! serumimport::filterTypeFromName ("NoSuchFilter", model, style, fid));
    // the source map
    const char* name = "";
    serumimport::Fid f2;
    CHECK_EQ (serumimport::sourceChoiceOf (27, name, f2), 21);  // Serum id 27 is Macro 3 = our choice 21 (macro1 is 19)
    CHECK_EQ (serumimport::sourceChoiceOf (1, name, f2), 27);   // Mod Wheel
    CHECK_EQ (serumimport::sourceChoiceOf (51, name, f2), 45);  // OSC C as a source: the output of oscillator C (GROUPB WP-06)
    CHECK_EQ (serumimport::sourceChoiceOf (53, name, f2), 48);  // Filter 1
    CHECK_EQ (serumimport::sourceChoiceOf (55, name, f2), 50);  // Active Voices
    CHECK_EQ (serumimport::sourceChoiceOf (23, name, f2), 51);  // Note-On Alt. 1
    CHECK_EQ (serumimport::sourceChoiceOf (59, name, f2), 54);  // Note-On Rand (Discrete)
    CHECK_EQ (serumimport::sourceChoiceOf (39, name, f2), -1);  // LFO 1 Y: no home
}

namespace
{
std::string realDir()
{
    namespace fs = std::filesystem;
    if (const char* e = std::getenv ("SB_SERUM_PRESETS_DIR")) return e;
    std::error_code ec;
    if (fs::is_directory (SB_SERUM_PRESETS_DIR, ec)) return SB_SERUM_PRESETS_DIR;
    return SB_SERUM_PRESETS_MAIN;
}
} // namespace

TEST (serum_real_presets_import_without_a_failure_and_with_every_leaf_accounted)
{
    namespace fs = std::filesystem;
    const fs::path root = realDir();
    std::error_code ec;
    if (! fs::is_directory (root / "Presets", ec))
    {
        std::printf ("  (skipped: %s has no Presets folder)\n", root.string().c_str());
        return;
    }
    const std::string store = (fs::temp_directory_path() / "sb_serum_test_store").string();
    fs::remove_all (store, ec);
    UserContentStore content (store);
    SerumFolderResolver resolver (root.string());
    int files = 0, failed = 0, unaccounted = 0, validated = 0;
    long long dropped = 0, imported = 0;
    double slowest = 0.0;
    for (const auto& e : fs::recursive_directory_iterator (root / "Presets", fs::directory_options::skip_permission_denied, ec))
    {
        if (! e.is_regular_file() || e.path().extension() != ".SerumPreset") continue;
        std::ifstream in (e.path(), std::ios::binary);
        const std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        ++files;
        const auto t0 = std::chrono::steady_clock::now();
        const SerumPluginImport r = importSerumFileForPlugin (e.path().filename().string(), bytes, content, kCapUserWavetable | kCapSampleOsc | kCapNoiseSample | kCapCustomLfoShape, &resolver);
        slowest = std::max (slowest, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        if (! r.ok) { ++failed; std::printf ("  FAILED %s: %s\n", e.path().string().c_str(), r.error.c_str()); continue; }
        if (r.report.unaccounted != 0) { ++unaccounted; std::printf ("  unaccounted %d in %s\n", r.report.unaccounted, e.path().string().c_str()); }
        imported += r.report.numImported;
        dropped += r.report.numDropped;
        // the patch the plugin loads is valid
        if (validateContent (r.content) && r.values.plain[static_cast<size_t> (findParamIndex (hashKey ("master.volume")))] <= 6.0f) ++validated;
    }
    std::printf ("  %d presets, %d failed, %d with unaccounted leaves, %d valid patches, imported %lld, dropped %lld, slowest %.2f s\n", files, failed, unaccounted, validated, imported, dropped, slowest);
    fs::remove_all (store, ec);
    CHECK (files > 600);
    CHECK_EQ (failed, 0);
    CHECK_EQ (unaccounted, 0);
    CHECK_EQ (validated, files);
    if (! SBTEST_INSTRUMENTED) CHECK (slowest < 20.0);
}

// GROUPA2 (WP-07, A379 / A380): the voice panel of a preset and of a .SerumStyle file, and the sources Voice Mod 1 / 2 and Voice Index
TEST (serum_voice_panel_is_imported_into_the_voice_control_keys_and_the_voice_sources_into_the_matrix)
{
    Preset p;
    p.set ("VoicePanel0", map ({ { "displayName", text ("My Panel") },
                                 { "plainParams", map (withText ({ { "kParamVoiceCount", 3 }, { "kParamVoice1Pan", -40 }, { "kParamVoice2Detune", 12.5 }, { "kParamVoice3FilterCutoff", 75 },
                                                                   { "kParamVoice2EnvTime", -50 }, { "kParamVoice1Mod1", 30 }, { "kParamVoice3Mod2", -20 },
                                                                   { "kParamGlobalRandomOscPan", 25 }, { "kParamGlobalRandomOscDetune", 40 }, { "kParamGlobalRandomOscDetune10x", 1 },
                                                                   { "kParamGlobalRandomFilterCutoff", 10 }, { "kParamGlobalRandomEnvTime", 20 },
                                                                   { "kParamGlobalScalingEnvTime", 150 }, { "kParamGlobalScalingLfoTime", 50 }, { "kParamGlobalScalingLfoTimeSnap", 1 },
                                                                   { "kParamOscA", 0 }, { "kParamOscS", 0 }, { "kParamVoice4Wobble", 3 } },
                                                                 {})) } }));
    const auto slot = [] (int source, const char* param)
    {
        return map ({ { "source", nums ({ static_cast<double> (source), 0 }) }, { "destModuleTypeString", text ("Oscillator") }, { "destModuleID", num (0) },
                      { "destModuleParamName", text (param) }, { "destModuleParamID", num (0) }, { "plainParams", plain ({ { "kParamAmount", 50 } }) } });
    };
    p.set ("ModSlot0", slot (56, "kParamVolume"));
    p.set ("ModSlot1", slot (57, "kParamVolume"));
    p.set ("ModSlot2", slot (58, "kParamVolume"));
    p.set ("ModSlot3", slot (55, "kParamVolume")); // Active Voices: still no home
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (value (r, "voicectl.length"), 3.0f);
    CHECK_NEAR (value (r, "voicectl.s1.pan"), -40.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.s2.detune"), 12.5, 1e-3);
    CHECK_NEAR (value (r, "voicectl.s3.cutoff"), 75.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.s2.env"), -50.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.s1.mod1"), 30.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.s3.mod2"), -20.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.rand.pan"), 25.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.rand.detune"), 40.0, 1e-3);
    CHECK_EQ (value (r, "voicectl.rand.detune10x"), 1.0f);
    CHECK_NEAR (value (r, "voicectl.rand.cutoff"), 10.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.rand.env"), 20.0, 1e-3);
    CHECK_NEAR (value (r, "voicectl.scale.env"), 150.0, 1e-2);
    CHECK_NEAR (value (r, "voicectl.scale.lfo"), 50.0, 1e-2);
    CHECK_EQ (value (r, "voicectl.scale.lfo_mode"), 1.0f);
    CHECK_EQ (value (r, "voicectl.osc.a"), 0.0f);
    CHECK_EQ (value (r, "voicectl.osc.b"), 1.0f);
    CHECK_EQ (value (r, "voicectl.osc.sub"), 0.0f);
    CHECK_EQ (value (r, "voicectl.s4.pan"), 0.0f);                                   // not mentioned: the default
    CHECK (countKind (r, ItemKind::dropped, "VoicePanel0/plainParams/kParamVoice4Wobble") == 1); // a name we do not know is accounted, not lost
    CHECK (entryWith (r, "voice_panel_parameter_unknown") >= 0);
    CHECK (entryWith (r, "voice_panel_not_modelled") < 0);
    CHECK_EQ (r.values.plain[modSlotParams (0).source], 41.0f); // voice_mod1
    CHECK_EQ (r.values.plain[modSlotParams (1).source], 42.0f); // voice_mod2
    CHECK_EQ (r.values.plain[modSlotParams (2).source], 40.0f); // voice_index
    CHECK_EQ (r.values.plain[modSlotParams (3).source], 50.0f); // Active Voices (GROUPB WP-06)
}

TEST (serum_style_files_are_complete_voice_control_configurations)
{
    // "8 Voice Pan": two steps with a pan, the count absent (8), everything else the default
    {
        const Bytes style = container ("SerumStyle", map ({ { "VoicePanel0", map ({ { "plainParams", plain ({ { "kParamVoice3Pan", 50 }, { "kParamVoice6Pan", -50 } }) } }) } }), "8 Voice Pan");
        const SerumImportResult r = run (style);
        CHECK (r.status == ImportStatus::ok);
        CHECK (r.kind == SerumFileKind::style);
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (value (r, "voicectl.length"), 8.0f);
        CHECK_NEAR (value (r, "voicectl.s3.pan"), 50.0, 1e-3);
        CHECK_NEAR (value (r, "voicectl.s6.pan"), -50.0, 1e-3);
        CHECK_NEAR (value (r, "voicectl.s1.pan"), 0.0, 1e-3);
        // every voice control key is part of what the style writes onto the patch (the rest of the patch is not touched)
        int voice = 0, other = 0;
        for (const int i : r.touched)
            (std::string (paramTable()[static_cast<size_t> (i)].key).rfind ("voicectl.", 0) == 0 ? voice : other) += 1;
        CHECK_EQ (voice, 62);
        CHECK_EQ (other, 0);
        CHECK (r.report.numDropped == 0);
    }
    // "Init": plainParams "default" resets every key
    {
        const Bytes init = container ("SerumStyle", map ({ { "VoicePanel0", map ({ { "plainParams", dflt() } }) } }), "Init");
        const SerumImportResult r = run (init);
        CHECK (r.kind == SerumFileKind::style);
        CHECK_EQ (r.report.unaccounted, 0);
        int voice = 0;
        for (const int i : r.touched)
            voice += std::string (paramTable()[static_cast<size_t> (i)].key).rfind ("voicectl.", 0) == 0 ? 1 : 0;
        CHECK_EQ (voice, 62);
        CHECK_EQ (value (r, "voicectl.length"), 8.0f);
        CHECK_NEAR (value (r, "voicectl.scale.env"), 100.0, 1e-3);
    }
}

// GROUPA2 (WP-02, A386): the MIDI CC map of a preset and of a .SerumMIDIMap file (the macros are decoded, other ids are reported)
TEST (serum_midi_cc_maps_of_presets_and_map_files_become_bindings)
{
    const auto entryOf = [] (double cc, const std::vector<double>& ids) { return map ({ { "ccNum", num (cc) }, { "paramIDs", nums (ids) } }); };
    const Bytes list = arr ({ entryOf (14, { 7000000 }), entryOf (21, { 7007000 }), entryOf (30, { 3001000 }), entryOf (64, { 7001000 }), entryOf (40, {}) });
    {
        Preset p;
        p.set ("midiMap", list);
        const SerumImportResult r = run (p.file());
        CHECK (r.status == ImportStatus::ok);
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (r.midiMap.size(), size_t { 2 });
        CHECK_EQ (r.midiMap[0].first, 14);
        CHECK_EQ (r.midiMap[0].second, std::string ("macro.1"));
        CHECK_EQ (r.midiMap[1].first, 21);
        CHECK_EQ (r.midiMap[1].second, std::string ("macro.8"));
        CHECK (entryWith (r, "midi_map_entry") >= 0);
        CHECK_EQ (countKind (r, ItemKind::dropped, "midiMap"), 2 + 2 + 1); // dropped: the other module (ccNum + id), the sustain pedal (ccNum + id), a ccNum with an empty id list (the ccNum)
        CHECK (entryWith (r, "midi_cc_map_not_imported") < 0);
        CHECK (entryWith (r, "midi_map_cc_not_learnable") >= 0);   // CC 64 is the sustain pedal
        CHECK (entryWith (r, "midi_map_parameter_not_decoded") >= 0);
    }
    {
        const Bytes file = container ("SerumMIDIMap", map ({ { "midiMap", list } }), "Macros");
        const SerumImportResult r = run (file);
        CHECK (r.status == ImportStatus::ok);
        CHECK (r.kind == SerumFileKind::midiMap);
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (r.midiMap.size(), size_t { 2 });
    }
}

// GROUPB (WP-05, A398 / A399): the ratio source, the table crossfade mode (smooth interpolation) and the single slice destination have keys
TEST (serum_ratio_source_smooth_interpolation_and_single_slice_destination_have_keys)
{
    const auto wtBlock = [] (double xfade)
    {
        return map ({ { "embeddedWTData", nums (ramp (4096)) }, { "numFrames", num (4096) }, { "numChannels", num (1) }, { "plainParams", plain ({ { "kParamTablePos", 1 }, { "kParamXfadeMode", xfade } }) } });
    };
    Preset p;
    // OSC A: Ratio mode with the source Sub (4); OSC B: Ratio with Osc A (0); OSC C: Ratio with itself (2) and the table crossfade mode on
    p.set ("Oscillator0", map ({ { "plainParams", map (withText ({ { "kParamPitchRatio", 4 }, { "kParamPitchSource", 4 } }, { { "kParamPitchMode", "Ratio" } })) }, { "WTOsc0", wtBlock (0) } }));
    p.set ("Oscillator1", map ({ { "plainParams", map (withText ({ { "kParamPitchRatio", 2 }, { "kParamPitchSource", 0 } }, { { "kParamPitchMode", "Ratio" } })) } }));
    p.set ("Oscillator2", map ({ { "plainParams", map (withText ({ { "kParamPitchRatio", 2 }, { "kParamPitchSource", 2 } }, { { "kParamPitchMode", "Ratio" } })) }, { "WTOsc2", wtBlock (1) } }));
    p.set ("ModSlot0", map ({ { "source", nums ({ 6, 0 }) }, { "destModuleTypeString", text ("Oscillator") }, { "destModuleID", num (1) }, { "destModuleParamName", text ("kParamSlicingSingleSlice") },
                              { "destModuleParamID", num (0) }, { "plainParams", plain ({ { "kParamAmount", 50 } }) } }));
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (value (r, "osc1.ratio_src"), 4.0f);   // Sub
    CHECK_EQ (value (r, "osc2.ratio_src"), 1.0f);   // Osc 1
    CHECK_EQ (value (r, "osc3.ratio_src"), 0.0f);   // itself: the note
    CHECK_EQ (value (r, "osc1.smooth_interp"), 0.0f);
    CHECK_EQ (value (r, "osc3.smooth_interp"), 1.0f);
    const ModSlotParams s0 = modSlotParams (0);
    CHECK_EQ (r.values.plain[s0.destination], static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (hashKey ("osc2.sample.slice"))))));
    CHECK (entryWith (r, "ratio_source_noise_not_modelled") < 0);

    // outside the Ratio mode the source is read and ignored; the Noise has no home
    Preset q;
    q.set ("Oscillator0", map ({ { "plainParams", map (withText ({ { "kParamPitchSource", 1 } }, { { "kParamPitchMode", "Harmonics" } })) } }));
    q.set ("Oscillator1", map ({ { "plainParams", map (withText ({ { "kParamPitchRatio", 2 }, { "kParamPitchSource", 3 } }, { { "kParamPitchMode", "Ratio" } })) } }));
    const SerumImportResult s = run (q.file());
    CHECK_EQ (s.report.unaccounted, 0);
    CHECK_EQ (value (s, "osc1.ratio_src"), 0.0f);
    CHECK_EQ (value (s, "osc2.ratio_src"), 0.0f);
    CHECK (entryWith (s, "ratio_source_noise_not_modelled") >= 0);
    CHECK (entryWith (s, "ratio_source_outside_ratio_mode") >= 0);
}

// GROUPC (WP-11, A412 .. A418): the granular oscillator keeps its type and its parameters (density and grain length with their modes, window, randomisations, X|Y axis, manual position)
TEST (serum_granular_oscillator_has_a_type_and_keys)
{
    const auto granularBlock = [] (std::vector<std::pair<std::string, double>> numbers, const std::vector<std::pair<std::string, std::string>>& texts)
    {
        return map ({ { "plainParams", map (withText (numbers, texts)) } });
    };
    Preset p;
    // OSC A: Free density in Hz, no length mode (percent of the period), hann default, randomisations, the dot placed by hand
    p.set ("Oscillator0", map ({ { "plainParams", map (withText ({ { "kParamManualPositionMode", 1 }, { "kParamPosition", 35 } }, { { "kParamType", "kOsc_Granular" } })) },
                                 { "GranularOsc0", granularBlock ({ { "kParamDensity", 42.5 }, { "kParamGrainLength", 2.0 }, { "kParamWindowParam", 30 }, { "kParamWindowSkew", -20 }, { "kParamRandomOffset", 12 },
                                                                   { "kParamRandomPitch", 0.5 }, { "kParamRandomGain", 40 }, { "kParamRandomWarp2", 10 } },
                                                                 { { "kParamWindowShape", "kWindowTukey" }, { "kParamUnisonTrigPattern", "kExponential" }, { "kParamYAxisAssignment", "kYAxisOscVolume" } }) } }));
    // OSC B: BPM density (2 beats) and BPM length (0.25 beats); OSC C: Grains density
    p.set ("Oscillator1", map ({ { "plainParams", map (withText ({}, { { "kParamType", "kOsc_Granular" } })) },
                                 { "GranularOsc1", granularBlock ({ { "kParamDensity", 2.0 }, { "kParamGrainLength", 0.25 } }, { { "kParamDensityMode", "kDensityBPM" }, { "kParamLengthMode", "kLengthBPM" } }) } }));
    p.set ("Oscillator2", map ({ { "plainParams", map (withText ({}, { { "kParamType", "kOsc_Granular" } })) },
                                 { "GranularOsc2", granularBlock ({ { "kParamDensity", 6.4 }, { "kParamGrainLength", 0.0 } }, { { "kParamDensityMode", "kDensityGrains" } }) } }));
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    for (int n = 1; n <= 3; ++n)
        CHECK_EQ (value (r, ("osc" + std::to_string (n) + ".type").c_str()), 2.0f);
    CHECK_EQ (value (r, "osc1.gran.density_mode"), 0.0f);
    CHECK_NEAR (value (r, "osc1.gran.density"), 42.5, 1.0e-3);
    CHECK_EQ (value (r, "osc1.gran.length_mode"), 2.0f);          // no mode in the file: percent of the density period (A413)
    CHECK_NEAR (value (r, "osc1.gran.length_percent"), 200.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.gran.window_amount"), 30.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.gran.window_skew"), -20.0, 1.0e-3);
    CHECK_EQ (value (r, "osc1.gran.window_shape"), 5.0f);          // Tukey
    CHECK_EQ (value (r, "osc1.gran.spawn_pattern"), 2.0f);         // Exp
    CHECK_EQ (value (r, "osc1.gran.y_axis"), 1.0f);                // Level
    CHECK_NEAR (value (r, "osc1.gran.rand_offset"), 12.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.gran.rand_pitch"), 0.5, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.gran.rand_level"), 40.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.gran.rand_warp2"), 10.0, 1.0e-3);
    CHECK_EQ (value (r, "osc1.gran.manual"), 1.0f);
    CHECK_NEAR (value (r, "osc1.gran.position"), 35.0, 1.0e-3);
    CHECK_EQ (value (r, "osc2.gran.density_mode"), 1.0f);
    CHECK_EQ (value (r, "osc2.gran.density_sync"), 3.0f);          // 2 beats = a 1/2 note: entry 3 of the divisions (1/1, 1/1T, 1/1., 1/2, ...)
    CHECK_EQ (value (r, "osc2.gran.length_mode"), 1.0f);
    CHECK_EQ (value (r, "osc2.gran.length_sync"), 12.0f);          // a quarter of a beat = a 1/16 note: entry 12
    CHECK_EQ (value (r, "osc3.gran.density_mode"), 2.0f);
    CHECK_EQ (value (r, "osc3.gran.density_grains"), 6.0f);
    CHECK_NEAR (value (r, "osc3.gran.length_percent"), 5.0, 1.0e-3);   // 0 x 100 % clamps to the 5 % minimum
    CHECK (entryWith (r, "granular_density_beats_to_the_nearest_division") >= 0);
    CHECK (entryWith (r, "manual_position_mode") >= 0);
    // a wavetable oscillator next to it keeps its type
    Preset q;
    q.set ("Oscillator0", map ({ { "plainParams", map (withText ({}, { { "kParamType", "kOsc_WT" } })) } }));
    const SerumImportResult t = run (q.file());
    CHECK_EQ (value (t, "osc1.type"), 0.0f);
    CHECK_EQ (value (t, "osc1.gran.density_mode"), 0.0f);
}

// GROUPC (WP-12, A419 .. A426): the spectral oscillator keeps its type, its band, filter and warps; the flex curve is the drawn filter mask; a flat curve is not stored
TEST (serum_spectral_oscillator_has_a_type_keys_warps_and_a_filter_curve)
{
    const auto flexBlock = [] (const std::vector<double>& xs, const std::vector<double>& ys)
    {
        std::vector<double> curve (xs.size(), 0.5);
        return map ({ { "curveVals", nums (curve) }, { "numPoints", num (static_cast<double> (xs.size()) - 1) }, { "xVals", nums (xs) }, { "yVals", nums (ys) } });
    };
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", map (withText ({ { "kParamManualPositionMode", 1 }, { "kParamPosition", 62 }, { "kParamHzOffset", -794.4 } }, { { "kParamType", "kOsc_Spectral" } })) },
                                 { "SpectralOsc0", map ({ { "plainParams", map (withText ({ { "kParamFreqLo", 110.0 }, { "kParamFreqHi", 2253.0 }, { "kParamSpecFltShift", -35.0 }, { "kParamSpecFltWetDry", 60.0 },
                                                                                          { "kParamWarp", 0.5 }, { "kParamWarp2", 0.25 } },
                                                                                        { { "kParamWarpMenu", "kSpectralComb" }, { "kParamWarpMenu2", "kVocode_NOISE" } })) },
                                                          { "flex", flexBlock ({ 0.0, 0.52, 0.57, 1.0 }, { 0.5, 0.5, 0.75, 0.86 }) } }) } }));
    // OSC B: a flat flex curve and the on switches; OSC C is a spectral oscillator too (smooth band off, phase lock and transients on)
    p.set ("Oscillator1", map ({ { "plainParams", map (withText ({}, { { "kParamType", "kOsc_Spectral" } })) },
                                 { "SpectralOsc1", map ({ { "plainParams", map (withText ({ { "kParamLoHiIsSmooth", 0 }, { "kParamPhaseLock", 1 }, { "kParamTransients", 1 } }, {})) },
                                                          { "flex", flexBlock ({ 0.0, 1.0 }, { 0.5, 0.5 }) } }) } }));
    const SerumImportResult r = run (p.file());
    CHECK (r.status == ImportStatus::ok);
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_EQ (value (r, "osc1.type"), 3.0f);
    CHECK_EQ (value (r, "osc2.type"), 3.0f);
    CHECK_EQ (value (r, "osc1.spec.manual"), 1.0f);
    CHECK_NEAR (value (r, "osc1.spec.position"), 62.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.spec.freq_shift"), -794.4, 1.0e-2);
    CHECK_NEAR (value (r, "osc1.spec.freq_lo"), 110.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.spec.freq_hi"), 2253.0, 1.0e-2);
    CHECK_NEAR (value (r, "osc1.spec.cut"), -35.0, 1.0e-3);
    CHECK_NEAR (value (r, "osc1.spec.mix"), 60.0, 1.0e-3);
    CHECK_EQ (value (r, "osc1.warp.type"), 76.0f);   // Spectral Comb
    CHECK_EQ (value (r, "osc1.warp2.type"), 98.0f);  // Vocode from Noise
    CHECK_EQ (value (r, "osc2.spec.lohi_smooth"), 0.0f);
    CHECK_EQ (value (r, "osc2.spec.phase_lock"), 1.0f);
    CHECK_EQ (value (r, "osc2.spec.transients"), 1.0f);
    CHECK_EQ (r.spectralFilters.size(), static_cast<size_t> (1));   // OSC B's flat curve is not stored
    CHECK_EQ (r.spectralFilters[0].osc, 1);
    CHECK_EQ (r.spectralFilters[0].slot, 1);
    CHECK (r.spectralFilters[0].points.size() >= 3);
    CHECK_NEAR (r.spectralFilters[0].points.back().y, 0.14, 1.0e-3);   // the file stores the screen y (up is 0), as for every curve of the Serum files
    CHECK (entryWith (r, "spectral_filter_curve") >= 0);
    CHECK (entryWith (r, "spectral_filter_curve_is_flat") >= 0);
}

// GROUPB (WP-06, A401 / A402): the warp modes from the noise, sub and filters and the matrix sources that were dropped have a home
TEST (serum_warp_modes_from_the_noise_sub_and_filters_and_the_new_matrix_sources)
{
    const struct { const char* name; int ours; } modes[] = { { "kFM_NOISE", 52 }, { "kFM_FILT2", 55 }, { "kFMX_SUB", 57 }, { "kFMP_FILT1", 62 }, { "kPD_SUB", 65 }, { "kAM_NOISE", 68 }, { "kRM_FILT2", 75 } };
    for (const auto& m : modes)
    {
        Preset p;
        p.set ("Oscillator0", map ({ { "plainParams", plain ({}) },
                                     { "WTOsc0", map ({ { "embeddedWTData", nums (ramp (4096)) }, { "numFrames", num (4096) }, { "numChannels", num (1) },
                                                        { "plainParams", map (withText ({ { "kParamWarp", 0.5 } }, { { "kParamWarpMenu", m.name } })) } }) } }));
        const SerumImportResult r = run (p.file());
        CHECK_EQ (r.report.unaccounted, 0);
        CHECK_EQ (value (r, "osc1.warp.type"), static_cast<float> (m.ours));
    }
    const auto slot = [] (int source, const std::string& type, int id, const std::string& param)
    {
        return map ({ { "source", nums ({ static_cast<double> (source), 0 }) }, { "destModuleTypeString", text (type) }, { "destModuleID", num (id) }, { "destModuleParamName", text (param) },
                      { "destModuleParamID", num (0) }, { "plainParams", plain ({ { "kParamAmount", 30 } }) } });
    };
    Preset p;
    const int ids[] = { 20, 22, 23, 24, 34, 37, 38, 49, 52, 54, 55, 59 };
    const float expected[] = { 46, 53, 51, 52, 56, 55, 57, 43, 47, 49, 50, 54 };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; ++i)
        p.set ("ModSlot" + std::to_string (i), slot (ids[i], "Oscillator", 1, "kParamVolume"));
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; ++i)
        CHECK_EQ (r.values.plain[modSlotParams (static_cast<int> (i)).source], expected[i]);
}

// GROUPB (WP-08, A408) / REMAINDER item 1 (A449): RISE / FALL of a modulation slot are the keys modNN.rise / fall (2000 ms x (v / 100)^2; absent link = linked)
TEST (serum_modulation_slot_rise_and_fall_become_the_slot_keys)
{
    const auto slot = [] (int source, const std::vector<std::pair<std::string, double>>& params)
    {
        std::vector<std::pair<std::string, double>> all { { "kParamAmount", 30 } };
        all.insert (all.end(), params.begin(), params.end());
        return map ({ { "source", nums ({ static_cast<double> (source), 0 }) }, { "destModuleTypeString", text ("Oscillator") }, { "destModuleID", num (1) },
                      { "destModuleParamName", text ("kParamVolume") }, { "destModuleParamID", num (0) }, { "plainParams", plain (all) } });
    };
    Preset p;
    p.set ("ModSlot0", slot (6, { { "kParamSmoothRise", 50 }, { "kParamSmoothFall", 100 } }));                    // both: 500 and 2000 ms
    p.set ("ModSlot1", slot (7, { { "kParamSmoothRise", 50 } }));                                                  // linked by default: the fall follows
    p.set ("ModSlot2", slot (8, { { "kParamSmoothRise", 50 }, { "kParamSmoothLink", 0 } }));                       // not linked: the fall stays 0
    p.set ("ModSlot3", slot (9, { { "kParamSmoothFall", 20 } }));                                                  // only a fall, linked: the rise follows
    p.set ("ModSlot4", slot (10, {}));                                                                              // no slew: no record
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    const auto ms = [&r] (int slotNumber, const char* tail) { char key[24]; std::snprintf (key, sizeof key, "mod%02d.%s", slotNumber, tail); return r.values.plain[findParamIndex (findParamByKey (key)->id)]; };
    CHECK (std::fabs (ms (1, "rise") - 500.0f) < 0.01f && std::fabs (ms (1, "fall") - 2000.0f) < 0.01f);
    CHECK (std::fabs (ms (2, "rise") - 500.0f) < 0.01f && std::fabs (ms (2, "fall") - 500.0f) < 0.01f);
    CHECK (std::fabs (ms (3, "rise") - 500.0f) < 0.01f && ms (3, "fall") == 0.0f);
    CHECK (std::fabs (ms (4, "rise") - 80.0f) < 0.01f && std::fabs (ms (4, "fall") - 80.0f) < 0.01f);
    CHECK (ms (5, "rise") == 0.0f && ms (5, "fall") == 0.0f);
    CHECK (r.modCurves.empty());
    CHECK (entryWith (r, "modulation_slew_not_modelled") < 0);
}

// REMAINDER item 1 (A450): the DelayOffset / DelayBeatSync of a slot, and a modulation of another slot's RISE / FALL / DELAY (ModSlot destinations, backlog S8)
TEST (serum_slot_delay_and_modulation_of_other_slots_smooth_and_delay)
{
    const auto slot = [] (int source, const char* type, int id, const char* param, const std::vector<std::pair<std::string, double>>& params)
    {
        std::vector<std::pair<std::string, double>> all { { "kParamAmount", 30 } };
        all.insert (all.end(), params.begin(), params.end());
        return map ({ { "source", nums ({ static_cast<double> (source), 0 }) }, { "destModuleTypeString", text (type) }, { "destModuleID", num (id) },
                      { "destModuleParamName", text (param) }, { "destModuleParamID", num (0) }, { "plainParams", plain (all) } });
    };
    Preset p;
    p.set ("ModSlot0", slot (6, "Oscillator", 1, "kParamVolume", { { "kParamDelayOffset", 0.5 }, { "kParamDelayBeatSync", 1 } }));   // 2 beats
    p.set ("ModSlot1", slot (7, "Oscillator", 1, "kParamVolume", { { "kParamDelayOffset", 0.25 } }));                                // 1000 ms, not synced
    p.set ("ModSlot2", slot (8, "ModSlot", 0, "kParamSmoothRise", {}));                                                              // modulates the RISE of slot 1
    p.set ("ModSlot3", slot (9, "ModSlot", 5, "kParamSmoothFall", {}));
    p.set ("ModSlot4", slot (10, "ModSlot", 1, "kParamDelayOffset", {}));
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    const auto value = [&r] (const char* key) { return r.values.plain[findParamIndex (findParamByKey (key)->id)]; };
    CHECK_NEAR (value ("mod01.delay_beats"), 2.0f, 1.0e-4f);
    CHECK_EQ (value ("mod01.delay_sync"), 1.0f);
    CHECK_NEAR (value ("mod02.delay"), 1000.0f, 0.01f);
    CHECK_EQ (value ("mod02.delay_sync"), 0.0f);
    const auto destination = [&r] (int slotNumber) { char key[24]; std::snprintf (key, sizeof key, "mod%02d.destination", slotNumber); return static_cast<size_t> (r.values.plain[findParamIndex (findParamByKey (key)->id)]); };
    CHECK_EQ (destination (3), modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("mod01.rise")->id))));
    CHECK_EQ (destination (4), modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("mod06.fall")->id))));
    CHECK_EQ (destination (5), modDestinationChoiceOf (static_cast<size_t> (findParamIndex (findParamByKey ("mod02.delay_beats")->id))));
    CHECK (entryWith (r, "modulation_delay_not_modelled") < 0 && entryWith (r, "destination_not_modelled") < 0);
}

// tools/ref-compare against the real Serum 2.1.5: the values of the parameters a file does not mention are the Serum defaults, not ours, and the gain staging is the measured one
// (REFCOMPARE-REPORT.md). The numbers are the ones measured with tools/ref-compare/experiment.py (levels of the same crafted preset in Serum and in our engine).
TEST (serum_absent_parameters_take_the_serum_defaults_and_the_gain_staging_is_the_measured_one)
{
    Preset p;
    p.set ("Oscillator0", map ({ { "plainParams", dflt() } }));
    p.set ("Global0", map ({ { "plainParams", dflt() } }));
    p.set ("Env0", map ({ { "plainParams", dflt() } }));
    p.set ("Env1", map ({ { "plainParams", dflt() } }));
    p.set ("VoiceFilter0", map ({ { "plainParams", plain ({ { "kParamEnable", 1 } }) } }));
    const SerumImportResult r = run (p.file());
    CHECK_EQ (r.report.unaccounted, 0);
    CHECK_NEAR (norm (r, "osc1.level"), 56.25, 1e-3);                                 // default 0.75^2 (the file value is the linear gain)
    CHECK_NEAR (norm (r, "master.volume"), 20.0 * std::log10 (0.25) + 2.98, 1e-2);    // default 0.25, +2.98 dB engine calibration
    CHECK_NEAR (norm (r, "env1.attack"), 1.0, 1e-3);                                  // envelope 1: 1 ms, envelopes 2 .. 4: 0.5 ms
    CHECK_NEAR (norm (r, "env2.attack"), 0.5, 1e-3);
    CHECK_NEAR (norm (r, "env1.release"), 15.0, 1e-3);
    CHECK_NEAR (norm (r, "env2.decay"), 1000.0, 1e-3);
    // envelope 1 is the amplitude envelope (curves measured on its dB envelope), envelopes 2 .. 4 modulate (curves measured on a filter they sweep): different tables, Serum 50 is not our 0 in the first
    CHECK_NEAR (norm (r, "env1.attack_curve"), 2.5, 1e-3);
    CHECK_NEAR (norm (r, "env2.attack_curve"), 0.5, 1e-3);
    CHECK_NEAR (norm (r, "env2.decay_curve"), -3.5, 1e-3);
    CHECK_NEAR (norm (r, "env2.release_curve"), -3.5, 1e-3);
    CHECK_NEAR (norm (r, "filter1.cutoff"), 440.0 * std::pow (2.0, (135.0 * 0.5 - 69.0 + 2.0) / 12.0), 0.5);   // default 0.5 on the measured scale (about 480 Hz)
    int mgModel = -1, mgStyle = -1;   // no type in the file: the Serum default MG Low 12
    serumimport::Fid mgFid = serumimport::Fid::exact;
    CHECK (serumimport::filterTypeFromName ("MgL12", mgModel, mgStyle, mgFid));
    CHECK_EQ (norm (r, "filter1.model"), static_cast<float> (mgModel));
    CHECK_EQ (norm (r, "filter1.style"), static_cast<float> (mgStyle));
    // a file value is the linear gain: 0.5 of the master is -6.02 dB on top of the calibration, the sub is trimmed
    Preset q;
    q.set ("Global0", map ({ { "plainParams", plain ({ { "kParamMasterVolume", 0.5 } }) } }));
    q.set ("Oscillator4", map ({ { "plainParams", plain ({ { "kParamEnable", 1 }, { "kParamVolume", 1.0 } }) } }));
    const SerumImportResult r2 = run (q.file());
    CHECK_NEAR (norm (r2, "master.volume"), 20.0 * std::log10 (0.5) + 2.98, 1e-2);
    CHECK_EQ (norm (r2, "master.velocity_track"), 0.0f);   // the velocity does not scale the level in Serum
    CHECK_NEAR (norm (r2, "sub.level"), 65.7, 1e-2);
}

// the unison of Serum 2.1.5: the detune of the voices is 200 * d ^ 1.25 cents at the outer voices (ours: 100 * d ^ 1.25 with the range of 2 semitones), the law is the Serum one
TEST (serum_unison_detune_follows_the_measured_power_law_and_the_serum_unison_law_is_on)
{
    size_t n = 0;
    const serumimport::Rule* rules = serumimport::rulesOf ("Osc", n);
    const serumimport::Rule* detune = nullptr;
    for (size_t i = 0; i < n; ++i)
        if (std::string (rules[i].param) == "kParamDetune")
            detune = &rules[i];
    CHECK (detune != nullptr);
    if (detune != nullptr)
    {
        for (double d : { 0.0, 0.05, 0.25, 0.5, 1.0 })
            CHECK_NEAR (serumimport::convert (*detune, d), 100.0 * std::pow (d, 1.25), 1e-9);
        CHECK_NEAR (detune->serumDefault, 0.25, 1e-12);   // the default of the plug-in: outer voices at 35 cents
    }
}

// tools/ref-compare against the real Serum 2.1.5: the compressor threshold is the opposite of a linear level (the measured peak ceiling of the compressor), the filter drive is a louder output
TEST (serum_compressor_threshold_and_filter_drive_follow_the_measured_curves)
{
    size_t count = 0;
    const serumimport::Rule* rows = serumimport::fxRulesOf (kFxTypeCompressor, count);
    const serumimport::Rule* thresh = nullptr;
    for (size_t i = 0; i < count; ++i)
        if (std::strcmp (rows[i].param, "kParamThresh") == 0) { thresh = rows + i; break; }
    CHECK (thresh != nullptr);
    if (thresh != nullptr)
    {
        // REFCOMPARE round 3 (tools/ref-compare/compthr2.py): the peak ceiling of the Serum compressor threshold, measured to -105 dB
        CHECK_NEAR (serumimport::convert (*thresh, 0.0), 10.6, 1e-9);    // above the measured range the law 10.60 + 59.73 log10 (1 - t), at most +12 dB
        CHECK_NEAR (serumimport::convert (*thresh, 0.3), 1.42, 1e-9);
        CHECK_NEAR (serumimport::convert (*thresh, 0.5), -7.34, 1e-9);
        CHECK_NEAR (serumimport::convert (*thresh, 0.7), -20.65, 1e-9);
        CHECK_NEAR (serumimport::convert (*thresh, 0.9), -49.27, 1e-9);
        CHECK (serumimport::convert (*thresh, 0.6) < serumimport::convert (*thresh, 0.5)); // monotonic
        CHECK (serumimport::convert (*thresh, 0.99) < serumimport::convert (*thresh, 0.95));
        CHECK_NEAR (serumimport::convert (*thresh, 1.0), -120.0, 1e-9);  // the end of the knob is the end of the key
    }
    CHECK_NEAR (serumimport::filterDriveMakeupDb (0.0), 0.0, 1e-9);
    CHECK_NEAR (serumimport::filterDriveMakeupDb (25.0), 7.7, 1e-9);
    CHECK_NEAR (serumimport::filterDriveMakeupDb (100.0), 10.2, 1e-9);
}
