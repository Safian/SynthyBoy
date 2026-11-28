// SynthyBoy - own engine tests: ADAA in the library oscillators at host rate (docs/plans/distortion-antialiasing.md task L1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "alias_measure.h"
#include "osc/warp.h"
#include "synthyboy/aa_switch.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "synthyboy/user_content.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kFs = 44100.0;
constexpr size_t kN = 65536;

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

std::vector<uint8_t> sineWav (double hz, double amp, int rate, double seconds)
{
    const size_t n = static_cast<size_t> (rate * seconds);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float> (amp * std::sin (6.283185307179586 * hz * static_cast<double> (i) / rate));
    return encodeWav (x.data(), nullptr, n, rate, 16);
}

struct RenderOut
{
    std::vector<float> l, r;
};

RenderOut renderNote (int note, int warpType, float warpAmount, Tier tier, bool withSample = true)
{
    MemoryContentStore store;
    StateContent content;
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kFs, 256, qualityForTier (tier));
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "env1.attack", 0.0f);
    set (*e, "osc1.random_phase", 0.0f);
    set (*e, "osc1.level", 100.0f);
    set (*e, "osc1.unison.voices", 1.0f);
    set (*e, "osc1.type", 1.0f); // Sample type
    if (withSample)
    {
        store.add ("test", "sine.wav", sineWav (440.0, 0.8, 44100, 2.0));
        ContentFileRef ref;
        ref.pack = "test";
        ref.file = "sine.wav";
        ref.name = "sine";
        attachSample (content, 1, ref, 44100, 88200, 1);
        e->setContentStore (&store);
        e->setContent (content);
    }
    set (*e, "osc1.sample.root", 69.0f);
    set (*e, "osc1.sample.loop_mode", 1.0f);
    set (*e, "osc1.warp.type", static_cast<float> (warpType));
    set (*e, "osc1.warp.amount", warpAmount);

    const size_t total = static_cast<size_t> (2.0 * kFs);
    RenderOut o;
    o.l.assign (total, 0.0f);
    o.r.assign (total, 0.0f);
    for (size_t pos = 0; pos < total; pos += 256)
    {
        const size_t n = std::min<size_t> (256, total - pos);
        Event ev[1];
        int ne = 0;
        if (pos == 0)
            ev[ne++] = sbtest::noteOn (0, note, 1.0f);
        e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, static_cast<int> (n));
    }
    return o;
}

aliasm::Result measureAlias (const std::vector<float>& l, int note)
{
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    return aliasm::measure (l.data() + 22050, kN, f0, kFs, 6.0);
}

void printChecksum (int warpType, bool aaOn, Tier tier, const std::vector<float>& l, const std::vector<float>& r)
{
    const size_t n = l.size();
    double sumAbs = 0.0;
    for (size_t i = 0; i < n; ++i)
        sumAbs += static_cast<double> (std::fabs (l[i])) + static_cast<double> (std::fabs (r[i]));

    std::printf ("LIBWARP-CHECKSUM warp=%d aa=%s tier=%s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                 warpType, aaOn ? "on" : "off", tier == Tier::low ? "low" : "high", sumAbs,
                 static_cast<double> (l[n - 8]), static_cast<double> (l[n - 7]),
                 static_cast<double> (l[n - 6]), static_cast<double> (l[n - 5]),
                 static_cast<double> (l[n - 4]), static_cast<double> (l[n - 3]),
                 static_cast<double> (l[n - 2]), static_cast<double> (l[n - 1]));
}
} // namespace

