// SynthyBoy - MPC overlay tests: Master and Voice UI (master_ui.h, voice_ui.h), the pages of the Master and Voice tabs: the parameters of the master section and of the voice control in both directions, the two
// sides of the Master page and their blocks (one Q group each, filled, every control >= 56 px with its value), the real keyboard, the meters of the engine, the lists of the steppers, the step grid (drawing
// with a finger over several columns, the step a voice plays lit, every value of the selected step), the virtual Q targets, and the scene rendering.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
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
            bad = std::to_string (c->rect().w) + " x " + std::to_string (c->rect().h);
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

void testMasterSidesAndBlocks()
{
    MRig r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    check (m.rect().w == 1280 && m.rect().h == 628 && r.stack.element() == MasterUi::kElementPage, "the page has the whole tab and is the element of the Master tab");
    check (m.side() == ui::MasterScreen::SideMain, "the first side is MAIN: OUTPUT, PITCH, VOICES, GLIDE");
    std::string bad;
    std::vector<ui::Component*> main = { &m.volume, &m.spread, &m.stereoRouting, &m.voiceOverride, &m.tune, &m.transpose, &m.bendUp, &m.bendDown, &m.bendLink, &m.polyphony, &m.legato, &m.polyLegato, &m.phaseLegato,
                                         &m.sameNote, &m.stealPriority, &m.voiceMeter, &m.portaTime, &m.portaSlope, &m.portaAlways, &m.portaScaled };
    check (sizesOk (main, bad), "every control of the side MAIN is >= 56 px high and has its minimum size " + bad);
    // each block holds its group, and the blocks are filled (the controls reach the lower part of the block)
    bool inside = true, filled = true;
    for (int g = 0; g < 4; ++g)
    {
        const ui::Rect b = m.blockRect (g), u = m.groups[g].rect();
        inside = inside && b.contains (u);
        ui::Component* all[96];
        const int n = m.collectLeaves (all, 0, 96);
        inside = inside && m.groups[g].check (all, n) == 0;
        // the VOICES group is one row of the grid of cells (64 px), the voice meter under it reaches the lower part of the block
        filled = filled && (g == 2 ? m.voiceMeter.rect().bottom() >= b.bottom() - 90 && u.h >= 64 : (g == 1 || g == 3 ? u.h >= 120 && (g == 1 ? m.bendLink.rect().bottom() : m.portaCurve.rect().bottom()) >= b.bottom() - 40 : u.bottom() >= b.bottom() - 90 && u.h >= 140));
    }
    check (inside, "the Q group of OUTPUT, PITCH, VOICES and GLIDE lies in its block and nothing else is in its frame");
    check (filled, "the groups fill their blocks (>= 140 px high, reaching the lower part; PITCH and GLIDE: a row of four cells and LINK / the curve under it)");
    check (m.volume.rect().h >= 200 && m.volume.rect().w >= 140, "the VOLUME is a big knob (" + std::to_string (m.volume.rect().w) + " x " + std::to_string (m.volume.rect().h) + ")");
    // one knob family: only VOLUME and SPREAD are large (their cell is taller, so they are drawn larger); PITCH and GLIDE use the cell grid of VOICES (four equal columns), their four Q cells have one height and width,
    // LINK is the same toggle as LEGATO under the row, and the two blocks reach the right edge and the bottom of their block
    check (m.volume.state.large && m.spread.state.large && ! m.tune.state.large && ! m.portaTime.state.large && ! m.portaSlope.state.large, "only VOLUME and SPREAD are large knobs");
    check (m.volume.rect().h >= m.tune.rect().h + 60 && m.spread.rect().h >= m.portaTime.rect().h + 60, "the large knobs have the tall cells (" + std::to_string (m.volume.rect().h) + " against " + std::to_string (m.tune.rect().h) + ")");
    {
        const int cw = m.polyphony.rect().w;
        const ui::Rect pi = m.inner (1), gi = m.inner (3);
        auto near = [] (int a, int b) { return std::abs (a - b) <= 1; };
        check (near (m.tune.rect().w, cw) && near (m.transpose.rect().w, cw) && near (m.bendUp.rect().w, cw) && near (m.bendDown.rect().w, cw) && near (m.portaTime.rect().w, cw) && near (m.portaSlope.rect().w, cw)
                   && near (m.portaAlways.rect().w, 2 * cw + m.portaSlope.rect().x - m.portaTime.rect().right()) && m.portaAlways.rect().w == m.portaScaled.rect().w,
               "PITCH and GLIDE cells are the cells of the VOICES grid (" + std::to_string (cw) + " px)");
        check (m.tune.rect().h == m.transpose.rect().h && m.transpose.rect().h == m.bendUp.rect().h && m.bendUp.rect().h == m.bendDown.rect().h && m.portaTime.rect().h == m.portaSlope.rect().h && m.portaAlways.rect().h == m.portaScaled.rect().h,
               "the four cells of the PITCH group have one height (TRANSPOSE is as tall as BEND UP); GLIDE and SLOPE one, ALWAYS and SCALED one (the wide toggle of the page)");
        check (m.tune.rect().h == m.portaTime.rect().h, "the knobs of PITCH and GLIDE have the same cell");
        check (m.bendDown.rect().right() == pi.right() && m.portaScaled.rect().right() == gi.right() && m.portaAlways.rect().right() == gi.right() && m.bendLink.rect().w == pi.w && m.bendLink.rect().bottom() <= pi.bottom() && m.portaCurve.rect().bottom() >= gi.bottom() - 2 && m.portaCurve.rect().w == gi.w,
               "PITCH and GLIDE fill their block across the width; LINK and the curve are under the row of four");
        check (! m.bendLink.state.knobLike && m.bendLink.rect().h >= 56 && ! m.bendLink.rect().overlaps (m.groups[1].rect()), "LINK is the plain toggle of the page (not a knob cell), outside the Q frame of PITCH");
    }
    // the second side
    r.tap (m.sideTuning);
    check (m.side() == ui::MasterScreen::SideTuning && m.activeGroup() == 4, "TUNING & MPE shows the second side, SCALE is the Q group");
    std::vector<ui::Component*> tun = { &m.scaleType, &m.scaleKey, &m.scaleMode, &m.tuningA4, &m.tuningScale, &m.mtsEsp, &m.mtsNoteOn, &m.mpeOn, &m.mpeBend, &m.mpeMacro, &m.mpeYModwheel, &m.mpeBendToX, &m.mpeYBipolar };
    check (sizesOk (tun, bad), "every control of the side TUNING & MPE is >= 56 px high and has its minimum size " + bad);
    check (m.keyboard.rect().w >= 1200 && m.keyboard.rect().h >= 120, "the keyboard is big (" + std::to_string (m.keyboard.rect().w) + " x " + std::to_string (m.keyboard.rect().h) + ")");
    bool inside2 = true;
    for (int g = 4; g < 8; ++g)
    {
        const ui::Rect b = m.blockRect (g < 6 ? g : 6), u = m.groups[g].rect();
        ui::Component* all[96];
        const int n = m.collectLeaves (all, 0, 96);
        inside2 = inside2 && b.contains (u) && m.groups[g].check (all, n) == 0;
    }
    check (inside2, "the groups SCALE, TUNING and the two rows of MPE lie in their blocks and nothing else is in their frames");
    r.tap (m.sideMain);
    check (m.side() == ui::MasterScreen::SideMain && m.activeGroup() < 4, "MAIN goes back");
}

