// SynthyBoy - MPC overlay tests: the oscillator UI (osc_ui.h): the wiring of every control to the engine parameters (both directions), the pages of the UiStack, the Q group, the modulations, the
// controls that are disabled because the engine has nothing behind them, the touch path (decoder events -> components -> parameters) and the drawing (one scene, inside the canvas).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "../osc_ui.h"
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

/** A plugin instance made of an array of plain values: writeParam converts like the plugin does and counts the writes (the host notification). */
class Inst final : public IInstanceView
{
public:
    Inst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }
    float param (int i) const noexcept override { return v_[i]; }
    bool live (synthyboy::LiveValues&) const noexcept override { return false; }
    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return sel; }
    void setQGroup (const int* t, int n) noexcept override { for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1; ++qCalls; }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setPlaySelection (int g, int i) noexcept override { if (g == 0) sel = i; ++selWrites; }
    int playLastSelector() const noexcept override { return lastSel; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    void contentRoots (std::vector<ContentRoot>& out) const override { out = roots; }
    void requestContent (int osc, int kind, const std::string& path) override { requests.push_back ({ osc, kind, path }); loadState = 1; }
    int contentLoad (std::string*) const override { return loadState; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    struct Request { int osc, kind; std::string path; };
    std::vector<ContentRoot> roots;
    std::vector<Request> requests;
    int loadState = 0;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }
    int sel = 2, lastSel = 0, q[4] = { -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0;
    bool fullOpen = false;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }
constexpr int kOY = 0;   // the canvas is the screen: the rectangles of the components are in screen pixels

struct Rig
{
    OscUi ui;
    Inst inst;
    UiStack stack;
    double now = 1000.0;
    Rig() { refresh(); }
    void refresh (int hl = 1) { now += 50.0; ui.update (inst, hl, stack, now, 1, 1); }
    bool tap (int cx, int cy, int holdMs = 40)
    {
        const bool a = ui.touch (ev (TouchEvent::Down, cx, cy + kOY), stack, inst, now);
        now += holdMs;
        ui.tick (now, stack, inst);
        ui.touch (ev (TouchEvent::Up, cx, cy + kOY), stack, inst, now);
        now += 100.0;
        refresh();
        return a;
    }
    void tapC (const ui::Component& c, int holdMs = 40) { tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs); }
    void drag (int x0, int y0, int x1, int y1, int steps = 10)
    {
        ui.touch (ev (TouchEvent::Down, x0, y0 + kOY), stack, inst, now);
        for (int i = 1; i <= steps; ++i)
        {
            now += 16.0;
            ui.touch (ev (TouchEvent::Move, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps + kOY), stack, inst, now);
        }
        ui.touch (ev (TouchEvent::Up, x1, y1 + kOY), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void centre (const ui::Component& c, int& x, int& y) { x = c.rect().x + c.rect().w / 2; y = c.rect().y + c.rect().h / 2; }
    void openFull() { tapC (ui.compact().full); }
};

void testEveryControlHasAParameterOrIsDisabled()
{
    for (int sel = 1; sel <= 5; ++sel)
        for (int raw : { 0, 4, 2, 1, 3 })
        {
            if ((sel == 1 || sel == 5) && raw != 0)
                continue;
            Rig r;
            r.inst.sel = sel;
            if (sel >= 2 && sel <= 4)
                r.inst.set ("osc" + std::to_string (sel - 1) + ".type", static_cast<float> (raw));
            r.refresh();
            const std::string name = "instance " + std::to_string (sel) + " type " + std::to_string (raw);
            int unbound = 0;
            std::string which;
            for (const OscUi::Item& it : r.ui.items())
            {
                if (it.type == OscUi::T::Inert || it.type == OscUi::T::Source)   // a source of content: no parameter, a file
                    continue;
                if (it.param < 0 && it.c->enabled())
                {
                    ++unbound;
                    which += " " + std::to_string (it.id);
                }
            }
            check (unbound == 0, name + ": every control has an engine parameter or is disabled (enabled without one:" + which + ")");
            check (r.ui.selection() == sel, name + ": the selection follows the Play Osc selector");
        }
}

void testNoiseBasicQ1IsNoiseSample()
{
    Rig r;
    r.inst.sel = 5;
    r.refresh();
    check (r.inst.q[0] == Inst::idx ("noise.sample") && r.inst.q[1] == Inst::idx ("noise.filter") && r.inst.q[2] == Inst::idx ("noise.pan") && r.inst.q[3] == Inst::idx ("noise.level"),
           "noise: the Basic group is noise.sample, noise.filter, noise.pan, noise.level (not noise.type)");
}

void testBasicGroupsAndSelection()
{
    Rig r;
    check (r.inst.q[0] == Inst::idx ("osc1.type") && r.inst.q[1] == Inst::idx ("osc1.wavetable") && r.inst.q[2] == Inst::idx ("osc1.pan") && r.inst.q[3] == Inst::idx ("osc1.level"),
           "A: the Basic group is type, wavetable, pan, level of osc1");
    r.inst.sel = 1;
    r.refresh();
    check (r.inst.q[0] == Inst::idx ("sub.waveform") && r.inst.q[1] == Inst::idx ("sub.octave") && r.inst.q[2] == Inst::idx ("sub.pan") && r.inst.q[3] == Inst::idx ("sub.level"), "Sub: waveform, octave, pan, level");
    r.inst.sel = 4;
    r.refresh();
    check (r.inst.q[0] == Inst::idx ("osc3.type") && r.inst.q[2] == Inst::idx ("osc3.pan"), "C is osc3");
    // the compact view's instance selector writes the Play Osc selector
    r.inst.sel = 2;
    r.refresh();
    const ui::Rect c = r.ui.compact().instances.rect();
    r.tap (c.x + c.w * 4 / 5 + 4, c.y + c.h / 2);
    check (r.inst.sel == 5, "tap on the fifth cell of the selector: Play Osc = Noise");
    r.refresh();
    check (r.ui.selection() == 5, "the UI follows the selector");
}

void testFullScreenOpensAndCloses()
{
    Rig r;
    check (r.stack.isMain() && ! r.inst.fullOpen, "main: no full screen");
    r.openFull();
    check (r.stack.depth() == 1 && r.inst.fullOpen, "the full-screen button opens the full screen and tells the plugin");
    check (r.ui.displayRect().w > 500, "the full screen has a display rectangle");
    r.tapC (r.ui.full().close);
    check (r.stack.isMain() && ! r.inst.fullOpen, "the close button is back (main again, the plugin is told)");
    // navigating away
    r.openFull();
    r.stack.closeAll();
    r.refresh();
    check (! r.inst.fullOpen, "the stack closed from outside (the page left): the plugin is told");
}

void testKnobDragWritesTheParameter()
{
    Rig r;
    r.inst.set ("osc1.level", 20.0f);
    r.refresh();
    int x, y;
    r.centre (r.ui.compact().basic.level, x, y);
    const int before = r.inst.writes;
    r.drag (x, y, x, y - 60);
    check (r.inst.get ("osc1.level") > 40.0f && r.inst.get ("osc1.level") < 60.0f, "drag up by 60 px on LEVEL: osc1.level 20 -> about 50 (" + std::to_string (r.inst.get ("osc1.level")) + ")");
    check (r.inst.writes > before && r.inst.lastWritten == Inst::idx ("osc1.level"), "the write went through writeParam (the host is told)");
    // the other direction: the parameter changes behind the UI's back
    r.inst.set ("osc1.level", 80.0f);
    r.refresh();
    check (std::fabs (r.ui.compact().basic.level.state.value - 0.8) < 1e-6, "a parameter change from outside shows in the knob on the next frame");
}

void testPanBipolarAndPower()
{
    Rig r;
    int x, y;
    r.centre (r.ui.compact().basic.pan, x, y);
    check (std::fabs (r.ui.compact().basic.pan.state.value - 0.5) < 1e-6 && r.ui.compact().basic.pan.state.bipolar, "PAN: centre value 0.5, bipolar");
    r.drag (x, y, x, y - 40);
    check (r.inst.get ("osc1.pan") > 10.0f, "PAN dragged up goes right");
    r.inst.set ("osc1.on", 1.0f);
    r.refresh();
    r.tapC (r.ui.compact().power);
    check (r.inst.get ("osc1.on") == 0.0f, "the power toggle turns the oscillator off");
}

void testTypeSelectorAndKinds()
{
    Rig r;
    r.openFull();
    const ui::Rect sel = r.ui.full().basic.selector.rect();
    // three cells: wavetable, multisample, granular
    r.tap (sel.x + sel.w * 5 / 6, sel.y + sel.h / 2);
    check (r.inst.get ("osc1.type") == 2.0f, "the third pictogram is Granular (osc1.type = 2)");
    r.refresh();
    check (r.ui.full().kind() == ui::OscKind::Granular && r.ui.full().panel.preset() == &ui::presets::kGranularFull, "the full screen switched to the granular layout");
    const ui::Rect sel2 = r.ui.full().basic.selector.rect();
    r.tap (sel2.x + sel2.w / 2, sel2.y + sel2.h / 2);
    check (r.inst.get ("osc1.type") == 4.0f, "the middle pictogram is Multisample (osc1.type = 4)");
    r.inst.set ("osc1.type", 1.0f);   // Sample: only on the host's pages
    r.refresh();
    check (std::string (r.ui.typeName()) == "Sample" && r.ui.full().panel.preset() == &ui::presets::kEmptyFull && r.ui.full().bar.numSlots() == 0, "type Sample: the type name, an empty type bar and an empty bottom band");
    check (r.ui.full().basic.selector.state.index == -1, "no pictogram is highlighted for a type the selector does not offer");
}

void testTuneOctSem()
{
    Rig r;
    r.inst.set ("osc1.transpose", 19.0f);
    r.refresh();
    r.openFull();
    auto& t = r.ui.full().tune;
    check (t.oct.state.value == 1.0 && t.sem.state.value == 7.0, "transpose 19 = OCT 1, SEM 7");
    r.inst.set ("osc1.transpose", -5.0f);
    r.refresh();
    check (t.oct.state.value == 0.0 && t.sem.state.value == -5.0, "transpose -5 = OCT 0, SEM -5");
    r.inst.set ("osc1.transpose", 19.0f);
    r.refresh();
    int x, y;
    r.centre (t.oct, x, y);
    r.drag (x, y, x, y - 9, 3);   // up: one step (8 px per step)
    const float tr = r.inst.get ("osc1.transpose");
    check (static_cast<int> (tr) % 12 == 7 && tr >= 31.0f, "OCT up keeps the SEM remainder: transpose " + std::to_string (tr));
    check (OscUi::transposeFrom (4, 12) == 48 && OscUi::transposeFrom (-4, -12) == -48, "OCT / SEM clamp at +-48");
    check (r.inst.get ("osc1.transpose") == std::floor (r.inst.get ("osc1.transpose")), "transpose stays whole");
    // FIN and CRS and the pitch tracking toggle
    r.inst.set ("osc1.tune", 12.0f); r.inst.set ("osc1.pitch_coarse", -3.5f); r.refresh();
    check (t.fin.state.value == 12.0 && t.crs.state.value == -3.5, "FIN = osc1.tune, CRS = osc1.pitch_coarse");
    r.inst.set ("osc1.pitch_track", 1.0f);
    r.refresh();
    r.tapC (t.track);
    check (r.inst.get ("osc1.pitch_track") == 0.0f, "the pitch tracking toggle writes osc1.pitch_track");
}

void testDisabledControls()
{
    Rig r;
    r.openFull();
    auto& bar = r.ui.full().bar;
    const OscUi::Item* mem = r.ui.item (OscUi::kBarSlot + 3);
    check (mem != nullptr && ! mem->c->enabled() && mem->alwaysOff, "wavetable: the phase memory stepper is disabled (no voice of the own engine reads osc.phase_memory)");
    check (r.ui.full().panel.warp()->first.hasVar() && ! r.ui.full().panel.warp()->first.var.enabled() && ! r.ui.full().panel.warp()->second.var.enabled(), "WARP Var is drawn disabled (there is no parameter)");
    const OscUi::Item* drop = r.ui.item (OscUi::kBasic + 2);
    check (drop != nullptr && ! drop->c->enabled(), "the source dropdown of the compact view is disabled (the list opens from the full screen)");
    // the source stepper of multisample is disabled
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    const OscUi::Item* src = r.ui.item (OscUi::kBasic + 1);
    check (src != nullptr && src->c->enabled() && src->type == OscUi::T::Source && src->param < 0, "multisample: the source (content, not a parameter) shows the loaded file and opens the browser");
    (void) bar;
}

void testRoutePopup()
{
    {
        Rig c;   // the route button of the compact view opens the full screen and the popup over it
        c.tapC (c.ui.compact().route);
        check (c.stack.depth() == 2 && c.stack.top() == OscUi::kPageRoute, "the route button of the compact view opens the full screen and the route popup");
    }
    Rig r;
    r.openFull();
    r.tapC (r.ui.full().route);
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageRoute, "the route button opens the route popup");
    check (r.ui.routePanel().target.state.count == 7 && std::string (r.ui.routePanel().target.state.text) == "Filter 1", "the target stepper lists the seven destinations");
    check (! r.ui.routePanel().balance.enabled(), "the balance knob is disabled unless the target is Filter blend");
    r.inst.set ("osc1.destination", 5.0f);
    r.refresh();
    check (r.ui.routePanel().balance.enabled() && std::string (r.ui.routePanel().target.state.text) == "Filter blend", "Filter blend enables the balance knob");
    // the Q group of the popup
    check (r.inst.q[0] == Inst::idx ("osc1.destination") && r.inst.q[1] == Inst::idx ("osc1.filter_blend") && r.inst.q[2] == Inst::idx ("osc1.bus1_send") && r.inst.q[3] == Inst::idx ("osc1.bus2_send"), "the popup's group: destination, filter_blend, bus1_send, bus2_send");
    // the target stepper: right half = next
    const ui::Rect t = r.ui.routePanel().target.rect();
    r.tap (t.x + t.w - 10, t.y + t.h / 2);
    check (r.inst.get ("osc1.destination") == 6.0f, "the right half of the target stepper selects the next destination");
    // a tap outside the popup closes it
    r.tap (20, 130);
    check (r.stack.depth() == 1, "a tap on the backdrop closes the popup");
    r.tapC (r.ui.full().route);
    r.tapC (r.ui.routePanel().close);
    check (r.stack.depth() == 1, "the close button of the popup is back");
}

void testDetailPages()
{
    Rig r;
    r.inst.set ("osc1.unison.mode", 2.0f);
    r.refresh();
    r.openFull();
    auto* box = r.ui.full().panel.unison();
    check (box != nullptr, "wavetable full screen has the unison box");
    r.tapC (box->gear);
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageUnison, "the gear opens the unison page");
    check (std::string (r.ui.detail().panel.stepper (ui::PanelSlot::Mode)->state.text) == "Exp", "MODE shows the engine's choice label of osc1.unison.mode");
    check (r.inst.q[0] == Inst::idx ("osc1.unison.mode") && r.inst.q[1] == Inst::idx ("osc1.unison.stack") && r.inst.q[2] == Inst::idx ("osc1.unison.spread") && r.inst.q[3] == Inst::idx ("osc1.unison.range"), "the unison page's first group: mode, stack, spread (WIDTH), range");
    ui::Stepper* st = r.ui.detail().panel.stepper (ui::PanelSlot::Mode);
    r.tap (st->rect().x + st->rect().w - 10, st->rect().y + st->rect().h / 2);
    check (r.inst.get ("osc1.unison.mode") == 3.0f, "MODE steps the unison mode");
    ui::Knob* w = r.ui.detail().panel.knob (ui::PanelSlot::Width);
    int x, y;
    r.centre (*w, x, y);
    r.inst.set ("osc1.unison.spread", 10.0f);
    r.refresh();
    r.drag (x, y, x, y - 40);
    check (r.inst.get ("osc1.unison.spread") > 20.0f, "WIDTH = osc1.unison.spread, a drag changes it");
    r.tapC (r.ui.detail().back);
    check (r.stack.depth() == 1, "BACK returns to the full screen");
    // granular: the grain window page and the loop mode stepper
    r.inst.set ("osc1.type", 2.0f);
    r.refresh();
    check (r.ui.full().bar.numSlots() == 4, "granular type bar has four slots");
    const OscUi::Item* loop = r.ui.item (OscUi::kBarSlot + 2);
    check (loop != nullptr && std::string (static_cast<ui::Stepper*> (loop->c)->state.text) == "One-shot", "loop mode: One-shot");
    ui::Stepper* ls = static_cast<ui::Stepper*> (loop->c);
    r.tap (ls->rect().x + ls->rect().w - 10, ls->rect().y + ls->rect().h / 2);
    check (r.inst.get ("osc1.gran.loop_grains") == 1.0f && r.inst.get ("osc1.gran.manual") == 0.0f, "loop mode Loop grains = gran.loop_grains 1");
    r.refresh();
    r.tap (ls->rect().x + ls->rect().w - 10, ls->rect().y + ls->rect().h / 2);
    check (r.inst.get ("osc1.gran.manual") == 1.0f && r.inst.get ("osc1.gran.loop_grains") == 0.0f, "loop mode Manual = gran.manual 1");
    // the grain window button
    const OscUi::Item* gw = r.ui.item (OscUi::kBarSlot + 1);
    r.tapC (*gw->c);
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageGrain, "the bell opens the grain window page");
    check (r.inst.q[0] == Inst::idx ("osc1.gran.window_amount") && r.inst.q[1] == Inst::idx ("osc1.gran.rand_window") && r.inst.q[2] == Inst::idx ("osc1.gran.window_skew") && r.inst.q[3] == Inst::idx ("osc1.gran.rand_skew"), "the grain window page's group");
    r.tapC (r.ui.detail().back);
    // multisample envelope
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    const OscUi::Item* env = r.ui.item (OscUi::kBarSlot);
    r.tapC (*env->c);
    check (r.stack.top() == OscUi::kPageEnvelope && r.ui.detail().panel.toggle (ui::PanelSlot::Override) != nullptr, "multisample: the envelope button opens the envelope page");
    r.inst.set ("osc1.multi.override", 0.0f);
    r.refresh();
    r.tapC (*r.ui.detail().panel.toggle (ui::PanelSlot::Override));
    check (r.inst.get ("osc1.multi.override") == 1.0f, "OVERRIDE = osc1.multi.override");
    check (r.inst.q[0] == Inst::idx ("osc1.multi.delay") && r.inst.q[3] == Inst::idx ("osc1.multi.decay"), "the envelope page's group: delay, attack, hold, decay");
}

