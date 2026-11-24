// SynthyBoy - own engine tests: the FX cost budget of a tier (Quality::fxBudgetPct, core/engine-own/fx/fx_budget.h): the effect instances are admitted in the chain order (rack Main, Bus 1,
// Bus 2, then the order of the rack) while the running sum of their costs stays within the budget, the rest is bypassed (the existing crossfade, no click), the same configuration always
// admits the same instances, a stack within the budget (and every tier without a budget) sounds as it did before.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fx/fx_module.h"
#include "synthyboy/engine.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
// The costs of one instance in hundredths of a percent of a 128 frame block, the Key 61 calibration (docs/perf/fx-budget.md): written out here from the measured table, not read from the engine.
constexpr int kChorus = 601, kPhaser = 428, kCompMultiLimit = 944, kFilterM13 = 650, kUtilityMonoBass = 150, kEqLaw0Mid2 = 298;
constexpr int kBudgetCenti = 3500; // 35 %: Quality of tier low

enum Type { tChorus = 1, tCompressor, tDelay, tDistortion, tEq, tFxFilter, tFlanger, tPhaser, tReverb, tHyper, tUtility, tBode, tConvolve };

using Cfg = std::initializer_list<std::pair<const char*, float>>;

void set (IEngine& e, const std::string& key, float v)
{
    const ParamInfo* info = findParamByKey (key.c_str());
    CHECK (info != nullptr);
    if (info != nullptr)
        e.setParam (info->id, v);
}

/** Slot `s` (0 .. 15): type, on, rack, order and the type's keys at their defaults with `cfg` (plain values of the source keys) laid over them. */
void setSlot (IEngine& e, int s, int type, int order, Cfg cfg = {}, int rack = 0, bool on = true)
{
    char pre[16];
    std::snprintf (pre, sizeof pre, "fxi%02d.", s + 1);
    const std::string p = pre;
    set (e, p + "type", static_cast<float> (type));
    set (e, p + "on", on ? 1.0f : 0.0f);
    set (e, p + "rack", static_cast<float> (rack));
    set (e, p + "order", static_cast<float> (order));
    float norm[kFxInstanceParams];
    fxInstanceDefaults (type, norm);
    const FxTypeDescriptor& d = fxTypeDescriptor (type);
    for (const auto& kv : cfg)
    {
        const int idx = own::fxModuleKeyIndex (kv.first);
        bool found = false;
        for (int k = 0; k < d.count; ++k)
            if (d.source[k] == idx)
            {
                norm[k] = fxInstanceNormalise (type, k, kv.second);
                found = true;
            }
        CHECK (found);
    }
    for (int k = 0; k < kFxInstanceParams; ++k)
    {
        char key[8];
        std::snprintf (key, sizeof key, "p%02d", k + 1);
        set (e, p + key, norm[k]);
    }
}

Quality lowQuality (int budgetPct)
{
    Quality q = qualityForTier (Tier::low);
    q.fxBudgetPct = budgetPct;
    return q;
}

void chord (IEngine& e)
{
    std::vector<Event> ev;
    for (int k = 0; k < 3; ++k)
    {
        Event n;
        n.type = Event::NoteOn;
        n.key = static_cast<int16_t> (40 + k * 7);
        n.value = 0.7f;
        n.frame = 0;
        ev.push_back (n);
    }
    std::vector<float> l (128), r (128);
    e.process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), 128);
}

/** `blocks` blocks of 128 frames appended to out (left then right of every block). */
void run (IEngine& e, int blocks, std::vector<float>* out = nullptr)
{
    std::vector<float> l (128), r (128);
    for (int b = 0; b < blocks; ++b)
    {
        e.process (nullptr, 0, l.data(), r.data(), 128);
        if (out != nullptr)
        {
            out->insert (out->end(), l.begin(), l.end());
            out->insert (out->end(), r.begin(), r.end());
        }
    }
}

bool same (const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0; }

FxBudgetStatus statusOf (const IEngine& e)
{
    FxBudgetStatus s;
    CHECK (e.fxBudgetStatus (s));
    return s;
}

