// SynthyBoy - own engine tests: Distortion 2x oversampling latency reporting and kEconomyWarp profile switch (docs/plans/distortion-antialiasing.md task E9).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;
using KV = std::vector<std::pair<std::string, float>>;

void setParam (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

std::string poolKey (int slot, const char* what)
{
    char buf[48];
    std::snprintf (buf, sizeof buf, "fxi%02d.%s", slot, what);
    return buf;
}

void setSlot (IEngine& e, int slot, int type, int rack = 0, int order = 1023, bool on = true)
{
    float d[kFxInstanceParams];
    fxInstanceDefaults (type, d);
    setParam (e, poolKey (slot, "type").c_str(), static_cast<float> (type));
    for (int k = 0; k < kFxInstanceParams; ++k)
    {
        char name[8];
        std::snprintf (name, sizeof name, "p%02d", k + 1);
        setParam (e, poolKey (slot, name).c_str(), d[k]);
    }
    setParam (e, poolKey (slot, "rack").c_str(), static_cast<float> (rack));
    setParam (e, poolKey (slot, "order").c_str(), static_cast<float> (order));
    setParam (e, poolKey (slot, "on").c_str(), on ? 1.0f : 0.0f);
}

struct LatencyResult { int reported; double onset; };

LatencyResult measureLatency (const Quality& q, const KV& cfg, const std::vector<std::pair<int, int>>& slots = {}, bool slotOn = true)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (kSr, 256, q);
    setParam (*e, "osc1.wavetable", 0.0f);
    setParam (*e, "osc1.level", 100.0f);
    setParam (*e, "filter1.on", 0.0f);
    setParam (*e, "env1.attack", 0.0f);
    setParam (*e, "master.volume", 0.0f);
    for (const auto& kv : cfg)
        setParam (*e, kv.first.c_str(), kv.second);
    int slot = 1;
    for (const auto& s : slots)
        setSlot (*e, slot++, s.first, s.second, 1023, slotOn);
    const auto r = sbtest::render (*e, { sbtest::noteOn (static_cast<int> (0.1 * kSr), 69) }, static_cast<int> (0.5 * kSr), 256);
    double onset = -1.0;
    for (size_t i = 0; i < r.left.size(); ++i)
        if (std::fabs (r.left[i]) > 1.0e-4f)
        {
            onset = static_cast<double> (i) - 0.1 * kSr;
            break;
        }
    return { e->latencySamples(), onset };
}

std::vector<float> renderDistPatch (const Quality& q, int distType, float mixPercent, bool distOn = true)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (44100.0, 256, q);
    setParam (*e, "filter1.on", 0.0f);
    setParam (*e, "master.volume", 0.0f);
    setParam (*e, "env1.attack", 0.0f);
    setParam (*e, "osc1.random_phase", 0.0f);
    setParam (*e, "osc1.level", 100.0f);
    setParam (*e, "osc1.wavetable", 1.0f);
    setParam (*e, "distortion.on", distOn ? 1.0f : 0.0f);
    setParam (*e, "distortion.type", static_cast<float> (distType));
    setParam (*e, "distortion.drive", 12.0f);
    setParam (*e, "distortion.mix", mixPercent);
    const auto r = sbtest::render (*e, { sbtest::noteOn (0, 60) }, 2048, 256);
    return r.left;
}
} // namespace

