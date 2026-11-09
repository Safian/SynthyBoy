// SynthyBoy - UI view-model: modulation assign flow, mod snapshot, A/B compare.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModModel.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::ui
{
namespace
{
/** The slot parameter indices, looked up once (modSlotParams searches the table by key). */
const std::array<ModSlotParams, kNumModSlots>& slotTable()
{
    static const std::array<ModSlotParams, kNumModSlots> table = []
    {
        std::array<ModSlotParams, kNumModSlots> t {};
        for (int i = 0; i < kNumModSlots; ++i)
            t[static_cast<size_t> (i)] = modSlotParams (i);
        return t;
    }();
    return table;
}
} // namespace

ModSnapshot ModSnapshot::build (const ModMatrixAccess& access)
{
    ModSnapshot s;
    s.sourceCounts.assign (static_cast<size_t> (kNumModSources) + 1, 0);
    const auto& slots = slotTable();
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const ModSlotParams& p = slots[static_cast<size_t> (i)];
        if (p.source < 0 || ! slotInUse (access, p))
            continue;

        ModEntry e;
        e.slot = i;
        e.source = static_cast<int> (std::lround (access.get (p.source)));
        e.amount = access.get (p.amount);
        e.bipolar = access.get (p.bipolar) >= 0.5f;
        e.bypass = access.get (p.bypass) >= 0.5f;
        const int dest = modDestinationParamIndex (static_cast<size_t> (std::lround (access.get (p.destination))));
        if (dest < 0)
            continue;

        s.byDestination[dest].push_back (e);
        ++s.used;
        if (e.source >= 1 && e.source <= kNumModSources)
            ++s.sourceCounts[static_cast<size_t> (e.source)];
    }
    return s;
}

const std::vector<ModEntry>& ModSnapshot::entriesFor (int destParamIndex) const
{
    static const std::vector<ModEntry> none;
    const auto it = byDestination.find (destParamIndex);
    return it == byDestination.end() ? none : it->second;
}

std::vector<std::pair<int, ModEntry>> ModSnapshot::entriesForSource (int sourceChoice) const
{
    std::vector<std::pair<int, ModEntry>> out;
    for (const auto& kv : byDestination)
        for (const auto& e : kv.second)
            if (e.source == sourceChoice)
                out.emplace_back (kv.first, e);
    std::sort (out.begin(), out.end(), [] (const auto& x, const auto& y) { return x.second.slot < y.second.slot; });
    return out;
}

int ModSnapshot::destinationCount (int sourceChoice) const
{
    return sourceChoice >= 0 && static_cast<size_t> (sourceChoice) < sourceCounts.size() ? sourceCounts[static_cast<size_t> (sourceChoice)] : 0;
}

bool ModSnapshot::operator== (const ModSnapshot& other) const noexcept
{
    if (used != other.used || byDestination.size() != other.byDestination.size())
        return false;
    for (const auto& kv : byDestination)
    {
        const auto it = other.byDestination.find (kv.first);
        if (it == other.byDestination.end() || it->second.size() != kv.second.size())
            return false;
        for (size_t i = 0; i < kv.second.size(); ++i)
        {
            const ModEntry &x = kv.second[i], &y = it->second[i];
            if (x.slot != y.slot || x.source != y.source || x.amount != y.amount || x.bipolar != y.bipolar || x.bypass != y.bypass)
                return false;
        }
    }
    return true;
}

// ---- AssignFlow -----------------------------------------------------------------------------------------------------------------

AssignFlow::Phase AssignFlow::tapSource (int sourceChoice)
{
    if (state == Phase::dragging || sourceChoice < 1 || sourceChoice > kNumModSources)
        return state;

    if (state == Phase::armed && source == sourceChoice)
    {
        state = Phase::idle;
        source = 0;
    }
    else
    {
        state = Phase::armed;
        source = sourceChoice;
    }
    return state;
}

void AssignFlow::cancel()
{
    if (state == Phase::dragging)
        endDrag();
    state = Phase::idle;
    source = 0;
}

bool AssignFlow::isTarget (int paramIndex) const
{
    return state != Phase::idle && paramIndex >= 0 && modDestinationChoiceOf (static_cast<size_t> (paramIndex)) > 0;
}

bool AssignFlow::defaultBipolar (const ParamInfo& d) noexcept
{
    return d.scale != Scale::stepped && d.min < 0.0f && d.max > 0.0f;
}