struct Stack
{
    std::unique_ptr<IEngine> e = createEngine (EngineKind::own);
    explicit Stack (const Quality& q) { e->prepare (44100.0, 128, q); }
};

/** The 16 instances of one type in slot order: order 100 + 10 s. */
void allOf (IEngine& e, int type, Cfg cfg = {})
{
    for (int s = 0; s < 16; ++s)
        setSlot (e, s, type, 100 + 10 * s, cfg);
}
} // namespace

TEST (fx_budget_tiers_set_the_budget)
{
    CHECK_EQ (qualityForTier (Tier::low).fxBudgetPct, 35);
    CHECK_EQ (qualityForTier (Tier::medium).fxBudgetPct, 0);
    CHECK_EQ (qualityForTier (Tier::high).fxBudgetPct, 0);
    CHECK_EQ (Quality {}.fxBudgetPct, 0);
}

TEST (fx_budget_16_chorus_admit_the_prefix_that_fits)
{
    Stack st (lowQuality (35));
    allOf (*st.e, tChorus);
    chord (*st.e);
    run (*st.e, 50);
    const FxBudgetStatus s = statusOf (*st.e);
    CHECK_EQ (s.budgetCenti, kBudgetCenti);
    CHECK_EQ (s.admittedCenti, 5 * kChorus);      // 6 x 6.01 = 36.06 > 35
    CHECK_EQ (s.requestedCenti, 16 * kChorus);
    CHECK_EQ (s.overSlots, 0xFFE0u);              // slots 5 .. 15
    CHECK_EQ (s.overModules, 0u);
}

TEST (fx_budget_16_multiband_limit_compressors_admit_three)
{
    Stack st (lowQuality (35));
    allOf (*st.e, tCompressor, { { "compressor.bands", 0.0f }, { "compressor.limit", 1.0f } });
    chord (*st.e);
    run (*st.e, 50);
    const FxBudgetStatus s = statusOf (*st.e);
    CHECK_EQ (s.admittedCenti, 3 * kCompMultiLimit);
    CHECK_EQ (s.overSlots, 0xFFF8u);
}

TEST (fx_budget_mixed_stack_in_slot_order)
{
    Stack st (lowQuality (35));
    setSlot (*st.e, 0, tCompressor, 100, { { "compressor.bands", 0.0f }, { "compressor.limit", 1.0f } });
    setSlot (*st.e, 1, tFxFilter, 110, { { "fxfilter.model", 13.0f } });
    setSlot (*st.e, 2, tChorus, 120);
    setSlot (*st.e, 3, tPhaser, 130);
    setSlot (*st.e, 4, tUtility, 140, { { "utility.mono_bass", 1.0f } });
    setSlot (*st.e, 5, tEq, 150, { { "eq.law", 0.0f }, { "eq.mid2.mode", 1.0f } });
    for (int s = 6; s < 16; ++s)
        setSlot (*st.e, s, tCompressor, 100 + 10 * s, { { "compressor.bands", 0.0f }, { "compressor.limit", 1.0f } });
    chord (*st.e);
    run (*st.e, 50);
    const FxBudgetStatus s = statusOf (*st.e);
    const int first6 = kCompMultiLimit + kFilterM13 + kChorus + kPhaser + kUtilityMonoBass + kEqLaw0Mid2; // 3071, the next limiter (944) would make 4015
    CHECK_EQ (s.admittedCenti, first6);
    CHECK_EQ (s.overSlots, 0xFFC0u);
}

TEST (fx_budget_cheaper_config_of_the_same_type_costs_less)
{
    // a single band Compress compressor (2.55 %) against the multiband Limit one (9.44 %): 13 of the first fit, 3 of the second
    Stack st (lowQuality (35));
    allOf (*st.e, tCompressor, { { "compressor.bands", 3.0f }, { "compressor.limit", 0.0f } });
    chord (*st.e);
    run (*st.e, 50);
    const FxBudgetStatus s = statusOf (*st.e);
    CHECK_EQ (s.admittedCenti, 13 * 255);      // 13 x 2.55 = 33.15, a 14th makes 35.70
    CHECK_EQ (s.overSlots, 0xE000u);
}

