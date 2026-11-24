// SynthyBoy - MPC overlay tests: the envelope UI (env_ui.h): the wiring of every control to the engine parameters (both directions, both units), the instance selector, the pages of the UiStack, the Q groups,
// the handles of the graph (a drag writes the parameters), the targets page (the modulation matrix seen from the source), the running dots and the drawing (one scene, inside the canvas).
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o env_ui_host shell-mpc/overlay/tests/env_ui_host.cpp <libsynthyboy_core.a> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../env_ui.h"
#include "../pages.h"
#include "../play_view.h"
#include "../scene_cpu.h"
#include "fakes.h"

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
    int playSelection (int g) const noexcept override { return g == 1 ? env : 1; }
    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1;
        router.setGroup (t, n, static_cast<int> (synthyboy::paramCount()));
        ++qCalls;
    }
    void setQVirtual (int id, float value) noexcept override { router.setVirtualValue (id, value); }
    bool takeQVirtual (int id, float& value) noexcept override { return router.takeVirtual (id, value); }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setPlaySelection (int g, int i) noexcept override { if (g == 1) env = i; ++selWrites; }
    void setNativeEnvSelector (const int* frames) noexcept override
    {
        std::copy_n (frames, nativeui::kEnvSelectorCells, nativeFrames.begin());
    }
    void setNativeEnvEditor (bool active, const nativeui::FxCell* cells) noexcept override { nativeEnv.publish (active ? 1 : 0, cells); }
    bool takeNativeEnvField (int field, const nativeui::FxCell& expected, float& value) noexcept override
    {
        return nativeEnv.take (field, expected, value);
    }
    void postNative (int field, float value)
    {
        const auto snap = nativeEnv.at (field);
        nativeEnv.report (snap);
        nativeEnv.post (snap, value);
    }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    uint64_t stateGeneration() const noexcept override { return generation; }
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }
    int env = 1, q[4] = { -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0;
    std::array<int, nativeui::kEnvSelectorCells> nativeFrames {};
    nativeui::NativeCellBank<nativeui::env::kFields> nativeEnv;
    QRouter router;
    bool fullOpen = false;
    uint64_t generation = 0;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

struct Rig
{
    EnvUi ui;
    Inst inst;
    UiStack stack;
    synthyboy::LiveValues live;
    bool haveLive = false;
    double now = 1000.0;
    Rig() { refresh(); }
    void refresh (int hl = 2) { now += 50.0; ui.update (inst, hl, stack, now, haveLive ? &live : nullptr); }
    void tap (int cx, int cy, int holdMs = 40)
    {
        ui.touch (ev (TouchEvent::Down, cx, cy), stack, inst, now);
        now += holdMs;
        ui.tick (now, stack, inst);
        ui.touch (ev (TouchEvent::Up, cx, cy), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void tapC (const ui::Component& c, int holdMs = 40) { tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs); }
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
    void dragHandle (ui::EnvGraph& g, int handle, int dx, int dy)
    {
        double hx, hy;
        g.handlePoint (handle, hx, hy);
        drag (static_cast<int> (std::lround (hx)), static_cast<int> (std::lround (hy)), static_cast<int> (std::lround (hx)) + dx, static_cast<int> (std::lround (hy)) + dy);
    }
    void openFull() { tapC (ui.compact().full); }
};

std::string cellsText (const ui::UsedSelector& s)
{
    int c[ui::UsedSelector::kMaxCells];
    const int n = s.cells (c, ui::UsedSelector::kMaxCells);
    std::string t;
    for (int k = 0; k < n; ++k)
        t += std::string (k ? " " : "") + (c[k] == ui::UsedSelector::kPlus ? "+" : "[" + std::string (s.labelOf (c[k])) + "]");
    return t;
}

int envIdx (int n, const char* suffix) { return Inst::idx ("env" + std::to_string (n) + "." + suffix); }

void testEveryControlHasAParameter()
{
    for (int env = 1; env <= 6; ++env)
        for (float units : { 0.0f, 1.0f })
        {
            Rig r;
            r.inst.env = env;
            r.inst.set ("env" + std::to_string (env) + ".units", units);
            r.refresh();
            int unbound = 0;
            for (const EnvUi::Item& it : r.ui.items())
            {
                if (it.type == EnvUi::T::Inert || it.type == EnvUi::T::Graph || it.type == EnvUi::T::Select || it.type == EnvUi::T::Pick)   // Pick: the selector of the compact view (the Play Env selector, no parameter of its own)
                    continue;
                if (it.active (r.ui.beats()) < 0)
                    ++unbound;
            }
            check (unbound == 0 && r.ui.selection() == env && r.ui.beats() == (units > 0.5f),
                   "envelope " + std::to_string (env) + (units > 0.5f ? " in beats" : " in ms") + ": every control has a parameter, the selection follows Play Env (unbound " + std::to_string (unbound) + ", selection " + std::to_string (r.ui.selection()) + ", beats " + std::to_string (r.ui.beats()) + ")");
        }
}

void testSelectorWritesPlayEnv()
{
    Rig r;
    const ui::Rect c = r.ui.compact().instances.rect();
    const int writes = r.inst.writes;
    r.tap (c.x + c.w / 2, c.y + c.h / 2);
    check (r.inst.env == 1 && r.inst.writes == writes && r.stack.isMain(), "the host-owned compact ENV selector does not open a plugin menu or write");
    r.inst.env = 3;   // selection supplied by the native host control
    r.refresh();
    check (r.ui.selection() == 3, "the compact ENV follows the native selection");
    int ct[4];
    r.ui.compactTargets (ct);   // the Play view publishes the main state's groups (PlayView::publishQ): the envelope's compact group is what it asks for
    check (ct[0] == envIdx (3, "attack") && ct[1] == envIdx (3, "decay") && ct[2] == envIdx (3, "sustain") && ct[3] == envIdx (3, "release"),
           "the ADSR group follows the envelope: attack, decay, sustain, release of env3");
}

/** The Play page's carousel and the Env tab show the SAME list (used + selected + one "+"): two UIs on one instance, for an empty, a partly used and an all used preset. */
void testPlayCarouselIsTheTabSelector()
{
    struct Both
    {
        EnvUi tab, play;
        Inst inst;
        UiStack tabStack, playStack;
        double now = 1000.0;
        Both() { tabStack.openFull (EnvUi::kElementPage); refresh(); }
        void refresh() { now += 150.0; page::Input in; tab.update (inst, tabStack, now, in); play.update (inst, 2, playStack, now, nullptr); }
        void route (int slot, int source, const char* key)
        {
            char k[32];
            std::snprintf (k, sizeof k, "mod%02d.", slot);
            inst.set (std::string (k) + "source", static_cast<float> (source));
            inst.set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx (key)))));
            inst.set (std::string (k) + "amount", 30.0f);
            refresh();
        }
        std::string playText() const
        {
            std::string t;
            const uibind::SelectorCells& c = play.playCells();
            for (int k = 0; k < c.n; ++k)
                t += std::string (k ? " " : "") + (c.id[k] == uibind::SelectorCells::kPlusCell ? "+" : "[" + std::to_string (c.id[k]) + "]");
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
            return true;
        }
    };
    {
        Both b;
        check (b.sameAsTab() && b.playText() == "[1] +", "empty preset: the Play carousel and the tab show [1] + (" + b.playText() + ")");
        check (b.play.compact().instances.state.count == 2, "empty preset: the carousel has two entries, not six");
    }
    {
        Both b;
        b.route (1, 2, "master.volume");
        b.inst.set ("env5.decay", 900.0f);
        b.refresh();
        check (b.sameAsTab() && b.playText() == "[1] [2] [5] +", "partly used (ENV 2 routed, ENV 5 edited): the Play carousel and the tab show [1] [2] [5] + (" + b.playText() + ")");
        check (b.play.compact().instances.state.count == 4 && std::string (b.play.compact().instances.nameOf (2)) == "ENV 5" && std::string (b.play.compact().instances.nameOf (3)) == "+", "partly used: the carousel's entries are ENV 1, ENV 2, ENV 5, +");
        b.inst.env = 3;   // an unused, unedited envelope is the selected one: shown while it is selected, on the Play page too
        b.refresh();
        check (b.sameAsTab() && b.playText() == "[1] [2] [3] [5] +", "a selected unused envelope is shown on both (" + b.playText() + ")");
    }
    {
        Both b;
        for (int n = 2; n <= 6; ++n)
            b.route (n, n, "master.volume");
        check (b.sameAsTab() && b.playText() == "[1] [2] [3] [4] [5] [6]" && b.play.compact().instances.state.count == 6, "all used: six entries, no + on both (" + b.playText() + ")");
    }
    // the tab was left with an unused selected envelope: it falls back to the last used one (a tab leaves the screen: onHidden -> leftTab)
    {
        Both b;
        b.route (1, 2, "master.volume");
        b.inst.env = 5;   // a "+" pick the user did not edit
        b.refresh();
        const int w0 = b.inst.writes;
        b.tab.leftTab (b.inst);
        check (b.inst.env == 2 && b.inst.writes == w0, "leaving the tab with ENV 5 selected (unused, unedited): back to the last used one, ENV 2; no parameter is written");
        b.refresh();
        b.inst.set ("env5.decay", 900.0f);
        b.inst.env = 5;
        b.refresh();
        b.tab.leftTab (b.inst);
        check (b.inst.env == 5, "an edited envelope stays selected when the tab is left");
        b.inst.env = 2;
        b.refresh();
        b.tab.leftTab (b.inst);
        check (b.inst.env == 2, "a used envelope stays selected when the tab is left");
    }
}

void testCompactQGroupOnlyOnTheEnvSubTab()
{
    Rig r;
    int ct[4];
    r.ui.compactTargets (ct);
    check (ct[0] == envIdx (1, "attack") && ct[3] == envIdx (1, "release"), "sub tab Env: the ADSR group of env1 is the group");
    Rig o;
    o.inst.qCalls = 0;
    o.refresh (1);
    check (o.inst.qCalls == 0 || o.inst.q[0] == -1, "another sub tab: the envelope does not publish its group");
    Rig m;
    m.inst.set ("env1.units", 1.0f);
    m.refresh();
    m.ui.compactTargets (ct);
    check (ct[0] == envIdx (1, "attack_beats") && ct[1] == envIdx (1, "decay_beats") && ct[2] == envIdx (1, "sustain") && ct[3] == envIdx (1, "release_beats"),
           "in beats the group is attack_beats, decay_beats, sustain, release_beats");
}

