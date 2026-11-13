// SynthyBoy - own engine: the voice (oscillators, sub, noise, two filters, six envelopes, ten LFOs, modulation) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "../dsp/util.h"
#include "synthyboy/quality.h"
#include "../env_lfo/envelope.h"
#include "../env_lfo/lfo.h"
#include "../matrix/mod_matrix.h"
#include "../osc/content_set.h"
#include "../osc/lib_osc.h"
#include "../osc/wt_osc.h"
#include "own_content.h"
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
    const float* channelBendSemis = nullptr; // [16] the effective bend of the channel in semitones (MPE: manager + member channel)
    const float* tunedNotes = nullptr;    // [128] the tuned note (fractional MIDI note, A4 = 69) of every key now (scale, tuning, A4, MTS-ESP)
    const float* channelModWheel = nullptr;  // [16] 0 .. 1
    const float* channelAftertouch = nullptr; // [16] 0 .. 1
    const float* lfoPhase = nullptr;      // [10] free running phases in cycles (global instance)
    const float* rlfoValue = nullptr;     // [4] global random LFO values -1 .. 1
    int activeVoices = 0;
    float concertA = 440.0f;
    const OwnContent* lfoContent = nullptr;  // custom LFO shapes, modulation curves
    const LfoCore* monoCore = nullptr;    // [10] the shared instance of the mono LFOs (sync types 2 .. 5)
    double songSeconds = 0.0;             // the host song position in seconds (the clock of the HOST lock)
    int maxUnison = kMaxUnisonVoices;
    const ContentSet* content = nullptr;     // decoded content of the state (user tables, samples, instruments, curves); never null once the engine runs
    int quality = 2;                         // 0 low, 1 medium, 2 high: the interpolation tier of the sample types
    MorphCache* morphCache = nullptr;        // the engine's cache of morph cycles (may be null)
    bool lowShareMorph = false;              // tier low: the unison voices share one spectral morph cycle (OwnEngine::setLowTierMorphSpread (false))
    uint32_t baseEpoch = 0;                  // changes whenever `base` was rebuilt from the parameters (a voice may keep what it derived from them while it is the same)
    uint64_t usedSources = ~uint64_t (0);    // bit k: some modulation slot (or custom shape point) reads source id k; the economy mode of tier low updates the LFOs nothing reads less often
    bool oversample = false;                 // ultra render / quality.oversample_4x: every wavetable oscillator runs 4x oversampled
    spectral::Scratch* spectralScratch = nullptr;
    uint32_t* chokeLatest = nullptr;         // [3][256] choke groups of the multisample oscillators (engine wide per slot)
    uint32_t* chokeCounter = nullptr;        // [3]
    const int* switchKey = nullptr;          // [3] last key switch of the multisample instrument of each oscillator, -1 = default
};

struct NoteStart
{
    int key = 60, channel = 0;
    int32_t noteId = -1;
    float velocity = 1.0f;
    float tunedNote = 60.0f;     // the tuned note of the key (fractional MIDI note)
    float glideFromSemis = 0.0f; // pitch to glide from (absolute semitones), only if glide
    int playedKey = -1;          // the key as the player pressed it (key is the one after the scale conforming)
    uint32_t sequence = 1;       // the note counter of the voice control (starts at 1, restarts at all sounds off)
    bool glide = false;
    bool retrigger = true;
    uint32_t serial = 0;         // note counter (random seeds, alternation)
};

/** Where a voice adds its sound: the Main bus, Bus 1, Bus 2 and the Direct output, left and right (the buffers of the engine, one control period long). */
struct MixOut
{
    float* l[4];
    float* r[4];
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
    int multisampleZone (int osc) const noexcept { return lib[osc].lastZone(); }
    int multisampleKey (int osc) const noexcept { return lib[osc].lastKey(); }