void testNavigatingAwayClosesEverything()
{
    Rig r;
    r.openFull();
    r.tapC (r.ui.full().route);
    check (r.stack.depth() == 2, "a popup is open");
    r.stack.closeAll();   // what the controller does when the Play page leaves the screen
    r.refresh();
    check (r.stack.isMain() && r.ui.topPage() == 0 && ! r.inst.fullOpen, "after closeAll: main, the compact view, the plugin is told");
}

void testModulations()
{
    Rig r;
    // LFO 2 modulates osc1.level at 40 %, bipolar: slot 3
    r.inst.set ("mod04.source", 8.0f);   // LFO 2 = 6 + 2
    r.inst.set ("mod04.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("osc1.level")))));
    r.inst.set ("mod04.amount", 40.0f);
    r.inst.set ("mod04.bipolar", 1.0f);
    r.inst.lastSel = 2;
    r.ui.update (r.inst, 1, r.stack, r.now += 500, 1, 2);
    const ui::Knob& lv = r.ui.compact().basic.level;
    check (lv.state.numMods == 1 && lv.state.mods[0].source == 8 && std::fabs (lv.state.mods[0].depth - 0.4) < 1e-6 && lv.state.mods[0].bidirectional, "the scan of the 64 slots finds LFO 2 on LEVEL, depth 0.4, bipolar");
    check (lv.state.selectedSource == 8, "the selected source is the LFO the Play page's LFO selector shows (LFO 2 = source 8)");
    r.inst.lastSel = 1;
    r.ui.update (r.inst, 1, r.stack, r.now += 500, 3, 2);
    check (lv.state.selectedSource == 3, "the last selector touched was the envelope's: Env 3 is the selected source");
    // depth by dragging the tab
    r.inst.lastSel = 2;
    r.ui.update (r.inst, 1, r.stack, r.now += 500, 3, 2);
    const ui::Rect kr = lv.rect();
    const int tx = kr.x + 8, ty = kr.y + 6;
    r.drag (tx, ty, tx, ty - 40);
    check (r.inst.get ("mod04.amount") > 40.0f, "dragging the modulation tab changes mod04.amount (" + std::to_string (r.inst.get ("mod04.amount")) + ")");
    // the long press opens the modulations page
    r.openFull();
    int x, y;
    r.centre (r.ui.full().basic.level, x, y);
    r.ui.touch (ev (TouchEvent::Down, x, y + kOY), r.stack, r.inst, r.now);
    r.now += 700.0;
    r.ui.tick (r.now, r.stack, r.inst);
    r.ui.touch (ev (TouchEvent::Up, x, y + kOY), r.stack, r.inst, r.now);
    r.refresh();
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageMods, "a long press on a knob opens its modulations page");
    check (r.ui.modsPage().numRows() == 1, "the page lists the one modulation");
    check (r.inst.q[0] == Inst::idx ("mod04.amount") && r.inst.q[1] == Inst::idx ("mod04.bipolar") && r.inst.q[2] == Inst::idx ("mod04.bypass"), "the page's Q group is the amount, polarity and bypass of the slot");
    // bypass toggle
    ui::ToggleIcon& by = r.ui.modsPage().rows[0].bypass;
    r.tapC (by);
    check (r.inst.get ("mod04.bypass") == 1.0f, "the bypass toggle writes mod04.bypass");
    ui::ToggleIcon& po = r.ui.modsPage().rows[0].polarity;
    r.tapC (po);
    check (r.inst.get ("mod04.bipolar") == 0.0f, "the polarity toggle writes mod04.bipolar");
    // add a source
    r.tapC (r.ui.modsPage().add);
    check (r.ui.modsPage().numRows() == 2, "the add row adds a second modulation (a free slot got the source, destination LEVEL)");
    // remove the first
    r.tapC (r.ui.modsPage().rows[1].remove);   // the added one took the free slot 0 (row 0), the old modulation is row 1
    check (r.inst.get ("mod04.source") == 0.0f && r.ui.modsPage().numRows() == 1, "the remove button frees the slot");
}

