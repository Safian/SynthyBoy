// SynthyBoy - MPC overlay tests: the FILTER UI (filter_ui.h), the page of the Filter tabs.
// Wiring of every control to the engine parameters (both directions), pages of the UiStack, Q groups (one block each),
// interactive response curve and Formant XY pad dragging with its values, route popup, model / style lists, modulations,
// touch targets >= 56 px, a value under every knob, and drawing inside the scene ops budget.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../filter_ui.h"
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

class FakeInst final : public IInstanceView
{
public:
    FakeInst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }

    float param (int i) const noexcept override { return (i >= 0 && static_cast<size_t>(i) < synthyboy::paramCount()) ? v_[i] : 0.0f; }
    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        out = liveVals_;
        return hasLive_;
    }
    double sampleRate() const noexcept override { return 48000.0; }

    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 4; ++i)
            q[i] = (i < n) ? t[i] : -1;
        ++qCalls;
    }

    void writeParam (int i, float norm) noexcept override
    {
        if (i >= 0 && static_cast<size_t>(i) < synthyboy::paramCount())
        {
            const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
            v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
            ++writes;
            lastWritten = i;
        }
    }

    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    int playLastSelector() const noexcept override { return lastSel; }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    void set (const std::string& key, float val) { const int i = idx (key); if (i >= 0) v_[i] = val; }
    float get (const std::string& key) const { const int i = idx (key); return i >= 0 ? v_[i] : 0.0f; }

    int q[4] = { -1, -1, -1, -1 };
    int qCalls = 0, writes = 0, lastWritten = -1;
    bool fullOpen = false;
    int lastSel = 0;

    bool hasLive_ = false;
    synthyboy::LiveValues liveVals_ {};

