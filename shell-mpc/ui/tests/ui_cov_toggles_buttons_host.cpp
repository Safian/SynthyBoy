// SynthyBoy - MPC UI library test coverage: toggles and buttons
// Tests untested public behaviors of ToggleIcon, MenuButton,
// DetailButton and RouteButton.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../base.h"
#include "../filter_display.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

static void test_toggle_icon_basic_and_turn()
{
    ToggleIcon t;
    t.setId (201);
    t.place ({ 50, 50, 80, 56 });
    t.state.on = false;
    t.state.icon = ToggleIcon::Icon::Power;
    t.setLabel ("POWER");

    check (t.minSize().w == 72 && t.minSize().h == 56, "ToggleIcon minSize with label is 72x56");
    t.setLabel ("");
    check (t.minSize().w == 56 && t.minSize().h == 56, "ToggleIcon minSize without label is 56x56");
    t.setLabel ("POWER");

    // Tap to turn on
    Change c = t.touchDown (60, 60, 1.0);
    check (c.kind == ChangeKind::None, "ToggleIcon touchDown returns None");
    c = t.touchUp (60, 60);
    check (c.kind == ChangeKind::Index && c.index == 1 && c.value == 1.0, "ToggleIcon tap turns ON");
    t.state.on = true;

    // Tap to turn off
    t.touchDown (60, 60, 1.0);
    c = t.touchUp (60, 60);
    check (c.kind == ChangeKind::Index && c.index == 0 && c.value == 0.0, "ToggleIcon tap turns OFF");
    t.state.on = false;

    // Turn up switches ON
    c = t.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 1 && c.value == 1.0, "ToggleIcon turn +1 switches ON");
    t.state.on = true;

    // Turn up when already ON does nothing
    c = t.turn (1.0);
    check (c.kind == ChangeKind::None, "ToggleIcon turn +1 when ON does nothing");

    // Turn down switches OFF
    c = t.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 0 && c.value == 0.0, "ToggleIcon turn -1 switches OFF");
    t.state.on = false;

    // Turn down when already OFF does nothing
    c = t.turn (-1.0);
    check (c.kind == ChangeKind::None, "ToggleIcon turn -1 when OFF does nothing");

    // Zero turn does nothing
    c = t.turn (0.0);
    check (c.kind == ChangeKind::None, "ToggleIcon turn 0 does nothing");

    // Drag away cancels tap
    t.touchDown (60, 60, 1.0);
    t.touchMove (200, 60);
    c = t.touchUp (200, 60);
    check (c.kind == ChangeKind::None, "ToggleIcon drag away cancels tap");

    Image im = drawComponent (t, 300, 200);
    check (drawnOutside (im, t.rect(), backgroundPixel()) == 0, "ToggleIcon draws strictly inside rect");
}

static void test_toggle_icon_knob_like_and_show_state()
{
    ToggleIcon t;
    t.setId (202);
    t.place ({ 50, 50, 80, 80 });
    t.state.knobLike = true;
    t.state.on = true;
    t.setLabel ("LEGATO");

    Image im = drawComponent (t, 300, 200);
    check (drawnOutside (im, t.rect(), backgroundPixel()) == 0, "KnobLike ToggleIcon draws strictly inside rect");

    // Standard with showState and PitchTrack icon
    t.state.knobLike = false;
    t.state.showState = true;
    t.state.icon = ToggleIcon::Icon::PitchTrack;
    im = drawComponent (t, 300, 200);
    check (drawnOutside (im, t.rect(), backgroundPixel()) == 0, "ShowState PitchTrack ToggleIcon draws strictly inside rect");

    // Generic icon
    t.state.icon = ToggleIcon::Icon::Generic;
    t.state.on = false;
    im = drawComponent (t, 300, 200);
    check (drawnOutside (im, t.rect(), backgroundPixel()) == 0, "Generic ToggleIcon off draws strictly inside rect");
}

static void test_menu_button_variants()
{
    MenuButton mb;
    mb.setId (204);
    mb.place ({ 50, 50, 80, 56 });

    check (mb.minSize().w == 56 && mb.minSize().h == 56, "MenuButton minSize is 56x56");

    mb.setLabel ("PRESET");
    mb.setValue ("INIT");
    mb.state.icon = MenuButton::Icon::Down;

    // Tap produces Request
    mb.touchDown (60, 60, 1.0);
    Change c = mb.touchUp (60, 60);
    check (c.kind == ChangeKind::Request && c.id == 204, "MenuButton tap produces Request");

    // Turn does nothing
    c = mb.turn (1.0);
    check (c.kind == ChangeKind::None, "MenuButton turn does nothing");

    // Icon Gear
    mb.state.icon = MenuButton::Icon::Gear;
    Image im = drawComponent (mb, 300, 200);
    check (drawnOutside (im, mb.rect(), backgroundPixel()) == 0, "MenuButton Gear draws strictly inside rect");

    // Icon Expand
    mb.state.icon = MenuButton::Icon::Expand;
    im = drawComponent (mb, 300, 200);
    check (drawnOutside (im, mb.rect(), backgroundPixel()) == 0, "MenuButton Expand draws strictly inside rect");

    // Icon None
    mb.state.icon = MenuButton::Icon::None;
    im = drawComponent (mb, 300, 200);
    check (drawnOutside (im, mb.rect(), backgroundPixel()) == 0, "MenuButton None draws strictly inside rect");
}

