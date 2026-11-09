// SynthyBoy - core: modulation matrix vocabulary (sources, destinations), see docs/ARCHITECTURE.md 3.5.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A modulation slot is described by seven ordinary parameters (modNN.source / destination / amount / power / bipolar /
// stereo / bypass) and four of the aux source (modNN.aux_source / aux_inv / aux_power / aux_output, state version 3). The source is a stepped choice over the table below (0 = none), the destination a stepped choice over
// every parameter flagged kParamModulatable in table order (0 = none).
#pragma once

#include <cstddef>

#include "params.h"

namespace synthyboy
{
/** Number of slots of the modulation matrix (mod01 .. mod64). */
constexpr int kNumModSlots = 64;

struct ModSourceInfo
{
    const char* key;  // "lfo3"
    const char* name; // "LFO 3"
    bool bipolar;     // the source swings around zero (LFOs, pitch wheel, ...) rather than 0..1
};

/** Number of modulation sources (without "none"). */
constexpr int kNumModSources = 57;
/** Modulation sources of state version 2 (choice values 1 .. 28); the sources 29 .. 39 were appended in version 3, 40 .. 42 (Voice Index, Voice Mod 1, Voice Mod 2) by block B26, 43 .. 57 (the outputs of the sound sources, Active Voices, Note-On Alt 1 / 2 / Rand 2 / Rand (Discrete), Release Velocity, Expression X, Fixed) by GROUPB WP-06. */
constexpr int kNumModSourcesV2 = 28;
/** Size of the destination choice (modNN.destination): "none" plus every parameter flagged kParamModulatable and the appended destinations. */
constexpr int kNumModDestinations = 912; // 453 after B15 + B20 stereo_routing (1 + 453 modulatable keys), + 64 mod amounts (454..517) + portamento time (518) + polyphony (519),
                                         // + the 384 parameters fxiNN.pKK of the effect instance pool (B22: choice values 520 .. 903)
                                         // + sub.phase, noise.stereo, noise.filter (B23, GROUPA1: 904 .. 906), delay.offset_left, reverb.feedback (B25: 907, 908)
                                         // + osc1 / 2 / 3 .sample.slice (B27, GROUPB: 909 .. 911)

/** Source k (0-based) has the choice value k + 1; the order is the one of the list in docs/PARAMETERS.md. */
const ModSourceInfo* modSourceTable() noexcept;
size_t modSourceCount() noexcept;

/** Number of parameters flagged kParamModulatable; the destination choice has this many entries plus "none". */
size_t modDestinationCount() noexcept;
/** Destination choice value 1 .. modDestinationCount() -> index into paramTable(), or -1 (also for 0 = none). */
int modDestinationParamIndex (size_t choiceValue) noexcept;
/** Index into paramTable() -> destination choice value (1-based), 0 if the parameter is not modulatable. */
size_t modDestinationChoiceOf (size_t paramIndex) noexcept;

/** Parameter indices of the parameters of slot 0 .. kNumModSlots - 1 (looked up by key, the aux block sits at the end of the
    table), -1 if out of range. */
struct ModSlotParams
{
    int source, destination, amount, power, bipolar, stereo, bypass;
    int auxSource, auxInv, auxPower, auxOutput;
};
ModSlotParams modSlotParams (int slot) noexcept;
} // namespace synthyboy
