// SynthyBoy - core: which engine capability a key of state version 3 needs (see engine_caps.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/engine_caps.h"
#include "synthyboy/fx_types.h"

#include <cstring>
#include <initializer_list>

#include "synthyboy/modulation.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace
{
bool startsWith (const char* s, const char* prefix) noexcept
{
    return std::strncmp (s, prefix, std::strlen (prefix)) == 0;
}

bool endsWith (const char* s, const char* suffix) noexcept
{
    const size_t n = std::strlen (s), m = std::strlen (suffix);
    return n >= m && std::strcmp (s + n - m, suffix) == 0;
}

/** The number after `prefix` ("lfo10.rate" -> 10), 0 if there is none. */
int numberAfter (const char* s, const char* prefix) noexcept
{
    if (! startsWith (s, prefix))
        return 0;
    int n = 0;
    for (const char* p = s + std::strlen (prefix); *p >= '0' && *p <= '9'; ++p)
        n = n * 10 + (*p - '0');
    return n;
}

EngineCapsMask classify (const char* key) noexcept
{
    // block B26 (GROUPA2, WP-07): voice control
    if (startsWith (key, "voicectl."))
        return kCapVoiceControl;
    // block B22: the keys of the effect instance pool (fxiNN.type / on / rack / order / pKK)
    if (startsWith (key, "fxi") && key[3] >= '0' && key[3] <= '9')
        return kCapFxInstances;
    // block B21: the ENV 1 buttons (sub.env1, oscN.env1, noise.env1, filterN.env1), before the module prefixes
    if (endsWith (key, ".env1"))
        return kCapEnv1Toggle;
    // block B16 (WTREMAP): before the module prefixes (osc..., filter..., reverb.... would claim these keys)
    if (std::strstr (key, ".remap.") != nullptr)
        return kCapWtRemap;
    if (endsWith (key, ".clean_mode"))
        return kCapCleanMode;
    if (endsWith (key, ".level"))
        for (const char* fx : { "chorus.", "compressor.", "delay.", "distortion.", "eq.", "flanger.", "phaser.", "reverb." })
            if (startsWith (key, fx))
                return kCapFxLevel;
    if (endsWith (key, ".rack"))
        return kCapFxRacks; // before the module prefixes: fxfilter.rack is not a filter key
    if (startsWith (key, "macro."))
        return kCapMacros58;

    if (startsWith (key, "lfo"))
    {
        const int n = numberAfter (key, "lfo");
        EngineCapsMask caps = n >= 9 ? kCapLfo910 : kCapNone;
        if (endsWith (key, ".mono") || endsWith (key, ".direction") || endsWith (key, ".host"))
            caps |= kCapLfoOptions;
        if (endsWith (key, ".swing"))
            caps |= kCapLfoSwing;
        return caps;
    }
    if (startsWith (key, "env"))
        return kCapEnvBeats; // envN.units, legato_inverted, *_beats: all the new env keys
    if (startsWith (key, "curve."))
        return kCapCurves;
    if (startsWith (key, "mod") && std::strstr (key, ".aux_") != nullptr)
        return kCapAuxSource;
    if (startsWith (key, "master.scale."))
        return kCapKeyScale;
    if (startsWith (key, "master.tuning."))
        return kCapTuning;
    if (std::strcmp (key, "master.mpe.xyz_macro") == 0 || std::strcmp (key, "master.mpe.y_modwheel") == 0
        || std::strcmp (key, "master.mpe.bend_to_x") == 0 || std::strcmp (key, "master.mpe.y_bipolar") == 0)
        return kCapMpeRouting;
    if (startsWith (key, "master.mpe."))
        return kCapMpe;
    if (startsWith (key, "master.voice."))
        return kCapVoiceSteal;
    if (std::strcmp (key, "master.poly_legato") == 0)
        return kCapPolyLegato;
    if (startsWith (key, "master.bend_down_"))
        return kCapBendDownRange;
    if (std::strcmp (key, "master.stereo_spread") == 0)
        return kCapStereoSpread;
    if (startsWith (key, "quality."))
        return kCapQualityOptions;

    if (startsWith (key, "osc"))
    {
        // block B27 (GROUPB, WP-05)
        if (endsWith (key, ".ratio_src") || endsWith (key, ".smooth_interp") || endsWith (key, ".sample.slice"))
            return kCapOscExtras2;
        if (std::strstr (key, ".pitch_") != nullptr || std::strstr (key, ".fixed_pitch") != nullptr
            || std::strstr (key, ".transpose_snap") != nullptr || std::strstr (key, ".unison.mode") != nullptr
            || std::strstr (key, ".unison.range") != nullptr || std::strstr (key, ".unison.blend_shape") != nullptr
            || std::strstr (key, ".unison.stack_repeat") != nullptr || std::strstr (key, ".unison.law") != nullptr || std::strstr (key, ".phase_legato") != nullptr
            || std::strstr (key, ".phase_memory") != nullptr)
            return kCapPitchUnison;
        if (std::strstr (key, ".map.") != nullptr)
            return kCapOscMapping;
        if (std::strstr (key, ".warp2.") != nullptr)
            return kCapWarp2;
        if (endsWith (key, ".filter_blend") || endsWith (key, "_send"))
            return kCapMixerGraph;
        return kCapSampleOsc; // oscN.type, oscN.sample.*
    }
    if (std::strcmp (key, "voice.phase_legato") == 0)
        return kCapPitchUnison;
    if (std::strcmp (key, "sub.phase") == 0 || std::strcmp (key, "sub.phase_contiguous") == 0)
        return kCapSubPhase;
    if (startsWith (key, "sub."))
        return kCapSubOsc;
    if (std::strcmp (key, "noise.stereo") == 0 || std::strcmp (key, "noise.filter") == 0)
        return kCapNoiseColour;
    if (std::strcmp (key, "noise.type") == 0 || std::strcmp (key, "noise.bounce") == 0)
        return kCapNoiseSample;
    if (startsWith (key, "noise."))
    {
        if (endsWith (key, ".filter_blend") || endsWith (key, "_send"))
            return kCapMixerGraph;
        if (std::strcmp (key, "noise.on") == 0 || std::strcmp (key, "noise.level") == 0
            || std::strcmp (key, "noise.pan") == 0 || std::strcmp (key, "noise.destination") == 0)
            return kCapNone;
        return kCapNoiseExtras;
    }

    if (startsWith (key, "filter") || startsWith (key, "fxfilter"))
    {
        if (endsWith (key, ".var") || endsWith (key, ".pan") || endsWith (key, ".level"))
            return kCapFilterExtras;
        return kCapMixerGraph; // filterN.destination, filterN.busM_send
    }
    if (startsWith (key, "bus") || startsWith (key, "direct.") || startsWith (key, "main."))
        return kCapMixerGraph;

    if (startsWith (key, "fx.order."))
        return kCapFxRacks;
    // block B25 (GROUPA1, WP-10), before the B17 prefixes
    if (std::strcmp (key, "delay.offset_left") == 0)
        return kCapFxDelayOffsetLeft;
    if (std::strcmp (key, "reverb.feedback") == 0)
        return kCapReverbFeedback;
    // block B17 (before the module prefixes of the version 2 effects)
    if (std::strcmp (key, "compressor.limit") == 0 || std::strcmp (key, "compressor.limit_latency_comp") == 0
        || endsWith (key, "compressor.crossover_low") || endsWith (key, "compressor.crossover_high"))
        return kCapFxCompressorLimit;
    if (std::strcmp (key, "delay.hq") == 0 || std::strcmp (key, "delay.offset") == 0)
        return kCapFxDelayExtras;
    if (std::strcmp (key, "distortion.filter_keytrack") == 0)
        return kCapFxDistortionExtras;
    if (std::strcmp (key, "phaser.depth2") == 0)
        return kCapFxPhaserDepth2;
    if (std::strcmp (key, "reverb.force_enable") != 0 && startsWith (key, "reverb."))
        return kCapFxReverbAlgos;
    if (startsWith (key, "reverb."))
        return kCapReverbForce;
    if (startsWith (key, "hyperdim."))
        return kCapFxHyperDimension;
    if (startsWith (key, "utility."))
        return kCapFxUtility;
    if (startsWith (key, "bode."))
        return kCapFxBode;
    if (startsWith (key, "convolve."))
        return kCapFxConvolve;
    if (startsWith (key, "splitter_"))
        return kCapFxSplitters;
    if (std::strcmp (key, "master.stereo_routing") == 0 || std::strcmp (key, "master.voice_override") == 0)
        return kCapModAmount;
    return kCapNone;
}

/** The bit of a warp mode of block B21 (warp list entry v >= kNumLegacyWarpTypes). */
EngineCapsMask warpTypeCaps (int v) noexcept
{
    if (v <= 24)
        return kCapWarpAlt;
    if (v <= 26)
        return kCapWarpFilter;
    if (v <= 40)
        return kCapWarpDist;
    if (v <= 46)
        return kCapWarpFm;
    if (v >= 52)
        return kCapWarpSources; // GROUPB WP-06: from the Noise, Sub and Filters
    return kCapWarpPdAm;
}

EngineCapsMask sourceCaps (int choice) noexcept
{
    if (choice <= kNumModSourcesV2)
        return kCapNone;
    if (choice <= 32)
        return kCapMacros58;
    if (choice <= 34)
        return kCapLfo910;
    if (choice <= 38)
        return kCapModSourcesExtra;
    if (choice == 39)
        return kCapModAmount;
    if (choice <= 42)
        return kCapVoiceSources;
    return kCapModSources2; // GROUPB WP-06: 43 .. 57
}
} // namespace

