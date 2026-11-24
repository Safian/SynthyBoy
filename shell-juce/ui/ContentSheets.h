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
    ImportReportSheet (UiContext& context, const SerumFlowResult& result);

    void paint (juce::Graphics& g) override;
    void resized() override;
    juce::TextButton& closeButton() noexcept { return closeBtn; }
    const juce::String& text() const noexcept { return body; }
    std::function<void()> onFinished;

private:
    UiContext& ctx;
    juce::String headline, body, title { "Vital import" };
    juce::TextEditor view;
    juce::TextButton closeBtn { "Close" };
};

/** A short message with a Close button (the result of a menu action that has no sheet of its own). */
class MessageSheet final : public juce::Component
{
public:
    MessageSheet (UiContext& context, juce::String titleText, juce::String bodyText);

    void paint (juce::Graphics& g) override;
    void resized() override;
    juce::TextButton& closeButton() noexcept { return closeBtn; }
    const juce::String& text() const noexcept { return body; }
    std::function<void()> onFinished;

private:
    UiContext& ctx;
    juce::String title, body;
    juce::TextButton closeBtn { "Close" };
};

/** The last note as audio (GROUPD WP-15, S01-09): the note is rendered offline (ultra quality when the preference says so), the sheet shows the waveform and offers Save as WAV (a file
    chooser), Share (the platform share sheet through UiHost::shareFile) and the drag chip (JUCE desktop: the WAV file is dragged out to the DAW). */
class LastNoteSheet final : public juce::Component
{
public:
    explicit LastNoteSheet (UiContext& context);
    ~LastNoteSheet() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Renders the last note again (the sheet does it on construction). */
    FlowResult render();
    const LastNoteAudio& note() const noexcept { return rendered; }
    const juce::String& status() const noexcept { return statusLine; }
    /** Save as WAV: asks the host for the file (FileChooser); `saveTo` writes it. */
    void requestSave();
    FlowResult saveTo (const juce::File& file);
    /** Share: the file in the temp folder goes to the share hook of the host. */
    FlowResult share();
    /** Drag-out: writes the temp file and starts an external drag of it (the chip calls this when the drag begins; false where the platform has no file drag, mobile). */
    bool startDragOut();
    /** The file the drag-out / share would hand over (written on demand). */
    juce::File dragFile();

    juce::TextButton& saveButton() noexcept { return saveBtn; }
    juce::TextButton& shareButton() noexcept { return shareBtn; }
    juce::TextButton& closeButton() noexcept { return closeBtn; }
    juce::Component& dragChip() noexcept;
    /** Test hook: replaces the external drag (headless runs cannot start one). */
    std::function<bool (const juce::File&)> dragHandler;
    std::function<void()> onFinished;

private:
    class Chip;
    UiContext& ctx;
    LastNoteAudio rendered;
    juce::String statusLine;
    std::vector<float> peaksMin, peaksMax;
    juce::File tempFile;
    juce::TextButton saveBtn { "Save as WAV..." }, shareBtn { "Share..." }, closeBtn { "Close" };
    std::unique_ptr<Chip> chip;
};
} // namespace synthyboy::ui
