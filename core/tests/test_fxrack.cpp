// SynthyBoy - core tests: the rack lists of the effect chain and their edits (fx_rack.h; docs/ui-design/18-FX-INSTANCES.md section 2.8).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "synthyboy/fx_rack.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
std::vector<FxRackItem> listOf (const ParamValues& v, int rack)
{
    return fxRacks (v.plain).rack[static_cast<size_t> (rack)];
}

std::vector<FxWrite> applyTo (ParamValues& v, const std::vector<FxWrite>& writes)
{
    fxApply (v.plain, writes);
    return writes;
}

/** Every module of instance 0 is in exactly one rack, every slot with a type in exactly one of the first three, none of the others anywhere. */
bool consistent (const ParamValues& v)
{
    const FxRacks racks = fxRacks (v.plain);
    std::set<int> modules, slots;
    for (int r = 0; r < kNumFxRacks; ++r)
        for (const FxRackItem& item : racks.rack[static_cast<size_t> (r)])
        {
            if (item.pool)
            {
                if (r >= kNumPoolRacks || ! slots.insert (item.index).second)
                    return false;
                if (static_cast<int> (std::lround (v.plain[fxInstanceKeys (item.index).type])) == kFxTypeNone)
                    return false;
            }
            else if (! modules.insert (item.index).second)
                return false;
        }
    int used = 0;
    for (int s = 0; s < kNumFxInstances; ++s)
        used += static_cast<int> (std::lround (v.plain[fxInstanceKeys (s).type])) != kFxTypeNone ? 1 : 0;
    // fx.order is a permutation after the writes of an edit (the UI never leaves a duplicate)
    std::set<int> order;
    for (int q = 0; q < kNumFxSlots; ++q)
        order.insert (static_cast<int> (std::lround (v.plain[findParamIndex (findParamByKey (("fx.order." + std::to_string (q + 1)).c_str())->id)])));
    return modules.size() == 16 && static_cast<int> (slots.size()) == used && order.size() == 16;
}
} // namespace

TEST (fxrack_defaults_list_the_sixteen_modules_in_the_default_order)
{
    const ParamValues v = ParamValues::defaults();
    const FxRacks racks = fxRacks (v.plain);
    CHECK_EQ (racks.rack[0].size(), static_cast<size_t> (16));
    for (int r = 1; r < kNumFxRacks; ++r)
        CHECK (racks.rack[static_cast<size_t> (r)].empty());
    // fx.order defaults: position 0 is the FX filter (5), 1 the distortion (3), 2 the compressor (1), 3 the EQ (4), 4 the chorus (0) ...
    const int expected[16] = { 5, 3, 1, 4, 0, 6, 7, 2, 8, 9, 10, 11, 12, 13, 14, 15 };
    for (int q = 0; q < 16; ++q)
    {
        CHECK (! racks.rack[0][static_cast<size_t> (q)].pool);
        CHECK_EQ (racks.rack[0][static_cast<size_t> (q)].index, expected[q]);
        CHECK_EQ (racks.position[static_cast<size_t> (expected[q])], q);
    }
    CHECK_EQ (fxFirstFreeSlot (v.plain), 0);
    CHECK (consistent (v));
    CHECK (std::string (fxItemName (v.plain, { false, 5 })) == "FX Filter");
    CHECK_EQ (fxItemType (v.plain, { false, 8 }), static_cast<int> (kFxTypeReverb));
    CHECK_EQ (fxItemType (v.plain, { false, 14 }), static_cast<int> (kFxTypeNone)); // a splitter has no type of the pool
}

