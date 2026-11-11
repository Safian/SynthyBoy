// SynthyBoy - engine-vital tests: the effect instance pool and the real-time rules (docs/ui-design/18-FX-INSTANCES.md section 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The global operator new / delete of this executable are replaced by counting versions (alloc_hook.h; only the thread that runs process () is
// counted: the builder thread of the pool may allocate, the audio thread must not). The handover test is meant to be run under ThreadSanitizer
// as well (CI job; docs/ui-design/FXINSTANCES-REPORT.md).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "alloc_hook.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "test_dsp.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOn;
using sbtest::setByKey;

// the timing check is meaningless under a sanitizer (5 to 15 times slower) or an emulator (QEMU user mode: SBTEST_INSTRUMENTED=1, see sbtest::instrumented ()); the native jobs run it

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

std::unique_ptr<IEngine> makeEngine (bool realtime, Tier tier = Tier::high)
{
    auto e = createEngine (EngineKind::vital);
    Quality q = qualityForTier (tier);
    q.realtime = realtime;
    q.fxMask = kFxAll;
    e->prepare (kSr, kBlock, q);
    setByKey (*e, "osc1.wavetable", 1.0f);
    setByKey (*e, "osc1.level", 100.0f);
    setByKey (*e, "osc1.unison.voices", 3.0f);
    setByKey (*e, "osc1.destination", 3.0f);
    setByKey (*e, "filter1.on", 0.0f);
    setByKey (*e, "master.volume", -12.0f);
    return e;
}

void setSlot (IEngine& e, int slot, int type, int rack, int order, float p)
{
    const FxInstanceKeys& k = fxInstanceKeys (slot);
    e.setParam (paramTable()[k.type].id, static_cast<float> (type));
    e.setParam (paramTable()[k.on].id, type != kFxTypeNone ? 1.0f : 0.0f);
    e.setParam (paramTable()[k.rack].id, static_cast<float> (rack));
    e.setParam (paramTable()[k.order].id, static_cast<float> (order));
    float defaults[kFxInstanceParams];
    fxInstanceDefaults (type, defaults);
    for (int i = 0; i < kFxInstanceParams; ++i)
        e.setParam (paramTable()[k.p[i]].id, p < 0.0f ? defaults[i] : p);
}

const int kHeavy[16] = { kFxTypeReverb, kFxTypeCompressor, kFxTypeDelay, kFxTypeConvolve, kFxTypeBode, kFxTypeHyperDimension, kFxTypeDistortion,
                         kFxTypeFilter, kFxTypeEq, kFxTypePhaser, kFxTypeFlanger, kFxTypeChorus, kFxTypeReverb, kFxTypeDelay, kFxTypeConvolve, kFxTypeUtility };

bool allModulesReady (const IEngine& e)
{
    for (int s = 0; s < kNumFxInstances; ++s)
        if (! vitalHasPoolModule (e, s))
            return false;
    return true;
}

/** Runs blocks until every slot has its module (realtime builds happen on the builder thread). */
bool runUntilReady (IEngine& e, float* left, float* right, double timeoutSeconds)
{
    const auto start = std::chrono::steady_clock::now();
    while (! allModulesReady (e))
    {
        e.process (nullptr, 0, left, right, kBlock);
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
        if (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() > timeoutSeconds)
            return false;
    }
    return true;
}
} // namespace

TEST (fxi_sixteen_active_slots_do_not_allocate_on_the_audio_thread)
{
    for (const bool realtime : { false, true })
    {
        auto e = makeEngine (realtime);
        for (int s = 0; s < 16; ++s)
            setSlot (*e, s, kHeavy[s], s % 3, 100 * s, -1.0f);
        static float left[kBlock], right[kBlock];
        // offline: the modules are there after the first block (the audio thread waits for the builder, it allocates nothing itself);
        // realtime: they arrive when the builder is done
        CHECK (runUntilReady (*e, left, right, 30.0) || ! realtime);
        e->process (nullptr, 0, left, right, kBlock);
        CHECK (allModulesReady (*e));

        sbtest::Rng rng (5);
        bool finite = true;
        const auto before = g_allocations.load();
        for (int block = 0; block < 600; ++block)
        {
            Event note[1] = { noteOn (0, 40 + static_cast<int> (rng.u32() % 40)) };
            e->process (block % 40 == 0 ? note : nullptr, block % 40 == 0 ? 1 : 0, left, right, kBlock);
            // parameter changes, a type change now and then: the request goes to the builder (realtime) or the audio thread waits for it
            if (block % 7 == 0)
            {
                const int s = static_cast<int> (rng.u32() % 16);
                const int k = static_cast<int> (rng.u32() % kFxInstanceParams);
                e->setParam (paramTable()[fxInstanceKeys (s).p[k]].id, static_cast<float> (rng.uniform()));
            }
            if (block % 150 == 100)
            {
                const int s = static_cast<int> (rng.u32() % 16);
                e->setParam (paramTable()[fxInstanceKeys (s).type].id, static_cast<float> (1 + rng.u32() % 4));   // Chorus .. Distortion
            }
            for (int i = 0; i < kBlock; ++i)
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
        }
        const auto after = g_allocations.load();
        std::printf ("    %s: allocations on the audio thread during 600 blocks with 16 active slots and type changes: %lu\n",
                     realtime ? "realtime" : "offline", after - before);
        CHECK (after == before);
        CHECK (finite);
    }
}

