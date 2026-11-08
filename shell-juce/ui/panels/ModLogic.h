// SynthyBoy - UI: modulation logic (cycle detection, slot sorting, macro operations, curve maths).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "../model/ControlModel.h"
#include "../model/ModModel.h"

namespace synthyboy::ui
{
/** Evaluates power curve for input t in [0, 1] (or [-1, 1] if bipolar), power in [-10, 10]. */
inline float modPowerCurve (float t, float power, bool bipolar = false) noexcept
{
    if (bipolar)
    {
        const float sign = t < 0.0f ? -1.0f : 1.0f;
        const float absT = std::abs (t);
        return sign * modPowerCurve (absT, power, false);
    }

    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (std::abs (power) < 0.001f) return t;

    float p = power;
    if (p > 25.0f) p = 25.0f;
    if (p < -25.0f) p = -25.0f;

    const float num = std::expm1 (p * t);
    const float den = std::expm1 (p);
    return num / den;
}

/** Returns the mod source choice (1..38) if parameter key belongs to that source generator, else 0. */
inline int modSourceChoiceForParamKey (std::string_view key) noexcept
{
    for (int e = 1; e <= 6; ++e)
    {
        char prefix[8];
        std::snprintf (prefix, sizeof prefix, "env%d.", e);
        if (key.rfind (prefix, 0) == 0)
            return e;
    }
    for (int l = 1; l <= 8; ++l)
    {
        char prefix[8];
        std::snprintf (prefix, sizeof prefix, "lfo%d.", l);
        if (key.rfind (prefix, 0) == 0)
            return 6 + l;
    }
    for (int r = 1; r <= 4; ++r)
    {
        char prefix[8];
        std::snprintf (prefix, sizeof prefix, "rlfo%d.", r);
        if (key.rfind (prefix, 0) == 0)
            return 14 + r;
    }
    for (int m = 1; m <= 4; ++m)
    {
        char prefix[10];
        std::snprintf (prefix, sizeof prefix, "macro.%d", m);
        if (key == prefix)
            return 18 + m;
    }
    for (int m = 5; m <= 8; ++m)
    {
        char prefix[10];
        std::snprintf (prefix, sizeof prefix, "macro.%d", m);
        if (key == prefix)
            return 28 + (m - 4);
    }
    if (key.rfind ("lfo9.", 0) == 0)
        return 33;
    if (key.rfind ("lfo10.", 0) == 0)
        return 34;

    return 0;
}

struct ModLoopResult
{
    bool isLoop = false;
    int conflictingSlot = -1; // 0-based
    std::string reason;
};

/** The 0-based matrix slot whose amount is the modulation destination `dstChoice` (modNN.amount, block B20 / G1), or -1. */
inline int modAmountSlotOfChoice (int dstChoice) noexcept
{
    if (dstChoice <= 0)
        return -1;
    const int p = modDestinationParamIndex (static_cast<size_t> (dstChoice));
    if (p < 0 || static_cast<size_t> (p) >= paramCount())
        return -1;
    const char* key = paramTable()[p].key;
    int n = 0;
    char tail[16] = {};
    if (std::sscanf (key, "mod%d.%15s", &n, tail) == 2 && std::string_view (tail) == "amount" && n >= 1 && n <= kNumModSlots)
        return n - 1;
    return -1;
}

/** True if slot `slot` pointed at the amount destination `dstChoice` would close a loop of amount modulations (a slot that modulates its
    own amount, or A -> amount of B -> ... -> amount of A). The engine leaves such a connection unwired (core/engine-vital, MODAMT). */
inline bool modAmountWouldLoop (const ModMatrixAccess& matrix, int slot, int dstChoice) noexcept
{
    int cur = modAmountSlotOfChoice (dstChoice);
    for (int hops = 0; cur >= 0 && hops <= kNumModSlots; ++hops)
    {
        if (cur == slot)
            return true;
        const auto p = modSlotParams (cur);
        if (p.source < 0 || p.destination < 0 || std::lround (matrix.get (p.source)) <= 0 || (p.bypass >= 0 && matrix.get (p.bypass) >= 0.5f))
            return false;
        cur = modAmountSlotOfChoice (static_cast<int> (std::lround (matrix.get (p.destination))));
    }
    return false;
}

/** Checks topological feedback loop cycles in the modulation dependency graph (06-modulation.md section 4.2). */
inline ModLoopResult modDetectFeedbackLoop (const ModMatrixAccess& matrix, int slotToCheck) noexcept
{
    if (slotToCheck < 0 || slotToCheck >= kNumModSlots)
        return {};

    const auto pCheck = modSlotParams (slotToCheck);
    if (pCheck.source < 0 || pCheck.destination < 0)
        return {};

    const int srcChoice = static_cast<int> (std::round (matrix.get (pCheck.source)));
    const int dstChoice = static_cast<int> (std::round (matrix.get (pCheck.destination)));
    if (srcChoice <= 0 || dstChoice <= 0)
        return {};

    if (modAmountWouldLoop (matrix, slotToCheck, dstChoice))
    {
        ModLoopResult res;
        res.isLoop = true;
        res.conflictingSlot = slotToCheck;
        res.reason = "Amount loop: the slot modulates its own amount (directly or through other slots); the engine leaves it off.";
        return res;
    }

    const int dstParam = modDestinationParamIndex (static_cast<size_t> (dstChoice));
    if (dstParam < 0 || static_cast<size_t> (dstParam) >= paramCount())
        return {};

    const int targetSrcChoice = modSourceChoiceForParamKey (paramTable()[dstParam].key);
    if (targetSrcChoice <= 0)
        return {};

    if (srcChoice == targetSrcChoice)
    {
        ModLoopResult res;
        res.isLoop = true;
        res.conflictingSlot = slotToCheck;
        res.reason = "Feedback loop: slot self-modulates source generator.";
        return res;
    }

    std::vector<std::pair<int, int>> adj[kNumModSources + 1];
    for (int s = 0; s < kNumModSlots; ++s)
    {
        if (s == slotToCheck)
            continue;
        const auto p = modSlotParams (s);
        if (p.source < 0 || p.destination < 0)
            continue;
        const int sc = static_cast<int> (std::round (matrix.get (p.source)));
        const int dc = static_cast<int> (std::round (matrix.get (p.destination)));
        if (sc <= 0 || dc <= 0)
            continue;
        if (p.bypass >= 0 && matrix.get (p.bypass) >= 0.5f)
            continue;

        const int dp = modDestinationParamIndex (static_cast<size_t> (dc));
        if (dp < 0 || static_cast<size_t> (dp) >= paramCount())
            continue;

        const int target = modSourceChoiceForParamKey (paramTable()[dp].key);
        if (target > 0 && sc <= static_cast<int> (kNumModSources))
            adj[sc].push_back ({ target, s });
    }

    std::vector<bool> visited (kNumModSources + 1, false);
    std::vector<int> firstSlot (kNumModSources + 1, -1);
    std::vector<int> queue;
    queue.push_back (targetSrcChoice);
    visited[static_cast<size_t> (targetSrcChoice)] = true;

    while (! queue.empty())
    {
        const int curr = queue.back();
        queue.pop_back();

        if (curr == srcChoice)
        {
            ModLoopResult res;
            res.isLoop = true;
            res.conflictingSlot = firstSlot[static_cast<size_t> (curr)];
            char buf[64];
            std::snprintf (buf, sizeof buf, "Feedback loop with Slot #%02d — unwired by engine.", res.conflictingSlot + 1);
            res.reason = buf;
            return res;
        }

        if (curr <= static_cast<int> (kNumModSources))
        {
            for (const auto& edge : adj[curr])
            {
                const int nextNode = edge.first;
                const int edgeSlot = edge.second;
                if (! visited[static_cast<size_t> (nextNode)])
                {
                    visited[static_cast<size_t> (nextNode)] = true;
                    firstSlot[static_cast<size_t> (nextNode)] = (curr == targetSrcChoice) ? edgeSlot : firstSlot[static_cast<size_t> (curr)];
                    queue.push_back (nextNode);
                }
            }
        }
    }

    return {};
}

struct ModSlotSnapshot
{
    int source = 0;
    int destination = 0;
    float amount = 0.0f;
    float power = 0.0f;
    bool bipolar = false;
    bool stereo = false;
    bool bypass = false;
    int auxSource = 0;
    bool auxInv = false;
    float auxPower = 0.0f;
    float auxOutput = 100.0f;