void testMasterValuesAndWrites()
{
    MRig r;
    r.inst.set ("master.volume", -6.0f);
    r.inst.set ("master.tune", 12.5f);
    r.inst.set ("master.portamento.time", 120.0f);
    r.inst.set ("master.stereo_spread", 100.0f);
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    check (std::string (m.volume.state.valueText) == "-6.0 dB", "the volume is -6.0 dB (" + std::string (m.volume.state.valueText) + ")");
    check (std::string (m.tune.state.valueText).find ("ct") != std::string::npos, "the tune is in cents (" + std::string (m.tune.state.valueText) + ")");
    check (std::string (m.portaTime.state.valueText) == "120 ms", "the glide time is 120 ms (" + std::string (m.portaTime.state.valueText) + ")");
    check (std::string (m.spread.state.valueText).find ("%") != std::string::npos, "the spread is a percent (" + std::string (m.spread.state.valueText) + ")");
    check (m.voiceOverride.state.caption[0] != '\0' && std::string (m.voiceOverride.state.caption) == "VOICE OVERRIDE" && std::string (m.stereoRouting.state.caption) == "STEREO", "the steppers of OUTPUT have their names (no unlabeled stepper)");
    check (std::string (m.voiceOverride.state.text) != "" && std::string (m.voiceOverride.state.text) == "Kill", "the voice override is written by name: " + std::string (m.voiceOverride.state.text));

    const ui::Rect v = m.volume.rect();
    r.drag (v.x + v.w / 2, v.y + v.h / 2, v.x + v.w / 2, v.y + v.h / 2 - 50, true);
    check (r.inst.get ("master.volume") > -6.0f, "dragging VOLUME writes master.volume (" + std::to_string (r.inst.get ("master.volume")) + ")");
    r.tap (m.bendLink);
    check (r.inst.get ("master.bend_down_link") == (m.bendLink.state.on ? 1.0f : 0.0f), "LINK writes master.bend_down_link");
    // a number field drags
    const ui::Rect t = m.transpose.rect();
    r.drag (t.x + t.w / 2, t.y + t.h / 2, t.x + t.w / 2, t.y + t.h / 2 - 40, true);
    check (r.inst.get ("master.transpose") > 0.0f, "dragging TRANSPOSE writes master.transpose (" + std::to_string (r.inst.get ("master.transpose")) + ")");
    // a stepper opens its list in the middle
    r.tap (m.voiceOverride);
    check (r.ui.topPage() == MasterUi::kPageList && r.ui.list().state.count == static_cast<int> (synthyboy::paramTable()[demo::Inst::idx ("master.voice_override")].numChoices), "a tap on the name of a stepper opens its list");
    const ui::Rect row = r.ui.list().rowRect (1);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
    check (r.inst.get ("master.voice_override") == 1.0f && r.ui.topPage() == 0, "a row writes the choice and closes the list");
}