void testDrawsOneSceneInsideTheCanvas()
{
    for (int depth = 0; depth < 3; ++depth)
        for (int sel = 1; sel <= 5; ++sel)
        {
            Rig r;
            r.inst.sel = sel;
            r.refresh();
            if (depth >= 1) r.openFull();
            if (depth == 2) r.tapC (r.ui.full().route);
            auto data = std::make_unique<gpu::SceneData>();
            std::memset (data.get(), 0, sizeof (gpu::SceneData));
            gpu::SceneBuilder sb (*data);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.ui.draw (sb);
            check (! sb.overflow && data->numOps > 20, "draw depth " + std::to_string (depth) + " instance " + std::to_string (sel) + ": one scene (" + std::to_string (data->numOps) + " ops)");
            std::vector<uint32_t> cv;
            renderScene (*data, cv);
            check (cv.size() == 1280u * 800u, "the CPU renderer draws it");
        }
}

void testQGroupFollowsTheActiveGroup()
{
    Rig r;
    r.openFull();
    const int calls = r.inst.qCalls;
    // tap the first tune field: the Tune group becomes active
    r.tapC (r.ui.full().tune.fin);
    check (r.inst.q[0] == QRouter::encode (Inst::idx ("osc1.transpose"), QRouter::kOct) && r.inst.q[1] == QRouter::encode (Inst::idx ("osc1.transpose"), QRouter::kSem) && r.inst.q[2] == Inst::idx ("osc1.tune") && r.inst.q[3] == Inst::idx ("osc1.pitch_coarse"), "a touch on FIN makes the Tune group active: OCT, SEM (both transpose), FIN, CRS");
    check (r.inst.qCalls > calls, "the group was handed to the instance");
    const int c2 = r.inst.qCalls;
    r.refresh();
    r.refresh();
    check (r.inst.qCalls == c2, "an unchanged group is not handed over again");
    r.tapC (*r.ui.full().panel.part (ui::PanelSlot::WtPos));
    check (r.inst.q[0] == Inst::idx ("osc1.frame") && r.inst.q[1] == Inst::idx ("osc1.unison.voices") && r.inst.q[2] == Inst::idx ("osc1.unison.detune") && r.inst.q[3] == Inst::idx ("osc1.unison.blend"), "the Unison group: frame, voices, detune, blend");
    r.tapC (r.ui.full().panel.warp()->first.amount);
    check (r.inst.q[0] == Inst::idx ("osc1.warp.type") && r.inst.q[1] == Inst::idx ("osc1.warp.amount") && r.inst.q[2] == Inst::idx ("osc1.warp2.type") && r.inst.q[3] == Inst::idx ("osc1.warp2.amount"), "the Warp group");
    // on the Env sub-tab the compact view does not own the group
    Rig e;
    const int c0 = e.inst.qCalls;
    e.inst.sel = 3;
    e.refresh (2);
    check (e.inst.qCalls == c0, "on the Env sub-tab the oscillator UI does not set the Q group in main");
}

