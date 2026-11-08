// SynthyBoy - UI: the module sheet (docs/ui-design/01 section 2.4): long-press a module header (OSC 1, FILTER 2, ENV 3, LFO 4, ...).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Copy | Copy with mods | Paste | Paste with mods | Swap with ... | Init | Lock module (GAP: no key, shown disabled). The operations are free
// functions on the UiContext (ModuleClipboard / ModuleSnapshot::swapModules of model/ModModel.h), so the gestures that reach them
// (header long-press, strip drag onto another strip) and the tests use the same code.
#pragma once

#include <string>
#include <vector>

#include "UiContext.h"

namespace synthyboy::ui
{
namespace module_ops
{
/** "osc2" -> "OSC 2", "filter1" -> "FILTER 1", "rlfo3" -> "RANDOM LFO 3". Empty for a prefix that is not a module. */
juce::String displayName (const std::string& prefix);
/** The modules of the same kind as `prefix` (osc1..3, filter1..2, env1..6, lfo1..10, rlfo1..4), `prefix` itself excluded. */
std::vector<std::string> siblings (const std::string& prefix);
bool isModule (const std::string& prefix);

bool copy (UiContext& ctx, const std::string& prefix, bool withMods);
bool paste (UiContext& ctx, const std::string& prefix, bool withMods);
bool init (UiContext& ctx, const std::string& prefix);
/** Swaps all settings of the two modules (and the modulation slots that name them when withMods). */
bool swap (UiContext& ctx, const std::string& a, const std::string& b, bool withMods = true);
/** Copies `from` onto `to` without touching the clipboard. */
bool copyOnto (UiContext& ctx, const std::string& from, const std::string& to, bool withMods);
} // namespace module_ops

class ModuleSheet final : public juce::Component
{
public:
    ModuleSheet (UiContext& context, const std::string& modulePrefix);

    void resized() override;
    void paint (juce::Graphics&) override;
    int preferredHeight() const;
    int preferredWidth() const;

    const std::string& prefix() const noexcept { return modulePrefix; }
    /** The buttons, by their text (the tests click them). */
    juce::TextButton* button (const juce::String& text);
    /** The "Swap with" entries: one button per sibling. */
    juce::TextButton* swapButton (const std::string& siblingPrefix);

private:
    void refreshEnabled();
    void closeSheet();

    UiContext& ctx;
    std::string modulePrefix;
    juce::OwnedArray<juce::TextButton> buttons;
    std::vector<std::pair<std::string, juce::TextButton*>> swapButtons;
    juce::Label swapLabel;
};

/** Opens the sheet as a call-out box pointing at `anchor` (async). */
void showModuleSheet (UiContext& context, const std::string& prefix, juce::Component& anchor);
} // namespace synthyboy::ui
