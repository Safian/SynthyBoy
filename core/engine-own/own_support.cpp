// SynthyBoy - own engine: implemented keys and capabilities.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "own_support.h"

#include <cstring>
#include <string>
#include <vector>

#include "synthyboy/params.h"

namespace synthyboy
{
namespace own
{
namespace
{
bool inList (const std::string& rest, std::initializer_list<const char*> names)
{
    for (const char* n : names)
        if (rest == n)
            return true;
    return false;
}

bool implementedKey (const std::string& k)
{
    auto dot = k.find ('.');
    if (dot == std::string::npos)
        return false;
    const std::string head = k.substr (0, dot), rest = k.substr (dot + 1);
    if (head == "master")
        return inList (rest, { "volume", "polyphony", "bend_range", "bend_down_range", "bend_down_link", "velocity_track", "transpose", "tune", "legato",
                               "portamento.time", "portamento.slope", "portamento.always", "portamento.scaled", "stereo_spread", "voice.same_note",
                               "voice.steal_priority", "poly_legato", "stereo_routing", "voice_override", "scale.key", "scale.type", "scale.mode",
                               "tuning.a4", "tuning.scale", "tuning.mts_esp", "tuning.mts_note_on_only", "mpe.on", "mpe.bend_range", "mpe.xyz_macro",
                               "mpe.y_modwheel", "mpe.bend_to_x", "mpe.y_bipolar" });
    if (head == "osc1" || head == "osc2" || head == "osc3")
    {
        if (rest.rfind ("unison.", 0) == 0)
            return inList (rest.substr (7), { "voices", "detune", "detune_curve", "spread", "blend", "mode", "range", "law" });
        if (rest.rfind ("warp.", 0) == 0 || rest.rfind ("warp2.", 0) == 0)
            return true;
        if (rest.rfind ("map.", 0) == 0)
            return inList (rest.substr (4), { "key_low", "key_high", "vel_low", "vel_high" });
        return inList (rest, { "on", "level", "pan", "transpose", "tune", "wavetable", "frame", "frame_spread", "phase", "random_phase", "destination", "env1",
                               "pitch_coarse", "pitch_track", "pitch_bend_track", "type" });
    }
    if (head == "sub")
        return inList (rest, { "on", "waveform", "octave", "tune", "level", "pan", "destination", "env1", "phase" });
    if (head == "noise")
        return inList (rest, { "on", "level", "pan", "destination", "env1", "sample", "stereo" });
    if (head == "filter1" || head == "filter2")
        return inList (rest, { "on", "model", "style", "cutoff", "resonance", "drive", "blend", "mix", "keytrack", "var", "level", "env1", "destination" });
    if (head == "filter")
        return rest == "routing";
    if (head.size() == 4 && head.rfind ("env", 0) == 0 && head[3] >= '1' && head[3] <= '6')
        return inList (rest, { "delay", "attack", "hold", "decay", "sustain", "release", "attack_curve", "decay_curve", "release_curve", "units", "legato_inverted",
                               "delay_beats", "attack_beats", "hold_beats", "decay_beats", "release_beats" });
    if (head.rfind ("lfo", 0) == 0 && head.size() >= 4)
        return inList (rest, { "shape", "rate", "sync", "division", "sync_type", "phase", "fade_in", "delay", "smooth", "smooth_time", "keytrack_transpose",
                               "keytrack_tune", "mono", "direction" });
    if (head.rfind ("rlfo", 0) == 0)
        return inList (rest, { "style", "rate", "sync", "division", "sync_type", "keytrack_transpose", "keytrack_tune" });
    if (head == "macro")
        return true;
    // ---- the effect chain (fx/)
    if (head == "chorus")
        return inList (rest, { "on", "mix", "rate", "sync", "division", "depth", "feedback", "delay1", "delay2", "voices", "filter_cutoff", "filter_spread", "level" });
    if (head == "compressor")
        return rest.rfind ("low.", 0) == 0 || rest.rfind ("mid.", 0) == 0 || rest.rfind ("high.", 0) == 0 || inList (rest, { "on", "mix", "attack", "release", "bands", "level", "crossover_low", "crossover_high" });
    if (head == "delay")
        return inList (rest, { "on", "mix", "feedback", "time", "time2", "sync", "division", "division2", "style", "filter_cutoff", "filter_spread", "level" });
    if (head == "distortion")
        return inList (rest, { "on", "type", "drive", "mix", "filter_order", "filter_cutoff", "filter_resonance", "filter_blend", "level" });
    if (head == "eq")
        return rest.rfind ("low.", 0) == 0 || rest.rfind ("mid.", 0) == 0 || rest.rfind ("high.", 0) == 0 || rest.rfind ("mid2.", 0) == 0 || inList (rest, { "on", "level" });
    if (head == "flanger" || head == "phaser")
        return inList (rest, { "on", "mix", "feedback", "rate", "sync", "division", "depth", "center", "phase_offset", "blend", "level" });
    if (head == "reverb")
        return inList (rest, { "on", "mix", "size", "decay", "predelay", "pre_low_cut", "pre_high_cut", "high_shelf_cutoff", "high_shelf_gain", "chorus_amount", "chorus_rate",
                               "type", "width", "diff_a", "diff_b", "level", "force_enable" });
    if (head == "fxfilter")
        return inList (rest, { "on", "model", "style", "cutoff", "resonance", "drive", "blend", "mix", "var", "level" });
    if (head == "hyperdim")
        return inList (rest, { "on", "rate", "unison", "detune", "mix", "dim_size", "dim_mix", "level" });
    if (head == "utility")
        return inList (rest, { "on", "polarity_l", "polarity_r", "lpf", "hpf", "mono_bass", "mono_freq", "width", "pan", "mix", "level" });
    if (head == "fx" && rest.rfind ("order.", 0) == 0)
        return true;
    if (head.rfind ("mod", 0) == 0 && head.size() == 5)
        return inList (rest, { "source", "destination", "amount", "power", "bipolar", "bypass", "aux_source", "aux_inv", "aux_power", "aux_output" });
    return false;
}
} // namespace

bool isImplemented (size_t paramIndex) noexcept
{
    static const std::vector<bool> table = []
    {
        std::vector<bool> v (paramCount(), false);
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = implementedKey (paramTable()[i].key);
        return v;
    }();
    return paramIndex < table.size() && table[paramIndex];
}

EngineCapsMask ownCapabilities() noexcept
{
    return kCapEventContract | kCapMacros58 | kCapLfo910 | kCapModSourcesExtra | kCapAuxSource | kCapSubOsc | kCapEnv1Toggle | kCapVoiceSteal
         | kCapBendDownRange | kCapStereoSpread | kCapWarp2 | kCapWarpDist | kCapWarpFm | kCapWarpPdAm | kCapFilterModels | kCapFilterModels2
         | kCapModSources2 | kCapWavetableSawDown
         | kCapKeyScale | kCapTuning | kCapMpe | kCapMpeRouting | kCapPolyLegato | kCapEnvBeats
         | kCapFxLevel | kCapFxUtility | kCapFxHyperDimension | kCapFxCompressorLimit | kCapFxDistortionExtras;
}

EngineCaps2Mask ownCapabilities2() noexcept
{
    return kCap2EqBand2;
}
} // namespace own
} // namespace synthyboy
