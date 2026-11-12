// SynthyBoy - UI: the Missing Files dialog and the Vital import report (ContentSheets.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ContentSheets.h"

#include <algorithm>

namespace synthyboy::ui
{
MissingFilesSheet::MissingFilesSheet (UiContext& context) : ctx (context), rows (context)
{
    closeBtn.onClick = [this] { if (onFinished) onFinished(); };
    removeAllBtn.onClick = [this]
    {
        statusLine = rows.removeAll().message;
        rebuild();
    };
    addAndMakeVisible (closeBtn);
    addAndMakeVisible (removeAllBtn);
    rebuild();
}

void MissingFilesSheet::rebuild()
{
    locateButtons.clear();
    removeButtons.clear();
    rows.refresh();
    for (size_t i = 0; i < rows.items().size(); ++i)
    {
        auto locate = std::make_unique<juce::TextButton> ("Locate...");
        auto remove = std::make_unique<juce::TextButton> ("Remove");
        locate->onClick = [this, i]
        {
            FileRequest request;
            request.title = "Locate the missing file";
            request.wildcards = rows.items()[i].kind == ContentKind::wavetable ? "*.wav;*.vitaltable;*.flac;*.aif;*.aiff" : audioFileWildcard();
            request.purpose = "locate";
            juce::Component::SafePointer<MissingFilesSheet> safe (this); // outside the lambda introducer: MSVC cannot init-capture a SafePointer built from `this` in a nested lambda
            if (! ctx.host.requestFile (request, [safe, i] (const juce::File& file)
                {
                    if (safe == nullptr)
                        return;
                    safe->statusLine = safe->rows.locate (i, file).message;
                    safe->rebuild();
                }))
                statusLine = "This host cannot ask for a file";
        };
        remove->onClick = [this, i]
        {
            statusLine = rows.remove (i).message;
            rebuild();
        };
        addAndMakeVisible (*locate);
        addAndMakeVisible (*remove);
        locateButtons.push_back (std::move (locate));
        removeButtons.push_back (std::move (remove));
    }
    removeAllBtn.setEnabled (! rows.empty());
    resized();
    repaint();
    if (rows.empty() && onFinished && statusLine.isNotEmpty())
        onFinished();
}

void MissingFilesSheet::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    g.setColour (t.colours.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (8.0f));
    g.setColour (t.colours.outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.px (8.0f), 1.5f);
    g.setColour (t.colours.text);
    g.setFont (t.titleFont());
    auto r = getLocalBounds().reduced (t.gap());
    g.drawText ("Missing files", r.removeFromTop (t.touch()), juce::Justification::centredLeft, true);
    g.setFont (t.labelFont());
    g.setColour (t.colours.textDim);
    g.drawText (rows.empty() ? juce::String ("Everything this preset needs was found.")
                             : juce::String ("This preset refers to files the library does not have. Locate them, or remove the references (the sound falls back)."),
                r.removeFromTop (t.ipx (36.0f)), juce::Justification::centredLeft, true);
    int y = r.getY();
    for (size_t i = 0; i < rows.items().size(); ++i)
    {
        g.setColour (t.colours.text);
        g.drawText (rows.describe (i), juce::Rectangle<int> (r.getX(), y, r.getWidth() - 2 * t.touch() * 2 - 2 * t.gap(), t.touch()), juce::Justification::centredLeft, true);
        y += t.touch() + t.gap();
    }
    if (statusLine.isNotEmpty())
    {
        g.setColour (t.colours.mod);
        g.drawText (statusLine, juce::Rectangle<int> (r.getX(), getHeight() - t.touch() * 2 - 2 * t.gap(), r.getWidth(), t.ipx (20.0f)), juce::Justification::centredLeft, true);
    }
}

