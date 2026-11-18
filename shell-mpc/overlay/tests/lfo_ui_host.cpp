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

#include <filesystem>
#include <fstream>

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
    void wavetableSources (synthyboy::CatalogueSources& out) const override { out = synthyboy::CatalogueSources {}; out.userRoots = userRoots; }
    void requestWavetable (int osc, const synthyboy::ContentFileRef& ref) override { wtOsc = osc; wtRef = ref; ++wtRequests; }
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
    std::vector<std::string> userRoots;   // the places the player's own files live (the card): a temporary directory in the tests of the saved shapes
    int wtOsc = 0, wtRequests = 0;
    synthyboy::ContentFileRef wtRef;
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
    auto ctq = [] (Rig& x, int i) { int t[4]; x.ui.compactTargets (t); return t[i]; };   // the main state's group is published by the Play view: the LFO's compact group is what it asks for
    Rig r;
    check (ctq (r, 0) == paramIdx ("lfo1.shape") && ctq (r, 1) == paramIdx ("lfo1.rate") && ctq (r, 2) == paramIdx ("lfo1.fade_in") && ctq (r, 3) == paramIdx ("lfo1.smooth_time"),
           "compact: the Q group is SHAPE, RATE, RISE, SMOOTH time of lfo1");
    check (r.ui.compact().group.active(), "compact: its frame is on (the LFO sub-tab)");
    r.key ("lfo1.sync", 1.0f);
    check (ctq (r, 1) == paramIdx ("lfo1.division"), "compact: tempo sync: Q2 is the division");
    r.key ("lfo1.sync", 4.0f);
    check (ctq (r, 1) == paramIdx ("lfo1.rate") && ! r.ui.compact().rate.rate.enabled(), "compact: key follow: Q2 stays the rate, the knob is dim");
    r.inst.sel[2] = 7;
    r.refresh();
    check (ctq (r, 0) == paramIdx ("lfo7.shape"), "compact: the group follows the Play LFO selector (lfo7)");
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
    check (ctq (q, 0) == paramIdx ("rlfo2.style") && ctq (q, 1) == paramIdx ("rlfo2.rate") && ctq (q, 2) == paramIdx ("rlfo2.sync") && ctq (q, 3) == paramIdx ("rlfo2.stereo"), "random compact: STYLE, RATE, SYNC, STEREO of rlfo2");
    // a tap on the name opens the list of all 14 entries over the third (no page of the stack), a tap on a row chooses
    {
        Rig l;
        l.inst.sel[2] = 1;
        l.refresh();
        const ui::Rect s = l.ui.compact().instance.rect();
        l.tap (s.x + s.w / 2, s.y + s.h / 2);
        const ui::ListPopup& lp = l.ui.list();
        check (l.stack.isMain() && lp.state.count == 14 && lp.rowRect (0).h >= 56 && std::string (lp.state.items[0]) == "LFO 1" && std::string (lp.state.items[13]) == "RND 4" && lp.scrolls(),
               "LFO selector: a tap on the name opens the list of the 14 entries (rows >= 56 px, it scrolls), over the third");
        const ui::Rect row = lp.rowRect (3);
        l.tap (row.x + row.w / 2, row.y + row.h / 2);
        check (l.inst.sel[2] == 4 && l.stack.isMain(), "a tap on the fourth row: LFO 4");
    }
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
        const ui::LedList& mc = r.ui.compact().rate.modeCells;
        r.tap (mc.rowRect (1).x + 20, mc.rowRect (1).y + 20);
        check (r.inst.get ("lfo1.sync") == 1.0f && r.ui.compact().rate.mode() == ui::lfo::SyncMode::Bpm && r.ui.compact().rate.primary() == &r.ui.compact().rate.division, "the compact view: the BPM cell makes the rate a division");
        r.tap (mc.rowRect (0).x + 20, mc.rowRect (0).y + 20);
        check (r.inst.get ("lfo1.sync") == 0.0f, "the compact view: the HZ cell");
    }
    // the switches of the sync
    r.openFull();
    Rig& R = r;
    R.stack.closeAll();
    R.refresh();
    R.openFull();
    const ui::SyncSwitch& sw = R.ui.full().syncSwitch;
    const ui::RateControl& rc = R.ui.full().rate;
    R.tap (sw.mode.rowRect (1).x + 20, sw.mode.rowRect (1).y + 20);
    check (R.inst.get ("lfo1.sync") == 1.0f, "the BPM cell: sync = tempo");
    check (rc.trip.rect().h >= 56 && rc.dot.rect().h >= 56 && ! rc.trip.rect().overlaps (rc.division.rect()), "in the tempo mode TRIP and DOT stand under the division (LED toggles of 56 px)");
    R.tapC (rc.dot);
    check (R.inst.get ("lfo1.sync") == 2.0f, "DOT: tempo dotted");
    R.tapC (rc.trip);
    check (R.inst.get ("lfo1.sync") == 3.0f, "TRIP: tempo triplet (it takes DOT off)");
    R.tap (sw.mode.rowRect (2).x + 20, sw.mode.rowRect (2).y + 20);
    check (R.inst.get ("lfo1.sync") == 4.0f && R.ui.full().keyActive() && R.ui.full().keyTranspose.enabled(), "the KEY cell: key follow, the KEY block works");
    R.tap (sw.mode.rowRect (0).x + 20, sw.mode.rowRect (0).y + 20);
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
    {   // SMOOTH is ONE control: the time knob with the switch as the LED on its label (a tap on the label / the LED switches it, off = the knob is dim and takes only the switch)
        ui::Knob& k = R.ui.full().smoothTime;
        const ui::Rect z = k.ledHit();
        check (k.state.led && k.state.ledOn == (smooth >= 0.5f), "the SMOOTH knob shows the state of the switch on its LED (" + std::to_string (smooth) + ")");
        R.tap (z.x + z.w - 10, z.y + z.h / 2);   // on the text column, off the dial
        check (R.inst.get ("lfo1.smooth") != smooth && k.state.ledOn == (R.inst.get ("lfo1.smooth") >= 0.5f), "a tap on the label of SMOOTH switches the smoothing");
        if (R.inst.get ("lfo1.smooth") >= 0.5f)
            R.tap (z.x + z.w - 10, z.y + z.h / 2);
        check (R.inst.get ("lfo1.smooth") < 0.5f && ! k.state.ledOn, "the smoothing is off: the LED is unlit");
        const float t0 = R.inst.get ("lfo1.smooth_time");
        R.drag (k.rect().x + 30, k.rect().y + k.rect().h / 2, k.rect().x + 30, k.rect().y + k.rect().h / 2 - 60, true);
        check (R.inst.get ("lfo1.smooth_time") == t0, "with the smoothing off a drag on the dial changes nothing (the knob is dim)");
        R.tap (z.x + z.w - 10, z.y + z.h / 2);
        check (R.inst.get ("lfo1.smooth") >= 0.5f && k.state.ledOn, "a tap on the label switches the smoothing on again");
        R.drag (k.rect().x + 30, k.rect().y + k.rect().h / 2, k.rect().x + 30, k.rect().y + k.rect().h / 2 - 60, true);
        check (R.inst.get ("lfo1.smooth_time") != t0, "and the dial sets the time (" + std::to_string (R.inst.get ("lfo1.smooth_time")) + ")");
    }
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
        check (! has (&r.ui.full().shape) && ! has (&r.ui.full().sync) && has (&r.ui.full().monoQ) && ! has (&r.ui.full().monoTop) && r.ui.full().groups[1].member (0) == &r.ui.full().syncSwitch.mode, "no SHAPE stepper, no SYNC stepper (the HZ / BPM / KEY row is the Timing group's Q1), MONO in Motion");
    }
    r.tapC (r.ui.full().swing);
    check (r.ui.activeQ() == 2 && r.inst.q[0] == paramIdx ("lfo1.direction") && r.inst.q[1] == paramIdx ("lfo1.swing") && r.inst.q[2] == paramIdx ("lfo1.stereo") && r.inst.q[3] == paramIdx ("lfo1.mono"),
           "a touch on SWING: the MOTION group (direction, swing, stereo, mono)");
    r.tapC (r.ui.full().mode);
    check (r.stack.depth() == 2 && r.ui.list().state.count == 6, "MODE: the list of the six sync types opens");
    r.stack.back();
    r.refresh();
    check (r.ui.activeQ() == 1 && r.inst.q[0] == paramIdx ("lfo1.sync") && r.inst.q[1] == paramIdx ("lfo1.phase") && r.inst.q[2] == paramIdx ("lfo1.delay") && r.inst.q[3] == paramIdx ("lfo1.sync_type"),
           "the TIMING group (sync, phase, delay, mode: the reading order of the block)");
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
    check (r.ui.random() && r.ui.selection() == 2 && r.ui.full().random() && r.ui.full().numGroups() == 2, "RND 2: the random LFO, the full screen of the random LFO");
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

