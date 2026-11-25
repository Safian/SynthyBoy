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
#include "../gpu/cell_plan.h"
#include "../play_view.h"
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
    uint64_t stateGeneration() const noexcept override { return generation; }
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
    int tabView (int what) const noexcept override { return what >= 0 && what < 3 ? tv[what] : 0; }
    void setTabView (int what, int value) noexcept override { if (what >= 0 && what < 3) tv[what] = value; ++tvWrites; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    bool nativeLfoControls() const noexcept override { return nativeEnabled; }
    void setNativeLfoState (const nativeui::lfo::ViewState& state) noexcept override
    {
        native = state;
        nativeBank.publish (state);
        nativeBank.report (nativeBank.publication());
    }
    bool takeNativeLfo (const nativeui::lfo::ViewState& state, int field, float& value) noexcept override
    {
        return nativeBank.take (state, field, value);
    }
    bool nativeLfoShapeEditor() const noexcept override { return nativeEditorEnabled; }
    void setNativeLfoEditor (const nativeui::lfoeditor::State& state) noexcept override
    {
        nativeEditor = state;
        nativeEditorBank.publish (state);
        for (int field = 0; field < nativeui::lfoeditor::kCount; ++field)
            nativeEditorBank.report (nativeEditorBank.at (field));
    }
    bool takeNativeLfoEditor (const nativeui::lfoeditor::State& state, int field, float& value) noexcept override
    {
        return nativeEditorBank.take (state, field, value);
    }
    bool nativeLfoShapeDetails() const noexcept override { return nativeDetailEnabled; }
    void setNativeLfoDetail (const nativeui::lfodetail::State& state) noexcept override
    {
        nativeDetail.publish (state);
        for (int field = 0; field < nativeui::lfodetail::kCount; ++field)
            nativeDetail.report (nativeDetail.at (field));
    }
    bool takeNativeLfoDetail (const nativeui::lfodetail::State& state, int field, float& value) noexcept override
    {
        return nativeDetail.take (state, field, value);
    }
    bool nativeDetailEnabled = false;
    nativeui::lfodetail::Bank nativeDetail;
    bool nativePlayEnvLfoControls() const noexcept override { return nativePlayEnvLfoEnabled; }
    uint64_t nativePlayEnvLfoRevision() const noexcept override { return compactSelectionRevision; }
    void setNativePlayEnvLfoPopup (int field) noexcept override { nativePlayEnvLfo.setPopup (field); }
    void setNativePlayEnvLfo (const nativeui::playenvlfo::State& state) noexcept override { nativePlayEnvLfo.publish (state); }
    bool takeNativePlayEnvLfo (const nativeui::playenvlfo::State& state, int field, float& value) noexcept override
    {
        return nativePlayEnvLfo.take (state, field, value);
    }
    bool nativePlayEnvLfoEnabled = false;
    nativeui::playenvlfo::Bank nativePlayEnvLfo;
    uint64_t compactSelectionRevision = 0;
    bool nativeEditorEnabled = false;
    nativeui::lfoeditor::State nativeEditor;
    nativeui::lfoeditor::Bank nativeEditorBank;
    bool nativeEnabled = false;
    nativeui::lfo::ViewState native;
    nativeui::lfo::Bank nativeBank;
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
    int tv[3] = { 0, 0, 0 }, tvWrites = 0;   // the chunk-only view state (mpcTabView): the random LFO shown
    int sel[3] = { 2, 1, 1 }, lastSel = 0, q[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0, shapeWrites = 0;
    bool fullOpen = false;
    std::vector<std::string> userRoots;   // the places the player's own files live (the card): a temporary directory in the tests of the saved shapes
    int wtOsc = 0, wtRequests = 0;
    synthyboy::ContentFileRef wtRef;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    QRouter router;
    uint64_t generation = 0;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

struct Rig
{
    std::unique_ptr<LfoUi> editor = std::make_unique<LfoUi>();
    LfoUi& ui = *editor;
    Inst inst;
    UiStack stack;
    synthyboy::LiveValues live;
    bool useLive = false;
    double now = 1000.0;
    int hl = 3;
    bool editingNative = false;
    Rig() { inst.nativeEditorEnabled = inst.nativeDetailEnabled = true; refresh(); }
    void refresh()
    {
        now += 50.0;
        ui.update (inst, hl, stack, now, useLive ? &live : nullptr);
        if (ui.topPage() == LfoUi::kPageEditor) editingNative = true;
        if (stack.depth() <= 1) editingNative = false;
        ui.syncNativePlayEditor (inst, stack, hl, now, useLive ? &live : nullptr, editingNative);
    }
    // Emulate the host delivering real native callbacks for the retired GPU fixture taps.
    bool hostTap (int x, int y)
    {
        namespace e = nativeui::lfoeditor;
        namespace d = nativeui::lfodetail;
        if (ui.topPage() == LfoUi::kPageEditor)
            for (int f = 0; f < e::kCount; ++f)
                if (ui.nativeEditorRect (f).contains (x, y) && inst.nativeEditor.allows (f))
                {
                    ui.editor().selectGroupAt (x, y);
                    double plain = e::rangeOf (f, ui.editor().model.n, ui.editor().canvas.selection().count() > 1).decode (inst.nativeEditor.values[f]);
                    if (e::kFields[f].type == e::kToolChoice)
                    {
                        for (int t = 0; t < ui::ToolSelector::kTools; ++t) if (ui.nativeEditorToolRect (t).contains (x, y)) plain = t;
                    }
                    else if (e::kFields[f].type == e::kToggle) plain = plain < 0.5 ? 1 : 0;
                    else if (e::kFields[f].type == e::kAction) plain = 1;
                    const auto range = e::rangeOf (f, ui.editor().model.n, ui.editor().canvas.selection().count() > 1);
                    inst.nativeEditorBank.post (inst.nativeEditorBank.at (f), range.encode (plain));
                    ui.syncNativePlayEditor (inst, stack, hl, now, useLive ? &live : nullptr, true);
                    refresh();
                    return true;
                }
        const auto detail = inst.nativeDetail.display();
        if (detail.view != d::Hidden)
            for (int f = 0; f < d::kCount; ++f)
                if (ui.nativeDetailRect (f).contains (x, y))
                {
                    double plain = 1;
                    if (f == d::Target)
                    {
                        const auto rect = ui.nativeDetailRect (f);
                        plain = (ui.pointModPage().target.state.index + (x < rect.x + rect.w / 2 ? 2 : 1)) % 3;
                    }
                    else if (f == d::Amount) plain = ui.pointModPage().amount.state.value;
                    inst.nativeDetail.post (inst.nativeDetail.at (f), d::rangeOf (f).encode (plain));
                    ui.syncNativePlayEditor (inst, stack, hl, now, useLive ? &live : nullptr, true);
                    refresh();
                    return true;
                }
        return false;
    }
    bool tap (int cx, int cy, int holdMs = 40)
    {
        if (hostTap (cx, cy)) return true;
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
    void tapRow (int i) { const ui::Rect w = ui.list().rowRect (i); tap (w.x + w.w / 2, w.y + w.h / 2); }
};

int paramIdx (const std::string& k) { return Inst::idx (k); }

std::string cellsText (const ui::UsedSelector& s)
{
    int c[ui::UsedSelector::kMaxCells];
    const int n = s.cells (c, ui::UsedSelector::kMaxCells);
    std::string t;
    for (int k = 0; k < n; ++k)
        t += std::string (k ? " " : "") + (c[k] == ui::UsedSelector::kPlus ? "+" : "[" + std::string (s.labelOf (c[k])) + "]");
    return t;
}
/** A tap on the selector cell with this label ("+" for the plus cell). */
bool tapCell (Rig& r, const char* label)
{
    const ui::UsedSelector& sel = r.ui.full().selector;
    int cs[ui::UsedSelector::kMaxCells];
    const int n = sel.cells (cs, ui::UsedSelector::kMaxCells);
    for (int k = 0; k < n; ++k)
        if (std::string (cs[k] == ui::UsedSelector::kPlus ? "+" : sel.labelOf (cs[k])) == label)
        {
            const ui::Rect c = sel.cell (k, n);
            r.tap (c.x + c.w / 2, c.y + c.h / 2);
            return true;
        }
    return false;
}

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
            r.ui.select (slot, r.inst);   // (the random LFO as the selector's `+` pick would make it)
            r.refresh();
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
    q.inst.sel[2] = 1;
    q.refresh();
    q.ui.select (11, q.inst);
    q.refresh();
    check (q.ui.random() && q.ui.selection() == 2, "RND 2 is the selected LFO (the compact view shows the random LFO)");
    check (ctq (q, 0) == paramIdx ("rlfo2.style") && ctq (q, 1) == paramIdx ("rlfo2.rate") && ctq (q, 2) == paramIdx ("rlfo2.sync") && ctq (q, 3) == paramIdx ("rlfo2.stereo"), "random compact: STYLE, RATE, SYNC, STEREO of rlfo2");
    // The compact fallback shows STEREO in the RND layout. The standard
    // Rise position is inert while that layout is active.
    {
        const float fade = q.inst.get ("lfo1.fade_in");
        const int writes = q.inst.writes;
        int rx, ry;
        q.centre (q.ui.compact().rise, rx, ry);
        check (q.ui.covers (rx, ry), "random compact: the old RISE cell stays covered by the overlay");
        q.drag (rx, ry, rx, ry - 40);
        check (q.inst.get ("lfo1.fade_in") == fade && q.inst.writes == writes, "random compact: dragging the hidden RISE cell does not write lfo1.fade_in");
        int sx, sy;
        q.centre (q.ui.compact().stereo, sx, sy);
        const float stereo = q.inst.get ("rlfo2.stereo");
        q.tap (sx, sy);
        check (q.inst.get ("rlfo2.stereo") != stereo && q.inst.get ("lfo1.fade_in") == fade, "random compact: STEREO remains writable without touching standard LFO RISE");
    }
    // a tap on the name opens the list of the cells over the third (used + the selected one + "+", the same cells as the tab's selector; no page of the stack), a tap on a row chooses; "+" with two free kinds opens the two-row list
    {
        Rig l;
        l.ui.compact().setNativeInstanceSelector (true);   // Exercise the overlay selector where it owns the row.
        l.inst.sel[2] = 1;
        l.inst.set ("lfo4.rate", 3.0f);   // edited: used
        l.refresh(); l.refresh(); l.refresh();   // (the scan of the used items is made every 100 ms)
        const ui::Rect s = l.ui.compact().instance.rect();
        const int w0 = l.inst.writes;
        check (l.ui.compact().instance.enabled() && ! l.ui.compact().instance.inactive(), "the always-active LFO selector remains enabled and coloured");
        l.tap (s.x + s.w / 2, s.y + s.h / 2);
        const ui::ListPopup& lp = l.ui.list();
        check (l.stack.isMain() && lp.state.count == 3 && lp.rowRect (0).h >= 56 && std::string (lp.state.items[0]) == "LFO 1" && std::string (lp.state.items[1]) == "LFO 4" && std::string (lp.state.items[2]) == "+" && ! lp.scrolls(),
               "LFO selector: a tap on the name opens the list LFO 1, LFO 4, + (rows >= 56 px), over the third");
        const ui::Rect row = lp.rowRect (1);
        l.tap (row.x + row.w / 2, row.y + row.h / 2);
        check (l.inst.sel[2] == 4 && l.stack.isMain() && l.inst.writes == w0, "a tap on the second row: LFO 4, no parameter write");
        // "+" (the cells are now LFO 4 and +: LFO 1 is unused and not selected any more): two free kinds, the lowest free LFO and the lowest free random LFO
        l.tap (s.x + s.w / 2, s.y + s.h / 2);
        const ui::Rect plus = l.ui.list().rowRect (1);
        l.tap (plus.x + plus.w / 2, plus.y + plus.h / 2);
        const ui::ListPopup& pl = l.ui.list();
        check (l.stack.isMain() && pl.state.count == 2 && std::string (pl.state.items[0]) == "LFO 1" && std::string (pl.state.items[1]) == "RND 1", "+ with two free kinds: the two-row list LFO 1 / RND 1 opens over the third");
        const ui::Rect r1 = pl.rowRect (1);
        l.tap (r1.x + r1.w / 2, r1.y + r1.h / 2);
        check (l.ui.random() && l.ui.selection() == 1 && l.inst.tv[0] == 1, "a tap on RND 1: the random LFO is selected (view state only)");
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
        check (has (&r.ui.full().monoQ) && r.ui.full().groups[1].member (0) == &r.ui.full().syncSwitch.mode, "(U1-U3: the dead MONO, SHAPE, STYLE and SYNC steppers are gone from the class) no SHAPE stepper, no SYNC stepper (the HZ / BPM / KEY row is the Timing group's Q1), MONO in Motion");
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
    // the selector (LFO 1 .. 10, R1 .. R4: "used + one free"): taps choose, nothing is written
    r.openFull();
    {
        const ui::UsedSelector& sel = r.ui.full().selector;
        const int w0 = r.inst.writes;
        check (cellsText (sel) == "[1] +", "an empty preset: [1] +  (" + cellsText (sel) + ")");
        check (tapCell (r, "+") && r.stack.depth() == 2 && r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 2 && std::string (r.ui.list().state.items[0]) == "LFO 2" && std::string (r.ui.list().state.items[1]) == "RND 1",
              "'+': a two-row list LFO 2 / RND 1 (the lowest free of each kind)");
        r.tapRow (0);
        check (r.inst.sel[2] == 2 && r.ui.selection() == 2 && ! r.ui.random() && r.stack.depth() == 1 && cellsText (sel) == "[1] [2] +", "choosing LFO 2: selected and shown at once (" + cellsText (sel) + ")");
        check (r.inst.writes == w0, "choosing writes NO parameter");
        check (tapCell (r, "+"), "'+' again");
        r.tapRow (1);
        check (r.ui.random() && r.ui.selection() == 1 && r.inst.tv[0] == 1 && r.inst.sel[2] == 2 && cellsText (sel) == "[1] [R1] +", "choosing RND 1: the random LFO shown, the unused LFO 2 disappears, Play LFO keeps its last LFO (2): " + cellsText (sel));
        check (r.ui.full().random() && r.ui.full().numGroups() == 2 && r.inst.writes == w0, "the random LFO page (RANDOM, TRIGGER), still no write");
        tapCell (r, "1");
        check (! r.ui.random() && r.ui.selection() == 1 && r.inst.tv[0] == 0 && r.inst.sel[2] == 1 && cellsText (sel) == "[1] +", "back to LFO 1: the unused LFO 2 and R1 disappear again (" + cellsText (sel) + ")");
        check (r.inst.writes == w0, "no parameter was written by any selection");
    }
    // the selected item is the Play page's selection: a random LFO shows on both
    r.ui.select (11, r.inst);   // RND 2 (a position: 10 + 1)
    r.refresh();
    check (r.ui.random() && r.ui.selection() == 2 && r.inst.tv[0] == 2, "select (RND 2): mpcTabView.rnd = 2");
    check (r.inst.q[0] == paramIdx ("rlfo2.style") && r.inst.q[1] == paramIdx ("rlfo2.rate") && r.inst.q[2] == paramIdx ("rlfo2.sync") && r.inst.q[3] == paramIdx ("rlfo2.stereo"), "random full screen: the RANDOM group follows the selected random LFO");

    r.tap (r.ui.full().picker.cell (3).x + 10, r.ui.full().picker.cell (3).y + 10);
    check (r.inst.get ("rlfo2.style") == 3.0f, "the style picker: Lorenz");
    r.inst.sel[2] = 6;
    r.ui.select (5, r.inst);
    r.refresh();
    check (! r.ui.random() && r.ui.selection() == 6 && r.inst.tv[0] == 0, "back to LFO 6");
}

// ---- the ONE LFO tab (step 3 of docs/ui-map/env-filter-lfo-plan.md): the page, the recount of its controls, the selector, the Q-Links that follow it, what hides
struct PageRig
{
    std::unique_ptr<PageView<LfoUi>> editorPage = std::make_unique<PageView<LfoUi>> ("lfo1", 1);
    PageView<LfoUi>& page = *editorPage;
    LfoUi& ui = page.ui();
    Inst inst;
    UiStack stack;
    double now = 1000.0;
    const synthyboy::LiveValues* live = nullptr;
    int panelEdge = page::kW;
    PageRig() { inst.nativeEditorEnabled = inst.nativeDetailEnabled = true; stack.openFull (page.pageElement()); refresh(); }
    bool refresh()
    {
        ViewInput in;
        in.io = &inst;
        in.ui = &stack;
        in.nowMs = now += 150.0;
        in.live = live;
        in.panelEdgeX = panelEdge;
        return page.update (in);
    }
    void tap (int cx, int cy)
    {
        namespace n = nativeui::lfoeditor;
        if (ui.topPage() == LfoUi::kPageEditor)
            for (int f = 0; f < n::kCount; ++f)
                if (n::kFields[f].type == n::kAction && ui.nativeEditorRect (f).contains (cx, cy) && inst.nativeEditor.allows (f))
                {
                    inst.nativeEditorBank.post (inst.nativeEditorBank.at (f), 1.0f);
                    refresh();
                    return;
                }
        page.onTouch (ev (TouchEvent::Down, cx, cy), stack, inst, now);
        now += 40;
        page.onTick (stack, inst, now);
        page.onTouch (ev (TouchEvent::Up, cx, cy), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void tapC (const ui::Component& c) { tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2); }
    void key (const std::string& k, float v) { inst.set (k, v); refresh(); }
    void route (int slot, int source, const char* dest)
    {
        char k[32];
        std::snprintf (k, sizeof k, "mod%02d.", slot);
        inst.set (std::string (k) + "source", static_cast<float> (source));
        inst.set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx (dest)))));
        inst.set (std::string (k) + "amount", 30.0f);
        refresh();
    }
    bool tapCell (const char* label)
    {
        const ui::Rect c = ui.full().instance.rect();
        tap (c.x + c.w / 2, c.y + c.h / 2);   // the standalone tab's dropdown
        std::string wanted = label;
        if (wanted == "+") {}
        else if (wanted.size() > 1 && wanted[0] == 'R') wanted = "RND " + wanted.substr (1);
        else if (wanted.find ("LFO ") != 0) wanted = "LFO " + wanted;
        for (int i = 0; i < ui.list().state.count; ++i)
            if (std::string (ui.list().state.items[i]) == wanted)
            {
                for (int guard = 0; guard < 16 && (i < ui.list().firstRow() || i >= ui.list().firstRow() + ui.list().visibleRows()); ++guard)
                {
                    const ui::Rect z = ui.list().zoneRect();
                    tap (z.x + z.w / 2, i < ui.list().firstRow() ? z.y + 2 : z.bottom() - 2);
                }
                const int visible = i - ui.list().firstRow();
                if (visible < 0 || visible >= ui.list().visibleRows())
                    continue;
                const ui::Rect row = ui.list().rowRect (i);
                tap (row.x + row.w / 2, row.y + row.h / 2);
                return true;
            }
        return false;
    }
    /** The controls the page shows now: the leaves of the full screen (the SMOOTH knob counts twice: its time and the LED of its switch), no CLOSE (page mode). */
    int controls (std::string* names = nullptr)
    {
        ui::Component* leaves[160];
        const int nl = ui.full().collectLeaves (leaves, 0, 160);
        int n = 0;
        struct N { const ui::Component* c; const char* name; };
        const N all[] = { { &ui.full().selector, "selector" }, { &ui.full().instance, "instance" }, { &ui.full().namedPicker, "named waveform/style" }, { &ui.full().picker, "picker/styles" }, { &ui.full().edit, "EDIT" }, { &ui.full().targets, "TARGETS" }, { &ui.full().rate.rate, "RATE" }, { &ui.full().rate.division, "DIVISION" },
                          { &ui.full().rate.trip, "TRIP" }, { &ui.full().rate.dot, "DOT" }, { &ui.full().rise, "RISE" }, { &ui.full().smoothTime, "SMOOTH" }, { &ui.full().syncSwitch.mode, "HZ/BPM/KEY" }, { &ui.full().phase, "PHASE" },
                          { &ui.full().delay, "DELAY" }, { &ui.full().mode, "MODE" }, { &ui.full().modeList, "MODE (list)" }, { &ui.full().hostPick, "HOST" }, { &ui.full().direction, "DIRECTION" }, { &ui.full().swing, "SWING" },
                          { &ui.full().stereo, "STEREO" }, { &ui.full().monoQ, "MONO" }, { &ui.full().stereoToggle, "STEREO (toggle)" }, { &ui.full().keyTranspose, "KEY TRANSPOSE" }, { &ui.full().keyTune, "KEY TUNE" } };
        for (int i = 0; i < nl; ++i)
        {
            if (leaves[i] == &ui.full().close)
                continue;
            ++n;
            if (names != nullptr)
                for (const N& a : all)
                    if (a.c == leaves[i])
                        *names += std::string (names->empty() ? "" : ", ") + a.name;
            if (leaves[i] == &ui.full().smoothTime)
            {
                ++n;
                if (names != nullptr)
                    *names += ", SMOOTH switch";
            }
        }
        return n;
    }
};

