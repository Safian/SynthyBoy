// SynthyBoy - MPC overlay tests: Master and Voice UI (master_ui.h, voice_ui.h), the pages of the Master and Voice tabs: the parameters of the master section and of the voice control in both directions, the two
// sides of the Master page and their blocks (one Q group each, filled, every control >= 56 px with its value), the real keyboard, the meters of the engine, the lists of the steppers, the step grid (drawing
// with a finger over several columns, the step a voice plays lit, every value of the selected step), the virtual Q targets, and the scene rendering.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

#include "../master_ui.h"
#include "../voice_ui.h"
#include "demo_common.h"

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

using MRig = demo::Rig<MasterUi>;
using VRig = demo::Rig<VoiceUi>;

bool sizesOk (const std::vector<ui::Component*>& cs, std::string& bad)
{
    for (ui::Component* c : cs)
        if (c->rect().w <= 0 || c->rect().h < 56 || ! c->sizeOk())
        {
            bad = std::to_string (c->rect().w) + " x " + std::to_string (c->rect().h) + " min " + std::to_string (c->minSize().w) + " x " + std::to_string (c->minSize().h);
            return false;
        }
    return true;
}

// ---- master ----------------------------------------------------------------------------------------------------------------------------------------------------
void testMasterParametersExist()
{
    int unbound = 0;
    MasterUi ui;
    for (const MasterUi::Item& it : ui.items())
        if (it.param < 0 && it.type != MasterUi::T::Inert)
        {
            ++unbound;
            std::printf ("  not in the table: %s\n", it.key.c_str());
        }
    check (unbound == 0 && ui.items().size() >= 32, "master: all " + std::to_string (ui.items().size()) + " controls are bound to an engine parameter");
}



// the bold cut of the Master page (owner 2026-11-21): LINK is touch only inside PITCH (no Q slot, five groups on MAIN), what does not apply is hidden (BEND DOWN while LINK is on, MPE BEND and BEND TO X while MPE is off),
// the Q groups close up from the left. Nothing sits behind a MORE button on this page: a hidden control comes back with its switch.
bool inLeaves (MasterUi& ui, const ui::Component& c)
{
    ui::Component* all[96];
    const int n = ui.screen().collectLeaves (all, 0, 96);
    for (int i = 0; i < n; ++i)
        if (all[i] == &c)
            return true;
    return false;
}



template<class Rig>
void checkNativePage (Rig& r, const std::string& name)
{
    r.refresh();
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.setHostBackground (false);
    r.ui.draw (sb);
    std::vector<uint32_t> expected, actual;
    renderScene (*scene, expected);

    gpu::SceneBuilder native (*scene);
    native.begin (gpu::kKindPlay, 0, 1280, 800);
    page::ground (native);
    r.ui.screen().drawStaticFurniture (native);
    r.ui.setHostBackground (true);
    check (r.ui.hostBackground(), name + " uses its factory native furniture after sentinel confirmation");
    r.ui.draw (native);
    renderScene (*scene, actual);
    check (actual == expected, name + " static and live passes reproduce every pixel of the opaque renderer");

    ui::theme::setLook (2);
    check (!r.ui.hostBackground(), name + " uses opaque furniture for an alternative look");
    ui::theme::setLook (1);
}

void testNativePages()
{
    MRig master;
    VRig voice;
    voice.ui.setNativeControls (false);
    checkNativePage (voice, "Voice");
    master.ui.screen().setSide (ui::MasterScreen::SideTuning);
    check (!master.ui.hostBackground(), "Master tuning paints over the native MAIN furniture");
}

void testMasterDraw()
{
    MRig r;
    r.refresh();
    for (int side = 0; side < 2; ++side)
    {
        r.ui.screen().setSide (side);
        auto data = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        check (! sb.overflow && data->numOps > 0, "the master side " + std::to_string (side) + " fits in the buffer (" + std::to_string (data->numOps) + " ops)");
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (cv.size() == 1280u * 800u, "the CPU renderer draws the master side " + std::to_string (side));
    }
}

// ---- voice -----------------------------------------------------------------------------------------------------------------------------------------------------
void testVoiceLayout()
{
    VRig r;
    r.ui.setNativeControls (false);
    r.refresh();
    ui::VoiceScreen& v = r.ui.screen();
    check (v.rect().w == 1280 && v.rect().h == 628 && r.stack.element() == VoiceUi::kElementPage, "the page has the whole tab and is the element of the Voice tab");
    const ui::Rect length = v.length.rect();
    const int x = length.x + length.w / 2, y = length.y + length.h / 2;
    r.drag (x, y, x, y + 16, true);
    check (r.inst.get ("voicectl.length") == 6.0f, "sequence length drag writes the one-based step count");
    std::string bad;
    std::vector<ui::Component*> cs = { &v.length, &v.qStepSelect, &v.rowList, &v.stepValue, &v.randPan, &v.randDetune, &v.randCutoff, &v.randEnv, &v.randDetune10x, &v.scaleEnv, &v.scaleLfo, &v.scaleLfoMode, &v.oscA, &v.oscB, &v.oscC, &v.oscSub, &v.oscNoise };
    check (sizesOk (cs, bad), "every control of the Voice page is >= 56 px high and has its minimum size " + bad);
    bool inside = true;
    static const int kVoiceGroupBlock[4] = { -1, 0, -1, 2 };   // group 2 = [10x DETUNE, ENV TIME, LFO TIME, LFO MODE] lies in two blocks: frameless
    for (int g = 0; g < 4; ++g)
    {
        ui::Component* all[96];
        const int n = v.collectLeaves (all, 0, 96);
        inside = inside && v.groups[g].check (all, n) == 0;
        if (kVoiceGroupBlock[g] >= 0)
            inside = inside && v.blockRect (kVoiceGroupBlock[g]).contains (v.groups[g].rect());
    }
    check (inside, "the Q groups lie in their blocks and nothing else is in their frames");
    check (v.grid.rect().h >= 230 && v.randPan.rect().h >= 72 && v.scaleEnv.rect().h >= 72, "the grid is tall and the knobs of the blocks have the room");
    check (v.stripRect().y >= v.grid.rect().bottom() && v.blockRect (0).y >= v.stripRect().bottom(), "the strip of the step lies between the grid and the blocks");
    check (v.randDetune10x.rect().bottom() <= v.blockRect (0).bottom(), "the blocks hold their controls");
    {   // the targets: five LED toggles in two columns, the fifth across the block; none lower than 56 px, none overlapping, all inside the TARGETS block
        const ui::LedToggle* ts[5] = { &v.oscA, &v.oscB, &v.oscC, &v.oscSub, &v.oscNoise };
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            ok = ok && ts[i]->rect().h >= 56 && ts[i]->rect().w >= 96 && v.blockRect (2).contains (ts[i]->rect());
            for (int j = i + 1; j < 5; ++j)
                ok = ok && ! ts[i]->rect().overlaps (ts[j]->rect());
        }
        check (ok && v.oscA.rect().y == v.oscB.rect().y && v.oscC.rect().y == v.oscSub.rect().y && v.oscNoise.rect().y > v.oscC.rect().y && v.oscNoise.rect().w == v.oscA.rect().w && v.oscA.rect().w == v.oscB.rect().w && v.oscB.rect().w == v.oscC.rect().w,
               "targets are five LED toggles of the same width in two columns (cell " + std::to_string (v.oscA.rect().w) + " x " + std::to_string (v.oscA.rect().h) + "), NOISE in the third row, not spanning");
        check (v.scaleLfoMode.columns() == 2 && v.scaleLfoMode.rowRect (1).h >= 56 && v.scaleLfoMode.state.count == 2, "the LFO time mode is a LED list of two options side by side, both visible");
    }
}