void testFullScreenGroups()
{
    Rig r;
    r.openFull();
    r.ui.setNativeADSR (false);
    check (r.stack.depth() == 1 && r.stack.element() == EnvUi::kElementEnv && r.inst.fullOpen, "the full-screen button opens the full screen of the envelope element and tells the plugin");
    check (r.inst.q[0] == envIdx (1, "attack") && r.ui.activeQ() == 0, "the ADSR group is active first");
    r.tapC (r.ui.full().aCurve);
    check (r.ui.activeQ() == 1 && r.inst.q[0] == envIdx (1, "attack_curve") && r.inst.q[1] == envIdx (1, "decay_curve") && r.inst.q[2] == envIdx (1, "release_curve") && r.inst.q[3] == -1,
           "a touch on a curve knob makes the SHAPE group the active one: the three curves");
    r.tapC (r.ui.full().hold);
    check (r.ui.activeQ() == 2 && r.inst.q[0] == envIdx (1, "delay") && r.inst.q[1] == envIdx (1, "hold") && r.inst.q[2] == envIdx (1, "units") && r.inst.q[3] == envIdx (1, "legato_inverted"),
           "the TIME group: delay, hold, units, legato");
    // a tap on the background of a block makes it active
    r.tap (r.ui.full().block (1).x + 10, r.ui.full().block (1).y + 10);
    check (r.ui.activeQ() == 1, "tap on SHAPE block background: SHAPE is active group");
    r.tap (r.ui.full().block (0).x + 10, r.ui.full().block (0).y + 10);
    check (r.ui.activeQ() == 0, "tap on ADSR block background: ADSR is active group");
    // opening a detail page and coming back restores default group ADSR
    r.tap (r.ui.full().block (1).x + 10, r.ui.full().block (1).y + 10);
    check (r.ui.activeQ() == 1, "active group is SHAPE before targets");
    r.tapC (r.ui.full().targets);
    check (r.ui.topPage() == EnvUi::kPageTargets, "targets page opened");
    r.tapC (r.ui.targetsPage().back);
    check (r.ui.topPage() == 0 && r.ui.activeQ() == 0 && r.inst.q[0] == envIdx (1, "attack"), "coming back from targets restores default group ADSR");
    r.tapC (r.ui.full().close);
    check (r.stack.isMain() && ! r.inst.fullOpen, "close: main again, the plugin is told");
    r.openFull();
    r.ui.setNativeADSR (false);
    r.stack.closeAll();
    r.refresh();
    check (! r.inst.fullOpen, "the stack closed from outside (the page left): the plugin is told");
    // another element's page is open: this UI is idle and does not take the host widgets back
    Rig o;
    o.stack.openFull (1);
    o.ui.update (o.inst, 2, o.stack, o.now + 50.0, nullptr);
    check (o.ui.isMain() && ! o.ui.covers (100, 300) && o.ui.covers (600, 300), "the stack of another element: this UI is main (and covers its third only)");
}

void testKnobsAndFieldsWriteParameters()
{
    Rig r;
    r.openFull();
    r.ui.setNativeADSR (false);
    ui::Knob& k = r.ui.full().decay;
    const float before = r.inst.get ("env1.decay");
    const int cx = k.rect().x + k.rect().w / 2, cy = k.rect().y + k.rect().h / 2;
    r.drag (cx, cy, cx, cy - 40);
    check (r.inst.get ("env1.decay") > before && r.inst.lastWritten == envIdx (1, "decay"), "drag up on DECAY raises env1.decay (" + std::to_string (before) + " -> " + std::to_string (r.inst.get ("env1.decay")) + ")");
    // the other direction
    r.inst.set ("env1.decay", 1000.0f);
    r.refresh();
    check (std::fabs (k.state.value - synthyboy::toNormalised (synthyboy::paramTable()[envIdx (1, "decay")], 1000.0f)) < 1.0e-6, "a parameter that changed behind the UI shows on the knob");
    check (std::string (k.state.valueText) == "1.00 s", "the knob says its value with the unit: " + std::string (k.state.valueText));
    // the sustain knob: a drag up raises the level
    ui::Knob& sk = r.ui.full().sustain;
    r.inst.set ("env1.sustain", 50.0f);
    r.refresh();
    const int fx = sk.rect().x + sk.rect().w / 2, fy = sk.rect().y + sk.rect().h / 2;
    r.drag (fx, fy, fx, fy - 20);
    check (r.inst.get ("env1.sustain") > 50.0f, "drag up on the SUSTAIN knob raises it (" + std::to_string (r.inst.get ("env1.sustain")) + ")");
    // the legato switch
    const float legato = r.inst.get ("env1.legato_inverted");
    r.tapC (r.ui.full().legato);
    check (r.inst.get ("env1.legato_inverted") != legato, "the LEGATO switch writes env1.legato_inverted");
}

void testBeatsSwitch()
{
    Rig r;
    r.openFull();
    r.ui.setNativeADSR (false);
    check (! r.ui.beats() && std::string (r.ui.full().attack.state.valueText).find ("ms") != std::string::npos, "milliseconds: the ATTACK knob says ms");
    r.tapC (r.ui.full().beats);
    check (r.inst.get ("env1.units") == 1.0f && r.ui.beats(), "the BEATS switch writes env1.units");
    ui::Knob& k = r.ui.full().attack;
    const int cx = k.rect().x + k.rect().w / 2, cy = k.rect().y + k.rect().h / 2;
    r.inst.lastWritten = -1;
    r.drag (cx, cy, cx, cy - 40);
    check (r.inst.lastWritten == envIdx (1, "attack_beats"), "in beats the ATTACK knob writes env1.attack_beats");
    check (std::string (k.state.valueText).find ("beat") != std::string::npos, "and says it in beats: " + std::string (k.state.valueText));
}

void testGraphHandlesWriteParameters()
{
    Rig r;
    r.openFull();
    ui::EnvGraph& g = r.ui.full().graph;
    r.inst.set ("env1.attack", 900.0f); r.inst.set ("env1.decay", 900.0f); r.inst.set ("env1.release", 900.0f);   // stages wide enough for their curve handles (a stage under 30 pixels has none)
    r.refresh();
    // the sizes of the touch targets
    ui::Rect t[8];
    const int n = g.touchTargets (t, 8);
    bool big = n == 8;
    for (int i = 0; i < n; ++i)
        big = big && t[i].w >= 56 && t[i].h >= 56;
    check (big, "the eight handles have touch targets of at least 56 x 56 pixels");
    // the labels under the axis: the decay TIME under DECAY and the sustain LEVEL under SUSTAIN, not run together
    r.inst.set ("env1.decay", 400.0f);
    r.inst.set ("env1.sustain", 55.0f);
    r.refresh();
    check (std::string (g.state.decayTime) == "400 ms" && std::string (g.state.sustainText) == "55%" && std::string (g.state.text[ui::envgraph::Decay]).find ("55%") != std::string::npos,
           "under the axis DECAY says " + std::string (g.state.decayTime) + " and SUSTAIN says " + std::string (g.state.sustainText) + " (the handle's own bubble keeps both: " + g.state.text[ui::envgraph::Decay] + ")");
    // the end of the attack: a drag to the right makes the attack longer
    const float a0 = r.inst.get ("env1.attack");
    r.dragHandle (g, ui::envgraph::Attack, 60, 0);
    check (r.inst.get ("env1.attack") > a0 && r.inst.lastWritten == envIdx (1, "attack"), "drag of the attack handle to the right: env1.attack grows (" + std::to_string (a0) + " -> " + std::to_string (r.inst.get ("env1.attack")) + ")");
    // the end of the decay is two dimensional: time and sustain level
    const float d0 = r.inst.get ("env1.decay"), s0 = r.inst.get ("env1.sustain");
    r.dragHandle (g, ui::envgraph::Decay, 40, 60);
    check (r.inst.get ("env1.decay") > d0 && r.inst.get ("env1.sustain") < s0 - 5.0f, "drag of the decay handle right and down: decay longer, sustain lower (" + std::to_string (r.inst.get ("env1.sustain")) + ")");
    // the delay and the release
    r.dragHandle (g, ui::envgraph::Delay, 50, 0);
    check (r.inst.get ("env1.delay") > 0.0f, "drag of the delay handle: env1.delay grows");
    const float rel0 = r.inst.get ("env1.release");
    r.dragHandle (g, ui::envgraph::Release, -30, 0);
    check (r.inst.get ("env1.release") < rel0, "drag of the release handle to the left: env1.release shrinks");
    // a curve handle follows the finger: up makes the decay curve drop faster at the start (a higher middle is a bigger curve value)
    r.inst.set ("env1.sustain", 30.0f);
    r.refresh();
    const float c0 = r.inst.get ("env1.decay_curve");
    r.dragHandle (g, ui::envgraph::CurveD, 0, 40);
    check (r.inst.get ("env1.decay_curve") < c0, "drag of the decay curve handle down: the curve value goes down (" + std::to_string (c0) + " -> " + std::to_string (r.inst.get ("env1.decay_curve")) + ")");
    // the handle stays under the finger: its middle is at the row the finger ended on
    double hx, hy;
    g.handlePoint (ui::envgraph::CurveD, hx, hy);
    double sx, sy;
    r.refresh();
    g.handlePoint (ui::envgraph::CurveD, sx, sy);
    check (std::fabs (hy - sy) < 1.5, "the curve handle sits where the curve is after the write");
}

void testGraphViewStateIsThePages()
{
    Rig r;
    r.openFull();
    ui::EnvGraph& g = r.ui.full().graph;
    r.inst.set ("env1.attack", 900.0f); r.inst.set ("env1.decay", 900.0f); r.inst.set ("env1.release", 900.0f);
    r.refresh();
    check (g.state.selected == ui::envgraph::None && g.state.locked && g.state.zoom == 0.0, "the graph starts with no selected handle, locked, no zoom (the page's view state)");
    // a tap on a handle: the graph asks, the page keeps the selection and gives it back with the next update
    double hx, hy;
    g.handlePoint (ui::envgraph::Attack, hx, hy);
    r.tap (static_cast<int> (std::lround (hx)), static_cast<int> (std::lround (hy)), 20);
    check (g.state.selected == ui::envgraph::Attack, "a tap on the attack handle: the page selects it and the next update gives it to the graph");
    // a drag selects the handle that is dragged
    r.dragHandle (g, ui::envgraph::Release, -20, 0);
    check (g.state.selected == ui::envgraph::Release, "a drag of the release handle makes it the selected one");
    // LOCK and the zoom of the unlocked ruler are the page's too
    const ui::Rect lk = g.lockRect();
    r.tap (lk.x + lk.w / 2, lk.y + lk.h / 2);
    check (! g.state.locked && g.state.zoom > 0.0, "a tap on LOCK: the page unlocks and the zoom starts at the window the picture stood for (" + std::to_string (g.state.zoom) + " ms)");
    const double z0 = g.state.zoom;
    const ui::Rect zr = g.zoomRect();
    r.drag (zr.x + zr.w / 2, zr.y + zr.h / 2, zr.x + zr.w / 2 + 80, zr.y + zr.h / 2);
    check (g.state.zoom < z0 * 0.8, "a drag along the ruler zooms in: the page keeps the zoom (" + std::to_string (z0) + " -> " + std::to_string (g.state.zoom) + ")");
    r.refresh();
    check (! g.state.locked && g.state.zoom < z0 * 0.8, "the zoom and the unlocked state stay at every update");
    r.tap (lk.x + lk.w / 2, lk.y + lk.h / 2);
    check (g.state.locked, "a second tap on LOCK locks again");
}

