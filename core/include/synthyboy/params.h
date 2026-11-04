// SynthyBoyVST - core: parameter table (single source for plugin params, state I/O and UI), see docs/ARCHITECTURE.md 5.1.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
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
    beats
};

enum ParamFlags : uint32_t
{
    kParamAutomatable    = 1u << 0,
    kParamModulatable    = 1u << 1,
    kParamPerNote        = 1u << 2,
    kParamHidden         = 1u << 3,
    kParamStepped        = 1u << 4,
    kParamEngineSpecific = 1u << 5 // meaning/range depends on the engine (own vs. Vital-derived)
};

struct ParamInfo
{
    uint32_t id;         // == hashKey (key); stable, never reused or reordered, new ids only appended
    const char* key;     // stable string key, used in state files, never renamed
    const char* name;    // display name
    const char* group;   // section ("Master", "Filter", ...)
    float min, max, def; // plain values
    float skew;          // used by Scale::exp_skew
    float interval;      // snap grid of the plain value, 0 = continuous
    Scale scale;
    Unit unit;
    uint32_t flags;
};

/** Upper bound of the table size; fixed-size containers (ParamValues, engine) are sized with it. */
constexpr size_t kMaxParams = 64;

/** Parameter ids of the current table (== hashKey of the keys). */
constexpr uint32_t kParamGain     = hashKey ("gain");
constexpr uint32_t kParamWaveform = hashKey ("waveform");
constexpr uint32_t kParamCutoff   = hashKey ("cutoff");

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

/** Display text: fixed one decimal for continuous parameters, the choice name for stepped ones. Always zero-terminated
    (if cap > 0), returns the length written (without the terminator). No allocation, locale independent. */
size_t valueToText (const ParamInfo& info, float plain, char* buf, size_t cap) noexcept;

/** Parses text produced by valueToText (or a plain number, trailing unit text is ignored; choice names are matched
    case-insensitively). The result is clamped into the range. Returns false if nothing usable was found. */
bool textToValue (const ParamInfo& info, const char* text, float& plain) noexcept;

/** Choice names of a stepped parameter (nullptr/0 for continuous ones). */
size_t choiceCount (const ParamInfo& info) noexcept;
const char* choiceName (const ParamInfo& info, size_t index) noexcept;

const char* unitLabel (Unit unit) noexcept;
} // namespace synthyboy