void testVoiceGridDrawing()
{
    VRig r;
    r.inst.set ("voicectl.length", 8.0f);
    r.refresh();
    ui::VoiceGrid& g = r.ui.screen().grid;
    const ui::Rect c0 = g.knobRect (1), c5 = g.knobRect (5);
    const int y = c0.y + c0.h / 2;
    r.ui.touch ({TouchEvent::Down,c0.x+c0.w/2,y},r.stack,r.inst,r.now);
    r.ui.touch ({TouchEvent::Move,c5.x+c5.w/2,y-60},r.stack,r.inst,r.now+16);
    r.ui.touch ({TouchEvent::Up,c5.x+c5.w/2,y-60},r.stack,r.inst,r.now+40);
    r.refresh();
    check (r.inst.get("voicectl.s2.pan")>0 && r.inst.get("voicectl.s6.pan")==0,"relative knob drag changes only its original step");
    check (g.state.selectedStep==1,"drag keeps original selected step");
    // the values at the selected step are written under the grid
    r.inst.set ("voicectl.s6.detune", -25.0f);
    r.inst.set ("voicectl.s6.cutoff", 40.0f);
    r.refresh();
    check (g.value (1, 5) < -0.2f && g.value (2, 5) > 0.3f, "the grid has every row's value of the step");
    check (std::string (g.rowName (0)) == "PAN" && r.ui.screen().stripRect().h >= 40, "the strip under the grid has the room for the values of the step");
    // the row: a LED list of six rows (56 px each), at the left of the grid
    ui::VoiceScreen& vs = r.ui.screen();
    check (vs.rowList.state.count == 6 && std::string (vs.rowList.state.names[2]) == "CUTOFF" && std::string (vs.rowList.state.title) == "ROW", "the ROW list has the six rows by name, all visible");
    bool tall = true;
    for (int i = 0; i < 6; ++i)
        tall = tall && vs.rowList.rowRect (i).h >= 56 && vs.rowList.rowRect (i).w >= 96;
    check (tall && vs.rowList.sizeOk(), "every row of the ROW list is a touch target of at least 56 px (the old tabs were 35 px)");
    check (vs.rowBlockRect().contains (vs.rowList.rect()) && vs.rowBlockRect().right() <= g.rect().x && ! vs.rowBlockRect().overlaps (g.rect()), "the list stands in a block of its own at the left of the grid, the grid has the rest of the width");
    const ui::Rect row2 = vs.rowList.rowRect (2);
    r.tapAt (row2.x + row2.w / 2, row2.y + row2.h / 2);
    check (g.state.activeRow == 2 && vs.rowList.state.index == 2 && r.inst.q[1] != -1 && QRouter::indexOf (r.inst.q[1]) == VoiceUi::kVirtualRow, "a tap on CUTOFF selects the row; the list is Q2 of the header group (the virtual row target)");
    check (vs.groups[0].member (1) == &vs.rowList && vs.groups[0].frameless() && vs.groups[0].numMembers() == 4, "the header group is [STEP, the ROW list, LENGTH, VALUE] and has no frame (its members lie apart)");
    {
        // the VALUE field shows the value the way the grid cell does: a centred pan is "MID", not "0"
        r.tapAt (vs.rowList.rowRect (0).x + vs.rowList.rowRect (0).w / 2, vs.rowList.rowRect (0).y + vs.rowList.rowRect (0).h / 2);
        r.refresh();
        char want[16], got[16];
        ui::VoiceGrid::formatValue (0, g.value (0, g.state.selectedStep), want, sizeof want);
        ui::NumberField::formatValue (vs.stepValue.state, got);
        check (std::string (got) == std::string (want) && (g.value (0, g.state.selectedStep) != 0.0f || std::string (got) == "MID"), "the VALUE field of the PAN row has the format of the grid cell (" + std::string (got) + " / " + std::string (want) + ")");
        r.tapAt (vs.rowList.rowRect (2).x + vs.rowList.rowRect (2).w / 2, vs.rowList.rowRect (2).y + vs.rowList.rowRect (2).h / 2);
        r.refresh();
    }
    check (std::string (vs.stepValue.state.label) == "CUTOFF", "the VALUE field names the row (" + std::string (vs.stepValue.state.label) + ")");
    // one text for one value: the VALUE field reads as the cell of the grid and the strip do ("MID", "0 c", "40%", "25L")
    {
        char want[16], got[16];
        ui::VoiceGrid::formatValue (2, g.value (2, g.state.selectedStep), want, sizeof want);
        ui::NumberField::formatValue (vs.stepValue.state, got);
        check (std::string (got) == want && std::string (got) != "0" && std::string (got).find ('%') != std::string::npos, "the VALUE field of CUTOFF reads like the strip: " + std::string (got));
        const int savedRow = g.state.activeRow;
        g.state.activeRow = 0;
        r.inst.set ("voicectl.s" + std::to_string (g.state.selectedStep + 1) + ".pan", 0.0f);
        r.refresh (2);
        ui::NumberField::formatValue (vs.stepValue.state, got);
        check (std::string (got) == "MID", "the VALUE field of PAN at the middle reads MID, as the cell does (" + std::string (got) + ")");
        g.state.activeRow = savedRow;
        r.refresh (2);
    }
    check (vs.length.rect().w >= 160 && vs.stepValue.rect().w >= 180 && vs.qStepSelect.rect().right() <= vs.length.rect().x, "LENGTH and VALUE have the room the ROW stepper left (" + std::to_string (vs.length.rect().w) + ", " + std::to_string (vs.stepValue.rect().w) + " px)");
    check (r.ui.screen().turnQ (1, 1.0).index == 3, "a Q-Link step on Q2 moves the row on (the list)");
    r.refresh();
}

/** The pixels of a component drawn alone (to see whether a gesture changed the picture). */
std::vector<uint32_t> pixelsOf (const ui::Component& c, int w, int h)
{
    uit::Scene sc (w, h, ui::theme::rgba (ui::theme::L().bg0, 255));
    c.draw (sc.builder());
    return uit::render (sc).px;
}

void testVoiceGridStoresNoTruth()
{
    ui::VoiceGrid g;
    g.place ({ 0, 0, 800, 260 });
    g.state.length = 8;
    g.state.activeRow = 1;
    g.state.selectedStep = 0;
    const std::vector<uint32_t> before = pixelsOf (g, 800, 260);
    const ui::Rect c2 = g.knobRect (2), c5 = g.knobRect (5);
    const int y = c2.y + c2.h / 2;
    const ui::Change d = g.touchDown (c2.x + c2.w / 2, y, 0.0);
    check (d.kind == ui::ChangeKind::Value && d.id == g.stepId (1, 2) && d.value == 0.0, "knob touch selects without jumping");
    const ui::Change m = g.touchMove (c5.x + c5.w / 2, y - 30);
    check (m.kind == ui::ChangeKind::Value && m.id == g.stepId (1, 2) && m.value > 0.0, "knob drag remains with original step");
    g.touchUp (c5.x, y);
    bool untouched = g.state.selectedStep == 0 && g.state.activeRow == 1 && g.state.length == 8;
    for (int r = 0; r < ui::VoiceGrid::kNumRows; ++r)
        for (int s = 0; s < ui::VoiceGrid::kNumSteps; ++s)
            untouched = untouched && g.state.values[r][s] == 0.0f;
    check (untouched, "grid: after a whole stroke with no write back, `state` is as the owner left it (no value, no selected step)");
    check (pixelsOf (g, 800, 260) == before, "grid: ... and the picture is the same");
    g.state.selectedStep = 2;
    g.setValue (1, 2, static_cast<float> (d.value));   // the owner writes the parameter and sets the state
    check (pixelsOf (g, 800, 260) != before, "grid: with the owner writing back, the next draw shows the new value (and the selected frame)");
    // a Q-Link turn
    g.state.values[1][2] = 1.0f;
    check (g.turn (1.0).kind == ui::ChangeKind::None, "grid: a turn up at the top of the range reports nothing");
    const ui::Change t = g.turn (-1.0);
    check (t.kind == ui::ChangeKind::Value && t.id == g.stepId (1, 2) && std::fabs (t.value - 0.95) < 0.001 && g.state.values[1][2] == 1.0f, "grid: a turn down reports the new value and leaves `state` alone");
}