void testStandaloneLfoSelectorQ()
{
    for (bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        PageRig r;
        r.route (1, uibind::lfoSource (6), "master.volume");
        r.route (2, uibind::rndSource (2), "filter1.cutoff");
        auto slot = [&] ()
        {
            int result = -1;
            for (int q = 0; q < QRouter::kCount; ++q)
                if (r.inst.router.virtualId (q) == LfoUi::kVirtualSelector)
                {
                    check (result < 0, "only one Q proxy owns the LFO selector");
                    result = q;
                }
            return result;
        };
        check (slot() == (wide ? 15 : 7), "LFO selector occupies a spare slot without moving TIMING");
        check (r.inst.hostQ (slot(), 0.25f) == QRouter::Write::written, "the host can write the LFO virtual dial");
        r.refresh();
        check (! r.ui.random() && r.ui.selection() == 6 && r.inst.router.target (0) == paramIdx ("lfo6.shape"),
               "the Q dial selects the second shown LFO and publishes its own Q targets");
        check (r.inst.hostQ (slot(), 1.0f / 3.0f) == QRouter::Write::written, "the same dial can select a shown random LFO after the unused LFO 1 cell disappears");
        r.refresh();
        check (r.ui.random() && r.ui.selection() == 2 && r.inst.sel[2] == 6 && r.inst.router.target (0) == paramIdx ("rlfo2.style"),
               "random selection keeps the last standard LFO and remaps the RANDOM controls");
        check (r.inst.hostQ (slot(), 1.0f) == QRouter::Write::written, "the high end of the LFO dial includes the free random choice");
        r.refresh();
        check (r.ui.random() && r.ui.selection() == 1, "the '+' random Q stop selects the lowest free RND");
        check (r.inst.hostQ (slot(), 0.75f) == QRouter::Write::written, "the other '+' Q stop offers a free standard LFO");
        r.refresh();
        check (! r.ui.random() && r.ui.selection() == 1 && r.inst.writes == 0,
               "the free normal LFO is reachable from the same dial without editing the patch");

        r.key ("lfo1.sync", 4.0f);
        r.ui.full().setActiveGroup (2);
        r.refresh();
        const int q = slot();
        const int keySlot = wide ? 5 : 0;
        check (q == (wide ? 15 : 13) && r.inst.router.target (keySlot) == paramIdx ("lfo1.keytrack_transpose")
               && r.inst.router.target (keySlot + 1) == paramIdx ("lfo1.keytrack_tune"),
               "the selector remains reachable with the KEY block active without replacing TRANSPOSE or TUNE");
        check (r.inst.hostQ (q, 1.0f) == QRouter::Write::written, "queue a selection just before a detail opens");
        r.tapC (r.ui.full().targets);
        check (r.ui.topPage() == LfoUi::kPageTargets && slot() < 0 && r.ui.selection() == 1,
               "opening Targets drops the root selector's pending write");
        r.tapC (r.ui.targetsPage().back);
        check (slot() >= 0 && r.ui.selection() == 1, "closing Targets restores the virtual selector without a stale jump");
        check (r.inst.hostQ (slot(), 1.0f) == QRouter::Write::written, "queue an LFO selection just before a preset load");
        ++r.inst.generation;
        r.refresh();
        check (r.ui.selection() == 1 && ! r.ui.random(), "the preset's selected LFO wins over the previous preset's queued write");
    }
    ui::QGroup::setWideDevice (false);
}

__attribute__((noinline)) void testLfoTab()
{
    // the recount (written in docs/ui-map/env-filter-lfo-plan.md): the controls of the tab in every mode
    {
        PageRig r;
        r.ui.setNativeSmooth (false);
        r.refresh();
        std::string nm;
        const int hz = r.controls (&nm);
        std::printf ("INFO LFO standard, Hz, built-in shape: %d controls: %s\n", hz, nm.c_str());
        check (hz == 17 && r.ui.full().editShown() && r.ui.full().edit.rect().w >= 56 && r.ui.full().edit.state.icon == ui::MenuButton::Icon::Expand,
               "standard LFO (Hz, standalone): 17 controls (instance, named waveform, TARGETS, Expand, RATE, RISE, SMOOTH + switch, HZ/BPM/KEY, PHASE, DELAY, MODE, HOST, DIRECTION, SWING, STEREO, MONO)");
        check (r.ui.full().hostPick.rect().h >= 56 && r.ui.full().block (1).contains (r.ui.full().hostPick.rect().x, r.ui.full().hostPick.rect().y) && r.ui.full().block (1).contains (r.ui.full().hostPick.rect().right() - 1, r.ui.full().hostPick.rect().bottom() - 1),
              "U14: HOST sits inside the TIMING block (touch only)");
        r.key ("lfo1.shape", 12.0f);
        std::string n2;
        const int drawn = r.controls (&n2);
        check (drawn == 17 && r.ui.full().editShown() && r.ui.full().edit.rect().w >= 56, "shape = DRAWN: Expand remains shown (17 controls)");
        r.key ("lfo1.shape", 3.0f);
        check (r.ui.full().editShown() && r.ui.full().edit.state.icon == ui::MenuButton::Icon::Expand, "built-in shape in standalone: Expand button stays consistent");
        r.key ("lfo1.sync", 1.0f);
        std::string n3;
        const int bpm = r.controls (&n3);
        std::printf ("INFO LFO standard, BPM: %d controls: %s\n", bpm, n3.c_str());
        check (bpm == 19 && r.ui.full().rate.trip.rect().h >= 56 && r.ui.full().rate.dot.rect().h >= 56 && ! r.ui.full().keyTranspose.rect().w, "BPM: DIVISION replaces RATE and TRIP, DOT appear (19 controls)");
        r.key ("lfo1.sync", 4.0f);
        std::string n4;
        const int key = r.controls (&n4);
        std::printf ("INFO LFO standard, KEY: %d controls: %s\n", key, n4.c_str());
        check (key == 18 && r.ui.full().keyTranspose.enabled() && r.ui.full().keyTune.enabled() && ! r.ui.full().rate.trip.rect().w, "KEY: TRANSPOSE and TUNE replace RATE (18 controls), no TRIP / DOT");
        r.key ("lfo1.sync", 0.0f);
        r.key ("lfo1.shape", 12.0f);
        r.key ("lfo1.sync", 1.0f);
        std::string n5;
        const int most = r.controls (&n5);
        std::printf ("INFO LFO standard, drawn + BPM (the most): %d controls\n", most);
        check (most == 19, "the most a standard LFO shows: drawn shape + BPM = 19 controls");
    }
    {
        PageRig r;
        r.inst.tv[0] = 1;   // RND 1
        r.refresh();
        std::string nm;
        const int rnd = r.controls (&nm);
        std::printf ("INFO random LFO, Hz: %d controls: %s\n", rnd, nm.c_str());
        check (r.ui.random() && rnd == 7 && nm.find ("HOST") == std::string::npos && nm.find ("RISE") == std::string::npos && nm.find ("PHASE") == std::string::npos && nm.find ("DIRECTION") == std::string::npos && nm.find ("EDIT") == std::string::npos && nm.find ("SWING") == std::string::npos && nm.find ("MONO") == std::string::npos,
               "random LFO (Hz): 7 controls (selector, style grid, RATE, HZ/BPM/KEY, STEREO toggle, trigger list, TARGETS); HOST, RISE, SMOOTH, PHASE, DELAY, DIRECTION, SWING, STEREO knob, MONO, MODE stepper, EDIT are hidden, not dimmed");
        r.key ("rlfo1.sync", 1.0f);
        std::string n2;
        const int b = r.controls (&n2);
        check (b == 9, "random LFO, BPM: 9 controls (DIVISION replaces RATE, TRIP and DOT appear): " + std::to_string (b));
        r.key ("rlfo1.sync", 4.0f);
        std::string n3;
        const int k = r.controls (&n3);
        std::printf ("INFO random LFO, KEY: %d controls: %s\n", k, n3.c_str());
        check (k == 8, "random LFO, KEY: 8 controls (TRANSPOSE and TUNE replace RATE): " + std::to_string (k));
    }
    // the selector for an empty, a partly used and an all used preset
    {
        PageRig r;
        check (cellsText (r.ui.selector()) == "[1] +" && r.ui.selector().state.selected == 1, "empty preset: [1] +  (" + cellsText (r.ui.selector()) + ")");
        const ui::Rect instance = r.ui.full().instance.rect();
        check (instance.h >= 56 && instance.w >= 200 && r.ui.full().instance.enabled() && ! r.ui.full().instance.inactive(), "the standalone selector dropdown is accessible (>= 200 x 56) and active");
        check (r.ui.full().picker.rect().empty() && r.ui.full().namedPicker.rect().w >= 240 && r.ui.full().namedPicker.rect().h >= 56,
               "standalone LFO replaces the shape grid with an accessible named waveform dropdown");
    }
    {
        PageRig r;
        r.route (1, 8, "master.volume");   // LFO 2 (source 8)
        r.route (2, 15, "master.volume");   // RND 1 (source 15)
        r.route (3, 39 - 6, "master.volume");   // 33 = LFO 9
        r.inst.set ("lfo5.rate", 9.0f);   // edited, no routing: counts as used
        r.refresh();
        check (cellsText (r.ui.selector()) == "[1] [2] [5] [9] [R1] +", "partly used (LFO 2, 9 routed, LFO 5 edited, RND 1 routed): [1] [2] [5] [9] [R1] +  (" + cellsText (r.ui.selector()) + ")");
        // the all used preset: 10 LFOs + 4 random: 14 cells, no '+', and the cells fit the top bar
        PageRig a;
        int slot = 1;
        for (int n = 1; n <= 10; ++n)
            a.route (slot++, uibind::lfoSource (n), "master.volume");
        for (int m = 1; m <= 4; ++m)
            a.route (slot++, uibind::rndSource (m), "master.volume");
        const ui::Rect sr = a.ui.full().instance.rect();
        check (a.ui.selector().numCells() == 14 && ! a.ui.selector().hasPlus() && sr.w >= 200 && ! sr.overlaps (a.ui.full().targets.rect()), "all used: 14 entries remain available through the standalone dropdown (" + std::to_string (sr.w) + " px)");
    }
    // LFO 7 appears after 6 is assigned; selecting away keeps a hidden LFO's parameters; the Q-Links follow the selected LFO
    {
        PageRig r;
        r.route (1, uibind::lfoSource (6), "master.volume");
        check (cellsText (r.ui.selector()) == "[1] [6] +", "LFO 6 assigned: [1] [6] +");
        check (r.tapCell ("6") && r.ui.selection() == 6 && r.inst.q[0] == paramIdx ("lfo6.shape") && r.inst.q[1] == paramIdx ("lfo6.rate") && r.inst.q[2] == paramIdx ("lfo6.fade_in") && r.inst.q[3] == paramIdx ("lfo6.smooth_time"),
              "Q-Links follow the selected LFO: the RATE group is shape, rate, rise, smooth of lfo6");
        int timingGrp = -1;
        for (int g = 0; g < r.ui.full().numGroups(); ++g)
            if (std::strcmp (r.ui.full().groupName (g), "Timing") == 0)
                timingGrp = g;
        check (timingGrp >= 0, "standalone LFO has a Timing group");
        r.ui.full().setActiveGroup (timingGrp);
        r.refresh();
        check (r.inst.q[0] == paramIdx ("lfo6.sync") && r.inst.q[1] == paramIdx ("lfo6.phase") && r.inst.q[2] == paramIdx ("lfo6.delay") && r.inst.q[3] == paramIdx ("lfo6.sync_type"), "the TIMING group of lfo6");
        r.inst.set ("lfo7.phase", 90.0f);   // an LFO that is not shown keeps what it has: nothing writes it
        r.refresh();
        const int w0 = r.inst.writes;
        const ui::Rect instR = r.ui.full().instance.rect();
        r.tap (instR.right() - 10, instR.y + instR.h / 2);   // step past LFO 7
        r.tap (instR.right() - 10, instR.y + instR.h / 2);   // step carousel onto `+` (LFO 2 and RND 1 free): Plus list opens
        check (r.stack.depth() == 2 && r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 2 && r.inst.writes == w0,
               "'+' opens the two-row Plus list and writes nothing");
        r.stack.back();
        r.refresh();
        const float ph = r.inst.get ("lfo7.phase");
        r.tapCell ("1");
        check (r.inst.get ("lfo7.phase") == ph && r.inst.writes == w0, "hidden LFOs keep their parameters (selection writes none)");
        r.inst.set ("rlfo2.sync", 1.0f);
        r.inst.tv[0] = 2;
        r.refresh();
        check (r.ui.random() && r.ui.selection() == 2 && r.inst.q[0] == paramIdx ("rlfo2.style") && (r.inst.q[1] == paramIdx ("rlfo2.division") || r.inst.q[1] == paramIdx ("rlfo2.rate")), "Q-Links follow the random LFO (rlfo2: style, then rate or division): " + std::to_string (r.ui.random()) + " " + std::to_string (r.ui.selection()) + " " + std::to_string (r.inst.q[0]) + " " + std::to_string (r.inst.q[1]));
    }
    // the targets and the display edit the selected LFO
    {
        PageRig r;
        r.route (1, uibind::lfoSource (3), "master.volume");
        r.tapCell ("3");
        r.tapC (r.ui.full().targets);
        check (r.ui.topPage() == LfoUi::kPageTargets && r.ui.source() == uibind::lfoSource (3), "TARGETS: the targets of the selected LFO (LFO 3)");
    }
}