void testDoubleTapResetsAHandle()
{
    Rig r;
    r.openFull();
    ui::EnvGraph& g = r.ui.full().graph;
    r.inst.set ("env1.release", 3000.0f);
    r.refresh();
    double hx, hy;
    g.handlePoint (ui::envgraph::Release, hx, hy);
    r.tap (static_cast<int> (hx), static_cast<int> (hy), 20);
    r.tap (static_cast<int> (hx), static_cast<int> (hy), 20);
    check (std::fabs (r.inst.get ("env1.release") - 200.0f) < 1.0f, "a double tap on the release handle: the default (200 ms), got " + std::to_string (r.inst.get ("env1.release")));
}

void testCompactGraphDragAndNoOverlapOfFullButton()
{
    Rig r;
    ui::EnvGraph& g = r.ui.compact().graph;
    const ui::Rect fb = r.ui.compact().full.rect();
    check (g.rect().contains (fb), "the full screen button lies in the corner of the graph");
    r.tapC (r.ui.compact().full);
    check (r.stack.depth() == 1, "a touch in the corner is the full screen button, not the graph");
    r.stack.closeAll();
    r.refresh();
    const float a0 = r.inst.get ("env1.attack");
    r.dragHandle (r.ui.compact().graph, ui::envgraph::Attack, 40, 0);
    check (r.inst.get ("env1.attack") > a0, "the handles of the compact graph can be dragged too");
}

void testTargetsPage()
{
    Rig r;
    r.openFull();
    const int slot0 = synthyboy::modSlotParams (0).source;
    check (r.inst.get ("mod01.source") == 0.0f && slot0 >= 0, "no modulation to start with");
    r.tapC (r.ui.full().targets);
    check (r.ui.topPage() == EnvUi::kPageTargets && r.ui.targetsPage().numRows() == 0, "the TARGETS button opens the targets page (no rows yet)");
    // add a target: ADD TARGET -> the group list -> a parameter of the group
    r.tapC (r.ui.targetsPage().add);
    check (r.ui.topPage() == EnvUi::kPageList && r.ui.list().state.count > 5, "ADD TARGET opens the list of the groups (" + std::to_string (r.ui.list().state.count) + ")");
    int groupRow = -1;
    for (int i = 0; i < r.ui.list().state.count; ++i)
        if (std::string (r.ui.list().state.items[i]) == "Filter 1")
            groupRow = i;
    check (groupRow >= 0, "a group named Filter 1 is in the list");
    // tap the row through the component
    auto tapRow = [&] (int row)
    {
        r.ui.list().showCurrent();
        const ui::Rect rr = r.ui.list().rowRect (row);
        r.tap (rr.x + rr.w / 2, rr.y + rr.h / 2);
    };
    if (groupRow >= 0)
    {
        // scroll the row into view
        for (int guard = 0; guard < 40 && r.ui.list().rowRect (groupRow).y + 56 > r.ui.list().rect().bottom(); ++guard)
        {
            const ui::Rect z = r.ui.list().rect();
            r.drag (z.x + 100, z.bottom() - 40, z.x + 100, z.y + 80);
        }
        tapRow (groupRow);
    }
    check (r.ui.list().state.header != nullptr && r.ui.list().state.count > 0, "a group opened: the list shows its parameters under a header");
    tapRow (0);
    check (r.ui.topPage() == EnvUi::kPageTargets && r.ui.targetsPage().numRows() == 1, "a parameter chosen: back on the targets page with one row");
    check (r.inst.get ("mod01.source") == 1.0f && r.inst.get ("mod01.destination") > 0.0f && std::fabs (r.inst.get ("mod01.amount") - 50.0f) < 0.5f, "slot 1 is env1 to the parameter at 50 %");
    // the row: depth, polarity, bypass, remove
    ui::ModTargetsPage& tp = r.ui.targetsPage();
    const ui::Rect d = tp.rows[0].depth.rect();
    r.drag (d.x + d.w / 2, d.y + d.h / 2, d.x + d.w / 2 + 40, d.y + d.h / 2);
    check (r.inst.get ("mod01.amount") > 50.0f, "drag of the depth bar raises the amount (" + std::to_string (r.inst.get ("mod01.amount")) + ")");
    r.tapC (tp.rows[0].bypass);
    check (r.inst.get ("mod01.bypass") == 1.0f, "the ON switch of the row bypasses the slot");
    r.tapC (tp.rows[0].remove);
    check (r.inst.get ("mod01.source") == 0.0f && r.ui.targetsPage().numRows() == 0, "X removes the slot");
    r.tapC (tp.back);
    check (r.ui.topPage() == 0 && r.stack.depth() == 1, "BACK is the full screen again");
}

void testModulatedKnobsShowTheirSource()
{
    Rig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (envIdx (1, "release")))));
    r.inst.set ("mod01.amount", 40.0f);
    r.now += 500.0;
    r.refresh();
    const ui::Knob& k = r.ui.compact().release;
    check (k.state.numMods == 1 && k.state.selected() != nullptr && std::fabs (k.state.selected()->depth - 0.4) < 1.0e-6, "a slot with env1 on env1.release: the RELEASE knob has the depth arc of the selected source (env1)");
    r.refresh();
    check (r.ui.numTargets() == 1, "the envelope counts its targets: 1");
    check (r.ui.full().numTargetLines == 1 && r.ui.full().tagsRect().w >= 200 && r.ui.full().graph.rect().w > 1200, "the full screen lists its target as a tag in the top bar (room " + std::to_string (r.ui.full().tagsRect().w) + " px) and the graph keeps the whole width");
    check (r.ui.compact().instances.hasTargets (0) && ! r.ui.compact().instances.hasTargets (1),
           "compact env selector: ENV 1 has target dot (hasTargets=true), ENV 2 has no mark (hasTargets=false)");
    check (cellsText (r.ui.selector()) == "[1] +", "full screen selector: ENV 1 (routed, always shown) and '+' (" + cellsText (r.ui.selector()) + ")");
    // add target for ENV 4 as well
    r.inst.set ("mod02.source", 4.0f);
    r.inst.set ("mod02.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (envIdx (1, "attack")))));
    r.inst.set ("mod02.amount", 25.0f);
    r.now += 500.0;
    r.refresh();
    check (r.ui.compact().instances.hasTargets (0) && r.ui.compact().instances.hasTargets (3) && ! r.ui.compact().instances.hasTargets (1),
           "ENV 1 and ENV 4 have target dots (targetMask " + std::to_string (r.ui.targetMask()) + ")");
    {
        Rig q;
        q.openFull();
        q.refresh();
        check (q.ui.numTargets() == 0 && q.ui.full().numTargetLines == 0 && q.ui.full().graph.rect().w > 1200, "no target: no tags, the graph takes the whole width (" + std::to_string (q.ui.full().graph.rect().w) + ")");
        check (q.ui.targetMask() == 0, "no envelope has targets: targetMask is 0, no dots or marks on any tab");
        for (int i = 0; i < 6; ++i)
            check (! q.ui.compact().instances.hasTargets (i), "env " + std::to_string (i + 1) + " has no target dot");
        check (cellsText (q.ui.selector()) == "[1] +", "no envelope used: the full screen selector is [1] + (" + cellsText (q.ui.selector()) + ")");
    }
}

/** A control nothing modulates: its modulations page is the BOARD of every source; a tap on a chip makes the modulation of that control. */
void testModulationsBoard()
{
    Rig r;
    r.openFull();
    r.tapC (r.ui.full().release, 700);   // a long press on RELEASE
    ui::ModulationsPage& mp = r.ui.modsPage();
    check (r.ui.topPage() == EnvUi::kPageMods && mp.numRows() == 0 && mp.boardShown() && mp.board.numSources() == static_cast<int> (synthyboy::modSourceCount()), "a long press on RELEASE with no modulation: the board of every source");
    bool fits = mp.board.fits();
    for (int i = 0; i < mp.board.numSources(); ++i)
        fits = fits && mp.board.chipRect (i).h >= 56 && mp.rect().contains (mp.board.chipRect (i));
    check (fits, "the chips are >= 56 px and inside the page");
    const ui::Rect chip = mp.board.chipRectOfId (uibind::lfoSource (2));
    r.tap (chip.x + chip.w / 2, chip.y + chip.h / 2);
    int found = 0, dest = -1;
    for (int sl = 0; sl < synthyboy::kNumModSlots; ++sl)
        if (std::lround (r.inst.get (synthyboy::paramTable()[synthyboy::modSlotParams (sl).source].key)) == uibind::lfoSource (2))
        {
            ++found;
            dest = static_cast<int> (std::lround (r.inst.get (synthyboy::paramTable()[synthyboy::modSlotParams (sl).destination].key)));
        }
    check (found == 1 && dest == static_cast<int> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (envIdx (1, "release")))), "a tap on the LFO 2 chip makes the modulation LFO 2 -> env1.release");
    r.refresh();
    check (mp.numRows() == 1 && ! mp.boardShown(), "the row is on the page, the board is gone");
}

void testDotsFromTheVoices()
{
    Rig r;
    r.openFull();
    r.haveLive = true;
    r.live = synthyboy::LiveValues();
    r.live.envVoiceStage[0][0] = 4.0f; r.live.envVoiceFraction[0][0] = 0.5f;
    r.live.envVoiceStage[0][1] = 6.0f; r.live.envVoiceFraction[0][1] = 0.25f;
    r.live.envVoiceStage[0][2] = 0.0f;
    r.live.envVoiceStage[1][0] = 2.0f;   // another envelope's voice: not shown
    r.refresh();
    const ui::EnvGraph::State& s = r.ui.full().graph.state;
    check (s.numDots == 2 && s.dots[0].stage == 4 && s.dots[1].stage == 6, "two voices sound: two dots, the newest first");
    // an engine that fills only the fine read-out: the one dot
    r.live = synthyboy::LiveValues();
    r.live.envFineStage[0] = 3.0f;
    r.refresh();
    check (r.ui.full().graph.state.numDots == 1 && r.ui.full().graph.state.dots[0].stage == 3, "only envFineStage: one dot");
    r.live = synthyboy::LiveValues();
    r.live.envVoiceStage[0][0] = 7.0f;
    r.refresh();
    check (r.ui.full().graph.state.numDots == 0, "a finished envelope has no dot");
    // the ghost: ENV 1 behind another envelope
    r.inst.env = 3;
    r.refresh();
    check (r.ui.full().graph.state.ghost, "envelope 3: envelope 1 is the ghost behind it");
    r.inst.env = 1;
    r.refresh();
    check (! r.ui.full().graph.state.ghost, "envelope 1: no ghost");
}

void drawAll (const char* what, EnvUi& ui)
{
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    sb.clear (0u);
    if (ui.isMain())
        ui.drawCompact (sb);
    else
        ui.drawPage (sb);
    ui.drawLive (sb);
    ui.drawPopup (sb);
    check (! sb.overflow && scene->numOps > 20, std::string (what) + ": one scene (" + std::to_string (scene->numOps) + " ops)");
    std::vector<uint32_t> px;
    renderScene (*scene, px);
    // the compact view stays inside its third (plus the ground): nothing of it is drawn right of x 850 or left of x 430
    if (ui.isMain())
    {
        int outside = 0;
        for (int y = 0; y < 800; ++y)
            for (int x = 0; x < 1280; ++x)
                if ((x < 430 || x >= 850) && px[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] != 0u)
                    ++outside;
        check (outside == 0, std::string (what) + ": the compact view draws only inside its third (" + std::to_string (outside) + " pixels outside)");
    }
}

