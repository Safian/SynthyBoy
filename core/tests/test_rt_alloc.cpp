// SynthyBoy - core tests: real-time rule "no allocation in process()" (docs/ARCHITECTURE.md 3.7).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The global operator new / delete of this executable are replaced by counting versions.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#if defined (_WIN32)
 #include <malloc.h>
#endif

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_engines.h"
#include "test_runner.h"
#include "test_util.h"

namespace
{
std::atomic<unsigned long> g_allocations { 0 };
std::atomic<unsigned long> g_frees { 0 };

void* countedAlloc (std::size_t size) noexcept
{
    g_allocations.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size != 0 ? size : 1);
}

void* countedAlignedAlloc (std::size_t size, std::size_t alignment) noexcept
{
    g_allocations.fetch_add (1, std::memory_order_relaxed);
    if (size == 0)
        size = 1;
#if defined (_WIN32)
    return _aligned_malloc (size, alignment);
#else
    void* p = nullptr;
    if (alignment < sizeof (void*))
        alignment = sizeof (void*);
    return posix_memalign (&p, alignment, size) == 0 ? p : nullptr;
#endif
}

void countedFree (void* p) noexcept
{
    if (p != nullptr)
        g_frees.fetch_add (1, std::memory_order_relaxed);
    std::free (p);
}

void countedAlignedFree (void* p) noexcept
{
    if (p != nullptr)
        g_frees.fetch_add (1, std::memory_order_relaxed);
#if defined (_WIN32)
    _aligned_free (p);
#else
    std::free (p);
#endif
}
} // namespace