TEST (fxi_sixteen_heavy_slots_fit_the_block_budget_of_the_high_tier)
{
    auto e = makeEngine (false);
    for (int s = 0; s < 16; ++s)
        setSlot (*e, s, kHeavy[s], s % 3, 100 * s, -1.0f);
    setByKey (*e, "osc1.bus1_send", 100.0f);
    setByKey (*e, "osc1.bus2_send", 100.0f);
    static float left[kBlock], right[kBlock];
    e->process (nullptr, 0, left, right, kBlock);   // builds the modules
    CHECK (allModulesReady (*e));
    std::vector<double> ms;
    sbtest::Rng rng (11);
    for (int block = 0; block < 1500; ++block)
    {
        Event note[1] = { noteOn (0, 36 + static_cast<int> (rng.u32() % 48)) };
        const auto t0 = std::chrono::steady_clock::now();
        e->process (block % 50 == 0 ? note : nullptr, block % 50 == 0 ? 1 : 0, left, right, kBlock);
        ms.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
    }
    std::sort (ms.begin(), ms.end());
    double mean = 0.0;
    for (double v : ms)
        mean += v;
    mean /= static_cast<double> (ms.size());
    const double budget = 1000.0 * kBlock / kSr;   // 2.667 ms per block of 128 frames at 48 kHz
    std::printf ("    16 heavy slots, 3 unison voices: mean %.3f ms, p99 %.3f ms, worst %.3f ms per block of %d frames (budget %.3f ms)\n",
                 mean, ms[ms.size() * 99 / 100], ms.back(), kBlock, budget);
    CHECK (mean < budget || sbtest::instrumented());
}

TEST (fxi_handover_while_the_audio_thread_runs_and_parameters_change_from_another_thread)
{
    // For ThreadSanitizer: one thread renders, another one changes types, switches and parameters of the slots as a UI / a host would; the
    // pool builds on its own thread and hands over through atomics.
    auto e = makeEngine (true);
    std::atomic<bool> stop { false };
    std::thread ui ([&]
    {
        sbtest::Rng rng (99);
        while (! stop.load())
        {
            const int s = static_cast<int> (rng.u32() % 16);
            const FxInstanceKeys& k = fxInstanceKeys (s);
            switch (rng.u32() % 6)
            {
                case 0: e->setParam (paramTable()[k.type].id, static_cast<float> (rng.u32() % kNumFxInstanceTypes)); break;
                case 1: e->setParam (paramTable()[k.on].id, static_cast<float> (rng.u32() % 2)); break;
                case 2: e->setParam (paramTable()[k.rack].id, static_cast<float> (rng.u32() % 3)); break;
                case 3: e->setParam (paramTable()[k.order].id, static_cast<float> (rng.u32() % 1024)); break;
                default: e->setParam (paramTable()[k.p[rng.u32() % kFxInstanceParams]].id, static_cast<float> (rng.uniform())); break;
            }
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });

    static float left[kBlock], right[kBlock];
    bool finite = true;
    const auto start = std::chrono::steady_clock::now();
    int block = 0;
    while (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() < 3.0)
    {
        Event note[1] = { noteOn (0, 48 + (block / 20) % 24) };
        e->process (block % 20 == 0 ? note : nullptr, block % 20 == 0 ? 1 : 0, left, right, kBlock);
        for (int i = 0; i < kBlock; ++i)
            finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
        ++block;
        std::this_thread::sleep_for (std::chrono::microseconds (500));
    }
    stop.store(true);
    ui.join();
    CHECK (finite);
    CHECK (block > 100);
    // the engine is destroyed with builds possibly in flight and modules in the chain
    e.reset();
}
