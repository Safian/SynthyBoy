// SynthyBoy - MPC UI component library tests: the BASE components, interface first. For every component: each gesture gives exactly the documented change (id and value), clamps at the
// ends, nothing happens on a disabled component, a gesture that went down inside is kept when the finger leaves the rectangle, double tap = default, turn (delta). Then the drawing: through
// the CPU renderer at the minimum size and at a large size, nothing outside the rectangle, the value marker moves monotonically, the modulated marker differs, no allocation, the same
// scene twice, legible text; snapshot pictures in $UI_OUT for a human.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <cstdlib>
#include <new>

#include "ui_test.h"

using namespace uit;

// ---- allocation counter: the drawing must not allocate (the replaced operators count between arm () and disarm ())
static std::atomic<long> g_allocs { 0 };
static std::atomic<bool> g_armed { false };
void* operator new (size_t n) { if (g_armed) ++g_allocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (size_t n) { if (g_armed) ++g_allocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, size_t) noexcept { std::free (p); }
void operator delete[] (void* p, size_t) noexcept { std::free (p); }

namespace
{
/** A tap: down and up at the same place; the change of the up (or of the down when only that gave one). */
Change tap (Component& c, int x, int y, double t)
{
    const Change d = c.touchDown (x, y, t);
    const Change u = c.touchUp (x, y);
    return u ? u : d;
}
/** A vertical drag from (x, y0) to (x, y1); the change of the last move that gave one. */
Change drag (Component& c, int x, int y0, int y1, double t = 0.0)
{
    Change last = c.touchDown (x, y0, t);
    const int dir = y1 > y0 ? 1 : -1;
    for (int y = y0 + dir; dir > 0 ? y <= y1 : y >= y1; y += dir * 4)
        if (const Change m = c.touchMove (x, y))
            last = m;
    if (const Change m = c.touchMove (x, y1))
        last = m;
    return last;
}
int cx (const ControlModel& c) { return c.rect().x + c.rect().w / 2; }
bool near (double a, double b, double e = 1.0e-9) { return std::fabs (a - b) <= e; }

void testKnob()
{
    Knob k ("WT POS");
    k.place ({ 50, 40, 120, 100 });
    k.setParamHandle ({ 7, 19, "osc1.wt_pos" });
    k.state.value = 0.2;
    k.state.defaultValue = 0.35;
    k.state.modValue = 0.45;
    k.state.bipolar = true;

    // binding & geometry & visibility/inactive metadata
    check (k.id() == 7 && k.param() == 19 && std::string (k.paramKey()) == "osc1.wt_pos" && k.paramHandle().bound(), "knob: paramHandle stores id/param/key");
    check (k.rect().x == 50 && k.rect().y == 40 && k.rect().w == 120 && k.rect().h == 100, "knob: place sets rect");
    check (k.minSize().w == 100 && k.minSize().h == 72 && k.sizeOk(), "knob: standard minSize is 100x72 and fits 120x100");
    Knob kSmall ("RAND", true);
    check (kSmall.minSize().w == 72 && kSmall.minSize().h == 56, "knob: small minSize is 72x56");
    check (near (k.value(), 0.2) && near (k.minValue(), 0.0) && near (k.maxValue(), 1.0) && near (k.defaultValue(), 0.35), "knob: ControlModel value/min/max/default getters match state");
    setText (k.state.valueText, sizeof k.state.valueText, "20%");
    char txt[32] {};
    k.formatText (txt, sizeof txt);
    check (std::string (txt) == "20%", "knob: formatText returns state.valueText");

    // turn and wheel
    k.state.value = 0.5;
    Change c = k.turn (0.1);
    check (c.kind == ChangeKind::Value && c.id == 7 && near (c.value, 0.6), "knob: turn (+0.1) adds delta");
    c = k.turn (-0.75);
    check (c.kind == ChangeKind::Value && near (c.value, 0.0), "knob: turn clamps at 0");
    k.state.value = 1.0;
    check (! k.turn (0.3), "knob: turn at the top end gives nothing");
    k.state.value = 0.0;
    check (! k.turn (-0.3), "knob: turn at the bottom end gives nothing");
    k.state.value = 0.5;
    check (near (k.wheelUnit(), 1.0 / 128.0), "knob: wheelUnit is 1/128");
    c = k.wheel (2);
    check (c.kind == ChangeKind::Value && c.id == 7 && near (c.value, 0.5 + 2.0 / 128.0), "knob: wheel (+2) advances by 2/128");
    Change overOut;
    check (cx (k) == 110 && k.wheelOver (110, 90, 1, overOut) && overOut.kind == ChangeKind::Value, "knob: wheelOver inside rect succeeds");
    check (! k.wheelOver (5, 5, 1, overOut), "knob: wheelOver outside rect returns false");

    // LED gating, disabled, visible, inactive
    k.state.led = true;
    k.state.ledOn = false;
    check (! k.turn (0.1) && ! k.wheel (1), "knob: led && !ledOn blocks turn and wheel");
    k.state.ledOn = true;
    check (k.turn (0.1).kind == ChangeKind::Value, "knob: led && ledOn permits turn");
    k.state.led = false;

    k.setVisible (false);
    check (! k.visible(), "knob: setVisible updates visible flag");
    k.setVisible (true);
    k.setInactive (true);
    check (k.inactive(), "knob: setInactive updates inactive flag");
    k.setInactive (false);

    k.state.value = 0.5;
    k.setEnabled (false);
    check (! k.turn (0.2) && ! k.wheel (1) && ! k.wheelOver (110, 90, 1, overOut), "knob: a disabled knob gives nothing (turn, wheel, wheelOver)");
    k.setEnabled (true);
}

void testNumberField()
{
    NumberField f ("OCT", NumberField::Format::IntSigned);
    f.place ({ 10, 10, 100, 60 });
    f.setParamHandle ({ 11, 23, "osc1.oct" });
    f.state.min = -4; f.state.max = 4; f.state.value = 0; f.state.defaultValue = 1;
    check (f.id() == 11 && f.param() == 23 && std::string (f.paramKey()) == "osc1.oct" && f.paramHandle().bound(), "number field: paramHandle stores id/param/key");
    check (f.minSize().w == 80 && f.minSize().h == 56 && f.sizeOk(), "number field: minSize is 80x56");
    check (near (f.value(), 0.0) && near (f.minValue(), -4.0) && near (f.maxValue(), 4.0) && near (f.stepValue(), 1.0) && near (f.defaultValue(), 1.0), "number field: ControlModel value/min/max/step/default match state");
    char t[16];
    NumberField::State s = f.state;
    s.value = 2; NumberField::formatValue (s, t); check (std::string (t) == "+2", "number field: integer with sign: +2");
    s.value = -3; NumberField::formatValue (s, t); check (std::string (t) == "-3", "number field: integer with sign: -3");
    s.value = 0; NumberField::formatValue (s, t); check (std::string (t) == "0", "number field: integer with sign: 0 without a sign");
    s.format = NumberField::Format::Decimal2; s.value = -64; NumberField::formatValue (s, t); check (std::string (t) == "-64.00", "number field: two decimals: -64.00");
    s.value = 0.005; NumberField::formatValue (s, t); check (std::string (t) == "0.01" || std::string (t) == "0.00", "number field: two decimals: rounded");
    s.format = NumberField::Format::Degrees; s.value = 180; NumberField::formatValue (s, t); check (std::string (t) == "180\xc2\xb0", "number field: degrees: 180 and the degree sign (UTF-8, in the font)");
    s.format = NumberField::Format::IntPlain; s.value = 17; NumberField::formatValue (s, t); check (std::string (t) == "17", "number field: plain integer");
    s.notSet = true; NumberField::formatValue (s, t); check (std::string (t) == "-", "number field: not set: a dash");
    f.formatText (t, sizeof t);
    check (std::string (t) == "0", "number field: formatText formats current state");
    check (f.numSteps() == 8 && f.indexOf (-4) == 0 && f.indexOf (4) == 8 && f.indexOf (100) == 8, "number field: steps and index of a value (clamped)");

    // turn & wheel in steps
    Change c = f.turn (2);
    check (c.kind == ChangeKind::Index && c.id == 11 && c.index == 6 && near (c.value, 2.0), "number field: turn (+2) = two steps");
    c = f.turn (-9);
    check (c.kind == ChangeKind::Index && c.index == 0 && near (c.value, -4.0), "number field: turn clamps at the minimum");
    c = f.turn (9);
    check (c.kind == ChangeKind::Index && c.index == 8 && near (c.value, 4.0), "number field: turn clamps at the maximum");
    f.state.value = 4;
    check (! f.turn (1) && ! f.wheel (1), "number field: at maximum turn/wheel give nothing");
    f.state.value = -4;
    check (! f.turn (-1) && ! f.wheel (-1), "number field: at minimum turn/wheel give nothing");
    f.state.value = 0;
    check (! f.turn (0.2), "number field: turn of less than half a step gives nothing");
    c = f.wheel (1);
    check (c.kind == ChangeKind::Index && c.id == 11 && c.index == 5 && near (c.value, 1.0), "number field: wheel (+1) advances by 1 step");

    // decimal steps: the value is a whole number of steps
    NumberField d ("CRS", NumberField::Format::Decimal2);
    d.place ({ 0, 0, 100, 60 });
    d.setId (3);
    d.state.min = -48; d.state.max = 48; d.state.step = 0.01; d.state.value = 0.0; d.state.pxPerStep = 2;
    c = d.turn (1);
    check (c.kind == ChangeKind::Index && c.index == 4801 && near (c.value, 0.01, 1e-9), "number field: CRS: turn (+1) = 0.01");

    // not set: the first move gives a value
    f.state.notSet = true;
    c = f.turn (1);
    check (c.kind == ChangeKind::Index, "number field: a value that is not set takes the first turn");
    f.state.notSet = false;

    // read only & disabled
    f.state.readOnly = true;
    f.state.value = 3;
    check (! f.turn (1) && ! f.wheel (1), "number field: read only: no reaction");
    f.state.readOnly = false;
    f.setEnabled (false);
    check (! f.turn (1) && ! f.wheel (1), "number field: disabled: no reaction");
}

void testStepper()
{
    Stepper s;
    s.place ({ 0, 0, 200, 56 });
    s.setId (21);
    s.state.count = 5; s.state.index = 2; s.setItem ("SAW");
    Change c = tap (s, 20, 28, 0.0);
    check (c.kind == ChangeKind::Index && c.id == 21 && c.index == 1 && near (c.value, 1), "stepper: a tap on the left half = previous: { id 21, index 1 }");
    c = tap (s, 180, 28, 100.0);
    check (c.kind == ChangeKind::Index && c.index == 3, "stepper: a tap on the right half = next");
    s.state.index = 0;
    check (! tap (s, 20, 28, 1000.0), "stepper: previous at the first item (no wrap) gives nothing");
    s.state.index = 4;
    check (! tap (s, 180, 28, 2000.0), "stepper: next at the last item (no wrap) gives nothing");
    s.state.wrap = true;
    s.state.index = 0;
    c = tap (s, 20, 28, 3000.0);
    check (c.kind == ChangeKind::Index && c.index == 4, "stepper: with wrap previous at the first item goes to the last");
    s.state.index = 4;
    c = tap (s, 180, 28, 4000.0);
    check (c.kind == ChangeKind::Index && c.index == 0, "stepper: with wrap next at the last item goes to the first");
    s.state.wrap = false;
    s.state.index = 2;
    c = s.turn (1);
    check (c.kind == ChangeKind::Index && c.index == 3, "stepper: turn (+1) = next");
    c = s.turn (-2);
    check (c.kind == ChangeKind::Index && c.index == 0, "stepper: turn (-2) = two back");
    c = s.turn (7);
    check (c.kind == ChangeKind::Index && c.index == 4, "stepper: turn clamps at the last item");
    s.state.wrap = true;
    c = s.turn (6);
    check (c.kind == ChangeKind::Index && c.index == 3, "stepper: with wrap turn (+6) from 2 goes round to 3");
    check (! s.turn (0.3), "stepper: turn of less than half a step gives nothing");
    s.state.wrap = false;
    // the half a touch went down in decides, also when the finger leaves
    s.state.index = 2;
    s.touchDown (20, 28, 5000.0);
    c = s.touchUp (190, 400);
    check (c.kind == ChangeKind::Index && c.index == 1, "stepper: the gesture is kept outside the rectangle: down on the left half, up far away = previous");
    s.touchDown (20, 28, 5100.0);
    s.touchMove (20, 200);
    check (! s.touchUp (20, 200), "stepper: a drag is not a tap");
    s.state.count = 1;
    check (! tap (s, 20, 28, 6000.0) && ! s.turn (1), "stepper: a list of one item gives nothing");
    s.state.count = 5;
    s.setEnabled (false);
    check (! tap (s, 20, 28, 7000.0) && ! s.turn (1), "stepper: disabled: no reaction");
    Rect z[4];
    check (s.touchTargets (z, 4) == 2 && z[0].w == 100 && z[1].x == 100, "stepper: two touch targets, the halves");
}

void testUpDown()
{
    UpDownStepper u;
    u.place ({ 0, 0, 72, 112 });
    u.setId (31);
    u.state.value = 3; u.state.min = 1; u.state.max = 5;
    Change c = tap (u, 30, 10, 0.0);
    check (c.kind == ChangeKind::Index && c.id == 31 && c.index == 4 && near (c.value, 4), "up/down: a tap on the upper half = +1");
    c = tap (u, 30, 100, 100.0);
    check (c.kind == ChangeKind::Index && c.index == 2, "up/down: a tap on the lower half = -1");
    u.state.value = 5;
    check (! tap (u, 30, 10, 1000.0), "up/down: at the maximum up gives nothing");
    u.state.value = 1;
    check (! tap (u, 30, 100, 2000.0), "up/down: at the minimum down gives nothing");
    u.state.value = 3;
    c = u.turn (1);
    check (c.kind == ChangeKind::Index && c.index == 4, "up/down: turn (+1)");
    c = u.turn (-10);
    check (c.kind == ChangeKind::Index && c.index == 1, "up/down: turn clamps at the minimum");
    u.touchDown (30, 10, 3000.0);
    c = u.touchUp (500, 500);
    check (c.kind == ChangeKind::Index && c.index == 4, "up/down: the gesture is kept outside the rectangle");
    u.setEnabled (false);
    check (! tap (u, 30, 10, 4000.0) && ! u.turn (1), "up/down: disabled: no reaction");
    Rect z[4];
    check (u.touchTargets (z, 4) == 2 && z[0].h == 56 && z[1].h == 56, "up/down: two targets of 56 px");
}

void testToggle()
{
    ToggleIcon t;
    t.place ({ 0, 0, 56, 56 });
    t.setId (41);
    Change c = tap (t, 28, 28, 0.0);
    check (c.kind == ChangeKind::Index && c.id == 41 && c.index == 1, "toggle: a tap switches on: { id 41, 1 }");
    t.state.on = true;
    c = tap (t, 28, 28, 1000.0);
    check (c.kind == ChangeKind::Index && c.index == 0, "toggle: a tap switches off");
    check (! t.turn (1), "toggle: turn (+1) when on gives nothing");
    c = t.turn (-1);
    check (c.kind == ChangeKind::Index && c.index == 0, "toggle: turn (-1) switches off");
    t.state.on = false;
    c = t.turn (1);
    check (c.kind == ChangeKind::Index && c.index == 1, "toggle: turn (+1) switches on");
    check (! t.turn (0.0) && ! t.turn (-1), "toggle: turn (0) and a turn to the state it is in give nothing");
    t.touchDown (28, 28, 3000.0);
    t.touchMove (28, 90);
    check (! t.touchUp (28, 90), "toggle: a drag is not a tap");
    t.touchDown (28, 28, 4000.0);
    c = t.touchUp (700, 700);
    check (c.kind == ChangeKind::Index, "toggle: the gesture is kept outside the rectangle");
    t.setEnabled (false);
    check (! tap (t, 28, 28, 5000.0) && ! t.turn (1), "toggle: disabled: no reaction");
}

void testButtons()
{
    MenuButton m;
    m.place ({ 0, 0, 56, 56 });
    m.setId (51);
    Change c = tap (m, 28, 28, 0.0);
    check (c.kind == ChangeKind::Request && c.id == 51, "menu button: a tap = { menu requested, id 51 }");
    check (! m.turn (1), "menu button: turn gives nothing");
    m.touchDown (28, 28, 100.0);
    m.touchMove (28, 80);
    check (! m.touchUp (28, 80), "menu button: a drag is not a tap");
    m.touchDown (28, 28, 200.0);
    check (m.touchUp (900, 900).kind == ChangeKind::Request, "menu button: the gesture is kept outside the rectangle");
    m.setEnabled (false);
    check (! tap (m, 28, 28, 300.0), "menu button: disabled: no reaction");

    RouteButton r ("F1");
    r.place ({ 0, 0, 64, 64 });
    r.setId (61);
    c = tap (r, 30, 40, 0.0);
    check (c.kind == ChangeKind::Request && c.id == 61, "route button: a tap = { route panel requested, id 61 }");
    check (! r.turn (1), "route button: turn gives nothing");
    r.state.segment[0] = true;
    check (tap (r, 30, 40, 100.0).kind == ChangeKind::Request, "route button: the indicator segments change nothing about the gesture");
    r.setEnabled (false);
    check (! tap (r, 30, 40, 200.0), "route button: disabled: no reaction");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
void checkDrawing (Component& c, const std::string& name, const Rect& r, const char* file)
{
    c.place (r);
    const int W = r.x + r.w + 24, H = r.y + r.h + 24;
    Scene s (W, H, kBackgroundRgba);
    g_allocs = 0;
    g_armed = true;
    c.draw (s.builder());
    g_armed = false;
    check (g_allocs == 0, name + ": drawing allocates nothing");
    check (! s.overflow(), name + ": the scene fits (" + std::to_string (s.numOps()) + " ops)");
    const Image im = render (s);
    const int out = drawnOutside (im, r, backgroundPixel());
    check (out == 0, name + ": nothing drawn outside the rectangle (pixels outside: " + std::to_string (out) + ")");
    check (drawnInside (im, r, backgroundPixel()) > 20, name + ": something is drawn");
    int smallText = 0;
    for (uint32_t i = 0; i < s.data->numOps; ++i)
        if (s.data->ops[i].op == gpu::kOpText && s.data->ops[i].f[2] < 2.0f)
            ++smallText;
    check (smallText == 0, name + ": every text is at least 2 pixels per font pixel (legible)");
    Scene s2 (W, H, kBackgroundRgba);
    c.draw (s2.builder());
    check (s.data->numOps == s2.data->numOps && std::memcmp (s.data->ops, s2.data->ops, sizeof (gpu::SceneOp) * s.data->numOps) == 0, name + ": the same state gives the same scene (deterministic)");
    if (file)
        writePng (outDir() + "/" + file, im, 2);
    Rect z[8];
    const int n = c.touchTargets (z, 8);
    bool targetsOk = n > 0;
    for (int i = 0; i < n; ++i)
        targetsOk = targetsOk && r.contains (z[i]) && std::min (z[i].w, z[i].h) >= std::min (56, std::min (r.w, r.h));
    check (targetsOk, name + ": every touch target is inside and at least 56 px in its smaller direction (or as big as the rectangle allows)");
    c.setEnabled (false);
    Scene sd (W, H, kBackgroundRgba);
    c.draw (sd.builder());
    const Image dim = render (sd);
    check (drawnOutside (dim, r, backgroundPixel()) == 0, name + ": disabled: still nothing outside the rectangle");
    bool dimmer = false;
    for (int y = r.y; y < r.bottom() && ! dimmer; ++y)
        for (int x = r.x; x < r.right(); ++x)
            if (dim.at (x, y) != im.at (x, y)) { dimmer = true; break; }
    check (dimmer, name + ": disabled: drawn differently (dim)");
    c.setEnabled (true);
}

void testOtherDrawing()
{
    Stepper s;
    s.state.count = 5; s.state.index = 1; s.setItem ("WIDEST TEXT.");
    checkDrawing (s, "stepper (12 characters)", { 10, 10, s.minSize().w, s.minSize().h }, "stepper_min.png");
    checkDrawing (s, "stepper (large)", { 10, 10, 420, 90 }, "stepper_large.png");
    s.state.dim = true; s.setItem ("OFF");
    checkDrawing (s, "stepper dim", { 10, 10, 200, 56 }, nullptr);

    UpDownStepper u;
    u.state.min = 1; u.state.max = 16; u.state.value = 7;
    checkDrawing (u, "up/down stepper", { 10, 10, u.minSize().w, u.minSize().h }, "updown_min.png");
    checkDrawing (u, "up/down stepper (large)", { 10, 10, 160, 200 }, nullptr);

    ToggleIcon t;
    for (int i = 0; i < 3; ++i)
        for (int on = 0; on < 2; ++on)
        {
            t.state.icon = static_cast<ToggleIcon::Icon> (i);
            t.state.on = on != 0;
            checkDrawing (t, "toggle icon " + std::to_string (i) + (on ? " on" : " off"), { 10, 10, 56, 56 }, i == 1 && on ? "toggle_pitch_on.png" : nullptr);
        }
    checkDrawing (t, "toggle icon (large)", { 10, 10, 150, 90 }, nullptr);

    MenuButton m;
    checkDrawing (m, "menu button", { 10, 10, 56, 56 }, "menu_min.png");
    MenuButton g ("", MenuButton::Icon::Gear);
    checkDrawing (g, "menu button gear", { 10, 10, 56, 56 }, "gear_min.png");
    MenuButton w ("WARP");
    checkDrawing (w, "menu button with a label", { 10, 10, 120, 80 }, "menu_label.png");
    RouteButton r ("F1");
    r.state.segment[1] = true;
    checkDrawing (r, "route button", { 10, 10, 64, 64 }, "route_min.png");
    checkDrawing (r, "route button (large)", { 10, 10, 140, 100 }, nullptr);
}

void testDetailButton()
{
    DetailButton d (DetailButton::Icon::Bell, "AMOUNT");
    d.place ({ 0, 0, 120, 56 });
    d.setId (5);
    d.state.hasValue = true; d.state.value = 0.4; d.setValueText ("40%");
    // a tap = details requested, a drag = the main value
    Change c = tap (d, 60, 28, 0.0);
    check (c.kind == ChangeKind::Request && c.id == 5, "detail button: a tap = { details requested, id 5 }");
    c = drag (d, 60, 40, 20);
    check (c.kind == ChangeKind::Value && c.id == 5 && near (c.value, 0.5), "detail button: a drag up by 20 px = +0.1 of the main value (200 px for the whole range)");
    check (! d.touchUp (60, 20), "detail button: the touch up of a drag does not ask for the details");
    d.touchDown (60, 28, 100.0);
    d.touchMove (60, 25);
    check (d.touchUp (60, 25).kind == ChangeKind::Request, "detail button: a movement inside the tap square (3 px) is still a tap");
    d.touchDown (60, 28, 200.0);
    c = d.touchMove (-400, -400);
    check (c.kind == ChangeKind::Value, "detail button: the drag is kept outside the rectangle");
    d.touchUp (-400, -400);
    c = d.turn (0.1);
    check (c.kind == ChangeKind::Value && near (c.value, 0.5), "detail button: turn adds delta to the main value");
    d.state.value = 1.0;
    check (! d.turn (0.1) && ! drag (d, 60, 40, 0), "detail button: clamps at the end");
    d.touchUp (60, 0);
    // integer main value
    d.state.integer = true; d.state.min = 1; d.state.max = 16; d.state.value = 7;
    c = d.turn (3);
    check (c.kind == ChangeKind::Index && c.index == 9 && near (c.value, 10), "detail button: integer: turn (+3) = three steps: index 9 (= value 10)");
    c = drag (d, 60, 50, 26);
    check (c.kind == ChangeKind::Index && c.index == 6 + 3 && near (c.value, 10), "detail button: integer: 24 px up at 8 px per step = +3");
    d.touchUp (60, 26);
    d.state.value = 16;
    check (! d.turn (1), "detail button: integer: nothing past the maximum");
    // no main value: only the request
    DetailButton g (DetailButton::Icon::Gear);
    g.place ({ 0, 0, 56, 56 });
    check (tap (g, 28, 28, 1000.0).kind == ChangeKind::Request && ! drag (g, 28, 40, 0) && ! g.turn (1), "detail button without a value: only the tap");
    g.touchUp (28, 0);
    d.setEnabled (false);
    check (! tap (d, 60, 28, 3000.0) && ! d.turn (1), "detail button: disabled: no reaction");
    for (int i = 0; i < 3; ++i)
    {
        DetailButton x (static_cast<DetailButton::Icon> (i + 1), i == 2 ? "ROUTE" : "");
        x.state.hasValue = true; x.setValueText ("123456");
        checkDrawing (x, "detail button icon " + std::to_string (i + 1), { 10, 10, x.minSize().w, x.minSize().h }, i == 1 ? "detail_bell.png" : nullptr);
        checkDrawing (x, "detail button icon " + std::to_string (i + 1) + " (large)", { 10, 10, 220, 90 }, nullptr);
    }
}

void testPictogram()
{
    PictogramSelector s;
    s.setItem (0, pictogram::Sine, "SUB");
    s.setItem (1, pictogram::Wavetable, "WAVE");
    s.setItem (2, pictogram::Granular, "GRAIN");
    s.setItem (3, pictogram::Multisample, "SAMPLE", false);
    s.setItem (4, pictogram::Noise, "NOISE");
    s.state.index = 1;
    s.setId (9);
    const Size m = s.minSize();
    check (m.w == 5 * s.cellMinW() && s.cellMinW() >= 56 && m.h == 56, "pictogram selector: minimum size of five items in a row: five cells of the width of the longest label");
    s.place ({ 10, 10, m.w, m.h });
    check (s.sizeOk() && ! s.vertical() && s.cell (0).w == s.cellMinW() && s.cell (4).x == 10 + 4 * s.cellMinW(), "pictogram selector: a wide rectangle: a row of cells");
    Change c = tap (s, s.cell (2).x + 10, 30, 0.0);
    check (c.kind == ChangeKind::Index && c.id == 9 && c.index == 2 && near (c.value, 2), "pictogram selector: a tap on a cell = { id 9, its index }");
    check (! tap (s, s.cell (3).x + 10, 30, 100.0), "pictogram selector: a tap on a disabled cell is ignored");
    check (! tap (s, s.cell (1).x + 10, 30, 200.0), "pictogram selector: a tap on the current cell changes nothing");
    s.touchDown (s.cell (4).x + 10, 30, 300.0);
    c = s.touchUp (-300, -300);
    check (c.kind == ChangeKind::Index && c.index == 4, "pictogram selector: the cell the touch went down in decides, also when the finger leaves");
    s.touchDown (s.cell (4).x + 10, 30, 400.0);
    s.touchMove (s.cell (4).x + 10, 200);
    check (! s.touchUp (s.cell (4).x + 10, 200), "pictogram selector: a drag is not a tap");
    c = s.turn (1);
    check (c.kind == ChangeKind::Index && c.index == 2, "pictogram selector: turn (+1) = the next enabled item");
    s.state.index = 2;
    c = s.turn (1);
    check (c.kind == ChangeKind::Index && c.index == 4, "pictogram selector: turn skips the disabled item");
    c = s.turn (5);
    check (! (s.state.index = 4, s.turn (3)) && ! s.turn (0.2), "pictogram selector: no wrap at the end, less than half a step: nothing");
    c = s.turn (-9);
    check (c.kind == ChangeKind::Index && c.index == 0, "pictogram selector: turn (-9) stops at the first enabled item");
    s.state.index = 1;
    c = s.turn (2);
    check (c.kind == ChangeKind::Index && c.index == 4, "pictogram selector: turn (+2) from 1 over the disabled one: index 4 (2, then 4)");
    s.setEnabled (false);
    check (! tap (s, s.cell (0).x + 10, 30, 900.0) && ! s.turn (1), "pictogram selector: disabled: no reaction");
    s.setEnabled (true);
    Rect z[8];
    check (s.touchTargets (z, 8) == 4, "pictogram selector: four touch targets (the enabled cells)");
    for (int i = 0; i < pictogram::Count; ++i)
        check (pictogram::find (i) != nullptr && std::strlen (pictogram::find (i)->name) > 0, "pictogram table: row " + std::to_string (i) + " exists");
    checkDrawing (s, "pictogram selector", { 10, 10, m.w, m.h }, "pictogram_row.png");
    checkDrawing (s, "pictogram selector (large)", { 10, 10, 700, 100 }, nullptr);
    PictogramSelector col;
    for (int i = 0; i < 5; ++i)
        col.setItem (i, i, "ITEM");
    col.state.index = 3;
    col.place ({ 10, 10, 90, 5 * 56 });
    check (col.vertical() && col.sizeOk() && col.minSize().w == 56 && col.minSize().h == 280 && col.cell (2).y == 10 + 2 * 56, "pictogram selector: a tall rectangle: a column of cells");
    c = tap (col, 50, col.cell (1).y + 20, 0.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "pictogram selector: column: a tap on the second cell");
    checkDrawing (col, "pictogram selector (column)", { 10, 10, 90, 5 * 56 }, "pictogram_column.png");
    // the pictograms differ from each other
    std::vector<uint32_t> first;
    bool allDifferent = true;
    for (int i = 0; i < pictogram::Count; ++i)
    {
        PictogramSelector one;
        one.setItem (0, i, "");
        one.place ({ 0, 0, 72, 56 });
        Scene sc (72, 56, kBackgroundRgba);
        one.draw (sc.builder());
        const Image im = render (sc);
        for (size_t k = 0; k < first.size() / (72 * 56); ++k)
            allDifferent = allDifferent && ! std::equal (im.px.begin(), im.px.end(), first.begin() + static_cast<long> (k * 72 * 56));
        first.insert (first.end(), im.px.begin(), im.px.end());
    }
    check (allDifferent, "pictograms: the ten drawings (five oscillator types, six sub shapes minus the shared sine) differ from each other");
}

void testModulation()
{
    // the arc ends, both polarities, both signs, clamped
    ModInput m;
    m.add (7, "LFO 2", 0.3);
    m.add (8, "ENV 1", -0.3, false);
    m.add (9, "MACRO1", 0.3, true);
    m.add (10, "MOD", -0.8, true);
    m.add (11, "OFF", 0.4, false, true);
    double a = -1, b = -1;
    m.selectedSource = 7;
    check (depthArc (m, 0.5, a, b) && near (a, 0.5) && near (b, 0.8), "modulation: unidirectional, positive: from the value to value + depth: 0.5 .. 0.8");
    check (depthArc (m, 0.9, a, b) && near (a, 0.9) && near (b, 1.0), "modulation: unidirectional, positive, clamped at the top: 0.9 .. 1.0");
    m.selectedSource = 8;
    check (depthArc (m, 0.5, a, b) && near (a, 0.2) && near (b, 0.5), "modulation: unidirectional, negative: 0.2 .. 0.5");
    check (depthArc (m, 0.1, a, b) && near (a, 0.0) && near (b, 0.1), "modulation: unidirectional, negative, clamped at the bottom: 0 .. 0.1");
    m.selectedSource = 9;
    check (depthArc (m, 0.5, a, b) && near (a, 0.2) && near (b, 0.8), "modulation: bidirectional: value -+ |depth|: 0.2 .. 0.8");
    check (depthArc (m, 0.1, a, b) && near (a, 0.0) && near (b, 0.4), "modulation: bidirectional, clamped at the bottom: 0 .. 0.4");
    m.selectedSource = 10;
    check (depthArc (m, 0.5, a, b) && near (a, 0.0) && near (b, 1.0), "modulation: bidirectional with a negative depth: the same |depth|, clamped both ends: 0 .. 1");
    m.selectedSource = 11;
    check (! depthArc (m, 0.5, a, b), "modulation: a bypassed source has no arc");
    m.selectedSource = 99;
    check (! depthArc (m, 0.5, a, b), "modulation: a selected source that is not in the entries has no arc");
    m.selectedSource = kNoSource;
    check (! depthArc (m, 0.5, a, b), "modulation: no selected source: no arc");
    // the tab, the four states
    ModInput none;
    check (tabState (none) == TabState::Absent, "tab: no entries: absent");
    check (tabState (m) == TabState::Grey, "tab: entries but no selected source: grey");
    m.selectedSource = 99;
    check (tabState (m) == TabState::Grey, "tab: entries, the selected source is not among them: grey");
    m.selectedSource = 7;
    check (tabState (m) == TabState::Coloured, "tab: the selected source is in the entries: coloured");
    m.selectedSource = 11;
    check (tabState (m) == TabState::Hollow, "tab: the selected source is bypassed: hollow");
    // ModEntry colour and KnobModel::tab helper
    Knob kt ("DETUNE");
    kt.state.value = 0.5;
    static_cast<ModInput&> (kt.state) = m;
    kt.state.selectedSource = 7;
    check (kt.tab() == TabState::Coloured && kt.state.mods[0].colour() == theme::sourceColour ("LFO 2"), "knob model: tab is Coloured and ModEntry::colour maps LFO 2");
    kt.state.selectedSource = 11;
    check (kt.tab() == TabState::Hollow && kt.state.mods[4].colour() == theme::sourceColour ("OFF"), "knob model: tab is Hollow and ModEntry::colour maps OFF");
    kt.state.selectedSource = kNoSource;
    check (kt.tab() == TabState::Grey, "knob model: tab is Grey when entries exist without selectedSource");
    // modulated() check
    kt.state.modValue = 0.7;
    check (kt.modulated(), "knob model: modulated() is true when modValue != value");
    kt.state.modValue = kt.state.value;
    check (! kt.modulated(), "knob model: modulated() is false when modValue == value");
}
} // namespace

void testHBarAndSideKnob()
{
    HBar b;
    b.place ({ 0, 0, 600, 56 });
    b.setId (5);
    b.state.value = 0.5; b.state.defaultValue = 0.5;
    b.touchDown (200, 28, 0.0);
    Change c;
    for (int x = 204; x <= 300; x += 4)
        if (const Change m = b.touchMove (x, 28)) c = m;
    check (c.kind == ChangeKind::Value && c.id == 5 && c.value > 0.5 && c.value <= 1.0, "bar: a drag to the right raises the value (both ways from the middle)");
    b.touchUp (300, 28);
    b.state.value = 0.8;
    b.touchDown (100, 28, 100.0); b.touchUp (100, 28);
    b.touchDown (101, 28, 200.0);
    c = b.touchUp (101, 28);
    check (c.kind == ChangeKind::Value && std::fabs (c.value - 0.5) < 1e-9, "bar: a double tap is the default (zero)");
    c = b.turn (0.1);
    check (c.kind == ChangeKind::Value && std::fabs (c.value - 0.9) < 1e-9, "bar: turn adds");
    Knob k ("DETUNE");
    k.state.side = true;
    k.state.valueText[0] = '4'; k.state.valueText[1] = '0'; k.state.valueText[2] = '\0';
    k.state.add (3, "MAC 4", 0.2);
    k.state.selectedSource = 3;
    k.place ({ 0, 0, 190, 90 });
    check (k.minSize().w == 172 && k.minSize().h == 72 && k.sizeOk(), "side knob model: minSize 172x72 and fits 190x90");
    Stepper st;
    st.place ({ 0, 0, 300, 64 });
    st.setCaption ("MODE"); st.setItem ("Linear");
    Scene sc (320, 80, kBackgroundRgba);
    st.draw (sc.builder());
    check (! sc.overflow() && drawnOutside (render (sc), st.rect(), backgroundPixel()) == 0, "stepper with a caption draws inside its rectangle");
}

int main()
{
    {
        // the pictures of the tests never go into the working directory (a test run in the source tree committed 79 of them): the default is the temporary directory, an absolute path
        const char* keep = std::getenv ("UI_OUT");
        const std::string saved = keep ? keep : "";
        unsetenv ("UI_OUT");
        const std::string d = outDir();
        check (! d.empty() && d != "." && std::filesystem::path (d).is_absolute() && std::filesystem::is_directory (d), "outDir () without UI_OUT is an absolute directory of its own, not the working directory");
        if (keep)
            setenv ("UI_OUT", saved.c_str(), 1);
    }
    testHBarAndSideKnob();
    testKnob();
    testNumberField();
    testStepper();
    testUpDown();
    testToggle();
    testButtons();
    testDetailButton();
    testPictogram();
    testModulation();
    testOtherDrawing();
    // the special signs of the screens are in the font (none of them draws as the question mark)
    {
        const uint32_t signs[] = { 0xb0, 0xb1, 0xb7, 0x2013, 0x2014, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x25b2, 0x25b6, 0x25bc, 0x25c0, 0xc1, 0xc9, 0xcd, 0xd3, 0xd6, 0x150, 0xda, 0xdc, 0x170, 0xe1, 0xe9, 0xed, 0xf3, 0xf6, 0x151, 0xfa, 0xfc, 0x171 };
        bool all = true;
        for (uint32_t cp : signs)
            all = all && sdf::find (cp) != nullptr;
        check (all, "font: the degree sign, the arrows, the dashes, the ellipsis and the Hungarian letters have glyphs");
        char t[16];
        NumberField::State s;
        s.format = NumberField::Format::Degrees; s.value = 0;
        NumberField::formatValue (s, t);
        check (std::string (t) == "0\xc2\xb0", "font: the phase field says 0 and the degree sign");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