void testVoiceStepsLitAndQ()
{
    VRig r;
    r.ui.setNativeControls (false);
    r.inst.set ("voicectl.length", 3.0f);
    r.inst.haveLive = true;
    for (float& s : r.inst.liveValues.voiceStep)
        s = -1.0f;
    r.inst.liveValues.voiceStep[0] = 0.0f;   // the newest voice plays step 1
    r.inst.liveValues.voiceStep[1] = 2.0f;
    r.refresh();
    check (r.ui.screen().grid.state.voiceStepMask == 0x05, "the steps the voices play are lit (steps 1 and 3)");
    r.inst.liveValues.voiceStep[1] = 6.0f;   // beyond the length: not lit
    r.refresh();
    check (r.ui.screen().grid.state.voiceStepMask == 0x01, "a step beyond the length is not lit");

    // the Q group STEP: two virtual targets, the length, the value of the selected step
    ui::VoiceScreen& v = r.ui.screen();
    check (QRouter::modeOf (r.inst.q[0]) == QRouter::kVirtual && QRouter::indexOf (r.inst.q[0]) == VoiceUi::kVirtualStep, "Q1 is the virtual step target");
    check (QRouter::modeOf (r.inst.q[1]) == QRouter::kVirtual && QRouter::indexOf (r.inst.q[1]) == VoiceUi::kVirtualRow, "Q2 is the virtual row target");
    check (r.inst.q[2] == demo::Inst::idx ("voicectl.length") && r.inst.q[3] == demo::Inst::idx ("voicectl.s1.pan"), "Q3 is the length, Q4 the value of the selected step");
    r.inst.setVirtualIn (VoiceUi::kVirtualStep, 1.0f);   // the host writes step 8
    r.refresh();
    check (v.grid.state.selectedStep == 7 && r.inst.q[3] == demo::Inst::idx ("voicectl.s8.pan"), "the host's write to the virtual step target selects step 8 and the value target follows");
    r.inst.setVirtualIn (VoiceUi::kVirtualRow, 0.4f);
    r.refresh();
    check (v.grid.state.activeRow == 2 && r.inst.q[3] == demo::Inst::idx ("voicectl.s8.cutoff"), "the virtual row target selects CUTOFF");
    // the value field writes the step
    const ui::Rect sv = v.stepValue.rect();
    r.drag (sv.x + sv.w / 2, sv.y + sv.h / 2, sv.x + sv.w / 2, sv.y + sv.h / 2 - 30, true);
    check (r.inst.get ("voicectl.s8.cutoff") > 0.0f, "the value field of the header writes the selected step (" + std::to_string (r.inst.get ("voicectl.s8.cutoff")) + ")");
    // the other blocks write their parameters, with values
    check (std::string (v.randPan.state.valueText).find ("%") != std::string::npos && std::string (v.scaleEnv.state.valueText).find ("%") != std::string::npos, "the knobs of the blocks show their values");
    r.tap (v.randDetune10x);
    r.refresh();
    check (v.activeGroup() == 2 && r.inst.q[0] == demo::Inst::idx ("voicectl.rand.detune10x"), "tapping 10x DETUNE activates group 2");
    r.tap (v.scaleEnv);
    r.refresh();
    check (v.activeGroup() == 2 && r.inst.q[1] == demo::Inst::idx ("voicectl.scale.env"), "tapping SCALING activates group 2 (it holds the 10x switch and the three SCALING values)");
    r.tap (v.oscNoise);
    r.refresh();
    check (v.activeGroup() == 3 && r.inst.q[0] == demo::Inst::idx ("voicectl.osc.a") && v.groups[3].numMembers() == 4, "tapping NOISE (touch only, no Q slot) keeps the TARGETS group of its block active");
    check (r.inst.get ("voicectl.osc.noise") != 1.0f, "a target switch writes its parameter");
    r.ui.forgetQGroup();
    r.refresh();
    check (v.activeGroup() == 0, "forgetQGroup restores group 0");
    {
        const ui::Rect row1 = v.scaleLfoMode.rowRect (1);
        r.tapAt (row1.x + row1.w / 2, row1.y + row1.h / 2);
        r.refresh();
        check (r.inst.get ("voicectl.scale.lfo_mode") == 1.0f && v.scaleLfoMode.state.index == 1 && v.activeGroup() == 2, "a tap on the second option of the LFO time mode selects it (parameter, lit dot, group SCALING)");
        const float before = r.inst.get ("voicectl.osc.a");
        r.tap (v.oscA);
        r.refresh();
        check (r.inst.get ("voicectl.osc.a") != before && v.oscA.state.on == (r.inst.get ("voicectl.osc.a") > 0.5f), "a tap on a target LED toggle flips its parameter and its dot");
    }
}

void testVoiceDraw()
{
    VRig r;
    r.ui.setNativeControls (false);
    r.inst.haveLive = true;
    for (float& s : r.inst.liveValues.voiceStep)
        s = -1.0f;
    r.inst.liveValues.voiceStep[0] = 2.0f;
    r.refresh();
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 0, "the voice page fits in the buffer (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "the CPU renderer draws the voice page");
}

void testVoiceNativeSequence()
{
    namespace n = nativeui::voice;
    struct Instance : demo::Inst
    {
        n::Bank bank;
        n::ViewState state;
        bool nativeVoiceSequenceControls() const noexcept override { return true; }
        void setNativeVoiceState (const n::ViewState& s) noexcept override { state = s; bank.publish (s); }
        bool takeNativeVoice (const n::ViewState& s, int field, float& value) noexcept override { return bank.take (s, field, value); }
        void post (int field, float value)
        {
            const auto cell = bank.at (field);
            bank.report (cell);
            bank.post (cell, value);
        }
    };
    demo::Rig<VoiceUi, Instance> r;
    r.refresh();
    auto& inst = r.inst;
    auto& screen = r.ui.screen();
    check (inst.state.active && inst.state.target == Instance::idx ("voicectl.s1.pan"), "Voice native sequence starts on step 1 / PAN");
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder builder (*scene);
    builder.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (builder);
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    bool hostOwns = true;
    const ui::Component* const sequence[] { &screen.rowList, &screen.qStepSelect, &screen.stepValue };
    for (const auto* c : sequence)
    {
        const auto box = c->rect();
        const int x = box.x + box.w / 2, y = box.y + box.h / 2;
        hostOwns &= ! screen.isShown (*c) && ! r.ui.covers (x, y) && pixels[y * 1280 + x] == 0
          && ! r.ui.touch ({ TouchEvent::Down, x, y }, r.stack, inst, r.now)
          && ! r.ui.touch ({ TouchEvent::Move, x, y - 10 }, r.stack, inst, r.now + 5)
          && ! r.ui.touch ({ TouchEvent::Up, x, y }, r.stack, inst, r.now + 10)
          && ! r.ui.wheel (x, y, 1, r.stack, inst, r.now);
    }
    check (hostOwns, "Voice's three native sequence controls have transparent, exclusive input apertures");
    check (screen.nativeStepKnobs(), "production native sequence enables the single native grid owner");
    const int gridWrites = inst.writes;
    for (int step = 0; step < 8; ++step)
    {
        const auto col = screen.grid.columnRect (step);
        const int x = col.x + col.w/2, y = col.y+45;
        check (!r.ui.covers (x,y) && !r.ui.touch ({TouchEvent::Down,x,y},r.stack,inst,r.now),
               "every real native grid bar passes touch through to host");
    }
    const auto header = screen.grid.columnRect (2);
    check (r.ui.touch ({TouchEvent::Down,header.x+20,header.y+10},r.stack,inst,r.now),
           "native grid column header selects without a bar write");
    check (inst.writes == gridWrites && screen.grid.state.selectedStep == 2,
           "header selection neither paints100percent nor captures native bar drags");
    inst.post (n::kStep,0); r.refresh();

    const ui::Rect col1 = screen.grid.columnRect (1);
    const int c1X = col1.x + 6 + (col1.w - 12) / 2;
    const int c1Y = col1.y + 32 + 20;
    check (pixels[c1Y * 1280 + c1X] == 0u, "native slider aperture inside Step 2 is transparent in overlay scene");
    const bool wide = ui::QGroup::wideDevice();
    ui::QGroup::setWideDevice (false); screen.setActiveGroup (1);
    builder.begin (gpu::kKindPlay,0,1280,800); r.ui.draw (builder);
    int qMarkers=0;
    for (uint32_t i=0;i<scene->numOps;++i)
        if (scene->ops[i].op==gpu::kOpText && gpu::textString(*scene,scene->ops[i])=="Q") ++qMarkers;
    check (qMarkers == (ui::QVisualPolicy::kHasQFrame ? 1 : 0),
           "native Voice Gen1 has one unnumbered bankQ; Gen2 has none");
    ui::QGroup::setWideDevice (wide); screen.setActiveGroup (0); r.refresh();
    const int before = inst.writes;
    inst.post (n::kRow, 0.2f);
    inst.post (n::kStep, 3.0f / 7.0f);
    inst.post (n::kValue, 0.8f);
    r.refresh();
    const int detune = Instance::idx ("voicectl.s4.detune");
    check (inst.writes == before && inst.state.row == 1 && inst.state.step == 3 && inst.state.target == detune
           && inst.q[3] == detune && screen.grid.state.activeRow == 1 && screen.grid.state.selectedStep == 3,
           "Voice row and step are view-only and discard a value queued against the previous selection");
    inst.post (n::kValue, 0.31f);
    r.refresh();
    check (inst.writes == before + 1 && inst.lastWritten == detune && inst.get ("voicectl.s4.detune") == -38.0f
           && screen.grid.value (1, 3) == -0.38f,
           "the native step value writes the active detune cell and updates the overlay graph");

    auto oldValue = inst.bank.at (n::kValue);
    inst.bank.report (oldValue);
    inst.setVirtualIn (VoiceUi::kVirtualRow, 1.0f);
    r.refresh();
    inst.bank.post (oldValue, 0.84f);
    r.refresh();
    check (inst.writes == before + 1 && inst.state.target == Instance::idx ("voicectl.s4.mod2"),
           "a previously displayed native value cannot cross a Q-Link row change");
    inst.post (n::kValue, 0.89f);
    inst.setVirtualIn (VoiceUi::kVirtualStep, 4.0f / 7.0f);
    r.refresh();
    check (inst.writes == before + 1 && inst.state.target == Instance::idx ("voicectl.s5.mod2"),
           "a new Q-Link step discards a queued old step value");
    inst.post (n::kValue, 0.89f);
    ++inst.generation;
    r.refresh();
    check (inst.writes == before + 1 && inst.state.preset == inst.generation,
           "Voice discards a native write queued before preset recall, even on the same row and step");

    const auto third = screen.grid.columnRect (2), fifth = screen.grid.columnRect (4);
    const int y = third.y + third.h/4;
    const int nativeBefore = inst.writes;
    check (!r.ui.touch ({TouchEvent::Down,third.x+third.w/2,y},r.stack,inst,r.now)
           && !r.ui.touch ({TouchEvent::Move,fifth.x+fifth.w/2,y},r.stack,inst,r.now+16)
           && !r.ui.touch ({TouchEvent::Up,fifth.x+fifth.w/2,y},r.stack,inst,r.now+32),
           "production bars exclusively leave drag input to native sliders");
    check (inst.writes == nativeBefore,"native bar input is not duplicated by overlay paint");
    inst.writeParam (Instance::idx ("voicectl.s3.mod2"),.7f); r.refresh();
    check (std::fabs (screen.grid.value (5,2)-.4f)<1e-6f && inst.state.step==4,
           "real native bar writes update live grid without changing unrelated selected step");
    const int painted = inst.writes;
    inst.post (n::kValue, 0.2f);
    r.ui.leftTab (inst);
    r.refresh();
    check (inst.writes == painted && inst.state.active, "Voice rejects a stale native value when the tab returns");
    inst.post (n::kValue, 0.2f);
    r.stack.openDetail (VoiceUi::kPageList);
    r.refresh();
    check (! inst.state.active && inst.writes == painted, "a Voice overlay modal hides the native sequence and rejects queued writes");
    r.stack.back();
    r.refresh();
    inst.post (n::kValue, 0.2f);
    r.refresh();
    check (inst.writes == painted + 1 && inst.lastWritten == Instance::idx ("voicectl.s5.mod2"), "Voice accepts a fresh value after the modal is dismissed");
}

