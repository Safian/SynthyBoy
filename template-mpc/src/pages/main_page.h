// MpcKit Demo - PAGE 1 "Main": three knobs and a toggle in one Q group, a number field and a radio list in a second one. Built only from ready components of shell-mpc/ui/.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// HOW A PAGE IS MADE (read this file top to bottom, the other two pages only add one idea each):
//   1. derive from mpckit::PageBase and give the class a unique `kElementPage` (the id of its full screen page on the UiStack);
//   2. declare the components as members (ui::Knob, ui::LedToggle, ui::NumberField, ui::LedList, ui::Stepper, ui::Fader ...: shell-mpc/ui/, docs/ui-components/);
//   3. in the constructor: set the labels, bind () every component to a parameter KEY of demo_params.h, put the components into Q groups with group (), end with relayout ();
//   4. place the components in layout (): one rectangle each, inside the tab { 0, 110, 1280, 628 }, with 16 pixels free above every member of a Q group (the index chip of the Q-Link).
// Nothing else: the values, the touch, the wheel, the Q-Links and the host's automation come from PageBase and the Binder.
//
// TO ADD A KNOB: (a) a `ui::Knob cutoff2_;` member, (b) `bind (kCutoff2, cutoff2_, Binder::Type::Knob, "cutoff2")` in the constructor, (c) put it in a group () or leave it without a Q-Link,
// (d) `cutoff2_.place ({ x, y, w, h })` in layout (). The parameter itself is added in demo_params.h.
// TO ADD A TOGGLE: ui::LedToggle + Binder::Type::Toggle + a stepped parameter with two values.  A LIST OF 2 .. 8 CHOICES: ui::LedList + Binder::Type::List.  A WHOLE NUMBER: ui::NumberField + Binder::Type::Number.
#pragma once

#include "../../../shell-mpc/kit/kit.h"
#include "../log_compat.h"

namespace demo {

namespace ui = synthyboy_mpc::ui;

class MainPage : public mpckit::PageBase
{
public:
    static constexpr int kElementPage = 1;   // unique per page of the plugin
    using Type = mpckit::Binder::Type;

    // the ids the components report their changes with (any numbers below 700, unique on this page)
    enum Id : int { kIdLevel = 1, kIdPan, kIdCutoff, kIdFilterOn, kIdPitch, kIdMode };

    MainPage() : level_ ("Level"), pan_ ("Pan"), cutoff_ ("Cutoff"), pitch_ ("Pitch", ui::NumberField::Format::IntSigned)
    {
        filterOn_.setLabel ("FILTER");
        mode_.setTitle ("MODE");
        pan_.state.bipolar = true;                          // a detent at the centre, the arc runs from the middle

        bind (kIdLevel, level_, Type::Knob, "level");         // component, what it is, the KEY of the parameter
        bind (kIdPan, pan_, Type::Knob, "pan");
        bind (kIdCutoff, cutoff_, Type::Knob, "cutoff");
        bind (kIdFilterOn, filterOn_, Type::Toggle, "filter_on");
        bind (kIdPitch, pitch_, Type::Number, "pitch");
        bind (kIdMode, mode_, Type::List, "mode");

        group ({ &level_, &pan_, &cutoff_, &filterOn_ });   // Q-Link 1 .. 4 while the player touches one of them
        group ({ &pitch_, &mode_ });                        // Q-Link 5 .. 6 (the XL has 16 knobs; the Key 61 steps through them with its bank button)
        MPCKIT_LOG ("page", DEMO_DEBUG, "main page built");
        relayout();
    }

protected:
    void layout (const ui::Rect& tab) override
    {
        const int top = tab.y + 40;   // the first row: 16 px of Q lane above the knobs
        level_.place ({ 40, top, 280, 240 });
        pan_.place ({ 340, top, 280, 240 });
        cutoff_.place ({ 640, top, 280, 240 });
        filterOn_.place ({ 960, top, 260, 240 });
        filterOn_.centre = 0.45f;                           // draws at the height of the dials beside it
        const int row2 = tab.y + 330;
        pitch_.place ({ 40, row2, 280, 100 });
        pitch_.maxBox = { 220, 80 };                        // the field is small, the touch zone is the whole cell
        mode_.place ({ 340, row2, 580, 200 });
    }

private:
    ui::Knob level_, pan_, cutoff_;
    ui::LedToggle filterOn_;
    ui::NumberField pitch_;
    ui::LedList mode_;
};

} // namespace demo
