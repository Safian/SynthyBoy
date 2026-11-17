// SynthyBoy - MPC shell tests: the rule of the stepped Q proxies (q_step_rule.h): every `stepped` parameter and the integer steppers of up to 32 values are relative, nothing else; the parameters of the second kind
// are listed here so a new one cannot change its behaviour unnoticed.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <set>
#include <string>

#include "q_step_rule.h"

using synthyboy_mpc::stepCountOf;

static int fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++fails;
}

int main()
{
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