__attribute__((noinline)) void testStandaloneLfoPage()
{
    auto rp = std::make_unique<PageRig>();
    PageRig& r = *rp;
    check (r.ui.full().displayRect().w >= 1200 && r.ui.full().displayRect().h >= 240,
           "standalone LFO page has a wide display curve across the screen");

    // Readable waveform dropdown highlights current choice and converts built-in -> Custom cleanly
    r.key ("lfo1.shape", 4.0f);   // Square (built-in shape)
    check (std::string (r.ui.full().namedPicker.state.text) == "Square", "namedPicker displays readable shape name");
    r.tapC (r.ui.full().namedPicker);
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 13 && r.ui.list().state.current == 4,
           "namedPicker opens 13-shape ListPopup with current shape highlighted");
    for (int g = 0; g < 12 && (12 < r.ui.list().firstRow() || 12 >= r.ui.list().firstRow() + r.ui.list().visibleRows()); ++g)
    {
        const ui::Rect z = r.ui.list().zoneRect();
        r.tap (z.x + z.w / 2, z.bottom() - 2);
    }
    check (12 >= r.ui.list().firstRow() && 12 < r.ui.list().firstRow() + r.ui.list().visibleRows(), "scrolled waveform list shows Custom");
    const ui::Rect cr = r.ui.list().rowRect (12);
    r.tap (cr.x + cr.w / 2, cr.y + cr.h / 2);
    check (r.ui.topPage() == 0 && r.inst.get ("lfo1.shape") == 12.0f && r.inst.contentPtr->lfoShapes.size() == 1,
           "choosing Custom from readable waveform dropdown initializes drawn shape from previous Square");

    // Expand icon is drawn ON TOP of the display background (not buried beneath lfodisplay::draw) even on built-in shapes
    r.key ("lfo1.shape", 2.0f);   // Saw up (built-in shape)
    const ui::Rect eb = r.ui.full().edit.rect();
    check (eb == ui::playrows::fullRect (r.ui.full().displayRect()) && r.ui.full().edit.state.icon == ui::MenuButton::Icon::Expand,
           "standalone Expand button sits in top-right playrows::fullRect with Expand icon on built-in shape");
    {
        auto sc = std::make_unique<uit::Scene> (1280, 800, 0u);
        r.ui.draw (sc->builder());
        const uit::Image im = uit::render (*sc);
        const uint32_t bgCorner = im.at (eb.x + 4, eb.y + 4);
        int ink = 0;
        for (int y = eb.y + 6; y < eb.bottom() - 6; ++y)
            for (int x = eb.x + 6; x < eb.right() - 6; ++x)
                ink += im.at (x, y) != bgCorner;
        check (ink > 0, "standalone Expand button glyphs are visible on top of the curve panel");
    }
    r.tapC (r.ui.full().edit);
    check (r.ui.topPage() == LfoUi::kPageEditor, "tapping standalone Expand on a built-in shape opens the shape editor");
    r.tapC (r.ui.editor().back);
    check (r.ui.topPage() == 0, "back from editor returns to standalone LFO page");

    // LFO/RND selector distinguishes selected vs assigned sources and navigates across unused/unrouted slots without writes
    r.route (1, uibind::lfoSource (4), "master.volume");
    r.route (2, uibind::rndSource (2), "master.volume");
    r.tapC (r.ui.full().instance);
    const unsigned expectedDots = (1u << 3) | (1u << 11);   // LFO 4 (idx 3) and RND 2 (idx 11) assigned; selected LFO 1 (idx 0) is NOT assigned
    check (r.ui.topPage() == LfoUi::kPageList && r.ui.list().state.count == 14 && r.ui.list().state.current == 0 && r.ui.list().state.dotMask == expectedDots,
           "PageInstance list marks assigned sources with dots separately from selected row");
    const int wNav0 = r.inst.writes;
    const ui::Rect r7 = r.ui.list().rowRect (6);
    r.tap (r7.x + r7.w / 2, r7.y + r7.h / 2);   // switch to unused LFO 7
    check (r.ui.topPage() == 0 && ! r.ui.random() && r.ui.selection() == 7 && r.inst.writes == wNav0,
           "selecting an unrouted LFO switches view without writing parameters");
    check (! r.ui.full().instance.inactive() && ! r.ui.full().targets.inactive(),
           "navigation controls stay live when an unrouted/inactive LFO is selected");
    r.tapC (r.ui.full().instance);
    for (int g = 0; g < 12 && (10 < r.ui.list().firstRow() || 10 >= r.ui.list().firstRow() + r.ui.list().visibleRows()); ++g)
    {
        const ui::Rect z = r.ui.list().zoneRect();
        r.tap (z.x + z.w / 2, z.bottom() - 2);
    }
    const ui::Rect rr1 = r.ui.list().rowRect (10);
    r.tap (rr1.x + rr1.w / 2, rr1.y + rr1.h / 2);   // switch from unused LFO 7 to unused RND 1
    check (r.ui.topPage() == 0 && r.ui.random() && r.ui.selection() == 1 && r.inst.writes == wNav0,
           "navigation from unused LFO to unused RND works and writes no sound/routing parameters");
    check (std::string (r.ui.full().namedPicker.state.text) == "Perlin", "Random mode namedPicker displays readable style name");
    check (r.tapCell ("R2") && r.ui.random() && r.ui.selection() == 2
           && r.ui.topPage() == 0 && r.ui.list().state.count == 0 && r.inst.writes == wNav0,
           "standalone RND2 selection closes and clears the instance list without writing parameters");
    r.tapCell ("R1");
    r.tapC (r.ui.full().targets);
    check (r.ui.topPage() == LfoUi::kPageTargets && r.ui.source() == uibind::rndSource (1) && r.inst.writes == wNav0,
           "TARGETS opens from an unused/inactive RND without writing parameters");
}

void testEditor()
{
    Rig r;
    r.inst.set ("lfo1.shape", 4.0f);   // square
    r.refresh();
    r.openFull();
    check (! r.ui.full().editShown() && r.ui.full().edit.rect().w == 0, "EDIT is hidden while a built-in shape is shown (bold cut)");
    r.tap (r.ui.full().picker.cell (12).x + 10, r.ui.full().picker.cell (12).y + 10);   // the DRAWN cell of the picker makes the shape a drawn one
    check (r.inst.get ("lfo1.shape") == 12.0f && r.inst.shapeWrites == 1 && r.inst.contentPtr->lfoShapes.size() == 1, "the DRAWN cell: a built-in shape becomes the drawn one: the parameter is Custom, the content has the points");
    check (r.ui.full().editShown() && r.ui.full().edit.rect().w >= 56, "the drawn shape: EDIT appears");
    r.tapC (r.ui.full().edit);
    check (r.stack.depth() == 2 && r.ui.topPage() == LfoUi::kPageEditor, "EDIT opens the shape editor");
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
    // The host delivers the native number edit through its original field callback.
    const ui::lfo::ShapeModel beforeYDrag = r.ui.editor().model;
    const int undoBeforeYDrag = r.ui.editor().undo.depth();
    r.inst.nativeEditorBank.post (r.inst.nativeEditorBank.at (nativeui::lfoeditor::kY),
                                 nativeui::lfoeditor::rangeOf (nativeui::lfoeditor::kY, 0, false).encode (0.95));
    r.ui.syncNativePlayEditor (r.inst, r.stack, r.hl, r.now, nullptr, true);
    r.refresh();
    check (r.ui.editor().undo.depth() == undoBeforeYDrag + 1, "native point-bar Y edit creates one undo state");
    r.tapC (r.ui.editor().undoButton);
    check (r.ui.editor().model == beforeYDrag, "UNDO restores the complete native point-bar Y edit");
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
    r.ui.select (10, r.inst);
    r.refresh();
    check (! r.ui.full().edit.enabled(), "EDIT is dim for a random LFO");
}

void openEditorOn (Rig&, const ui::lfo::ShapeModel&);
ui::lfo::ShapeModel triangleWith (std::initializer_list<std::pair<float, float>>);

void testEditorPresetContentReload()
{
    Rig r;
    const ui::lfo::ShapeModel a = triangleWith ({ { 0.25f, 0.15f } });
    const ui::lfo::ShapeModel b = triangleWith ({ { 0.25f, 0.85f } });
    auto contentFor = [] (const ui::lfo::ShapeModel& m, int gx, int gy) {
        synthyboy::StateContent c;
        c.lfoShapes.push_back (LfoUi::contentOf (1, m, gx, gy));
        return std::make_shared<const synthyboy::StateContent> (c);
    };
    openEditorOn (r, a);
    check (std::fabs (r.ui.editor().model.p[1].y - 0.15f) < 1.0e-5f, "shape editor opens with preset A's points");
    const int writes = r.inst.shapeWrites;
    r.inst.contentPtr = contentFor (b, 16, 4);
    r.refresh();
    check (std::fabs (r.ui.editor().model.p[1].y - 0.85f) < 1.0e-5f && r.inst.shapeWrites == writes,
           "an open shape editor reloads changed points without writing the passive preset");

    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    const int x = static_cast<int> (cv.xOf (0.25f)), y = static_cast<int> (cv.yOf (0.85f));
    r.ui.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    r.now += 16.0;
    r.ui.touch (ev (TouchEvent::Move, x, y - 40), r.stack, r.inst, r.now);
    r.inst.contentPtr = contentFor (a, 0, 0);
    r.refresh();
    const int writesBeforeRelease = r.inst.shapeWrites;
    r.ui.touch (ev (TouchEvent::Up, x, y - 40), r.stack, r.inst, r.now);
    r.refresh();
    check (std::fabs (r.ui.editor().model.p[1].y - 0.15f) < 1.0e-5f && r.inst.shapeWrites == writesBeforeRelease,
           "a passive preset swap during editing is adopted after release without an old-preset write");
    const int inferred = ui::lfo::inferGrid (a);
    check (cv.state.gridX == inferred && cv.state.gridY == inferred,
           "a preset without stored grid dimensions derives both axes from its own shape");
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
    // The native host choice writes the original trigger parameter.
    r.inst.nativeEditorBank.post (r.inst.nativeEditorBank.at (nativeui::lfoeditor::kMode),
                                 nativeui::lfoeditor::rangeOf (nativeui::lfoeditor::kMode, 0, false).encode (3));
    r.ui.syncNativePlayEditor (r.inst, r.stack, r.hl, r.now, nullptr, true);
    r.refresh();
    check (r.ui.topPage() == LfoUi::kPageEditor && r.inst.get ("lfo1.sync_type") == 3.0f, "a choice of the list writes lfo1.sync_type and closes the list (the editor is below)");
    // the LOOP toggle sets the loop point at the selected point (and the PHASE in a loop mode)
    r.inst.set ("lfo1.phase", 0.0f);
    r.ui.editor().model.loop = -1;
    r.refresh();
    r.tap (static_cast<int> (cv.xOf (0.6f)), static_cast<int> (cv.yOf (1.0f)));
    r.tapC (r.ui.editor().loop);
    check (r.ui.editor().model.loop == 1 && std::fabs (r.inst.get ("lfo1.phase") - 216.0f) < 0.1f, "LOOP: the selected point is the loop point, the PHASE goes to it");
    // The seam links are editor state, but their effect is rendered into the
    // model/content immediately so playback and a subsequent save agree.
    r.ui.editor().model.p[0].y = 0.2f;
    r.ui.editor().model.p[2].y = 0.8f;
    r.refresh();
    r.tapC (r.ui.editor().cycleLink);
    check (r.ui.editor().model.linkCycleEnds && std::fabs (r.ui.editor().model.p[0].y - r.ui.editor().model.p[r.ui.editor().model.n - 1].y) < 1.0e-5f,
           "CYCLE LINK joins the first and last point ordinates");
    r.ui.editor().model.p[r.ui.editor().model.n - 1].y = 0.17f;
    r.tapC (r.ui.editor().loopLink);
    auto pointAt = [&] (float x) {
        for (int i = 0; i < r.ui.editor().model.n; ++i)
            if (std::fabs (r.ui.editor().model.p[i].x - x) < 1.0e-4f) return i;
        return -1;
    };
    int seam4 = pointAt (r.ui.editor().canvas.state.markerX);
    check (r.ui.editor().model.linkLoopEnds && seam4 >= 0 && std::fabs (r.ui.editor().model.p[seam4].y - r.ui.editor().model.p[r.ui.editor().model.n - 1].y) < 1.0e-5f,
           "LOOP LINK joins the mode-4 PHASE seam even without an authored loop index");
    check (std::fabs (r.ui.editor().model.p[seam4].y - 0.17f) < 1.0e-5f, "editing either mode-4 seam endpoint keeps the link");
    check (std::fabs (r.ui.editor().model.p[0].y - 0.17f) < 1.0e-5f,
           "enabling LOOP LINK preserves the active full-cycle link after editing the last point");
    r.ui.editor().model.p[0].y = 0.42f;
    r.tapC (r.ui.editor().smooth);
    check (std::fabs (r.ui.editor().model.p[seam4].y - 0.42f) < 1.0e-5f
               && std::fabs (r.ui.editor().model.p[r.ui.editor().model.n - 1].y - 0.42f) < 1.0e-5f,
           "editing the cycle-only endpoint propagates across both active links");
    r.tapC (r.ui.editor().loopLink); // off before switching to the other playback seam
    r.inst.set ("lfo1.sync_type", 5.0f);
    r.inst.set ("lfo1.phase", 90.0f);
    r.refresh();
    r.ui.editor().model.loop = -1;
    r.ui.editor().model.p[0].y = 0.3f;
    r.refresh();
    r.tapC (r.ui.editor().loopLink);
    int seam5 = pointAt (r.ui.editor().canvas.state.markerX);
    check (r.ui.editor().model.linkLoopEnds && seam5 >= 0 && std::fabs (r.ui.editor().model.p[0].y - r.ui.editor().model.p[seam5].y) < 1.0e-5f,
           "LOOP LINK joins the mode-5 PHASE seam when it falls between authored points");
    r.ui.editor().model.p[seam5].y = 0.71f;
    r.tapC (r.ui.editor().cycleLink); // any editor commit must enforce the active loop seam
    check (std::fabs (r.ui.editor().model.p[0].y - 0.71f) < 1.0e-5f, "editing the mode-5 seam point updates the other endpoint");
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
    check (r.ui.editor().redoButton.enabled(), "REDO becomes available after UNDO");
    r.tapC (r.ui.editor().redoButton);
    check (m.n == 4 && r.inst.contentPtr->lfoShapes[0].points.size() == 4 && ! r.ui.editor().redoButton.enabled(), "REDO reapplies the deleted point change and consumes its branch");
    r.tapC (r.ui.editor().undoButton);
    check (m == start, "UNDO after REDO restores the original shape again");
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
    // a rename that changes only the letter case: on a file system that does not tell the cases apart (the Mac, FAT / exFAT cards) the new file IS the old one, and it must not be removed
    check (lib.rename (lib.find ("Beta"), "BETA") && lib.count() == 2 && lib.find ("BETA") >= 0 && lib.load (lib.find ("BETA"), back) && back == m, "rename that only changes the letter case keeps the shape (the file is not lost; which case the card then shows is up to its file system)");
    // a name that holds the loop key's own text does not confuse the reader (the quotes inside a name are escaped in the file)
    {
        ui::lfo::ShapeModel tricky = m;
        tricky.loop = -1;
        ui::lfo::ShapeModel got;
        check (LfoShapeLibrary::fromText (LfoShapeLibrary::toText ("a \"loop_point\":3 b", tricky), got) && got.loop == -1, "a name with the text of the loop key: the shape has no loop point");
        tricky.loop = 1;
        check (LfoShapeLibrary::fromText (LfoShapeLibrary::toText ("a \"loop_point\":3 b", tricky), got) && got.loop == 1, "a name with the text of the loop key: the real loop point is read");
    }
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
    // the point modulation is stored: an optional "point_mod" array with the stable text id of the source (never the number)
    {
        ui::lfo::ShapeModel mm = ui::lfo::ShapeModel::fromBuiltin (1);
        mm.insert (0.25f, 0.3f);
        mm.loop = 1;
        ui::lfo::PointSet a, b;
        a.add (1);
        b.add (2);
        b.add (3);
        mm.setMod (a, 0, 8, -0.25f);    // the time of point 1 by LFO 2
        mm.setMod (a, 1, 9, 0.75f);     // the level of point 1 by LFO 3
        mm.setMod (b, 2, 7, 1.0f);      // the curve of points 2 and 3 by LFO 1
        check (lib.save ("Modded", mm, false), "save: a shape with point modulation");
        std::ifstream mf (tmp.path + "/SynthyBoy/LFO Shapes/Modded.lfoshape.json");
        const std::string mtext ((std::istreambuf_iterator<char> (mf)), std::istreambuf_iterator<char>());
        check (mtext.find ("\"point_mod\":[") != std::string::npos && mtext.find ("\"source\":\"lfo2\"") != std::string::npos && mtext.find ("\"target\":\"curve\"") != std::string::npos && mtext.find ("\"version\":1") != std::string::npos
                   && mtext.find ("\"source\":8") == std::string::npos, "the file has point_mod with text ids of the sources, version stays 1");
        check (mtext.find ("\"point_mod\"") < mtext.find ("\"points\""), "point_mod stands before the points (with loop_point)");
        synthyboy::LfoShapeFile mcore;
        check (synthyboy::parseLfoShapeFile (mtext, mcore) && mcore.name == "Modded" && static_cast<int> (mcore.points.size()) == mm.n, "the desktop parser of the core reads a file with point_mod (it skips the key)");
        ui::lfo::ShapeModel got;
        check (lib.load (lib.find ("Modded"), got) && got == mm && got.p[1].mx.source == 8 && got.p[2].mc.source == 7 && got.loop == 1, "save then load: the modulation (time, level, curve, source, amount) and the loop point are back");
        check (lib.rename (lib.find ("Modded"), "Modded 2") && lib.load (lib.find ("Modded 2"), got) && got == mm, "rename keeps the point modulation");
        std::filesystem::remove (tmp.path + "/SynthyBoy/LFO Shapes/Modded 2.lfoshape.json");
        lib.refresh();
        // a name with the text of the key, in both directions
        ui::lfo::ShapeModel plain = mm;
        for (int i = 0; i < plain.n; ++i)
            plain.p[i].mx = plain.p[i].my = plain.p[i].mc = ui::lfo::PointMod {};
        check (LfoShapeLibrary::fromText (LfoShapeLibrary::toText ("a \"point_mod\":[{\"point\":1,\"target\":\"level\",\"source\":\"lfo1\",\"amount\":1}] b", plain), got) && got == plain, "a name with the text of the key: a shape without modulation has none");
        check (LfoShapeLibrary::fromText (LfoShapeLibrary::toText ("a \"point_mod\":[] b", mm), got) && got == mm, "a name with the text of the key: the real modulation is read");
        // a hand-written file that holds the text inside strings and other values is not fooled either
        check (LfoShapeLibrary::fromText ("{\"format\":\"synthyboy-lfo-shape\",\"name\":\"x \\\"point_mod\\\":[{\\\"point\\\":1}]\",\"other\":{\"point_mod\":[1]},\"points\":[{\"x\":0,\"y\":0},{\"x\":0.5,\"y\":1},{\"x\":1,\"y\":0}]}", got) && got.n == 3 && ! got.p[1].mx.on() && ! got.p[1].my.on() && ! got.p[1].mc.on(), "point_mod inside a string or inside another object is not the key");
        // a file without the key loads with no modulation
        check (LfoShapeLibrary::fromText ("{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":\"x\",\"smooth\":false,\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}", got) && got.n == 2 && ! got.p[0].mx.on() && ! got.p[0].my.on() && ! got.p[1].mc.on(), "a file without point_mod loads with no modulation, as before");
        // bad entries are skipped one by one, the good ones load
        const std::string bad = "{\"format\":\"synthyboy-lfo-shape\",\"version\":1,\"name\":\"x\",\"smooth\":false,\"point_mod\":["
                                "{\"point\":1,\"target\":\"level\",\"source\":\"no_such_source\",\"amount\":0.5},"   // unknown source
                                "{\"point\":9,\"target\":\"level\",\"source\":\"lfo1\",\"amount\":0.5},"             // point outside the shape
                                "{\"point\":-1,\"target\":\"level\",\"source\":\"lfo1\",\"amount\":0.5},"
                                "{\"point\":1.5,\"target\":\"level\",\"source\":\"lfo1\",\"amount\":0.5},"
                                "{\"point\":1,\"target\":\"sideways\",\"source\":\"lfo1\",\"amount\":0.5},"          // unknown target
                                "{\"point\":1,\"target\":\"level\",\"source\":7,\"amount\":0.5},"                    // a raw number is not an id
                                "{\"point\":1,\"target\":\"level\",\"source\":\"lfo1\"},"                            // no amount
                                "5,\"text\",null,{\"nested\":{\"a\":[1,2]}},"
                                "{\"point\":1,\"target\":\"time\",\"source\":\"lfo3\",\"amount\":-0.5,\"future\":[true]},"   // good, with a key we do not know
                                "{\"point\":0,\"target\":\"curve\",\"source\":\"lfo2\",\"amount\":9}"          // good, the amount is clamped
                                "],\"points\":[{\"x\":0,\"y\":0},{\"x\":0.5,\"y\":1},{\"x\":1,\"y\":0}]}";
        check (LfoShapeLibrary::fromText (bad, got) && got.n == 3, "a file with bad entries still loads");
        check (! got.p[1].my.on() && got.p[1].mx.source == 9 && std::fabs (got.p[1].mx.amount + 0.5f) < 1.0e-6f && got.p[0].mc.source == 8 && got.p[0].mc.amount == 1.0f && ! got.p[2].mx.on() && ! got.p[2].my.on() && ! got.p[2].mc.on(),
               "bad entries (unknown source, point out of range, raw number, unknown target, no amount) are skipped, the good ones are applied (amount clamped to 1)");
        // a damaged tail ends the walk, what was read stays; the shape still loads
        check (LfoShapeLibrary::fromText ("{\"format\":\"synthyboy-lfo-shape\",\"point_mod\":[{\"point\":1,\"target\":\"level\",\"source\":\"lfo1\",\"amount\":0.5},{\"point\":", got) == false, "a file cut off in the middle is no shape file (the core's parser says so)");
    }
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

void testEditorPointModulationStored()
{
    TempDir tmp;
    Rig r;
    r.inst.userRoots = { tmp.path };
    openEditorOn (r, triangleWith ({ { 0.25f, 0.5f } }));
    ui::lfo::ShapeModel& m = r.ui.editor().model;
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    r.tap (static_cast<int> (cv.xOf (0.25f)), static_cast<int> (cv.yOf (0.5f)));
    r.tapC (r.ui.editor().modButton);
    ui::PointModPage& pm = r.ui.pointModPage();
    const ui::Rect chip = pm.board.chipRectOfId (7);   // LFO 1
    r.tap (chip.x + chip.w / 2, chip.y + chip.h / 2);
    r.tapC (pm.back);
    check (m.p[1].my.source == 7, "a point of the shape is modulated by LFO 1");
    const ui::lfo::ShapeModel modded = m;
    // SAVE AS: the status no longer warns that the modulation is not stored
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 2);
    typeName (r, "Wob");
    pressKey (r, ui::TextEntry::Ok);
    check (std::string (r.ui.editor().status()) == "SAVED Wob", "the status of a save with point modulation is just SAVED <name> (the modulation is stored)");
    ui::lfo::ShapeModel stored;
    check (r.ui.library().load (0, stored) && stored == modded && stored.p[1].my.source == 7, "the file holds the modulation");
    // another shape in the editor, then load the saved one: the modulation comes back in ONE undo step
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 0);
    chooseRow (r, 4);   // SQUARE
    const ui::lfo::ShapeModel square = m;
    check (! (square == modded) && ! square.p[1].my.on(), "another shape without modulation is in the editor");
    const int undoBefore = r.ui.editor().undo.depth();
    r.tapC (r.ui.editor().shapes);
    chooseRow (r, 1);
    chooseRow (r, 0);
    check (m == modded && m.p[1].my.source == 7 && r.ui.editor().undo.depth() == undoBefore + 1, "loading the saved shape applies its point modulation in the same single undo step");
    check (r.inst.contentPtr->lfoShapes[0].points[1].modY.present && r.inst.contentPtr->lfoShapes[0].points[1].modY.source == synthyboy::modSourceTable()[6].key, "... and the content has it");
    r.tapC (r.ui.editor().undoButton);
    check (m == square && ! m.p[1].my.on(), "one UNDO takes shape and modulation back together");
}

