// SynthyBoy - core: which engine capability a key of state version 3 needs (see engine_caps.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/engine_caps.h"

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
    if (startsWith (key, "master.mpe."))
        return kCapMpe;

    if (startsWith (key, "osc"))
    {
        if (std::strstr (key, ".map.") != nullptr)
            return kCapOscMapping;
        if (std::strstr (key, ".warp2.") != nullptr)
            return kCapWarp2;
        if (endsWith (key, ".filter_blend") || endsWith (key, "_send"))
            return kCapMixerGraph;
        return kCapSampleOsc; // oscN.type, oscN.sample.*
    }
    if (startsWith (key, "noise."))
        return kCapMixerGraph; // filter_blend, bus sends

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
    return kCapNone;
}

EngineCapsMask sourceCaps (int choice) noexcept
{
    if (choice <= kNumModSourcesV2)
        return kCapNone;
    if (choice <= 32)
        return kCapMacros58;
    if (choice <= 34)
        return kCapLfo910;
    return kCapModSourcesExtra;
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
                const int target = modDestinationParamIndex (static_cast<size_t> (v));
                caps |= target >= 0 ? requiredCaps (static_cast<size_t> (target)) : kCapNone;
            }
            return caps;
        case ChoiceSet::list:
            break;
        case ChoiceSet::none:
            return caps;
    }

    if (endsWith (key, ".wavetable") && v >= kNumBuiltinWavetables)
        caps |= kCapUserWavetable;
    else if (startsWith (key, "lfo") && endsWith (key, ".shape") && v >= kLfoShapeCustom)
        caps |= kCapCustomLfoShape;
    else if (startsWith (key, "rlfo") && endsWith (key, ".style") && v >= 4)
        caps |= kCapLfoOptions;
    else if (endsWith (key, ".model") && v >= 8)
        caps |= kCapFilterModels;
    else if ((startsWith (key, "filter") || startsWith (key, "fxfilter")) && endsWith (key, ".style") && v >= 6)
        caps |= kCapFilterModels;
    else if ((startsWith (key, "osc") || startsWith (key, "noise")) && endsWith (key, ".destination") && v >= 5)
        caps |= kCapMixerGraph;
    else if (startsWith (key, "fx.order.") && v >= kNumLegacyFxSlots)
        caps |= kCapFxRacks;
    return caps;
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
        case kCapNone:             break;
    }
    return "";
}
} // namespace synthyboy