TEST (fxrack_a_broken_fx_order_is_repaired_the_way_the_engine_does)
{
    ParamValues v = ParamValues::defaults();
    const auto set = [&] (int position, float module) { v.plain[findParamIndex (findParamByKey (("fx.order." + std::to_string (position)).c_str())->id)] = module; };
    for (int p = 1; p <= 16; ++p)
        set (p, 3.0f); // every position wants the distortion
    const FxRacks racks = fxRacks (v.plain);
    CHECK_EQ (racks.rack[0].size(), static_cast<size_t> (16));
    CHECK_EQ (racks.rack[0][0].index, 3);  // the first one keeps it, the others take the lowest free module in turn
    CHECK_EQ (racks.rack[0][1].index, 0);
    CHECK_EQ (racks.rack[0][2].index, 1);
    CHECK_EQ (racks.rack[0][3].index, 2);
    CHECK_EQ (racks.rack[0][4].index, 4);
    CHECK (consistent (ParamValues::defaults()));
}

TEST (fxrack_add_fills_the_first_free_slot_at_the_end_of_the_rack_until_the_pool_is_full)
{
    ParamValues v = ParamValues::defaults();
    std::vector<FxWrite> w;
    const int first = fxAddModule (v.plain, kFxTypeDelay, 1, w);
    CHECK_EQ (first, 0);
    applyTo (v, w);
    CHECK_EQ (static_cast<int> (v.plain[fxInstanceKeys (0).type]), static_cast<int> (kFxTypeDelay));
    CHECK_EQ (v.plain[fxInstanceKeys (0).on], 1.0f);
    CHECK_EQ (v.plain[fxInstanceKeys (0).rack], 1.0f);
    float defaults[kFxInstanceParams];
    fxInstanceDefaults (kFxTypeDelay, defaults);
    for (int p = 0; p < kFxInstanceParams; ++p)
        CHECK_EQ (v.plain[fxInstanceKeys (0).p[p]], defaults[p]);
    CHECK_EQ (listOf (v, 1).size(), static_cast<size_t> (1));
    CHECK (listOf (v, 1)[0] == (FxRackItem { true, 0 }));

    // the second one in the same rack comes behind it, a third in Main behind the sixteen modules
    w.clear();
    CHECK_EQ (fxAddModule (v.plain, kFxTypeChorus, 1, w), 1);
    applyTo (v, w);
    w.clear();
    CHECK_EQ (fxAddModule (v.plain, kFxTypeReverb, 0, w), 2);
    applyTo (v, w);
    CHECK (listOf (v, 1)[0] == (FxRackItem { true, 0 }) && listOf (v, 1)[1] == (FxRackItem { true, 1 }));
    CHECK (listOf (v, 0).back() == (FxRackItem { true, 2 }));
    CHECK (consistent (v));

    // fill the pool; the 17th is refused and writes nothing
    for (int i = 3; i < 16; ++i)
    {
        w.clear();
        CHECK_EQ (fxAddModule (v.plain, kFxTypeUtility, 2, w), i);
        applyTo (v, w);
    }
    CHECK_EQ (fxFirstFreeSlot (v.plain), -1);
    w.clear();
    CHECK_EQ (fxAddModule (v.plain, kFxTypeUtility, 0, w), -1);
    CHECK (w.empty());
    CHECK_EQ (listOf (v, 2).size(), static_cast<size_t> (13));
    for (size_t i = 1; i < listOf (v, 2).size(); ++i)   // in the order they were added
        CHECK_EQ (listOf (v, 2)[i].index, static_cast<int> (i) + 3);
    CHECK (consistent (v));

    // types that the pool does not have
    w.clear();
    CHECK_EQ (fxAddModule (ParamValues::defaults().plain, kFxTypeNone, 0, w), -1);
    CHECK_EQ (fxAddModule (ParamValues::defaults().plain, kNumFxInstanceTypes, 0, w), -1);
}

