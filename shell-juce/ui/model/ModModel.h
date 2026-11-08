// SynthyBoy - UI view-model: modulation by touch (docs/ui-design/06-modulation.md 2.1) and the A/B compare (decision Q4).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pure logic, no JUCE. Modulation is stored in the modNN slots (core/include/synthyboy/modulation.h) as ordinary parameters; the
// state machine reaches them through ModMatrixAccess, which the JUCE side implements on the APVTS (with host gestures) and the
// tests implement on a plain array.
#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ControlModel.h"
#include "synthyboy/content_state.h"
#include "synthyboy/modulation.h"
#include "synthyboy/state.h"

namespace synthyboy::ui
{
/** Read / write access to parameters by table index (message thread). */
class ModMatrixAccess
{
public:
    virtual ~ModMatrixAccess() = default;
    virtual float get (int paramIndex) const = 0;
    virtual void set (int paramIndex, float plain) = 0;
    virtual void beginGesture (int paramIndex) = 0;
    virtual void endGesture (int paramIndex) = 0;
};

/** One assigned modulation: slot (0-based), source choice value (1..kNumModSources), amount -100..100 (%). */
struct ModEntry
{
    int slot = -1;
    int source = 0;
    float amount = 0.0f;
    bool bipolar = false;
    bool bypass = false;
};

/** A slot is in use when both source and destination are set (docs/ui-design/06 4). */
inline bool slotInUse (const ModMatrixAccess& m, const ModSlotParams& p)
{
    return m.get (p.source) >= 0.5f && m.get (p.destination) >= 0.5f;
}

/** All used slots grouped by destination parameter index. Cheap to rebuild (64 slots). */
class ModSnapshot
{
public:
    static ModSnapshot build (const ModMatrixAccess& access);

    const std::vector<ModEntry>& entriesFor (int destParamIndex) const;
    int slotsUsed() const noexcept { return used; }
    bool full() const noexcept { return used >= kNumModSlots; }
    /** Every assignment of a source as (destination parameter index, entry), in slot order (the source chip's list). */
    std::vector<std::pair<int, ModEntry>> entriesForSource (int sourceChoice) const;
    /** Number of destinations a source is assigned to (the count badge of a source chip). */
    int destinationCount (int sourceChoice) const;
    bool operator== (const ModSnapshot& other) const noexcept;

private:
    std::unordered_map<int, std::vector<ModEntry>> byDestination;
    std::vector<int> sourceCounts;
    int used = 0;
};

/** The assign-by-touch flow: tap a source chip (armed), drag a modulatable knob (dragging: allocates or reuses a slot and sets its
    amount), release (armed again, ready for the next knob), tap the chip again or Done (idle). */
class AssignFlow
{
public:
    enum class Phase { idle, armed, dragging };
    enum class BeginResult { ok, notArmed, notModulatable, matrixFull };

    explicit AssignFlow (ModMatrixAccess& access) : matrix (access) {}

    Phase phase() const noexcept { return state; }
    /** Armed source as a choice value (1..kNumModSources), 0 when idle. */
    int armedSource() const noexcept { return source; }
    /** Slot being edited (0-based) while dragging, else -1. */
    int activeSlot() const noexcept { return slot; }
    int activeDestination() const noexcept { return destination; }

    /** idle -> armed; the armed chip again -> idle; another chip -> armed with that source. Ignored while dragging. Returns the new phase. */
    Phase tapSource (int sourceChoice);
    /** Leave assign mode (the Done button); ends a running drag first. */
    void cancel();

    /** True if `paramIndex` is a valid target in the current phase (armed or dragging, kParamModulatable). */
    bool isTarget (int paramIndex) const;

    /** armed -> dragging. Reuses the slot that already holds (source, destination), else takes the first free slot (source or destination
        unset) and writes source, destination and polarity; amount of an existing slot is kept, a new slot starts at 0. */
    BeginResult beginDrag (int paramIndex);
    /** dragging: the amount as a normalised position (0..1 == -100..+100 %). */
    float amountNormalised() const;
    void dragBy (float dyUpPx, FineZone zone);
    void setAmount (float percent);
    /** dragging -> armed. */
    void endDrag();