void testDrawing()
{
    for (int lvl : { 1, 2 })
    {
        ui::theme::setDetail (lvl);
        Rig r;
        r.haveLive = true;
        r.live.envVoiceStage[0][0] = 4.0f; r.live.envVoiceFraction[0][0] = 0.5f;
        r.inst.env = 3;
        r.refresh();
        const std::string d = lvl == 1 ? "full detail " : "reduced detail ";
        drawAll ((d + "compact").c_str(), r.ui);
        r.openFull();
        drawAll ((d + "full screen").c_str(), r.ui);
        r.tapC (r.ui.full().targets);
        drawAll ((d + "targets page").c_str(), r.ui);
        r.tapC (r.ui.targetsPage().add);
        drawAll ((d + "list").c_str(), r.ui);
    }
    ui::theme::setDetail (1);
}

void testHashChangesWithTheDots()
{
    Rig r;
    auto scratch = std::make_unique<gpu::SceneData>();
    const uint32_t a = r.ui.drawHash (*scratch);
    check (a == r.ui.drawHash (*scratch), "the hash of an unchanged picture is the same");
    r.haveLive = true;
    r.live.envVoiceStage[0][0] = 2.0f; r.live.envVoiceFraction[0][0] = 0.3f;
    r.refresh();
    const uint32_t b = r.ui.drawHash (*scratch);
    r.live.envVoiceFraction[0][0] = 0.6f;
    r.refresh();
    check (b != a && r.ui.drawHash (*scratch) != b, "a dot that appears and moves changes the picture");
}
// ---- the Play view with the envelope UI in it: the stack is shared by the elements
void testPlayViewRoutesTheElements()
{
    ovt::FakeInstance inst;
    // This routing test intentionally opens both navigation destinations; keep
    // their power fixtures explicit so an OFF default cannot disable the full
    // screen hit target and mask the routing assertion.
    inst.set ("osc1.on", 1.0f);
    inst.set ("osc2.on", 1.0f);
    inst.set ("sub.on", 1.0f);
    inst.playSel[1] = 3;
    synthyboy::LiveValues lv;
    PlayView pv (2, 10.0);
    UiStack st;
    ViewInput in;
    in.instance = &inst; in.io = &inst; in.ui = &st; in.live = &lv; in.nowMs = 0.0;
    pv.update (in);
    in.nowMs = 500.0;
    pv.update (in);
    check (pv.valid(), "the Play view has data");
    check (inst.qGroup[0] == inst.index ("env3.attack") && inst.qGroup[3] == inst.index ("env3.release"), "the envelope third is the active one: its Q group (the ADSR of env3) is on Q 1 .. 4");
    auto touch = [&] (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; in.nowMs += 60.0; return pv.onTouch (e, st, inst, in.nowMs); };
    auto tap = [&] (int x, int y) { const bool a = touch (TouchEvent::Down, x, y); touch (TouchEvent::Up, x, y); in.nowMs += 200.0; pv.update (in); return a; };
    // the full screen button of the envelope's compact view (the corner of its graph)
    EnvUi probe;
    const ui::Rect nativeAtk = probe.compact().attack.rect();
    const ui::Rect nativeDec = probe.compact().decay.rect();
    const ui::Rect native = probe.compact().sustain.rect();
    const ui::Rect nativeRel = probe.compact().release.rect();
    check (nativeAtk.x == 446 && nativeAtk.y == 496 && nativeAtk.w == 192 && nativeAtk.h == 108, "the Attack skin control occupies the compact knob rectangle");
    check (nativeDec.x == 642 && nativeDec.y == 496 && nativeDec.w == 192 && nativeDec.h == 108, "the Decay skin control occupies the compact knob rectangle");
    check (native.x == 446 && native.y == 620 && native.w == 192 && native.h == 108, "the Sustain skin control occupies the compact knob rectangle");
    check (nativeRel.x == 642 && nativeRel.y == 620 && nativeRel.w == 192 && nativeRel.h == 108, "the Release skin control occupies the compact knob rectangle");
    for (const ui::Rect& r : { nativeAtk, nativeDec, native, nativeRel })
    {
        check (! tap (r.x + 45, r.y + 54), "native env dial owns its Play touch");
        check (! pv.onWheel (r.x + 45, r.y + 54, 1, st, inst, in.nowMs), "native env dial owns its Play wheel");
    }
    std::vector<uint32_t> pixels (static_cast<size_t> (PlayView::kW) * PlayView::kH);
    const bool wasWide = ui::QGroup::wideDevice();
    ui::QGroup::setWideDevice (true);
    pv.render (pixels.data(), PlayView::kW, PlayView::kH);
    ui::QGroup::setWideDevice (wasWide);
    bool clearDial = true;
    for (const ui::Rect& r : { nativeAtk, nativeDec, native, nativeRel })
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x)
                clearDial &= pixels[static_cast<size_t> (y) * PlayView::kW + x] == 0u;
    check (clearDial, "the complete native dial interiors are transparent in the Play overlay");
    const ui::Rect fb = probe.compact().full.rect();
    check (tap (fb.x + fb.w / 2, fb.y + fb.h / 2) && st.depth() == 1 && st.element() == EnvUi::kElementEnv, "a tap on the envelope's full screen button opens the envelope element");
    check (pv.oscUi()->isMain(), "the oscillator UI does not take the envelope's full screen for its own");
    std::vector<uint32_t> cv (static_cast<size_t> (PlayView::kW) * PlayView::kH);
    pv.render (cv.data(), PlayView::kW, PlayView::kH);
    int transparent = 0;
    for (int y = EnvUi::kTabY; y < EnvUi::kTabY + EnvUi::kTabH; ++y)
        for (int x = 0; x < PlayView::kW; ++x)
            transparent += (cv[static_cast<size_t> (y) * PlayView::kW + static_cast<size_t> (x)] >> 24) != 255u;
    check (transparent == 0, "the envelope's full screen is opaque over the whole tab (" + std::to_string (transparent) + " transparent pixels)");
    check (inst.qGroup[0] == inst.index ("env3.attack"), "full screen: the ADSR group");
    // a touch in the oscillator's third goes to the envelope's page while it is open (nothing of the oscillator opens)
    touch (TouchEvent::Down, 200, 400);
    touch (TouchEvent::Up, 200, 400);
    check (st.depth() == 1 && st.element() == EnvUi::kElementEnv, "a touch in the oscillator's third does not open the oscillator while the envelope is open");
    // close, then the oscillator's own full screen button
    const ui::Rect cl = probe.full().close.rect();
    tap (cl.x + cl.w / 2, cl.y + cl.h / 2);
    check (st.isMain(), "CLOSE: main");
    OscUi* o = pv.oscUi();
    const ui::Rect ob = o->compact().full.rect();
    check (inst.get ("sub.on") >= 0.5f && o->compact().power.state.on && o->compact().full.enabled(), "oscillator fixture is ON and its full-screen route is enabled");
    tap (ob.x + ob.w / 2, ob.y + ob.h / 2);
    check (st.depth() == 1 && st.element() == OscUi::kElementOsc, "a tap on the oscillator's full screen button opens the oscillator element");
    pv.render (cv.data(), PlayView::kW, PlayView::kH);
    // a touch in the envelope's third while the oscillator is open is the oscillator's page, not the envelope's compact view
    touch (TouchEvent::Down, 640, 300);
    touch (TouchEvent::Up, 640, 300);
    check (st.element() == OscUi::kElementOsc, "the oscillator element stays open when the envelope's third is touched");
    // navigating away closes everything
    st.closeAll();
    in.nowMs += 100.0;
    pv.update (in);
    check (! inst.uiFullOpen.load(), "closed from outside: the host widgets come back");
}

