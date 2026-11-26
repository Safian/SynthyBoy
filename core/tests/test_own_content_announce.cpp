// SynthyBoy - the hand over of the decoded content set to the audio thread of the own engine is wait free: a block makes a fixed, bounded number of steps, always ends up with a set
// that is not freed, and never frees anything itself (OwnEngine::announceContent, publishContent).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The old hand over read `current`, announced it, and read `current` again until the two agreed: lock free, but a publisher that is faster than the loop kept it running without an upper bound.
// The stress test publishes as fast as it can from a second thread (the sets are freed by that thread, as in the engine) while the audio thread renders 32 frame blocks with a voice that reads
// the set, and checks after every block that the set the engine reads has not been freed. A freed set is not given back to the heap at once: the deleter marks it and keeps it in a
// ring of 256 (so the check reads valid memory and an ABA of addresses cannot hide a violation); an ASan / TSan build of this binary checks the real frees.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "alloc_hook.h"
#include "../engine-own/osc/content_set.h"
#include "own_engine.h"
#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr size_t kFreedMark = 0xDEADBEEFu;
constexpr int kBlock = 32;

/** Makes the sets of the test and tracks how many are alive. */
struct SetFactory
{
    std::mutex mutex;
    std::vector<own::ContentSet*> graveyard; // freed sets, kept for a while so that a read of one is a read of marked memory, not of reused memory
    std::atomic<long> created { 0 }, freed { 0 };

    static void drop (SetFactory* self, const own::ContentSet* set)
    {
        auto* s = const_cast<own::ContentSet*> (set);
        s->bytes = kFreedMark;
        own::ContentSet* old = nullptr;
        {
            std::lock_guard<std::mutex> lock (self->mutex);
            self->graveyard.push_back (s);
            if (self->graveyard.size() > 256)
            {
                old = self->graveyard.front();
                self->graveyard.erase (self->graveyard.begin());
            }
        }
        self->freed.fetch_add (1, std::memory_order_relaxed);
        delete old;
    }
    std::shared_ptr<const own::ContentSet> make()
    {
        auto set = own::ContentSet::build (StateContent {}, nullptr);
        set->bytes = static_cast<size_t> (created.fetch_add (1, std::memory_order_relaxed)) + 1u;
        SetFactory* self = this;
        return std::shared_ptr<const own::ContentSet> (set.release(), [self] (const own::ContentSet* p) { drop (self, p); });
    }
    ~SetFactory()
    {
        for (auto* g : graveyard)
            delete g;
    }
};

own::OwnEngine& ownOf (std::unique_ptr<IEngine>& e)
{
    return *dynamic_cast<own::OwnEngine*> (e.get());
}

std::unique_ptr<IEngine> makeEngine()
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 4.0f);
    e->prepare (48000.0, kBlock, qualityForTier (Tier::high));
    // oscillator 1 reads the user wavetable of the content: the engine dereferences the set every block (the voice at its tick, the missing content report)
    for (const char* key : { "osc1.on" })
        if (const ParamInfo* info = findParamByKey (key))
            e->setParam (info->id, 1.0f);
    if (const ParamInfo* info = findParamByKey ("osc1.wavetable"))
        e->setParam (info->id, static_cast<float> (kWavetablePresetTable));
    return e;
}

const own::ContentSet* heldOf (std::unique_ptr<IEngine>& e)
{
    return static_cast<const own::ContentSet*> (ownOf (e).heldContentForTest());
}
} // namespace

TEST (own_content_announce_the_next_block_takes_the_newest_set_in_one_attempt)
{
    SetFactory factory;
    auto engine = makeEngine();
    float l[kBlock], r[kBlock];
    engine->process (nullptr, 0, l, r, kBlock);
    const auto first = ownOf (engine).contentStats();
    CHECK_EQ (first.maxTries, 1u);
    CHECK_EQ (first.fallbacks, 0u);
    for (int i = 0; i < 5; ++i)
    {
        auto set = factory.make();
        const own::ContentSet* raw = set.get();
        engine->setPreparedContent (StateContent {}, set);
        set.reset();
        engine->process (nullptr, 0, l, r, kBlock);
        CHECK (heldOf (engine) == raw);   // published between two blocks: the block that follows reads it (as before)
        CHECK_EQ (ownOf (engine).preparedOscContent().get(), raw);
    }
    const auto st = ownOf (engine).contentStats();
    CHECK_EQ (st.maxTries, 1u);
    CHECK_EQ (st.fallbacks, 0u);
    CHECK_EQ (st.adoptions, 5u);
}

