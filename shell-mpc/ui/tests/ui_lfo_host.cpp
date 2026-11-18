// SynthyBoy - MPC UI component library tests: the LFO element (ui/lfo_model.h, lfo_display.h, lfo_parts.h, lfo_screen.h, shape_editor.h). The shape model against the engine's evaluator (the built-in shapes as
// points give the engine's values), the editing operations (insert, move, remove, flat and ramp cells, undo, the grid a shape was drawn on), the sync value, both views for the LFO and the random LFO (every
// leaf inside the screen, at least its minimum size, nothing overlaps, a tap on the middle reaches the leaf, every Q group adjacent with free lanes for the index chips, the pictures fit one scene), the
// parts (shape picker, sync switch, rate control, instance bar) and the gestures of the shape editor's canvas: the quick delete (a double tap), the step drawing of one gesture with no gap, the selection (rectangle, taps,
// moving, scaling, the soft and the bend tools), the marker, flip / shift / bend of the whole shape, the modulation of points, the page with its three Q groups and the page of the point modulation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../lfo_screen.h"
#include "../play_rows.h"
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
    sw.mode.place ({ 0, 0, 252, 56 });
    sw.mode.setId (1);
    {   // the logic of HZ / BPM / KEY with TRIP and DOT (the three LED controls are drawn by the screen and the rate control)
        RateControl sr;
        sr.setTripDot (true);
        sr.place ({ 0, 0, 300, 200 });
        sr.trip.setId (2); sr.dot.setId (3);
        sr.setMode (lfo::SyncMode::Bpm);
        check (sr.trip.enabled() && sr.dot.enabled() && sr.trip.rect().h >= 56 && sr.dot.rect().h >= 56 && sr.trip.rect().y >= sr.division.rect().bottom() && sr.trip.rect().right() <= sr.dot.rect().x + 1, "sync switch: TRIP and DOT (LED toggles of 56 px) stand under the division in the tempo mode");
        sr.setMode (lfo::SyncMode::Hz);
        check (sr.trip.rect().empty() && sr.rate.rect().h == 200, "sync switch: in the Hz mode the knob has the whole cell, TRIP and DOT are gone");
    }
    sw.setSync (0);
    check (sw.mode.state.index == 0 && sw.mode.state.count == 3, "sync switch: the list HZ / BPM / KEY, Hz is the first");
    sw.setSync (4);
    check (sw.mode.state.index == 2, "sync switch: the key follow is the third option");
    sw.setSync (2);
    check (sw.dot() && ! sw.trip() && sw.mode.state.index == 1, "sync switch: dotted = BPM with DOT");
    c = Change::indexed (1, 0, 0);
    check (sw.nextSync (SyncSwitch::Mode, c) == 0, "sync switch: the HZ option gives sync 0");
    c = Change::indexed (2, 1, 1);
    check (sw.nextSync (SyncSwitch::Trip, c) == 3, "sync switch: TRIP on a dotted one: triplet");
    c = Change::indexed (3, 0, 0);
    check (sw.nextSync (SyncSwitch::Dot, c) == 1, "sync switch: DOT off: plain tempo");
    c = Change::indexed (1, 1, 1);
    sw.setSync (0);
    check (sw.nextSync (SyncSwitch::Mode, c) == 1, "sync switch: the BPM option from Hz: tempo");
    check (sw.mode.turn (1.0).index == 1 && sw.mode.turn (-1.0).kind == ChangeKind::None, "sync switch: a Q-Link step moves the selection (no wrap)");

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
    ib.place ({ 0, 0, 844, 56 });
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

/** The compact LFO display (the Play page's third): the header band at its top holds the label and the full screen button, the curve lies in the plot area below it (the common one, playrows::plotRect; the lead-in strip of a delay / rise is the top of that
    area), for every shape of data (a cycle with and without a lead strip, an envelope type with a loop point and a phase marker, a random trace, a dim LFO), with the cursors; nothing is drawn under the button. The full page and the shape editor keep their rectangle. */