void* operator new (std::size_t n)                                    { if (void* p = countedAlloc (n)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n)                                  { if (void* p = countedAlloc (n)) return p; throw std::bad_alloc(); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept    { return countedAlloc (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept  { return countedAlloc (n); }
void* operator new (std::size_t n, std::align_val_t a)                { if (void* p = countedAlignedAlloc (n, static_cast<std::size_t> (a))) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n, std::align_val_t a)              { if (void* p = countedAlignedAlloc (n, static_cast<std::size_t> (a))) return p; throw std::bad_alloc(); }
void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept   { return countedAlignedAlloc (n, static_cast<std::size_t> (a)); }
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return countedAlignedAlloc (n, static_cast<std::size_t> (a)); }

void operator delete (void* p) noexcept                                       { countedFree (p); }
void operator delete[] (void* p) noexcept                                     { countedFree (p); }
void operator delete (void* p, std::size_t) noexcept                          { countedFree (p); }
void operator delete[] (void* p, std::size_t) noexcept                        { countedFree (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept                { countedFree (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept              { countedFree (p); }
void operator delete (void* p, std::align_val_t) noexcept                     { countedAlignedFree (p); }
void operator delete[] (void* p, std::align_val_t) noexcept                   { countedAlignedFree (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept        { countedAlignedFree (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept      { countedAlignedFree (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept   { countedAlignedFree (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { countedAlignedFree (p); }

using namespace synthyboy;

namespace
{
void* g_sink = nullptr;
} // namespace

TEST (alloc_hook_counts_allocations)
{
    const auto before = g_allocations.load();
    g_sink = new int (1); // escapes through a global, so the compiler cannot elide it
    CHECK (g_allocations.load() == before + 1);
    delete static_cast<int*> (g_sink);
    g_sink = nullptr;

    const auto beforeVector = g_allocations.load();
    {
        std::vector<int> v (1000, 1);
        g_sink = v.data();
        CHECK (g_allocations.load() > beforeVector);
    }
    g_sink = nullptr;
    const auto beforeAligned = g_allocations.load();
    {
        struct alignas (64) Wide { char c[64]; };
        auto* w = new Wide;
        g_sink = w;
        CHECK (g_allocations.load() == beforeAligned + 1);
        delete w;
        g_sink = nullptr;
    }
}

TEST (process_does_not_allocate)
{
    for (const sbtest::EngineCase& engineCase : sbtest::engineCases())
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
    {
        sbtest::Context context (engineCase.name);
        auto engine = createEngine (engineCase.kind);
        const auto beforePrepare = g_allocations.load();
        engine->prepare (48000.0, 256, qualityForTier (tier)); // allocation is allowed here
        CHECK (g_allocations.load() > beforePrepare);          // ... and the hook does see the engine's allocations

        constexpr int blockSize = 256;
        constexpr int numBlocks = 375; // 2 s at 48 kHz
        static float left[blockSize], right[blockSize];
        static Event events[64];
        sbtest::Rng rng (3);
        bool finite = true;
        int maxActive = 0;

        const auto before = g_allocations.load();
        for (int block = 0; block < numBlocks; ++block)
        {
            int n = 0;
            // a note on at the start of the block, one at 100 and note offs, bends, all-notes-off now and then
            events[n++] = sbtest::noteOn (0, 36 + static_cast<int> (rng.u32() % 48), 0.3f + 0.7f * static_cast<float> (rng.uniform()),
                                          static_cast<int> (rng.u32() % 4), block);
            events[n++] = sbtest::noteOn (100, 48 + static_cast<int> (rng.u32() % 48), 1.0f);
            if (block % 2 == 0)
                events[n++] = sbtest::noteOff (130, 36 + static_cast<int> (rng.u32() % 48), static_cast<int>(rng.u32() % 4));
            if (block % 3 == 0)
                events[n++] = sbtest::pitchBend (150, static_cast<float> (rng.uniform() * 2.0 - 1.0));
            if (block % 40 == 39)
                events[n++] = sbtest::allNotesOff (200);

            // parameter changes from the "UI thread" (here: the same thread) between blocks
            engine->setParam (kParamMasterVolume, -40.0f + static_cast<float> (rng.uniform()) * 40.0f);
            engine->setParam (kParamFilter1Cutoff, 100.0f + static_cast<float> (rng.uniform()) * 10000.0f);
            engine->setParam (kParamOsc1Wavetable, (block / 25) % 2 == 0 ? 0.0f : 1.0f);
            (void) engine->getParam (kParamMasterVolume);

            engine->process (events, n, left, right, blockSize);
            for (int i = 0; i < blockSize; ++i)
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
            maxActive = std::max (maxActive, engine->activeVoices());
        }
        const auto after = g_allocations.load();

        if (! CHECK_EQ (after - before, 0ul))
            std::printf ("    %s, tier %d: %lu allocation(s) during process()\n", engineCase.name, static_cast<int> (tier), after - before);
        CHECK (finite);
        CHECK (maxActive > 0);

        // reset and state-like changes during playback do not allocate either
        const auto beforeReset = g_allocations.load();
        engine->reset();
        engine->process (events, 2, left, right, blockSize);
        CHECK_EQ (g_allocations.load() - beforeReset, 0ul);
    }
}

// Whole patches that change while the engine plays: random parameters of every kind (a few per block), the modulation matrix
// wired and rewired, the quality handed over, notes and bends. Nothing may allocate in process().
TEST (process_does_not_allocate_while_patches_change)
{
    for (const sbtest::EngineCase& engineCase : sbtest::engineCases())
    for (const Tier tier : { Tier::low, Tier::high })
    {
        sbtest::Context context (engineCase.name);
        auto engine = createEngine (engineCase.kind);
        engine->setParam (kParamMasterPolyphony, 32.0f);
        engine->prepare (48000.0, 128, qualityForTier (tier));

        constexpr int blockSize = 128;
        static float left[blockSize], right[blockSize];
        sbtest::Rng rng (tier == Tier::low ? 11 : 12);
        const size_t numParams = paramCount();
        Event events[8];
        bool finite = true;
        unsigned long allocations = 0;

        for (int block = 0; block < 1500; ++block)
        {
            // parameters first: they are the "UI thread" of this test (allocation here is not counted)
            for (int k = 0; k < 6; ++k)
            {
                const auto& info = paramTable()[rng.u32() % numParams];
                engine->setParam (info.id, info.min + static_cast<float> (rng.uniform()) * (info.max - info.min));
            }
            for (int k = 0; k < 3; ++k) // the matrix: slots get a source, a destination, an amount
            {
                const auto slot = modSlotParams (static_cast<int> (rng.u32() % kNumModSlots));
                engine->setParam (paramTable()[slot.source].id, static_cast<float> (rng.u32() % (1 + modSourceCount())));
                engine->setParam (paramTable()[slot.destination].id, static_cast<float> (rng.u32() % (1 + modDestinationCount())));
                engine->setParam (paramTable()[slot.amount].id, -100.0f + 200.0f * static_cast<float> (rng.uniform()));
            }
            if (block % 100 == 50)
            {
                Quality q = qualityForTier (rng.u32() % 2 == 0 ? Tier::low : Tier::high);
                q.maxVoices = 1 + static_cast<int> (rng.u32() % 32);
                engine->setQuality (q);
            }

            int n = 0;
            if (block % 2 == 0)
                events[n++] = sbtest::noteOn (static_cast<int> (rng.u32() % 100), 30 + static_cast<int> (rng.u32() % 60), 0.3f + 0.7f * static_cast<float> (rng.uniform()));
            if (block % 3 == 0)
                events[n++] = sbtest::noteOff (120, 30 + static_cast<int> (rng.u32() % 60));
            std::stable_sort (events, events + n, [] (const Event& a, const Event& b) { return a.frame < b.frame; });

            const auto before = g_allocations.load();
            engine->process (events, n, left, right, blockSize);
            allocations += g_allocations.load() - before;
            for (int i = 0; i < blockSize; ++i)
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
        }

        if (! CHECK_EQ (allocations, 0ul))
            std::printf ("    %s, tier %d: %lu allocation(s) during process()\n", engineCase.name, static_cast<int> (tier), allocations);
        CHECK (finite);
    }
}