EngineCapsMask requiredCaps (size_t paramIndex) noexcept
{
    if (paramIndex < kNumKeysV2 || paramIndex >= paramCount())
        return kCapNone;
    return classify (paramTable()[paramIndex].key);
}

EngineCapsMask requiredCapsForValue (size_t paramIndex, float plain) noexcept
{
    if (paramIndex >= paramCount())
        return kCapNone;

    EngineCapsMask caps = requiredCaps (paramIndex);
    const ParamInfo& info = paramTable()[paramIndex];
    {
        // the type of a pool slot needs the capability of the module it stands for (a slot of type Bode needs what Bode needs)
        int slot = 0, number = 0;
        if (fxInstanceOfParam (paramIndex, slot, number) && number < 0 && info.choiceSet == ChoiceSet::list && std::strcmp (info.key + 6, "type") == 0)
        {
            const int type = static_cast<int> (plain + 0.5f);
            const int onIndex = type > 0 && type < kNumFxInstanceTypes ? fxTypeDescriptor (type).onIndex : -1;
            return caps | (onIndex >= 0 ? requiredCaps (static_cast<size_t> (onIndex)) : kCapNone);
        }
    }
    if (info.scale != Scale::stepped || ! (plain >= 0.0f && plain < 1.0e6f))
        return caps;

    const int v = static_cast<int> (plain + 0.5f);
    const char* key = info.key;
    switch (info.choiceSet)
    {
        case ChoiceSet::modSource:
            return caps | sourceCaps (v);
        case ChoiceSet::modDestination:
            if (v > 269)
            {
                if (v >= 454)
                    caps |= kCapModAmount;
                const int target = modDestinationParamIndex (static_cast<size_t> (v));
                caps |= target >= 0 ? requiredCaps (static_cast<size_t> (target)) : kCapNone;
            }
            return caps;
        case ChoiceSet::list:
            break;
        case ChoiceSet::none:
            return caps;
    }

    if (endsWith (key, ".wavetable") && v == kWavetableSawDown)
        caps |= kCapWavetableSawDown;   // GROUPB WP-18
    else if (endsWith (key, ".wavetable") && v >= kNumBuiltinWavetables)
        caps |= kCapUserWavetable;
    else if (startsWith (key, "lfo") && endsWith (key, ".shape") && v >= kLfoShapeCustom)
        caps |= kCapCustomLfoShape;
    else if (startsWith (key, "rlfo") && endsWith (key, ".style") && v >= 4)
        caps |= kCapLfoOptions;
    else if (endsWith (key, ".model") && v >= 22)
        caps |= kCapFilterModels2;
    else if (endsWith (key, ".model") && v >= 8)
        caps |= kCapFilterModels;
    else if ((startsWith (key, "filter") || startsWith (key, "fxfilter")) && endsWith (key, ".style") && v >= 6)
        caps |= kCapFilterModels;
    else if ((startsWith (key, "osc") || startsWith (key, "noise") || startsWith (key, "sub")) && endsWith (key, ".destination") && v >= 5)
        caps |= kCapMixerGraph;
    else if (startsWith (key, "fx.order.") && v >= kNumLegacyFxSlots)
        caps |= kCapFxRacks;
    else if (std::strcmp (key, "distortion.type") == 0 && v >= kNumLegacyDistortionTypes)
        caps |= kCapFxDistortionExtras;
    else if (startsWith (key, "osc") && (endsWith (key, ".warp.type") || endsWith (key, ".warp2.type")) && v >= kNumLegacyWarpTypes)
        caps |= warpTypeCaps (v);

    return caps;
}

