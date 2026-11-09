// SynthyBoy - engine-vital tests: RISE / FALL of a modulation connection (GROUPB WP-08, 13-new-keys.md 4.35): a slew limiter on the source in front of the curve.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "alloc_hook.h"
#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/readout.h"
#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"
#include "vital_engine.h"

using namespace synthyboy;
using sbtest::noteOn;
using sbtest::setByKey;

namespace
{
constexpr double kSr = 48000.0;
/** process () with the allocations of the audio thread counted into `allocs`. */
void renderCounted (IEngine& engine, const std::vector<Event>& events, int totalFrames, int blockSize, unsigned long& allocs)
{
    std::vector<float> l (static_cast<size_t> (blockSize)), r (static_cast<size_t> (blockSize));
    std::vector<Event> local;
    size_t idx = 0;
    for (int start = 0; start < totalFrames; start += blockSize)
    {
        local.clear();
        while (idx < events.size() && events[idx].frame < start + blockSize)
        {
            auto e = events[idx++];
            e.frame -= start;
            local.push_back (e);
        }
        const auto before = g_allocations.load();
        engine.process (local.data(), static_cast<int> (local.size()), l.data(), r.data(), blockSize);
        allocs += g_allocations.load() - before;
    }
}

constexpr int kBlock = 256;

std::unique_ptr<IEngine> make()
{
    auto e = createEngine (EngineKind::vital);
    e->setParam (kParamMasterPolyphony, 16.0f);
    e->prepare (kSr, kBlock, Quality {});
    return e;
}

int paramIndexOf (const char* key) { return findParamIndex (findParamByKey (key)->id); }

void route (IEngine& e, int slot, const char* source, const char* destinationKey, float amountPercent)
{
    char key[32];
    std::snprintf (key, sizeof key, "mod%02d.source", slot);
    setByKey (e, key, sbtest::sourceChoice (source));
    std::snprintf (key, sizeof key, "mod%02d.destination", slot);
    setByKey (e, key, static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (paramIndexOf (destinationKey)))));
    std::snprintf (key, sizeof key, "mod%02d.amount", slot);
    setByKey (e, key, amountPercent);
}

StateContent slewContent (int slot, float riseMs, float fallMs, std::vector<ContentPoint> points = {})
{
    StateContent c;
    ContentModCurve mc;
    mc.slot = slot;
    mc.riseMs = riseMs;
    mc.fallMs = fallMs;
    mc.points = std::move (points);
    c.modCurves.push_back (mc);
    return c;
}

/** The live value of osc1.pan (macro 1 -> pan with +50 % of the 200 wide range: pan = 100 x the slewed macro) after every block; the macro steps at `stepBlock`. */
std::vector<float> stepResponse (IEngine& e, float from, float to, int stepBlock, int blocks)
{
    const int targets[] = { paramIndexOf ("osc1.pan") };
    e.setReadoutTargets (targets, 1);
    setByKey (e, "macro.1", from);
    std::vector<float> response;
    std::vector<float> l (kBlock), r (kBlock);
    for (int block = 0; block < blocks; ++block)
    {
        if (block == stepBlock)
            setByKey (e, "macro.1", to);
        const Event on = noteOn (0, 60, 0.8f);
        e.process (block == 0 ? &on : nullptr, block == 0 ? 1 : 0, l.data(), r.data(), kBlock);
        LiveValues v;
        CHECK (e.copyReadout (v));
        response.push_back (v.target[0]);
    }
    return response;
}

int blocksToReach (const std::vector<float>& response, int stepBlock, float level, bool upwards)
{
    for (size_t b = static_cast<size_t> (stepBlock); b < response.size(); ++b)
        if (upwards ? response[b] >= level : response[b] <= level)
            return static_cast<int> (b) - stepBlock + 1;
    return -1;
}
} // namespace

