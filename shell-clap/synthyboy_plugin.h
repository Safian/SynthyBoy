// SynthyBoy - native CLAP shell: the state of one plug-in instance (shared by synthyboy_clap.cpp and the bitmap GUI in shell-clap/bitmap/; no JUCE in this header).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <clap/clap.h>

#include "synthyboy/engine.h"
#include "synthyboy/events.h"
#include "synthyboy/params.h"
#include "synthyboy/preferences.h"
#include "synthyboy/quality.h"
#include "synthyboy/shell_features.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/user_library.h"
#include "synthyboy_content_service.h"
#include "synthyboy_ui_link.h"

namespace synthyboy
{
/** What the bitmap GUI of an instance is, for the shell: an object it deletes with the plug-in (shell-clap/bitmap/). */
class BitmapGuiHandle
{
public:
    virtual ~BitmapGuiHandle() = default;
};

/** One instance of the native shell. `values` is the working copy of the MAIN thread (state load, the content extension, the GUI); the audio thread never touches it: parameter
    changes of the host go to the engine and to the mirror in `ui` (UiLink), which is the one place every thread reads the current values from. */
struct SynthyBoyClap
{
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    std::unique_ptr<IEngine> engine;
    std::string userDir = userDataDirectory();
    UserContentStore contentStore { userDir }; // the user content library (WP-01): the engine resolves the references of `content` through it
    ParamValues values;
    StateContent content; // the `content` section of the state (not parameters): custom LFO shapes, curves and the tuning go to the engine (setContent), main thread only
    MidiCcMap midiMap;                // MIDI learn table (G07): CC -> parameter, read on the audio thread; saved in the `shell` section
    std::vector<std::string> locks;   // module locks (G08), main thread; kept for the round trip with the editor shell
    Preferences prefs;                // the global preferences (GROUPA2 WP-02): read once when the plug-in is created (the editor shell writes the file)
    TransportStopWatch stopWatch;     // audio thread: the host transport stops -> silence () when prefs.silenceOnStop
    bool offlineRender = false;       // clap_plugin_render: the host renders offline
    // the last note (audio thread writes, the main thread reads: Resample, the audio of the last note) and the host tempo
    std::atomic<int> lastKey { -1 };
    std::atomic<float> lastVelocity { 0.8f };
    std::atomic<int64_t> lastOnSample { 0 }, lastOffSample { -1 }, samplesDone { 0 };
    std::atomic<double> tempo { 120.0 };
    Quality quality;
    std::vector<Event> events;
    double sampleRate = 48000.0;
    uint32_t maxBlockSize = 512;
    bool active = false;
    std::atomic<int> reportedLatency { 0 };   // the latency the host was told (ext_latency_get)
    bool processing = false;
    std::unique_ptr<ContentHost> contentHost; // the view of this plug-in for the extension synthyboy.content (synthyboy_content_service.h)

    // ---- the link to a GUI (docs/BITMAP-GUI-EXTENSION.md), always present, free of cost without a GUI ----
    UiLink ui;
    std::unique_ptr<UserLibrary> itemLibrary;       // the library of small JSON items (LFO shapes, formulas, FX racks), created with the plug-in
    ParamValues savedValues;                        // what the host loaded last (or the defaults): "Revert to saved"
    StateContent savedContent;
    std::atomic<uint32_t> contentRev { 0 };         // a whole document was loaded
    std::atomic<uint32_t> prefsRev { 0 };
    std::atomic<bool> prefSilenceOnStop { true };
    std::atomic<float> modWheelPosition { 0.0f };
    std::unique_ptr<BitmapGuiHandle> bitmapGui;     // the GUI of this instance, when the host created one

    explicit SynthyBoyClap (const clap_host_t* h);
    ~SynthyBoyClap();

    SynthyBoyClap (const SynthyBoyClap&) = delete;
    SynthyBoyClap& operator= (const SynthyBoyClap&) = delete;

    /** The default MIDI CC map file (S p.325): when `force`, it replaces the table; else only an empty table takes it (a new instance). */
    void applyDefaultMidiMap (bool force);

    // ---- audio thread ----
    /** The edits the GUI posted (UiLink): handled exactly like host automation (values, engine) and reported to the host as gesture + value events in `out` (null: not reported). */
    void drainUiEdits (const clap_output_events_t* out, uint32_t time) noexcept;
    /** One MIDI message (note on / off, CC, bend ...) of the host or of the GUI. */
    void handleMidi (const uint8_t* data, uint32_t time, const clap_output_events_t* out) noexcept;
    /** A parameter value from the host or the MIDI map: values, engine and the mirror of the GUI. */
    void applyHostValue (int index, float constrained) noexcept;

    // ---- main thread ----
    /** All parameters as a ParamValues (from the mirror: safe while the plug-in processes). */
    ParamValues currentValues() const;
    /** The mirror and the GUI get every value of `all` (and `values` and the engine are set): a state load, a patch, a value reset. */
    void setAllValues (const ParamValues& all);
    /** A whole patch (a preset, the init patch, a state): values + content, with the locked modules kept when `respectLocks`. No host notification (the host is loading a state itself). */
    void installPatch (ParamValues newValues, StateContent newContent, bool respectLocks);
    /** installPatch () for a change made by the GUI: tells the host to read the values again and that the state changed. */
    void applyPatch (ParamValues newValues, StateContent newContent, bool respectLocks);
    /** The content section replaced by the GUI: validated, handed to the engine, host told the state is dirty. False if it is invalid. */
    bool setContentFromGui (const StateContent& newContent);
    /** The text of the host state / the user default (values + content + shell section). */
    std::string stateText() const;
    ShellState shellState() const;
    void applyShell (const ShellState& shell);
    /** Remembers the current values + content as the state "Revert to saved" goes back to. */
    void rememberSaved();
    /** Host notifications (each tolerates a host without the extension). */
    void markStateDirty() const;
    void rescanValues() const;
    void requestFlush() const;
    void requestRestart() const;
    /** True when the host has not (yet) called start_processing: then main thread edits reach the engine at once. */
    bool audioRunning() const noexcept { return processing; }
};

inline SynthyBoyClap* toClap (const clap_plugin_t* p) noexcept
{
    return static_cast<SynthyBoyClap*> (p->plugin_data);
}
} // namespace synthyboy
