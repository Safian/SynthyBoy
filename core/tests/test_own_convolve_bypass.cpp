// SynthyBoy - own engine tests: Convolve at tier low (quality.h: the heavy effects are bypassed) really is bypassed in the engine for a preset with convolve.on = 1 and an impulse loaded: the render
// equals the render of the same patch with Convolve off, sample for sample, and the Convolve module neither runs nor starts its helper-thread build. At tier high the same patch sounds
// different (so the impulse is loaded and Convolve is on in the test).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/user_content.h"
#include "fx/fx_makeup.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
class OneFileStore final : public IContentStore
{
public:
    explicit OneFileStore (std::vector<uint8_t> b) : bytes (std::move (b)) {}
    bool readFile (const ContentFileRef&, std::vector<uint8_t>& out) const override
    {
        out = bytes;
        return true;
    }

private:
    std::vector<uint8_t> bytes;
};

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

sbtest::Rendered play (Tier tier, bool convolveOn, const IContentStore* store, bool makeup = true)
{
    auto e = createOwnEngine();
    Quality q = qualityForTier (tier);
    q.realtime = false;
    q.fxMakeup = makeup;
    e->prepare (48000.0, 256, q);
    set (*e, "master.volume", 0);
    set (*e, "osc1.level", 100);
    set (*e, "osc1.random_phase", 0);
    set (*e, "env1.attack", 1);
    set (*e, "convolve.on", convolveOn ? 1.0f : 0.0f);
    set (*e, "convolve.ir", 1);   // the impulse of the content
    set (*e, "convolve.mix", 100);
    e->setContentStore (store);
    StateContent content;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "ir.wav";
    ref.name = "ir";
    content.impulses.push_back (ref);
    e->setContent (content);
    return sbtest::render (*e, { sbtest::noteOn (0, 60) }, 48000, 256);
}

bool same (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    return a.left.size() == b.left.size() && std::memcmp (a.left.data(), b.left.data(), a.left.size() * sizeof (float)) == 0
        && std::memcmp (a.right.data(), b.right.data(), a.right.size() * sizeof (float)) == 0;
}
} // namespace

TEST (own_convolve_is_bypassed_at_tier_low)
{
    std::vector<float> ir (6000);
    for (size_t i = 0; i < ir.size(); ++i)
        ir[i] = (i % 97 == 0 ? 0.6f : 0.0f) * std::exp (-static_cast<float> (i) / 1500.0f);
    const OneFileStore store (encodeWav (ir.data(), ir.data(), ir.size(), 48000, 16));
    // the bypass itself: with the level makeup off the renders with Convolve on and off are the same samples
    const auto lowOn = play (Tier::low, true, &store, false), lowOff = play (Tier::low, false, &store, false);
    CHECK (same (lowOn, lowOff));
    // with the level makeup (the default, tier-makeup branch) the bypassed Convolve puts a gain in its place: not the same samples, but
    // the impulse is still not convolved, so the signal is the dry note at another level (energy within a factor 4 either way)
    const auto lowMk = play (Tier::low, true, &store, true);
    double eMk = 0.0, eOff = 0.0;
    for (size_t i = 0; i < lowMk.left.size(); ++i)
    {
        eMk += static_cast<double> (lowMk.left[i]) * lowMk.left[i];
        eOff += static_cast<double> (lowOff.left[i]) * lowOff.left[i];
    }
    CHECK (eMk > 0.25 * eOff && eMk < 4.0 * eOff);
    double energy = 0.0;
    for (float v : lowOn.left)
        energy += static_cast<double> (v) * v;
    CHECK (energy > 1.0e-6); // the note sounds
    const auto highOn = play (Tier::high, true, &store), highOff = play (Tier::high, false, &store);
    CHECK (! same (highOn, highOff)); // at tier high Convolve is on: the impulse is loaded and the test patch really has it on
}