TEST (slew_step_response_rise_reaches_90_percent_in_the_set_time)
{
    for (const float riseMs : { 100.0f, 400.0f })
    {
        auto e = make();
        route (*e, 1, "macro1", "osc1.pan", 50.0f);
        e->setContent (slewContent (1, riseMs, 0.0f));
        const auto response = stepResponse (*e, 0.0f, 100.0f, 8, 260);
        // before the step: the base value; after it the value climbs to 100 (pan +100 %)
        CHECK_NEAR (response[4], 0.0f, 0.5f);

        const int blocks = blocksToReach (response, 8, 90.0f, true);
        const double ms = blocks * kBlock * 1000.0 / kSr;
        std::printf ("    rise %g ms: 90 %% after %d blocks = %g ms\n", static_cast<double> (riseMs), blocks, ms);
        CHECK (blocks > 0);
        CHECK (ms >= riseMs - kBlock * 1000.0 / kSr && ms <= riseMs + kBlock * 1000.0 / kSr);   // within one block
        // monotone, and approaching 100
        for (size_t b = 9; b < response.size(); ++b)
            CHECK (response[b] >= response[b - 1] - 1.0e-3f);
        CHECK (response.back() > 99.0f);
        // the first block after the step is a fraction of it (an exponential: 1 - 10^(-t / rise))
        const double expectedFirst = 100.0 * (1.0 - std::pow (10.0, -(kBlock / kSr) * 1000.0 / riseMs));
        CHECK_NEAR (response[8], expectedFirst, 1.5);
    }
}

TEST (slew_fall_is_its_own_time_and_rise_only_leaves_the_fall_instant)
{
    {
        auto e = make();
        route (*e, 1, "macro1", "osc1.pan", 50.0f);
        e->setContent (slewContent (1, 0.0f, 300.0f));
        const auto response = stepResponse (*e, 100.0f, 0.0f, 40, 160);
        const int up = blocksToReach (response, 0, 99.0f, true);
        CHECK (up >= 1 && up <= 2);   // the rise is instant: the voice starts on its value
        const int blocks = blocksToReach (response, 40, 10.0f, false);
        const double ms = blocks * kBlock * 1000.0 / kSr;
        std::printf ("    fall 300 ms: 90 %% down after %g ms\n", ms);
        CHECK (ms >= 300.0 - 6.0 && ms <= 300.0 + 6.0);
    }
    {
        auto e = make();
        route (*e, 1, "macro1", "osc1.pan", 50.0f);
        e->setContent (slewContent (1, 500.0f, 0.0f));
        const auto response = stepResponse (*e, 100.0f, 0.0f, 40, 80);
        CHECK (response[40] < 1.0f);  // the fall is instant, the rise is the slow one
    }
}

TEST (slew_defaults_are_bit_identical_and_other_slots_do_not_leak)
{
    const auto render = [] (const StateContent& content)
    {
        auto e = make();
        setByKey (*e, "lfo1.rate", 4.0f);
        setByKey (*e, "lfo1.sync", 0.0f);
        route (*e, 1, "lfo1", "osc1.pan", 25.0f);
        setByKey (*e, "osc1.destination", 3.0f);
        setByKey (*e, "osc1.level", 60.0f);
        e->setContent (content);
        return sbtest::render (*e, { noteOn (0, 60, 0.8f) }, 24000, kBlock);
    };
    const auto without = render ({});
    CHECK (sbtest::rmsOf (without.left, 4000, 20000) > 0.001);
    const auto otherSlot = render (slewContent (2, 800.0f, 800.0f));            // slot 2 is not used
    CHECK (without.left == otherSlot.left && without.right == otherSlot.right);
    const auto drawnOnly = render (slewContent (1, 0.0f, 0.0f, { ContentPoint (0.0f, 0.0f, 0.0f), ContentPoint (1.0f, 1.0f, 0.0f) }));
    CHECK (without.left == drawnOnly.left && without.right == drawnOnly.right);  // a straight curve and no slew: the connection as it was
    const auto slewed = render (slewContent (1, 600.0f, 600.0f));
    CHECK (without.left != slewed.left);
    CHECK (sbtest::allFinite (slewed.left));
}

