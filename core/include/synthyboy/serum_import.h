// SynthyBoy - core: import of Serum 2 presets (.SerumPreset) and the other Serum 2 file types into our parameter model.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// docs/ui-design/19-serum-preset-format.md (the container), 13-new-keys.md 4.26 (the mapping, assumptions A325 ..). JUCE-free, engine-free, no I/O,
// no threads, no globals: the importer works on bytes handed over by the host; the files a preset only references (tables, samples, noise, impulses,
// multisamples) are asked from a SerumContentResolver the host implements. The result is plain data. Same discipline as vital_import.h: every leaf
// of the file ends up in the report as imported / approximated / dropped / ignored, nothing is silently lost.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "content_state.h"
#include "preset.h"
#include "state.h"
#include "user_content.h"
#include "vital_import.h"

namespace synthyboy
{
enum class SerumFileKind : uint8_t
{
    unknown,
    preset,       // .SerumPreset
    fxModule,     // .SerumFX            one effect module with its parameters
    fxRack,       // .SerumFXRack        a rack: a list of modules
    shape,        // .XferShape          an LFO shape or a curve (the Curves folder)
    path,         // .XferPath           an LFO path (x / y)
    clip,         // .XferClip           a MIDI clip of the clip player
    clipBank,     // .XferClipBank
    arp,          // .XferArp            an arpeggiator pattern
    arpBank,      // .XferArpBank
    style,        // .SerumStyle         a voice panel style
    filterPz,     // .XferFilterPZSingle / .XferFilterPZMulti
    midiMap,      // .SerumMIDIMap
    impulse,      // .flac / .wav in the Impulses folder (the content itself, no container)
    audio         // any other .flac / .wav (a sample or a table)
};

/** What a file name says it holds (case-insensitive extension). */
SerumFileKind serumFileKindFromName (const std::string& fileName) noexcept;

/** One line of the report; `count` is the number of leaves of the file it stands for (a block of unmapped data is one line). */
struct SerumReportEntry
{
    ItemKind kind = ItemKind::ignored;
    Reason reason = Reason::exact;
    std::string area;        // "osc1", "filter2", "env3", "lfo4", "mod07", "fx.rack0", "voice", "content", "meta", "ui"
    std::string serumPath;   // "Oscillator1/WTOsc1/kParamTablePos"
    std::string serumValue;  // as read, text
    std::string ourKey;      // "osc2.frame" or ""
    std::string ourValue;    // value after the import (display text of the plain value) or ""
    std::string detail;      // short English sentence, stable id: "multisample_reduced_to_one_zone"
    int count = 1;
};

struct SerumImportReport
{
    std::vector<SerumReportEntry> entries; // in file order, at most SerumImportLimits::maxReportEntries (counts stay exact)
    int numImported = 0, numApproximated = 0, numDropped = 0, numIgnored = 0, numDefaulted = 0;
    int leaves = 0;           // leaves of the value tree: numImported + numApproximated + numDropped + numIgnored, exactly
    int unaccounted = 0;      // leaves - that sum: 0 for every file (the tests check it)
    bool truncated = false;
    Fidelity fidelity = Fidelity::exact;
    std::string serumVersion; // productVersion of the header
    uint32_t fileVersion = 0; // "version" of the header

