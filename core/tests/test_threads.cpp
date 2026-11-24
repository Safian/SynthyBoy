// SynthyBoy - core tests: audio thread vs. parameter thread (meaningful under ThreadSanitizer).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <string_view>
#include <thread>
#include <vector>

#include "../engine-own/voice/own_content.h"
#include "synthyboy/engine.h"
#include "test_engines.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
void setParamFromOtherThreadsWhileProcessing (const sbtest::EngineCase& engineCase)
{
    sbtest::Context context (engineCase.name);
    auto engine = createEngine (engineCase.kind);
    engine->prepare (48000.0, 128, Quality {});

    std::atomic<bool> done { false };
    std::atomic<long> setterCalls { 0 };

    // "UI / host automation" thread: hammers setParam
    std::thread setter ([&]
    {
        sbtest::Rng rng (17);
        long calls = 0;
        while (! done.load (std::memory_order_acquire))
        {
            for (size_t i = 0; i < paramCount(); ++i)
            {
                const auto& info = paramTable()[i];
                engine->setParam (info.id, info.min + static_cast<float> (rng.uniform()) * (info.max - info.min));
                ++calls;
            }
            std::this_thread::yield(); // a sweep of every parameter at a time: the audio thread below must get its share on a two core machine (TSan CI runner)
        }
        setterCalls.store (calls);
    });

    // second thread: reads parameters and the voice count (e.g. a UI timer)
    std::atomic<long> readerSum { 0 };
    std::thread reader ([&]
    {
        long sum = 0;
        while (! done.load (std::memory_order_acquire))
        {
            for (size_t i = 0; i < paramCount(); ++i)
                sum += engine->getParam (paramTable()[i].id) > 0.0f ? 1 : 0;
            sum += engine->activeVoices();
            std::this_thread::yield();
        }
        readerSum.store (sum);
    });

    // audio thread (this one)
    sbtest::Rng rng (23);
    std::vector<float> l (128), r (128);
    bool finite = true;
    const auto start = std::chrono::steady_clock::now();
    // 3000 blocks natively; under ThreadSanitizer / QEMU (sbtest::instrumented ()) the same races are exercised with 100 blocks (the 1500 s limit of the ctest entry was hit)
    const int blocks = sbtest::scaled (3000, 100);
    for (int block = 0; block < blocks; ++block)
    {
        std::vector<Event> ev;
        if (block % 4 == 0)
            ev.push_back (sbtest::noteOn (static_cast<int> (rng.u32() % 128), 40 + static_cast<int> (rng.u32() % 40)));
        if (block % 6 == 0)
            ev.push_back (sbtest::noteOff (127, 40 + static_cast<int> (rng.u32() % 40)));
        std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return a.frame < b.frame; });
        engine->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 128);
        for (int i = 0; i < 128; ++i)
            finite = finite && std::isfinite (l[i]) && std::fabs (l[i]) < 20.0f;
    }
    (void) start;

    done.store (true, std::memory_order_release);
    setter.join();
    reader.join();

    CHECK (finite);
    CHECK (setterCalls.load() > 0);
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& info = paramTable()[i];
        const float v = engine->getParam (info.id);
        CHECK (v >= info.min && v <= info.max);
    }
}
} // namespace

TEST (set_param_from_other_threads_while_processing)
{
    for (const auto& engineCase : sbtest::engineCases())
        setParamFromOtherThreadsWhileProcessing (engineCase);
}

namespace
{
StateContent constantContent (float value)
{
    StateContent content;
    auto& lfo = content.lfoShapes.emplace_back();
    lfo.lfo = 1;
    lfo.points = { { 0.0f, value }, { 1.0f, value } };
    auto& curve = content.modCurves.emplace_back();
    curve.slot = 1;
    curve.points = lfo.points;
    return content;
}

int lfoSourceIndex()
{
    const auto* sources = modSourceTable();
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (std::string_view (sources[i].key) == "lfo1")
            return static_cast<int> (i);
    return -1;
}

bool snapshotHas (const own::OwnContent* snapshot, float value)
{
    return snapshot != nullptr && snapshot->lfo[0].present && snapshot->lfo[0].fixed.points[0].y == value
        && snapshot->modCurve[0].present && snapshot->modCurve[0].table.front() == value
        && snapshot->modCurve[0].table.back() == value;
}
}

TEST (content_hub_retains_the_audio_snapshot_until_the_next_acquire)
{
    own::ContentHub hub;
    CHECK (hub.acquire().content == nullptr);
    hub.publish (constantContent (0.125f));
    const auto first = hub.acquire();
    const auto reused = hub.acquire();
    CHECK (first.generation == 1 && reused.content == first.content && reused.generation == first.generation);
    for (int i = 0; i < 64; ++i)
        hub.publish (constantContent (0.25f));
    CHECK (snapshotHas (first.content, 0.125f)); // the old eight-slot ring frees this object
    const auto latest = hub.acquire();
    CHECK (latest.generation == 65 && latest.content != first.content);
    CHECK (snapshotHas (latest.content, 0.25f));
}