TEST (slew_acts_before_the_curve_and_per_voice)
{
    // curve and slew together: the slewed source goes through the drawn curve (a curve that is 1 at 0.5 and above makes the slewed value jump to full there)
    {
        auto e = make();
        route (*e, 1, "macro1", "osc1.pan", 50.0f);
        e->setContent (slewContent (1, 400.0f, 0.0f, { ContentPoint (0.0f, 0.0f, 0.0f), ContentPoint (0.5f, 1.0f, 0.0f), ContentPoint (1.0f, 1.0f, 0.0f) }));
        const auto response = stepResponse (*e, 0.0f, 100.0f, 8, 80);
        // the source climbs slowly, the curve (steeper than 2) makes the output reach 90 % when the source is at 45 %: sooner than the 400 ms of the source
        const int blocks = blocksToReach (response, 8, 90.0f, true);
        const double ms = blocks * kBlock * 1000.0 / kSr;
        std::printf ("    curve + rise 400 ms: 90 %% after %g ms\n", ms);
        CHECK (ms > 40.0 && ms < 250.0);
    }
    // per voice: a note that starts later starts on its own value (a reset takes the source value), the older voice keeps its slewed value
    {
        auto e = make();
        route (*e, 1, "velocity", "osc1.level", 60.0f);
        e->setContent (slewContent (1, 1000.0f, 1000.0f));
        setByKey (*e, "osc1.destination", 3.0f);
        setByKey (*e, "filter1.on", 0.0f);
        const auto r = sbtest::render (*e, { noteOn (0, 48, 1.0f), noteOn (12000, 60, 1.0f) }, 24000, kBlock);
        CHECK (sbtest::allFinite (r.left));
    }
}

TEST (slew_on_an_audio_rate_source_smooths_the_connection)
{
    // the amplitude envelope is the one audio rate source: env 1 -> osc 1 pan with a slow rise stays centred longer than without
    const auto balance = [] (float riseMs)
    {
        auto e = make();
        setByKey (*e, "osc1.destination", 3.0f);
        setByKey (*e, "osc1.level", 60.0f);
        setByKey (*e, "env1.attack", 1.0f);
        setByKey (*e, "env1.sustain", 100.0f);
        route (*e, 1, "env1", "osc1.pan", 100.0f);
        if (riseMs > 0.0f)
            e->setContent (slewContent (1, riseMs, 0.0f));
        const auto r = sbtest::render (*e, { noteOn (0, 48, 1.0f) }, 24000, kBlock);
        const auto rms = [&] (const std::vector<float>& x, double t0, double t1) { return sbtest::rmsOf (x, static_cast<size_t> (t0 * kSr), static_cast<size_t> (t1 * kSr)); };
        // right / left at 50 ms and at 400 ms
        return std::pair<double, double> { rms (r.right, 0.04, 0.06) / std::max (1.0e-9, rms (r.left, 0.04, 0.06)),
                                           rms (r.right, 0.40, 0.45) / std::max (1.0e-9, rms (r.left, 0.40, 0.45)) };
    };
    const auto fast = balance (0.0f), slow = balance (300.0f);
    std::printf ("    right / left at 50 ms and 400 ms: no slew %g %g, rise 300 ms %g %g\n", fast.first, fast.second, slow.first, slow.second);
    CHECK (fast.first > 3.0 * slow.first);      // without the slew the pan has gone right by 50 ms, with it it has barely started
    CHECK (slow.second > 3.0 * slow.first);     // and it gets there later
}

TEST (slew_never_allocates_on_the_audio_thread)
{
    auto e = make();
    setByKey (*e, "osc1.destination", 3.0f);
    setByKey (*e, "osc1.level", 60.0f);
    setByKey (*e, "lfo1.rate", 3.0f);
    setByKey (*e, "lfo1.sync", 0.0f);
    route (*e, 1, "lfo1", "osc1.pan", 25.0f);
    route (*e, 2, "env1", "osc1.tune", 10.0f);   // the audio rate source
    unsigned long allocs = 0;
    renderCounted (*e, { noteOn (0, 60, 0.8f) }, 2048, kBlock, allocs);   // warm up: the matrix is wired
    allocs = 0;
    StateContent content = slewContent (1, 200.0f, 300.0f);
    ContentModCurve second;
    second.slot = 2;
    second.riseMs = 150.0f;
    content.modCurves.push_back (second);
    e->setContent (content);
    renderCounted (*e, { noteOn (0, 64, 0.8f) }, 8192, kBlock, allocs);
    CHECK_EQ (allocs, 0ul);
}