int AssignFlow::findSlot (int sourceChoice, int destinationChoice) const
{
    const auto& slots = slotTable();
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const ModSlotParams& p = slots[static_cast<size_t> (i)];
        if (p.source >= 0 && std::lround (matrix.get (p.source)) == sourceChoice && std::lround (matrix.get (p.destination)) == destinationChoice)
            return i;
    }
    return -1;
}

int AssignFlow::findFreeSlot() const
{
    const auto& slots = slotTable();
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const ModSlotParams& p = slots[static_cast<size_t> (i)];
        if (p.source >= 0 && ! slotInUse (matrix, p))
            return i;
    }
    return -1;
}

AssignFlow::BeginResult AssignFlow::beginDrag (int paramIndex)
{
    if (state != Phase::armed)
        return BeginResult::notArmed;
    if (! isTarget (paramIndex))
        return BeginResult::notModulatable;

    const int destChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (paramIndex)));
    int s = findSlot (source, destChoice);
    const bool isNew = s < 0;
    if (isNew)
    {
        s = findFreeSlot();
        if (s < 0)
            return BeginResult::matrixFull;
    }

    const ModSlotParams& p = slotTable()[static_cast<size_t> (s)];
    if (isNew)
    {
        // source last: a half written slot is not in use (destination or source 0) and so is inaudible
        matrix.set (p.amount, 0.0f);
        matrix.set (p.bipolar, defaultBipolar (paramTable()[static_cast<size_t> (paramIndex)]) ? 1.0f : 0.0f);
        matrix.set (p.bypass, 0.0f);
        matrix.set (p.destination, static_cast<float> (destChoice));
        matrix.set (p.source, static_cast<float> (source));
    }

    slot = s;
    destination = paramIndex;
    state = Phase::dragging;
    matrix.beginGesture (p.amount);
    return BeginResult::ok;
}

float AssignFlow::amountNormalised() const
{
    if (state != Phase::dragging)
        return 0.5f;
    const ParamInfo& info = paramTable()[static_cast<size_t> (slotTable()[static_cast<size_t> (slot)].amount)];
    return toNormalised (info, matrix.get (slotTable()[static_cast<size_t> (slot)].amount));
}

void AssignFlow::dragBy (float dyUpPx, FineZone zone)
{
    if (state != Phase::dragging)
        return;
    const int idx = slotTable()[static_cast<size_t> (slot)].amount;
    const ParamInfo& info = paramTable()[static_cast<size_t> (idx)];
    const float n = applyDragDelta (toNormalised (info, matrix.get (idx)), dyUpPx, zone);
    matrix.set (idx, fromNormalised (info, n));
}

void AssignFlow::setAmount (float percent)
{
    if (state != Phase::dragging)
        return;
    const int idx = slotTable()[static_cast<size_t> (slot)].amount;
    matrix.set (idx, constrainValue (paramTable()[static_cast<size_t> (idx)], percent));
}

void AssignFlow::endDrag()
{
    if (state != Phase::dragging)
        return;
    matrix.endGesture (slotTable()[static_cast<size_t> (slot)].amount);
    slot = -1;
    destination = -1;
    state = Phase::armed;
}

void clearSlot (ModMatrixAccess& matrix, int slot)
{
    if (slot < 0 || slot >= kNumModSlots)
        return;
    const ModSlotParams& p = slotTable()[static_cast<size_t> (slot)];
    const int all[] = { p.source, p.destination, p.amount, p.power, p.bipolar, p.stereo, p.bypass };
    // source first: the slot is out of use before the rest changes
    for (const int idx : all)
        if (idx >= 0)
        {
            matrix.beginGesture (idx);
            matrix.set (idx, paramTable()[static_cast<size_t> (idx)].def);
            matrix.endGesture (idx);
        }
}

