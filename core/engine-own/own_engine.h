// SynthyBoy - own engine: the Serum-class engine behind IEngine, without JUCE and without Vital-derived code (docs/ARCHITECTURE.md 2.2, 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <vector>

#include "dsp/util.h"
#include "matrix/mod_matrix.h"
#include "osc/content_set.h"
#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "fx/fx_chain.h"
#include "fx/fx_latency.h"
#include "voice/note_tuning.h"
#include "voice/voice.h"

namespace synthyboy
{
namespace own
{
/** The default of OwnEngine::setLowTierUnisonCap: the one place of the number. */
constexpr int kDefaultLowTierUnisonCap = 3;
/** The default of OwnEngine::setLowTierKillFade: a stolen voice (Kill mode) fades out in 3 ms at tier low instead of 50 ms, so it stops costing a full voice after a few control periods. */
constexpr float kDefaultLowTierKillSeconds = 0.003f;
/** The default of OwnEngine::setLowTierMorphSpread: keep the morph spread across the unison voices at tier low (two morph cycles instead of one shared one). */
constexpr bool kDefaultLowTierMorphSpread = true;

class OwnEngine final : public IEngine
{
public:
    OwnEngine();
    ~OwnEngine() override;

    void prepare (double sampleRate, int maxBlockFrames, const Quality& quality) override;
    void setQuality (const Quality& newQuality) noexcept override;
    /** The unison voices per oscillator at tier low (a quality with oversampling 1 and no explicit Quality::economy), on top of the cap of the quality: the voice count is min (patch value, cap), the rest of the unison law
        (detune width, spread, blend, level normalisation) is that of a patch that asks for the cap. 0 = no such cap. Never touches a parameter value, never applies at
        medium / high or to an offline render (that runs on a twin at the high tier). Any thread, lock free; read at the start of every block. */
    void setLowTierUnisonCap (int voices) noexcept { lowUnisonCap.store (std::max (0, voices), std::memory_order_relaxed); }
    /** Pad pressure to Filter 1 cutoff modulation depth (0 = off, 1 = +36 semitones at full pressure). Any thread, lock free. */
    void setPadPressureToCutoff (float amount01) noexcept { padPressureToCutoff.store (std::clamp (amount01, 0.0f, 1.0f), std::memory_order_relaxed); }
    float getPadPressureToCutoff() const noexcept { return padPressureToCutoff.load (std::memory_order_relaxed); }
    /** Tier low: true keeps the spectral morph spread across the unison voices (two morph cycles, as at medium / high), false shares one cycle between them (about half the
        morph build cost, the morph spread knob then does nothing at tier low). Any thread, lock free. */
    void setLowTierMorphSpread (bool keep) noexcept { lowKeepMorphSpread.store (keep, std::memory_order_relaxed); }
    /** Tier low: the fade of a stolen voice in seconds (0.05 = the 50 ms of medium / high). Any thread, lock free. */
    void setLowTierKillFade (float seconds) noexcept { lowKillSeconds.store (std::max (0.0005f, seconds), std::memory_order_relaxed); }
    int lowTierUnisonCap() const noexcept { return lowUnisonCap.load (std::memory_order_relaxed); }
    /** Measurements only (setOwnSavingsOff): the OwnSaving bits that are switched off. Any thread, lock free; read at the start of every block. */
    void setSavingsOff (uint32_t off) noexcept { savingsOff.store (off, std::memory_order_relaxed); }
    void reset() override;
    void setParam (uint32_t id, float plain) noexcept override;
    float getParam (uint32_t id) const noexcept override;
    bool supportsParam (uint32_t id) const noexcept override;
    void process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept override;
    void setTransport (const TransportInfo& t) noexcept override;
    void setReadoutTargets (const int* idx, int count) noexcept override;
    void readyContent() noexcept override;
    double wavetableBuildMs() const noexcept override;
    int activeVoices() const noexcept override { return activeCount.load (std::memory_order_relaxed); }
    int latencySamples() const noexcept override { return reportedLatency.load (std::memory_order_relaxed); }
    void setTuningSource (const tuning::ITuningSource* s) noexcept override { noteTuning.setSource (s); paramGen.fetch_add (1u, std::memory_order_release); }
    EngineCapsMask capabilities() const noexcept override;
    EngineCaps2Mask capabilities2() const noexcept override;
    bool copyReadout (LiveValues& out) const noexcept override;
    void silence() noexcept override;
    void setUltraRender (bool on) noexcept override { ultraRender.store (on, std::memory_order_relaxed); }
    void setContent (const StateContent& content) override;
    void setContentStore (const IContentStore* store) override;

private:
    static constexpr int numChannels = 16;
    static constexpr int kPoolVoices = kMaxVoices + 4; // the spare voices of the Kill mode (document 01, 3.1)

