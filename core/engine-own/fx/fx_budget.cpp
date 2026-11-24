// SynthyBoy - own engine: the FX cost budget (fx_budget.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fx_budget.h"

#include <algorithm>

#include "fx_chain.h"

namespace synthyboy
{
namespace own
{
namespace
{
/* The cost table, in hundredths of a percent of a 128 frame block at 44.1 kHz. Calibration: the MPC Key 61 (Cortex-A17, 1608 MHz, core 3), tier low, Acid Saw Reso with 8 notes, 16 identical instances in
   slots fxi01..16 of the Main rack, cost = (median of the load - median of the base) / 16, one run per config (`.hw-local/mpc-perf/key61-night-2026-11-21/fx-stack/screen-table.md`, 2026-11-21); the
   numbers of the summary of the final runs (compressor multiband Limit 9.4, FX filter model 13 6.5, model 18 4.9, chorus 6.0, Compress / Serum 5.5, phaser 4.3, delay hq 4.1, distortion 06 with the pre filter
   3.6, EQ with the second mid band 3.0, utility mono bass 1.5) are used where they are higher than the screening run.
   What was not measured gets a conservative value from the nearest measured one (each is marked "not measured"):
     - compressor bands 1 / 2 (Low / High band, two bands): the Multiband figure;
     - chorus voices, delay times, drive and the like: only the default settings were measured; the one figure is used for every setting;
     - distortion filter order 2 (Post): the Pre figure (the same filter runs);
     - EQ without the second mid band, utility without mono bass: the figure with them (the nearest measured one);
     - Flanger: the Chorus figure (the same kind of modulated delay), not measured on the device;
     - Reverb, Hyper / Dimension, Bode, Convolve: not measured on the device; 20 %, twice the dearest measured instance, until they are (the Mac figures of bench/fx_cost_bench do not carry over to the A17);
     - Splitters: 1.5 % (Low/High, Mid/Side) and 3 % (Low/Mid/High: two crossovers), the utility mono bass figure per crossover, not measured. */
constexpr int kChorus = 601, kPhaser = 428, kFlanger = kChorus, kHeavyUnmeasured = 2000, kUtility = 150;
// compressor: [single band (bands 3) ? 1 : 0][mode 0 Compress, 1 Limit, 2 Serum]
constexpr int kCompressor[2][3] = { { 555, 944, 549 }, { 255, 372, 258 } };
// delay: [style 0 Mono, 1 Stereo, 2 Ping pong, 3 Mid ping pong, 4 Tap->Delay][hq]
constexpr int kDelay[5][2] = { { 217, 405 }, { 222, 406 }, { 218, 406 }, { 214, 412 }, { 219, 407 } };
// distortion: [type 0 .. 13][filter order None, Pre (Post uses Pre)]
constexpr int kDistortion[14][2] = { { 286, 339 }, { 91, 195 }, { 153, 245 }, { 198, 278 }, { 78, 196 }, { 122, 210 }, { 306, 360 }, { 246, 307 },
                                     { 283, 297 }, { 130, 225 }, { 228, 293 }, { 145, 245 }, { 117, 217 }, { 171, 256 } };
// EQ: [law 0 Vital, 1 Serum][second mid band on]; the second mid band off was not measured: the figure with it
constexpr int kEq[2][2] = { { 298, 298 }, { 213, 213 } };
// FX filter model 0 .. 26 (the choices of fxfilter.model)
constexpr int kFilterModel[27] = { 97, 276, 319, 84, 448, 125, 202, 406, 253, 358, 218, 160, 168, 650, 372, 273, 236, 293, 490, 263, 268, 237, 357, 250, 194, 369, 254 };
// utility: [mono bass on]; off was not measured: the figure with it
constexpr int kUtilityMono[2] = { kUtility, kUtility };

struct Keys
{
    int bands, limit, delayStyle, delayHq, distType, distOrder, eqLaw, eqMid2, filterModel, monoBass;
    Keys()
    {
        bands = fxModuleKeyIndex ("compressor.bands");
        limit = fxModuleKeyIndex ("compressor.limit");
        delayStyle = fxModuleKeyIndex ("delay.style");
        delayHq = fxModuleKeyIndex ("delay.hq");
        distType = fxModuleKeyIndex ("distortion.type");
        distOrder = fxModuleKeyIndex ("distortion.filter_order");
        eqLaw = fxModuleKeyIndex ("eq.law");
        eqMid2 = fxModuleKeyIndex ("eq.mid2.mode");
        filterModel = fxModuleKeyIndex ("fxfilter.model");
        monoBass = fxModuleKeyIndex ("utility.mono_bass");
    }
};

const Keys& keys()
{
    static const Keys k;
    return k;
}

int pick (float v, int hi) noexcept { return std::clamp (static_cast<int> (v + 0.5f), 0, hi); }

/** The cost of module `module`; `get (keyIndex)` gives the plain value of a source key. */
template <class Get>
int costOf (int module, Get&& get) noexcept
{
    const Keys& k = keys();
    switch (module)
    {
        case 0: return kChorus;
        case 1: return kCompressor[pick (get (k.bands), 3) == 3 ? 1 : 0][pick (get (k.limit), 2)];
        case 2: return kDelay[pick (get (k.delayStyle), 4)][pick (get (k.delayHq), 1)];
        case 3: return kDistortion[pick (get (k.distType), 13)][pick (get (k.distOrder), 2) == 0 ? 0 : 1];
        case 4: return kEq[pick (get (k.eqLaw), 1)][pick (get (k.eqMid2), 2) == 0 ? 0 : 1];
        case 5: return kFilterModel[pick (get (k.filterModel), 26)];
        case 6: return kFlanger;
        case 7: return kPhaser;
        case 8: case 9: case 11: case 12: return kHeavyUnmeasured;
        case 10: return kUtilityMono[pick (get (k.monoBass), 1)];
        case 13: case 15: return 150;
        case 14: return 300;
        default: return 0;
    }
}

struct Item
{
    int ownerRack, ownerKey, band, key, pool, index, cost, module;
    bool operator< (const Item& o) const noexcept
    {
        if (ownerRack != o.ownerRack) return ownerRack < o.ownerRack;
        if (ownerKey != o.ownerKey) return ownerKey < o.ownerKey;
        if (band != o.band) return band < o.band;
        if (key != o.key) return key < o.key;
        if (pool != o.pool) return pool < o.pool;
        return index < o.index;
    }
};
} // namespace

int fxModuleCostCenti (int module, const float* values) noexcept
{
    if (module < 0 || module > 15)
        return 0;
    return costOf (module, [values] (int idx) { return idx >= 0 ? values[idx] : 0.0f; });
}

int fxSlotCostCenti (int slot, const float* base) noexcept
{
    const FxInstanceKeys& sk = fxInstanceKeys (slot);
    if (sk.type < 0)
        return 0;
    const int type = std::clamp (static_cast<int> (base[sk.type] + 0.5f), 0, kNumFxInstanceTypes - 1);
    if (type == 0)
        return 0;
    const FxTypeDescriptor& d = fxTypeDescriptor (type);
    return costOf (type - 1, [&] (int idx)
    {
        for (int k = 0; k < d.count; ++k)
            if (d.source[k] == idx)
                return fxInstancePlain (type, k, std::clamp (base[sk.p[k]], 0.0f, 1.0f));
        return 0.0f; // a key the type does not map: its first choice
    });
}

FxBudgetResult FxBudget::evaluate (const float* base, uint32_t fxMask, int budgetPct) noexcept
{
    const FxIndex& f = fxIndex();
    FxBudgetResult out;
    Item items[16 + FxPool::kSlots];
    int count = 0;
    // fx.order repaired to a permutation, as FxChain::update does
    int position[16] = {};
    {
        bool used[16] = {};
        for (int i = 0; i < 16; ++i)
        {
            int m = f.order[i] >= 0 ? std::clamp (static_cast<int> (base[f.order[i]] + 0.5f), 0, 15) : i;
            if (used[m])
            {
                m = 0;
                while (m < 16 && used[m])
                    ++m;
            }
            m = std::min (m, 15);
            used[m] = true;
            position[m] = i;
        }
    }
    bool on[16] = {};
    int rack[16] = {};
    for (int m = 0; m < 16; ++m)
    {
        on[m] = f.on[m] >= 0 && base[f.on[m]] >= 0.5f && (fxMask & (1u << m)) != 0;
        int r = f.rack[m] >= 0 ? std::clamp (static_cast<int> (base[f.rack[m]] + 0.5f), 0, 9) : 0;
        if (m >= 13 && r >= 3)
            r = 0;
        rack[m] = r;
    }
    for (int m = 0; m < 16; ++m)
    {
        if (!on[m])
            continue;
        const int r = rack[m];
        Item it { r, fxModuleSortKey (position[m]), 0, fxModuleSortKey (position[m]), 0, m, 0, m };
        if (r >= 3)
        {
            const int splitter = r <= 4 ? 13 : (r <= 7 ? 14 : 15); // the band of this splitter; an effect in the band of a splitter that is off runs nowhere
            if (!on[splitter])
                continue;
            it.ownerRack = rack[splitter];
            it.ownerKey = fxModuleSortKey (position[splitter]);
            it.band = r;
        }
        it.cost = fxModuleCostCenti (m, base);
        items[count++] = it;
    }
    for (int s = 0; s < FxPool::kSlots; ++s)
    {
        const FxInstanceKeys& k = fxInstanceKeys (s);
        if (k.type < 0 || base[k.on] < 0.5f)
            continue;
        const int type = std::clamp (static_cast<int> (base[k.type] + 0.5f), 0, kNumFxInstanceTypes - 1);
        if (type == 0 || (fxMask & (1u << (type - 1))) == 0)
            continue;
        const int r = std::clamp (static_cast<int> (base[k.rack] + 0.5f), 0, kNumPoolRacks - 1);
        const int order = std::clamp (static_cast<int> (base[k.order] + 0.5f), 0, kFxOrderMax);
        items[count++] = Item { r, order, 0, order, 1, s, fxSlotCostCenti (s, base), type - 1 };
    }
    for (int i = 1; i < count; ++i) // insertion sort (at most 32 items, no allocation)
    {
        const Item it = items[i];
        int j = i;
        for (; j > 0 && it < items[j - 1]; --j)
            items[j] = items[j - 1];
        items[j] = it;
    }
    const int budget = budgetPct > 0 ? budgetPct * 100 : 0;
    bool closed = false;
    for (int i = 0; i < count; ++i)
    {
        const Item& it = items[i];
        out.requestedCenti += it.cost;
        if (budget > 0 && !fxModuleExemptFromBudget (it.module) && (closed || out.admittedCenti + it.cost > budget))
        {
            closed = true;
            if (it.pool != 0)
                out.overSlots |= 1u << it.index;
            else
                out.overModules |= 1u << it.index;
            continue;
        }
        out.admittedCenti += it.cost;
    }
    return out;
}
} // namespace own
} // namespace synthyboy