void MissingFilesSheet::resized()
{
    const auto& t = ctx.theme;
    auto r = getLocalBounds().reduced (t.gap());
    auto bottom = r.removeFromBottom (t.touch());
    closeBtn.setBounds (bottom.removeFromRight (juce::jmax (t.touch() * 2, t.ipx (90.0f))));
    bottom.removeFromRight (t.gap());
    removeAllBtn.setBounds (bottom.removeFromRight (juce::jmax (t.touch() * 2, t.ipx (110.0f))));
    r.removeFromTop (t.touch() + t.ipx (36.0f));
    int y = r.getY();
    for (size_t i = 0; i < locateButtons.size(); ++i)
    {
        const int bw = t.touch() * 2;
        removeButtons[i]->setBounds (r.getRight() - bw, y, bw, t.touch());
        locateButtons[i]->setBounds (r.getRight() - 2 * bw - t.gap(), y, bw, t.touch());
        y += t.touch() + t.gap();
    }
}

ImportReportSheet::ImportReportSheet (UiContext& context, const VitalFlowResult& result) : ctx (context)
{
    const VitalPluginImport& in = result.imported;
    headline = result.flow.message;
    if (in.kind == VitalFileKind::preset)
    {
        static const char* const fidelity[] = { "exact", "close", "degraded" };
        body << "Fidelity: " << fidelity[static_cast<int> (in.report.fidelity)] << "\n"
             << in.report.numImported << " settings imported, " << in.report.numApproximated << " approximated, " << in.report.numDropped << " dropped, "
             << in.report.numIgnored << " ignored, " << in.report.numDefaulted << " defaulted\n\n";
        for (const auto& line : reportLines (in.report))
            body << juce::String (line) << "\n";
        if (in.report.truncated)
            body << "... (the list is cut)\n";
    }
    else if (in.bank)
    {
        for (const auto& row : in.summary.rows)
        {
            static const char* const outcome[] = { "written", "already there", "needs a decision", "failed", "ignored" };
            body << outcome[static_cast<int> (row.outcome)] << ": " << juce::String (row.source);
            if (! row.error.empty())
                body << "  (" << juce::String (row.error) << ")";
            body << "\n";
        }
    }
    view.setMultiLine (true, true);
    view.setReadOnly (true);
    view.setText (body, false);
    closeBtn.onClick = [this] { if (onFinished) onFinished(); };
    addAndMakeVisible (view);
    addAndMakeVisible (closeBtn);
}

ImportReportSheet::ImportReportSheet (UiContext& context, const SerumFlowResult& result) : ctx (context)
{
    const SerumPluginImport& in = result.imported;
    title = "Serum 2 import";
    headline = result.flow.message;
    static const char* const fidelity[] = { "exact", "close", "degraded" };
    body << "Fidelity: " << fidelity[static_cast<int> (in.report.fidelity)] << "\n"
         << in.report.numImported << " values imported, " << in.report.numApproximated << " approximated, " << in.report.numDropped << " dropped, "
         << in.report.numIgnored << " ignored, " << in.report.numDefaulted << " Serum defaults applied\n";
    if (in.missingFiles > 0)
        body << in.missingFiles << " referenced files were not found (open the file from inside the Serum content folder)\n";
    body << "\n";
    for (const auto& line : serumReportLines (in.report))
        body << juce::String (juce::CharPointer_UTF8 (line.c_str())) << "\n";
    if (in.report.truncated)
        body << "... (the list is cut)\n";
    view.setMultiLine (true, true);
    view.setReadOnly (true);
    view.setText (body, false);
    closeBtn.onClick = [this] { if (onFinished) onFinished(); };
    addAndMakeVisible (view);
    addAndMakeVisible (closeBtn);
}

void ImportReportSheet::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    g.setColour (t.colours.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (8.0f));
    g.setColour (t.colours.outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.px (8.0f), 1.5f);
    g.setColour (t.colours.text);
    g.setFont (t.titleFont());
    g.drawText (title, getLocalBounds().reduced (t.gap()).removeFromTop (t.touch()), juce::Justification::centredLeft, true);
    g.setFont (t.labelFont());
    g.setColour (t.colours.textDim);
    auto r = getLocalBounds().reduced (t.gap());
    r.removeFromTop (t.touch());
    g.drawText (headline, r.removeFromTop (t.ipx (36.0f)), juce::Justification::centredLeft, true);
}