    void handleEvent (const Event& e) noexcept;
    void noteOn (const Event& e) noexcept;
    void noteOff (const Event& e) noexcept;
    void setSustain (int channel, bool down) noexcept;
    struct Pressed { int playedKey = 0, key = 0, channel = 0; float velocity = 0.0f; int32_t noteId = -1; uint32_t order = 0; };
    void startNote (const Pressed& n, bool fromRecall, const Voice* releasing) noexcept;
    void removePressed (int playedKey, int channel) noexcept;
    bool hasVoice (const Pressed& n) const noexcept;
    int polyphonyLimit() const noexcept;
    Voice* freeVoice() noexcept;
    Voice* pickVictim (bool includeKilling) noexcept;
    void applyAxes (int channel, int axis, float value) noexcept;
    void killExcess() noexcept;
    /** sub.mono: the lowest tuned key note of the sounding voices (globals.lowestNote), read by the voices when they tick. */
    void updateLowestNote() noexcept;
    void writeReadout() noexcept;
    void controlTick() noexcept;
    Voice* allocateVoice (bool legato) noexcept;
    void applyPhaseOptions (Voice& v, const uint32_t (*prev)[Voice::kPhaseCount], bool prevHeld) noexcept;
    uint32_t memPhase[3][Voice::kPhaseCount] = {};   // Phase Memory Per Voice: the captured phases of the three oscillators
    bool memValid[3] = {};
    float readParam (uint32_t id) const noexcept;
    void rebuildContent();
    void publishContent (std::unique_ptr<ContentSet> set);

    std::array<std::atomic<float>, kMaxParams> params;
    std::atomic<int> voiceCap { 8 }, unisonCap { kMaxUnisonVoices };
    std::atomic<int> lowUnisonCap { kDefaultLowTierUnisonCap };
    std::atomic<float> padPressureToCutoff { 0.0f };
    std::atomic<float> lowKillSeconds { kDefaultLowTierKillSeconds };
    std::atomic<bool> lowKeepMorphSpread { kDefaultLowTierMorphSpread };
    std::atomic<uint32_t> savingsOff { 0 };
    std::atomic<int> activeCount { 0 };
    // transport (set from any thread by the shell between blocks; read by the audio thread)
    std::atomic<double> tpBpm { 120.0 }, tpPos { 0.0 };
    std::atomic<bool> tpPlaying { false };