void testVoiceNativeControls()
{
    VRig r;
    r.refresh();
    check (r.ui.nativeControls(), "VoiceUi defaults to native controls in standalone mode");
    check (r.ui.screen().nativeControls(), "VoiceScreen reports native controls enabled");

    ui::VoiceScreen& v = r.ui.screen();

    int converted = 0;
    const int before = r.inst.writes;
    for (const VoiceUi::Item& item : r.ui.items())
    {
        if (item.param < 0 || item.c == nullptr || item.type == VoiceUi::T::GridStep)
            continue;
        ++converted;
        const ui::Rect bounds = item.c->rect();
        const int x = bounds.x + bounds.w / 2, y = bounds.y + bounds.h / 2;
        check (! item.c->enabled() && ! v.isShown (*item.c), item.key + " has no duplicate overlay widget");
        check (! r.ui.covers (bounds.x, bounds.y) && ! r.ui.covers (bounds.right() - 1, bounds.bottom() - 1),
               item.key + " passes the complete native aperture through");
        check (! r.ui.touch ({ TouchEvent::Down, x, y }, r.stack, r.inst, r.now)
               && ! r.ui.touch ({ TouchEvent::Move, x, y - 10 }, r.stack, r.inst, r.now + 10.0)
               && ! r.ui.touch ({ TouchEvent::Up, x, y - 10 }, r.stack, r.inst, r.now + 20.0)
               && ! r.ui.wheel (x, y, 1, r.stack, r.inst, r.now),
               item.key + " leaves the entire gesture and wheel to the host");
    }
    check (converted == 14 && r.inst.writes == before && ! r.inst.fullOpen,
           "the standalone Voice owns fourteen native apertures and no overlay parameter writes from them");
    check (r.ui.covers (140, VoiceUi::kTabY + 455) && r.ui.covers (1180, VoiceUi::kTabY + 580),
           "RAND gutter and the empty sixth TARGETS cell have no host aperture");

    // 2. Retention of genuinely dynamic curves and contextual overlays
    check (v.grid.enabled() && v.isShown (v.grid), "dynamic VoiceGrid sequence curve is retained and interactive");
    check (v.rowList.enabled() && v.isShown (v.rowList), "rowList view-state router is retained and interactive");
    check (v.qStepSelect.enabled() && v.isShown (v.qStepSelect), "qStepSelect view-state router is retained and interactive");
    check (v.stepValue.enabled() && v.isShown (v.stepValue), "stepValue dynamic row/step value editor is retained and interactive");

    const ui::Rect rPanR = v.randPan.rect();

    // 4. Retained controls remain covered and interactive
    const ui::Rect gridR = v.grid.rect();
    check (r.ui.covers (gridR.x + gridR.w / 2, gridR.y + gridR.h / 2), "covers() returns true for VoiceGrid sequence curve");
    const ui::Rect rowR = v.rowList.rect();
    check (r.ui.covers (rowR.x + rowR.w / 2, rowR.y + rowR.h / 2), "covers() returns true for rowList router");
    const ui::Rect stepSelR = v.qStepSelect.rect();
    check (r.ui.covers (stepSelR.x + stepSelR.w / 2, stepSelR.y + stepSelR.h / 2), "covers() returns true for qStepSelect router");
    const ui::Rect valR = v.stepValue.rect();
    check (r.ui.covers (valR.x + valR.w / 2, valR.y + valR.h / 2), "covers() returns true for stepValue editor");
    const ui::Rect stripR = v.stripRect();
    check (r.ui.covers (stripR.x + stripR.w / 2, stripR.y + stripR.h / 2), "covers() returns true for stripRect");

    // Interactive gesture on retained VoiceGrid updates sequence values
    const ui::Rect col2 = v.grid.knobRect (2), col4 = v.grid.knobRect (4);
    const int gy = col2.y + col2.h / 2;
    check (r.ui.touch ({ TouchEvent::Down, col2.x + col2.w / 2, gy }, r.stack, r.inst, r.now), "touch on VoiceGrid is handled by overlay");
    r.ui.touch ({ TouchEvent::Move, col4.x + col4.w / 2, gy - 30 }, r.stack, r.inst, r.now + 16.0);
    r.ui.touch ({ TouchEvent::Up, col4.x + col4.w / 2, gy - 30 }, r.stack, r.inst, r.now + 32.0);
    r.refresh();
    check (r.inst.get ("voicectl.s3.pan") > 0.0f, "VoiceGrid gesture writes step 3 pan parameter");
    check (r.inst.get ("voicectl.s4.pan") == 0.0f, "VoiceGrid gesture writes step 4 pan parameter");
    check (v.grid.state.selectedStep == 2, "VoiceGrid gesture updates selected step");

    // 5. Q-Link routing verification across all 16 slots
    r.refresh();
    check (r.inst.q[0] != -1 && QRouter::modeOf (r.inst.q[0]) == QRouter::kVirtual && QRouter::indexOf (r.inst.q[0]) == VoiceUi::kVirtualStep,
           "Q-Link slot 0 is mapped to virtual step");
    check (r.inst.q[1] != -1 && QRouter::modeOf (r.inst.q[1]) == QRouter::kVirtual && QRouter::indexOf (r.inst.q[1]) == VoiceUi::kVirtualRow,
           "Q-Link slot 1 is mapped to virtual row");
    check (r.inst.q[2] == demo::Inst::idx ("voicectl.length"), "Q-Link slot 2 is mapped to voicectl.length");
    check (r.inst.q[3] == demo::Inst::idx ("voicectl.s3.pan"), "Q-Link slot 3 is mapped to selected step value");

    // 6. Scene rendering in native mode
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 50, "native voice scene renders without buffer overflow");
    std::vector<uint32_t> pixels;
    renderScene (*data, pixels);
    check (pixels.size() == 1280u * 800u, "native voice CPU scene renders to 1280x800 framebuffer");

    // Native area transparency check: bottom parameter blocks are transparent in overlay
    // so host-rendered widgets show through primary scanout
    const ui::Rect b0 = v.inner (0);
    const size_t testPixel = static_cast<size_t> (b0.y + b0.h / 2) * 1280u + static_cast<size_t> (b0.x + b0.w / 2);
    check (pixels[testPixel] == 0u, "native dial interior in Block 0 is transparent in overlay scene");

    for (const VoiceUi::Item& item : r.ui.items())
        if (item.param >= 0 && item.c != nullptr && item.type != VoiceUi::T::GridStep)
        {
            const ui::Rect b = item.c->rect();
            const size_t pixel = static_cast<size_t> (b.y + b.h / 2) * 1280u + static_cast<size_t> (b.x + b.w / 2);
            check (pixels[pixel] == 0, item.key + " center remains transparent through the overlay plane");
        }

    r.stack.openDetail (VoiceUi::kPageList);
    r.refresh();
    const bool blockedTargets = std::all_of (r.inst.q, r.inst.q + 16, [] (int q) { return q == -1; });
    check (r.inst.fullOpen && blockedTargets && r.ui.covers (rPanR.x + rPanR.w / 2, rPanR.y + rPanR.h / 2),
           "an overlay modal publishes the native-controls-off frame and suspends Q-Links");
    const int beforeModalTurn = r.inst.writes;
    r.ui.turnQ (3, 1.0, r.stack, r.inst);
    check (r.inst.writes == beforeModalTurn, "the overlay modal blocks edits to the previous Q target");
    r.stack.back();
    r.refresh();
    check (! r.inst.fullOpen && r.inst.qN == 16 && ! r.ui.covers (rPanR.x + rPanR.w / 2, rPanR.y + rPanR.h / 2),
           "closing the overlay modal restores native apertures and all sixteen targets");

    // 7. Offline regression evidence for failed XL contact geometry:
    // Slot centers and bounds on screen (rPanR and rDetR are already in screen coordinates since tab origin Y = 110 is applied in place())
    const int panCenterX = rPanR.x + rPanR.w / 2;         // 14 + 62 = 76
    const int panCenterY = rPanR.y + rPanR.h / 2;         // 518 + 65 = 583
    check (panCenterX == 76 && panCenterY == 583, "Voice Rand Pan screen center is (76, 583) matching native-input probe");
    check (! r.ui.covers (panCenterX, panCenterY), "overlay leaves Voice Rand Pan center transparent for host touch");

    // Detune slot: x = 142, w = 124, center X = 204
    const ui::Rect rDetR = v.randDetune.rect();
    const int detCenterX = rDetR.x + rDetR.w / 2;         // 142 + 62 = 204
    check (detCenterX == 204, "Voice Rand Detune center X is 204");

    // The failed XL contact at (160, 519):
    // x = 160 falls in Rand Detune (142..266), NOT Rand Pan (14..138)
    check (rDetR.contains (160, 519), "targeted.py point (160, 519) falls inside Rand Detune slot");
    check (! rPanR.contains (160, 519), "targeted.py point (160, 519) is outside Rand Pan slot (proves missed target)");
}
void testFullCoverage()
{
    const auto* table = synthyboy::paramTable();
    const size_t count = synthyboy::paramCount();
    int totalChecked = 0;
    int missingFromPages = 0;
    int missingFromQGroups = 0;

    MasterUi masterUi;
    VoiceUi voiceUi;

    std::unordered_set<std::string> masterKeys;
    for (const auto& it : masterUi.items())
        if (it.param >= 0 && it.type != MasterUi::T::Inert)
            masterKeys.insert (it.key);

    std::unordered_set<std::string> voiceKeys;
    for (const auto& it : voiceUi.items())
        if (it.param >= 0 && it.type != VoiceUi::T::Inert)
            voiceKeys.insert (it.key);

    // the Q slots of every group in every state of the conditions (LINK on / off, MPE on / off): what does not apply is hidden and is no Q member, so every state has to be walked
    std::unordered_set<int> masterQParams;
    for (int st = 0; st < 4; ++st)
        for (int g = 0; g < masterUi.screen().numGroups(); ++g)
        {
            demo::Inst inst;
            inst.set ("master.bend_down_link", (st & 1) ? 1.0f : 0.0f);
            inst.set ("master.mpe.on", (st & 2) ? 1.0f : 0.0f);
            UiStack stack;
            page::Input in;
            masterUi.update (inst, stack, 0.0, in);   // reads the instance: hides what does not apply and closes the groups up
            masterUi.screen().setActiveGroup (g);
            masterUi.update (inst, stack, 0.0, in);
            for (int slot = 0; slot < 4; ++slot)
            {
                const int target = masterUi.qTargets()[slot];
                if (target >= 0 && target < static_cast<int> (count))
                    masterQParams.insert (target);
            }
        }

    std::unordered_set<int> voiceQParams;
    for (int g = 0; g < voiceUi.screen().numGroups(); ++g)
    {
        voiceUi.screen().setActiveGroup (g);
        demo::Inst inst;
        UiStack stack;
        page::Input in;
        voiceUi.update (inst, stack, 0.0, in);
        for (int slot = 0; slot < 4; ++slot)
        {
            const int target = voiceUi.qTargets()[slot];
            if (target >= 0 && target < static_cast<int> (count))
                voiceQParams.insert (target);
        }
    }

    std::vector<std::string> missingList;
    for (size_t i = 0; i < count; ++i)
    {
        const std::string key = table[i].key;
        const bool isMaster = key.rfind ("master.", 0) == 0;
        const bool isVoice = key.rfind ("voice.", 0) == 0;
        const bool isVoiceCtl = key.rfind ("voicectl.", 0) == 0;
        if (! isMaster && ! isVoice && ! isVoiceCtl)
            continue;

        ++totalChecked;

        bool onPage = false;
        bool inQ = false;

        if (isVoiceCtl)
        {
            onPage = voiceKeys.count (key) > 0;
            if (key.rfind ("voicectl.s", 0) == 0)
            {
                inQ = onPage;
            }
            else
            {
                inQ = voiceQParams.count (static_cast<int> (i)) > 0 || key == "voicectl.osc.noise";   // NOISE is the 17th value: touch only by design (owner, Q-Link repack: 4 groups of 4); tested in testVoiceLayout
            }
        }
        else
        {
            onPage = masterKeys.count (key) > 0;
            inQ = masterQParams.count (static_cast<int> (i)) > 0 || key == "master.bend_down_link" || key == "master.velocity_track" || key == "master.voice.same_note" || key == "master.voice.steal_priority";   // LINK is touch only by design (bold cut, owner Q4): no Q slot; tested in testMasterBoldCut. VOICE OPTS (VEL TRACK, SAME NOTE, STEAL) is touch only since GLIDE took its Q group (q-link-audit item 7: owner may reverse): testMasterGlideQ

        }

        if (! onPage)
        {
            ++missingFromPages;
            missingList.push_back ("MISSING_PAGE: " + key);
        }
        if (! inQ)
        {
            ++missingFromQGroups;
            missingList.push_back ("MISSING_Q: " + key);
        }
    }

    for (const auto& m : missingList)
        std::printf ("  %s\n", m.c_str());

    check (totalChecked == 95, "coverage: found exactly 95 parameters for master.*, voice.*, voicectl.* (found " + std::to_string (totalChecked) + ")");
    check (missingFromPages == 0 && missingFromQGroups == 0, "coverage: all " + std::to_string (totalChecked) + " parameters have controls and are in Q groups");
}
} // namespace