/** The page of the point modulation on a rig: the editor of a triangle with one inner point, that point selected, MOD open, the source LFO 1 on its level (half depth). */
void openPointModPage (Rig& r, bool all)
{
    openEditorOn (r, triangleWith ({ { 0.25f, 0.5f } }));
    ui::ShapeCanvas& cv = r.ui.editor().canvas;
    if (all)
    {
        r.tap (r.ui.editor().tools.cell (4).x + 10, r.ui.editor().tools.cell (4).y + 10);
        cv.selectAll();
        r.refresh();
    }
    else
        r.tap (static_cast<int> (cv.xOf (0.25f)), static_cast<int> (cv.yOf (0.5f)));
    r.tapC (r.ui.editor().modButton);
    const ui::Rect chip = r.ui.pointModPage().board.chipRectOfId (7);   // LFO 1
    r.tap (chip.x + chip.w / 2, chip.y + chip.h / 2);
}

void testPointModulationQKnobs()
{
    const float noRead = 0.0f;
    auto val = [&] (Rig& r, int q) { return r.inst.router.value (q, [noRead] (int) noexcept { return noRead; }); };
    // ---- 4 knob device (the default): MODULATE and AMOUNT are Q 1 and Q 2 of the page
    {
        Rig r;
        openPointModPage (r, false);
        ui::PointModPage& pm = r.ui.pointModPage();
        ui::lfo::ShapeModel& m = r.ui.editor().model;
        check (r.ui.topPage() == LfoUi::kPagePointMod && m.p[1].my.source == 7 && std::fabs (m.p[1].my.amount - 0.5f) < 1.0e-6f, "the page is open on a point whose level is modulated by LFO 1 at 50 percent");
        check (r.inst.q[0] == QRouter::encode (LfoUi::kVirtPmTarget, QRouter::kVirtual) && r.inst.q[1] == QRouter::encode (LfoUi::kVirtPmAmount, QRouter::kVirtual) && r.inst.q[2] == -1 && r.inst.q[3] == -1 && r.inst.q[4] == -1,
               "Q1 = MODULATE and Q2 = AMOUNT (virtual 8 and 9), nothing else: one group");
        check (pm.target.qFrame() && pm.target.qIndex() == 1 && pm.amount.qFrame() && pm.amount.qIndex() == 2 && pm.group.active() && pm.group.block() == 0, "the group is the active one: the frame and the chips 1 and 2 (4 knob device)");
        check (std::fabs (val (r, 0) - 0.5f) < 1.0e-6f && std::fabs (val (r, 1) - 0.75f) < 1.0e-6f, "the router holds the knob values: LEVEL = 0.5, 50 percent = 0.75");
        // one continuous turn of AMOUNT: every tick changes the amount, the whole turn is ONE undo state
        const int undo0 = r.ui.editor().undo.depth(), writes0 = r.inst.shapeWrites;
        check (r.inst.hostQ (1, 0.9f) == QRouter::Write::written, "the host turns Q2");
        r.refresh();
        check (std::fabs (m.p[1].my.amount - 0.8f) < 1.0e-6f && std::fabs (static_cast<float> (pm.amount.state.value) - 80.0f) < 1.0e-3f, "AMOUNT is 80 percent in the model and in the field the touch control is (the same value text)");
        r.inst.hostQ (1, 0.95f); r.refresh();
        r.inst.hostQ (1, 1.0f); r.refresh();
        check (std::fabs (m.p[1].my.amount - 1.0f) < 1.0e-6f && r.ui.editor().undo.depth() == undo0 + 1, "three ticks of one turn: 100 percent, one undo state (" + std::to_string (r.ui.editor().undo.depth() - undo0) + ")");
        check (r.inst.shapeWrites > writes0 && std::fabs (r.inst.contentPtr->lfoShapes[0].points[1].modY.amount - 1.0f) < 1.0e-6f, "the content follows");
        check (std::fabs (val (r, 1) - 1.0f) < 1.0e-6f, "the router says the new value");
        // a pause starts a new turn: a new undo state
        r.now += 2000.0;
        r.inst.hostQ (1, 0.25f); r.refresh();
        check (std::fabs (m.p[1].my.amount + 0.5f) < 1.0e-6f && r.ui.editor().undo.depth() == undo0 + 2, "after a pause the next turn is a new undo state (-50 percent)");
        // a tick that changes nothing (a value that rounds to the same percent) pushes no state and writes nothing
        const int undo1 = r.ui.editor().undo.depth(), writes1 = r.inst.shapeWrites;
        r.now += 2000.0;
        r.inst.hostQ (1, 0.2505f); r.refresh();
        check (std::fabs (m.p[1].my.amount + 0.5f) < 1.0e-6f && r.ui.editor().undo.depth() == undo1 && r.inst.shapeWrites == writes1, "a turn that changes nothing: no undo state, no write");
        // a touch (a tap on another source of the board) ends the run of ticks: the next knob tick is a new undo state
        const int undo2 = r.ui.editor().undo.depth();
        r.inst.hostQ (1, 0.3f); r.refresh();
        const ui::Rect chip9 = pm.board.chipRectOfId (9);
        r.tap (chip9.x + chip9.w / 2, chip9.y + chip9.h / 2);
        r.inst.hostQ (1, 0.1f); r.refresh();
        check (m.p[1].my.source == 9 && std::fabs (m.p[1].my.amount + 0.8f) < 1.0e-6f && r.ui.editor().undo.depth() == undo2 + 3, "a knob tick, a touch on the board and another knob tick: three undo states (" + std::to_string (r.ui.editor().undo.depth() - undo2) + ")");
        // MODULATE chooses the value: TIME / LEVEL / CURVE as 0 / 0.5 / 1; no model change, no undo state
        const ui::lfo::ShapeModel beforeT = m;
        const int undo3 = r.ui.editor().undo.depth();
        check (r.inst.hostQ (0, 1.0f) == QRouter::Write::written, "the host turns Q1 to the end");
        r.refresh();
        check (pm.target.state.index == 2 && m == beforeT && r.ui.editor().undo.depth() == undo3, "MODULATE = CURVE: the stepper follows, the model and the undo stack are untouched");
        // inactive: nothing modulates the curve of the point, the AMOUNT knob is off and does nothing
        check (! pm.amount.enabled() && std::fabs (val (r, 1) - 0.5f) < 1.0e-6f, "no modulation on CURVE: AMOUNT is off (disabled look) and its knob reads the middle");
        r.inst.hostQ (1, 0.9f); r.refresh();
        check (m == beforeT && ! m.p[1].mc.on() && r.ui.editor().undo.depth() == undo3, "a turn of the inactive AMOUNT knob does nothing: no modulation appears, no undo state");
        r.inst.hostQ (0, 0.0f); r.refresh();
        check (pm.target.state.index == 0 && ! pm.amount.enabled(), "MODULATE = TIME: nothing modulates it either");
        r.inst.hostQ (0, 0.5f); r.refresh();
        check (pm.target.state.index == 1 && pm.amount.enabled(), "MODULATE = LEVEL: AMOUNT is on again");
        // the touch control and the knob are one value: REMOVE through touch makes the knob inactive
        r.tapC (pm.remove);
        check (! m.p[1].my.on() && ! pm.amount.enabled(), "REMOVE (touch): the knob is inactive");
        // back to the editor: its own Q controls are published again and work
        r.tapC (pm.back);
        check (r.ui.topPage() == LfoUi::kPageEditor && r.inst.q[0] == QRouter::encode (0, QRouter::kVirtual) && r.inst.q[3] == QRouter::encode (3, QRouter::kVirtual) && r.inst.q[4] == QRouter::encode (4, QRouter::kVirtual) && r.inst.q[6] == QRouter::encode (6, QRouter::kVirtual),
               "BACK: the editor's own Q controls (EDIT, GRID) are published again");
        check (r.inst.hostQ (2, 0.875f) == QRouter::Write::written, "the host turns Q3 (Y) of the editor");
        r.refresh();
        check (std::fabs (m.p[1].y - 0.875f) < 1.0e-4f, "... and the editor's Y follows (the knobs of the point modulation page were not left behind)");
        // the write to the knob of the other page while the editor is shown does nothing to the modulation
        check (! m.p[1].mx.on() && ! m.p[1].my.on() && ! m.p[1].mc.on(), "the editor's turns did not touch the modulation");
    }
    // ---- several points selected: a turn sets the amount of EVERY selected point
    {
        Rig r;
        openPointModPage (r, true);
        ui::lfo::ShapeModel& m = r.ui.editor().model;
        bool all = m.n >= 3;
        for (int i = 0; i < m.n; ++i)
            all = all && m.p[i].my.source == 7;
        check (all, "every point of the selection has LFO 1 on its level");
        const int undo0 = r.ui.editor().undo.depth();
        r.inst.hostQ (1, 0.1f); r.refresh();
        for (int i = 0; i < m.n; ++i)
            all = all && std::fabs (m.p[i].my.amount + 0.8f) < 1.0e-6f;
        check (all && r.ui.editor().undo.depth() == undo0 + 1, "the knob sets the amount of every selected point (-80 percent) in one undo state");
        r.tapC (r.ui.pointModPage().back);
        r.tapC (r.ui.editor().undoButton);
        bool restored = true;
        for (int i = 0; i < m.n; ++i)
            restored = restored && std::fabs (m.p[i].my.amount - 0.5f) < 1.0e-6f;
        check (restored, "UNDO in the editor takes the whole turn back: every point at 50 percent again");
    }
    // ---- 16 knob device: the same two knobs, the chips 1 and 2, no frame and no active group
    {
        ui::QGroup::setWideDevice (true);
        Rig r;
        openPointModPage (r, false);
        ui::PointModPage& pm = r.ui.pointModPage();
        check (r.inst.q[0] == QRouter::encode (LfoUi::kVirtPmTarget, QRouter::kVirtual) && r.inst.q[1] == QRouter::encode (LfoUi::kVirtPmAmount, QRouter::kVirtual) && r.inst.q[2] == -1 && r.inst.q[4] == -1,
               "16 knob device: the same slots, Q1 and Q2");
        check (! pm.target.qFrame() && ! pm.amount.qFrame() && pm.group.block() == 0, "16 knob device: no active group look, the group has block 0 (chips 1 and 2)");
        r.inst.hostQ (1, 0.9f); r.refresh();
        check (std::fabs (r.ui.editor().model.p[1].my.amount - 0.8f) < 1.0e-6f, "16 knob device: the knob works the same");
        uit::Scene sc (1280, 800, uit::kBackgroundRgba);
        pm.draw (sc.builder());
        const uit::Image im = uit::render (sc);
        check (! sc.overflow() && uit::drawnOutside (im, pm.rect(), uit::backgroundPixel()) == 0, "16 knob device: the page with its chips draws inside its rectangle");
        ui::QGroup::setWideDevice (false);
        Rig r4;
        openPointModPage (r4, false);
        uit::Scene sc4 (1280, 800, uit::kBackgroundRgba);
        r4.ui.pointModPage().draw (sc4.builder());
        const uit::Image im4 = uit::render (sc4);
        check (! sc4.overflow() && uit::drawnOutside (im4, r4.ui.pointModPage().rect(), uit::backgroundPixel()) == 0, "4 knob device: the page with its frame and chips draws inside its rectangle");
        const ui::Rect fr = r4.ui.pointModPage().group.frameRect();
        check (r4.ui.pointModPage().group.rect().w > 0 && fr.y >= r4.ui.pointModPage().rect().y + ui::kQLane && r4.ui.pointModPage().target.rect().y - ui::kQLane >= r4.ui.pointModPage().rect().y, "the lane for the chips is inside the page");
        ui::Component* leaves[64];
        const int nl = r4.ui.pointModPage().collectLeaves (leaves, 0, 64);
        check (r4.ui.pointModPage().group.check (leaves, nl) == 0, "QGroup::check: no other control lies in the frame of the group");
    }
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
    q.inst.sel[2] = 1;
    q.refresh();
    q.ui.select (10, q.inst);
    q.refresh();
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

void testRandomTraceSettles()
{
    PageRig r;
    r.inst.tv[0] = 1;
    synthyboy::LiveValues live;
    live.numLfoVoices = 1;
    live.source[14] = 0.5f;
    r.live = &live;
    check (r.refresh() && r.ui.displayData().traceHasSignal, "an all-zero active trace is signal instead of the resting style illustration");
    live.source[14] = 0.8f;
    r.refresh();
    live.source[14] = 0.5f;
    r.refresh();
    const auto& data = r.ui.displayData();
    const auto end = data.trace + data.traceN;
    check (end[-1] == 0.0f && end[-2] > 0.5f, "an active RND zero enters the trace after a nonzero sample");

    auto scene = std::make_unique<gpu::SceneData>();
    const uint32_t compactBefore = r.ui.drawHash (*scene);
    r.refresh();  // the head stays at zero while the earlier sample moves
    check (r.ui.drawHash (*scene) != compactBefore, "Play's LFO picture hash observes trace vertices when the newest sample stays still");
    const std::vector<float> lastTrace (data.trace, end);
    auto describe = [&]
    {
        gpu::SceneBuilder sb (*scene);
        check (r.page.describe (sb, 0, 1280, 800), "the RND page describes its current picture");
    };
    auto liveCurves = [&]
    {
        gpu::CellPlan plan;
        if (! gpu::planCells (*scene, plan))
            return -1;
        int curves = 0;
        for (uint32_t i = 0; i < scene->numOps; ++i)
            curves += ! plan.inCell[i] && scene->ops[i].op == gpu::kOpCurve;
        return curves;
    };
    describe();
    check (liveCurves() == 1, "a moving RND trace is in the animated layer");
    std::vector<uint32_t> playing, stopped;
    renderScene (*scene, playing);

    live.numLfoVoices = 0;
    check (! r.refresh(), "a no-voice readout freezes the trace without forcing a new picture");
    r.live = nullptr;
    check (! r.refresh(), "a missing readout also preserves the last audible trace");
    check (r.refresh(), "after the hold the RND trace is baked once");
    describe();
    renderScene (*scene, stopped);
    check (liveCurves() == 0 && playing == stopped && std::equal (lastTrace.begin(), lastTrace.end(), data.trace),
           "the settled cached RND picture matches the last live picture pixel for pixel");
    check (! r.refresh() && ! r.refresh(), "the settled RND page stops reporting picture changes");

    live.numLfoVoices = 1;
    live.source[14] = 0.95f;
    r.live = &live;
    check (r.refresh() && end[-1] > 0.85f && std::equal (lastTrace.begin() + 1, lastTrace.end(), data.trace),
           "the next note resumes with the preserved trace history");

    ++r.inst.generation;
    r.refresh();
    const auto clear = [&] { return std::all_of (data.trace, end, [] (float v) { return v == 0.0f; }); };
    check (clear() && ! data.traceHasSignal, "a preset recall retires the old trace and its first stale readout");
    r.refresh();
    check (end[-1] > 0.85f, "the new preset accepts a new live trace");
    r.live = nullptr;
    r.inst.tv[0] = 2;
    r.refresh();
    check (r.ui.selection() == 2 && clear(), "selecting RND 2 does not carry the RND 1 history");
    r.inst.tv[0] = 1;
    r.refresh();
    check (r.ui.selection() == 1 && clear(), "returning to RND 1 starts with a fresh history");
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
    r.ui.select (13, r.inst);
    r.refresh();
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
            if (std::string (pi.key) == pre + "host")
            {
                // HOST is a touch-only toggle inside TIMING (owner decision U14, like Master LINK): a control that writes the parameter, a member of no Q group
                Rig r;
                r.inst.sel[2] = lfo;
                r.refresh();
                r.openFull();
                bool inQ = false;
                for (int g = 0; g < r.ui.full().numGroups(); ++g)
                {
                    r.ui.full().setActiveGroup (g);
                    r.refresh();
                    for (int m = 0; m < 4; ++m)
                        inQ = inQ || r.inst.q[m] == static_cast<int> (i);
                }
                r.tapC (r.ui.full().hostPick);
                check (! inQ && r.inst.get (pi.key) == 1.0f && r.ui.full().hostPick.rect().h >= 56 && r.ui.full().block (1).contains (r.ui.full().hostPick.rect().x, r.ui.full().hostPick.rect().y),
                       std::string (pi.key) + " is a touch-only toggle inside the TIMING block (no Q slot)");
                continue;
            }
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
                r.ui.select (9 + rlfo, r.inst);
                r.refresh();
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

void testSpikeDecimation()
{
    // A spike one sample wide in d.y must reach its own row in both detail levels (full and reduced)
    const ui::Rect rect = { 0, 0, 400, 200 };
    const ui::Rect plot = ui::lfodisplay::plotRect (rect, ui::lfodisplay::Data {});
    const float expectedRow = ui::lfodisplay::rowOf (plot, 1.0f);

    for (int spikeIdx : { 1, 35, 36, 100, 125, 126 })
    {
        float y[128] = {};
        y[spikeIdx] = 1.0f;
        ui::lfodisplay::Data d;
        d.kind = ui::lfodisplay::Data::Kind::Shape;
        d.y = y;
        d.n = 128;

        for (int detail : { 1, 2 })
        {
            ui::theme::setDetail (detail);
            uit::Scene sc (400, 200, uit::kBackgroundRgba);
            ui::lfodisplay::draw (sc.builder(), rect, d, false);

            bool reachedRow = false;
            const gpu::SceneData& sd = *sc.data;
            for (uint32_t opIdx = 0; opIdx < sd.numOps; ++opIdx)
            {
                if (sd.ops[opIdx].op == gpu::kOpCurve)
                {
                    const int first = static_cast<int> (sd.ops[opIdx].f[0]);
                    const int n = static_cast<int> (sd.ops[opIdx].f[1]);
                    for (int i = 0; i < n; ++i)
                    {
                        if (std::abs (sd.y[first + i] - expectedRow) < 1e-3f)
                        {
                            reachedRow = true;
                            break;
                        }
                    }
                }
            }
            check (reachedRow, "detail level " + std::to_string (detail) +
                               ": a spike one sample wide (at index " + std::to_string (spikeIdx) +
                               ") reaches its own row");
        }
    }

    // Alternating case: max at odd index directly followed by min at next even index
    for (int oddIdx : { 1, 35, 125 })
    {
        float y[128] = {};
        y[oddIdx] = 1.0f;
        y[oddIdx + 1] = -0.5f;
        ui::lfodisplay::Data d;
        d.kind = ui::lfodisplay::Data::Kind::Shape;
        d.y = y;
        d.n = 128;

        for (int detail : { 1, 2 })
        {
            ui::theme::setDetail (detail);
            uit::Scene sc (400, 200, uit::kBackgroundRgba);
            ui::lfodisplay::draw (sc.builder(), rect, d, false);

            bool reachedRow = false;
            const gpu::SceneData& sd = *sc.data;
            for (uint32_t opIdx = 0; opIdx < sd.numOps; ++opIdx)
            {
                if (sd.ops[opIdx].op == gpu::kOpCurve)
                {
                    const int first = static_cast<int> (sd.ops[opIdx].f[0]);
                    const int n = static_cast<int> (sd.ops[opIdx].f[1]);
                    for (int i = 0; i < n; ++i)
                    {
                        if (std::abs (sd.y[first + i] - expectedRow) < 1e-3f)
                        {
                            reachedRow = true;
                            break;
                        }
                    }
                }
            }
            check (reachedRow, "detail level " + std::to_string (detail) +
                               ": alternating max at odd index " + std::to_string (oddIdx) +
                               " and min at even index " + std::to_string (oddIdx + 1) +
                               " reaches max row");
        }
    }

    ui::theme::setDetail (1);
}

void testEditorSpikeInColumn()
{
    // Three model points inside one column, the middle one a peak: the peak's row is reached
    ui::ShapeCanvas canvas;
    canvas.place ({ 0, 0, 400, 200 });
    const ui::Rect pl = canvas.plot();
    const int cols = std::clamp (pl.w, 2, ui::lfodisplay::kMaxPoints);

    ui::lfo::ShapeModel model;
    model.n = 5;
    const float colX = 191.0f / static_cast<float> (cols - 1);
    model.p[0] = { 0.0f, 0.0f, 0.0f };
    model.p[1] = { colX - 0.1f / static_cast<float> (cols - 1), 0.0f, 0.0f };
    model.p[2] = { colX, 1.0f, 0.0f };
    model.p[3] = { colX + 0.1f / static_cast<float> (cols - 1), 0.0f, 0.0f };
    model.p[4] = { 1.0f, 0.0f, 0.0f };
    canvas.state.shape = &model;

    const float expectedRow = canvas.yOf (1.0f);
    uit::Scene sc (400, 200, uit::kBackgroundRgba);
    canvas.draw (sc.builder());

    bool reachedRow = false;
    const gpu::SceneData& sd = *sc.data;
    for (uint32_t opIdx = 0; opIdx < sd.numOps; ++opIdx)
    {
        if (sd.ops[opIdx].op == gpu::kOpCurve)
        {
            const int first = static_cast<int> (sd.ops[opIdx].f[0]);
            const int n = static_cast<int> (sd.ops[opIdx].f[1]);
            for (int i = 0; i < n; ++i)
            {
                if (std::abs (sd.y[first + i] - expectedRow) < 1e-3f)
                {
                    reachedRow = true;
                    break;
                }
            }
        }
    }
    check (reachedRow, "shape editor: three model points inside one column, middle one a peak: peak's row is reached");
}

void testReducedEndpoints()
{
    const ui::Rect rect { 0, 0, 400, 200 };
    const auto plot = ui::lfodisplay::plotRect (rect, {});
    const int originalDetail = ui::theme::detailLevel();
    ui::theme::setDetail (2);
    for (int count : { 2, 3, 127, 128 })
    {
        float samples[128] {};
        samples[0] = -0.75f;
        samples[count - 1] = 0.25f;
        if (count > 2) samples[count - 2] = 1.0f;
        ui::lfodisplay::Data data;
        data.kind = ui::lfodisplay::Data::Kind::Shape;
        data.y = samples;
        data.n = count;
        uit::Scene scene (400, 200, uit::kBackgroundRgba);
        ui::lfodisplay::draw (scene.builder(), rect, data, false);
        bool correct = false;
        for (uint32_t i = 0; i < scene.data->numOps; ++i)
        {
            const auto& op = scene.data->ops[i];
            if (op.op != gpu::kOpCurve) continue;
            const int first = static_cast<int> (op.f[0]), last = first + static_cast<int> (op.f[1]) - 1;
            correct = last > first && scene.data->y[first] == ui::lfodisplay::rowOf (plot, samples[0])
                                   && scene.data->y[last] == ui::lfodisplay::rowOf (plot, samples[count - 1]);
            break;
        }
        check (correct, "reduced LFO keeps both endpoints for " + std::to_string (count) + " samples");
    }
    ui::theme::setDetail (originalDetail);
}

/** The Play page's LFO carousel and the LFO tab show the SAME list (used + selected + one "+"): two UIs on one instance, for an empty, a partly used and an all used preset; leaving the tab with an unused random LFO selected falls back. */
void testPlayCarouselIsTheTabSelector()
{
    struct Both
    {
        LfoUi tab, play;
        Inst inst;
        UiStack tabStack, playStack;
        double now = 1000.0;
        Both() { tabStack.openFull (LfoUi::kElementPage); refresh(); }
        void refresh() { now += 150.0; page::Input in; tab.update (inst, tabStack, now, in); play.update (inst, 3, playStack, now, nullptr); }
        void route (int slot, int source, const char* dest)
        {
            char k[32];
            std::snprintf (k, sizeof k, "mod%02d.", slot);
            inst.set (std::string (k) + "source", static_cast<float> (source));
            inst.set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx (dest)))));
            inst.set (std::string (k) + "amount", 30.0f);
            refresh();
        }
        std::string playText() const
        {
            std::string t;
            const uibind::SelectorCells& c = play.playCells();
            for (int k = 0; k < c.n; ++k)
                t += std::string (k ? " " : "") + c.names[k];
            return t;
        }
        bool sameAsTab() const
        {
            int cs[ui::UsedSelector::kMaxCells];
            const ui::UsedSelector& sel = tab.selector();
            const int n = sel.cells (cs, ui::UsedSelector::kMaxCells);
            const uibind::SelectorCells& c = play.playCells();
            if (n != c.n)
                return false;
            for (int k = 0; k < n; ++k)
                if (c.id[k] != (cs[k] == ui::UsedSelector::kPlus ? uibind::SelectorCells::kPlusCell : sel.state.items[cs[k]].id))
                    return false;
            return c.selectedPos >= 0 && c.selectedPos < c.n;
        }
    };
    {
        Both b;
        check (b.sameAsTab() && b.playText() == "LFO 1 +" && b.play.compact().instance.state.count == 2, "empty preset: the Play carousel and the tab show LFO 1, + (" + b.playText() + ")");
    }
    {
        Both b;
        b.route (1, 9, "master.volume");   // LFO 3
        b.inst.set ("rlfo2.rate", 5.0f);
        b.refresh();
        check (b.sameAsTab() && b.playText() == "LFO 1 LFO 3 RND 2 +", "partly used (LFO 3 routed, RND 2 edited): the Play carousel and the tab show LFO 1 (selected), LFO 3, RND 2, + (" + b.playText() + ")");
        b.tab.select (11, b.inst);   // RND 2 on the tab: the Play page shows it too
        b.refresh();
        check (b.sameAsTab() && b.play.random() && b.play.playCells().selectedPos == 1 && b.playText() == "LFO 3 RND 2 +", "RND 2 selected on the tab: the Play page follows (" + b.playText() + ")");
    }
    {
        Both b;
        for (int n = 1; n <= 10; ++n)
            b.route (n, 6 + (n <= 8 ? n : (n == 9 ? 27 : 28)), "master.volume");
        for (int m = 1; m <= 4; ++m)
            b.inst.set ("rlfo" + std::to_string (m) + ".rate", 3.0f + static_cast<float> (m));
        b.refresh();
        check (b.sameAsTab() && b.play.playCells().n == 14 && b.playText().find ('+') == std::string::npos && b.play.compact().instance.state.count == 14, "all fourteen used: fourteen entries, no + on both (" + b.playText() + ")");
    }
    // a random LFO chosen as the free one and not edited: leaving the tab falls back to the last used item (decision 2026-11-21, no parameter is written)
    {
        Both b;
        b.route (1, 8, "master.volume");   // LFO 2 used
        b.tab.select (10, b.inst);          // RND 1: a pick of "+", not edited
        b.refresh();
        check (b.tab.random() && b.sameAsTab(), "RND 1 picked (unused, unedited): shown while selected");
        const int w0 = b.inst.writes;
        b.tab.leftTab (b.inst);
        b.refresh();
        check (! b.tab.random() && b.tab.selection() == 2 && b.inst.tv[0] == 0 && b.inst.sel[2] == 2 && b.inst.writes == w0, "leaving the tab: back to LFO 2, the last used one; no parameter is written");
        b.inst.set ("rlfo3.rate", 9.0f);
        b.tab.select (12, b.inst);
        b.refresh();
        b.tab.leftTab (b.inst);
        check (b.tab.random() && b.tab.selection() == 3, "an edited random LFO stays selected when the tab is left");
        b.tab.select (1, b.inst);   // LFO 2, used
        b.refresh();
        b.tab.leftTab (b.inst);
        check (! b.tab.random() && b.tab.selection() == 2, "a used LFO stays selected when the tab is left");
        Both e;   // nothing used at all: the first candidate
        e.tab.select (12, e.inst);
        e.refresh();
        e.tab.leftTab (e.inst);
        e.refresh();
        check (! e.tab.random() && e.tab.selection() == 1, "nothing used: an unused pick falls back to LFO 1");
    }
}

