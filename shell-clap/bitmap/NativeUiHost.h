// SynthyBoy - bitmap GUI: the UiHost of the native CLAP shell (shell-juce/ui/UiHost.h implemented over one SynthyBoyClap).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What the JUCE plug-in's SynthyBoyProcessor is for the editor of the VST3 / JUCE-CLAP build, this class is for the editor inside the native module: parameters through a
// PlainParameterStore whose edits go to the audio thread as UiLink slots (handled exactly like host automation, reported to the host as gesture + value events), notes and wheels as raw MIDI
// through the same link, the live displays from the link, the content library and the files through the host callbacks of the bitmap GUI extension. Main thread only.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "PresetBrowser.h"
#include "bitmap_gui_ext.h"
#include "synthyboy_plugin.h"
#include "ui/UiHost.h"

namespace synthyboy::bitmap
{
class NativeUiHost final : public ui::UiHost, private juce::MidiKeyboardStateListener
{
public:
    NativeUiHost (SynthyBoyClap& plugin, const safian_host_bitmap_gui_t* hostExtension);
    ~NativeUiHost() override;

    /** Called on every tick: the changes the host / the audio thread made to parameters become changes of the store (the controls refresh). */
    void pullChangesFromHost();
    /** Called with the answer of a request_file / share_file of this host (the extension's request_done). */
    void requestDone (uint32_t requestId, uint32_t result, const char* path);
    /** The preset the browser holds, for diagnostics and the tests. */
    PresetBrowser& presets() noexcept { return presetBrowser; }

    // ---- ui::UiHost ----
    ui::ParameterStore& parameterStore() override { return store; }
    EngineCapsMask engineCaps() const noexcept override { return plugin.engine ? plugin.engine->capabilities() : kCapNone; }
    EngineCaps2Mask engineCaps2() const noexcept override { return plugin.engine ? plugin.engine->capabilities2() : kCap2None; }
    Tier qualityTier() const noexcept override { return kDefaultTier; }
    bool dragScrollsPanels() const noexcept override { return true; } // one finger arrives as a mouse (BitmapSession::fingerEvent)
    juce::MidiKeyboardState& keyboardState() override { return keyboard; }
    ui::AbCompare& abCompare() override { return ab; }
    void stepPreset (int direction) override;
    juce::String presetName() const override { return juce::String::fromUTF8 (presetBrowser.currentName().c_str()); }

    bool canMidiLearn() const noexcept override { return true; }
    void startMidiLearn (const std::string& key) override;
    void cancelMidiLearn() override { plugin.midiMap.disarm(); }
    std::string midiLearnKey() const override;
    int midiCcForKey (const std::string& key) const override;
    void clearMidiCc (const std::string& key) override;
    uint32_t midiMapRevision() const noexcept override { return plugin.midiMap.revision(); }

    bool saveAsDefault() override { return saveUserDefault (plugin.userDir, plugin.stateText()); }
    bool hasUserDefault() const override { return synthyboy::hasUserDefault (plugin.userDir); }
    bool clearUserDefault() override { return synthyboy::clearUserDefault (plugin.userDir); }
    bool initPatch() override;
    bool canLock() const noexcept override { return true; }
    bool isModuleLocked (const std::string& prefix) const override { return isLocked (plugin.locks, prefix); }
    void setModuleLocked (const std::string& prefix, bool locked) override;
    bool loadPatch (const ParamValues& values, const StateContent& newContent, const ui::MidiMapBindings* presetMidiMap = nullptr) override;

    const Preferences& preferences() const noexcept override { return plugin.prefs; }
    void setPreferences (const Preferences& prefs) override;
    uint32_t preferencesRevision() const noexcept override { return plugin.prefsRev.load (std::memory_order_relaxed); }
    float modWheel() const noexcept override { return plugin.modWheelPosition.load (std::memory_order_relaxed); }
    juce::String aboutText() const override;
    ui::MidiMapBindings midiMapBindings() const override { return plugin.midiMap.bindings(); }
    int setMidiMapBindings (const ui::MidiMapBindings& bindings) override;
    bool hasDefaultMidiMap() const override { return synthyboy::hasDefaultMidiMap (plugin.userDir); }
    bool saveDefaultMidiMap() override { return synthyboy::saveDefaultMidiMap (plugin.userDir, plugin.midiMap.bindings()); }
    bool clearDefaultMidiMap() override { return synthyboy::clearDefaultMidiMap (plugin.userDir); }

    float outputPeakL() const noexcept override { return plugin.ui.peakL.load (std::memory_order_relaxed); }
    float outputPeakR() const noexcept override { return plugin.ui.peakR.load (std::memory_order_relaxed); }

    StateContent content() const override { return plugin.content; }
    bool setContent (const StateContent& newContent) override { return plugin.setContentFromGui (newContent); }
    void reloadContent() override;

    UserContentStore* contentLibrary() override { return &plugin.contentStore; }
    UserLibrary* userLibrary() override { return plugin.itemLibrary.get(); }
    std::string userDirectory() const override { return plugin.userDir; }
    bool requestFile (const ui::FileRequest& request, std::function<void (const juce::File&)> done) override;
    bool canRequestFile() const noexcept override { return hostExt != nullptr && hostExt->request_file != nullptr; }
    uint32_t contentRevision() const noexcept override { return plugin.contentRev.load (std::memory_order_relaxed); }

    bool revertToSaved() override;
    int activeVoiceCount() const noexcept override { return plugin.ui.voiceCount.load (std::memory_order_relaxed); }
    float cpuLoad() const noexcept override { return plugin.ui.cpu.load (std::memory_order_relaxed); }
    int copyScope (float* dest, int maxSamples) const override { return plugin.ui.copyScope (dest, maxSamples); }
    void setReadoutTargets (const std::vector<int>& paramIndices) override;
    bool copyReadout (LiveValues& out) const override { return plugin.engine != nullptr && plugin.engine->copyReadout (out); }

    bool renderOffline (const OfflineRequest& request, OfflineAudio& out) override;
    LastNote lastNote() const override;
    double tempoBpm() const noexcept override { return plugin.tempo.load (std::memory_order_relaxed); }
    bool shareFile (const juce::File& file, const juce::String& title, std::function<void (bool)> done) override;
    bool canShareFiles() const noexcept override { return hostExt != nullptr && hostExt->share_file != nullptr; }
    void injectMidiMessage (const juce::MidiMessage& message) override;
    bool openUrl (const juce::String& url) override { return canOpenUrl() && hostExt->open_url (plugin.host, url.toRawUTF8()); }
    bool canOpenUrl() const noexcept override { return hostExt != nullptr && hostExt->open_url != nullptr; }
    juce::String aboutUrl() const override { return "https://github.com/Safian/SynthyBoy"; } // the plug-in's own url (the descriptor of the shell)

private:
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void applyMapOfPresetChange (const ui::MidiMapBindings* presetMidiMap);
    uint32_t newRequestId() noexcept { return ++lastRequestId; }

    SynthyBoyClap& plugin;
    const safian_host_bitmap_gui_t* hostExt;
    ui::PlainParameterStore store;
    juce::MidiKeyboardState keyboard;
    ui::AbCompare ab;
    PresetBrowser presetBrowser;

    uint32_t lastRequestId = 0;
    std::map<uint32_t, std::function<void (const juce::File&)>> fileRequests;
    std::map<uint32_t, std::function<void (bool)>> shareRequests;
};
} // namespace synthyboy::bitmap
