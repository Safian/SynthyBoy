// SynthyBoy - core: shell-side features that are not parameters (docs/ui-design/15-shell-features.md): MIDI learn (G07), module / parameter
// locks (G08) and the user default patch "Save as Default" (G02). No JUCE, no engine, no new parameter keys: the shells (shell-juce, shell-clap)
// own one MidiCcMap and one lock list each, store them in the additive `shell` section of the host state and call the helpers below.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "content_state.h"
#include "state.h"

namespace synthyboy
{
// ---- MIDI learn (G07) -----------------------------------------------------------------------------------------------------------------

/** CC number -> parameter table index. One parameter per CC and one CC per parameter, any channel (omni; A183). The audio thread reads and
    learns (handleCc: atomics only, no allocation, no lock); the message thread arms, assigns and removes. */
class MidiCcMap
{
public:
    static constexpr int kNumCc = 128;

    MidiCcMap() noexcept { clearAll(); }

    /** CCs that can never be learned: bank select (0, 32), sustain (64), the RPN / NRPN data bytes (6, 38, 96-101) and the channel mode
        messages (120-127). CC 1 and CC 74 can be learned: the engine keeps its own use of them (mod wheel / slide) as well. */
    static bool isLearnableCc (int cc) noexcept;

    /** Table index mapped to `cc`, or -1. */
    int paramForCc (int cc) const noexcept;
    /** The CC mapped to the parameter at table index `paramIndex`, or -1. */
    int ccForParam (int paramIndex) const noexcept;
    int numMappings() const noexcept;

    /** Maps `cc` to the parameter (moves an older mapping of either side away). False for a CC that is not learnable or an index outside the table. */
    bool assign (int cc, int paramIndex) noexcept;
    bool assignKey (int cc, const char* key) noexcept;
    void clearCc (int cc) noexcept;
    void clearParam (int paramIndex) noexcept;
    void clearAll() noexcept;

    /** Learn mode: the next learnable CC that arrives is mapped to this parameter. -1 (disarm) / an index outside the table disarms. */
    void armLearn (int paramIndex) noexcept;
    void disarm() noexcept { armed.store (-1, std::memory_order_relaxed); }
    int armedParam() const noexcept { return armed.load (std::memory_order_relaxed); }

    /** Bumped on every change of the table or of the learn state (the UI polls it to redraw). */
    uint32_t revision() const noexcept { return rev.load (std::memory_order_relaxed); }

    struct Hit
    {
        int paramIndex = -1;  // -1: nothing to do
        float plain = 0.0f;   // the parameter value for the CC value (valid when apply)
        bool apply = false;   // set the parameter
        bool learned = false; // this CC was just mapped (learn mode); apply is false: the knob stays where it is
    };
    /** Handles one control change (value 0..127). Audio thread safe. */
    Hit handleCc (int cc, int value) noexcept;