    bool inUse() const noexcept { return source > 0 && destination > 0; }
};

inline ModSlotSnapshot readSlot (const ModMatrixAccess& matrix, int slot)
{
    const auto p = modSlotParams (slot);
    ModSlotSnapshot s;
    if (p.source >= 0) s.source = static_cast<int> (std::round (matrix.get (p.source)));
    if (p.destination >= 0) s.destination = static_cast<int> (std::round (matrix.get (p.destination)));
    if (p.amount >= 0) s.amount = matrix.get (p.amount);
    if (p.power >= 0) s.power = matrix.get (p.power);
    if (p.bipolar >= 0) s.bipolar = matrix.get (p.bipolar) >= 0.5f;
    if (p.stereo >= 0) s.stereo = matrix.get (p.stereo) >= 0.5f;
    if (p.bypass >= 0) s.bypass = matrix.get (p.bypass) >= 0.5f;
    if (p.auxSource >= 0) s.auxSource = static_cast<int> (std::round (matrix.get (p.auxSource)));
    if (p.auxInv >= 0) s.auxInv = matrix.get (p.auxInv) >= 0.5f;
    if (p.auxPower >= 0) s.auxPower = matrix.get (p.auxPower);
    if (p.auxOutput >= 0) s.auxOutput = matrix.get (p.auxOutput);
    return s;
}

inline void writeSlot (ModMatrixAccess& matrix, int slot, const ModSlotSnapshot& s)
{
    const auto p = modSlotParams (slot);
    if (p.source >= 0) matrix.set (p.source, static_cast<float> (s.source));
    if (p.destination >= 0) matrix.set (p.destination, static_cast<float> (s.destination));
    if (p.amount >= 0) matrix.set (p.amount, s.amount);
    if (p.power >= 0) matrix.set (p.power, s.power);
    if (p.bipolar >= 0) matrix.set (p.bipolar, s.bipolar ? 1.0f : 0.0f);
    if (p.stereo >= 0) matrix.set (p.stereo, s.stereo ? 1.0f : 0.0f);
    if (p.bypass >= 0) matrix.set (p.bypass, s.bypass ? 1.0f : 0.0f);
    if (p.auxSource >= 0) matrix.set (p.auxSource, static_cast<float> (s.auxSource));
    if (p.auxInv >= 0) matrix.set (p.auxInv, s.auxInv ? 1.0f : 0.0f);
    if (p.auxPower >= 0) matrix.set (p.auxPower, s.auxPower);
    if (p.auxOutput >= 0) matrix.set (p.auxOutput, s.auxOutput);
}

inline void modSortSlotsBySource (ModMatrixAccess& matrix)
{
    std::vector<ModSlotSnapshot> active;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto s = readSlot (matrix, i);
        if (s.inUse())
            active.push_back (s);
    }
    std::stable_sort (active.begin(), active.end(), [] (const ModSlotSnapshot& a, const ModSlotSnapshot& b) {
        return a.source < b.source;
    });
    for (int i = 0; i < kNumModSlots; ++i)
    {
        if (i < static_cast<int> (active.size()))
            writeSlot (matrix, i, active[static_cast<size_t> (i)]);
        else
            clearSlot (matrix, i);
    }
}