void testSourceAndWarpLists()
{
    Rig r;
    r.openFull();
    // the source dropdown opens the list of the wavetables; the stepper's name does too, its outer quarters step
    ui::MenuButton& drop = r.ui.full().basic.source.dropdown;
    check (drop.enabled() && ! r.ui.compact().basic.source.dropdown.enabled(), "the source dropdown is enabled on the full screen (wavetable type), not on the compact view");
    r.tapC (drop);
    check (r.stack.top() == OscUi::kPageList && r.ui.list().state.count == 15, "the dropdown opens the list of the 15 wavetables");
    const ui::Rect row = r.ui.list().rowRect (r.ui.list().firstRow());
    r.tap (row.x + 30, row.y + 20);
    check (r.inst.get ("osc1.wavetable") == static_cast<float> (r.ui.list().firstRow()) && r.stack.depth() == 1, "a tap on a row writes osc1.wavetable");
    ui::Stepper& st = r.ui.full().basic.source.stepper;
    const ui::Rect sr = st.rect();
    r.tap (sr.x + sr.w / 2, sr.y + sr.h / 2);
    check (r.stack.top() == OscUi::kPageList, "a tap on the name of the source stepper opens the list as well");
    r.tap (20, 130);
    const float w0 = r.inst.get ("osc1.wavetable");
    r.tap (sr.x + sr.w - 8, sr.y + sr.h / 2);
    check (r.inst.get ("osc1.wavetable") == w0 + 1.0f && r.stack.depth() == 1, "the right quarter still steps to the next wavetable");
    // the warp type steppers of the wavetable panel: the name opens the list
    ui::WarpPair* wp = r.ui.full().panel.warp();
    const ui::Rect wr = wp->first.type.rect();
    r.tap (wr.x + wr.w / 2, wr.y + wr.h / 2);
    check (r.stack.top() == OscUi::kPageList && r.ui.list().state.count == 99, "the name of a warp type stepper opens the list of the warp types");
    r.tap (20, 130);
    // the other types have no source list
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    check (r.ui.full().basic.source.dropdown.enabled(), "multisample: the dropdown of the source opens the content browser");
    // drawing with the list open
    r.inst.set ("osc1.type", 0.0f);
    r.refresh();
    r.tapC (r.ui.full().basic.source.dropdown);
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps < 640, "the full screen with the list open is one scene (" + std::to_string (data->numOps) + " ops)");
}