// MK1 (docs/release/CPU-REVIEW-CONTENT.md): the loudness makeup of the bypassed Convolve with its caches (the constants of the meter, the weights of the program, the key of the impulse, the level gains, the
// smoothing constant) is bit for bit the makeup that computes everything per call (makeup::setLegacyCachesForTest). Tier low, a note, and the Convolve keys moved between the segments of the render (size,
// decay, attack, damp, tone, pre delay, mix, IR gain, level, the law), at two sample rates.
namespace
{
sbtest::Rendered playMoving (double sampleRate, const IContentStore* store)
{
    auto e = createOwnEngine();
    Quality q = qualityForTier (Tier::low);
    q.realtime = false;
    q.fxMakeup = true;
    e->prepare (sampleRate, 256, q);
    set (*e, "master.volume", 0);
    set (*e, "osc1.level", 100);
    set (*e, "osc1.random_phase", 0);
    set (*e, "env1.attack", 1);
    set (*e, "convolve.on", 1.0f);
    set (*e, "convolve.ir", 1);
    set (*e, "convolve.mix", 100);
    e->setContentStore (store);
    StateContent content;
    ContentFileRef ref;
    ref.pack = "test";
    ref.file = "ir.wav";
    ref.name = "ir";
    content.impulses.push_back (ref);
    e->setContent (content);
    sbtest::Rendered all;
    const auto append = [&] (const sbtest::Rendered& r)
    {
        all.left.insert (all.left.end(), r.left.begin(), r.left.end());
        all.right.insert (all.right.end(), r.right.begin(), r.right.end());
    };
    append (sbtest::render (*e, { sbtest::noteOn (0, 60) }, 6000, 256));
    for (int step = 0; step < 8; ++step)
    {
        const float f = static_cast<float> (step);
        set (*e, "convolve.size", 20.0f + 9.0f * f);
        set (*e, "convolve.decay", 100.0f - 7.0f * f);
        set (*e, "convolve.attack", 3.0f * f);
        set (*e, "convolve.damp", 5.0f * f);
        set (*e, "convolve.tone", -50.0f + 14.0f * f);
        set (*e, "convolve.predelay", 2.0f * f);
        set (*e, "convolve.mix", 100.0f - 6.0f * f);
        set (*e, "convolve.ir_gain", -3.0f + f);
        set (*e, "convolve.level", -2.0f * f);
        set (*e, "convolve.law", step % 2 == 0 ? 0.0f : 1.0f);
        append (sbtest::render (*e, step == 4 ? std::vector<Event> { sbtest::noteOn (0, 67) } : std::vector<Event> {}, 5000, 256));
    }
    return all;
}
} // namespace

TEST (own_convolve_makeup_caches_are_bit_identical_to_the_per_call_computation)
{
    std::vector<float> ir (6000);
    for (size_t i = 0; i < ir.size(); ++i)
        ir[i] = (i % 97 == 0 ? 0.6f : 0.0f) * std::exp (-static_cast<float> (i) / 1500.0f);
    int compared = 0;
    for (double sr : { 48000.0, 44100.0 })
    {
        const OneFileStore store (encodeWav (ir.data(), ir.data(), ir.size(), static_cast<int> (sr), 16));
        double ms[2] = {};
        sbtest::Rendered r[2];
        for (int legacy = 0; legacy < 2; ++legacy)
        {
            synthyboy::own::makeup::setLegacyCachesForTest (legacy != 0);
            const auto t0 = std::chrono::steady_clock::now();
            r[legacy] = playMoving (sr, &store);
            ms[legacy] = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        }
        synthyboy::own::makeup::setLegacyCachesForTest (false);
        ++compared;
        CHECK (same (r[0], r[1]));
        double energy = 0.0;
        for (float v : r[0].left)
            energy += static_cast<double> (v) * v;
        CHECK (energy > 1.0e-6);   // the note sounds
        std::printf ("    %.0f Hz: %zu frames identical with the per call computation; render %.1f ms with the caches, %.1f ms without (whole engine, includes the load)\n", sr, r[0].left.size(), ms[0], ms[1]);
    }
    CHECK_EQ (compared, 2);
}

