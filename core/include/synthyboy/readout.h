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
    static constexpr int kMaxSources = 48;   // >= kNumModSources
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
    /** The modulated value of the subscribed knobs, in plain units of the parameter (the order of setReadoutTargets); the plain value itself for a knob nothing modulates. */
    int numTargets = 0;
    float target[kMaxTargets] = {};
};
} // namespace synthyboy