// ---- the editor: the marker, the quick delete and the selection, the saved shapes, the actions, the modulation of points
struct TempDir
{
    std::string path;
    TempDir()
    {
        std::string base = std::filesystem::temp_directory_path().string() + "/synthyboy_lfo_XXXXXX";
        std::vector<char> t (base.begin(), base.end());
        t.push_back ('\0');
        path = ::mkdtemp (t.data()) != nullptr ? std::string (t.data()) : std::string();
    }
    ~TempDir() { if (! path.empty()) { std::error_code ec; std::filesystem::remove_all (path, ec); } }
};

/** Opens the editor of LFO 1 on a drawn shape of the given points (the content of the instance has it). */
void openEditorOn (Rig& r, const ui::lfo::ShapeModel& m)
{
    r.inst.set ("lfo1.shape", 12.0f);
    r.inst.setLfoShape (1, LfoUi::contentOf (1, m, 8, 8));
    r.refresh();
    if (r.stack.depth() == 0)
        r.openFull();
    r.tapC (r.ui.full().edit);
    r.ui.editor().canvas.state.gridX = r.ui.editor().canvas.state.gridY = 8;
    r.ui.editor().canvas.state.snap = false;
}
ui::lfo::ShapeModel triangleWith (std::initializer_list<std::pair<float, float>> more)
{
    ui::lfo::ShapeModel m = ui::lfo::ShapeModel::fromBuiltin (1);
    for (const auto& p : more)
        m.insert (p.first, p.second);
    return m;
}
void chooseRow (Rig& r, int row)
{
    const ui::Rect rr = r.ui.list().rowRect (row);
    r.tap (rr.x + 20, rr.y + 20);
}
void typeName (Rig& r, const std::string& name)
{
    for (char c : name)
    {
        const int k = c == ' ' ? r.ui.keyboard().keyOfKind (ui::TextEntry::Space) : r.ui.keyboard().keyOf (c);
        const ui::TextEntry::Key& key = r.ui.keyboard().key (k);
        r.tap (key.r.x + key.r.w / 2, key.r.y + key.r.h / 2);
    }
}
void pressKey (Rig& r, ui::TextEntry::KeyKind kind)
{
    const ui::TextEntry::Key& key = r.ui.keyboard().key (r.ui.keyboard().keyOfKind (kind));
    r.tap (key.r.x + key.r.w / 2, key.r.y + key.r.h / 2);
}
/** An action of the MORE list: the group (0 EDIT, 1 GRID, 2 COPY) and the row in it. */
void more (Rig& r, int group, int row)
{
    r.tapC (r.ui.editor().more);
    chooseRow (r, group);
    chooseRow (r, row);
}