void testGranularAndSubAndNoise()
{
    Rig r;
    r.inst.set ("osc1.type", 2.0f);
    r.refresh();
    r.openFull();
    auto& p = r.ui.full().panel;
    r.inst.set ("osc1.gran.rand_pitch", 12.0f);
    r.refresh();
    check (p.knob (ui::PanelSlot::Pitch)->state.value > 0.2, "PITCH reads osc1.gran.rand_pitch");
    check (r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::Scan))->param == Inst::idx ("osc1.gran.position"), "SCAN = osc1.gran.position");
    check (r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::Offset))->param == Inst::idx ("osc1.gran.rand_offset") && r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::Dir))->param == Inst::idx ("osc1.gran.rand_dir"), "OFFSET and DIR = gran.rand_offset, gran.rand_dir");
    // the warp menu opens the list of the warp types; a tap on a row writes it
    r.inst.set ("osc1.warp.type", 3.0f);
    r.refresh();
    r.tapC (*p.menu());
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageList, "the WARP menu button opens the list popup");
    check (r.ui.list().state.count == 99 && r.ui.list().state.current == 3 && r.ui.list().state.items != nullptr, "the list holds the 99 warp types of the engine, the current one is 3");
    {
        const ui::Rect row = r.ui.list().rowRect (r.ui.list().firstRow() + 1);
        r.tap (row.x + 30, row.y + 20);
    }
    check (r.inst.get ("osc1.warp.type") == static_cast<float> (r.ui.list().firstRow() + 1) && r.stack.depth() == 1, "a tap on a row writes osc1.warp.type and closes the list");
    r.tapC (*p.menu());
    r.tap (20, 130);
    check (r.stack.depth() == 1, "a tap on the backdrop closes the list without a write");
    // sub
    Rig s;
    s.inst.sel = 1;
    s.refresh();
    s.openFull();
    check (s.ui.full().panel.preset() == &ui::presets::kEmptyFull, "sub: an empty bottom band");
    s.inst.set ("sub.phase", 50.0f);
    s.refresh();
    check (static_cast<ui::NumberField*> (s.ui.item (OscUi::kBarSlot)->c)->state.value == 180.0, "sub phase 50 % shows as 180 degrees");
    s.inst.set ("sub.tune", 7.0f);
    s.refresh();
    check (s.ui.full().tune.crs.state.value == 7.0, "sub CRS = sub.tune");
}
}   // namespace