bool swapModSlots (ModMatrixAccess& matrix, int slotA, int slotB)
{
    if (slotA == slotB || slotA < 0 || slotB < 0 || slotA >= kNumModSlots || slotB >= kNumModSlots)
        return false;
    char a[8], b[8];
    std::snprintf (a, sizeof a, "mod%02d.", slotA + 1);
    std::snprintf (b, sizeof b, "mod%02d.", slotB + 1);
    const auto* table = paramTable();
    const std::string_view prefixA (a);
    // an unused slot has source or destination unset: write the sources last when filling, first when emptying, so the engine never sees a
    // half moved slot (the same order clearSlot uses)
    std::vector<std::pair<int, int>> pairs; // (index in A, index in B)
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const std::string_view key (table[i].key);
        if (key.rfind (prefixA, 0) != 0)
            continue;
        const auto other = resolveKey (std::string (b) + std::string (key.substr (prefixA.size())));
        if (other.valid())
            pairs.emplace_back (static_cast<int> (i), other.index);
    }
    for (const auto& [ia, ib] : pairs)
    {
        const float va = matrix.get (ia), vb = matrix.get (ib);
        matrix.beginGesture (ia);
        matrix.set (ia, vb);
        matrix.endGesture (ia);
        matrix.beginGesture (ib);
        matrix.set (ib, va);
        matrix.endGesture (ib);
    }
    return ! pairs.empty();
}

int macroOfSourceChoice (int sourceChoice) noexcept
{
    if (sourceChoice >= 19 && sourceChoice <= 22)
        return sourceChoice - 18;
    if (sourceChoice >= 29 && sourceChoice <= 32)
        return sourceChoice - 28 + 4;
    return 0;
}

void setSlotBypass (ModMatrixAccess& matrix, int slot, bool bypass)
{
    if (slot < 0 || slot >= kNumModSlots)
        return;
    const int idx = slotTable()[static_cast<size_t> (slot)].bypass;
    if (idx < 0)
        return;
    matrix.beginGesture (idx);
    matrix.set (idx, bypass ? 1.0f : 0.0f);
    matrix.endGesture (idx);
}

// ---- AbCompare ------------------------------------------------------------------------------------------------------------------

const ParamValues& AbCompare::toggle (const ParamValues& live)
{
    if (current == Slot::a)
    {
        a = live;
        storedA = true;
        if (! storedB)
        {
            b = live;
            storedB = true;
        }
        current = Slot::b;
        return b;
    }
    b = live;
    storedB = true;
    if (! storedA)
    {
        a = live;
        storedA = true;
    }
    current = Slot::a;
    return a;
}

void AbCompare::copyActiveToOther (const ParamValues& live)
{
    a = live; // the live values are the active slot; the other slot becomes a copy of it
    b = live;
    storedA = storedB = true;
}

// ---- Module Clipboard & Operations -----------------------------------------------------------------------------------------------

std::string moduleKindForPrefix (std::string_view prefix)
{
    if (prefix.rfind ("env", 0) == 0) return "env";
    if (prefix.rfind ("rlfo", 0) == 0) return "rlfo";
    if (prefix.rfind ("lfo", 0) == 0) return "lfo";
    if (prefix.rfind ("osc", 0) == 0) return "osc";
    if (prefix.rfind ("filter", 0) == 0) return "filter";
    if (prefix.rfind ("fxi", 0) == 0) return "fxi"; // the slots of the effect instance pool paste onto each other
    return std::string (prefix);
}

int sourceChoiceForModulePrefix (std::string_view prefix)
{
    if (prefix.rfind ("env", 0) == 0 && prefix.length() > 3)
    {
        const int n = std::atoi (prefix.data() + 3);
        if (n >= 1 && n <= 6) return n; // 1..6
    }
    else if (prefix.rfind ("lfo", 0) == 0 && prefix.length() > 3)
    {
        const int n = std::atoi (prefix.data() + 3);
        if (n >= 1 && n <= 8) return 6 + n; // 7..14
        if (n == 9) return 33;
        if (n == 10) return 34;
    }
    else if (prefix.rfind ("rlfo", 0) == 0 && prefix.length() > 4)
    {
        const int n = std::atoi (prefix.data() + 4);
        if (n >= 1 && n <= 4) return 14 + n; // 15..18
    }
    return 0;
}

ModuleClipboard& ModuleClipboard::instance()
{
    static ModuleClipboard clip;
    return clip;
}

bool ModuleClipboard::canPasteTo (std::string_view destPrefix) const
{
    if (current.empty() || destPrefix == current.prefix)
        return false;
    return moduleKindForPrefix (destPrefix) == current.kind;
}