TEST (fxrack_remove_gives_the_slot_back_at_its_defaults)
{
    ParamValues v = ParamValues::defaults();
    std::vector<FxWrite> w;
    fxAddModule (v.plain, kFxTypeBode, 2, w);
    applyTo (v, w);
    v.plain[fxInstanceKeys (0).p[3]] = 0.77f;
    w.clear();
    CHECK (fxRemoveSlot (v.plain, 0, w));
    applyTo (v, w);
    const ParamValues defaults = ParamValues::defaults();
    for (size_t i = 1603; i < 1603 + 28; ++i)
        CHECK_EQ (v.plain[i], defaults.plain[i]);
    CHECK (listOf (v, 2).empty());
    w.clear();
    CHECK (! fxRemoveSlot (v.plain, 0, w)); // already empty
    CHECK (! fxRemoveSlot (v.plain, 16, w));
}

TEST (fxrack_duplicate_copies_a_module_of_instance_zero_and_a_slot_behind_the_original)
{
    ParamValues v = ParamValues::defaults();
    // a reverb of instance 0 with its own settings, in Bus 1
    v.plain[findParamIndex (findParamByKey ("reverb.size")->id)] = 80.0f;
    v.plain[findParamIndex (findParamByKey ("reverb.rack")->id)] = 1.0f;
    v.plain[findParamIndex (findParamByKey ("reverb.on")->id)] = 1.0f;
    std::vector<FxWrite> w;
    const int slot = fxDuplicate (v.plain, { false, 8 }, w);
    CHECK_EQ (slot, 0);
    applyTo (v, w);
    CHECK_EQ (static_cast<int> (v.plain[fxInstanceKeys (0).type]), static_cast<int> (kFxTypeReverb));
    CHECK_EQ (v.plain[fxInstanceKeys (0).on], 1.0f);
    CHECK_EQ (v.plain[fxInstanceKeys (0).rack], 1.0f);
    // "Reverb Size" is the 2nd key of the reverb: the slot holds it normalised, the plain value comes back
    CHECK_NEAR (fxInstancePlain (kFxTypeReverb, 1, v.plain[fxInstanceKeys (0).p[1]]), 80.0, 1.0e-3);
    CHECK (listOf (v, 1).size() == 2 && listOf (v, 1)[0] == (FxRackItem { false, 8 }) && listOf (v, 1)[1] == (FxRackItem { true, 0 }));

    // a duplicate of the slot: behind it
    w.clear();
    CHECK_EQ (fxDuplicate (v.plain, { true, 0 }, w), 1);
    applyTo (v, w);
    CHECK (listOf (v, 1).size() == 3 && listOf (v, 1)[2] == (FxRackItem { true, 1 }));
    for (int p = 0; p < kFxInstanceParams; ++p)
        CHECK_EQ (v.plain[fxInstanceKeys (1).p[p]], v.plain[fxInstanceKeys (0).p[p]]);

    // a splitter cannot be duplicated, nor can an empty slot
    w.clear();
    CHECK_EQ (fxDuplicate (v.plain, { false, 13 }, w), -1);
    CHECK_EQ (fxDuplicate (v.plain, { true, 9 }, w), -1);
    CHECK (w.empty());
    CHECK (consistent (v));

    // a module in a band rack is copied to Main
    v.plain[findParamIndex (findParamByKey ("chorus.rack")->id)] = 4.0f;
    w.clear();
    const int copy = fxDuplicate (v.plain, { false, 0 }, w);
    applyTo (v, w);
    CHECK_EQ (v.plain[fxInstanceKeys (copy).rack], 0.0f);
}