void testEditorMarker()
{
    Rig r;
    r.inst.set ("lfo1.sync_type", 4.0f);   // loop at point
    openEditorOn (r, triangleWith ({}));
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    check (cv.state.markerKind == ui::ShapeCanvas::MarkerLoop && std::fabs (cv.state.markerX - r.inst.get ("lfo1.phase") / 360.0f) < 1.0e-4f, "loop at point: the editor shows the LOOP marker at the PHASE of the LFO");
    // the Q groups: TRIGGER = MODE, PHASE (parameters of the table), BEND ALL (virtual 7); EDIT is the active group and is on Q1 .. Q4
    check (r.inst.q[8] == paramIdx ("lfo1.sync_type") && r.inst.q[9] == paramIdx ("lfo1.phase") && r.inst.q[10] == QRouter::encode (7, QRouter::kVirtual) && r.inst.q[11] == -1, "the TRIGGER group is on Q9 .. Q11: MODE, PHASE, BEND ALL");
    check (r.ui.editor().mode.state.index == 4 && std::string (r.ui.editor().mode.state.text) == "Loop at point", "the MODE of the LFO is on the page");
    r.inst.set ("lfo1.phase", 90.0f);
    r.refresh();
    check (std::fabs (cv.state.markerX - 0.25f) < 1.0e-4f && std::fabs (static_cast<float> (r.ui.editor().fphase.state.value) - 90.0f) < 1.0e-3f, "the marker and the PHASE field follow the parameter");
    // dragging the marker: the PHASE is the point it snaps to, the loop point of the model too
    const int w0 = r.inst.shapeWrites;
    r.drag (static_cast<int> (cv.xOf (0.25f)), cv.plot().y + 20, static_cast<int> (cv.xOf (0.52f)), cv.plot().y + 20, 8);
    check (std::fabs (r.inst.get ("lfo1.phase") - 180.0f) < 0.1f && r.ui.editor().model.loop == 1 && r.ui.editor().model.n == 3, "dragging the LOOP marker sets the PHASE (180 degrees = the peak) and the loop point (point 2)");
    check (r.inst.shapeWrites > w0 && r.inst.contentPtr->lfoShapes[0].loopPoint == 1, "the loop point goes to the content");
    check (r.ui.editor().undo.depth() == 1, "the marker drag is one undo state");
    // the loop point moved with its point: the PHASE follows while they belong together
    r.tap (static_cast<int> (cv.xOf (0.5f)), static_cast<int> (cv.yOf (1.0f)));
    check (cv.state.selected == 1, "the peak is selected");
    r.inst.hostQ (1, 0.6f);   // X
    r.refresh();
    check (std::fabs (r.ui.editor().model.p[1].x - 0.6f) < 1.0e-4f && std::fabs (r.inst.get ("lfo1.phase") - 216.0f) < 0.1f, "moving the loop point in time moves the PHASE with it (216 degrees)");
    // the PHASE field moves the marker
    r.inst.set ("lfo1.phase", 36.0f);
    r.refresh();
    check (std::fabs (cv.state.markerX - 0.1f) < 1.0e-4f, "the marker follows the PHASE (36 degrees = 0.1)");
    // the other trigger modes: START (retrigger, envelope), dimmed with a hint (free-run)
    for (int mode = 0; mode < 6; ++mode)
    {
        r.inst.set ("lfo1.sync_type", static_cast<float> (mode));
        r.refresh();
        check (cv.state.markerKind == ui::ShapeEditorPage::markerKindOf (mode) && cv.state.markerKind == (mode == 1 ? ui::ShapeCanvas::MarkerDim : (mode >= 3 ? ui::ShapeCanvas::MarkerLoop : ui::ShapeCanvas::MarkerStart)), "the marker kind of the trigger mode " + std::to_string (mode));
    }
    // the MODE stepper: a tap on its name opens the list of the six trigger types, a choice writes the parameter
    r.tapC (r.ui.editor().mode);
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 6, "MODE: the list of the six trigger types opens over the editor");
    chooseRow (r, 3);
    check (r.ui.topPage() == LfoUi::kPageEditor && r.inst.get ("lfo1.sync_type") == 3.0f, "a choice of the list writes lfo1.sync_type and closes the list (the editor is below)");
    // the LOOP toggle sets the loop point at the selected point (and the PHASE in a loop mode)
    r.inst.set ("lfo1.phase", 0.0f);
    r.ui.editor().model.loop = -1;
    r.refresh();
    r.tap (static_cast<int> (cv.xOf (0.6f)), static_cast<int> (cv.yOf (1.0f)));
    r.tapC (r.ui.editor().loop);
    check (r.ui.editor().model.loop == 1 && std::fabs (r.inst.get ("lfo1.phase") - 216.0f) < 0.1f, "LOOP: the selected point is the loop point, the PHASE goes to it");
}

