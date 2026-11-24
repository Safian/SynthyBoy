// SynthyBoy - MPC overlay tests: the FILTER UI (filter_ui.h), the page of the ONE Filter tab with its four states F1 | F2 | NOTE | VELO.
// Wiring of every control to the engine parameters (both directions), the state selector and its chunk-only state, the F1+F2 view toggle (view only), hidden-not-dimmed controls by model, the read-only OUT indicators,
// pages of the UiStack, Q groups (one block each), interactive response curve and Formant XY pad dragging with its values, model / style lists, modulations, the NOTE / VELO editors embedded (writes, TARGETS page,
// Q groups, virtual targets), touch targets >= 56 px, a value under every knob, and drawing inside the scene ops budget.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "demo_common.h"
#include "../../filter_curve_edits.h"
#include "../filter_ui.h"
#include "../pages.h"
#include "../scene_cpu.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
int g_fails = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}

struct FakeInst : demo::Inst
{
    nativeui::FilterCellBank nativeFilter;
    FilterCurveEdits pzEdits;
    int pzPosts = 0;
    void setFilterCurve (int filter, const std::vector<synthyboy::ContentFilterCurvePoint>& points, uint64_t generation) override
    {
        ++pzPosts;
        pzEdits.post (filter, points, generation);
    }
    void setNativeFilterControls (int view, const nativeui::FxCell* cells, const float*, const int*) noexcept override
    {
        nativeFilter.publish (view, cells);
    }
    bool takeNativeFilterSlot (int slot, const nativeui::FxCell& binding, float& value) noexcept override
    {
        return nativeFilter.take (slot, binding, value);
    }
    void nativeWrite (int slot, float value)
    {
        const auto binding = nativeFilter.at (slot);
        nativeFilter.report (binding);
        nativeFilter.post (binding, value);
    }
    void action (int action)
    {
        nativeWrite (nativeui::FilterAction, static_cast<float> (action) / nativeui::FilterLastAction);
    }
};

TouchEvent ev (TouchEvent::Kind k, int x, int y)
{
    TouchEvent e;
    e.kind = k;
    e.x = x;
    e.y = y;
    return e;
}

struct Rig
{
    FilterUi ui;
    FakeInst inst;
    UiStack stack;
    double now = 1000.0;

    float targets[4] = { 12000.0f, 20.0f, 12000.0f, 20.0f };   // the live cutoff and resonance of both filters
    bool useTargets = false;
    int instance = 1;

    /** `inst1` 2: the page starts in the state F2 (the chunk-only view state of the instance), 3 NOTE, 4 VELO. */
    Rig (int inst1 = 1) : instance (inst1)
    {
        inst.set ("filter1.on", 1.0f);
        inst.set ("filter2.on", 1.0f);
        inst.tabViewV[IInstanceView::kTabViewFilter] = inst1 - 1;
        stack.openFull (FilterUi::kElementFilter);
        refresh();
    }

    void refresh()
    {
        now += 50.0;
        page::Input in;
        in.instance = instance;
        in.targets = useTargets ? targets : nullptr;
        in.live = inst.haveLive ? &inst.liveValues : nullptr;
        ui.update (inst, stack, now, in);
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

    void tapC (const ui::Component& c, int holdMs = 40)
    {
        const auto& f = ui.full();
        const auto& ed = ui.curveUi().screen;
        int action = &c == &ui.pzEditorButton() ? nativeui::FilterEditPz
                   : &c == &f.both ? nativeui::FilterBoth
                   : &c == &f.expand ? nativeui::FilterExpandFilter
                   : &c == &ed.expandBtn ? nativeui::FilterExpandCurve
                   : &c == &ed.init ? nativeui::FilterInit
                   : &c == &ed.undoButton ? nativeui::FilterUndo
                   : &c == &ed.del ? nativeui::FilterDelete
                   : &c == &ed.targets ? nativeui::FilterTargets : -1;
        if (action >= 0)
        {
            refresh(); inst.action (action); refresh(); refresh(); return;
        }
        int slot = &c == &f.power ? nativeui::FilterPower
                 : &c == &f.env1 ? nativeui::FilterEnv1
                 : &c == &f.clean ? nativeui::FilterClean
                 : &c == &ed.legato ? nativeui::FilterLegato
                 : &c == &ed.snap ? nativeui::FilterSnap : -1;
        if (slot >= 0)
        {
            const bool on = &c == &f.power ? f.power.state.on : &c == &f.env1 ? f.env1.state.on
                         : &c == &f.clean ? f.clean.state.on : &c == &ed.legato ? ed.legato.state.on : ed.snap.state.on;
            refresh(); inst.nativeWrite (slot, on ? 0.0f : 1.0f); refresh(); return;
        }
        tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs);
    }

    void drag (int x0, int y0, int x1, int y1, int steps = 10)
    {
        ui.touch (ev (TouchEvent::Down, x0, y0), stack, inst, now);
        for (int i = 1; i <= steps; ++i)
        {
            now += 16.0;
            ui.touch (ev (TouchEvent::Move, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps), stack, inst, now);
        }
        ui.touch (ev (TouchEvent::Up, x1, y1), stack, inst, now);
        now += 100.0;
        refresh();
    }

    void centre (const ui::Component& c, int& x, int& y)
    {
        x = c.rect().x + c.rect().w / 2;
        y = c.rect().y + c.rect().h / 2;
    }

    void turnQ (int slot, double delta)
    {
        ui.turnQ (slot, delta, stack, inst);
        refresh();
    }
};

void testEveryControlHasAParameter()
{
    for (int f = 1; f <= 2; ++f)
    {
        const std::string pfx = "filter" + std::to_string (f) + ".";
        const char* const keys[] = {
            "on", "model", "style", "cutoff", "resonance", "drive", "mix",
            "keytrack", "env1", "clean_mode", "var", "comb_offset",
            "blend", "level", "pan", "destination", "bus1_send", "bus2_send",
            "formant.x", "formant.y", "formant.transpose", "formant.resonance", "formant.spread"
        };
        for (const char* k : keys)
        {
            const std::string fullKey = pfx + k;
            check (FakeInst::idx (fullKey) >= 0, fullKey + " exists in parameter table");
        }
    }
    check (FakeInst::idx ("filter.routing") >= 0, "filter.routing exists in parameter table");
}

/** Is the component shown (not hidden) by the screen? */
// Native visibility comes from the published geometry, not a GPU child flag.
bool shown (Rig& r, const ui::Component& c)
{
    const auto& f = r.ui.full();
    const auto controls = f.nativeControls();
    for (int slot = 0; slot < nativeui::FilterPoint; ++slot)
        if (controls[slot] == &c) return f.hasNativeGeometry (slot);
    if (&c == &f.view) return ! f.view.rect().empty();
    if (&c == &f.both) return ! r.ui.isSplit() && ! r.ui.curveView();
    if (&c == &f.scalarView) return r.ui.isSplit();
    if (&c == &f.outReadout) return ! f.nativeOutReadoutRect().empty();
    return &c == &f.display && ! r.ui.curveView();
}
bool gpuShown (Rig& r, const ui::Component& c)
{
    const auto& f = r.ui.full();
    for (int i = 0; i < f.numChildren(); ++i)
        if (f.child (i) == &c) return f.childShown (i);
    return false;
}
/** The tap on the cell `state` (0 F1, 1 F2, 2 NOTE, 3 VELO) of the appropriate selector. */
void tapState (Rig& r, int state)
{
    r.refresh();
    r.inst.action (nativeui::FilterF1 + state);
    r.refresh();
    r.refresh();
}

void testStatesSwitching()
{
    Rig r;
    check (r.ui.isSplit() && r.ui.filterSelection() == 0 && r.ui.scalarSelection() == 0
           && r.ui.editFilter() == 1 && r.ui.full().view.state.selected == 1 && r.ui.full().scalarView.state.selected == 1,
           "initial state is 60/40 Split (F1 left + NOTE right)");
    check (r.ui.full().view.numCells() == 2 && std::string (r.ui.full().view.labelOf (0)) == "F1" && std::string (r.ui.full().view.labelOf (1)) == "F2"
           && r.ui.full().scalarView.numCells() == 2 && std::string (r.ui.full().scalarView.labelOf (0)) == "NOTE" && std::string (r.ui.full().scalarView.labelOf (1)) == "VELO",
           "Split selectors show independent F1 | F2 and NOTE | VELO");

    // Legacy 0..3 and expanded 4..11 chunk reload restores independent F1/F2, NOTE/VELO, and expand mode
    {
        Rig f2 (2);
        check (f2.ui.isSplit() && f2.ui.filterSelection() == 1 && f2.ui.scalarSelection() == 0 && f2.ui.editFilter() == 2,
               "reloaded legacy state 1: F2 + NOTE Split");
        Rig note (3), velo (4);
        check (note.ui.view() == 2 && note.ui.curveView() && note.ui.scalarSelection() == 0
               && velo.ui.view() == 3 && velo.ui.curveView() && velo.ui.scalarSelection() == 1,
               "reloaded legacy state 2/3: Expanded NOTE / VELO");
    }

    const int writesBefore = r.inst.writes;
    tapState (r, 1);   // F2
    check (r.ui.isSplit() && r.ui.filterSelection() == 1 && r.ui.scalarSelection() == 0 && r.ui.editFilter() == 2 && r.inst.tabViewV[IInstanceView::kTabViewFilter] == 1,
           "tapping F2 keeps NOTE selected on right and writes tabView 1");
    tapState (r, 3);   // VELO on right panel
    check (r.ui.isSplit() && r.ui.filterSelection() == 1 && r.ui.scalarSelection() == 1 && r.inst.tabViewV[IInstanceView::kTabViewFilter] == 5,
           "tapping VELO keeps F2 selected on left and writes tabView 5");
    r.tapC (r.ui.full().expand);
    check (! r.ui.isSplit() && ! r.ui.curveView() && r.ui.filterSelection() == 1 && r.ui.scalarSelection() == 1
           && r.inst.tabViewV[IInstanceView::kTabViewFilter] == 9 && r.ui.full().view.numCells() == 4,
           "tapping expand opens Expanded Filter preserving F2 and VELO");
    tapState (r, 2);   // NOTE in Expanded View -> Expanded NOTE
    check (r.ui.curveView() && r.ui.view() == 2 && r.ui.scalarSelection() == 0 && r.ui.filterSelection() == 1,
           "expanded NOTE preserves F2 selection");
    tapState (r, 3);   // VELO in Expanded View -> Expanded VELO
    check (r.ui.curveView() && r.ui.view() == 3 && r.ui.scalarSelection() == 1, "expanded VELO sets scalarSelection 1");
    r.tapC (r.ui.full().expand);   // Collapse from Expanded VELO back to Split
    check (r.ui.isSplit() && r.ui.filterSelection() == 1 && r.ui.scalarSelection() == 1,
           "collapsing from Expanded VELO returns to Split keeping F2 + VELO");
    tapState (r, 0);
    tapState (r, 2);
    check (r.ui.filterSelection() == 0 && r.ui.scalarSelection() == 0 && r.ui.editFilter() == 1 && ! r.ui.curveView(),
           "tapping F1 and NOTE returns to F1 + NOTE Split");
    check (r.inst.writes == writesBefore, "the state is the plugin's view state: no engine or host parameter was written");

    // the dots of the cells: F1 and F2 are on, NOTE / VELO are routed
    r.inst.set ("filter2.on", 0.0f);
    r.inst.set ("filter1.on", 1.0f);
    r.inst.mod (3, 24, "filter1.cutoff", 25.0f);
    r.refresh(); r.refresh(); r.refresh();
    const auto& v = r.ui.full().view.state;
    const auto& sv = r.ui.full().scalarView.state;
    check (v.items[0].used && ! v.items[1].used && sv.items[0].used && ! sv.items[1].used,
           "the dots: F1 is on, F2 off, NOTE has a slot, VELO none");
}

void testPageIsAlwaysOpen()
{
    Rig r;
    check (! r.inst.fullOpen && r.ui.hasNativePower(), "native root keeps host widgets available without a GPU base-control mode");
    check (r.ui.full().rect() == ui::Rect {0, 110, 1280, 628}, "native Filter geometry fills the content area");
}

void testBothIsAViewOnly()
{
    Rig r;
    r.tapC (r.ui.full().expand);
    check (shown (r, r.ui.full().both) && ! r.ui.isDual() && ! r.ui.full().display.state.dual, "F1+F2 is off at the start");
    const int writes0 = r.inst.writes, sel0 = r.inst.selectorWrites;
    r.tapC (r.ui.full().both);
    check (r.ui.isDual() && r.ui.full().display.state.dual && r.ui.full().both.state.on, "a tap on F1+F2: the display is dual");
    check (r.inst.writes == writes0 && r.inst.selectorWrites == sel0, "nothing is written, not even the view state (a view only)");
    check (r.ui.view() == 0 && r.ui.editFilter() == 1, "the controls still edit the selected filter (F1)");
    int x, y;
    r.inst.set ("filter1.cutoff", 1000.0f);
    r.refresh();
    r.centre (r.ui.full().cutoff, x, y);
    r.inst.nativeWrite (nativeui::FilterCutoff, 0.8f); r.refresh();
    check (r.inst.get ("filter1.cutoff") > 1500.0f && r.inst.get ("filter2.cutoff") == 12000.0f, "the CUTOFF knob edits F1 only while both curves are shown");
    // with F2 selected the same knob edits F2
    tapState (r, 1);
    check (r.ui.isDual() && r.ui.editFilter() == 2, "F2 selected, the toggle stays on");
    r.inst.set ("filter2.cutoff", 1000.0f);
    r.refresh();
    r.centre (r.ui.full().cutoff, x, y);
    r.inst.nativeWrite (nativeui::FilterCutoff, 0.8f); r.refresh();
    check (r.inst.get ("filter2.cutoff") > 1500.0f, "with F2 selected the knob edits F2");
    r.tapC (r.ui.full().both);
    check (! r.ui.isDual() && ! r.ui.full().display.state.dual, "a second tap turns the view off");
    // hidden in NOTE / VELO
    r.tapC (r.ui.full().both);
    tapState (r, 2);
    check (! shown (r, r.ui.full().both) && ! r.ui.isDual(), "F1+F2 is hidden in NOTE and not dual there");
    tapState (r, 0);
    check (shown (r, r.ui.full().both) && r.ui.isDual(), "back in F1 the toggle is shown and still on");
}





