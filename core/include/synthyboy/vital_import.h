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
#include "zip_reader.h"
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
    /** `name` is the path of the preset inside the store without extension ("Bank/Pad" or "Pad"); an empty srcTag asks whether the name is taken at all,
        otherwise whether it holds the preset imported from that source (the "src:<12 hex>" tag of meta.tags). */
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

// ---- batch import (docs/ui-design/11-vital-preset-import.md 2.6) and .vitalbank --------------------------------------------------------

/** What a file name says it holds (case-insensitive extension): .vital preset, .vitaltable wavetable, .vitallfo LFO shape. */
enum class VitalFileKind : uint8_t { unknown, preset, wavetable, lfo };
VitalFileKind vitalFileKindFromName (const std::string& fileName) noexcept;
bool isVitalBankName (const std::string& fileName) noexcept; // ".vitalbank"

enum class BatchOutcome : uint8_t
{
    written,        // preset / shape written, or the wavetable stored
    skippedSame,    // the store already has this name imported from the identical source (src: tag)
    needsDecision,  // BatchOptions::ask and the name is taken by different content: nothing written (a non-interactive run cannot ask)
    failed,         // the file did not import, or a store refused to write
    ignored         // not a Vital file kind (other entries of a bank, directories, unsafe names)
};

struct BatchRow
{
    std::string source;      // "Bank.vitalbank/Pad.vital" or the file name
    std::string name;        // the preset / table / shape name as imported ("" if the file did not import)
    std::string relPath;     // what was written: "<bank>/<name>.sbpreset", "<name>.lfoshape.json", or the stored table of the pack
    VitalFileKind kind = VitalFileKind::unknown;
    BatchOutcome outcome = BatchOutcome::failed;
    ImportStatus status = ImportStatus::malformed;
    ZipStatus zip = ZipStatus::ok; // not ok: the entry could not be read out of its archive
    Fidelity fidelity = Fidelity::exact;
    int imported = 0, approximated = 0, dropped = 0, ignoredItems = 0, wavetables = 0, samples = 0, lfoShapes = 0;
    std::string error;
};

struct BatchSummary
{
    std::vector<BatchRow> rows;
    ZipStatus zip = ZipStatus::ok; // the status of opening the archive (a bank); ok for loose files
    int written = 0, skippedSame = 0, needsDecision = 0, failed = 0, ignored = 0;
    std::string error;
};

/** Imports one Vital document: `fileName` decides the kind (.vital / .vitaltable / .vitallfo), `folder` is the relPath prefix
    ("" or "Bank"). A preset is imported with options, its wavetables / samples go to `content`, the .sbpreset (+ report) to `presets`
    under the duplicate policy of `batch`: identical source (PresetStore::exists ("<folder>/<name>", "src:<12 hex>")) is skipped (replace: written
    again), a name taken by different content is replaced / kept both ("Name (2)") / left for the caller (ask); skipSame keeps both.
    A stand-alone table is stored in `content`, a stand-alone LFO shape written as "<name>.lfoshape.json" (see serializeImportedLfoShape)
    with an empty report. Never throws. */
BatchRow importVitalDocument (const std::string& source, const std::string& fileName, const std::string& folder, const void* data, size_t size,
                              const ImportOptions& options, const BatchOptions& batch, ContentStore& content, PresetStore& presets) noexcept;

/** Reads a .vitalbank (a zip of .vital files; .vitaltable / .vitallfo entries import as well) and runs importVitalDocument on every entry
    that is a Vital kind and has a safe path; the other entries are rows with outcome ignored. Entries are inflated one at a time (memory:
    the archive plus one entry). `bankName` is the file name of the bank (its stem is the folder). A bank that does not open gives an empty
    summary with `zip` set and `error` filled. */
BatchSummary importVitalBank (const void* data, size_t size, const std::string& bankName, const ImportOptions& options, const BatchOptions& batch,
                              ContentStore& content, PresetStore& presets, const ZipLimits& limits = {}) noexcept;

/** Deterministic JSON of a stand-alone LFO shape: {"format":"synthyboy-lfo-shape","version":1,"name":..,"smooth":..,"points":[{"x","y","power"}]}. */
std::string serializeImportedLfoShape (const ImportedLfoShape& shape);

/** The table of the one-to-one settings (for the tests and the report UI): number of rows, key and Vital control of a row. */
size_t vitalRowCount() noexcept;
const char* vitalRowKey (size_t row) noexcept;
const char* vitalRowControl (size_t row) noexcept;

/** Decodes the value of `effect_chain_order` (clean statement of the permutation encoding, design 1.1) into order[9]:
    order[position] = effect id 0..8. Returns false (identity written) for a value that is not an integer in 0 .. 9! - 1. */
bool decodeVitalEffectOrder (double value, int order[9]) noexcept;
} // namespace synthyboy
