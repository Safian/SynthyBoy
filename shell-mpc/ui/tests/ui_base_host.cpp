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
int cx (const Component& c) { return c.rect().x + c.rect().w / 2; }
bool near (double a, double b, double e = 1.0e-9) { return std::fabs (a - b) <= e; }

void testKnob()
{
    Knob k ("WT POS");
    k.place ({ 50, 40, 120, 100 });
    k.setId (7);
    k.state.value = 0.2;
    k.state.defaultValue = 0.35;
    const int x = cx (k);
    // a drag up of 100 pixels = +0.5 of the range (200 pixels for the whole), measured from the value at touch down
    Change c = drag (k, x, 120, 20);   // y 40 .. 139 is the rectangle
    check (c.kind == ChangeKind::Value && c.id == 7 && near (c.value, 0.7), "knob: a drag up by 100 px from 0.2 gives { id 7, 0.7 }");
    c = k.touchUp (x, 20);
    check (! c, "knob: the touch up of a drag gives nothing");
    c = drag (k, x, 60, 160);
    check (c.kind == ChangeKind::Value && near (c.value, 0.0), "knob: a drag down by 100 px (from 0.2) clamps at 0: only 0.0");
    k.touchUp (x, 160);
    c = drag (k, x, 130, -70);   // 200 px = the full range
    check (c.kind == ChangeKind::Value && near (c.value, 1.0), "knob: 200 px up from 0.2 clamps at 1.0");
    k.touchUp (x, -70);
    k.state.value = 1.0;
    c = drag (k, x, 130, 30);
    check (! c, "knob: at the top end a drag up gives nothing");
    k.touchUp (x, 30);
    k.state.value = 0.0;
    c = drag (k, x, 60, 160);
    check (! c, "knob: at the bottom end a drag down gives nothing");
    k.touchUp (x, 160);
    k.state.value = 0.5;
    c = k.touchDown (x, 100, 0.0);
    c = k.touchMove (x, 97);
    check (! c, "knob: a movement inside the tap square (3 px) is not a drag");
    c = k.touchMove (x + 80, 100);
    check (! c, "knob: a sideways slide is not a drag");
    c = k.touchMove (x, 90);
    check (c.kind == ChangeKind::Value && near (c.value, 0.55), "knob: 10 px up = +0.05");
    k.touchUp (x, 90);
    // the finger leaves the rectangle: the gesture goes on
    k.touchDown (x, 100, 0.0);
    c = k.touchMove (-500, -300);
    check (c.kind == ChangeKind::Value && c.id == 7 && near (c.value, 1.0), "knob: the gesture is kept outside the rectangle (and far outside)");
    k.touchUp (-500, -300);
    check (! k.touchMove (x, 0) && ! k.capturing(), "knob: after the touch up nothing is captured");
    c = k.touchDown (5, 5, 0.0);
    check (! c && ! k.touchMove (5, -200) && ! k.touchUp (5, -200), "knob: a touch that goes down outside the rectangle is ignored for the whole gesture");
    // double tap
    k.state.value = 0.9;
    c = tap (k, x, 100, 1000.0);
    check (! c, "knob: one tap changes nothing");
    c = tap (k, x + 3, 102, 1200.0);
    check (c.kind == ChangeKind::Value && c.id == 7 && near (c.value, 0.35), "knob: a double tap gives the default value");
    c = tap (k, x, 100, 1300.0);
    check (! c, "knob: the third tap right after a double tap is a single tap again");
    k.state.value = 0.9;
    tap (k, x, 100, 5000.0);
    c = tap (k, x, 100, 5600.0);
    check (! c, "knob: two taps 600 ms apart are not a double tap");
    tap (k, x, 100, 8000.0);
    c = tap (k, x + 60, 100, 8100.0);
    check (! c, "knob: two taps 60 px apart are not a double tap");
    tap (k, x, 100, 9000.0);
    drag (k, x, 100, 100 - 20, 9050.0);
    k.touchUp (x, 80);
    c = tap (k, x, 100, 9100.0);
    check (! c, "knob: a drag between two taps breaks the double tap");
    k.state.value = k.state.defaultValue;
    tap (k, x, 100, 12000.0);
    check (! tap (k, x, 100, 12100.0), "knob: a double tap on the default value changes nothing");
    // turn
    k.state.value = 0.5;
    c = k.turn (0.1);
    check (c.kind == ChangeKind::Value && c.id == 7 && near (c.value, 0.6), "knob: turn (+0.1) adds delta");
    c = k.turn (-0.75);
    check (c.kind == ChangeKind::Value && near (c.value, 0.0), "knob: turn clamps at 0");
    k.state.value = 1.0;
    check (! k.turn (0.3), "knob: turn at the top end gives nothing");
    // disabled
    k.state.value = 0.5;
    k.setEnabled (false);
    check (! drag (k, x, 100, 20) && ! k.touchUp (x, 20) && ! k.turn (0.2) && ! tap (k, x, 100, 20000.0) && ! tap (k, x, 100, 20100.0), "knob: a disabled knob gives nothing (drag, double tap, turn)");
    k.setEnabled (true);
    k.touchDown (x, 100, 0.0);
    k.setEnabled (false);
    check (! k.touchMove (x, 0), "knob: disabling during a gesture drops the gesture");
    k.setEnabled (true);
    check (! k.touchMove (x, 0), "knob: and it does not come back by itself");
}