    // the decoded content (message thread builds, audio thread reads without a lock; a set is freed only when the audio thread no longer uses it)
    std::mutex contentMutex;
    StateContent contentState;
    const IContentStore* contentStore = nullptr;
    std::vector<std::unique_ptr<ContentSet>> contentOwned;
    std::atomic<const ContentSet*> contentCurrent { nullptr }, contentInUse { nullptr };
    std::atomic<int> switchKeyAtomic[3];
    int switchKeySnap[3] = { -1, -1, -1 };
    uint32_t chokeLatest[3][256] = {}, chokeCounter[3] = {};
    std::atomic<int> qualityTier { 2 };
    std::atomic<bool> tierLowCap { false };  // the quality is a plain tier low (Quality::economy negative, oversampling 1): setLowTierUnisonCap applies; a quality with explicit economy groups carries its own cap in maxUnison
    std::atomic<uint32_t> economyMask { 0 }; // the economy groups in force (Quality::economy, EconomyFlags); read at the start of every block
    std::atomic<bool> ultraRender { false };
    std::atomic<float> liveSampleHead[3];         // the read head of the sample oscillators (the voice that is heard), -1 = none
    std::atomic<float> liveVoicePos[3][LiveValues::kReadVoices];   // the read positions of the newest voices (newest first), -1 = none
    std::atomic<int> liveNumGrains[3];
    std::atomic<float> liveGrainPos[3][LiveValues::kReadGrains], liveGrainLife[3][LiveValues::kReadGrains];
    std::atomic<float> liveSubPhase[LiveValues::kReadVoices];
    std::atomic<float> liveVoiceStep[LiveValues::kReadVoices];
    std::atomic<float> liveEnvVoiceStage[LiveValues::kNumEnvs][LiveValues::kReadVoices], liveEnvVoiceFrac[LiveValues::kNumEnvs][LiveValues::kReadVoices];   // the fine stage and fraction of every envelope in the newest voices (newest first)
    std::atomic<float> liveLfoVoicePhase[LiveValues::kNumLfos][LiveValues::kReadVoices];   // the cycle position of every LFO in the newest voices (newest first)
    std::atomic<int> liveNumLfoVoices { 0 };
    std::atomic<float> liveNoisePos;
    std::atomic<int> multiZone[3], multiKey[3];   // the zone and key of the last multisample note (read-out)
    std::atomic<uint32_t> blockCounter { 0 };
    // the live read-out (written at the end of a block, read by the display)
    std::atomic<float> liveSource[LiveValues::kMaxSources], liveLfo[LiveValues::kNumLfos], liveEnvStage[LiveValues::kNumEnvs], liveEnvPos[LiveValues::kNumEnvs], liveEnvFine[LiveValues::kNumEnvs], liveEnvFineFrac[LiveValues::kNumEnvs], liveTarget[LiveValues::kMaxTargets], liveStripPeak[LiveValues::kMaxStrips];
    std::atomic<int> liveNumTargets { 0 }, pendingTargets[LiveValues::kMaxTargets];
    std::atomic<int> pendingCount { 0 };
    int activeTargets[LiveValues::kMaxTargets] = {}, activeTargetCount = 0;
    std::unique_ptr<spectral::Scratch> spectralScratch;

