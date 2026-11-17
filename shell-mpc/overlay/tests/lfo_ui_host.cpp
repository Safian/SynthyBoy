// SynthyBoy - MPC overlay tests: the LFO UI (lfo_ui.h): the wiring of every control to the engine parameters of the LFO and of the random LFO (both directions), the Q groups of the compact view and the full
// screen, the instance selection, the sync switch, the shape picker, the shape editor (the drawn shape in the content state, the undo, the coalesced writes, the virtual Q group), the targets page and the list
// of destinations, the cursors of the voices and the trace of a random LFO, the touch path (decoder events -> components -> parameters) and the drawing (one scene, inside the canvas). With LFO_IMAGES=<dir>
// the pages are written as PNG pictures for a human.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "../lfo_ui.h"
#include "../scene_cpu.h"
#include "../../ui/tests/ui_test.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
using uit::check;

/** A plugin instance made of an array of plain values: writeParam converts like the plugin does; the content and the Q router are real (the router is the plugin's own). */
class Inst final : public IInstanceView
{
public:
    Inst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
        contentPtr = std::make_shared<const synthyboy::StateContent> (synthyboy::StateContent {});
    }
    float param (int i) const noexcept override { return v_[i]; }
    bool live (synthyboy::LiveValues&) const noexcept override { return false; }
    double sampleRate() const noexcept override { return 48000.0; }
    double bpm() const noexcept override { return 120.0; }
    int playSelection (int g) const noexcept override { return sel[std::clamp (g, 0, 2)]; }
    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 16; ++i) q[i] = i < n ? t[i] : -1;
        router.setGroup (t, n, static_cast<int> (synthyboy::paramCount()));
        ++qCalls;
    }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setPlaySelection (int g, int i) noexcept override { sel[std::clamp (g, 0, 2)] = i; ++selWrites; lastSel = g == 2 ? 2 : 1; }
    int playLastSelector() const noexcept override { return lastSel; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    void setLfoShape (int lfo, const synthyboy::ContentLfoShape& s) override
    {
        synthyboy::StateContent next = *contentPtr;
        bool done = false;
        for (synthyboy::ContentLfoShape& e : next.lfoShapes)
            if (e.lfo == lfo) { e = s; done = true; }
        if (! done)
            next.lfoShapes.push_back (s);
        contentPtr = std::make_shared<const synthyboy::StateContent> (next);
        ++shapeWrites;
        check (synthyboy::validateContent (next), "the content the LFO page wrote is valid (the state would keep it)");
    }
    void setQVirtual (int id, float v) noexcept override { router.setVirtualValue (id, v); }
    bool takeQVirtual (int id, float& v) noexcept override { return router.takeVirtual (id, v); }
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }
    /** The host turns a Q-Link: the write to the Q proxy `q` (0 .. 3). */
    QRouter::Write hostQ (int q, float value)
    {
        return router.hostWrite (q, value, false, [this] (int t) noexcept { return synthyboy::toNormalised (synthyboy::paramTable()[t], v_[t]); },
                                 [this] (int t, float n) { writeParam (t, n); });
    }
    int sel[3] = { 2, 1, 1 }, lastSel = 0, q[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0, shapeWrites = 0;
    bool fullOpen = false;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    QRouter router;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

struct Rig
{
    LfoUi ui;
    Inst inst;
    UiStack stack;
    synthyboy::LiveValues live;
    bool useLive = false;
    double now = 1000.0;
    int hl = 3;
    Rig() { refresh(); }
    void refresh()
    {
        now += 50.0;
        ui.update (inst, hl, stack, now, useLive ? &live : nullptr);
    }
    bool tap (int cx, int cy, int holdMs = 40)
    {
        const bool a = ui.touch (ev (TouchEvent::Down, cx, cy), stack, inst, now);
        now += holdMs;
        ui.tick (now, stack, inst);
        ui.touch (ev (TouchEvent::Up, cx, cy), stack, inst, now);
        now += 100.0;
        refresh();
        return a;
    }
    void tapC (const ui::Component& c, int holdMs = 40) { tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs); }
    void tapIn (const ui::Rect& r, int fx, int fy) { tap (r.x + r.w * fx / 100, r.y + r.h * fy / 100); }
    void drag (int x0, int y0, int x1, int y1, int steps = 10)
    {
        ui.touch (ev (TouchEvent::Down, x0, y0), stack, inst, now);
        for (int i = 1; i <= steps; ++i)
        {
            now += 16.0;
            ui.touch (ev (TouchEvent::Move, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps), stack, inst, now);
            refresh();
        }
        ui.touch (ev (TouchEvent::Up, x1, y1), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void centre (const ui::Component& c, int& x, int& y) { x = c.rect().x + c.rect().w / 2; y = c.rect().y + c.rect().h / 2; }
    void openFull() { tapC (ui.compact().full); }
    void key (const std::string& k, float v) { inst.set (k, v); refresh(); }
};

int paramIdx (const std::string& k) { return Inst::idx (k); }

void testBindings()
{
    for (int slot = 0; slot < 14; ++slot)
    {
        Rig r;
        if (slot < 10)
            r.inst.sel[2] = slot + 1;
        else
            r.inst.sel[2] = 1;
        r.refresh();
        if (slot >= 10)
        {
            // the compact view's stepper: next until RND 1 .. 4
            r.inst.sel[2] = 1;
            r.refresh();
            const ui::Rect c = r.ui.compact().instance.rect();
            for (int i = 0; i < slot; ++i)
                r.tap (c.x + c.w - 14, c.y + c.h / 2);
        }
        const std::string nm = slot < 10 ? "LFO " + std::to_string (slot + 1) : "RND " + std::to_string (slot - 9);
        const std::string pre = slot < 10 ? "lfo" + std::to_string (slot + 1) + "." : "rlfo" + std::to_string (slot - 9) + ".";
        check (r.ui.random() == (slot >= 10) && r.ui.selection() == (slot < 10 ? slot + 1 : slot - 9), nm + ": the selection");
        int unbound = 0, wrong = 0;
        std::string which;
        for (const LfoUi::Item& it : r.ui.items())
        {
            if (it.type == LfoUi::T::Inert)
                continue;
            if (it.param < 0 && it.c->enabled())
            {
                ++unbound;
                which += " " + std::to_string (it.id);
            }
            if (it.param >= 0 && std::strncmp (synthyboy::paramTable()[it.param].key, pre.c_str(), pre.size()) != 0)
                ++wrong;
        }
        check (unbound == 0, nm + ": every control has an engine parameter or is disabled (enabled without one:" + which + ")");
        check (wrong == 0, nm + ": every parameter is of " + pre);
    }
}

void testCompactQGroup()
{
    Rig r;
    check (r.inst.q[0] == paramIdx ("lfo1.shape") && r.inst.q[1] == paramIdx ("lfo1.rate") && r.inst.q[2] == paramIdx ("lfo1.fade_in") && r.inst.q[3] == paramIdx ("lfo1.smooth_time"),
           "compact: the Q group is SHAPE, RATE, RISE, SMOOTH time of lfo1");
    check (r.ui.compact().group.active(), "compact: its frame is on (the LFO sub-tab)");
    r.key ("lfo1.sync", 1.0f);
    check (r.inst.q[1] == paramIdx ("lfo1.division"), "compact: tempo sync: Q2 is the division");
    r.key ("lfo1.sync", 4.0f);
    check (r.inst.q[1] == paramIdx ("lfo1.rate") && ! r.ui.compact().rate.rate.enabled(), "compact: key follow: Q2 stays the rate, the knob is dim");
    r.inst.sel[2] = 7;
    r.refresh();
    check (r.inst.q[0] == paramIdx ("lfo7.shape"), "compact: the group follows the Play LFO selector (lfo7)");
    // another sub-tab: the group is not ours
    Rig o;
    const int before = o.inst.qCalls;
    o.hl = 1;
    o.inst.sel[2] = 3;
    o.refresh();
    check (o.inst.qCalls == before && ! o.ui.compact().group.active(), "compact: on another sub-tab the frame is off and the group is not published");
    // a random LFO
    Rig q;
    const ui::Rect c = q.ui.compact().instance.rect();
    q.inst.sel[2] = 1;
    q.refresh();
    for (int i = 0; i < 11; ++i)
        q.tap (c.x + c.w - 14, c.y + c.h / 2);
    check (q.ui.random() && q.ui.selection() == 2, "the stepper goes on from LFO 10 to RND 1 and RND 2");
    check (q.inst.q[0] == paramIdx ("rlfo2.style") && q.inst.q[1] == paramIdx ("rlfo2.rate") && q.inst.q[2] == paramIdx ("rlfo2.sync") && q.inst.q[3] == paramIdx ("rlfo2.stereo"), "random compact: STYLE, RATE, SYNC, STEREO of rlfo2");
}

void testWrites()
{
    Rig r;
    r.inst.set ("lfo1.rate", 2.0f);
    r.refresh();
    int x, y;
    r.centre (r.ui.compact().rate.rate, x, y);
    const int before = r.inst.writes;
    r.drag (x, y, x, y - 40);
    check (r.inst.get ("lfo1.rate") > 2.0f && r.inst.writes > before && r.inst.lastWritten == paramIdx ("lfo1.rate"), "a drag up on RATE writes lfo1.rate (" + std::to_string (r.inst.get ("lfo1.rate")) + " Hz)");
    // the value is under the name
    check (std::string (r.ui.compact().rate.rate.state.valueText).find ("Hz") != std::string::npos, "the knob shows its value with the unit (" + std::string (r.ui.compact().rate.rate.state.valueText) + ")");
    // the shape stepper: a tap on the right quarter steps, a tap on the middle opens the list
    const ui::Rect s = r.ui.compact().shape.rect();
    r.key ("lfo1.shape", 0.0f);
    r.tap (s.x + s.w - 14, s.y + s.h / 2);
    check (r.inst.get ("lfo1.shape") == 1.0f, "a tap on the right end of SHAPE: the next shape");
    r.tap (s.x + s.w / 2, s.y + s.h / 2);
    check (r.stack.depth() == 2 && r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 13, "a tap on the name of SHAPE opens the list of the 13 shapes over the full screen");
    r.tap (r.ui.list().rect().x + 100, r.ui.list().rect().y + ui::ListPopup::kPad + ui::ListPopup::kRowH * (0 - r.ui.list().firstRow()) + 28);
    check (r.stack.depth() == 1 && r.inst.get ("lfo1.shape") == 0.0f, "the first row of the list: Sine; the list closes");
    // the compact view has the sync mode cells next to the rate
    {
        r.stack.closeAll();
        r.refresh();
        const ui::PictogramSelector& mc = r.ui.compact().rate.modeCells;
        r.tap (mc.cell (1).x + 4, mc.cell (1).y + 20);
        check (r.inst.get ("lfo1.sync") == 1.0f && r.ui.compact().rate.mode() == ui::lfo::SyncMode::Bpm && r.ui.compact().rate.primary() == &r.ui.compact().rate.division, "the compact view: the BPM cell makes the rate a division");
        r.tap (mc.cell (0).x + 4, mc.cell (0).y + 20);
        check (r.inst.get ("lfo1.sync") == 0.0f, "the compact view: the HZ cell");
    }
    // the switches of the sync
    r.openFull();
    Rig& R = r;
    R.stack.closeAll();
    R.refresh();
    R.openFull();
    const ui::SyncSwitch& sw = R.ui.full().syncSwitch;
    R.tap (sw.mode.cell (1).x + 4, sw.mode.cell (1).y + 20);
    check (R.inst.get ("lfo1.sync") == 1.0f, "the BPM cell: sync = tempo");
    R.tapC (sw.dot);
    check (R.inst.get ("lfo1.sync") == 2.0f, "DOT: tempo dotted");
    R.tapC (sw.trip);
    check (R.inst.get ("lfo1.sync") == 3.0f, "TRIP: tempo triplet (it takes DOT off)");
    R.tap (sw.mode.cell (2).x + 4, sw.mode.cell (2).y + 20);
    check (R.inst.get ("lfo1.sync") == 4.0f && R.ui.full().keyActive() && R.ui.full().keyTranspose.enabled(), "the KEY cell: key follow, the KEY block works");
    R.tap (sw.mode.cell (0).x + 4, sw.mode.cell (0).y + 20);
    check (R.inst.get ("lfo1.sync") == 0.0f && ! R.ui.full().keyActive() && ! R.ui.full().keyTranspose.enabled(), "the HZ cell: free; the KEY block is gone");
    // the shape picker of the full screen
    const ui::ShapePicker& pk = R.ui.full().picker;
    R.tap (pk.cell (4).x + 10, pk.cell (4).y + 10);
    check (R.inst.get ("lfo1.shape") == 4.0f, "the picker: square");
    R.tap (pk.cell (12).x + 10, pk.cell (12).y + 10);
    check (R.inst.get ("lfo1.shape") == 12.0f && R.inst.contentPtr->lfoShapes.size() == 1 && R.inst.contentPtr->lfoShapes[0].lfo == 1, "the picker: the drawn shape; the content has a shape for it (the engine reads a drawn shape without points as a flat line)");
    // MONO, HOST, SMOOTH
    R.tapC (R.ui.full().monoQ);
    check (R.inst.get ("lfo1.mono") == 1.0f && R.ui.full().monoQ.state.on, "MONO in the motion block writes lfo1.mono");
    R.tapC (R.ui.full().hostPick);
    check (R.inst.get ("lfo1.host") == 1.0f, "HOST");
    const float smooth = R.inst.get ("lfo1.smooth");
    R.tapC (R.ui.full().smoothPick);
    check (R.inst.get ("lfo1.smooth") != smooth, "SMOOTH");
}

void testFullScreen()
{
    Rig r;
    check (r.stack.isMain() && ! r.inst.fullOpen, "main: no full screen");
    r.openFull();
    check (r.stack.depth() == 1 && r.stack.element() == LfoUi::kElementLfo && r.inst.fullOpen, "the full screen button opens the LFO's element and tells the plugin");
    check (r.ui.displayRect().w > 800, "the full screen has a display rectangle");
    check (r.inst.q[0] == paramIdx ("lfo1.shape") && r.inst.q[1] == paramIdx ("lfo1.rate") &&
           r.inst.q[2] == paramIdx ("lfo1.fade_in") && r.inst.q[3] == paramIdx ("lfo1.smooth_time"),
           "full screen: the Q group RATE is shape (grid), rate, rise, smooth");
    // Q1 is the shape grid: the knob writes lfo1.shape (the proxy writes the target) and the grid follows; the page has no SHAPE stepper and no SYNC stepper (the HZ / BPM / KEY buttons are the sync) and one MONO
    r.key ("lfo1.shape", 5.0f);
    check (r.ui.full().picker.state.index == 5 && r.ui.full().groups[0].member (0) == &r.ui.full().picker, "a knob step on Q1 moves the selected cell of the shape grid (the grid is Q1)");
    {
        ui::Component* leaves[160];
        const int nl = r.ui.full().collectLeaves (leaves, 0, 160);
        auto has = [&] (const ui::Component* c) { for (int i = 0; i < nl; ++i) if (leaves[i] == c) return true; return false; };
        check (! has (&r.ui.full().shape) && ! has (&r.ui.full().sync) && has (&r.ui.full().monoQ) && ! has (&r.ui.full().monoTop) && r.ui.full().groups[1].member (0) == &r.ui.full().syncSwitch, "no SHAPE stepper, no SYNC stepper (the HZ / BPM / KEY row is the Timing group's Q1), MONO in Motion");
    }
    r.tapC (r.ui.full().swing);
    check (r.ui.activeQ() == 2 && r.inst.q[0] == paramIdx ("lfo1.direction") && r.inst.q[1] == paramIdx ("lfo1.swing") && r.inst.q[2] == paramIdx ("lfo1.stereo") && r.inst.q[3] == paramIdx ("lfo1.mono"),
           "a touch on SWING: the MOTION group (direction, swing, stereo, mono)");
    r.tapC (r.ui.full().mode);
    check (r.stack.depth() == 2 && r.ui.list().state.count == 6, "MODE: the list of the six sync types opens");
    r.stack.back();
    r.refresh();
    check (r.ui.activeQ() == 1 && r.inst.q[0] == paramIdx ("lfo1.sync") && r.inst.q[1] == paramIdx ("lfo1.sync_type") && r.inst.q[2] == paramIdx ("lfo1.phase") && r.inst.q[3] == paramIdx ("lfo1.delay"),
           "the TIMING group (sync, mode, phase, delay)");
    r.tapC (r.ui.full().close);
    check (r.stack.isMain() && ! r.inst.fullOpen, "the close button: main again");
    r.openFull();
    r.stack.closeAll();
    r.refresh();
    check (! r.inst.fullOpen, "the stack closed from outside (the page left): the plugin is told");
    // the stack belongs to another element: this UI is idle and does not touch
    r.stack.openFull (2);
    r.refresh();
    check (r.ui.isMain() && ! r.ui.covers (100, 300) && r.ui.covers (900, 300), "another element's full screen: this UI draws the compact view only and does not cover the screen");
    r.stack.closeAll();
    r.refresh();
    // the instance cells
    r.openFull();
    const ui::InstanceBar& bar = r.ui.full().bar;
    r.tap (bar.lfoCells.cell (2).x + 4, bar.lfoCells.cell (2).y + 20);
    check (r.inst.sel[2] == 3 && r.ui.selection() == 3, "the cell 3: Play LFO = 3");
    r.tap (bar.lfoCells.cell (6).x + 4, bar.lfoCells.cell (6).y + 20);
    check (r.inst.sel[2] == 7 && r.ui.selection() == 7 && bar.lfoCells.state.index == 6, "the seventh cell: LFO 7 (no pages: all ten LFOs are in the bar)");
    r.tap (bar.rndCells.cell (1).x + 4, bar.rndCells.cell (1).y + 20);
    check (r.ui.random() && r.ui.selection() == 2 && r.ui.full().random() && r.ui.full().numGroups() == 3, "RND 2: the random LFO, the full screen of the random LFO");
    check (r.inst.q[0] == paramIdx ("rlfo2.style") && r.inst.q[1] == paramIdx ("rlfo2.rate") && r.inst.q[2] == paramIdx ("rlfo2.sync") && r.inst.q[3] == paramIdx ("rlfo2.stereo"), "random full screen: the RANDOM group");
    r.tap (r.ui.full().picker.cell (3).x + 10, r.ui.full().picker.cell (3).y + 10);
    check (r.inst.get ("rlfo2.style") == 3.0f, "the style picker: Lorenz");
    r.tap (bar.lfoCells.cell (5).x + 4, bar.lfoCells.cell (5).y + 20);
    check (! r.ui.random() && r.ui.selection() == 6, "back to LFO 6");
}

void testEditor()
{
    Rig r;
    r.inst.set ("lfo1.shape", 4.0f);   // square
    r.refresh();
    r.openFull();
    r.tapC (r.ui.full().edit);
    check (r.stack.depth() == 2 && r.ui.topPage() == LfoUi::kPageEditor, "EDIT opens the shape editor");
    check (r.inst.get ("lfo1.shape") == 12.0f && r.inst.shapeWrites == 1 && r.inst.contentPtr->lfoShapes.size() == 1, "a built-in shape becomes the drawn one: the parameter is Custom, the content has the points");
    check (r.inst.contentPtr->lfoShapes[0].points.size() == 4 && r.inst.contentPtr->lfoShapes[0].points[2].x == 0.5f && r.inst.contentPtr->lfoShapes[0].points[2].y == 0.0f, "the square's points: (0 1) (.5 1) (.5 0) (1 0)");
    check ((r.ui.editor().canvas.state.gridX == 4 || r.ui.editor().canvas.state.gridX == 8) && r.ui.editor().canvas.state.gridX == r.ui.editor().canvas.state.gridY, "the grid is the one the shape was drawn on, the same both ways (" + std::to_string (r.ui.editor().canvas.state.gridX) + ")");
    check (r.inst.q[0] == QRouter::encode (0, QRouter::kVirtual) && r.inst.q[3] == QRouter::encode (3, QRouter::kVirtual) && r.inst.q[4] == QRouter::encode (4, QRouter::kVirtual) && r.inst.q[6] == QRouter::encode (6, QRouter::kVirtual) && r.inst.q[7] == -1,
           "the Q groups of the editor: EDIT = POINT, X, Y, CURVE on Q1 .. Q4 (virtual 0 .. 3), GRID = GRID X, GRID Y, SNAP on Q5 .. Q7 (virtual 4 .. 6)");
    // add a point with the point tool
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    r.ui.editor().canvas.state.gridX = r.ui.editor().canvas.state.gridY = 8;
    const int w0 = r.inst.shapeWrites;
    const int px = static_cast<int> (cv.xOf (0.25f)), py = static_cast<int> (cv.yOf (0.5f));
    r.tap (px, py);
    check (r.ui.editor().model.n == 5 && cv.state.selected == 1, "a tap on the canvas adds a point (5 points)");
    check (r.inst.shapeWrites > w0 && r.inst.contentPtr->lfoShapes[0].points.size() == 5, "at the end of the gesture the shape goes to the content");
    // the virtual Q: the model's values, then the host's write
    float v;
    check (std::fabs (r.inst.router.value (1, [] (int) { return 0.0f; }) - 0.25f) < 1.0e-4f && std::fabs (r.inst.router.value (2, [] (int) { return 0.0f; }) - 0.5f) < 1.0e-4f && std::fabs (r.inst.router.value (0, [] (int) { return 0.0f; }) - 0.25f) < 1.0e-4f,
           "the router holds the virtual values of the selected point (POINT 2 of 5 = 0.25, x 0.25, y 0.5)");
    check (r.inst.hostQ (2, 0.875f) == QRouter::Write::written, "the host turns Q3 (Y): written to the virtual target");
    r.refresh();
    check (std::fabs (r.ui.editor().model.p[1].y - 0.875f) < 1.0e-4f && r.inst.contentPtr->lfoShapes[0].points[1].y == 0.875f, "the Y of the selected point follows the Q-Link (and the content)");
    check (! r.inst.takeQVirtual (2, v), "the host's write was taken once");
    check (r.inst.hostQ (4, 1.0f) == QRouter::Write::written, "the host turns Q5 to the end: GRID X (the GRID group is on Q5 .. Q7)");
    check (r.inst.hostQ (5, 0.0f) == QRouter::Write::written, "... and Q6 to the start: GRID Y");
    r.refresh();
    check (r.ui.editor().canvas.state.gridX == 32 && r.ui.gridXOf (1) == 32 && r.ui.editor().canvas.state.gridY == 2 && r.ui.gridYOf (1) == 2, "GRID X 32 and GRID Y 2 (the two grids are independent, kept for this LFO)");
    check (r.inst.contentPtr->lfoShapes[0].gridX == 32 && r.inst.contentPtr->lfoShapes[0].gridY == 2, "the grid goes to the content with the shape (stored with the LFO: state version 5)");
    check (r.inst.hostQ (6, 0.0f) == QRouter::Write::written, "the host turns Q7: SNAP off");
    r.refresh();
    check (! r.ui.editor().canvas.state.snap, "SNAP is off (a toggle as a Q member)");
    r.inst.hostQ (6, 1.0f);
    r.refresh();
    check (r.ui.editor().canvas.state.snap, "... and on again");
    check (r.inst.hostQ (3, 1.0f) == QRouter::Write::written, "the host turns Q4: CURVE");
    r.refresh();
    check (std::fabs (r.ui.editor().model.p[1].power - 20.0f) < 1.0e-3f, "CURVE of the selected segment is +20");
    // undo
    r.tapC (r.ui.editor().undoButton);
    r.tapC (r.ui.editor().undoButton);
    r.tapC (r.ui.editor().undoButton);
    check (r.ui.editor().model.n == 4 || r.ui.editor().model.n == 5, "UNDO steps back (" + std::to_string (r.ui.editor().model.n) + " points)");
    r.tapC (r.ui.editor().undoButton);
    r.tapC (r.ui.editor().undoButton);
    check (r.ui.editor().model.n == 4 && r.inst.contentPtr->lfoShapes[0].points.size() == 4, "UNDO back to the 4 points of the square; the content follows");
    // the tools
    r.tap (r.ui.editor().tools.cell (1).x + 10, r.ui.editor().tools.cell (1).y + 10);
    check (cv.state.tool == ui::ToolSelector::Flat, "the FLAT tool");
    r.tap (static_cast<int> (cv.xOf (0.5f)), static_cast<int> (cv.yOf (0.5f)));
    check (r.ui.editor().model.n >= 2, "a flat piece drawn");
    // back: the shape is in the content and the page closes
    r.tapC (r.ui.editor().back);
    check (r.stack.depth() == 1 && r.ui.topPage() == 0, "BACK is the full screen");
    check (r.ui.gridXOf (1) == 32 && r.ui.gridYOf (1) == 2, "the grid stays");
    // open again: the drawn shape comes from the content
    r.tapC (r.ui.full().edit);
    check (r.ui.editor().model.n == static_cast<int> (r.inst.contentPtr->lfoShapes[0].points.size()), "the editor opens with the points of the content");
    // a random LFO has no editor
    r.tapC (r.ui.editor().back);
    r.tap (r.ui.full().bar.rndCells.cell (0).x + 4, r.ui.full().bar.rndCells.cell (0).y + 20);
    check (! r.ui.full().edit.enabled(), "EDIT is dim for a random LFO");
}

void testTargets()
{
    Rig r;
    r.openFull();
    r.tapC (r.ui.full().targets);
    check (r.stack.depth() == 2 && r.ui.topPage() == LfoUi::kPageTargets && r.ui.targetsPage().numRows() == 0, "TARGETS: the page of the targets, nothing yet");
    r.tapC (r.ui.targetsPage().add);
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count > 10, "ADD TARGET: the list of the groups of destinations (" + std::to_string (r.ui.list().state.count) + ")");
    // the group Filter 1, then its first parameter
    int g = -1;
    for (int i = 0; i < r.ui.list().state.count; ++i)
        if (std::string (r.ui.list().state.items[i]) == "Filter 1")
            g = i;
    check (g >= 0, "the group Filter 1 is in the list");
    r.ui.list().showCurrent();
    ui::Rect lr = r.ui.list().rect();
    // choose by the component's own change (the row may be off the window): the list is a component with rows
    r.ui.list().state.current = g;
    r.ui.list().showCurrent();
    const int row = g - r.ui.list().firstRow();
    r.tap (lr.x + 100, lr.y + ui::ListPopup::kPad + row * ui::ListPopup::kRowH + 20);
    check (r.ui.list().state.header != nullptr && std::string (r.ui.list().state.header) == "Filter 1", "the group opens its parameters under a header");
    r.tap (lr.x + 100, lr.y + ui::ListPopup::kPad + ui::ListPopup::kRowH + ui::ListPopup::kRowH / 2 + 20);
    check (r.ui.topPage() == LfoUi::kPageTargets && r.ui.targetsPage().numRows() == 1, "a parameter: the target is added (the list closes, the page shows it)");
    int src = -1, dst = -1;
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (std::lround (r.inst.get (synthyboy::paramTable()[sp.source].key)) == 7)
        {
            src = s;
            dst = static_cast<int> (std::lround (r.inst.get (synthyboy::paramTable()[sp.destination].key)));
            break;
        }
    }
    check (src >= 0 && dst > 0, "the slot has the source LFO 1 (7) and a destination");
    check (std::string (r.ui.targetsPage().rows[0].name).find ("Filter 1") != std::string::npos, "the row names the target (" + std::string (r.ui.targetsPage().rows[0].name) + ")");
    // the depth, the remove button
    ui::HBar& bar = r.ui.targetsPage().rows[0].depth;
    int x, y;
    r.centre (bar, x, y);
    r.drag (x, y, x + 100, y);
    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (src);
    check (r.inst.get (synthyboy::paramTable()[sp.amount].key) > 50.0f, "the depth bar changes the amount (" + std::to_string (r.inst.get (synthyboy::paramTable()[sp.amount].key)) + ")");
    r.tapC (r.ui.targetsPage().rows[0].remove);
    check (r.ui.targetsPage().numRows() == 0 && std::lround (r.inst.get (synthyboy::paramTable()[sp.source].key)) == 0, "remove: the slot is empty again");
    r.tapC (r.ui.targetsPage().back);
    check (r.stack.depth() == 1, "BACK: the full screen");
    // the targets button of the full screen shows the knobs' own arcs: a modulated knob has a chip
    uibind::writePlain (r.inst, sp.source, 7);
    uibind::writePlain (r.inst, sp.destination, static_cast<double> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (paramIdx ("lfo2.rate")))));
    uibind::writePlain (r.inst, sp.amount, 40.0);
    r.inst.sel[2] = 2;
    r.refresh();
    r.ui.update (r.inst, 3, r.stack, r.now + 500.0, nullptr);
    check (r.ui.full().rate.rate.state.numMods == 1 && r.ui.compact().rate.rate.state.numMods == 1, "lfo2.rate is modulated by LFO 1: the knob has its chip");
    // a long press on a knob opens the modulations of the control
    int kx, ky;
    r.stack.closeAll();
    r.refresh();
    r.centre (r.ui.compact().rate.rate, kx, ky);
    r.tap (kx, ky, 700);
    check (r.stack.depth() == 2 && r.ui.topPage() == LfoUi::kPageMods && r.ui.modsPage().numRows() == 1, "a long press on RATE: the modulations of the control (the page of the matrix, 1 row)");
}