TEST (content_hub_serializes_concurrent_publishers)
{
    own::ContentHub hub;
    const auto first = constantContent (0.125f);
    const auto second = constantContent (0.875f);
    hub.publish (first);
    std::atomic<bool> start { false };
    std::atomic<unsigned> done { 0 };
    constexpr int updates = 512;
    auto publish = [&] (const StateContent& value)
    {
        while (! start.load (std::memory_order_acquire))
            std::this_thread::yield();
        for (int i = 0; i < updates; ++i)
            hub.publish (value);
        done.fetch_add (1, std::memory_order_release);
    };
    std::thread a (publish, std::cref (first));
    std::thread b (publish, std::cref (second));
    start.store (true, std::memory_order_release);

    uint64_t generation = 0;
    bool coherent = true;
    do
    {
        const auto snap = hub.acquire();
        const bool monotonic = snap.generation >= generation;
        std::this_thread::yield(); // give publishers time to rotate both of their available slots
        coherent &= monotonic && (snapshotHas (snap.content, 0.125f) || snapshotHas (snap.content, 0.875f));
        generation = snap.generation;
    } while (done.load (std::memory_order_acquire) < 2);
    a.join();
    b.join();
    CHECK (coherent);
    CHECK (hub.acquire().generation == 1 + 2 * updates);
}

TEST (content_hub_independent_audio_instances_follow_live_content)
{
    const int source = lfoSourceIndex();
    if (! CHECK (source >= 0))
        return;
    const auto cutoff = findParamByKey ("filter1.cutoff");
    const int target = findParamIndex (cutoff->id);
    auto create = [&]
    {
        auto engine = createOwnEngine();
        engine->prepare (48000.0, 128, qualityForTier (Tier::low));
        auto set = [&] (const char* key, float value) { engine->setParam (findParamByKey (key)->id, value); };
        set ("filter1.on", 1);
        set ("filter1.cutoff", 1000);
        set ("lfo1.shape", 12);
        set ("lfo1.smooth", 0);
        set ("mod01.source", static_cast<float> (source + 1));
        set ("mod01.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (target))));
        set ("mod01.amount", 25);
        set ("mod01.bipolar", 0);
        engine->setReadoutTargets (&target, 1);
        return engine;
    };
    auto first = create();
    auto second = create();
    std::atomic<unsigned> ready { 0 };
    const int updates = sbtest::scaled (256, 96);
    auto run = [&] (IEngine& engine, float initial, float final)
    {
        const auto initialContent = constantContent (initial);
        const auto finalContent = constantContent (final);
        engine.setContent (initialContent);
        std::atomic<bool> published { false };
        std::thread writer ([&]
        {
            while (ready.load (std::memory_order_acquire) < 2)
                std::this_thread::yield();
            for (int i = 0; i < updates; ++i)
                engine.setContent (i % 2 == 0 ? initialContent : finalContent);
            engine.setContent (finalContent);
            published.store (true, std::memory_order_release);
        });
        std::array<float, 128> left {}, right {};
        auto block = [&] (const Event* event = nullptr)
        {
            engine.process (event, event != nullptr ? 1 : 0, left.data(), right.data(), static_cast<int> (left.size()));
        };
        auto matches = [&] (float value)
        {
            LiveValues out;
            // The constant slot curve applies 25% of the cutoff's 128-semitone modulation range.
            const float expectedCutoff = 1000.0f * std::exp2 (value * 32.0f / 12.0f);
            return engine.copyReadout (out) && out.activeVoices > 0 && out.numTargets == 1
                && std::fabs (out.source[source] - (2.0f * value - 1.0f)) < 1.0e-5f
                && std::fabs (out.target[0] - expectedCutoff) < 0.5f;
        };
        const Event note = sbtest::noteOn (0, 60);
        block (&note);
        const bool startedWithOwnCurve = matches (initial);
        ready.fetch_add (1, std::memory_order_release);
        bool consistent = true;
        do
        {
            block();
            consistent &= matches (initial) || matches (final);
        } while (! published.load (std::memory_order_acquire));
        writer.join();
        block();
        return startedWithOwnCurve && consistent && matches (final);
    };
    bool firstOk = false;
    std::thread other ([&] { firstOk = run (*first, 0.125f, 0.375f); });
    const bool secondOk = run (*second, 0.625f, 0.875f);
    other.join();
    CHECK (firstOk);
    CHECK (secondOk);
}
