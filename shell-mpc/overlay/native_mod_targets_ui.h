// SynthyBoy - host control ownership and edit routing shared by the TARGETS pages.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../native_mod_targets.h"
#include "../ui/mod_targets.h"
#include "instance_view.h"
#include "synthyboy/modulation.h"

namespace synthyboy_mpc::overlay
{
class NativeModTargets
{
public:
    static_assert (ui::ModTargetsPage::kPerPage == nativeui::targets::kRows);

    template<class Screen>
    void hide (IInstanceView& inst, Screen& screen)
    {
        if (! active_)
            return;
        active_ = false;
        screen.setNativeControls (false);
        inst.hideNativeModTargets (this);
    }

    template<class Screen, class Row, class Apply>
    void update (IInstanceView& inst, Screen& screen, bool shown, nativeui::targets::Owner owner,
                 int subject, const Row* routes, Apply&& apply, nativeui::targets::Mode mode = nativeui::targets::Targets)
    {
        namespace n = nativeui::targets;
        static_assert (Screen::kPerPage == n::kRows);
        if (! shown || subject <= 0 || screen.rect().w != 1280 || screen.rect().h != 628
            || ! inst.nativeModTargetsSupported())
        {
            hide (inst, screen);
            return;
        }

        active_ = true;
        screen.setNativeControls (true);
        const auto bind = [&]
        {
            n::State state;
            state.owner = owner;
            state.mode = mode;
            state.presenter = this;
            state.subject = subject;
            state.page = screen.page.state.index;
            state.preset = inst.stateGeneration();
            state.bindings[n::Back] = {0, 0, nativeui::kFxDial};
            state.bindings[n::Add] = {0, 0, nativeui::kFxDial};
            if (screen.page.state.count > 1)
                state.bindings[n::Page] = {screen.page.state.count, state.page, nativeui::kFxDial};
            for (int row = 0; row < screen.numRows(); ++row)
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
        for (auto* button : {&screen.back, &screen.add})
        {
            if (take (button == &screen.back ? n::Back : n::Add, value))
            {
                hide (inst, screen);
                apply (ui::Change::request (button->id()));
                return;
            }
        }
        if (take (n::Page, value))
        {
            const int index = state.page + (value > 0.5f ? 1 : -1);
            if (index >= 0 && index < screen.page.state.count)
            {
                inst.hideNativeModTargets (this);
                apply (ui::Change::indexed (screen.page.id(), index, index));
                bind();
                return;
            }
        }
        for (int row = 0; row < screen.numRows(); ++row)
        {
            if (take (n::field (row, n::Remove), value))
            {
                inst.hideNativeModTargets (this);
                screen.selectRow (row);
                apply (ui::Change::request (screen.rows[row].remove.id()));
                if (mode == n::Modulations && screen.numRows() == 0)
                    hide (inst, screen);
                else
                    bind();
                return;
            }
        }
        for (int row = 0; row < screen.numRows(); ++row)
        {
            auto& r = screen.rows[row];
            if (take (n::field (row, n::Amount), value))
            {
                screen.selectRow (row);
                apply (ui::Change::normalised (r.depth.id(), value));
            }
            if (take (n::field (row, n::Polarity), value))
            {
                screen.selectRow (row);
                const int index = value >= 0.5f;
                apply (ui::Change::indexed (r.polarity.id(), index, index));
            }
            if (take (n::field (row, n::Active), value))
            {
                screen.selectRow (row);
                const int index = value >= 0.5f;
                apply (ui::Change::indexed (r.bypass.id(), index, index));
            }
        }
    }

private:
    bool active_ = false;
};
} // namespace synthyboy_mpc::overlay