    /** Polarity written to a new slot: bipolar for destinations that are centred around zero (pan, detune, ...). */
    static bool defaultBipolar (const ParamInfo& destination) noexcept;

private:
    int findSlot (int sourceChoice, int destinationChoice) const;
    int findFreeSlot() const;

    ModMatrixAccess& matrix;
    Phase state = Phase::idle;
    int source = 0;
    int slot = -1;
    int destination = -1;
};

/** Slot editing outside the touch flow (the sheet's mod list): remove an assignment (all seven slot parameters back to their defaults)
    or bypass it. `slot` is 0-based. */
void clearSlot (ModMatrixAccess& matrix, int slot);
void setSlotBypass (ModMatrixAccess& matrix, int slot, bool bypass);
/** Swaps everything the two slots hold (source, destination, amount, curve, polarity, stereo, bypass, aux source / invert / curve / output):
    "reorder the matrix" (design 06 row handle). Slots are 0-based. False for equal or out of range slots. One gesture per parameter. */
bool swapModSlots (ModMatrixAccess& matrix, int slotA, int slotB);
// ---- Point modulation of a custom LFO shape (content.lfo_shapes[].points[].mod_x / mod_y; docs/ui-design/05 row 05-22) -----------------
// Stored exactly as the data model defines: {present, source key, amount -1..1} per axis, one source per axis. There is no bypass field, so
// a bypassed modulation is "present with amount 0" (the engine adds nothing); the editor remembers the amount to restore.

enum class PointAxis { x, y };

inline ContentModRef& pointModOf (ContentPoint& p, PointAxis a) noexcept { return a == PointAxis::x ? p.modX : p.modY; }
inline const ContentModRef& pointModOf (const ContentPoint& p, PointAxis a) noexcept { return a == PointAxis::x ? p.modX : p.modY; }

/** The end points of a shape stay at x = 0 and x = 1 (the engine ignores mod_x there), so only Y can be modulated on them. */
bool canModulatePointAxis (size_t index, size_t pointCount, PointAxis axis) noexcept;
/** Source key of a choice value 1..kNumModSources (empty for anything else) and back (0 for an unknown key). */
const char* pointModSourceKey (int sourceChoice) noexcept;
int pointModSourceChoice (const std::string& key) noexcept;
/** Sets the modulation of one axis (replacing whatever source it had); the amount is clamped to -1..1. False for a bad source or axis. */
bool setPointMod (ContentPoint& p, size_t index, size_t pointCount, PointAxis axis, int sourceChoice, float amount) noexcept;
/** Removes it; false when there was none. */
bool removePointMod (ContentPoint& p, PointAxis axis) noexcept;
/** Present and not bypassed. */
bool pointModActive (const ContentPoint& p, PointAxis axis) noexcept;
/** A drag of `deltaPx` (positive = more) over a plot that is `rangePx` wide / high: the depth bar follows the finger (the whole plot = 1.0). */
float pointModDragAmount (float startAmount, float deltaPx, float rangePx) noexcept;
/** The axis a drag from the point means: the larger of |dx|, |dy| wins (Y when equal or when X is not allowed). */
PointAxis pointModDragAxis (float dx, float dy, bool xAllowed) noexcept;

/** The macro (1..8) a source choice stands for (19..22 = macro 1..4, 29..32 = macro 5..8), 0 for anything else. */
int macroOfSourceChoice (int sourceChoice) noexcept;

// ---- A/B compare (decision Q4: two ParamValues snapshots) -----------------------------------------------------------------------

/** The live state is always A or B; toggling stores the live values into the active slot and returns the other slot's values to apply.
    The first time B is used it starts as a copy of A, so the first toggle changes nothing audible. Message thread only. */
class AbCompare
{
public:
    enum class Slot { a, b };

    Slot active() const noexcept { return current; }
    bool otherIsStored() const noexcept { return current == Slot::a ? storedB : storedA; }