/** A switched-off filter keeps POWER and page navigation available, but blocks
    display/knob/Q-Link writes until it is enabled again. */
void testDisabledFilterIsReadOnly()
{
    Rig r;
    r.inst.set ("filter1.on", 0.0f);
    r.inst.set ("filter2.on", 0.0f);
    r.inst.set ("filter1.cutoff", 1000.0f);
    r.refresh();
    const int w0 = r.inst.writes;
    check (std::all_of (std::begin (r.inst.q), std::begin (r.inst.q) + 12, [] (int id) { return id == -1; }),
           "both filters OFF: Filter Q-Link banks MAIN/TRACK/OUT publish empty targets while right EDIT stays independent");
    check (! r.ui.full().power.inactive() && r.ui.full().power.enabled(), "filter OFF: POWER remains reachable");
    check (! r.ui.full().view.inactive() && r.ui.full().view.enabled(), "filter OFF: navigation selector remains reachable");
    check (! r.ui.full().routing.enabled() && r.ui.full().routing.inactive(), "both filters OFF: shared ROUTING is passive");
    check (! r.ui.full().cutoff.enabled() && ! r.ui.full().display.enabled(), "filter OFF: cutoff and response display are disabled");
    r.turnQ (0, 0.2);
    r.drag (r.ui.full().display.rect().x + 100, r.ui.full().display.rect().y + 100,
            r.ui.full().display.rect().x + 300, r.ui.full().display.rect().y + 40);
    r.inst.nativeWrite (nativeui::FilterRouting, 0.5f); r.refresh();
    check (r.inst.writes == w0 && r.inst.get ("filter1.cutoff") == 1000.0f && r.inst.get ("filter.routing") == 0.0f,
           "both OFF: display/Q-Link/ROUTING touch produce zero parameter writes");
    // If F2 is ON while F1 is OFF, shared ROUTING becomes active & writable!
    r.inst.set ("filter2.on", 1.0f);
    r.refresh();
    check (r.ui.full().routing.enabled() && ! r.ui.full().routing.inactive() && ! r.ui.full().cutoff.enabled(),
           "F1 OFF / F2 ON: F1 cutoff disabled, shared ROUTING active");
    r.inst.nativeWrite (nativeui::FilterRouting, 0.5f); r.refresh();
    check (r.inst.get ("filter.routing") == 1.0f, "F1 OFF / F2 ON: tapping shared ROUTING writes filter.routing");
    r.inst.set ("filter.routing", 0.0f);
    r.tapC (r.ui.full().power);
    check (r.inst.get ("filter1.on") > 0.5f && r.ui.full().cutoff.enabled()
           && std::any_of (std::begin (r.inst.q), std::begin (r.inst.q) + 12, [] (int id) { return id >= 0; }),
           "filter ON: POWER restores editing and Q-Link targets");
}

bool powerApertureIsClear (Rig& r)
{
    const auto rect = r.ui.nativePowerRect();
    if (rect.w <= 0 || rect.h <= 0 || rect.x < 0 || rect.y < 0
        || rect.right() > page::kW || rect.bottom() > page::kH)
        return false;
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder builder (*scene);
    builder.begin (gpu::kKindPlay, 0, page::kW, page::kH);
    r.ui.draw (builder);
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    for (int y = rect.y; y < rect.bottom(); ++y)
        for (int x = rect.x; x < rect.right(); ++x)
            if (pixels[static_cast<size_t> (y) * page::kW + x] != 0u)
                return false;
    return true;
}

void testStandaloneFilterPowerAndOffNavigation()
{
    // Filter 1 standalone
    Rig r1;
    r1.ui.setStandaloneFilter (1);
    r1.inst.set ("filter1.on", 1.0f);
    r1.inst.set ("filter2.on", 1.0f);
    r1.refresh();
    check (r1.ui.editFilter() == 1, "standalone filter 1: editFilter is 1");
    check (r1.ui.full().power.state.on, "standalone filter 1: power is ON");
    check (r1.ui.hasNativePower(), "standalone filter 1: native power active");

    const ui::Rect p1 = r1.ui.nativePowerRect();
    check (p1.w >= 56 && p1.h >= 56, "standalone filter 1: power button >= 56 px");

    // Exclusive native touch ownership: touch and wheel are rejected by overlay and passed to host
    check (! r1.tap (p1.x + p1.w / 2, p1.y + p1.h / 2), "standalone filter 1: host has exclusive native touch ownership of power switch");
    check (! r1.ui.wheel (p1.x + p1.w / 2, p1.y + p1.h / 2, 1, r1.stack, r1.inst, r1.now), "standalone filter 1: host has exclusive native wheel ownership of power switch");

    check (powerApertureIsClear (r1), "standalone filter 1 ON: power switch interior is transparent host aperture");

    // Host parameter write turns Filter 1 OFF and leaves Filter 2 ON
    r1.inst.set ("filter1.on", 0.0f);
    r1.refresh();
    check (! r1.ui.full().power.state.on, "standalone filter 1: power turns OFF");
    check (r1.inst.get ("filter2.on") == 1.0f, "standalone filter 1: filter 2 power is isolated and untouched");

    // In OFF state, exclusive native touch ownership is retained!
    check (! r1.tap (p1.x + p1.w / 2, p1.y + p1.h / 2), "standalone filter 1 OFF: host retains exclusive native touch ownership");
    check (! r1.ui.wheel (p1.x + p1.w / 2, p1.y + p1.h / 2, 1, r1.stack, r1.inst, r1.now), "standalone filter 1 OFF: host retains exclusive native wheel ownership");

    check (powerApertureIsClear (r1), "standalone filter 1 OFF: power switch interior is transparent host aperture");

    // OFF navigation invariants: view and close remain reachable and operable
    check (! r1.ui.full().view.inactive() && r1.ui.full().view.enabled(), "standalone filter 1 OFF: navigation remains operable");
    check (! r1.ui.full().cutoff.enabled() && ! r1.ui.full().display.enabled(), "standalone filter 1 OFF: DSP controls are disabled");

    // Filter 2 standalone
    Rig r2;
    r2.ui.setStandaloneFilter (2);
    r2.inst.set ("filter1.on", 1.0f);
    r2.inst.set ("filter2.on", 1.0f);
    r2.refresh();
    check (r2.ui.editFilter() == 2, "standalone filter 2: editFilter is 2");
    check (r2.ui.full().power.state.on, "standalone filter 2: power is ON");
    check (r2.ui.hasNativePower(), "standalone filter 2: native power active");

    const ui::Rect p2 = r2.ui.nativePowerRect();
    check (p2.w >= 56 && p2.h >= 56, "standalone filter 2: power button >= 56 px");

    // Exclusive native touch ownership on Filter 2
    check (! r2.tap (p2.x + p2.w / 2, p2.y + p2.h / 2), "standalone filter 2: host has exclusive native touch ownership of power switch");
    check (! r2.ui.wheel (p2.x + p2.w / 2, p2.y + p2.h / 2, 1, r2.stack, r2.inst, r2.now), "standalone filter 2: host has exclusive native wheel ownership of power switch");

    check (powerApertureIsClear (r2), "standalone filter 2 ON: power switch interior is transparent host aperture");

    // Host parameter write turns Filter 2 OFF and leaves Filter 1 ON
    r2.inst.set ("filter2.on", 0.0f);
    r2.refresh();
    check (! r2.ui.full().power.state.on, "standalone filter 2: power turns OFF");
    check (r2.inst.get ("filter1.on") == 1.0f, "standalone filter 2: filter 1 power is isolated and untouched");

    // OFF state retains exclusive native touch ownership on Filter 2
    check (! r2.tap (p2.x + p2.w / 2, p2.y + p2.h / 2), "standalone filter 2 OFF: host retains exclusive native touch ownership");
    check (! r2.ui.wheel (p2.x + p2.w / 2, p2.y + p2.h / 2, 1, r2.stack, r2.inst, r2.now), "standalone filter 2 OFF: host retains exclusive native wheel ownership");

    check (powerApertureIsClear (r2), "standalone filter 2 OFF: power switch interior is transparent host aperture");

    // OFF navigation invariants on Filter 2
    check (! r2.ui.full().view.inactive() && r2.ui.full().view.enabled(), "standalone filter 2 OFF: navigation remains operable");
    check (! r2.ui.full().cutoff.enabled() && ! r2.ui.full().display.enabled(), "standalone filter 2 OFF: DSP controls are disabled");

    // Host parameter write restores Filter 2 ON
    r2.inst.set ("filter2.on", 1.0f);
    r2.refresh();
    check (r2.inst.get ("filter2.on") == 1.0f && r2.ui.full().power.state.on, "standalone filter 2: power restored ON");
    check (r2.ui.full().cutoff.enabled() && r2.ui.full().display.enabled(), "standalone filter 2: DSP controls restored");
}



void testDisplayInteractiveDrag()
{
    Rig r;
    r.inst.set ("filter1.cutoff", 500.0f);
    r.inst.set ("filter1.resonance", 10.0f);
    r.refresh();

    // Drag on FilterDisplay in compact view
    const ui::Rect dRect = r.ui.full().display.rect();
    // Drag towards top right (higher cutoff and higher resonance)
    r.drag (dRect.x + dRect.w / 4, dRect.y + dRect.h * 3 / 4, dRect.x + dRect.w * 3 / 4, dRect.y + dRect.h / 4);

    check (r.inst.get ("filter1.cutoff") > 2000.0f, "dragging filter display moves cutoff up (" + std::to_string (r.inst.get ("filter1.cutoff")) + " Hz)");
    check (r.inst.get ("filter1.resonance") > 40.0f, "dragging filter display moves resonance up (" + std::to_string (r.inst.get ("filter1.resonance")) + " %)");
}

void testFormantModeSwitchAndDrag()
{
    Rig r;
    check (! r.ui.full().formantMode(), "initial mode is not Formant");

    // Switch model to Formant (model 5)
    r.inst.set ("filter1.model", 5.0f);
    r.refresh();
    check (r.ui.full().formantMode(), "switching filter1.model to 5 enables Formant mode");
    check (r.ui.full().display.state.isFormant, "display switches to Formant XY pad");

    // Drag on Formant XY pad
    r.inst.set ("filter1.formant.x", 20.0f);
    r.inst.set ("filter1.formant.y", 20.0f);
    r.refresh();

    const ui::Rect dRect = r.ui.full().display.rect();
    r.drag (dRect.x + dRect.w / 5, dRect.bottom() - dRect.h / 5, dRect.x + dRect.w * 4 / 5, dRect.y + dRect.h / 5);

    check (r.inst.get ("filter1.formant.x") > 60.0f, "dragging Formant XY pad updates formant.x (" + std::to_string (r.inst.get ("filter1.formant.x")) + " %)");
    check (r.inst.get ("filter1.formant.y") > 60.0f, "dragging Formant XY pad updates formant.y (" + std::to_string (r.inst.get ("filter1.formant.y")) + " %)");
}



/** LEVEL, PAN, DEST, BUS 1, BUS 2 are read-only indicators (the Mixer is their home); BLEND and ROUTING stay editable; there is no OUTPUT popup, no list button, no OUT button. */