TEST (own_alias_lib_warp_sample_hard_clip_and_quantize)
{
    const int note = 84;
    // Hard Clip 50 %
    {
        aa::setOff (0);
        const auto on = renderNote (note, kWarpDistHardClip, 50.0f, Tier::high);
        aa::setOff (aa::kLibWarp);
        const auto off = renderNote (note, kWarpDistHardClip, 50.0f, Tier::high);
        aa::setOff (0);

        const auto mOn = measureAlias (on.l, note);
        const auto mOff = measureAlias (off.l, note);
        std::printf ("    LIBWARP-AA warp=hard_clip note=%d alias off=%.2f dB on=%.2f dB (diff=%.2f dB), harm off=%.2f on=%.2f\n",
                     note, mOff.aliasDb, mOn.aliasDb, mOn.aliasDb - mOff.aliasDb, mOff.harmDb, mOn.harmDb);
        CHECK (mOn.aliasDb <= mOff.aliasDb - 3.0);
    }

    // Quantize 50 %
    {
        aa::setOff (0);
        const auto on = renderNote (note, kWarpQuantize, 50.0f, Tier::high);
        aa::setOff (aa::kLibWarp);
        const auto off = renderNote (note, kWarpQuantize, 50.0f, Tier::high);
        aa::setOff (0);

        const auto mOn = measureAlias (on.l, note);
        const auto mOff = measureAlias (off.l, note);
        std::printf ("    LIBWARP-AA warp=quantize note=%d alias off=%.2f dB on=%.2f dB (diff=%.2f dB), harm off=%.2f on=%.2f\n",
                     note, mOff.aliasDb, mOn.aliasDb, mOn.aliasDb - mOff.aliasDb, mOff.harmDb, mOn.harmDb);
        CHECK (mOn.aliasDb <= mOff.aliasDb - 3.0);
    }
}

TEST (own_alias_lib_warp_bit_identity_and_checksums)
{
    const int note = 84;

    // warp.type none: on == off bit for bit
    {
        aa::setOff (0);
        const auto on = renderNote (note, kWarpNone, 0.0f, Tier::high);
        aa::setOff (aa::kLibWarp);
        const auto off = renderNote (note, kWarpNone, 0.0f, Tier::high);
        aa::setOff (0);
        CHECK (on.l == off.l);
        CHECK (on.r == off.r);
    }

    // Types that are not sample-domain (Sync, RM, AM, FM): on == off bit for bit
    const int nonSampleDomain[] = { kWarpSync, kWarpRmA, kWarpAmA, kWarpFmA };
    for (int wt : nonSampleDomain)
    {
        aa::setOff (0);
        const auto on = renderNote (note, wt, 50.0f, Tier::high);
        aa::setOff (aa::kLibWarp);
        const auto off = renderNote (note, wt, 50.0f, Tier::high);
        aa::setOff (0);
        CHECK (on.l == off.l);
        CHECK (on.r == off.r);

        printChecksum (wt, true, Tier::high, on.l, on.r);
        printChecksum (wt, false, Tier::high, off.l, off.r);
    }

    // Tier settings: library oscillators have no oversampling at any tier
    // Low and High tier with warp none: print checksums
    for (const Tier tier : { Tier::low, Tier::high })
    {
        for (const bool aaOn : { false, true })
        {
            aa::setOff (aaOn ? 0 : aa::kLibWarp);
            const auto r = renderNote (note, kWarpNone, 0.0f, tier);
            printChecksum (kWarpNone, aaOn, tier, r.l, r.r);
        }
    }

    // Checksums for Hard Clip and Quantize with aa=off (legacy baseline bits preserved)
    for (int wt : { static_cast<int> (kWarpDistHardClip), static_cast<int> (kWarpQuantize) })
    {
        aa::setOff (aa::kLibWarp);
        const auto r = renderNote (note, wt, 50.0f, Tier::high);
        printChecksum (wt, false, Tier::high, r.l, r.r);
    }
    aa::setOff (0);
}

TEST (own_alias_lib_warp_sample_without_content_falls_back_to_builtin_wavetable)
{
    const int note = 84;
    // Without content, voice falls back to built-in wavetable (WtOsc) per docs/ARCHITECTURE.md section 5
    // WtOsc uses aa::kWarpAdaa, so toggling aa::kLibWarp leaves it identical bit for bit
    aa::setOff (0);
    const auto on = renderNote (note, kWarpDistHardClip, 50.0f, Tier::high, /*withSample=*/false);
    aa::setOff (aa::kLibWarp);
    const auto off = renderNote (note, kWarpDistHardClip, 50.0f, Tier::high, /*withSample=*/false);
    aa::setOff (0);
    CHECK (on.l == off.l);
    CHECK (on.r == off.r);
}
