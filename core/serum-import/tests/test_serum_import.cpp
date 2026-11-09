// SynthyBoy - tests of the Serum 2 importer: hand-built fixtures for every mapping class, the accounting identity, content, effects, robustness; a gated run
// over the real presets (tests/serum-presets, never committed) when the folder exists.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "serum_fixtures.h"
#include "serum_map.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/serum_plugin_import.h"
#include "test_runner.h"

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
    CHECK_NEAR (norm (r, "osc1.level"), 50.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.pan"), 50.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.transpose"), -9.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.tune"), 12.0, 1e-3);
    CHECK_EQ (norm (r, "osc1.unison.voices"), 5.0f);
    CHECK_NEAR (norm (r, "osc1.unison.detune"), 25.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.unison.blend"), 60.0, 1e-3);
    CHECK_NEAR (norm (r, "osc1.unison.spread"), 80.0, 1e-3);
    CHECK_EQ (norm (r, "osc1.pitch_track"), 0.0f);
    CHECK_EQ (norm (r, "osc1.unison.stack"), 3.0f); // Octaves
    CHECK_EQ (norm (r, "osc1.unison.mode"), 1.0f);  // Super
    CHECK_NEAR (norm (r, "osc1.frame"), (129.0 - 1.0) / 255.0 * 100.0, 1e-2);
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
    // the Init filter type is MG Low 6 = model Slope (9), style 0
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
    CHECK_NEAR (norm (r, "filter1.cutoff"), 440.0 * std::pow (2.0, (135.0 * 0.5 - 69.0) / 12.0), 0.5);
    CHECK_NEAR (norm (r, "filter1.resonance"), 40.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.drive"), 10.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.var"), 30.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.mix"), 80.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.keytrack"), 100.0, 1e-3);
    CHECK_NEAR (norm (r, "filter1.level"), 20.0 * std::log10 (0.5), 1e-2);
    CHECK_NEAR (norm (r, "filter1.pan"), 20.0, 1e-3);
    CHECK_EQ (norm (r, "filter1.model"), 26.0f); // New: MG Ladder is style 0
    CHECK_EQ (norm (r, "filter1.style"), 0.0f);
    CHECK_NEAR (norm (r, "env1.attack"), 10.0, 1e-3);
    CHECK_NEAR (norm (r, "env1.hold"), 100.0, 1e-3);
    CHECK_NEAR (norm (r, "env1.decay"), 1500.0, 1e-2);
    CHECK_NEAR (norm (r, "env1.sustain"), 50.0, 1e-3);
    CHECK_NEAR (norm (r, "env1.release"), 2000.0, 1e-2);
    CHECK_NEAR (norm (r, "env1.attack_curve"), 0.0, 1e-3);
    CHECK_NEAR (norm (r, "env1.decay_curve"), -3.0, 0.02);
    CHECK_NEAR (norm (r, "env1.release_curve"), 1.8, 1e-3);
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
    p.set ("ModSlot2", slot (51, 0, "VoiceFilter", 0, "kParamFreq", plain ({ { "kParamAmount", 10 } })));                                // OSC C as a source: no equivalent
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
    CHECK_EQ (r.values.plain[s2.source], 0.0f);                      // dropped
    CHECK (countKind (r, ItemKind::dropped, "ModSlot2") >= 1);
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
        CHECK_NEAR (limit, ratio >= 1.0e5 ? 1.0 : 0.0, 1e-4);
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
    CHECK_NEAR (r.values.plain[k0.p[mixNumber]], 0.4, 1e-4);
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
    CHECK (entryWith (r, "eq_peak_band_on_the_mid_band") >= 0);
    CHECK (entryWith (r, "eq_second_peak_as_shelf") >= 0);
    CHECK (entryWith (r, "unknown_effect_module") >= 0);
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
    const SerumImportResult r = run (p.file(), &res);
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
    CHECK_NEAR (norm (r, "convolve.mix"), 40.0, 1e-3);
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
    CHECK_NEAR (norm (r, "osc1.bus1_send"), 25.0, 1e-3);
    CHECK_EQ (norm (r, "osc2.destination"), 4.0f);
    CHECK_NEAR (norm (r, "osc2.bus2_send"), 60.0, 1e-3);
    CHECK_EQ (norm (r, "filter1.destination"), 2.0f);
    CHECK_NEAR (norm (r, "master.volume"), 20.0 * std::log10 (0.25), 1e-2);
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
    CHECK_EQ (serumimport::sourceChoiceOf (51, name, f2), -1);  // OSC C as a source: none
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
    CHECK (slowest < 20.0);
}