void testQGroupsAndTurn()
{
    Rig r;
    check (std::string (r.ui.qGroupName()) == "MAIN", "the active Q group is MAIN");
    check (r.inst.q[0] == FakeInst::idx ("filter1.cutoff")
           && r.inst.q[1] == FakeInst::idx ("filter1.resonance")
           && r.inst.q[2] == FakeInst::idx ("filter1.drive")
           && r.inst.q[3] == FakeInst::idx ("filter1.mix"),
           "MAIN Q targets are CUTOFF, RESO, DRIVE, MIX");

    // Touch a control in the OUT group (the blend knob): the group is [BLEND, ROUTING, MODEL, STYLE]
    int bx, by;
    r.centre (r.ui.full().blend, bx, by);
    r.tap (bx, by);
    check (std::string (r.ui.qGroupName()) == "OUT", "touching blend knob switches active QGroup to OUT");
    check (r.inst.q[0] == FakeInst::idx ("filter1.blend")
           && r.inst.q[1] == FakeInst::idx ("filter.routing")
           && r.inst.q[2] == FakeInst::idx ("filter1.model")
           && r.inst.q[3] == FakeInst::idx ("filter1.style"),
           "OUT QGroup targets are BLEND, ROUTING, MODEL, STYLE");
    check (r.ui.full().groups[2].frameless() && r.ui.full().groups[2].member (2) == &r.ui.full().model && r.ui.full().groups[2].member (3) == &r.ui.full().style, "the OUT group is frameless: MODEL and STYLE stand in the top bar");

    // a touch on the model stepper (a member of OUT) keeps OUT; a touch elsewhere on the page does not change the group
    r.centre (r.ui.full().cutoff, bx, by);
    r.tap (bx, by);
    check (std::string (r.ui.qGroupName()) == "MAIN", "touching cutoff switches back to MAIN");
    const ui::Rect mr = r.ui.full().model.rect();
    r.tap (mr.right() - 10, mr.y + mr.h / 2);   // the right chevron: a step, and the group becomes OUT
    check (std::string (r.ui.qGroupName()) == "OUT", "touching the model stepper makes OUT the Q group");

    // turnQ routes to the active group member
    const float blendBefore = r.inst.get ("filter1.blend");
    r.turnQ (0, 0.05);
    check (r.inst.get ("filter1.blend") != blendBefore, "turnQ on slot 0 routes to BLEND");

    // Touch KEYTRACK in TRACK group
    r.centre (r.ui.full().keytrack, bx, by);
    r.tap (bx, by);
    check (std::string (r.ui.qGroupName()) == "TRACK", "touching keytrack switches active QGroup to TRACK");
    check (r.inst.q[0] == FakeInst::idx ("filter1.keytrack")
           && r.inst.q[1] == FakeInst::idx ("filter1.env1")
           && r.inst.q[2] == FakeInst::idx ("filter1.var")
           && r.inst.q[3] == -1,
           "TRACK QGroup targets are KEYTRACK, ENV1, VAR (COMB only for the Comb model)");

    // the Comb model: COMB is the fourth member; the Formant model: the formant RESONANCE takes the place of VAR (the FORMANT block keeps X, Y, TRANSPOSE, SPREAD)
    r.inst.set ("filter1.model", 6.0f);
    r.refresh();
    check (r.inst.q[3] == FakeInst::idx ("filter1.comb_offset"), "Comb: the fourth TRACK member is COMB");
    r.inst.set ("filter1.model", 5.0f);
    r.refresh();
    check (r.inst.q[0] == FakeInst::idx ("filter1.keytrack") && r.inst.q[2] == FakeInst::idx ("filter1.formant.resonance") && r.inst.q[3] == -1, "Formant: TRACK is KEYTRACK, ENV 1, formant RESONANCE");
    r.tapC (r.ui.full().expand);   // Expanded Filter holds the dedicated 4-knob FORMANT block
    r.centre (r.ui.full().formantX, bx, by);
    r.tap (bx, by);
    check (std::string (r.ui.qGroupName()) == "FORMANT" && r.inst.q[0] == FakeInst::idx ("filter1.formant.x") && r.inst.q[1] == FakeInst::idx ("filter1.formant.y")
           && r.inst.q[2] == FakeInst::idx ("filter1.formant.transpose") && r.inst.q[3] == FakeInst::idx ("filter1.formant.spread"), "FORMANT group is X, Y, TRANSPOSE, SPREAD");

    // the targets follow the selected filter
    tapState (r, 1);
    r.centre (r.ui.full().cutoff, bx, by);
    r.tap (bx, by);
    check (r.inst.q[0] == FakeInst::idx ("filter2.cutoff") && r.inst.q[1] == FakeInst::idx ("filter2.resonance"), "in the state F2 the Q targets are the parameters of filter 2");
}

void testModulationsPage()
{
    Rig r;
    // Set a matrix modulation slot targeting filter1.cutoff
    const int cutIdx = FakeInst::idx ("filter1.cutoff");
    r.inst.set ("mod04.source", 8.0f); // LFO 2
    r.inst.set ("mod04.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (cutIdx))));
    r.inst.set ("mod04.amount", 40.0f);
    r.inst.set ("mod04.bipolar", 1.0f);
    r.inst.set ("mod04.bypass", 0.0f);
    r.refresh();

    // Long press on CUTOFF knob opens modulations page
    int x, y;
    r.centre (r.ui.full().cutoff, x, y);
    r.tap (x, y, 700); // 700 ms hold (> 600 ms long press)

    check (r.ui.topPage() == FilterUi::kPageMods, "long press on CUTOFF opens ModulationsPage");
    check (r.ui.modsPage().numRows() == 1, "ModulationsPage lists the 1 active modulation");

    // Close modulations page
    r.tapC (r.ui.modsPage().back);
    check (r.ui.topPage() == 0, "tapping BACK closes ModulationsPage");
}

/** A control that nothing modulates: its modulations page is the BOARD of every source (the same board as the empty modulation matrix), a tap on a chip makes the modulation of THIS control from that source; no stepper, no add row. */
void testModulationsBoard()
{
    Rig r;
    r.refresh();
    int x, y;
    r.centre (r.ui.full().cutoff, x, y);
    r.tap (x, y, 700);
    ui::ModulationsPage& mp = r.ui.modsPage();
    check (r.ui.topPage() == FilterUi::kPageMods && mp.numRows() == 0 && mp.boardShown(), "a long press on CUTOFF with no modulation opens the board of the sources");
    check (mp.board.numSources() == static_cast<int> (synthyboy::modSourceCount()), "the board has every source of the engine (" + std::to_string (mp.board.numSources()) + ")");
    check (mp.board.numGroups() == uibind::kSourceGroups, "in the groups envelopes, LFOs, macros, player, sound");
    bool big = true, inside = true, noRowAdd = true;
    for (int i = 0; i < mp.board.numSources(); ++i)
    {
        const ui::Rect c = mp.board.chipRect (i);
        big = big && c.h >= 56 && c.w >= 64;
        inside = inside && mp.rect().contains (c);
    }
    for (int c = 0; c < mp.numChildren(); ++c)
        if ((mp.child (c) == &mp.addSource || mp.child (c) == &mp.add) && mp.childShown (c))
            noRowAdd = false;
    check (big && inside && mp.board.fits(), "every chip is >= 56 x 64 px and inside the page, the board fits without squeezing the chips");
    check (noRowAdd, "the stepper and the ADD button of the compact row are not on the page");
    const int lfo1 = uibind::lfoSource (1);
    const ui::Rect chip = mp.board.chipRectOfId (lfo1);
    check (! chip.empty(), "the board has the LFO 1 chip");
    r.tap (chip.x + chip.w / 2, chip.y + chip.h / 2);
    int slot = -1;
    for (int s = 0; s < synthyboy::kNumModSlots && slot < 0; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (std::lround (r.inst.get (synthyboy::paramTable()[sp.source].key)) == lfo1)
            slot = s;
    }
    check (slot >= 0 && r.ui.topPage() == FilterUi::kPageMods, "a tap on the chip makes a modulation from LFO 1 and the page stays open");
    r.refresh();
    r.refresh();
    check (mp.numRows() == 1 && ! mp.boardShown(), "with the row present the board is gone: the row and the compact add row");
    bool addRow = false;
    for (int c = 0; c < mp.numChildren(); ++c)
        addRow = addRow || ((mp.child (c) == &mp.add) && mp.childShown (c));
    check (addRow, "the compact add row is back under the row");
    r.tapC (mp.rows[0].remove);   // X: the modulation is removed, the board is the page again
    r.refresh();
    r.refresh();
    check (mp.numRows() == 0 && mp.boardShown(), "removing the only row brings the board back");
}

void testTouchTargetsAndLayoutSizes()
{
    Rig r;
    r.tapC (r.ui.full().expand);   // Full Expanded Filter layout
    ui::FilterFullScreen& f = r.ui.full();
    check (f.power.rect().w >= 56 && f.power.rect().h >= 56, "power button >= 56 px");
    check (f.model.rect().w >= 208 && f.model.rect().h >= 56 && f.style.rect().w >= 208 && f.style.rect().h >= 56, "the model and the style steppers >= 208 x 56 px");
    check (f.clean.rect().w >= 72 && f.clean.rect().h >= 56 && f.both.rect().w >= 96 && f.both.rect().h >= 56, "CLEAN and F1+F2 >= 56 px");
    {   // the state selector: four cells of at least 56 px in one row, in the top bar, apart from the ROUTING list of the output block
        bool cells = true;
        for (int i = 0; i < 4; ++i)
            cells = cells && f.view.cell (i, 4).w >= 56 && f.view.cell (i, 4).h >= 56 && f.view.cell (i, 4).y == f.view.rect().y;
        check (f.view.rect().w >= 4 * 56 && cells && f.view.rect().y == f.model.rect().y, "F1 | F2 | NOTE | VELO: four cells >= 56 px in the row of the top bar");
        check (f.routing.state.count == 3 && f.view.rect().y < f.routing.rect().y && ! f.view.rect().overlaps (f.routing.rect()) && f.both.rect().x >= f.view.rect().right() && f.model.rect().x >= f.both.rect().right(), "the selector is in the top bar, apart from the ROUTING list of the output block");
        check (f.model.rect().y >= f.rect().y + ui::kQLane + 0 && f.groups[2].member (2) == &f.model, "the top bar starts one chip lane below the top of the tab (the chips of MODEL and STYLE)");
    }
    ui::Component* leaves[64];
    const int n = f.collectLeaves (leaves, 0, 64);
    int small = 0, leavesOk = 0;
    for (int i = 0; i < n; ++i)
        if (leaves[i]->rect().w > 0 && ! leaves[i]->sizeOk())
        {
            std::printf ("  too small: %d x %d (min %d x %d) id %d\n", leaves[i]->rect().w, leaves[i]->rect().h, leaves[i]->minSize().w, leaves[i]->minSize().h, leaves[i]->id());
            ++small;
        }
        else
            ++leavesOk;
    check (small == 0, "every component of the page has at least its minimum size (" + std::to_string (leavesOk) + " ok, " + std::to_string (small) + " too small)");
    // one family of knobs: every knob of the three blocks has the same size
    const ui::Rect a = f.cutoff.rect();
    bool same = true;
    for (const ui::Knob* k : { &f.resonance, &f.drive, &f.mix, &f.keytrack, &f.var, &f.combOffset, &f.blend })
        same = same && std::abs (k->rect().w - a.w) <= 3 && k->rect().h == a.h;
    check (same, "MAIN, TRACK and OUT knobs are one family of size");
}

void testQFrameIsOneBlock()
{
    Rig r;
    r.tapC (r.ui.full().expand);   // Expanded Filter has FORMANT block too
    ui::FilterFullScreen& f = r.ui.full();
    for (int g = 0; g < 2; ++g)
    {
        const ui::Rect b = f.block (g), u = f.groups[g].rect();
        check (b.contains (u), std::string ("the Q group ") + ui::FilterFullScreen::groupName (g) + " lies inside its block");
        ui::Component* leaves[64];
        const int n = f.collectLeaves (leaves, 0, 64);
        check (f.groups[g].check (leaves, n) == 0, std::string ("no other component lies in the frame of the group ") + ui::FilterFullScreen::groupName (g));
    }
    {   // the OUT group is frameless (MODEL and STYLE stand in the top bar): its members are the BLEND knob, the ROUTING list and the two steppers
        check (f.groups[2].frameless() && f.block (2).contains (f.blend.rect()) && f.block (2).contains (f.routing.rect()) && ! f.block (2).contains (f.model.rect()), "OUT: BLEND and ROUTING in the block, MODEL and STYLE in the top bar, the group frameless");
        check (f.block (2).contains (f.outReadout.rect()), "the read-only indicators lie inside the OUT block");
    }
    r.inst.set ("filter1.model", 5.0f);
    r.refresh();
    const ui::Rect b = f.block (3), u = f.groups[3].rect();
    ui::Component* leaves[64];
    const int n = f.collectLeaves (leaves, 0, 64);
    check (b.w > 300 && b.contains (u) && f.groups[3].check (leaves, n) == 0, "the FORMANT block holds its Q group (X, Y, TRANSPOSE, SPREAD) alone");
    check (f.block (1).contains (f.formantRes.rect()) && f.formantRes.rect().y == f.keytrack.rect().y, "the formant RESONANCE sits in the TRACK block (the place of VAR)");
    check (f.display.rect().right() < f.block (3).x, "the display and the FORMANT block do not overlap");
}







void testDisplayStoresNoTruth()
{
    Rig r;
    r.inst.set ("filter1.cutoff", 500.0f);
    r.inst.set ("filter1.resonance", 10.0f);
    r.refresh();
    ui::FilterDisplay& d = r.ui.full().display;
    const ui::Rect dr = d.rect();
    // a finger down writes both axes at once (X as the cutoff, Y as the resonance)
    r.ui.touch (ev (TouchEvent::Down, dr.x + dr.w * 3 / 4, dr.y + dr.h / 4), r.stack, r.inst, r.now);
    const float cut1 = r.inst.get ("filter1.cutoff"), res1 = r.inst.get ("filter1.resonance");
    check (d.dragging() && cut1 > 2000.0f && res1 > 40.0f, "a finger down on the display writes the cutoff and the resonance (" + std::to_string (cut1) + " Hz, " + std::to_string (res1) + " %)");
    // the finger is still down: an update shows the parameters (the display keeps no copy of the finger)
    r.refresh();
    check (std::fabs (d.state.cutoffHz - cut1) < 0.5 && std::fabs (d.state.resonancePct - res1) < 0.5, "under the finger the display state is what the parameters say");
    // a move to the other side: the parameters follow the finger, and after the next update so does the display
    r.ui.touch (ev (TouchEvent::Move, dr.x + dr.w / 4, dr.y + dr.h * 3 / 4), r.stack, r.inst, r.now + 16.0);
    const float cut2 = r.inst.get ("filter1.cutoff"), res2 = r.inst.get ("filter1.resonance");
    check (cut2 < cut1 * 0.5f && res2 < res1 * 0.5f, "a move writes the new position (" + std::to_string (cut2) + " Hz, " + std::to_string (res2) + " %)");
    r.refresh();
    check (std::fabs (d.state.cutoffHz - cut2) < 0.5 && std::fabs (d.state.resonancePct - res2) < 0.5, "the next update shows the point where the finger wrote it, with the finger still down");
    r.ui.touch (ev (TouchEvent::Up, dr.x + dr.w / 4, dr.y + dr.h * 3 / 4), r.stack, r.inst, r.now + 40.0);
    r.refresh();
    check (! d.dragging() && std::fabs (r.inst.get ("filter1.cutoff") - cut2) < 0.5f, "lifting writes nothing more");

    // the formant pad: X and Y both written, from one finger
    r.inst.set ("filter1.model", 5.0f);
    r.inst.set ("filter1.formant.x", 50.0f);
    r.inst.set ("filter1.formant.y", 50.0f);
    r.refresh();
    const ui::Rect fr = d.rect();   // the formant layout has its own display rectangle (the rectangle of the response curve is not the pad's)
    r.ui.touch (ev (TouchEvent::Down, fr.x + fr.w / 5, fr.bottom() - fr.h / 5), r.stack, r.inst, r.now);
    check (r.inst.get ("filter1.formant.x") < 30.0f && r.inst.get ("filter1.formant.y") < 30.0f, "a finger down on the formant pad writes X and Y (" + std::to_string (r.inst.get ("filter1.formant.x")) + ", " + std::to_string (r.inst.get ("filter1.formant.y")) + ")");
    r.ui.touch (ev (TouchEvent::Up, fr.x + fr.w / 5, fr.bottom() - fr.h / 5), r.stack, r.inst, r.now + 40.0);
}

void testRealCurveAndLiveTargets()
{
    Rig r;
    r.useTargets = true;
    r.targets[0] = 3000.0f;   // the live (modulated) cutoff of F1
    r.targets[1] = 40.0f;
    r.refresh();
    r.refresh();
    check (r.ui.curve (1) != nullptr && r.ui.curve (1)->valid() && r.ui.curve (1)->points() == 256, "the display has the 256 points of the real response");
    check (std::fabs (r.ui.full().display.state.liveCutoffHz - 3000.0) < 1.0, "the live cutoff comes from the readout targets of the view (" + std::to_string (r.ui.full().display.state.liveCutoffHz) + " Hz)");
    // a resonant low pass has its peak near the cutoff and falls above it
    const float* c = r.ui.curve (1)->curve();
    float lo, hi;
    r.ui.curve (1)->minMax (lo, hi);
    check (hi > 0.0f && lo < -20.0f, "the curve is a filter response: a peak (" + std::to_string (hi) + " dB) and a fall (" + std::to_string (lo) + " dB)");
    (void) c;
}

void testDrawingAndSceneOps()
{
    Rig r;
    // 2. Draw the page
    {
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        check (! sb.overflow && data->numOps > 10, "full screen view scene fits ops budget (" + std::to_string (data->numOps) + " ops)");
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (cv.size() == 1280u * 800u, "CPU renderer renders full screen view scene");
        // the response line is a thin crisp line like the curves of the LFO and the FX displays (2 px), not a 4 px band: on the flat part of the curve (column at the left of the cutoff) count the bright pixels
        const ui::Rect dr = r.ui.displayRect();
        const int cx = dr.x + 260;
        int run = 0;
        for (int y = dr.y; y < dr.bottom(); ++y)
        {
            const uint32_t px = cv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (cx)];
            const int cr = static_cast<int> ((px >> 16) & 255u), cg = static_cast<int> ((px >> 8) & 255u), cb = static_cast<int> (px & 255u);
            run += cg > 190 && cb > 190 && cr < 150 ? 1 : 0;
        }
        check (run >= 1 && run <= 3, "the response line of the filter display is 2 px thin (" + std::to_string (run) + " bright pixels in a column of the flat part)");
    }

    // 3. Draw Formant view
    {
        r.inst.set ("filter1.model", 5.0f);
        r.refresh();
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        check (! sb.overflow && data->numOps > 10, "formant full screen scene fits ops budget (" + std::to_string (data->numOps) + " ops)");
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (cv.size() == 1280u * 800u, "CPU renderer renders formant view scene");
    }

    // 4. Draw the NOTE and VELO states
    for (int state = 2; state <= 3; ++state)
    {
        r.inst.set ("filter1.model", 0.0f);
        tapState (r, state);
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        check (! sb.overflow && data->numOps > 10, std::string (state == 2 ? "NOTE" : "VELO") + " state scene fits ops budget (" + std::to_string (data->numOps) + " ops)");
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (cv.size() == 1280u * 800u, "CPU renderer renders the state scene");
    }
}

