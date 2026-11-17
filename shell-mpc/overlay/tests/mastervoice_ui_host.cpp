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

void testMasterSidesAndBlocks()
{
    MRig r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    check (m.rect().w == 1280 && m.rect().h == 628 && r.stack.element() == MasterUi::kElementPage, "the page has the whole tab and is the element of the Master tab");
    check (m.side() == ui::MasterScreen::SideMain, "the first side is MAIN: OUTPUT, PITCH, VOICES, GLIDE");
    std::string bad;
    std::vector<ui::Component*> main = { &m.volume, &m.spread, &m.stereoRouting, &m.voiceOverride, &m.outputLimiter, &m.tune, &m.transpose, &m.bendUp, &m.bendDown, &m.bendLink, &m.polyphony, &m.legato, &m.polyLegato, &m.phaseLegato,
                                         &m.sameNote, &m.stealPriority, &m.velocityTrack, &m.padPressure, &m.padAmount, &m.portaTime, &m.portaSlope, &m.portaAlways, &m.portaScaled };
    check (sizesOk (main, bad), "every control of the side MAIN is >= 56 px high and has its minimum size " + bad);
    // each block holds its group, and the blocks are filled (the controls reach the lower part of the block)
    bool inside = true, filled = true;
    static const int kMainGroupBlock[6] = { 0, 1, 1, 2, 2, 3 };
    for (int g = 0; g < 6; ++g)
    {
        const ui::Rect b = m.blockRect (kMainGroupBlock[g]), u = m.groups[g].rect();
        inside = inside && b.contains (u);
        ui::Component* all[96];
        const int n = m.collectLeaves (all, 0, 96);
        inside = inside && m.groups[g].check (all, n) == 0;
    }
    filled = filled && m.padAmount.rect().bottom() >= m.blockRect (2).bottom() - 90;
    filled = filled && m.bendLink.rect().bottom() >= m.blockRect (1).bottom() - 40;
    filled = filled && m.portaScaled.rect().bottom() >= m.blockRect (3).bottom() - 40 && m.portaCurve.rect().bottom() >= m.blockRect (3).bottom() - 40;
    filled = filled && m.voiceOverride.rect().bottom() >= m.blockRect (0).bottom() - 90;
    check (inside, "the Q group of OUTPUT, PITCH, VOICES and GLIDE lies in its block and nothing else is in its frame");
    check (filled, "the groups fill their blocks (reaching the lower part: PITCH has LINK under its row, VOICES three rows, GLIDE the two toggles and a curve of the whole height)");
    check (m.volume.rect().h >= 180 && m.volume.rect().w >= 110, "the knobs of OUTPUT are big (" + std::to_string (m.volume.rect().w) + " x " + std::to_string (m.volume.rect().h) + ")");
    // one knob family: the three knobs of OUTPUT are large (one size); TUNE is the medium variant (dial at the left, text beside it); GLIDE and SLOPE the common size
    check (m.volume.state.large && m.spread.state.large && m.stereoRouting.state.large && ! m.tune.state.large && ! m.portaTime.state.large && ! m.portaSlope.state.large, "VOLUME, SPREAD and STEREO are the large knobs, the others are not");
    check (m.volume.rect().w == m.spread.rect().w && std::abs (m.stereoRouting.rect().w - m.spread.rect().w) <= 1 && m.volume.rect().h == m.stereoRouting.rect().h, "the three knobs of OUTPUT share one cell size");
    {
        const ui::Rect pi = m.inner (1), gi = m.inner (3), vi = m.inner (2);
        check (m.tune.state.side && m.tune.rect().h == m.transpose.rect().h && m.transpose.rect().h == m.bendUp.rect().h && m.bendUp.rect().h == m.bendDown.rect().h && m.portaTime.rect().h == m.portaSlope.rect().h && m.portaAlways.rect().h == m.portaScaled.rect().h,
               "the four cells of the PITCH group have one height (TRANSPOSE is as tall as BEND UP); GLIDE and SLOPE one, ALWAYS and SCALED one");
        check (m.bendDown.rect().right() == pi.right() && m.bendLink.rect().bottom() <= pi.bottom() && m.bendLink.rect().y >= m.bendUp.rect().bottom() + ui::kQLane && m.portaCurve.rect().right() == gi.right() && m.portaCurve.rect().h == gi.h,
               "PITCH fills its block across the width with LINK under the bend fields; the curve of GLIDE takes the right half in the whole height");
        // the LED controls: no box and no second look: every switch of the page is a LedToggle, every short list a LedList (the type says it; the library draws no box for them)
        check (dynamic_cast<ui::LedToggle*> (&m.legato) && dynamic_cast<ui::LedToggle*> (&m.bendLink) && dynamic_cast<ui::LedToggle*> (&m.portaAlways) && dynamic_cast<ui::LedList*> (&m.sameNote) && dynamic_cast<ui::LedList*> (&m.stealPriority) && dynamic_cast<ui::LedList*> (&m.voiceOverride),
               "the switches and the short lists of MAIN are LED controls (no box around each of them)");
        // the chips of one group stand on one line where the members are in one row (the cell is the member: the toggle is drawn inside it)
        check (m.polyphony.rect().y == m.legato.rect().y && m.legato.rect().y == m.polyLegato.rect().y && m.polyLegato.rect().y == m.phaseLegato.rect().y, "VOICES 1: the four members stand in one row (the chips of the group on one line)");
        check (m.velocityTrack.rect().y == m.sameNote.rect().y && m.sameNote.rect().y == m.stealPriority.rect().y && m.stealPriority.rect().bottom() <= vi.bottom(), "VOICES 2: VEL TRACK, SAME NOTE and STEAL in one row");
        check (m.portaTime.rect().y == m.portaSlope.rect().y && m.portaAlways.rect().y == m.portaScaled.rect().y, "GLIDE: the knobs in one row, ALWAYS and SCALED in one row");
    }
    // the second side
    r.tap (m.sideTuning);
    check (m.side() == ui::MasterScreen::SideTuning && m.activeGroup() == 6, "TUNING & MPE shows the second side, SCALE is the Q group");
    std::vector<ui::Component*> tun = { &m.scaleType, &m.scaleKey, &m.scaleMode, &m.tuningA4, &m.tuningScale, &m.mtsEsp, &m.mtsNoteOn, &m.mpeOn, &m.mpeBend, &m.mpeMacro, &m.mpeYModwheel, &m.mpeBendToX, &m.mpeYBipolar };
    check (sizesOk (tun, bad), "every control of the side TUNING & MPE is >= 56 px high and has its minimum size " + bad);
    check (m.keyboard.rect().w >= 1200 && m.keyboard.rect().h >= 120, "the keyboard is big (" + std::to_string (m.keyboard.rect().w) + " x " + std::to_string (m.keyboard.rect().h) + ")");
    bool inside2 = true;
    static const int kSubGroupBlock[4] = { 4, 5, 6, 6 };
    for (int i = 0; i < 4; ++i)
    {
        const int g = 6 + i;
        const ui::Rect b = m.blockRect (kSubGroupBlock[i]), u = m.groups[g].rect();
        ui::Component* all[96];
        const int n = m.collectLeaves (all, 0, 96);
        inside2 = inside2 && b.contains (u) && m.groups[g].check (all, n) == 0;
    }
    check (inside2, "the groups SCALE, TUNING and the two rows of MPE lie in their blocks and nothing else is in their frames");
    r.tap (m.sideMain);
    check (m.side() == ui::MasterScreen::SideMain && m.activeGroup() < 6, "MAIN goes back");
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
    check (std::string (m.voiceOverride.state.title) == "VOICE OVERRIDE" && std::string (m.stereoRouting.state.label) == "STEREO", "the list and the knob of OUTPUT have their names (no unlabeled control)");
    check (m.voiceOverride.state.count == 2 && std::string (m.voiceOverride.state.names[m.voiceOverride.state.index]) == "Kill", "the voice override is a LED list of both options, the lit one written by name: " + std::string (m.voiceOverride.state.names[m.voiceOverride.state.index]));
    check (m.stereoRouting.state.valueText[0] != '\0' && std::string (m.stereoRouting.state.valueText).find ("%") != std::string::npos, "STEREO is a knob with a percentage (" + std::string (m.stereoRouting.state.valueText) + ")");

    const ui::Rect v = m.volume.rect();
    r.drag (v.x + v.w / 2, v.y + v.h / 2, v.x + v.w / 2, v.y + v.h / 2 - 50, true);
    check (r.inst.get ("master.volume") > -6.0f, "dragging VOLUME writes master.volume (" + std::to_string (r.inst.get ("master.volume")) + ")");
    r.tap (m.bendLink);
    check (r.inst.get ("master.bend_down_link") == (m.bendLink.state.on ? 1.0f : 0.0f), "LINK writes master.bend_down_link");
    // a number field drags
    const ui::Rect t = m.transpose.rect();
    r.drag (t.x + t.w / 2, t.y + t.h / 2, t.x + t.w / 2, t.y + t.h / 2 - 40, true);
    check (r.inst.get ("master.transpose") > 0.0f, "dragging TRANSPOSE writes master.transpose (" + std::to_string (r.inst.get ("master.transpose")) + ")");
    // a LED list: one tap on the row writes the choice, no list pops up
    {
        const ui::Rect row = m.voiceOverride.rowRect (1);
        r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
        check (r.inst.get ("master.voice_override") == 1.0f && r.ui.topPage() == 0, "a tap on a row of the LED list writes the choice (no pop-up)");
        const ui::Rect s5 = m.stealPriority.rowRect (4);
        r.tapAt (s5.x + s5.w / 2, s5.y + s5.h / 2);
        check (r.inst.get ("master.voice.steal_priority") == 4.0f && m.stealPriority.state.count == 5, "the STEAL list shows its five options and the fifth one is written by a tap");
    }
    // a long list stays a stepper and opens its list in the middle
    r.tap (m.sideTuning);
    r.tap (m.scaleType);
    check (r.ui.topPage() == MasterUi::kPageList && r.ui.list().state.count == static_cast<int> (synthyboy::paramTable()[demo::Inst::idx ("master.scale.type")].numChoices), "a tap on the name of a long list (SCALE) opens its list");
    const ui::Rect row = r.ui.list().rowRect (1);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
    check (r.inst.get ("master.scale.type") == 1.0f && r.ui.topPage() == 0, "a row writes the choice and closes the list");
    r.tap (m.sideMain);
}