TEST (fxrack_move_reorders_within_a_rack_and_between_racks)
{
    ParamValues v = ParamValues::defaults();
    std::vector<FxWrite> w;
    // two slots in Main: A (slot 0) behind the sixteen modules, B (slot 1) behind A
    fxAddModule (v.plain, kFxTypeDelay, 0, w);
    applyTo (v, w);
    w.clear();
    fxAddModule (v.plain, kFxTypeChorus, 0, w);
    applyTo (v, w);

    // B to the front of Main
    w.clear();
    CHECK (fxMove (v.plain, { true, 1 }, 0, 0, w));
    applyTo (v, w);
    CHECK (listOf (v, 0)[0] == (FxRackItem { true, 1 }));
    CHECK (listOf (v, 0)[1] == (FxRackItem { false, 5 }));   // the FX filter was first
    CHECK_EQ (listOf (v, 0).size(), static_cast<size_t> (18));

    // A behind the 3rd module of instance 0 (the list is now B, FX filter, distortion, compressor, EQ ...)
    w.clear();
    CHECK (fxMove (v.plain, { true, 0 }, 0, 4, w));
    applyTo (v, w);
    CHECK (listOf (v, 0)[4] == (FxRackItem { true, 0 }));
    CHECK (listOf (v, 0)[3] == (FxRackItem { false, 1 }));
    CHECK (listOf (v, 0)[5] == (FxRackItem { false, 4 }));

    // a module of instance 0 within Main: the EQ (3rd of instance 0) to the end
    w.clear();
    CHECK (fxMove (v.plain, { false, 4 }, 0, 17, w));
    applyTo (v, w);
    CHECK (listOf (v, 0).back() == (FxRackItem { false, 4 }));

    // a module of instance 0 to Bus 2, and a slot to Bus 1
    w.clear();
    CHECK (fxMove (v.plain, { false, 8 }, 2, 0, w));
    applyTo (v, w);
    CHECK (listOf (v, 2).size() == 1 && listOf (v, 2)[0] == (FxRackItem { false, 8 }));
    CHECK_EQ (v.plain[findParamIndex (findParamByKey ("reverb.rack")->id)], 2.0f);
    w.clear();
    CHECK (fxMove (v.plain, { true, 1 }, 1, 0, w));
    applyTo (v, w);
    CHECK (listOf (v, 1).size() == 1 && listOf (v, 1)[0] == (FxRackItem { true, 1 }));
    CHECK_EQ (v.plain[fxInstanceKeys (1).rack], 1.0f);

    // into a rack behind a module of instance 0 and in front of another one that sits in the same rack
    w.clear();
    CHECK (fxMove (v.plain, { true, 1 }, 2, 1, w));
    applyTo (v, w);
    CHECK (listOf (v, 2).size() == 2 && listOf (v, 2)[0] == (FxRackItem { false, 8 }) && listOf (v, 2)[1] == (FxRackItem { true, 1 }));

    // a move to where it is changes nothing
    w.clear();
    CHECK (! fxMove (v.plain, { true, 1 }, 2, 1, w));
    CHECK (w.empty());
    CHECK (consistent (v));
}

