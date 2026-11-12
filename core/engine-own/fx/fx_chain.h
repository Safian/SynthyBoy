// SynthyBoy - own engine: the effect racks (Main, Bus 1, Bus 2 and the bands of the splitters): the 16 modules in fx.order order and the 16 slot instance pool.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <array>
#include <atomic>
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
        int typeSeen = 0;             // message thread bookkeeping: the type of the last module created
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

private:
    std::array<Slot, kSlots> slots;
    double sr = 48000.0;
    int maxBlk = 512;
    std::atomic<bool> prepared { false };
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
    /** Reads the parameters (once per control period). `delta` is the global modulation in control units (may be zero). */
    void update (const float* base, const float* delta, const FxFrame& frame, uint32_t fxMask) noexcept;
    /** `t0`: the position of this call in the control period (the output trims and the EQ interpolate across the period). */
    void process (float* l, float* r, int n, int t0 = 0) noexcept;
    bool anyActive() const noexcept { return active; }
    /** The latency of rack `rack` (0 .. 9) in samples of the chain rate: the sum of the modules that are on in it, in series; a splitter adds the slowest of its bands
        (document 06, 11). `reported` leaves out what the host is not told about (a limiter with limit_latency_comp off). */
    int rackLatency (int rack, bool reported) const noexcept;

private:
    struct Item { bool pool; int index; int key; };
    struct Plan { std::array<Item, 32> item; int count = 0; };
    void buildPlan (const float* base) noexcept;
    void configureSlot (int s, const float* base, const float* delta, const FxFrame& frame, uint32_t fxMask) noexcept;
    void runRack (int rack, float* l, float* r, int n, int t0, int depth) noexcept;
    void runModule (int m, float* l, float* r, int n, int t0, int depth) noexcept;
    void runSlot (int s, float* l, float* r, int n, int t0) noexcept;
    bool slotRuns (int s) const noexcept;

    double sr = 48000.0;
    bool active = false;
    int order[16] = {};
    bool on[16] = {};
    bool prevOn[16] = {};
    bool fadeIn[16] = {};
    std::unique_ptr<FxModule> inst[kNumFxModules];
    int period = kControlPeriod;
    int rackId = 0;
    int moduleRack[16] = {};
    float levelGain[16] = {}, levelGainPrev[16] = {};
    FxPool* pool = nullptr;
    bool slotOn[FxPool::kSlots] = {};
    int slotRack[FxPool::kSlots] = {}, slotOrder[FxPool::kSlots] = {}, slotType[FxPool::kSlots] = {};
    bool slotMine[FxPool::kSlots] = {};
    std::array<Plan, 10> plan;
    std::vector<float> vbase;
    fx::SplitterLH splitLh;
    fx::SplitterLMH splitLmh;
    fx::SplitterMS splitMs;
    float scratchA[2][kControlPeriod] = {}, scratchB[2][kControlPeriod] = {};
    int maxBlock = 512;
};
} // namespace own
} // namespace synthyboy