inline void modSortSlotsByDestination (ModMatrixAccess& matrix)
{
    std::vector<ModSlotSnapshot> active;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto s = readSlot (matrix, i);
        if (s.inUse())
            active.push_back (s);
    }
    std::stable_sort (active.begin(), active.end(), [] (const ModSlotSnapshot& a, const ModSlotSnapshot& b) {
        return a.destination < b.destination;
    });
    for (int i = 0; i < kNumModSlots; ++i)
    {
        if (i < static_cast<int> (active.size()))
            writeSlot (matrix, i, active[static_cast<size_t> (i)]);
        else
            clearSlot (matrix, i);
    }
}

inline void modSwapMacros (ModMatrixAccess& matrix, int macroA, int macroB,
                           const std::function<float (int)>& getParam,
                           const std::function<void (int, float)>& setParam)
{
    if (macroA < 1 || macroA > 8 || macroB < 1 || macroB > 8 || macroA == macroB)
        return;

    auto sourceChoiceForMacro = [] (int m) -> int {
        return m <= 4 ? 18 + m : 28 + (m - 4);
    };
    const int srcA = sourceChoiceForMacro (macroA);
    const int srcB = sourceChoiceForMacro (macroB);

    char keyA[16], keyB[16];
    std::snprintf (keyA, sizeof keyA, "macro.%d", macroA);
    std::snprintf (keyB, sizeof keyB, "macro.%d", macroB);
    const KeyRef refA = resolveKey (keyA);
    const KeyRef refB = resolveKey (keyB);
    const int dstChoiceA = refA.valid() ? static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (refA.index))) : -1;
    const int dstChoiceB = refB.valid() ? static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (refB.index))) : -1;

    if (refA.valid() && refB.valid() && getParam && setParam)
    {
        const float valA = getParam (refA.index);
        const float valB = getParam (refB.index);
        setParam (refA.index, valB);
        setParam (refB.index, valA);
    }

    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto s = readSlot (matrix, i);
        bool changed = false;

        if (s.source == srcA) { s.source = srcB; changed = true; }
        else if (s.source == srcB) { s.source = srcA; changed = true; }

        if (dstChoiceA > 0 && dstChoiceB > 0)
        {
            if (s.destination == dstChoiceA) { s.destination = dstChoiceB; changed = true; }
            else if (s.destination == dstChoiceB) { s.destination = dstChoiceA; changed = true; }
        }

        if (s.auxSource == srcA) { s.auxSource = srcB; changed = true; }
        else if (s.auxSource == srcB) { s.auxSource = srcA; changed = true; }

        if (changed)
            writeSlot (matrix, i, s);
    }
}

