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
    kCapFilterModels2    = 1ull << 43, // WP-03 (FILTERS80): filter models 22..26 and styles for the remaining 43 Serum types
};

/** The bits of block B17. */
constexpr EngineCapsMask kCapFxExtrasAll = kCapFxCompressorLimit | kCapFxDelayExtras | kCapFxDistortionExtras | kCapFxPhaserDepth2 | kCapFxReverbAlgos;

/** Every defined bit (for tests and for masking a capability word read from somewhere else). */
constexpr EngineCapsMask kCapAllKnown = (1ull << 44) - 1;

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