void testDisplay()
{
    Rig r;
    r.useLive = true;
    std::memset (&r.live, 0, sizeof r.live);
    r.live.block = 1;
    r.live.numLfoVoices = 2;
    r.live.lfoVoicePhase[0][0] = 0.25f;
    r.live.lfoVoicePhase[0][1] = 0.75f;
    r.live.source[6] = 0.4f;   // LFO 1 (source 7)
    r.refresh();
    const ui::lfodisplay::Data& d = r.ui.displayData();
    check (d.kind == ui::lfodisplay::Data::Kind::Shape && d.n == 256 && d.numCursors == 2, "the display: the shape of the engine, one cursor per sounding voice");
    check (std::fabs (d.cx[0] - 0.25f) < 1.0e-4f && std::fabs (d.cy[0] - 0.4f) < 1.0e-4f, "the first cursor is at the phase and at the output the LFO gives now");
    float lo = 1, hi = -1;
    for (int i = 0; i < d.n; ++i) { lo = std::min (lo, d.y[i]); hi = std::max (hi, d.y[i]); }
    check (lo < -0.9f && hi > 0.9f, "the sine shape swings -1 .. 1 (" + std::to_string (lo) + " .. " + std::to_string (hi) + ")");
    r.key ("lfo1.mono", 1.0f);
    r.refresh();
    check (r.ui.displayData().numCursors == 1, "a mono LFO has one cursor");
    r.live.numLfoVoices = 0;
    r.refresh();
    check (r.ui.displayData().numCursors == 0, "no voice: no cursor");
    // the lead-in, the loop point, the envelope type
    r.key ("lfo1.delay", 400.0f);
    r.key ("lfo1.fade_in", 800.0f);
    r.key ("lfo1.sync_type", 4.0f);
    r.key ("lfo1.phase", 90.0f);
    const ui::lfodisplay::Data& e = r.ui.displayData();
    check (e.delayShare > 0.0f && e.riseShare > e.delayShare && e.envelope && std::fabs (e.loopX - 0.25f) < 1.0e-4f, "delay, rise (a share of the cycle), the envelope type and its loop point");
    // the display follows the shape
    r.key ("lfo1.sync_type", 0.0f);
    r.key ("lfo1.phase", 0.0f);
    r.key ("lfo1.shape", 4.0f);
    r.now += 100.0;
    r.refresh();
    check (r.ui.displayData().y[10] > 0.9f && r.ui.displayData().y[200] < -0.9f, "the display follows the shape parameter (square)");
    // a drawn shape comes from the content
    synthyboy::StateContent c;
    synthyboy::ContentLfoShape s;
    s.lfo = 1;
    s.points = { synthyboy::ContentPoint (0.0f, 0.0f), synthyboy::ContentPoint (0.5f, 1.0f), synthyboy::ContentPoint (1.0f, 0.0f) };
    c.lfoShapes.push_back (s);
    r.inst.contentPtr = std::make_shared<const synthyboy::StateContent> (c);
    r.key ("lfo1.shape", 12.0f);
    r.now += 100.0;
    r.refresh();
    check (r.ui.displayData().y[128] > 0.8f && r.ui.displayData().y[0] < -0.8f, "a drawn shape: the display reads it from the content (a triangle)");
    // a random LFO: the trace moves
    Rig q;
    q.useLive = true;
    std::memset (&q.live, 0, sizeof q.live);
    q.live.block = 1;
    q.live.numLfoVoices = 1;
    const ui::Rect c0 = q.ui.compact().instance.rect();
    q.inst.sel[2] = 1;
    q.refresh();
    for (int i = 0; i < 10; ++i)
        q.tap (c0.x + c0.w - 14, c0.y + c0.h / 2);
    check (q.ui.random() && q.ui.displayData().kind == ui::lfodisplay::Data::Kind::Random, "RND 1: the display is the trace");
    q.live.source[14] = 1.0f;   // the engine's 0 .. 1 scale: 1 = +1
    q.now += 100.0;
    q.refresh();
    check (q.ui.displayData().trace[191] > 0.99f, "the newest value of the trace is the random LFO's output (-1 .. 1)");
    q.live.source[14] = 0.0f;
    q.now += 100.0;
    q.refresh();
    check (q.ui.displayData().trace[191] < -0.99f && q.ui.displayData().trace[190] > 0.99f, "the trace scrolls");
}