private:
    float v_[synthyboy::kMaxParams] = {};
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

    Rig (int inst1 = 1) : instance (inst1) { stack.openFull (FilterUi::kElementFilter); refresh(); }

    void refresh()
    {
        now += 50.0;
        page::Input in;
        in.instance = instance;
        in.targets = useTargets ? targets : nullptr;
        in.live = inst.hasLive_ ? &inst.liveVals_ : nullptr;
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

void testInstancesSwitching()
{
    Rig r;
    check (r.ui.selection() == 1 && r.ui.editFilter() == 1, "initial selection is F1 (filter 1)");

    // Tap F2 in instances selector
    const ui::Rect selR = r.ui.full().instances.rect();
    r.tap (selR.x + selR.w / 2, selR.y + selR.h / 2);
    check (r.ui.selection() == 2 && r.ui.editFilter() == 2, "tap middle of instances selector switches to F2");

    // Tap F1+F2 in instances selector
    r.tap (selR.x + selR.w * 5 / 6, selR.y + selR.h / 2);
    check (r.ui.selection() == 3 && r.ui.isDual(), "tap right of instances selector switches to dual F1+F2");
    check (r.ui.full().display.state.dual, "display state is dual");

    // Tap F1 in instances selector
    r.tap (selR.x + selR.w / 6, selR.y + selR.h / 2);
    check (r.ui.selection() == 1 && ! r.ui.isDual(), "tap left switches back to F1");
}

void testPageIsAlwaysOpen()
{
    Rig r;
    check (r.stack.depth() == 1 && r.stack.element() == FilterUi::kElementFilter && r.inst.fullOpen, "the page is the full screen of the element: always open, the host widgets hidden");
    check (r.ui.displayRect().w > 1000 && r.ui.displayRect().h > 200, "the display is large (" + std::to_string (r.ui.displayRect().w) + " x " + std::to_string (r.ui.displayRect().h) + ")");
    check (r.ui.full().pageMode() && r.ui.full().close.rect().w == 0, "a page has no CLOSE button");
    Rig two (2);
    check (two.ui.selection() == 2 && two.ui.editFilter() == 2, "the tab Filter 2 shows F2");
}

void testKnobDragWritesParameter()
{
    Rig r;
    r.inst.set ("filter1.cutoff", 1000.0f);
    r.refresh();

    int x, y;
    r.centre (r.ui.full().cutoff, x, y);
    const int writesBefore = r.inst.writes;
    r.drag (x, y, x, y - 60);

    check (r.inst.get ("filter1.cutoff") > 1500.0f, "drag up on CUTOFF increases filter1.cutoff (" + std::to_string (r.inst.get ("filter1.cutoff")) + " Hz)");
    check (r.inst.writes > writesBefore && r.inst.lastWritten == FakeInst::idx ("filter1.cutoff"), "writeParam was notified");

    // External change reflection
    r.inst.set ("filter1.cutoff", 8000.0f);
    r.refresh();
    check (std::string (r.ui.full().cutoff.state.valueText) == "8.00 kHz", "outside parameter change updates valueText on next frame (8.00 kHz)");
}

void testBipolarAndToggles()
{
    Rig r;

    // Pan
    check (r.ui.full().pan.state.bipolar, "PAN knob is bipolar");
    r.inst.set ("filter1.pan", 0.0f);
    r.refresh();
    check (std::string (r.ui.full().pan.state.valueText) == "C", "PAN 0% formats as 'C'");

    int x, y;
    r.centre (r.ui.full().pan, x, y);
    r.drag (x, y, x, y - 40);
    check (r.inst.get ("filter1.pan") > 10.0f, "dragging PAN up moves right");

    // Power toggle
    r.inst.set ("filter1.on", 1.0f);
    r.refresh();
    check (r.ui.full().power.state.on, "power state is initially ON");
    r.tapC (r.ui.full().power);
    check (r.inst.get ("filter1.on") == 0.0f && ! r.ui.full().power.state.on, "tapping power toggle turns filter OFF");

    // Clean mode toggle
    r.inst.set ("filter1.clean_mode", 0.0f);
    r.refresh();
    check (! r.ui.full().clean.state.on, "clean mode initially OFF");
    r.tapC (r.ui.full().clean);
    check (r.inst.get ("filter1.clean_mode") == 1.0f, "tapping clean mode toggle turns it ON");
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

void testModelSelectionAndListPopup()
{
    Rig r;
    check (r.ui.topPage() == 0, "no popup initially open");

    // Tap model stepper opens list popup
    r.tapC (r.ui.full().modelList);
    check (r.ui.topPage() == FilterUi::kPageList, "tapping model stepper opens ListPopup");
    check (r.ui.list().state.count == 27, "model list contains 27 models");

    // Tap row 6 ("Comb")
    const ui::Rect row6 = r.ui.list().rowRect (6);
    r.tap (row6.x + row6.w / 2, row6.y + row6.h / 2);
    check (r.inst.get ("filter1.model") == 6.0f, "tapping row 6 in ListPopup writes filter1.model = 6 (Comb)");
    check (r.ui.topPage() == 0, "selecting a row closes ListPopup");
    check (r.ui.full().combOffset.enabled(), "Comb model enables combOffset knob");
}

void testRoutePopup()
{
    Rig r;
    check (r.ui.topPage() == 0, "no popup open");

    r.tapC (r.ui.full().route);
    check (r.ui.topPage() == FilterUi::kPageRoute, "tapping route button opens route popup");

    // Drag level knob inside route panel
    r.inst.set ("filter1.level", 0.0f);
    r.refresh();
    int x, y;
    r.centre (r.ui.routePanel().level, x, y);
    r.drag (x, y, x, y - 40);
    check (r.inst.get ("filter1.level") > 2.0f, "dragging level knob in route popup modifies filter1.level");

    // Close via close button
    r.tapC (r.ui.routePanel().close);
    check (r.ui.topPage() == 0, "tapping close in route panel closes popup");

    // Open and close via backdrop tap
    r.tapC (r.ui.full().route);
    check (r.ui.topPage() == FilterUi::kPageRoute, "reopened route popup");
    r.tap (20, 130); // tap outside route panel box inside tab
    check (r.ui.topPage() == 0, "tap on backdrop closes route popup");
}

void testQGroupsAndTurn()
{
    Rig r;
    check (std::string (r.ui.qGroupName()) == "MAIN", "the active Q group is MAIN");
    check (r.inst.q[0] == FakeInst::idx ("filter1.cutoff")
           && r.inst.q[1] == FakeInst::idx ("filter1.resonance")
           && r.inst.q[2] == FakeInst::idx ("filter1.drive")
           && r.inst.q[3] == FakeInst::idx ("filter1.mix"),
           "MAIN Q targets are CUTOFF, RESO, DRIVE, MIX");

    check (std::string (r.ui.qGroupName()) == "MAIN", "the default Q group is MAIN");

    // Touch a control in OUT group (e.g. blend knob)
    int bx, by;
    r.centre (r.ui.full().blend, bx, by);
    r.tap (bx, by);
    check (std::string (r.ui.qGroupName()) == "OUT", "touching blend knob switches active QGroup to OUT");
    check (r.inst.q[0] == FakeInst::idx ("filter1.level")
           && r.inst.q[1] == FakeInst::idx ("filter1.pan")
           && r.inst.q[2] == FakeInst::idx ("filter1.blend")
           && r.inst.q[3] == FakeInst::idx ("filter.routing"),
           "OUT QGroup targets are LEVEL, PAN, BLEND, ROUTING");

    // Test turnQ routing to active group member
    const float lvlBefore = r.inst.get ("filter1.level");
    r.turnQ (0, 0.05); // turn Q-Link 1 (Level)
    check (r.inst.get ("filter1.level") != lvlBefore, "turnQ on slot 0 routes to LEVEL knob");

    // Touch KEYTRACK in TRACK group
    r.centre (r.ui.full().keytrack, bx, by);
    r.tap (bx, by);
    check (std::string (r.ui.qGroupName()) == "TRACK", "touching keytrack switches active QGroup to TRACK");
    check (r.inst.q[0] == FakeInst::idx ("filter1.keytrack")
           && r.inst.q[1] == FakeInst::idx ("filter1.env1")
           && r.inst.q[2] == FakeInst::idx ("filter1.var")
           && r.inst.q[3] == FakeInst::idx ("filter1.comb_offset"),
           "TRACK QGroup targets are KEYTRACK, ENV1, VAR, COMB");
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

void testTouchTargetsAndLayoutSizes()
{
    Rig r;
    ui::FilterFullScreen& f = r.ui.full();
    check (f.power.rect().w >= 56 && f.power.rect().h >= 56, "power button >= 56 px");
    check (f.model.rect().w >= 208 && f.model.rect().h >= 56 && f.style.rect().w >= 208 && f.style.rect().h >= 56, "the model and the style steppers >= 208 x 56 px");
    check (f.modelList.rect().w >= 56 && f.modelList.rect().h >= 56, "the list button of the models >= 56 px");
    check (f.clean.rect().w >= 72 && f.clean.rect().h >= 56 && f.route.rect().w >= 64 && f.route.rect().h >= 56, "CLEAN and OUTPUT >= 56 px");
    check (f.instances.rect().w >= 3 * 56 && f.instances.rect().h >= 56, "F1 | F2 | F1+F2 selector >= 56 px per cell");
    ui::Component* leaves[64];
    const int n = f.collectLeaves (leaves, 0, 64);
    int small = 0, leavesOk = 0;
    for (int i = 0; i < n; ++i)
        if (leaves[i]->rect().w > 0 && ! leaves[i]->sizeOk())
            ++small;
        else
            ++leavesOk;
    check (small == 0, "every component of the page has at least its minimum size (" + std::to_string (leavesOk) + " ok, " + std::to_string (small) + " too small)");
    // one family of knobs: every knob of the three blocks has the same size
    const ui::Rect a = f.cutoff.rect();
    bool same = true;
    for (const ui::Knob* k : { &f.resonance, &f.drive, &f.mix, &f.keytrack, &f.var, &f.combOffset, &f.level, &f.pan, &f.blend })
        same = same && std::abs (k->rect().w - a.w) <= 3 && k->rect().h == a.h;
    check (same, "MAIN, TRACK and OUT knobs are one family of size");
}

void testQFrameIsOneBlock()
{
    Rig r;
    ui::FilterFullScreen& f = r.ui.full();
    for (int g = 0; g < 3; ++g)
    {
        const ui::Rect b = f.block (g), u = f.groups[g].rect();
        check (b.contains (u), std::string ("the Q group ") + ui::FilterFullScreen::groupName (g) + " lies inside its block");
        ui::Component* leaves[64];
        const int n = f.collectLeaves (leaves, 0, 64);
        check (f.groups[g].check (leaves, n) == 0, std::string ("no other component lies in the frame of the group ") + ui::FilterFullScreen::groupName (g));
    }
    r.inst.set ("filter1.model", 5.0f);
    r.refresh();
    const ui::Rect b = f.block (3), u = f.groups[3].rect();
    ui::Component* leaves[64];
    const int n = f.collectLeaves (leaves, 0, 64);
    check (b.w > 300 && b.contains (u) && f.groups[3].check (leaves, n) == 0, "the FORMANT block holds its Q group (X, Y, TRANSPOSE, SPREAD) alone, RESONANCE beside it");
    check (f.display.rect().right() < f.block (3).x, "the display and the FORMANT block do not overlap");
}

void testEveryKnobShowsItsValue()
{
    Rig r;
    ui::FilterFullScreen& f = r.ui.full();
    bool all = true;
    for (const ui::Knob* k : { &f.cutoff, &f.resonance, &f.drive, &f.mix, &f.keytrack, &f.var, &f.combOffset, &f.level, &f.pan, &f.blend })
    {
        const bool ok = k->state.valueText[0] != '\0';
        if (! ok)
            std::printf ("  no value under %s\n", k->state.label);
        all = all && ok;
    }
    check (all, "every knob shows its value (parameter's own format)");
    check (std::string (f.cutoff.state.valueText) == "12.00 kHz" || std::string (f.cutoff.state.valueText).find ("kHz") != std::string::npos, "the cutoff is written in Hz / kHz (" + std::string (f.cutoff.state.valueText) + ")");
    check (std::string (f.drive.state.valueText).find ("dB") != std::string::npos, "the drive is written in dB (" + std::string (f.drive.state.valueText) + ")");
    check (std::string (f.env1.state.label) == "ENV 1" && std::string (f.clean.state.label) == "CLEAN", "the switches have their names");
}

void testStyleListAndModelList()
{
    Rig r;
    r.tapC (r.ui.full().style);   // the centre of the stepper asks for the list
    check (r.ui.topPage() == FilterUi::kPageList && r.ui.list().state.count == 12, "tapping the style opens its list (12 styles)");
    const ui::Rect row3 = r.ui.list().rowRect (3);
    r.tap (row3.x + row3.w / 2, row3.y + row3.h / 2);
    check (r.inst.get ("filter1.style") == 3.0f && r.ui.topPage() == 0, "a row of the list writes filter1.style");
    // the ends of the stepper step by one
    const ui::Rect s = r.ui.full().style.rect();
    r.tap (s.x + 12, s.y + s.h / 2);
    check (r.inst.get ("filter1.style") == 2.0f, "the left end of the style stepper steps back");
    r.tapC (r.ui.full().model);
    check (r.ui.topPage() == FilterUi::kPageList && r.ui.list().state.count == 27, "the middle of the model stepper opens the list of the models as well");
    r.tap (20, 130);
    check (r.ui.topPage() == 0, "a tap beside the list closes it");
}

void testDragShowsValues()
{
    Rig r;
    ui::FilterDisplay& d = r.ui.full().display;
    const ui::Rect dr = d.rect();
    r.ui.touch (ev (TouchEvent::Down, dr.x + dr.w / 2, dr.y + dr.h / 2), r.stack, r.inst, r.now);
    r.ui.touch (ev (TouchEvent::Move, dr.x + dr.w / 2 + 40, dr.y + dr.h / 2 - 20), r.stack, r.inst, r.now + 16.0);
    check (d.dragging(), "while the point is dragged the display knows");
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder with (*data);
    with.begin (gpu::kKindPlay, 0, 1280, 800);
    d.draw (with);
    const uint32_t opsDragging = data->numOps;
    r.ui.touch (ev (TouchEvent::Up, dr.x + dr.w / 2 + 40, dr.y + dr.h / 2 - 20), r.stack, r.inst, r.now + 40.0);
    r.refresh();
    check (! d.dragging(), "the finger lifted: no bubble");
    auto data2 = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder without (*data2);
    without.begin (gpu::kKindPlay, 0, 1280, 800);
    d.draw (without);
    check (opsDragging > data2->numOps, "the dragged values are drawn beside the point only while it is dragged (" + std::to_string (opsDragging) + " > " + std::to_string (data2->numOps) + " ops)");
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

    // 4. Draw route popup
    {
        r.tapC (r.ui.full().route);
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        check (! sb.overflow && data->numOps > 10, "route popup scene fits ops budget (" + std::to_string (data->numOps) + " ops)");
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (cv.size() == 1280u * 800u, "CPU renderer renders route popup scene");
    }
}

} // namespace

int main()
{
    testEveryControlHasAParameter();
    testInstancesSwitching();
    testPageIsAlwaysOpen();
    testKnobDragWritesParameter();
    testBipolarAndToggles();
    testDisplayInteractiveDrag();
    testFormantModeSwitchAndDrag();
    testModelSelectionAndListPopup();
    testRoutePopup();
    testQGroupsAndTurn();
    testModulationsPage();
    testTouchTargetsAndLayoutSizes();
    testQFrameIsOneBlock();
    testEveryKnobShowsItsValue();
    testStyleListAndModelList();
    testDragShowsValues();
    testRealCurveAndLiveTargets();
    testDrawingAndSceneOps();

    std::printf (g_fails ? "filter_ui_host: FAILED (%d)\n" : "filter_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
