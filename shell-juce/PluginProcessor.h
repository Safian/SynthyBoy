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

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/state.h"

/**
 * JUCE shell: owns no DSP. The parameter layout is generated from synthyboy::paramTable() (one JUCE parameter per entry, grouped
 * by the " / " hierarchy of ParamInfo::group), MIDI and parameter values are converted into synthyboy::Event /
 * IEngine::setParam each block (only the parameters whose value changed), and the state blob is synthyboy::saveState().
 */
class SynthyBoyProcessor final : public juce::AudioProcessor
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
    uint32_t getDroppedEventCount() const noexcept { return droppedEvents.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    synthyboy::ParamValues currentValues() const;
    void applyValues (const synthyboy::ParamValues& values);
    bool loadLegacyXml (const void* data, int sizeInBytes, synthyboy::ParamValues& values) const;

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, synthyboy::kMaxParams> rawParams {};
    std::array<float, synthyboy::kMaxParams> sentToEngine {}; // audio thread: the value the engine has, for change detection

    std::unique_ptr<synthyboy::IEngine> engine;
    std::vector<synthyboy::Event> events;
    std::atomic<uint32_t> droppedEvents { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthyBoyProcessor)
};
