// SynthyBoy - core: the formats of the items of the user library (user_library.h): formula presets and FX chain (rack) presets.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// {"format":"synthyboy-formula","version":1,"name":"..","expression":".."}
// {"format":"synthyboy-fx-rack","version":1,"name":"..","values":{"reverb.on":1,"fx.order.1":3,...}}
// The LFO shape item is the `.lfoshape.json` of user_content.h. JUCE free.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "synthyboy/state.h"

namespace synthyboy
{
struct FormulaExample
{
    const char* name;
    const char* expression;
};
/** The built-in formula examples (S14-04: the manual's p.309-311 patterns and the editor's presets). Every expression compiles. */
const std::vector<FormulaExample>& formulaExamples();

std::string serializeFormulaItem (const std::string& name, const std::string& expression);
bool parseFormulaItem (const std::string& json, std::string& name, std::string& expression);

/** True for the keys the FX chain consists of: the parameters of the effect modules (their prefixes), the splitters, `fx.order.N` and the pool slots
    `fxiNN.*`. */
bool isFxChainKey (const char* key) noexcept;
/** The FX chain of `values` as a preset item. */
std::string serializeFxRackPreset (const std::string& name, const ParamValues& values);
/** Reads an item and writes its FX chain keys into `values` (keys that are not FX chain keys or not in the table are skipped; values are clamped to
    the range of the key). Returns the number of values taken, 0 when the text is not an FX rack item. */
int applyFxRackPreset (const std::string& json, ParamValues& values);

/** The voice control (GROUPA2, WP-07): the 62 keys `voicectl.*` as a preset item
    {"format":"synthyboy-voice-control","version":1,"name":"..","values":{"voicectl.length":8,...}}. */
bool isVoiceControlKey (const char* key) noexcept;
std::string serializeVoiceControlPreset (const std::string& name, const ParamValues& values);
/** Reads an item and writes its voice control keys into `values` (other keys are skipped; values are clamped to the range of the key). Keys the item does
    not mention are set to their default (a saved configuration is complete). Returns the number of values taken, 0 when the text is not a voice control item. */
int applyVoiceControlPreset (const std::string& json, ParamValues& values);
} // namespace synthyboy