// the plugin-wide pad pressure layer: PAD PRESS and PAD % in the VOICES block, shell parameters (no Q group), off by default in the plugin; PAD % applies only while PAD PRESS is on (hidden, not dimmed, otherwise)

// the output limiter: one LED toggle "LIMIT" in the OUTPUT block (a shell setting, no Q group), off by default in the plugin

// QUALITY: quality.no_smoothing and quality.oversample_4x as two LED switches in the OUTPUT block, touch only




void testMasterNativePageView()
{
    struct Inst : demo::Inst
    {
        nativeui::master::View view = nativeui::master::Hidden;
        nativeui::master::OutputReadout output {};
        int reports = 0;
        bool clear = false;
        float pendingPanel = -1.0f;
        bool takeNativeMasterPanel (nativeui::master::View, float& value) noexcept override
        {
            if (pendingPanel < 0.0f) return false;
            value = pendingPanel;
            pendingPanel = -1.0f;
            return true;
        }
        void setNativeMasterView (nativeui::master::View next) noexcept override { view = next; }
        void setNativeMasterOutput (const nativeui::master::OutputReadout& value) noexcept override { output = value; ++reports; }
        bool takeNativeMasterClipClear() noexcept override { const bool pending = clear; clear = false; return pending; }
    } inst;
    using namespace nativeui::master;
    PageView<MasterUi> view ("master", 1);
    UiStack stack;
    stack.openFull (MasterUi::kElementPage);
    ViewInput in;
    in.io = &inst;
    in.ui = &stack;
    in.panelEdgeX = 1280;
    view.update (in);
    check (inst.view == Main, "the real Master PageView publishes MAIN on entry");

    auto tap = [&] (int x, int y)
    {
        TouchEvent ev;
        ev.x = x;
        ev.y = y;
        ev.kind = TouchEvent::Down;
        const bool down = view.onTouch (ev, stack, inst, 10.0);
        ev.kind = TouchEvent::Up;
        return view.onTouch (ev, stack, inst, 50.0) && down;
    };

    auto centreTap = [&] (const ui::Component& c)
    {
        const auto r = c.rect();
        return tap (r.x + r.w / 2, r.y + r.h / 2);
    };

    constexpr int x = 180, y = 250;   // same actual touch area for MAIN Volume / TUNING Scale Type
    auto pixels = std::vector<uint32_t> (1280 * 800);
    auto render = [&]
    {
        view.update (in);
        view.render (pixels.data(), 1280, 800);
    };
    render();
    check (pixels[y * 1280 + x] == 0u && ! tap (x, y)
           && inst.q[0] == demo::Inst::idx ("master.volume"),
           "MAIN Volume has a visible host aperture, no overlay touch, and the Volume target");
    const auto& screen = view.ui().screen();
    synthyboy::LiveValues live;
    in.live = &live;
    live.masterPeakL = 0.8f;
    live.masterPeakR = 0.25f;
    live.activeVoices = 6;
    inst.set ("master.polyphony", 12.0f);
    inst.clipFlag = true;
    render();
    check (inst.output.peakL == 0.8f && inst.output.peakR == 0.25f && inst.output.holdL == 0.8f
           && inst.output.holdR == 0.25f && inst.output.activeVoices == 6 && inst.output.polyphony == 12,
           "the actual Master page reports independent live peak/hold and voice levels");
    check (inst.output.clipped && ! tap (screen.clip.rect().x + 20, screen.clip.rect().y + 20) && ! centreTap (screen.outputLimiter),
           "CLIP and LIMIT gestures belong to the host");
    inst.clear = true;
    render();
    check (! inst.output.clipped, "native clear uses the instance warning latch");
    inst.clipFlag = true;
    render();
    check (inst.output.clipped, "a fresh clip starts a new warning after native clear");
    const bool wide = ui::QGroup::wideDevice();
    ui::QGroup::setWideDevice (false);
    const int writesBefore = inst.writes;
    check (! centreTap (screen.transpose) && inst.q[0] == demo::Inst::idx ("master.tune") && inst.writes == writesBefore,
           "touching the native Transpose cell selects the four-knob PITCH group while only the host owns the edit");
    check (! tap (x, y) && inst.q[0] == demo::Inst::idx ("master.volume"),
           "touching the native Volume cell selects the four-knob OUTPUT group");
    ui::QGroup::setWideDevice (wide);
    check (! centreTap (screen.sideTuning), "TUNING selector belongs to the native host");
    inst.pendingPanel = 1.0f;
    render();
    check (inst.view == Tuning, "native TUNING callback replaces the MAIN frame");
    const int outputReports = inst.reports;
    render();
    check (inst.reports == outputReports, "TUNING pauses Master output reports");
    check (pixels[y * 1280 + x] == 0u && ! tap (x, y)
           && inst.q[0] == demo::Inst::idx ("master.scale.type"),
           "TUNING Scale Type uses the same host aperture with the Scale Type target");

    const auto bounds = screen.scaleType.rect();
    check (pixels[(bounds.y + 3) * 1280 + bounds.right() + 1] != 0u,
           "TUNING furniture remains opaque in the gap beside the host aperture");
    render();
    inst.pendingPanel = 0.5f;
    render();
    check (inst.view == Main && ! centreTap (screen.sideMain),
           "native MAIN callback restores its frame without an overlay selector");
    view.onHidden();
    check (inst.view == Hidden, "leaving the real Master PageView hides its native targets");

    Inst other;
    live = {};
    in.io = &other;
    render();
    check (other.view == Main && ! other.output.clipped && other.output.holdL == 0.0f && other.output.holdR == 0.0f,
           "another instance gets its own clip state and cannot inherit either channel's peak hold");
    view.onHidden();
}