    /** All mappings as (cc, parameter key), ascending by cc. */
    std::vector<std::pair<int, std::string>> bindings() const;
    /** Replaces the table; entries with an unknown key or a CC that is not learnable are skipped. Returns the number taken. */
    int setBindings (const std::vector<std::pair<int, std::string>>& list);

private:
    std::atomic<int16_t> table[kNumCc];
    std::atomic<int> armed { -1 };
    std::atomic<uint32_t> rev { 0 };
};

// ---- locks (G08) ----------------------------------------------------------------------------------------------------------------------

constexpr size_t kMaxLocks = 64;
constexpr const char* kMatrixLock = "matrix";          // Lock Matrix
constexpr const char* kQualityLock = "quality";        // lock the quality settings
constexpr const char* kTuningLock = "master.tuning";   // lock the tuning configuration
constexpr size_t kMaxLockNameBytes = 48;

/** A lock names a module prefix ("osc1", "filter2", "lfo3", "env1", "fx": every key "<name>.*") or one parameter key ("master.volume"). Three locks of the
    global settings are names too (GROUPA2 WP-02): "matrix" (Lock Matrix: the 64 slots `modNN.*` and the per-connection curves, S p.214), "quality" (S p.319)
    and "master.tuning" (the tuning keys and the tuning table, S p.321) - the last two are ordinary prefixes. */
bool isValidLockName (const std::string& name) noexcept;
/** True if the parameter `key` is covered by the lock entry `lock` (key == lock, or key starts with "<lock>."). */
bool lockCoversKey (const std::string& lock, const char* key) noexcept;
bool isLocked (const std::vector<std::string>& locks, const std::string& name) noexcept;
/** Adds / removes an entry (ascending order, no duplicates, at most kMaxLocks). False if the name is invalid or the list is full. */
bool setLocked (std::vector<std::string>& locks, const std::string& name, bool locked);

/** A preset change must not overwrite a locked module: copies the values of every locked key from `current` into `patch`. Returns how many
    values were restored (differences only). */
int keepLockedValues (ParamValues& patch, const ParamValues& current, const std::vector<std::string>& locks) noexcept;
/** The same for the content section: the wavetable / sample / remap of a locked "oscN", the shape of a locked "lfoN", the curve of a locked
    "filterN". Returns true if `patch` changed. */
bool keepLockedContent (StateContent& patch, const StateContent& current, const std::vector<std::string>& locks);

// ---- the additive `shell` section of the state ---------------------------------------------------------------------------------------

/** What the shells store next to the parameters: {"shell":{"midi_cc":[{"cc":21,"key":"filter1.cutoff"}],"locks":["osc1"]}}. */
struct ShellState
{
    std::vector<std::pair<int, std::string>> midiCc;
    std::vector<std::string> locks;

    bool empty() const noexcept { return midiCc.empty() && locks.empty(); }
    bool operator== (const ShellState& o) const { return midiCc == o.midiCc && locks == o.locks; }
};

enum class ShellStatus : uint8_t
{
    absent,   // the document has no shell section (a preset, an older state): the output is left untouched
    ok,       // parsed
    rejected, // present but outside the caps: dropped as a whole (output cleared)
    invalid   // the document is not parseable JSON up to the section: output untouched
};

/** The text of the section value: {"midi_cc":[...],"locks":[...]} (deterministic, no white space). */
std::string saveShellSection (const ShellState& shell);
/** `stateDocument` (saveState output, ends with '}') with ,"shell":{...} appended. Unchanged if it does not end with '}'. The section is always written
    by the shells (even empty), so "no shell section" means "this document is a preset, not a session". */
std::string withShellSection (const std::string& stateDocument, const ShellState& shell);
/** Looks for the top-level "shell" member of a state / preset document. Bounded, never throws, never allocates beyond the output. */
ShellStatus loadShellSection (const void* data, size_t size, ShellState& out) noexcept;

// ---- Save as Default (G02) ------------------------------------------------------------------------------------------------------------

/** The directory of the user data: $SYNTHYBOY_USER_DIR if set (tests), else Application Support / %APPDATA% / $XDG_DATA_HOME or ~/.local/share
    + "/SynthyBoy". Not created here. Empty if no home directory can be found. */
std::string userDataDirectory();
/** <dir>/default.sbstate: a state document (saveState with content). */
std::string userDefaultPath (const std::string& dir);
/** Writes the document atomically (temp file + rename), creating the directory. False on any failure (the old default stays). */
bool saveUserDefault (const std::string& dir, const std::string& stateDocument);
bool hasUserDefault (const std::string& dir);
/** Loads the user default into values / content (and, when `shell` is given, its shell section: cleared if the file has none). False if
    there is none or it does not load (everything untouched then). */
bool loadUserDefault (const std::string& dir, ParamValues& values, StateContent& content, ShellState* shell = nullptr);
/** Removes the user default (back to the factory init patch). True if there is none afterwards. */
bool clearUserDefault (const std::string& dir);
} // namespace synthyboy
