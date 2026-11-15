// SynthyBoy - core: the live values the engine hands to the display (GROUPA2 WP-17, docs/ui-design/13-new-keys.md 4.32).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The audio thread writes them at the end of every block (relaxed atomics, no lock, no allocation) like the scope of the shell; the UI copies them on a timer.
// Read only: nothing here changes a sound, there is no capability bit and no key.
#pragma once

#include <cstdint>

#include "modulation.h"

namespace synthyboy
{
struct LiveValues
{
    static constexpr int kMaxTargets = 96;   // knobs that show their modulated value at the same time
    static constexpr int kMaxSources = 64;   // >= kNumModSources
    static constexpr int kNumLfos = 10;
    static constexpr int kNumEnvs = 6;

    uint32_t block = 0;                      // counts the blocks that wrote the values (0: the engine wrote nothing yet)
    int activeVoices = 0;
    /** The value of every modulation source (index k = choice value k + 1): the voice that was played last for a per voice source, the one value for a global
        source. LFOs swing around 0 (-1 .. 1); envelopes, macros, velocity, the mod wheel ... are 0 .. 1, and so is the pitch wheel (0.5 in the middle: the engine's own scale of that
        source). */
    float source[kMaxSources] = {};
    /** Position in the cycle of LFO n + 1 (0 .. 1) for the voice that is heard; -1 when no voice sounds. */
    float lfoPhase[kNumLfos] = {};
    /** Stage of envelope n + 1 for the voice that is heard: 0 idle, 1 attack (delay included), 2 hold, 3 decay / sustain, 4 release, 5 killed; with the position inside the
        stage (0 .. 1). */
    float envStage[kNumEnvs] = {};
    float envPosition[kNumEnvs] = {};
    /** The same envelopes, finer (own engine only; zero from the Vital engine): stage 0 idle, 1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release, 7 finished or killed, and the
        fraction 0 .. 1 of that stage that has run (the sustain has none: 0). */
    float envFineStage[kNumEnvs] = {};
    float envFineFraction[kNumEnvs] = {};
    /** The modulated value of the subscribed knobs, in plain units of the parameter (the order of setReadoutTargets); the plain value itself for a knob nothing modulates. */
    /** GROUPC WP-13: the zone (index into the SFZ regions of oscillator n + 1) and the key of the last note of a multisample oscillator; -1 before one was played. */
    int multisampleZone[3] = { -1, -1, -1 };
    int multisampleKey[3] = { -1, -1, -1 };
    /** The read head (0 .. 1 of the file) of the sample oscillator n + 1 for the voice that is heard; -1 when no voice plays it or the oscillator is of another type. Own engine only. */
    float sampleHead[3] = { -1.0f, -1.0f, -1.0f };
    /** The read-outs of the sample based oscillators and the sub (own engine only; fixed size, written without locks by the audio thread at the end of a block). Voices are the newest active ones (at most
        kReadVoices, newest first). voicePos[n][v]: where the oscillator n + 1 reads its file in voice v (sample type: the read head; multisample: the head of the zone being played; granular: the play head of
        the cloud), 0 .. 1; -1 = that voice does not play it. The grains (granular oscillator n + 1, the voice that is heard: its first cloud, at most kReadGrains): where each reads (0 .. 1 of the file) and how
        far it has run (0 .. 1 of its length). subPhase[v]: the phase of the sub oscillator of voice v (0 .. 1), -1 = no voice. noisePos: the read head of the noise when its source is a sample, else -1. */
    static constexpr int kReadVoices = 8, kReadGrains = 32;
    float voicePos[3][kReadVoices] = {};
    int numGrains[3] = {};
    float grainPos[3][kReadGrains] = {};
    float grainLife[3][kReadGrains] = {};
    float subPhase[kReadVoices] = {};
    float noisePos = -1.0f;
    int numTargets = 0;
    float target[kMaxTargets] = {};
};
} // namespace synthyboy