void ModuleClipboard::copy (std::string_view prefix, const ParamValues& live, bool withMods, const ModMatrixAccess* matrix)
{
    current = ModuleSnapshot {};
    current.prefix = std::string (prefix);
    current.kind = moduleKindForPrefix (prefix);

    const std::string dotPrefix = current.prefix + ".";
    const auto* table = paramTable();
    const size_t count = paramCount();

    for (size_t i = 0; i < count; ++i)
    {
        std::string_view key (table[i].key);
        if (key.rfind (dotPrefix, 0) == 0)
        {
            const std::string prop (key.substr (dotPrefix.length()));
            current.params.emplace_back (prop, live.plain[i]);
        }
    }

    if (withMods && matrix != nullptr)
    {
        const int srcChoice = sourceChoiceForModulePrefix (prefix);
        for (int slot = 0; slot < kNumModSlots; ++slot)
        {
            const auto p = modSlotParams (slot);
            if (! slotInUse (*matrix, p))
                continue;

            const int slotSrc = static_cast<int> (std::lround (matrix->get (p.source)));
            const int destChoice = static_cast<int> (std::lround (matrix->get (p.destination)));
            const int destParam = modDestinationParamIndex (static_cast<size_t> (destChoice));
            const std::string destKey = destParam >= 0 ? table[destParam].key : "";

            if (srcChoice > 0 && slotSrc == srcChoice)
            {
                current.mods.push_back ({ slot, true, slotSrc, destKey,
                                          matrix->get (p.amount), matrix->get (p.bipolar) >= 0.5f, matrix->get (p.bypass) >= 0.5f });
            }
            else if (! destKey.empty() && destKey.rfind (dotPrefix, 0) == 0)
            {
                current.mods.push_back ({ slot, false, slotSrc, destKey,
                                          matrix->get (p.amount), matrix->get (p.bipolar) >= 0.5f, matrix->get (p.bypass) >= 0.5f });
            }
        }
    }
}

bool ModuleClipboard::paste (std::string_view destPrefix, ParamValues& target, bool withMods, ModMatrixAccess* matrix) const
{
    if (current.empty())
        return false;

    const std::string dotPrefix = std::string (destPrefix) + ".";
    for (const auto& [prop, val] : current.params)
    {
        const std::string key = dotPrefix + prop;
        const auto ref = resolveKey (key);
        if (ref.valid())
            target.plain[ref.index] = val;
    }

    if (withMods && matrix != nullptr)
    {
        const int newSrcChoice = sourceChoiceForModulePrefix (destPrefix);
        for (const auto& m : current.mods)
        {
            int slotToUse = -1;
            int finalSrc = m.source;
            std::string finalDestKey = m.destKey;

            if (m.isSource)
            {
                if (newSrcChoice <= 0)
                    continue;
                finalSrc = newSrcChoice;
            }
            else
            {
                if (m.destKey.length() > current.prefix.length())
                {
                    const std::string prop = m.destKey.substr (current.prefix.length() + 1);
                    finalDestKey = dotPrefix + prop;
                }
            }

            const auto destRef = resolveKey (finalDestKey);
            if (! destRef.valid() || modDestinationChoiceOf (static_cast<size_t> (destRef.index)) == 0)
                continue;

            const size_t destChoice = modDestinationChoiceOf (static_cast<size_t> (destRef.index));
            if (destChoice == 0)
                continue;

            for (int s = 0; s < kNumModSlots; ++s)
            {
                const auto p = modSlotParams (s);
                const int curSrc = static_cast<int> (std::lround (matrix->get (p.source)));
                const int curDst = static_cast<int> (std::lround (matrix->get (p.destination)));
                if (curSrc == finalSrc && curDst == static_cast<int> (destChoice))
                {
                    slotToUse = s;
                    break;
                }
                if (slotToUse < 0 && (curSrc == 0 || curDst == 0))
                    slotToUse = s;
            }

            if (slotToUse >= 0)
            {
                const auto p = modSlotParams (slotToUse);
                matrix->beginGesture (p.amount);
                matrix->set (p.destination, static_cast<float> (destChoice));
                matrix->set (p.amount, m.amount);
                matrix->set (p.bipolar, m.bipolar ? 1.0f : 0.0f);
                matrix->set (p.bypass, m.bypass ? 1.0f : 0.0f);
                matrix->set (p.source, static_cast<float> (finalSrc));
                matrix->endGesture (p.amount);
            }
        }
    }
    return true;
}