TEST (fx_budget_order_decides_who_is_admitted)
{
    Stack st (lowQuality (35));
    for (int s = 0; s < 16; ++s)
        setSlot (*st.e, s, tChorus, 100 + 10 * (15 - s)); // slot 15 first
    chord (*st.e);
    run (*st.e, 50);
    CHECK_EQ (statusOf (*st.e).overSlots, 0x07FFu);   // slots 11 .. 15 are admitted
}

TEST (fx_budget_racks_come_in_the_order_main_bus1_bus2)
{
    Stack st (lowQuality (35));
    setSlot (*st.e, 0, tChorus, 0, {}, 1);              // Bus 1, the lowest order, but Main goes first
    for (int s = 1; s < 16; ++s)
        setSlot (*st.e, s, tChorus, 100 + 10 * s, {}, 0);
    chord (*st.e);
    run (*st.e, 50);
    CHECK_EQ (statusOf (*st.e).overSlots, 0xFFC1u);     // Main slots 1 .. 5 are admitted, Bus 1 (slot 0) comes after them
}

TEST (fx_budget_the_module_of_instance_0_takes_part)
{
    Stack st (lowQuality (35));
    set (*st.e, "chorus.on", 1.0f);                      // at its place of fx.order, before the slots (their order is 1023)
    for (int s = 0; s < 16; ++s)
        setSlot (*st.e, s, tChorus, 1023);
    chord (*st.e);
    run (*st.e, 50);
    const FxBudgetStatus s = statusOf (*st.e);
    CHECK_EQ (s.overModules, 0u);
    CHECK_EQ (s.overSlots, 0xFFF0u);                     // the module and slots 0 .. 3
    CHECK_EQ (s.admittedCenti, 5 * kChorus);
}

TEST (fx_budget_off_or_off_instances_cost_nothing)
{
    Stack st (lowQuality (35));
    for (int s = 0; s < 16; ++s)
        setSlot (*st.e, s, tChorus, 100 + 10 * s, {}, 0, s < 3);   // 3 on, 13 off
    chord (*st.e);
    run (*st.e, 50);
    const FxBudgetStatus s = statusOf (*st.e);
    CHECK_EQ (s.admittedCenti, 3 * kChorus);
    CHECK_EQ (s.overSlots, 0u);
}

TEST (fx_budget_a_stack_within_the_budget_is_bit_identical_to_no_budget)
{
    std::vector<float> a, b;
    for (int pass = 0; pass < 2; ++pass)
    {
        Stack st (lowQuality (pass == 0 ? 35 : 0));
        for (int s = 0; s < 3; ++s)
            setSlot (*st.e, s, s == 0 ? tChorus : (s == 1 ? tPhaser : tDelay), 100 + 10 * s);
        chord (*st.e);
        run (*st.e, 100, pass == 0 ? &a : &b);
        const FxBudgetStatus s = statusOf (*st.e);
        CHECK_EQ (s.overSlots, 0u);
        CHECK_EQ (s.overModules, 0u);
    }
    CHECK (!a.empty() && same (a, b));
}

TEST (fx_budget_zero_admits_all_and_is_the_same_as_a_huge_budget)
{
    // tier high (and medium) have no budget: every instance runs; the sound is the one of a budget that nothing reaches
    std::vector<float> a, b;
    for (int pass = 0; pass < 2; ++pass)
    {
        Quality q = qualityForTier (Tier::high);
        if (pass == 1)
            q.fxBudgetPct = 100000;
        Stack st (q);
        allOf (*st.e, tChorus);
        chord (*st.e);
        run (*st.e, 100, pass == 0 ? &a : &b);
        const FxBudgetStatus s = statusOf (*st.e);
        CHECK_EQ (s.overSlots, 0u);
        CHECK_EQ (s.overModules, 0u);
        CHECK_EQ (s.admittedCenti, 16 * kChorus);
        CHECK_EQ (s.budgetCenti, pass == 0 ? 0 : 10000000);
    }
    CHECK (!a.empty() && same (a, b));
}