void testMasterGroupsAndMeters()
{
    MRig r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    check (r.inst.q[0] == demo::Inst::idx ("master.volume") && r.inst.q[1] == demo::Inst::idx ("master.stereo_spread"), "OUTPUT is the first Q group: VOLUME, SPREAD, STEREO, OVERRIDE");
    r.tap (m.portaSlope);
    check (m.activeGroup() == 3 && r.inst.q[0] == demo::Inst::idx ("master.portamento.time") && r.inst.q[1] == demo::Inst::idx ("master.portamento.slope"), "a touch in GLIDE makes it the Q group");
    r.tap (m.sideTuning);
    r.tap (m.mpeBendToX);
    check (m.activeGroup() == 7 && r.inst.q[0] == demo::Inst::idx ("master.mpe.y_modwheel") && r.inst.q[3] == -1, "the second row of MPE is a group of its own (3 members)");
    // meters
    r.tap (m.sideMain);
    r.inst.haveLive = true;
    r.inst.liveValues.stripPeak[9] = 0.8f;
    r.inst.liveValues.activeVoices = 5;
    r.refresh();
    check (std::fabs (m.meters.peakL - 0.8f) < 1.0e-6f && m.voiceMeter.activeVoices == 5, "the meters are the engine's master peak and its voice count");
    check (m.meters.holdL >= 0.8f - 1.0e-6f, "the meter has a peak hold (it stays at the highest peak)");
    r.inst.liveValues.stripPeak[9] = 0.1f;
    r.now += 40.0;
    r.refresh();
    check (m.meters.holdL > m.meters.peakL && m.meters.holdL < 0.8f + 1.0e-6f, "the peak hold falls slowly (hold " + std::to_string (m.meters.holdL) + " over the peak " + std::to_string (m.meters.peakL) + ")");
    // the glide curve follows SLOPE live
    r.inst.set ("master.portamento.slope", 6.0f);
    r.refresh();
    const float s6 = m.portaCurve.slope;
    r.inst.set ("master.portamento.slope", -4.0f);
    r.refresh();
    check (s6 == 6.0f && m.portaCurve.slope == -4.0f, "the GLIDE CURVE display follows the SLOPE parameter at every update");
    // the keyboard: C major lights 7 of the 12 notes
    r.tap (m.sideTuning);
    r.inst.set ("master.scale.type", 1.0f);
    r.inst.set ("master.scale.key", 0.0f);
    r.refresh();
    int lit = 0;
    for (int n = 0; n < 12; ++n)
        lit += ui::MiniKeyboardDisplay::isScaleNote (m.keyboard.scaleType, m.keyboard.scaleKey, n) ? 1 : 0;
    check (lit == 7 && m.keyboard.scaleType == 1, "the keyboard shows 7 of the 12 notes for a major scale");
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
        check (! sb.overflow && data->numOps > 100, "the master side " + std::to_string (side) + " fits in the buffer (" + std::to_string (data->numOps) + " ops)");
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (cv.size() == 1280u * 800u, "the CPU renderer draws the master side " + std::to_string (side));
    }
}

