// SynthyBoy - core: parameter table (single source for plugin params, state I/O and UI), see docs/ARCHITECTURE.md 5.1.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The table is our own design of a synthesizer feature set (docs/ARCHITECTURE.md 2.2): our keys, names, units and ranges.
// It is constexpr, lives in core/src/param_table.cpp and is described for humans in docs/PARAMETERS.md (generated).
#pragma once

#include <cstddef>
#include <cstdint>

#include "hash.h"

namespace synthyboy
{
/** Mapping between the plain value and the normalised 0..1 position (juce::NormalisableRange semantics). */
enum class Scale : uint8_t
{
    linear,   // plain = min + (max - min) * normalised
    exp_skew, // normalised = pow (proportion, skew); skew < 1 gives more room to the low end
    stepped   // choice parameter: integer steps 0..max, interval 1, display names
};

enum class Unit : uint8_t
{
    none,
    dB,
    Hz,
    ms,
    percent,
    semitone,
    cents,
    beats,
    degrees,
    voices
};

enum ParamFlags : uint32_t
{
    kParamAutomatable    = 1u << 0,
    kParamModulatable    = 1u << 1, // can be the destination of a modulation matrix slot (listed in the destination choice)
    kParamPerNote        = 1u << 2,
    kParamHidden         = 1u << 3,
    kParamStepped        = 1u << 4,
    kParamEngineSpecific = 1u << 5  // meaning/range depends on the engine
};

/** Where the names of a stepped parameter come from. */
enum class ChoiceSet : uint8_t
{
    none,           // continuous parameter
    list,           // ParamInfo::choices
    modSource,      // 0 = none, then the table of modulation.h (modSourceTable)
    modDestination  // 0 = none, then every parameter flagged kParamModulatable, in table order
};

struct ParamInfo
{
    uint32_t id;         // == hashKey (key); stable, never reused or reordered, new ids only appended
    const char* key;     // stable string key ("osc1.level"), used in state files, never renamed
    const char* name;    // display name, unique in the table ("Osc 1 Level")
    const char* group;   // section hierarchy, levels separated by " / " ("Osc 1 / Unison")
    float min, max, def; // plain values
    float skew;          // used by Scale::exp_skew
    float interval;      // snap grid of the plain value, 0 = continuous
    Scale scale;
    Unit unit;
    uint32_t flags;
    const char* const* choices; // ChoiceSet::list: numChoices names
    uint16_t numChoices;        // ChoiceSet::list: number of names (the range is 0 .. numChoices - 1)
    ChoiceSet choiceSet;
    uint8_t decimals;           // decimals of the display text of a continuous parameter
    uint16_t order;             // display order (position in the table; set when the table is built)
};

/** Upper bound of the table size; fixed-size containers (ParamValues, engine) are sized with it. Raised from 2048 to 4096 for the FX
    instance pool (block B22, docs/ui-design/18-FX-INSTANCES.md): it is a compile-time constant and part of no file format (ids are
    hashKey (key), the state stores keys), so the raise changes no saved state. Table indices are stored as uint16_t (IdIndex, order). */
constexpr size_t kMaxParams = 4096;
static_assert (kMaxParams <= 65535, "table indices are stored as uint16_t");

/** The table of state version 2 had 880 keys; the keys of version 3 (docs/ui-design/13-new-keys.md) are appended after them, so the
    first kNumKeysV2 table positions are the ones every older state, preset and engine knows. */
constexpr size_t kNumKeysV2 = 880;

/** Ids of the parameters the engines and tests address by name (== hashKey of the keys, see core/src/param_table.cpp). */
constexpr uint32_t kParamMasterVolume   = hashKey ("master.volume");
constexpr uint32_t kParamMasterPolyphony = hashKey ("master.polyphony");
constexpr uint32_t kParamMasterBendRange = hashKey ("master.bend_range");
constexpr uint32_t kParamMasterTranspose = hashKey ("master.transpose");
constexpr uint32_t kParamMasterTune      = hashKey ("master.tune");
constexpr uint32_t kParamOsc1Wavetable   = hashKey ("osc1.wavetable");
constexpr uint32_t kParamFilter1On       = hashKey ("filter1.on");
constexpr uint32_t kParamFilter1Cutoff   = hashKey ("filter1.cutoff");

/** The built-in wavetables of oscillator 1..3 (the first choice names of oscN.wavetable); index 0 = Sine, 1 = Saw. */
constexpr int kNumBuiltinWavetables = 13;
/** Choices of oscN.wavetable: the built-in tables plus index kNumBuiltinWavetables = "Preset table" (content.wavetables[]) and index 14 = "Saw Down" (GROUPB WP-18). */
constexpr int kNumWavetableChoices = kNumBuiltinWavetables + 2;
constexpr int kWavetableSine = 0;
constexpr int kWavetableSaw  = 1;
constexpr int kWavetablePresetTable = 13;
/** GROUPB WP-18 (13-new-keys.md 4.36): the choice appended after "Preset table": "Saw Down", the falling saw whose jump is at mid cycle (-saw (phase + 180 degrees)), the waveform of
    Vital's Init table, a built-in single cycle table (needs kCapWavetableSawDown). */
constexpr int kWavetableSawDown = 14;
/** The built-in LFO shapes (the first choice names of lfoN.shape). */
constexpr int kNumLfoShapes = 12;
/** The choice of lfoN.shape after the built-in ones: "Custom" (point list in content.lfo_shapes[]). */
constexpr int kLfoShapeCustom = 12;
/** Number of effect slots of the FX chain (fx.order.1 .. fx.order.16) and of effect modules (the choices of fx.order.N). */
constexpr int kNumFxSlots = 16;
/** The effect slots and modules of state version 2 (the nine Vital-style effects); the engines implement these only. */
constexpr int kNumLegacyFxSlots = 9;
/** distortion.type entries 0 .. 5 are the ones of state version 2; 6 .. 13 were appended in block B17. */
constexpr int kNumLegacyDistortionTypes = 6;
/** oscN.warp.type / oscN.warp2.type entries 0 .. 10 are the Vital modes of state version 2; 11 .. 51 were appended in block B21
    (Serum 2 warp modes: 11 Sync Var .. 51 AM from the second other oscillator, 13-new-keys.md 4.22). */
constexpr int kNumLegacyWarpTypes = 11;
/** Number of entries of the warp lists after GROUPB WP-06 (52 .. 75: FM Thru-Zero / Exp / Linear, PD, AM and RM from the Noise, Sub, Filter 1 and Filter 2; 52 after block B21). */
constexpr int kNumWarpTypes = 99; // GROUPC WP-12: 76 .. 98 are the 23 spectral warps

/** The values of oscN.type (13-new-keys.md 4.9 and, from GROUPC, 4.37 .. 4.39): 0 and 1 are the wavetable and sample oscillators of state version 3, 2 .. 4 were appended in the blocks B28 .. B30. */
constexpr int kOscTypeWavetable = 0;
constexpr int kOscTypeSample = 1;
constexpr int kOscTypeGranular = 2;
constexpr int kOscTypeSpectral = 3;
constexpr int kOscTypeMultisample = 4;
/** The first entry of the warp lists that only the spectral oscillator acts on (GROUPC WP-12, 13-new-keys.md 4.38; needs kCap2SpectralWarps). */
constexpr int kFirstSpectralWarp = 76;

/** The table, in stable order (state files and UIs list parameters in this order). */
const ParamInfo* paramTable() noexcept;
size_t paramCount() noexcept;

const ParamInfo* findParam (uint32_t id) noexcept;
const ParamInfo* findParamByKey (const char* key) noexcept;
/** Index into paramTable(), or -1. */
int findParamIndex (uint32_t id) noexcept;

/** Plain value -> 0..1 (clamped), and back (clamped, then snapped to the interval / to the nearest choice). */
float toNormalised (const ParamInfo& info, float plain) noexcept;
float fromNormalised (const ParamInfo& info, float normalised) noexcept;

/** Clamp to [min, max] and snap to the interval; NaN becomes the default. */
float constrainValue (const ParamInfo& info, float plain) noexcept;

/** Display text: ParamInfo::decimals decimals for continuous parameters, the choice name for stepped ones. Always
    zero-terminated (if cap > 0), returns the length written (without the terminator). No allocation, locale independent. */
size_t valueToText (const ParamInfo& info, float plain, char* buf, size_t cap) noexcept;

/** Parses text produced by valueToText (or a plain number, trailing unit text is ignored; choice names are matched
    case-insensitively). The result is clamped into the range. Returns false if nothing usable was found. */
bool textToValue (const ParamInfo& info, const char* text, float& plain) noexcept;

/** Choice names of a stepped parameter (0 / nullptr for continuous ones). Valid for every ChoiceSet. */
size_t choiceCount (const ParamInfo& info) noexcept;
const char* choiceName (const ParamInfo& info, size_t index) noexcept;

const char* unitLabel (Unit unit) noexcept;
} // namespace synthyboy