void testMasterGroupsAndMeters()
{
    MRig r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    check (r.inst.q[0] == demo::Inst::idx ("master.volume") && r.inst.q[1] == demo::Inst::idx ("master.stereo_spread"), "OUTPUT is the first Q group: VOLUME, SPREAD, STEREO, OVERRIDE");
    r.tap (m.portaSlope);
    check (m.activeGroup() == 5 && r.inst.q[0] == demo::Inst::idx ("master.portamento.time") && r.inst.q[1] == demo::Inst::idx ("master.portamento.slope"), "a touch in GLIDE makes it the Q group");
    r.tap (m.bendLink);
    check (m.activeGroup() == 2 && r.inst.q[0] == demo::Inst::idx ("master.bend_down_link"), "a touch on LINK makes it the Q group");
    r.tap (m.velocityTrack);
    check (m.activeGroup() == 4 && r.inst.q[0] == demo::Inst::idx ("master.velocity_track"), "a touch on VEL TRACK makes VOICES 2 the Q group");
    r.tap (m.sideTuning);
    check (m.activeGroup() == 6, "switching to TUNING restores SCALE default Q group");
    r.tap (m.mpeBendToX);
    check (m.activeGroup() == 9 && r.inst.q[0] == demo::Inst::idx ("master.mpe.y_modwheel") && r.inst.q[3] == -1, "the second row of MPE is a group of its own (3 members)");
    r.ui.forgetQGroup();
    r.refresh();
    check (m.activeGroup() == 6, "forgetQGroup restores default group on tuning side");
    r.tap (m.sideMain);
    r.ui.forgetQGroup();
    r.refresh();
    check (m.activeGroup() == 0, "forgetQGroup restores default group on main side");
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
    // the numbers of the dB scale never run into each other, on the meter of the page and on a short one
    for (int h : { m.meters.rect().h, 90, 120, 200, 400 })
    {
        ui::PeakMeterDisplay pm;
        pm.place ({ 10, 10, 90, h });
        ui::Rect box[ui::PeakMeterDisplay::kNumMarks];
        bool shown[ui::PeakMeterDisplay::kNumMarks];
        pm.scaleLabels (box, shown);
        bool apart = shown[0] && shown[ui::PeakMeterDisplay::kNumMarks - 1], inside = true;
        int last = -1000, writes = 0;
        for (int i = 0; i < ui::PeakMeterDisplay::kNumMarks; ++i)
            if (shown[i])
            {
                apart = apart && box[i].y >= last;
                last = box[i].bottom();
                inside = inside && pm.rect().contains (box[i]);
                ++writes;
            }
        check (apart && inside && writes >= 3, "the scale numbers of a meter " + std::to_string (h) + " px tall do not overlap and stand inside it (" + std::to_string (writes) + " of 5 written)");
    }
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
    // keyboard touch targets and note selection
    const auto kr = m.keyboard.computeRects();
    bool allKeysWide = true;
    for (int i = 0; i < 7; ++i) if (kr.white[i].w < 56) allKeysWide = false;
    for (int i = 0; i < 5; ++i) if (kr.black[i].w < 56) allKeysWide = false;
    check (allKeysWide, "every key of the keyboard is at least 56 px wide (touch target)");
    {   // N2: the black keys are narrower than the white ones (under 0.5) and sit at the piano's positions (centres 0.877, 2.04, 3.79, 4.95, 6.12 white widths from the C)
        const double wk = kr.white[0].w, x0 = kr.white[0].x, std5[5] = { 0.877, 2.040, 3.790, 4.950, 6.120 };
        bool pos = true, narrow = true;
        for (int i = 0; i < 5; ++i)
        {
            pos = pos && std::fabs ((kr.black[i].x + kr.black[i].w * 0.5 - x0) / wk - std5[i]) < 0.03;
            narrow = narrow && kr.black[i].w < 0.5 * wk && kr.black[i].h < kr.white[0].h;
        }
        check (pos && narrow, "the black keys are narrow (under half a white key) and at the standard positions");
    }
    // tap on black key F# (black key index 2, note 6)
    r.tapAt (kr.black[2].x + kr.black[2].w / 2, kr.black[2].y + kr.black[2].h / 2);
    check (r.inst.get ("master.scale.key") == 6.0f, "tap on F# black key sets scale key to 6 (F#)");
    // tap on white key G (white key index 4, note 7)
    r.tapAt (kr.white[4].x + kr.white[4].w / 2, kr.white[4].bottom() - 15);
    check (r.inst.get ("master.scale.key") == 7.0f, "tap on G white key sets scale key to 7 (G)");
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
    std::vector<ui::Component*> cs = { &v.length, &v.qStepSelect, &v.rowList, &v.stepValue, &v.randPan, &v.randDetune, &v.randCutoff, &v.randEnv, &v.randDetune10x, &v.scaleEnv, &v.scaleLfo, &v.scaleLfoMode, &v.oscA, &v.oscB, &v.oscC, &v.oscSub, &v.oscNoise };
    check (sizesOk (cs, bad), "every control of the Voice page is >= 56 px high and has its minimum size " + bad);
    bool inside = true;
    static const int kVoiceGroupBlock[6] = { -1, 0, 0, 1, 2, 2 };
    for (int g = 0; g < 6; ++g)
    {
        ui::Component* all[96];
        const int n = v.collectLeaves (all, 0, 96);
        inside = inside && v.groups[g].check (all, n) == 0;
        if (g > 0)
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
    check (g.activeRow == 2 && vs.rowList.state.index == 2 && r.inst.q[1] != -1 && QRouter::indexOf (r.inst.q[1]) == VoiceUi::kVirtualRow, "a tap on CUTOFF selects the row; the list is Q2 of the header group (the virtual row target)");
    check (vs.groups[0].member (1) == &vs.rowList && vs.groups[0].frameless() && vs.groups[0].numMembers() == 4, "the header group is [STEP, the ROW list, LENGTH, VALUE] and has no frame (its members lie apart)");
    {
        // the VALUE field shows the value the way the grid cell does: a centred pan is "MID", not "0"
        r.tapAt (vs.rowList.rowRect (0).x + vs.rowList.rowRect (0).w / 2, vs.rowList.rowRect (0).y + vs.rowList.rowRect (0).h / 2);
        r.refresh();
        char want[16], got[16];
        ui::VoiceGrid::formatValue (0, g.value (0, g.selectedStep), want, sizeof want);
        ui::NumberField::formatValue (vs.stepValue.state, got);
        check (std::string (got) == std::string (want) && (g.value (0, g.selectedStep) != 0.0f || std::string (got) == "MID"), "the VALUE field of the PAN row has the format of the grid cell (" + std::string (got) + " / " + std::string (want) + ")");
        r.tapAt (vs.rowList.rowRect (2).x + vs.rowList.rowRect (2).w / 2, vs.rowList.rowRect (2).y + vs.rowList.rowRect (2).h / 2);
        r.refresh();
    }
    check (std::string (vs.stepValue.state.label) == "CUTOFF", "the VALUE field names the row (" + std::string (vs.stepValue.state.label) + ")");
    // one text for one value: the VALUE field reads as the cell of the grid and the strip do ("MID", "0 c", "40%", "25L")
    {
        char want[16], got[16];
        ui::VoiceGrid::formatValue (2, g.value (2, g.selectedStep), want, sizeof want);
        ui::NumberField::formatValue (vs.stepValue.state, got);
        check (std::string (got) == want && std::string (got) != "0" && std::string (got).find ('%') != std::string::npos, "the VALUE field of CUTOFF reads like the strip: " + std::string (got));
        const int savedRow = g.activeRow;
        g.activeRow = 0;
        r.inst.set ("voicectl.s" + std::to_string (g.selectedStep + 1) + ".pan", 0.0f);
        r.refresh (2);
        ui::NumberField::formatValue (vs.stepValue.state, got);
        check (std::string (got) == "MID", "the VALUE field of PAN at the middle reads MID, as the cell does (" + std::string (got) + ")");
        g.activeRow = savedRow;
        r.refresh (2);
    }
    check (vs.length.rect().w >= 160 && vs.stepValue.rect().w >= 180 && vs.qStepSelect.rect().right() <= vs.length.rect().x, "LENGTH and VALUE have the room the ROW stepper left (" + std::to_string (vs.length.rect().w) + ", " + std::to_string (vs.stepValue.rect().w) + " px)");
    check (r.ui.screen().turnQ (1, 1.0).index == 3, "a Q-Link step on Q2 moves the row on (the list)");
    r.refresh();
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
    r.tap (v.randDetune10x);
    r.refresh();
    check (v.activeGroup() == 2 && r.inst.q[0] == demo::Inst::idx ("voicectl.rand.detune10x"), "tapping 10x DETUNE activates group 2");
    r.tap (v.scaleEnv);
    r.refresh();
    check (v.activeGroup() == 3 && r.inst.q[0] == demo::Inst::idx ("voicectl.scale.env"), "tapping SCALING activates group 3");
    r.tap (v.oscNoise);
    r.refresh();
    check (v.activeGroup() == 5 && r.inst.q[0] == demo::Inst::idx ("voicectl.osc.noise"), "tapping NOISE activates group 5");
    check (r.inst.get ("voicectl.osc.noise") != 1.0f, "a target switch writes its parameter");
    r.ui.forgetQGroup();
    r.refresh();
    check (v.activeGroup() == 0, "forgetQGroup restores group 0");
    {
        const ui::Rect row1 = v.scaleLfoMode.rowRect (1);
        r.tapAt (row1.x + row1.w / 2, row1.y + row1.h / 2);
        r.refresh();
        check (r.inst.get ("voicectl.scale.lfo_mode") == 1.0f && v.scaleLfoMode.state.index == 1 && v.activeGroup() == 3, "a tap on the second option of the LFO time mode selects it (parameter, lit dot, group SCALING)");
        const float before = r.inst.get ("voicectl.osc.a");
        r.tap (v.oscA);
        r.refresh();
        check (r.inst.get ("voicectl.osc.a") != before && v.oscA.state.on == (r.inst.get ("voicectl.osc.a") > 0.5f), "a tap on a target LED toggle flips its parameter and its dot");
    }
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

    std::unordered_set<int> masterQParams;
    for (int g = 0; g < masterUi.screen().numGroups(); ++g)
    {
        masterUi.screen().setActiveGroup (g);
        demo::Inst inst;
        UiStack stack;
        page::Input in;
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
                inQ = voiceQParams.count (static_cast<int> (i)) > 0;
            }
        }
        else
        {
            onPage = masterKeys.count (key) > 0;
            inQ = masterQParams.count (static_cast<int> (i)) > 0;
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

// the plugin-wide pad pressure layer: two controls in the VOICES block, shell parameters (no Q group), off by default in the plugin
void testMasterPadPressure()
{
    MRig r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    check (m.padPressure.enabled() && m.padAmount.enabled() && ! m.padPressure.state.on && m.padAmount.state.value == 35.0, "pad pressure: the two controls show the layer (off, 35 %)");
    check (m.padPressure.rect().h >= 56 && m.padAmount.rect().h >= 56 && m.padPressure.rect().w >= 56 && m.padAmount.rect().w >= 56 && m.padPressure.sizeOk() && m.padAmount.sizeOk(), "pad pressure: touch targets of at least 56 pixels");
    check (! m.padPressure.rect().overlaps (m.voiceMeter.rect()) && ! m.padAmount.rect().overlaps (m.voiceMeter.rect()) && ! m.padPressure.rect().overlaps (m.padAmount.rect()) && m.blockRect (2).contains (m.padAmount.rect()) && m.blockRect (2).contains (m.padPressure.rect()), "pad pressure: inside the VOICES block, over nothing");
    r.tap (m.padPressure);
    check (r.inst.padOn && r.inst.padWrites == 1, "a tap on PAD PRESS switches the layer on");
    r.refresh();
    check (m.padPressure.state.on, "... and the control shows it");
    const ui::Rect a = m.padAmount.rect();
    r.drag (a.x + a.w / 2, a.y + a.h / 2, a.x + a.w / 2, a.y + a.h / 2 - 60, true);
    check (r.inst.padOn && r.inst.padPercent > 35.0f && r.inst.padPercent <= 100.0f, "dragging PAD % sets the amount (" + std::to_string (r.inst.padPercent) + ")");
    check (! r.ui.item (MasterUi::kPadPressure) || r.ui.item (MasterUi::kPadPressure)->param < 0, "the two are not engine parameters");
}

// the output limiter: one LED toggle "LIMIT" in the OUTPUT block (a shell setting, no Q group), on by default in the plugin
void testMasterOutputLimiter()
{
    MRig r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    check (m.outputLimiter.enabled() && m.outputLimiter.state.on, "output limiter: LIMIT shows the setting (on by default)");
    check (m.outputLimiter.rect().h >= 56 && m.outputLimiter.rect().w >= 56 && m.outputLimiter.sizeOk(), "output limiter: touch target of at least 56 pixels");
    check (m.blockRect (0).contains (m.outputLimiter.rect()) && ! m.outputLimiter.rect().overlaps (m.voiceOverride.rect()) && ! m.outputLimiter.rect().overlaps (m.stereoRouting.rect())
               && ! m.outputLimiter.rect().overlaps (m.spread.rect()) && ! m.outputLimiter.rect().overlaps (m.volume.rect()) && ! m.outputLimiter.rect().overlaps (m.meters.rect()),
           "output limiter: inside the OUTPUT block, over nothing");
    r.tap (m.outputLimiter);
    check (! r.inst.limiterOn && r.inst.limiterWrites == 1, "a tap on LIMIT switches the limiter off");
    r.refresh();
    check (! m.outputLimiter.state.on, "... and the control shows it");
    r.tap (m.outputLimiter);
    check (r.inst.limiterOn && r.inst.limiterWrites == 2, "a second tap switches it on again");
    check (! r.ui.item (MasterUi::kLimiter) || r.ui.item (MasterUi::kLimiter)->param < 0, "the switch is not an engine parameter");
}

int main()
{
    std::printf ("=== SynthyBoy MPC Overlay: Master & Voice Host Tests ===\n");
    testMasterParametersExist();
    testMasterSidesAndBlocks();
    testMasterValuesAndWrites();
    testMasterGroupsAndMeters();
    testMasterDraw();
    testMasterPadPressure();
    testMasterOutputLimiter();
    testVoiceLayout();
    testVoiceGridDrawing();
    testVoiceStepsLitAndQ();
    testVoiceDraw();
    testFullCoverage();
    std::printf (g_fails ? "mastervoice_ui_host: FAILED (%d failures)\n" : "mastervoice_ui_host: OK (0 failures)\n", g_fails);
    return g_fails ? 1 : 0;
}