void testEditorQuickDeleteAndSelection()
{
    Rig r;
    openEditorOn (r, triangleWith ({ { 0.25f, 0.5f }, { 0.75f, 0.5f } }));
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    ui::lfo::ShapeModel& m = r.ui.editor().model;
    check (m.n == 5, "five points");
    const ui::lfo::ShapeModel start = m;
    // a double tap on a point deletes it (the touch path: two taps in a row)
    const int px = static_cast<int> (cv.xOf (0.25f)), py = static_cast<int> (cv.yOf (0.5f));
    r.tap (px, py, 30);
    check (m.n == 5 && cv.state.selected == 1, "the first tap selects");
    r.tap (px, py, 30);
    check (m.n == 4 && r.inst.contentPtr->lfoShapes[0].points.size() == 4 && r.ui.editor().undo.depth() == 1, "the second tap deletes it: the content has four points, one undo state");
    r.tapC (r.ui.editor().undoButton);
    check (m == start && r.inst.contentPtr->lfoShapes[0].points.size() == 5, "UNDO restores the point exactly (and the content)");
    // the SELECT tool: a rectangle over the two inner points of the sides, then DELETE
    r.tap (r.ui.editor().tools.cell (4).x + 10, r.ui.editor().tools.cell (4).y + 10);
    check (cv.state.tool == ui::ToolSelector::Select, "the SELECT tool");
    r.drag (static_cast<int> (cv.xOf (0.15f)), static_cast<int> (cv.yOf (1.0f)) - 15, static_cast<int> (cv.xOf (0.85f)), static_cast<int> (cv.yOf (0.3f)), 8);
    check (cv.selection().count() == 3 && r.ui.editor().del.enabled() && std::string (r.ui.editor().fpoint.state.text) == "3 SEL", "the rectangle selects the three inner points under it, the POINT field shows the count (" + std::to_string (cv.selection().count()) + ")");
    r.tapC (r.ui.editor().del);
    check (m.n == 2 && m.p[0].x == 0.0f && m.p[1].x == 1.0f, "DELETE removes the three selected points, the end points stay (" + std::to_string (m.n) + " left)");
    check (r.inst.contentPtr->lfoShapes[0].points.size() == static_cast<size_t> (m.n), "the content follows the DELETE");
    r.tapC (r.ui.editor().undoButton);
    check (m == start, "one UNDO brings every deleted point back");
    // the 128 point limit: a full shape adds nothing with the POINT tool
    ui::lfo::ShapeModel full = ui::lfo::ShapeModel::fromBuiltin (2);
    for (int i = 1; i < 127; ++i)
        full.insert (static_cast<float> (i) / 127.0f, (i % 2) != 0 ? 0.2f : 0.8f);
    r.tapC (r.ui.editor().back);
    openEditorOn (r, full);
    r.tap (r.ui.editor().tools.cell (0).x + 10, r.ui.editor().tools.cell (0).y + 10);
    const int before = r.ui.editor().model.n;
    r.tap (static_cast<int> (r.ui.editor().canvas.xOf (0.333f)), static_cast<int> (r.ui.editor().canvas.yOf (0.5f)));
    check (before == ui::lfo::kMaxPoints && r.ui.editor().model.n == ui::lfo::kMaxPoints, "128 points: a tap on the canvas adds nothing");
}

void testShapeLibrary()
{
    TempDir tmp;
    LfoShapeLibrary lib;
    lib.setRoots ({ tmp.path });
    lib.refresh();
    check (lib.count() == 0 && lib.canSave(), "the library: nothing saved yet, a card is there");
    ui::lfo::ShapeModel m = ui::lfo::ShapeModel::fromBuiltin (1);
    m.insert (0.25f, 0.3f);
    m.p[1].power = 5.5f;
    m.smooth = true;
    m.loop = 2;
    check (lib.save ("Wobble", m, false) && lib.count() == 1 && lib.items()[0].name == "Wobble", "save: a new shape");
    check (std::filesystem::exists (tmp.path + "/SynthyBoy/LFO Shapes/Wobble.lfoshape.json"), "the file is SynthyBoy/LFO Shapes/<name>.lfoshape.json below the root");
    // the file is the core's format (the desktop reads it) with the loop point added
    std::ifstream in (tmp.path + "/SynthyBoy/LFO Shapes/Wobble.lfoshape.json");
    const std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    synthyboy::LfoShapeFile coreFile;
    check (synthyboy::parseLfoShapeFile (text, coreFile) && coreFile.name == "Wobble" && coreFile.smooth && coreFile.points.size() == 4 && text.find ("\"loop_point\":2") != std::string::npos, "the file is read by the core's parser (name, smooth, four points) and has the loop point");
    ui::lfo::ShapeModel back;
    check (lib.load (0, back) && back == m, "save then load gives an equal model (points, curves, SMOOTH, the loop point)");
    check (! lib.save ("wobble", m, false) && lib.error() == "THAT NAME IS TAKEN" && lib.count() == 1, "save without overwrite refuses a taken name (any case)");
    ui::lfo::ShapeModel m2 = m;
    m2.smooth = false;
    check (lib.save ("wobble", m2, true) && lib.count() == 1, "save with overwrite replaces it");
    check (lib.load (0, back) && back == m2 && ! (back == m), "the replaced shape is the new one");
    check (lib.save ("Alpha", m, false) && lib.count() == 2 && lib.items()[0].name == "Alpha", "the list is sorted by name");
    check (lib.rename (0, "Beta") && lib.count() == 2 && lib.find ("Beta") >= 0 && lib.find ("Alpha") < 0 && lib.load (lib.find ("Beta"), back) && back == m, "rename: the file and the shape in it");
    check (! lib.rename (0, "wobble") && lib.error() == "THAT NAME IS TAKEN", "rename to a taken name is refused");
    check (lib.remove (lib.find ("Beta")) && lib.count() == 1 && ! std::filesystem::exists (tmp.path + "/SynthyBoy/LFO Shapes/Beta.lfoshape.json"), "delete removes the file");
    check (! lib.save ("   ", m, true) && lib.error() == "THE NAME IS EMPTY", "an empty name is refused");
    // a shape of the full 128 points
    ui::lfo::ShapeModel big = ui::lfo::ShapeModel::fromBuiltin (2);
    for (int i = 1; i < 127; ++i)
        big.insert (static_cast<float> (i) / 127.0f, (i % 2) != 0 ? 0.2f : 0.8f);
    check (big.n == 128 && lib.save ("Big", big, true) && lib.load (lib.find ("Big"), back) && back == big, "128 points survive save and load");
    // a file of the plain core format (no loop point), a plain .json, and a damaged file
    {
        std::ofstream o (tmp.path + "/SynthyBoy/LFO Shapes/FromImport.lfoshape.json");
        o << "{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":\"x\",\"smooth\":false,\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1,\"power\":3}]}";
        std::ofstream o2 (tmp.path + "/SynthyBoy/LFO Shapes/Plain.json");
        o2 << "{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":\"y\",\"smooth\":true,\"points\":[{\"x\":0,\"y\":1},{\"x\":1,\"y\":0}]}";
        std::ofstream o3 (tmp.path + "/SynthyBoy/LFO Shapes/Broken.lfoshape.json");
        o3 << "{\"format\":\"nothing\"}";
        std::ofstream o4 (tmp.path + "/SynthyBoy/LFO Shapes/.hidden.lfoshape.json");
        o4 << "{}";
    }
    lib.refresh();
    check (lib.count() == 5 && lib.find (".hidden") < 0, "a file of the core's format, a plain .json and a damaged one are listed, a hidden file is not");
    check (lib.load (lib.find ("FromImport"), back) && back.n == 2 && back.loop == -1 && std::fabs (back.p[0].power) < 1.0e-6f, "a file without a loop point loads with none");
    check (lib.load (lib.find ("Plain"), back) && back.smooth && back.n == 2, "a plain .json loads");
    check (! lib.load (lib.find ("Broken"), back) && lib.error() == "NOT A SHAPE FILE", "a damaged file is refused with a reason");
    // two roots: the first one wins a name, the list holds both
    TempDir other;
    lib.setRoots ({ tmp.path, other.path });
    lib.refresh();
    const int before = lib.count();
    {
        std::filesystem::create_directories (other.path + "/SynthyBoy/LFO Shapes");
        std::ofstream o (other.path + "/SynthyBoy/LFO Shapes/Wobble.lfoshape.json");
        o << "{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":\"other\",\"smooth\":false,\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}";
        std::ofstream o2 (other.path + "/SynthyBoy/LFO Shapes/Second.lfoshape.json");
        o2 << "{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":\"s\",\"smooth\":false,\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}";
    }
    lib.refresh();
    check (lib.count() == before + 1 && lib.find ("Second") >= 0, "two roots: a name both have is listed once (the first root's), the other root's shapes are listed too");
    check (lib.load (lib.find ("Wobble"), back) && back == m2, "the shape of the first root is the one that loads");
    // no root: nothing can be saved
    LfoShapeLibrary none;
    check (! none.canSave() && ! none.save ("X", m, true) && none.error() == "NO CARD OR DRIVE FOUND", "no card or drive: the save says so");
}

