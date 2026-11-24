// SynthyBoy - own engine: the effect racks (Main, Bus 1, Bus 2 and the bands of the splitters): the 16 modules in fx.order order and the 16 slot instance pool.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>

#include "../matrix/mod_matrix.h"
#include "fx_module.h"
#include "synthyboy/fx/hyper_dimension.h"
#include "synthyboy/fx/splitter_lh.h"
#include "synthyboy/fx/splitter_lmh.h"
#include "synthyboy/fx/splitter_ms.h"
#include "synthyboy/fx/utility.h"
#include "bode.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/quality.h"
#include "fx_makeup.h"
#include "fx_latency.h"

namespace synthyboy
{
namespace own
{
/** Positions in paramTable() of the keys the chain itself reads (built once). */
struct FxIndex
{
    int order[16];
    int rack[16];
    int on[16]; // by module (the fx.order choices 0 .. 15)
    int level[16];
    int lhFreq, lhLow, lhHigh, lmhFreqLow, lmhFreqHigh, lmhLow, lmhMid, lmhHigh, msMid, msSide;
};
const FxIndex& fxIndex() noexcept;

/** The 16 slots of the effect instance pool (document 06, 12). The module of a slot is created on the thread that sets the type (never the audio thread) and handed
    over with an atomic pointer; the modules the audio thread retires are collected on the next set. A slot belongs to the chain of its rack, but its state (the module, the
    crossfade) lives here so that a slot that moves from one rack to another keeps its tail. */
class FxPool
{
public:
    static constexpr int kSlots = kNumFxInstances;
    struct Slot
    {
        // message thread -> audio thread
        std::atomic<FxModule*> pending { nullptr };
        std::atomic<int> pendingType { 0 };
        // audio thread -> message thread
        std::atomic<FxModule*> grave[4] = {};
        // audio thread only
        FxModule* cur = nullptr;      // the module of the slot's type
        int curType = 0;
        FxModule* running = nullptr;  // cur while the slot runs (on, type matches), else nullptr
        FxModule* from = nullptr;     // crossfade: the outgoing module (nullptr = dry), only meaningful while fading
        bool fading = false;
        int fadePos = 0, fadeLen = 0;
        // builder side only (typeChanged, prepare: the message thread or the FX builder thread, never the audio thread), guarded by FxPool::typeMutex
        int typeSeen = 0;             // the type of the last module created
        bool handedOver = false;      // a module of typeSeen was handed to the audio thread (pending set); it stands for "pending != nullptr || cur != nullptr" without reading the audio thread's `cur`
    };
    ~FxPool();
    void prepare (double sampleRate, int maxBlock, const int* types);
    /** The slot's type key was set (message thread or any non audio thread): creates the module of the new type (prepare ()d), frees what the audio thread retired. */
    void typeChanged (int slot, int type);
    void collect (int slot) noexcept;
    Slot& slot (int i) noexcept { return slots[static_cast<size_t> (i)]; }
    const Slot& slot (int i) const noexcept { return slots[static_cast<size_t> (i)]; }
    bool isPrepared() const noexcept { return prepared.load(); }
    void resetAll() noexcept;
    void retire (Slot& s, FxModule* m) noexcept;
    /** True while a module that the message thread created waits to be taken over by the audio thread. */
    bool anyPending() const noexcept
    {
        for (const Slot& s : slots)
            if (s.pending.load (std::memory_order_acquire) != nullptr)
                return true;
        return false;
    }

private:
    std::array<Slot, kSlots> slots;
    double sr = 48000.0;
    int maxBlk = 512;
    std::atomic<bool> prepared { false };
    std::mutex typeMutex;   // typeChanged / prepare against each other (builder thread vs a re-prepare on the message thread); never taken on the audio thread
};

class FxChain
{
public:
    FxChain();
    ~FxChain();
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    /** Which rack this chain is: 0 Main, 1 Bus 1, 2 Bus 2 (the modules whose `rack` key names it, and the band racks of the splitters that sit in it). */
    void setRack (int id) noexcept { rackId = id; }
    void setPool (FxPool* p) noexcept { pool = p; }
    /** Reads the parameters (once per control period). `delta` is the global modulation in control units (may be zero). `inputsSame`: the parameters, the
        modulation and the mask are what the previous call saw; a chain with nothing on that has settled then does nothing (the call would redo the same bookkeeping). */
    void update (const float* base, const float* delta, const FxFrame& frame, uint32_t fxMask, bool inputsSame = false) noexcept;
    /** `t0`: the position of this call in the control period (the output trims and the EQ interpolate across the period). */
    void process (float* l, float* r, int n, int t0 = 0) noexcept;
    bool anyActive() const noexcept { return active; }
    /** Tier low: a rack whose input has been exactly zero and whose output has stayed below -120 dBFS for longer than the longest gap between echoes is not run (its LFOs keep
        their pace); the first non zero input wakes it. */
    void setLowCost (bool on) noexcept { lowCost = on; }
    /** The FX budget (fx_budget.h): the modules of instance 0 (bit m) and the pool slots (bit s) that are over budget are bypassed like an effect the mask switches off (the crossfade, the loudness left as a gain).
        Set by the engine before update () whenever FxBudget::evaluate found something new; read only for the UI. */
    void setOverBudget (uint32_t modules, uint32_t slots) noexcept { overModules = modules; overSlots = slots; }
    uint32_t overBudgetModules() const noexcept { return overModules; }
    uint32_t overBudgetSlots() const noexcept { return overSlots; }
    /** True if the last update () did the work (it did not return early): latency and plan may have changed. */
    bool configuredNow() const noexcept { return configured; }
    /** The latency of rack `rack` (0 .. 9) in samples of the chain rate: the sum of the modules that are on in it, in series; a splitter adds the slowest of its bands
        (document 06, 11). `reported` leaves out what the host is not told about (a limiter with limit_latency_comp off). */
    int rackLatency (int rack, bool reported) const noexcept;
    /** On a bus chain (rack 1 or 2) the gain of the bypassed effects is not applied inside the chain but by the engine at the bus (OwnEngine, `runBus`), which knows what else reaches the output
        from the same source: true when the bus rack holds a bypassed effect with a gain; `power` is the product of their power gains, `coherent` the product of their coherent amplitudes,
        `amplitude` the product of their gains (what the engine applies when nothing else shares the source). */
    bool busMakeup (float& power, float& coherent, float& amplitude) const noexcept;
    /** The chain of a bus defers its makeup gain to the engine. */
    void setBusDeferred (bool on) noexcept { busDeferred = on; }

private:
    struct Item { bool pool; int index; int key; bool bypass = false; }; // bypass: the item is the makeup gain of a bypassed effect (fx_makeup.h), not the effect
    struct Plan { std::array<Item, 32> item; int count = 0; };
    void buildPlan (const float* base) noexcept;
    void configureSlot (int s, const float* base, const float* delta, const FxFrame& frame, uint32_t fxMask) noexcept;
    void runRack (int rack, float* l, float* r, int n, int t0, int depth) noexcept;
    void runModule (int m, float* l, float* r, int n, int t0, int depth) noexcept;
    void runSlot (int s, float* l, float* r, int n, int t0) noexcept;
    /** The gain that stands in for a bypassed effect (Quality::fxMakeup): ramped across the control period from the previous gain to the current one. */
    void runMakeup (float prev, float now, float* l, float* r, int n, int t0, makeup::ProgramMeter& meter) noexcept;
    /** One step of the smoothing of a makeup gain towards its target (immediately when `first` or `noSmoothing`). */
    void stepMakeup (float target, bool& seen, float& prev, float& now, const FxFrame& frame) const noexcept;
    mutable int smoothPeriod = -1;       // the smoothing constant of stepMakeup and what it was made for
    mutable double smoothRate = -1.0;
    mutable float smoothStep = 0.0f;
    bool slotRuns (int s) const noexcept;
    uint32_t overModules = 0, overSlots = 0;   // the FX budget (setOverBudget)