void ImportReportSheet::resized()
{
    const auto& t = ctx.theme;
    auto r = getLocalBounds().reduced (t.gap());
    closeBtn.setBounds (r.removeFromBottom (t.touch()).removeFromRight (juce::jmax (t.touch() * 2, t.ipx (90.0f))));
    r.removeFromTop (t.touch() + t.ipx (36.0f));
    r.removeFromBottom (t.gap());
    view.setBounds (r);
}

MessageSheet::MessageSheet (UiContext& context, juce::String titleText, juce::String bodyText) : ctx (context), title (std::move (titleText)), body (std::move (bodyText))
{
    closeBtn.onClick = [this] { if (onFinished) onFinished(); };
    addAndMakeVisible (closeBtn);
}

void MessageSheet::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    g.setColour (t.colours.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (8.0f));
    g.setColour (t.colours.outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.px (8.0f), 1.5f);
    auto r = getLocalBounds().reduced (t.gap());
    g.setColour (t.colours.text);
    g.setFont (t.titleFont());
    g.drawText (title, r.removeFromTop (t.touch()), juce::Justification::centredLeft, true);
    g.setFont (t.labelFont());
    g.setColour (t.colours.textDim);
    g.drawFittedText (body, r.removeFromTop (t.touch() * 2), juce::Justification::topLeft, 4);
}

void MessageSheet::resized()
{
    const auto& t = ctx.theme;
    closeBtn.setBounds (getLocalBounds().reduced (t.gap()).removeFromBottom (t.touch()).removeFromRight (juce::jmax (t.touch() * 2, t.ipx (90.0f))));
}

// ---- the last note as audio -------------------------------------------------------------------------------------------------------------

class LastNoteSheet::Chip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Chip (LastNoteSheet& owner) : sheet (owner)
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = sheet.ctx.theme;
        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (6.0f));
        g.setColour (sheet.ctx.host.canShareFiles() && ! sheet.ctx.host.canRequestFile() ? t.colours.outline : t.colours.accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.px (6.0f), 1.5f);
        g.setColour (t.colours.text);
        g.setFont (t.valueFont());
        g.drawFittedText ("Drag out", getLocalBounds(), juce::Justification::centred, 1);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < sheet.ctx.theme.ipx (6.0f))
            return;
        started = sheet.startDragOut();
    }
    void mouseUp (const juce::MouseEvent&) override { started = false; }

private:
    LastNoteSheet& sheet;
    bool started = false;
};

LastNoteSheet::LastNoteSheet (UiContext& context) : ctx (context)
{
    chip = std::make_unique<Chip> (*this);
    saveBtn.onClick = [this] { requestSave(); };
    shareBtn.onClick = [this] { share(); };
    closeBtn.onClick = [this] { if (onFinished) onFinished(); };
    addAndMakeVisible (saveBtn);
    addAndMakeVisible (shareBtn);
    addAndMakeVisible (closeBtn);
    addAndMakeVisible (*chip);
    chip->setTooltip ("Drag the rendered note into your DAW or a folder");
    render();
}

LastNoteSheet::~LastNoteSheet() = default;

juce::Component& LastNoteSheet::dragChip() noexcept { return *chip; }

