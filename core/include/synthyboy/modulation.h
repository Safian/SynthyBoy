// SynthyBoy - core: modulation matrix vocabulary (sources, destinations), see docs/ARCHITECTURE.md 3.5.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A modulation slot is described by seven ordinary parameters (modNN.source / destination / amount / power / bipolar /
// stereo / bypass). The source is a stepped choice over the table below (0 = none), the destination a stepped choice over
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
constexpr int kNumModSources = 28;

/** Source k (0-based) has the choice value k + 1; the order is the one of the list in docs/PARAMETERS.md. */
const ModSourceInfo* modSourceTable() noexcept;
size_t modSourceCount() noexcept;

/** Number of parameters flagged kParamModulatable; the destination choice has this many entries plus "none". */
size_t modDestinationCount() noexcept;
/** Destination choice value 1 .. modDestinationCount() -> index into paramTable(), or -1 (also for 0 = none). */
int modDestinationParamIndex (size_t choiceValue) noexcept;
/** Index into paramTable() -> destination choice value (1-based), 0 if the parameter is not modulatable. */
size_t modDestinationChoiceOf (size_t paramIndex) noexcept;

/** Parameter indices of the seven parameters of slot 0 .. kNumModSlots - 1 (table order), -1 if out of range. */
struct ModSlotParams
{
    int source, destination, amount, power, bipolar, stereo, bypass;
};
ModSlotParams modSlotParams (int slot) noexcept;
} // namespace synthyboy
