// SynthyBoy - MPC overlay tests: the envelope UI (env_ui.h): the wiring of every control to the engine parameters (both directions, both units), the instance selector, the pages of the UiStack, the Q groups,
// the handles of the graph (a drag writes the parameters), the targets page (the modulation matrix seen from the source), the running dots and the drawing (one scene, inside the canvas).
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o env_ui_host shell-mpc/overlay/tests/env_ui_host.cpp <libsynthyboy_core.a> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../env_ui.h"
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
    void setQGroup (const int* t, int n) noexcept override { for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1; ++qCalls; }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setPlaySelection (int g, int i) noexcept override { if (g == 1) env = i; ++selWrites; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }
    int env = 1, q[4] = { -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0;
    bool fullOpen = false;

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
                if (it.type == EnvUi::T::Inert || it.type == EnvUi::T::Graph || it.type == EnvUi::T::Select)
                    continue;
                if (it.active (r.ui.beats()) < 0)
                    ++unbound;
            }
            check (unbound == 0 && r.ui.selection() == env && r.ui.beats() == (units > 0.5f),
                   "envelope " + std::to_string (env) + (units > 0.5f ? " in beats" : " in ms") + ": every control has a parameter, the selection follows Play Env");
        }
}

void testSelectorWritesPlayEnv()
{
    Rig r;
    const ui::Rect c = r.ui.compact().instances.rect();
    r.tap (c.x + c.w * 4 / 6 + 4, c.y + c.h / 2);
    check (r.inst.env == 5, "tap on the fifth cell of the selector: Play Env = 5");
    check (r.ui.selection() == 5, "the UI follows the selector");
    check (r.inst.q[0] == envIdx (5, "attack") && r.inst.q[1] == envIdx (5, "decay") && r.inst.q[2] == envIdx (5, "sustain") && r.inst.q[3] == envIdx (5, "release"),
           "the ADSR group follows the envelope: attack, decay, sustain, release of env5");
}

void testCompactQGroupOnlyOnTheEnvSubTab()
{
    Rig r;
    check (r.inst.q[0] == envIdx (1, "attack") && r.inst.q[3] == envIdx (1, "release"), "sub tab Env: the ADSR group of env1 is the group");
    Rig o;
    o.inst.qCalls = 0;
    o.refresh (1);
    check (o.inst.qCalls == 0 || o.inst.q[0] == -1, "another sub tab: the envelope does not publish its group");
    Rig m;
    m.inst.set ("env1.units", 1.0f);
    m.refresh();
    check (m.inst.q[0] == envIdx (1, "attack_beats") && m.inst.q[1] == envIdx (1, "decay_beats") && m.inst.q[2] == envIdx (1, "sustain") && m.inst.q[3] == envIdx (1, "release_beats"),
           "in beats the group is attack_beats, decay_beats, sustain, release_beats");
}

void testFullScreenGroups()
{
    Rig r;
    r.openFull();
    check (r.stack.depth() == 1 && r.stack.element() == EnvUi::kElementEnv && r.inst.fullOpen, "the full-screen button opens the full screen of the envelope element and tells the plugin");
    check (r.inst.q[0] == envIdx (1, "attack") && r.ui.activeQ() == 0, "the ADSR group is active first");
    r.tapC (r.ui.full().aCurve);
    check (r.ui.activeQ() == 1 && r.inst.q[0] == envIdx (1, "attack_curve") && r.inst.q[1] == envIdx (1, "decay_curve") && r.inst.q[2] == envIdx (1, "release_curve") && r.inst.q[3] == -1,
           "a touch on a curve knob makes the SHAPE group the active one: the three curves");
    r.tapC (r.ui.full().hold);
    check (r.ui.activeQ() == 2 && r.inst.q[0] == envIdx (1, "delay") && r.inst.q[1] == envIdx (1, "hold") && r.inst.q[2] == envIdx (1, "units") && r.inst.q[3] == envIdx (1, "legato_inverted"),
           "the TIME group: delay, hold, units, legato");
    r.tapC (r.ui.full().close);
    check (r.stack.isMain() && ! r.inst.fullOpen, "close: main again, the plugin is told");
    r.openFull();
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
    // the sizes of the touch targets
    ui::Rect t[8];
    const int n = g.touchTargets (t, 8);
    bool big = n == 8;
    for (int i = 0; i < n; ++i)
        big = big && t[i].w >= 56 && t[i].h >= 56;
    check (big, "the eight handles have touch targets of at least 56 x 56 pixels");
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
    check (! r.ui.full().targetsBlock().empty(), "the full screen has the block of the targets when there is one");
    check (r.ui.compact().instances.hasTargets (0) && ! r.ui.compact().instances.hasTargets (1),
           "compact env tabs: ENV 1 has target dot (hasTargets=true), ENV 2 has no mark (hasTargets=false)");
    check (r.ui.full().instances.hasTargets (0) && ! r.ui.full().instances.hasTargets (1),
           "full screen env tabs: ENV 1 has target dot, ENV 2 has no mark");
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
        check (q.ui.numTargets() == 0 && q.ui.full().targetsBlock().empty() && q.ui.full().graph.rect().w > 1200, "no target: no empty block, the graph takes the whole width (" + std::to_string (q.ui.full().graph.rect().w) + ")");
        check (q.ui.full().instances.targetMask() == 0, "no envelope has targets: targetMask is 0, no dots or marks on any tab");
        for (int i = 0; i < 6; ++i)
            check (! q.ui.full().instances.hasTargets (i) && ! q.ui.compact().instances.hasTargets (i),
                   "env " + std::to_string (i + 1) + " has no target dot");
    }
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
    check (inst.qGroup[0] == inst.index ("env3.attack") && inst.qGroup[3] == inst.index ("env3.release"), "sub-tab Env: the Q group is the ADSR of the selected envelope (env3)");
    auto touch = [&] (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; in.nowMs += 60.0; return pv.onTouch (e, st, inst, in.nowMs); };
    auto tap = [&] (int x, int y) { const bool a = touch (TouchEvent::Down, x, y); touch (TouchEvent::Up, x, y); in.nowMs += 200.0; pv.update (in); return a; };
    // the full screen button of the envelope's compact view (the corner of its graph)
    EnvUi probe;
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

} // namespace

int main()
{
    testEveryControlHasAParameter();
    testSelectorWritesPlayEnv();
    testCompactQGroupOnlyOnTheEnvSubTab();
    testFullScreenGroups();
    testKnobsAndFieldsWriteParameters();
    testBeatsSwitch();
    testGraphHandlesWriteParameters();
    testDoubleTapResetsAHandle();
    testCompactGraphDragAndNoOverlapOfFullButton();
    testTargetsPage();
    testModulatedKnobsShowTheirSource();
    testDotsFromTheVoices();
    testDrawing();
    testHashChangesWithTheDots();
    testPlayViewRoutesTheElements();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