EngineCaps2Mask requiredCaps2 (size_t paramIndex) noexcept
{
    if (paramIndex < kNumKeysV2 || paramIndex >= paramCount())
        return kCap2None;
    const char* key = paramTable()[paramIndex].key;
    if (startsWith (key, "mod") && std::strlen (key) > 6 && key[5] == '.')   // modNN.rise / fall / delay* (block B31)
    {
        if (std::strcmp (key + 6, "rise") == 0 || std::strcmp (key + 6, "fall") == 0)
            return kCap2ModSlew;
        if (startsWith (key + 6, "delay"))
            return kCap2ModDelay;
        return kCap2None;
    }
    if (! startsWith (key, "osc"))
        return kCap2None;
    if (std::strstr (key, ".unison.law") != nullptr)   // block B33
        return kCap2SerumUnison;
    if (std::strstr (key, ".gran.") != nullptr)
        return kCap2Granular;
    if (std::strstr (key, ".spec.flt_") != nullptr)   // block B32: a wavetable as the spectral filter
        return kCap2Spectral | kCap2SpectralFilterTable;
    if (std::strstr (key, ".spec.") != nullptr)
        return kCap2Spectral;
    if (std::strstr (key, ".multi.") != nullptr)
        return kCap2Multisample;
    return kCap2None;
}