static void test_detail_button_request_vs_value()
{
    DetailButton db;
    db.setId (205);
    db.place ({ 50, 50, 120, 56 });

    // 1. Without value: tap = Request, drag and turn do nothing
    db.state.hasValue = false;
    db.setLabel ("EDIT");
    check (db.minSize().w == 72 && db.minSize().h == 56, "DetailButton minSize without icon is 72x56");

    db.touchDown (60, 60, 1.0);
    Change c = db.touchUp (60, 60);
    check (c.kind == ChangeKind::Request && c.id == 205, "DetailButton tap produces Request");

    db.touchDown (60, 60, 1.0);
    c = db.touchMove (60, 10);
    check (c.kind == ChangeKind::None, "DetailButton without value ignores drag");
    db.touchUp (60, 10);

    c = db.turn (1.0);
    check (c.kind == ChangeKind::None, "DetailButton without value ignores turn");

    // 2. With continuous value (0 .. 1)
    db.state.hasValue = true;
    db.state.integer = false;
    db.state.value = 0.5;
    check (std::fabs (db.wheelUnit() - 1.0 / 128.0) < 1.0e-9, "DetailButton continuous wheelUnit is 1/128");

    // Turn adds delta
    c = db.turn (0.1);
    check (c.kind == ChangeKind::Value && std::fabs (c.value - 0.6) < 1.0e-6, "DetailButton turn +0.1 gives 0.6");

    // Turn clamps at 1.0
    db.state.value = 0.95;
    c = db.turn (0.2);
    check (c.kind == ChangeKind::Value && std::fabs (c.value - 1.0) < 1.0e-6, "DetailButton turn clamps at 1.0");

    // Turn clamps at 0.0
    db.state.value = 0.05;
    c = db.turn (-0.2);
    check (c.kind == ChangeKind::Value && std::fabs (c.value - 0.0) < 1.0e-6, "DetailButton turn clamps at 0.0");

    // Vertical drag: dy / 200.0 (dy = startY - y = 60 - 40 = 20 -> +0.1)
    db.state.value = 0.5;
    db.touchDown (60, 60, 1.0);
    c = db.touchMove (60, 40);
    check (c.kind == ChangeKind::Value && std::fabs (c.value - 0.6) < 1.0e-6, "DetailButton vertical drag up increases value");
    db.touchUp (60, 40);

    // Tap still produces Request even when hasValue is true!
    db.touchDown (60, 60, 1.0);
    c = db.touchUp (60, 60);
    check (c.kind == ChangeKind::Request && c.id == 205, "DetailButton tap with value still produces Request");

    // 3. With integer value
    db.state.integer = true;
    db.state.min = 1.0;
    db.state.max = 8.0;
    db.state.value = 1.0;
    db.state.pxPerStep = 10;
    check (db.wheelUnit() == 1.0, "DetailButton integer wheelUnit is 1.0");

    c = db.turn (2.0);
    check (c.kind == ChangeKind::Index && c.index == 2 && c.value == 3.0, "DetailButton integer turn +2 moves to index 2 (value 3)");

    // MinSize with icon and label
    db.state.icon = DetailButton::Icon::Gear;
    check (db.minSize().w == 116 && db.minSize().h == 56, "DetailButton minSize with icon and label is 116x56");

    Image im = drawComponent (db, 300, 200);
    check (drawnOutside (im, db.rect(), backgroundPixel()) == 0, "DetailButton draws strictly inside rect");
}

static void test_route_button()
{
    RouteButton rb;
    rb.setId (206);
    rb.place ({ 50, 50, 64, 64 });

    check (rb.minSize().w == 64 && rb.minSize().h == 64, "RouteButton minSize is 64x64");

    rb.setTarget ("F1");
    rb.state.segment[0] = true;
    rb.state.segment[1] = false;

    // Tap produces Request
    rb.touchDown (60, 60, 1.0);
    Change c = rb.touchUp (60, 60);
    check (c.kind == ChangeKind::Request && c.id == 206, "RouteButton tap produces Request");

    // Turn does nothing
    c = rb.turn (1.0);
    check (c.kind == ChangeKind::None, "RouteButton turn does nothing");

    Image im = drawComponent (rb, 200, 200);
    check (drawnOutside (im, rb.rect(), backgroundPixel()) == 0, "RouteButton draws strictly inside rect");

    // Compact 48 px variant
    RouteButton compactRb;
    compactRb.compact = true;
    compactRb.setId (207);
    check (compactRb.minSize().w == 48 && compactRb.minSize().h == 48, "Compact RouteButton minSize is 48x48");
    compactRb.place ({ 50, 50, 48, 48 });
    compactRb.setTarget ("F2");
    compactRb.state.segment[0] = false;
    compactRb.state.segment[1] = true;
    compactRb.touchDown (60, 60, 1.0);
    Change compactC = compactRb.touchUp (60, 60);
    check (compactC.kind == ChangeKind::Request && compactC.id == 207, "Compact RouteButton tap produces Request");
    Image compactIm = drawComponent (compactRb, 200, 200);
    check (drawnOutside (compactIm, compactRb.rect(), backgroundPixel()) == 0, "Compact RouteButton draws strictly inside 48x48 rect");
}

int main()
{
    test_toggle_icon_basic_and_turn();
    test_toggle_icon_knob_like_and_show_state();
    test_menu_button_variants();
    test_detail_button_request_vs_value();
    test_route_button();

    std::printf ("ui_cov_toggles_buttons_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