/** A filter that is switched off is drawn as what switching it on will do, dim and without the fill, with "OFF" in its name: the picture differs from the picture of the same filter when it is on, and the lit curve is brighter. */
void testSwitchedOffFilterIsDim()
{
    auto draw = [] (float on, std::vector<uint32_t>& px)
    {
        Rig r (2);
        r.inst.set ("filter2.on", on);
        r.inst.set ("filter2.model", 2.0f);
        r.inst.set ("filter2.cutoff", 3000.0f);
        r.refresh();
        check (r.ui.full().display.state.filter2On == (on > 0.5f), "the display knows the filter is " + std::string (on > 0.5f ? "on" : "off"));
        check (r.ui.full().display.inactive() == (on <= 0.5f), "the filter display follows the module inactive look");
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        renderScene (*data, px);
    };
    std::vector<uint32_t> onPx, offPx;
    draw (1.0f, onPx);
    draw (0.0f, offPx);
    // the light of the display region (the curve, its fill and the name): less for the filter that is off
    auto peak = [] (const std::vector<uint32_t>& px)
    {
        uint64_t sum = 0;
        for (int y = 180; y < 520; ++y)
            for (int x = 120; x < 1100; ++x)
            {
                const uint32_t c = px[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)];
                sum += ((c >> 16) & 255u) + ((c >> 8) & 255u) + (c & 255u);
            }
        return sum;
    };
    check (onPx != offPx && peak (offPx) < peak (onPx), "the response of a switched-off filter is drawn dimmer than the one of the filter when it is on (" + std::to_string (peak (offPx)) + " against " + std::to_string (peak (onPx)) + ")");
}

void testDualSceneHasThreeCurves()
{
    Rig r;
    r.inst.set ("filter1.on", 1.0f);
    r.inst.set ("filter2.on", 1.0f);
    r.inst.set ("filter1.model", 2.0f);
    r.inst.set ("filter1.cutoff", 2000.0f);
    r.inst.set ("filter2.model", 3.0f);
    r.inst.set ("filter2.cutoff", 6000.0f);
    r.refresh();

    // Tap the expand + F1+F2 view toggle
    r.tapC (r.ui.full().expand);
    r.tapC (r.ui.full().both);
    r.refresh();
    check (r.ui.isDual(), "switched to dual F1+F2");

    // Check display component drawing has exactly 3 curves (F1, F2, F1+F2)
    {
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.full().display.draw (sb);

        int numCurves = 0;
        for (uint32_t i = 0; i < data->numOps; ++i)
            if (data->ops[i].op == gpu::kOpCurve)
                ++numCurves;

        check (numCurves == 3, "F1+F2 display scene has exactly three curves (F1, F2 and F1+F2 resultant): found " + std::to_string (numCurves));
    }

    // Check full page scene has exactly 3 response curves
    {
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);

        int numCurves = 0;
        for (uint32_t i = 0; i < data->numOps; ++i)
            if (data->ops[i].op == gpu::kOpCurve && data->ops[i].f[1] > 2.0f)
                ++numCurves;

        check (numCurves == 3, "F1+F2 page scene has exactly three filter curves: found " + std::to_string (numCurves));
    }
}

void testSerialAndParallelGiveDifferentResultingCurves()
{
    Rig r;
    r.inst.set ("filter1.on", 1.0f);
    r.inst.set ("filter2.on", 1.0f);
    r.inst.set ("filter1.model", 2.0f);
    r.inst.set ("filter1.cutoff", 1000.0f);
    r.inst.set ("filter1.resonance", 40.0f);
    r.inst.set ("filter2.model", 3.0f);
    r.inst.set ("filter2.cutoff", 4000.0f);
    r.inst.set ("filter2.resonance", 40.0f);

    // Switch to Expanded Filter and dual
    r.tapC (r.ui.full().expand);
    r.tapC (r.ui.full().both);

    // Parallel routing
    r.inst.set ("filter.routing", 0.0f);
    r.refresh();
    const float* parRes = r.ui.full().display.state.curveResult;
    check (parRes != nullptr, "parallel curveResult is valid");
    std::vector<float> parCurve (parRes, parRes + CurveSource::kPoints);

    // Serial routing
    r.inst.set ("filter.routing", 1.0f);
    r.refresh();
    const float* serRes = r.ui.full().display.state.curveResult;
    check (serRes != nullptr, "serial curveResult is valid");

    float maxDiff = 0.0f;
    for (int i = 0; i < CurveSource::kPoints; ++i)
        maxDiff = std::max (maxDiff, std::fabs (parCurve[static_cast<size_t> (i)] - serRes[i]));

    check (maxDiff > 3.0f, "serial and parallel give different resulting curves for the same two filters (max diff " + std::to_string (maxDiff) + " dB)");
}

void testResultingCurveEqualsSingleFilterWhenOtherOff()
{
    Rig r;
    // Switch to Expanded Filter and dual F1+F2
    r.tapC (r.ui.full().expand);
    r.tapC (r.ui.full().both);

    r.inst.set ("filter1.model", 2.0f);
    r.inst.set ("filter1.cutoff", 1500.0f);
    r.inst.set ("filter1.resonance", 50.0f);
    r.inst.set ("filter2.model", 3.0f);
    r.inst.set ("filter2.cutoff", 5000.0f);
    r.inst.set ("filter2.resonance", 60.0f);

    for (int routing = 0; routing <= 1; ++routing)
    {
        const std::string rName = routing == 0 ? "parallel" : "serial";
        r.inst.set ("filter.routing", static_cast<float> (routing));

        // Case A: F1 ON, F2 OFF
        r.inst.set ("filter1.on", 1.0f);
        r.inst.set ("filter2.on", 0.0f);
        r.refresh();
        const float* resA = r.ui.full().display.state.curveResult;
        const float* c1 = r.ui.full().display.state.curve1;
        check (resA != nullptr && c1 != nullptr, "curves present for F1 ON / F2 OFF (" + rName + ")");
        float diffA = 0.0f;
        for (int i = 0; i < CurveSource::kPoints; ++i)
            diffA = std::max (diffA, std::fabs (resA[i] - c1[i]));
        check (diffA < 1.0e-5f, "resulting curve equals filter 1 curve when filter 2 is off (" + rName + ", diff " + std::to_string (diffA) + ")");

        // Case B: F1 OFF, F2 ON
        r.inst.set ("filter1.on", 0.0f);
        r.inst.set ("filter2.on", 1.0f);
        r.refresh();
        const float* resB = r.ui.full().display.state.curveResult;
        const float* c2 = r.ui.full().display.state.curve2;
        check (resB != nullptr && c2 != nullptr, "curves present for F1 OFF / F2 ON (" + rName + ")");
        float diffB = 0.0f;
        for (int i = 0; i < CurveSource::kPoints; ++i)
            diffB = std::max (diffB, std::fabs (resB[i] - c2[i]));
        check (diffB < 1.0e-5f, "resulting curve equals filter 2 curve when filter 1 is off (" + rName + ", diff " + std::to_string (diffB) + ")");
    }
}

void testDualHandlesOnCurvesAndDraggable()
{
    Rig r;
    r.inst.set ("filter1.on", 1.0f);
    r.inst.set ("filter2.on", 1.0f);
    r.inst.set ("filter1.cutoff", 800.0f);
    r.inst.set ("filter1.resonance", 30.0f);
    r.inst.set ("filter2.cutoff", 6000.0f);
    r.inst.set ("filter2.resonance", 45.0f);

    // Switch to Expanded Filter and dual
    r.tapC (r.ui.full().expand);
    r.tapC (r.ui.full().both);
    r.refresh();

    // Verify handle points lie on the curves
    float hx1 = 0.0f, hy1 = 0.0f, hx2 = 0.0f, hy2 = 0.0f;
    r.ui.full().display.handlePointFor (1, hx1, hy1);
    r.ui.full().display.handlePointFor (2, hx2, hy2);
    check (hx1 > 0.0f && hy1 > 0.0f, "handle 1 position is valid");
    check (hx2 > hx1 && hy2 > 0.0f, "handle 2 position is valid and to the right of handle 1");

    // Drag near handle 2 -> should move filter 2 cutoff, filter 1 remains unchanged
    const float cut1Before = r.inst.get ("filter1.cutoff");
    r.drag (static_cast<int> (hx2), static_cast<int> (hy2), static_cast<int> (hx2 - 60), static_cast<int> (hy2));
    check (r.inst.get ("filter2.cutoff") < 5500.0f, "dragging handle 2 decreases filter 2 cutoff (" + std::to_string (r.inst.get ("filter2.cutoff")) + " Hz)");
    check (std::fabs (r.inst.get ("filter1.cutoff") - cut1Before) < 1.0f, "dragging handle 2 does not alter filter 1 cutoff");

    // Drag near handle 1 -> should move filter 1 cutoff
    r.ui.full().display.handlePointFor (1, hx1, hy1);
    r.drag (static_cast<int> (hx1), static_cast<int> (hy1), static_cast<int> (hx1 + 60), static_cast<int> (hy1));
    check (r.inst.get ("filter1.cutoff") > 1000.0f, "dragging handle 1 increases filter 1 cutoff (" + std::to_string (r.inst.get ("filter1.cutoff")) + " Hz)");
}