// The pieces: the meter's smoothing and the weights of the means against the old forms, on arbitrary programs.
TEST (own_makeup_meter_weights_and_constants_equal_the_old_forms)
{
    using namespace synthyboy::own::makeup;
    // the weights: the old functions of the ProgramDesc against the cached set, valid and standard program, changing shares
    BandWeights w;
    uint32_t rng = 12345u;
    const auto next = [&] { rng = rng * 1664525u + 1013904223u; return static_cast<float> (rng >> 8) * (1.0f / 16777216.0f); };
    int checked = 0;
    for (int i = 0; i < 400; ++i)
    {
        ProgramDesc d;
        d.valid = i % 7 != 0;
        float total = 0.0f;
        for (float& v : d.share) { v = next() + 0.001f; total += v; }
        for (float& v : d.share) v /= total;
        float bands[kBands], coh[kBands];
        for (int b = 0; b < kBands; ++b) { bands[b] = 3.0f * next(); coh[b] = 2.0f * next() - 0.5f; }
        updateWeights (w, d);
        if (i % 3 == 0)
            updateWeights (w, d);   // (a second call with the same program is a no-op)
        const float a = convolvePower (bands, d), b = convolvePower (bands, w), c = convolveCoherent (coh, d), e = convolveCoherent (coh, w);
        CHECK (std::memcmp (&a, &b, sizeof (float)) == 0);
        CHECK (std::memcmp (&c, &e, sizeof (float)) == 0);
        ++checked;
    }
    // the meter: the same input through a meter with the cached constants and one without, several period lengths and rates
    int meters = 0;
    for (double sr : { 44100.0, 48000.0 })
        for (int period : { 32, 32, 64, 32 })
        {
            ProgramMeter a, b;
            a.prepare (sr);
            b.prepare (sr);
            for (int block = 0; block < 300; ++block)
            {
                float l[64], r[64];
                for (int n = 0; n < 64; ++n)
                {
                    const float amp = block < 100 ? 0.4f : (block < 200 ? 0.0f : 0.05f);
                    l[n] = amp * (2.0f * next() - 1.0f);
                    r[n] = amp * (2.0f * next() - 1.0f);
                }
                setLegacyCachesForTest (false);
                a.accumulate (l, r, period);
                a.endPeriod (period);
                setLegacyCachesForTest (true);
                b.accumulate (l, r, period);
                b.endPeriod (period);
                const ProgramDesc da = a.desc(), db = b.desc();
                // member by member: ProgramDesc has three padding bytes after `valid`, which a returned copy leaves indeterminate (a memcmp of the whole object compared them)
                CHECK (std::memcmp (da.share, db.share, sizeof da.share) == 0 && std::memcmp (&da.wide, &db.wide, sizeof (float)) == 0
                       && std::memcmp (&da.steady, &db.steady, sizeof (float)) == 0 && da.valid == db.valid);
            }
            ++meters;
        }
    setLegacyCachesForTest (false);
    // Mac timing of what a bypassed Convolve does with the program per period: both means, and the meter's end of period
    {
        ProgramDesc d;
        d.valid = true;
        for (int i = 0; i < kMeterBands; ++i)
            d.share[i] = 0.2f;
        float bands[kBands], coh[kBands];
        for (int b = 0; b < kBands; ++b) { bands[b] = 1.0f + 0.01f * static_cast<float> (b); coh[b] = 0.5f; }
        BandWeights cached;
        float sink = 0.0f;
        double us[2] = {};
        for (int legacy = 0; legacy < 2; ++legacy)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 20000; ++i)
            {
                d.share[0] = 0.2f + 1.0e-6f * static_cast<float> (i & 1);   // (the shares move a little every period, as a real program does)
                if (legacy != 0)
                    sink += convolvePower (bands, d) + convolveCoherent (coh, d);
                else
                {
                    updateWeights (cached, d);
                    sink += convolvePower (bands, cached) + convolveCoherent (coh, cached);
                }
            }
            us[legacy] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 20000.0;
        }
        std::printf ("    both weighted means per period: %.3f us with the shared weights, %.3f us before (this Mac; %g)\n", us[0], us[1], static_cast<double> (sink) * 0.0);
    }
    CHECK_EQ (checked, 400);
    CHECK_EQ (meters, 8);
}