// ---- the Play page: the carousels of the Env and LFO thirds are the tabs' selectors (used + selected + one "+"), the fourth Q group has an LFO MONO member only for a standard LFO
void testPlayPageSelectorsAndExtraGroup()
{
    ovt::FakeInstance inst;
    inst.playSel[0] = 2; inst.playSel[1] = 1; inst.playSel[2] = 1;
    synthyboy::LiveValues lv;
    PlayView pv (1, 10.0);
    UiStack st;
    ViewInput in;
    in.instance = &inst; in.io = &inst; in.ui = &st; in.live = &lv; in.nowMs = 0.0;
    auto run = [&] { for (int i = 0; i < 3; ++i) { in.nowMs += 150.0; pv.update (in); } };
    run();
    // empty preset: the carousel of the envelope has [ENV 1, +], the LFO's [LFO 1, +]; not six and fourteen
    check (pv.envUi()->compact().instances.state.count == 2 && pv.lfoUi()->compact().instance.state.count == 2, "Play page, empty preset: the envelope carousel has 2 entries (ENV 1, +), the LFO carousel 2 (LFO 1, +)");
    check (std::string (pv.envUi()->compact().instances.nameOf (1)) == "+" && std::string (pv.lfoUi()->compact().instance.nameOf (1)) == "+", "the second entry is the +");
    // partly used: LFO 3 routed, RND 2 edited
    inst.set ("mod01.source", 9.0f);   // LFO 3 (6 + 3)
    inst.set ("mod01.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (inst.index ("master.volume")))));
    inst.set ("mod01.amount", 30.0f);
    inst.set ("rlfo2.rate", 5.0f);
    run();
    {
        const uibind::SelectorCells& c = pv.lfoUi()->playCells();
        std::string t;
        for (int k = 0; k < c.n; ++k) t += std::string (k ? " " : "") + c.names[k];
        check (t == "LFO 1 LFO 3 RND 2 +", "Play page, partly used: the LFO list is LFO 1 (selected), LFO 3, RND 2, + (" + t + ")");
        check (pv.lfoUi()->playCells().same (pv.lfoUi()->playCells()) && pv.lfoUi()->tabSelector().numCells() == c.n, "the list is made from the tab's UsedSelector (the same number of cells)");
    }
    // the fourth Q group: [LFO SYNC, OSC POWER, LFO MONO, -]; a random LFO has no mono parameter: the member is empty by design
    {
        int t[4];
        pv.extraTargets (t);
        check (t[0] == inst.index ("lfo1.sync") && t[1] == inst.index ("osc1.on") && t[2] == inst.index ("lfo1.mono") && t[3] == -1, "standard LFO: the extra group is [LFO SYNC, OSC POWER, LFO MONO, -]");
        check (synthyboy::findParamByKey ("rlfo1.mono") == nullptr && synthyboy::findParamByKey ("rlfo2.mono") == nullptr && synthyboy::findParamByKey ("rlfo3.mono") == nullptr && synthyboy::findParamByKey ("rlfo4.mono") == nullptr, "the parameter table has no rlfoN.mono");
        for (int m = 1; m <= 4; ++m)
        {
            pv.lfoUi()->select (9 + m, inst);
            run();
            pv.extraTargets (t);
            check (pv.lfoUi()->random() && t[0] == inst.index (("rlfo" + std::to_string (m) + ".sync").c_str()) && t[1] == inst.index ("osc1.on") && t[2] == -1 && t[3] == -1, "RND " + std::to_string (m) + ": the extra group is [LFO SYNC, OSC POWER, -, -]: the MONO member exists only for a standard LFO");
        }
        pv.lfoUi()->select (2, inst);
        run();
        pv.extraTargets (t);
        check (! pv.lfoUi()->random() && t[2] == inst.index ("lfo3.mono"), "back on a standard LFO (LFO 3): the MONO member is back");
    }
}

// ---- ONE Play page: the groups of the three thirds on Q 1 .. 12 in reading order; the third that is touched becomes the active one (4 knob device: it moves to Q 1 .. 4, the others follow in reading order, wrapping);
// the 16 knob device: the slots stay where they are, whatever is touched
void testPlayPageQSlots()
{
    for (const bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        ovt::FakeInstance inst;
        inst.playSel[0] = 2; inst.playSel[1] = 3; inst.playSel[2] = 1;   // 2 = Osc A (osc1), Env 3, LFO 1
        synthyboy::LiveValues lv;
        PlayView pv (1, 10.0);
        UiStack st;
        ViewInput in;
        in.instance = &inst; in.io = &inst; in.ui = &st; in.live = &lv; in.nowMs = 0.0;
        pv.update (in);
        in.nowMs = 500.0;
        pv.update (in);
        const std::string dev = wide ? "16 knob device: " : "4 knob device: ";
        check (std::string (pv.name()) == "play1", dev + "the one Play view is named play1");
        check (inst.qGroup[0] == inst.index ("osc1.type") && inst.qGroup[2] == inst.index ("osc1.pan") && inst.qGroup[3] == inst.index ("osc1.level") && inst.qGroup[4] == inst.index ("env3.attack") && inst.qGroup[7] == inst.index ("env3.release")
                   && inst.qGroup[8] == inst.index ("lfo1.shape") && inst.qGroup[12] == inst.index ("lfo1.sync") && inst.qGroup[13] == inst.index ("osc1.on") && inst.qGroup[14] == inst.index ("lfo1.mono") && inst.qGroup[15] == -1,
               dev + "the oscillator's group on Q 1 .. 4, the envelope's on 5 .. 8, the LFO's on 9 .. 12, the fourth group [LFO SYNC, OSC POWER, LFO MONO, -] on 13 .. 16");
        auto touch = [&] (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; in.nowMs += 60.0; return pv.onTouch (e, st, inst, in.nowMs); };
        touch (TouchEvent::Down, 1000, 330);   // the LFO's third (a touch on its display)
        touch (TouchEvent::Up, 1000, 330);
        in.nowMs += 200.0;
        pv.update (in);
        check (pv.highlight() == 3, dev + "a touch in the LFO's third makes its group the active one");
        if (wide)
            check (inst.qGroup[0] == inst.index ("osc1.type") && inst.qGroup[4] == inst.index ("env3.attack") && inst.qGroup[8] == inst.index ("lfo1.shape") && inst.qGroup[12] == inst.index ("lfo1.sync"), dev + "the slots stay where they are under a touch");
        else
            check (inst.qGroup[0] == inst.index ("lfo1.shape") && inst.qGroup[4] == inst.index ("lfo1.sync") && inst.qGroup[5] == inst.index ("osc1.on") && inst.qGroup[8] == inst.index ("osc1.type") && inst.qGroup[12] == inst.index ("env3.attack"),
                   dev + "the LFO's group is on Q 1 .. 4, the fourth group, the oscillator's and the envelope's follow in reading order (wrapping)");
        touch (TouchEvent::Down, 640, 330);   // the envelope's third
        touch (TouchEvent::Up, 640, 330);
        in.nowMs += 200.0;
        pv.update (in);
        if (! wide)
            check (pv.highlight() == 2 && inst.qGroup[0] == inst.index ("env3.attack") && inst.qGroup[4] == inst.index ("lfo1.shape") && inst.qGroup[8] == inst.index ("lfo1.sync") && inst.qGroup[12] == inst.index ("osc1.type"), dev + "the envelope touched: its group on Q 1 .. 4, then the LFO's, the fourth group, then the oscillator's");
        const int envBase = wide ? 4 : 0;
        inst.set ("env4.units", 1.0f);
        inst.playSel[1] = 4;
        in.nowMs += 200.0;
        pv.update (in);
        check (inst.qGroup[envBase] == inst.index ("env4.attack_beats") && inst.qGroup[envBase + 1] == inst.index ("env4.decay_beats")
               && inst.qGroup[envBase + 2] == inst.index ("env4.sustain") && inst.qGroup[envBase + 3] == inst.index ("env4.release_beats"),
               dev + "selecting an envelope in BEATS publishes that instance's beat time fields and its sustain");
        inst.set ("env4.units", 0.0f);
        in.nowMs += 200.0;
        pv.update (in);
        check (inst.qGroup[envBase] == inst.index ("env4.attack") && inst.qGroup[envBase + 1] == inst.index ("env4.decay")
               && inst.qGroup[envBase + 2] == inst.index ("env4.sustain") && inst.qGroup[envBase + 3] == inst.index ("env4.release"),
               dev + "returning to milliseconds restores that instance's original time targets");
        // another oscillator and LFO on the page: the fourth group follows them (Sub and Noise have their own power parameters)
        inst.playSel[0] = playosc::kSub; inst.playSel[2] = 3;
        in.nowMs += 200.0;
        pv.update (in);
        in.nowMs += 200.0;
        pv.update (in);
        {
            int base = -1;
            for (int k = 0; k < 16; ++k) if (inst.qGroup[k] == inst.index ("lfo3.sync")) base = k;
            check (base >= 0 && inst.qGroup[base + 1] == inst.index ("sub.on") && inst.qGroup[base + 2] == inst.index ("lfo3.mono") && inst.qGroup[base + 3] == -1, dev + "the fourth group follows the Sub and LFO 3: [lfo3.sync, sub.on, lfo3.mono, -]");
        }
    }
    ui::QGroup::setWideDevice (false);
}

void testEnvelopeParameterCoverage()
{
    // Every single env<N>.* parameter in the engine parameter table must be reachable on the page
    // in some state (ms or beats) and be a member of some Q group.
    for (int env = 1; env <= 6; ++env)
    {
        const std::string pre = "env" + std::to_string (env) + ".";
        int tested = 0;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        {
            const synthyboy::ParamInfo& pi = synthyboy::paramTable()[i];
            if (std::strncmp (pi.key, pre.c_str(), pre.size()) != 0)
                continue;
            ++tested;
            bool foundInQ = false;
            for (float units : { 0.0f, 1.0f })
            {
                Rig r;
                r.inst.env = env;
                r.inst.set (pre + "units", units);
                r.refresh();
                r.openFull();
                for (int g = 0; g < ui::EnvFullScreen::kMaxGroups && ! foundInQ; ++g)
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
        check (tested == 16, "envelope " + std::to_string (env) + ": all 16 engine parameters checked");
    }
}

// ---- the ONE Env tab (step 2 of docs/ui-map/env-filter-lfo-plan.md): the page, the selector "used + one free", the shared selection, the Q-Links that follow it
struct PageRig
{
    std::unique_ptr<IView> page = pages::make ("env1", layout::kKindEnvUi, 1);
    EnvUi& ui = static_cast<pages::EnvPage&> (*page).ui();
    Inst inst;
    UiStack stack;
    double now = 1000.0;
    PageRig() { stack.openFull (page->pageElement()); refresh(); }
    void refresh()
    {
        ViewInput in;
        in.io = &inst;
        in.ui = &stack;
        in.nowMs = now += 150.0;
        page->update (in);
    }
    void tap (int cx, int cy)
    {
        const bool down = page->onTouch (ev (TouchEvent::Down, cx, cy), stack, inst, now);
        now += 40;
        page->onTick (stack, inst, now);
        const bool up = page->onTouch (ev (TouchEvent::Up, cx, cy), stack, inst, now);
        if (! down && ! up && stack.depth() == 1 && ui.full().targetsArea().contains (cx, cy))
            inst.postNative (nativeui::env::Targets, 1.0f);
        now += 100.0;
        refresh();
    }
    void tapC (const ui::Rect& c) { tap (c.x + c.w / 2, c.y + c.h / 2); }
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
    void dragHandle (ui::EnvGraph& g, int handle, int dx, int dy)
    {
        double hx, hy;
        g.handlePoint (handle, hx, hy);
        drag (static_cast<int> (std::lround (hx)), static_cast<int> (std::lround (hy)), static_cast<int> (std::lround (hx)) + dx, static_cast<int> (std::lround (hy)) + dy);
    }
    /** A tap on the cell whose label is `label` ("+" for the plus cell). */
    bool tapCell (const char* label)
    {
        const ui::UsedSelector& sel = ui.selector();
        int cs[ui::UsedSelector::kMaxCells];
        const int n = sel.cells (cs, ui::UsedSelector::kMaxCells);
        for (int k = 0; k < n; ++k)
            if (std::string (cs[k] == ui::UsedSelector::kPlus ? "+" : sel.labelOf (cs[k])) == label)
            {
                const ui::Rect cell = sel.cell (k, n);
                const int x = cell.x + cell.w / 2, y = cell.y + cell.h / 2;
                const int frame = inst.nativeFrames[k];
                const bool down = page->onTouch (ev (TouchEvent::Down, x, y), stack, inst, now);
                const bool up = page->onTouch (ev (TouchEvent::Up, x, y), stack, inst, now + 40.0);
                if (down || up || frame <= 0 || frame > nativeui::kEnvSelectorLastFrame)
                    return false;
                // The one visible native Button queues its six-way Env step.
                inst.postNative (nativeui::env::Selection, static_cast<float> ((frame - 1) / 5) / 5.0f);
                refresh();
                return true;
            }
        return false;
    }
    void route (int slot, int source, const char* key)
    {
        char k[32];
        std::snprintf (k, sizeof k, "mod%02d.", slot);
        inst.set (std::string (k) + "source", static_cast<float> (source));
        inst.set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx (key)))));
        inst.set (std::string (k) + "amount", 30.0f);
        refresh();
    }
};

