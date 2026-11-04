// SynthyBoy - core tests: audio thread vs. parameter thread (meaningful under ThreadSanitizer).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

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
        }
        readerSum.store (sum);
    });

    // audio thread (this one)
    sbtest::Rng rng (23);
    std::vector<float> l (128), r (128);
    bool finite = true;
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < 3000; ++block)
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