void testTouchAndDraw()
{
    Rig r;
    // the touch of the other thirds is not ours
    TouchEvent e = ev (TouchEvent::Down, 200, 300);
    check (! r.ui.touch (e, r.stack, r.inst, r.now), "a touch in the oscillator third is not ours");
    r.ui.touch (ev (TouchEvent::Up, 200, 300), r.stack, r.inst, r.now);
    e = ev (TouchEvent::Down, 1000, 300);
    check (r.ui.touch (e, r.stack, r.inst, r.now), "a touch in the LFO third is ours");
    r.ui.touch (ev (TouchEvent::Up, 1000, 300), r.stack, r.inst, r.now);
    // every page fits one scene and stays inside the canvas
    auto render = [&] (const char* name, int w, int h, auto&& draw) -> uint32_t
    {
        uit::Scene sc (w, h, uit::kBackgroundRgba);
        draw (sc.builder());
        check (! sc.overflow(), std::string (name) + ": one scene (" + std::to_string (sc.numOps()) + " ops of " + std::to_string (gpu::kMaxOps) + ")");
        const uit::Image im = uit::render (sc);
        if (const char* dir = std::getenv ("LFO_IMAGES"))
            uit::writePng (std::string (dir) + "/" + name + ".png", im, 1);
        uint32_t h2 = 2166136261u;
        for (uint32_t p : im.px)
            h2 = (h2 ^ p) * 16777619u;
        return h2;
    };
    r.useLive = true;
    std::memset (&r.live, 0, sizeof r.live);
    r.live.block = 1;
    r.live.numLfoVoices = 3;
    r.live.lfoVoicePhase[0][0] = 0.30f; r.live.lfoVoicePhase[0][1] = 0.55f; r.live.lfoVoicePhase[0][2] = 0.9f;
    r.live.source[6] = 0.5f;
    r.key ("lfo1.delay", 200.0f);
    r.key ("lfo1.fade_in", 300.0f);
    r.key ("lfo1.rate", 1.5f);
    uint32_t a = render ("lfo_compact_sine", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawCompact (sb); r.ui.drawLive (sb); });
    r.live.lfoVoicePhase[0][0] = 0.60f;
    r.refresh();
    uint32_t b = render ("lfo_compact_sine_later", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawCompact (sb); r.ui.drawLive (sb); });
    check (a != b, "the cursor moved: another picture");
    check (r.ui.drawHash (*std::make_unique<gpu::SceneData>()) != 0, "a hash of the picture");
    {
        auto s1 = std::make_unique<gpu::SceneData>(), s2 = std::make_unique<gpu::SceneData>();
        const uint32_t h1 = r.ui.drawHash (*s1);
        r.live.lfoVoicePhase[0][0] = 0.61f;
        r.refresh();
        check (r.ui.drawHash (*s2) != h1, "the hash of the picture follows the cursor");
    }
    r.key ("lfo1.shape", 8.0f);
    r.now += 100.0;
    r.refresh();
    render ("lfo_compact_randomsteps", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawCompact (sb); r.ui.drawLive (sb); });
    r.key ("lfo1.shape", 0.0f);
    r.key ("lfo1.sync", 1.0f);
    r.key ("lfo1.division", 8.0f);
    render ("lfo_compact_tempo", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawCompact (sb); r.ui.drawLive (sb); });
    r.key ("lfo1.sync", 0.0f);
    r.openFull();
    render ("lfo_full_sine", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.tapC (r.ui.full().swing);
    render ("lfo_full_motion_group", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.key ("lfo1.sync", 4.0f);
    r.tapC (r.ui.full().keyTranspose);
    render ("lfo_full_key_follow", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.key ("lfo1.sync", 1.0f);
    r.key ("lfo1.shape", 4.0f);
    r.key ("lfo1.sync_type", 3.0f);
    r.key ("lfo1.phase", 140.0f);
    r.tapC (r.ui.full().rate.division);
    render ("lfo_list_division", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.stack.back();
    r.refresh();
    render ("lfo_full_square_envelope", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    // the editor, with a drawn shape
    r.key ("lfo1.sync_type", 0.0f);
    r.key ("lfo1.shape", 1.0f);
    r.tapC (r.ui.full().edit);
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    r.tap (static_cast<int> (cv.xOf (0.25f)), static_cast<int> (cv.yOf (0.9f)));
    r.tap (static_cast<int> (cv.xOf (0.75f)), static_cast<int> (cv.yOf (0.1f)));
    r.ui.editor().model.setPower (1, 6.0f);
    r.refresh();
    render ("lfo_editor", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.tapC (r.ui.editor().back);
    // the targets page and a long press page
    r.tapC (r.ui.full().targets);
    r.tapC (r.ui.targetsPage().add);
    render ("lfo_list_targets_groups", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.stack.back();
    r.refresh();
    for (int i = 0; i < 3; ++i)
    {
        const int dests[3] = { paramIdx ("filter1.cutoff"), paramIdx ("osc1.pan"), paramIdx ("lfo2.rate") };
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (i);
        uibind::writePlain (r.inst, sp.source, 7);
        uibind::writePlain (r.inst, sp.destination, static_cast<double> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (dests[i]))));
        uibind::writePlain (r.inst, sp.amount, 30.0 * (i + 1) - 40.0);
    }
    r.stack.back();
    r.refresh();
    r.tapC (r.ui.full().targets);
    r.refresh();
    render ("lfo_targets", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.stack.closeAll();
    r.refresh();
    // a random LFO
    r.stack.closeAll();
    r.refresh();
    r.openFull();
    r.tap (r.ui.full().bar.rndCells.cell (3).x + 4, r.ui.full().bar.rndCells.cell (3).y + 20);
    for (int i = 0; i < 60; ++i)
    {
        r.live.source[17] = 0.5f + 0.45f * std::sin (static_cast<float> (i) * 0.35f) * std::cos (static_cast<float> (i) * 0.11f);
        r.live.numLfoVoices = 1;
        r.now += 40.0;
        r.refresh();
    }
    render ("lfo_full_random", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawPage (sb); r.ui.drawLive (sb); r.ui.drawPopup (sb); });
    r.stack.closeAll();
    r.refresh();
    render ("lfo_compact_random", 1280, 740, [&] (gpu::SceneBuilder& sb) { r.ui.drawCompact (sb); r.ui.drawLive (sb); });
}

void testLfoParameterCoverage()
{
    // B1.b: Every parameter of every LFO (lfo1..lfo10, 17 params each)
    // and every random LFO (rlfo1..rlfo4, 8 params each) must have a control
    // on the page in some state and be a member of some Q group.
    for (int lfo = 1; lfo <= 10; ++lfo)
    {
        const std::string pre = "lfo" + std::to_string (lfo) + ".";
        int tested = 0;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        {
            const synthyboy::ParamInfo& pi = synthyboy::paramTable()[i];
            if (std::strncmp (pi.key, pre.c_str(), pre.size()) != 0)
                continue;
            ++tested;
            bool foundInQ = false;
            for (float sync : { 0.0f, 1.0f, 4.0f })
            {
                Rig r;
                r.inst.sel[2] = lfo;
                r.inst.set (pre + "sync", sync);
                r.refresh();
                r.openFull();
                for (int g = 0; g < r.ui.full().numGroups() && ! foundInQ; ++g)
                {
                    r.ui.full().setActiveGroup (g);
                    r.refresh();
                    for (int m = 0; m < 4; ++m)
                    {
                        if (r.inst.q[m] == static_cast<int> (i))
                        {
                            foundInQ = true;
                            break;
                        }
                    }
                }
            }
            check (foundInQ, std::string (pi.key) + " is mapped to a control and reachable in a Q group");
        }
        check (tested == 17, "LFO " + std::to_string (lfo) + ": all 17 engine parameters checked");
    }

    for (int rlfo = 1; rlfo <= 4; ++rlfo)
    {
        const std::string pre = "rlfo" + std::to_string (rlfo) + ".";
        int tested = 0;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        {
            const synthyboy::ParamInfo& pi = synthyboy::paramTable()[i];
            if (std::strncmp (pi.key, pre.c_str(), pre.size()) != 0)
                continue;
            ++tested;
            bool foundInQ = false;
            for (float sync : { 0.0f, 1.0f, 4.0f })
            {
                Rig r;
                r.inst.sel[2] = 1;
                r.refresh();
                r.openFull();
                r.tap (r.ui.full().bar.rndCells.cell (rlfo - 1).x + 4, r.ui.full().bar.rndCells.cell (rlfo - 1).y + 20);
                r.inst.set (pre + "sync", sync);
                r.refresh();
                for (int g = 0; g < r.ui.full().numGroups() && ! foundInQ; ++g)
                {
                    r.ui.full().setActiveGroup (g);
                    r.refresh();
                    for (int m = 0; m < 4; ++m)
                    {
                        if (r.inst.q[m] == static_cast<int> (i))
                        {
                            foundInQ = true;
                            break;
                        }
                    }
                }
            }
            check (foundInQ, std::string (pi.key) + " is mapped to a control and reachable in a Q group");
        }
        check (tested == 8, "random LFO " + std::to_string (rlfo) + ": all 8 engine parameters checked");
    }
}
}

int main()
{
    if (const char* dir = std::getenv ("LFO_IMAGES"))
        mkdir (dir, 0755);
    testLfoParameterCoverage();
    testBindings();
    testCompactQGroup();
    testWrites();
    testFullScreen();
    testEditor();
    testTargets();
    testDisplay();
    testTouchAndDraw();
    std::printf ("%s: %d failed\n", uit::g_fails ? "FAIL" : "PASS", uit::g_fails);
    return uit::g_fails ? 1 : 0;
}