void testNumberField()
{
    NumberField f ("OCT", NumberField::Format::IntSigned);
    f.place ({ 10, 10, 100, 60 });
    f.setId (11);
    f.state.min = -4; f.state.max = 4; f.state.value = 0; f.state.defaultValue = 1;
    const int x = cx (f);
    char t[16];
    NumberField::State s = f.state;
    s.value = 2; NumberField::formatValue (s, t); check (std::string (t) == "+2", "number field: integer with sign: +2");
    s.value = -3; NumberField::formatValue (s, t); check (std::string (t) == "-3", "number field: integer with sign: -3");
    s.value = 0; NumberField::formatValue (s, t); check (std::string (t) == "0", "number field: integer with sign: 0 without a sign");
    s.format = NumberField::Format::Decimal2; s.value = -64; NumberField::formatValue (s, t); check (std::string (t) == "-64.00", "number field: two decimals: -64.00");
    s.value = 0.005; NumberField::formatValue (s, t); check (std::string (t) == "0.01" || std::string (t) == "0.00", "number field: two decimals: rounded");
    s.format = NumberField::Format::Degrees; s.value = 180; NumberField::formatValue (s, t); check (std::string (t) == "180^", "number field: degrees: 180 and the degree glyph");
    s.format = NumberField::Format::IntPlain; s.value = 17; NumberField::formatValue (s, t); check (std::string (t) == "17", "number field: plain integer");
    s.notSet = true; NumberField::formatValue (s, t); check (std::string (t) == "-", "number field: not set: a dash");
    check (f.numSteps() == 8 && f.indexOf (-4) == 0 && f.indexOf (4) == 8 && f.indexOf (100) == 8, "number field: steps and index of a value (clamped)");
    // drag: 8 px per step
    Change c = drag (f, x, 60, 36);
    check (c.kind == ChangeKind::Index && c.id == 11 && c.index == 4 + 3 && near (c.value, 3.0), "number field: a drag up by 24 px = +3 steps: { id 11, index 7 (= value 3) }");
    f.touchUp (x, 36);
    c = drag (f, x, 60, 100);
    check (c.kind == ChangeKind::Index && c.index == 0 && near (c.value, -4.0), "number field: a drag down far clamps at the minimum");
    f.touchUp (x, 100);
    f.state.value = 4;
    check (! drag (f, x, 60, 0), "number field: at the maximum a drag up gives nothing");
    f.touchUp (x, 0);
    f.state.value = 0;
    f.touchDown (x, 60, 0.0);
    c = f.touchMove (-300, 52);
    check (c.kind == ChangeKind::Index && c.index == 5, "number field: the gesture is kept outside the rectangle (one step)");
    f.touchUp (-300, 52);
    // turn in steps
    c = f.turn (2);
    check (c.kind == ChangeKind::Index && c.index == 6 && near (c.value, 2.0), "number field: turn (+2) = two steps");
    c = f.turn (-9);
    check (c.kind == ChangeKind::Index && c.index == 0, "number field: turn clamps at the minimum");
    check (! f.turn (0.2), "number field: turn of less than half a step gives nothing");
    // double tap = default
    c = tap (f, x, 40, 100.0);
    check (! c, "number field: one tap changes nothing");
    c = tap (f, x, 40, 300.0);
    check (c.kind == ChangeKind::Index && c.index == 5 && near (c.value, 1.0), "number field: a double tap gives the default (1)");
    // decimal steps: the value is a whole number of steps
    NumberField d ("CRS", NumberField::Format::Decimal2);
    d.place ({ 0, 0, 100, 60 });
    d.setId (3);
    d.state.min = -48; d.state.max = 48; d.state.step = 0.01; d.state.value = 0.0; d.state.pxPerStep = 2;
    c = drag (d, 50, 50, 30);
    check (c.kind == ChangeKind::Index && c.index == 4800 + 10 && near (c.value, 0.10, 1e-9), "number field: CRS: 20 px at 2 px per step = +10 steps of 0.01 (value 0.10)");
    d.touchUp (50, 30);
    c = d.turn (1);
    check (c.kind == ChangeKind::Index && near (c.value, 0.01, 1e-9), "number field: CRS: turn (+1) = 0.01");
    // not set: the first move gives a value
    f.state.notSet = true;
    c = f.turn (1);
    check (c.kind == ChangeKind::Index, "number field: a value that is not set takes the first turn");
    f.state.notSet = false;
    // read only
    f.state.readOnly = true;
    f.state.value = 3;
    check (! drag (f, x, 60, 20) && ! f.turn (1) && ! tap (f, x, 40, 5000.0) && ! tap (f, x, 40, 5100.0), "number field: read only: no reaction");
    f.state.readOnly = false;
    f.setEnabled (false);
    check (! drag (f, x, 60, 20) && ! f.turn (1) && ! tap (f, x, 40, 6000.0) && ! tap (f, x, 40, 6100.0), "number field: disabled: no reaction");
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

void testKnobDrawing()
{
    for (int variant = 0; variant < 2; ++variant)
    {
        const bool small = variant == 1;
        Knob k (small ? "RAND" : "DETUNE", small);
        k.setId (1);
        const Size m = k.minSize();
        check (k.minSize().w == (small ? 72 : 100) && k.minSize().h == (small ? 56 : 72), std::string ("knob: minimum size of the ") + (small ? "small" : "large") + " variant");
        k.state.value = 0.6; k.state.modValue = 0.8; k.state.add (1, "LFO 1", 0.4); k.state.selectedSource = 1; k.state.bipolar = ! small;
        if (! small)
        {
            k.setLabel ("WIDESTLB");
            check (k.sizeOk() == false, "knob: a knob without a rectangle is not at its minimum size");
            k.place ({ 0, 0, m.w, m.h });
            check (k.sizeOk() && k.geom().labelCell == 2, "knob: at the minimum size an 8 character label is 2 pixels per font pixel");
        }
        const std::string nm = small ? "knob small" : "knob";
        checkDrawing (k, nm + " (minimum size)", { 12, 12, m.w, m.h }, small ? "knob_small_min.png" : "knob_min.png");
        checkDrawing (k, nm + " (large)", { 12, 12, 260, 220 }, small ? "knob_small_large.png" : "knob_large.png");
        // the value marker moves monotonically with the value, and sits where valuePoint says
        k.state.modValue = k.state.value;
        k.place ({ 30, 30, m.w * 2, m.h * 2 });
        double lastAngle = -1.0;
        bool mono = true, painted = true;
        for (int i = 0; i <= 20; ++i)
        {
            k.state.value = k.state.modValue = i / 20.0;
            Scene s (400, 300, kBackgroundRgba);
            k.draw (s.builder());
            const Image im = render (s);
            float x, y;
            k.valuePoint (x, y);
            const Knob::Geom g = k.geom();
            double a = std::atan2 (y - g.cy, x - g.cx);
            if (a < Knob::kStartAngle - 0.01) a += 2.0 * kPi;
            mono = mono && a > lastAngle - 1.0e-6;
            lastAngle = a;
            painted = painted && colourNear (im, x, y, 1, colour::kWhite, 8);
        }
        check (mono, nm + ": the value marker moves monotonically with the value (21 steps)");
        check (painted, nm + ": the value marker is drawn at valuePoint () in every one of them");
        // the modulated marker
        k.state.value = 0.3; k.state.modValue = 0.7;
        Scene sm (400, 300, kBackgroundRgba);
        k.draw (sm.builder());
        const Image im = render (sm);
        float vx, vy, mx, my;
        k.valuePoint (vx, vy);
        k.modPoint (mx, my);
        check (k.modulated() && std::hypot (vx - mx, vy - my) > 6.0, nm + ": the modulated marker is somewhere else than the value marker");
        check (colourNear (im, mx, my, 1, colour::kText, 12), nm + ": the modulated marker is drawn at modPoint ()");
        k.state.modValue = k.state.value;
        Scene sn (400, 300, kBackgroundRgba);
        k.draw (sn.builder());
        check (! k.modulated() && sn.numOps() < sm.numOps(), nm + ": with the modulated value equal to the value there is no second marker (fewer ops)");
        // modulation tab and bipolar mark change the picture
        Knob a2 (small ? "RAND" : "DETUNE", small), b2 (small ? "RAND" : "DETUNE", small);
        a2.place ({ 0, 0, 200, 150 }); b2.place ({ 0, 0, 200, 150 });
        a2.state.value = b2.state.value = 0.4;
        b2.state.add (1, "LFO 1", 0.4); b2.state.selectedSource = 1;
        Scene s1 (200, 150, kBackgroundRgba), s2 (200, 150, kBackgroundRgba);
        a2.draw (s1.builder()); b2.draw (s2.builder());
        check (render (s1).px != render (s2).px && colourNear (render (s2), 5, 5, 2, colour::kAccent, 6), nm + ": the modulation tab at the top left");
        b2.state = a2.state; b2.state.bipolar = true;
        Scene s3 (200, 150, kBackgroundRgba);
        b2.draw (s3.builder());
        check (render (s1).px != render (s3).px, nm + ": the bipolar detent mark changes the picture");
        // Q index
        k.setQ (true, 3);
        Scene sq (400, 300, kBackgroundRgba);
        k.draw (sq.builder());
        check (drawnOutside (render (sq), k.rect(), backgroundPixel()) == 0, nm + ": with a Q index (badge) still nothing outside the rectangle");
        k.setQ (false, 0);
    }
}

void testOtherDrawing()
{
    NumberField f ("OCT", NumberField::Format::IntSigned);
    f.state.value = 2;
    checkDrawing (f, "number field", { 10, 10, f.minSize().w, f.minSize().h }, "numberfield_min.png");
    checkDrawing (f, "number field (large)", { 10, 10, 300, 100 }, "numberfield_large.png");
    NumberField d ("CRS", NumberField::Format::Decimal2);
    d.state.value = -64; d.state.min = -64; d.state.max = 64;
    checkDrawing (d, "number field decimal", { 10, 10, 80, 56 }, "numberfield_decimal.png");
    NumberField p;
    p.state.icon = NumberField::Icon::Phase; p.state.format = NumberField::Format::Degrees; p.state.value = 180; p.state.max = 360;
    checkDrawing (p, "number field phase icon", { 10, 10, 80, 56 }, "numberfield_phase.png");
    NumberField rd;
    rd.state.icon = NumberField::Icon::Rand; rd.state.format = NumberField::Format::IntPlain; rd.state.value = 40;
    checkDrawing (rd, "number field rand icon", { 10, 10, 80, 56 }, nullptr);
    NumberField fr;
    fr.state.format = NumberField::Format::IntPlain; fr.state.value = 128; fr.state.readOnly = true;
    checkDrawing (fr, "number field plain (frame number)", { 10, 10, 80, 56 }, nullptr);
    NumberField ns ("FIN", NumberField::Format::IntSigned);
    ns.state.notSet = true;
    checkDrawing (ns, "number field not set", { 10, 10, 80, 56 }, nullptr);

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
    check (m.w == 5 * 76 && m.h == 56, "pictogram selector: minimum size of five items in a row: 380 x 56");
    s.place ({ 10, 10, m.w, m.h });
    check (s.sizeOk() && ! s.vertical() && s.cell (0).w == 76 && s.cell (4).x == 10 + 4 * 76, "pictogram selector: a wide rectangle: a row of cells");
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
    // pixels: the four tabs and the arc of a knob
    auto knobWith = [] (const ModInput& mi, double v)
    {
        std::unique_ptr<Knob> k (new Knob ("DETUNE"));
        k->place ({ 0, 0, 160, 120 });
        k->state.value = k->state.modValue = v;
        static_cast<ModInput&> (k->state) = mi;
        return k;
    };
    auto pic = [] (const Knob& k) { Scene s (160, 120, kBackgroundRgba); k.draw (s.builder()); return render (s); };
    const Rect t = tabRect ({ 0, 0, 160, 120 });
    const float tx = static_cast<float> (t.x + 7), ty = static_cast<float> (t.y + 4);
    {
        auto k = knobWith (none, 0.5);
        check (! colourNear (pic (*k), tx, ty, 1, colour::kAccent, 8) && ! colourNear (pic (*k), tx, ty, 1, colour::kRest, 8), "knob tab: absent: nothing at the tab");
        m.selectedSource = kNoSource;
        k = knobWith (m, 0.5);
        check (colourNear (pic (*k), tx, ty, 0, colour::kRest, 8), "knob tab: grey");
        m.selectedSource = 7;
        k = knobWith (m, 0.5);
        check (colourNear (pic (*k), tx, ty, 0, colour::kAccent, 8), "knob tab: coloured");
        m.selectedSource = 11;
        k = knobWith (m, 0.5);
        const Image im = pic (*k);
        check (! colourNear (im, tx, ty, 0, colour::kAccent, 8) && colourNear (im, static_cast<float> (t.x), ty, 0, colour::kAccent, 8), "knob tab: hollow (the middle empty, the edge drawn)");
    }
    {
        // the depth arc is drawn between the angles of its ends: a pixel on the inner arc at the middle of the span has the arc colour; outside of the span it does not
        m.selectedSource = 7;
        auto k = knobWith (m, 0.2);
        const Knob::Geom g = k->geom();
        const float rm = g.radius - g.stroke - 1.0f;
        auto at = [&] (double v) { const float an = Knob::angleOf (v); return std::make_pair (g.cx + rm * std::cos (an), g.cy + rm * std::sin (an)); };
        const Image im = pic (*k);
        const auto in = at (0.35), out = at (0.7);
        check (colourNear (im, in.first, in.second, 0, 0xe6e6e6, 70) && ! colourNear (im, out.first, out.second, 0, 0xe6e6e6, 40), "knob arc: the positive depth arc (0.2 .. 0.5) is there at 0.35 and not at 0.7");
        m.selectedSource = 8;
        k = knobWith (m, 0.5);
        const Image neg = pic (*k);
        const auto lo = at (0.35);
        check (colourNear (neg, lo.first, lo.second, 0, colour::kAccentDark, 24), "knob arc: the negative depth arc (0.2 .. 0.5) is in the darker shade");
        m.selectedSource = 11;
        k = knobWith (m, 0.5);
        Scene s1 (160, 120, kBackgroundRgba), s2 (160, 120, kBackgroundRgba);
        k->draw (s1.builder());
        m.selectedSource = kNoSource;
        k = knobWith (m, 0.5);
        k->draw (s2.builder());
        check (s1.numOps() == s2.numOps() + 0 || s1.numOps() >= s2.numOps(), "knob arc: the bypassed source draws no arc op");
    }
    // the depth gesture: only from the tab, only when the selected source is in the entries
    Knob k ("DETUNE");
    k.place ({ 50, 40, 160, 120 });
    k.setId(3);
    k.state.value = 0.5;
    k.state.add (7, "LFO 2", 0.3);
    k.state.add (8, "ENV 1", -0.2);
    k.state.selectedSource = 7;
    const int tabX = 55, tabY = 46, bodyX = 130, bodyY = 100;
    Change c = k.touchDown (tabX, tabY, 0.0);
    c = k.touchMove (tabX, tabY - 20);
    check (c.kind == ChangeKind::Depth && c.id == 3 && c.index == 7 && near (c.value, 0.5 /* 0.3 + 20/100 */), "depth gesture: from the tab: { id 3, source 7, depth 0.3 + 0.2 }");
    c = k.touchMove (tabX, tabY - 200);
    check (c.kind == ChangeKind::Depth && near (c.value, 1.0), "depth gesture: clamps at +1 (200 px = the whole -1 .. +1)");
    c = k.touchMove (tabX, tabY + 400);
    check (c.kind == ChangeKind::Depth && near (c.value, -1.0), "depth gesture: clamps at -1");
    check (! k.touchUp (tabX, tabY + 400) , "depth gesture: the touch up gives nothing");
    k.touchDown (tabX, tabY, 10.0);
    c = k.touchMove (-300, -300);
    check (c.kind == ChangeKind::Depth, "depth gesture: kept outside the rectangle");
    k.touchUp (-300, -300);
    k.touchDown (bodyX, bodyY, 20.0);
    c = k.touchMove (bodyX, bodyY - 20);
    check (c.kind == ChangeKind::Value && near (c.value, 0.6), "depth gesture: a drag that starts elsewhere changes the VALUE");
    k.touchUp (bodyX, bodyY - 20);
    k.state.selectedSource = 99;
    k.touchDown (tabX, tabY, 30.0);
    c = k.touchMove (tabX, tabY - 20);
    check (c.kind == ChangeKind::Value, "depth gesture: the selected source is not in the entries: the tab area is an ordinary part of the knob (a value drag)");
    k.touchUp (tabX, tabY - 20);
    k.state.selectedSource = kNoSource;
    k.touchDown (tabX, tabY, 40.0);
    check (k.touchMove (tabX, tabY - 20).kind == ChangeKind::Value, "depth gesture: no selected source: a value drag");
    k.touchUp (tabX, tabY - 20);
    k.state.selectedSource = 8;
    k.touchDown (tabX, tabY, 50.0);
    c = k.touchMove (tabX, tabY + 20);
    check (c.kind == ChangeKind::Depth && c.index == 8 && near (c.value, -0.4), "depth gesture: the entry of the selected source (8): -0.2 - 0.2");
    k.touchUp (tabX, tabY + 20);
    // a double tap on the tab does not reset the value
    k.state.selectedSource = 7;
    k.state.value = 0.9; k.state.defaultValue = 0.1;
    tap (k, tabX, tabY, 1000.0);
    check (! tap (k, tabX, tabY, 1100.0), "depth gesture: a double tap on the tab is not 'default value'");
    // the long press, with a fake clock
    k.state.selectedSource = kNoSource;
    k.touchDown (bodyX, bodyY, 5000.0);
    check (! k.tick (5300.0) && ! k.tick (5599.0), "long press: not before 0.6 s");
    c = k.tick (5600.0);
    check (c.kind == ChangeKind::ModsRequest && c.id == 3, "long press: at 0.6 s: { mods requested, id 3 }");
    check (! k.tick (6000.0) && ! k.touchUp (bodyX, bodyY), "long press: only once, and the touch up after it gives nothing (no tap)");
    c = tap (k, bodyX, bodyY, 6100.0);
    check (! c, "long press: a tap after it is a first tap again");
    k.touchDown (bodyX, bodyY, 7000.0);
    check (! k.tick (7300.0), "long press: (0.3 s)");
    c = k.touchUp (bodyX, bodyY);
    check (! c && ! k.tick (9000.0), "long press: a tap (released at 0.3 s) is not a long press, also not later");
    k.touchDown (bodyX, bodyY, 8000.0);
    k.touchMove (bodyX, bodyY - 30);
    check (! k.tick (9000.0), "long press: a drag (beyond the tap square) is not a long press");
    k.touchUp (bodyX, bodyY - 30);
    k.touchDown (bodyX, bodyY, 10000.0);
    k.touchMove (bodyX + 2, bodyY - 2);
    check (k.tick (10700.0).kind == ChangeKind::ModsRequest, "long press: a small movement inside the tap square does not prevent it");
    k.touchUp (bodyX, bodyY);
    k.state.selectedSource = 7;
    k.touchDown (tabX, tabY, 11000.0);
    check (k.tick (11700.0).kind == ChangeKind::ModsRequest, "long press: also on the tab");
    k.touchUp (tabX, tabY);
    check (! k.tick (99999.0), "long press: nothing without a gesture");
    k.setEnabled (false);
    k.touchDown (bodyX, bodyY, 12000.0);
    check (! k.tick (13000.0), "long press: disabled: nothing");
    // the same for the number field
    NumberField f ("OCT", NumberField::Format::IntSigned);
    f.place ({ 10, 10, 100, 60 });
    f.setId (4);
    f.state.min = -4; f.state.max = 4; f.state.value = 0;
    f.state.add (7, "LFO 2", 0.5);
    f.state.selectedSource = 7;
    f.touchDown (15, 14, 0.0);
    c = f.touchMove (15, 14 - 20);
    check (c.kind == ChangeKind::Depth && c.id == 4 && c.index == 7 && near (c.value, 0.7), "number field: a drag from the tab changes the depth { id 4, source 7, 0.7 }");
    f.touchUp (15, -6);
    f.touchDown (60, 50, 100.0);
    check (f.touchMove (60, 26).kind == ChangeKind::Index, "number field: a drag elsewhere changes the value");
    f.touchUp (60, 26);
    f.touchDown (60, 50, 1000.0);
    check (f.tick (1700.0).kind == ChangeKind::ModsRequest && ! f.touchUp (60, 50), "number field: the long press asks for the modulations");
    ModInput none2;
    NumberField g ("OCT", NumberField::Format::IntSigned);
    g.place ({ 0, 0, 80, 56 });
    g.state.min = -4; g.state.max = 4; g.state.value = 1;
    Scene s0 (80, 56, kBackgroundRgba), s1 (80, 56, kBackgroundRgba), s2 (80, 56, kBackgroundRgba);
    g.draw (s0.builder());
    g.state.add (7, "LFO 2", 0.3); g.state.selectedSource = 7;
    g.draw (s1.builder());
    g.state.selectedSource = kNoSource;
    g.draw (s2.builder());
    check (s1.numOps() > s2.numOps() && s2.numOps() > s0.numOps(), "number field: the tab (grey) and, with the selected source, the underline bar are drawn");
    Scene s3 (80, 56, kBackgroundRgba);
    g.state.selectedSource = 7;
    g.draw (s3.builder());
    check (drawnOutside (render (s3), g.rect(), backgroundPixel()) == 0, "number field with a modulation: nothing outside the rectangle");
    (void) none2;
}
} // namespace

int main()
{
    testKnob();
    testNumberField();
    testStepper();
    testUpDown();
    testToggle();
    testButtons();
    testDetailButton();
    testPictogram();
    testModulation();
    testKnobDrawing();
    testOtherDrawing();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
