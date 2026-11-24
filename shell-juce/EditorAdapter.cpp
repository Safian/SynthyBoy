// SynthyBoy - JUCE shell: the thin juce::AudioProcessorEditor around the host-independent SynthyBoyEditor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EditorAdapter.h"

namespace synthyboy::ui
{
SynthyBoyEditorAdapter::SynthyBoyEditorAdapter (juce::AudioProcessor& owner, UiHost& host)
    : juce::AudioProcessorEditor (owner), editor (host)
{
    const auto initialSize = editor.getBounds(); // the limits below would squeeze the default 400 x 300 of an AudioProcessorEditor into the editor
    addAndMakeVisible (editor);
    setResizable (true, false);
    setResizeLimits (SynthyBoyEditor::kMinWidth, SynthyBoyEditor::kMinHeight, SynthyBoyEditor::kMaxWidth, SynthyBoyEditor::kMaxHeight);
    setSize (initialSize.getWidth(), initialSize.getHeight());
    editor.addComponentListener (this);
}

SynthyBoyEditorAdapter::~SynthyBoyEditorAdapter()
{
    editor.removeComponentListener (this);
}

void SynthyBoyEditorAdapter::resized()
{
    editor.setBounds (getLocalBounds());
}

void SynthyBoyEditorAdapter::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    // setReferenceSize () / setUiScale () resize the editor itself: the plug-in window follows
    if (wasResized && (getWidth() != editor.getWidth() || getHeight() != editor.getHeight()))
        setSize (editor.getWidth(), editor.getHeight());
}
} // namespace synthyboy::ui
