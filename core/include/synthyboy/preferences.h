// SynthyBoy - core: the global preferences (Serum 2 manual p.313 - 315, GROUPA2 WP-02) and the MIDI CC map files. No JUCE, no engine, no parameter keys.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The preferences are per user, not per patch: they live in <user dir>/preferences.json and are shared by the plug-in formats. The UI reads them through
// UiHost::preferences (); the shells read the few that act on the audio side (silence on stop, ultra render, MPE defaults, the default MIDI map).
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace synthyboy
{
struct Preferences
{
    // ---- user interface (S p.313 - 314) ----
    bool valueBubbles = true;           // "Param value tooltips": the bubble that shows the number while a control is dragged
    bool doubleClickResets = true;      // "Double-click params": reset to the default (true) or open the value box (false)
    bool mouseWheel = true;             // "Mouse wheel param control": the wheel turns the control under the pointer
    bool keyboardShortcuts = true;      // "Keyboard shortcuts": respond to the computer keyboard (Appendix G)
    bool waveform3d = false;            // "Default waveform view": 2D (false) or 3D (true) in the oscillator panels
    bool freqSemitones = false;         // frequency units (Vital p.42): cutoff readouts in semitones from middle C instead of Hz
    // ---- MPE (S p.314): the defaults of a new instance ----
    bool mpeByDefault = false;          // "MPE enabled by default": master.mpe.on of a new instance
    bool mpeBendToX = false;            // "MPE Pitch Bend (also) maps to Expr X": master.mpe.bend_to_x of a new instance
    bool mpeYBipolar = false;           // "MPE Expr Y acts bi-directional": master.mpe.y_bipolar of a new instance
    // ---- general (S p.314 - 315) ----
    bool limitModDepth = false;         // "Limit Mod depth on drop (based on knob value)"
    bool modWheelToWtPos = false;       // "Mod Wheel -> WT Pos (when WT Editor is open)"
    bool silenceOnStop = true;          // "Silence note + FX tails when host transport stops"
    bool loadMidiMapFromPresets = false; // "Load MIDI Map from Presets": a preset brings its own CC map instead of the default one
    bool ultraRender = false;           // "Use Ultra quality when rendering": a host bounce (offline) runs at 4x oversampling

    bool operator== (const Preferences& o) const noexcept;
    bool operator!= (const Preferences& o) const noexcept { return ! (*this == o); }
};

/** One line per preference: the key in the file and the field (for the settings panel and the tests). */
struct PreferenceInfo
{
    const char* key;
    const char* label;
    bool Preferences::* field;
};
const PreferenceInfo* preferenceTable() noexcept;
size_t preferenceCount() noexcept;

/** {"format":"synthyboy-preferences","version":1,"valueBubbles":true,...} (deterministic). */
std::string serializePreferences (const Preferences& prefs);
/** Reads the text over `prefs` (members that are not there or are not booleans stay as they are; unknown members are ignored). False when the text is
    not a preferences document (nothing changed). Bounded, never throws. */
bool parsePreferences (const char* data, size_t size, Preferences& prefs) noexcept;

/** <dir>/preferences.json */
std::string preferencesPath (const std::string& dir);
/** The file, or the defaults when there is none / it is damaged. */
Preferences loadPreferences (const std::string& dir);
/** Writes the file atomically (temp + rename). False on any failure. */
bool savePreferences (const std::string& dir, const Preferences& prefs);

// ---- MIDI CC map files (S p.325, GROUPA2 WP-02) ----------------------------------------------------------------------------------------

/** {"format":"synthyboy-midi-map","version":1,"midi_cc":[{"cc":21,"key":"filter1.cutoff"}]} */
std::string serializeMidiMap (const std::vector<std::pair<int, std::string>>& bindings);
/** Reads a map file of ours (a Serum 2 `.SerumMIDIMap` is CBOR: the Serum importer reads it). Entries with an unknown key or a CC that cannot be learned are
    skipped. Returns the number of entries taken, -1 when the text is not a map document (`bindings` unchanged). */
int parseMidiMap (const char* data, size_t size, std::vector<std::pair<int, std::string>>& bindings) noexcept;

/** The default MIDI CC map (S p.325, "Default.SerumMIDIMap"): <dir>/midimap/Default.sbmidimap */
std::string defaultMidiMapPath (const std::string& dir);
bool hasDefaultMidiMap (const std::string& dir);
bool saveDefaultMidiMap (const std::string& dir, const std::vector<std::pair<int, std::string>>& bindings);
bool loadDefaultMidiMap (const std::string& dir, std::vector<std::pair<int, std::string>>& bindings);
bool clearDefaultMidiMap (const std::string& dir);

/** The key a Serum 2 parameter id (`module * 1,000,000 + index * 1,000 + parameter`) of a MIDI CC map stands for: only the macros (module 7, "Macros CC14-21") are known, "" for
    every other id. */
std::string serumMidiParamKey (long long paramId);

// ---- the transport (S p.315) ----------------------------------------------------------------------------------------------------------

/** Detects the moment the host transport stops. A host that gives no transport information at all never stops (`known` false). Audio thread safe (no state beyond two bools). */
class TransportStopWatch
{
public:
    /** Returns true exactly once per running -> stopped change. */
    bool update (bool known, bool playing) noexcept
    {
        const bool stopped = known && ! playing && wasPlaying;
        wasPlaying = known && playing;
        return stopped;
    }
    void reset() noexcept { wasPlaying = false; }

private:
    bool wasPlaying = false;
};
} // namespace synthyboy
