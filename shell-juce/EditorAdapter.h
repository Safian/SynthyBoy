// SynthyBoy - JUCE shell: the thin juce::AudioProcessorEditor around the host-independent SynthyBoyEditor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// All the UI lives in ui/Editor.h (a plain juce::Component); this class only gives the plug-in format its AudioProcessorEditor: the window size
// and the resize limits are mirrored both ways, nothing else.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/Editor.h"

namespace synthyboy::ui
{
class SynthyBoyEditorAdapter final : public juce::AudioProcessorEditor,
                                     private juce::ComponentListener
{
public:
    SynthyBoyEditorAdapter (juce::AudioProcessor& owner, UiHost& host);
    ~SynthyBoyEditorAdapter() override;

    SynthyBoyEditor& inner() noexcept { return editor; }
    void resized() override;

private:
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    SynthyBoyEditor editor;
};

/** The SynthyBoyEditor inside `base` (what the plug-in's createEditor () returned), null for another editor. */
inline SynthyBoyEditor* innerEditor (juce::AudioProcessorEditor* base) noexcept
{
    auto* adapter = dynamic_cast<SynthyBoyEditorAdapter*> (base);
    return adapter != nullptr ? &adapter->inner() : nullptr;
}
} // namespace synthyboy::ui
