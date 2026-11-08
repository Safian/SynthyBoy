// SynthyBoy - core: import of Vital presets (.vital) into our parameter model.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// docs/ui-design/11-vital-preset-import.md (sections 1.1 - 1.4, Annex A.2). JUCE-free, Vital-free, no I/O, no threads, no globals.
// The importer works on bytes handed over by the host; the result is plain data.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "content_pack.h"
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
struct ImportCaps
{
    bool customWavetables = false;
    bool customLfoShapes = false;
    bool sampleOsc = false;
    int maxWavetableFrames = 257;
};

struct ImportOptions
{
    VitalImportLimits limits;
    ImportCaps caps;
    bool renderWavetables = true;
    std::function<bool()> cancel;                                   // polled between components; true = cancel
    std::function<void(int percent, const char* step)> progress;    // optional
    const char* fallbackName = nullptr;                             // file stem when preset_name is empty
};

enum class ImportStatus : uint8_t { ok, notVital, malformed, tooLarge, cancelled, budgetExceeded };

struct ImportedWavetable
{
    int osc = 0;                // 1..3, 0 for a stand-alone .vitaltable
    std::string name, author, sha256; // sha256 of the stored file (wavBytes): the identity in the content pack and the reference of the state
    int numFrames = 0;          // <= 257
    std::vector<int16_t> frames; // numFrames * 2048
    std::vector<uint8_t> wavBytes; // the stored file: RIFF/WAVE, 16-bit mono PCM, numFrames * 2048 samples (the format of the content store,
                                   // docs/ARCHITECTURE.md 5.2: a length that is a multiple of 2048 is that many frames)
};

struct ImportedLfoShape
{
    int lfo = 0;                // 1..8, 0 for a stand-alone .vitallfo
    std::string name;
    bool smooth = false;
    std::vector<std::array<float, 3>> points; // x, y (value, up = high), power of the segment starting at the point
};

struct ImportedSample
{
    int osc = 0;                              // 1..3
    std::string name;
    std::string sha256;                       // sha256 of the generated 16-bit PCM WAV bytes
    int sampleRate = 44100;
    int numFrames = 0;
    int channels = 1;                         // 1 (mono) or 2 (stereo)
    std::vector<uint8_t> wavBytes;            // complete RIFF/WAVE 16-bit PCM file
    std::vector<float> slices;                // manual slice positions 0..1
};

/** Reference to a stored wavetable in a ContentPack for an oscillator (1..3). */
struct ContentWavetableRef
{
    int osc = 0;
    std::string pack;
    std::string file;
};

/** Reference to a stored sample in a ContentPack for an oscillator (1..3). */
struct ContentSampleRef
{
    int osc = 0;
    std::string pack;
    std::string file;
};

struct ContentRefs
{
    std::vector<ContentWavetableRef> wavetables;
    std::vector<ContentSampleRef> samples;

    const ContentWavetableRef* findWavetable (int osc) const noexcept
    {
        for (const auto& w : wavetables)
            if (w.osc == osc)
                return &w;
        return nullptr;
    }

    const ContentSampleRef* findSample (int osc) const noexcept
    {
        for (const auto& s : samples)
            if (s.osc == osc)
                return &s;
        return nullptr;
    }
};

/** Host-side interface for wavetable and sample storage (docs/ui-design/11-vital-preset-import.md Annex A.2, A26). */
struct ContentStore
{
    virtual ~ContentStore() = default;
    virtual bool hasWavetable (const std::string& sha256) = 0;
    virtual bool addWavetable (const ImportedWavetable& wt, std::string& packOut, std::string& fileOut) = 0;
    virtual bool hasSample (const std::string& sha256) { (void) sha256; return false; }
    virtual bool addSample (const ImportedSample& s, std::string& packOut, std::string& fileOut) { (void) s; (void) packOut; (void) fileOut; return false; }
};

/** User-supplied ContentPack store for imported Vital wavetables and samples (user.imported-vital). */
class UserWavetableStore : public ContentStore
{
public:
    explicit UserWavetableStore (const std::string& packDirectory);

    bool hasWavetable (const std::string& sha256) override;
    bool addWavetable (const ImportedWavetable& wt, std::string& packOut, std::string& fileOut) override;
    bool hasSample (const std::string& sha256) override;
    bool addSample (const ImportedSample& s, std::string& packOut, std::string& fileOut) override;

    const ContentPackInfo& packInfo() const noexcept { return info_; }
    const std::string& packDirectory() const noexcept { return packDir_; }

private:
    std::string packDir_;
    ContentPackInfo info_;
};

struct PresetStore
{
    virtual ~PresetStore() = default;
    virtual bool exists (const std::string& name, const std::string& srcTag) = 0;
    virtual bool write (const std::string& relPath, const std::string& sbpresetText, const std::string& reportJson) = 0;
};

struct BatchOptions
{
    enum Duplicate { skipSame, replace, keepBoth, ask } duplicate = skipSame;
    bool useVitalStyle = true;
    std::string unknownStyleCategory = "Lead";
};

struct ImportResult
{
    ImportStatus status = ImportStatus::malformed;
    ParamValues values = ParamValues::defaults(); // defaults() unless status == ok
    PresetMeta meta;
    std::vector<ImportedWavetable> wavetables; // custom tables that are not a built-in
    std::vector<ImportedLfoShape> lfoShapes;   // custom shapes that are not a built-in
    std::vector<ImportedSample> samples;       // custom sample for oscN.type = Sample
    ContentTuning tuning;                      // custom tuning scl/kbm
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

/** .sbpreset text for an imported result: savePreset() content plus additive sections
    "content": {"wavetables":[{osc,pack,file,sha256,name,author,frames}],"lfo_shapes":[{lfo,name,smooth,points}]},
    "import":  {"source":{"format":"vital","version","sha256","preset_name","comments","macro_names"},"approximated":[keys<=64]}.
    contentRefs fills pack/file for the wavetables the host stored. */
std::string serializeImportedPreset (const ImportResult& result, const PresetMeta& finalMeta, const ContentRefs& contentRefs);

/** The table of the one-to-one settings (for the tests and the report UI): number of rows, key and Vital control of a row. */
size_t vitalRowCount() noexcept;
const char* vitalRowKey (size_t row) noexcept;
const char* vitalRowControl (size_t row) noexcept;

/** Decodes the value of `effect_chain_order` (clean statement of the permutation encoding, design 1.1) into order[9]:
    order[position] = effect id 0..8. Returns false (identity written) for a value that is not an integer in 0 .. 9! - 1. */
bool decodeVitalEffectOrder (double value, int order[9]) noexcept;
} // namespace synthyboy