void testSteepEdgeNoLongLine()
{
    // a saw polyline (steep flanks) in a 300 x 200 canvas: nothing is drawn far above or below the polyline's own extent (the CPU renderer used to stretch the line's reach by the slope)
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 300, 200);
    float rows[512];
    for (int i = 0; i < 512; ++i)
        rows[i] = 80.0f + 40.0f * (1.0f - 2.0f * static_cast<float> ((i * 6) % 512) / 512.0f);   // six teeth between rows 40 and 120
    sb.curve (rows, 512, 10, 280);
    sb.stroke (2.0f, 0xffffffffu);
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    int outside = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 300; ++x)
            if (cv[static_cast<size_t> (y) * 300 + x] != 0 && (y < 34 || y > 126))
                ++outside;
    check (outside == 0, "a steep polyline draws nothing outside its own extent (" + std::to_string (outside) + " pixels)");
}


/** A directory tree of the machine's sample library: a folder with WAV files and an SFZ, a hidden file, a text file, a nested folder. */
std::vector<std::string> g_libraries;   // the directories this run made (removed at the end)
std::string makeLibrary()
{
    char tmpl[] = "/tmp/sbovl-XXXXXX";
    const char* dir = ::mkdtemp (tmpl);
    const std::string root = dir != nullptr ? dir : "/tmp";
    if (dir != nullptr)
        g_libraries.push_back (root);
    auto touch = [&] (const std::string& rel) { std::ofstream (root + "/" + rel) << "x"; };
    ::mkdir ((root + "/Drums").c_str(), 0755);
    ::mkdir ((root + "/Drums/Kicks").c_str(), 0755);
    ::mkdir ((root + "/Pads").c_str(), 0755);
    touch ("Drums/kick 1.wav"); touch ("Drums/Snare.WAV"); touch ("Drums/notes.txt"); touch ("Drums/.hidden.wav"); touch ("Drums/Kicks/deep.wav");
    touch ("Pads/pad.sfz"); touch ("Pads/pad C3.wav");
    return root;
}

