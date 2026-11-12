// SynthyBoy - own engine: the modulation matrix (64 slots, sources, aux source) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../voice/param_index.h"
#include "synthyboy/modulation.h"

namespace synthyboy
{
namespace own
{
/** Modulation source ids == the choice values of modNN.source (docs/PARAMETERS.md order); 0 = none. */
enum Source : int
{
    kSrcNone = 0,
    kSrcEnv1 = 1,            // .. 6
    kSrcLfo1 = 7,            // .. 14 (lfo1 .. lfo8)
    kSrcRlfo1 = 15,          // .. 18
    kSrcMacro1 = 19,         // .. 22 (macro1 .. macro4)
    kSrcVelocity = 23, kSrcNote, kSrcAftertouch, kSrcPitchWheel, kSrcModWheel, kSrcRandom,
    kSrcMacro5 = 29,         // .. 32
    kSrcLfo9 = 33, kSrcLfo10,
    kSrcLift = 35, kSrcSlide, kSrcOctNote, kSrcPolyAftertouch, kSrcStereo, kSrcVoiceIndex, kSrcVoiceMod1, kSrcVoiceMod2,
    kSrcOscA = 43, kSrcOscB, kSrcOscC, kSrcNoise, kSrcSub, kSrcFilter1, kSrcFilter2,
    kSrcActiveVoices = 50, kSrcNoteOnAlt1, kSrcNoteOnAlt2, kSrcNoteOnRand2, kSrcNoteOnRandDiscrete, kSrcReleaseVelocity, kSrcExpressionX, kSrcFixed,
    kNumSourceIds = 58
};

inline int srcEnv (int i) noexcept { return kSrcEnv1 + i; }
inline int srcLfo (int i) noexcept { return i < 8 ? kSrcLfo1 + i : kSrcLfo9 + (i - 8); }
inline int srcRlfo (int i) noexcept { return kSrcRlfo1 + i; }
inline int srcMacro (int i) noexcept { return i < 4 ? kSrcMacro1 + i : kSrcMacro5 + (i - 4); }

/** How the control value of a destination relates to its knob (docs/engine-spec/04-modulation-matrix.md 3): the modulation is summed in the control domain. */
enum class DestKind : uint8_t { lin, sqrtLevel, quart, log2Seconds, log2Hz, note, volume };

struct DestLaw
{
    DestKind kind = DestKind::lin;
    float scale = 1.0f; // lin: control = plain * scale
    float width = 1.0f; // width D of the control range
};

/** Law of the table entry (computed once per index, cached). */
const DestLaw& destLawOf (size_t paramIndex) noexcept;

/** The plain value of the parameter after `delta` (in control units) was added in the control domain. */
float modulatedValue (const ParamInfo& info, const DestLaw& law, float base, float delta) noexcept;

struct CompiledSlot
{
    int source = 0, aux = 0, dest = -1;   // dest = table index
    float amount = 0.0f, power = 0.0f, auxPower = 0.0f, auxOut = 1.0f, width = 1.0f;
    bool bipolar = false, auxInv = false, stereo = false;
    bool global = false;                  // the destination is not a per voice parameter
};

class ModMatrix
{
public:
    /** Reads the 64 slots from the parameter snapshot. */
    void compile (const float* base) noexcept;

    int size() const noexcept { return count; }

    /** Adds the modulation of every slot of the requested scope to delta[] (table index) and records the touched entries. */
    void evaluate (const float* src, bool global, float* delta, int* touched, int& numTouched, int maxTouched, float* deltaRight = nullptr) const noexcept;

private:
    CompiledSlot slots[kNumModSlots];
    int count = 0;
};
} // namespace own
} // namespace synthyboy