    /** Schema "synthyboy-serum-import-report" v1, deterministic. */
    std::string toJson() const;
};

struct SerumImportLimits
{
    size_t maxFileBytes = 512u << 20;
    size_t maxBodyBytes = 256u << 20;
    size_t maxWorkingBytes = 768u << 20;
    size_t maxAudioFrames = 48u << 20; // per referenced audio file
    int maxDepth = 24;
    int maxReportEntries = 40000;
    uint32_t deadlineMs = 120000; // 0 = none
};

/** The host side of the files a preset references. `area` is "Tables", "Samples", "Multisamples", "Noises" or "Impulses" (the Serum content
    folders; "Noises" is Samples/Factory Non-Tonal/Noises), `relPath` the path as the preset has it (either slash, a leading slash is allowed). */
struct SerumContentResolver
{
    virtual ~SerumContentResolver() = default;
    virtual bool read (const std::string& area, const std::string& relPath, std::vector<uint8_t>& bytes) = 0;
};

struct SerumImportOptions
{
    SerumImportLimits limits;
    SerumContentResolver* resolver = nullptr;                   // null = referenced files are reported as missing
    std::function<bool()> cancel;                               // polled between modules; true = cancel
    const char* fallbackName = nullptr;                         // file stem when the preset has no name
    bool customWavetables = true;                               // the engine can play a stored table (kCapUserWavetable); false: the nearest built-in table
    bool customSamples = true;                                  // the engine has the sample oscillator (kCapSampleOsc)
    bool multisampleOsc = true;                                 // the engine has the multisample oscillator (kCap2Multisample), otherwise an instrument is reduced to one zone played by a sample oscillator
    bool spectralOsc = true;                                    // the engine has the spectral oscillator (kCap2Spectral), otherwise a spectral oscillator is imported as a sample oscillator
    bool granularOsc = true;                                    // the engine has the granular oscillator (kCap2Granular); without it a granular oscillator is imported as a sample oscillator
    bool customLfoShapes = true;                                // the engine plays a drawn LFO shape (kCapCustomLfoShape)
    bool noiseSample = true;                                    // the noise source plays a stored sample (noise.type = Sample, kCapNoiseSample)
    const char* categoryHint = nullptr;                         // the folder of the file ("Bass"): the preset category
    int fileRack = 0;                                           // an effect chain file (.SerumFX / .SerumFXRack) goes into this rack (0 Main, 1 Bus 1, 2 Bus 2)
};

struct SerumImportedImpulse
{
    std::string name, sha256;
    int sampleRate = 0, channels = 0, numFrames = 0;
    std::vector<uint8_t> wavBytes; // 24-bit WAV
};

/** A multisample instrument of a preset (GROUPC WP-13): the SFZ text (the sample paths are the ones of `files`) and the samples as 24-bit WAV files, paths relative to the SFZ. */
struct ImportedInstrument
{
    int osc = 0; // 1..3
    std::string name;
    std::string sfzText;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    int zones = 0, missingFiles = 0;
};

struct SerumImportResult
{
    ImportStatus status = ImportStatus::malformed;
    SerumFileKind kind = SerumFileKind::unknown;
    ParamValues values = ParamValues::defaults();
    std::vector<int> touched;                  // table indices the import wrote (a preset: all it set; an effect chain: only these apply to a patch)
    PresetMeta meta;
    std::vector<ImportedWavetable> wavetables; // one per oscillator whose table is custom or not a built-in
    std::vector<ImportedSample> samples;
    std::vector<SerumImportedImpulse> impulses;
    std::vector<ContentLfoShape> lfoShapes;
    std::vector<ContentRemap> remaps;
    std::vector<ImportedInstrument> instruments; // GROUPC WP-13: the SFZ instruments of the multisample oscillators
    std::vector<ContentRemap> spectralFilters;   // GROUPC WP-12: the drawn filter mask of a spectral oscillator (flex)
    std::vector<ContentModCurve> modCurves;
    std::vector<ContentPoint> velocityCurve, noteCurve, shaperA, shaperB;
    ContentTuning tuning;
    std::vector<std::pair<int, std::string>> midiMap; // the MIDI CC map of a preset (`midiMap`) or of a .SerumMIDIMap file: (CC, our parameter key), ascending by CC (GROUPA2 WP-02)
    LfoShapeFile shape;                        // kind shape: the one shape / curve of the file
    SerumImportReport report;
    std::string macroNames[8];
    std::string sourceSha256;                  // SHA-256 of the input bytes
    std::string error;
    int missingFiles = 0;                      // referenced files the resolver could not give
};

/** Cheap sniff: the "XferJson" container magic. */
bool looksLikeSerumFile (const void* data, size_t size) noexcept;

/** Imports any file of the Serum 2 families; `fileName` decides nothing, the header "fileType" does (a preset renamed to anything imports). Never throws. */
SerumImportResult importSerumFile (const void* data, size_t size, const SerumImportOptions& options) noexcept;
/** The same for a .flac / .wav in the Impulses folder: one impulse in `impulses`, the other fields untouched. */
SerumImportResult importSerumImpulse (const void* data, size_t size, const std::string& name, const SerumImportOptions& options) noexcept;

/** .sbpreset text for an imported preset: savePreset () content plus the additive "import" section
    {"source":{"format":"serum","version","sha256","preset_name","macro_names"},"approximated":[keys<=64]}. `contentRefs` fills pack / file for the
    tables and samples the host stored (ContentWavetableRef / ContentSampleRef of vital_import.h), `impulseRef` the impulse. */
std::string serializeSerumPreset (const SerumImportResult& result, const PresetMeta& finalMeta, const ContentRefs& contentRefs, const ContentFileRef* impulseRef);

/** The text of the report sheet: one line per entry that is not plain imported ("approximated: osc1.warp.type ... detail"), at most `maxLines`. */
std::vector<std::string> serumReportLines (const SerumImportReport& report, size_t maxLines = 200);

/** Table of the one-to-one rules for the tests / the audit: number of rows and a text description of row k ("Oscillator#/WTOsc#/kParamTablePos -> osc#.frame"). */
size_t serumRuleCount() noexcept;
std::string serumRuleDescription (size_t row);

/** Converts the audio of a Serum content file (FLAC / WAV) to the WAV the stores keep. bits: 16 / 24. Empty on failure. */
std::vector<uint8_t> serumAudioToWav (const void* data, size_t size, int bits, size_t maxFrames, int* sampleRate = nullptr, int* channels = nullptr, size_t* frames = nullptr);
} // namespace synthyboy