    double sr = 48000.0;
    bool active = false;
    bool configured = true;
    bool lowCost = false;
    bool busDeferred = false;
    int quietRun = 0, holdSamples = 0;   // samples of silence in and out so far, and how many are needed
    void idleRack (int rack, int n, int depth) noexcept;
    int idleSettled = 0;            // consecutive updates with the same inputs that ended with nothing on
    int order[16] = {};
    bool on[16] = {};
    bool prevOn[16] = {};
    bool fadeIn[16] = {};
    std::unique_ptr<FxModule> inst[kNumFxModules];
    int period = kControlPeriod;
    int rackId = 0;
    int moduleRack[16] = {};
    float levelGain[16] = {}, levelGainPrev[16] = {};
    // the effects that are on in the preset but bypassed by the quality mask, and the gain that stands in for them (module 0 .. 12; the slots of the pool)
    bool bypassed[16] = {}, makeupSeen[16] = {};
    float makeupNow[16] = {}, makeupPrev[16] = {};
    float makeupPow[16] = {}, makeupCoh[16] = {}, slotMakeupPow[FxPool::kSlots] = {}, slotMakeupCoh[FxPool::kSlots] = {};
    bool slotBypassed[FxPool::kSlots] = {}, slotMakeupSeen[FxPool::kSlots] = {};
    float slotMakeupNow[FxPool::kSlots] = {}, slotMakeupPrev[FxPool::kSlots] = {};
    makeup::ProgramMeter meter[16], slotMeter[FxPool::kSlots]; // the program at the place of each bypassed effect (the input of the gain)
    FxPool* pool = nullptr;
    bool slotOn[FxPool::kSlots] = {};
    int slotRack[FxPool::kSlots] = {}, slotOrder[FxPool::kSlots] = {}, slotType[FxPool::kSlots] = {};
    bool slotMine[FxPool::kSlots] = {};
    std::array<Plan, 10> plan;
    std::vector<float> vbase;
    fx::SplitterLH splitLh;
    fx::SplitterLMH splitLmh;
    fx::SplitterMS splitMs;
    IntDelay dlLh[2], dlLmh[3], dlMs[2];
    float scratchA[2][kControlPeriod] = {}, scratchB[2][kControlPeriod] = {};
    int maxBlock = 512;
};
} // namespace own
} // namespace synthyboy
