// SynthyBoy - engine-vital adapter: the effect instance pool, the part that builds and hands over effect modules
// (docs/ui-design/18-FX-INSTANCES.md section 2.5, docs/ui-design/13-new-keys.md 4.23).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A pool slot owns one sbvital::FxInstanceModule of the type of its fxiNN.type. The module is constructed, initialised and settled OFF the audio
// thread, on a builder thread, and handed over through an atomic pointer. In offline mode (Quality::realtime == false) the audio thread waits for
// the builder and takes the module over in the same block, so a render never depends on thread timing (the pattern of the Convolve module); in
// realtime mode it never waits and the slot passes audio dry until the module is ready. This class knows nothing of the parameter values after
// the build: the engine applies them (vital_engine.cpp). Threading:
//   audio thread:   request (), takeReady (), release (), cancel ()   - lock free in the realtime mode, no allocation in either
//   builder thread: build, deletion of the modules the chain has finished with and of the packages the audio thread gave back
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "reorderable_effect_chain.h"
#include "synthyboy/fx_types.h"
#include "vital_bindings.h"

namespace sbvital
{
class SoundEngine;
class Value;
class ValueSwitch;
class Processor;
} // namespace sbvital

namespace synthyboy
{
/** One built instance: the module and everything the audio thread needs to drive it without a lookup by name. */
struct FxBuilt
{
    struct Param
    {
        sbvital::Value* control = nullptr;       // the control of the module that the source binding names
        const Binding* binding = nullptr;        // the binding of the source key of instance 0 (conversion, cap)
        const ParamInfo* source = nullptr;       // the source key: range, curve, names
        double lo = 0.0, hi = 0.0;               // range of the control
        sbvital::Processor* mono = nullptr;      // modulation destination (null: the control is not modulatable)
        sbvital::ValueSwitch* monoSwitch = nullptr;
        float scale = 1.0f;                      // full sweep of the destination (the range of the control)
    };

    int slot = 0;
    int type = 0;
    uint32_t generation = 0;                     // the request this was built for
    sbvital::FxInstanceModule* module = nullptr; // owned by the FxPool (deleted when the chain has retired it), never by the audio thread
    int count = 0;                               // parameters of the type
    std::array<Param, kFxInstanceParams> params {};
    std::array<float, kFxInstanceParams> appliedNorm {}; // the normalised value each control has now
    uint32_t contentId = ~0u;                    // snapshot whose content (filter curve, shaper, impulse) the module has
    bool handedOver = false;                     // the module has been given to the chain (the chain's ring, not the package, ends its life)
};

class FxPool
{
public:
    /** The pool builds modules that fit the effect chain of the engine and takes its sample rate and oversampling (read at each build). */
    explicit FxPool (sbvital::SoundEngine& engine);
    ~FxPool();

    /** Audio thread (prepare thread). Asks for an instance of `type` for the slot with these normalised parameters. synchronous: the caller waits until it is built (offline mode,
        deterministic: the take-over happens in the same block); else it is taken over when the builder thread is ready. Always built on the builder thread. A newer request for the slot makes an older, unfinished one useless. */
    bool request (int slot, int type, const float* normalised, bool synchronous) noexcept;
    /** Audio thread. The slot wants no instance any more: an unfinished build for it is discarded. */
    void cancel (int slot) noexcept;
    /** The built instance of the slot that has not been taken yet, or nullptr. A stale one (an older request) is released here. */
    FxBuilt* takeReady (int slot, uint32_t currentGeneration) noexcept;
    /** The newest request number of a slot (0 before the first). */
    uint32_t generation (int slot) const noexcept { return generations[static_cast<size_t> (slot)]; }
    /** The audio thread has finished with the package (its module is retired by the chain separately). */
    void release (FxBuilt* built) noexcept;
    /** Wakes the builder thread, which deletes the modules the chain has retired and the packages given back (never the audio thread). */
    void collectNow();

    /** Number of builds that are queued or running (tests wait for 0). */
    int pendingBuilds() const noexcept { return inFlight.load (std::memory_order_acquire); }

private:
    struct Request
    {
        int slot = 0;
        int type = 0;
        uint32_t generation = 0;
        std::array<float, kFxInstanceParams> normalised {};
        sbvital::poly_float beatsPerSecond = 2.0f, keytrack = 0.0f;   // read from the engine by the requesting (audio) thread
    };

    FxBuilt* build (const Request& request);
    void startThread();
    void threadMain();
    void deleteModule (sbvital::FxInstanceModule* module);
    void drain();

    sbvital::ReorderableEffectChain* chain;

    std::array<uint32_t, kNumFxInstances> generations {};            // audio thread
    std::array<std::atomic<FxBuilt*>, kNumFxInstances> ready;        // builder -> audio thread

    // audio thread -> builder: the requests (single producer, single consumer) and the packages given back
    static constexpr unsigned kRing = 128;
    std::array<Request, kRing> requests {};
    std::atomic<unsigned> requestWrite { 0 }, requestRead { 0 };
    std::array<std::atomic<FxBuilt*>, kRing> graveyard;
    std::atomic<unsigned> graveWrite { 0 }, graveRead { 0 };
    std::array<std::atomic<uint32_t>, kNumFxInstances> latestGeneration; // what the builder checks before it builds / publishes

    std::mutex registryMutex;                                         // the set of live modules, the consumer side of the chain's ring
    std::set<sbvital::FxInstanceModule*> live;
    std::mutex wakeMutex;
    std::mutex doneMutex;
    std::condition_variable done;
    std::condition_variable wake;
    std::thread thread;
    std::atomic<bool> stopping { false };
    std::atomic<int> inFlight { 0 };
};
} // namespace synthyboy