EngineCaps2Mask requiredCapsForValue2 (size_t paramIndex, float plain) noexcept
{
    if (paramIndex >= paramCount())
        return kCap2None;
    EngineCaps2Mask caps = requiredCaps2 (paramIndex);
    const ParamInfo& info = paramTable()[paramIndex];
    if (info.scale != Scale::stepped || ! (plain >= 0.0f && plain < 1.0e6f))
        return caps;
    const int v = static_cast<int> (plain + 0.5f);
    if (info.choiceSet == ChoiceSet::modDestination)
    {
        const int target = v > 269 ? modDestinationParamIndex (static_cast<size_t> (v)) : -1;
        return caps | (target >= 0 ? requiredCaps2 (static_cast<size_t> (target)) : kCap2None);
    }
    if (info.choiceSet != ChoiceSet::list)
        return caps;
    const char* key = info.key;
    if (startsWith (key, "osc") && std::strlen (key) == 9 && endsWith (key, ".type")) // oscN.type (not oscN.warp.type)
    {
        if (v == kOscTypeGranular)
            caps |= kCap2Granular;
        else if (v == kOscTypeSpectral)
            caps |= kCap2Spectral;
        else if (v == kOscTypeMultisample)
            caps |= kCap2Multisample;
    }
    else if (startsWith (key, "osc") && (endsWith (key, ".warp.type") || endsWith (key, ".warp2.type")) && v >= kFirstSpectralWarp)
        caps |= kCap2SpectralWarps;
    return caps;
}