void ModuleClipboard::initModule (std::string_view prefix, ParamValues& target)
{
    const std::string dotPrefix = std::string (prefix) + ".";
    const auto* table = paramTable();
    const size_t count = paramCount();
    for (size_t i = 0; i < count; ++i)
    {
        std::string_view key (table[i].key);
        if (key.rfind (dotPrefix, 0) == 0)
            target.plain[i] = table[i].def;
    }
}

bool ModuleClipboard::swapModules (std::string_view prefixA, std::string_view prefixB, ParamValues& target, bool withMods, ModMatrixAccess* matrix)
{
    if (prefixA == prefixB || moduleKindForPrefix (prefixA) != moduleKindForPrefix (prefixB))
        return false;

    const std::string dotA = std::string (prefixA) + ".";
    const std::string dotB = std::string (prefixB) + ".";
    const auto* table = paramTable();
    const size_t count = paramCount();

    for (size_t i = 0; i < count; ++i)
    {
        std::string_view keyA (table[i].key);
        if (keyA.rfind (dotA, 0) == 0)
        {
            const std::string prop (keyA.substr (dotA.length()));
            const std::string keyB = dotB + prop;
            const auto refB = resolveKey (keyB);
            if (refB.valid())
                std::swap (target.plain[i], target.plain[refB.index]);
        }
    }

    if (withMods && matrix != nullptr)
    {
        const int srcA = sourceChoiceForModulePrefix (prefixA);
        const int srcB = sourceChoiceForModulePrefix (prefixB);

        for (int s = 0; s < kNumModSlots; ++s)
        {
            const auto p = modSlotParams (s);
            if (! slotInUse (*matrix, p))
                continue;

            int curSrc = static_cast<int> (std::lround (matrix->get (p.source)));
            int curDstChoice = static_cast<int> (std::lround (matrix->get (p.destination)));
            int destParam = modDestinationParamIndex (static_cast<size_t> (curDstChoice));
            std::string destKey = destParam >= 0 ? table[destParam].key : "";

            bool changed = false;
            if (srcA > 0 && srcB > 0)
            {
                if (curSrc == srcA) { curSrc = srcB; changed = true; }
                else if (curSrc == srcB) { curSrc = srcA; changed = true; }
            }

            if (! destKey.empty())
            {
                if (destKey.rfind (dotA, 0) == 0)
                {
                    const std::string newKey = dotB + destKey.substr (dotA.length());
                    const auto r = resolveKey (newKey);
                    if (r.valid()) { curDstChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (r.index))); changed = true; }
                }
                else if (destKey.rfind (dotB, 0) == 0)
                {
                    const std::string newKey = dotA + destKey.substr (dotB.length());
                    const auto r = resolveKey (newKey);
                    if (r.valid()) { curDstChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (r.index))); changed = true; }
                }
            }

            if (changed)
            {
                matrix->set (p.destination, static_cast<float> (curDstChoice));
                matrix->set (p.source, static_cast<float> (curSrc));
            }
        }
    }
    return true;
}

// ---- ScaleInfo --------------------------------------------------------------------------------------------------------------------

namespace
{
struct ScalePattern
{
    int count;
    int steps[8];
};

constexpr ScalePattern kScalePatterns[] = {
    { 12, { 0 } },                          // 0: Chromatic (off)
    { 7, { 0, 2, 4, 5, 7, 9, 11 } },        // 1: Major
    { 7, { 0, 2, 3, 5, 7, 8, 10 } },        // 2: Natural Minor
    { 7, { 0, 2, 3, 5, 7, 8, 11 } },        // 3: Harmonic Minor
    { 7, { 0, 2, 3, 5, 7, 9, 11 } },        // 4: Melodic Minor
    { 7, { 0, 2, 3, 5, 7, 9, 10 } },        // 5: Dorian
    { 7, { 0, 1, 3, 5, 7, 8, 10 } },        // 6: Phrygian
    { 7, { 0, 2, 4, 6, 7, 9, 11 } },        // 7: Lydian
    { 7, { 0, 2, 4, 5, 7, 9, 10 } },        // 8: Mixolydian
    { 7, { 0, 1, 3, 5, 6, 8, 10 } },        // 9: Locrian
    { 5, { 0, 2, 4, 7, 9 } },               // 10: Major Pentatonic
    { 5, { 0, 3, 5, 7, 10 } },              // 11: Minor Pentatonic
    { 6, { 0, 3, 5, 6, 7, 10 } },           // 12: Blues
    { 6, { 0, 2, 4, 6, 8, 10 } },           // 13: Whole Tone
    { 8, { 0, 1, 3, 4, 6, 7, 9, 10 } },     // 14: Diminished (H-W)
    { 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },     // 15: Diminished (W-H)
    { 7, { 0, 1, 4, 5, 7, 8, 10 } },        // 16: Phrygian Dominant
    { 7, { 0, 2, 3, 6, 7, 8, 11 } },        // 17: Hungarian Minor
    { 7, { 0, 1, 4, 5, 7, 8, 11 } },        // 18: Double Harmonic
    { 5, { 0, 2, 3, 7, 8 } },               // 19: Hirajoshi
};
} // namespace