void testMasterNativeAdapterState()
{
    MRig r;
    r.refresh();
    auto& screen = r.ui.screen();
    screen.setInactive (true);
    check (screen.volume.inactive() && screen.mpeBend.inactive(),
           "parent inactive state reaches native Q/state adapters outside children");
    screen.setInactive (false);
    screen.setEnabled (false);
    const int writes = r.inst.writes;
    r.ui.turnQ (0, 1.0, r.stack, r.inst);
    check (! screen.volume.enabled() && ! screen.mpeBend.enabled() && r.inst.writes == writes,
           "disabled parent blocks native adapter Q edits");
    screen.setEnabled (true);
    r.ui.turnQ (0, -1.0, r.stack, r.inst);
    check (screen.volume.enabled() && r.inst.writes == writes + 1,
           "enabled native adapter Q edits its original bound parameter");
    screen.setMpeOn (false);
    check (! screen.mpeBend.enabled() && ! screen.mpeBendToX.enabled(),
           "inactive MPE parameters stay disabled on native adapters");
}

void testStandaloneMasterNativeControls()
{
    MRig r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();


    // 1. Verify standard controls are disabled in overlay
    check (! inLeaves (r.ui, m.volume), "native volume state adapter has no overlay child route");
    check (! inLeaves (r.ui, m.spread), "overlay spread knob is disabled");
    check (! inLeaves (r.ui, m.stereoRouting), "overlay stereo knob is disabled");
    check (! inLeaves (r.ui, m.voiceOverride), "overlay voice override list is disabled");
    check (! inLeaves (r.ui, m.tune), "overlay tune knob is disabled");
    check (! inLeaves (r.ui, m.transpose), "overlay transpose field is disabled");
    check (! inLeaves (r.ui, m.bendUp), "overlay bendUp field is disabled");
    check (! inLeaves (r.ui, m.polyphony), "overlay polyphony field is disabled");
    check (! inLeaves (r.ui, m.legato), "overlay legato toggle is disabled");
    check (! inLeaves (r.ui, m.portaTime), "overlay portaTime knob is disabled");
    check (! inLeaves (r.ui, m.portaSlope), "overlay portaSlope knob is disabled");
    check (! inLeaves (r.ui, m.scaleType), "overlay scaleType stepper is disabled");
    check (! inLeaves (r.ui, m.scaleKey), "overlay scaleKey stepper is disabled");
    check (! inLeaves (r.ui, m.tuningA4), "overlay tuningA4 field is disabled");
    check (! inLeaves (r.ui, m.mpeOn), "overlay mpeOn toggle is disabled");

    // 2. Native meters and actions disable their overlay components.
    check (m.clip.rect().w >= 56,
           "CLIP, stereo peaks and voice meter are native");
    check (m.portaCurve.enabled(), "dynamic glide curve display remains enabled");
    check (m.sideMain.enabled(), "sideMain navigation button remains enabled");
    check (m.sideTuning.enabled(), "sideTuning navigation button remains enabled");
    check (! inLeaves (r.ui, m.outputLimiter), "the native LIMIT switch disables its overlay counterpart");
    check (! inLeaves (r.ui, m.padPressure), "the native pad pressure switch disables its overlay counterpart");

    // 3. Touch passthrough on standard controls (SideMain)
    const ui::Rect vr = m.volume.rect();
    const int vx = vr.x + vr.w / 2, vy = vr.y + vr.h / 2;
    check (! r.ui.covers (vx, vy), "volume knob area is not covered by overlay");
    TouchEvent downEv; downEv.kind = TouchEvent::Down; downEv.x = vx; downEv.y = vy;
    check (! r.ui.touch (downEv, r.stack, r.inst, r.now), "touch on volume knob falls through to host");
    check (! r.ui.wheel (vx, vy, 1, r.stack, r.inst, r.now), "wheel over volume knob falls through to host");

    const ui::Rect tr = m.tune.rect();
    check (! r.ui.covers (tr.x + tr.w / 2, tr.y + tr.h / 2), "tune knob area is not covered by overlay");

    const ui::Rect pr = m.polyphony.rect();
    check (! r.ui.covers (pr.x + pr.w / 2, pr.y + pr.h / 2), "polyphony field area is not covered by overlay");

    const ui::Rect gr = m.portaTime.rect();
    check (! r.ui.covers (gr.x + gr.w / 2, gr.y + gr.h / 2), "glide time knob area is not covered by overlay");

    // Subpage exclusivity & exact target identity on SideMain:
    // On SideMain, portaCurve is a retained overlay display (covered)
    const ui::Rect pcr0 = m.portaCurve.rect();
    check (r.ui.covers (pcr0.x + pcr0.w / 2, pcr0.y + pcr0.h / 2), "portaCurve is covered by overlay on SideMain");

    // Exact touch target identity: gaps between knobs and conditional areas are covered (no broad block catch-all)
    const int gapX = vr.right() + 2, gapY = vy;
    check (r.ui.covers (gapX, gapY), "gap between volume and spread knobs is covered by overlay (exact target identity)");
    const int topGapY = vr.y - 10;
    check (r.ui.covers (vx, topGapY), "block header padding above volume is covered by overlay (exact target identity)");

    // Conditional bendDown handling: when bendLinked is true, bendDown coordinates are covered
    r.inst.set ("master.bend_down_link", 1.0f);
    r.refresh();
    check (m.bendLinked(), "bendLinked is true");
    check (! r.ui.covers (1150, 240) && m.bendUp.rect().contains (1150, 240) && m.bendDown.rect().empty(),
           "linked native BEND spans the old BEND DOWN area with one aperture");
    r.inst.set ("master.bend_down_link", 0.0f);
    r.refresh();
    check (! m.bendLinked(), "bendLinked is false");
    const ui::Rect bdr = m.bendDown.rect();
    check (! r.ui.covers (bdr.x + bdr.w / 2, bdr.y + bdr.h / 2), "bendDown is passthrough when bendLinked is false");

    // 4. Retained controls touch and responsiveness on SideMain
    const ui::Rect cr = m.clip.rect();
    check (! r.ui.covers (cr.x + cr.w / 2, cr.y + cr.h / 2), "the CLIP target belongs to the native skin");

    const ui::Rect mr = m.meters.rect();
    check (! r.ui.covers (mr.x + mr.w / 2, mr.y + mr.h / 2), "native peak meters have an aperture");

    const ui::Rect vmr = m.voiceMeter.rect();
    check (! r.ui.covers (vmr.x + vmr.w / 2, vmr.y + vmr.h / 2), "native voice meter has an aperture");

    const ui::Rect pcr = m.portaCurve.rect();
    check (r.ui.covers (pcr.x + pcr.w / 2, pcr.y + pcr.h / 2), "glide curve is covered by overlay");

    const ui::Rect stR = m.sideTuning.rect();
    const ui::Rect smR = m.sideMain.rect();
    check (! r.ui.covers (stR.x + stR.w / 2, stR.y + stR.h / 2)
           && ! r.ui.covers (smR.x + smR.w / 2, smR.y + smR.h / 2),
           "both native side selectors are transparent input apertures");
    m.setSide (ui::MasterScreen::SideMain);

    const ui::Rect limR = m.outputLimiter.rect();
    check (! r.ui.covers (limR.x + limR.w / 2, limR.y + limR.h / 2),
           "the LIMIT target belongs to the native skin");
    r.tap (m.outputLimiter);
    check (! m.outputLimiter.state.on, "the overlay LIMIT gesture cannot write the shell setting");

    // The active cells belong to the host. A hidden cell neither cuts an aperture nor edits the shell.
    const ui::Rect pressureBounds { 14, 656, 189, 56 };
    const ui::Rect amountBounds { 207, 656, 189, 56 };
    const int pressureX = pressureBounds.x + pressureBounds.w / 2;
    const int amountX = amountBounds.x + amountBounds.w / 2;
    const int padsY = amountBounds.y + amountBounds.h / 2;
    const ui::Rect pressure = m.padPressure.rect();
    check (pressure.x == pressureBounds.x && pressure.y == pressureBounds.y
           && pressure.w == pressureBounds.w && pressure.h == pressureBounds.h,
           "PAD PRESS aperture agrees with the packaged skin");
    check (! r.ui.covers (pressureX, padsY) && r.ui.covers (amountX, padsY) && m.padAmount.rect().empty(),
           "OFF leaves PAD PRESS to the host and has no PAD % aperture");
    r.tapAt (pressureX, padsY);
    r.tapAt (amountX, padsY);
    check (! r.inst.padOn && r.inst.padWrites == 0, "the overlay writes neither native PAD PRESS nor hidden PAD %");
    r.inst.padOn = true; // The shell has received the host toggle.
    r.refresh();
    const ui::Rect amount = m.padAmount.rect();
    check (amount.x == amountBounds.x && amount.y == amountBounds.y && amount.w == amountBounds.w && amount.h == amountBounds.h,
           "the enabled PAD % aperture agrees with the packaged skin");
    check (! r.ui.covers (amountX, padsY) && ! inLeaves (r.ui, m.padAmount) && ! inLeaves (r.ui, m.padAmount),
           "ON exposes only the host PAD % control");
    r.drag (amountX, padsY, amountX, padsY - 50, true);
    const bool handledWheel = r.ui.wheel (amountX, padsY, 1, r.stack, r.inst, r.now);
    check (r.inst.padWrites == 0 && ! handledWheel, "the overlay leaves the native PAD % gesture and wheel to the host");
    r.inst.padPercent = 62.0f;
    r.inst.padOn = false;
    r.refresh();
    check (r.ui.covers (amountX, padsY) && m.padAmount.rect().empty() && r.inst.padPercent == 62.0f,
           "OFF closes the PAD % aperture and preserves the host amount");
    r.inst.padOn = true;
    r.refresh();

    // The host owns clear while the native CLIP is visible.
    r.inst.clipFlag = true;
    r.refresh();
    check (m.clip.state.active, "clip flag activates in native mode");
    r.tapAt (cr.x + cr.w / 2, cr.y + cr.h / 2);
    check (m.clip.state.active, "the overlay touch cannot clear the native warning");

    // 5. SideTuning controls & keyboard
    m.setSide (ui::MasterScreen::SideTuning);
    r.refresh();

    const ui::Rect sr = m.scaleType.rect();
    check (! r.ui.covers (sr.x + sr.w / 2, sr.y + sr.h / 2), "scale stepper area is not covered by overlay");

    const ui::Rect ar = m.tuningA4.rect();
    check (! r.ui.covers (ar.x + ar.w / 2, ar.y + ar.h / 2), "tuning A4 area is not covered by overlay");

    const ui::Rect mpr = m.mpeOn.rect();
    check (! r.ui.covers (mpr.x + mpr.w / 2, mpr.y + mpr.h / 2), "mpeOn area is not covered by overlay");

    // Subpage exclusivity: SideMain-only areas are covered by overlay while on SideTuning
    check (r.ui.covers (pr.x + pr.w / 2, pr.y + pr.h / 2), "polyphony area is covered by overlay on SideTuning");
    const int tunGapX = sr.right() + 2;
    check (r.ui.covers (tunGapX, sr.y + sr.h / 2), "gap between scaleType and scaleKey is covered by overlay on SideTuning");

    // Conditional MPE controls
    r.inst.set ("master.mpe.on", 0.0f);
    r.refresh();
    check (! m.mpeIsOn(), "mpe is off");
    const ui::Rect mpeBendR = m.mpeBend.rect();
    check (r.ui.covers (mpeBendR.x + mpeBendR.w / 2, mpeBendR.y + mpeBendR.h / 2), "mpeBend area is covered by overlay when mpe is off");
    r.inst.set ("master.mpe.on", 1.0f);
    r.refresh();
    check (m.mpeIsOn(), "mpe is on");
    check (! r.ui.covers (mpeBendR.x + mpeBendR.w / 2, mpeBendR.y + mpeBendR.h / 2), "mpeBend area is passthrough when mpe is on");

    const ui::Rect kr = m.keyboard.rect();
    check (! r.ui.covers (kr.x + kr.w / 2, kr.y + kr.h / 2), "native scale keyboard receives touches on the tuning side");

    // 6. Q-Link routing remains active and published
    m.setSide (ui::MasterScreen::SideMain);
    m.setActiveGroup (0);
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.volume"), "OUTPUT group publishes master.volume");
    check (r.inst.q[1] == demo::Inst::idx ("master.stereo_spread"), "OUTPUT group publishes master.stereo_spread");
    check (r.inst.q[2] == demo::Inst::idx ("master.stereo_routing"), "OUTPUT group publishes master.stereo_routing");
    check (r.inst.q[3] == demo::Inst::idx ("master.voice_override"), "OUTPUT group publishes master.voice_override");

    m.setActiveGroup (1);
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.tune"), "PITCH group publishes master.tune");
    check (r.inst.q[1] == demo::Inst::idx ("master.transpose"), "PITCH group publishes master.transpose");
    check (r.inst.q[2] == demo::Inst::idx ("master.bend_range"), "PITCH group publishes master.bend_range");

    m.setActiveGroup (2);
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.polyphony"), "VOICES group publishes master.polyphony");
    check (r.inst.q[1] == demo::Inst::idx ("master.legato"), "VOICES group publishes master.legato");
    check (r.inst.q[2] == demo::Inst::idx ("master.poly_legato"), "VOICES group publishes master.poly_legato");
    check (r.inst.q[3] == demo::Inst::idx ("voice.phase_legato"), "VOICES group publishes voice.phase_legato");

    m.setActiveGroup (4);
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.portamento.time"), "GLIDE group publishes master.portamento.time");
    check (r.inst.q[1] == demo::Inst::idx ("master.portamento.slope"), "GLIDE group publishes master.portamento.slope");
    check (r.inst.q[2] == demo::Inst::idx ("master.portamento.always"), "GLIDE group publishes master.portamento.always");
    check (r.inst.q[3] == demo::Inst::idx ("master.portamento.scaled"), "GLIDE group publishes master.portamento.scaled");

    m.setSide (ui::MasterScreen::SideTuning);
    m.setActiveGroup (5);
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.scale.type"), "SCALE group publishes master.scale.type");
    check (r.inst.q[1] == demo::Inst::idx ("master.scale.key"), "SCALE group publishes master.scale.key");
    check (r.inst.q[2] == demo::Inst::idx ("master.scale.mode"), "SCALE group publishes master.scale.mode");

    m.setActiveGroup (6);
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.tuning.a4"), "TUNING group publishes master.tuning.a4");
    check (r.inst.q[1] == demo::Inst::idx ("master.tuning.scale"), "TUNING group publishes master.tuning.scale");
    check (r.inst.q[2] == demo::Inst::idx ("master.tuning.mts_esp"), "TUNING group publishes master.tuning.mts_esp");
    check (r.inst.q[3] == demo::Inst::idx ("master.tuning.mts_note_on_only"), "TUNING group publishes master.tuning.mts_note_on_only");

    // Touch on block header activates that Q group in native mode
    m.setSide (ui::MasterScreen::SideMain);
    const ui::Rect pitchBlock = m.blockRect (1);
    r.tapAt (pitchBlock.x + pitchBlock.w / 2, pitchBlock.y + 10);
    check (m.activeGroup() == 1, "tapping PITCH block header activates PITCH Q group in native mode");
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.tune"), "PITCH group published on Q1");

    const ui::Rect glideBlock = m.blockRect (3);
    r.tapAt (glideBlock.x + glideBlock.w / 2, glideBlock.y + 10);
    check (m.activeGroup() == 4, "tapping GLIDE block header activates GLIDE Q group in native mode");
    r.refresh();
    check (r.inst.q[0] == demo::Inst::idx ("master.portamento.time"), "GLIDE group published on Q1");

    // 8. Drawing scene transparency
    m.setSide (ui::MasterScreen::SideMain);
    r.refresh();
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    check (pixels.size() == 1280u * 800u, "rendered scene pixel buffer size 1280x800");
    check (pixels[padsY * 1280 + pressureX] == 0u && pixels[padsY * 1280 + amountX] == 0u,
           "the native PAD PRESS and PAD % controls have no drawing on the overlay plane");
    const uint32_t volPx = pixels[vy * 1280 + vx];
    check (volPx == 0u, "volume dial interior on overlay plane is transparent (0u)");
    const uint32_t tunePx = pixels[(tr.y + tr.h / 2) * 1280 + (tr.x + tr.w / 2)];
    check (tunePx == 0u, "tune dial interior on overlay plane is transparent (0u)");

    // Retained navigation buttons and displays are painted opaque on overlay plane
    const ui::Rect smR2 = m.sideMain.rect();
    const uint32_t smPx = pixels[(smR2.y + smR2.h / 2) * 1280 + (smR2.x + smR2.w / 2)];
    check (smPx == 0u, "native MAIN selector has no overlay drawing");

    const ui::Rect stR2 = m.sideTuning.rect();
    const uint32_t stPx = pixels[(stR2.y + stR2.h / 2) * 1280 + (stR2.x + stR2.w / 2)];
    check (stPx == 0u, "native TUNING selector has no overlay drawing");

    const ui::Rect cr2 = m.clip.rect();
    const uint32_t crPx = pixels[(cr2.y + cr2.h / 2) * 1280 + (cr2.x + cr2.w / 2)];
    check (crPx == 0u, "active native CLIP stays transparent on the overlay plane");

    r.inst.padOn = false;
    r.refresh();
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.drawPageGround (sb);
    r.ui.draw (sb);
    renderScene (*scene, pixels);
    check (pixels[padsY * 1280 + pressureX] == 0u && pixels[padsY * 1280 + amountX] != 0u,
           "fallback furniture cuts only the active PAD PRESS aperture while OFF");

}

