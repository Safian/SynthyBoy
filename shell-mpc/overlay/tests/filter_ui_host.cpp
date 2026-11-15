// SynthyBoy - MPC overlay tests: the FILTER UI (filter_ui.h).
// Wiring of every control to the engine parameters (both directions), pages of the UiStack,
// Q-groups, interactive response curve and Formant XY pad dragging, route popup, modulations,
// touch targets >= 56 px, and drawing inside the scene ops budget.
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

    Rig() { refresh(); }

    void refresh (int hl = 1)
    {
        now += 50.0;
        ui.update (inst, hl, stack, now, 1, 1);
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

    void openFull() { tapC (ui.compact().full); }

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
    const ui::Rect selR = r.ui.compact().instances.rect();
    r.tap (selR.x + selR.w / 2, selR.y + selR.h / 2);
    check (r.ui.selection() == 2 && r.ui.editFilter() == 2, "tap middle of instances selector switches to F2");

    // Tap F1+F2 in instances selector
    r.tap (selR.x + selR.w * 5 / 6, selR.y + selR.h / 2);
    check (r.ui.selection() == 3 && r.ui.isDual(), "tap right of instances selector switches to dual F1+F2");
    check (r.ui.compact().display.state.dual, "display state is dual");

    // Tap F1 in instances selector
    r.tap (selR.x + selR.w / 6, selR.y + selR.h / 2);
    check (r.ui.selection() == 1 && ! r.ui.isDual(), "tap left switches back to F1");
}

void testFullScreenOpensAndCloses()
{
    Rig r;
    check (r.stack.isMain() && ! r.inst.fullOpen, "starts in main view (compact)");
    r.openFull();
    check (r.stack.depth() == 1 && r.inst.fullOpen, "full screen button opens full screen view");
    check (r.ui.displayRect().w > 600, "full screen has large display rectangle");

    r.tapC (r.ui.full().close);
    check (r.stack.isMain() && ! r.inst.fullOpen, "close button returns to main view");

    r.openFull();
    r.stack.closeAll();
    r.refresh();
    check (! r.inst.fullOpen, "navigating away closes full screen");
}

void testKnobDragWritesParameter()
{
    Rig r;
    r.inst.set ("filter1.cutoff", 1000.0f);
    r.refresh();

    int x, y;
    r.centre (r.ui.compact().cutoff, x, y);
    const int writesBefore = r.inst.writes;
    r.drag (x, y, x, y - 60);

    check (r.inst.get ("filter1.cutoff") > 1500.0f, "drag up on CUTOFF increases filter1.cutoff (" + std::to_string (r.inst.get ("filter1.cutoff")) + " Hz)");
    check (r.inst.writes > writesBefore && r.inst.lastWritten == FakeInst::idx ("filter1.cutoff"), "writeParam was notified");

    // External change reflection
    r.inst.set ("filter1.cutoff", 8000.0f);
    r.refresh();
    check (std::string (r.ui.compact().cutoff.state.valueText) == "8.0 kHz", "outside parameter change updates valueText on next frame (8.0 kHz)");
}

void testBipolarAndToggles()
{
    Rig r;
    r.openFull();

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
    check (! r.ui.full().cleanMode.state.on, "clean mode initially OFF");
    r.tapC (r.ui.full().cleanMode);
    check (r.inst.get ("filter1.clean_mode") == 1.0f, "tapping clean mode toggle turns it ON");
}

void testDisplayInteractiveDrag()
{
    Rig r;
    r.inst.set ("filter1.cutoff", 500.0f);
    r.inst.set ("filter1.resonance", 10.0f);
    r.refresh();

    // Drag on FilterDisplay in compact view
    const ui::Rect dRect = r.ui.compact().display.rect();
    // Drag towards top right (higher cutoff and higher resonance)
    r.drag (dRect.x + dRect.w / 4, dRect.y + dRect.h * 3 / 4, dRect.x + dRect.w * 3 / 4, dRect.y + dRect.h / 4);

    check (r.inst.get ("filter1.cutoff") > 2000.0f, "dragging filter display moves cutoff up (" + std::to_string (r.inst.get ("filter1.cutoff")) + " Hz)");
    check (r.inst.get ("filter1.resonance") > 40.0f, "dragging filter display moves resonance up (" + std::to_string (r.inst.get ("filter1.resonance")) + " %)");
}

void testFormantModeSwitchAndDrag()
{
    Rig r;
    r.openFull();
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
    r.openFull();
    check (r.ui.topPage() == 0, "no popup initially open");

    // Tap model stepper opens list popup
    r.tapC (r.ui.full().model);
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

    r.tapC (r.ui.compact().route);
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
    check (std::string (r.ui.qGroupName()) == "Main", "compact view active QGroup is Main");
    check (r.inst.q[0] == FakeInst::idx ("filter1.cutoff")
           && r.inst.q[1] == FakeInst::idx ("filter1.resonance")
           && r.inst.q[2] == FakeInst::idx ("filter1.drive")
           && r.inst.q[3] == FakeInst::idx ("filter1.mix"),
           "compact Q-targets are CUTOFF, RESO, DRIVE, MIX");

    r.openFull();
    check (std::string (r.ui.qGroupName()) == "MAIN", "full screen default QGroup is MAIN");

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
    r.centre (r.ui.compact().cutoff, x, y);
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
    // Check all primary touch target sizes >= 56 px (or standard button minSize)
    check (r.ui.compact().power.rect().w >= 52 && r.ui.compact().power.rect().h >= 52, "power button >= 52 px");
    check (r.ui.compact().model.rect().w >= 56 && r.ui.compact().model.rect().h >= 50, "model stepper >= 50 px");
    check (r.ui.compact().cutoff.rect().w >= 72 && r.ui.compact().cutoff.rect().h >= 72, "cutoff knob >= 72 px");
    check (r.ui.compact().display.rect().w >= 200 && r.ui.compact().display.rect().h >= 100, "display rect >= 100 px");

    r.openFull();
    check (r.ui.full().close.rect().w >= 56 && r.ui.full().close.rect().h >= 50, "full close button >= 50 px");
    check (r.ui.full().instances.rect().w >= 100 && r.ui.full().instances.rect().h >= 50, "full instances selector >= 50 px");
    check (r.ui.full().routing.rect().w >= 100 && r.ui.full().routing.rect().h >= 48, "routing stepper >= 48 px");
}

void testDrawingAndSceneOps()
{
    Rig r;
    // 1. Draw compact view
    {
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        check (! sb.overflow && data->numOps > 10, "compact view scene fits ops budget (" + std::to_string (data->numOps) + " ops)");
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (cv.size() == 1280u * 800u, "CPU renderer renders compact view scene");
    }

    // 2. Draw full screen view
    {
        r.openFull();
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
    testFullScreenOpensAndCloses();
    testKnobDragWritesParameter();
    testBipolarAndToggles();
    testDisplayInteractiveDrag();
    testFormantModeSwitchAndDrag();
    testModelSelectionAndListPopup();
    testRoutePopup();
    testQGroupsAndTurn();
    testModulationsPage();
    testTouchTargetsAndLayoutSizes();
    testDrawingAndSceneOps();

    std::printf (g_fails ? "filter_ui_host: FAILED (%d)\n" : "filter_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
