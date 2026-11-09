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
#include "synthyboy/shell_features.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/user_library.h"
#include "ui/UiHost.h"

/**
 * JUCE shell: owns no DSP. The parameter layout is generated from synthyboy::paramTable() (one JUCE parameter per entry, grouped
 * by the " / " hierarchy of ParamInfo::group), MIDI and parameter values are converted into synthyboy::Event /
 * IEngine::setParam each block (only the parameters whose value changed), and the state blob is synthyboy::saveState().
 */
class SynthyBoyProcessor final : public juce::AudioProcessor,
                                 public synthyboy::ui::UiHost,
                                 private juce::Timer
{
public:
    /** Capacity of the per-block event list (allocated in prepareToPlay); further events of a block are dropped and counted. */
    static constexpr int maxEventsPerBlock = 1024;

    SynthyBoyProcessor();
    ~SynthyBoyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void setNonRealtime (bool isNonRealtime) noexcept override;
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
    float outputPeakL() const noexcept override { return peakL.load (std::memory_order_relaxed); }
    float outputPeakR() const noexcept override { return peakR.load (std::memory_order_relaxed); }
    void injectMidiMessage (const juce::MidiMessage& msg) override;
    bool revertToSaved() override;
    int activeVoiceCount() const noexcept override { return voiceCount.load (std::memory_order_relaxed); }
    float cpuLoad() const noexcept override { return cpu.load (std::memory_order_relaxed); }
    int copyScope (float* dest, int maxSamples) const override;
    synthyboy::StateContent content() const override;
    bool setContent (const synthyboy::StateContent& newContent) override;
    void reloadContent() override;

    // content pipeline (WP-01): the user content library behind the engine, the JSON item library, file requests
    synthyboy::UserContentStore* contentLibrary() override { return contentStore.get(); }
    synthyboy::UserLibrary* userLibrary() override { return itemLibrary.get(); }
    std::string userDirectory() const override { return userDir; }
    bool requestFile (const synthyboy::ui::FileRequest& request, std::function<void (const juce::File&)> done) override;
    bool canRequestFile() const noexcept override { return true; }
    uint32_t contentRevision() const noexcept override { return contentRev.load (std::memory_order_relaxed); }
    /** Tests and alternative shells answer the file requests themselves (instead of a juce::FileChooser). */
    using FileRequestHandler = std::function<bool (const synthyboy::ui::FileRequest&, std::function<void (const juce::File&)>)>;
    void setFileRequestHandler (FileRequestHandler handler) { fileRequestHandler = std::move (handler); }

    // MIDI learn (G07), Save as Default (G02), locks (G08): docs/ui-design/15-shell-features.md
    bool canMidiLearn() const noexcept override { return true; }
    void startMidiLearn (const std::string& key) override;
    void cancelMidiLearn() override;
    std::string midiLearnKey() const override;
    int midiCcForKey (const std::string& key) const override;
    void clearMidiCc (const std::string& key) override;
    uint32_t midiMapRevision() const noexcept override { return midiMap.revision(); }
    bool saveAsDefault() override;
    bool hasUserDefault() const override { return synthyboy::hasUserDefault (userDir); }
    bool clearUserDefault() override;
    bool initPatch() override;
    bool canLock() const noexcept override { return true; }
    bool isModuleLocked (const std::string& prefix) const override;
    void setModuleLocked (const std::string& prefix, bool locked) override;
    bool loadPatch (const synthyboy::ParamValues& values, const synthyboy::StateContent& newContent) override;
    synthyboy::MidiCcMap& midiCcMap() noexcept { return midiMap; }
    /** The directory of the user default (SYNTHYBOY_USER_DIR, else the user data directory); fixed at construction. */
    const std::string& userDataDir() const noexcept { return userDir; }

    /** Preset browsing belongs to Kick: the owner of the preset list installs the prev / next callback and sets the shown name. */
    void setPresetStepHandler (std::function<void (int)> handler) { presetStepHandler = std::move (handler); }
    void setPresetName (const juce::String& name) { presetNameText = name; }
    uint32_t getDroppedEventCount() const noexcept { return droppedEvents.load(); }

private:
    /** Message thread, 20 Hz: IEngine::needsPrepare () (quality.oversample_4x) -> prepare the engine again with the processing suspended. */
    void timerCallback() override;
    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 512;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    synthyboy::ParamValues currentValues() const;
    void applyValues (const synthyboy::ParamValues& values);
    bool loadLegacyXml (const void* data, int sizeInBytes, synthyboy::ParamValues& values) const;

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, synthyboy::kMaxParams> rawParams {};
    std::array<juce::RangedAudioParameter*, synthyboy::kMaxParams> paramPtrs {}; // looked up once: the audio thread must not search by name
    std::array<float, synthyboy::kMaxParams> sentToEngine {}; // audio thread: the value the engine has, for change detection

    // The `content` section of the state (custom LFO shapes, content references, ...: not parameters). Part of the host state
    // (docs/ui-design/13-new-keys.md A25); no engine reads it yet. Message thread only, the mutex covers hosts that do not.
    mutable std::mutex contentMutex;
    synthyboy::StateContent patchContent;

    // what the host loaded last (or the defaults): "Revert to saved". Message thread, the content mutex covers hosts that do not.
    synthyboy::ParamValues savedValues = synthyboy::ParamValues::defaults();
    synthyboy::StateContent savedContent;

    // shell features (not parameters): the CC map is read and learned on the audio thread (atomics), the locks belong to the message thread
    synthyboy::MidiCcMap midiMap;
    std::vector<std::string> locks; // guarded by contentMutex
    std::string userDir;
    synthyboy::ShellState shellState() const;       // takes contentMutex
    std::string stateText();                         // saveState + content + shell section (the host state and the user default)
    void applyShell (const synthyboy::ShellState& shell);
    /** Parameters and content of a loaded patch; `respectLocks` keeps the locked modules of the current patch. */
    void applyPatch (synthyboy::ParamValues values, synthyboy::StateContent newContent, bool respectLocks);

    // declared before the engine: the engine is destroyed first and never sees a dead store
    std::unique_ptr<synthyboy::UserContentStore> contentStore;
    std::unique_ptr<synthyboy::UserLibrary> itemLibrary;
    std::atomic<uint32_t> contentRev { 0 };
    FileRequestHandler fileRequestHandler;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::unique_ptr<synthyboy::IEngine> engine;
    synthyboy::EngineCapsMask caps = synthyboy::kCapNone; // engine->capabilities (), constant after construction

    // editor state that outlives the editor window
    juce::MidiKeyboardState keyboard; // notes of the on-screen keyboard, merged into the MIDI input of processBlock
    synthyboy::ui::AbCompare ab;
    juce::String presetNameText { "Init" };
    std::function<void (int)> presetStepHandler;
    std::vector<synthyboy::Event> events;
    std::atomic<uint32_t> droppedEvents { 0 };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<int> voiceCount { 0 };
    std::atomic<float> cpu { 0.0f };
    // mono ring of the last output samples for the master scope (audio thread writes, the UI reads; relaxed atomics, a torn read only
    // shows one odd sample)
    std::array<std::atomic<float>, synthyboy::ui::UiHost::kScopeSamples> scopeRing {};
    std::atomic<uint32_t> scopeWrite { 0 };
    std::mutex uiMidiMutex;
    juce::MidiBuffer uiMidiBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthyBoyProcessor)
};
