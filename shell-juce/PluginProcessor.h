/*
 * SynthyBoy - minimal 8-voice synthesizer (VST3 + CLAP): thin JUCE shell around synthyboy_core
 * Copyright (C) 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/state.h"
#include "ui/UiHost.h"

/**
 * JUCE shell: owns no DSP. The parameter layout is generated from synthyboy::paramTable() (one JUCE parameter per entry, grouped
 * by the " / " hierarchy of ParamInfo::group), MIDI and parameter values are converted into synthyboy::Event /
 * IEngine::setParam each block (only the parameters whose value changed), and the state blob is synthyboy::saveState().
 */
class SynthyBoyProcessor final : public juce::AudioProcessor,
                                 public synthyboy::ui::UiHost
{
public:
    /** Capacity of the per-block event list (allocated in prepareToPlay); further events of a block are dropped and counted. */
    static constexpr int maxEventsPerBlock = 1024;

    SynthyBoyProcessor();
    ~SynthyBoyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    // synthyboy::ui::UiHost (the editor's view of the processor; message thread)
    juce::AudioProcessorValueTreeState& parameters() override { return apvts; }
    synthyboy::EngineCapsMask engineCaps() const noexcept override { return caps; }
    juce::MidiKeyboardState& keyboardState() override { return keyboard; }
    synthyboy::ui::AbCompare& abCompare() override { return ab; }
    void stepPreset (int direction) override { if (presetStepHandler) presetStepHandler (direction); }
    juce::String presetName() const override { return presetNameText; }

    /** Preset browsing belongs to Kick: the owner of the preset list installs the prev / next callback and sets the shown name. */
    void setPresetStepHandler (std::function<void (int)> handler) { presetStepHandler = std::move (handler); }
    void setPresetName (const juce::String& name) { presetNameText = name; }
    uint32_t getDroppedEventCount() const noexcept { return droppedEvents.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    synthyboy::ParamValues currentValues() const;
    void applyValues (const synthyboy::ParamValues& values);
    bool loadLegacyXml (const void* data, int sizeInBytes, synthyboy::ParamValues& values) const;

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, synthyboy::kMaxParams> rawParams {};
    std::array<float, synthyboy::kMaxParams> sentToEngine {}; // audio thread: the value the engine has, for change detection

    // The `content` section of the state (custom LFO shapes, content references, ...: not parameters). Part of the host state
    // (docs/ui-design/13-new-keys.md A25); no engine reads it yet. Message thread only, the mutex covers hosts that do not.
    std::mutex contentMutex;
    synthyboy::StateContent content;

    std::unique_ptr<synthyboy::IEngine> engine;
    synthyboy::EngineCapsMask caps = synthyboy::kCapNone; // engine->capabilities (), constant after construction

    // editor state that outlives the editor window
    juce::MidiKeyboardState keyboard; // notes of the on-screen keyboard, merged into the MIDI input of processBlock
    synthyboy::ui::AbCompare ab;
    juce::String presetNameText { "Init" };
    std::function<void (int)> presetStepHandler;
    std::vector<synthyboy::Event> events;
    std::atomic<uint32_t> droppedEvents { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthyBoyProcessor)
};
