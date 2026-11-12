// SynthyBoy - own engine: the Serum-class engine behind IEngine, without JUCE and without Vital-derived code (docs/ARCHITECTURE.md 2.2, 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "dsp/util.h"
#include "matrix/mod_matrix.h"
#include "synthyboy/engine.h"
#include "fx/fx_chain.h"
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
    int activeVoices() const noexcept override { return activeCount.load (std::memory_order_relaxed); }
    void setContent (const StateContent& c) override { noteTuning.setContent (c.tuning); content.publish (c); }
    void setTuningSource (const tuning::ITuningSource* s) noexcept override { noteTuning.setSource (s); }
    EngineCapsMask capabilities() const noexcept override;
    EngineCaps2Mask capabilities2() const noexcept override;

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
    void controlTick() noexcept;
    Voice* allocateVoice (bool legato) noexcept;
    float readParam (uint32_t id) const noexcept;

    std::array<std::atomic<float>, kMaxParams> params;
    std::atomic<int> voiceCap { 8 }, unisonCap { kMaxUnisonVoices };
    std::atomic<int> activeCount { 0 };
    // transport (set from any thread by the shell between blocks; read by the audio thread)
    std::atomic<double> tpBpm { 120.0 }, tpPos { 0.0 };
    std::atomic<bool> tpPlaying { false };

    std::vector<std::unique_ptr<Voice>> voices;
    std::vector<uint8_t> sustainPending;
    std::vector<float> base;       // snapshot of all parameters for the block
    std::vector<float> gdelta, gdeltaR;     // modulation of the global parameters
    std::vector<int> gtouched;
    int gnumTouched = 0;
    ModMatrix matrix;
    FxChain fx[3];                 // the racks Main, Bus 1, Bus 2
    float mixL[4][kControlPeriod] = {}, mixR[4][kControlPeriod] = {}; // Main, Bus 1, Bus 2, Direct of the current period
    float busGain[3] = { 1.0f, 1.0f, 1.0f }, busGainPrev[3] = { 1.0f, 1.0f, 1.0f }, directGain = 1.0f, directGainPrev = 1.0f;
    int busDest[2] = {}, mainBypass = 0, busBypass[2] = {};
    std::atomic<uint32_t> fxMask { kFxAll };
    TickGlobals globals;
    NoteTuning noteTuning;
    ContentHub content;
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
    std::vector<float> scratchRight;
};
} // namespace own
} // namespace synthyboy
