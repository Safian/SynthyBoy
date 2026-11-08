// SynthyBoy - core: import of Vital presets (.vital) into our parameter model.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// docs/ui-design/11-vital-preset-import.md (sections 1.1 - 1.4, Annex A.2). JUCE-free, Vital-free, no I/O, no threads, no globals.
// The importer works on bytes handed over by the host; the result is plain data.
//
// Not part of this header yet (blocked on the core changes of the design, Annex A.4): serializeImportedPreset / ContentRefs (C3),
// ContentStore / PresetStore (C3, C4). Imported custom wavetables and LFO shapes are returned in the result; the parameters that
// would select them ("Preset table" C1, "Custom" C2) do not exist in the table, so oscN.wavetable / lfoN.shape get the nearest
// built-in and the report says so (TODO(C1), TODO(C2) in core/vital-import/vital_import.cpp mark the two places).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "preset.h"
#include "state.h"

namespace synthyboy
{
enum class ItemKind : uint8_t { imported, approximated, dropped, ignored };

enum class Reason : uint8_t
{
    exact, clamped, rounded, enumRemapped, legacyMigration, defaulted, typeMismatch, truncated,
    nearestWavetable, nearestLfoShape, unsupportedComponent, renderFallback,
    noEngineEquivalent, customSample, subOscillator, tuningUnsupported,
    modSourceUnsupported, modDestinationUnsupported, effectOrderInvalid, limitExceeded, tierNote
};

enum class Fidelity : uint8_t { exact, close, degraded };

struct ReportItem
{
    ItemKind kind = ItemKind::ignored;
    Reason reason = Reason::exact;
    std::string area;       // "osc1", "filter2", "env3", "lfo4", "mod07", "fx.reverb", "voice", "content", "meta"
    std::string vitalName;  // "osc_1_distortion_type" or "modulations[6]"
    std::string vitalValue; // as read, text
    std::string ourKey;     // "osc1.warp.type" or ""
    std::string ourValue;   // value after import, display text, or ""
    std::string detail;     // short English sentence, stable id: "warp_from_sample_unsupported"
};

struct ImportedEntry
{
    uint16_t row;      // row of the 730-row table (vitalRowKey)
    float vitalValue;
    float ourValue;
};

struct ImportReport
{
    std::vector<ImportedEntry> imported;
    std::vector<ReportItem> items; // approximated + dropped + ignored, in file order (at most maxReportItems)
    int numImported = 0, numApproximated = 0, numDropped = 0, numIgnored = 0, numDefaulted = 0;
    int vitalSettingsSeen = 0, vitalUnknownKeys = 0;
    Fidelity fidelity = Fidelity::exact;
    bool truncated = false; // more items than maxReportItems
    std::string vitalVersion;

    /** Schema "synthyboy-import-report" v1, deterministic. */
    std::string toJson() const;
};

struct VitalImportLimits
{
    size_t maxFileBytes = 32u << 20;
    size_t maxWorkingBytes = 192u << 20;
    int maxDepth = 16;
    int maxReportItems = 2000;
    uint32_t deadlineMs = 20000; // 0 = none
};

/** What the running engine can represent. Both false in this build of the table: see the note at the top. */
struct EngineCaps
{
    bool customWavetables = false;
    bool customLfoShapes = false;
    int maxWavetableFrames = 257;
};

struct ImportOptions
{
    VitalImportLimits limits;
    EngineCaps caps;
    bool renderWavetables = true;
    std::function<bool()> cancel;                                   // polled between components; true = cancel
    std::function<void(int percent, const char* step)> progress;    // optional
    const char* fallbackName = nullptr;                             // file stem when preset_name is empty
};

enum class ImportStatus : uint8_t { ok, notVital, malformed, tooLarge, cancelled, budgetExceeded };

struct ImportedWavetable
{
    int osc = 0;                // 1..3, 0 for a stand-alone .vitaltable
    std::string name, author, sha256; // sha256 of the rendered 16 bit frames
    int numFrames = 0;          // <= 257
    std::vector<int16_t> frames; // numFrames * 2048
};

struct ImportedLfoShape
{
    int lfo = 0;                // 1..8, 0 for a stand-alone .vitallfo
    std::string name;
    bool smooth = false;
    std::vector<std::array<float, 3>> points; // x, y (value, up = high), power of the segment starting at the point
};

struct ImportResult
{
    ImportStatus status = ImportStatus::malformed;
    ParamValues values = ParamValues::defaults(); // defaults() unless status == ok
    PresetMeta meta;
    std::vector<ImportedWavetable> wavetables; // custom tables that are not a built-in
    std::vector<ImportedLfoShape> lfoShapes;   // custom shapes that are not a built-in
    ImportReport report;
    std::string macroNames[4];                 // Vital's macro display names
    std::string sourceSha256;                  // SHA-256 of the input bytes (tag "src:" uses the first 12 hex digits)
    std::string style;                         // Vital's preset_style as read
    bool categoryNeedsChoice = false;          // style unknown / Template / empty: the save sheet asks (meta.category is "Lead")
    std::vector<std::string> unknownKeys;      // first 32 unknown names in settings
    std::string error;                         // short, English, for the log
};

/** Cheap sniff of the first 64 KiB: a JSON object that has a "settings" key. */
bool looksLikeVitalPreset (const void* data, size_t size) noexcept;

ImportResult importVitalPreset (const void* data, size_t size, const ImportOptions& options) noexcept;
/** Stand-alone .vitaltable (the wavetable object) / .vitallfo (the LFO line object): the result holds one table / shape. */
ImportResult importVitalWavetableFile (const void* data, size_t size, const ImportOptions& options) noexcept;
ImportResult importVitalLfoFile (const void* data, size_t size, const ImportOptions& options) noexcept;

/** The table of the one-to-one settings (for the tests and the report UI): number of rows, key and Vital control of a row. */
size_t vitalRowCount() noexcept;
const char* vitalRowKey (size_t row) noexcept;
const char* vitalRowControl (size_t row) noexcept;

/** Decodes the value of `effect_chain_order` (clean statement of the permutation encoding, design 1.1) into order[9]:
    order[position] = effect id 0..8. Returns false (identity written) for a value that is not an integer in 0 .. 9! - 1. */
bool decodeVitalEffectOrder (double value, int order[9]) noexcept;
} // namespace synthyboy
