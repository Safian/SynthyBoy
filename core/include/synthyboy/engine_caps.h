// SynthyBoy - core: engine capability bits (docs/ui-design/13-new-keys.md 6 and 11, rule R5).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// WP0 puts all 604 keys of state version 3 into the table at once; the DSP work packages WP1-WP8 make them audible one area at a time.
// An engine reports which areas it implements as a bitmask (IEngine::capabilities ()); by default a new engine reports none, so
// the new keys are stored, saved and read back but not live. The UI dims what is not live, the importer falls back (docs/ui-design/11),
// and IEngine::supportsParam () is false for a new key until every bit it needs is set.
#pragma once

#include <cstddef>
#include <cstdint>

namespace synthyboy
{
using EngineCapsMask = uint64_t;

/** One bit per area of new functionality. The bit values are part of no file format, but they are never renumbered. */
enum EngineCaps : EngineCapsMask
{
    kCapNone = 0,
    kCapEventContract    = 1ull << 0,  // WP1: sustain pedal, note expression, note-off velocity events
    kCapMacros58         = 1ull << 1,  // WP2: macro.5 .. macro.8
    kCapLfo910           = 1ull << 2,  // WP2: lfo9, lfo10
    kCapModSourcesExtra  = 1ull << 3,  // WP2/WP3: sources lift, slide, oct_note, poly_aftertouch
    kCapAuxSource        = 1ull << 4,  // WP2: modNN.aux_*
    kCapLfoOptions       = 1ull << 5,  // WP2: lfoN.mono / direction / host, Rossler style
    kCapEnvBeats         = 1ull << 6,  // WP2: envN.units, beat times, legato inverted
    kCapCurves           = 1ull << 7,  // WP2: velocity / note curve legato (+ content.curves)
    kCapCustomLfoShape   = 1ull << 8,  // WP2: lfoN.shape = Custom (content.lfo_shapes)
    kCapKeyScale         = 1ull << 9,  // WP3: master.scale.*
    kCapTuning           = 1ull << 10, // WP3: master.tuning.* (A4, scales, MTS-ESP)
    kCapMpe              = 1ull << 11, // WP3: master.mpe.*
    kCapOscMapping       = 1ull << 12, // WP3: oscN.map.*
    kCapSampleOsc        = 1ull << 13, // WP4: oscN.type, oscN.sample.*
    kCapUserWavetable    = 1ull << 14, // WP4: oscN.wavetable = Preset table (content.wavetables)
    kCapWarp2            = 1ull << 15, // WP4: oscN.warp2.*
    kCapMixerGraph       = 1ull << 16, // WP5: filter blend, sends, buses, DIRECT, filterN.destination, osc destinations 5 and 6
    kCapFxRacks          = 1ull << 17, // WP5: <fx>.rack, fx.order.10 .. 16, the 16-slot chain
    kCapFilterExtras     = 1ull << 18, // WP6: filterN.var / pan / level
    kCapFilterModels     = 1ull << 19, // WP6: filter models 8 .. 21 and styles 6 .. 11
    kCapFxUtility        = 1ull << 20, // WP7
    kCapFxHyperDimension = 1ull << 21, // WP7
    kCapFxBode           = 1ull << 22, // WP7
    kCapFxConvolve       = 1ull << 23, // WP7
    kCapFxSplitters      = 1ull << 24, // WP7
    kCapReverbForce      = 1ull << 25, // WP8
    kCapWtRemap          = 1ull << 26, // WTREMAP: oscN.remap.* (content.remaps), the frame position remap with RISE / FALL (G69)
    kCapFxLevel          = 1ull << 27, // WTREMAP: <fx>.level of chorus, compressor, delay, distortion, eq, flanger, phaser, reverb (G72)
    kCapCleanMode        = 1ull << 28, // WTREMAP: filterN.clean_mode, fxfilter.clean_mode (G54)
    kCapPitchUnison      = 1ull << 29, // B14: pitch modes, CRS, snap, unison laws/range, phase legato
    // block B17 (fx-extras, 13-new-keys.md 4.17); bits 30 .. 34 follow the B14 bit
    kCapFxCompressorLimit = 1ull << 30, // G73: compressor.limit / limit_latency_comp / crossover_low / crossover_high
    kCapFxDelayExtras     = 1ull << 31, // G74: delay.hq, delay.offset
    kCapFxDistortionExtras = 1ull << 32, // G75: distortion.type 6 .. 13, distortion.filter_keytrack, content.curves.shaper_a / shaper_b
    kCapFxPhaserDepth2    = 1ull << 33, // G76: phaser.depth2
    kCapFxReverbAlgos     = 1ull << 34, // G77: reverb.type, room, width, er_size, diff_a, diff_b
    // block B18 (osc-extras, 13-new-keys.md 4.18); bits 35 and 36 follow the B17 bits
    kCapSubOsc            = 1ull << 35, // G23: sub.* (sub oscillator)
    kCapNoiseExtras       = 1ull << 36, // G38: noise.sample / pitch / fine / phase / random_phase / keytrack / loop
    // block B15 (voiceglb, 13-new-keys.md 4.14); bits 37 .. 42 follow the B18 bits
    kCapVoiceSteal       = 1ull << 37, // G12: master.voice.same_note, master.voice.steal_priority
    kCapPolyLegato       = 1ull << 38, // G13: master.poly_legato
    kCapBendDownRange    = 1ull << 39, // G14: master.bend_down_range
    kCapStereoSpread     = 1ull << 40, // G15: master.stereo_spread
    kCapLfoSwing         = 1ull << 41, // G16: lfo1.swing .. lfo10.swing
    kCapQualityOptions   = 1ull << 42, // G18: quality.oversample_4x, quality.no_smoothing
    // block B20 (mod-amount, 13-new-keys.md 4.20); bit 43 follows the B15 bits
    kCapModAmount        = 1ull << 43, // B20: mod-amount (G1-G6: amount modulation, mod curves, stereo source, stereo routing, voice override)
    // block B21 (warp-env, 13-new-keys.md 4.22); bits 44 .. 49 follow the B20 bit
    kCapWarpAlt          = 1ull << 44, // G34: warp modes Sync Var, Bend +/-, Asym +/-, Flip, Mirror, Remap 1-4, Odd/Even (warp list entries 11 .. 24)
    kCapWarpFilter       = 1ull << 45, // G35: warp modes Filter LPF / HPF (25, 26)
    kCapWarpDist         = 1ull << 46, // G35: the 14 distortion warp modes (27 .. 40)
    kCapWarpFm           = 1ull << 47, // G36: warp modes FM Thru-Zero / Exp / Linear (41 .. 46)
    kCapWarpPdAm         = 1ull << 48, // G37: warp modes PD (47 .. 49) and AM (50, 51)
    kCapEnv1Toggle       = 1ull << 49, // G39: sub.env1, oscN.env1, noise.env1, filterN.env1
    // block B22 (fx-instances, 13-new-keys.md 4.23); bit 50 follows the B21 bits
    kCapFxInstances      = 1ull << 50, // G73: the pool of 16 effect instance slots fxi01 .. fxi16 (several instances of one effect)
    // block B23 (filters80, 13-new-keys.md); bit 51 follows the B22 bit
    kCapFilterModels2    = 1ull << 51, // WP-03 (FILTERS80): filter models 22..26 and styles for the remaining 43 Serum types
    // block B23 (GROUPA1, 13-new-keys.md 4.27); bits 52 .. 54 follow the WP-03 bit
    kCapSubPhase         = 1ull << 52, // WP-04: sub.phase, sub.phase_contiguous
    kCapNoiseColour      = 1ull << 53, // WP-04: noise.stereo, noise.filter (the colour samples White / Pink / Brown / Geiger)
    kCapNoiseSample      = 1ull << 54, // WP-04: noise.type = Sample (a content sample as the noise source), noise.bounce
    // block B24 (GROUPA1, 13-new-keys.md 4.28); bit 55
    kCapMpeRouting       = 1ull << 55, // WP-09: master.mpe.xyz_macro / y_modwheel / bend_to_x / y_bipolar
    // block B25 (GROUPA1, 13-new-keys.md 4.29); bits 56 and 57
    kCapFxDelayOffsetLeft = 1ull << 56, // WP-10: delay.offset_left
    kCapReverbFeedback   = 1ull << 57, // WP-10: reverb.feedback (Nitrous, Basin)
    // block B26 (GROUPA2, 13-new-keys.md 4.30); bits 58 and 59
    kCapVoiceControl     = 1ull << 58, // WP-07: voicectl.* (per voice pan / detune / cutoff / envelope time, randomisation, envelope and LFO scaling, oscillator mask)
    kCapVoiceSources     = 1ull << 59, // WP-07: the modulation sources Voice Index, Voice Mod 1, Voice Mod 2 (choice values 40 .. 42)
    // block B27 (GROUPB, 13-new-keys.md 4.33); bit 60
    kCapOscExtras2       = 1ull << 60, // WP-05: oscN.ratio_src, oscN.smooth_interp, oscN.sample.slice
    // GROUPB WP-06 (13-new-keys.md 4.34): choices only, no keys; bits 61 and 62
    kCapModSources2      = 1ull << 61, // WP-06: the modulation sources 43 .. 57 (Oscillator A .. Fixed)
    kCapWarpSources      = 1ull << 62, // WP-06: the warp modes 52 .. 75 (FM / PD / AM / RM from the Noise, Sub, Filter 1, Filter 2)
    // GROUPB WP-18 (13-new-keys.md 4.36): a choice only; bit 63, the last one
    kCapWavetableSawDown = 1ull << 63, // WP-18: oscN.wavetable = 14 "Saw Down" (the built-in falling saw of Vital's Init table)
};

// ---- the second capability word (GROUPC, 13-new-keys.md 4.37): the 64 bits above are all in use, so the capabilities of the new oscillator types live in a
// second word. IEngine::capabilities2 () reports it (a new engine reports none), requiredCaps2 () / requiredCapsForValue2 () name the bits a key / a value needs,
// supportsParam () needs both words, the UI dims with both. Word 1 is untouched: every earlier bit keeps its value.
using EngineCaps2Mask = uint64_t;

enum EngineCaps2 : EngineCaps2Mask
{
    kCap2None            = 0,
    kCap2Granular        = 1ull << 0, // WP-11 (B28): oscN.type = Granular, oscN.gran.*
    kCap2Spectral        = 1ull << 1, // WP-12 (B29): oscN.type = Spectral, oscN.spec.*
    kCap2SpectralWarps   = 1ull << 2, // WP-12: the spectral warp modes 76 .. 96 of the shared warp list
    kCap2Multisample     = 1ull << 3, // WP-13 (B30): oscN.type = Multisample, oscN.multi.*, content.instruments
    // block B31 (REMAINDER, 13-new-keys.md 4.41): the per slot RISE / FALL / DELAY of the modulation matrix
    kCap2ModSlew         = 1ull << 4, // REMAINDER item 1: modNN.rise, modNN.fall (a slew limiter on the source of the slot, modulatable)
    kCap2ModDelay        = 1ull << 5, // REMAINDER item 1: modNN.delay, modNN.delay_beats, modNN.delay_sync (the connection starts after the delay)
    // block B32 (REMAINDER item 2, 13-new-keys.md 4.42)
    kCap2SpectralFilterTable = 1ull << 6, // REMAINDER item 2: oscN.spec.flt_use / flt_table / flt_frame (a wavetable acts as the spectral filter)
    // block B33 (REFCOMPARE, 13-new-keys.md 4.47)
    kCap2SerumUnison     = 1ull << 7, // REFCOMPARE: oscN.unison.law = Serum (the level of the centre / outer voices by blend and voice count, the stereo spread of Serum 2.1.5)
    // block B34 (REFCOMPARE round 3, 13-new-keys.md 4.48)
    kCap2EqBand2         = 1ull << 8, // REFCOMPARE round 3: eq.mid2.* (the second peak / notch band of the EQ)
    kCap2SerumEq         = 1ull << 9, // REFCOMPARE round 3: eq.law = Serum (the RBJ biquads of Serum 2.1.5: shelf / peak / high pass / low pass with the measured Q laws, gains to +-24 dB)
    kCap2SerumConvolve   = 1ull << 11, // REFCOMPARE round 3: convolve.law = Serum (the impulse is normalised to the peak 1, decay is a T60 in seconds, size 0.1 .. 10x)
    kCap2SerumCompressor = 1ull << 10, // REFCOMPARE round 3: compressor.limit = Serum (the peak compressor of Serum 2.1.5: peak detector, dB gain smoothing, thresholds to -120 dB)
};

constexpr EngineCaps2Mask kCap2AllKnown = kCap2Granular | kCap2Spectral | kCap2SpectralWarps | kCap2Multisample | kCap2ModSlew | kCap2ModDelay | kCap2SpectralFilterTable | kCap2SerumUnison | kCap2EqBand2 | kCap2SerumEq | kCap2SerumCompressor | kCap2SerumConvolve;

/** The bits of the second word a key of the table needs (0 for every key that predates it). Total, no allocation. */
EngineCaps2Mask requiredCaps2 (size_t paramIndex) noexcept;
/** The bits of the second word a value of a choice parameter needs (oscN.type 2 .. 4, the warp modes 76 .. 96, a modulation destination that is a key of the blocks above). */
EngineCaps2Mask requiredCapsForValue2 (size_t paramIndex, float plain) noexcept;
inline bool capsCover2 (EngineCaps2Mask caps, EngineCaps2Mask needed) noexcept { return (caps & needed) == needed; }
/** Short stable name of a single bit of the second word ("granular"), "" for anything else. */
const char* capName2 (EngineCaps2Mask bit) noexcept;

/** The bits of block B21 that belong to the warp list (kCapEnv1Toggle is the switch of the mixer channels). */
constexpr EngineCapsMask kCapWarpAll = kCapWarpAlt | kCapWarpFilter | kCapWarpDist | kCapWarpFm | kCapWarpPdAm;

/** The bits of block B17. */
constexpr EngineCapsMask kCapFxExtrasAll = kCapFxCompressorLimit | kCapFxDelayExtras | kCapFxDistortionExtras | kCapFxPhaserDepth2 | kCapFxReverbAlgos;

/** Every defined bit (for tests and for masking a capability word read from somewhere else). */
constexpr EngineCapsMask kCapAllKnown = ~0ull; // all 64 bits are defined

/** The bits a key of the table needs to be live: 0 for the 880 keys of state version 2 (every engine knows them). Total, no allocation. */
EngineCapsMask requiredCaps (size_t paramIndex) noexcept;

/** The bits a value of a choice parameter needs (an appended entry of a version 2 list: oscN.wavetable 13, lfoN.shape 12, rlfoN.style 4,
    filter model >= 8, filter style >= 6, oscN / noise destination >= 5, a mod source >= 29, a mod destination >= 270); 0 otherwise. For
    a plain value of any other parameter it is requiredCaps (paramIndex). */
EngineCapsMask requiredCapsForValue (size_t paramIndex, float plain) noexcept;

/** True if caps cover everything a key needs. */
inline bool capsCover (EngineCapsMask caps, EngineCapsMask needed) noexcept
{
    return (caps & needed) == needed;
}

/** Short stable name of a single bit ("sample_osc"), "" for anything else. */
const char* capName (EngineCapsMask bit) noexcept;
} // namespace synthyboy