// ---- voice -----------------------------------------------------------------------------------------------------------------------------------------------------
void testVoiceLayout()
{
    VRig r;
    r.refresh();
    ui::VoiceScreen& v = r.ui.screen();
    check (v.rect().w == 1280 && v.rect().h == 628 && r.stack.element() == VoiceUi::kElementPage, "the page has the whole tab and is the element of the Voice tab");
    std::string bad;
    std::vector<ui::Component*> cs = { &v.length, &v.qStepSelect, &v.qRowSelect, &v.stepValue, &v.randPan, &v.randDetune, &v.randCutoff, &v.randEnv, &v.randDetune10x, &v.scaleEnv, &v.scaleLfo, &v.scaleLfoMode, &v.oscA, &v.oscB, &v.oscC, &v.oscSub, &v.oscNoise };
    check (sizesOk (cs, bad), "every control of the Voice page is >= 56 px high and has its minimum size " + bad);
    bool inside = true;
    for (int g = 0; g < 4; ++g)
    {
        ui::Component* all[96];
        const int n = v.collectLeaves (all, 0, 96);
        inside = inside && v.groups[g].check (all, n) == 0;
        if (g > 0)
            inside = inside && v.blockRect (g - 1).contains (v.groups[g].rect());
    }
    check (inside, "the Q groups lie in their blocks and nothing else is in their frames");
    check (v.grid.rect().h >= 230 && v.randPan.rect().h >= 72 && v.scaleEnv.rect().h >= 72, "the grid is tall and the knobs of the blocks have the room");
    check (v.stripRect().y >= v.grid.rect().bottom() && v.blockRect (0).y >= v.stripRect().bottom(), "the strip of the step lies between the grid and the blocks");
    check (v.randDetune10x.rect().bottom() <= v.blockRect (0).bottom(), "the blocks hold their controls");
    check (v.oscA.rect().w == v.oscB.rect().w && v.oscB.rect().w == v.oscC.rect().w &&
           v.oscC.rect().w == v.oscSub.rect().w && v.oscSub.rect().w == v.oscNoise.rect().w &&
           v.oscA.rect().h == v.oscNoise.rect().h,
           "targets are five equal cells in one grid (width " + std::to_string (v.oscA.rect().w) + ", height " + std::to_string (v.oscA.rect().h) + ")");
}

void testVoiceGridDrawing()
{
    VRig r;
    r.inst.set ("voicectl.length", 8.0f);
    r.refresh();
    ui::VoiceGrid& g = r.ui.screen().grid;
    // a stroke over four columns at the height of +60 %
    const ui::Rect c0 = g.columnRect (1), c5 = g.columnRect (5);
    const int y = c0.y + 32 + (c0.h - 62) / 2 - static_cast<int> (0.6 * (c0.h - 62) * 0.47);
    r.ui.touch ({ TouchEvent::Down, c0.x + c0.w / 2, y }, r.stack, r.inst, r.now);
    r.ui.touch ({ TouchEvent::Move, c5.x + c5.w / 2, y }, r.stack, r.inst, r.now + 16.0);   // a jump over three columns
    r.ui.touch ({ TouchEvent::Up, c5.x + c5.w / 2, y }, r.stack, r.inst, r.now + 40.0);
    r.refresh();
    int written = 0;
    for (int s = 1; s <= 5; ++s)
        written += r.inst.get ("voicectl.s" + std::to_string (s + 1) + ".pan") > 50.0f ? 1 : 0;
    check (written == 5, "a stroke over columns 2 .. 6 writes every one of them, the skipped ones too (" + std::to_string (written) + " of 5)");
    check (g.selectedStep == 5, "the selected step is the last one touched");
    // the values at the selected step are written under the grid
    r.inst.set ("voicectl.s6.detune", -25.0f);
    r.inst.set ("voicectl.s6.cutoff", 40.0f);
    r.refresh();
    check (g.value (1, 5) < -0.2f && g.value (2, 5) > 0.3f, "the grid has every row's value of the step");
    check (std::string (g.rowName (0)) == "PAN" && r.ui.screen().stripRect().h >= 40, "the strip under the grid has the room for the values of the step");
    // a row tab
    r.tapAt (g.tabRect (2).x + 10, g.tabRect (2).y + 10);
    check (g.activeRow == 2 && std::string (r.ui.screen().qRowSelect.state.text) == "CUTOFF", "a row tab selects the row");
}