/** The native Play Rise proxy shares the compact cell with RND STEREO.  The
    PlayView route must hand that cell to LfoUi before the host can write the
    standard LFO's stale fade_in proxy. */
void testPlayRandomRiseRoute()
{
    Inst inst;
    inst.sel[2] = 1;
    inst.tv[0] = 1; // RND 1, while Play LFO remains LFO 1
    PlayView pv (3, 10.0);
    UiStack stack;
    ViewInput in;
    in.instance = &inst; in.io = &inst; in.ui = &stack; in.nowMs = 1000.0;
    pv.update (in);
    in.nowMs += 200.0;
    pv.update (in);
    check (pv.lfoUi() != nullptr && pv.lfoUi()->random(), "PlayView: RND state reaches the compact LFO view");
    auto pixel = [&](int x, int y)
    {
        std::vector<uint32_t> pixels (1280 * 800);
        pv.render (pixels.data(), 1280, 800);
        return pixels[static_cast<size_t> (y * 1280 + x)];
    };
    check ((pixel (1100, 690) >> 24) == 255 && (pixel (1220, 690) >> 24) == 255,
           "Play RND occupies the standard Smooth control area with opaque foreground");
    const ui::Rect rise = pv.lfoUi()->compact().rise.rect();
    const ui::Rect smooth = pv.lfoUi()->compact().smoothTime.rect();
    const int x = rise.x + rise.w / 2, y = rise.y + rise.h / 2;
    const int mx = smooth.x + smooth.w / 2, my = smooth.y + smooth.h / 2;
    const float fade = inst.get ("lfo1.fade_in");
    const int writes = inst.writes;
    TouchEvent d { TouchEvent::Down, x, y }, m { TouchEvent::Move, x, y - 40 }, u { TouchEvent::Up, x, y - 40 };
    check (pv.onTouch (d, stack, inst, in.nowMs), "PlayView: RND claims the shared Rise cell");
    TouchEvent md { TouchEvent::Down, mx, my };
    check (pv.onTouch (md, stack, inst, in.nowMs), "PlayView: RND claims the shared Smooth cell");
    pv.onTouch (m, stack, inst, in.nowMs + 16.0);
    pv.onTouch (u, stack, inst, in.nowMs + 32.0);
    check (inst.get ("lfo1.fade_in") == fade && inst.writes == writes, "PlayView: RND Rise drag does not write the stale lfo1.fade_in proxy");
    const ui::Rect stereo = pv.lfoUi()->compact().stereo.rect();
    const int sx = stereo.x + stereo.w / 2, sy = stereo.y + stereo.h / 2;
    const float beforeStereo = inst.get ("rlfo1.stereo");
    TouchEvent sd { TouchEvent::Down, sx, sy }, su { TouchEvent::Up, sx, sy };
    check (pv.onTouch (sd, stack, inst, in.nowMs + 48.0), "PlayView: RND STEREO remains owned by the overlay");
    pv.onTouch (su, stack, inst, in.nowMs + 88.0);
    check (inst.get ("rlfo1.stereo") != beforeStereo && inst.get ("lfo1.fade_in") == fade, "PlayView: RND STEREO toggles without writing standard Rise");
    inst.tv[0] = 0;
    inst.set ("lfo1.smooth", 0.0f);
    pv.update (in);
    for (const int x : { 1100, 1220 })
    {
        check (pixel (x, 690) == 0
               && ! pv.onTouch ({ TouchEvent::Down, x, 690 }, stack, inst, in.nowMs),
               "Play standard LFO OFF leaves native Smooth time and switch apertures to the host");
    }
    stack.openFull (LfoUi::kElementLfo);
    pv.update (in);
    check (! pv.lfoUi()->isMain() && (pixel (1100, 690) >> 24) == 255 && (pixel (1220, 690) >> 24) == 255,
           "expanded Play LFO owns the Smooth screen area");
}

