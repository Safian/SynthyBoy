// SynthyBoy - core: Vital import inside the plugin (WP-01 e, docs/ui-design/11-vital-preset-import.md, 13-new-keys.md 4.25).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One call per file the user picked: `.vital` (a patch: parameters + content), `.vitalbank` (every preset written to the user preset directory),
// `.vitaltable` (a table stored in the user library), `.vitallfo` (an LFO shape for a slot). The tables and samples of a preset go to the
// `user.imported-vital` pack of the content store, so the references of the loaded patch resolve at once. JUCE free; message thread.
#pragma once

#include <string>
#include <vector>

#include "synthyboy/engine_caps.h"
#include "synthyboy/user_content.h"
#include "synthyboy/vital_import.h"

namespace synthyboy
{
struct VitalPluginImport
{
    VitalFileKind kind = VitalFileKind::unknown;
    bool bank = false;
    bool ok = false;
    std::string error;
    std::string name; // preset / table / shape / bank name

    // a preset (.vital): the patch to load and the report of the sheet
    ParamValues values = ParamValues::defaults();
    StateContent content;
    PresetMeta meta;
    ImportReport report;
    std::string reportJson;
    std::vector<std::string> macroNames;

    // a stand-alone table (.vitaltable): stored in the library, 1 frame count
    ContentFileRef tableRef;
    int tableFrames = 0;
    std::string tableAuthor;

    // a stand-alone shape (.vitallfo)
    LfoShapeFile lfoShape;

    // a bank (.vitalbank): one row per entry, the presets are under `presetDirectory`
    BatchSummary summary;
    std::string presetDirectory;
};

/** Imports one Vital file. `bytes` is the file, `fileName` decides the kind (case insensitive extension). `caps` are the capabilities of the engine
    (what the importer may keep: user tables, custom shapes, the sample oscillator). `userDir` receives the presets of a bank
    (`<userDir>/presets/<bank>/`). Never throws. */
VitalPluginImport importVitalFileForPlugin (const std::string& fileName, const std::vector<uint8_t>& bytes, UserContentStore& store,
                                            const std::string& userDir, EngineCapsMask caps);

/** The ImportCaps the importer needs for the capabilities of an engine. */
ImportCaps importCapsFor (EngineCapsMask caps) noexcept;

/** The text of the report sheet: one line per item ("approximated: osc1.warp.type  ... detail"), at most `maxLines`. */
std::vector<std::string> reportLines (const ImportReport& report, size_t maxLines = 200);
} // namespace synthyboy
