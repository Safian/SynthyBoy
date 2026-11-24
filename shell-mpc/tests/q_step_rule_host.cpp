// SynthyBoy - MPC shell tests: the rule of the stepped Q proxies (q_step_rule.h): every `stepped` parameter and the integer steppers of up to 32 values are relative, nothing else; the parameters of the second kind
// are listed here so a new one cannot change its behaviour unnoticed.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "q_router.h"
#include "q_step_rule.h"

using synthyboy_mpc::stepCountOf;

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
}

/** The safety rule of the Q-Link mapping extensions (owner, 2026-11-21): a toggle or a short list that sits on a knob (OSC POWER, LFO SYNC / MONO, the Voice TARGETS, the modulation polarity and switch ...) must not change on ONE click of the
    Q-Link (OSC POWER off by accident = a silent sound). QRouter::hostWrite drives every stepped target RELATIVELY: the host's travel is accumulated and one step happens per kStepTravelDefault (6 clicks of 1/128). This walks real parameters
    of the engine table through a router the way the host does (one write per click, the host adopts every report), from both states of the toggle: 1 click and 5 clicks change nothing, a jiggle (+1, -1, +1 ...) never does, 6 clicks make
    exactly one step, and so do 6 clicks that arrive in one coalesced write. */
void testNudge()
{
    using synthyboy_mpc::QRouter;
    const synthyboy::ParamInfo* table = synthyboy::paramTable();
    const size_t count = synthyboy::paramCount();
    const char* const keys[] = { "osc1.on", "osc2.on", "osc3.on", "sub.on", "noise.on", "lfo1.sync", "lfo1.mono", "lfo2.mono", "voicectl.osc.a", "voicectl.osc.sub", "voicectl.osc.noise", "voicectl.rand.detune10x", "mod01.bipolar", "mod01.bypass" };
    constexpr float click = QRouter::kOneIncrement;
    for (const char* key : keys)
    {
        int idx = -1;
        for (size_t i = 0; i < count; ++i)
            if (std::string (table[i].key) == key)
                idx = static_cast<int> (i);
        if (idx < 0)
        {
            check (false, std::string ("the nudge test knows the key ") + key);
            continue;
        }
        const int n = stepCountOf (table[idx]);
        check (n >= 2, std::string (key) + " is a stepped target (" + std::to_string (n) + " values): relative, not absolute");
        auto stepsOf = [&] (int t) noexcept { return t == idx ? n : 0; };
        for (const float start : { 0.0f, 1.0f })   // a toggle that is OFF and one that is ON; a list at its first and its last value
        {
            const float dir = start < 0.5f ? 1.0f : -1.0f;
            float value = start;
            QRouter r;
            auto rd = [&] (int) noexcept { return value; };
            auto wr = [&] (int, float v) noexcept { value = v; };
            const int g[4] = { idx, -1, -1, -1 };
            float host = start;
            auto pollAll = [&] { r.poll (rd, [&] (int q, float v) { if (q == 0) host = v; }, [] (int) {}); };
            r.setGroup (g, 4, static_cast<int> (count));
            pollAll();   // the group report: the host holds the target's value
            // "turn": one write of `clicks` increments in `d` direction (the host moves its own value and writes it), then the host adopts the report like the plugin makes it
            auto turn = [&] (float d, int clicks)
            {
                host = std::clamp (host + d * static_cast<float> (clicks) * click, 0.0f, 1.0f);
                r.hostWrite (0, host, false, rd, wr, stepsOf);
                pollAll();
            };
            const std::string tag = std::string (key) + " from " + (start < 0.5f ? "its first value" : "its last value") + ": ";
            turn (dir, 1);
            check (value == start, tag + "1 click does not change it");
            turn (-dir, 1);
            turn (dir, 1);
            turn (-dir, 1);
            turn (dir, 1);
            check (value == start, tag + "a jiggle of single clicks (+1 -1 +1 -1 +1) never changes it");
            QRouter r2;   // a fresh router: 5 clicks one by one change nothing, the 6th does
            float v2 = start, h2 = start;
            auto rd2 = [&] (int) noexcept { return v2; };
            auto wr2 = [&] (int, float v) noexcept { v2 = v; };
            auto poll2 = [&] { r2.poll (rd2, [&] (int q, float v) { if (q == 0) h2 = v; }, [] (int) {}); };
            r2.setGroup (g, 4, static_cast<int> (count));
            poll2();
            bool earlyChange = false;
            for (int c = 1; c <= 6; ++c)
            {
                h2 = std::clamp (h2 + dir * click, 0.0f, 1.0f);
                r2.hostWrite (0, h2, false, rd2, wr2, stepsOf);
                poll2();
                if (c < 6 && v2 != start)
                    earlyChange = true;
            }
            const float one = 1.0f / static_cast<float> (n - 1);
            check (! earlyChange && std::fabs (v2 - (start + dir * one)) < 1.0e-6f, tag + "clicks 1 .. 5 change nothing, the 6th makes exactly one step (" + std::to_string (v2) + ")");
            QRouter r3;   // 6 clicks that reach the plugin in ONE coalesced write: one step
            float v3 = start, h3 = start;
            auto rd3 = [&] (int) noexcept { return v3; };
            auto wr3 = [&] (int, float v) noexcept { v3 = v; };
            auto poll3 = [&] { r3.poll (rd3, [&] (int q, float v) { if (q == 0) h3 = v; }, [] (int) {}); };
            r3.setGroup (g, 4, static_cast<int> (count));
            poll3();
            h3 = std::clamp (h3 + dir * 5.0f * click, 0.0f, 1.0f);
            r3.hostWrite (0, h3, false, rd3, wr3, stepsOf);
            poll3();
            check (v3 == start, tag + "5 clicks in one write change nothing");
            h3 = std::clamp (h3 + dir * click, 0.0f, 1.0f);
            r3.hostWrite (0, h3, false, rd3, wr3, stepsOf);
            poll3();
            check (std::fabs (v3 - (start + dir * one)) < 1.0e-6f, tag + "the 6th click completes the step");
        }
    }
}

