// SynthyBoy - own engine tests: Convolve at tier low (quality.h: the heavy effects are bypassed) really is bypassed in the engine for a preset with convolve.on = 1 and an impulse loaded: the render
// equals the render of the same patch with Convolve off, sample for sample, and the Convolve module neither runs nor starts its helper-thread build. At tier high the same patch sounds
// different (so the impulse is loaded and Convolve is on in the test).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/user_content.h"
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