void testEnvTab()
{
    // the 13 controls of the tab: selector, TARGETS, ADSR (4), SHAPE (3), DELAY, HOLD, BEATS, RETRIG; the graph carries the handles
    {
        PageRig r;
        int knobs = 0, toggles = 0;
        for (const EnvUi::Item& it : r.ui.items())
        {
            if (it.c == &r.ui.compact().attack || it.c == &r.ui.compact().decay || it.c == &r.ui.compact().sustain || it.c == &r.ui.compact().release)
                continue;   // the compact view of the Play page has its own copies
            knobs += it.type == EnvUi::T::Knob;
            toggles += it.type == EnvUi::T::Toggle;
        }
        check (knobs == 9 && toggles == 2 && r.ui.item (EnvUi::kTargets) != nullptr && r.ui.item (EnvUi::kInstances) != nullptr && r.ui.item (EnvUi::kGraph) != nullptr,
               "the Env tab has 13 controls: selector, TARGETS, 9 knobs, BEATS, RETRIG (" + std::to_string (knobs) + " knobs, " + std::to_string (toggles) + " toggles) and the graph");
        check (std::string (ui::EnvFullScreen::groupName (0)) == "ADSR" && std::string (ui::EnvFullScreen::groupName (1)) == "SHAPE" && std::string (ui::EnvFullScreen::groupName (2)) == "TIME",
              "three Q groups: ADSR, SHAPE, TIME (4 + 3 + 4 members, none of 5)");
        check (r.ui.full().legato.state.on == false && std::string (r.ui.full().legato.state.label) == "RETRIG", "U11: the switch of env.legato_inverted reads RETRIG (on = the envelope restarts on a legato note)");
        check (r.ui.full().beats.rect().w > 0 && r.ui.full().legato.rect().w > 0 && r.ui.full().groups[2].member (3) == &r.ui.full().legato, "TIME is [DELAY, HOLD, BEATS, RETRIG]");
    }
    // the selector for an empty, a partly used and an all used preset
    {
        PageRig r;
        check (cellsText (r.ui.selector()) == "[1] +" && r.ui.selector().state.selected == 1, "empty preset: [1] + , ENV 1 selected (" + cellsText (r.ui.selector()) + ")");
        const ui::Rect sr = r.ui.selector().rect();
        ui::Rect cells[ui::UsedSelector::kMaxCells];
        const int nt = r.ui.selector().touchTargets (cells, ui::UsedSelector::kMaxCells);
        bool big = nt == 2;
        for (int i = 0; i < nt; ++i)
            big = big && cells[i].h >= 56 && cells[i].w >= 56 && sr.contains (cells[i].x, cells[i].y);
        check (big, "the selector cells are >= 56 x 56 px and inside its rectangle (" + std::to_string (nt) + " cells)");
    }
    {
        PageRig r;
        r.route (1, 2, "master.volume");
        r.inst.set ("env5.decay", 900.0f);   // edited, no routing: counts as used
        r.refresh();
        check (cellsText (r.ui.selector()) == "[1] [2] [5] +", "partly used (ENV 2 routed, ENV 5 edited): [1] [2] [5] + (" + cellsText (r.ui.selector()) + ")");
        PageRig a;
        for (int n = 2; n <= 6; ++n)
            a.route (n, n, "master.volume");
        check (cellsText (a.ui.selector()) == "[1] [2] [3] [4] [5] [6]", "all used: six cells, no '+' (" + cellsText (a.ui.selector()) + ")");
        check (a.ui.selector().numCells() == 6 && ! a.ui.selector().hasPlus(), "all used: no free candidate");
    }
    // choosing writes nothing, "+" selects the lowest free envelope, an unused selected one disappears when another is chosen, its parameters stay
    {
        PageRig r;
        r.route (1, 2, "master.volume");
        const int w0 = r.inst.writes;
        const float decay5 = r.inst.get ("env5.decay");
        check (r.tapCell ("+"), "tap on '+'");
        check (r.inst.env == 3 && r.ui.selection() == 3, "'+' selects the lowest free envelope (ENV 3: ENV 1 and 2 are shown)");
        check (r.inst.writes == w0, "choosing writes NO parameter");
        check (cellsText (r.ui.selector()) == "[1] [2] [3] +", "the chosen unused envelope is shown while it is selected (" + cellsText (r.ui.selector()) + ")");
        r.tapCell ("1");
        check (r.inst.env == 1 && cellsText (r.ui.selector()) == "[1] [2] +", "selecting another one: the unused ENV 3 disappears again (" + cellsText (r.ui.selector()) + ")");
        check (r.inst.writes == w0 && r.inst.get ("env5.decay") == decay5, "nothing was written, the hidden envelopes keep their parameters");
        r.tapCell ("2");
        check (r.inst.env == 2, "a tap on a shown cell selects it (Play Env = 2, shared with the Play page)");
    }
    // the Q-Links, the controls and the display follow the selected envelope
    {
        PageRig r;
        r.route (1, 4, "master.volume");
        r.tapCell ("4");
        check (r.ui.selection() == 4 && r.inst.q[0] == envIdx (4, "attack") && r.inst.q[1] == envIdx (4, "decay") && r.inst.q[2] == envIdx (4, "sustain") && r.inst.q[3] == envIdx (4, "release"),
              "Q-Links follow the selected envelope: Q 1 .. 4 are attack, decay, sustain, release of env4");
        r.ui.full().setActiveGroup (2);
        r.refresh();
        check (r.inst.q[0] == envIdx (4, "delay") && r.inst.q[1] == envIdx (4, "hold") && r.inst.q[2] == envIdx (4, "units") && r.inst.q[3] == envIdx (4, "legato_inverted"), "the TIME group of env4: delay, hold, units, legato_inverted");
        r.inst.set ("env4.attack", 700.0f);
        r.refresh();
        check (std::fabs (r.ui.full().graph.state.shape.timeMs[1] - 700.0) < 1.0, "the display shows the selected envelope (ENV 4 attack 700 ms)");
        r.inst.writes = 0;
        r.tapC (r.ui.full().legato.rect());
        check (r.inst.writes == 0, "the overlay leaves the native RETRIG tap to the host");
        r.inst.writeParam (envIdx (4, "legato_inverted"), 1.0f);
        r.refresh();
        check (r.inst.get ("env4.legato_inverted") == 1.0f && r.inst.get ("env1.legato_inverted") == 0.0f && r.inst.writes == 1, "RETRIG writes env4.legato_inverted only");
        r.tapCell ("1");
        check (r.inst.q[0] == envIdx (1, "delay") || r.inst.q[0] == envIdx (1, "attack"), "back on ENV 1: the Q-Links point at env1");
    }
    // the selection is the Play page's selection (the data model is shared): a choice on the Play page shows on the tab and the other way round
    {
        PageRig r;
        r.inst.env = 5;
        r.refresh();
        check (r.ui.selection() == 5 && cellsText (r.ui.selector()) == "[1] [5] +", "Play Env = 5 (set on the Play page): the tab shows ENV 5 (" + cellsText (r.ui.selector()) + ")");
        r.tapCell ("1");
        check (r.inst.env == 1, "a choice on the tab is Play Env (the Play page follows)");
    }
    // the selector is laid out again with the number of its cells (the tags take the rest)
    {
        PageRig r;
        const int w1 = r.ui.selector().rect().w;
        for (int n = 2; n <= 6; ++n)
            r.route (n - 1, n, "master.volume");
        check (r.ui.selector().rect().w > w1 && r.ui.selector().rect().w >= 6 * 56 && r.ui.full().tagsRect().x >= r.ui.selector().rect().right(), "more cells: the selector grows (" + std::to_string (w1) + " -> " + std::to_string (r.ui.selector().rect().w) + " px), the tags move right");
    }
}

void testStandaloneEnvNativeADSR()
{
    PageRig r;
    check (r.ui.full().nativeADSR(), "the standalone Env page enables native ADSR");
    check (r.ui.nativeADSR(), "EnvUi reports native ADSR enabled");
    check (! r.ui.hostBackground(), "EnvUi starts in fallback mode before sentinel is verified");
    r.ui.setHostBackground (true);
    check (r.ui.hostBackground(), "EnvUi enables transparent host background for native dials when sentinel is verified");
    r.ui.setHostBackground (false);
    check (! r.ui.hostBackground(), "returning to fallback mode restores aperture rendering");

    const ui::Rect adsr = r.ui.full().adsrArea();
    check (! adsr.empty() && adsr.w >= 400 && adsr.h >= 100, "native ADSR area covers the bottom ADSR block knobs");
    check (! r.ui.full().attack.enabled(), "overlay attack knob is disabled");
    check (! r.ui.full().decay.enabled(), "overlay decay knob is disabled");
    check (! r.ui.full().sustain.enabled(), "overlay sustain knob is disabled");
    check (! r.ui.full().release.enabled(), "overlay release knob is disabled");

    const auto attack = r.ui.full().attack.rect();
    const int cx = attack.x + attack.w / 2, cy = attack.y + attack.h / 2;
    check (! r.ui.covers (cx, cy), "native ADSR interior is not covered by the overlay");
    check (! r.ui.touch (ev (TouchEvent::Down, cx, cy), r.stack, r.inst, r.now), "touch over native ADSR falls through to host");
    check (! r.ui.wheel (cx, cy, 1, r.stack, r.inst, r.now), "wheel over native ADSR falls through to host");

    const ui::Rect graph = r.ui.full().graph.rect();
    check (r.ui.covers (graph.x + graph.w / 2, graph.y + graph.h / 2), "the overlay still owns the graph");

    // Rendering transparency: verify the native ADSR dial interior is transparent in the standalone overlay
    std::vector<uint32_t> pixels (1280u * 800u);
    gpu::SceneData scratch;
    gpu::SceneBuilder sb (scratch);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    renderScene (scratch, pixels);
    check (pixels[cy * 1280 + cx] == 0u && pixels[cy * 1280 + attack.right() + 1] != 0u
           && r.ui.covers (attack.right() + 1, cy), "the dial aperture and the opaque gap have distinct input and alpha ownership");
}

