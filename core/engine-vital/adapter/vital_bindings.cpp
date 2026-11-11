// SynthyBoy - engine-vital adapter: the table that maps our parameters onto the controls of the Vital-derived engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vital-side code: the control names, scales and ranges are the engine's (core/engine-vital/upstream), the parameters are ours
// (core/src/param_table.cpp). Only this file and vital_engine.cpp know both.

#include "vital_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "synth_constants.h"
#include "vital_tables.h"
#include "synth_parameters.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"

namespace synthyboy
{
namespace
{
constexpr double kMidi0Hz = 8.1757989156437073;
// our warp types -> the engine's (9 and 12 would use the sample). From 11 on (block B21, 13-new-keys.md 4.22) the engine's enum continues
// after kRmSample (12): our 11 is the engine's 13, and so on in the same order
constexpr int makeWarpMap (int index) { return index < 11 ? (index < 9 ? index : index + 1) : index + 2; }
struct WarpMapTable
{
    int values[kNumWarpTypes];
    constexpr WarpMapTable() : values()
    {
        for (int i = 0; i < kNumWarpTypes; ++i)
            values[i] = makeWarpMap (i);
    }
};
constexpr WarpMapTable kWarpMapTable;
constexpr const int* kWarpMap = kWarpMapTable.values;

struct Builder
{
    std::vector<Binding> out;

    size_t indexOf (const std::string& key) const
    {
        const ParamInfo* info = findParamByKey (key.c_str());
        return info != nullptr ? static_cast<size_t> (findParamIndex (info->id)) : static_cast<size_t> (-1);
    }

    void add (const std::string& key, const std::string& control, Conv conv, double a = 1.0, double b = 0.0, Cap cap = Cap::none, int capArg = 0,
              const int* map = nullptr)
    {
        Binding binding;
        binding.param = indexOf (key);
        binding.control = control;
        binding.conv = conv;
        binding.a = a;
        binding.b = b;
        binding.map = map;
        binding.cap = cap;
        binding.capArg = capArg;
        if (binding.param != static_cast<size_t> (-1))
            out.push_back (std::move (binding));
        else
            out.push_back (Binding { static_cast<size_t> (-1), "missing parameter " + key, Conv::linear }); // caught by the tests
    }

