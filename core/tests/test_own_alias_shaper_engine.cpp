// SynthyBoy - own engine tests: X-Shaper transfer curves handed from content to Distortion in engine (docs/plans/distortion-antialiasing.md task X3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "own_engine.h"
#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 44100.0;

void setParam (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

std::vector<float> renderSawShaper (const StateContent* content, int distType, float driveDb, double seconds = 1.0)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 8.0f);
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    setParam (*e, "filter1.on", 0.0f);
    setParam (*e, "master.volume", 0.0f);
    setParam (*e, "env1.attack", 0.0f);
    setParam (*e, "osc1.random_phase", 0.0f);
    setParam (*e, "osc1.level", 100.0f);
    setParam (*e, "osc1.wavetable", 1.0f); // Saw
    setParam (*e, "distortion.on", 1.0f);
    setParam (*e, "distortion.type", static_cast<float> (distType));
    setParam (*e, "distortion.drive", driveDb);
    setParam (*e, "distortion.mix", 100.0f);
    setParam (*e, "distortion.filter_order", 0.0f);
    if (content != nullptr)
        e->setContent (*content);

    const size_t total = static_cast<size_t> (seconds * kSr);
    std::vector<float> l (total), r (total);
    const size_t block = 256;
    for (size_t pos = 0; pos < total; pos += block)
    {
        const size_t n = std::min (block, total - pos);
        Event ev[1];
        int ne = 0;
        if (pos == 0)
            ev[ne++] = sbtest::noteOn (0, 60, 1.0f);
        e->process (ev, ne, l.data() + pos, r.data() + pos, static_cast<int> (n));
    }
    return l;
}

double rmsOfRange (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (b - a));
}
} // namespace

TEST (own_alias_shaper_engine_curves_scale_wet_signal_rms)
{
    // (a) content with shaperA = shaperB = {(0, 0.5), (1, 0.5)} against {(0, 0.25), (1, 0.25)}:
    // the RMS of the left channel over 0.3 .. 1.0 s has the ratio 2.0 within 2 %
    // (the wet path is a +-c square wave of the saw's sign; the rest of the chain is linear below the master knee; today 1.00)
    StateContent c1;
    c1.shaperA = c1.shaperB = { ContentPoint (0.0f, 0.5f), ContentPoint (1.0f, 0.5f) };
    const auto out1 = renderSawShaper (&c1, 12, 0.0f, 1.0);

    StateContent c2;
    c2.shaperA = c2.shaperB = { ContentPoint (0.0f, 0.25f), ContentPoint (1.0f, 0.25f) };
    const auto out2 = renderSawShaper (&c2, 12, 0.0f, 1.0);

    const size_t a = static_cast<size_t> (0.3 * kSr);
    const size_t b = static_cast<size_t> (1.0 * kSr);
    const double rms1 = rmsOfRange (out1, a, b);
    const double rms2 = rmsOfRange (out2, a, b);
    const double ratio = rms1 / rms2;
    std::printf ("    X-Shaper engine RMS ratio: %.4f (expected 2.0 within 2 %%)\n", ratio);
    CHECK_NEAR (ratio, 2.0, 0.04);
}

TEST (own_alias_shaper_engine_no_content_identity_equals_hard_clip)
{
    // (b) no content, drive +20 dB, type 12 against type 1 at drive 0 dB:
    // every sample of the left channel within 1e-4
    // (the identity curve is clamp (x, -1, 1), the hard clip at 0 dB is the same function through another ADAA formula;
    // today type 12 at +20 dB is clamp (10 x) and differs by far more)
    const auto out12 = renderSawShaper (nullptr, 12, 20.0f, 1.0);
    const auto out1 = renderSawShaper (nullptr, 1, 0.0f, 1.0);

    CHECK_EQ (out12.size(), out1.size());
    double maxDiff = 0.0;
    for (size_t i = 0; i < out12.size(); ++i)
    {
        const double diff = std::fabs (static_cast<double> (out12[i]) - static_cast<double> (out1[i]));
        if (diff > maxDiff)
            maxDiff = diff;
        CHECK_NEAR (out12[i], out1[i], 1e-4);
    }
    std::printf ("    X-Shaper engine identity max sample diff: %.6f (expected < 1e-4)\n", maxDiff);
}
