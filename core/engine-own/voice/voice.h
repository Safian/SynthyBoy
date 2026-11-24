// SynthyBoy - own engine: the voice (oscillators, sub, noise, two filters, six envelopes, ten LFOs, modulation) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../dsp/noise_coeff.h"
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
/** Pad pressure to Filter 1 cutoff (MPC shell): the cutoff range of full pressure at depth 1 (semitones, the control domain of the cutoff) and the time constant of the
    one pole smoothing of the pressure stream (seconds; the pad sends about 170 values a second per note). */
constexpr float kPadPressureSemitones = 36.0f;
constexpr float kPadPressureSeconds = 0.012f;

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
    bool noSmoothing = false;             // quality.no_smoothing (Serum 2 Disable Smoothing): the per period ramps are skipped, values jump at the start of the period
    bool tuningNoteOnOnly = false;        // master.tuning.mts_note_on_only with an MTS-ESP master connected: a sounding note keeps the tuning it started with
    float lowestNote = 1000.0f;           // sub.mono: the lowest tuned key note of the sounding voices (OwnEngine::updateLowestNote)
    float concertA = 440.0f;
    const OwnContent* lfoContent = nullptr;  // custom LFO shapes, modulation curves
    const LfoCore* monoCore = nullptr;    // [10] the shared instance of the mono LFOs (sync types 2 .. 5)
    double songSeconds = 0.0;             // the host song position in seconds (the clock of the HOST lock)
    int maxUnison = kMaxUnisonVoices;
    const ContentSet* content = nullptr;     // decoded content of the state (user tables, samples, instruments, curves); never null once the engine runs
    int quality = 2;                         // 0 low, 1 medium, 2 high: the interpolation tier of the sample types
    bool scalarWarp = false;                 // measurements (kOwnSavingVectorWarp): no four lane form of the oversampled oscillator samples
    bool scalarContent = false;              // measurements (kOwnSavingContentKernels): the library oscillators run their reference code, not the block kernels
    bool scalarFilters = false;              // measurements (setOwnSavingsOff, kOwnSavingVectorFilters): no pair kernels in the voice filters
    bool referenceControl = false;           // measurements (setOwnSavingsOff, kOwnSavingVoiceControl): the control code of the voice as it was before the caches of CPU-REVIEW-VOICE-MOD.md
    uint32_t economy = 0;                    // the economy groups in force (EconomyFlags of quality.h); the three tiers: all at low, none at medium / high
    MorphCache* morphCache = nullptr;        // the engine's cache of morph cycles (may be null)
    float killSeconds = 0.05f;               // the fade of a voice that was stolen (Kill mode); tier low: OwnEngine::setLowTierKillFade
    bool lowShareMorph = false;              // tier low: the unison voices share one spectral morph cycle (OwnEngine::setLowTierMorphSpread (false))
    uint32_t baseEpoch = 0;                  // changes whenever `base` was rebuilt from the parameters (a voice may keep what it derived from them while it is the same)
    uint64_t usedSources = ~uint64_t (0);    // bit k: some modulation slot (or custom shape point) reads source id k; the economy mode of tier low updates the LFOs nothing reads less often
    bool oversample = false;                 // ultra render / quality.oversample_4x: every wavetable oscillator runs 4x oversampled
    spectral::Scratch* spectralScratch = nullptr;
    uint32_t* chokeLatest = nullptr;         // [3][256] choke groups of the multisample oscillators (engine wide per slot)
    uint32_t* chokeCounter = nullptr;        // [3]
    const int* switchKey = nullptr;          // [3] last key switch of the multisample instrument of each oscillator, -1 = default
    const Wavetable* const* wtResolved = nullptr; // [3] the built-in table each oscillator plays in this block (OwnEngine: the chosen one when built, else the one it had); null: the voice looks the choice up itself
    float padPressureToCutoff = 0.0f;        // MPC shell: depth of the pad pressure to Filter 1 cutoff layer, 0 = off, 1 = kPadPressureSemitones at full pressure
    uint32_t mixerSoloMask = 0;
    uint32_t mixerMuteMask = 0;
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
    /** The level meters of the mixer (one per strip: Sub, A, B, C, Noise, Filter 1, Filter 2): every voice ADDS the square of the peak of its source's mono output over this chunk to srcSq[strip] (a power sum: the
        voices are uncorrelated; for one voice it is exactly its peak). null: nobody listens. */
    float* srcSq = nullptr;
    // Optional fixed-size per-strip stereo sums, owned/cleared by the engine for this chunk.
    float (*meterL)[kControlPeriod] = nullptr;
    float (*meterR)[kControlPeriod] = nullptr;
};

