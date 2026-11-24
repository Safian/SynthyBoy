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
    /** The same envelopes, finer (zero from an engine without the finer stages): stage 0 idle, 1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release, 7 finished or killed, and the
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
    /** The step of the voice control (0 .. 7) each of the newest voices (newest first) plays, -1 = no voice: the highlighted step column of the Voice page (own engine only). */
    float voiceStep[kReadVoices] = {};
    float noisePos = -1.0f;
    /** The envelopes of the newest voices (newest first, kReadVoices, own engine only): the fine stage (0 idle, 1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release, 7 finished or killed) and the fraction 0 .. 1 of that stage
        that has run, of envelope n + 1 in voice v; stage 0 for a slot without a voice. The running dots of the envelope display (one per sounding voice). */
    float envVoiceStage[kNumEnvs][kReadVoices] = {};
    float envVoiceFraction[kNumEnvs][kReadVoices] = {};
    /** The cycle position (as lfoPhase: 0 .. 1, the x of the cursor of the LFO display) of LFO n + 1 in the newest voices (newest first, own engine only); only the first numLfoVoices voices are valid. The running cursors of the LFO display (one per sounding voice). */
    float lfoVoicePhase[kNumLfos][kReadVoices] = {};
    int numLfoVoices = 0;
    /** The level meters of the mixer (zero from an engine without meters): per strip (Sub, A, B, C, Noise, Filter 1, Filter 2, Bus 1, Bus 2, Master) the peak of the signal on a scale of 60 dB:
        0 = -60 dBFS .. 1 = full scale, with a fast attack and a release of about 300 ms. The sources are measured at their own output (the power sum of the voices), the buses before their racks, the master at
        the output of the engine. Written by the audio thread into atomics at the end of a block, read lock free by the display thread. */
    static constexpr int kMaxStrips = 10;
    static constexpr int kMasterStrip = kMaxStrips - 1;
    float stripPeak[kMaxStrips] = {};
    // True stereo sample peaks (polyphonic sources are summed before peak detection).
    // Source/filter taps: post gain/pan, before route/ENV1/Mute; buses: post rack/level/Mute;
    // Master: final stereo output after safety/Mute, before optional mono fold.
    // Peak/hold use -60..0 dBFS normalized display values; Clip is a two-second signal latch.
    float stripPeakL[kMaxStrips] = {}, stripPeakR[kMaxStrips] = {};
    float stripHoldL[kMaxStrips] = {}, stripHoldR[kMaxStrips] = {};
    float stripClipL[kMaxStrips] = {}, stripClipR[kMaxStrips] = {};
    // Output levels before the mono fold, with the same scale and release as stripPeak.
    float masterPeakL = 0.0f, masterPeakR = 0.0f;
    int numTargets = 0;
    float target[kMaxTargets] = {};
};
} // namespace synthyboy
