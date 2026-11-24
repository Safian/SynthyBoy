// SynthyBoy - core tests: EngineCaps (docs/ui-design/13-new-keys.md 6 and 11): which bit a key of state version 3 needs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>
#include <cstring>
#include <string>

#include "synthyboy/engine.h"
#include "synthyboy/engine_caps.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "test_engines.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
size_t positionOf (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? static_cast<size_t> (findParamIndex (p->id)) : static_cast<size_t> (-1);
}

EngineCapsMask need (const char* key, float v)
{
    return requiredCapsForValue (positionOf (key), v);
}
} // namespace

TEST (every_new_key_needs_a_capability_and_no_old_key_does)
{
    for (size_t i = 0; i < kNumKeysV2; ++i)
        CHECK_EQ (requiredCaps (i), static_cast<EngineCapsMask> (kCapNone));

    EngineCapsMask all = 0;
    for (size_t i = kNumKeysV2; i < paramCount(); ++i)
    {
        const auto caps = requiredCaps (i);
        if (! CHECK (caps != kCapNone || requiredCaps2 (i) != kCap2None))   // the keys of the second word (B28 .. B31) need no bit of the first
            std::printf ("    %s\n", paramTable()[i].key);
        all |= caps;
    }
    // every defined bit has a name and is used by some key; five need no key: the event contract, and four that gate values of
    // lists (appended_choice_values_need_a_capability)
    const EngineCapsMask valueOnly = kCapEventContract | kCapModSourcesExtra | kCapCustomLfoShape | kCapUserWavetable | kCapFilterModels;
    for (int bit = 0; bit < 30; ++bit)
    {
        const EngineCapsMask b = 1ull << bit;
        CHECK (std::strlen (capName (b)) > 0);
        if ((valueOnly & b) == 0 && ! CHECK ((all & b) != 0))
            std::printf ("    bit %s has no key\n", capName (b));
    }
    // block B17: its five bits sit apart from the work package bits
    for (const EngineCapsMask b : { kCapFxCompressorLimit, kCapFxDelayExtras, kCapFxDistortionExtras, kCapFxPhaserDepth2, kCapFxReverbAlgos, kCapSubOsc, kCapNoiseExtras,
                                     kCapVoiceSteal, kCapPolyLegato, kCapBendDownRange, kCapStereoSpread, kCapLfoSwing, kCapQualityOptions, kCapModAmount, kCapEnv1Toggle, kCapFxInstances, kCapSubPhase, kCapNoiseColour, kCapNoiseSample, kCapMpeRouting, kCapFxDelayOffsetLeft, kCapReverbFeedback, kCapVoiceControl, kCapOscExtras2 })
    {
        CHECK (std::strlen (capName (b)) > 0);
        if (! CHECK ((all & b) != 0))
            std::printf ("    bit %s has no key\n", capName (b));
    }
    CHECK_EQ (all & ~kCapAllKnown, static_cast<EngineCapsMask> (0));
    CHECK_EQ (all >> 61, static_cast<EngineCapsMask> (0)); // bit 59 (the sources Voice Index / Mod) is needed by values of a choice list only; bit 58 by the voicectl keys
    // bits 44 .. 48 (the warp modes) are needed by values of a choice list only

    struct Case { const char* key; EngineCapsMask caps; };
    const Case cases[] = {
        { "macro.7", kCapMacros58 },
        { "sub.phase", kCapSubPhase },
        { "sub.phase_contiguous", kCapSubPhase },
        { "noise.stereo", kCapNoiseColour },
        { "noise.filter", kCapNoiseColour },
        { "noise.type", kCapNoiseSample },
        { "noise.bounce", kCapNoiseSample },
        { "master.mpe.xyz_macro", kCapMpeRouting },
        { "master.mpe.y_modwheel", kCapMpeRouting },
        { "master.mpe.bend_to_x", kCapMpeRouting },
        { "master.mpe.y_bipolar", kCapMpeRouting },
        { "master.mpe.on", kCapMpe },
        { "osc1.ratio_src", kCapOscExtras2 },
        { "osc2.smooth_interp", kCapOscExtras2 },
        { "osc3.sample.slice", kCapOscExtras2 },
        { "osc3.sample.slice_play", kCapSampleOsc },
        { "delay.offset_left", kCapFxDelayOffsetLeft },
        { "reverb.feedback", kCapReverbFeedback },
        { "delay.offset", kCapFxDelayExtras },
        { "osc2.remap.slot", kCapWtRemap },
        { "osc3.remap.fall", kCapWtRemap },
        { "chorus.level", kCapFxLevel },
        { "reverb.level", kCapFxLevel },
        { "filter2.clean_mode", kCapCleanMode },
        { "fxfilter.clean_mode", kCapCleanMode },
        { "osc1.pitch_mode", kCapPitchUnison },
        { "osc3.unison.range", kCapPitchUnison },
        { "osc2.phase_memory", kCapPitchUnison },
        { "voice.phase_legato", kCapPitchUnison },
        { "lfo9.rate", kCapLfo910 },
        { "lfo10.mono", kCapLfo910 | kCapLfoOptions },
        { "lfo3.direction", kCapLfoOptions },
        { "env2.attack_beats", kCapEnvBeats },
        { "curve.note.legato", kCapCurves },
        { "mod33.aux_inv", kCapAuxSource },
        { "master.scale.type", kCapKeyScale },
        { "master.tuning.a4", kCapTuning },
        { "master.mpe.on", kCapMpe },
        { "osc2.map.vel_high", kCapOscMapping },
        { "osc3.type", kCapSampleOsc },
        { "osc1.sample.scan", kCapSampleOsc },
        { "osc1.warp2.amount", kCapWarp2 },
        { "osc1.filter_blend", kCapMixerGraph },
        { "noise.bus2_send", kCapMixerGraph },
        { "filter2.bus1_send", kCapMixerGraph },
        { "filter1.destination", kCapMixerGraph },
        { "bus2.level", kCapMixerGraph },
        { "direct.level", kCapMixerGraph },
        { "filter1.var", kCapFilterExtras },
        { "fxfilter.level", kCapFilterExtras },
        { "fx.order.12", kCapFxRacks },
        { "fxfilter.rack", kCapFxRacks },
        { "reverb.force_enable", kCapReverbForce },
        { "hyperdim.rate", kCapFxHyperDimension },
        { "utility.width", kCapFxUtility },
        { "bode.shift", kCapFxBode },
        { "convolve.ir", kCapFxConvolve },
        { "splitter_lmh.freq_low", kCapFxSplitters },
        { "compressor.limit", kCapFxCompressorLimit },
        { "compressor.crossover_high", kCapFxCompressorLimit },
        { "delay.hq", kCapFxDelayExtras },
        { "delay.offset", kCapFxDelayExtras },
        { "distortion.filter_keytrack", kCapFxDistortionExtras },
        { "phaser.depth2", kCapFxPhaserDepth2 },
        { "reverb.type", kCapFxReverbAlgos },
        { "reverb.diff_b", kCapFxReverbAlgos },
        { "sub.waveform", kCapSubOsc },
        { "noise.pitch", kCapNoiseExtras },
        { "master.voice.same_note", kCapVoiceSteal },
        { "master.voice.steal_priority", kCapVoiceSteal },
        { "master.poly_legato", kCapPolyLegato },
        { "master.bend_down_range", kCapBendDownRange },
        { "master.bend_down_link", kCapBendDownRange },
        { "master.stereo_spread", kCapStereoSpread },
        { "lfo1.swing", kCapLfoSwing },
        { "lfo10.swing", kCapLfo910 | kCapLfoSwing },
        { "quality.oversample_4x", kCapQualityOptions },
        { "quality.no_smoothing", kCapQualityOptions },
        { "master.stereo_routing", kCapModAmount },
        { "master.voice_override", kCapModAmount },
    };
    for (const auto& c : cases)
        if (! CHECK_EQ (requiredCaps (positionOf (c.key)), c.caps))
            std::printf ("    %s\n", c.key);
}

