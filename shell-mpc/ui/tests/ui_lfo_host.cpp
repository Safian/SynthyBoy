// SynthyBoy - MPC UI component library tests: the LFO element (ui/lfo_model.h, lfo_display.h, lfo_parts.h, lfo_screen.h, shape_editor.h). The shape model against the engine's evaluator (the built-in shapes as
// points give the engine's values), the editing operations (insert, move, remove, flat and ramp cells, undo, the grid a shape was drawn on), the sync value, both views for the LFO and the random LFO (every
// leaf inside the screen, at least its minimum size, nothing overlaps, a tap on the middle reaches the leaf, every Q group adjacent with free lanes for the index chips, the pictures fit one scene), the
// parts (shape picker, sync switch, rate control, instance bar) and the gestures of the shape editor's canvas.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../lfo_screen.h"
#include "../shape_editor.h"

using namespace uit;

namespace
{
int cx (const Component& c) { return c.rect().x + c.rect().w / 2; }
int cy (const Component& c) { return c.rect().y + c.rect().h / 2; }
Change tap (Component& c, int x, int y, double t)
{
    const Change d = c.touchDown (x, y, t);
    const Change u = c.touchUp (x, y);
    return u ? u : d;
}
int chipsCovered (const QGroup& g, Component* const* all, int n)
{
    int bad = 0;
    for (int i = 0; i < QGroup::kSlots; ++i)
    {
        const Component* m = g.member (i);
        if (m == nullptr)
            continue;
        const Rect chip { m->rect().x + 4, std::max (0, m->rect().y - 15), 18, std::min (15, m->rect().y) };
        for (int k = 0; k < n; ++k)
            bad += all[k]->rect().overlaps (chip);
    }
    return bad;
}
bool near (float a, float b, float tol = 1.0e-4f) { return std::fabs (a - b) <= tol; }

void testModel()
{
    // the built-in shapes as points give the engine's value at the cycle positions (exact for the shapes of straight pieces and steps, within a hundredth for the sampled ones)
    for (int shape = 0; shape < lfo::kNumBuiltin; ++shape)
    {
        const lfo::ShapeModel m = lfo::ShapeModel::fromBuiltin (shape);
        float worst = 0.0f;
        for (int i = 0; i < 200; ++i)
        {
            const float p = (static_cast<float> (i) + 0.37f) / 200.0f;
            worst = std::max (worst, std::fabs (m.eval (p) - (0.5f + 0.5f * lfo::builtinAt (shape, p))));
        }
        check (m.n >= 2 && m.n <= lfo::kMaxPoints && near (m.p[0].x, 0.0f) && near (m.p[m.n - 1].x, 1.0f), "shape " + std::to_string (shape) + ": a valid point list (" + std::to_string (m.n) + " points)");
        check (worst < (shape >= 9 ? 0.02f : 1.0e-4f), "shape " + std::to_string (shape) + ": the points give the engine's value (worst error " + std::to_string (worst) + ")");
    }
    // the power: the middle of a segment follows the finger
    for (float mid : { 0.2f, 0.35f, 0.5f, 0.8f })
    {
        const float p = lfo::powerForMid (0.0f, 1.0f, mid);
        check (near (lfo::powerScale (0.5f, p), mid, 0.01f), "powerForMid: the middle of the segment reaches " + std::to_string (mid));
    }
    lfo::ShapeModel m;
    m.initLinear();
    const int a = m.insert (0.5f, 0.8f);
    check (a == 1 && m.n == 3 && near (m.eval (0.5f), 0.8f) && near (m.eval (0.25f), 0.4f), "insert: a point between the ends");
    m.move (1, 0.9f, 2.0f);
    check (near (m.p[1].x, 0.9f) && near (m.p[1].y, 1.0f), "move: y is limited to 0 .. 1");
    m.move (0, 0.3f, 0.5f);
    check (near (m.p[0].x, 0.0f) && near (m.p[0].y, 0.5f), "move: the first point stays at x = 0");
    check (m.remove (1) && m.n == 2 && ! m.remove (0) && ! m.remove (1), "remove: an inner point goes, the end points and the last two stay");
    // flat and ramp cells
    m.initLinear();
    check (m.replaceRange (0.25f, 0.5f, 0.5f, 0.5f) && near (m.eval (0.3f), 0.5f) && near (m.eval (0.4f), 0.5f), "flat cell: the piece is flat");
    check (near (m.eval (0.1f), 0.1f) && near (m.eval (0.9f), 0.5f + 0.0f * 0.9f, 1.0f), "flat cell: the line before it is where it was");
    check (m.replaceRange (0.5f, 0.75f, 0.25f, 0.75f) && near (m.eval (0.625f), 0.5f, 0.01f), "ramp cell: from the lower to the upper line");
    lfo::ShapeModel saw;
    saw.initLinear();
    bool ok = true;
    for (int c = 0; c < 8; ++c)
        ok = saw.replaceRange (static_cast<float> (c) / 8.0f, static_cast<float> (c + 1) / 8.0f, 0.0f, 1.0f) && ok;
    check (ok && saw.n >= 8 && near (saw.eval (0.0625f), 0.5f, 0.01f) && near (saw.eval (0.4375f), 0.5f, 0.01f), "eight ramps in a row make a saw tooth (" + std::to_string (saw.n) + " points)");
    for (int i = 1; i < saw.n; ++i)
        ok = ok && saw.p[i].x >= saw.p[i - 1].x;
    check (ok, "the points stay in ascending x");
    // the whole shape replaced from edge to edge
    lfo::ShapeModel w;
    w.initLinear();
    check (w.replaceRange (0.0f, 1.0f, 0.3f, 0.3f) && w.n == 2 && near (w.eval (0.5f), 0.3f), "a flat piece over the whole shape");
    // 128 points are the limit
    lfo::ShapeModel full;
    full.initLinear();
    int added = 0;
    for (int i = 0; i < 200; ++i)
        added += full.insert (static_cast<float> (i % 97) / 97.0f, 0.5f) >= 0;
    check (full.n == lfo::kMaxPoints && added == lfo::kMaxPoints - 2 && full.insert (0.5f, 0.5f) == -1, "at most 128 points");
    // undo
    lfo::Undo u;
    lfo::ShapeModel s0;
    s0.initLinear();
    lfo::ShapeModel s1 = s0;
    s1.insert (0.5f, 1.0f);
    u.push (s0);
    u.push (s0);
    lfo::ShapeModel back;
    check (u.depth() == 1 && u.pop (back) && back == s0 && ! u.pop (back), "undo: the state before a change comes back (the same state twice is kept once)");
    for (int i = 0; i < 100; ++i)
    {
        lfo::ShapeModel t = s0;
        t.p[0].y = static_cast<float> (i % 50) / 50.0f;
        u.push (t);
    }
    check (u.depth() == lfo::Undo::kDepth, "undo: the last " + std::to_string (lfo::Undo::kDepth) + " states");
    // the grid a shape was drawn on
    lfo::ShapeModel g;
    g.n = 3; g.p[0] = { 0.0f, 0.25f, 0.0f }; g.p[1] = { 0.5f, 0.75f, 0.0f }; g.p[2] = { 1.0f, 0.5f, 0.0f };
    check (lfo::inferGrid (g) == 4, "inferGrid: points on a grid of four");
    g.p[1].x = 0.4137f;
    check (lfo::inferGrid (g) == 8, "inferGrid: a free shape starts on 8");
    // the sync
    check (lfo::syncOf (lfo::SyncMode::Hz, true, true) == 0 && lfo::syncOf (lfo::SyncMode::Key, false, false) == 4 && lfo::syncOf (lfo::SyncMode::Bpm, false, false) == 1 && lfo::syncOf (lfo::SyncMode::Bpm, true, false) == 3 && lfo::syncOf (lfo::SyncMode::Bpm, false, true) == 2, "the sync value of a mode and TRIP / DOT");
    check (lfo::syncModeOf (2) == lfo::SyncMode::Bpm && lfo::syncDot (2) && lfo::syncTrip (3) && lfo::syncModeOf (4) == lfo::SyncMode::Key, "the mode and the switches of a sync value");
}

void testParts()
{
    ShapePicker sp;
    sp.place ({ 0, 0, 304, 224 });
    sp.setId (5);
    check (sp.sizeOk(), "shape picker: its minimum size");
    Change c = tap (sp, sp.cell (6).x + 10, sp.cell (6).y + 10, 0.0);
    check (c.kind == ChangeKind::Index && c.id == 5 && c.index == 6, "shape picker: a tap on a cell is its Index change");
    sp.state.index = 6;
    check (! tap (sp, sp.cell (6).x + 10, sp.cell (6).y + 10, 1000.0), "shape picker: the current one is no change");
    check (sp.turn (1.0).index == 7 && sp.turn (-1.0).index == 5, "shape picker: turn steps");
    check (sp.rows() == 4 && sp.cell (13).y == sp.cell (12).y && sp.cell (13).x > sp.cell (12).x, "shape picker: the free cells of the last row come after the 13th");
    sp.state.styles = true;
    sp.state.cols = 2;
    check (sp.count() == 5 && sp.rows() == 3, "shape picker: five styles in two columns");

    SyncSwitch sw;
    sw.place ({ 0, 0, 340, 56 });
    sw.mode.setId (1); sw.trip.setId (2); sw.dot.setId (3);
    sw.setSync (1);
    check (sw.trip.enabled() && sw.dot.enabled(), "sync switch: TRIP and DOT work in the tempo mode");
    sw.setSync (0);
    check (! sw.trip.enabled() && ! sw.dot.enabled() && sw.mode.state.index == 0, "sync switch: dim in the Hz mode");
    sw.setSync (4);
    check (sw.mode.state.index == 2, "sync switch: the key follow is the third cell");
    sw.setSync (2);
    check (sw.dot.state.on && ! sw.trip.state.on && sw.mode.state.index == 1, "sync switch: dotted = BPM with DOT");
    c = Change::indexed (1, 0, 0);
    check (sw.nextSync (&sw.mode, c) == 0, "sync switch: the HZ cell gives sync 0");
    c = Change::indexed (2, 1, 1);
    check (sw.nextSync (&sw.trip, c) == 3, "sync switch: TRIP on a dotted one: triplet");
    c = Change::indexed (3, 0, 0);
    check (sw.nextSync (&sw.dot, c) == 1, "sync switch: DOT off: plain tempo");
    c = Change::indexed (1, 1, 1);
    sw.setSync (0);
    check (sw.nextSync (&sw.mode, c) == 1, "sync switch: the BPM cell from Hz: tempo");

    RateControl rc;
    rc.place ({ 0, 0, 300, 72 });
    rc.rate.setId (1); rc.division.setId (2);
    rc.setMode (lfo::SyncMode::Hz);
    check (rc.childShown (0) && ! rc.childShown (1) && rc.primary() == &rc.rate, "rate control: the knob in the Hz mode");
    rc.setMode (lfo::SyncMode::Bpm);
    check (! rc.childShown (0) && rc.childShown (1) && rc.primary() == &rc.division, "rate control: the division stepper in the tempo mode");
    rc.setMode (lfo::SyncMode::Key);
    check (rc.childShown (0) && ! rc.rate.enabled() && rc.primary() == &rc.rate, "rate control: the knob dim in the key follow mode");

    InstanceBar ib;
    ib.place ({ 0, 0, 624, 56 });
    ib.lfoCells.setId (1); ib.rndCells.setId (2);
    ib.configure (false, 7);
    check (ib.lfoCells.state.index == 6 && ib.rndCells.state.index == -1, "instance bar: LFO 7 is the seventh cell (all ten LFOs are in the bar)");
    ib.configure (true, 3);
    check (ib.lfoCells.state.index == -1 && ib.rndCells.state.index == 2, "instance bar: RND 3 is the third random cell");
    check (ib.sizeOk(), "instance bar: its minimum size");
}

/** The common checks of a view: leaves inside, apart, minimum size, a tap reaches, groups adjacent, chips free, the picture fits one scene and stays inside the area. */
template <class View>
void checkView (View& s, const Rect& area, const std::string& nm, QGroup* groups, int numGroups, uint32_t bgRgba)
{
    Component* all[96];
    const int n = s.collectLeaves (all, 0, 96);
    check (n > 5 && n < 96, nm + ": " + std::to_string (n) + " leaves");
    bool inside = true, apart = true, size = true;
    std::string which;
    for (int i = 0; i < n; ++i)
    {
        inside = inside && area.contains (all[i]->rect());
        if (! all[i]->sizeOk())
        {
            size = false;
            which += " (" + std::to_string (all[i]->rect().w) + "x" + std::to_string (all[i]->rect().h) + " at " + std::to_string (all[i]->rect().x) + "," + std::to_string (all[i]->rect().y) + ")";
        }
        for (int j = i + 1; j < n; ++j)
            apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
    }
    check (inside, nm + ": every leaf inside the area");
    check (size, nm + ": every leaf at least its minimum size" + which);
    check (apart, nm + ": no two leaves overlap");
    int missed = 0;
    std::string miss;
    for (int i = 0; i < n; ++i)
    {
        if (! all[i]->enabled())
            continue;
        const int px = cx (*all[i]), py = cy (*all[i]);
        s.touchDown (px, py, 0.0);
        if (! all[i]->capturing())
        {
            ++missed;
            miss += " (" + std::to_string (px) + "," + std::to_string (py) + ")";
        }
        s.touchUp (px, py);
    }
    check (missed == 0, nm + ": a tap on the middle of each enabled leaf reaches it" + miss);
    for (int g = 0; g < numGroups; ++g)
    {
        const Component* off = nullptr;
        const int bad = groups[g].check (all, n, &off);
        check (bad == 0 && groups[g].numMembers() >= 1, nm + ": group " + std::to_string (g) + ": all members adjacent (offenders " + std::to_string (bad) + ")");
        check (chipsCovered (groups[g], all, n) == 0, nm + ": group " + std::to_string (g) + ": the index chips lie on free lanes");
    }
    Scene sc (area.right() + 10, area.bottom() + 10, bgRgba);
    s.draw (sc.builder());
    check (! sc.overflow(), nm + ": the picture fits one scene (" + std::to_string (sc.numOps()) + " ops)");
}

void testViews()
{
    for (bool random : { false, true })
    {
        LfoCompact c;
        c.place ({ 858, 114, 408, 620 });
        c.configure (random);
        const std::string nm = std::string ("compact ") + (random ? "random" : "LFO");
        checkView (c, { 858, 114, 408, 620 }, nm, &c.group, 1, 0x0b1026ffu);
        check (c.group.numMembers() == 4 && c.group.active(), nm + ": one Q group of four");
        check (! c.displayRect().empty() && c.displayRect().w >= 380 && c.displayRect().h >= 200 && ! c.displayRect().overlaps (c.group.rect()), nm + ": the display " + std::to_string (c.displayRect().w) + " x " + std::to_string (c.displayRect().h) + ", the group under it");
        check (c.full.rect().w >= 56 && c.full.rect().h >= 56 && c.displayRect().contains (c.full.rect()), nm + ": the full screen button on the display, a touch target");
        check (c.group.member (1) == &c.rate, nm + ": Q2 is the rate control");
    }
    for (int variant = 0; variant < 3; ++variant)
    {
        const bool random = variant == 2;
        LfoFullScreen f;
        f.place ({ 0, 110, 1280, 628 });
        f.configure (random);
        f.setKeyActive (variant == 1);
        const std::string nm = std::string ("full screen ") + (random ? "random" : (variant == 1 ? "LFO, key follow" : "LFO"));
        checkView (f, { 0, 110, 1280, 628 }, nm, f.groups, f.numGroups(), 0x0b1026ffu);
        f.setActiveGroup (0);
        check (f.numGroups() == (random ? 3 : 5) && f.activeGroup() == 0 && f.groups[0].active(), nm + ": " + std::to_string (f.numGroups()) + " groups, the first active");
        check (! f.displayRect().empty() && f.displayRect().w > 800 && f.displayRect().h >= 200, nm + ": the display " + std::to_string (f.displayRect().w) + " x " + std::to_string (f.displayRect().h));
        check (f.displayRect().right() <= f.picker.rect().x && f.picker.rect().right() <= 1280, nm + ": the picker at the right of the display");
        for (int b = 0; b < f.numBlocks(); ++b)
            check (f.block (b).y >= f.displayRect().bottom() && f.block (b).bottom() <= 738, nm + ": block " + std::to_string (b) + " under the display, inside the tab");
        // the Q frame moves to a tapped group
        if (! random)
        {
            check (f.groups[0].member (0) == &f.picker && f.groups[0].member (1) == &f.rate &&
                   f.groups[0].member (2) == &f.rise && f.groups[0].member (3) == &f.smoothTime,
                   nm + ": group 0 is picker (Q1), rate, rise, smooth");
            check (f.groups[1].member (0) == &f.syncSwitch && f.groups[1].member (1) == &f.mode &&
                   f.groups[1].member (2) == &f.phase && f.groups[1].member (3) == &f.delay,
                   nm + ": group 1 is the HZ / BPM / KEY row, mode, phase, delay (no second SYNC stepper)");
            check (f.groups[2].member (0) == &f.direction && f.groups[2].member (1) == &f.swing &&
                   f.groups[2].member (2) == &f.stereo && f.groups[2].member (3) == &f.monoQ,
                   nm + ": group 2 is direction, swing, stereo, mono");
            check (f.groups[4].member (0) == &f.smoothPick && f.groups[4].member (1) == &f.hostPick,
                   nm + ": group 4 is smooth, host");
            check (f.rate.rate.state.large, nm + ": rate knob is large");
            check (f.rise.rect().h == f.smoothTime.rect().h && f.rise.rect().h == f.phase.rect().h &&
                   f.rise.rect().h == f.delay.rect().h && f.rise.rect().h == f.swing.rect().h &&
                   f.rise.rect().h == f.stereo.rect().h,
                   nm + ": one knob size family for rise, smooth, phase, delay, swing, stereo");
            f.touchDown (cx (f.direction), cy (f.direction), 0.0);
            f.touchUp (cx (f.direction), cy (f.direction));
            check (f.activeGroup() == 2 && f.groups[2].active() && ! f.groups[0].active(), nm + ": a touch on DIRECTION makes MOTION the active group");
            // tap on TIMING block background
            f.touchDown (f.block (1).x + 10, f.block (1).y + 10, 0.0);
            f.touchUp (f.block (1).x + 10, f.block (1).y + 10);
            check (f.activeGroup() == 1 && f.groups[1].active(), nm + ": tap on TIMING block makes it active");
            // tap on smoothPick activates group 4 (Shape)
            f.touchDown (cx (f.smoothPick), cy (f.smoothPick), 0.0);
            f.touchUp (cx (f.smoothPick), cy (f.smoothPick));
            check (f.activeGroup() == 4 && f.groups[4].active(), nm + ": tap on SMOOTH makes SHAPE active");
            f.touchDown (cx (f.keyTune), cy (f.keyTune), 0.0);
            f.touchUp (cx (f.keyTune), cy (f.keyTune));
            check ((variant == 1) == (f.activeGroup() == 3), nm + ": the KEY group is only active in the key follow sync");
            check (f.keyTranspose.enabled() == (variant == 1) && f.numBlocks() == (variant == 1 ? 4 : 3), nm + ": the KEY block is there in the key follow sync only (the others take its place)");
        }
        Change t = f.turnQ (0, 1.0);
        (void) t;
        check (f.groups[f.activeGroup()].active(), nm + ": an active group");
    }
}

void testEditor()
{
    ShapeEditorPage pg;
    pg.place ({ 0, 110, 1280, 628 });
    pg.canvas.setId (7); pg.tools.setId (8); pg.fx.setId (9);
    pg.model = lfo::ShapeModel::fromBuiltin (1);   // a triangle: (0, 0) (0.5, 1) (1, 0)
    pg.canvas.state.grid = 8;
    pg.sync();
    Component* all[40];
    const int n = pg.collectLeaves (all, 0, 40);
    bool inside = true, size = true, apart = true;
    for (int i = 0; i < n; ++i)
    {
        inside = inside && Rect { 0, 110, 1280, 628 }.contains (all[i]->rect());
        size = size && all[i]->sizeOk();
        for (int j = i + 1; j < n; ++j)
            apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
    }
    check (inside && size && apart, "editor page: " + std::to_string (n) + " leaves inside, at least their minimum size, apart");
    const Component* off = nullptr;
    check (pg.group.check (all, n, &off) == 0 && chipsCovered (pg.group, all, n) == 0, "editor page: the EDIT group is adjacent, free lanes for the chips");
    check (pg.canvas.rect().w > 900 && pg.canvas.rect().h > 380, "editor page: the canvas is big (" + std::to_string (pg.canvas.rect().w) + " x " + std::to_string (pg.canvas.rect().h) + ")");
    Scene sc (1290, 740, 0x0b1026ffu);
    pg.draw (sc.builder());
    check (! sc.overflow(), "editor page: the picture fits one scene (" + std::to_string (sc.numOps()) + " ops)");

    ShapeCanvas& cv = pg.canvas;
    const lfo::ShapeModel start = pg.model;
    // POINT: a tap on empty ground adds a point (snapped), the finger keeps it
    cv.state.tool = ToolSelector::Point;
    cv.state.snap = true;
    const int x25 = static_cast<int> (cv.xOf (0.25f)), y75 = static_cast<int> (cv.yOf (0.75f));
    Change c = cv.touchDown (x25 + 3, y75 + 2, 0.0);
    check (c.kind == ChangeKind::Index && c.index == ShapeCanvas::Edited && pg.model.n == 4 && cv.state.selected == 1, "canvas, point tool: a tap on empty ground adds a point");
    check (near (pg.model.p[1].x, 0.25f) && near (pg.model.p[1].y, 0.75f), "canvas: the new point snapped to the grid (0.25, 0.75)");
    c = cv.touchMove (static_cast<int> (cv.xOf (0.375f)), static_cast<int> (cv.yOf (0.25f)));
    check (near (pg.model.p[1].x, 0.375f) && near (pg.model.p[1].y, 0.25f), "canvas: the finger drags the new point");
    cv.touchUp (0, 0);
    check (pg.undo.depth() == 1, "canvas: one undo step for the gesture");
    // the point is selected: touching it again drags it (the others stay)
    const int px = static_cast<int> (cv.xOf (0.375f)), py = static_cast<int> (cv.yOf (0.25f));
    cv.touchDown (px + 2, py + 2, 1000.0);
    cv.touchMove (px + 2, py - 100);
    cv.touchUp (px, py - 100);
    check (pg.model.n == 4 && pg.model.p[1].y > 0.4f && pg.undo.depth() == 2, "canvas: a touch on a point drags it");
    // the end point only moves up and down
    const int ex = static_cast<int> (cv.xOf (1.0f)), ey = static_cast<int> (cv.yOf (0.0f));
    cv.touchDown (ex, ey, 2000.0);
    cv.touchMove (ex - 200, ey - 120);
    cv.touchUp (ex - 200, ey - 120);
    check (near (pg.model.p[pg.model.n - 1].x, 1.0f) && pg.model.p[pg.model.n - 1].y > 0.1f, "canvas: the last point keeps x = 1, it moves up");
    // a long press on an inner point deletes it
    const lfo::ShapeModel before = pg.model;
    const int mx = static_cast<int> (cv.xOf (pg.model.p[1].x)), my = static_cast<int> (cv.yOf (pg.model.p[1].y));
    cv.touchDown (mx, my, 3000.0);
    check (! cv.tick (3300.0), "canvas: no delete before 600 ms");
    c = cv.tick (3700.0);
    check (c.kind == ChangeKind::Index && pg.model.n == before.n - 1 && cv.state.selected == -1, "canvas: a long press on a point deletes it");
    cv.touchUp (mx, my);
    // the middle of a segment drags the curve
    pg.model = lfo::ShapeModel::fromBuiltin (2);   // a saw up: one segment
    cv.state.selected = -1;
    float hx, hy;
    check (cv.midHandle (0, hx, hy), "canvas: a segment that slopes has a middle handle");
    cv.touchDown (static_cast<int> (hx), static_cast<int> (hy), 4000.0);
    cv.touchMove (static_cast<int> (hx), static_cast<int> (hy) + 80);
    cv.touchUp (static_cast<int> (hx), static_cast<int> (hy) + 80);
    check (pg.model.p[0].power > 1.0f && cv.state.selectedSeg == 0, "canvas: dragging the middle down bends the segment (power " + std::to_string (pg.model.p[0].power) + ")");
    // FLAT: a cell is a flat piece at the height of the finger; a drag across draws a staircase
    pg.model.initLinear();
    cv.state.tool = ToolSelector::Flat;
    cv.touchDown (static_cast<int> (cv.xOf (0.0625f)), static_cast<int> (cv.yOf (0.5f)), 5000.0);
    cv.touchMove (static_cast<int> (cv.xOf (0.1875f)), static_cast<int> (cv.yOf (0.75f)));
    cv.touchMove (static_cast<int> (cv.xOf (0.3125f)), static_cast<int> (cv.yOf (0.25f)));
    cv.touchUp (0, 0);
    check (near (pg.model.eval (0.06f), 0.5f, 0.02f) && near (pg.model.eval (0.19f), 0.75f, 0.02f) && near (pg.model.eval (0.31f), 0.25f, 0.02f), "canvas, flat tool: a staircase over three cells");
    // RAMP UP / DOWN
    pg.model.initLinear();
    cv.state.tool = ToolSelector::RampUp;
    cv.touchDown (static_cast<int> (cv.xOf (0.0625f)), static_cast<int> (cv.yOf (0.4f)), 6000.0);
    cv.touchUp (0, 0);
    check (near (pg.model.eval (0.0625f), 0.4375f, 0.01f) && near (pg.model.eval (0.001f), 0.375f, 0.01f) && near (pg.model.eval (0.124f), 0.5f, 0.01f), "canvas, ramp up: the cell rises from the lower to the upper line of its row");
    cv.state.tool = ToolSelector::RampDown;
    cv.touchDown (static_cast<int> (cv.xOf (0.4375f)), static_cast<int> (cv.yOf (0.4f)), 7000.0);
    cv.touchUp (0, 0);
    check (pg.model.eval (0.4375f) > 0.4f && pg.model.eval (0.4375f) < 0.6f, "canvas, ramp down: the cell falls");
    (void) start;
    // the sync of the fields and buttons
    pg.model.initLinear();
    pg.model.insert (0.5f, 0.5f);
    cv.state.selected = 1;
    pg.sync();
    check (pg.fx.enabled() && pg.fy.enabled() && pg.fcurve.enabled() && pg.del.enabled() && pg.loop.enabled(), "editor page: the fields work for an inner point");
    cv.state.selected = 0;
    pg.sync();
    check (! pg.fx.enabled() && ! pg.del.enabled(), "editor page: the first point keeps x and cannot be deleted");
    cv.state.selected = -1;
    pg.sync();
    check (! pg.fx.enabled() && ! pg.fy.enabled() && ! pg.loop.enabled(), "editor page: nothing selected: the fields are dim");
}

void testDisplay()
{
    float y[256];
    for (int i = 0; i < 256; ++i)
        y[i] = std::sin (6.2832f * static_cast<float> (i) / 255.0f);
    lfodisplay::Data d;
    d.kind = lfodisplay::Data::Kind::Shape;
    d.y = y;
    d.n = 256;
    d.delayShare = 0.2f;
    d.riseShare = 0.4f;
    d.loopX = 0.5f;
    d.numCursors = 2;
    d.cx[0] = 0.25f; d.cy[0] = 1.0f; d.cx[1] = 0.6f; d.cy[1] = -0.5f;
    d.label = "LFO 1 - SINE";
    const Rect r { 20, 20, 700, 300 };
    check (lfodisplay::leadWidth (r, d) >= 20 && lfodisplay::leadWidth (r, d) <= 210, "display: the lead-in strip (" + std::to_string (lfodisplay::leadWidth (r, d)) + " px)");
    Scene sc (760, 340, 0x0b1026ffu);
    lfodisplay::draw (sc.builder(), r, d);
    lfodisplay::drawLive (sc.builder(), r, d);
    check (! sc.overflow(), "display: the picture fits one scene (" + std::to_string (sc.numOps()) + " ops)");
    const Image im = render (sc);
    check (drawnOutside (im, { 20, 20, 700, 300 }, backgroundPixel()) >= 0, "display: rendered");
    float tr[192];
    for (int i = 0; i < 192; ++i)
        tr[i] = std::sin (static_cast<float> (i) * 0.3f);
    lfodisplay::Data rnd;
    rnd.kind = lfodisplay::Data::Kind::Random;
    rnd.trace = tr;
    rnd.traceN = 192;
    Scene s2 (760, 340, 0x0b1026ffu);
    lfodisplay::draw (s2.builder(), r, rnd);
    lfodisplay::drawLive (s2.builder(), r, rnd);
    check (! s2.overflow(), "display: the trace of a random LFO fits one scene");
    // the reduced tier of detail: less is drawn (no lead-in, no area, two cursors)
    theme::setDetail (2);
    Scene sr (760, 340, 0x0b1026ffu);
    lfodisplay::draw (sr.builder(), r, d);
    lfodisplay::drawLive (sr.builder(), r, d);
    theme::setDetail (1);
    Scene sf (760, 340, 0x0b1026ffu);
    lfodisplay::draw (sf.builder(), r, d);
    lfodisplay::drawLive (sf.builder(), r, d);
    check (! sr.overflow() && sr.numOps() < sf.numOps(), "display: the reduced tier draws fewer ops (" + std::to_string (sr.numOps()) + " against " + std::to_string (sf.numOps()) + ")");
    d.dim = true;
    Scene s3 (760, 340, 0x0b1026ffu);
    lfodisplay::drawLive (s3.builder(), r, d);
    check (s3.numOps() <= 1, "display: a dim LFO has no cursors");

    // B3 (LFO graph): PHASE moves a start marker; RISE/DELAY > 0 shows separate strip above graph
    {
        lfodisplay::Data dClean = d;
        dClean.dim = false;
        dClean.delayShare = 0.0f;
        dClean.riseShare = 0.0f;
        dClean.phase = 0.25f;
        Scene scNoLead (760, 340, 0x0b1026ffu);
        lfodisplay::draw (scNoLead.builder(), r, dClean);
        lfodisplay::drawLive (scNoLead.builder(), r, dClean);
        check (! scNoLead.overflow(), "display clean: no lead strip fits scene");

        lfodisplay::Data dWithLead = dClean;
        dWithLead.delayShare = 0.3f;
        dWithLead.riseShare = 0.4f;
        Scene scWithLead (760, 340, 0x0b1026ffu);
        lfodisplay::draw (scWithLead.builder(), r, dWithLead);
        lfodisplay::drawLive (scWithLead.builder(), r, dWithLead);
        check (! scWithLead.overflow(), "display with delay/rise: separate lead strip fits scene");
        check (scWithLead.numOps() > scNoLead.numOps(), "display with delay/rise: draws extra ops for the lead-in strip above the shape graph");
    }
}
}

int main()
{
    testModel();
    testParts();
    testViews();
    testEditor();
    testDisplay();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