const char* capName2 (EngineCaps2Mask bit) noexcept
{
    switch (bit)
    {
        case kCap2Granular:      return "granular";
        case kCap2Spectral:      return "spectral";
        case kCap2SpectralWarps: return "spectral_warps";
        case kCap2Multisample:   return "multisample";
        case kCap2ModSlew:       return "mod_slew";
        case kCap2ModDelay:      return "mod_delay";
        case kCap2SpectralFilterTable: return "spectral_filter_table";
        case kCap2SerumUnison:   return "serum_unison";
        case kCap2None:          break;
    }
    return "";
}

const char* capName (EngineCapsMask bit) noexcept
{
    switch (bit)
    {
        case kCapEventContract:    return "event_contract";
        case kCapMacros58:         return "macros_5_8";
        case kCapLfo910:           return "lfo_9_10";
        case kCapModSourcesExtra:  return "mod_sources_extra";
        case kCapAuxSource:        return "aux_source";
        case kCapLfoOptions:       return "lfo_options";
        case kCapEnvBeats:         return "env_beats";
        case kCapCurves:           return "curves";
        case kCapCustomLfoShape:   return "custom_lfo_shape";
        case kCapKeyScale:         return "key_scale";
        case kCapTuning:           return "tuning";
        case kCapMpe:              return "mpe";
        case kCapOscMapping:       return "osc_mapping";
        case kCapSampleOsc:        return "sample_osc";
        case kCapUserWavetable:    return "user_wavetable";
        case kCapWarp2:            return "warp2";
        case kCapMixerGraph:       return "mixer_graph";
        case kCapFxRacks:          return "fx_racks";
        case kCapFilterExtras:     return "filter_extras";
        case kCapFilterModels:     return "filter_models";
        case kCapFxUtility:        return "fx_utility";
        case kCapFxHyperDimension: return "fx_hyper_dimension";
        case kCapFxBode:           return "fx_bode";
        case kCapFxConvolve:       return "fx_convolve";
        case kCapFxSplitters:      return "fx_splitters";
        case kCapReverbForce:      return "reverb_force";
        case kCapWtRemap:          return "wt_remap";
        case kCapFxLevel:          return "fx_level";
        case kCapCleanMode:        return "clean_mode";
        case kCapPitchUnison:      return "pitch_unison";
        case kCapFxCompressorLimit: return "fx_compressor_limit";
        case kCapFxDelayExtras:    return "fx_delay_extras";
        case kCapFxDistortionExtras: return "fx_distortion_extras";
        case kCapFxPhaserDepth2:   return "fx_phaser_depth2";
        case kCapFxReverbAlgos:    return "fx_reverb_algos";
        case kCapSubOsc:           return "sub_osc";
        case kCapNoiseExtras:      return "noise_extras";
        case kCapVoiceSteal:       return "voice_steal";
        case kCapPolyLegato:       return "poly_legato";
        case kCapBendDownRange:    return "bend_down_range";
        case kCapStereoSpread:     return "stereo_spread";
        case kCapLfoSwing:         return "lfo_swing";
        case kCapQualityOptions:   return "quality_options";
        case kCapModAmount:        return "mod_amount";
        case kCapWarpAlt:          return "warp_alt";
        case kCapWarpFilter:       return "warp_filter";
        case kCapWarpDist:         return "warp_distortion";
        case kCapWarpFm:           return "warp_fm";
        case kCapWarpPdAm:         return "warp_pd_am";
        case kCapEnv1Toggle:       return "env1_toggle";
        case kCapFxInstances:      return "fx_instances";
        case kCapFilterModels2:    return "filter_models2";
        case kCapSubPhase:         return "sub_phase";
        case kCapNoiseColour:      return "noise_colour";
        case kCapNoiseSample:      return "noise_sample";
        case kCapMpeRouting:       return "mpe_routing";
        case kCapFxDelayOffsetLeft: return "fx_delay_offset_left";
        case kCapReverbFeedback:   return "reverb_feedback";
        case kCapVoiceControl:     return "voice_control";
        case kCapVoiceSources:     return "voice_sources";
        case kCapOscExtras2:       return "osc_extras2";
        case kCapModSources2:      return "mod_sources2";
        case kCapWarpSources:      return "warp_sources";
        case kCapWavetableSawDown: return "wavetable_saw_down";
        case kCapNone:             break;
    }
    return "";
}
} // namespace synthyboy