TEST (appended_choice_values_need_a_capability)
{
    CHECK_EQ (need ("osc1.wavetable", 12.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("osc1.wavetable", 13.0f), static_cast<EngineCapsMask> (kCapUserWavetable));
    CHECK_EQ (need ("lfo4.shape", 11.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("lfo4.shape", 12.0f), static_cast<EngineCapsMask> (kCapCustomLfoShape));
    CHECK_EQ (need ("rlfo2.style", 4.0f), static_cast<EngineCapsMask> (kCapLfoOptions));
    CHECK_EQ (need ("filter1.model", 7.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("filter1.model", 21.0f), static_cast<EngineCapsMask> (kCapFilterModels));
    CHECK_EQ (need ("filter1.model", 22.0f), static_cast<EngineCapsMask> (kCapFilterModels2));
    CHECK_EQ (need ("filter2.model", 26.0f), static_cast<EngineCapsMask> (kCapFilterModels2));
    CHECK_EQ (need ("fxfilter.model", 21.0f), static_cast<EngineCapsMask> (kCapFilterModels));
    CHECK_EQ (need ("fxfilter.model", 26.0f), static_cast<EngineCapsMask> (kCapFilterModels2));
    CHECK_EQ (need ("filter2.style", 5.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("filter2.style", 6.0f), static_cast<EngineCapsMask> (kCapFilterModels));
    CHECK_EQ (need ("osc3.destination", 4.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("noise.destination", 5.0f), static_cast<EngineCapsMask> (kCapMixerGraph));
    CHECK_EQ (need ("fx.order.3", 8.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("fx.order.3", 9.0f), static_cast<EngineCapsMask> (kCapFxRacks));
    CHECK_EQ (need ("mod01.source", 28.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("mod01.source", 31.0f), static_cast<EngineCapsMask> (kCapMacros58));
    CHECK_EQ (need ("mod01.source", 34.0f), static_cast<EngineCapsMask> (kCapLfo910));
    CHECK_EQ (need ("mod01.source", 38.0f), static_cast<EngineCapsMask> (kCapModSourcesExtra));
    CHECK_EQ (need ("mod01.source", 39.0f), static_cast<EngineCapsMask> (kCapModAmount));
    CHECK_EQ (need ("mod01.source", 40.0f), static_cast<EngineCapsMask> (kCapVoiceSources));
    CHECK_EQ (need ("mod01.source", 42.0f), static_cast<EngineCapsMask> (kCapVoiceSources));
    CHECK_EQ (need ("mod01.source", 43.0f), static_cast<EngineCapsMask> (kCapModSources2));   // GROUPB WP-06
    CHECK_EQ (need ("mod01.source", 57.0f), static_cast<EngineCapsMask> (kCapModSources2));
    CHECK_EQ (need ("mod01.aux_source", 50.0f), static_cast<EngineCapsMask> (kCapAuxSource | kCapModSources2));
    CHECK_EQ (need ("osc1.warp.type", 51.0f), static_cast<EngineCapsMask> (kCapWarpPdAm));
    CHECK_EQ (need ("osc2.warp.type", 52.0f), static_cast<EngineCapsMask> (kCapWarpSources));
    CHECK_EQ (need ("osc2.warp2.type", 52.0f), static_cast<EngineCapsMask> (kCapWarp2 | kCapWarpSources));
    CHECK_EQ (need ("osc3.warp.type", 75.0f), static_cast<EngineCapsMask> (kCapWarpSources));
    CHECK_EQ (need ("mod01.aux_source", 41.0f), static_cast<EngineCapsMask> (kCapAuxSource | kCapVoiceSources));
    CHECK_EQ (requiredCaps (static_cast<size_t> (findParamIndex (findParamByKey ("voicectl.s4.cutoff")->id))), static_cast<EngineCapsMask> (kCapVoiceControl));
    CHECK_EQ (requiredCaps (static_cast<size_t> (findParamIndex (findParamByKey ("voicectl.osc.sub")->id))), static_cast<EngineCapsMask> (kCapVoiceControl));
    CHECK_EQ (need ("mod09.destination", 269.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("mod09.destination", 270.0f), static_cast<EngineCapsMask> (kCapMacros58));
    CHECK_EQ (need ("mod09.destination", 403.0f), static_cast<EngineCapsMask> (kCapFxSplitters));
    CHECK_EQ (need ("mod09.destination", 424.0f), static_cast<EngineCapsMask> (kCapFxCompressorLimit)); // compressor.crossover_low
    CHECK_EQ (need ("mod09.destination", 431.0f), static_cast<EngineCapsMask> (kCapFxReverbAlgos));   // reverb.diff_b
    CHECK_EQ (need ("mod09.destination", 453.0f), static_cast<EngineCapsMask> (kCapModAmount));       // master.stereo_routing
    CHECK_EQ (need ("mod09.destination", 454.0f), static_cast<EngineCapsMask> (kCapModAmount));       // mod01.amount
    CHECK_EQ (need ("mod09.destination", 519.0f), static_cast<EngineCapsMask> (kCapModAmount));       // master.polyphony
    CHECK_EQ (need ("distortion.type", 5.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("distortion.type", 6.0f), static_cast<EngineCapsMask> (kCapFxDistortionExtras));
    CHECK_EQ (need ("distortion.type", 13.0f), static_cast<EngineCapsMask> (kCapFxDistortionExtras));
    CHECK_EQ (need ("mod09.destination", 442.0f), static_cast<EngineCapsMask> (kCapStereoSpread));
    CHECK_EQ (need ("mod09.destination", 443.0f), static_cast<EngineCapsMask> (kCapLfoSwing));
    CHECK_EQ (need ("mod09.destination", 452.0f), static_cast<EngineCapsMask> (kCapLfo910 | kCapLfoSwing));
    CHECK_EQ (need ("master.volume", 5.0f), static_cast<EngineCapsMask> (kCapNone));
    CHECK_EQ (need ("macro.5", 50.0f), static_cast<EngineCapsMask> (kCapMacros58));
    CHECK (capsCover (kCapMacros58 | kCapLfo910, kCapLfo910) && ! capsCover (kCapMacros58, kCapMacros58 | kCapLfo910));
    CHECK_EQ (requiredCapsForValue (999999, 0.0f), static_cast<EngineCapsMask> (kCapNone));
}

TEST (engines_report_only_known_capabilities_and_never_change_them)
{
    // every bit an engine reports is a defined bit, the report does not change with prepare() or playing, and supportsParam () follows it
    for (const auto& c : sbtest::engineCases())
    {
        sbtest::Context ctx (c.name);
        auto e = sbtest::makeEngine (c);
        CHECK (e != nullptr);
        const EngineCapsMask before = e->capabilities();
        CHECK_EQ (before & ~kCapAllKnown, static_cast<EngineCapsMask> (0));
        e->process (nullptr, 0, nullptr, nullptr, 0);
        CHECK_EQ (e->capabilities(), before);
        for (size_t i = kNumKeysV2; i < paramCount(); ++i)
            if (! capsCover (before, requiredCaps (i)))
                CHECK (! e->supportsParam (paramTable()[i].id)); // a key whose capability bit is not set is never claimed
    }
}

// GROUPC: the second capability word (all 64 bits of the first are in use)
TEST (second_capability_word_names_the_bits_of_the_new_oscillator_types)
{
    const auto need2 = [] (const char* key, float v) { return requiredCapsForValue2 (positionOf (key), v); };
    CHECK_EQ (need2 ("osc1.type", 0.0f), kCap2None);
    CHECK_EQ (need2 ("osc1.type", 1.0f), kCap2None);
    CHECK_EQ (need2 ("osc1.type", 2.0f), static_cast<EngineCaps2Mask> (kCap2Granular));
    CHECK_EQ (need2 ("osc2.warp.type", 3.0f), kCap2None);   // oscN.warp.type is not oscN.type
    CHECK_EQ (need2 ("osc2.warp2.type", 75.0f), kCap2None);
    CHECK_EQ (requiredCaps2 (positionOf ("osc3.gran.density")), static_cast<EngineCaps2Mask> (kCap2Granular));
    CHECK_EQ (requiredCaps2 (positionOf ("osc3.sample.scan")), kCap2None);
    CHECK_EQ (requiredCaps2 (positionOf ("osc1.level")), kCap2None);
    CHECK_EQ (requiredCaps2 (positionOf ("master.volume")), kCap2None);
    CHECK_EQ (requiredCaps2 (999999), kCap2None);
    // a modulation destination that is a key of the new blocks needs the bit of that key
    const int destination = modDestinationChoiceOf (positionOf ("osc2.gran.position"));
    CHECK (destination > 269);
    CHECK_EQ (need2 ("mod01.destination", static_cast<float> (destination)), static_cast<EngineCaps2Mask> (kCap2Granular));
    CHECK_EQ (need2 ("mod01.destination", 100.0f), kCap2None);
    CHECK (capsCover2 (kCap2Granular | kCap2Spectral, kCap2Spectral) && ! capsCover2 (kCap2Granular, kCap2Granular | kCap2Spectral));
    CHECK_EQ (std::string (capName2 (kCap2Granular)), "granular");
    // the first word did not change: bit 63 is the last one and nothing of the new blocks claims it
    CHECK_EQ (kCapWavetableSawDown, 1ull << 63);
    // every engine reports only known bits of the second word, and a key whose bits are missing is never claimed
    for (const auto& c : sbtest::engineCases())
    {
        sbtest::Context ctx (c.name);
        auto e = sbtest::makeEngine (c);
        CHECK (e != nullptr);
        const EngineCaps2Mask caps2 = e->capabilities2();
        CHECK_EQ (caps2 & ~kCap2AllKnown, kCap2None);
        for (size_t i = kNumKeysV2; i < paramCount(); ++i)
            if (! capsCover2 (caps2, requiredCaps2 (i)))
                CHECK (! e->supportsParam (paramTable()[i].id));
    }
}
