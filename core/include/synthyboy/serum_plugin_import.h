// SynthyBoy - core: Serum 2 import inside the plugin and the command line tool (WP-21, docs/ui-design/13-new-keys.md 4.26).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One call per file the user picked. `.SerumPreset` is a patch (parameters + content), `.SerumFX` / `.SerumFXRack` are effect chains (the parameters they write
// are listed in `touched`: the caller applies them to the current patch), `.XferShape` is an LFO shape / curve, `.flac` / `.wav` from an Impulses folder is an
// impulse, the other Serum families are reported as not modelled. The tables, samples and impulses go to the user content store, so the references of the loaded
// patch resolve at once. JUCE free; message thread.
#pragma once

#include <string>
#include <vector>

#include "synthyboy/engine_caps.h"
#include "synthyboy/serum_import.h"
#include "synthyboy/user_content.h"

namespace synthyboy
{
struct SerumPluginImport
{
    SerumFileKind kind = SerumFileKind::unknown;
    bool ok = false;
    std::string error;
    std::string name;

    // a preset (.SerumPreset): the patch to load, its text (.sbpreset) and the report of the sheet
    ParamValues values = ParamValues::defaults();
    StateContent content;
    PresetMeta meta;
    std::string presetText;
    std::vector<std::string> macroNames;

    // an effect chain (.SerumFX / .SerumFXRack): `values` holds the chain over the defaults, `touched` the table indices to copy onto the current patch;
    // `content` may hold the impulse response of the chain
    std::vector<int> touched;

    // the MIDI CC map of a preset or of a .SerumMIDIMap file (GROUPA2 WP-02; only the macros are known, the other ids are reported)
    std::vector<std::pair<int, std::string>> midiMap;

    // a shape or curve (.XferShape)
    LfoShapeFile shape;

    // an impulse (.flac / .wav of the Impulses folder)
    ContentFileRef impulseRef;

    SerumImportReport report;
    std::string reportJson;
    int storedFiles = 0, missingFiles = 0;
};

/** Imports one Serum file. `bytes` is the file, `fileName` decides the kind for audio files (the header decides for every Serum container); `caps` are the
    capabilities of the engine. `resolver` supplies the files a preset references (null: they are reported missing). Never throws. */
SerumPluginImport importSerumFileForPlugin (const std::string& fileName, const std::vector<uint8_t>& bytes, UserContentStore& store, EngineCapsMask caps,
                                            SerumContentResolver* resolver, const std::string& categoryHint = {});

/** Resolver on a Serum content folder (the parent of Tables / Samples / Multisamples / Impulses): the common implementation of the CLI and the plugin. */
class SerumFolderResolver final : public SerumContentResolver
{
public:
    explicit SerumFolderResolver (std::string rootDirectory) : root_ (std::move (rootDirectory)) {}
    bool read (const std::string& area, const std::string& relPath, std::vector<uint8_t>& bytes) override;
    const std::string& root() const noexcept { return root_; }

private:
    std::string root_;
};

/** The Serum content folder that holds a file: the nearest ancestor with a "Tables" or "Samples" directory ("" when none). */
std::string findSerumContentRoot (const std::string& filePath);
} // namespace synthyboy