int main()
{
    testNudge();

    const synthyboy::ParamInfo* table = synthyboy::paramTable();
    const size_t n = synthyboy::paramCount();
    std::set<std::string> integerSteppers, quantisedAmounts;
    size_t stepped = 0;
    bool steppedAll = true;
    for (size_t i = 0; i < n; ++i)
    {
        const synthyboy::ParamInfo& p = table[i];
        const int c = stepCountOf (p);
        if (p.scale == synthyboy::Scale::stepped)
        {
            ++stepped;
            steppedAll = steppedAll && c == static_cast<int> (std::lround (p.max - p.min)) + 1 && c >= 2;
        }
        else if (c >= 2)
            integerSteppers.insert (p.key);
        else if (p.interval > 0.0f && p.max > p.min)
            quantisedAmounts.insert (p.key);
    }
    check (steppedAll && stepped > 1000, "every stepped parameter (" + std::to_string (stepped) + ") is a stepped control with its number of values");
    for (const std::string& k : integerSteppers) std::printf ("INFO  integer stepper (relative): %s\n", k.c_str());
    for (const std::string& k : quantisedAmounts) std::printf ("INFO  quantised amount (stays absolute): %s\n", k.c_str());
    const std::set<std::string> want = { "master.polyphony", "master.bend_range", "master.bend_down_range", "osc1.unison.voices", "osc2.unison.voices", "osc3.unison.voices", "chorus.voices", "hyperdim.unison", "sub.octave", "voicectl.length" };
    check (integerSteppers == want, "the integer steppers that are relative are exactly the ten known ones (" + std::to_string (integerSteppers.size()) + ")");
    check (quantisedAmounts.count ("osc1.gran.density_grains") == 1 && quantisedAmounts.count ("osc2.gran.density_grains") == 1 && quantisedAmounts.count ("osc3.gran.density_grains") == 1, "the grain density (1 .. 64 grains, an amount) stays an absolute knob");
    check (quantisedAmounts.size() == 63, "the quantised parameters that stay absolute knobs are pinned at 63 (fx order slots, key tracking and other transposes, note and velocity ranges, grain density and count, sample roots): a new one changes this number (" + std::to_string (quantisedAmounts.size()) + ")");
    std::printf (fails ? "q_step_rule_host: FAILED (%d)\n" : "q_step_rule_host: OK\n", fails);
    return fails ? 1 : 0;
}
