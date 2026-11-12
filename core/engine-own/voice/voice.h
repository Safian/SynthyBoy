// SynthyBoy - own engine: the voice (oscillators, sub, noise, two filters, six envelopes, ten LFOs, modulation) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "../dsp/util.h"
#include "synthyboy/quality.h"
#include "../env_lfo/envelope.h"
#include "../env_lfo/lfo.h"
#include "../matrix/mod_matrix.h"
#include "../osc/wt_osc.h"
#include "filter_unit.h"
#include "param_index.h"

namespace synthyboy
{
namespace own
{
/** What the engine hands to every voice at the start of a control period. */
struct TickGlobals
{
    const float* base = nullptr;          // plain values of all parameters (snapshot of the block)
    const ModMatrix* matrix = nullptr;
    double sampleRate = 48000.0;
    double bpm = 120.0, songBeats = 0.0;
    bool playing = false;
    float macro[8] = {};                  // 0 .. 1
    const float* channelBendSemis = nullptr; // [16]
    const float* channelModWheel = nullptr;  // [16] 0 .. 1
    const float* channelAftertouch = nullptr; // [16] 0 .. 1
    const float* lfoPhase = nullptr;      // [10] free running phases in cycles (global instance)
    const float* rlfoValue = nullptr;     // [4] global random LFO values -1 .. 1
    int activeVoices = 0;
    float concertA = 440.0f;
    int maxUnison = kMaxUnisonVoices;
};

struct NoteStart
{
    int key = 60, channel = 0;
    int32_t noteId = -1;
    float velocity = 1.0f;
    float glideFromSemis = 0.0f; // pitch to glide from (absolute semitones), only if glide
    bool glide = false;
    bool retrigger = true;
    uint32_t serial = 0;         // note counter (random seeds, alternation)
};

class Voice
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void start (const NoteStart& n, float lastOutL, float lastOutR) noexcept;
    /** Legato: a new key on a sounding voice (no envelope retrigger): glide from the old pitch. */
    void legatoTo (const NoteStart& n) noexcept;
    void release (float velocity) noexcept;
    void kill() noexcept;

    bool isActive() const noexcept { return active; }
    bool isReleased() const noexcept { return released; }
    int key() const noexcept { return keyNum; }
    int channel() const noexcept { return chan; }
    int32_t noteId() const noexcept { return id; }
    uint32_t serial() const noexcept { return noteSerial; }
    float velocity() const noexcept { return vel; }
    float level() const noexcept { return lastLevel; }
    float lastL() const noexcept { return outL; }
    float lastR() const noexcept { return outR; }
    void setTuningExpression (float semis) noexcept { tuneExpr = semis; }
    void setPressure (float v) noexcept { polyPressure = v; }
    void setSlide (float v) noexcept { slide = v; }
    void setExpressionX (float v) noexcept { exprX = v; }
    float currentSemitones() const noexcept { return lastSemis; }

    /** Advances envelopes, LFOs and the matrix by one control period and computes the parameters of the period. */
    void tick (const TickGlobals& g, int voiceIndex) noexcept;
    /** Adds samples t0 .. t0 + n - 1 of the current period to L / R. */
    void render (float* L, float* R, int n, int t0) noexcept;

private:
    float P (int idx) const noexcept;
    void clearMod() noexcept;
    float lfoValue (int l, const TickGlobals& g) noexcept;

    const TickGlobals* gl = nullptr;
    double sr = 48000.0;
    bool active = false, released = false;
    int keyNum = 60, chan = 0;
    int32_t id = -1;
    uint32_t noteSerial = 0;
    float vel = 1.0f, releaseVel = 0.0f, tuneExpr = 0.0f, polyPressure = 0.0f, slide = 0.0f, exprX = 0.0f;
    float glideOffset = 0.0f, glideTime = 0.0f, glideElapsed = 0.0f, glideSlope = 0.0f; // semitones left, seconds
    float lastSemis = 60.0f;
    Rng rng;
    float randomPerNote = 0.0f, rand2 = 0.0f, randDiscrete = 0.0f;
    int altParity = 0;
    int index = 0;

    Envelope env[6];
    float envOut[6] = {};
    struct LfoState
    {
        float phase = 0.0f, smoothed = 0.0f, age = 0.0f, value = 0.0f;
        bool oneShotDone = false;
    } lfo[10];
    RandomLfo rlfo[4];
    float rlfoOut[4] = {};
    float lfoOut[10] = {};

    std::vector<float> mod;
    int touched[kNumModSlots] = {};
    int numTouched = 0;
    float src[kNumSourceIds] = {};

    // sound sources
    WtOsc osc[3], sub;
    OscTick oscTick[3], subTick;
    float oscMono[3][kControlPeriod] = {};
    float noiseState[2][4] = {}; // left and right generator
    Rng noiseRng;
    float noiseGain = 0.0f, noiseGainPrev = 0.0f, noisePan = 0.0f, noiseStereo = 0.0f;
    int noiseColour = 0;
    struct Chan { float level = 0, levelPrev = 0, gl = 0, gr = 0, glPrev = 0, grPrev = 0; int dest = 3; bool env1 = true, on = false; };
    Chan subCh, noiseCh;
    int oscDest[3] = { 0, 0, 0 };
    bool oscEnv1[3] = { true, true, true };
    bool oscOn[3] = { true, false, false };
    FilterUnit filt[2];
    FilterSettings filtSet[2];
    bool filtOn[2] = { true, false };
    bool filtEnv1[2] = { true, true };
    int filtDest[2] = { 0, 0 };
    float filtLevel[2] = { 1.0f, 1.0f }, filtLevelPrev[2] = { 1.0f, 1.0f };
    int routing = 0;
    float ampPrev = 0.0f, ampCur = 0.0f, velGain = 1.0f, velGainPrev = 1.0f;
    float srcLevel[7] = {};
    float outL = 0.0f, outR = 0.0f, lastLevel = 0.0f;
    float declickL = 0.0f, declickR = 0.0f;
    int declickPos = 0;
    bool fadeOut = false, fresh = true;
    float glideStart = 0.0f;
    float fadeGain = 1.0f;
};
} // namespace own
} // namespace synthyboy
