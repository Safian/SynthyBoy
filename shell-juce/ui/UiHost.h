// SynthyBoy - UI: what the editor needs from its processor (and nothing more).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The UI never touches the engine or core internals: it reads and writes parameters through a ParameterStore, asks for the engine's capability
// bits (dimming), plays notes through a MidiKeyboardState and calls the preset callbacks. SynthyBoyProcessor implements this
// interface (parameters: ApvtsParameterStore); the Kick app implements it over its CLAP plug-in instance (PlainParameterStore); the tests
// implement it with a stub. The contract for implementers is in shell-juce/ui/README.md.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h> // dragFileOut () names juce::Component

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ParameterStore.h"
#include "model/ModModel.h"
#include "synthyboy/content_state.h"
#include "synthyboy/offline_render.h"
#include "synthyboy/engine_caps.h"
#include "synthyboy/preferences.h"
#include "synthyboy/quality.h"
#include "synthyboy/readout.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/user_library.h"

namespace synthyboy::ui
{
struct PresetItem
{
    int index = 0;
    std::string category;
    std::string name;
};

/** A MIDI CC map as the shell keeps it: (CC number, parameter key), ascending by CC. */
using MidiMapBindings = std::vector<std::pair<int, std::string>>;

/** A request for a file the host asks the user for (WP-01): the JUCE shell opens a juce::FileChooser, a test host answers at once. */
struct FileRequest
{
    enum class Mode { open, save };
    Mode mode = Mode::open;
    juce::String title;
    juce::String wildcards;      // "*.wav;*.flac"
    juce::String suggestedName;  // save: the proposed file name
    juce::String purpose;        // "sample", "wavetable", "impulse", "vital", "lfoshape", "export": the host remembers the last folder per purpose
};

class UiHost
{
public:
    virtual ~UiHost() = default;

    /** Parameter access (message thread), by index into paramTable(). The store outlives the editor. */
    virtual ParameterStore& parameterStore() = 0;
    /** IEngine::capabilities () of the engine behind the processor; constant for the life of the processor. */
    virtual EngineCapsMask engineCaps() const noexcept = 0;
    /** The quality tier the host runs the engine with (core/include/synthyboy/quality.h): the effect displays of a tier that masks the effect say so.
        A host that does not override it runs the full engine. */
    virtual Tier qualityTier() const noexcept { return Tier::high; }
    /** True when pop-up menus and call-outs open INSIDE the editor Component (OverlayHost.h): the choice for a host without native windows (the software framebuffer
        of the Akai MPC, an embedded panel). The JUCE plug-in answers false: its menus are desktop level windows that may reach over the edge of the plug-in window. */
    virtual bool overlaysInsideEditor() const noexcept { return true; }
    /** True when one finger on a touch screen reaches the editor as a plain mouse (the bitmap GUI host of the Akai MPC: no touch input source of the toolkit): the scrolling
        areas then scroll on a drag that starts on anything that does not use the drag itself (a card, a title, a gap; a control marks itself with
        Component::setViewportIgnoreDragFlag, see UiContext.h scrollOnDrag ()). The JUCE plug-in answers false: a mouse drag on a background must not scroll, and a touch
        screen of the operating system has real touch sources. */
    virtual bool dragScrollsPanels() const noexcept { return false; }
    /** IEngine::capabilities2 () (GROUPC: granular, spectral, multisample); a host that does not override it reports every bit. */
    virtual EngineCaps2Mask engineCaps2() const noexcept { return kCap2AllKnown; }
    /** Notes played on the mini keyboard; the processor merges them into the MIDI input of the audio callback. */
    virtual juce::MidiKeyboardState& keyboardState() = 0;
    /** The two A/B snapshots live with the processor so they survive closing the editor. */
    virtual AbCompare& abCompare() = 0;

    /** Preset browsing: stepPreset steps by direction (-1 or +1), presetName returns the active name. */
    virtual void stepPreset (int direction) = 0;
    virtual juce::String presetName() const = 0;
    /** Available presets provided by the shell (index 0 is always "Init", followed by factory presets). */
    virtual std::vector<PresetItem> presetList() const { return {}; }
    /** Index (in presetList) of the active preset, -1 if the shell has no list. */
    virtual int currentPresetIndex() const { return -1; }
    /** Select a preset by index (0 .. presetList().size() - 1). */
    virtual void selectPreset (int /*index*/) {}
    /** True when the active patch differs from the loaded/saved preset. */
    virtual bool isPresetModified() const { return false; }