void testVoiceStepsLitAndQ()
{
    VRig r;
    r.inst.set ("voicectl.length", 3.0f);
    r.inst.haveLive = true;
    for (float& s : r.inst.liveValues.voiceStep)
        s = -1.0f;
    r.inst.liveValues.voiceStep[0] = 0.0f;   // the newest voice plays step 1
    r.inst.liveValues.voiceStep[1] = 2.0f;
    r.refresh();
    check (r.ui.screen().grid.voiceStepMask == 0x05, "the steps the voices play are lit (steps 1 and 3)");
    r.inst.liveValues.voiceStep[1] = 6.0f;   // beyond the length: not lit
    r.refresh();
    check (r.ui.screen().grid.voiceStepMask == 0x01, "a step beyond the length is not lit");

    // the Q group STEP: two virtual targets, the length, the value of the selected step
    ui::VoiceScreen& v = r.ui.screen();
    check (QRouter::modeOf (r.inst.q[0]) == QRouter::kVirtual && QRouter::indexOf (r.inst.q[0]) == VoiceUi::kVirtualStep, "Q1 is the virtual step target");
    check (QRouter::modeOf (r.inst.q[1]) == QRouter::kVirtual && QRouter::indexOf (r.inst.q[1]) == VoiceUi::kVirtualRow, "Q2 is the virtual row target");
    check (r.inst.q[2] == demo::Inst::idx ("voicectl.length") && r.inst.q[3] == demo::Inst::idx ("voicectl.s1.pan"), "Q3 is the length, Q4 the value of the selected step");
    r.inst.setVirtualIn (VoiceUi::kVirtualStep, 1.0f);   // the host writes step 8
    r.refresh();
    check (v.grid.selectedStep == 7 && r.inst.q[3] == demo::Inst::idx ("voicectl.s8.pan"), "the host's write to the virtual step target selects step 8 and the value target follows");
    r.inst.setVirtualIn (VoiceUi::kVirtualRow, 0.4f);
    r.refresh();
    check (v.grid.activeRow == 2 && r.inst.q[3] == demo::Inst::idx ("voicectl.s8.cutoff"), "the virtual row target selects CUTOFF");
    // the value field writes the step
    const ui::Rect sv = v.stepValue.rect();
    r.drag (sv.x + sv.w / 2, sv.y + sv.h / 2, sv.x + sv.w / 2, sv.y + sv.h / 2 - 30, true);
    check (r.inst.get ("voicectl.s8.cutoff") > 0.0f, "the value field of the header writes the selected step (" + std::to_string (r.inst.get ("voicectl.s8.cutoff")) + ")");
    // the other blocks write their parameters, with values
    check (std::string (v.randPan.state.valueText).find ("%") != std::string::npos && std::string (v.scaleEnv.state.valueText).find ("%") != std::string::npos, "the knobs of the blocks show their values");
    r.tap (v.oscNoise);
    check (r.inst.get ("voicectl.osc.noise") != 1.0f, "a target switch writes its parameter");
    r.tap (v.scaleLfoMode);
    check (r.ui.topPage() == VoiceUi::kPageList && r.ui.list().state.count > 1, "the mode stepper opens its list");
}

void testVoiceDraw()
{
    VRig r;
    r.inst.haveLive = true;
    for (float& s : r.inst.liveValues.voiceStep)
        s = -1.0f;
    r.inst.liveValues.voiceStep[0] = 2.0f;
    r.refresh();
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 100, "the voice page fits in the buffer (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "the CPU renderer draws the voice page");
}
} // namespace

int main()
{
    std::printf ("=== SynthyBoy MPC Overlay: Master & Voice Host Tests ===\n");
    testMasterParametersExist();
    testMasterSidesAndBlocks();
    testMasterValuesAndWrites();
    testMasterGroupsAndMeters();
    testMasterDraw();
    testVoiceLayout();
    testVoiceGridDrawing();
    testVoiceStepsLitAndQ();
    testVoiceDraw();
    std::printf (g_fails ? "mastervoice_ui_host: FAILED (%d failures)\n" : "mastervoice_ui_host: OK (0 failures)\n", g_fails);
    return g_fails ? 1 : 0;
}