/** What does not apply is HIDDEN, not dimmed: COMB for every model but Comb, VAR and CLEAN with the Formant model, CLEAN with the Comb model, the FORMANT block and its RESONANCE for every model but Formant. */
void testHiddenNotDimmedByModel()
{
    Rig r;
    r.tapC (r.ui.full().expand);   // Expanded Filter shows full FORMANT 4-knob block + TRACK F RESO
    ui::FilterFullScreen& f = r.ui.full();
    auto formantParts = [&] (bool on)
    {
        const bool all = shown (r, f.formantX) == on && shown (r, f.formantY) == on && shown (r, f.formantTrans) == on && shown (r, f.formantSpread) == on && shown (r, f.formantRes) == on;
        return all;
    };
    // Analog (model 0)
    r.inst.set ("filter1.model", 0.0f);
    r.refresh();
    check (! shown (r, f.combOffset) && shown (r, f.var) && shown (r, f.clean) && formantParts (false), "Analog: no COMB, no FORMANT block and RESONANCE; VAR and CLEAN shown");
    check (f.var.enabled() && f.clean.enabled(), "Analog: VAR and CLEAN are enabled (nothing is dimmed)");
    // Comb (model 6)
    r.inst.set ("filter1.model", 6.0f);
    r.refresh();
    check (shown (r, f.combOffset) && f.combOffset.enabled() && shown (r, f.var) && ! shown (r, f.clean) && formantParts (false), "Comb: COMB shown and enabled, VAR shown, CLEAN hidden");
    // Formant (model 5)
    r.inst.set ("filter1.model", 5.0f);
    r.refresh();
    check (! shown (r, f.var) && ! shown (r, f.combOffset) && ! shown (r, f.clean) && formantParts (true), "Formant: VAR, COMB and CLEAN hidden; the FORMANT block and RESONANCE shown");
    check (f.formantRes.rect().y == f.keytrack.rect().y && f.formantRes.rect().w == f.keytrack.rect().w, "the formant RESONANCE takes the third slot of the TRACK block (a knob of the same row)");
    check (f.formantRes.enabled() && f.formantX.enabled(), "the shown formant controls are enabled");
    // and back: nothing stays hidden
    r.inst.set ("filter1.model", 3.0f);
    r.refresh();
    check (! shown (r, f.combOffset) && shown (r, f.var) && shown (r, f.clean) && formantParts (false), "Digital: back to the plain page");
    // the Formant model of F2 shows the same for F2
    tapState (r, 1);
    r.inst.set ("filter2.model", 5.0f);
    r.refresh();
    check (formantParts (true) && ! shown (r, f.var), "the state F2 with filter2.model = Formant shows the Formant parts");
    // the hidden controls take no touch: a tap where COMB would be does nothing
    tapState (r, 0);
    r.refresh();
    const int w0 = r.inst.writes;
    const ui::Rect slot = f.combOffset.rect();
    r.drag (slot.x + slot.w / 2, slot.y + slot.h / 2, slot.x + slot.w / 2, slot.y);
    check (r.inst.writes == w0 || r.inst.lastWritten != FakeInst::idx ("filter1.comb_offset"), "a hidden COMB takes no touch");
}



/** The states NOTE and VELO: ONE ScalarCurveUi per source embedded under the top bar (1280 x 556), the filter controls hidden, the writes of the editor, the legato key, the TARGETS page of the source (USED BY), the Q groups
    (the virtual targets 0 .. 6 and the legato parameter) and the state from a reloaded chunk. */
void testNoteVeloStates()
{
    static const std::vector<synthyboy::ContentPoint> kThree = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.8f, 3.0f }, { 1.0f, 1.0f, 0.0f } };
    for (int which = 1; which >= 0; --which)   // 1 NOTE (state 2, source 24), 0 VELO (state 3, source 23)
    {
        const int state = which == 1 ? 2 : 3, src = which == 1 ? 24 : 23;
        const std::string name = which == 1 ? "NOTE" : "VELO";
        Rig r;
        auto c = std::make_shared<synthyboy::StateContent>();
        (which == 1 ? c->noteCurve : c->velocityCurve) = kThree;
        r.inst.contentV = c;
        tapState (r, state);
        r.tapC (r.ui.curveUi().screen.expandBtn);   // Open Expanded NOTE / VELO (1280 x 556)
        r.refresh();
        ScalarCurveUi& cu = r.ui.curveUi();
        ui::ScalarCurveEditor& ed = cu.screen;
        check (cu.which() == which && cu.source() == src && r.ui.curveView(), name + ": the embedded editor is the one of the source " + std::to_string (src));
        check (ed.rect().x == 0 && ed.rect().w == 1280 && ed.rect().h == 556 && ed.rect().y == r.ui.full().rect().y + ui::FilterFullScreen::kTopBarH, name + ": the editor is 1280 x 556 under the top bar (" + std::to_string (ed.rect().w) + " x " + std::to_string (ed.rect().h) + ")");
        ui::FilterFullScreen& f = r.ui.full();
        check (! shown (r, f.power) && ! shown (r, f.both) && ! shown (r, f.model) && ! shown (r, f.style) && ! shown (r, f.clean) && ! shown (r, f.display) && ! shown (r, f.cutoff) && ! shown (r, f.resonance) && ! shown (r, f.drive)
               && ! shown (r, f.mix) && ! shown (r, f.keytrack) && ! shown (r, f.env1) && ! shown (r, f.var) && ! shown (r, f.blend) && ! shown (r, f.routing) && ! shown (r, f.outReadout) && shown (r, f.view),
               name + ": the filter controls are hidden (not dimmed), the selector stays");
        check (ed.canvas.model.n == 3 && std::fabs (ed.canvas.model.p[1].y - 0.8f) < 1.0e-4f, name + ": the content curve is in the editor (pull)");

        // a drag of the middle point writes the CONTENT of this source, throttled, the last write at the end; no engine parameter
        ui::ScalarCurveCanvas& cv = ed.canvas;
        const int w0 = r.inst.writes, s0 = r.inst.scalarWrites;
        r.drag (cv.pxOf (0.5f), cv.pyOf (0.8f), cv.pxOf (0.5f) + 40, cv.pyOf (0.8f), 20);
        check (r.inst.scalarWrites > s0 && r.inst.scalarCurve[which].size() == 3 && r.inst.scalarCurve[1 - which].empty() && r.inst.writes == w0, name + ": the drag wrote the content curve of its own source and no parameter");
        check (r.inst.scalarCurve[which].size() == 3 && r.inst.scalarCurve[which][1].x > 0.55f, name + ": the point moved right (" + std::to_string (r.inst.scalarCurve[which][1].x) + ")");
        // LEGATO: the parameter of its own source
        r.tapC (ed.legato);
        check (r.inst.get (which == 1 ? "curve.note.legato" : "curve.velocity.legato") == 1.0f && r.inst.get (which == 1 ? "curve.velocity.legato" : "curve.note.legato") == 0.0f, name + ": LEGATO writes curve." + (which == 1 ? "note" : "velocity") + ".legato");
        // a tap on the display area of the filter (hidden) does nothing: nothing of the filter takes a touch in this state
        const int w1 = r.inst.writes;
        const ui::Rect cr = f.cutoff.rect();
        r.tap (cr.x + cr.w / 2, cr.y + cr.h / 2);
        check (r.inst.writes == w1, name + ": a touch where the CUTOFF knob is does not write the filter");

        // the Q groups: EDIT [POINT X Y CURVE] = the virtual targets 0 .. 3, LEGATO AND GRID [LEGATO, GRID X, GRID Y, SNAP] = the legato parameter and the virtual 4 .. 6
        const int legato = FakeInst::idx (which == 1 ? "curve.note.legato" : "curve.velocity.legato");
        auto virt = [] (int id) { return QRouter::encode (id, QRouter::kVirtual); };
        check (r.inst.q[0] == virt (0) && r.inst.q[1] == virt (1) && r.inst.q[2] == virt (2) && r.inst.q[3] == virt (3) && r.inst.q[4] == legato && r.inst.q[5] == virt (4) && r.inst.q[6] == virt (5) && r.inst.q[7] == virt (6) && std::string (r.ui.qGroupName()) == "EDIT",
               name + ": Q groups: EDIT = virtual 0 .. 3, then LEGATO and the virtual 4 .. 6");
        r.tapC (ed.gridX);
        check (std::string (r.ui.qGroupName()) == "LEGATO AND GRID" && r.inst.q[0] == legato && r.inst.q[1] == virt (4) && r.inst.q[3] == virt (6) && r.inst.q[4] == virt (0), name + ": a touch on GRID X makes the second group the active one");
        r.tapC (ed.point);
        // the virtual targets: the host writes id 1 (X of the selected point) and the page gives every value back
        cv.select (1);
        r.refresh();
        check (std::fabs (r.inst.virtualOut[1] - cv.model.p[1].x) < 1.0e-4f && std::fabs (r.inst.virtualOut[2] - cv.model.p[1].y) < 1.0e-4f, name + ": the page gives X and Y of the selected point to the virtual targets");
        r.inst.setVirtualIn (2, 0.3f);
        r.refresh();
        check (std::fabs (cv.model.p[1].y - 0.3f) < 1.0e-3f && r.inst.scalarCurve[which].size() == 3 && std::fabs (r.inst.scalarCurve[which][1].y - 0.3f) < 1.0e-3f, name + ": a Q-Link write of the virtual Y moves the point and writes the content");

        // TARGETS: the ModTargetsPage of the source (USED BY), the other source's slots are not in it
        r.inst.mod (1, src, "master.volume", 20.0f);
        r.inst.mod (2, 47 - src, "master.volume", 30.0f);   // the other source (23 + 24 = 47)
        r.inst.mod (3, src, "filter1.cutoff", -40.0f, true);
        r.refresh(); r.refresh(); r.refresh();
        r.tapC (ed.targets);
        check (r.ui.topPage() == FilterUi::kPageTargets && r.ui.targetsPage().numRows() == 2, name + ": TARGETS opens the targets page of the source with its two slots (" + std::to_string (r.ui.targetsPage().numRows()) + " rows)");
        check (std::string (r.ui.qGroupName()) == "Targets", name + ": on the targets page the Q groups are the rows");
        // a depth edit of the first row writes the slot's amount
        const float amt0 = r.inst.get ("mod01.amount");
        const ui::Rect dr = r.ui.targetsPage().rows[0].depth.rect();
        r.drag (dr.x + dr.w / 2, dr.y + dr.h / 2, dr.x + dr.w / 2 + 40, dr.y + dr.h / 2);
        check (r.inst.get ("mod01.amount") != amt0, name + ": a depth knob of the targets page writes the slot");
        // ADD TARGET: the list of the groups, then the parameters of a group, then the slot is written with this source
        int before[uibind::kMaxTargetSlots];
        const int n0 = uibind::slotsOfSource (r.inst, src, before, uibind::kMaxTargetSlots);
        r.tapC (r.ui.targetsPage().add);
        check (r.ui.topPage() == FilterUi::kPageDest && r.ui.destList().state.count > 3, name + ": ADD TARGET opens the list of the groups");
        ui::Rect row = r.ui.destList().rowRect (0);
        r.tap (row.x + row.w / 2, row.y + row.h / 2);
        check (r.ui.topPage() == FilterUi::kPageDest && r.ui.destList().state.headerBack, name + ": a group row shows the parameters of the group");
        int pick = 0;   // the first parameter of the group that is not a target of this source yet
        const std::vector<int>& choices = uibind::DestIndex::get().choices[0];
        while (pick + 1 < 6 && pick + 1 < static_cast<int> (choices.size()) && uibind::slotOfPair (r.inst, src, choices[static_cast<size_t> (pick)]) >= 0)
            ++pick;
        row = r.ui.destList().rowRect (pick);
        r.tap (row.x + row.w / 2, row.y + row.h / 2);
        const int n1 = uibind::slotsOfSource (r.inst, src, before, uibind::kMaxTargetSlots);
        check (n1 == n0 + 1 && r.ui.topPage() == FilterUi::kPageTargets, name + ": the chosen parameter is a new slot of this source and the targets page is back (" + std::to_string (n0) + " -> " + std::to_string (n1) + ")");
        // the remove button of a row clears the slot
        r.refresh(); r.refresh();
        r.tapC (r.ui.targetsPage().rows[0].remove);
        r.refresh(); r.refresh();
        check (uibind::slotsOfSource (r.inst, src, before, uibind::kMaxTargetSlots) == n1 - 1, name + ": X on a row removes the slot");
        r.tapC (r.ui.targetsPage().back);
        check (r.ui.topPage() == 0 && r.ui.curveView() && r.ui.view() == state, name + ": BACK returns to the state");
    }
    // the live marker: the last note-on of the plugin; the cached picture of the page is the same with and without it, the animated layer adds the marker
    {
        Rig r;
        tapState (r, 2);   // NOTE
        auto draw = [&] (std::vector<gpu::SceneOp>& ops, uint32_t& n)
        {
            auto data = std::make_unique<gpu::SceneData>();
            std::memset (data.get(), 0, sizeof (gpu::SceneData));
            gpu::SceneBuilder sb (*data);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.ui.draw (sb);
            n = data->numOps;
            ops.assign (data->ops, data->ops + data->numOps);
        };
        std::vector<gpu::SceneOp> before, after;
        uint32_t nBefore = 0, nAfter = 0;
        draw (before, nBefore);
        r.inst.lastKey = 60;
        r.refresh();
        draw (after, nAfter);
        check (nAfter > nBefore && std::memcmp (before.data(), after.data(), sizeof (gpu::SceneOp) * nBefore) == 0, "a note played: the page gets the marker ops AFTER every cached op, the ops before them are unchanged (" + std::to_string (nAfter - nBefore) + " more)");
        r.inst.lastKey = 72;
        r.refresh();
        std::vector<gpu::SceneOp> moved;
        uint32_t nMoved = 0;
        draw (moved, nMoved);
        check (nMoved == nAfter && std::memcmp (after.data(), moved.data(), sizeof (gpu::SceneOp) * nBefore) == 0 && std::memcmp (after.data(), moved.data(), sizeof (gpu::SceneOp) * nAfter) != 0, "a new note moves only the marker");
        tapState (r, 3);   // VELO follows the velocity, not the key
        r.inst.lastVel = 100;
        r.refresh();
        check (std::fabs (r.ui.curveUi().screen.canvas.state.marker - 100.0f / 127.0f) < 1.0e-4f, "VELO: the marker is the velocity of the last note (100 / 127)");
        tapState (r, 0);
        draw (moved, nMoved);
        check (nMoved > 100, "back in F1 the filter page is drawn (no marker there)");
    }
    // the selector in a curve state goes back to the filter
    {
        Rig r;
        tapState (r, 3);
        tapState (r, 0);
        check (r.ui.view() == 0 && shown (r, r.ui.full().cutoff) && shown (r, r.ui.full().display) && ! r.ui.curveView(), "from VELO a tap on F1 shows the filter again");
    }
}

} // namespace