    /** MIDI learn (G07, docs/ui-design/15-shell-features.md): the shell keeps a CC -> parameter table (any channel) and saves it in the state.
        startMidiLearn arms one key: the next learnable CC is mapped to it (the CC does not move the knob that first time). Message thread. */
    virtual bool canMidiLearn() const noexcept { return false; }
    virtual void startMidiLearn (const std::string& /*key*/) {}
    virtual void cancelMidiLearn() {}
    /** The key that waits for a CC, "" when learn mode is off. */
    virtual std::string midiLearnKey() const { return {}; }
    /** The CC mapped to `key`, -1 if none. */
    virtual int midiCcForKey (const std::string& /*key*/) const { return -1; }
    virtual void clearMidiCc (const std::string& /*key*/) {}
    /** Changes with every change of the table or of the learn state (the editor timer polls it to redraw the knobs). */
    virtual uint32_t midiMapRevision() const noexcept { return 0; }

    /** Save as Default (G02): the current patch (parameters, content, MIDI map, locks) becomes the init patch of every new instance, stored in
        the user data directory. hasUserDefault / clearUserDefault: the slot; initPatch loads it (or the factory init patch when the slot is
        empty), the locked modules are kept. */
    virtual bool saveAsDefault() { return false; }
    virtual bool hasUserDefault() const { return false; }
    virtual bool clearUserDefault() { return false; }
    virtual bool initPatch() { return false; }

    /** Locks (G08): a locked module ("osc1", "filter2", "lfo3", "env1" ...) keeps its parameters and content when a preset is loaded
        (loadPatch, initPatch, a state without a shell section). Saved with the state. */
    virtual bool canLock() const noexcept { return false; }
    virtual bool isModuleLocked (const std::string& /*prefix*/) const { return false; }
    virtual void setModuleLocked (const std::string& /*prefix*/, bool /*locked*/) {}
    /** Loads a patch as a preset change: parameters and content, except what the locks keep. False when the host cannot. */
    virtual bool loadPatch (const ParamValues& /*values*/, const StateContent& /*content*/, const MidiMapBindings* /*presetMidiMap*/ = nullptr) { return false; }

    // ---- preferences, MIDI map files, global locks (GROUPA2 WP-02, docs/ui-design/13-new-keys.md 4.31) ----
    /** The global preferences (core/include/synthyboy/preferences.h), stored per user in <userDirectory>/preferences.json. A reference to the host's copy; message thread.
        A host without a store answers the defaults and ignores setPreferences (). */
    virtual const Preferences& preferences() const noexcept { static const Preferences defaults; return defaults; }
    /** Replaces the preferences, saves the file and applies the ones the shell acts on. */
    virtual void setPreferences (const Preferences& /*prefs*/) {}
    /** Bumped by every setPreferences () (the editor timer polls it to redraw). */
    virtual uint32_t preferencesRevision() const noexcept { return 0; }
    /** The position of the mod wheel (0..1) as the last MIDI message set it (any channel; the on-screen wheel included). */
    virtual float modWheel() const noexcept { return 0.0f; }
    /** One line for Preferences / About: version, build date, engine, content (S p.322). */
    virtual juce::String aboutText() const { return {}; }
    /** The MIDI CC map in full, and its replacement (S p.325: Load / Save MIDI Map). setMidiMapBindings () returns the number of entries taken, -1 if the host keeps none. */
    virtual MidiMapBindings midiMapBindings() const { return {}; }
    virtual int setMidiMapBindings (const MidiMapBindings& /*bindings*/) { return -1; }
    /** The default MIDI CC map (S p.325, Default.SerumMIDIMap): loaded in a new instance, by Init Preset and by a preset change. */
    virtual bool hasDefaultMidiMap() const { return false; }
    virtual bool saveDefaultMidiMap() { return false; }
    virtual bool clearDefaultMidiMap() { return false; }

    /** Live output meter peaks (linear amplitude, e.g. 0..1 or higher if clipping) from the audio callback. */
    virtual float outputPeakL() const noexcept { return 0.0f; }
    virtual float outputPeakR() const noexcept { return 0.0f; }

    /** The `content` section of the patch (custom LFO shapes, ...; not parameters). A copy, message thread. */
    virtual StateContent content() const { return {}; }
    /** Replaces the content, hands it to the engine and marks the host state dirty. Returns false (nothing changed) if the content
        fails validateContent () or the host keeps no content. Message thread. */
    virtual bool setContent (const StateContent& /*newContent*/) { return false; }
    /** The engine reads the files of the current content again (a file that was missing when the content was set has been added to the library:
        setContent () with an equal content does nothing). Message thread. */
    virtual void reloadContent() {}