void testCompactPlot()
{
    LfoCompact c;
    c.place ({ 858, 114, 408, 620 });
    const Rect disp = c.displayRect(), area = playrows::plotRect (disp), band = playrows::bandRect (disp), fb = c.full.rect();
    check (disp == playrows::displayRect (c.rect()) && fb == playrows::fullRect (disp) && band.contains (fb) && fb.w >= 56 && fb.h >= 56, "compact LFO display: the full screen button stands in the header band, the same place as the oscillator's and the envelope's");
    float y[256], tr[192];
    for (int i = 0; i < 256; ++i)
        y[i] = i < 128 ? 1.0f : -1.0f;   // a square wave: its full height, the steps run up to the plot's top and bottom
    float tri[256];
    for (int i = 0; i < 256; ++i)
        tri[i] = 2.0f * std::fabs (2.0f * (static_cast<float> (i) / 255.0f - 0.5f)) - 1.0f;
    for (int i = 0; i < 192; ++i)
        tr[i] = std::sin (static_cast<float> (i) * 0.3f);
    struct Case { const char* name; bool random; bool lead; bool envelope; bool dim; };
    const Case cases[] = { { "square, no lead strip", false, false, false, false }, { "triangle with delay and rise (lead strip)", false, true, false, false }, { "envelope type with loop point and phase", false, false, true, false },
                           { "dim LFO", false, true, false, true }, { "random trace", true, false, false, false } };
    for (const Case& cs : cases)
    {
        lfodisplay::Data d;
        d.kind = cs.random ? lfodisplay::Data::Kind::Random : lfodisplay::Data::Kind::Shape;
        d.y = cs.lead ? tri : y;
        d.n = 256;
        d.trace = tr; d.traceN = 192;
        d.delayShare = cs.lead ? 0.3f : 0.0f; d.riseShare = cs.lead ? 0.4f : 0.0f;
        d.envelope = cs.envelope; d.loopX = cs.envelope ? 0.6f : -1.0f; d.phase = cs.envelope ? 0.25f : 0.0f;
        d.numCursors = 2; d.cx[0] = 0.0f; d.cy[0] = 1.0f; d.cx[1] = 1.0f; d.cy[1] = -1.0f;
        d.dim = cs.dim;
        d.label = "LFO 1 - A LONG SHAPE NAME THAT NEEDS ITS ROOM";
        const std::string nm = std::string ("compact LFO display, ") + cs.name + ": ";
        const Rect pl = lfodisplay::plotRect (disp, d);
        check (! pl.empty() && area.contains (pl) && pl.y >= band.bottom() && (cs.lead ? pl.y - area.y == 24 : pl == area), nm + "the curve's rectangle is the common plot area (under the lead strip, if any)");
        if (cs.lead)
            check (area.contains (lfodisplay::stripRect (disp, d)) && lfodisplay::stripRect (disp, d).y == area.y, nm + "the lead-in strip is the top of the plot area, under the band");
        Scene s (1280, 800, 0x0b1026ffu);
        lfodisplay::draw (s.builder(), disp, d);
        lfodisplay::drawLive (s.builder(), disp, d);
        check (! s.overflow() && dataOpsInto (s, fb) == 0 && dataOpsInto (s, band) == 0, nm + "no curve, fill, grid line, marker or cursor lies in the header band or under the full screen button (" + std::to_string (dataOpsInto (s, band)) + ")");
        // the label is written in the band, left of the button: its text ops start inside the label's room and stay clear of the button
        const Rect room = playrows::labelRect (disp, false, false);
        bool label = false, clear = true;
        for (uint32_t k = 0; k < s.data->numOps; ++k)
            if (s.data->ops[k].op == gpu::kOpText && gpu::textString (*s.data, s.data->ops[k]).find ("LFO 1") == 0)
            {
                const float x = s.data->ops[k].f[0];
                label = true;
                clear = clear && x >= static_cast<float> (room.x) - 0.5f && x < static_cast<float> (room.right());
            }
        check (label && clear && room.right() <= fb.x, nm + "the label starts in its room of the band, left of the full screen button");
    }
    // the full page and the shape editor keep their rectangle (the compact rule is for the narrow display of the third only)
    lfodisplay::Data d;
    d.kind = lfodisplay::Data::Kind::Shape; d.y = y; d.n = 256;
    const Rect wide { 20, 20, 700, 300 };
    check (lfodisplay::plotRect (wide, d) == Rect { wide.x + 8, wide.y + 8 + 6, wide.w - 16, wide.h - 16 - 12 }, "full page display: the curve's rectangle is unchanged (inset 8, 6 px padding)");
    d.editing = true;
    check (lfodisplay::plotRect (Rect { 20, 20, 400, 300 }, d) == Rect { 28, 34, 384, 284 - 12 }, "shape editor canvas (editing): the curve's rectangle is unchanged even in a narrow canvas");
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
    for (int variant = 0; variant < 4; ++variant)
    {
        const bool random = variant >= 2, keyFollow = variant == 1 || variant == 3;
        LfoFullScreen f;
        f.place ({ 0, 110, 1280, 628 });
        f.configure (random);
        f.setKeyActive (keyFollow);
        const std::string nm = std::string ("full screen ") + (random ? (keyFollow ? "random, key follow" : "random") : (keyFollow ? "LFO, key follow" : "LFO"));
        checkView (f, { 0, 110, 1280, 628 }, nm, f.groups, f.numGroups(), 0x0b1026ffu);
        f.setActiveGroup (0);
        // the groups in reading order: RATE, [KEY], TIMING, MOTION, SHAPE; RANDOM, [KEY], TRIGGER (the KEY group only in the key follow sync: its two numbers stand where the rate knob stands)
        check (f.numGroups() == (random ? (keyFollow ? 3 : 2) : (keyFollow ? 5 : 4)) && f.activeGroup() == 0 && f.groups[0].active(), nm + ": " + std::to_string (f.numGroups()) + " groups, the first active");
        check (f.numBlocks() == (random ? 1 : 3), nm + ": " + std::to_string (f.numBlocks()) + " blocks (the key follow has no block of its own)");
        check (f.keyGroup() == (keyFollow ? 1 : -1) && (! keyFollow || (f.groups[1].member (0) == &f.keyTranspose && f.groups[1].member (1) == &f.keyTune && f.groups[1].numMembers() == 2)), nm + ": the KEY group (TRANSPOSE, TUNE) is the second group in the key follow sync and there only");
        {
            bool shownRate = false;
            for (int c = 0; c < f.numChildren(); ++c)
                shownRate = shownRate || (f.child (c) == &f.rate && f.childShown (c));
            check (shownRate == ! keyFollow, nm + ": the rate control is on the page exactly when the rate is not the key's (no dead dial)");
            if (keyFollow)
            {
                const Rect b0 = f.block (0);
                check (b0.contains (f.keyTranspose.rect()) && b0.contains (f.keyTune.rect()) && f.keyTranspose.rect().h >= 56 && f.keyTune.rect().h >= 56 && ! f.keyTranspose.rect().overlaps (f.keyTune.rect()) && f.keyTranspose.rect().w >= 120,
                       nm + ": TRANSPOSE and TUNE stand in the first block, one above the other, >= 56 px, apart");
                if (! random)
                    check (! f.keyTranspose.rect().overlaps (f.rise.rect()) && ! f.keyTune.rect().overlaps (f.smoothTime.rect()) && f.keyTranspose.rect().right() <= f.rise.rect().x && f.keyTune.rect().right() <= f.smoothTime.rect().x, nm + ": and clear of RISE and SMOOTH");
            }
        }
        check (! f.displayRect().empty() && f.displayRect().w > 800 && f.displayRect().h >= 200, nm + ": the display " + std::to_string (f.displayRect().w) + " x " + std::to_string (f.displayRect().h));
        check (f.displayRect().right() <= f.picker.rect().x && f.picker.rect().right() <= 1280, nm + ": the picker at the right of the display");
        for (int b = 0; b < f.numBlocks(); ++b)
            check (f.block (b).y >= f.displayRect().bottom() && f.block (b).bottom() <= 738, nm + ": block " + std::to_string (b) + " under the display, inside the tab");
        // the Q frame moves to a tapped group
        if (! random)
        {
            const int iT = keyFollow ? 2 : 1, iM = iT + 1, iS = iT + 2;   // TIMING, MOTION, SHAPE
            check (f.groups[0].member (0) == &f.picker && f.groups[0].member (1) == (keyFollow ? nullptr : static_cast<Component*> (&f.rate)) &&
                   f.groups[0].member (2) == &f.rise && f.groups[0].member (3) == &f.smoothTime,
                   nm + ": group 0 is picker (Q1), rate (not in the key follow), rise, smooth");
            check (f.groups[iT].member (0) == &f.syncSwitch.mode && f.groups[iT].member (1) == &f.phase &&
                   f.groups[iT].member (2) == &f.delay && f.groups[iT].member (3) == &f.mode,
                   nm + ": group 1 is the HZ / BPM / KEY list, phase, delay, mode (the reading order of the block; no second SYNC stepper)");
            check (f.syncSwitch.mode.rect().y == f.phase.rect().y && f.phase.rect().y < f.delay.rect().y && f.delay.rect().y < f.mode.rect().y && f.mode.rect().x == f.syncSwitch.mode.rect().x, nm + ": the chips of TIMING read top to bottom: SYNC and PHASE on one line, DELAY, MODE");
            check (f.direction.rect().y == f.swing.rect().y && f.swing.rect().y < f.stereo.rect().y && f.stereo.rect().y < f.monoQ.rect().y, nm + ": the chips of MOTION read top to bottom: DIRECTION and SWING on one line, STEREO, MONO");
            check (f.hostPick.rect().h >= 56 && f.hostPick.rect().y > f.picker.rect().bottom() && f.bar.rect().h >= 56 && InstanceBar::kCellW >= 56, nm + ": HOST is a LED toggle under the picker (SMOOTH is the LED on the label of its time knob), the instance cells are 56 px or more");
            check (f.smoothTime.state.led && f.smoothTime.ledHit().h >= 56 && f.smoothTime.ledHit().w >= 80 && f.rect().contains (f.smoothTime.ledHit()), nm + ": the SMOOTH knob has the LED on its label and a switch zone of at least 56 px");
            check (f.groups[iM].member (0) == &f.direction && f.groups[iM].member (1) == &f.swing &&
                   f.groups[iM].member (2) == &f.stereo && f.groups[iM].member (3) == &f.monoQ,
                   nm + ": group MOTION is direction, swing, stereo, mono");
            check (f.groups[iS].member (0) == &f.hostPick && f.groups[iS].member (1) == &f.smoothSwitch && f.groups[iS].numMembers() == 2,
                   nm + ": group SHAPE is host and the Q member of the SMOOTH switch (the LED on the label of the SMOOTH knob, which is a member of the first group)");
            check (f.smoothSwitch.rect().y >= f.smoothTime.rect().y && f.smoothSwitch.rect().x > f.smoothTime.rect().x && f.smoothTime.rect().contains (f.smoothSwitch.rect()) && f.smoothSwitch.touchDown (f.smoothSwitch.rect().x + 2, f.smoothSwitch.rect().y + 2, 0.0).kind == ChangeKind::None,
                   nm + ": the switch's Q member stands on the SMOOTH knob and takes no touch (the label is the touch)");
            check (f.rate.rate.state.large, nm + ": rate knob is large");
            check (f.rise.rect().h == f.smoothTime.rect().h && f.rise.rect().h == f.phase.rect().h &&
                   f.rise.rect().h == f.delay.rect().h && f.rise.rect().h == f.swing.rect().h &&
                   f.rise.rect().h == f.stereo.rect().h,
                   nm + ": one knob size family for rise, smooth, phase, delay, swing, stereo");
            f.touchDown (cx (f.direction), cy (f.direction), 0.0);
            f.touchUp (cx (f.direction), cy (f.direction));
            check (f.activeGroup() == iM && f.groups[iM].active() && ! f.groups[0].active(), nm + ": a touch on DIRECTION makes MOTION the active group");
            // tap on TIMING block background
            f.touchDown (f.block (1).x + 10, f.block (1).y + 10, 0.0);
            f.touchUp (f.block (1).x + 10, f.block (1).y + 10);
            check (f.activeGroup() == iT && f.groups[iT].active(), nm + ": tap on TIMING block makes it active");
            // a tap on hostPick activates the SHAPE group
            f.touchDown (cx (f.hostPick), cy (f.hostPick), 0.0);
            f.touchUp (cx (f.hostPick), cy (f.hostPick));
            check (f.activeGroup() == iS && f.groups[iS].active(), nm + ": tap on HOST makes SHAPE active");
            f.touchDown (cx (f.keyTune), cy (f.keyTune), 0.0);
            f.touchUp (cx (f.keyTune), cy (f.keyTune));
            check (keyFollow == (f.activeGroup() == 1), nm + ": a touch on TUNE makes KEY the active group in the key follow sync (and is a touch on nothing without it)");
            check (f.keyTranspose.enabled() == keyFollow, nm + ": the KEY numbers are there in the key follow sync only");
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
    pg.canvas.state.gridX = 8; pg.canvas.state.gridY = 8;
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
    check (pg.groups[0].check (all, n, &off) == 0 && chipsCovered (pg.groups[0], all, n) == 0, "editor page: the EDIT group (POINT, X, Y, CURVE) is adjacent, free lanes for the chips");
    check (pg.groups[1].check (all, n, &off) == 0 && chipsCovered (pg.groups[1], all, n) == 0 && pg.groups[1].member (0) == &pg.fgx && pg.groups[1].member (1) == &pg.fgy && pg.groups[1].member (2) == &pg.snap,
           "editor page: the GRID group (GRID X, GRID Y, SNAP) is adjacent, free lanes for the chips");
    {
        bool big = true;
        for (const Component* c : std::initializer_list<const Component*> { &pg.fpoint, &pg.fx, &pg.fy, &pg.fcurve, &pg.fgx, &pg.fgy, &pg.snap, &pg.undoButton, &pg.del, &pg.loop, &pg.smooth, &pg.back })
            big = big && c->rect().h >= 56 && c->rect().w >= 56;
        check (big, "editor page: every control is at least 56 x 56 (touch targets)");
        bool once = true;   // no control twice: SNAP is only in the GRID group, not in a bar as well
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                once = once && all[i] != all[j];
        check (once && pg.tools.rect().y == pg.back.rect().y, "editor page: no control twice, the tools in the top bar");
    }
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

// a shape of five points on a grid of quarters: (0, 0) (0.25, 0.5) (0.5, 1) (0.75, 0.5) (1, 0)
lfo::ShapeModel fivePoints()
{
    lfo::ShapeModel m;
    m.n = 5;
    m.p[0] = lfo::Point (0.0f, 0.0f); m.p[1] = lfo::Point (0.25f, 0.5f); m.p[2] = lfo::Point (0.5f, 1.0f); m.p[3] = lfo::Point (0.75f, 0.5f); m.p[4] = lfo::Point (1.0f, 0.0f);
    return m;
}
bool sameShape (const lfo::ShapeModel& a, const lfo::ShapeModel& b, float tol, int samples = 97)
{
    for (int i = 0; i < samples; ++i)
    {
        const float x = (static_cast<float> (i) + 0.5f) / static_cast<float> (samples);
        if (std::fabs (a.eval (x) - b.eval (x)) > tol)
            return false;
    }
    return true;
}

void testSelectionModel()
{
    // the point set follows an insert and a removal
    lfo::PointSet s;
    s.add (1); s.add (3);
    check (s.count() == 2 && s.has (1) && s.has (3) && ! s.has (2) && s.first() == 1 && s.last() == 3, "point set: add, has, count, first, last");
    s.onInsert (2);
    check (s.has (1) && ! s.has (2) && s.has (4) && ! s.has (3), "point set: an insert at 2 moves point 3 to 4");
    s.onRemove (1);
    check (! s.has (1) && s.has (3) && s.count() == 1, "point set: removing point 1 moves point 4 to 3");
    // moving a group: one move for all, clamped as a whole (the form of the selection stays, the order of the points stays)
    lfo::ShapeModel m = fivePoints();
    lfo::PointSet g;
    g.add (1); g.add (2);
    float dx = 0.5f, dy = 0.3f;
    check (m.moveSet (g, dx, dy) && near (dx, 0.25f) && near (dy, 0.0f) && near (m.p[1].x, 0.5f) && near (m.p[2].x, 0.75f) && near (m.p[1].y, 0.5f) && near (m.p[2].y, 1.0f), "moveSet: x is limited by the neighbour that stays, y by the highest point (the form stays)");
    m = fivePoints();
    dx = -0.1f; dy = -0.3f;
    check (m.moveSet (g, dx, dy) && near (m.p[1].x, 0.15f) && near (m.p[2].x, 0.4f) && near (m.p[1].y, 0.2f) && near (m.p[2].y, 0.7f), "moveSet: both points move by the same amount");
    lfo::PointSet ends;
    ends.add (0); ends.add (4);
    m = fivePoints();
    dx = 0.2f; dy = 0.3f;
    check (m.moveSet (ends, dx, dy) && near (dx, 0.0f) && near (m.p[0].x, 0.0f) && near (m.p[4].x, 1.0f) && near (m.p[0].y, 0.3f) && near (m.p[4].y, 0.3f), "moveSet: the end points move only up and down");
    // scaling a group around its centre
    m = fivePoints();
    lfo::PointSet mid;
    mid.add (1); mid.add (2); mid.add (3);
    check (m.scaleSetY (mid, 0.25f) && near (m.p[1].y, 0.625f) && near (m.p[2].y, 0.875f) && near (m.p[3].y, 0.625f), "scaleSetY: the height 0.5 becomes 0.25 around the centre 0.75");
    m = fivePoints();
    lfo::PointSet level;
    level.add (1); level.add (3);
    check (! m.scaleSetY (level, 0.9f), "scaleSetY: a selection that has no height cannot be scaled");
    // removing a selection: the loop point follows
    m = fivePoints();
    m.loop = 3;
    lfo::PointSet del;
    del.add (1); del.add (2); del.add (0);
    check (m.removeSet (del) == 2 && m.n == 3 && m.loop == 1 && near (m.p[1].x, 0.75f), "removeSet: the inner points go, the end point stays, the loop point is still the same point");
    // a double removal of the ends does nothing
    del = lfo::PointSet {};
    del.add (0); del.add (m.n - 1);
    check (m.removeSet (del) == 0 && m.n == 3, "removeSet: the end points never go");
    // falloff
    m = fivePoints();
    float w[lfo::kMaxPoints];
    check (m.falloffWeights (2, nullptr, 0.25f, w) == 1 && near (w[2], 1.0f) && near (w[1], 0.0f), "falloff: nothing within a radius that reaches no neighbour but the anchor");
    check (m.falloffWeights (2, nullptr, 0.5f, w) == 3 && near (w[1], 0.5f) && near (w[3], 0.5f) && near (w[0], 0.0f), "falloff: the neighbours at half the radius follow with half");
    lfo::PointSet three;
    three.add (1); three.add (2); three.add (3);
    check (m.falloffWeights (2, &three, 0.25f, w) == 3 && near (w[2], 1.0f) && near (w[1], 0.2f) && near (w[0], 0.0f) && near (w[4], 0.0f), "falloff: a selection: only the selected points, the farthest one with 0.2");
    m.moveWeighted (w, 0.0f, -0.25f);
    check (near (m.p[2].y, 0.75f) && near (m.p[1].y, 0.45f) && near (m.p[3].y, 0.45f) && near (m.p[0].y, 0.0f), "moveWeighted: the points move by their share");
    // a time move that would cross the neighbour is repaired: the order stays
    m = fivePoints();
    for (int i = 0; i < 5; ++i) w[i] = i == 1 ? 1.0f : (i == 2 ? 0.1f : 0.0f);
    m.moveWeighted (w, 0.6f, 0.0f);
    check (m.p[1].x <= m.p[2].x + 1.0e-6f && m.p[2].x <= m.p[3].x + 1.0e-6f, "moveWeighted: the order of the points is kept");
}

void testWholeShapeModel()
{
    // flip in time: the shape mirrors, the curves with it; twice is the identity
    lfo::ShapeModel m;
    m.n = 3;
    m.p[0] = lfo::Point (0.0f, 0.0f, 4.0f); m.p[1] = lfo::Point (0.25f, 1.0f, -2.0f); m.p[2] = lfo::Point (1.0f, 0.5f, 0.0f);
    m.loop = 0;
    const lfo::ShapeModel orig = m;
    lfo::ShapeModel f = m;
    f.flipH();
    bool mirror = true;
    for (int i = 0; i < 97; ++i)
    {
        const float x = (static_cast<float> (i) + 0.5f) / 97.0f;
        mirror = mirror && std::fabs (f.eval (x) - orig.eval (1.0f - x)) < 1.0e-4f;
    }
    check (mirror && f.loop == 2 && near (f.p[0].x, 0.0f) && near (f.p[2].x, 1.0f) && near (f.p[0].power, 2.0f) && near (f.p[1].power, -4.0f), "flipH: the shape and its curves are mirrored (power changes sign, the loop point is the mirrored point)");
    f.flipH();
    check (f == orig, "flipH twice is the identity");
    lfo::ShapeModel v = orig;
    v.flipV();
    bool inverse = true;
    for (int i = 0; i < 97; ++i)
    {
        const float x = (static_cast<float> (i) + 0.5f) / 97.0f;
        inverse = inverse && std::fabs (v.eval (x) - (1.0f - orig.eval (x))) < 1.0e-4f;
    }
    check (inverse, "flipV: every level is 1 - y, the curves stay");
    v.flipV();
    check (v == orig, "flipV twice is the identity");
    // the modulation of a point is part of the shape and follows the flips
    lfo::ShapeModel mm = orig;
    lfo::PointSet one;
    one.add (1);
    mm.setMod (one, 1, 7, 0.5f);
    check (mm.p[1].my.source == 7 && near (mm.p[1].my.amount, 0.5f) && ! (mm == orig), "setMod: the level of the point is modulated by source 7 with 0.5");
    mm.flipV();
    check (near (mm.p[1].my.amount, -0.5f), "flipV: the modulation of the level changes sign");
    mm.setMod (one, 1, 0, 0.0f);
    mm.flipV();
    check (mm == orig, "setMod with no source removes the modulation");
    // shift: the shape turns, nothing is lost, the curves of the cut segment are those of the original
    lfo::ShapeModel c;
    c.n = 3;
    c.p[0] = lfo::Point (0.0f, 0.0f, 3.0f); c.p[1] = lfo::Point (0.5f, 1.0f, -2.0f); c.p[2] = lfo::Point (1.0f, 0.0f, 0.0f);
    for (float d : { 0.25f, 0.7f, 0.5f, 0.125f })
    {
        lfo::ShapeModel t = c;
        bool ok = t.shift (d) && t.n >= 3 && t.n <= lfo::kMaxPoints && near (t.p[0].x, 0.0f) && near (t.p[t.n - 1].x, 1.0f);
        bool same = true;
        for (int i = 0; i < 197; ++i)
        {
            const float x = (static_cast<float> (i) + 0.5f) / 197.0f;
            float src = x - d;
            src -= std::floor (src);
            same = same && std::fabs (t.eval (x) - c.eval (src)) < 3.0e-3f;
        }
        check (ok && same, "shift by " + std::to_string (d) + ": the shape is the same turned (" + std::to_string (t.n) + " points)");
    }
    lfo::ShapeModel sq = lfo::ShapeModel::fromBuiltin (4);   // (0, 1) (0.5, 1) (0.5, 0) (1, 0): the step is at the cut
    check (sq.shift (0.5f) && near (sq.eval (0.25f), 0.0f) && near (sq.eval (0.75f), 1.0f) && near (sq.p[0].x, 0.0f) && near (sq.p[sq.n - 1].x, 1.0f), "shift: a vertical step at the cut makes the shape start and end with the halves");
    lfo::ShapeModel z = c;
    check (! z.shift (0.0f) && ! z.shift (1.0f) && z == c, "shift by a whole cycle is no shift");
    lfo::ShapeModel loopShape = c;
    loopShape.loop = 1;
    loopShape.shift (0.25f);
    check (loopShape.loop >= 0 && near (loopShape.p[loopShape.loop].y, 1.0f), "shift: the loop point stays on its point (the peak)");
    // bending all curves
    lfo::ShapeModel b;
    b.n = 4;
    b.p[0] = lfo::Point (0.0f, 0.0f, 2.0f); b.p[1] = lfo::Point (0.3f, 1.0f, -4.0f); b.p[2] = lfo::Point (0.6f, 1.0f, 7.0f); b.p[3] = lfo::Point (1.0f, 0.0f);
    check (near (b.meanCurve(), 4.5f) && b.bendable (0) && ! b.bendable (1) && b.bendable (2), "meanCurve: the mean of the segments that can be bent (the flat one has no curve and does not count)");
    check (b.bendAll (3.0f) && near (b.p[0].power, 5.0f) && near (b.p[1].power, -4.0f) && near (b.p[2].power, 10.0f), "bendAll: every bendable curve moves by the same amount, the flat segment stays");
    check (b.bendAll (100.0f) && near (b.p[0].power, 20.0f) && near (b.p[2].power, 20.0f) && near (b.p[1].power, -4.0f), "bendAll: limited to 20");
    check (b.resetCurves() && near (b.p[0].power, 0.0f) && near (b.p[1].power, 0.0f) && near (b.p[2].power, 0.0f) && ! b.resetCurves(), "resetCurves: every segment straight");
    check (b.nearestByX (0.62f) == 2 && b.nearestByX (0.1f) == 0, "nearestByX");
}

// the canvas on its page: the gestures
struct CanvasRig
{
    ShapeEditorPage pg;
    ShapeCanvas& cv = pg.canvas;
    double t = 0.0;
    CanvasRig()
    {
        pg.place ({ 0, 110, 1280, 628 });
        pg.canvas.setId (7);
        pg.model = fivePoints();
        cv.state.gridX = cv.state.gridY = 8;
        cv.state.snap = false;
        pg.sync();
    }
    int X (float f) const { return static_cast<int> (std::lround (cv.xOf (f))); }
    int Y (float f) const { return static_cast<int> (std::lround (cv.yOf (f))); }
    Change tapAt (int x, int y, double gapMs = 100.0)
    {
        t += gapMs;
        cv.touchDown (x, y, t);
        t += 40.0;
        return cv.touchUp (x, y);
    }
    Change dragTo (int x0, int y0, int x1, int y1, int steps = 4)
    {
        t += 500.0;
        Change last = cv.touchDown (x0, y0, t);
        for (int i = 1; i <= steps; ++i)
        {
            t += 16.0;
            const Change c = cv.touchMove (x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
            if (c) last = c;
        }
        const Change u = cv.touchUp (x1, y1);
        return u ? u : last;
    }
};

void testCanvasQuickDelete()
{
    CanvasRig r;
    r.cv.state.tool = ToolSelector::Point;
    const lfo::ShapeModel start = r.pg.model;
    // a double tap on an inner point deletes it: ONE undo state, the loop point follows
    r.pg.model.loop = 3;
    const lfo::ShapeModel withLoop = r.pg.model;
    r.tapAt (r.X (0.25f), r.Y (0.5f));
    check (r.pg.model.n == 5 && r.cv.state.selected == 1, "double tap: the first tap only selects the point");
    const Change c = r.tapAt (r.X (0.25f), r.Y (0.5f), 150.0);
    check (c.kind == ChangeKind::Index && c.index == ShapeCanvas::Edited && r.pg.model.n == 4 && near (r.pg.model.p[1].x, 0.5f) && r.pg.model.loop == 2 && r.cv.state.selected == -1, "double tap: the second tap deletes the point and the loop point is still on its point");
    check (r.pg.undo.depth() == 1, "double tap: one undo state");
    lfo::ShapeModel back;
    check (r.pg.undo.pop (back) && back == withLoop, "double tap: undo restores the shape and the loop point exactly");
    // the end points are never deleted
    r.pg.model = start;
    r.tapAt (r.X (0.0f), r.Y (0.0f));
    r.tapAt (r.X (0.0f), r.Y (0.0f), 150.0);
    r.tapAt (r.X (1.0f), r.Y (0.0f), 1000.0);
    r.tapAt (r.X (1.0f), r.Y (0.0f), 150.0);
    check (r.pg.model == start, "double tap: the end points stay");
    // two slow taps are no double tap
    r.tapAt (r.X (0.25f), r.Y (0.5f), 1000.0);
    r.tapAt (r.X (0.25f), r.Y (0.5f), 1000.0);
    check (r.pg.model.n == 5, "two taps a second apart do not delete");
    // a double tap on empty ground: the first tap adds a point, the second keeps it (no delete of the point just added)
    r.pg.model = start;
    r.pg.undo.clear();
    r.tapAt (r.X (0.375f), r.Y (0.9f), 1000.0);
    const int nAfterFirst = r.pg.model.n;
    r.tapAt (r.X (0.375f), r.Y (0.9f), 120.0);
    check (nAfterFirst == 6 && r.pg.model.n == 6, "double tap on empty ground: one point is added and it stays");
    // a double tap on a curve handle sets the curve straight
    r.pg.model = lfo::ShapeModel::fromBuiltin (2);
    r.pg.model.p[0].power = 5.0f;
    r.pg.undo.clear();
    float hx, hy;
    check (r.cv.midHandle (0, hx, hy), "the segment has a handle");
    r.tapAt (static_cast<int> (hx), static_cast<int> (hy), 1000.0);
    check (near (r.pg.model.p[0].power, 5.0f) && r.cv.state.selectedSeg == 0, "a single tap on the handle selects the segment");
    const Change d = r.tapAt (static_cast<int> (hx), static_cast<int> (hy), 120.0);
    check (d.index == ShapeCanvas::Edited && near (r.pg.model.p[0].power, 0.0f) && r.pg.undo.depth() == 1, "a double tap on the curve handle resets the curve (one undo state)");
    // a disabled canvas does nothing
    r.pg.model = start;
    r.cv.setEnabled (false);
    r.tapAt (r.X (0.375f), r.Y (0.9f), 1000.0);
    check (r.pg.model == start, "a disabled canvas changes nothing");
}

void testCanvasStepDrawing()
{
    CanvasRig r;
    r.cv.state.snap = true;
    r.cv.state.tool = ToolSelector::Flat;
    r.pg.model.initLinear();
    // ONE move from column 0 to column 5: every column in between is drawn at the interpolated height, snapped to the rows
    r.t = 100.0;
    r.cv.touchDown (r.X (0.0625f), r.Y (0.5f), r.t);
    r.cv.touchMove (r.X (5.5f / 8.0f), r.Y (0.75f));
    r.cv.touchUp (0, 0);
    bool all = true;
    std::string got;
    for (int col = 0; col <= 5; ++col)
    {
        const float want = lfo::snapTo (0.5f + 0.25f * static_cast<float> (col) / 5.0f, 8);
        const float at = r.pg.model.eval ((static_cast<float> (col) + 0.5f) / 8.0f);
        all = all && near (at, want, 0.02f);
        got += std::to_string (at).substr (0, 4) + " ";
    }
    check (all, "flat tool, one fast move: all six columns are drawn, none skipped (" + got + ")");
    check (r.pg.undo.depth() == 1, "flat tool: the whole gesture is one undo state");
    lfo::ShapeModel back;
    lfo::ShapeModel line;
    line.initLinear();
    check (r.pg.undo.pop (back) && back == line, "flat tool: undo gives the shape before the gesture");
    // the finger back to the left in one move: the columns it crosses are drawn again
    r.pg.model.initLinear();
    r.pg.undo.clear();
    r.cv.touchDown (r.X (0.4375f), r.Y (0.25f), 200.0);
    r.cv.touchMove (r.X (0.0625f), r.Y (0.25f));
    r.cv.touchUp (0, 0);
    bool left = true;
    for (int col = 0; col <= 3; ++col)
        left = left && near (r.pg.model.eval ((static_cast<float> (col) + 0.5f) / 8.0f), 0.25f, 0.02f);
    check (left, "flat tool, a fast move to the left: no gap");
    // the height changes inside one column: the piece follows the finger
    r.pg.model.initLinear();
    r.cv.touchDown (r.X (0.0625f), r.Y (0.25f), 300.0);
    r.cv.touchMove (r.X (0.0625f), r.Y (0.75f));
    r.cv.touchUp (0, 0);
    check (near (r.pg.model.eval (0.06f), 0.75f, 0.02f), "flat tool: moving up inside the column raises the piece");
    // without SNAP the height is the finger's
    r.cv.state.snap = false;
    r.pg.model.initLinear();
    r.cv.touchDown (r.X (0.0625f), r.Y (0.6f), 400.0);
    r.cv.touchUp (0, 0);
    check (near (r.pg.model.eval (0.06f), 0.6f, 0.01f), "flat tool without SNAP: the height of the finger");
    // ramps: a saw tooth over four columns in one move
    r.cv.state.snap = true;
    r.cv.state.tool = ToolSelector::RampUp;
    r.pg.model.initLinear();
    r.pg.undo.clear();
    r.cv.touchDown (r.X (0.0625f), r.Y (0.3f), 500.0);
    r.cv.touchMove (r.X (3.5f / 8.0f), r.Y (0.3f));
    r.cv.touchUp (0, 0);
    bool saw = true;
    for (int col = 0; col <= 3; ++col)
        saw = saw && near (r.pg.model.eval ((static_cast<float> (col) + 0.01f) / 8.0f), 0.25f, 0.02f) && near (r.pg.model.eval ((static_cast<float> (col) + 0.99f) / 8.0f), 0.375f, 0.02f);
    check (saw && r.pg.undo.depth() == 1, "ramp tool, one fast move: four ramps of the same row, one undo state");
    // 128 points are the limit: the drawing stops, nothing breaks
    r.pg.model.initLinear();
    for (int i = 1; i < 127; ++i) r.pg.model.insert (static_cast<float> (i) / 127.0f, (i % 2) != 0 ? 0.2f : 0.8f);
    check (r.pg.model.n == lfo::kMaxPoints, "the shape is full");
    const lfo::ShapeModel full = r.pg.model;
    r.cv.state.tool = ToolSelector::Flat;
    r.tapAt (r.X (0.5f), r.Y (0.5f), 1000.0);
    check (r.pg.model.n <= lfo::kMaxPoints && (r.pg.model == full || r.pg.model.n < lfo::kMaxPoints), "a full shape: the cell tool never goes above 128 points");
}

void testCanvasSelection()
{
    CanvasRig r;
    r.pg.model = fivePoints();
    r.pg.model.insert (0.125f, 0.2f);   // six points: 0, 0.125, 0.25, 0.5, 0.75, 1
    r.cv.state.tool = ToolSelector::Select;
    // a rectangle over the points 1 and 2 (x 0.125 and 0.25)
    r.dragTo (r.X (0.06f), r.Y (0.05f), r.X (0.30f), r.Y (0.75f), 6);
    lfo::PointSet s = r.cv.selection();
    check (s.count() == 2 && s.has (1) && s.has (2) && r.cv.state.marks.count() == 2, "SELECT: a rectangle selects the points inside it");
    r.pg.sync();
    check (std::strcmp (r.pg.fpoint.state.text, "2 SEL") == 0 && ! r.pg.fpoint.state.notSet && r.pg.del.enabled() && r.pg.fcurve.state.label[0] == 'S' && r.pg.modButton.enabled(), "the page: the POINT field shows the count, the third field is SCALE, DELETE and MOD are on");
    check (near (static_cast<float> (r.pg.fx.state.value), 0.1875f) && near (static_cast<float> (r.pg.fcurve.state.value), 0.3f, 1.0e-3f), "the page: X is the centre of the selection in time");
    // a tap on another point adds it, a tap on a selected one removes it
    r.tapAt (r.X (0.5f), r.Y (1.0f), 1000.0);
    s = r.cv.selection();
    check (s.count() == 3 && s.has (3), "SELECT: a tap on a point adds it to the selection");
    r.tapAt (r.X (0.125f), r.Y (0.2f), 1000.0);
    s = r.cv.selection();
    check (s.count() == 2 && ! s.has (1) && s.has (2) && s.has (3), "SELECT: a tap on a selected point takes it out");
    r.tapAt (r.X (0.5f), r.Y (1.0f), 1000.0);
    s = r.cv.selection();
    check (s.count() == 1 && s.has (2) && r.cv.state.selected == 2 && ! r.cv.state.marks.any(), "SELECT: with one point left the selection is that point");
    // a tap on empty ground clears
    r.tapAt (r.X (0.9f), r.Y (0.9f), 1000.0);
    check (! r.cv.selection().any() && r.cv.state.selected == -1, "SELECT: a tap on empty ground clears the selection");
    // select all by the owner's call
    r.cv.selectAll();
    check (r.cv.selection().count() == 6, "selectAll: every point");
    // moving the selection by dragging one of its points (tool POINT too): all move by the same amount, one undo state
    r.pg.undo.clear();
    r.cv.selectOnly (-1);
    r.cv.state.marks.add (1);
    r.cv.state.marks.add (2);
    r.cv.state.selected = 1;
    r.cv.state.tool = ToolSelector::Point;
    const lfo::ShapeModel before = r.pg.model;
    const Change c = r.dragTo (r.X (0.125f), r.Y (0.2f), r.X (0.125f), r.Y (0.5f), 5);
    check (c.index == ShapeCanvas::Edited && near (r.pg.model.p[1].y - before.p[1].y, r.pg.model.p[2].y - before.p[2].y, 0.01f) && r.pg.model.p[1].y > before.p[1].y + 0.2f && near (r.pg.model.p[3].y, before.p[3].y), "dragging a selected point moves the whole selection by the same amount");
    check (r.pg.undo.depth() == 1 && r.cv.selection().count() == 2, "the group move is one undo state and keeps the selection");
    lfo::ShapeModel back;
    check (r.pg.undo.pop (back) && back == before, "undo restores the group move exactly");
    // the DELETE of the page works on the selection (the owner's call: removeSet)
    lfo::ShapeModel d = r.pg.model;
    check (d.removeSet (r.cv.selection()) == 2 && d.n == 4, "the selection deletes in one call");
}

void testCanvasSoftAndBend()
{
    CanvasRig r;
    r.pg.model.n = 6;
    for (int i = 0; i < 6; ++i)
        r.pg.model.p[i] = lfo::Point (static_cast<float> (i) * 0.2f, 0.3f);
    r.cv.state.tool = ToolSelector::Soft;
    r.pg.undo.clear();
    const lfo::ShapeModel before = r.pg.model;
    const Change c = r.dragTo (r.X (0.4f), r.Y (0.3f), r.X (0.4f), r.Y (0.7f), 5);
    const float d2 = r.pg.model.p[2].y - before.p[2].y, d1 = r.pg.model.p[1].y - before.p[1].y, d3 = r.pg.model.p[3].y - before.p[3].y, d0 = r.pg.model.p[0].y - before.p[0].y;
    check (c.index == ShapeCanvas::Edited && d2 > 0.3f && d1 > 0.0f && d1 < d2 && d3 > 0.0f && d3 < d2 && near (d0, 0.0f), "SOFT: the neighbours follow, the nearer the more, the far ones stay (" + std::to_string (d1) + " " + std::to_string (d2) + ")");
    check (r.pg.undo.depth() == 1, "SOFT: one undo state");
    // a selection of three: only they follow
    r.pg.model = before;
    r.cv.state.marks.clear();
    r.cv.state.marks.add (1); r.cv.state.marks.add (2); r.cv.state.marks.add (3);
    r.cv.state.selected = 2;
    r.dragTo (r.X (0.4f), r.Y (0.3f), r.X (0.4f), r.Y (0.6f), 5);
    check (r.pg.model.p[2].y > 0.5f && r.pg.model.p[1].y > 0.3f && near (r.pg.model.p[4].y, 0.3f) && near (r.pg.model.p[0].y, 0.3f), "SOFT with a selection: only the selected points follow");
    // BEND: all curves by the same amount
    r.cv.state.marks.clear();
    r.cv.selectOnly (-1);
    r.pg.model.n = 4;
    r.pg.model.p[0] = lfo::Point (0.0f, 0.0f, 2.0f); r.pg.model.p[1] = lfo::Point (0.3f, 1.0f, -3.0f); r.pg.model.p[2] = lfo::Point (0.6f, 0.2f, 0.0f); r.pg.model.p[3] = lfo::Point (1.0f, 0.9f, 0.0f);
    r.pg.undo.clear();
    r.cv.state.tool = ToolSelector::Bend;
    float hx, hy;
    check (r.cv.midHandle (0, hx, hy), "segment 0 has a handle");
    const lfo::ShapeModel b0 = r.pg.model;
    r.dragTo (static_cast<int> (hx), static_cast<int> (hy), static_cast<int> (hx), static_cast<int> (hy) + 60, 4);
    const float delta = r.pg.model.p[0].power - b0.p[0].power;
    check (std::fabs (delta) > 0.5f && near (r.pg.model.p[1].power - b0.p[1].power, delta, 1.0e-3f) && near (r.pg.model.p[2].power - b0.p[2].power, delta, 1.0e-3f), "BEND: every curve moves by the amount of the one that was dragged (" + std::to_string (delta) + ")");
    check (r.pg.undo.depth() == 1, "BEND: one undo state");
    // a touch on empty ground with SOFT or BEND adds nothing
    const lfo::ShapeModel kept = r.pg.model;
    r.tapAt (r.X (0.9f), r.Y (0.5f), 1000.0);
    r.cv.state.tool = ToolSelector::Soft;
    r.tapAt (r.X (0.8f), r.Y (0.8f), 1000.0);
    check (r.pg.model == kept, "BEND and SOFT: empty ground adds no point");
}

void testCanvasMarker()
{
    CanvasRig r;
    r.pg.model.n = 4;
    r.pg.model.p[0] = lfo::Point (0.0f, 0.0f); r.pg.model.p[1] = lfo::Point (0.5f, 0.2f); r.pg.model.p[2] = lfo::Point (0.75f, 0.2f); r.pg.model.p[3] = lfo::Point (1.0f, 0.0f);
    r.cv.state.tool = ToolSelector::Point;
    r.cv.state.markerKind = ShapeCanvas::MarkerLoop;
    r.cv.state.markerX = 0.5f;
    const Rect zone = r.cv.markerZone();
    check (zone.w >= 56 && zone.h >= 56 && zone.contains (r.X (0.5f), r.cv.plot().y + 20), "the marker has a handle of at least 56 px at the top of its line");
    const Change c = r.dragTo (r.X (0.5f), r.cv.plot().y + 20, r.X (0.72f), r.cv.plot().y + 20, 5);
    check (c.kind == ChangeKind::Index && c.index == ShapeCanvas::Marker && near (static_cast<float> (c.value), 0.75f, 1.0e-3f) && near (r.cv.state.markerX, 0.75f, 1.0e-3f), "LOOP marker: dragging it snaps to the nearest point (0.75)");
    check (r.pg.model.loop == 2 && r.pg.undo.depth() == 1 && r.pg.model.n == 4, "LOOP marker: the loop point of the model is that point, one undo state, no point added");
    lfo::ShapeModel back;
    check (r.pg.undo.pop (back) && back.loop == -1, "LOOP marker: undo gives the old loop point back");
    // the START marker is free between the points (a point within 14 px takes it)
    r.cv.state.markerKind = ShapeCanvas::MarkerStart;
    r.cv.state.markerX = 0.5f;
    r.pg.undo.clear();
    const Change s = r.dragTo (r.X (0.5f), r.cv.plot().y + 20, r.X (0.4f), r.cv.plot().y + 20, 5);
    check (s.index == ShapeCanvas::Marker && near (static_cast<float> (s.value), 0.4f, 0.01f) && r.pg.model.loop == -1 && r.pg.undo.depth() == 0, "START marker: free between points, the model untouched");
    const Change s2 = r.dragTo (r.X (0.4f), r.cv.plot().y + 20, r.X (0.505f), r.cv.plot().y + 20, 5);
    check (near (static_cast<float> (s2.value), 0.5f, 1.0e-3f), "START marker: sticks to a point within 14 px");
    // a dimmed marker is no handle: a touch there is a touch on the canvas
    r.cv.state.markerKind = ShapeCanvas::MarkerDim;
    r.cv.state.markerX = 0.3f;
    const Change dm = r.tapAt (r.X (0.3f), r.cv.plot().y + 20, 1000.0);
    check (dm.index != ShapeCanvas::Marker, "a dimmed marker cannot be dragged");
    // the marker is drawn inside the canvas
    Scene sc (1290, 740, 0x0b1026ffu);
    r.cv.state.markerKind = ShapeCanvas::MarkerLoop;
    r.cv.state.markerX = 0.0f;
    r.pg.draw (sc.builder());
    check (! sc.overflow(), "the page with the marker at the left edge fits one scene");
}

void testEditorPageAndPointMod()
{
    ShapeEditorPage pg;
    pg.place ({ 0, 110, 1280, 628 });
    pg.canvas.setId (7);
    pg.model = fivePoints();
    pg.setTrigger (4, "Loop at point", 6, 90.0);
    pg.sync();
    Component* all[40];
    const int n = pg.collectLeaves (all, 0, 40);
    bool inside = true, size = true, apart = true, big = true;
    for (int i = 0; i < n; ++i)
    {
        inside = inside && Rect { 0, 110, 1280, 628 }.contains (all[i]->rect());
        size = size && all[i]->sizeOk();
        big = big && all[i]->rect().h >= 56 && all[i]->rect().w >= 56;
        for (int j = i + 1; j < n; ++j)
            apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
    }
    check (n == 20 && inside && size && apart && big, "editor page: " + std::to_string (n) + " leaves inside, at least their minimum size, 56 px, apart");
    const Component* off = nullptr;
    for (int g = 0; g < ShapeEditorPage::kGroups; ++g)
        check (pg.groups[g].check (all, n, &off) == 0 && chipsCovered (pg.groups[g], all, n) == 0, "editor page: Q group " + std::to_string (g) + " is adjacent with free lanes for the chips");
    check (pg.groups[2].member (0) == &pg.mode && pg.groups[2].member (1) == &pg.fphase && pg.groups[2].member (2) == &pg.fbend && pg.groups[2].numMembers() == 3, "editor page: TRIGGER = MODE, PHASE, BEND ALL");
    // reading order: the groups of the column run from the top down
    check (pg.groups[0].rect().y < pg.groups[1].rect().y && pg.groups[1].rect().y < pg.groups[2].rect().y && pg.loop.rect().y > pg.groups[2].rect().bottom() - 1, "editor page: the groups are in reading order, LOOP and SMOOTH under them");
    check (pg.canvas.rect().w >= 956 && pg.canvas.rect().h >= 540, "editor page: the canvas is not smaller than before (" + std::to_string (pg.canvas.rect().w) + " x " + std::to_string (pg.canvas.rect().h) + ")");
    check (pg.tools.rect().y == pg.back.rect().y && pg.shapes.rect().y == pg.back.rect().y && pg.more.rect().y == pg.back.rect().y && pg.undoButton.rect().y == pg.back.rect().y && pg.del.rect().y == pg.back.rect().y && pg.modButton.rect().y == pg.back.rect().y, "editor page: the top bar holds BACK, the tools, SHAPES, MORE, UNDO, DELETE, MOD");
    // the trigger: the marker is the PHASE in the loop modes, the stepper shows the mode
    check (pg.canvas.state.markerKind == ShapeCanvas::MarkerLoop && near (pg.canvas.state.markerX, 0.25f) && pg.mode.state.index == 4 && std::strcmp (pg.mode.state.text, "Loop at point") == 0 && near (static_cast<float> (pg.fphase.state.value), 90.0f), "setTrigger: mode 4 shows the LOOP marker at the phase");
    for (int st = 0; st < 6; ++st)
        check (ShapeEditorPage::markerKindOf (st) == (st == 1 ? ShapeCanvas::MarkerDim : (st >= 3 ? ShapeCanvas::MarkerLoop : ShapeCanvas::MarkerStart)), "the marker kind of the trigger mode " + std::to_string (st));
    // the bend field is the mean curve
    pg.model.p[1].power = 4.0f;
    pg.sync();
    check (pg.fbend.enabled() && near (static_cast<float> (pg.fbend.state.value), 1.0f, 0.51f), "BEND ALL shows the mean curve");
    // one point selected: the fields show it, CURVE is the curve
    pg.canvas.selectOnly (1);
    pg.sync();
    check (std::strcmp (pg.fcurve.state.label, "CURVE") == 0 && pg.fpoint.state.text[0] == '\0' && pg.loop.enabled() && pg.modButton.enabled(), "one point selected: the usual fields");
    pg.canvas.selectOnly (-1);
    pg.sync();
    check (! pg.modButton.enabled() && ! pg.del.enabled() && ! pg.loop.enabled(), "nothing selected: MOD, DELETE and LOOP are off");
    Scene sc (1290, 740, 0x0b1026ffu);
    pg.draw (sc.builder());
    check (! sc.overflow(), "editor page: the picture fits one scene (" + std::to_string (sc.numOps()) + " ops)");

    // the page of the modulation of points
    PointModPage mp;
    mp.place ({ 0, 110, 1280, 628 });
    mp.back.setId (1); mp.target.setId (2); mp.amount.setId (3); mp.remove.setId (4); mp.board.setId (5);
    mp.board.add (7, "LFO 1", "LFO", 0);
    mp.board.add (9, "ENV 2", "ENV", 0);
    mp.load (1, "", 0.0, 3, 1);
    Component* leaves[40];
    const int m = mp.collectLeaves (leaves, 0, 40);
    bool ok = true;
    for (int i = 0; i < m; ++i)
    {
        ok = ok && Rect { 0, 110, 1280, 628 }.contains (leaves[i]->rect()) && leaves[i]->sizeOk();
        for (int j = i + 1; j < m; ++j)
            ok = ok && ! leaves[i]->rect().overlaps (leaves[j]->rect());
    }
    check (ok, "point modulation page: " + std::to_string (m) + " leaves inside, big enough, apart");
    check (! mp.amount.enabled() && ! mp.remove.enabled(), "point modulation page: AMOUNT and REMOVE are off while nothing modulates");
    mp.load (1, "LFO 1", 50.0, 1, 2);
    check (mp.amount.enabled() && mp.remove.enabled() && near (static_cast<float> (mp.amount.state.value), 50.0f) && mp.target.state.index == 1, "point modulation page: with a source the amount and REMOVE work");
    const Rect chip = mp.board.chipRectOfId (9);
    Change bc = mp.touchDown (chip.x + chip.w / 2, chip.y + chip.h / 2, 0.0);
    const Change bu = mp.touchUp (chip.x + chip.w / 2, chip.y + chip.h / 2);
    if (bu) bc = bu;
    check (bc.kind == ChangeKind::Index && bc.id == 5 && bc.index == 9, "point modulation page: a tap on a source gives its id");
    Change tg = tap (mp.target, cx (mp.target) + mp.target.rect().w / 2 - 10, cy (mp.target), 0.0);
    check (tg.kind == ChangeKind::Index && tg.id == 2 && tg.index == 2, "point modulation page: the stepper goes from LEVEL to CURVE");
    Scene sc2 (1290, 740, 0x0b1026ffu);
    mp.draw (sc2.builder());
    check (! sc2.overflow(), "point modulation page: the picture fits one scene");
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
    {   // a random LFO at rest (nothing sounds: a flat trace) shows the dim picture of its style, not a bare line: every style has a path that moves, and the picture differs from the bare line and between styles
        float flat[192] = {};
        float lo[5], hi[5];
        bool bounded = true;
        for (int st = 0; st < 5; ++st)
        {
            lo[st] = 9.0f; hi[st] = -9.0f;
            for (int i = 0; i <= 64; ++i)
            {
                const float y = lfodisplay::restPath (st, static_cast<float> (i) / 64.0f);
                lo[st] = std::min (lo[st], y); hi[st] = std::max (hi[st], y);
                bounded = bounded && std::fabs (y) <= 1.0f;
            }
        }
        check (bounded && hi[0] - lo[0] > 0.5f && hi[1] - lo[1] > 0.5f && hi[2] - lo[2] > 0.5f && hi[3] - lo[3] > 0.5f && hi[4] - lo[4] > 0.5f, "display: the rest path of each of the five styles moves over at least a quarter of the height and stays inside it");
        lfodisplay::Data fl;
        fl.kind = lfodisplay::Data::Kind::Random;
        fl.trace = flat; fl.traceN = 192;
        Scene sa (760, 340, 0x0b1026ffu), sb (760, 340, 0x0b1026ffu), sc2 (760, 340, 0x0b1026ffu);
        lfodisplay::draw (sa.builder(), r, fl);
        fl.style = 1;
        lfodisplay::draw (sb.builder(), r, fl);
        fl.style = 3;
        lfodisplay::draw (sc2.builder(), r, fl);
        const Image ia = render (sa), ib = render (sb), ic = render (sc2);
        check (ia.px != ib.px && ib.px != ic.px, "display: at rest the picture follows the style (the bare line of no style, S & H, Lorenz differ)");
    }
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
    testCompactPlot();
    testEditor();
    testSelectionModel();
    testWholeShapeModel();
    testCanvasQuickDelete();
    testCanvasStepDrawing();
    testCanvasSelection();
    testCanvasSoftAndBend();
    testCanvasMarker();
    testEditorPageAndPointMod();
    testDisplay();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