    /** Returns the values to apply (the other slot); `live` is remembered for the slot being left. */
    const ParamValues& toggle (const ParamValues& live);
    /** "Copy A to B" (or B to A): the live values overwrite the inactive slot. */
    void copyActiveToOther (const ParamValues& live);
    /** The snapshot of a slot (for tests and the editor's tooltips). */
    const ParamValues& snapshot (Slot s) const noexcept { return s == Slot::a ? a : b; }

private:
    ParamValues a, b;
    bool storedA = false, storedB = false;
    Slot current = Slot::a;
};

inline bool operator== (const ParamValues& a, const ParamValues& b) noexcept
{
    const size_t count = paramCount();
    for (size_t i = 0; i < count; ++i)
        if (std::abs (a.plain[i] - b.plain[i]) > 1.0e-7f)
            return false;
    return true;
}
inline bool operator!= (const ParamValues& a, const ParamValues& b) noexcept { return ! (a == b); }

// ---- Undo / Redo (docs/ui-design/01 section 1.1, 4: session-only, 200 depth) ----------------------------------------------------

class UndoHistory
{
public:
    static constexpr size_t kMaxDepth = 200;

    void push (const ParamValues& before)
    {
        undoStack.push_back (before);
        if (undoStack.size() > kMaxDepth)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    bool undo (const ParamValues& currentLive, ParamValues& outTarget)
    {
        if (undoStack.empty())
            return false;
        redoStack.push_back (currentLive);
        outTarget = undoStack.back();
        undoStack.pop_back();
        return true;
    }

    bool redo (const ParamValues& currentLive, ParamValues& outTarget)
    {
        if (redoStack.empty())
            return false;
        undoStack.push_back (currentLive);
        outTarget = redoStack.back();
        redoStack.pop_back();
        return true;
    }

    void clear()
    {
        undoStack.clear();
        redoStack.clear();
    }

    size_t undoDepth() const noexcept { return undoStack.size(); }
    size_t redoDepth() const noexcept { return redoStack.size(); }

private:
    std::vector<ParamValues> undoStack;
    std::vector<ParamValues> redoStack;
};

// ---- Module Clipboard & Operations (docs/ui-design/01 section 1.4, 2.4) ---------------------------------------------------------

struct CopiedModSlot
{
    int slot = -1;
    bool isSource = false; // true: module was source; false: module was destination
    int source = 0;
    std::string destKey;
    float amount = 0.0f;
    bool bipolar = false;
    bool bypass = false;
};

struct ModuleSnapshot
{
    std::string prefix;
    std::string kind;
    std::vector<std::pair<std::string, float>> params; // (property suffix, plain value)
    std::vector<CopiedModSlot> mods;
    bool empty() const noexcept { return params.empty(); }
};

std::string moduleKindForPrefix (std::string_view prefix);
int sourceChoiceForModulePrefix (std::string_view prefix);

class ModuleClipboard
{
public:
    static ModuleClipboard& instance();

    bool hasContent() const noexcept { return ! current.empty(); }
    const ModuleSnapshot& content() const noexcept { return current; }
    bool canPasteTo (std::string_view destPrefix) const;

    void copy (std::string_view prefix, const ParamValues& live, bool withMods = false, const ModMatrixAccess* matrix = nullptr);
    bool paste (std::string_view destPrefix, ParamValues& target, bool withMods = false, ModMatrixAccess* matrix = nullptr) const;
    static void initModule (std::string_view prefix, ParamValues& target);
    static bool swapModules (std::string_view prefixA, std::string_view prefixB, ParamValues& target, bool withMods = false, ModMatrixAccess* matrix = nullptr);

    void clear() { current = ModuleSnapshot {}; }

private:
    ModuleSnapshot current;
};

// ---- Key & Scale helpers (docs/ui-design/01 section 1.3, 2.4, docs/params.json) --------------------------------------------------

struct ScaleInfo
{
    static constexpr int kNumKeys = 12;
    static constexpr int kNumScales = 20;

    static const char* keyName (int keyIndex) noexcept;
    static const char* scaleName (int scaleType) noexcept;
    static bool inScale (int scaleKey, int scaleType, int midiNote) noexcept;
    static bool isRoot (int scaleKey, int scaleType, int midiNote) noexcept;
};
} // namespace synthyboy::ui