    bool isActive() const noexcept { return active; }
    bool isReleased() const noexcept { return released; }
    int key() const noexcept { return keyNum; }
    int playedKey() const noexcept { return played; }
    bool killing() const noexcept { return killFading; }
    /** Kill mode (document 01, 3.2): the envelopes fade to zero linearly in 50 ms, then the voice ends. */
    void startKill() noexcept { if (active && !killFading) { killFading = true; killElapsed = 0.0f; } }
    /** Poly legato: the envelopes continue at the stage, position and value of the held voice's envelopes. */
    void inheritEnvelopes (const Voice& from) noexcept { for (int i = 0; i < 6; ++i) env[i] = from.env[i]; }
    bool envelopeHeld() const noexcept { return active && !released && !env[0].finished(); }
    void setExpressionXRaw (float v) noexcept { exprX = v; }
    float tunedKeyNote() const noexcept { return tunedKey; }
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
    /** The values of all modulation sources of the last period (index = source id, 0 .. 1). */
    const float* sourceValues() const noexcept { return src; }
    /** Read-out: stage and position (0 .. 1) of envelope e, the cycle position of LFO l, and a parameter as this voice hears it. */
    void envReadout (int e, float& stage, float& position) const noexcept
    {
        stage = static_cast<float> (env[e].stageCode());
        const int st = env[e].stageCode();
        const float len = st == 1 ? envLen[e][0] : (st == 2 ? envLen[e][1] : (st == 3 ? envLen[e][2] : (st == 4 ? envLen[e][3] : 0.0f)));
        position = len > 0.0f ? clamp01 (static_cast<float> (env[e].stageSeconds()) / len) : 0.0f;
    }
    float lfoReadout (int l) const noexcept { return lfoCore[l].offset - std::floor (lfoCore[l].offset); }
    float modulatedParam (int idx) const noexcept { return gl != nullptr ? P (idx) : 0.0f; }

    /** Advances envelopes, LFOs and the matrix by one control period and computes the parameters of the period. */
    void tick (const TickGlobals& g, int voiceIndex) noexcept;
    /** Adds samples t0 .. t0 + n - 1 of the current period to L / R. */
    void render (const MixOut& out, int n, int t0) noexcept;

private:
    // the value of a parameter as this voice hears it: the knob, or the knob plus the modulation of the slots (inline fast path: most parameters are not modulated)
    float P (int idx) const noexcept
    {
        const float d = modP[idx];
        return d == 0.0f ? gl->base[idx] : modulatedSlow (idx, gl->base[idx], d);
    }
    float PR (int idx) const noexcept // the right lane (differs from P () where a slot has the stereo flag)
    {
        const float d = modRP[idx];
        return d == 0.0f ? gl->base[idx] : modulatedSlow (idx, gl->base[idx], d);
    }
    float modulatedSlow (int idx, float base, float delta) const noexcept;
    void clearMod() noexcept;
    float lfoValue (int l, const TickGlobals& g, float& valueRight, int steps = 1) noexcept;
    float customShape (const CompiledShape& cs, float u) noexcept;

    const TickGlobals* gl = nullptr;
    double sr = 48000.0;
    bool active = false, released = false;
    int keyNum = 60, chan = 0, played = 60;
    float tunedKey = 60.0f, tuneTrack = 60.0f;
    bool killFading = false;
    float killElapsed = 0.0f;
    bool legatoInv[6] = {};
    uint32_t sequence = 1;
    float vcRand[4] = {};  // the draws of the voice control: pan, cutoff, env, detune (-1 .. 1), fixed per note
    // the values of the voice control for this note (evaluated in every tick: edits reach sounding notes)
    struct VoiceCtl { float pan = 0, detuneSemis = 0, cutoffSemis = 0, envFactor = 1, lfoFactor = 1, mod1 = 0, mod2 = 0, index = 0; bool mask[5] = { true, true, true, true, true }; } vc;
    void evaluateVoiceControl (const TickGlobals& g) noexcept;
    int32_t id = -1;
    uint32_t noteSerial = 0;
    float vel = 1.0f, releaseVel = 0.0f, tuneExpr = 0.0f, polyPressure = 0.0f, slide = 0.0f, exprX = 0.0f;
    float glideOffset = 0.0f, glideTime = 0.0f, glideElapsed = 0.0f, glideSlope = 0.0f; // semitones left, seconds
    float lastSemis = 60.0f;
    float tuneCoefDt = 0.0f, tuneCoef = 0.0f; // 1 - exp (-period / 20 ms) for the period tuneCoefDt
    Rng rng;
    float randomPerNote = 0.0f, rand2 = 0.0f, randDiscrete = 0.0f;
    int altParity = 0;
    int index = 0;