inline void modBakeMacros (ModMatrixAccess& matrix,
                           const std::function<float (int)>& getParam,
                           const std::function<void (int, float)>& setParam)
{
    if (! getParam || ! setParam)
        return;

    auto sourceChoiceForMacro = [] (int m) -> int {
        return m <= 4 ? 18 + m : 28 + (m - 4);
    };

    for (int m = 1; m <= 8; ++m)
    {
        char mkey[16];
        std::snprintf (mkey, sizeof mkey, "macro.%d", m);
        const KeyRef mref = resolveKey (mkey);
        if (! mref.valid())
            continue;

        const float macroVal = getParam (mref.index);
        const int src = sourceChoiceForMacro (m);

        for (int i = 0; i < kNumModSlots; ++i)
        {
            auto s = readSlot (matrix, i);
            if (s.source == src && s.destination > 0 && ! s.bypass)
            {
                const int dstParam = modDestinationParamIndex (static_cast<size_t> (s.destination));
                if (dstParam >= 0 && static_cast<size_t> (dstParam) < paramCount())
                {
                    const auto& info = paramTable()[dstParam];
                    const float currentVal = getParam (dstParam);
                    const float range = info.max - info.min;
                    float normMod = (macroVal / 100.0f) * (s.amount / 100.0f);
                    if (s.power != 0.0f)
                        normMod = modPowerCurve (normMod, s.power, s.bipolar);
                    const float delta = normMod * range;
                    const float newVal = std::clamp (currentVal + delta, info.min, info.max);
                    setParam (dstParam, newVal);
                }
                clearSlot (matrix, i);
            }
        }
        setParam (mref.index, 0.0f);
    }
}

inline bool modCreateVibrato (ModMatrixAccess& matrix,
                              const std::function<void (int, float)>& setParam = nullptr)
{
    ModSnapshot snap = ModSnapshot::build (matrix);
    int unusedLfo = 0;
    for (int l = 1; l <= 8; ++l)
    {
        const int choice = 6 + l;
        if (snap.destinationCount (choice) == 0)
        {
            unusedLfo = choice;
            if (setParam)
            {
                char rateKey[16];
                std::snprintf (rateKey, sizeof rateKey, "lfo%d.rate", l);
                const KeyRef rref = resolveKey (rateKey);
                if (rref.valid())
                    setParam (rref.index, 5.5f);
            }
            break;
        }
    }
    if (unusedLfo == 0)
        unusedLfo = 7;

    int freeSlot = -1;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto s = readSlot (matrix, i);
        if (! s.inUse())
        {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0)
        return false;

    const KeyRef tuneRef = resolveKey ("master.tune");
    if (! tuneRef.valid())
        return false;
    const int destChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (tuneRef.index)));
    if (destChoice <= 0)
        return false;

    ModSlotSnapshot s;
    s.source = unusedLfo;
    s.destination = destChoice;
    s.amount = 15.0f;
    s.bipolar = true;
    s.stereo = false;
    s.bypass = false;
    writeSlot (matrix, freeSlot, s);
    return true;
}

inline bool modCreateVeloAmp (ModMatrixAccess& matrix)
{
    const KeyRef volRef = resolveKey ("master.volume");
    if (! volRef.valid())
        return false;
    const int destChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (volRef.index)));
    if (destChoice <= 0)
        return false;

    int freeSlot = -1;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto s = readSlot (matrix, i);
        if (! s.inUse())
        {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0)
        return false;

    ModSlotSnapshot s;
    s.source = 23; // velocity
    s.destination = destChoice;
    s.amount = 50.0f;
    s.bipolar = false;
    s.stereo = false;
    s.bypass = false;
    writeSlot (matrix, freeSlot, s);
    return true;
}

inline void modInitAllSlots (ModMatrixAccess& matrix)
{
    for (int i = 0; i < kNumModSlots; ++i)
        clearSlot (matrix, i);
}

inline int modCountActiveSlots (const ModMatrixAccess& matrix)
{
    int count = 0;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const auto p = modSlotParams (i);
        if (slotInUse (matrix, p))
            ++count;
    }
    return count;
}
} // namespace synthyboy::ui
