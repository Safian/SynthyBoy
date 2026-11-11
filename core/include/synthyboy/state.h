// SynthyBoy - core: versioned state (deterministic JSON), see docs/ARCHITECTURE.md 5.2.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

#include "content_state.h"
#include "params.h"

namespace synthyboy
{
constexpr int kStateVersion = 4;

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

/** What happened to the additive `content` section of a document (state version 3, content_state.h). */
enum class ContentStatus
{
    absent,   // no content section (every document older than version 3)
    ok,       // parsed, validated and handed to the caller (loadStateWithContent)
    rejected, // present but outside the caps / ranges: dropped as a whole, the parameters were still applied
    ignored   // present but not parsed: loadState () without a StateContent
};

struct LoadResult
{
    LoadStatus status = LoadStatus::invalid;
    int applied = 0; // number of distinct parameters taken from the document (parameters the migrations derive are not counted)
    int version = 0; // version found in the document (0 if none)
    ContentStatus content = ContentStatus::absent;
};

/** Deterministic text: {"format":"synthyboy-state","version":4,"params":{"master.volume":-12,...}}, parameters in table order,
    floats as "%.9g" (locale independent), no white space. Allocates: not for the audio thread. */
std::string saveState (const ParamValues& values);

/** The same, followed by the `content` section ("content":{"wavetables":[..],"samples":[..],"lfo_shapes":[..],"curves":{..},
    "tuning":{..},"impulses":[..],"filter_curves":[..]}, only the non-empty parts, in this order) unless content is empty or fails
    validateContent (). Host state and .sbpreset use this one writer. */
std::string saveState (const ParamValues& values, const StateContent& content);

/** Parses a state document into inOut. Bounded (kMaxStateBytes, small nesting), never reads outside [data, data+size),
    never throws, never allocates. Unknown keys are ignored, missing parameters keep their value in inOut, values are
    clamped to the range and snapped (stepped/interval). On `invalid` and `empty` inOut is left unchanged. A version 1
    document (the three keys gain, waveform, cutoff) is migrated, see core/src/state.cpp (migrateV1ToV2); `applied`
    counts the parameters of version 2 that were set (a v1 cutoff sets filter1.cutoff and filter1.on). */
LoadResult loadState (const void* data, size_t size, ParamValues& inOut) noexcept;

/** loadState () that also takes the `content` section. Allocates (only for the content), never throws (an allocation failure is
    `invalid`). On ok / newer_version contentInOut is replaced by the document's section, or cleared if there is none or it is
    rejected (a loaded document is a whole patch: the content of the previous one must not leak into it); on invalid / empty it is
    left unchanged. A version 1 or 2 document has no content section. Not for the audio thread. */
LoadResult loadStateWithContent (const void* data, size_t size, ParamValues& inOut, StateContent& contentInOut) noexcept;

/** The migration of one parameter of format version 1 ("gain", "waveform", "cutoff"), for state of older shells that is not
    a state document (the spike's XML blob). Returns the number of version 2 parameters set (0 for an unknown key). */
int migrateLegacyParam (const char* key, double value, ParamValues& inOut) noexcept;
} // namespace synthyboy