TEST (fxrack_random_edits_always_give_the_lists_that_were_asked_for)
{
    sbtest::Rng rng (2026);
    ParamValues v = ParamValues::defaults();
    // the model: the three lists kept by hand, the edits applied to them and compared with what the keys say
    std::array<std::vector<FxRackItem>, kNumPoolRacks> model;
    for (int q = 0; q < 16; ++q)
        model[0].push_back (listOf (v, 0)[static_cast<size_t> (q)]);
    for (int step = 0; step < 4000; ++step)
    {
        std::vector<FxWrite> w;
        const int op = static_cast<int> (rng.u32() % 10);
        std::vector<FxRackItem> all;
        for (const auto& list : model)
            all.insert (all.end(), list.begin(), list.end());
        const FxRackItem item = all[rng.u32() % all.size()];
        if (op <= 1)
        {
            const int rack = static_cast<int> (rng.u32() % 3);
            const int slot = fxAddModule (v.plain, 1 + static_cast<int> (rng.u32() % 13), rack, w);
            if (slot >= 0)
                model[static_cast<size_t> (rack)].push_back ({ true, slot });
        }
        else if (op == 2 && item.pool)
        {
            if (fxRemoveSlot (v.plain, item.index, w))
                for (auto& list : model)
                    list.erase (std::remove (list.begin(), list.end(), item), list.end());
        }
        else if (op == 3)
        {
            const int slot = fxDuplicate (v.plain, item, w);
            if (slot >= 0)
                for (auto& list : model)
                {
                    const auto at = std::find (list.begin(), list.end(), item);
                    if (at != list.end())
                    {
                        list.insert (at + 1, FxRackItem { true, slot });
                        break;
                    }
                }
        }
        else
        {
            const int rack = static_cast<int> (rng.u32() % 3);
            // the model: remove, then insert at the index of the list without the item
            auto without = model[static_cast<size_t> (rack)];
            without.erase (std::remove (without.begin(), without.end(), item), without.end());
            const int index = static_cast<int> (rng.u32() % (without.size() + 1));
            if (fxMove (v.plain, item, rack, index, w) || true)
            {
                for (auto& list : model)
                    list.erase (std::remove (list.begin(), list.end(), item), list.end());
                model[static_cast<size_t> (rack)].insert (model[static_cast<size_t> (rack)].begin() + index, item);
            }
        }
        applyTo (v, w);
        for (int r = 0; r < kNumPoolRacks; ++r)
            if (listOf (v, r) != model[static_cast<size_t> (r)])
            {
                CHECK (false);
                std::printf ("    step %d op %d: rack %d differs (%zu items, the model has %zu)\n", step, op, r, listOf (v, r).size(),
                             model[static_cast<size_t> (r)].size());
                return;
            }
        if (! consistent (v))
        {
            CHECK (false);
            std::printf ("    step %d: the racks are not consistent\n", step);
            return;
        }
        for (const auto& wr : w)
            CHECK (wr.param >= 0 && wr.param < static_cast<int> (paramCount()) && wr.value >= paramTable()[wr.param].min && wr.value <= paramTable()[wr.param].max);
    }
    CHECK (true);
}

TEST (fxrack_init_slot_keeps_type_power_rack_and_order)
{
    ParamValues v = ParamValues::defaults();
    std::vector<FxWrite> w;
    fxAddModule (v.plain, kFxTypeEq, 1, w);
    applyTo (v, w);
    const float order = v.plain[fxInstanceKeys (0).order];
    for (int p = 0; p < kFxInstanceParams; ++p)
        v.plain[fxInstanceKeys (0).p[p]] = 0.5f;
    w.clear();
    CHECK (fxInitSlot (v.plain, 0, w));
    applyTo (v, w);
    float defaults[kFxInstanceParams];
    fxInstanceDefaults (kFxTypeEq, defaults);
    for (int p = 0; p < kFxInstanceParams; ++p)
        CHECK_EQ (v.plain[fxInstanceKeys (0).p[p]], defaults[p]);
    CHECK_EQ (static_cast<int> (v.plain[fxInstanceKeys (0).type]), static_cast<int> (kFxTypeEq));
    CHECK_EQ (v.plain[fxInstanceKeys (0).on], 1.0f);
    CHECK_EQ (v.plain[fxInstanceKeys (0).rack], 1.0f);
    CHECK_EQ (v.plain[fxInstanceKeys (0).order], order);
    w.clear();
    CHECK (! fxInitSlot (v.plain, 5, w)); // an empty slot has nothing to init
}

TEST (fxrack_set_on_writes_the_power_of_instance_zero_and_of_a_slot)
{
    ParamValues v = ParamValues::defaults();
    std::vector<FxWrite> w;
    fxSetOn (v.plain, { false, 2 }, true, w);
    applyTo (v, w);
    CHECK (fxItemOn (v.plain, { false, 2 }));
    CHECK_EQ (v.plain[findParamIndex (findParamByKey ("delay.on")->id)], 1.0f);
    w.clear();
    fxAddModule (v.plain, kFxTypeFlanger, 0, w);
    applyTo (v, w);
    w.clear();
    fxSetOn (v.plain, { true, 0 }, false, w);
    applyTo (v, w);
    CHECK (! fxItemOn (v.plain, { true, 0 }));
    w.clear();
    fxSetOn (v.plain, { true, 0 }, false, w);
    CHECK (w.empty()); // no write for no change
}