void testEditorShapesList()
{
    TempDir tmp;
    Rig r;
    r.inst.userRoots = { tmp.path };
    openEditorOn (r, triangleWith ({ { 0.25f, 0.5f } }));
    ui::lfo::ShapeModel& m = r.ui.editor().model;
    const ui::lfo::ShapeModel original = m;
    r.tapC (r.ui.editor().shapes);
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 6 && std::string (r.ui.list().state.items[2]) == "SAVE AS ...", "SHAPES opens the list: factory shapes, my shapes, save as, replace, rename, delete");
    // the factory shapes: the 12 built-in ones as starting points
    chooseRow (r, 0);
    check (r.ui.list().state.count == ui::lfo::kNumBuiltin && r.ui.list().state.headerBack, "FACTORY SHAPES: the 12 built-in shapes (the header goes back)");
    chooseRow (r, 4);   // SQUARE
    check (r.ui.topPage() == LfoUi::kPageEditor && m == ui::lfo::ShapeModel::fromBuiltin (4) && r.ui.editor().undo.depth() == 1, "a factory shape replaces the model: one undo state, the list closes");
    check (r.inst.contentPtr->lfoShapes[0].points.size() == 4, "... and the content has its points");
    r.tapC (r.ui.editor().undoButton);
    check (m == original, "UNDO brings the old shape back exactly");
    // SAVE AS: the keyboard, a name, OK
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 2);
    check (r.ui.topPage() == LfoUi::kPageKeyboard && std::string (r.ui.keyboard().title()) == "NAME OF THE SHAPE", "SAVE AS opens the name keyboard (the list closed)");
    typeName (r, "Wobble");
    check (r.ui.keyboard().text() == "Wobble", "the keyboard types the name");
    pressKey (r, ui::TextEntry::Ok);
    check (r.ui.topPage() == LfoUi::kPageEditor && std::string (r.ui.editor().status()) == "SAVED Wobble" && r.ui.library().count() == 1, "OK saves the shape and the status says so");
    check (std::filesystem::exists (tmp.path + "/SynthyBoy/LFO Shapes/Wobble.lfoshape.json"), "the file is on the card");
    // change the shape, save again under the same name: the replace question
    r.tap (r.ui.editor().tools.cell (1).x + 10, r.ui.editor().tools.cell (1).y + 10);
    r.tap (static_cast<int> (r.ui.editor().canvas.xOf (0.5f)), static_cast<int> (r.ui.editor().canvas.yOf (0.9f)));
    const ui::lfo::ShapeModel changed = m;
    check (! (changed == original), "the shape was changed");
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 2);
    typeName (r, "Wobble");
    pressKey (r, ui::TextEntry::Ok);
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 2 && std::string (r.ui.list().state.items[0]) == "REPLACE" && std::string (r.ui.list().state.items[1]) == "CANCEL", "a taken name asks once: REPLACE or CANCEL");
    chooseRow (r, 1);
    check (r.ui.topPage() == LfoUi::kPageEditor, "CANCEL closes the question");
    ui::lfo::ShapeModel stored;
    check (r.ui.library().load (0, stored) && stored == original, "CANCEL: the saved shape is still the old one");
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 2);
    typeName (r, "Wobble");
    pressKey (r, ui::TextEntry::Ok);
    chooseRow (r, 0);
    check (r.ui.topPage() == LfoUi::kPageEditor && r.ui.library().load (0, stored) && stored == changed, "REPLACE: the saved shape is the new one");
    // LOAD: my shapes (loading replaces the model: one undo state)
    r.tapC (r.ui.editor().undoButton);   // back to the shape before the change (the saved one is the changed one)
    const ui::lfo::ShapeModel beforeLoad = m;
    const int undoBefore = r.ui.editor().undo.depth();
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 1);
    check (r.ui.list().state.count == 1 && std::string (r.ui.list().state.items[0]) == "Wobble", "MY SHAPES lists the saved shape");
    chooseRow (r, 0);
    check (r.ui.topPage() == LfoUi::kPageEditor && m == changed && r.ui.editor().undo.depth() == undoBefore + 1, "a saved shape loads: the model is the saved one, one undo state");
    r.tapC (r.ui.editor().undoButton);
    check (m == beforeLoad, "UNDO of the load restores the model exactly");
    // OVERWRITE a saved shape from the list
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 3);
    check (std::string (r.ui.list().state.header) == "REPLACE WHICH SHAPE?", "REPLACE A SAVED SHAPE lists the saved shapes");
    chooseRow (r, 0);
    check (r.ui.list().state.count == 2 && std::string (r.ui.list().state.items[0]) == "REPLACE", "... and asks once");
    chooseRow (r, 0);
    check (r.ui.library().load (0, stored) && stored == m, "the saved shape is replaced by the shape in the editor");
    // RENAME
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 4);
    chooseRow (r, 0);
    check (r.ui.topPage() == LfoUi::kPageKeyboard && r.ui.keyboard().text() == "Wobble" && std::string (r.ui.keyboard().title()) == "NEW NAME OF THE SHAPE", "RENAME opens the keyboard with the old name");
    pressKey (r, ui::TextEntry::Clear);
    typeName (r, "Pulse");
    pressKey (r, ui::TextEntry::Ok);
    check (r.ui.library().find ("Pulse") == 0 && r.ui.library().find ("Wobble") < 0 && ! std::filesystem::exists (tmp.path + "/SynthyBoy/LFO Shapes/Wobble.lfoshape.json"), "the shape has the new name, the old file is gone");
    // DELETE asks once; KEEP keeps it
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 5);
    chooseRow (r, 0);
    check (r.ui.list().state.count == 2 && std::string (r.ui.list().state.items[0]) == "DELETE", "DELETE A SAVED SHAPE asks once");
    chooseRow (r, 1);
    check (r.ui.library().count() == 1, "CANCEL keeps the shape");
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 5);
    chooseRow (r, 0);
    chooseRow (r, 0);
    check (r.ui.library().count() == 0 && std::string (r.ui.editor().status()) == "DELETED Pulse", "DELETE removes the file");
    // no card: the save says so
    r.inst.userRoots.clear();
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 2);
    check (r.ui.topPage() == LfoUi::kPageEditor && std::string (r.ui.editor().status()) == "NO CARD OR DRIVE FOUND", "no card or drive: SAVE AS says so and opens no keyboard");
    // the list closes with the backdrop
    r.tapC (r.ui.editor().shapes);
    r.stack.back();
    r.refresh();
    check (r.ui.topPage() == LfoUi::kPageEditor, "the editor is below the list");
}

