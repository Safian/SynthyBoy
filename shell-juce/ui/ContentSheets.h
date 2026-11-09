// SynthyBoy - UI: the two sheets of the content pipeline: the Missing Files dialog and the Vital import report (WP-01).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "ContentIO.h"

namespace synthyboy::ui
{
/** The Missing Files dialog: one row per reference the library cannot resolve, with Locate (file chooser) and Remove, and Remove All / Close. */
class MissingFilesSheet final : public juce::Component
{
public:
    explicit MissingFilesSheet (UiContext& context);

    void paint (juce::Graphics& g) override;
    void resized() override;

    MissingFilesModel& model() noexcept { return rows; }
    int rowCount() const noexcept { return static_cast<int> (locateButtons.size()); }
    juce::TextButton& locateButton (int row) { return *locateButtons[static_cast<size_t> (row)]; }
    juce::TextButton& removeButton (int row) { return *removeButtons[static_cast<size_t> (row)]; }
    juce::TextButton& closeButton() noexcept { return closeBtn; }
    juce::TextButton& removeAllButton() noexcept { return removeAllBtn; }
    /** Called when the dialog has nothing left to show or Close was pressed. */
    std::function<void()> onFinished;
    /** Rebuilds the rows from the model. */
    void rebuild();

private:
    UiContext& ctx;
    MissingFilesModel rows;
    std::vector<std::unique_ptr<juce::TextButton>> locateButtons, removeButtons;
    juce::TextButton closeBtn { "Close" }, removeAllBtn { "Remove all" };
    juce::String statusLine;
};

/** The report of a Vital import: what was taken, approximated and dropped (the lines of the ImportReport). */
class ImportReportSheet final : public juce::Component
{
public:
    ImportReportSheet (UiContext& context, const VitalFlowResult& result);

    void paint (juce::Graphics& g) override;
    void resized() override;
    juce::TextButton& closeButton() noexcept { return closeBtn; }
    const juce::String& text() const noexcept { return body; }
    std::function<void()> onFinished;

private:
    UiContext& ctx;
    juce::String headline, body;
    juce::TextEditor view;
    juce::TextButton closeBtn { "Close" };
};
} // namespace synthyboy::ui
