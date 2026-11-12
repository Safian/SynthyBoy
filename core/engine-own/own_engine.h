// SynthyBoy - own engine: the Serum-class engine behind IEngine, without JUCE and without Vital-derived code (docs/ARCHITECTURE.md 2.2, 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <array>
#include <atomic>
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
class OwnEngine final : public IEngine
{
public:
    OwnEngine();
    ~OwnEngine() override;

    void prepare (double sampleRate, int maxBlockFrames, const Quality& quality) override;
    void setQuality (const Quality& newQuality) noexcept override;
    void reset() override;
    void setParam (uint32_t id, float plain) noexcept override;
    float getParam (uint32_t id) const noexcept override;
    bool supportsParam (uint32_t id) const noexcept override;
    void process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept override;
    void setTransport (const TransportInfo& t) noexcept override;
    void setReadoutTargets (const int* idx, int count) noexcept override;
    int activeVoices() const noexcept override { return activeCount.load (std::memory_order_relaxed); }
    int latencySamples() const noexcept override { return reportedLatency.load (std::memory_order_relaxed); }
    void setTuningSource (const tuning::ITuningSource* s) noexcept override { noteTuning.setSource (s); }
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
    void writeReadout() noexcept;
    void controlTick() noexcept;
    Voice* allocateVoice (bool legato) noexcept;
    float readParam (uint32_t id) const noexcept;
    void rebuildContent();
    void publishContent (std::unique_ptr<ContentSet> set);

    std::array<std::atomic<float>, kMaxParams> params;
    std::atomic<int> voiceCap { 8 }, unisonCap { kMaxUnisonVoices };
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
    std::atomic<bool> ultraRender { false };
    std::atomic<int> multiZone[3], multiKey[3];   // the zone and key of the last multisample note (read-out)
    std::atomic<uint32_t> blockCounter { 0 };
    // the live read-out (written at the end of a block, read by the display)
    std::atomic<float> liveSource[LiveValues::kMaxSources], liveLfo[LiveValues::kNumLfos], liveEnvStage[LiveValues::kNumEnvs], liveEnvPos[LiveValues::kNumEnvs], liveTarget[LiveValues::kMaxTargets];
    std::atomic<int> liveNumTargets { 0 }, pendingTargets[LiveValues::kMaxTargets];
    std::atomic<int> pendingCount { 0 };
    int activeTargets[LiveValues::kMaxTargets] = {}, activeTargetCount = 0;
    std::unique_ptr<spectral::Scratch> spectralScratch;

    std::vector<std::unique_ptr<Voice>> voices;
    std::vector<uint8_t> sustainPending;
    std::vector<float> base;       // snapshot of all parameters for the block
    std::vector<float> gdelta, gdeltaR;     // modulation of the global parameters
    std::vector<int> gtouched;
    int gnumTouched = 0;
    ModMatrix matrix;
    FxChain fx[3];                 // the racks Main, Bus 1, Bus 2
    FxPool fxPool;                 // the 16 slots of the effect instance pool
    int fxiTypeIndex[kNumFxInstances] = {};
    IntDelay dlVoices, dlBusOut[2], dlBusIn[2], dlMainOut, dlDirect;  // latency compensation (document 06, 11)
    std::atomic<int> reportedLatency { 0 };
    void clearLatencyDelays() noexcept;
    bool realtimeFlag = false;
    float mixL[4][kControlPeriod] = {}, mixR[4][kControlPeriod] = {}; // Main, Bus 1, Bus 2, Direct of the current period
    float busGain[3] = { 1.0f, 1.0f, 1.0f }, busGainPrev[3] = { 1.0f, 1.0f, 1.0f }, directGain = 1.0f, directGainPrev = 1.0f;
    int busDest[2] = {}, mainBypass = 0, busBypass[2] = {};
    std::atomic<uint32_t> fxMask { kFxAll };
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
    std::vector<float> scratchRight;
};
} // namespace own
} // namespace synthyboy
