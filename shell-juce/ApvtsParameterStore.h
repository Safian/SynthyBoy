// SynthyBoy - JUCE shell: the editor's ParameterStore on the plug-in's AudioProcessorValueTreeState.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Plug-in side only (needs juce_audio_processors, which the UI library does not use). Every table entry is an APVTS parameter with ParameterID { key, 1 }.
// Writes go through the JUCE parameter (beginChangeGesture / setValueNotifyingHost / endChangeGesture) so the host records automation. Changes made
// by the host (automation, state load) arrive on any thread; they are handed to the listeners on the message thread (at once when already there).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

#include "ui/ParameterStore.h"

namespace synthyboy::ui
{
class ApvtsParameterStore final : public ParameterStore,
                                  private juce::AudioProcessorParameter::Listener,
                                  private juce::AsyncUpdater
{
public:
    explicit ApvtsParameterStore (juce::AudioProcessorValueTreeState& apvts);
    ~ApvtsParameterStore() override;

    bool contains (int index) const noexcept override { return parameterAt (index) != nullptr; }
    float plain (int index) const override;
    float normalised (int index) const override;
    float toNormalised (int index, float plainValue) const override;
    float fromNormalised (int index, float normalisedValue) const override;
    void beginGesture (int index) override;
    void endGesture (int index) override;
    void setNormalised (int index, float normalisedValue) override;

private:
    juce::RangedAudioParameter* parameterAt (int index) const noexcept;
    void parameterValueChanged (int juceIndex, float) override;
    void parameterGestureChanged (int juceIndex, bool starting) override;
    void handleAsyncUpdate() override;
    int tableIndexOf (int juceIndex) const noexcept;

    juce::AudioProcessorValueTreeState& state;
    std::vector<juce::RangedAudioParameter*> parameters; // by table index
    std::vector<int> tableIndexOfJuceIndex;              // by AudioProcessorParameter::getParameterIndex (), -1: not in the table
    std::unique_ptr<std::atomic<bool>[]> changedOffThread; // by table index
};
} // namespace synthyboy::ui