void testContentBrowser()
{
    const std::string root = makeLibrary();
    ContentBrowser b;
    b.setRoots ({ { "LIBRARY", root }, { "NOWHERE", root + "/missing" } });
    b.open (1);
    check (b.atRoots() && b.count() == 3 && b.rows()[0].text == "SOURCES" && b.rows()[1].text == "LIBRARY \xe2\x96\xb6", "browser: it starts at the list of the roots");
    check (b.tap (0) == ContentBrowser::Action::None, "browser: the title row at the roots does nothing");
    check (b.tap (1) == ContentBrowser::Action::Changed && ! b.atRoots() && b.count() == 3, "browser: a root opens its folders (Drums, Pads): " + std::to_string (b.count()));
    check (b.rows()[1].text == "Drums \xe2\x96\xb6" && b.rows()[2].text == "Pads \xe2\x96\xb6", "browser: the folders are sorted, a folder says so");
    check (b.tap (1) == ContentBrowser::Action::Changed && b.count() == 4 && b.rows()[1].text == "Kicks \xe2\x96\xb6" && b.rows()[2].text == "kick 1.wav" && b.rows()[3].text == "Snare.WAV", "browser: a sample browser lists the folders first, then the WAV files, not the text file or the hidden one");
    check (b.rows()[0].text.find ("Drums") != std::string::npos, "browser: the first row names the folder and goes up");
    check (b.tap (2) == ContentBrowser::Action::Chosen && b.chosen() == root + "/Drums/kick 1.wav", "browser: a file is chosen with its path");
    check (b.tap (0) == ContentBrowser::Action::Changed && b.count() == 3 && b.tap (0) == ContentBrowser::Action::Changed && b.atRoots(), "browser: the first row goes up, the last step is the list of the roots");
    b.open (2);
    b.tap (1); b.tap (2);   // LIBRARY, Pads
    check (b.count() == 3 && b.rows()[1].text == "pad C3.wav" && b.rows()[2].text == "pad.sfz", "browser: for an instrument the SFZ files and the WAV files are listed (" + std::to_string (b.count()) + ")");
    b.openAt (1, root + "/Drums/Kicks/deep.wav");
    check (! b.atRoots() && b.folder() == root + "/Drums/Kicks" && b.rowOf (root + "/Drums/Kicks/deep.wav") == 1, "browser: it opens in the folder of the file that was loaded last");
    check (b.tap (-1) == ContentBrowser::Action::None && b.tap (99) == ContentBrowser::Action::None, "browser: a row that is not there does nothing");
}