TEST (fx_budget_the_same_configuration_admits_the_same_instances_and_renders_the_same)
{
    std::vector<float> a, b;
    FxBudgetStatus sa, sb;
    for (int pass = 0; pass < 2; ++pass)
    {
        Stack st (lowQuality (35));
        if (pass == 0)
            for (int s = 0; s < 16; ++s)
                setSlot (*st.e, s, s % 2 == 0 ? tChorus : tPhaser, 100 + 10 * s);
        else
            for (int s = 15; s >= 0; --s)       // the same configuration written in the other order
                setSlot (*st.e, s, s % 2 == 0 ? tChorus : tPhaser, 100 + 10 * s);
        chord (*st.e);
        run (*st.e, 150, pass == 0 ? &a : &b);
        (pass == 0 ? sa : sb) = statusOf (*st.e);
    }
    CHECK (sa.overSlots == sb.overSlots && sa.overModules == sb.overModules && sa.admittedCenti == sb.admittedCenti);
    CHECK (sa.overSlots != 0u);
    CHECK (!a.empty() && same (a, b));
}

TEST (fx_budget_a_new_instance_pushes_the_last_one_out_without_a_click)
{
    Stack st (lowQuality (35));
    for (int s = 1; s <= 5; ++s)
        setSlot (*st.e, s, tChorus, 100 + 10 * s);   // 5 x 6.01 = 30.05: all admitted
    chord (*st.e);
    run (*st.e, 400);
    CHECK_EQ (statusOf (*st.e).overSlots, 0u);
    std::vector<float> before;
    run (*st.e, 64, &before);
    auto maxStep = [] (const std::vector<float>& x, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = from + 1; i < to; ++i)
            if (i % 256 != 0 && i % 256 != 128)          // the block boundaries of the left / right layout are not neighbours
                m = std::max (m, std::fabs (x[i] - x[i - 1]));
        return m;
    };
    const float steady = maxStep (before, 0, before.size());
    CHECK (steady > 0.0f);
    setSlot (*st.e, 0, tChorus, 10);                 // first in the chain: slot 5 no longer fits
    std::vector<float> during;
    run (*st.e, 64, &during);
    const FxBudgetStatus s = statusOf (*st.e);
    CHECK_EQ (s.overSlots, 1u << 5);
    const float step = maxStep (during, 0, during.size());
    std::printf ("    steady max step %.5f, max step across the change %.5f\n", static_cast<double> (steady), static_cast<double> (step));
    CHECK (step <= 1.5f * steady);                   // the 10 ms crossfade: nothing larger than the signal's own steps
    for (const float v : during)
        CHECK (std::isfinite (v));
}

TEST (fx_budget_a_budget_bypassed_effect_leaves_its_loudness_like_a_masked_one)
{
    // flangers have a loudness model (fx_makeup.h): with the makeup on the bypassed ones leave a gain, so the two renders differ; with the budget off (0) the makeup changes nothing
    auto render = [] (int budget, bool makeup)
    {
        Quality q = qualityForTier (Tier::high);
        q.fxBudgetPct = budget;
        q.fxMakeup = makeup;
        Stack st (q);
        for (int s = 0; s < 6; ++s)
            setSlot (*st.e, s, tFlanger, 100 + 10 * s);
        chord (*st.e);
        std::vector<float> out;
        run (*st.e, 400);
        run (*st.e, 50, &out);
        return out;
    };
    const auto withMakeup = render (20, true), without = render (20, false);
    CHECK (!same (withMakeup, without));
    CHECK (same (render (0, true), render (0, false)));
}

TEST (fx_budget_the_over_budget_instances_are_really_bypassed)
{
    // 16 chorus instances at budget 35 sound exactly like the first 5 alone without a budget: the other 11 pass the signal
    std::vector<float> a, b;
    {
        Stack st (lowQuality (35));
        allOf (*st.e, tChorus);
        chord (*st.e);
        run (*st.e, 100, &a);
    }
    {
        Stack st (lowQuality (0));
        for (int s = 0; s < 5; ++s)
            setSlot (*st.e, s, tChorus, 100 + 10 * s);
        chord (*st.e);
        run (*st.e, 100, &b);
    }
    CHECK (!a.empty() && same (a, b));
}
