// SynthyBoy - UI: the long-press sheet of any control (docs/ui-design/01 section 2.4).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <string>

#include "ParamHandle.h"
#include "UiContext.h"

namespace synthyboy::ui
{
/** The sheet content: name, exact value entry (units allowed, empty text + Enter = cancel), - / + nudge, default and range, Reset,
    MIDI learn (enabled only if the host has the hook), Assign mod (source list -> arms the touch flow), and the list of the
    modulations on this key with bypass and remove. Has its own ParamHandle, so it outlives the control that opened it safely. */
class ParamSheet final : public juce::Component, private juce::ChangeListener
{
public:
    ParamSheet (UiContext& context, const std::string& key);
    ~ParamSheet() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Parses and applies the text of the value box. Returns false (and changes nothing) for empty or unusable text. */
    bool applyValueText (const juce::String& text);
    /** One - / + step. */
    void nudge (int direction);
    int preferredHeight() const;
    int preferredWidth() const;
    ParamHandle& paramHandle() noexcept { return handle; }

private:
    class ModRow;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshValue();
    void rebuildModRows();
    void showSourceMenu();
    void close();

    UiContext& ctx;
    ParamHandle handle;
    juce::Label title, subtitle, info;
    juce::TextEditor valueBox;
    juce::TextButton minus { "-" }, plus { "+" }, reset { "Reset" }, learn { "MIDI learn" }, assign { "Assign mod" };
    juce::OwnedArray<ModRow> rows;
};

/** Opens the sheet for `key` as a call-out box pointing at `anchor` (async; returns immediately). */
void showParamSheet (UiContext& context, const std::string& key, juce::Component& anchor);
} // namespace synthyboy::ui