FlowResult LastNoteSheet::render()
{
    tempFile = juce::File();
    rendered = LastNoteAudio();
    const FlowResult result = renderLastNote (ctx, rendered);
    statusLine = result.message;
    peaksMin.clear();
    peaksMax.clear();
    if (result.ok)
    {
        constexpr int columns = 256;
        const size_t n = rendered.audio.left.size();
        for (int c = 0; c < columns; ++c)
        {
            const size_t from = n * static_cast<size_t> (c) / columns, to = std::max (from + 1, n * static_cast<size_t> (c + 1) / columns);
            float lo = 0.0f, hi = 0.0f;
            for (size_t i = from; i < to && i < n; ++i)
            {
                const float v = 0.5f * (rendered.audio.left[i] + rendered.audio.right[i]);
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
            peaksMin.push_back (lo);
            peaksMax.push_back (hi);
        }
    }
    saveBtn.setEnabled (result.ok);
    shareBtn.setEnabled (result.ok && ctx.host.canShareFiles());
    chip->setVisible (result.ok);
    repaint();
    return result;
}

juce::File LastNoteSheet::dragFile()
{
    if (! tempFile.existsAsFile())
        tempFile = writeLastNoteTemp (rendered);
    return tempFile;
}

void LastNoteSheet::requestSave()
{
    FileRequest request;
    request.mode = FileRequest::Mode::save;
    request.title = "Save the last note as audio";
    request.wildcards = "*.wav";
    request.suggestedName = rendered.name + ".wav";
    request.purpose = "export";
    if (! ctx.host.requestFile (request, [safe = juce::Component::SafePointer<LastNoteSheet> (this)] (const juce::File& file)
        {
            if (safe != nullptr)
                safe->saveTo (file);
        }))
    {
        statusLine = "This host cannot ask for a file";
        repaint();
    }
}

FlowResult LastNoteSheet::saveTo (const juce::File& file)
{
    const FlowResult result = saveLastNoteAs (rendered, file);
    statusLine = result.message;
    repaint();
    return result;
}

FlowResult LastNoteSheet::share()
{
    const juce::File file = dragFile();
    if (! file.existsAsFile())
    {
        statusLine = "Nothing to share";
        repaint();
        return { false, statusLine };
    }
    if (! ctx.host.shareFile (file, rendered.name))
    {
        statusLine = "This host has no share function";
        repaint();
        return { false, statusLine };
    }
    statusLine = "Shared " + file.getFileName();
    repaint();
    return { true, statusLine };
}

bool LastNoteSheet::startDragOut()
{
    const juce::File file = dragFile();
    if (! file.existsAsFile())
        return false;
    const bool started = dragHandler ? dragHandler (file) : ctx.host.dragFileOut (file, *chip);
    statusLine = started ? "Dragging " + file.getFileName() : juce::String ("This platform cannot drag files out: use Share or Save as WAV");
    repaint();
    return started;
}

void LastNoteSheet::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    g.setColour (t.colours.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (8.0f));
    g.setColour (t.colours.outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.px (8.0f), 1.5f);
    auto r = getLocalBounds().reduced (t.gap());
    g.setColour (t.colours.text);
    g.setFont (t.titleFont());
    g.drawText ("Last note as audio", r.removeFromTop (t.touch()), juce::Justification::centredLeft, true);
    g.setFont (t.labelFont());
    g.setColour (t.colours.textDim);
    g.drawText (statusLine, r.removeFromTop (t.ipx (28.0f)), juce::Justification::centredLeft, true);
    auto plot = r.removeFromTop (juce::jmax (t.touch() * 2, t.ipx (120.0f))).toFloat();
    g.setColour (t.colours.panelRaised);
    g.fillRoundedRectangle (plot, t.px (4.0f));
    if (! peaksMax.empty())
    {
        g.setColour (t.colours.accent);
        const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f;
        for (size_t c = 0; c < peaksMax.size(); ++c)
        {
            const float x = plot.getX() + plot.getWidth() * (static_cast<float> (c) + 0.5f) / static_cast<float> (peaksMax.size());
            g.drawLine (x, mid - juce::jlimit (0.0f, 1.0f, peaksMax[c]) * half, x, mid - juce::jlimit (-1.0f, 0.0f, peaksMin[c]) * half, 1.0f);
        }
    }
}

void LastNoteSheet::resized()
{
    const auto& t = ctx.theme;
    auto r = getLocalBounds().reduced (t.gap());
    auto bottom = r.removeFromBottom (t.touch());
    closeBtn.setBounds (bottom.removeFromRight (juce::jmax (t.touch() * 2, t.ipx (90.0f))));
    bottom.removeFromRight (t.gap());
    shareBtn.setBounds (bottom.removeFromRight (juce::jmax (t.touch() * 2, t.ipx (100.0f))));
    bottom.removeFromRight (t.gap());
    saveBtn.setBounds (bottom.removeFromRight (juce::jmax (t.touch() * 2, t.ipx (130.0f))));
    r.removeFromTop (t.touch() + t.ipx (28.0f) + juce::jmax (t.touch() * 2, t.ipx (120.0f)) + t.gap());
    chip->setBounds (r.removeFromTop (t.touch()).removeFromLeft (juce::jmax (t.touch() * 3, t.ipx (140.0f))));
}
} // namespace synthyboy::ui
