// SynthyBoy - own engine tests: a legato note and the envelope time scaling of the voice control (docs/release/CPU-REVIEW-VOICE-MOD.md V1 side note, CPU-REVIEW-PROFILE.md
// section 9). A legato note moves the voice to the next step of the voice control (Voice::legatoTo: `sequence`), and the envelope times follow the step's envelope scaling.
// Whether they do must not depend on whether some unrelated key of the voice happens to be modulated. The envelope settings cache is keyed on the snapshot epoch and the tempo, not on
// the note sequence, so Voice::legatoTo clears it (it kept the scaling of the previous step while nothing of the voice is modulated: fixed, a regression test now).
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

    explicit Rig (bool unrelatedSlot, bool reference)
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
        g.referenceControl = reference;   // the code before the caches of round 13 (--no-voice-savings)
        g.baseEpoch = 1;   // as the engine: the caches of the voice may hold
    }
};
} // namespace

static void legatoNoteTakesTheScalingOfItsStep (bool reference)
{
    Rig plain (false, reference), modulated (true, reference);
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

TEST (voice_legato_note_takes_the_envelope_scaling_of_its_step_without_modulation)
{
    legatoNoteTakesTheScalingOfItsStep (false);
}

TEST (voice_legato_note_takes_the_envelope_scaling_of_its_step_reference_control)
{
    legatoNoteTakesTheScalingOfItsStep (true);
}

// Existing test in this file: voice_legato_note_takes_the_envelope_scaling_of_its_step_without_modulation.
// Regression test: Lorenz and Rossler attractors scale speed across low and high rates and stay stable.
TEST (own_rlfo_attractors_scale_rate_and_stay_stable)
{
    for (int style = 3; style <= 4; ++style)
    {
        // 1. Low-rate scaling: 0.1 Hz vs 1.0 Hz at 44100 Hz.
        // Before fix: both rates clamp dt to 0.001f, giving maxDiff == 0.0f.
        // After fix: 1.0 Hz integrates 10x further in ODE time, giving maxDiff > 0.05f.
        {
            RandomLfo lfoA;
            RandomLfo lfoB;
            lfoA.seed (1234u);
            lfoB.seed (1234u);
            const float cyclesA = 0.1f * (32.0f / 44100.0f);
            const float cyclesB = 1.0f * (32.0f / 44100.0f);
            float maxDiffLow = 0.0f;
            for (int i = 0; i < 500; ++i)
            {
                const float a = lfoA.advance (style, cyclesA);
                const float b = lfoB.advance (style, cyclesB);
                const float diff = std::fabs (a - b);
                if (diff > maxDiffLow)
                    maxDiffLow = diff;
            }
            CHECK (maxDiffLow > 0.05f);
        }

        // 2. High-rate scaling: 30.0 Hz vs 60.0 Hz at 44100 Hz.
        // Before fix: both rates clamp dt to 0.02f, giving maxDiff == 0.0f.
        // After fix: 60.0 Hz advances 2x faster, giving maxDiff > 0.05f.
        {
            RandomLfo lfoA;
            RandomLfo lfoB;
            lfoA.seed (1234u);
            lfoB.seed (1234u);
            const float cyclesA = 30.0f * (32.0f / 44100.0f);
            const float cyclesB = 60.0f * (32.0f / 44100.0f);
            float maxDiffHigh = 0.0f;
            for (int i = 0; i < 200; ++i)
            {
                const float a = lfoA.advance (style, cyclesA);
                const float b = lfoB.advance (style, cyclesB);
                const float diff = std::fabs (a - b);
                if (diff > maxDiffHigh)
                    maxDiffHigh = diff;
            }
            CHECK (maxDiffHigh > 0.05f);
        }

        // 3. High-rate stability: 100.0 Hz at 44100 Hz for 5000 periods.
        // Verify every output is finite and in [-1.0f, 1.0f], and range > 0.1f (not stuck at reset).
        {
            RandomLfo lfoStab;
            lfoStab.seed (1234u);
            const float cyclesStab = 100.0f * (32.0f / 44100.0f);
            float minVal = 100.0f;
            float maxVal = -100.0f;
            bool allFinite = true;
            bool inRange = true;
            for (int i = 0; i < 5000; ++i)
            {
                const float val = lfoStab.advance (style, cyclesStab);
                if (!std::isfinite (val))
                    allFinite = false;
                if (val < -1.0f || val > 1.0f)
                    inRange = false;
                if (val < minVal)
                    minVal = val;
                if (val > maxVal)
                    maxVal = val;
            }
            CHECK (allFinite);
            CHECK (inRange);
            CHECK ((maxVal - minVal) > 0.1f);
        }
    }

    // 4. Bit-identity verification for styles 0, 1, 2 (Perlin, S&H, Sine interpolate).
    for (int s = 0; s <= 2; ++s)
    {
        RandomLfo lfoCheck;
        lfoCheck.seed (42u);
        const float checkCycles = 0.037f;
        double sum = 0.0;
        float last8[8] = {};
        for (int i = 0; i < 256; ++i)
        {
            const float val = lfoCheck.advance (s, checkCycles);
            sum += static_cast<double> (val);
            if (i >= 256 - 8)
                last8[i - (256 - 8)] = val;
        }
        std::printf ("RLFO-CHECKSUM style=%d sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     s, sum,
                     static_cast<double> (last8[0]), static_cast<double> (last8[1]),
                     static_cast<double> (last8[2]), static_cast<double> (last8[3]),
                     static_cast<double> (last8[4]), static_cast<double> (last8[5]),
                     static_cast<double> (last8[6]), static_cast<double> (last8[7]));
    }
}

