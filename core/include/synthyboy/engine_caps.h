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
    kCapSubOsc           = 1ull << 26, // B13: sub oscillator (G23)
    kCapNoiseExtras      = 1ull << 27, // B13: noise colours and sample extras (G38)
};

/** Every defined bit (for tests and for masking a capability word read from somewhere else). */
constexpr EngineCapsMask kCapAllKnown = (1ull << 28) - 1;

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
