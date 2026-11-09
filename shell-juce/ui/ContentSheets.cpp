// SynthyBoy - UI: the Missing Files dialog and the Vital import report (ContentSheets.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ContentSheets.h"

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
            if (! ctx.host.requestFile (request, [safe = juce::Component::SafePointer<MissingFilesSheet> (this), i] (const juce::File& file)
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
} // namespace synthyboy::ui
