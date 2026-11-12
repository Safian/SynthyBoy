// SynthyBoy - own engine: the Serum-class engine behind IEngine, without JUCE and without Vital-derived code (docs/ARCHITECTURE.md 2.2, 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

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
    int activeVoices() const noexcept override { return activeCount.load (std::memory_order_relaxed); }
    EngineCapsMask capabilities() const noexcept override;
    EngineCaps2Mask capabilities2() const noexcept override;
    void setContent (const StateContent& content) override;
    void setContentStore (const IContentStore* store) override;

private:
    static constexpr int numChannels = 16;

    void handleEvent (const Event& e) noexcept;
    void noteOn (const Event& e) noexcept;
    void noteOff (const Event& e) noexcept;
    void setSustain (int channel, bool down) noexcept;
    void controlTick() noexcept;
    Voice* allocateVoice (int key, int channel, float velocity) noexcept;
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
    std::atomic<int> qualityTier { 2 };
    std::unique_ptr<spectral::Scratch> spectralScratch;

    std::vector<std::unique_ptr<Voice>> voices;
    std::vector<uint8_t> sustainPending;
    std::vector<float> base;       // snapshot of all parameters for the block
    std::vector<float> gdelta;     // modulation of the global parameters
    std::vector<int> gtouched;
    int gnumTouched = 0;
    ModMatrix matrix;
    FxChain fx;
    std::atomic<uint32_t> fxMask { kFxAll };
    TickGlobals globals;
    bool sustainDown[numChannels] = {};
    float bendSemis[numChannels] = {}, modWheel[numChannels] = {}, aftertouch[numChannels] = {};
    float lfoPhase[10] = {}, rlfoValue[4] = {};
    RandomLfo rlfo[4];
    LinearSmoother smoothedGain;
    double sampleRate = 0.0;
    bool prepared = false;
    uint32_t noteCounter = 0;
    int controlPos = 0;
    float lastNoteSemis = 60.0f;
    bool haveLastNote = false;
    int heldKeys = 0;
    float lastVelocity = 0.0f;
    const int fxReverbForceIndex;
    std::vector<float> scratchRight;
};
} // namespace own
} // namespace synthyboy