class Voice
{
    friend struct VoiceOptTest; // core/tests/test_own_voice_opt.cpp
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
    float sampleHead (int osc) const noexcept { return lib[osc].sampleHead(); }
    /** The read-outs of the display for this voice (called by the audio thread at the end of a block): the read position of oscillator osc (sample, multisample, granular play head; -1 none), the
        grains of its first cloud, the phase of the sub oscillator (0 .. 1) and the read head of the noise when its source is a sample (-1 otherwise). */
    float readPosition (int osc) const noexcept { return lib[osc].readPosition(); }
    int readGrains (int osc, float* pos, float* life, int maxN) const noexcept { return lib[osc].readGrains (pos, life, maxN); }
    float subPhase() const noexcept { return sub.firstPhase(); }
    /** sub.phase_contiguous: the sub oscillator continues with the phase of the previous note. */
    void setSubPhase (float p) noexcept { sub.setPhase (p); }
    /** The phases of the unison voices of wavetable oscillator o (phase memory, phase legato: OwnEngine::startNote). */
    static constexpr int kPhaseCount = WtOsc::kMaxVoices;
    void oscPhases (int o, uint32_t* out) const noexcept { osc[o].getPhases (out); }
    void setOscPhases (int o, const uint32_t* in) noexcept { osc[o].setPhases (in); }
    /** The step (0 .. 7) of the voice control this voice plays: (the number of the note - 1) modulo the length. */
    /** Bit o: oscillator o (on) is a Sample / Granular / Spectral / Multisample oscillator without its file (the instance shows "content missing"). */
    uint32_t missingMask() const noexcept { return missingBits; }
    int voiceControlStep() const noexcept { return vcStep; }
    float noisePosition() const noexcept { return noiseUseSample ? noiseSv.playheadFraction() : -1.0f; }

    bool isActive() const noexcept { return active; }
    /** The engine's bit set of the active voices of its pool (CPU-REVIEW-VOICE-MOD.md V10): this voice keeps bit `index` of *mask equal to isActive (). */
    void attachPool (uint64_t* mask, int index) noexcept
    {
        poolMask = mask;
        poolBit = uint64_t (1) << index;
        setActive (active);
    }
    bool isReleased() const noexcept { return released; }
    int key() const noexcept { return keyNum; }
    int playedKey() const noexcept { return played; }
    bool killing() const noexcept { return killFading; }
    /** The last sample this voice added to the Main / Bus 1 / Bus 2 / Direct buses (left, right): where the declick ramp of a voice that is stolen at tier low starts. */
    float lastBusLeft (int target) const noexcept { return lastBusL[target]; }
    float lastBusRight (int target) const noexcept { return lastBusR[target]; }
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
    /** The voice's level for the voice-count gain (docs/release/VOICE-COUNT-GAIN-SPEC.md): envelope 1 times velocity factor. */
    float countLevel() const noexcept { return ampCur * velGain; }
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
    /** The finer read-out: stage 0 idle, 1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release, 7 done and the fraction 0 .. 1 of that stage that has run. */
    void envReadoutFine (int e, float& stage, float& fraction) const noexcept
    {
        const int st = env[e].fineStage();
        stage = static_cast<float> (st);
        const float len = st == 2 ? envLen[e][4] : (st == 3 ? envLen[e][1] : (st == 4 ? envLen[e][2] : (st == 6 ? envLen[e][3] : 0.0f)));
        fraction = env[e].fineFraction (len);
    }
    float lfoReadout (int l) const noexcept { return lfoReadPos[l] - ffloor (lfoReadPos[l]); }
    float modulatedParam (int idx) const noexcept { return gl != nullptr ? P (idx) : 0.0f; }