__attribute__((noinline)) void testStandaloneLfoNativeSmooth()
{
    PageRig r;
    r.inst.nativeEnabled = true;
    r.refresh();
    check (r.ui.full().nativeSmooth(), "the standalone LFO page enables native Smooth");
    check (r.ui.nativeSmooth(), "LfoUi reports native Smooth enabled");
    check (! r.ui.hostBackground(), "native Smooth keeps its aperture before the root background is confirmed");

    const ui::Rect smooth = r.ui.full().smoothArea();
    check (! smooth.empty() && smooth.w >= 100 && smooth.h >= 56, "native Smooth area covers the smooth control in RATE block");
    check (! r.ui.full().smoothSwitch.enabled(), "overlay smooth switch is disabled in standalone mode");

    const int cx = smooth.x + smooth.w / 2, cy = smooth.y + smooth.h / 2;
    check (! r.ui.covers (cx, cy), "native Smooth interior is not covered by the overlay (exclusive touch handoff)");
    check (! r.ui.touch (ev (TouchEvent::Down, cx, cy), r.stack, r.inst, r.now), "touch over native Smooth falls through to host");
    check (! r.ui.wheel (cx, cy, 1, r.stack, r.inst, r.now), "wheel over native Smooth falls through to host");

    // Scalar controls belong to the host. The display itself stays overlay-owned.
    const ui::Rect rise = r.ui.full().rise.rect();
    check (! r.ui.covers (rise.x + rise.w / 2, rise.y + rise.h / 2), "RISE is native alongside Smooth");
    const ui::Rect phase = r.ui.full().phase.rect();
    check (! r.ui.covers (phase.x + phase.w / 2, phase.y + phase.h / 2), "PHASE is native alongside Smooth");
    const ui::Rect graph = r.ui.full().displayRect();
    check (r.ui.covers (graph.x + graph.w / 2, graph.y + graph.h / 2), "the native LFO tab retains the overlay graph");

    // Rendering transparency: verify the native Smooth control interior is transparent in the standalone overlay
    uit::Scene sc (1280, 800, 0u);
    r.ui.draw (sc.builder());
    const uit::Image im = uit::render (sc);
    bool clearSmooth = true;
    for (int y = smooth.y; y < smooth.bottom(); ++y)
        for (int x = smooth.x; x < smooth.right(); ++x)
            clearSmooth &= im.at (x, y) == 0u;
    check (clearSmooth, "the complete native Smooth interior is transparent in the standalone LFO overlay");

    // Random LFO: verify native Smooth is inactive (only applicable to standard LFOs)
    r.inst.tv[0] = 1; // RND 1
    r.refresh();
    check (r.ui.random(), "LfoUi is in Random mode");
    check (! r.ui.full().nativeSmooth(), "native Smooth is false on Random LFO (inapplicable)");
    check (! r.ui.nativeSmooth(), "LfoUi nativeSmooth is false on Random LFO");
    check (r.ui.full().smoothArea().empty(), "Random LFO has empty smoothArea");

    // Compact Play protection: compact view does not hand off smooth
    Inst pinst;
    PlayView pv (3, 10.0);
    UiStack pstack;
    ViewInput pin;
    pin.instance = &pinst; pin.io = &pinst; pin.ui = &pstack; pin.nowMs = 1000.0;
    pv.update (pin);
    check (pv.lfoUi() != nullptr, "PlayView has LfoUi");
    check (pv.lfoUi()->isMain(), "PlayView LfoUi is in main compact mode");
    check (! pv.lfoUi()->nativeSmooth(), "compact Play LfoUi nativeSmooth is false");
}

void testStandaloneLfoNativeGround()
{
    namespace n = nativeui::lfo;
    const auto* asset = backgroundOf ("lfo1");
    const auto view = std::find_if (std::begin (layout::kViews), std::end (layout::kViews),
                                    [](const auto& v) { return v.kind == layout::kKindLfoUi; });
    check (asset != nullptr && view != std::end (layout::kViews), "LFO/RND has a registered host background and detector geometry");
    if (asset == nullptr || view == std::end (layout::kViews))
        return;
    auto rig = std::make_unique<PageRig>();
    auto& r = *rig;
    r.inst.nativeEnabled = true;
    const int sx = asset->sentinelX, sy = page::kTabY + asset->sentinelY;
    r.page.setSentinel ({ sx, sy, 5, 5 }, asset->sentinelRgb);
    uit::Scene groundScene (page::kW, page::kH, 0);
    page::ground (groundScene.builder());
    const auto base = uit::render (groundScene);
    auto render = [&]
    {
        uit::Image image { page::kW, page::kH, std::vector<uint32_t> (page::kW * page::kH) };
        r.page.render (image.px.data(), image.w, image.h);
        return image;
    };

    for (int family = 0; family < 2; ++family)
        for (const int sync : { 0, 1, 4 })
        {
            r.inst.tv[0] = family;
            r.key (family ? "rlfo1.sync" : "lfo1.sync", static_cast<float> (sync));
            const std::string label = (family ? "RND " : "LFO ") + std::to_string (sync);
            const auto state = r.inst.native;
            r.page.setHostBackground (false);
            check (! r.ui.hostBackground(), label + " starts with the apertured fallback");
            const auto fallback = render();
            r.page.setHostBackground (true);
            check (r.ui.hostBackground(), label + " uses the host floor after sentinel confirmation");
            const auto foreground = render();
            const auto r0 = r.ui.nativeRect (n::kSelection);
            check (foreground.at (r0.x + 10, r0.y + 10) == 0 && fallback.at (r0.x + 10, r0.y + 10) == 0,
                   label + " leaves its host selector transparent in both paths");
            bool allHoles = true;
            for (int field = 0; field < n::kCount; ++field)
                if ((state.fields & n::bit (field)) != 0)
                {
                    const auto native = r.ui.nativeRect (field);
                    const int cx = native.x + native.w / 2, cy = native.y + native.h / 2;
                    allHoles &= ! native.empty() && foreground.at (cx, cy) == 0 && fallback.at (cx, cy) == 0;
                    allHoles &= ! r.ui.covers (cx, cy);
                }
            check (allHoles, label + " native controls retain their pointer targets and pixel apertures with or without the sentinel");

            bool covers = true;
            for (const auto& spot : { view->markers[0].rect, view->instance, view->markers[1].rect })
                for (int y = spot.y - 1; y < spot.y + spot.h + 1; ++y)
                    for (int x = spot.x - 1; x < spot.x + spot.w + 1; ++x)
                        covers &= foreground.at (x, y) == (0xff000000u | ui::theme::kNativeCanvas);
            for (int y = sy; y < sy + 5; ++y)
                for (int x = sx; x < sx + 5; ++x)
                    covers &= foreground.at (x, y) == 0u;
            check (covers, label + " hides the three detector tiles in the native canvas and leaves the host sentinel visible");

            // Replay the SAME foreground scene over the ground before rasterizing.
            // This isolates 8-bit over() reassociation from layout/aperture changes:
            // fallback paints onto opaque ground; the host path first flattens the
            // translucent foreground to ARGB8 and only then composites that plane.
            uit::Scene replayScene (page::kW, page::kH, 0);
            page::ground (replayScene.builder());
            r.ui.draw (replayScene.builder());
            const auto replay = uit::render (replayScene);
            int replayDelta = 0;
            int delta = 0;
            int alphaGeometry = 0;
            size_t roundingPixel = 0;
            for (size_t i = 0; i < foreground.px.size(); ++i)
            {
                const uint32_t a = draw::over (base.px[i], fallback.px[i], 255), b = draw::over (base.px[i], foreground.px[i], 255);
                alphaGeometry += (a >> 24) != (b >> 24);
                for (unsigned shift : { 0u, 8u, 16u })
                {
                    const int channelDelta = std::abs (static_cast<int> ((a >> shift) & 255) - static_cast<int> ((b >> shift) & 255));
                    if (channelDelta > delta) roundingPixel = i;
                    delta = std::max (delta, channelDelta);
                    replayDelta = std::max (replayDelta, std::abs (static_cast<int> ((a >> shift) & 255)
                                                               - static_cast<int> ((replay.px[i] >> shift) & 255)));
                }
            }
            check (replayDelta <= 1, label + " identical foreground replayed over opaque ground agrees within one channel level");
            // A two-level plane-composite difference is accepted ONLY when the
            // identical-op opaque replay above still meets the original <=1 gate.
            check (alphaGeometry == 0 && (delta <= 1 || (delta == 2 && replayDelta <= 1)),
                   label + " fallback and host composites agree (ARGB8 reassociation delta " + std::to_string (delta) + ")");
            if (delta > 1)
                std::printf ("ARGB8 rounding evidence %s at (%zu,%zu): ground=%08x foreground=%08x fallback=%08x opaque-replay=%08x\n",
                             label.c_str(), roundingPixel % page::kW, roundingPixel / page::kW,
                             base.px[roundingPixel], foreground.px[roundingPixel], fallback.px[roundingPixel], replay.px[roundingPixel]);
            r.page.setHostBackground (false);
            check (render().px == fallback.px, label + " lost sentinel restores the same fallback on the next render");
        }

    r.inst.tv[0] = 0;
    r.refresh();
    r.page.setHostBackground (true);
    r.inst.nativeBank.post (n::kAction, 1.0f);
    r.refresh();
    auto editor = render();
    check (r.ui.topPage() == LfoUi::kPageEditor && ! r.ui.hostBackground() && r.inst.native.layout == 0
           && (editor.at (1090, 120) >> 24) == 0, "the standalone shape editor preserves native apertures and hides root controls");
    r.stack.back();
    r.refresh();
    check (r.ui.hostBackground() && r.inst.native.layout != 0, "closing the shape editor restores the confirmed host root");
}

void testStandaloneNativeLfoControls()
{
    namespace n = nativeui::lfo;
    auto rig = std::make_unique<PageRig>();
    auto& r = *rig;
    auto& inst = r.inst;
    inst.nativeEnabled = true;
    r.refresh();
    const auto post = [&](int field, float value)
    {
        inst.nativeBank.post (field, value);
        r.refresh();
    };
    const int before = inst.writes;
    for (int i = 0; i < n::kInstances; ++i)
    {
        post (n::kSelection, static_cast<float> (i) / (n::kInstances - 1));
        check (inst.native.selection == i + 1 && inst.writes == before,
               "native selector chooses source " + std::to_string (i + 1) + " without a sound write");
    }
    post (n::kSelection, 2.0f / (n::kInstances - 1));
    r.key ("lfo1.smooth", 0.0f);
    r.key ("lfo3.smooth", 0.0f);
    const float oldTime = inst.get ("lfo3.smooth_time");
    post (n::kSmoothTime, 0.65f);
    check (inst.get ("lfo3.smooth_time") == oldTime && inst.writes == before, "inactive Smooth time rejects host edits");
    inst.nativeBank.post (n::kSmooth, 1.0f);
    post (n::kSmoothTime, 0.65f);
    check (inst.get ("lfo3.smooth") == 1.0f && inst.get ("lfo1.smooth") == 0.0f && inst.lastWritten == paramIdx ("lfo3.smooth"),
           "native Smooth writes only the selected standard source");
    check (inst.get ("lfo3.smooth_time") == oldTime, "a time gesture issued while Smooth is off cannot ride along with enabling it");
    post (n::kSmoothTime, 0.65f);
    check (inst.lastWritten == paramIdx ("lfo3.smooth_time") && inst.get ("lfo3.smooth_time") != oldTime,
           "native Smooth time is editable on the enabled source");

    r.key ("lfo3.shape", 4.0f);
    const int oldShapes = inst.shapeWrites;
    post (n::kShape, 1.0f);
    check (inst.get ("lfo3.shape") == 12.0f && inst.shapeWrites == oldShapes + 1 && inst.contentPtr->lfoShapes.back().lfo == 3,
           "native Drawn initializes points from the selected LFO's previous waveform");
    check (inst.contentPtr->lfoShapes.back().points.size() == static_cast<size_t> (ui::lfo::ShapeModel::fromBuiltin (4).n),
           "native Drawn copies the previous Square shape");
    post (n::kShape, 1.0f);
    check (inst.shapeWrites == oldShapes + 1, "reselecting Drawn preserves the points");

    r.key ("lfo3.sync", 0.0f);
    check (inst.native.layout == 1, "standard Hz layout owns Rate");
    post (n::kRate, 0.71f);
    check (inst.lastWritten == paramIdx ("lfo3.rate"), "Rate reaches the selected standard oscillator");
    post (n::kSync, 0.75f);
    check (inst.native.layout == 2 && inst.get ("lfo3.sync") == 3.0f, "native Sync can choose BPM triplet");
    post (n::kDivision, 0.5f);
    check (inst.lastWritten == paramIdx ("lfo3.division"), "BPM native Division replaces Rate");
    post (n::kSync, 1.0f);
    post (n::kTranspose, 0.75f);
    check (inst.native.layout == 3 && inst.lastWritten == paramIdx ("lfo3.keytrack_transpose"), "key mode writes its own transpose");
    post (n::kTune, 0.25f);
    check (inst.lastWritten == paramIdx ("lfo3.keytrack_tune"), "key mode writes its own tune");
    check (std::find (inst.q, inst.q + QRouter::kCount, paramIdx ("lfo3.keytrack_tune")) != inst.q + QRouter::kCount
           && std::find (inst.q, inst.q + QRouter::kCount, paramIdx ("lfo3.host")) == inst.q + QRouter::kCount,
           "KEY retains both Q targets and HOST remains touch only");

    const auto zoom = r.ui.nativeRect (n::kAction, n::kExpand);
    check (zoom == ui::playrows::fullRect (r.ui.full().displayRect()), "native Expand sits in the graph's upper-right corner");
    post (n::kAction, 1.0f);
    check (r.ui.topPage() == LfoUi::kPageEditor && inst.native.layout == 0 && r.ui.covers (zoom.x + 8, zoom.y + 8),
           "native Expand opens the overlay shape editor and disables the base controls");
    inst.nativeBank.post (n::kAction, 0.5f);
    r.stack.back();
    r.refresh();
    check (r.ui.topPage() == 0, "editing discards requests from covered controls");
    post (n::kAction, 0.5f);
    check (r.ui.topPage() == LfoUi::kPageTargets && inst.native.layout == 0, "native TARGETS opens the original destination editor");
    r.stack.back();
    r.refresh();

    const int writes = inst.writes;
    inst.nativeBank.post (n::kShape, 0.5f);
    inst.sel[2] = 5;
    ++inst.generation;
    r.refresh();
    check (inst.native.selection == 5 && inst.writes == writes, "preset generation invalidates a queued waveform edit");
    inst.nativeBank.post (n::kShape, 0.75f);
    r.ui.leftTab (inst);
    r.refresh();
    check (inst.writes == writes, "a queued edit cannot survive leaving the tab");
    post (n::kSelection, 11.0f / (n::kInstances - 1));
    check (r.ui.random() && r.ui.selection() == 2 && inst.native.layout >= 4, "native selection shows RND 2");
    post (n::kStyle, 0.75f);
    check (inst.lastWritten == paramIdx ("rlfo2.style"), "RND style writes the actual selected random source");
    const int randomWrites = inst.writes;
    post (n::kSmooth, 1.0f);
    post (n::kAction, 1.0f);
    check (inst.writes == randomWrites && r.ui.topPage() == 0, "RND has no Smooth or Expand edit");
}

void testExpandPixelsWithLiveData()
{
    auto render = [] (const LfoUi& view)
    {
        auto scene = std::make_unique<uit::Scene> (1280, 800, 0u);
        view.draw (scene->builder());
        return uit::render (*scene);
    };
    auto equalCell = [] (const uit::Image& a, const uit::Image& b, ui::Rect r)
    {
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x)
                if (a.at (x, y) != b.at (x, y))
                    return false;
        return true;
    };
    auto standalone = std::make_unique<PageRig>();
    standalone->inst.nativeEnabled = true;
    standalone->inst.set ("lfo1.phase", 353.0f);
    standalone->inst.set ("lfo1.delay", 350.0f);
    auto play = std::make_unique<Rig>();
    play->inst.set ("lfo1.shape", static_cast<float> (ui::lfo::kShapeCustom));
    play->inst.set ("lfo1.phase", 353.0f);
    play->inst.set ("lfo1.delay", 350.0f);
    play->openFull();
    synthyboy::LiveValues live;
    live.numLfoVoices = 1;
    live.lfoVoicePhase[0][0] = 0.98f;
    live.source[uibind::lfoSource (1) - 1] = 1.0f;

    for (bool native : { false, true })
    {
        LfoUi& view = native ? standalone->ui : play->ui;
        auto refresh = [&] { if (native) standalone->refresh(); else play->refresh(); };
        for (int i = 0; i < 5; ++i)
            refresh();
        const auto edit = view.full().edit.rect();
        const auto plot = view.full().displayRect();
        const auto settled = render (view);
        if (native)
        {
            standalone->live = &live;
            standalone->inst.set ("lfo1.shape", 4.0f);
        }
        else
        {
            play->live = live;
            play->useLive = true;
            play->inst.set ("lfo1.phase", 356.0f);
        }
        refresh();
        const auto hot = render (view);
        check (! edit.empty() && edit == ui::playrows::fullRect (plot) && view.displayData().numCursors == 1
                   && equalCell (settled, hot, edit) && ! equalCell (settled, hot, plot),
               native ? "standalone LFO hot waveform and cursor leave every Expand pixel untouched"
                      : "expanded Play LFO live phase and cursor leave the Expand glyph unchanged");
        if (native)
        {
            bool clear = true;
            for (int y = edit.y; y < edit.bottom(); ++y)
                for (int x = edit.x; x < edit.right(); ++x)
                    clear &= hot.at (x, y) == 0u;
            check (clear && ! standalone->page.onTouch (ev (TouchEvent::Down, edit.x + 8, edit.y + 8),
                                                        standalone->stack, standalone->inst, standalone->now),
                   "standalone corner EXPAND is fully transparent and passes touch to the host");
            standalone->panelEdge = edit.x + 20;
            refresh();
            std::vector<uint32_t> clipped (1280 * 800);
            standalone->page.render (clipped.data(), 1280, 800);
            bool panelClear = true;
            for (int y = 0; y < 800; ++y)
                for (int x = standalone->panelEdge; x < 1280; ++x)
                    panelClear &= clipped[static_cast<size_t> (y * 1280 + x)] == 0u;
            check (panelClear, "the Expand aperture respects the real PageView Q-Link panel clip");
            standalone->inst.tv[0] = 1;
            refresh();
            check (view.random() && view.full().edit.rect().empty(), "RND has no Expand aperture");
        }
        else
        {
            bool icon = false;
            for (int y = edit.y + 8; y < edit.bottom() - 8; ++y)
                for (int x = edit.x + 8; x < edit.right() - 8; ++x)
                    icon |= hot.at (x, y) != hot.at (edit.x + 5, edit.y + 5);
            check (icon, "the Play expansion renders an actual corner glyph");
        }
    }
}