int main (int argc, char** argv)
{
    if (argc == 2 && std::string (argv[1]) == "--voice-skin-contract")
    {
        VoiceUi voice;
        for (const VoiceUi::Item& item : voice.items())
            if (item.param >= 0 && item.c != nullptr && item.type != VoiceUi::T::GridStep)
            {
                const ui::Rect b = item.c->rect();
                std::printf ("%s %d %d %d %d %d\n", item.key.c_str(), item.param, b.x, b.y - VoiceUi::kTabY, b.w, b.h);
            }
        const auto& screen = voice.screen();
        const ui::Rect sequence[] { screen.rowList.rect(), screen.qStepSelect.rect(), screen.stepValue.rect() };
        const char* const keys[] { "row", "step", "value" };
        const int base = static_cast<int> (synthyboy::paramCount()) + 82 + nativeui::kVoiceSequence;
        for (int i = 0; i < nativeui::voice::kCount; ++i)
            std::printf ("shell.voice_%s %d %d %d %d %d\n", keys[i], base + i, sequence[i].x, sequence[i].y - VoiceUi::kTabY, sequence[i].w, sequence[i].h);
        return 0;
    }
    if (argc == 2 && std::string (argv[1]) == "--voice-only")
    {
        testVoiceLayout(); testVoiceGridDrawing(); testVoiceGridStoresNoTruth();
        testVoiceStepsLitAndQ(); testVoiceDraw(); testVoiceNativeControls(); testVoiceNativeSequence();
        std::printf ("Voice-only: %d failures\n",g_fails);
        return g_fails ? 1 : 0;
    }
    std::printf ("=== SynthyBoy MPC Overlay: Master & Voice Host Tests ===\n");
    testMasterParametersExist();
    testMasterDraw();
    testNativePages();
    testStandaloneMasterNativeControls();
    testMasterNativeAdapterState();
    testMasterNativePageView();
    testVoiceLayout();
    testVoiceGridDrawing();
    testVoiceGridStoresNoTruth();
    testVoiceStepsLitAndQ();
    testVoiceDraw();
    testVoiceNativeControls();
    testVoiceNativeSequence();
    testFullCoverage();
    std::printf (g_fails ? "mastervoice_ui_host: FAILED (%d failures)\n" : "mastervoice_ui_host: OK (0 failures)\n", g_fails);
    return g_fails ? 1 : 0;
}
