// SynthyBoy - UI: the always-visible strip (docs/ui-design/01-shell-layout.md section 2): top bar, macro row, keyboard.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "ParamControls.h"

namespace synthyboy::ui
{
/** Preset name with prev / next (browsing is Kick's: the buttons only call UiHost::stepPreset), A/B compare, assign-mode toggle, menu. */
class TopBar final : public juce::Component
{
public:
    explicit TopBar (UiContext& context);

    void setPresetName (const juce::String& name);
    void setAbLabel (const juce::String& text) { ab.setButtonText (text); }
    void setAssignOpen (bool open) { assign.setToggleState (open, juce::dontSendNotification); }

    std::function<void()> onAbToggle, onAssignToggle, onMenu;

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::TextButton prev { "<" }, next { ">" }, ab { "A" }, assign { "MOD" }, menu { "..." };

private:
    UiContext& ctx;
    juce::String presetName;
};

/** macro.1 .. macro.8 as compact knobs (keys macro.5..8 dim when the engine lacks kCapMacros58). */
class MacroRow final : public juce::Component
{
public:
    explicit MacroRow (UiContext& context);
    void resized() override;
    const std::vector<std::unique_ptr<ParamKnob>>& knobs() const noexcept { return macros; }

private:
    UiContext& ctx;
    std::vector<std::unique_ptr<ParamKnob>> macros;
};

/** Mini keyboard: notes go through UiHost::keyboardState(), which the processor merges into its MIDI input. */
class KeyboardStrip final : public juce::Component
{
public:
    explicit KeyboardStrip (UiContext& context);

    void resized() override;
    juce::MidiKeyboardComponent& keyboard() noexcept { return *keys; }
    void shiftOctave (int octaves);

    juce::TextButton octDown { "oct-" }, octUp { "oct+" };

private:
    UiContext& ctx;
    std::unique_ptr<juce::MidiKeyboardComponent> keys;
};
} // namespace synthyboy::ui