TEST (own_alias_dist_latency_reported_when_os2_and_module_on)
{
    const Quality high = qualityForTier (Tier::high);
    const auto plainHigh = measureLatency (high, {});
    CHECK_EQ (plainHigh.reported, 0);

    // Distortion on at tier high: reported latency is 15 samples and onset shifts by 15 samples
    const auto distHigh = measureLatency (high, { { "distortion.on", 1.0f } });
    CHECK_EQ (distHigh.reported, 15);
    CHECK (std::fabs ((distHigh.onset - plainHigh.onset) - 15.0) <= 1.5);

    // Mix 0 still reports 15 samples and delays the dry path (module with latency is never muted)
    const auto mixZeroHigh = measureLatency (high, { { "distortion.on", 1.0f }, { "distortion.mix", 0.0f } });
    CHECK_EQ (mixZeroHigh.reported, 15);
    CHECK (std::fabs ((mixZeroHigh.onset - plainHigh.onset) - 15.0) <= 1.5);

    // Main FX bypass clears the reported latency and delay
    const auto bypassedHigh = measureLatency (high, { { "distortion.on", 1.0f }, { "main.fx_bypass", 1.0f } });
    CHECK_EQ (bypassedHigh.reported, 0);
    CHECK (std::fabs ((bypassedHigh.onset - plainHigh.onset) - 0.0) <= 1.5);

    // Direct destination compensates to match Main's 15-sample latency
    const auto directHigh = measureLatency (high, { { "distortion.on", 1.0f }, { "osc1.destination", 4.0f } });
    CHECK_EQ (directHigh.reported, 15);
    CHECK (std::fabs ((directHigh.onset - plainHigh.onset) - 15.0) <= 1.5);

    // Distortion in Bode + Main adds 15 to Bode's 64
    const auto bodeDistHigh = measureLatency (high, { { "distortion.on", 1.0f }, { "bode.on", 1.0f }, { "bode.mix", 100.0f } });
    CHECK_EQ (bodeDistHigh.reported, 64 + 15);
    CHECK (std::fabs ((bodeDistHigh.onset - plainHigh.onset) - 79.0) <= 1.5);

    // Pool slot kFxTypeDistortion reports 15 when on, 0 when off
    const auto slotOn = measureLatency (high, {}, { { kFxTypeDistortion, 0 } }, true);
    const auto slotOff = measureLatency (high, {}, { { kFxTypeDistortion, 0 } }, false);
    CHECK_EQ (slotOn.reported, 15);
    CHECK_EQ (slotOff.reported, 0);

    // Tier medium also runs os2
    const auto distMed = measureLatency (qualityForTier (Tier::medium), { { "distortion.on", 1.0f } });
    CHECK_EQ (distMed.reported, 15);

    // Tier low keeps os2 off: reported latency is 0 and onset is unchanged
    const Quality low = qualityForTier (Tier::low);
    const auto plainLow = measureLatency (low, {});
    const auto distLow = measureLatency (low, { { "distortion.on", 1.0f } });
    const auto mixZeroLow = measureLatency (low, { { "distortion.on", 1.0f }, { "distortion.mix", 0.0f } });
    CHECK_EQ (plainLow.reported, 0);
    CHECK_EQ (distLow.reported, 0);
    CHECK_EQ (mixZeroLow.reported, 0);
    CHECK (std::fabs ((distLow.onset - plainLow.onset) - 0.0) <= 1.5);
}

TEST (own_alias_dist_latency_follows_kEconomyWarp_profile_switch)
{
    // Tier low with kEconomyWarp cleared (warp=4x profile on Key 61/XL): os2 is ON -> 15 samples
    Quality lowWarp4x = qualityForTier (Tier::low);
    lowWarp4x.economy = static_cast<int> (kEconomyAll & ~kEconomyWarp);
    CHECK_EQ (measureLatency (lowWarp4x, { { "distortion.on", 1.0f } }).reported, 15);
    CHECK_EQ (measureLatency (lowWarp4x, { { "distortion.on", 0.0f } }).reported, 0);

    // Tier high with kEconomyWarp set (warp=2x): os2 is OFF -> 0 samples
    Quality highWarp2x = qualityForTier (Tier::high);
    highWarp2x.economy = static_cast<int> (kEconomyWarp);
    CHECK_EQ (measureLatency (highWarp2x, { { "distortion.on", 1.0f } }).reported, 0);

    // Tier low with explicit kEconomyAll: os2 is OFF -> 0 samples
    Quality lowAll = qualityForTier (Tier::low);
    lowAll.economy = static_cast<int> (kEconomyAll);
    CHECK_EQ (measureLatency (lowAll, { { "distortion.on", 1.0f } }).reported, 0);
}

