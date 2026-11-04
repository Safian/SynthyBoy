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
constexpr int kWarpMap[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11 }; // our warp types -> the engine's (9 and 12 would use the sample)

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
    void hzNote (const std::string& key, const std::string& control) { add (key, control, Conv::noteFromHz); }
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
    b.add ("master.portamento.time", "portamento_time", Conv::log2Ms, -10.0);
    b.add ("master.portamento.slope", "portamento_slope", Conv::linear);
    b.choice ("master.portamento.always", "portamento_force");
    b.choice ("master.portamento.scaled", "portamento_scale");

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
        b.add (k + "phase", c + "phase", Conv::linear, 1.0 / 360.0);
        b.percent (k + "random_phase", c + "random_phase");
        b.choice (k + "destination", c + "destination");
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
    }

    // --- noise (the engine's built-in white noise sample) ------------------------------------------------------------
    b.choice ("noise.on", "sample_on");
    b.add ("noise.level", "sample_level", Conv::sqrtScaled, 0.01);
    b.percent ("noise.pan", "sample_pan");
    b.choice ("noise.destination", "sample_destination");

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
        b.choice (k + "model", c + "model");
        b.choice (k + "style", c + "style");
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
        b.add (k + "attack_curve", c + "attack_power", Conv::linear);
        b.add (k + "decay_curve", c + "decay_power", Conv::linear);
        b.add (k + "release_curve", c + "release_power", Conv::linear);
    }

    // --- LFOs ------------------------------------------------------------------------------------------------------
    for (int n = 1; n <= 8; ++n)
    {
        const std::string k = "lfo" + std::to_string (n) + ".", c = "lfo_" + std::to_string (n) + "_";
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
    for (int n = 1; n <= 4; ++n)
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
    }

    // --- effects ---------------------------------------------------------------------------------------------------
    const auto on = [&] (const char* key, const char* control, int slot) { b.add (std::string (key) + ".on", std::string (control) + "_on", Conv::index, 1.0, 0.0, Cap::fx, slot); };
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

    return std::move (b.out);
}
} // namespace

const std::vector<Binding>& vitalBindings()
{
    static const std::vector<Binding> table = build();
    return table;
}

bool isSpecialParam (size_t paramIndex)
{
    const char* key = paramTable()[paramIndex].key;
    const auto endsWith = [&] (const char* suffix)
    {
        const size_t n = std::strlen (key), m = std::strlen (suffix);
        return n >= m && std::strcmp (key + n - m, suffix) == 0;
    };
    return endsWith (".wavetable") || endsWith (".shape") || std::strcmp (key, "filter.routing") == 0 || std::strncmp (key, "fx.order.", 9) == 0
        || ((endsWith (".source") || endsWith (".destination")) && key[0] == 'm' && key[1] == 'o') || std::strcmp (key, "master.bend_range") == 0;
}

double convertParam (const Binding& binding, double plain, double sampleRate)
{
    switch (binding.conv)
    {
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
        case Conv::index:      return plain + binding.b;
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

void resolveFxOrder (const float* choices, int* order)
{
    bool used[kNumFxSlots] = {};
    for (int position = 0; position < kNumFxSlots; ++position)
    {
        const float f = choices[position];
        int effect = f >= 0.0f && f < static_cast<float> (kNumFxSlots) ? static_cast<int> (f + 0.5f) : -1;
        if (effect < 0 || effect >= kNumFxSlots || used[effect])
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

std::string vitalControlOf (size_t paramIndex)
{
    for (const auto& binding : vitalBindings())
        if (binding.param == paramIndex)
            return binding.control;
    return {};
}
} // namespace synthyboy