    /** Advances envelopes, LFOs and the matrix by one control period and computes the parameters of the period. */
    void tick (const TickGlobals& g, int voiceIndex) noexcept;
    /** Adds samples t0 .. t0 + n - 1 of the current period to L / R. */
    void render (const MixOut& out, int n, int t0) noexcept;

private:
    // the value of a parameter as this voice hears it: the knob, or the knob plus the modulation of the slots (inline fast path: most parameters are not modulated)
    float P (int idx) const noexcept { return readParam (gl->base, modP, idx); }
    float PR (int idx) const noexcept { return readParam (gl->base, modRP, idx); } // the right lane (differs from P () where a slot has the stereo flag)
    /** P () / PR () with the snapshot and the deltas given (Voice::tick keeps both in locals). The delta is tested for +-0 on its bits, (bits << 1) == 0, which is d == 0.0f for every
        float (NaN included) without a float compare and its transfer of the flags to the core (CPU-REVIEW-VOICE-MOD.md V5). */
    float readParam (const float* base, const float* delta, int idx) const noexcept
    {
        uint32_t u;
        std::memcpy (&u, delta + idx, sizeof u);
        return (u << 1) == 0u ? base[idx] : modulatedSlow (idx, base[idx], delta[idx]);
    }
    float modulatedSlow (int idx, float base, float delta) const noexcept;
    void clearMod() noexcept;
    float lfoValue (int l, const TickGlobals& g, float& valueRight, int steps = 1) noexcept;
    float lfoValueReference (int l, const TickGlobals& g, float& valueRight, int steps = 1) noexcept;   // as before the settings cache (TickGlobals::referenceControl)
    /** What an LFO derives from its keys, the tempo, the voice control and the sample rate (CPU-REVIEW-VOICE-MOD.md V3): kept per voice and LFO while those are the same and no slot touched a
        key of the LFO (the key follow rate, sync 4, reads the pitch of the voice and is computed every time from the kept keys). */
    struct LfoSettings
    {
        int shape = 0, sync = 0, syncType = 0, division = 0, dir = 0;
        float rateKnob = 0.0f, keyTranspose = 0.0f, keyTune = 0.0f, rate = 0.0f;   // rate: vc.lfoFactor * lfoRateHz (...) for sync != 4
        float knob = 0.0f, delay = 0.0f, fade = 0.0f, stereo = 0.0f, swing = 0.0f, half = 0.0f;
        bool mono = false, host = false, smooth = false;
        float dt[2] = {}, coef[2] = {};   // the period of 1 and of 4 control periods (the decimated LFOs of tier low) and 1 - exp2 (-dt / half)
        const CompiledShape* custom = nullptr;
        // the key
        bool ok = false;
        uint32_t epoch = 0, factorBits = 0, concertABits = 0;
        double bpm = 0.0, sampleRate = 0.0;
        const OwnContent* content = nullptr;
        const LfoCore* monoCore = nullptr;
    };
    LfoSettings lfoSet[10];
    /** The same for a random LFO: its keys and the rate (except the key follow rate). */
    struct RlfoSettings
    {
        int syncType = 0, sync = 0, division = 0, style = 0;
        float rateKnob = 0.0f, keyTranspose = 0.0f, keyTune = 0.0f, rate = 0.0f;
        bool stereo = false;
        bool ok = false;
        uint32_t epoch = 0, factorBits = 0, concertABits = 0;
        double bpm = 0.0;
    };
    RlfoSettings rlfoSet[4];
    uint32_t lastTouchedGroups = 0;   // touchedGroups () of the last period, as the envelopes and LFOs read it (before this period's matrix)
    /** What an oscillator that is on derives from its keys alone (CPU-REVIEW-VOICE-MOD.md V7): kept while the snapshot epoch is the one it was built with and no slot touched a key of the
        oscillator then or in this period. Everything that reads the note, the voice control, the content, the tables, the tempo or the state is computed every period from these. */
    struct OscPart
    {
        int dest = 0, wtChoice = 0, pmode = 0, ratioSrc = -1, transposeSnap = 0, voicesRaw = 1, stack = 0, stackRepeat = 0, mode = 0, law = 0, morphType = 0, remapSlot = 0;
        int warp[2] = {};
        float blend = 0, send[2] = {}, fixedNote = 0, coarseSemis = 0, transpose = 0, tune001 = 0, frame = 0, frameSpread = 0, level = 0, pan = 0, panRight = 0, detuneSemis = 0, curve = 0,
              blendShape = 0, spread = 0, uniBlend = 0, morphAmount = 0, morphSpread = 0, remapRise = 0, remapFall = 0;
        float warpAmount[2] = {}, warpPhase[2] = {}, warpSpread[2] = {};
        bool env1 = true, track = true, bendTrack = true, smooth = false, remapOn = false;
        float libV[kLibKeyCount] = {};   // the keys of the library oscillators, filled when a library type first needs them (libValid)
        bool libValid = false;
        bool ok = false;
        uint32_t epoch = 0;
    };
    OscPart oscPart[3];
    void fillOscPart (int o, OscPart& op) noexcept;
    /** The same for the sub oscillator (its keys alone). */
    struct SubPart
    {
        int dest = 0, table = 0;
        float blend = 0, send[2] = {}, octave12 = 0, tune = 0, level = 0, pan = 0;
        bool on = false, env1 = true;
        bool ok = false;
        uint32_t epoch = 0;
    };
    SubPart subPart;
    float customShape (const CompiledShape& cs, float u) noexcept;