// ENV 1 and CLEAN are LED toggles, ROUTING a LED list of its three options (all visible); a tap writes the parameter, the lit dot follows it


static void testFilterFactoryBackground ()
{
    using namespace nativeui;
    auto view = pages::make ("filter1", layout::kKindFilterUi, 1);
    auto& root = static_cast<pages::FilterPage&> (*view);
    FakeInst inst;
    UiStack stack;
    stack.openFull (FilterUi::kElementPage);
    ViewInput in;
    in.io = &inst;
    in.ui = &stack;
    in.panelEdgeX = 1280;
    in.nowMs = 1000.0;
    std::vector<uint32_t> pixels (1280 * 800);
    const auto pixel = [&] (int x, int y) { return pixels[y * 1280 + x]; };
    auto render = [&] (bool sentinel)
    {
        view->setHostBackground (sentinel);
        in.nowMs += 20;
        view->update (in);
        view->render (pixels.data(), 1280, 800);
    };
    for (int filter : { 1, 2 })
    {
        inst.setTabView (IInstanceView::kTabViewFilter, 5 + filter);   // Expanded Filter F1 / F2
        const std::string prefix = "filter" + std::to_string (filter);
        inst.set ((prefix + ".on").c_str(), 1.0f);
        render (false);
        const auto power = root.ui().nativePowerRect();
        const auto cutoff = root.ui().full().cutoff.rect();
        const int x = cutoff.x + cutoff.w / 2, y = cutoff.y + cutoff.h / 2;
        const auto noOverlay = ev (TouchEvent::Down, x, y);
        check (view->hasSentinel() && ! root.ui().hostBackground() && pixel (600, 185) != 0u
               && pixel (power.x + 10, power.y + 10) == 0u && pixel (x, y) == 0u
               && ! view->onTouch (noOverlay, stack, inst, in.nowMs),
               prefix + " factory fallback paints the ground and leaves the native footprint and touch intact");
        render (true);
        bool markersCovered = true;
        for (int marker : { 33, 45, 1241 })
            for (int py = 113; py < 121; ++py)
                for (int px = marker - 1; px < marker + 7; ++px)
                    markersCovered &= (pixel (px, py) & 0x00ffffffu) == 0x0b1026u;
        check (root.ui().hostBackground() && pixel (600, 185) == 0u
               && pixel (power.x + 10, power.y + 10) == 0u && pixel (x, y) == 0u && markersCovered,
               prefix + " verified host ground stays transparent outside native marker covers");
        const auto out = root.ui().full().nativeOutReadoutRect();
        const int outX = out.x + out.w / 2, outY = out.y + out.h / 2;
        const int writes = inst.writes;
        view->onTouch (ev (TouchEvent::Down, outX, outY), stack, inst, in.nowMs);
        view->onTouch (ev (TouchEvent::Up, outX, outY), stack, inst, in.nowMs + 1);
        check (! out.empty() && pixel (outX, outY) == 0u && inst.writes == writes,
               prefix + " OUT is a native readout with no write path");

        inst.set ((prefix + ".model").c_str(), FilterUi::kModelPz);
        render (false);
        const auto pz = root.ui().pzEditorButton().rect();
        const int pzX = pz.x + pz.w / 2, pzY = pz.y + pz.h / 2;
        const auto clearIcon = [&] {
            for (int y = pz.y; y < pz.bottom(); ++y)
                for (int x = pz.x; x < pz.right(); ++x)
                    if (pixel (x, y) != 0u)
                        return false;
            return true;
        };
        const bool fallbackClear = clearIcon();
        render (true);
        const ui::Rect expectedPz { root.ui().full().expand.rect().x - 64, root.ui().full().expand.rect().y, 56, 56 };
        check (pz == expectedPz && fallbackClear && clearIcon()
               && ! view->onTouch (ev (TouchEvent::Down, pzX, pzY), stack, inst, in.nowMs),
               prefix + " PZ graph corner is fully transparent and owned by the native launcher");
        view->onTouch (ev (TouchEvent::Up, pzX, pzY), stack, inst, in.nowMs + 1);
        const auto plot = root.ui().full().display.plot();
        const int x0 = plot.x + plot.w / 2, y0 = plot.y + plot.h / 2;
        const auto before = inst.get (prefix + ".cutoff");
        const bool graphDown = view->onTouch (ev (TouchEvent::Down, x0, y0), stack, inst, in.nowMs + 10);
        view->onTouch (ev (TouchEvent::Move, x0 - 80, y0), stack, inst, in.nowMs + 20);
        view->onTouch (ev (TouchEvent::Up, x0 - 80, y0), stack, inst, in.nowMs + 30);
        check (graphDown && plot.contains (x0, y0) && pixel (x0, y0) != 0u && inst.get (prefix + ".cutoff") != before
               && root.ui().topPage() == 0, prefix + " graph tap and drag outside the icon still edit the filter response");
        inst.action (FilterEditPz);
        render (true);
        check (root.ui().topPage() == FilterUi::kPagePz && inst.nativeFilter.layout() == 0,
               prefix + " the native launcher opens the existing PZ curve editor and hides the root");
        stack.back();
        render (true);
        inst.nativeWrite (FilterPower, 0.0f);
        render (true);
        check (inst.get ((prefix + ".on").c_str()) == 0.0f && root.ui().covers (x, y)
               && pixel (power.x + 10, power.y + 10) == 0u && ! root.ui().covers (power.x + 10, power.y + 10),
               prefix + " OFF suppresses native cutoff but keeps the host power aperture");
        const int offWrites = inst.writes;
        view->onTouch (ev (TouchEvent::Down, pzX, pzY), stack, inst, in.nowMs);
        view->onTouch (ev (TouchEvent::Up, pzX, pzY), stack, inst, in.nowMs + 1);
        inst.action (FilterEditPz);
        render (true);
        check (root.ui().topPage() == 0 && inst.pzPosts == 0 && inst.writes == offWrites && stack.depth() == 1,
               prefix + " OFF rejects both the icon tap and a delayed native PZ request");
        inst.set ((prefix + ".on").c_str(), 1.0f);
        render (true);
        view->onTouch (ev (TouchEvent::Down, x, y), stack, inst, in.nowMs);
        in.nowMs += 600.0;
        view->onTick (stack, inst, in.nowMs);
        view->onTouch (ev (TouchEvent::Up, x, y), stack, inst, in.nowMs);
        render (true);
        check (root.ui().topPage() == FilterUi::kPageMods && ! root.ui().hostBackground()
               && inst.nativeFilter.layout() == 0 && (pixel (600, 185) >> 24) == 255,
               prefix + " a valid modulation detail stays opaque and suppresses native root controls");
        stack.back();
    }
    for (int tab : { ui::FilterFullScreen::kViewNote, ui::FilterFullScreen::kViewVelo })
    {
        inst.setTabView (IInstanceView::kTabViewFilter, tab);
        const char* key = tab == ui::FilterFullScreen::kViewNote ? "curve.note.legato" : "curve.velocity.legato";
        inst.set (key, 1.0f);
        render (false);
        const auto glide = root.ui().curveUi().screen.nativeGlideRect();
        const int x = glide.x + glide.w / 2, y = glide.y + glide.h / 2;
        check (! glide.empty() && pixel (x, y) == 0u, std::string (key) + " opens the host-only GLIDE readout");
        inst.set (key, 0.0f);
        render (false);
        check (root.ui().curveUi().screen.nativeGlideRect().empty() && pixel (x, y) != 0u,
               std::string (key) + " OFF closes the GLIDE aperture");
    }
}

static void testNativeFilterBindings ()
{
    using namespace nativeui;
    Rig r;
    r.ui.setHostBackground (true);
    r.inst.set ("filter1.cutoff", 1000.0f);
    r.inst.set ("filter2.cutoff", 2500.0f);
    r.refresh();
    check (r.inst.nativeFilter.layout() == 1 && r.inst.nativeFilter.at (FilterCutoff).cell().target == FakeInst::idx ("filter1.cutoff"),
           "native root initially binds the actual F1 scalar target");
    const auto aperture = r.ui.full().nativeRects()[FilterCutoff];
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    bool clear = ! sb.overflow;
    for (int y = aperture.y; y < aperture.bottom(); ++y)
        for (int x = aperture.x; x < aperture.right(); ++x)
            clear &= pixels[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] == 0u;
    check (clear && ! r.ui.covers (aperture.x + 5, aperture.y + 5), "the live native scalar aperture is transparent and belongs to host input");

    r.inst.nativeWrite (FilterCutoff, 0.9f);
    r.inst.action (FilterF2);
    r.refresh();
    r.refresh();
    check (r.inst.get ("filter1.cutoff") == 1000.0f && r.inst.get ("filter2.cutoff") == 2500.0f
           && r.inst.nativeFilter.layout() == 2 && r.inst.nativeFilter.at (FilterCutoff).cell().target == FakeInst::idx ("filter2.cutoff"),
           "F2 discards a queued F1 gesture and exposes F2 only");
    r.inst.nativeWrite (FilterCutoff, 0.9f);
    r.refresh();
    check (r.inst.get ("filter1.cutoff") == 1000.0f && r.inst.get ("filter2.cutoff") != 2500.0f, "the F2 host dial changes only F2");
    const int x = aperture.x + aperture.w / 2, y = aperture.y + aperture.h / 2;
    r.ui.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    ++r.inst.generation;
    r.inst.set ("filter2.cutoff", 3200.0f);
    r.refresh();
    r.inst.nativeWrite (FilterCutoff, 0.95f);
    r.refresh();
    check (r.inst.get ("filter2.cutoff") == 3200.0f, "movement from a held native dial cannot reach a new powered-on preset");
    r.ui.touch (ev (TouchEvent::Up, x, y), r.stack, r.inst, r.now);
    r.ui.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    r.inst.nativeWrite (FilterCutoff, 0.9f);
    r.refresh();
    check (r.inst.get ("filter2.cutoff") != 3200.0f, "a new native gesture can edit the restored preset");
    r.ui.touch (ev (TouchEvent::Up, x, y), r.stack, r.inst, r.now);
    r.ui.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    r.ui.tick (r.now + 600.0, r.stack, r.inst);
    r.refresh();
    check (r.ui.topPage() == FilterUi::kPageMods, "long press on a native dial still opens its modulation editor");
    r.ui.touch (ev (TouchEvent::Up, x, y), r.stack, r.inst, r.now);
    r.stack.back();
    r.refresh();
    r.inst.nativeWrite (FilterCutoff, 0.95f);
    r.inst.set ("filter2.on", 0.0f);
    r.inst.set ("filter2.cutoff", 3000.0f);
    ++r.inst.generation;
    r.refresh();
    const int writes = r.inst.writes;
    check (r.inst.get ("filter2.cutoff") == 3000.0f && r.inst.nativeFilter.at (FilterCutoff).cell().target < 0
           && r.inst.nativeFilter.at (FilterPower).cell().target == FakeInst::idx ("filter2.on") && r.ui.covers (aperture.x + 5, aperture.y + 5),
           "preset OFF discards the stale dial, leaves POWER usable and closes the live scalar aperture");
    r.inst.nativeWrite (FilterCutoff, 0.25f);
    r.refresh();
    check (r.inst.writes == writes, "native scalar input on OFF makes no parameter write");
    r.inst.action (FilterNote);
    r.refresh();
    r.refresh();
    check (r.inst.nativeFilter.layout() == 2 && r.ui.isSplit()
           && r.inst.nativeFilter.at (FilterPower).cell().target == FakeInst::idx ("filter2.on")
           && r.inst.nativeFilter.at (FilterLegato).cell().target == FakeInst::idx ("curve.note.legato"),
           "Split: FilterNote keeps F2 selected while switching right panel to NOTE legato");
    r.inst.nativeWrite (FilterLegato, 1.0f);
    r.refresh();
    check (r.inst.get ("curve.note.legato") == 1.0f && r.inst.get ("curve.velocity.legato") == 0.0f,
           "Split NOTE legato changes only the note curve parameter");
    const auto powerBeforeExpand = r.ui.nativePowerRect();
    r.inst.action (FilterExpandCurve);
    r.refresh();
    r.refresh();
    check (r.inst.nativeFilter.layout() == 3 && r.inst.nativeFilter.at (FilterPower).cell().target < 0
           && r.inst.nativeFilter.at (FilterLegato).cell().target == FakeInst::idx ("curve.note.legato")
           && r.ui.covers (powerBeforeExpand.x + 3, powerBeforeExpand.y + 3),
           "Expanded NOTE is reachable from OFF, takes away POWER, and binds the note legato");
    check (! gpuShown (r, r.ui.full().view) && ! gpuShown (r, r.ui.full().expand),
           "Expanded native NOTE has one host header/expand owner, no fallback header drawn over it");
    r.inst.action (FilterVelo);
    r.refresh();
    r.refresh();
    check (r.inst.nativeFilter.layout() == 4 && r.inst.nativeFilter.at (FilterLegato).cell().target == FakeInst::idx ("curve.velocity.legato"),
           "VELO has its independent legato and view context");
    r.inst.action (FilterExpandCurve);   // Collapse back to Split
    r.refresh();
    r.refresh();
    check (r.ui.isSplit() && r.ui.filterSelection() == 1 && r.ui.scalarSelection() == 1,
           "FilterExpandCurve toggles back from Expanded VELO to F2+VELO Split");
    r.inst.action (FilterTargets);
    r.refresh();
    check (r.ui.topPage() == FilterUi::kPageTargets && r.inst.nativeFilter.layout() == 0, "native TARGETS opens the existing overlay and disables root skin widgets");
}