void testEditorActions()
{
    TempDir tmp;
    Rig r;
    r.inst.userRoots = { tmp.path };
    ui::lfo::ShapeModel start = ui::lfo::ShapeModel::fromBuiltin (1);
    start.p[0].power = 3.0f;
    start.insert (0.25f, 0.4f);
    openEditorOn (r, start);
    ui::lfo::ShapeModel& m = r.ui.editor().model;
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    // EDIT group: select all, flip horizontally, flip vertically, shift, reset curves
    r.tapC (r.ui.editor().more);
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 3 && std::string (r.ui.list().state.items[0]) == "EDIT" && std::string (r.ui.list().state.items[2]) == "COPY", "MORE opens the list of the three groups");
    chooseRow (r, 0);
    check (r.ui.list().state.count == 7 && r.ui.list().state.headerBack, "the EDIT group has seven actions (the header goes back)");
    chooseRow (r, 0);
    check (r.ui.topPage() == LfoUi::kPageEditor && cv.selection().count() == m.n, "SELECT ALL selects every point");
    const ui::lfo::ShapeModel a0 = m;
    more (r, 0, 1);
    ui::lfo::ShapeModel mirrored = a0;
    mirrored.flipH();
    check (m == mirrored && r.ui.editor().undo.depth() == 1 && r.inst.contentPtr->lfoShapes[0].points.size() == static_cast<size_t> (m.n), "FLIP HORIZONTAL: the shape mirrored, one undo state, the content follows");
    r.tapC (r.ui.editor().undoButton);
    check (m == a0, "UNDO restores the shape exactly");
    more (r, 0, 2);
    ui::lfo::ShapeModel inverted = a0;
    inverted.flipV();
    check (m == inverted, "FLIP VERTICAL");
    r.tapC (r.ui.editor().undoButton);
    more (r, 0, 4);   // SHIFT RIGHT by one grid column (8 columns)
    bool turned = true;
    for (int i = 0; i < 97; ++i)
    {
        const float x = (static_cast<float> (i) + 0.5f) / 97.0f;
        float src = x - 0.125f;
        src -= std::floor (src);
        turned = turned && std::fabs (m.eval (x) - a0.eval (src)) < 4.0e-3f;
    }
    check (turned, "SHIFT RIGHT: the shape turned by one grid column (1/8)");
    r.tapC (r.ui.editor().undoButton);
    check (m == a0, "UNDO of the shift restores the shape exactly");
    more (r, 0, 5);
    bool straight = true;
    for (int i = 0; i + 1 < m.n; ++i)
        straight = straight && m.p[i].power == 0.0f;
    check (straight, "RESET ALL CURVES");
    r.tapC (r.ui.editor().undoButton);
    // the loop point is cleared by an action
    m.loop = 1;
    more (r, 0, 6);
    check (m.loop == -1, "CLEAR LOOP POINT");
    // GRID group: presets, halve, double, follow the points
    more (r, 1, 3);
    check (cv.state.gridX == 16 && cv.state.gridY == 16 && r.ui.gridXOf (1) == 16, "GRID 16 x 16");
    more (r, 1, 5);
    check (cv.state.gridX == 8 && cv.state.gridY == 8, "HALVE THE GRID");
    more (r, 1, 6);
    check (cv.state.gridX == 16, "DOUBLE THE GRID");
    more (r, 1, 4);
    more (r, 1, 6);
    check (cv.state.gridX == 32, "DOUBLE stops at 32");
    more (r, 1, 0);
    more (r, 1, 5);
    check (cv.state.gridX == 2, "HALVE stops at 2");
    more (r, 1, 7);
    check (cv.state.gridX == ui::lfo::inferGrid (m) && cv.state.gridY == cv.state.gridX, "THE GRID FOLLOWS THE POINTS");
    // COPY group: to another LFO
    more (r, 2, 0);
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 9 && std::string (r.ui.list().state.items[0]) == "LFO 2", "COPY TO LFO ... lists the other nine LFOs");
    chooseRow (r, 1);   // LFO 3
    check (r.ui.topPage() == LfoUi::kPageEditor && r.inst.get ("lfo3.shape") == 12.0f && std::string (r.ui.editor().status()) == "COPIED TO LFO 3", "the shape is copied to LFO 3, which plays the drawn shape");
    bool found = false;
    for (const synthyboy::ContentLfoShape& s : r.inst.contentPtr->lfoShapes)
        if (s.lfo == 3)
        {
            found = LfoUi::modelOf (s) == m;
        }
    check (found, "the content of LFO 3 holds the same shape");
    // from an oscillator: OSC A (on, the default saw table) gives a shape; OSC C (off) says so
    more (r, 2, 1);
    check (r.ui.topPage() == LfoUi::kPageEditor && m.n >= 2 && m.n <= ui::lfo::kMaxPoints && std::string (r.ui.editor().status()) == "LOADED OSC A", "OSC A CYCLE TO THIS LFO: a shape from the table");
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        const float v = m.eval (static_cast<float> (i) / 99.0f);
        lo = std::min (lo, v); hi = std::max (hi, v);
    }
    check (lo >= 0.0f && hi <= 1.0f && hi - lo > 0.8f, "... that spans the range of the cycle");
    r.tapC (r.ui.editor().undoButton);
    more (r, 2, 3);
    check (std::string (r.ui.editor().status()) == "THAT OSCILLATOR HAS NO WAVETABLE CYCLE", "OSC C CYCLE: a switched off oscillator gives no shape");
    // to an oscillator: a WAV file in SynthyBoy/Wavetables, requested like a browser choice
    more (r, 2, 5);
    check (r.inst.wtRequests == 1 && r.inst.wtOsc == 2 && r.inst.wtRef.pack == "SynthyBoy/Wavetables" && r.inst.wtRef.file == "LFO 1 Shape.wav", "THIS SHAPE TO OSC B: the file is requested as the table of oscillator 2");
    const std::string wav = tmp.path + "/SynthyBoy/Wavetables/LFO 1 Shape.wav";
    check (std::filesystem::exists (wav) && std::filesystem::file_size (wav) > 8192 && std::filesystem::file_size (wav) < 9000, "the WAV file of one 2048 sample frame is on the card");
    more (r, 2, 5);
    check (r.inst.wtRef.file == "LFO 1 Shape 2.wav" && std::filesystem::exists (tmp.path + "/SynthyBoy/Wavetables/LFO 1 Shape 2.wav"), "a second export is a new file (a preset keeps the hash of the file it was made with)");
}