const char* ScaleInfo::keyName (int keyIndex) noexcept
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((keyIndex % 12) + 12) % 12];
}

const char* ScaleInfo::scaleName (int scaleType) noexcept
{
    static constexpr const char* names[] = {
        "Chromatic (off)", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
        "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
        "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone",
        "Diminished (H-W)", "Diminished (W-H)", "Phrygian Dominant",
        "Hungarian Minor", "Double Harmonic", "Hirajoshi"
    };
    if (scaleType < 0 || scaleType >= kNumScales)
        return "Chromatic (off)";
    return names[scaleType];
}

bool ScaleInfo::inScale (int scaleKey, int scaleType, int midiNote) noexcept
{
    if (scaleType <= 0 || scaleType >= kNumScales)
        return true;

    const int pitchClass = ((midiNote % 12) - (scaleKey % 12) + 12) % 12;
    const auto& pat = kScalePatterns[scaleType];
    for (int i = 0; i < pat.count; ++i)
        if (pat.steps[i] == pitchClass)
            return true;
    return false;
}

bool ScaleInfo::isRoot (int scaleKey, int scaleType, int midiNote) noexcept
{
    if (scaleType <= 0)
        return false;
    const int pitchClass = ((midiNote % 12) - (scaleKey % 12) + 12) % 12;
    return pitchClass == 0;
}
// ---- Point modulation ---------------------------------------------------------------------------------------------------------------

bool canModulatePointAxis (size_t index, size_t pointCount, PointAxis axis) noexcept
{
    if (index >= pointCount)
        return false;
    return axis == PointAxis::y || (index > 0 && index + 1 < pointCount);
}

const char* pointModSourceKey (int sourceChoice) noexcept
{
    return sourceChoice >= 1 && sourceChoice <= kNumModSources ? modSourceTable()[sourceChoice - 1].key : "";
}

int pointModSourceChoice (const std::string& key) noexcept
{
    for (int i = 0; i < kNumModSources; ++i)
        if (key == modSourceTable()[i].key)
            return i + 1;
    return 0;
}

bool setPointMod (ContentPoint& p, size_t index, size_t pointCount, PointAxis axis, int sourceChoice, float amount) noexcept
{
    if (sourceChoice < 1 || sourceChoice > kNumModSources || ! canModulatePointAxis (index, pointCount, axis))
        return false;
    auto& m = pointModOf (p, axis);
    m.present = true;
    m.source = pointModSourceKey (sourceChoice);
    m.amount = std::clamp (amount, -static_cast<float> (kMaxPointModAmount), static_cast<float> (kMaxPointModAmount));
    return true;
}

bool removePointMod (ContentPoint& p, PointAxis axis) noexcept
{
    auto& m = pointModOf (p, axis);
    if (! m.present)
        return false;
    m = {};
    return true;
}

bool pointModActive (const ContentPoint& p, PointAxis axis) noexcept
{
    const auto& m = pointModOf (p, axis);
    return m.present && std::abs (m.amount) > 0.0f;
}

float pointModDragAmount (float startAmount, float deltaPx, float rangePx) noexcept
{
    const float swing = 1.0f / std::max (1.0f, rangePx);
    return std::clamp (startAmount + deltaPx * swing, -static_cast<float> (kMaxPointModAmount), static_cast<float> (kMaxPointModAmount));
}

PointAxis pointModDragAxis (float dx, float dy, bool xAllowed) noexcept
{
    return xAllowed && std::abs (dx) > std::abs (dy) ? PointAxis::x : PointAxis::y;
}
} // namespace synthyboy::ui