void testPointModSelectionGuards()
{
    Rig r;
    openPointModPage (r, true);
    auto& editor = r.ui.editor();
    auto& page = r.ui.pointModPage();
    auto& model = editor.model;
    model.p[0].my = { 8, 0.12f };
    r.refresh();
    check (std::strstr (page.summary(), "MIXED SOURCES") != nullptr && ! page.amount.enabled()
               && page.remove.enabled() && r.inst.q[1] == -1, "Point Mod reports mixed sources without claiming a shared amount");
    const auto mixedSource = model;
    r.inst.hostQ (1, 0.88f);
    r.refresh();
    check (mixedSource == model, "mixed-source amount is non-writing");
    model.p[0].my = { 7, 0.12f };
    r.refresh();
    check (std::strstr (page.summary(), "MIXED AMOUNTS") != nullptr && page.amount.enabled() && page.amount.state.notSet,
           "a common source with differing amounts displays a mixed value");
    const auto board = page.board.chipRectOfId (7);
    const int writes = r.inst.shapeWrites;
    editor.canvas.selectOnly (-1);
    r.refresh();
    check (std::strcmp (page.summary(), "NO POINT SELECTED") == 0 && page.back.enabled()
               && ! page.target.enabled() && ! page.board.enabled() && ! page.remove.enabled() && ! page.amount.enabled()
               && r.inst.q[0] == -1 && r.inst.q[1] == -1, "empty Point Mod selection disables all point input and keeps BACK");
    r.tap (board.x + board.w / 2, board.y + board.h / 2);
    r.inst.hostQ (0, 1.0f);
    r.inst.hostQ (1, 0.87f);
    r.refresh();
    check (r.inst.shapeWrites == writes, "point-specific touch and Q produce zero writes without a selection");
    r.tapC (page.back);
    check (r.ui.topPage() == LfoUi::kPageEditor, "Point Mod BACK remains usable without selection");
}

void testNativeEditorOwnership()
{
    namespace n = nativeui::lfoeditor;
    auto rig = std::make_unique<PageRig>();
    auto& r = *rig;
    r.inst.nativeEnabled = r.inst.nativeEditorEnabled = r.inst.nativeDetailEnabled = true;
    r.refresh();
    r.inst.nativeBank.post (nativeui::lfo::kAction, 1.0f);
    r.refresh();
    auto& editor = r.ui.editor();
    ui::Component* leaves[32];
    check (editor.collectLeaves (leaves, 0, 32) == 1 && leaves[0] == &editor.canvas,
           "editor GPU tree contains only the curve canvas");
    for (bool available : { true, false })
    {
        r.inst.nativeEditorEnabled = available;
        r.refresh();
        for (int pass = 0; pass < 4; ++pass)
        {
            auto scene = std::make_unique<uit::Scene> (1280, 800, 0u);
            if (pass == 0) r.ui.drawPage (scene->builder());
            if (pass == 1) r.ui.drawLive (scene->builder());
            if (pass == 2) r.ui.drawPopup (scene->builder());
            if (pass == 3) r.ui.draw (scene->builder());
            const auto pixels = uit::render (*scene);
            for (int field = 0; field < n::kCount; ++field)
            {
                const auto rect = r.ui.nativeEditorRect (field);
                check ((pixels.at (rect.x + rect.w / 2, rect.y + rect.h / 2) >> 24) == 0,
                       "all native editor apertures stay transparent in every pass with either availability");
            }
        }
    }
    r.inst.nativeEditorEnabled = true;
    r.refresh();
    editor.canvas.selectOnly (1);
    editor.sync();
    editor.setEnabled (false);
    editor.sync();
    for (auto* c : editor.nativeControls()) check (! c->enabled(), "disabled editor cannot rearm native adapter");
    check (! editor.turnQ (0, 1.0), "disabled editor rejects Q edits");
    editor.setInactive (true);
    for (auto* c : editor.nativeControls()) check (c->inactive(), "editor inactive state reaches detached native adapters");
    editor.setEnabled (true);
    editor.setInactive (false);
    r.refresh();
    r.inst.nativeEditorBank.post (r.inst.nativeEditorBank.at (n::kMod), 1.0f);
    r.refresh();
    auto& point = r.ui.pointModPage();
    check (point.collectLeaves (leaves, 0, 32) == 1 && leaves[0] == &point.board,
           "Point Mod keeps only the uncovered source board in GPU input tree");
    point.setEnabled (false);
    point.load (1, "LFO 1", 50.0, 1, 1);
    check (! point.target.enabled() && ! point.amount.enabled() && ! point.remove.enabled() && ! point.board.enabled(),
           "Point Mod load cannot rearm disabled parent controls");
}

void testNativeShapeEditor()
{
    namespace n = nativeui::lfoeditor;
    auto rig = std::make_unique<PageRig>();
    auto& r = *rig;
    auto& inst = r.inst;
    auto& editor = r.ui.editor();
    inst.nativeEnabled = inst.nativeEditorEnabled = true;
    inst.sel[2] = 3;
    r.refresh();
    inst.nativeBank.post (nativeui::lfo::kAction, 1.0f);
    r.refresh();
    check (r.ui.topPage() == LfoUi::kPageEditor && inst.nativeEditor.context.owner == 3 && inst.native.layout == 0,
           "standalone Expand opens native Shape Editor controls for the selected LFO");

    const auto post = [&] (int field, double plain)
    {
        const auto state = inst.nativeEditorBank.display();
        const auto range = n::rangeOf (field, state.points, state.scale);
        inst.nativeEditorBank.post (inst.nativeEditorBank.at (field), range.encode (plain));
    };
    const auto set = [&] (int field, double plain) { post (field, plain); r.refresh(); };
    const int initialShapes = inst.shapeWrites;
    const int initialWrites = inst.writes;
    const auto offset = [] (ui::Rect rect, int x, int y) { return std::pair<int, int> { rect.x + x, rect.y + y }; };
    const auto [x, y] = offset (r.ui.nativeEditorRect (n::kX), 12, 11);
    const auto [gx, gy] = offset (r.ui.nativeEditorRect (n::kGridX), 12, 11);
    const auto [cx, cy] = offset (r.ui.editorCanvasRect(), 12, 65);
    {
        auto scene = std::make_unique<uit::Scene> (1280, 800, 0u);
        r.ui.draw (scene->builder());
        const auto pixels = uit::render (*scene);
        check ((pixels.at (x, y) >> 24) == 0 && (pixels.at (gx, gy) >> 24) == 0 && (pixels.at (cx, cy) >> 24) == 255
                   && r.ui.covers (x, y) && ! r.ui.covers (gx, gy) && r.ui.covers (cx, cy),
               "active and passive native controls use transparent cells while the overlay owns the canvas and blocks disabled input");
    }
    r.tap (x, y);
    set (n::kUndo, 1);
    set (n::kDelete, 1);
    set (n::kY, 0.18);
    check (inst.shapeWrites == initialShapes && inst.writes == initialWrites && editor.undo.depth() == 0,
           "disabled controls produce no value, content or undo writes");
    set (n::kPoint, 1);
    check (editor.canvas.state.selected == 0 && ! inst.nativeEditor.allows (n::kX) && inst.nativeEditor.allows (n::kY),
           "native POINT selects the first point and its fixed X remains protected");
    set (n::kPoint, 2);
    check (editor.canvas.state.selected == 1 && inst.nativeEditor.allows (n::kX), "interior point arms native X");
    set (n::kX, 0.37);
    set (n::kY, 0.82);
    set (n::kCurve, -6);
    check (std::abs (editor.model.p[1].x - 0.37f) < 1e-6f && std::abs (editor.model.p[1].y - 0.82f) < 1e-6f
               && editor.model.p[1].power == -6.f, "native hundredths and signed values reach the selected point");
    const int noOpShapes = inst.shapeWrites, noOpUndo = editor.undo.depth();
    set (n::kX, 0.37);
    check (inst.shapeWrites == noOpShapes && editor.undo.depth() == noOpUndo, "native echo creates no content write or undo step");
    set (n::kUndo, 1);
    check (editor.model.p[1].power == 0.0f && inst.nativeEditor.allows (n::kRedo), "native UNDO restores the authored curve");
    set (n::kRedo, 1);
    check (editor.model.p[1].power == -6.f, "native REDO restores the changed curve");
    set (n::kTool, ui::ToolSelector::Select);
    editor.canvas.state.marks.selectAll (editor.model.n);
    r.refresh();
    check (editor.canvas.state.tool == ui::ToolSelector::Select && inst.nativeEditorBank.display().scale, "native Tool selects the original gesture and multiple points expose SCALE");
    set (n::kCurve, 0.61);
    check (std::abs (n::rangeOf (n::kCurve, editor.model.n, true).decode (inst.nativeEditor.values[n::kCurve]) - 0.61) < 1e-6,
           "the SCALE field uses its own hundredth range and reports the new height");

    set (n::kGridX, 19);
    set (n::kGridY, 11);
    const bool snap = editor.canvas.state.snap;
    set (n::kSnap, snap ? 0 : 1);
    const bool smooth = editor.model.smooth;
    set (n::kSmooth, smooth ? 0 : 1);
    set (n::kCycleLink, 1);
    set (n::kLoopLink, 1);
    set (n::kPoint, 2);
    set (n::kLoop, 1);
    check (editor.canvas.state.gridX == 19 && editor.canvas.state.gridY == 11 && editor.canvas.state.snap != snap
               && editor.model.smooth != smooth && editor.model.linkCycleEnds && editor.model.linkLoopEnds && editor.model.loop == 1,
           "all native grid and toggle controls feed the original editor model");
    const float oldBend = editor.model.meanCurve();
    set (n::kBend, 12);
    check (editor.model.meanCurve() != oldBend, "native BEND ALL changes the authored curves");
    set (n::kMode, 3);
    set (n::kPhase, 123);
    check (inst.get ("lfo3.sync_type") == 3.0f && inst.get ("lfo3.phase") == 123.0f
               && inst.lastWritten == paramIdx ("lfo3.phase"), "native MODE and PHASE route to the selected LFO's engine parameters");
    const auto mode = r.ui.nativeEditorRect (n::kMode);
    check (! r.page.onTouch (ev (TouchEvent::Down, mode.x + 2, mode.y + 10), r.stack, inst, r.now)
               && editor.activeGroup() == 2 && ! r.ui.fingerDown(), "MODE reaches the host while selecting the existing TRIGGER Q-Link group");

    post (n::kMode, 4);
    ++inst.generation;
    r.refresh();
    check (inst.get ("lfo3.sync_type") == 3.0f, "a queued mode from the previous preset is rejected");
    post (n::kY, 0.19);
    editor.canvas.selectOnly (0);
    const float initialY = editor.model.p[0].y;
    r.refresh();
    check (editor.model.p[0].y == initialY, "an edit owed to another point is rejected before touching the new point");

    for (int field : { n::kShapes, n::kMore, n::kMod })
    {
        set (field, 1);
        const int want = field == n::kMod ? LfoUi::kPagePointMod : LfoUi::kPageList;
        check (r.ui.topPage() == want && ! inst.nativeEditorBank.display().shown,
               "native navigation opens its established detail, hiding and disarming underlying host cells");
        r.stack.back();
        r.refresh();
        check (inst.nativeEditorBank.display().shown, "closing the detail reopens the same Shape Editor");
    }
    set (n::kPoint, 2);
    set (n::kLoopLink, 0);   // the enabled phase seam can materialise an extra point during deletion
    const int pointCount = editor.model.n;
    set (n::kDelete, 1);
    check (editor.model.n == pointCount - 1, "native DELETE removes the selected interior point");
    post (n::kGridX, 21);
    r.ui.leftTab (inst);
    r.refresh();
    check (editor.canvas.state.gridX == 19, "leaving the tab cancels native writes yet to be consumed");
    set (n::kBack, 1);
    check (r.stack.depth() == 1 && ! inst.nativeEditorBank.display().shown && inst.native.layout != 0,
           "native BACK returns to the standalone root and withdraws editor handles");

    Rig compact;
    compact.inst.nativeEditorEnabled = true;
    compact.openFull();
    compact.stack.openDetail (LfoUi::kPageEditor);
    compact.refresh();
    check (compact.inst.nativeEditorBank.display().shown, "the Play expansion uses the same native editor controls");
}

void testNativeLfoDetails()
{
    namespace n = nativeui::lfodetail;
    namespace e = nativeui::lfoeditor;
    auto rig = std::make_unique<PageRig>();
    auto& r = *rig;
    auto& inst = r.inst;
    inst.nativeEnabled = inst.nativeEditorEnabled = inst.nativeDetailEnabled = true;
    TempDir files;
    inst.userRoots = { files.path };
    LfoShapeLibrary library;
    library.setRoots (inst.userRoots);
    const auto triangle = ui::lfo::ShapeModel::fromBuiltin (1);
    bool saved = true;
    for (int i = 1; i <= 12; ++i)
        saved &= library.save ("Stored " + std::to_string (100 + i), triangle, false);
    check (saved, "native menu fixture saves twelve actual shape files");

    r.refresh();
    inst.nativeBank.post (nativeui::lfo::kAction, 1.0f);
    r.refresh();
    auto edit = [&] (int field, float value = 1.0f)
    {
        inst.nativeEditorBank.post (inst.nativeEditorBank.at (field), value);
        r.refresh();
    };
    auto post = [&] (int field, double plain = 1.0)
    {
        inst.nativeDetail.post (inst.nativeDetail.at (field), n::rangeOf (field).encode (plain));
    };
    auto set = [&] (int field, double plain = 1.0) { post (field, plain); r.refresh(); };
    auto name = [&] (int field)
    {
        char text[n::kLabelSize];
        inst.nativeDetail.label (field, text, sizeof text);
        return std::string (text);
    };
    auto& editor = r.ui.editor();
    const auto& model = editor.model;
    edit (e::kPoint, e::rangeOf (e::kPoint, model.n, false).encode (2.0));
    edit (e::kMod);
    check (inst.nativeDetail.display().view == n::PointMod && inst.nativeDetail.display().point == n::NoMod,
           "native Point Mod starts with selectable target, passive amount and usable back");
    const auto amount = r.ui.nativeDetailRect (n::Amount);
    const auto back = r.ui.nativeDetailRect (n::Back);
    const auto board = r.ui.pointModPage().board.chipRectOfId (7);
    r.tap (board.x + board.w / 2, board.y + board.h / 2);
    set (n::Amount, -27);
    check (model.p[1].my.source == 7 && std::abs (model.p[1].my.amount + 0.27f) < 1e-6f,
           "the point source board stays operable and native keypad applies signed amount to its exact source");
    set (n::Target, 2);
    const int writes = inst.shapeWrites;
    set (n::Amount, 65);
    check (! model.p[1].mc.on() && inst.shapeWrites == writes && r.ui.covers (amount.x + 8, amount.y + 8)
               && ! r.ui.covers (back.x + 8, back.y + 8), "native target changes availability while disabled amount stays visible and non-writing");
    set (n::Target, 1);
    post (n::Amount, 55);
    editor.canvas.selectOnly (0);
    r.refresh();
    check (std::abs (model.p[1].my.amount + 0.27f) < 1e-6f && ! model.p[0].my.on(),
           "Point Mod rejects amount queued for another selection");
    editor.canvas.selectOnly (1);
    r.refresh();
    set (n::Remove);
    check (! model.p[1].my.on() && inst.nativeDetail.display().point == n::NoMod,
           "native REMOVE clears just the selected point modulation");
    editor.canvas.selectOnly (-1);
    r.refresh();
    const int emptyWrites = inst.shapeWrites;
    set (n::Amount, 75);
    set (n::Target, 0);
    set (n::Remove);
    check (inst.nativeDetail.display().point == n::NoPoints && inst.shapeWrites == emptyWrites
               && inst.nativeDetail.at (n::Back).cell().target == n::Back,
           "empty native Point Mod keeps BACK armed with no other writes");
    set (n::Back);
    check (r.ui.topPage() == LfoUi::kPageEditor && inst.nativeDetail.display().view == n::Hidden,
           "native Point Mod BACK restores the shape editor");

    edit (e::kShapes);
    check (inst.nativeDetail.display().view == n::Menu && name (n::Header) == "SHAPES"
               && name (n::FirstRow) == "FACTORY SHAPES" && inst.nativeDetail.display().rows == 6,
           "the host sees native SHAPES captions and the six original options");
    set (n::FirstRow + 1);
    check (inst.nativeDetail.display().view == n::ScrollingMenu && name (n::Header) == "MY SHAPES"
               && name (n::FirstRow) == "Stored 101" && inst.nativeDetail.display().rows == n::kRows,
           "the native rows read actual dynamic names from a scrollable shape library");
    const auto row = r.ui.nativeDetailRect (n::FirstRow);
    const auto scrollbar = r.ui.list().zoneRect();
    const auto thumb = r.ui.list().thumbRect();
    std::vector<uint32_t> image (1280 * 800);
    r.page.render (image.data(), 1280, 800);
    const auto at = [&] (int x, int y) { return image[static_cast<size_t> (y * 1280 + x)]; };
    check (at (row.x + 14, row.y + 14) == 0u && at (thumb.x + 2, thumb.y + 8) != 0u
               && ! r.ui.covers (row.x + 14, row.y + 14) && r.ui.covers (thumb.x + 2, thumb.y + 8),
           "the real PageView leaves the native row aperture while retaining the custom scrollbar");
    post (n::FirstRow);
    r.tap (scrollbar.x + scrollbar.w / 2, scrollbar.bottom() - 4);
    check (r.ui.topPage() == LfoUi::kPageList && name (n::FirstRow) == "Stored 105",
           "scrolling rejects a queued command from the previous row window");
    r.page.render (image.data(), 1280, 800);
    set (n::FirstRow + 1);
    check (r.ui.topPage() == LfoUi::kPageEditor && model == triangle
               && std::string (editor.status()) == "LOADED Stored 106", "the native row loads the file under its current caption");

    edit (e::kMore);
    set (n::FirstRow + 2);
    check (name (n::Header) == "COPY" && name (n::FirstRow) == "COPY TO LFO ...", "native MORE opens its original COPY subgroup");
    set (n::FirstRow);
    check (name (n::Header) == "COPY THIS SHAPE TO" && name (n::FirstRow) == "LFO 2", "the native copy chooser omits its own source");
    post (n::FirstRow);
    synthyboy::StateContent recalled = *inst.contentPtr;
    synthyboy::ContentLfoShape newShape;
    newShape.lfo = 1;
    newShape.points = { { 0.0f, 0.1f }, { 0.4f, 0.7f }, { 1.0f, 0.2f } };
    recalled.lfoShapes = { newShape };
    inst.contentPtr = std::make_shared<const synthyboy::StateContent> (std::move (recalled));
    ++inst.generation;
    r.refresh();
    check (inst.get ("lfo2.shape") != static_cast<float> (ui::lfo::kShapeCustom)
               && model.n == 3 && model.p[1].x == 0.4f && model.p[1].y == 0.7f,
           "native copy ignores a queued row after preset recall and reloads the underlying shape");
    set (n::Header);
    check (name (n::Header) == "COPY", "the active native header returns one level");
    r.tap (42, 300);
    check (r.ui.topPage() == LfoUi::kPageEditor && inst.nativeDetail.display().view == n::Hidden,
           "tapping the overlay backdrop still dismisses the exclusive native list");

    edit (e::kShapes);
    set (n::FirstRow + 5);
    set (n::FirstRow);
    check (name (n::Header) == "DELETE Stored 101?" && name (n::FirstRow + 1) == "CANCEL",
           "native confirmation publishes its dynamic title and cancel choice");
    set (n::FirstRow + 1);
    library.refresh();
    check (library.find ("Stored 101") >= 0 && r.ui.topPage() == LfoUi::kPageEditor,
           "cancelling the native file action preserves the stored shape");
}

