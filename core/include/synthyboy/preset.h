// SynthyBoy - core: preset container (.sbpreset) and metadata discovery (docs/ARCHITECTURE.md 5.2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "params.h"
#include "state.h"

namespace synthyboy
{
constexpr const char* kPresetFormatName = "synthyboy-preset";
constexpr int kPresetVersion = 1;

/** Metadata of a preset (read without loading state or initializing DSP). */
struct PresetMeta
{
    std::string name;
    std::string category;
    std::string author;
    std::string license;
    std::string tags;
    std::string description;

    static PresetMeta initDefault() noexcept;
};

struct PresetEntry
{
    std::string path;
    PresetMeta meta;
};

/** Deterministic text of a .sbpreset container:
    {"format":"synthyboy-preset","version":1,"meta":{...},"state":{"format":"synthyboy-state","version":3,"params":{...}}} */
std::string savePreset (const PresetMeta& meta, const ParamValues& values);

/** The same with the \`content\` section of state version 3 (custom LFO shapes, content references, ...) inside the state object. */
std::string savePreset (const PresetMeta& meta, const ParamValues& values, const StateContent& content);

/** Overload that allows keeping an empty author empty (when allowEmptyAuthor is true). */
std::string savePreset (const PresetMeta& meta, const ParamValues& values, const StateContent& content, bool allowEmptyAuthor);

/** Reads only the preset metadata from a .sbpreset document without loading parameter state or allocating engine structures.
    Returns true if metadata was found and valid. */
bool readPresetMeta (const void* data, size_t size, PresetMeta& metaOut) noexcept;

/** Parses a complete preset document (.sbpreset), extracting metadata and parameter values.
    Also accepts a raw state document (format: synthyboy-state) for backward compatibility, populating default metadata. */
LoadResult loadPreset (const void* data, size_t size, PresetMeta& metaOut, ParamValues& valuesOut) noexcept;

/** loadPreset () that also takes the content section (see loadStateWithContent for the rules). */
LoadResult loadPreset (const void* data, size_t size, PresetMeta& metaOut, ParamValues& valuesOut, StateContent& contentInOut) noexcept;

/** Scans a directory recursively for .sbpreset files and reads their metadata without parsing full states. */
std::vector<PresetEntry> scanPresetDirectory (const std::string& directoryPath);
} // namespace synthyboy