void testStandaloneEnvNativeRemainingControls()
{
    PageRig r;
    check (r.ui.nativeADSR() && r.ui.nativeShape() && r.ui.nativeTime() && r.ui.nativeTargets(),
           "opening the normal Env page enables native ADSR, SHAPE, TIME and TARGETS artwork");
    check (r.ui.nativeInstanceSelector(), "the normal page gives the used/free selector to the native skin");

    // SHAPE knobs are disabled in overlay
    check (! r.ui.full().aCurve.enabled(), "overlay aCurve knob is disabled");
    check (! r.ui.full().dCurve.enabled(), "overlay dCurve knob is disabled");
    check (! r.ui.full().rCurve.enabled(), "overlay rCurve knob is disabled");

    // TIME controls are disabled in overlay
    check (! r.ui.full().delay.enabled(), "overlay delay knob is disabled");
    check (! r.ui.full().hold.enabled(), "overlay hold knob is disabled");
    check (! r.ui.full().beats.enabled(), "overlay beats toggle is disabled");
    check (! r.ui.full().legato.enabled(), "overlay legato toggle is disabled");

    // Bounds and fall-through hit testing
    const ui::Rect shape = r.ui.full().shapeArea();
    check (! shape.empty() && shape.w >= 200 && shape.h >= 100, "shape area covers SHAPE block knobs");
    const int sx = shape.x + shape.w / 2, sy = shape.y + shape.h / 2;
    check (! r.ui.covers (sx, sy), "native SHAPE interior is not covered by overlay");
    check (! r.ui.touch (ev (TouchEvent::Down, sx, sy), r.stack, r.inst, r.now), "touch over native SHAPE falls through to host");
    check (! r.ui.wheel (sx, sy, 1, r.stack, r.inst, r.now), "wheel over native SHAPE falls through to host");

    const ui::Rect time = r.ui.full().timeArea();
    check (! time.empty() && time.w >= 250 && time.h >= 100, "time area covers TIME block controls");
    const auto delay = r.ui.full().delay.rect();
    const int tx = delay.x + delay.w / 2, ty = delay.y + delay.h / 2;
    check (! r.ui.covers (tx, ty), "native TIME interior is not covered by overlay");
    check (! r.ui.touch (ev (TouchEvent::Down, tx, ty), r.stack, r.inst, r.now), "touch over native TIME falls through to host");
    check (! r.ui.wheel (tx, ty, 1, r.stack, r.inst, r.now), "wheel over native TIME falls through to host");

    const ui::Rect tgt = r.ui.full().targetsArea();
    check (tgt.x == 1072 && tgt.y == 122 && tgt.w == 160 && tgt.h == 56,
           "TARGETS matches the skin aperture and leaves the right detector marker clear");
    const int tgtx = tgt.x + tgt.w / 2, tgty = tgt.y + tgt.h / 2;
    check (! r.ui.covers (tgtx, tgty), "the host draws TARGETS artwork through the overlay aperture");
    check (! r.page->onTouch (ev (TouchEvent::Down, tgtx, tgty), r.stack, r.inst, r.now) && r.stack.depth() == 1,
           "TARGETS touch-down goes through to the native button");
    check (! r.page->onTouch (ev (TouchEvent::Up, tgtx, tgty), r.stack, r.inst, r.now + 40) && r.stack.depth() == 1,
           "the touch observer leaves the native button as the sole action owner");
    r.inst.postNative (nativeui::env::Targets, 1.0f);
    r.refresh();
    check (r.ui.topPage() == EnvUi::kPageTargets && r.stack.depth() == 2, "one native TARGETS action opens the selected envelope's detail");
    r.inst.postNative (nativeui::env::Targets, 1.0f);
    r.refresh();
    check (r.stack.depth() == 2 && ! r.inst.nativeEnv.layout(), "a repeated host action neither reopens the detail nor exposes native controls");
    check (std::all_of (r.inst.nativeFrames.begin(), r.inst.nativeFrames.end(), [] (int frame) { return frame == 0; }),
           "native selector buttons are disabled behind Targets");
    r.tapC (r.ui.targetsPage().back.rect());
    check (r.stack.depth() == 1 && r.ui.topPage() == 0, "one Back returns to the Env page");
    check (r.inst.nativeFrames[0] > 0, "returning restores the native selector cells");

    // The interactive curve REMAINS covered and functional
    const ui::Rect gr = r.ui.full().graph.rect();
    check (! gr.empty(), "graph area is non-empty");
    const int gx = gr.x + gr.w / 2, gy = gr.y + gr.h / 2;
    check (r.ui.covers (gx, gy), "graph interactive curve area remains covered by overlay");
    const int writesBefore = r.inst.writes;
    r.dragHandle (r.ui.full().graph, ui::envgraph::Attack, 50, 0);
    check (r.inst.writes > writesBefore, "interactive graph handles continue to edit parameters");

    // Standalone instance helpers
    check (std::string (EnvUi::instanceName (1)) == "ENV 1", "instanceName(1) is ENV 1");
    check (std::string (EnvUi::instanceName (6)) == "ENV 6", "instanceName(6) is ENV 6");
    check (EnvUi::choiceLabel (r.inst, 3) == "ENV 3", "choiceLabel(3) is ENV 3");
    r.ui.setSelection (r.inst, 5);
    check (r.inst.env == 5, "setSelection writes Play Env");
    r.refresh();
    check (r.ui.selection() == 5, "EnvUi selection follows native choice");

    // Rendering transparency: verify all native control areas are transparent in overlay
    std::vector<uint32_t> pixels (1280u * 800u);
    gpu::SceneData scratch;
    gpu::SceneBuilder sb (scratch);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    renderScene (scratch, pixels);

    auto checkTransparent = [&] (const ui::Rect& area) -> bool
    {
        for (int y = area.y; y < area.bottom(); ++y)
            for (int x = area.x; x < area.right(); ++x)
                if (pixels[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] != 0u)
                    return false;
        return true;
    };

    check (checkTransparent (r.ui.full().aCurve.rect()) && checkTransparent (r.ui.full().dCurve.rect()) &&
           checkTransparent (r.ui.full().rCurve.rect()), "the native SHAPE cells are transparent");
    check (checkTransparent (r.ui.full().delay.rect()) && checkTransparent (r.ui.full().hold.rect()) &&
           checkTransparent (r.ui.full().beats.rect()) && checkTransparent (r.ui.full().legato.rect()),
           "the native TIME cells are transparent");
    check (checkTransparent (tgt), "the complete native TARGETS interior is transparent");
    check (checkTransparent (r.ui.full().selectorArea()), "the overlay leaves native selection-cell artwork transparent");
}

void testNativeUsedSelector()
{
    PageRig r;
    check (r.inst.nativeFrames == std::array<int, 6>{ 3, 10, 0, 0, 0, 0 },
           "empty: selected ENV 1 has no dot, '+' selects ENV 2, and later slots are blank");
    const ui::Rect area = r.ui.full().selectorArea();
    check (area.x == 68 && area.y == 122 && area.w == 176 && area.h == 56,
           "the two visible cells align with the generated skin slots");
    r.route (1, 2, "master.volume");
    r.inst.set ("env5.decay", 900.0f);
    r.refresh();
    check (r.inst.nativeFrames == std::array<int, 6>{ 3, 7, 22, 15, 0, 0 },
           "partial use: active ENV 1, routed ENV 2 and edited ENV 5 get their states; '+' offers ENV 3");
    check (r.tapCell ("+") && r.inst.env == 3 && r.inst.writes == 0,
           "native '+' selects the lowest free envelope without changing the patch");
    check (r.inst.nativeFrames == std::array<int, 6>{ 1, 7, 13, 22, 20, 0 },
           "an unused selected ENV 3 is shown without a dot; '+' now offers ENV 4");
    const ui::Rect selected = r.ui.full().selectorArea();
    check (r.page->onWheel (selected.x + 8, selected.y + 24, 1, r.stack, r.inst, r.now) && r.inst.env == 5,
           "wheel over the native selector still steps only over the shown envelopes");
    r.refresh();
    check (r.inst.nativeFrames == std::array<int, 6>{ 1, 7, 24, 15, 0, 0 },
           "leaving an unedited selection drops its cell and retains the two used dots");
    for (int env : { 3, 4, 6 })
        r.route (env, env, "master.volume");
    check (r.inst.nativeFrames == std::array<int, 6>{ 1, 7, 12, 17, 24, 27 },
           "all used: six numbered states, no '+', and ENV 5 is selected and used");

    check (r.tapCell ("1"), "the native ENV 1 button returns to the amplitude envelope");
    Inst other;
    UiStack otherStack;
    otherStack.openFull (r.page->pageElement());
    ViewInput in;
    in.io = &other; in.ui = &otherStack; in.nowMs = r.now + 1.0;
    r.page->onHidden();
    check (r.inst.nativeFrames == std::array<int, 6>{}, "leaving clears the old plugin instance's native cells");
    r.page->onShown();
    r.page->update (in);
    check (other.nativeFrames == std::array<int, 6>{ 3, 10, 0, 0, 0, 0 },
           "an immediate instance switch with the same selected envelope refreshes the used flags");
    other.set ("env6.decay", 900.0f);
    ++other.generation;
    ++in.nowMs;
    r.page->update (in);
    check (other.nativeFrames == std::array<int, 6>{ 3, 27, 10, 0, 0, 0 },
           "a preset change refreshes used flags within the normal scan interval");
}

void testStandaloneEnvSelectorQ()
{
    for (bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        PageRig r;
        r.route (1, uibind::envSource (3), "master.volume");
        r.route (2, uibind::envSource (6), "filter1.cutoff");
        const int id = EnvUi::kVirtualSelector;
        int slot = 7;
        check (r.inst.router.virtualId (slot) == id, "ENV selector is Q8 in the SHAPE block");
        auto q = [&] (float x)
        {
            return r.inst.router.hostWrite (slot, x, false, [] (int) { return 0.0f; },
                [&] (int i, float n) { r.inst.writeParam (i, n); });
        };
        check (q (0.05f) == QRouter::Write::written, "the router keeps small absolute ENV dial movements");
        r.refresh();
        check (r.inst.env == 1 && std::fabs (r.inst.router.value (slot, [] (int) { return 0.0f; }) - 0.05f) < 1e-6f,
               "ENV sub-step travel is preserved without changing the selection");
        check (q (1.0f / 3.0f) == QRouter::Write::written, "the ENV virtual dial accepts a point on the used row");
        r.refresh();
        check (r.inst.env == 3 && r.ui.selection() == 3 && r.inst.router.target (0) == envIdx (3, "attack"),
               "the Q turn selects the second shown ENV and remaps ADSR in the same update");
        r.ui.full().setActiveGroup (2);
        r.refresh();
        slot = wide ? 7 : 11;
        check (r.inst.router.virtualId (slot) == id, "the ENV selector follows SHAPE bank rotation only on the Key 61");
        check (q (1.0f) == QRouter::Write::written, "the upper end includes the free selector cell");
        r.refresh();
        check (r.inst.env == 2 && r.inst.writes == 0, "the ENV '+' Q position selects the lowest free ENV without editing the patch");
        check (q (1.0f) == QRouter::Write::written, "queue a Q write just before opening Targets");
        r.tapC (r.ui.full().targetsArea());
        check (r.ui.topPage() == EnvUi::kPageTargets && r.inst.env == 2,
               "a Q write from the ENV selector cannot leak into Targets");
        r.tapC (r.ui.targetsPage().back.rect());
        check (r.inst.env == 2 && r.inst.router.virtualId (7) == id,
               "closing Targets restores the ENV selector");
        slot = 7;
        check (q (1.0f) == QRouter::Write::written, "queue an ENV selection just before a preset load");
        ++r.inst.generation;
        r.refresh();
        check (r.inst.env == 2, "the newly loaded selection ignores the prior preset's queued Q write");
    }
    ui::QGroup::setWideDevice (false);
}