void testEditorPointModulation()
{
    Rig r;
    openEditorOn (r, triangleWith ({ { 0.25f, 0.5f } }));
    ui::lfo::ShapeModel& m = r.ui.editor().model;
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    check (! r.ui.editor().modButton.enabled(), "MOD is off while no point is selected");
    r.tap (static_cast<int> (cv.xOf (0.25f)), static_cast<int> (cv.yOf (0.5f)));
    check (cv.state.selected == 1 && r.ui.editor().modButton.enabled(), "a point is selected: MOD is on");
    r.tapC (r.ui.editor().modButton);
    check (r.ui.topPage() == LfoUi::kPagePointMod, "MOD opens the page of the modulation");
    ui::PointModPage& pm = r.ui.pointModPage();
    check (pm.target.state.index == 1 && ! pm.remove.enabled() && pm.board.numSources() > 10, "the page offers every modulation source, LEVEL is the value, nothing modulates yet");
    const ui::Rect chip = pm.board.chipRectOfId (7);   // LFO 1
    check (! chip.empty(), "the board has the source LFO 1");
    r.tap (chip.x + chip.w / 2, chip.y + chip.h / 2);
    check (m.p[1].my.source == 7 && std::fabs (m.p[1].my.amount - 0.5f) < 1.0e-4f && ! m.p[1].mx.on() && ! m.p[1].mc.on(), "a tap on a source modulates the level of the point by it at half depth");
    check (pm.remove.enabled() && pm.amount.enabled() && std::fabs (static_cast<float> (pm.amount.state.value) - 50.0f) < 1.0e-3f, "the page shows the source and the amount");
    check (r.ui.editor().undo.depth() >= 1, "the change is on the undo stack");
    check (r.inst.contentPtr->lfoShapes[0].points[1].modY.present && r.inst.contentPtr->lfoShapes[0].points[1].modY.source == synthyboy::modSourceTable()[6].key && std::fabs (r.inst.contentPtr->lfoShapes[0].points[1].modY.amount - 0.5f) < 1.0e-4f, "the content has the modulation of the level (source lfo1, 0.5)");
    // TIME is another value of the same point
    r.tap (pm.target.rect().x + pm.target.rect().w - 10, pm.target.rect().y + pm.target.rect().h / 2);   // the right half: next
    check (pm.target.state.index == 2 || pm.target.state.index == 0, "the stepper goes to another value (" + std::to_string (pm.target.state.index) + ")");
    r.tap (pm.target.rect().x + 10, pm.target.rect().y + pm.target.rect().h / 2);   // back to LEVEL
    check (pm.target.state.index == 1 && pm.remove.enabled(), "back to LEVEL: its source is shown again");
    r.tapC (pm.remove);
    check (! m.p[1].my.on() && ! r.inst.contentPtr->lfoShapes[0].points[1].modY.present, "REMOVE takes the modulation away (the content too)");
    // the conversion between the model and the content keeps every modulation
    ui::lfo::ShapeModel mm = triangleWith ({ { 0.25f, 0.5f } });
    ui::lfo::PointSet one;
    one.add (1);
    mm.setMod (one, 0, 8, -0.25f);
    mm.setMod (one, 1, 9, 0.75f);
    mm.setMod (one, 2, 11, 1.0f);
    check (LfoUi::modelOf (LfoUi::contentOf (1, mm)) == mm, "the content and back: the modulation of the time, the level and the curve is kept");
    check (synthyboy::validateContent ([&] { synthyboy::StateContent c; c.lfoShapes.push_back (LfoUi::contentOf (1, mm)); return c; }()), "the content with point modulation is valid");
    r.tapC (pm.back);
    check (r.ui.topPage() == LfoUi::kPageEditor, "BACK is the editor");
    // MOD for several points: one assignment for all
    r.tap (r.ui.editor().tools.cell (4).x + 10, r.ui.editor().tools.cell (4).y + 10);
    cv.selectAll();
    r.refresh();
    r.tapC (r.ui.editor().modButton);
    const ui::Rect chip2 = r.ui.pointModPage().board.chipRectOfId (9);
    r.tap (chip2.x + chip2.w / 2, chip2.y + chip2.h / 2);
    bool all = true;
    for (int i = 0; i < m.n; ++i)
        all = all && m.p[i].my.source == 9;
    check (all, "with every point selected the source goes to all of them");
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
    // a control nothing modulates: the page is the board of every source, a tap on a chip makes the modulation
    Rig q;
    q.refresh();
    q.stack.closeAll();
    q.refresh();
    q.centre (q.ui.compact().rate.rate, kx, ky);
    q.tap (kx, ky, 700);
    ui::ModulationsPage& mp = q.ui.modsPage();
    check (q.ui.topPage() == LfoUi::kPageMods && mp.numRows() == 0 && mp.boardShown() && mp.board.numSources() == static_cast<int> (synthyboy::modSourceCount()), "a long press on RATE with no modulation: the board of every source");
    const ui::Rect chip = mp.board.chipRectOfId (uibind::envSource (2));
    q.tap (chip.x + chip.w / 2, chip.y + chip.h / 2);
    int found = 0;
    for (int sl = 0; sl < synthyboy::kNumModSlots; ++sl)
        found += std::lround (q.inst.get (synthyboy::paramTable()[synthyboy::modSlotParams (sl).source].key)) == uibind::envSource (2);
    q.refresh();
    check (found == 1 && mp.numRows() == 1 && ! mp.boardShown(), "a tap on the ENV 2 chip makes the modulation of the RATE: one slot, one row, the board is gone");
}