    std::vector<std::unique_ptr<Voice>> voices;
    float heldSrc[kNumSourceIds] = {};   // the source values of the voice the global modulation read last (the held values while no voice sounds, document 04, 3.1)
    bool heldSrcValid = false;
    std::vector<uint8_t> sustainPending;
    std::vector<float> base;       // snapshot of all parameters for the block
    // The snapshot, the compiled matrix and the tuning table are rebuilt only when something they read changed: every writer (setParam, content, tuning source) bumps
    // paramGen after its store, the audio thread compares it with the generation of its last snapshot.
    std::atomic<uint32_t> paramGen { 1 };
    uint32_t snapGen = 0;
    const OwnContent* snapContent = nullptr;
    bool snapValid = false;
    uint32_t baseEpoch = 0;        // counts the rebuilds of the snapshot
    bool blockDirty = true;        // the snapshot of this block differs from the one of the previous block
    bool mixCached = false, latCached = false;
    uint32_t lastFxMask = 0;
    std::vector<float> gdelta, gdeltaR;     // modulation of the global parameters
    std::vector<int> gtouched;
    int gnumTouched = 0;
    std::vector<float> pdelta;              // modulation of the voice parameters by the sources an idle engine has (macros, wheels ...): the readout without a voice
    std::vector<int> ptouched;
    int pnumTouched = 0;
    ModMatrix matrix;
    FxChain fx[3];                 // the racks Main, Bus 1, Bus 2
    FxPool fxPool;                 // the 16 slots of the effect instance pool
    int fxiTypeIndex[kNumFxInstances] = {};
    int wtParamIdx[6] = { -1, -1, -1, -1, -1, -1 };   // table positions of osc1..3.wavetable and osc1..3.spec.flt_table: the keys that name a built-in table (set once in the constructor)
    unsigned wavetableMaskOfParams() const noexcept;   // bit (1 << choice) for every built-in table the current values of those keys name
    IntDelay dlVoices, dlBusOut[2], dlBusIn[2], dlMainOut, dlDirect;  // latency compensation (document 06, 11)
    std::atomic<int> reportedLatency { 0 };
    void clearLatencyDelays() noexcept;
    // Tier low: a stolen voice stops at once and its last output is ramped to zero on the buses it fed (a few ms), by the engine, so the voice costs nothing after the steal
    struct Ghost
    {
        float v[4][2] = {};
        int age = 0, total = 0;
        bool used = false;
    };
    Ghost ghosts[16];
    void ghostKill (Voice& v) noexcept;
    void addGhosts (int n) noexcept;
    bool realtimeFlag = false;
    // The FX type parameters: setParam may be called by the host on its audio thread, and a new module is built with new / prepare (allocation). In a live (realtime) run setParam only records the wanted type here
    // and a small worker thread builds the module and hands it to the pool (FxPool::typeChanged, the same code as before); the sound is the same, the module is there a few milliseconds later. An offline run (tests,
    // renders: quality.realtime false) builds it at once on the calling thread, as it always did.
    std::atomic<int> fxWant[kNumFxInstances];
    std::atomic<bool> fxWorkDue { false }, fxWorkerStop { false };
    std::thread fxWorker;
    void startFxWorker();
    void stopFxWorker() noexcept;
    float mixL[4][kControlPeriod] = {}, mixR[4][kControlPeriod] = {}; // Main, Bus 1, Bus 2, Direct of the current period
    // the level meters of the mixer (LiveValues::stripPeak, audio thread only until writeReadout publishes them): per strip (Sub, A, B, C, Noise, Filter 1, Filter 2, Bus 1, Bus 2, Master) the peak of this block and the
    // shown level (the peak with a slow release); stripSq collects the power sums of the voices' sources of one control period (MixOut::srcSq)
    float stripSq[7] = {}, stripBlockPeak[LiveValues::kMaxStrips] = {}, stripShown[LiveValues::kMaxStrips] = {};
    int lastBlockFrames = 0;   // the frames of the block that writeReadout publishes
    float busGain[3] = { 1.0f, 1.0f, 1.0f }, busGainPrev[3] = { 1.0f, 1.0f, 1.0f }, directGain = 1.0f, directGainPrev = 1.0f;
    int busDest[2] = {}, mainBypass = 0, busBypass[2] = {};
    std::atomic<uint32_t> fxMask { kFxAll };
    MorphCache morphCache;
    TickGlobals globals;
    NoteTuning noteTuning;
    ContentHub ownContent;
    const OwnContent* contentNow = nullptr;
    LfoCore monoCore[10];
    float tunedNotes[128] = {};
    Pressed pressed[64];
    int numPressed = 0;
    float effBend[numChannels] = {};
    float macroOverride[8] = {};
    bool macroOverridden[8] = {};
    int lastXyzMode = 0;
    bool sustainDown[numChannels] = {};
    float bendSemis[numChannels] = {}, modWheel[numChannels] = {}, aftertouch[numChannels] = {};
    float lfoPhase[10] = {}, rlfoValue[4] = {};
    RandomLfo rlfo[4];
    LinearSmoother smoothedGain;
    double sampleRate = 0.0;
    bool prepared = false;
    uint32_t noteCounter = 0, voiceCtlCounter = 0;
    int controlPos = 0;
    float lastNoteSemis = 60.0f;
    bool haveLastNote = false;
    float routeCos = 1.0f, routeSin = 0.0f;
    float lastVelocity = 0.0f;
    const int fxReverbForceIndex;
    const int fxOversampleIndex;
    const int noSmoothingIndex;
    std::vector<float> scratchRight;
};
} // namespace own
} // namespace synthyboy