void testSourceBrowserInTheOscillatorUi()
{
    const std::string root = makeLibrary();
    Rig r;
    r.inst.roots = { { "LIBRARY", root } };
    r.inst.set ("osc1.type", 2.0f);   // granular
    r.refresh();
    const OscUi::Item* src = r.ui.item (OscUi::kBasic + 1);
    check (src != nullptr && src->c->enabled() && r.ui.compact().basic.source.stepper.listable && r.ui.compact().basic.source.dropdown.enabled(), "granular: the source of the compact view is enabled (a tap opens the browser)");
    check (std::string (r.ui.compact().basic.source.stepper.state.text) == "NO SAMPLE", "granular: no sample loaded: the source says NO SAMPLE");
    // from the compact view the dropdown opens the full screen and the list over it
    r.tapC (r.ui.compact().basic.source.dropdown);
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageList && r.ui.list().state.count == 2 && std::string (r.ui.list().state.items[0]) == "SOURCES", "granular: the dropdown opens the full screen and the list of the sources");
    // LIBRARY, then Drums, then the sample
    auto row = [&] (int i) { const ui::Rect q = r.ui.list().rowRect (i); r.tap (q.x + 30, q.y + q.h / 2); };
    row (1);
    check (r.ui.list().state.count == 3 && r.stack.top() == OscUi::kPageList, "granular: a tap on a root shows its folders, the popup stays");
    row (1);
    check (r.ui.list().state.count == 4 && r.stack.top() == OscUi::kPageList, "granular: a tap on a folder shows its files");
    row (2);
    check (r.inst.requests.size() == 1 && r.inst.requests[0].osc == 1 && r.inst.requests[0].kind == 1 && r.inst.requests[0].path == root + "/Drums/kick 1.wav" && r.stack.depth() == 1, "granular: a tap on a file asks the instance to load it as a sample of oscillator 1 and closes the list");
    r.inst.loadState = 1;
    r.refresh();
    check (std::string (r.ui.full().basic.source.stepper.state.text) == "LOADING ...", "granular: while it loads the source says LOADING ...");
    r.inst.loadState = 3;
    r.refresh();
    check (std::string (r.ui.full().basic.source.stepper.state.text) == "LOAD FAILED", "granular: a failed load is said");
    r.inst.loadState = 2;
    auto content = std::make_shared<synthyboy::StateContent>();
    synthyboy::ContentSample cs;
    cs.osc = 1; cs.ref.pack = "user.samples"; cs.ref.file = "abc/kick 1.wav"; cs.ref.name = "kick 1";
    content->samples.push_back (cs);
    r.inst.contentPtr = content;
    r.refresh();
    check (std::string (r.ui.full().basic.source.stepper.state.text) == "kick 1", "granular: the loaded sample's name is on the source");
    // the browser opens where the last file was
    r.tapC (r.ui.full().basic.source.dropdown);
    check (r.stack.top() == OscUi::kPageList && std::string (r.ui.list().state.items[0]).find ("Drums") != std::string::npos && r.ui.list().state.current == 2, "granular: the browser opens in the folder of the last file, on it");
    r.tap (20, 130);   // the backdrop closes
    check (r.stack.depth() == 1, "the backdrop closes the browser");
    // multisample: SFZ and WAV
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    r.tapC (r.ui.full().basic.source.dropdown);
    check (r.stack.top() == OscUi::kPageList, "multisample: the source opens the browser too");
    r.tap (20, 130);
    // no roots: nothing opens
    Rig none;
    none.inst.set ("osc1.type", 4.0f);
    none.refresh();
    none.tapC (none.ui.compact().basic.source.dropdown);
    check (none.stack.depth() == 0, "an instance without roots: the browser does not open");
}

int main()
{
    testSteepEdgeNoLongLine();
    testEveryControlHasAParameterOrIsDisabled();
    testNoiseBasicQ1IsNoiseSample();
    testBasicGroupsAndSelection();
    testFullScreenOpensAndCloses();
    testKnobDragWritesTheParameter();
    testPanBipolarAndPower();
    testTypeSelectorAndKinds();
    testTuneOctSem();
    testDisabledControls();
    testRoutePopup();
    testDetailPages();
    testNavigatingAwayClosesEverything();
    testModulations();
    testDrawsOneSceneInsideTheCanvas();
    testQGroupFollowsTheActiveGroup();
    testContentBrowser();
    testSourceBrowserInTheOscillatorUi();
    testGranularAndSubAndNoise();
    testSourceAndWarpLists();
    for (const std::string& d : g_libraries)
        if (d.compare (0, 11, "/tmp/sbovl-") == 0)
            (void) std::system (("rm -rf '" + d + "'").c_str());
    std::printf (g_fails ? "osc_ui_host: FAILED (%d)\n" : "osc_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