    void percent (const std::string& key, const std::string& control, double scale = 0.01) { add (key, control, Conv::linear, scale); }
    void choice (const std::string& key, const std::string& control, double offset = 0.0) { add (key, control, Conv::index, 1.0, offset); }
    /** Limits the choice of an already added binding to the entries the engine implements (see Binding::maxIndex). */
    void limit (const std::string& key, int maxIndex)
    {
        const size_t param = indexOf (key);
        for (auto& binding : out)
            if (binding.param == param)
                binding.maxIndex = maxIndex;
    }
    void hzNote (const std::string& key, const std::string& control) { add (key, control, Conv::noteFromHz); }
    void frameOsc (const std::string& key, int osc)
    {
        const size_t param = indexOf (key);
        for (auto& binding : out)
            if (binding.param == param)
                binding.frameOsc = osc;
    }
    /** The binding of `key` writes its control only while the choice `gateKey` equals `value`. */
    void gate (const std::string& key, const std::string& gateKey, int value)
    {
        const size_t param = indexOf (key);
        for (auto& binding : out)
            if (binding.param == param)
            {
                binding.gateParam = static_cast<int> (indexOf (gateKey));
                binding.gateValue = value;
            }
    }
};

std::vector<Binding> build()
{
    Builder b;

    // --- master ---------------------------------------------------------------------------------------------------
    b.add ("master.volume", "volume", Conv::volumeDb);
    b.add ("master.polyphony", "polyphony", Conv::linear, 1.0, 0.0, Cap::voices);
    b.percent ("master.velocity_track", "velocity_track");
    b.add ("master.transpose", "voice_transpose", Conv::linear);
    b.percent ("master.tune", "voice_tune");
    b.choice ("master.legato", "legato");
    // master.voice.steal_priority uses the names and the sense of Serum 2 (Voice Steal Priority: the kind of voice that is NOT stolen),
    // which is also Vital's voice_priority: Newest (default: the earliest note goes) -> kRoundRobin (4, the engine default, which keeps the
    // list in the order of playing, so nothing changes), Oldest -> kOldest (1), Highest -> kHighest (2), Lowest -> kLowest (3),
    // Velocity (the lowest velocity goes) -> kVelocity (5, ours)
    static constexpr int kVoicePriorityMap[] = { 4, 1, 2, 3, 5 };
    b.add ("master.voice.steal_priority", "voice_priority", Conv::indexMap, 1.0, 0.0, Cap::none, 0, kVoicePriorityMap);
    b.choice ("master.poly_legato", "poly_legato");
    b.percent ("master.stereo_spread", "master_stereo_spread");
    b.add ("master.portamento.time", "portamento_time", Conv::log2Ms, -10.0);
    b.add ("master.portamento.slope", "portamento_slope", Conv::linear);
    b.choice ("master.portamento.always", "portamento_force");
    b.choice ("master.portamento.scaled", "portamento_scale");
    b.choice ("voice.phase_legato", "voice_phase_legato");
    // Block B20: stereo routing and voice override (G5)
    b.percent ("master.stereo_routing", "stereo_routing");
    b.choice ("master.voice_override", "voice_override");

    // --- oscillators ----------------------------------------------------------------------------------------------
    for (int n = 1; n <= 3; ++n)
    {
        const std::string k = "osc" + std::to_string (n) + ".", c = "osc_" + std::to_string (n) + "_";
        b.choice (k + "on", c + "on");
        b.add (k + "level", c + "level", Conv::sqrtScaled, 0.01);
        b.percent (k + "pan", c + "pan");
        b.add (k + "transpose", c + "transpose", Conv::linear);
        b.percent (k + "tune", c + "tune");
        b.add (k + "frame", c + "wave_frame", Conv::linear, 0.01 * (vitaltables::kMorphFrames - 1));
        b.add (k + "frame_spread", c + "frame_spread", Conv::linear, 0.01 * (vitaltables::kMorphFrames - 1));
        b.frameOsc (k + "frame", n);
        b.frameOsc (k + "frame_spread", n);
        // state version 3: the oscillator type, the modulatable controls of the sample oscillator and the second warp slot
        b.choice (k + "type", c + "type");
        b.percent (k + "sample.scan", c + "sample_scan");
        b.percent (k + "sample.uni_start", c + "sample_uni_start");
        b.percent (k + "sample.uni_span", c + "sample_uni_span");
        b.add (k + "warp2.type", c + "warp2_type", Conv::indexMap, 1.0, 0.0, Cap::none, 0, kWarpMap);
        b.percent (k + "warp2.amount", c + "warp2_amount");
        b.add (k + "warp2.phase", c + "warp2_phase", Conv::linear, 1.0 / 360.0);
        b.percent (k + "warp2.spread", c + "warp2_spread", 0.005);
        b.add (k + "phase", c + "phase", Conv::linear, 1.0 / 360.0);
        b.percent (k + "random_phase", c + "random_phase");
        b.choice (k + "env1", c + "env1"); // B21: the ENV 1 button of the Main / Direct channel
        b.choice (k + "destination", c + "destination"); // 5 Filter blend and 6 None (WP5) are the engine's destinations 5 and 6
        b.percent (k + "filter_blend", c + "filter_blend");
        b.percent (k + "bus1_send", c + "bus1_send");
        b.percent (k + "bus2_send", c + "bus2_send");
        b.add (k + "unison.voices", c + "unison_voices", Conv::linear, 1.0, 0.0, Cap::unison);
        b.add (k + "unison.detune", c + "unison_detune", Conv::sqrtScaled, 1.0);
        b.add (k + "unison.detune_curve", c + "detune_power", Conv::linear);
        b.percent (k + "unison.spread", c + "stereo_spread");
        b.percent (k + "unison.blend", c + "unison_blend");
        b.choice (k + "unison.stack", c + "stack_style");
        b.choice (k + "morph.type", c + "spectral_morph_type");
        b.percent (k + "morph.amount", c + "spectral_morph_amount");
        b.percent (k + "morph.spread", c + "spectral_morph_spread", 0.005);
        b.add (k + "warp.type", c + "distortion_type", Conv::indexMap, 1.0, 0.0, Cap::none, 0, kWarpMap);
        b.percent (k + "warp.amount", c + "distortion_amount");
        b.add (k + "warp.phase", c + "distortion_phase", Conv::linear, 1.0 / 360.0);
        b.percent (k + "warp.spread", c + "distortion_spread", 0.005);

        // Block B14: Pitch and unison modes, transpose snap, phase legato
        b.choice (k + "pitch_mode", c + "pitch_mode");
        b.add (k + "pitch_coarse", c + "pitch_coarse", Conv::linear);
        b.choice (k + "pitch_track", c + "midi_track");
        b.add (k + "fixed_pitch", c + "fixed_pitch", Conv::linear);
        b.choice (k + "pitch_bend_track", c + "pitch_bend_track");
        b.add (k + "transpose_snap", c + "transpose_quantize", Conv::linear);
        b.choice (k + "unison.mode", c + "unison_mode");
        b.add (k + "unison.range", c + "detune_range", Conv::linear);
        b.percent (k + "unison.blend_shape", c + "unison_blend_shape");
        b.choice (k + "unison.stack_repeat", c + "unison_stack_repeat");
        b.choice (k + "unison.law", c + "unison_law");   // block B33: 1 = the unison law of Serum 2 (level of the centre / outer voices by blend and voice count, stereo spread, 13-new-keys.md 4.47)
        b.choice (k + "phase_legato", c + "phase_legato");
        b.choice (k + "phase_memory", c + "phase_memory");

        // Block B27 (GROUPB, WP-05): Ratio SRC, smooth interpolation of the frame position and the slice destination
        b.choice (k + "ratio_src", c + "ratio_src");
        b.choice (k + "smooth_interp", c + "smooth_interpolation");
        b.percent (k + "sample.slice", c + "sample_slice");

        // Block B28 (GROUPC, WP-11): the modulatable keys of the granular oscillator (the static ones are handled by the adapter, isSpecialParam)
        for (const char* name : { "window_amount", "window_skew", "position", "rand_offset", "rand_dir", "rand_length", "rand_pan", "rand_level",
                                  "rand_window", "rand_skew", "rand_warp", "rand_warp2", "xy_y" })
            b.percent (k + "gran." + name, c + "gran_" + name);
        b.add (k + "gran.density", c + "gran_density", Conv::linear);
        b.add (k + "gran.length", c + "gran_length", Conv::linear);
        b.add (k + "gran.length_percent", c + "gran_length_percent", Conv::linear);
        b.add (k + "gran.rand_pitch", c + "gran_rand_pitch", Conv::linear);

        // Block B29 (GROUPC, WP-12): the modulatable keys of the spectral oscillator
        for (const char* name : { "cut", "mix", "position", "xy_y", "flt_frame" })   // flt_frame: B32, REMAINDER item 2
            b.percent (k + "spec." + name, c + "spec_" + name);
        b.add (k + "spec.freq_lo", c + "spec_freq_lo", Conv::linear);
        b.add (k + "spec.freq_hi", c + "spec_freq_hi", Conv::linear);
        b.add (k + "spec.freq_shift", c + "spec_freq_shift", Conv::linear);

        // Block B30 (GROUPC, WP-13): the timbre of the multisample oscillator (the other keys are read by the adapter)
        b.percent (k + "multi.timbre", c + "multi_timbre", 0.01);
    }

    // --- noise (the engine's built-in noise sample and extras) --------------------------------------------------------
    b.choice ("noise.on", "sample_on");
    b.add ("noise.level", "sample_level", Conv::sqrtScaled, 0.01);
    b.percent ("noise.pan", "sample_pan");
    b.choice ("noise.destination", "sample_destination");
    b.choice ("noise.env1", "sample_env1");
    b.percent ("noise.filter_blend", "sample_filter_blend");
    b.percent ("noise.bus1_send", "sample_bus1_send");
    b.percent ("noise.bus2_send", "sample_bus2_send");
    b.add ("noise.pitch", "sample_transpose", Conv::linear);
    b.percent ("noise.fine", "sample_tune", 0.01);
    b.percent ("noise.phase", "sample_phase");
    b.choice ("noise.random_phase", "sample_random_phase");
    b.choice ("noise.keytrack", "sample_keytrack");
    b.choice ("noise.loop", "sample_loop");
    // block B23 (GROUPA1, WP-04); noise.type is handled by the adapter (a generator or the content sample)
    b.percent ("noise.stereo", "sample_stereo");
    b.percent ("noise.filter", "sample_filter");
    b.choice ("noise.bounce", "sample_bounce");

    // --- sub oscillator (G23) ---------------------------------------------------------------------------------------
    b.choice ("sub.on", "sub_on");
    b.choice ("sub.waveform", "sub_waveform");
    b.add ("sub.octave", "sub_octave", Conv::linear);
    b.add ("sub.tune", "sub_tune", Conv::linear);
    b.add ("sub.level", "sub_level", Conv::sqrtScaled, 0.01);
    b.percent ("sub.pan", "sub_pan");
    b.choice ("sub.destination", "sub_destination");
    b.choice ("sub.env1", "sub_env1");
    b.choice ("sub.mono", "sub_mono");
    b.percent ("sub.phase", "sub_phase");
    b.choice ("sub.phase_contiguous", "sub_phase_contiguous");
    b.percent ("sub.filter_blend", "sub_filter_blend");
    b.percent ("sub.bus1_send", "sub_bus1_send");
    b.percent ("sub.bus2_send", "sub_bus2_send");

    // --- mixer buses and DIRECT (WP5) -------------------------------------------------------------------------------
    for (int bus = 1; bus <= 2; ++bus)
    {
        const std::string k = "bus" + std::to_string (bus) + ".", c = "bus" + std::to_string (bus) + "_";
        b.add (k + "level", c + "level", Conv::linear);
        b.choice (k + "destination", c + "destination");
        b.choice (k + "fx_bypass", c + "fx_bypass");
    }
    b.add ("direct.level", "direct_level", Conv::linear);
    b.choice ("main.fx_bypass", "main_fx_bypass");

    // --- filters --------------------------------------------------------------------------------------------------
    for (const char* name : { "filter1", "filter2", "fxfilter" })
    {
        const std::string k = std::string (name) + ".";
        const std::string c = std::strcmp (name, "filter1") == 0 ? "filter_1_" : (std::strcmp (name, "filter2") == 0 ? "filter_2_" : "filter_fx_");
        const bool fx = std::strcmp (name, "fxfilter") == 0;
        if (fx)
            b.add (k + "on", c + "on", Conv::index, 1.0, 0.0, Cap::fx, 5);
        else
            b.choice (k + "on", c + "on");
        if (! fx)
        {
            b.choice (k + "destination", c + "destination"); // WP5: effects only, direct out, none
            b.choice (k + "env1", c + "env1");           // B21: the ENV 1 button
            b.percent (k + "bus1_send", c + "bus1_send");
            b.percent (k + "bus2_send", c + "bus2_send");
        }
        b.choice (k + "model", c + "model");
        b.choice (k + "style", c + "style");
        b.percent (k + "var", c + "var");    // WP6: the fourth knob of the models of state version 3
        b.percent (k + "pan", c + "pan");
        b.add (k + "level", c + "level", Conv::linear);
        b.choice (k + "clean_mode", c + "clean_mode"); // block B16: the drive as a clean gain after the filter
        b.hzNote (k + "cutoff", c + "cutoff");
        b.percent (k + "resonance", c + "resonance");
        b.add (k + "drive", c + "drive", Conv::linear);
        b.percent (k + "blend", c + "blend", 0.02);
        b.percent (k + "mix", c + "mix");
        b.percent (k + "keytrack", c + "keytrack");
        b.add (k + "comb_offset", c + "blend_transpose", Conv::linear);
        b.percent (k + "formant.x", c + "formant_x");
        b.percent (k + "formant.y", c + "formant_y");
        b.percent (k + "formant.resonance", c + "formant_resonance");
        b.percent (k + "formant.spread", c + "formant_spread");
        b.add (k + "formant.transpose", c + "formant_transpose", Conv::linear);
    }

    // --- envelopes -------------------------------------------------------------------------------------------------
    for (int n = 1; n <= 6; ++n)
    {
        const std::string k = "env" + std::to_string (n) + ".", c = "env_" + std::to_string (n) + "_";
        b.add (k + "delay", c + "delay", Conv::quarticMs);
        b.add (k + "attack", c + "attack", Conv::quarticMs);
        b.add (k + "hold", c + "hold", Conv::quarticMs);
        b.add (k + "decay", c + "decay", Conv::quarticMs);
        b.percent (k + "sustain", c + "sustain");
        b.add (k + "release", c + "release", Conv::quarticMs);
        // beat units (state version 3): the same engine controls, written from the beat keys while envN.units is Beats
        for (const char* stage : { "delay", "attack", "hold", "decay", "release" })
        {
            b.add (k + stage + "_beats", c + stage, Conv::quarticBeats);
            b.gate (k + stage, k + "units", 0);
            b.gate (k + stage + "_beats", k + "units", 1);
        }
        b.choice (k + "legato_inverted", c + "legato_inverted");
        b.add (k + "attack_curve", c + "attack_power", Conv::linear);
        b.add (k + "decay_curve", c + "decay_power", Conv::linear);
        b.add (k + "release_curve", c + "release_power", Conv::linear);
    }

    // --- LFOs ------------------------------------------------------------------------------------------------------
    for (int n = 1; n <= 10; ++n)
    {
        const std::string k = "lfo" + std::to_string (n) + ".", c = "lfo_" + std::to_string (n) + "_";
        b.choice (k + "direction", c + "direction");
        b.add (k + "rate", c + "frequency", Conv::log2Hz);
        b.choice (k + "sync", c + "sync");
        b.choice (k + "division", c + "tempo");
        b.choice (k + "sync_type", c + "sync_type");
        b.add (k + "phase", c + "phase", Conv::linear, 1.0 / 360.0);
        b.add (k + "fade_in", c + "fade_time", Conv::linear, 0.001);
        b.add (k + "delay", c + "delay_time", Conv::linear, 0.001);
        b.choice (k + "smooth", c + "smooth_mode");
        b.add (k + "smooth_time", c + "smooth_time", Conv::log2Ms, -10.0);
        b.add (k + "stereo", c + "stereo", Conv::linear, 1.0 / 360.0);
        b.add (k + "keytrack_transpose", c + "keytrack_transpose", Conv::linear);
        b.percent (k + "keytrack_tune", c + "keytrack_tune");
        b.percent (k + "swing", c + "swing");
    }
    for (int n = 1; n <= 4; ++n)
    {
        const std::string k = "rlfo" + std::to_string (n) + ".", c = "random_" + std::to_string (n) + "_";
        b.choice (k + "style", c + "style");
        b.add (k + "rate", c + "frequency", Conv::log2Hz);
        b.choice (k + "sync", c + "sync");
        b.choice (k + "division", c + "tempo");
        b.choice (k + "sync_type", c + "sync_type");
        b.choice (k + "stereo", c + "stereo");
        b.add (k + "keytrack_transpose", c + "keytrack_transpose", Conv::linear);
        b.percent (k + "keytrack_tune", c + "keytrack_tune");
    }
    for (int n = 1; n <= 8; ++n)
        b.percent ("macro." + std::to_string (n), "macro_control_" + std::to_string (n));

    // --- modulation slots (source and destination are special) ---------------------------------------------------------
    for (int slot = 1; slot <= kNumModSlots; ++slot)
    {
        char key[16];
        std::snprintf (key, sizeof key, "mod%02d.", slot);
        const std::string k = key, c = "modulation_" + std::to_string (slot) + "_";
        b.percent (k + "amount", c + "amount");
        b.add (k + "power", c + "power", Conv::linear);
        b.choice (k + "bipolar", c + "bipolar");
        b.choice (k + "stereo", c + "stereo");
        b.choice (k + "bypass", c + "bypass");
        // REMAINDER item 1 (B31): RISE / FALL (ms) and DELAY (ms, beats) of the slot, the controls are in the unit of the key; delay_sync is the adapter's
        b.add (k + "rise", c + "rise", Conv::linear);
        b.add (k + "fall", c + "fall", Conv::linear);
        b.add (k + "delay", c + "delay", Conv::linear);
        b.add (k + "delay_beats", c + "delay_beats", Conv::linear);
    }

    // --- effects ---------------------------------------------------------------------------------------------------
    const auto on = [&] (const char* key, const char* control, int slot) { b.add (std::string (key) + ".on", std::string (control) + "_on", Conv::index, 1.0, 0.0, Cap::fx, slot); };
    // the rack of each of the 16 modules (WP5)
    {
        static const char* const keys[16] = { "chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb",
                                              "hyperdim", "utility", "bode", "convolve", "splitter_lh", "splitter_lmh", "splitter_ms" };
        static const char* const controls[16] = { "chorus", "compressor", "delay", "distortion", "eq", "filter_fx", "flanger", "phaser", "reverb",
                                                  "hyperdim", "utility", "bode", "convolve", "splitter_lh", "splitter_lmh", "splitter_ms" };
        for (int m = 0; m < 16; ++m)
            b.choice (std::string (keys[m]) + ".rack", std::string (controls[m]) + "_rack");
    }

    on ("chorus", "chorus", 0);
    b.percent ("chorus.mix", "chorus_dry_wet");
    b.add ("chorus.rate", "chorus_frequency", Conv::log2Hz);
    b.choice ("chorus.sync", "chorus_sync");
    b.choice ("chorus.division", "chorus_tempo");
    b.percent ("chorus.depth", "chorus_mod_depth");
    b.percent ("chorus.feedback", "chorus_feedback");
    b.add ("chorus.delay1", "chorus_delay_1", Conv::log2Ms, -10.0);
    b.add ("chorus.delay2", "chorus_delay_2", Conv::log2Ms, -10.0);
    b.add ("chorus.voices", "chorus_voices", Conv::linear);
    b.hzNote ("chorus.filter_cutoff", "chorus_cutoff");
    b.percent ("chorus.filter_spread", "chorus_spread");

    on ("compressor", "compressor", 1);
    b.percent ("compressor.mix", "compressor_mix");
    b.percent ("compressor.attack", "compressor_attack");
    b.percent ("compressor.release", "compressor_release");
    b.choice ("compressor.bands", "compressor_enabled_bands");
    for (const char* band : { "low", "mid", "high" })
    {
        const std::string k = std::string ("compressor.") + band + ".";
        const std::string c = std::string ("compressor_") + (std::strcmp (band, "mid") == 0 ? "band" : band) + "_";
        b.add (k + "lower_threshold", c + "lower_threshold", Conv::linear);
        b.add (k + "upper_threshold", c + "upper_threshold", Conv::linear);
        b.percent (k + "lower_ratio", c + "lower_ratio");
        b.percent (k + "upper_ratio", c + "upper_ratio");
        b.add (k + "gain", c + "gain", Conv::linear);
    }

    on ("delay", "delay", 2);
    b.percent ("delay.mix", "delay_dry_wet");
    b.percent ("delay.feedback", "delay_feedback");
    b.add ("delay.time", "delay_frequency", Conv::log2PeriodMs);
    b.add ("delay.time2", "delay_aux_frequency", Conv::log2PeriodMs);
    b.choice ("delay.sync", "delay_sync");
    b.choice ("delay.division", "delay_tempo", 4.0);
    b.choice ("delay.division2", "delay_aux_tempo", 4.0);
    b.choice ("delay.style", "delay_style");
    b.hzNote ("delay.filter_cutoff", "delay_filter_cutoff");
    b.percent ("delay.filter_spread", "delay_filter_spread");

    on ("distortion", "distortion", 3);
    b.choice ("distortion.type", "distortion_type");
    b.add ("distortion.drive", "distortion_drive", Conv::linear);
    b.percent ("distortion.mix", "distortion_mix");
    b.choice ("distortion.filter_order", "distortion_filter_order");
    b.hzNote ("distortion.filter_cutoff", "distortion_filter_cutoff");
    b.percent ("distortion.filter_resonance", "distortion_filter_resonance");
    b.percent ("distortion.filter_blend", "distortion_filter_blend", 0.02);

    on ("eq", "eq", 4);
    for (const char* band : { "low", "mid", "high" })
    {
        const std::string k = std::string ("eq.") + band + ".";
        const std::string c = std::string ("eq_") + (std::strcmp (band, "mid") == 0 ? "band" : band) + "_";
        b.choice (k + "mode", c + "mode");
        b.hzNote (k + "cutoff", c + "cutoff");
        b.add (k + "gain", c + "gain", Conv::linear);
        b.add (k + "resonance", c + "resonance", Conv::sqrtScaled, 0.01);
    }
    // block B34 (13-new-keys.md 4.48): the second peak / notch band (Off by default) and the EQ law (1 = the RBJ biquads of Serum 2.1.5)
    b.choice ("eq.mid2.mode", "eq_band2_mode");
    b.hzNote ("eq.mid2.cutoff", "eq_band2_cutoff");
    b.add ("eq.mid2.gain", "eq_band2_gain", Conv::linear);
    b.add ("eq.mid2.resonance", "eq_band2_resonance", Conv::sqrtScaled, 0.01);
    b.choice ("eq.law", "eq_law");
    // block B35 (13-new-keys.md 4.49): the convolution law (1 = Serum 2.1.5: the impulse is normalised to the peak 1, decay is a T60, size 0.1 .. 10x)
    b.choice ("convolve.law", "convolve_law");

    on ("flanger", "flanger", 6);
    b.percent ("flanger.mix", "flanger_dry_wet", 0.005);
    b.percent ("flanger.feedback", "flanger_feedback");
    b.add ("flanger.rate", "flanger_frequency", Conv::log2Hz);
    b.choice ("flanger.sync", "flanger_sync");
    b.choice ("flanger.division", "flanger_tempo");
    b.percent ("flanger.depth", "flanger_mod_depth");
    b.hzNote ("flanger.center", "flanger_center");
    b.add ("flanger.phase_offset", "flanger_phase_offset", Conv::linear, 1.0 / 360.0);

    on ("phaser", "phaser", 7);
    b.percent ("phaser.mix", "phaser_dry_wet");
    b.percent ("phaser.feedback", "phaser_feedback");
    b.add ("phaser.rate", "phaser_frequency", Conv::log2Hz);
    b.choice ("phaser.sync", "phaser_sync");
    b.choice ("phaser.division", "phaser_tempo");
    b.add ("phaser.depth", "phaser_mod_depth", Conv::linear);
    b.hzNote ("phaser.center", "phaser_center");
    b.percent ("phaser.blend", "phaser_blend", 0.02);
    b.add ("phaser.phase_offset", "phaser_phase_offset", Conv::linear, 1.0 / 360.0);

    on ("reverb", "reverb", 8);
    b.percent ("reverb.mix", "reverb_dry_wet");
    b.percent ("reverb.size", "reverb_size");
    b.add ("reverb.decay", "reverb_decay_time", Conv::log2Ms, -6.0);
    b.add ("reverb.predelay", "reverb_delay", Conv::linear, 0.001);
    b.hzNote ("reverb.pre_low_cut", "reverb_pre_low_cutoff");
    b.hzNote ("reverb.pre_high_cut", "reverb_pre_high_cutoff");
    b.hzNote ("reverb.low_shelf_cutoff", "reverb_low_shelf_cutoff");
    b.add ("reverb.low_shelf_gain", "reverb_low_shelf_gain", Conv::linear);
    b.hzNote ("reverb.high_shelf_cutoff", "reverb_high_shelf_cutoff");
    b.add ("reverb.high_shelf_gain", "reverb_high_shelf_gain", Conv::linear);
    b.add ("reverb.chorus_amount", "reverb_chorus_amount", Conv::sqrtScaled, 0.01);
    b.add ("reverb.chorus_rate", "reverb_chorus_frequency", Conv::log2Hz);

    // block B16 (G72): the output LEVEL trim of the nine Vital effects (the FX filter has fxfilter.level)
    for (const char* fx : { "chorus", "compressor", "delay", "distortion", "eq", "flanger", "phaser", "reverb" })
        b.add (std::string (fx) + ".level", std::string (fx) + "_level", Conv::linear);

    // --- block B17 (docs/ui-design/13-new-keys.md 4.17): compressor / delay / distortion / phaser / reverb extras ---------------------------------
    b.choice ("compressor.limit", "compressor_limit");
    b.choice ("compressor.limit_latency_comp", "compressor_limit_latency_comp");
    b.add ("compressor.crossover_low", "compressor_crossover_low", Conv::linear);
    b.add ("compressor.crossover_high", "compressor_crossover_high", Conv::linear);
    b.choice ("delay.hq", "delay_hq");
    b.add ("delay.offset", "delay_offset", Conv::linear);
    b.add ("delay.offset_left", "delay_offset_left", Conv::linear);   // block B25 (GROUPA1, WP-10)
    b.percent ("distortion.filter_keytrack", "distortion_filter_keytrack");
    b.add ("phaser.depth2", "phaser_depth2", Conv::linear);
    b.choice ("reverb.type", "reverb_type");
    b.choice ("reverb.room", "reverb_room");
    b.percent ("reverb.width", "reverb_width");
    b.percent ("reverb.er_size", "reverb_er_size");
    b.percent ("reverb.diff_a", "reverb_diff_a");
    b.percent ("reverb.diff_b", "reverb_diff_b");
    b.percent ("reverb.feedback", "reverb_feedback");               // block B25 (GROUPA1, WP-10)

    // --- the Serum modules (WP7): every key is the control of the same name in its own unit ------------------------------------------
    struct Module { const char* key; int slot; std::initializer_list<const char*> params; };
    const Module modules[] = {
        { "hyperdim", 9, { "rate", "unison", "detune", "retrig", "mix", "dim_size", "dim_mix", "level" } },
        { "utility", 10, { "polarity_l", "polarity_r", "lpf", "hpf", "mono_bass", "mono_freq", "width", "pan", "mix", "level" } },
        { "bode", 11, { "mono_input", "shift", "range", "dir", "width", "delay", "sync", "division", "feed", "balance", "blur", "retrig", "mix", "level" } },
        { "convolve", 12, { "ir", "size", "tone", "phi_min", "predelay", "sync", "division", "attack", "decay", "damp", "ir_gain", "mix", "level" } },
        { "splitter_lh", 13, { "freq", "low_bypass", "high_bypass", "level" } },
        { "splitter_lmh", 14, { "freq_low", "freq_high", "low_bypass", "mid_bypass", "high_bypass", "level" } },
        { "splitter_ms", 15, { "mid_bypass", "side_bypass", "level" } },
    };
    for (const Module& m : modules)
    {
        on (m.key, m.key, m.slot);
        for (const char* param : m.params)
            b.add (std::string (m.key) + "." + param, std::string (m.key) + "_" + param, Conv::linear);
    }

    return std::move (b.out);
}
} // namespace

const std::vector<Binding>& vitalBindings()
{
    static const std::vector<Binding> table = build();
    return table;
}

namespace
{
/** The modulatable keys of oscN.gran.* (bound to controls; every other oscN.gran.* key is read by the adapter). */
bool isGranularModulatable (const char* key)
{
    for (const char* name : { "window_amount", "window_skew", "position", "rand_offset", "rand_dir", "rand_length", "rand_pan", "rand_level", "rand_window",
                              "rand_skew", "rand_warp", "rand_warp2", "xy_y", "density", "length", "length_percent", "rand_pitch" })
    {
        const std::string tail = std::string (".gran.") + name;
        const size_t n = std::strlen (key);
        if (n >= tail.size() && tail == key + n - tail.size())
            return true;
    }
    return false;
}

/** The modulatable keys of oscN.spec.* (bound to controls; every other oscN.spec.* key is read by the adapter). */
bool isSpectralModulatable (const char* key)
{
    for (const char* name : { "freq_lo", "freq_hi", "cut", "mix", "freq_shift", "position", "xy_y", "flt_frame" })
    {
        const std::string tail = std::string (".spec.") + name;
        const size_t n = std::strlen (key);
        if (n >= tail.size() && tail == key + n - tail.size())
            return true;
    }
    return false;
}
} // namespace

bool isSpecialParam (size_t paramIndex)
{
    const char* key = paramTable()[paramIndex].key;
    const auto endsWith = [&] (const char* suffix)
    {
        const size_t n = std::strlen (key), m = std::strlen (suffix);
        return n >= m && std::strcmp (key + n - m, suffix) == 0;
    };
    const auto startsWith = [&] (const char* prefix) { return std::strncmp (key, prefix, std::strlen (prefix)) == 0; };
    return endsWith (".wavetable") || endsWith (".shape") || std::strcmp (key, "filter.routing") == 0
        || std::strncmp (key, "fx.order.", 9) == 0 || std::strcmp (key, "reverb.force_enable") == 0
        || ((endsWith (".source") || endsWith (".destination")) && key[0] == 'm' && key[1] == 'o') || std::strcmp (key, "master.bend_range") == 0
        // state version 3: handled by the adapter (vital_engine.cpp), not one control each
        || (startsWith ("env") && endsWith (".units")) || (startsWith ("lfo") && (endsWith (".mono") || endsWith (".host")))
        || (startsWith ("mod") && std::strstr (key, ".aux_") != nullptr) || (startsWith ("mod") && endsWith (".delay_sync")) || startsWith ("curve.") || startsWith ("master.scale.")
        || startsWith ("master.tuning.") || startsWith ("master.mpe.") || (startsWith ("osc") && std::strstr (key, ".map.") != nullptr)
        // block B16: the wavetable position remap of an oscillator (on, slot, RISE, FALL) goes to its FrameRemap, not to a control
        || (startsWith ("osc") && std::strstr (key, ".remap.") != nullptr)
        // WP4: the static keys of the sample oscillator (scan, unison start and span are modulatable controls, bound normally)
        || (startsWith ("osc") && std::strstr (key, ".sample.") != nullptr && ! endsWith (".scan") && ! endsWith (".uni_start") && ! endsWith (".uni_span") && ! endsWith (".sample.slice"))
        // B28: the static keys of the granular oscillator (the modulatable ones are bound normally)
        || (startsWith ("osc") && std::strstr (key, ".gran.") != nullptr && ! isGranularModulatable (key))
        || (startsWith ("osc") && std::strstr (key, ".spec.") != nullptr && ! isSpectralModulatable (key))
        || (startsWith ("osc") && std::strstr (key, ".multi.") != nullptr && ! endsWith (".multi.timbre"))
        // B13: noise sample choice (procedural noise buffers)
        || std::strcmp (key, "noise.sample") == 0 || std::strcmp (key, "noise.type") == 0
        // B22: the effect instance pool (type, on, rack, order and the normalised parameters of a slot): the adapter builds the module and drives its controls
        || (startsWith ("fxi") && key[3] >= '0' && key[3] <= '9')
        // B15 (G12, G14, G18): handled by the adapter (note on handling, the bend wheel, the quality controls of the thread)
        || std::strcmp (key, "master.voice.same_note") == 0
        || std::strcmp (key, "master.bend_down_range") == 0 || std::strcmp (key, "master.bend_down_link") == 0
        || std::strcmp (key, "quality.oversample_4x") == 0 || std::strcmp (key, "quality.no_smoothing") == 0
        // B26 (GROUPA2, WP-07): the voice control: the adapter hands the whole configuration to the voice handler (applyVoiceControl)
        || startsWith ("voicectl.");
}

double convertParam (const Binding& binding, double plain, double sampleRate, double bpm)
{
    switch (binding.conv)
    {
        case Conv::quarticBeats: return std::pow (std::max (0.0, plain) * 60.0 / std::max (bpm, 1.0), 0.25);
        case Conv::linear:     return plain * binding.a + binding.b;
        case Conv::sqrtScaled: return std::sqrt (std::max (0.0, plain * binding.a));
        case Conv::quarticMs:  return std::pow (std::max (0.0, plain) * 0.001, 0.25);
        case Conv::log2Ms:     return std::max (binding.a, std::log2 (std::max (plain * 0.001, 1.0e-12)));
        case Conv::log2Hz:     return std::log2 (std::max (plain, 1.0e-6));
        case Conv::log2PeriodMs: return std::log2 (1000.0 / std::max (plain, 1.0e-3));
        case Conv::noteFromHz:
        {
            const double limited = std::clamp (plain, 20.0, std::max (20.0, 0.45 * sampleRate));
            return 12.0 * std::log2 (limited / kMidi0Hz);
        }
        case Conv::index:      return (binding.maxIndex >= 0 && plain > binding.maxIndex ? static_cast<double> (binding.maxIndex) : plain) + binding.b;
        case Conv::indexMap:
        {
            const int i = static_cast<int> (std::floor (plain + 0.5));
            return binding.map != nullptr ? binding.map[std::max (0, i)] : plain;
        }
        case Conv::volumeDb:
        {
            // the engine shows dB = sqrt (control) - 80, so control = (dB + 80)^2; -60 dB and below is silence (-80 dB)
            const double shown = plain <= -60.0 ? -80.0 : std::min (plain, 6.0);
            return (shown + 80.0) * (shown + 80.0);
        }
    }
    return plain;
}

double invertParam (const Binding& binding, double raw, double sampleRate, double bpm)
{
    (void) sampleRate;
    switch (binding.conv)
    {
        case Conv::quarticBeats: return std::pow (std::max (0.0, raw), 4.0) * std::max (bpm, 1.0) / 60.0;
        case Conv::linear:       return binding.a != 0.0 ? (raw - binding.b) / binding.a : std::nan ("");
        case Conv::sqrtScaled:   return binding.a != 0.0 ? std::max (0.0, raw) * std::max (0.0, raw) / binding.a : std::nan ("");
        case Conv::quarticMs:    return std::pow (std::max (0.0, raw), 4.0) * 1000.0;
        case Conv::log2Ms:       return std::exp2 (raw) * 1000.0;
        case Conv::log2Hz:       return std::exp2 (raw);
        case Conv::log2PeriodMs: return 1000.0 / std::exp2 (raw);
        case Conv::noteFromHz:   return kMidi0Hz * std::exp2 (raw / 12.0);
        case Conv::volumeDb:     return std::sqrt (std::max (0.0, raw)) - 80.0;
        case Conv::index:
        case Conv::indexMap:     return std::nan ("");
    }
    return std::nan ("");
}

void resolveFxOrder (const float* choices, int* order, int count)
{
    bool used[kNumFxSlots] = {};
    count = std::min (count, static_cast<int> (kNumFxSlots));
    for (int position = 0; position < count; ++position)
    {
        const float f = choices[position];
        int effect = f >= 0.0f && f < static_cast<float> (count) ? static_cast<int> (f + 0.5f) : -1;
        if (effect < 0 || effect >= count || used[effect])
        {
            effect = 0;
            while (used[effect])
                ++effect; // always one is free: at most position effects are used so far
        }
        used[effect] = true;
        order[position] = effect;
    }
}

ControlRange vitalControlRange (const std::string& control)
{
    ControlRange range;
    if (sbvital::Parameters::isParameter (control))
    {
        const auto& d = sbvital::Parameters::getDetails (control);
        range.exists = true;
        range.min = d.min;
        range.max = d.max;
        range.def = d.default_value;
    }
    return range;
}

Conversion vitalConvert (size_t paramIndex, double plain, double sampleRate)
{
    Conversion c;
    for (const auto& binding : vitalBindings())
        if (binding.param == paramIndex)
        {
            c.bound = true;
            c.control = binding.control;
            c.raw = convertParam (binding, plain, sampleRate);
            c.range = vitalControlRange (binding.control);
            break;
        }
    return c;
}

const Binding* vitalBindingOf (size_t paramIndex)
{
    static const std::vector<const Binding*> index = []
    {
        std::vector<const Binding*> v (paramCount(), nullptr);
        for (const auto& binding : vitalBindings())
            if (binding.param < v.size() && v[binding.param] == nullptr)
                v[binding.param] = &binding;
        return v;
    }();
    return paramIndex < index.size() ? index[paramIndex] : nullptr;
}

std::string vitalControlOf (size_t paramIndex)
{
    for (const auto& binding : vitalBindings())
        if (binding.param == paramIndex)
            return binding.control;
    return {};
}
} // namespace synthyboy