TEST (own_alias_dist_latency_section_11_vectors_unchanged_with_distortion_off)
{
    const Quality high = qualityForTier (Tier::high);
    const auto plain = measureLatency (high, {});
    CHECK_EQ (plain.reported, 0);

    const int L = 64;
    const int Lim = static_cast<int> (std::lround (0.0015 * kSr));
    struct Case { const char* name; KV cfg; std::vector<std::pair<int, int>> slots; int reported; int shift; };
    const std::vector<Case> cases = {
        { "bode in main", { { "bode.on", 1 }, { "bode.mix", 100 } }, {}, L, L },
        { "bode in bus 1, dest main", { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.rack", 1 } }, {}, L, L },
        { "bode in bus 1, dest direct", { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.rack", 1 }, { "bus1.destination", 1 } }, {}, L, L },
        { "limiter", { { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, Lim, Lim },
        { "limiter, latency comp off", { { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.limit_latency_comp", 0 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, 0, Lim },
        { "bode + limiter in main", { { "bode.on", 1 }, { "bode.mix", 100 }, { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, L + Lim, L + Lim },
        { "bode in bus 1 + limiter in main", { { "bode.on", 1 }, { "bode.mix", 100 }, { "bode.rack", 1 }, { "compressor.on", 1 }, { "compressor.limit", 1 }, { "compressor.bands", 3 }, { "compressor.mid.upper_threshold", 12 } }, {}, L + Lim, L + Lim },
        { "bode slot in main", {}, { { kFxTypeBode, 0 } }, L, L },
        { "compressor slot, defaults", {}, { { kFxTypeCompressor, 0 } }, 0, 0 },
    };
    for (const Case& c : cases)
    {
        const auto r = measureLatency (high, c.cfg, c.slots);
        CHECK_EQ (r.reported, c.reported);
        CHECK (std::fabs ((r.onset - plain.onset) - c.shift) <= 1.5);
    }
}

TEST (own_alias_dist_latency_tier_low_checksums_with_distortion_on)
{
    const Quality low = qualityForTier (Tier::low);
    Quality lowSpelled = low;
    lowSpelled.economy = static_cast<int> (kEconomyAll);

    for (const int type : { 0, 1, 4, 6 })
    {
        const std::vector<float> a = renderDistPatch (low, type, 100.0f, true);
        const std::vector<float> b = renderDistPatch (lowSpelled, type, 100.0f, true);
        CHECK_EQ (a.size(), b.size());
        CHECK_EQ (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)), 0);

        double sumAbs = 0.0;
        for (float v : a)
            sumAbs += static_cast<double> (std::fabs (v));
        const size_t n = a.size();
        std::printf ("DIST-LOW-CHECKSUM type=%d sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     type, sumAbs,
                     static_cast<double> (a[n - 8]), static_cast<double> (a[n - 7]),
                     static_cast<double> (a[n - 6]), static_cast<double> (a[n - 5]),
                     static_cast<double> (a[n - 4]), static_cast<double> (a[n - 3]),
                     static_cast<double> (a[n - 2]), static_cast<double> (a[n - 1]));
    }

    // At tier low, distortion with mix 0 is bit-identical to distortion off (os2 is off, latency is 0, muted)
    const std::vector<float> mix0 = renderDistPatch (low, 1, 0.0f, true);
    const std::vector<float> off = renderDistPatch (low, 1, 100.0f, false);
    CHECK_EQ (mix0.size(), off.size());
    CHECK_EQ (std::memcmp (mix0.data(), off.data(), mix0.size() * sizeof (float)), 0);
}
