// SynthyBoy - own engine: the FX cost budget of a quality tier (Quality::fxBudgetPct), docs/perf/fx-budget.md. A cost table per module type and configuration (the Key 61 calibration) and
// the admission of the effect instances in the chain order: deterministic, allocation free, a function of the effect configuration only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

#include "synthyboy/engine.h"

namespace synthyboy
{
namespace own
{
/** What FxBudget::evaluate found: the instances of the three racks that are over budget (bit m of `overModules`: the module m of instance 0, 0 .. 12; bit s of `overSlots`: the slot s of the pool)
    and the summed costs, in hundredths of a percent of a 128 frame block at 44.1 kHz on the Key 61. */
struct FxBudgetResult
{
    uint32_t overModules = 0, overSlots = 0;
    int admittedCenti = 0;   // what runs
    int requestedCenti = 0;  // what would run without the budget (the instances the mask switches off are in neither)
    bool operator== (const FxBudgetResult& o) const noexcept
    {
        return overModules == o.overModules && overSlots == o.overSlots && admittedCenti == o.admittedCenti && requestedCenti == o.requestedCenti;
    }
    bool operator!= (const FxBudgetResult& o) const noexcept { return ! (*this == o); }
};

/** The cost of module `module` (0 .. 15: the fx.order choices, Splitters 13 .. 15) in the configuration that the keys in `values` (plain values indexed like the parameter table) hold, in hundredths
    of a percent of a 128 frame block at 44.1 kHz on the MPC Key 61. It reads only the keys that change what runs (compressor bands and mode, delay style and hq, distortion type and filter order,
    EQ law and second mid band, FX filter model, utility mono bass), never a modulated value. */
int fxModuleCostCenti (int module, const float* values) noexcept;
/** The cost of the effect in pool slot `slot` (0 .. 15), from the plain values of its p keys in `base` (the snapshot of the parameters); 0 for a slot without a type. */
int fxSlotCostCenti (int slot, const float* base) noexcept;
/** true for a module that the budget counts but never bypasses: the Reverb (it runs only when the profile or reverb.force_enable asks for it) and the Splitters (bypassing one would take the effects in its bands out of the chain). */
constexpr bool fxModuleExemptFromBudget (int module) noexcept { return module == 8 || module >= 13; }

class FxBudget
{
public:
    /** Admits the effect instances that run (on, type allowed by `fxMask`, in a rack that is reached) in the chain order: the rack Main, Bus 1, Bus 2 (an effect in a band of a splitter at the place of the splitter), inside a rack
        by the order key (the modules of instance 0 by fx.order, the slots by fxiNN.order; on a tie the module first, then the slots by number: the order of FxChain's plan). While the running sum of the costs stays
        within `budgetPct` percent of a block the instance is admitted; from the first one that does not fit, every later one is over budget. `budgetPct` <= 0: nothing is over. Exempt modules
        (fxModuleExemptFromBudget) are counted and always admitted. Reads `base` only (no modulation): the result changes only when the effect configuration does. */
    static FxBudgetResult evaluate (const float* base, uint32_t fxMask, int budgetPct) noexcept;
};
} // namespace own
} // namespace synthyboy
