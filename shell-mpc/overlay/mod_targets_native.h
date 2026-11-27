// SynthyBoy - MPC overlay: host control ownership and edit routing of the TARGETS page of one source on plain data (modmodel::Targets, mod_model.h): the binding of the native cells (BACK, ADD TARGET, the page cell and the depth /
// polarity / state / remove cells of the rows) and the take of what the host wrote into them. No widget: the callback `apply` gets the page's change with the ids the owner gave.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../native_mod_targets.h"
#include "instance_view.h"
#include "mod_model.h"
#include "synthyboy/modulation.h"

namespace synthyboy_mpc::overlay
{
class ModTargetsNative
{
public:
    static_assert (modmodel::Targets::kPerPage == nativeui::targets::kRows);

    /** The ids of the cells the page reports changes with (the owner's numbering). */
    struct Ids { int back, add, page, rowBase; };

    void hide (IInstanceView& inst)
    {
        if (! active_)
            return;
        active_ = false;
        inst.hideNativeModTargets (this);
    }
    bool active() const noexcept { return active_; }

    template<class Row, class Apply>
    void update (IInstanceView& inst, modmodel::Targets& page, bool shown, nativeui::targets::Owner owner,
                 int subject, const Row* routes, const Ids& ids, Apply&& apply, nativeui::targets::Mode mode = nativeui::targets::Targets)
    {
        namespace n = nativeui::targets;
        if (! shown || subject <= 0 || page.rect().w != 1280 || page.rect().h != 628 || ! inst.nativeModTargetsSupported())
        {
            hide (inst);
            return;
        }

        active_ = true;
        const auto bind = [&]
        {
            n::State state;
            state.owner = owner;
            state.mode = mode;
            state.presenter = this;
            state.subject = subject;
            state.page = page.pageIndex();
            state.preset = inst.stateGeneration();
            state.bindings[n::Back] = {0, 0, nativeui::kFxDial};
            state.bindings[n::Add] = {0, 0, nativeui::kFxDial};
            if (page.pageCount() > 1)
                state.bindings[n::Page] = {page.pageCount(), state.page, nativeui::kFxDial};
            for (int row = 0; row < page.numRows(); ++row)
            {
                const int slot = routes[row].slot;
                if (slot < 0)
                    continue;
                const auto sp = synthyboy::modSlotParams (slot);
                if (sp.source < 0 || sp.destination < 0)
                    continue;
                const int src = static_cast<int> (std::lround (inst.param (sp.source)));
                const int dest = static_cast<int> (std::lround (inst.param (sp.destination)));
                const int route = mode == n::Targets && src == subject ? dest
                                : mode == n::Modulations && dest == subject ? src : 0;
                if (route <= 0)
                    continue;
                state.bindings[n::field (row, n::Amount)] = {sp.amount, route, nativeui::kFxDial};
                state.bindings[n::field (row, n::Polarity)] = {sp.bipolar, route, nativeui::kFxToggle};
                state.bindings[n::field (row, n::Active)] = {sp.bypass, route, nativeui::kFxToggle};
                state.bindings[n::field (row, n::Remove)] = {slot, route, nativeui::kFxDial};
            }
            inst.publishNativeModTargets (state);
            return state;
        };
        const auto state = bind();

        const auto take = [&] (int field, float& value)
        {
            return state.bindings[field].target >= 0 && inst.stateGeneration() == state.preset
                && inst.takeNativeModTarget (state, field, value);
        };
        float value;
        for (const int field : { n::Back, n::Add })
        {
            if (take (field, value))
            {
                hide (inst);
                apply (ui::Change::request (field == n::Back ? ids.back : ids.add));
                return;
            }
        }
        if (take (n::Page, value))
        {
            const int index = state.page + (value > 0.5f ? 1 : -1);
            if (index >= 0 && index < page.pageCount())
            {
                inst.hideNativeModTargets (this);
                apply (ui::Change::indexed (ids.page, index, index));
                bind();
                return;
            }
        }
        for (int row = 0; row < page.numRows(); ++row)
        {
            if (take (n::field (row, n::Remove), value))
            {
                inst.hideNativeModTargets (this);
                page.selectRow (row);
                apply (ui::Change::request (ids.rowBase + 4 * row + 3));
                if (mode == n::Modulations && page.numRows() == 0)
                    hide (inst);
                else
                    bind();
                return;
            }
        }
        for (int row = 0; row < page.numRows(); ++row)
        {
            if (take (n::field (row, n::Amount), value))
            {
                page.selectRow (row);
                apply (ui::Change::normalised (ids.rowBase + 4 * row, value));
            }
            if (take (n::field (row, n::Polarity), value))
            {
                page.selectRow (row);
                const int index = value >= 0.5f;
                apply (ui::Change::indexed (ids.rowBase + 4 * row + 1, index, index));
            }
            if (take (n::field (row, n::Active), value))
            {
                page.selectRow (row);
                const int index = value >= 0.5f;
                apply (ui::Change::indexed (ids.rowBase + 4 * row + 2, index, index));
            }
        }
    }

private:
    bool active_ = false;
};
} // namespace synthyboy_mpc::overlay
