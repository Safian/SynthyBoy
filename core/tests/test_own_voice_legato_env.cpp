// SynthyBoy - own engine tests: a legato note and the envelope time scaling of the voice control (docs/release/CPU-REVIEW-VOICE-MOD.md V1 side note, CPU-REVIEW-PROFILE.md
// section 9). A legato note moves the voice to the next step of the voice control (Voice::legatoTo: `sequence`), and the envelope times follow the step's envelope scaling.
// Whether they do must not depend on whether some unrelated key of the voice happens to be modulated. This test is EXPECTED TO FAIL on the code as it is: the envelope
// settings cache (keyed on the snapshot epoch and the tempo, not on the note sequence) keeps the scaling of the previous step while nothing of the voice is modulated.
// It is not registered with CTest (core/tests/CMakeLists.txt: synthyboy_test_own_voice_legato_env) until the behaviour is fixed (an owner decision: the fix changes output).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "voice/param_index.h"
#include "voice/voice.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kSr = 48000.0;

int idxOf (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    return p != nullptr ? findParamIndex (p->id) : -1;
}

/** One voice; envelope 1 attack 1 s, the voice control alternates the envelope scaling between x1 (step 1) and x4 (step 2). `unrelatedSlot`: one modulation slot that
    moves the tune of oscillator 3, which is off (nothing of the envelopes, nothing audible). */
struct Rig
{
    Voice v;
    std::vector<float> base;
    ModMatrix matrix;
    TickGlobals g;

    explicit Rig (bool unrelatedSlot)
    {
        v.prepare (kSr);
        base.assign (kMaxParams, 0.0f);
        for (size_t i = 0; i < paramCount(); ++i)
            base[i] = paramTable()[i].def;
        base[static_cast<size_t> (idxOf ("env1.attack"))] = 1000.0f;
        base[static_cast<size_t> (idxOf ("voicectl.length"))] = 2.0f;
        base[static_cast<size_t> (idxOf ("voicectl.s1.env"))] = 0.0f;
        base[static_cast<size_t> (idxOf ("voicectl.s2.env"))] = 100.0f;   // 4 ^ 1: four times the times
        if (unrelatedSlot)
        {
            const ModSlotParams& sp = paramIndex().slot[0];
            base[static_cast<size_t> (sp.source)] = static_cast<float> (kSrcFixed);
            base[static_cast<size_t> (sp.destination)] = static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (idxOf ("osc3.tune"))));
            base[static_cast<size_t> (sp.amount)] = 50.0f;
        }
        matrix.compile (base.data());
        g.base = base.data();
        g.matrix = &matrix;
        g.sampleRate = kSr;
        g.bpm = 120.0;
        g.baseEpoch = 1;   // as the engine: the caches of the voice may hold
    }
};
} // namespace

TEST (voice_legato_note_takes_the_envelope_scaling_of_its_step_without_modulation)
{
    Rig plain (false), modulated (true);
    NoteStart n1;
    n1.key = 60;
    n1.tunedNote = 60.0f;
    n1.sequence = 1;
    NoteStart n2 = n1;
    n2.key = 64;
    n2.tunedNote = 64.0f;
    n2.sequence = 2;
    for (Rig* r : { &plain, &modulated })
    {
        r->v.start (n1, 0.0f, 0.0f);
        for (int t = 0; t < 20; ++t)
            r->v.tick (r->g, 0);
        r->v.legatoTo (n2);
    }
    int firstDiff = -1;
    float a = 0.0f, b = 0.0f;
    for (int t = 0; t < 200 && firstDiff < 0; ++t)
    {
        plain.v.tick (plain.g, 0);
        modulated.v.tick (modulated.g, 0);
        a = plain.v.sourceValues()[kSrcEnv1];
        b = modulated.v.sourceValues()[kSrcEnv1];
        if (a != b)
            firstDiff = t;
    }
    if (firstDiff >= 0)
        std::printf ("envelope 1 after the legato note differs %d periods after it: %.9g without modulation, %.9g with an unrelated slot\n", firstDiff, static_cast<double> (a),
                     static_cast<double> (b));
    CHECK_EQ (firstDiff, -1);
}