/** The key follow sync (point 50 of the whole-UI review): the rate depends on the note, so the display has no time to smooth in: it shows the SHAPE, one clean cycle from the floor at full amplitude, with the smoothing on and off alike. */
void testKeyFollowCurve()
{
    Rig r;
    r.refresh();
    r.key ("lfo1.sync", 4.0f);
    r.key ("lfo1.shape", 0.0f);
    r.key ("lfo1.phase", 0.0f);
    r.key ("lfo1.smooth", 1.0f);
    r.key ("lfo1.smooth_time", 5.0f);
    r.now += 200.0;
    r.refresh();
    const ui::lfodisplay::Data& d = r.ui.displayData();
    check (d.area.empty() && r.ui.compactData().area == ui::playrows::plotRect (r.ui.compact().displayRect()) && ! r.ui.compactData().area.empty(), "the compact view's display data carries the plot area below the header band; the data of the full page and the editor carries none");
    std::vector<float> smoothOn (d.y, d.y + d.n);
    float lo = 1, hi = -1;
    for (float y : smoothOn) { lo = std::min (lo, y); hi = std::max (hi, y); }
    check (lo < -0.97f && hi > 0.97f, "key follow, SMOOTH on: the sine swings the whole height, -1 .. 1 (" + std::to_string (lo) + " .. " + std::to_string (hi) + ")");
    check (smoothOn[0] < -0.97f && smoothOn[smoothOn.size() - 1] < -0.97f, "key follow, SMOOTH on: one clean cycle from the floor to the floor (" + std::to_string (smoothOn[0]) + " .. " + std::to_string (smoothOn[smoothOn.size() - 1]) + ")");
    r.key ("lfo1.smooth", 0.0f);
    r.now += 200.0;
    r.refresh();
    float diff = 0.0f;
    for (int i = 0; i < d.n; ++i)
        diff = std::max (diff, std::fabs (d.y[i] - smoothOn[static_cast<size_t> (i)]));
    check (diff < 1.0e-6f, "key follow: the picture is the same with SMOOTH on and off (it shows the shape; " + std::to_string (diff) + ")");
    // in the free sync the smoothing is in the picture (a fast LFO is rounded by it)
    r.key ("lfo1.sync", 0.0f);
    r.key ("lfo1.rate", 200.0f);
    r.key ("lfo1.smooth", 1.0f);
    r.key ("lfo1.smooth_time", 5.0f);
    r.now += 200.0;
    r.refresh();
    float lo2 = 1, hi2 = -1;
    for (int i = 0; i < d.n; ++i) { lo2 = std::min (lo2, d.y[i]); hi2 = std::max (hi2, d.y[i]); }
    check (hi2 - lo2 < 1.9f, "free sync, a fast rate with SMOOTH: the smoothing is still drawn (" + std::to_string (lo2) + " .. " + std::to_string (hi2) + ")");
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
    testEditorMarker();
    testEditorQuickDeleteAndSelection();
    testShapeLibrary();
    testEditorShapesList();
    testEditorActions();
    testEditorPointModulation();
    testTargets();
    testDisplay();
    testKeyFollowCurve();
    testTouchAndDraw();
    std::printf ("%s: %d failed\n", uit::g_fails ? "FAIL" : "PASS", uit::g_fails);
    return uit::g_fails ? 1 : 0;
}