TEST (own_content_announce_old_sets_are_freed_and_the_pinned_one_is_not)
{
    SetFactory factory;
    auto engine = makeEngine();
    float l[2 * kBlock], r[2 * kBlock];
    engine->process (nullptr, 0, l, r, kBlock);
    for (int i = 0; i < 10; ++i)
    {
        engine->setPreparedContent (StateContent {}, factory.make());
        engine->process (nullptr, 0, l, r, 2 * kBlock);   // adopts the set; the period start inside releases the pin of the one before
    }
    engine->setPreparedContent (StateContent {}, factory.make());
    // the audio thread has not seen the last set: it still reads the one before, which is pinned and not freed; every older set is freed
    const own::ContentSet* held = heldOf (engine);
    CHECK (held->bytes != kFreedMark);
    CHECK_EQ (factory.created.load() - factory.freed.load(), 2L);   // the pinned set and the current one (the empty set of the constructor was freed)
    engine->process (nullptr, 0, l, r, 2 * kBlock);
    engine->setPreparedContent (StateContent {}, factory.make());
    CHECK_EQ (factory.created.load() - factory.freed.load(), 2L);
}

namespace
{
/** What the hook of the engine does between the pin and the check of an attempt: waits until the publisher thread has completed two more publications (the one that was in flight when the attempt read `current` may have stored before it; the second one stored after it, so the attempt loses), for every attempt (loseAll) or for every other one. */
struct RaceHook
{
    std::atomic<long>* published = nullptr;
    bool loseAll = true;
    long calls = 0;
    static void run (void* arg)
    {
        auto* self = static_cast<RaceHook*> (arg);
        const bool lose = self->loseAll || (self->calls++ % 2) == 0;
        if (! lose)
            return;
        const long seen = self->published->load();
        const auto t0 = std::chrono::steady_clock::now();
        while (self->published->load() < seen + 2 && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (50))
            std::this_thread::yield();
    }
};

struct StressResult
{
    long blocks = 0, violations = 0, publications = 0;
    own::OwnEngine::ContentStats stats {};
    unsigned long allocs = 0, frees = 0;
    long live = 0;
};

/** The audio thread renders 32 frame blocks with a held note that reads the set, the publisher thread publishes as fast as it can; `mode`: 0 nothing else, 1 every attempt of the hand over loses against a
    publication (the hook), 2 every other attempt loses. */
StressResult stress (int mode, int millis)
{
    SetFactory factory;
    auto engine = makeEngine();
    own::OwnEngine& own = ownOf (engine);
    float l[kBlock], r[kBlock];
    const Event on = sbtest::noteOn (0, 60, 0.8f, 0, 60);
    engine->process (&on, 1, l, r, kBlock);
    std::atomic<bool> stop { false };
    std::atomic<long> published { 0 };
    std::thread publisher ([&]
    {
        while (! stop.load (std::memory_order_relaxed))
        {
            engine->setPreparedContent (StateContent {}, factory.make());
            published.fetch_add (1, std::memory_order_relaxed);
        }
    });
    while (published.load() < 2)   // the publisher is running: from here on every block finds a newer set than the one it holds
        std::this_thread::yield();
    RaceHook hook;
    hook.published = &published;
    hook.loseAll = mode == 1;
    if (mode != 0)
        own.setContentAnnounceHookForTest (&RaceHook::run, &hook);
    StressResult res;
    const unsigned long allocBefore = g_allocations.load();
    const unsigned long freeBefore = g_threadFrees.load();
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds (millis))
    {
        engine->process (nullptr, 0, l, r, kBlock);
        VoiceStats vs;
        engine->voiceStats (vs);   // keeps the readout going: the end of every block reads the set again
        const own::ContentSet* held = heldOf (engine);
        if (held == nullptr || held->bytes == kFreedMark)
            ++res.violations;
        ++res.blocks;
    }
    res.allocs = g_allocations.load() - allocBefore;
    res.frees = g_threadFrees.load() - freeBefore;
    own.setContentAnnounceHookForTest (nullptr, nullptr);
    stop.store (true);
    publisher.join();
    res.publications = published.load();
    res.stats = own.contentStats();
    // the publisher is quiet: the next blocks settle on the last set, and one more publication frees every set but the pinned one and the current one
    float l2[2 * kBlock], r2[2 * kBlock];
    engine->process (nullptr, 0, l2, r2, 2 * kBlock);
    engine->process (nullptr, 0, l2, r2, 2 * kBlock);
    engine->setPreparedContent (StateContent {}, factory.make());
    res.live = factory.created.load() - factory.freed.load();
    if (heldOf (engine)->bytes == kFreedMark)
        ++res.violations;
    return res;
}
} // namespace