    const TickGlobals* gl = nullptr;
    uint64_t* poolMask = nullptr;
    uint64_t poolBit = 0;
    void setActive (bool on) noexcept
    {
        active = on;
        if (poolMask != nullptr)
            *poolMask = on ? (*poolMask | poolBit) : (*poolMask & ~poolBit);
    }
    // the tables of the keys, looked up once in prepare () (CPU-REVIEW-VOICE-MOD.md V11): paramIndex (), libKeys (), destLawOf () and paramTable () are function local statics whose guard
    // check (a load and a barrier on ARM) every call paid on the audio thread
    const ParamIndex* pidx = nullptr;
    const LibKeys* libk[3] = {};
    const DestLaw* laws = nullptr;       // destLawOf (i) == laws[i] for every table index
    const ParamInfo* infos = nullptr;
    int noiseFilterKey = -1;             // noise.filter
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
    float velCurved = 0.0f, noteCurved = 0.0f;   // the velocity and note sources after their curves (content.curves), gliding on a legato transition when curve.*.legato is On
    bool curveFresh = true;                       // the next tick starts the curved sources at their targets (a new note)
    float curvedSource (float x, const CompiledModCurve* curve, float& state, bool glide, float dt, float portSeconds) noexcept;
    float vel = 1.0f, releaseVel = 0.0f, tuneExpr = 0.0f, polyPressure = 0.0f, smoothedPressure = 0.0f, padCoefDt = 0.0f, padCoef = 0.0f, slide = 0.0f, exprX = 0.0f;
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
    float envLen[6][6] = {};              // attack + delay, hold, decay, release (envReadout), then attack and delay alone (envReadoutFine)
    // what the voice derived from the parameters alone (no modulation touched): valid while the snapshot epoch, the note sequence and the tempo are the same
    EnvSettings envCache[6];
    uint32_t envCacheEpoch = 0, vcCacheEpoch = 0, vcCacheSequence = 0;
    double envCacheBpm = 0.0;
    bool envCacheValid = false, vcCacheValid = false;
    // the same per envelope (CPU-REVIEW-VOICE-MOD.md V1): envCache[e] holds the settings of the parameters alone while the snapshot epoch, the tempo and the envelope factor of the voice
    // control are the ones it was built with, and no slot touched a key of envelope e then or in the last period (other keys may be modulated)
    uint32_t envSetEpoch[6] = {};
    double envSetBpm[6] = {};
    float envSetFactor[6] = {};
    bool envSetOk[6] = {};
    /** The group (kGroup...) of every parameter index (voice.cpp paramGroups ()), and the groups of the keys the matrix touched (bit = group). */
    const uint8_t* groupOf = nullptr;
    uint32_t touchedGroups() const noexcept;
    float lfoAge[10] = {}, lfoSmoothL[10] = {}, lfoSmoothR[10] = {};
    float lfoReadPos[10] = {};            // the cycle position the display reads (lfoReadout): the offset of the LFO, or the clock of a free-running one
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
    int padCutIdx = -1;
    bool padCutTouched = false;
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
    NoiseCoeffCache noiseCoeffs;   // pitch gain, pitch low pass and filter pole of the noise (they change with their parameters only)
    float noiseGain = 0.0f, noiseGainPrev = 0.0f, noisePan = 0.0f, noiseStereo = 0.0f;
    int noiseColour = 0;
    struct Chan { float level = 0, levelPrev = 0, gl = 0, gr = 0, glPrev = 0, grPrev = 0; int dest = 3; bool env1 = true, on = false; };
    Chan subCh, noiseCh;
    int oscDest[3] = { 0, 0, 0 };
    float oscBlend[3] = {}, oscSend[3][2] = {}, subBlend = 0.0f, subSend[2] = {}, noiseBlend = 0.0f, noiseSend[2] = {}, filtSend[2][2] = {};
    bool oscEnv1[3] = { true, true, true };
    bool oscOn[3] = { true, false, false };
    uint32_t missingBits = 0;   // bit o: oscillator o is of a file type and has no file (it plays its built-in wavetable)
    bool oscWarpOnly[3] = {};   // out of its key range with Key Warp on: the oscillator runs (its signal warps the others) but is not heard
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
    int vcStep = 0;
    float outL = 0.0f, outR = 0.0f, lastLevel = 0.0f;
    float lastBusL[4] = {}, lastBusR[4] = {};
    float declickL[4] = {}, declickR[4] = {};
    int declickPos = 0;
    bool fresh = true;
    float glideStart = 0.0f;
};
} // namespace own
} // namespace synthyboy