static void testNativeFilterMenus ()
{
    using namespace nativeui;
    Rig r;
    r.inst.set ("filter1.model", 0.0f);
    r.inst.set ("filter1.style", 3.0f);
    r.inst.set ("filter2.model", 0.0f);
    r.inst.set ("filter2.style", 2.0f);
    r.ui.setHostBackground (true);
    r.refresh();
    const auto filter = r.inst.nativeFilter.domain (kFxFirstDomain + kFxModel);
    const auto style = r.inst.nativeFilter.domain (kFxFirstDomain + kFxStyleBase);
    check (filter.slot == FilterModel && filter.cell().target == FakeInst::idx ("filter1.model")
           && style.slot == FilterStyle && style.cell().target == FakeInst::idx ("filter1.style"),
           "native MODEL and Analog STYLE domains use the selected F1 controls");
    r.inst.nativeWrite (FilterStyle, 0.0f);
    r.inst.nativeWrite (FilterModel, 4.0f / 26.0f);   // Diode has two styles
    r.refresh();
    check (r.inst.get ("filter1.model") == 4.0f && r.inst.get ("filter1.style") == 1.0f
           && r.inst.nativeFilter.domain (kFxFirstDomain + kFxStyleBase + 4).slot == FilterStyle
           && r.inst.nativeFilter.domain (kFxFirstDomain + kFxStyleBase).slot < 0,
           "native MODEL retains the existing style clamp and discards the former style domain's queued choice");
    const int before = r.inst.writes;
    r.inst.nativeWrite (FilterStyle, 0.0f);
    r.refresh();
    check (r.inst.get ("filter1.style") == 0.0f && r.inst.get ("filter2.style") == 2.0f && r.inst.writes == before + 1,
           "native Diode style changes only the selected filter and indexes only its two styles");
    r.inst.action (FilterF2);
    r.refresh();
    r.refresh();
    check (r.inst.nativeFilter.domain (kFxFirstDomain + kFxModel).cell().target == FakeInst::idx ("filter2.model"),
           "switching to F2 moves the same SDK MODEL menu to the F2 parameter");
    r.inst.nativeWrite (FilterModel, 6.0f / 26.0f);
    ++r.inst.generation;
    r.refresh();
    check (r.inst.get ("filter2.model") == 0.0f, "a native menu choice queued before the preset change is discarded");
    r.inst.set ("filter.routing", 0.0f);
    r.refresh();
    const int routeWrites = r.inst.writes;
    r.inst.nativeWrite (FilterRouting, 1.0f);
    r.refresh();
    check (r.inst.get ("filter.routing") == 2.0f && r.inst.writes == routeWrites + 1
           && r.inst.lastWritten == FakeInst::idx ("filter.routing"), "native routing writes exactly one parameter for a selected row");
}

static void exportNativeFilterContract ()
{
    using namespace nativeui;
    ui::FilterFullScreen full;
    full.place ({ 0, FilterUi::kTabY, FilterUi::kCanvasW, FilterUi::kTabH });
    full.setPageMode (true);
    full.power.state.on = true;
    full.style.state.count = 3;
    full.setSplitMode (false);
    auto rect = [] (ui::Rect r)
    {
        if (r.empty())
            std::printf ("null");
        else
            std::printf ("[%d, %d, %d, %d]", r.x, r.y - FilterUi::kTabY, r.w, r.h);
    };
    auto all = [&] (const auto& areas)
    {
        bool first = true;
        for (auto r : areas)
        {
            std::printf ("%s", first ? "[" : ", ");
            rect (r);
            first = false;
        }
        std::printf ("]");
    };
    std::printf ("{\n  \"view\": ");
    rect (full.view.rect());
    std::printf (",\n  \"both\": ");
    rect (full.both.rect());
    full.setFormantMode (true);
    std::printf (",\n  \"expand_filter_formant\": ");
    rect (full.nativeExpandFilterRect());
    full.setFormantMode (false);
    std::printf (",\n  \"expand_filter\": ");
    rect (full.nativeExpandFilterRect());
    std::printf (",\n  \"out\": ");
    rect (full.nativeOutReadoutRect());
    std::printf (",\n  \"pz_expand\": ");
    rect ({ full.expand.rect().x - 64, full.expand.rect().y, 56, 56 });
    ui::PzCurveEditor pz;
    pz.place ({0, 110, 1280, 628});
    std::printf (",\n  \"pz_toolbar\": ");
    all (pz.toolbarRects());
    std::printf (",\n  \"filter\": [\n    ");
    for (int kind = 0; kind < 3; ++kind)
    {
        full.setFormantMode (kind == 1);
        full.setCombMode (kind == 2);
        all (full.nativeRects());
        std::printf (kind < 2 ? ",\n    " : "\n");
    }
    ui::ScalarCurveEditor curve;
    curve.place (full.bodyRect());
    std::printf ("  ],\n  \"curve\": ");
    all (curve.nativeRects());
    std::printf (",\n  \"glide\": ");
    rect (curve.glideRect().inset (2));
    {
        full.setView (ui::FilterFullScreen::kViewNote);
        std::printf (",\n  \"expand_curve_top\": ");
        rect (full.expand.rect());
        full.setView (ui::FilterFullScreen::kViewF1);
    }
    // 60/40 Split screen contract
    full.setFormantMode (false);
    full.setCombMode (false);
    full.setSplitMode (true);
    ui::ScalarCurveEditor splitCurve;
    splitCurve.setCompact (true);
    const int lx = 8, lw = (FilterUi::kCanvasW - 28) * 60 / 100;
    const int rx = lx + lw + 12, rw = FilterUi::kCanvasW - 8 - rx;
    splitCurve.place ({ rx, FilterUi::kTabY + ui::kQLane, rw, FilterUi::kTabH - ui::kQLane - 8 });
    std::printf (",\n  \"split\": {\n    \"view\": ");
    rect (full.view.rect());
    std::printf (",\n    \"scalar_view\": ");
    rect (full.nativeScalarViewRect());
    std::printf (",\n    \"filter_expand\": ");
    rect (full.nativeExpandFilterRect());
    std::printf (",\n    \"curve_expand\": ");
    rect (splitCurve.nativeExpandRect());
    std::printf (",\n    \"pz_expand\": ");
    rect ({ full.expand.rect().x - 64, full.expand.rect().y, 56, 56 });
    std::printf (",\n    \"filter\": [\n      ");
    for (int kind = 0; kind < 3; ++kind)
    {
        full.setFormantMode (kind == 1);
        full.setCombMode (kind == 2);
        all (full.nativeRects());
        std::printf (kind < 2 ? ",\n      " : "\n");
    }
    std::printf ("    ],\n    \"curve\": ");
    all (splitCurve.nativeRects());
    std::printf ("\n  }");
    std::printf (",\n  \"one_style\": [");
    bool first = true;
    for (int model = 0; model < 27; ++model)
        if (synthyboy::filters::filterModelStyleCount (model) <= 1)
        {
            std::printf ("%s%d", first ? "" : ", ", model);
            first = false;
        }
    std::printf ("]\n}\n");
}

static void testPzCurveEditor()
{
    Rig r;
    auto content = std::make_shared<synthyboy::StateContent>();
    content->filterCurves = { { "filter1", {{0.0f, -48.0f}, {0.5f, -12.0f}, {1.0f, 50.0f}} },
                              { "filter2", {{0.0f, 6.0f}, {1.0f, -9.0f}} },
                              { "fxfilter", {{0.0f, -11.0f}, {1.0f, 20.0f}} } };
    r.inst.contentV = content;
    r.inst.set ("filter1.model", FilterUi::kModelPz);
    r.inst.set ("filter2.model", FilterUi::kModelPz);
    r.inst.set ("filter1.on", 0.0f);
    r.refresh();
    const int originalWrites = r.inst.writes;
    r.tapC (r.ui.pzEditorButton());
    check (r.ui.topPage() == 0 && ! r.ui.pzEditorButton().enabled() && r.inst.writes == originalWrites,
           "OFF PZ SVF shows a disabled editor button and does not edit the response underneath");
    r.inst.set ("filter1.on", 1.0f);
    r.refresh();
    auto open = [&] { r.tapC (r.ui.pzEditorButton()); };
    const auto& editor = r.ui.pzEditor();
    open();
    check (r.ui.topPage() == FilterUi::kPagePz && r.inst.fullOpen && r.inst.nativeFilter.layout() == 0
           && r.inst.q[0] == -1 && ! editor.apply.enabled(), "the PZ editor owns its modal and suspends the native Filter controls and Q-Links");
    check (editor.canvas.points().size() == 3 && editor.canvas.points().front().y == -48.0f
           && editor.canvas.points().back().y == 50.0f, "the editor loads imported dB values across the full stored range");

    const auto& canvas = editor.canvas;
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder painter (*scene);
    painter.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (painter);
    bool graph = false;
    for (uint32_t i = 0; i < scene->numOps; ++i)
        graph = graph || scene->ops[i].op == gpu::kOpCurve;
    check (! painter.overflow && graph && canvas.plot().w > 640, "the modal draws the shaped dB curve inside its plot");
    r.tap (canvas.px (0.24f), canvas.py (-52.0f));
    check (canvas.points().size() == 4 && editor.remove.enabled(), "tapping the graph adds a point at its frequency and gain");
    r.tapC (editor.remove);
    check (canvas.points().size() == 3 && ! editor.apply.enabled(), "deleting the selected point restores the previous curve without an edit");
    r.drag (canvas.px (0.5f), canvas.py (-12.0f), canvas.px (0.7f), canvas.py (31.0f));
    check (std::fabs (canvas.points()[1].x - 0.7f) < 0.005f && std::fabs (canvas.points()[1].y - 31.0f) < 0.5f
           && r.inst.pzPosts == 0 && r.inst.writes == originalWrites, "dragging previews the PZ point and writes no synthesis parameter or preset until Apply");
    r.tapC (editor.apply);
    FilterCurveEdits::Batch batch;
    check (r.ui.topPage() == 0 && r.inst.pzPosts == 1 && r.inst.pzEdits.take (batch), "Apply posts one content edit for F1");
    check (FilterCurveEdits::apply (*content, std::move (batch), r.inst.generation, [] (int) { return true; })
           && synthyboy::validateContent (*content), "the content edit is valid");
    check (content->filterCurves[0].points[1].y > 30.0f && content->filterCurves[1].points.front().y == 6.0f
           && content->filterCurves[2].points.front().y == -11.0f,
           "F1 updates only its content entry and keeps F2 and the FX curve untouched");
    auto params = synthyboy::ParamValues::defaults();
    params.plain[demo::Inst::idx ("filter1.model")] = FilterUi::kModelPz;
    const auto chunk = synthyboy::saveState (params, *content);
    synthyboy::StateContent restored;
    check (synthyboy::loadStateWithContent (chunk.data(), chunk.size(), params, restored).status == synthyboy::LoadStatus::ok
           && restored == *content, "preset and chunk serialization round-trip the edited filter curves");

    r.inst.tabViewV[IInstanceView::kTabViewFilter] = 1;
    r.refresh();
    open();
    check (canvas.points().size() == 2 && canvas.points().front().y == 6.0f, "F2 opens its own saved curve");
    r.tapC (editor.reset);
    check (canvas.points().size() == 3 && canvas.points()[1].y == 0.0f && editor.apply.enabled(), "Reset previews a flat curve");
    for (int i = 0; i < 20; ++i)
        r.tap (canvas.px (0.1f + 0.04f * static_cast<float> (i)), canvas.py (36.0f));
    check (canvas.points().size() == synthyboy::kMaxFilterCurvePoints, "the editor stops adding at the content format's 16-point limit");
    r.tapC (editor.back);
    check (r.ui.topPage() == 0 && r.inst.pzPosts == 1 && content->filterCurves[1].points.size() == 2,
           "Back discards the untouched preset's preview");

    open();
    r.tapC (editor.reset);
    r.inst.set ("filter2.on", 0.0f);
    r.tapC (editor.apply);
    check (r.ui.topPage() == 0 && r.inst.pzPosts == 1, "switching the filter OFF prevents a pending Apply even before repaint");
    r.inst.set ("filter2.on", 1.0f);
    r.refresh();
    open();
    r.tapC (editor.reset);
    auto next = std::make_shared<synthyboy::StateContent> (*content);
    next->filterCurves[1].points = {{0.0f, -22.0f}, {1.0f, 10.0f}};
    r.inst.contentV = next;
    ++r.inst.generation;
    r.tapC (editor.apply);
    check (r.ui.topPage() == FilterUi::kPagePz && ! editor.apply.enabled() && r.inst.pzPosts == 1
           && canvas.points().front().y == -22.0f, "preset recall keeps the same PZ filter open and discards the previous draft and Apply");
    r.inst.set ("filter2.model", 0.0f);
    r.refresh();
    check (r.ui.topPage() == 0 && r.inst.pzPosts == 1, "a host model change closes the PZ editor without a write");
    r.inst.set ("filter2.model", FilterUi::kModelPz);
    r.refresh();
    open();
    r.tapC (editor.reset);
    FakeInst other;
    other.set ("filter2.model", FilterUi::kModelPz);
    other.set ("filter2.on", 1.0f);
    page::Input input;
    input.instance = 2;
    other.tabViewV[IInstanceView::kTabViewFilter] = 1;
    r.ui.update (other, r.stack, r.now + 50.0, input);
    check (r.ui.topPage() == 0 && other.pzPosts == 0 && r.inst.pzPosts == 1,
           "changing plugin instance discards the old editor even if both instances show the same filter");

    FilterCurveEdits queue;
    queue.post (1, {{0.0f, 4.0f}, {1.0f, 8.0f}}, 8);
    queue.post (2, {{0.0f, -4.0f}, {1.0f, -2.0f}}, 9);
    check (queue.take (batch) && ! FilterCurveEdits::apply (restored, std::move (batch), 9, [] (int which) { return which == 1; })
           && restored == *content, "the shell commit discards prior-generation and now-disabled queued edits");
}