    // ---- content pipeline (WP-01, docs/ui-design/13-new-keys.md 4.25) ----
    /** The user content library behind the engine (wavetables, samples, impulse responses on disk; the engine resolves the references of `content`
        through it). Null when the host keeps none. Message thread. */
    virtual UserContentStore* contentLibrary() { return nullptr; }
    /** The library of small JSON items (LFO shapes, formulas, FX racks, voice control presets). Null when the host keeps none. */
    virtual UserLibrary* userLibrary() { return nullptr; }
    /** The user data directory of the host ("" when unknown); the Vital importer writes the presets of a bank below it. */
    virtual std::string userDirectory() const { return {}; }
    /** Asks the user for a file; `done` is called on the message thread with the chosen file, never with a file that does not exist (open), and not at
        all when the user cancels. Returns false (and never calls `done`) when the host cannot ask. */
    virtual bool requestFile (const FileRequest& /*request*/, std::function<void (const juce::File&)> /*done*/) { return false; }
    /** True when requestFile () can ask (the menus dim their file entries otherwise). */
    virtual bool canRequestFile() const noexcept { return false; }
    /** Bumped whenever a whole document is loaded (setStateInformation, loadPatch, initPatch, revert): the editor looks for missing files then. */
    virtual uint32_t contentRevision() const noexcept { return 0; }

    /** "Revert to saved" (design 07): the parameters and content go back to what the host last loaded (setStateInformation, or the defaults of a
        new instance). False when the host keeps no such snapshot. Message thread. */
    virtual bool revertToSaved() { return false; }

    /** Sounding voices (released ones that still fade out included) after the last audio block: IEngine::activeVoices (). 0 without an engine. */
    virtual int activeVoiceCount() const noexcept { return 0; }
    /** Load of the audio callback: the time process () needed for the last blocks as a fraction of the time the blocks last (smoothed;
        0 = idle, 1 = at the limit). 0 when the host does not measure. */
    virtual float cpuLoad() const noexcept { return 0.0f; }
    /** Copies the latest `maxSamples` (up to kScopeSamples) mono samples of the master output, oldest first, into `dest`; returns how many were
        copied (0 before the first block). Message thread; lock free on the audio side. */
    virtual int copyScope (float* /*dest*/, int /*maxSamples*/) const { return 0; }
    static constexpr int kScopeSamples = 4096;

    /** The live values of the engine for the displays (GROUPA2 WP-17, readout.h): the knobs whose modulated value is wanted (table indices) and the copy of the
        values of the last blocks. A host without an engine answers false and the displays show the stored values. Message thread; the engine side is lock free. */
    virtual void setReadoutTargets (const std::vector<int>& /*paramIndices*/) {}
    virtual bool copyReadout (LiveValues& /*out*/) const { return false; }

    // ---- offline render, share / drag-out of the last note (GROUPD WP-15, 13-new-keys.md 4.40) ----
    /** Renders `request` on a private copy of the engine (IEngine::renderOffline, offline_render.h): the live engine is not touched. The host fills `request.values` with the values of its
        parameter tree when the caller left it null (the engine only has them after the next block). False when the host has no engine that renders offline. Message thread. */
    virtual bool renderOffline (const OfflineRequest& /*request*/, OfflineAudio& /*out*/) { return false; }
    /** The last note that was played (keyboard, MIDI input, the on-screen keys): key, velocity, how long it was held (still held: so far). `valid` is false before the first note. */
    struct LastNote
    {
        bool valid = false;
        int key = 60;
        float velocity = 0.8f;
        double heldSeconds = 1.0;
    };
    virtual LastNote lastNote() const { return {}; }
    /** The tempo of the host transport (Resample plays one bar at this tempo); 120 without a host tempo. */
    virtual double tempoBpm() const noexcept { return 120.0; }
    /** The share hook: hands an audio file to the platform's share sheet (iOS / Android: juce::ContentSharer; desktop: the file is shown in the file manager). `done` gets the success.
        False when the host has no way to share. */
    virtual bool shareFile (const juce::File& /*file*/, const juce::String& /*title*/, std::function<void (bool)> /*done*/ = {}) { return false; }
    virtual bool canShareFiles() const noexcept { return false; }
    /** Drag-out of a file to the desktop (the "Last note as audio" chip): starts an OS drag of `file` from `source` (desktop: juce::DragAndDropContainer::performExternalDragDropOfFiles).
        False when the host cannot (the sheet then points to Share and Save as WAV). Like requestFile / shareFile, one of the few ways out to the OS. */
    virtual bool dragFileOut (const juce::File& /*file*/, juce::Component& /*source*/) { return false; }

    /** Opens a link in the system's browser (a host without windows asks the platform through its own host: the bitmap GUI extension's open_url). `aboutUrl ()` is the project page the About
        line of the Preferences links to ("" = no link). False / "" when the host cannot. */
    virtual bool openUrl (const juce::String& /*url*/) { return false; }
    virtual bool canOpenUrl() const noexcept { return false; }
    virtual juce::String aboutUrl() const { return {}; }

    /** Injects a MIDI message from the UI (pitch bend, mod wheel, CC 64 sustain, etc.) into the audio callback. */
    virtual void injectMidiMessage (const juce::MidiMessage& /*msg*/) {}
};
} // namespace synthyboy::ui
