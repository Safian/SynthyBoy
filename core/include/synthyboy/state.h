// SynthyBoy - core: versioned state (deterministic JSON), see docs/ARCHITECTURE.md 5.2.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

#include "params.h"

namespace synthyboy
{
constexpr int kStateVersion = 2;

/** Largest accepted state document. */
constexpr size_t kMaxStateBytes = 1u << 20;

/** Plain values of all parameters, indexed like paramTable(). */
struct ParamValues
{
    float plain[kMaxParams] = {};

    /** All parameters at their table defaults. */
    static ParamValues defaults() noexcept;
};

enum class LoadStatus
{
    ok,            // parsed and applied
    empty,         // size 0: nothing to load, inOut untouched
    invalid,       // not a valid state document: inOut untouched
    newer_version  // written by a newer version: the parameters known here were applied
};

struct LoadResult
{
    LoadStatus status = LoadStatus::invalid;
    int applied = 0; // number of distinct parameters taken from the document
    int version = 0; // version found in the document (0 if none)
};

/** Deterministic text: {"format":"synthyboy-state","version":2,"params":{"master.volume":-12,...}}, parameters in table order,
    floats as "%.9g" (locale independent), no white space. Allocates: not for the audio thread. */
std::string saveState (const ParamValues& values);

/** Parses a state document into inOut. Bounded (kMaxStateBytes, small nesting), never reads outside [data, data+size),
    never throws, never allocates. Unknown keys are ignored, missing parameters keep their value in inOut, values are
    clamped to the range and snapped (stepped/interval). On `invalid` and `empty` inOut is left unchanged. A version 1
    document (the three keys gain, waveform, cutoff) is migrated, see core/src/state.cpp (migrateV1ToV2); `applied`
    counts the parameters of version 2 that were set (a v1 cutoff sets filter1.cutoff and filter1.on). */
LoadResult loadState (const void* data, size_t size, ParamValues& inOut) noexcept;

/** The migration of one parameter of format version 1 ("gain", "waveform", "cutoff"), for state of older shells that is not
    a state document (the spike's XML blob). Returns the number of version 2 parameters set (0 for an unknown key). */
int migrateLegacyParam (const char* key, double value, ParamValues& inOut) noexcept;
} // namespace synthyboy
