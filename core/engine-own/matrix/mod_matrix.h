// SynthyBoy - own engine: the modulation matrix (64 slots, sources, aux source) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include "../voice/own_content.h"
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
float modulatedValue (const ParamInfo& info, const DestLaw& law, float base, float delta, bool fast = false) noexcept;

struct CompiledSlot
{
    int source = 0, aux = 0, dest = -1;   // dest = table index
    float amount = 0.0f, power = 0.0f, auxPower = 0.0f, auxOut = 1.0f, width = 1.0f;
    bool bipolar = false, auxInv = false, stereo = false;
    bool global = false;                  // the destination is not a per voice parameter
    bool amountDest = false;              // the destination is the amount of another slot (modNN.amount)
    int amountSlot = -1;                  // ... of this slot (0 .. 63)
    int index = 0;                        // this slot's number (0 .. 63)
    float riseMs = 0.0f, fallMs = 0.0f;   // slew of the source
    float delayMs = 0.0f, delayBeats = 0.0f;
    bool delaySync = false;
    const CompiledModCurve* curve = nullptr;
};

/** The per voice state of the slots (slew, delay timer): reset at a note start. */
struct SlotState
{
    float heldL = 0.0f, heldR = 0.0f;
    bool init = false;
};

struct EvalContext
{
    const float* src = nullptr;       // source values, left lane (index = source id)
    const float* srcR = nullptr;      // right lane (nullptr = the same)
    bool global = false;              // evaluate the global slots (destinations that are not voice parameters)
    float* delta = nullptr;           // control domain sums per table index (left lane)
    float* deltaRight = nullptr;      // right lane or nullptr
    int* touched = nullptr;
    int* numTouched = nullptr;
    int maxTouched = 0;
    SlotState* state = nullptr;       // per voice slot state or nullptr
    float dt = 0.0f;                  // the control period in seconds
    double bpm = 120.0;
    float voiceAge = 0.0f;            // seconds since the note start
};

class ModMatrix
{
public:
    /** Reads the 64 slots from the parameter snapshot. */
    void compile (const float* base) noexcept;

    int size() const noexcept { return count; }
    /** Bit k: some compiled slot reads source id k (as source or aux). `globalSourceMask`: the same for the slots whose destination is not a voice parameter. */
    uint64_t sourceMask() const noexcept { return srcMask; }
    uint64_t globalSourceMask() const noexcept { return globalSrcMask; }
    void setContent (const OwnContent* c) noexcept { content = c; }

    /** Adds the modulation of every slot of the requested scope to delta[] (table index) and records the touched entries. */
    void evaluate (const EvalContext& ctx) const noexcept;

private:
    CompiledSlot slots[kNumModSlots];
    int count = 0;
    uint64_t srcMask = 0, globalSrcMask = 0;
    const OwnContent* content = nullptr;
};
} // namespace own
} // namespace synthyboy