struct PlayEnvLfoRig
{
    Inst inst;
    UiStack stack;
    PlayView view { 3, 10.0 };
    synthyboy::LiveValues live;
    double now = 2000;
    PlayEnvLfoRig() { inst.nativePlayEnvLfoEnabled = true; refresh(); }
    void refresh()
    {
        ViewInput input;
        input.instance = input.io = &inst;
        input.ui = &stack;
        input.live = &live;
        input.nowMs = now += 80;
        view.update (input);
        for (int field = 0; field < nativeui::playenvlfo::Count; ++field)
            inst.nativePlayEnvLfo.report (inst.nativePlayEnvLfo.at (field));
    }
    ui::Rect rect (int field) noexcept { return PlayEnvLfo::rect (*view.envUi(), *view.lfoUi(), field); }
    void post (int field, float value = 1.0f)
    {
        inst.nativePlayEnvLfo.post (field, value, inst.stateGeneration(), inst.nativePlayEnvLfoRevision());
    }
    void openPopup (int field)
    {
        namespace n = nativeui::playenvlfo;
        const auto* match = std::find (std::begin (n::kPopupFields), std::end (n::kPopupFields), field);
        view.setNativeActionPopup (match != std::end (n::kPopupFields) ? static_cast<int> (match - std::begin (n::kPopupFields)) + n::kFirstPopup : -1);
    }
    void set (int field, float value = 1.0f)
    {
        openPopup (field);
        post (field, value);
        openPopup (-1);
        refresh();
    }
};

void testNativePlayEnvLfo()
{
    namespace n = nativeui::playenvlfo;
    auto rig = std::make_unique<PlayEnvLfoRig>();
    auto& r = *rig;
    auto& inst = r.inst;
    inst.sel[1] = 3;
    inst.sel[2] = 4;
    r.refresh();
    check (inst.nativePlayEnvLfo.layout() == n::LfoHz
               && inst.nativePlayEnvLfo.at (n::Shape).cell().target == paramIdx ("lfo4.shape")
               && inst.nativePlayEnvLfo.at (n::Rate).cell().target == paramIdx ("lfo4.rate"),
           "compact native SHAPE and Hz RATE resolve to the selected LFO");
    r.set (n::Shape, 1.0f);
    r.set (n::Rate, 0.68f);
    check (inst.get ("lfo4.shape") == ui::lfo::kShapeCustom && inst.lastWritten == paramIdx ("lfo4.rate")
               && ! inst.contentPtr->lfoShapes.empty() && inst.contentPtr->lfoShapes.front().lfo == 4,
           "compact SHAPE Drawn preserves authored content initialisation and RATE reaches LFO 4");
    inst.set ("lfo4.sync", 2);
    r.refresh();
    r.set (n::Division, 0.5f);
    check (inst.nativePlayEnvLfo.layout() == n::LfoBpm
               && inst.nativePlayEnvLfo.at (n::Rate).cell().target == -1 && inst.lastWritten == paramIdx ("lfo4.division")
               && std::find (inst.q, inst.q + QRouter::kCount, paramIdx ("lfo4.division")) != inst.q + QRouter::kCount,
           "BPM replaces RATE with the selected division and follows the same physical Q target");
    r.post (n::StandardSync, 0.5f);
    r.refresh();
    check (inst.get ("lfo4.sync") == 2.0f, "standard native BPM preserves an active dotted subdivision");
    r.post (n::StandardSync, 1.0f);
    r.refresh();
    check (inst.get ("lfo4.sync") == 4.0f, "standard native KEY routes directly through the existing mode logic");
    const auto writes = inst.writes;
    r.set (n::Rate, 0.81f);
    check (inst.nativePlayEnvLfo.layout() == n::LfoKey && inst.writes == writes
               && std::find (inst.q, inst.q + QRouter::kCount, paramIdx ("lfo4.rate")) == inst.q + QRouter::kCount,
           "KEY keeps its passive rate, rejects both native speed writes and clears the RATE Q-Link");
    inst.tv[0] = 2;
    r.refresh();
    check (r.view.lfoUi()->random() && inst.nativePlayEnvLfo.selection() == n::choiceValue (12)
               && inst.nativePlayEnvLfo.at (n::Style).cell().target == paramIdx ("rlfo2.style"),
           "Play follows the available RND view without changing the last standard LFO");
    r.set (n::Style, 0.5f);
    r.set (n::Stereo, 1.0f);
    r.set (n::Sync, 0.25f);
    r.set (n::Division, 0.3f);
    check (inst.get ("rlfo2.style") == 2.0f && inst.get ("rlfo2.stereo") == 1.0f && inst.get ("rlfo2.sync") == 1.0f
               && inst.lastWritten == paramIdx ("rlfo2.division"), "native RND Style, Stereo, Sync and Division target RND 2");
    r.set (n::Choose, n::choiceValue (4));
    check (! r.view.lfoUi()->random() && r.view.lfoUi()->selection() == 4 && inst.tv[0] == 0,
           "choosing the same last LFO from RND returns to the standard compact controls");
    r.set (n::Choose, n::choiceValue (14));
    check (r.view.lfoUi()->random() && r.view.lfoUi()->selection() == 4 && inst.sel[2] == 4,
           "the same native chooser opens RND 4 and retains the last standard index");
    r.set (n::Choose, n::choiceValue (4));

    std::vector<uint32_t> pixels (1280 * 800);
    r.view.render (pixels.data(), 1280, 800);
    const auto at = [&] (int x, int y) { return pixels[static_cast<size_t> (y * 1280 + x)]; };
    for (int field : { n::StandardSync, n::Sync })
    {
        r.set (n::Choose, n::choiceValue (field == n::StandardSync ? 4 : 12));
        r.view.render (pixels.data(), 1280, 800);
        const auto sync = r.rect (field);
        const int x = sync.x + sync.w / 2, y = sync.y + sync.h / 2;
        check (! sync.empty() && at (x, y) == 0u && ! r.view.onTouch (ev (TouchEvent::Down, x, y), r.stack, inst, r.now),
               "the active Sync control has a transparent cell and passes touches to the MPC in both families");
    }
    r.set (n::Choose, n::choiceValue (4));
    r.view.render (pixels.data(), 1280, 800);
    for (int field : { n::EnvExpand, n::LfoExpand })
    {
        const auto expand = r.rect (field);
        const auto graph = field == n::EnvExpand ? r.view.envUi()->compact().graphRect() : r.view.lfoUi()->compact().displayRect();
        check (expand == ui::playrows::fullRect (graph) && at (expand.x + 12, expand.y + 12) == 0u
                   && at (graph.x + graph.w / 2, graph.y + graph.h / 2) != 0u
                   && ! r.view.onTouch (ev (TouchEvent::Down, expand.x + 12, expand.y + 12), r.stack, inst, r.now),
               "each compact graph exposes the native corner button and keeps the overlay plot");
        r.set (field);
        check (r.stack.depth() == 1 && r.stack.element() == (field == n::EnvExpand ? EnvUi::kElementEnv : LfoUi::kElementLfo)
                   && inst.nativePlayEnvLfo.layout() == n::Hidden, "each native Expand opens its own full panel and hides compact controls");
        r.stack.back();
        r.refresh();
        check (r.stack.isMain() && inst.nativePlayEnvLfo.layout() != n::Hidden, "closing a full panel restores the compact native controls");
    }
    r.post (n::LfoExpand);
    ++inst.generation;
    r.refresh();
    check (r.stack.isMain(), "a queued compact expansion does not survive preset recall");
    r.post (n::EnvExpand);
    r.view.onHidden();
    r.refresh();
    check (r.stack.isMain(), "leaving Play disarms a queued native expansion");
}

void nativePlayEnvLfoContract()
{
    namespace n = nativeui::playenvlfo;
    auto rig = std::make_unique<PlayEnvLfoRig>();
    auto& r = *rig;
    bool first = true;
    std::printf ("[");
    for (int family = 0; family < 2; ++family)
    {
        r.inst.tv[0] = family;
        for (int sync : { 0, 1, 4 })
        {
            r.inst.set (family == 0 ? "lfo1.sync" : "rlfo1.sync", static_cast<float> (sync));
            r.refresh();
            const int layout = r.inst.nativePlayEnvLfo.layout();
            for (int field = 0; field < n::Count; ++field)
            {
                if (r.inst.nativePlayEnvLfo.at (field).cell().target < 0)
                    continue;
                const auto rect = r.rect (field);
                std::printf ("%s\n  [%d,%d,%d,%d,%d,%d]", first ? "" : ",", layout, field, rect.x, rect.y - LfoUi::kTabY, rect.w, rect.h);
                first = false;
            }
        }
    }
    std::printf ("\n]\n");
}

void nativeLfoDetailContract()
{
    namespace n = nativeui::lfodetail;
    LfoUi ui;
    ui.list().state.header = "CONTRACT";
    ui.list().state.count = n::kRows;
    ui.list().reserveScrollZone (true);
    std::printf ("[");
    for (int field = 0; field < n::kCount; ++field)
    {
        const auto r = ui.nativeDetailRect (field);
        std::printf ("%s\n  [%d,%d,%d,%d]", field == 0 ? "" : ",", r.x, r.y - LfoUi::kTabY, r.w, r.h);
    }
    std::printf ("\n]\n");
}

void nativeShapeEditorContract()
{
    namespace n = nativeui::lfoeditor;
    LfoUi ui;
    const auto rect = [] (const ui::Rect& r)
    {
        std::printf ("[%d,%d,%d,%d]", r.x, r.y - LfoUi::kTabY, r.w, r.h);
    };
    std::printf ("{\n  \"fields\": [");
    for (int field = 0; field < n::kCount; ++field)
    {
        const char* types[] { "Action", "Tool", "Toggle", "Number", "Choice" };
        const auto& info = n::kFields[field];
        std::printf ("%s\n    {\"name\":\"%s\",\"type\":\"%s\",\"rect\":", field == 0 ? "" : ",", info.name, types[info.type]);
        rect (ui.nativeEditorRect (field));
        std::printf ("}");
    }
    std::printf ("\n  ],\n  \"availability\": [");
    for (int i = 0; i < n::kAvailabilityCount; ++i)
        std::printf ("%s%d", i == 0 ? "" : ",", n::kOptional[i]);
    std::printf ("],\n  \"tools\": [");
    for (int i = 0; i < ui::ToolSelector::kTools; ++i)
    {
        std::printf ("%s\n    {\"name\":\"%s\",\"rect\":", i == 0 ? "" : ",", ui::ToolSelector::name (i));
        rect (ui.nativeEditorToolRect (i));
        std::printf ("}");
    }
    std::printf ("\n  ],\n  \"canvas\": ");
    rect (ui.editorCanvasRect());
    std::printf ("\n}\n");
}

void nativeContract ()
{
    namespace n = nativeui::lfo;
    auto rig = std::make_unique<PageRig>();
    rig->inst.nativeEnabled = true;
    bool first = true;
    std::printf ("[");
    for (int family = 0; family < 2; ++family)
    {
        rig->inst.tv[0] = family;
        for (int sync : { 0, 1, 4 })
        {
            rig->inst.set (family == 0 ? "lfo1.sync" : "rlfo1.sync", static_cast<float> (sync));
            rig->refresh();
            const auto state = rig->inst.native;
            for (int field = 0; field < n::kCount; ++field)
            {
                if ((state.fields & n::bit (field)) == 0)
                    continue;
                const int actions = field == n::kAction && family == 0 ? 2 : 1;
                for (int action = 0; action < actions; ++action)
                {
                    const auto r = rig->ui.nativeRect (field, action + 1);
                    std::printf ("%s\n  [%d, %d, %d, %d, %d, %d, %d]", first ? "" : ",",
                                 state.layout, field, field == n::kAction ? action + 1 : 0, r.x, r.y - LfoUi::kTabY, r.w, r.h);
                    first = false;
                }
            }
        }
    }
    std::printf ("\n]\n");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--test-editor-fixtures") == 0)
    {
        testEditor();
        testEditorMarker();
        testEditorShapesList();
        testEditorActions();
        testEditorPointModulation();
        testEditorPointModulationStored();
        testPointModulationQKnobs();
        testPointModSelectionGuards();
        std::printf ("%s: %d failed\n", uit::g_fails ? "FAIL" : "PASS", uit::g_fails);
        return uit::g_fails ? 1 : 0;
    }
    if (argc == 2 && std::strcmp (argv[1], "--test-native-editor") == 0)
    {
        testNativeEditorOwnership();
        testNativeShapeEditor();
        testNativeLfoDetails();
        std::printf ("%s: %d failed\n", uit::g_fails ? "FAIL" : "PASS", uit::g_fails);
        return uit::g_fails ? 1 : 0;
    }
    if (argc == 2 && std::strcmp (argv[1], "--test-play-env-lfo") == 0)
    {
        testNativePlayEnvLfo();
        return uit::g_fails ? 1 : 0;
    }
    if (argc == 2 && std::strcmp (argv[1], "--play-env-lfo-skin-contract") == 0)
    {
        nativePlayEnvLfoContract();
        return 0;
    }
    if (argc == 2 && std::strcmp (argv[1], "--shape-detail-skin-contract") == 0)
    {
        nativeLfoDetailContract();
        return 0;
    }
    if (argc == 2 && std::strcmp (argv[1], "--shape-editor-skin-contract") == 0)
    {
        nativeShapeEditorContract();
        return 0;
    }
    if (argc == 2 && std::strcmp (argv[1], "--skin-contract") == 0)
    {
        nativeContract();
        return 0;
    }
    if (const char* dir = std::getenv ("LFO_IMAGES"))
        mkdir (dir, 0755);
    testLfoParameterCoverage();
    testBindings();
    testCompactQGroup();
    testWrites();
    ui::QGroup::setWideDevice (true);
    {
        Rig xl;
        xl.openFull();
        const int time = paramIdx ("lfo1.smooth_time"), enable = paramIdx ("lfo1.smooth");
        check (xl.inst.q[3] == time && xl.inst.q[4] == enable && xl.inst.q[5] == paramIdx ("lfo1.sync") && xl.inst.q[12] == paramIdx ("lfo1.mono"),
               "XL smoothing dial and switch are Q4/Q5, timing and motion stay contiguous");
        auto& full = xl.ui.full();
        uit::Scene drawn (1280, 800);
        full.groups[1].drawWide (drawn.builder());
        full.groups[2].drawWide (drawn.builder());
        check (drawn.numOps() == 0, "XL keeps Q4/Q5 and the timing routes without drawing numbered markers");
        xl.key ("lfo1.sync", 4.0f);
        check (xl.inst.q[3] == time && xl.inst.q[4] == enable && xl.inst.q[5] == paramIdx ("lfo1.keytrack_transpose")
               && xl.inst.q[14] == paramIdx ("lfo1.mono"), "XL KEY includes both smoothing and key controls alongside timing and motion");
    }
    ui::QGroup::setWideDevice (false);
    testFullScreen();
    testLfoTab();
    testStandaloneLfoSelectorQ();
    testStandaloneLfoPage();
    testStandaloneLfoNativeSmooth();
    testStandaloneNativeLfoControls();
    testNativeEditorOwnership();
    testNativeShapeEditor();
    testNativeLfoDetails();
    testNativePlayEnvLfo();
    testExpandPixelsWithLiveData();
    testPointModSelectionGuards();
    testStandaloneLfoNativeGround();
    testPlayCarouselIsTheTabSelector();
    testPlayRandomRiseRoute();
    testEditor();
    testEditorPresetContentReload();
    testEditorMarker();
    testEditorQuickDeleteAndSelection();
    testShapeLibrary();
    testEditorShapesList();
    testEditorActions();
    testEditorPointModulation();
    testEditorPointModulationStored();
    testPointModulationQKnobs();
    testTargets();
    testDisplay();
    testRandomTraceSettles();
    testKeyFollowCurve();
    testTouchAndDraw();
    testSpikeDecimation();
    testReducedEndpoints();
    testEditorSpikeInColumn();
    std::printf ("%s: %d failed\n", uit::g_fails ? "FAIL" : "PASS", uit::g_fails);
    return uit::g_fails ? 1 : 0;
}