void testStandaloneEnvNativeControlsAcrossInstancesAndPresets()
{
    PageRig r;
    r.ui.setHostBackground (true);
    check (r.ui.hostBackground(), "standalone ENV enables host background when sentinel is verified");
    check (r.ui.nativeADSR() && r.ui.nativeShape() && r.ui.nativeTime(), "native scalar groups are enabled by entering the actual Env page");

    // 1. Across all 6 instances: verify selection, labels, parameters and Q targets
    for (int env = 1; env <= 6; ++env)
    {
        r.ui.setSelection (r.inst, env);
        check (r.inst.env == env, "setSelection writes instance " + std::to_string (env));
        r.refresh();
        check (r.ui.selection() == env, "EnvUi selection follows native choice for env " + std::to_string (env));
        check (r.ui.currentChoiceLabel (r.inst) == "ENV " + std::to_string (env),
               "currentChoiceLabel matches ENV " + std::to_string (env));
        check (EnvUi::choiceLabel (r.inst, env) == "ENV " + std::to_string (env),
               "choiceLabel matches ENV " + std::to_string (env));

        // Q targets follow selected instance
        check (r.inst.q[0] == envIdx (env, "attack") &&
               r.inst.q[1] == envIdx (env, "decay") &&
               r.inst.q[2] == envIdx (env, "sustain") &&
               r.inst.q[3] == envIdx (env, "release"),
               "ADSR Q targets follow selected instance env" + std::to_string (env));
    }

    // Return to env 1
    r.ui.setSelection (r.inst, 1);
    r.refresh();

    // Modulation targets added: preset with multiple targets.
    for (int n = 2; n <= 6; ++n)
        r.route (n - 1, n, "master.volume");
    r.refresh();

    // Top tags must not overlap the native selector aperture
    const ui::Rect tags = r.ui.full().tagsRect();
    check (tags.x >= r.ui.full().selectorArea().right(), "tagsRect does not encroach on the live selector");

    // Beats unit preset change
    check (r.tapCell ("4") && r.inst.env == 4, "a normal selector gesture changes the edited envelope");
    r.inst.writeParam (envIdx (4, "units"), 1.0f);
    r.refresh();
    check (r.ui.beats() && r.inst.get ("env1.units") == 0.0f, "the selected envelope changes to beats without touching ENV 1");
    check (r.ui.qTargets()[0] == envIdx (4, "attack_beats") && r.ui.qTargets()[1] == envIdx (4, "decay_beats")
           && r.ui.qTargets()[2] == envIdx (4, "sustain") && r.ui.qTargets()[3] == envIdx (4, "release_beats"),
           "ADSR Q targets match the selected envelope's BEATS fields");
    check (r.ui.qTargets()[8] == envIdx (4, "delay_beats") && r.ui.qTargets()[9] == envIdx (4, "hold_beats"),
           "TIME Q targets switch to BEATS, too");
    const ui::Rect timeArea = r.ui.full().timeArea();
    check (! timeArea.empty() && timeArea.w >= 250, "timeArea remains valid in beats preset");
    const auto delay = r.ui.full().delay.rect();
    check (! r.ui.covers (delay.x + delay.w / 2, delay.y + delay.h / 2), "native DELAY is clear in the beats preset");

    // Advance state generation (preset reload simulation)
    const uint64_t nextGen = ++r.inst.generation;
    (void) nextGen;
    r.refresh();
    check (r.ui.nativeADSR() && r.ui.nativeShape() && r.ui.nativeTime() && r.ui.nativeTargets(), "preset reload preserves native controls");
    check (r.ui.hostBackground(), "preset reload preserves hostBackground");
    check (r.ui.selection() == 4, "preset reload preserves selection");

    // Verify complete transparency of all native apertures
    std::vector<uint32_t> pixels (1280u * 800u);
    gpu::SceneData scratch;
    gpu::SceneBuilder sb (scratch);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    renderScene (scratch, pixels);

    auto checkTransparent = [&] (const ui::Rect& area) -> bool
    {
        for (int y = area.y; y < area.bottom(); ++y)
            for (int x = area.x; x < area.right(); ++x)
                if (pixels[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] != 0u)
                    return false;
        return true;
    };

    for (const auto& cell : r.ui.full().nativeApertures())
        check (checkTransparent (cell), "each native cell remains transparent after a preset change");
}

void testEnvEditorHostsOnlyCurrentFields()
{
    namespace n = nativeui::env;
    PageRig r;
    constexpr const char* keys[] { "attack", "decay", "sustain", "release", "attack_curve", "decay_curve",
                                  "release_curve", "delay", "hold", "units", "legato_inverted" };
    for (int env = 1; env <= 6; ++env)
    {
        r.inst.env = env;
        for (bool beats : { false, true })
        {
            r.inst.set ("env" + std::to_string (env) + ".units", beats ? 1.0f : 0.0f);
            r.refresh();
            for (int field = 0; field < n::Selection; ++field)
            {
                const bool timed = field == n::Attack || field == n::Decay || field == n::Release || field == n::Delay || field == n::Hold;
                const std::string suffix = std::string (keys[field]) + (timed && beats ? "_beats" : "");
                const int real = envIdx (env, suffix.c_str());
                check (r.inst.nativeEnv.at (field).cell().target == real, "the standalone host binds Env " + std::to_string (env) + " " + suffix);
                const int writes = r.inst.writes;
                const float value = field == n::Beats ? (beats ? 1.0f : 0.0f) : 0.36f;
                r.inst.postNative (field, value);
                r.refresh();
                check (r.inst.writes == writes + 1 && r.inst.lastWritten == real, "the standalone host writes exactly one live " + suffix);
            }
        }
    }
    r.inst.env = 2;
    r.inst.set ("env2.units", 0.0f);
    r.refresh();
    const int writes = r.inst.writes;
    r.inst.postNative (n::Delay, 0.9f);
    r.inst.postNative (n::Beats, 1.0f);
    r.refresh();
    r.refresh();
    check (r.inst.writes == writes + 1 && r.inst.lastWritten == envIdx (2, "units")
           && r.inst.nativeEnv.at (n::Delay).cell().target == envIdx (2, "delay_beats"),
           "BEATS discards a queued millisecond edit and publishes the new domain");

    const auto attack = r.ui.full().attack.rect();
    const int x = attack.x + attack.w / 2, y = attack.y + attack.h / 2;
    r.page->onTouch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    r.inst.postNative (n::Attack, 0.8f);
    ++r.inst.generation;
    r.inst.env = 6;
    r.refresh();
    check (r.inst.writes == writes + 1 && r.inst.nativeEnv.at (n::Attack).cell().target < 0,
           "a held, queued edit cannot cross a preset and envelope change");
    r.page->onTouch (ev (TouchEvent::Up, x, y), r.stack, r.inst, r.now + 40.0);
    r.inst.postNative (n::Attack, 0.25f);
    r.refresh();
    check (r.inst.writes == writes + 2 && r.inst.lastWritten == envIdx (6, "attack_beats"),
           "the newly bound dial is editable again after release");

    r.page->onTouch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    r.page->onTick (r.stack, r.inst, r.now + 650.0);
    r.refresh();
    check (r.ui.topPage() == EnvUi::kPageMods && ! r.inst.nativeEnv.layout() && r.ui.covers (x, y),
           "a stationary press still opens the modulation detail and covers the native cells");
    r.page->onTouch (ev (TouchEvent::Up, x, y), r.stack, r.inst, r.now + 650.0);
    r.stack.back();
    r.refresh();
    check (r.inst.nativeEnv.layout() == 1, "the ENV editor is republished when the modulation detail closes");
    r.page->onHidden();
    check (! r.inst.nativeEnv.layout(), "leaving the tab disarms the dedicated Env handles");
}

} // namespace

static void testEnvFactoryComposite()
{
    PageRig r;
    auto fixed = [&] {
        auto scene = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder builder (*scene);
        builder.begin (gpu::kKindPlay, 0, 1280, 800);
        page::ground (builder);
        r.ui.full().drawStaticFurniture (builder);
        std::vector<uint32_t> pixels;
        renderScene (*scene, pixels);
        return pixels;
    };
    const auto first = fixed();
    r.ui.setSelection (r.inst, 4);
    r.inst.set ("env4.attack", 500);
    r.route (1, 4, "master.volume");
    r.refresh();
    check (fixed() == first, "ENV static art is identical after changing envelope, attack and routing");
    for (bool beats : {false, true})
    {
        r.inst.set ("env4.units", beats ? 1 : 0);
        r.refresh();
        auto render = [&](bool host) {
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            if (host)
            {
                page::ground (sb);
                r.ui.full().drawStaticFurniture (sb);
            }
            for (auto cell : r.ui.full().nativeApertures())
                if (!cell.empty())
                    sb.rect (cell.x, cell.y, cell.right(), cell.bottom(), gpu::rgba8FromRgb (0x593c50, 255));
            r.ui.setHostBackground (host);
            r.ui.draw (sb);
            std::vector<uint32_t> pixels;
            renderScene (*scene, pixels);
            return pixels;
        };
        const auto fallback = render (false);
        const auto native = render (true);
        int maxDifference = 0;
        for (size_t i = 0; i < native.size(); ++i)
            for (unsigned shift : {0u, 8u, 16u, 24u})
                maxDifference = std::max (maxDifference, std::abs (int ((native[i] >> shift) & 255u) - int ((fallback[i] >> shift) & 255u)));
        check (maxDifference <= 1, "ENV host composite matches fallback with native controls; max channel delta " + std::to_string (maxDifference));
    }
    r.tapC (r.ui.full().targetsArea());
    check (!r.ui.hostBackground(), "ENV targets cover the native root");
    Rig play;
    play.openFull();
    play.ui.setHostBackground (true);
    check (!play.ui.hostBackground() && !play.ui.full().nativeADSR() && play.ui.full().close.rect().w > 0,
           "Play expanded envelope retains its close button and owns its controls");

}

static void printModTargetsContract()
{
    ui::ModTargetsPage screen;
    ui::ModTargetsPage::Entry dummy[6];
    screen.place ({0, page::kTabY, page::kW, page::kTabH});
    screen.load (dummy, 6, "ENV 1", 0xffffff, 7, 0);
    const auto rect = [] (ui::Rect area)
    {
        std::printf ("[%d, %d, %d, %d]", area.x, area.y - page::kTabY, area.w, area.h);
    };
    std::printf ("{\n");
    for (const auto* child : {static_cast<ui::Component*> (&screen.back), static_cast<ui::Component*> (&screen.add),
                             static_cast<ui::Component*> (&screen.page)})
    {
        const char* key = child == &screen.back ? "back" : child == &screen.add ? "add" : "page";
        std::printf ("  \"%s\": ", key);
        rect (child->rect());
        std::printf (",\n");
    }
    std::printf ("  \"row\": [");
    const auto& row = screen.rows[0];
    rect (screen.nameRect (0));
    for (const auto area : {row.depth.rect(), row.polarity.rect(), row.bypass.rect(), row.remove.rect()})
    {
        std::printf (", ");
        rect (area);
    }
    std::printf ("],\n  \"stride\": %d\n}\n", screen.rowY (1) - screen.rowY (0));
}

int main (int argc, char** argv)
{
    if (argc == 2 && std::string_view (argv[1]) == "--mod-targets-skin-contract")
    {
        printModTargetsContract();
        return 0;
    }
    testEnvFactoryComposite();
    testEnvelopeParameterCoverage();
    testEveryControlHasAParameter();
    testSelectorWritesPlayEnv();
    testCompactQGroupOnlyOnTheEnvSubTab();
    testFullScreenGroups();
    testKnobsAndFieldsWriteParameters();
    testBeatsSwitch();
    testGraphHandlesWriteParameters();
    testGraphViewStateIsThePages();
    testDoubleTapResetsAHandle();
    testCompactGraphDragAndNoOverlapOfFullButton();
    testTargetsPage();
    testModulatedKnobsShowTheirSource();
    testModulationsBoard();
    testDotsFromTheVoices();
    testDrawing();
    testHashChangesWithTheDots();
    testPlayViewRoutesTheElements();
    testPlayPageQSlots();
    testEnvTab();
    testStandaloneEnvNativeADSR();
    testStandaloneEnvNativeRemainingControls();
    testNativeUsedSelector();
    testStandaloneEnvSelectorQ();
    testStandaloneEnvNativeControlsAcrossInstancesAndPresets();
    testEnvEditorHostsOnlyCurrentFields();
    testPlayCarouselIsTheTabSelector();
    testPlayPageSelectorsAndExtraGroup();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