    Envelope env[6];
    float envOut[6] = {};
    LfoCore lfoCore[10];
    float envLen[6][4] = {};
    // what the voice derived from the parameters alone (no modulation touched): valid while the snapshot epoch, the note sequence and the tempo are the same
    EnvSettings envCache[6];
    uint32_t envCacheEpoch = 0, vcCacheEpoch = 0, vcCacheSequence = 0;
    double envCacheBpm = 0.0;
    bool envCacheValid = false, vcCacheValid = false;
    float lfoAge[10] = {}, lfoSmoothL[10] = {}, lfoSmoothR[10] = {};
    float lfoOutR[10] = {};
    float srcR[kNumSourceIds] = {};
    SlotState slotState[kNumModSlots];
    float voiceAge = 0.0f;
    uint32_t tickCount = 0;   // control periods of this note so far (the phase of the decimation of unused LFOs)
    shapes::ShapeData scratchShape;
    RandomLfo rlfo[4], rlfoR[4];
    float rlfoOut[4] = {}, rlfoOutR[4] = {};
    float lfoOut[10] = {};

    std::vector<float> mod, modR;
    float* modP = nullptr;   // mod.data () / modR.data (): the vectors are sized in prepare () and never reallocated
    float* modRP = nullptr;
    int touched[kNumModSlots] = {};
    int numTouched = 0;
    float src[kNumSourceIds] = {};

    // sound sources
    WtOsc osc[3], sub;
    LibOsc lib[3];
    float remapCur[3] = {};
    bool remapInit[3] = {};
    float pitchTap[4] = {};   // pitch of oscillators 1 .. 3 and the sub of the last period (Ratio SRC)
    bool pitchTapOn[4] = {};
    OscTick oscTick[3], subTick;
    float oscMono[3][kControlPeriod] = {};
    float extMono[4][kControlPeriod] = {};   // Noise, Sub, Filter 1, Filter 2 of the last chunk: the modulators of the warps from those sources
    float noiseState[2][4] = {}; // left and right generator
    Rng noiseRng;
    sampleosc::SampleVoice noiseSv;   // noise.type = Sample: the content sample of the noise source
    const ContentSet* noiseBound = nullptr;
    bool noiseUseSample = false;
    float noiseFilt[2][2] = {}, noiseLp[2][4] = {}; // noise.filter poles, generator pitch low pass
    float noiseFilterAmount = 0.0f, noisePitchSemis = 0.0f;
    float noiseGain = 0.0f, noiseGainPrev = 0.0f, noisePan = 0.0f, noiseStereo = 0.0f;
    int noiseColour = 0;
    struct Chan { float level = 0, levelPrev = 0, gl = 0, gr = 0, glPrev = 0, grPrev = 0; int dest = 3; bool env1 = true, on = false; };
    Chan subCh, noiseCh;
    int oscDest[3] = { 0, 0, 0 };
    float oscBlend[3] = {}, oscSend[3][2] = {}, subBlend = 0.0f, subSend[2] = {}, noiseBlend = 0.0f, noiseSend[2] = {}, filtSend[2][2] = {};
    bool oscEnv1[3] = { true, true, true };
    bool oscOn[3] = { true, false, false };
    FilterUnit filt[2];
    FilterSettings filtSet[2];
    bool filtOn[2] = { true, false };
    bool filtStale[2] = { false, false };
    bool filtEnv1[2] = { true, true };
    int filtDest[2] = { 0, 0 };
    float filtLevel[2] = { 1.0f, 1.0f }, filtLevelPrev[2] = { 1.0f, 1.0f };
    int routing = 0;
    float envStart = 0.0f;
    float ampPrev = 0.0f, ampCur = 0.0f, velGain = 1.0f, velGainPrev = 1.0f;
    float srcLevel[7] = {};
    float outL = 0.0f, outR = 0.0f, lastLevel = 0.0f;
    float declickL = 0.0f, declickR = 0.0f;
    int declickPos = 0;
    bool fresh = true;
    float glideStart = 0.0f;
};
} // namespace own
} // namespace synthyboy
