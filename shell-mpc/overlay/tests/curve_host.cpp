// SynthyBoy - MPC overlay tests: the curve source (parameter change -> new curve, cutoff only -> translated curve, then exact; nothing changed -> nothing).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fakes.h"

using namespace ovt;

static uint32_t hashOf (const float* c) { uint32_t h = 2166136261u; for (int i = 0; i < 256; ++i) { int32_t v = (int32_t) std::lround (c[i] * 100.0); for (int b = 0; b < 4; ++b) h = (h ^ (((uint32_t) v >> (8 * b)) & 255u)) * 16777619u; } return h; }

int main()
{
    CurveSource::Config cfg;
    cfg.fixedCostMs = 10.0;   // the gap between two full computations: 10 ms / 0.08 = 125 ms
    CurveSource src (cfg);
    FakeInstance inst;
    inst.set ("filter1.cutoff", 1000.0f);
    inst.set ("filter1.resonance", 30.0f);
    double now = 1000.0;

    check (src.update (inst, inst.liveTargets(), now), "first update computes a curve");
    check (src.valid() && src.stats().fullComputes == 1, "one full computation");
    const uint32_t h0 = src.hash();
    float lo, hi;
    src.minMax (lo, hi);
    check (hi > lo + 3.0f, "the curve is not flat (min " + std::to_string (lo) + " max " + std::to_string (hi) + " dB)");

    // the curve is the one FilterResponse gives for the same settings
    {
        synthyboy::FilterResponse direct (48000.0);
        synthyboy::ParamValues pv = synthyboy::ParamValues::defaults();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i) pv.plain[i] = inst.param ((int) i);
        float ref[256];
        synthyboy::FilterResponse::Request r; r.filter = 1;
        direct.compute (pv, r, ref, 256);
        check (hashOf (ref) == h0 && std::memcmp (ref, src.curve(), sizeof ref) == 0, "the curve is bit identical to FilterResponse::compute for the same snapshot");
    }

    // nothing changed: no recompute, no new picture
    for (int i = 0; i < 100; ++i)
    {
        now += 33.0;
        if (src.update (inst, inst.liveTargets(), now)) { check (false, "nothing changed: update returned true"); break; }
    }
    check (src.stats().fullComputes == 1 && src.stats().translations == 0, "nothing changed: no recompute, no translation in 100 updates");
    check (src.hash() == h0, "nothing changed: same hash");

    // a parameter change (resonance) -> new curve (budget gap passed)
    now += 200.0;
    inst.set ("filter1.resonance", 60.0f);
    check (src.update (inst, inst.liveTargets(), now), "resonance changed: update returns a new curve");
    check (src.hash() != h0 && src.stats().fullComputes == 2, "resonance changed: new hash, one more full computation");
    const uint32_t h1 = src.hash();

    // model change -> new curve
    now += 200.0;
    inst.set ("filter1.model", 2.0f);
    const bool changedModel = src.update (inst, inst.liveTargets(), now);
    check (changedModel && src.hash() != h1, "model changed: new hash");

    // cutoff only, inside the gap: a translated curve, no full computation; later the exact one
    now += 10.0;
    inst.hasLive = true; inst.liveCutoff = 1000.0f; inst.liveRes = 60.0f;
    check (! src.update (inst, inst.liveTargets(), now), "live values equal to the knob state: nothing changes");
    const unsigned long fulls = src.stats().fullComputes;
    const uint32_t hBase = src.hash();
    now += 33.0;
    inst.liveCutoff = 1400.0f;
    check (src.update (inst, inst.liveTargets(), now), "cutoff moved inside the budget gap: new picture");
    check (src.stats().fullComputes == fulls && src.stats().translations == 1, "... made by translation (no full computation)");
    check (src.hash() != hBase, "... which differs from the base curve");
    // the translated curve against the exact one at the new cutoff: peak position error
    float transl[256];
    std::memcpy (transl, src.curve(), sizeof transl);
    now += 33.0;   // 76 ms after the last full computation: inside the 125 ms gap
    inst.liveCutoff = 1500.0f;
    src.update (inst, inst.liveTargets(), now);           // still inside the gap: translated again
    check (src.stats().fullComputes == fulls, "a second move inside the gap: still no full computation");
    now += 300.0;                     // budget allows it now; the cutoff does not move any more
    check (src.update (inst, inst.liveTargets(), now), "after the motion stops the exact curve follows");
    check (src.stats().fullComputes == fulls + 1, "... by one full computation");
    {
        synthyboy::FilterResponse direct (48000.0);
        synthyboy::ParamValues pv = synthyboy::ParamValues::defaults();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i) pv.plain[i] = inst.param ((int) i);
        float ref[256];
        synthyboy::FilterResponse::Request r; r.filter = 1; r.cutoffHz = 1500.0f; r.resonancePercent = 60.0f;
        direct.compute (pv, r, ref, 256);
        check (std::memcmp (ref, src.curve(), sizeof ref) == 0, "the converged curve is bit identical to the exact computation at the final cutoff");
        // translation quality: translate the exact curve at 1000 Hz to 1500 Hz and compare with the exact curve at 1500 Hz
        float base[256];
        r.cutoffHz = 1000.0f;
        direct.compute (pv, r, base, 256);
        const double shift = std::log2 (1.5) * 255.0 / std::log2 (1000.0);
        double worst = 0.0; int worstAt = 0;
        float exact15[256];
        r.cutoffHz = 1500.0f;
        direct.compute (pv, r, exact15, 256);
        for (int i = 40; i < 230; ++i)
        {
            const double srcPos = i - shift; const int j = (int) srcPos; const double f = srcPos - j;
            const double t = base[j] * (1.0 - f) + base[j + 1] * f;
            if (std::fabs (t - exact15[i]) > worst) { worst = std::fabs (t - exact15[i]); worstAt = i; }
        }
        std::printf ("INFO  translation 1000 -> 1500 Hz (model 2, resonance 60 %%): worst difference %.2f dB at point %d\n", worst, worstAt);
        check (worst < 3.0, "the translated curve stays within 3 dB of the exact one (points 40..230)");
    }
    (void) transl;

    // after convergence nothing more happens
    const unsigned long fulls2 = src.stats().fullComputes;
    for (int i = 0; i < 50; ++i) { now += 33.0; src.update (inst, inst.liveTargets(), now); }
    check (src.stats().fullComputes == fulls2, "converged: no further computation");

    // filter off: flat 0 dB, still a valid picture
    now += 300.0;
    inst.set ("filter1.on", 0.0f);
    check (src.update (inst, inst.liveTargets(), now), "filter switched off: new picture");
    src.minMax (lo, hi);
    check (lo == 0.0f && hi == 0.0f, "... flat at 0 dB");

    std::printf (g_fails ? "curve_host: FAILED (%d)\n" : "curve_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
