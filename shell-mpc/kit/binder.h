// MpcKit - the BINDER: connects ready UI components (Knob, NumberField, Stepper, LedToggle, LedList, Fader) to parameters of the plugin. Every frame it pulls the parameter values into the
// components' state structs (pull) and, when a component reports a Change, writes the new value back through the instance (apply). The page code only says WHICH component shows WHICH parameter.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This is the generic form of what shell-mpc/overlay/master_ui.h (pull / handle) and shell-mpc/overlay/ui_bind.h do for SynthyBoy's parameters, over IParamSource / IInstance instead of
// SynthyBoy's table. Components never store the truth: the value comes in through their state, and after a change the owner (this class) writes the new value to the instance, which the next
// pull shows.
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../ui/base.h"
#include "../ui/led_list.h"
#include "../ui/led_toggle.h"
#include "../ui/mixer_strip.h"
#include "instance.h"
#include "param_source.h"

namespace mpckit {

class Binder
{
public:
    /** What the component is (the Binder casts to it): Knob = ui::Knob, Number = ui::NumberField, Toggle = ui::ToggleIcon / LedToggle, Stepper = ui::Stepper, List = ui::LedList (2 .. 8 options),
        Fader = ui::Fader. Inert: a component the page handles itself. */
    enum class Type : uint8_t { Knob, Number, Toggle, Stepper, List, Fader, Inert };

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        Type type = Type::Inert;
        const char* key = "";
        int param = -1;   // resolved by resolve ()
    };

    /** Adds a binding. `id` is the number the component reports its changes with (unique on the page). */
    void bind (int id, ui::Component* c, Type type, const char* key)
    {
        Item it;
        it.id = id;
        it.c = c;
        it.type = type;
        it.key = key;
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }
    /** Looks up the keys in the table (once: the page calls it at its first update). A key the table does not have leaves the component disabled. */
    void resolve (const IParamSource& params)
    {
        for (Item& it : items_)
        {
            it.param = params.indexOfKey (it.key);
            if (it.c != nullptr)
                it.c->setEnabled (it.param >= 0);
        }
        resolved_ = true;
    }
    bool resolved() const noexcept { return resolved_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept
    {
        for (const Item& it : items_)
            if (it.id == id)
                return &it;
        return nullptr;
    }
    const Item* itemOf (const ui::Component* c) const noexcept
    {
        for (const Item& it : items_)
            if (it.c == c)
                return &it;
        return nullptr;
    }
    /** The parameter index a component shows, -1 when it has none (a Q slot with no target). */
    int paramOf (const ui::Component* c) const noexcept
    {
        const Item* it = itemOf (c);
        return it != nullptr ? it->param : -1;
    }

    /** Parameter values -> component states. */
    void pull (const IParamSource& params, const IInstance& inst) const
    {
        for (const Item& it : items_)
        {
            if (it.param < 0 || it.c == nullptr)
                continue;
            const ParamDesc& p = params.desc (it.param);
            const float val = inst.param (it.param);
            switch (it.type)
            {
                case Type::Knob:
                {
                    ui::Knob* k = static_cast<ui::Knob*> (it.c);
                    k->state.value = k->state.modValue = params.normOf (it.param, val);
                    k->state.defaultValue = params.normOf (it.param, p.def);
                    params.valueText (it.param, val, k->state.valueText, sizeof k->state.valueText);
                    break;
                }
                case Type::Fader:
                {
                    ui::Fader* f = static_cast<ui::Fader*> (it.c);
                    f->state.value = f->state.modValue = params.normOf (it.param, val);
                    f->state.defaultValue = params.normOf (it.param, p.def);
                    params.valueText (it.param, val, f->state.valueText, sizeof f->state.valueText);
                    break;
                }
                case Type::Number:
                {
                    ui::NumberField* n = static_cast<ui::NumberField*> (it.c);
                    n->state.value = val;
                    n->state.min = p.min;
                    n->state.max = p.max;
                    n->state.defaultValue = p.def;
                    n->state.step = p.interval > 0.0f ? p.interval : 1.0;
                    break;
                }
                case Type::Toggle:
                    static_cast<ui::ToggleIcon*> (it.c)->state.on = val > 0.5f;
                    break;
                case Type::Stepper:
                {
                    ui::Stepper* s = static_cast<ui::Stepper*> (it.c);
                    s->state.count = p.numChoices > 0 ? p.numChoices : std::max (1, static_cast<int> (std::lround (p.max - p.min)) + 1);
                    s->state.index = std::clamp (static_cast<int> (std::lround (val - p.min)), 0, s->state.count - 1);
                    char text[48];
                    params.valueText (it.param, val, text, sizeof text);
                    s->setItem (text);
                    break;
                }
                case Type::List:
                {
                    ui::LedList* l = static_cast<ui::LedList*> (it.c);
                    l->setOptions (p.choices, p.numChoices);
                    l->state.index = std::clamp (static_cast<int> (std::lround (val - p.min)), 0, std::max (0, l->state.count - 1));
                    break;
                }
                case Type::Inert:
                    break;
            }
        }
    }

    /** A component's change -> parameter write. true: it was a change of a bound parameter and was written. A Request (a tap on the name of a stepper) is not handled here. */
    bool apply (const ui::Change& ch, const IParamSource& params, IInstance& inst) const
    {
        const Item* it = item (ch.id);
        if (it == nullptr || it->param < 0 || it->type == Type::Inert)
            return false;
        switch (ch.kind)
        {
            case ui::ChangeKind::Value:
                if (it->type == Type::Knob || it->type == Type::Fader)
                {
                    inst.writeParam (it->param, static_cast<float> (std::clamp (ch.value, 0.0, 1.0)));
                    return true;
                }
                return false;
            case ui::ChangeKind::Index:
                if (it->type == Type::Number)
                    writePlain (params, inst, it->param, static_cast<float> (ch.value));
                else if (it->type == Type::Toggle)
                    writePlain (params, inst, it->param, static_cast<float> (ch.index));
                else if (it->type == Type::Stepper || it->type == Type::List)
                    writePlain (params, inst, it->param, params.desc (it->param).min + static_cast<float> (ch.index));
                else
                    return false;
                return true;
            default:
                return false;
        }
    }
    /** Writes a plain value (constrained to the range and the grid) as the normalised value the instance takes. */
    static void writePlain (const IParamSource& params, IInstance& inst, int index, float plain)
    {
        inst.writeParam (index, params.toNormalised (index, params.constrain (index, plain)));
    }

private:
    std::vector<Item> items_;
    bool resolved_ = false;
};

} // namespace mpckit