TEST (own_content_announce_stress_free_running_publisher)
{
    const int millis = sbtest::quickScaled ("own_content_announce stress (free running publisher), run time (ms)", 5000, 300, 0);
    const StressResult res = stress (0, millis);
    std::printf ("    %ld blocks, %ld publications, announce: max %u attempts, %u fallback(s), %u adoption(s)\n", res.blocks, res.publications, res.stats.maxTries, res.stats.fallbacks, res.stats.adoptions);
    CHECK_EQ (res.violations, 0L);
    CHECK (res.stats.maxTries >= 1u && res.stats.maxTries <= static_cast<uint32_t> (own::OwnEngine::contentAnnounceTries()));
    CHECK (res.stats.adoptions > 0u);
    CHECK_EQ (res.allocs, 0ul);   // the audio thread allocates nothing ...
    CHECK_EQ (res.frees, 0ul);    // ... and frees nothing (the publisher thread frees the retired sets)
    CHECK_EQ (res.live, 2L);
}

TEST (own_content_announce_stress_every_attempt_loses_against_a_publication)
{
    // the old loop would not return while a publication won every attempt; this one gives up after kContentAnnounceTries and keeps the set it has pinned, which is never freed under it
    const int millis = sbtest::quickScaled ("own_content_announce stress (publication wins every attempt), run time (ms)", 3000, 200, 0);
    const StressResult res = stress (1, millis);
    std::printf ("    %ld blocks, %ld publications, announce: max %u attempts, %u fallback(s), %u adoption(s)\n", res.blocks, res.publications, res.stats.maxTries, res.stats.fallbacks, res.stats.adoptions);
    CHECK_EQ (res.violations, 0L);
    CHECK_EQ (res.stats.maxTries, static_cast<uint32_t> (own::OwnEngine::contentAnnounceTries()));
    CHECK_EQ (static_cast<long> (res.stats.fallbacks), res.blocks);   // every block fell back (the settling blocks after the run happen without the hook and are not part of the count)
    CHECK_EQ (res.stats.adoptions, 0u);
    CHECK_EQ (res.allocs, 0ul);
    CHECK_EQ (res.frees, 0ul);
    CHECK_EQ (res.live, 2L);
}

TEST (own_content_announce_stress_every_other_attempt_loses)
{
    const int millis = sbtest::quickScaled ("own_content_announce stress (publication wins every other attempt), run time (ms)", 3000, 200, 0);
    const StressResult res = stress (2, millis);
    std::printf ("    %ld blocks, %ld publications, announce: max %u attempts, %u fallback(s), %u adoption(s)\n", res.blocks, res.publications, res.stats.maxTries, res.stats.fallbacks, res.stats.adoptions);
    CHECK_EQ (res.violations, 0L);
    CHECK (res.stats.maxTries <= static_cast<uint32_t> (own::OwnEngine::contentAnnounceTries()));
    CHECK (res.stats.adoptions > 0u);
    CHECK (res.stats.fallbacks < res.stats.adoptions);   // the free running publisher can win the attempts the hook leaves alone too, but not most of the blocks
    CHECK_EQ (res.allocs, 0ul);
    CHECK_EQ (res.frees, 0ul);
    CHECK_EQ (res.live, 2L);
}