static void testSplit6040LayoutAndState ()
{
    Rig r;
    auto& f = r.ui.full();
    auto& c = r.ui.curveUi().screen;
    check (r.ui.isSplit() && f.splitMode() && c.compact(), "Split root starts in 60/40 Split mode with compact ScalarCurveEditor");

    // 1. 60/40 Split exact columns (8 px margin, 12 px center gap, 751 px left + 501 px right)
    check (f.view.rect().x == 8 && f.view.rect().y == FilterUi::kTabY + ui::kQLane && f.view.rect().h == 56,
           "Split left header starts at x=8, y=kTabY+16, h=56");
    check (f.clean.rect().right() == 8 + 751 && f.expand.rect().right() <= 8 + 751,
           "Split left panel fits within 751 px (60% column)");
    check (f.scalarView.rect().x == 771 && c.rect().x == 771 && c.rect().right() == 771 + 501,
           "Split right panel occupies x=771..1272 (501 px, 12 px gap from left)");
    check (f.power.rect().x == 16 && f.power.rect().y == f.graphRect().y + 4
           && f.expand.rect().right() == f.graphRect().right() - 8 && f.expand.rect().y == f.graphRect().y + 4,
           "Split places POWER at graph top-left and EXPAND at graph top-right");
    check (f.display.rect().y >= f.power.rect().bottom() && f.display.rect().y >= f.expand.rect().bottom(),
           "Split left graph plots strictly below POWER/EXPAND top row");

    // 2. Side dials (60 px) & vertical ROUTING >= 56 px/row & all hitboxes >= 56 px
    for (const ui::FilterKnob* k : { &f.cutoff, &f.resonance, &f.drive, &f.mix, &f.keytrack, &f.var, &f.blend })
        check (k->state.side && k->state.dialMax == 60 && k->rect().w >= 56 && k->rect().h >= 56,
               std::string ("Split knob uses 60 px side-dial >=56px: ") + k->state.label);
    check (f.routing.columns() == 1 && f.routing.rect().h >= 3 * 56,
           "Split ROUTING is a single vertical column with >=56 px per radio row");
    check (c.gridX.rect().h >= 56 && c.gridY.rect().h >= 56 && c.snap.rect().h >= 56 && c.legato.rect().h >= 56
           && c.init.rect().h >= 56 && c.undoButton.rect().h >= 56 && c.del.rect().h >= 56,
           "Split right panel controls all have >=56 px touch height");

    // 3. Independent state round-trip (tabview 0..11) + independent F1/F2 & NOTE/VELO + Formant/Comb
    for (int fs = 0; fs < 2; ++fs)
        for (int ss = 0; ss < 2; ++ss)
            for (int em = 0; em < 3; ++em)
            {
                const int enc = tabview::encodeFilterSplit (fs, ss, em);
                int df = -1, ds = -1, de = -1;
                tabview::decodeFilterSplit (enc, df, ds, de);
                check (enc >= 0 && enc <= tabview::kMax[1] && df == fs && ds == ss && de == em,
                       "tabview filter split encode/decode round-trip");
            }
    tapState (r, 1);   // F2
    tapState (r, 3);   // VELO
    check (r.ui.filterSelection() == 1 && r.ui.scalarSelection() == 1, "Split selects F2 + VELO independently");
    r.inst.set ("filter2.model", 5.0f);   // Formant in Split: F RESO in TRACK slot 3, XY pad active
    r.refresh();
    check (f.formantMode() && shown (r, f.formantRes) && ! shown (r, f.formantX),
           "Split Formant puts F RESO in TRACK and defers X/Y/TRANSPOSE/SPREAD to Expanded Filter");
    r.drag (f.display.rect().x + 40, f.display.rect().y + 30, f.display.rect().x + 120, f.display.rect().y + 70);
    check (r.inst.lastWritten == FakeInst::idx ("filter2.formant.x") || r.inst.lastWritten == FakeInst::idx ("filter2.formant.y"),
           "Split Formant XY pad directly drives F2 formant X/Y");
    r.inst.set ("filter2.model", 6.0f);   // Comb in Split
    r.refresh();
    check (f.combMode() && shown (r, f.combOffset), "Split Comb shows conditional COMB knob in TRACK");
    r.inst.set ("filter2.model", 3.0f);
    r.refresh();

    // 4. Shared ROUTING OFF rule + right-panel NOTE/VELO independence when BOTH filters OFF
    r.inst.set ("filter1.on", 0.0f);
    r.inst.set ("filter2.on", 1.0f);
    tapState (r, 0);   // F1 selected (OFF), F2 is ON
    r.refresh();
    check (! f.cutoff.enabled() && f.routing.enabled() && ! f.routing.inactive(),
           "Split F1 OFF / F2 ON: F1 controls OFF, shared ROUTING active");
    r.inst.nativeWrite (nativeui::FilterRouting, 1.0f); r.refresh();
    check (r.inst.get ("filter.routing") == 2.0f, "Split F1 OFF / F2 ON: tapping ROUTING writes filter.routing");
    r.inst.set ("filter2.on", 0.0f);   // Both OFF
    r.refresh();
    check (! f.routing.enabled() && f.routing.inactive(), "Split Both OFF: shared ROUTING passive");
    const int beforeBothOff = r.inst.writes;
    r.inst.nativeWrite (nativeui::FilterRouting, 0.0f); r.refresh();
    check (r.inst.writes == beforeBothOff && r.inst.get ("filter.routing") == 2.0f,
           "Split Both OFF: ROUTING touch rejected");
    // Right panel LEGATO still works with both filters OFF!
    r.tapC (r.ui.curveUi().screen.legato);
    check (r.inst.get ("curve.velocity.legato") == 1.0f,
           "Split Both OFF: right NOTE/VELO panel stays active and writes velocity legato");
    // Passive native artwork is visible through its own aperture while all edit
    // proxies and overlay gestures remain disabled. Values come from read-only
    // engine handles rather than stale native edit proxies.
    r.refresh();
    check (! f.hasNativeControl (nativeui::FilterCutoff)
           && f.nativeRects()[nativeui::FilterCutoff].empty()
           && ! f.nativeRects (true)[nativeui::FilterCutoff].empty()
           && ! gpuShown (r, f.cutoff),
           "OFF native Cutoff keeps a passive-art aperture without an editable control");

}

static void testNativeOnlyControlOwnership()
{
    using namespace nativeui;
    Rig r;
    for (int filter = 0; filter < 2; ++filter)
        for (int model : { 3, 5, 6 })
        {
            r.inst.setTabView (IInstanceView::kTabViewFilter, tabview::encodeFilterSplit (filter, 0, tabview::kFilterExpandedFilter));
            r.inst.set ("filter" + std::to_string (filter + 1) + ".model", static_cast<float> (model));
            r.refresh();
            auto& full = r.ui.full();
            bool onlyGraph = true;
            for (int child = 0; child < full.numChildren(); ++child)
                onlyGraph &= ! full.childShown (child) || full.child (child) == &full.display;
            check (onlyGraph, "Filter native controls are state/Q adapters; the graph is the only drawn/touchable child");
            for (int slot = FilterCutoff; slot < FilterModel; ++slot)
            {
                const auto binding = r.inst.nativeFilter.at (slot).cell();
                if (binding.target < 0) continue;
                const auto area = full.nativeRects()[slot];
                const int before = r.inst.writes;
                r.tap (area.x + area.w / 2, area.y + area.h / 2);
                check (r.inst.writes == before, "overlay tap cannot edit a native Filter parameter");
                const float norm = binding.kind == kFxToggle ? 1.0f : 0.7f;
                r.inst.nativeWrite (slot, norm);
                r.refresh();
                const auto& parameter = synthyboy::paramTable()[binding.target];
                const float expected = synthyboy::constrainValue (parameter, synthyboy::fromNormalised (parameter, norm));
                check (std::fabs (r.inst.param (binding.target) - expected) < std::max (0.001f, std::fabs (expected) * 0.0001f),
                       "the acknowledged native Filter proxy edits its exact current parameter");
            }
            // Invalidating the host binding must not reveal another widget UI.
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene); sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.inst.nativeFilter.publish (0, nullptr);
            r.ui.draw (sb);
            std::vector<uint32_t> pixels; renderScene (*scene, pixels);
            const auto aperture = full.nativeRects (true)[FilterCutoff];
            bool transparent = true;
            for (int y = aperture.y; y < aperture.bottom(); ++y)
                for (int x = aperture.x; x < aperture.right(); ++x)
                    transparent &= pixels[static_cast<size_t> (y) * 1280 + x] == 0u;
            check (transparent, "missing native binding leaves a clear aperture, never a fallback GPU dial");
            r.refresh();
        }
    ui::ScalarCurveEditor editor;
    editor.place ({0, 0, 1280, 556});
    bool onlyGraphAndReadout = true;
    for (int child = 0; child < editor.numChildren(); ++child)
        onlyGraphAndReadout &= ! editor.childShown (child) || editor.child (child) == &editor.canvas || editor.child (child) == &editor.usedList;
    check (onlyGraphAndReadout, "Scalar editor has no GPU field/button children, only its canvas and used-by readout");
    const auto field = editor.point.rect();
    check (! editor.touchDown (field.x + field.w / 2, field.y + field.h / 2, 100.0)
           && ! editor.touchUp (field.x + field.w / 2, field.y + field.h / 2),
           "Scalar native field coordinates have no fallback input path");
}

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--skin-contract") == 0)
    {
        exportNativeFilterContract();
        return 0;
    }
    testNativeOnlyControlOwnership();
    testFilterFactoryBackground();
    testNativeFilterBindings();
    testPzCurveEditor();
    testNativeFilterMenus();
    testEveryControlHasAParameter();
    testStatesSwitching();
    testSplit6040LayoutAndState();
    testBothIsAViewOnly();
    testPageIsAlwaysOpen();
    testDisabledFilterIsReadOnly();
    testStandaloneFilterPowerAndOffNavigation();
    testDisplayInteractiveDrag();
    testFormantModeSwitchAndDrag();
    testQGroupsAndTurn();
    testModulationsPage();
    testModulationsBoard();
    testTouchTargetsAndLayoutSizes();
    testQFrameIsOneBlock();
    testDisplayStoresNoTruth();
    testRealCurveAndLiveTargets();
    testDrawingAndSceneOps();
    testSwitchedOffFilterIsDim();
    testDualSceneHasThreeCurves();
    testSerialAndParallelGiveDifferentResultingCurves();
    testResultingCurveEqualsSingleFilterWhenOtherOff();
    testDualHandlesOnCurvesAndDraggable();
    testHiddenNotDimmedByModel();
    testNoteVeloStates();

    std::printf (g_fails ? "filter_ui_host: FAILED (%d)\n" : "filter_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
