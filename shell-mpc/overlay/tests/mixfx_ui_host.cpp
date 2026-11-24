// SynthyBoy - MPC overlay tests: the MIXER and FX UI (mixer_ui.h, fx_ui.h), the pages of the Mixer and FX tabs: layout and touch targets >= 56 px, the values in the parameters' own units, the parameters in both
// directions, the Q groups (one frame around the column of a strip, the row of knobs of an effect), the destination list, the meters of the engine, the chain of the effects (add, select, power, rack, remove,
// move), every effect type (names, values, no empty surface), the displays, and the CPU reference renderer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../fx_ui.h"
#include "../gpu/cell_plan.h"
#include "../mixer_ui.h"
#include "demo_common.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

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

using MixRig = demo::Rig<MixerUi>;
struct FxRig : demo::Rig<FxUi>
{
    // Exercise the overlay-only editor (also used by Mixer PEEK).
    FxRig() { ui.setNativeControls (false); }
};

bool isShown (const ui::Composite& p, const ui::Component* target)
{
    for (int c = 0; c < p.numChildren(); ++c)
        if (p.child (c) == target && p.childShown (c))
            return true;
    return false;
}

// ---- the mixer -------------------------------------------------------------------------------------------------------------------------------------------------
void enableMixerSources (MixRig& r)
{
    for (const char* key : { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on", "filter1.on", "filter2.on" })
        r.inst.set (key, 1.0f);
}

void testMixerLayout()
{
    MixRig r;
    enableMixerSources (r);
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();
    check (m.rect().w == 1280 && m.rect().h == 628 && r.stack.element() == MixerUi::kElementPage, "the page has the whole tab and is the element of the Mixer tab");
    bool targets = true, family = true, inside = true, uniformHeader = true;
    for (int s = 0; s < 10; ++s)
    {
        const ui::MixerStrip& st = m.strips[s];
        targets = targets && st.power.rect().h >= 56 && (! st.hasDestination() || st.destButton.rect().h >= 56) && st.fader.rect().h >= 150 && st.sizeOk();
        if (st.hasPan())
            targets = targets && st.pan.rect().h >= 56 && st.send1.rect().h >= 56 && st.send2.rect().h >= 56;
        if (s < 7)
            family = family && st.pan.rect().w == m.strips[0].pan.rect().w && st.fader.rect().h == m.strips[0].fader.rect().h;
        else
            family = family && st.fader.rect().h >= 380; // BUS 1, BUS 2 and MASTER have full-height faders
        inside = inside && (! st.hasPan() || st.rect().contains (st.pan.rect())) && st.rect().contains (st.fader.rect());
        ui::Component* all[64];
        const int n = m.collectLeaves (all, 0, 64);
        inside = inside && m.groups[ui::MixerFullScreen::groupOfStrip (s)].check (all, n) == 0 && m.groups[ui::MixerFullScreen::groupOfStrip (s)].member (ui::MixerFullScreen::slotOfStrip (s)) == &st.fader;   // the group holds the strip's fader
        uniformHeader = uniformHeader && st.power.rect().h == 56 && (! st.hasDestination() || st.destButton.rect().h == 56);
    }
    check (targets, "every cell of every strip is >= 56 px high, the fader >= 150 px, and the strip has its minimum size");
    check (family, "all strips are one family (standard fader and full-height fader families)");
    check (inside, "the Q group of every strip lies in its strip and nothing else is in its frame");
    check (uniformHeader, "all ten strips have uniform 56 px header and route button heights");
    check (m.strips[0].pan.rect().w >= 96 && m.strips[0].pan.rect().h == 56, "the PAN is a 56 px slider cell, not a speck (" + std::to_string (m.strips[0].pan.rect().w) + " x " + std::to_string (m.strips[0].pan.rect().h) + ")");
}

// the bold cut of the Mixer (owner 2026-11-21): the dead OUT button of the MASTER strip is gone (hidden, not a greyed-out dummy), the routing bar is a picture with plain labels, everything else stays
void testMixerBoldCut()
{
    MixRig r;
    enableMixerSources (r);
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();
    int controls = 0;
    bool destOnly9 = true;
    for (int s = 0; s < 10; ++s)
    {
        ui::Component* all[16];
        const int n = m.strips[s].collectLeaves (all, 0, 16);
        controls += n;
        bool dest = false;
        for (int i = 0; i < n; ++i)
            dest = dest || all[i] == &m.strips[s].destButton;
        destOnly9 = destOnly9 && (dest == (s != 9));
    }
    check (controls == 50, "the Mixer shows 50 controls: 7 strips x 6, the 2 buses x 3 and the MASTER x 2, no OUT button (51 before the cut) (" + std::to_string (controls) + ")");
    check (destOnly9 && ! m.strips[9].hasDestination() && m.strips[9].destButton.rect().empty(), "the MASTER strip has no destination button at all (hidden, no rectangle), every other strip has one");
    // a touch where the button was (under the power glyph of the MASTER) opens nothing and writes nothing
    const ui::Rect pw = m.strips[9].power.rect();
    r.tapAt (pw.x + pw.w / 2, pw.bottom() + 30);
    check (! r.ui.listOpen() && r.stack.top() == MixerUi::kElementPage, "a tap under the MASTER power opens no destination list");
    for (int s = 0; s < 9; ++s)
        check (m.strips[s].hasDestination() && m.strips[s].destButton.rect().h == 56 && m.strips[s].destButton.rect().w > 0, "strip " + std::to_string (s) + " keeps its destination button");
    // inert destination labels of the routing bar (DIRECT, MASTER) take no touch
    const int sel = m.routingBar.selectedStrip;
    const ui::Rect rb = m.routingBar.rect();
    r.tapAt (rb.x + static_cast<int> (rb.w * ui::MixerRoutingBar::kTargetX[5]), rb.y + 15);   // the "DIRECT" label
    check (! r.ui.listOpen() && r.ui.topPage() != MixerUi::kPagePeek && m.routingBar.selectedStrip == sel, "a tap on an inert destination label of the routing bar does nothing (no touch, the selection stays)");
    // the LEVELS layout: [SUB, A, B, C], [NOISE, F1, F2, MAIN], [BUS 1, BUS 2]; PAN, the sends and the destinations are touch only
    check (ui::MixerFullScreen::kMaxGroups == 3 && m.groups[0].numMembers() == 4 && m.groups[1].numMembers() == 4 && m.groups[2].numMembers() == 2 && m.groups[1].frameless(), "the Q groups are the levels: four, four (frameless: the buses lie between) and two members");
    check (m.groups[1].member (3) == &m.strips[9].fader && m.groups[2].member (0) == &m.strips[7].fader && m.groups[2].member (2) == nullptr, "the fourth member of NOISE, F1, F2 is the MAIN fader, the third group is the two buses");
}

void testMixerValuesAndWrites()
{
    MixRig r;
    r.inst.set ("osc1.level", 50.0f);
    r.inst.set ("filter1.level", 0.0f);
    r.inst.set ("bus1.level", -6.0f);
    r.inst.set ("osc1.pan", -35.0f);
    r.inst.set ("osc1.bus1_send", 40.0f);
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();
    check (std::string (m.strips[1].fader.state.valueText) == "50.0%", "the level of A is a percent (" + std::string (m.strips[1].fader.state.valueText) + ")");
    check (std::string (m.strips[5].fader.state.valueText).find ("dB") != std::string::npos, "the level of F1 is in dB (" + std::string (m.strips[5].fader.state.valueText) + ")");
    check (std::string (m.strips[7].fader.state.valueText).find ("dB") != std::string::npos, "the level of BUS 1 is in dB (" + std::string (m.strips[7].fader.state.valueText) + ")");
    check (m.strips[1].fader.state.numTicks == 5 && std::string (m.strips[1].fader.state.tickText[4]) == "100" && std::string (m.strips[5].fader.state.tickText[0]) == "0", "the faders have their scale: 0 .. 100 for a percent, 0 / -12 / -24 / -48 for a dB level");
    check (m.strips[7].fader.state.isFine && m.strips[7].fader.state.numTicks == 8, "BUS 1 fader has a finer dB scale with 8 marks (+6 to -48 dB)");
    check (m.strips[9].fader.state.isFine && m.strips[9].fader.state.numTicks == 8, "MASTER fader has a finer dB scale with 8 marks (+6 to -48 dB)");
    check (m.strips[1].fader.rect().y < m.strips[1].pan.rect().y && m.strips[1].pan.rect().y < m.strips[1].send1.rect().y && m.strips[1].send1.rect().y < m.strips[1].send2.rect().y, "the strip reads top down in the order of its Q numbers: LEVEL 1, PAN 2, B1 3, B2 4");
    check (std::string (m.strips[1].pan.state.valueText) == "L 35", "the pan is L 35 (" + std::string (m.strips[1].pan.state.valueText) + ")");
    check (std::string (m.strips[1].send1.state.valueText) == "40.0%", "the send is 40.0% (" + std::string (m.strips[1].send1.state.valueText) + ")");
    check (! m.strips[7].hasPan() && ! m.strips[7].hasSend1() && ! m.strips[7].hasSend2() && m.strips[7].hasLevel(), "a bus has no pan and no sends: those controls are left out (not drawn dim), the level stays");

    // writes: the fader, the pan, the sends, the switch
    const ui::Rect f = m.strips[1].fader.rect();
    r.drag (f.x + f.w / 2, f.y + f.h / 2, f.x + f.w / 2, f.y + f.h / 2 - 40, true);
    check (r.inst.get ("osc1.level") > 55.0f, "dragging the fader writes osc1.level (" + std::to_string (r.inst.get ("osc1.level")) + ")");
    const ui::Rect pn = m.strips[1].pan.rect();
    r.drag (pn.x + pn.w / 2, pn.y + 40, pn.x + pn.w / 2 + 40, pn.y + 40, true);
    check (r.inst.get ("osc1.pan") > -35.0f, "dragging the pan slider writes osc1.pan (" + std::to_string (r.inst.get ("osc1.pan")) + ")");
    const ui::Rect sd = m.strips[1].send2.rect();
    r.drag (sd.x + 20, sd.y + 40, sd.x + sd.w - 10, sd.y + 40, true);
    check (r.inst.get ("osc1.bus2_send") > 40.0f, "dragging the B2 send writes osc1.bus2_send (" + std::to_string (r.inst.get ("osc1.bus2_send")) + ")");
    r.tap (m.strips[1].power);
    check (r.inst.get ("osc1.on") == 0.0f && std::string (m.strips[1].power.state.label) == "A", "tapping the name of the strip switches the oscillator off");
    const int busWrites = r.inst.writes;
    r.tap (m.strips[7].power);
    check (r.inst.writes == busWrites + 1 && r.inst.lastWritten == demo::Inst::idx ("bus1.fx_bypass"),
           "the overlay fallback controls BUS 1 rack bypass");
    r.tap (m.strips[1].power); // enable the oscillator before editing its level
    // a double tap on the fader resets it
    r.tapAt (f.x + f.w / 2, f.y + f.h / 2, 30);
    r.tapAt (f.x + f.w / 2, f.y + f.h / 2, 30);
    check (r.inst.get ("osc1.level") == synthyboy::paramTable()[demo::Inst::idx ("osc1.level")].def, "a double tap resets the fader to its default");
}

void testMixerGroupsAndList()
{
    MixRig r;
    enableMixerSources (r);
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();
    check (m.activeGroup() == 0 && r.inst.q[0] == demo::Inst::idx ("sub.level") && r.inst.q[1] == demo::Inst::idx ("osc1.level") && r.inst.q[3] == demo::Inst::idx ("osc3.level"), "the first group is the Q group: the levels of SUB, A, B, C");
    r.tap (m.strips[6].send1);
    check (m.activeGroup() == 1 && m.selectedStrip() == 6 && r.inst.q[0] == demo::Inst::idx ("noise.level") && r.inst.q[1] == demo::Inst::idx ("filter1.level") && r.inst.q[2] == demo::Inst::idx ("filter2.level") && r.inst.q[3] == demo::Inst::idx ("master.volume"),
           "a touch in a strip selects it and makes the group of its level the Q group: NOISE, F1, F2, MAIN");
    const ui::Rect gr = m.groups[0].rect();
    check (gr.w >= 400 && m.groups[0].member (0) == &m.strips[0].fader && ! m.groups[0].frameless(), "the frame of the first group is one row around the four faders");
    r.inst.set ("sub.on", 1);
    r.refresh();
    r.tap (m.strips[0].destButton);
    check (r.ui.listOpen() && r.stack.top() == MixerUi::kPageList && r.ui.list().state.count == static_cast<int> (synthyboy::paramTable()[demo::Inst::idx ("sub.destination")].numChoices), "a tap on the destination opens the list of its choices");
    const ui::Rect row1 = r.ui.list().rowRect (1);
    r.tapAt (row1.x + row1.w / 2, row1.y + row1.h / 2);
    check (r.inst.get ("sub.destination") == 1.0f && ! r.ui.listOpen(), "choosing a row writes the destination and closes the list");
    // the meters
    r.inst.haveLive = true;
    r.inst.liveValues.stripPeak[0] = 0.82f;
    r.inst.liveValues.stripPeak[9] = 0.94f;
    r.refresh();
    check (std::fabs (m.strips[0].fader.state.peak - 0.82f) < 0.001f && std::fabs (m.strips[9].fader.state.peak - 0.94f) < 0.001f && m.strips[3].fader.state.peak == 0.0f, "the meters follow the engine's strip peaks");
    check (m.strips[0].fader.state.hasPeakHold && m.strips[0].fader.state.peakHold >= 0.82f, "strip 0 meter has peak hold matching engine peak");
    check (m.strips[9].fader.state.hasPeakHold && m.strips[9].fader.state.peakHold >= 0.94f, "MASTER meter has peak hold matching engine peak");
}

// The Q groups are the LEVELS of the strips ([SUB, A, B, C], [NOISE, F1, F2, MAIN], [BUS 1, BUS 2]); a touch on a strip selects it (the routing line follows) and makes the group of its fader the active one. A strip member the engine has
// no parameter for (BUS 1, BUS 2 and MASTER have a level only) is left out of the strip: its control is not drawn and not touchable; the strip keeps its height and the other controls their places.
void testMixerQPerStrip()
{
    const char* const keys[10][4] = {
        { "sub.level", "sub.pan", "sub.bus1_send", "sub.bus2_send" },
        { "osc1.level", "osc1.pan", "osc1.bus1_send", "osc1.bus2_send" },
        { "osc2.level", "osc2.pan", "osc2.bus1_send", "osc2.bus2_send" },
        { "osc3.level", "osc3.pan", "osc3.bus1_send", "osc3.bus2_send" },
        { "noise.level", "noise.pan", "noise.bus1_send", "noise.bus2_send" },
        { "filter1.level", "filter1.pan", "filter1.bus1_send", "filter1.bus2_send" },
        { "filter2.level", "filter2.pan", "filter2.bus1_send", "filter2.bus2_send" },
        { "bus1.level", "", "", "" },
        { "bus2.level", "", "", "" },
        { "master.volume", "", "", "" } };
    MixRig r;
    enableMixerSources (r);
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();
    bool all = true;
    std::string why;
    auto fail = [&] (int s, const char* what) { if (all) why = std::string (" (strip ") + std::to_string (s) + ": " + what + ")"; all = false; };
    // the pixels the hidden controls would have drawn: the strip's own ground (a vertical gradient: every row of it is one colour), nothing else
    auto flat = [] (const std::vector<uint32_t>& cv, ui::Rect a) -> bool
    {
        for (int y = a.y; y < a.bottom(); ++y)
            for (int x = a.x; x < a.right(); ++x)
                if (cv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] != cv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (a.x)])
                    return false;
        return true;
    };
    const ui::Rect pan0 = m.strips[0].pan.rect();
    for (int s = 0; s < 10; ++s)
    {
        ui::MixerStrip& st = m.strips[s];
        // a tap in the strip's name cell would switch it; a tap on the fader track alone changes nothing
        r.tapAt (st.fader.rect().x + 6, st.fader.rect().y + 6);
        r.refresh();
        const int gOf = ui::MixerFullScreen::groupOfStrip (s), slot = ui::MixerFullScreen::slotOfStrip (s);
        if (m.activeGroup() != gOf || ! m.groups[gOf].active() || m.selectedStrip() != s)
            fail (s, "the strip's group is not the active one");
        if (m.routingBar.selectedStrip != s)
            fail (s, "the routing bar does not follow the strip");
        for (int g = 0; g < ui::MixerFullScreen::kMaxGroups; ++g)
            if (g != gOf && m.groups[g].active())
                fail (s, "another group is active too");
        // Q 1 .. 4 are the four levels of the active group (the 4 knob device: the active group first), a chip only on a fader that has a target
        for (int q = 0; q < 4; ++q)
        {
            const int strip = ui::MixerFullScreen::kStripOfSlot[gOf][q];
            const int want = strip >= 0 ? demo::Inst::idx (keys[strip][0]) : -1;
            if (r.inst.q[q] != want)
                fail (s, "the published target is wrong");
            const bool member = m.groups[gOf].member (q) != nullptr;
            if (member != (r.inst.q[q] >= 0))
                fail (s, "a chip without a target, or a target without a chip");
            if (member && m.groups[gOf].member (q)->qIndex() != q + 1)
                fail (s, "the chip number is not the Q number");
        }
        if (m.groups[gOf].member (slot) != &st.fader)
            fail (s, "the fader of the strip is not its slot");
        if (st.hasPan() && st.pan.qIndex() != 0)
            fail (s, "PAN carries a chip (it is touch only)");
        // the left-out controls: same places as everywhere, not touchable, not among the leaves
        if (s < 7)
        {
            if (st.pan.rect().y != pan0.y || st.pan.rect().h != pan0.h || st.rect().h != m.strips[0].rect().h)
                fail (s, "the strip or its controls moved");
        }
        else
        {
            if (st.fader.rect().h < 380 || st.fader.rect().bottom() < st.rect().bottom() - 10)
                fail (s, "the bus fader does not run the full height");
        }
        ui::Component* leaves[64];
        const int n = st.collectLeaves (leaves, 0, 64);
        for (int i = 0; i < n; ++i)
            if ((leaves[i] == &st.pan && ! st.hasPan()) || (leaves[i] == &st.send1 && ! st.hasSend1()) || (leaves[i] == &st.send2 && ! st.hasSend2()))
                fail (s, "a left-out control is still a leaf");
    }
    check (all, "selecting each of the ten strips makes its level group active, publishes the four levels, moves the routing line, no chip on PAN and the sends, no chip without a target" + why);

    // the bus strips: the fader and meter run the full height of the strip, PAN / B1 / B2 are not present
    r.tapAt (m.strips[7].fader.rect().x + 6, m.strips[7].fader.rect().y + 6);
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    const ui::MixerStrip& bus = m.strips[7];
    check (bus.fader.rect().h >= 380 && bus.fader.rect().bottom() >= bus.rect().bottom() - 10, "BUS 1: the fader and meter run the full height of the strip (longer throw, no empty space at bottom)");
    check (! flat (cv, { m.strips[1].pan.rect().x + 2, m.strips[1].pan.rect().y + 2, m.strips[1].pan.rect().w - 4, m.strips[1].pan.rect().h - 4 }), "the same place on strip A is drawn (the check sees a control)");
    const int writesBefore = r.inst.writes;
    r.tapAt (bus.rect().x + bus.rect().w / 2, pan0.y + pan0.h / 2);
    check (r.inst.writes == writesBefore && m.selectedStrip() == 7 && m.activeGroup() == 2 && r.inst.q[0] == demo::Inst::idx ("bus1.level") && r.inst.q[1] == demo::Inst::idx ("bus2.level") && r.inst.q[2] == -1, "a tap where the left-out PAN would be writes nothing and keeps BUS 1 selected (the group of the buses: two levels)");
    // the strips with all four members are still all four: a strip that was selected before is not left changed
    r.tapAt (m.strips[3].fader.rect().x + 6, m.strips[3].fader.rect().y + 6);
    check (m.activeGroup() == 0 && m.selectedStrip() == 3 && r.inst.q[3] == demo::Inst::idx ("osc3.level") && m.strips[3].hasPan() && m.strips[3].hasSend2(), "going back from a bus to a full strip: its group is active again, PAN and the sends are still on the strip (touch only)");
}

void testMixerDraw()
{
    MixRig r;
    r.inst.haveLive = true;
    r.inst.liveValues.stripPeak[1] = 0.6f;
    r.refresh();
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 100, "the mixer scene fits in the buffer (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "the CPU renderer draws the mixer");
}

// PEEK is local to the Mixer. Compare the complete published XL map with the
// native module's map, including unused slots, then return to the Mixer.
void testMixerPeek()
{
    const bool wide = ui::QGroup::wideDevice();
    ui::QGroup::setWideDevice (true);
    MixRig r;
    r.inst.setTabView (IInstanceView::kTabViewFilter, 3); // VELO on the native Filter tab
    r.inst.set ("filter1.on", 1);
    r.inst.set ("filter2.on", 1);
    r.inst.set ("filter1.style", 8);                     // old preset with a stale style
    r.inst.set ("filter2.style", 8);
    r.inst.set ("chorus.on", 1);                         // BUS 1 never owns MAIN FX slots
    r.inst.set ("chorus.rack", 1);
    r.refresh();

    auto tapCentre = [&r] (ui::Rect target) { r.tapAt (target.x + target.w / 2, target.y + target.h / 2); };
    std::array<int, QRouter::kCount> mixerTargets;
    std::copy_n (r.inst.q, mixerTargets.size(), mixerTargets.begin());
    const int selectorWrites = r.inst.selectorWrites;
    auto exercise = [&] (int target, const int* expected, const char* name)
    {
        const std::string label (name);
        const int writes = r.inst.writes;
        const ui::Rect button = r.ui.screen().routingBar.peekRect (target);
        check (button.w >= 56 && button.h >= 56, label + " has a full touch target");
        tapCentre (button);
        check (r.ui.topPage() == MixerUi::kPagePeek && r.stack.element() == MixerUi::kElementPage,
               label + " opens over the Mixer");
        check (std::equal (expected, expected + QRouter::kCount, r.inst.q)
                 && std::equal (expected, expected + QRouter::kCount, r.ui.qTargets()), label + " publishes all 16 module slots");

        const ui::Rect close = r.ui.peekCloseRect();
        check (close.w >= 56 && close.h >= 56, label + " has a full CLOSE target");
        tapCentre (close);
        check (r.ui.topPage() == 0 && r.stack.top() == MixerUi::kElementPage && r.stack.depth() == 1, label + " closes back to the Mixer");
        check (std::equal (mixerTargets.begin(), mixerTargets.end(), r.inst.q)
                 && std::equal (mixerTargets.begin(), mixerTargets.end(), r.ui.qTargets()), label + " restores every Mixer slot");
        check (r.inst.writes == writes && r.inst.selectorWrites == selectorWrites
                 && r.inst.tabView (IInstanceView::kTabViewFilter) == 3, label + " open/close preserves parameters and the native tab selection");
    };
    for (int filter = 0; filter < 2; ++filter)
    {
        demo::Rig<FilterUi> native;
        native.inst.setTabView (IInstanceView::kTabViewFilter,
                                tabview::encodeFilterSplit (filter, 0, tabview::kFilterExpandedFilter));
        native.inst.set (filter == 0 ? "filter1.on" : "filter2.on", 1);
        native.refresh();
        const char* prefix = filter == 0 ? "filter1.cutoff" : "filter2.cutoff";
        check (std::find (std::begin (native.inst.q), std::end (native.inst.q), demo::Inst::idx (prefix)) != std::end (native.inst.q),
               "native reference maps the requested filter cutoff");
        exercise (filter, native.inst.q, filter == 0 ? "FLT 1" : "FLT 2");
    }
    std::array<int, QRouter::kCount> empty;
    empty.fill (-1);
    exercise (2, empty.data(), "MAIN FX with only a bus effect");
    r.inst.set ("delay.on", 1);
    FxRig nativeFx;
    nativeFx.inst.set ("delay.on", 1);
    nativeFx.refresh();
    check (std::any_of (std::begin (nativeFx.inst.q), std::end (nativeFx.inst.q), [] (int id) { return id >= 0; }),
           "native reference maps a populated main FX rack");
    exercise (2, nativeFx.inst.q, "MAIN FX with a delay");
    // BUS 1 / BUS 2 use the same editor, but their PEEK is isolated to the
    // tapped rack and ADD writes the new slot into that rack.
    r.inst.set ("chorus.on", 1);
    r.inst.set ("chorus.rack", 1);
    FxRig nativeBus;
    nativeBus.inst.set ("chorus.on", 1);
    nativeBus.inst.set ("chorus.rack", 1);
    nativeBus.refresh();
    exercise (3, nativeBus.inst.q, "BUS 1 FX");
    r.inst.set ("chorus.rack", 2);
    nativeBus.inst.set ("chorus.rack", 2);
    nativeBus.refresh();
    exercise (4, nativeBus.inst.q, "BUS 2 FX");
    ui::QGroup::setWideDevice (wide);
}

// Verifies the complete 10-channel Mixer natively on XL: switches, routing selectors,
// pan, sends, faders, values, per-channel peak/hold and PEEK openers.
// Proves real track signal bindings, touch interaction, 16-knob Q-Link allocation,
// preset changes (A -> B -> A), and full return allocation when closing PEEK panels.
void testMixerCompleteNativeXL()
{
    const bool wide = ui::QGroup::wideDevice();
    ui::QGroup::setWideDevice (true);

    MixRig r;
    enableMixerSources (r);
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();

    // 1. All 10 channel switches (power/bypass), routing selectors, pan, sends, faders, value texts, per-channel peak and hold
    for (int s = 0; s < 10; ++s)
    {
        const ui::MixerStrip& st = m.strips[s];
        check (st.hasLevel(), "strip " + std::to_string (s) + " has fader");
        check (st.power.rect().h == 56, "strip " + std::to_string (s) + " has 56px power switch");
        if (s < 7)
        {
            check (st.hasPan() && st.hasSend1() && st.hasSend2(), "source/filter strip " + std::to_string (s) + " has pan, send1, send2");
            check (st.hasDestination(), "source/filter strip " + std::to_string (s) + " has destination selector");
        }
        else if (s == 7 || s == 8)
        {
            check (! st.hasPan() && ! st.hasSend1() && ! st.hasSend2(), "bus strip " + std::to_string (s) + " omits pan and sends");
            check (st.hasDestination(), "bus strip " + std::to_string (s) + " has destination selector");
        }
        else // MASTER
        {
            check (! st.hasPan() && ! st.hasSend1() && ! st.hasSend2(), "master strip omits pan and sends");
            check (! st.hasDestination(), "master strip omits destination selector (bold cut)");
        }
    }

    // 2. Real signal bindings and per-channel peak/hold
    r.inst.haveLive = true;
    const float testPeaks[10] = { 0.45f, 0.72f, 0.61f, 0.55f, 0.38f, 0.81f, 0.69f, 0.50f, 0.40f, 0.88f };
    for (int s = 0; s < 10; ++s)
        r.inst.liveValues.stripPeak[s] = testPeaks[s];
    r.refresh();

    bool allPeaksAndHold = true;
    for (int s = 0; s < 10; ++s)
    {
        const ui::Fader& f = m.strips[s].fader;
        allPeaksAndHold = allPeaksAndHold && (std::fabs (f.state.peak - testPeaks[s]) < 0.001f)
                          && f.state.hasPeakHold && (f.state.peakHold >= testPeaks[s]);
    }
    check (allPeaksAndHold, "all 10 channels show live track signals with active peak/hold");

    // Advance time with decayed input signals -> verify peak hold decay on all 10 channels
    r.inst.haveLive = true;
    for (int s = 0; s < 10; ++s)
        r.inst.liveValues.stripPeak[s] = 0.1f;
    r.refresh(); // advances by 150ms -> triggers fall decay
    bool allDecaying = true;
    for (int s = 0; s < 10; ++s)
    {
        const ui::Fader& f = m.strips[s].fader;
        allDecaying = allDecaying && (f.state.peak == 0.1f)
                      && (f.state.peakHold < testPeaks[s]) && (f.state.peakHold > 0.1f);
    }
    check (allDecaying, "all 10 channels hold peaks with continuous decay over time");

    // 3. Native widgets and one input owner for every available strip binding
    r.ui.setHostBackground (true);
    r.refresh();
    check (r.ui.nativeWidgetsEnabled(), "native widgets enabled by default on MixerUi");
    check (r.ui.isNativeOwned (5, 1), "filter 1 destination is host native owned");
    check (r.ui.isNativeOwned (6, 1), "filter 2 destination is host native owned");
    check (r.ui.isNativeOwned (7, 0) && r.ui.isNativeOwned (7, 1) && r.ui.isNativeOwned (7, 5), "bus 1 bypass, dest, and level are host native owned");
    check (r.ui.isNativeOwned (8, 0) && r.ui.isNativeOwned (8, 1) && r.ui.isNativeOwned (8, 5), "bus 2 bypass, dest, and level are host native owned");
    check (r.ui.isNativeOwned (9, 0), "main fx bypass is host native owned");
    check (r.ui.isNativeOwned (0, 0) && r.ui.isNativeOwned (1, 5) && r.ui.isNativeOwned (9, 5),
           "Sub power, Osc A level and Main level are host-native");

    // The page router must also yield these rectangles; returning false only
    // from MixerUi::touch is too late for the normal host dispatch path.
    const ui::Rect mainBypassRect = m.strips[9].power.rect();
    check (! r.ui.covers (mainBypassRect.x + mainBypassRect.w / 2,
                          mainBypassRect.y + mainBypassRect.h / 2),
           "Mixer Main bypass passes through the page router to the host");
    check (! r.ui.covers (m.strips[1].fader.rect().x + m.strips[1].fader.rect().w / 2,
                       m.strips[1].fader.rect().y + m.strips[1].fader.rect().h / 2),
           "Osc A fader passes through to the host");

    // Under single input ownership:
    // Taps/drags on host native controls yield touch to the host (return false, 0 writes from overlay)
    // while selecting the touched strip
    int writesBefore = r.inst.writes;
    const ui::Rect b1FaderRect = m.strips[7].fader.rect();
    TouchEvent evDown;
    evDown.kind = TouchEvent::Down;
    evDown.x = b1FaderRect.x + b1FaderRect.w / 2;
    evDown.y = b1FaderRect.y + b1FaderRect.h / 2;
    const bool b1Consumed = r.ui.touch (evDown, r.stack, r.inst, r.now);
    check (! b1Consumed && r.inst.writes == writesBefore && m.selectedStrip() == 7, "touch on native Bus 1 fader yields input to host without overlay write");

    const ui::Rect f1DestRect = m.strips[5].destButton.rect();
    evDown.x = f1DestRect.x + f1DestRect.w / 2;
    evDown.y = f1DestRect.y + f1DestRect.h / 2;
    const bool f1Consumed = r.ui.touch (evDown, r.stack, r.inst, r.now);
    check (! f1Consumed && ! r.ui.listOpen() && m.selectedStrip() == 5, "touch on native Filter 1 dest yields input to host without overlay list popup");

    const ui::Rect b2PwrRect = m.strips[8].power.rect();
    evDown.x = b2PwrRect.x + b2PwrRect.w / 2;
    evDown.y = b2PwrRect.y + b2PwrRect.h / 2;
    const bool b2Consumed = r.ui.touch (evDown, r.stack, r.inst, r.now);
    check (! b2Consumed && r.inst.writes == writesBefore && m.selectedStrip() == 8, "touch on native Bus 2 bypass yields input to host without overlay write");

    // The overlay serves controls directly when the native widgets are unavailable.
    r.ui.setNativeWidgetsEnabled (false);
    const ui::Rect osc1FaderRect = m.strips[1].fader.rect();
    writesBefore = r.inst.writes;
    r.drag (osc1FaderRect.x + osc1FaderRect.w / 2, osc1FaderRect.y + osc1FaderRect.h / 2, osc1FaderRect.x + osc1FaderRect.w / 2, osc1FaderRect.y + osc1FaderRect.h / 2 - 30, true);
    check (r.inst.writes > writesBefore, "dragging plugin-owned fader on Osc A writes level from overlay");

    const ui::Rect masterFaderRect = m.strips[9].fader.rect();
    writesBefore = r.inst.writes;
    r.drag (masterFaderRect.x + masterFaderRect.w / 2, masterFaderRect.y + masterFaderRect.h / 2, masterFaderRect.x + masterFaderRect.w / 2, masterFaderRect.y + masterFaderRect.h / 2 - 30, true);
    check (r.inst.writes > writesBefore, "dragging plugin-owned fader on Master writes volume from overlay");

    // Dragging pan and sends on strips 0..6
    for (int s = 0; s < 7; ++s)
    {
        const ui::Rect pRect = m.strips[s].pan.rect();
        writesBefore = r.inst.writes;
        r.drag (pRect.x + pRect.w / 2, pRect.y + 20, pRect.x + pRect.w / 2 + 30, pRect.y + 20, true);
        check (r.inst.writes > writesBefore, "dragging pan on strip " + std::to_string (s) + " writes pan");

        const ui::Rect s1Rect = m.strips[s].send1.rect();
        writesBefore = r.inst.writes;
        r.drag (s1Rect.x + 20, s1Rect.y + 20, s1Rect.x + s1Rect.w - 10, s1Rect.y + 20, true);
        check (r.inst.writes > writesBefore, "dragging send1 on strip " + std::to_string (s) + " writes send1");

        const ui::Rect s2Rect = m.strips[s].send2.rect();
        writesBefore = r.inst.writes;
        r.drag (s2Rect.x + 20, s2Rect.y + 20, s2Rect.x + s2Rect.w - 10, s2Rect.y + 20, true);
        check (r.inst.writes > writesBefore, "dragging send2 on strip " + std::to_string (s) + " writes send2");
    }

    // Destination popup opening on the overlay fallback for strips 0..4
    for (int s = 0; s < 5; ++s)
    {
        r.tap (m.strips[s].destButton);
        check (r.ui.listOpen() && r.stack.top() == MixerUi::kPageList, "tapping destButton on strip " + std::to_string (s) + " opens destination popup");
        const ui::Rect row = r.ui.list().rowRect (0);
        r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
        check (! r.ui.listOpen(), "selecting row closes destination popup on strip " + std::to_string (s));
    }

    // The fallback also handles all faders and destination buttons.
    for (int s = 0; s < 10; ++s)
    {
        const ui::Rect fRect = m.strips[s].fader.rect();
        writesBefore = r.inst.writes;
        r.drag (fRect.x + fRect.w / 2, fRect.y + fRect.h / 2, fRect.x + fRect.w / 2, fRect.y + fRect.h / 2 - 30, true);
        check (r.inst.writes > writesBefore, "fallback mode: dragging fader on strip " + std::to_string (s) + " writes level");
    }
    for (int s = 0; s < 9; ++s)
    {
        r.tap (m.strips[s].destButton);
        check (r.ui.listOpen() && r.stack.top() == MixerUi::kPageList, "fallback mode: tapping destButton on strip " + std::to_string (s) + " opens destination popup");
        const ui::Rect row = r.ui.list().rowRect (0);
        r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
        check (! r.ui.listOpen(), "fallback mode: selecting row closes destination popup on strip " + std::to_string (s));
    }
    for (int s = 0; s < 10; ++s)
    {
        writesBefore = r.inst.writes;
        r.tap (m.strips[s].power);
        check (r.inst.writes > writesBefore, "fallback mode: tapping power on strip " + std::to_string (s) + " toggles switch/bypass");
        r.refresh();
        const ui::MixerStrip& st = m.strips[s];
        check (st.inactive() == (s < 7), "only source/filter OFF dims strip " + std::to_string (s));
        const int offWrites = r.inst.writes;
        const ui::Rect fRect = st.fader.rect();
        r.drag (fRect.x + fRect.w / 2, fRect.y + fRect.h / 2, fRect.x + fRect.w / 2, fRect.y + fRect.h / 2 - 20, true);
        check (s < 7 ? r.inst.writes == offWrites : r.inst.writes > offWrites,
               "fader of strip " + std::to_string (s) + (s < 7 ? " ignores edits while off" : " edits the dry level during rack bypass"));
        r.tap (m.strips[s].power);
        r.refresh();
        check (! st.inactive(), "re-enabled strip " + std::to_string (s) + " restores active state");
    }
    r.ui.setNativeWidgetsEnabled (true);

    // 4. Q-Link allocation on XL (16 knobs) and group switching
    std::array<int, QRouter::kCount> xlExpected;
    xlExpected.fill (-1);
    xlExpected[0] = demo::Inst::idx ("sub.level");
    xlExpected[1] = demo::Inst::idx ("osc1.level");
    xlExpected[2] = demo::Inst::idx ("osc2.level");
    xlExpected[3] = demo::Inst::idx ("osc3.level");
    xlExpected[4] = demo::Inst::idx ("noise.level");
    xlExpected[5] = demo::Inst::idx ("filter1.level");
    xlExpected[6] = demo::Inst::idx ("filter2.level");
    xlExpected[9] = demo::Inst::idx ("master.volume");
    xlExpected[7] = demo::Inst::idx ("bus1.level");
    xlExpected[8] = demo::Inst::idx ("bus2.level");
    check (std::equal (xlExpected.begin(), xlExpected.end(), r.inst.q), "16 XL knobs map all 10 mixer channel levels contiguously");

    // 5. Preset change verification
    // Store snapshot of preset A
    r.inst.set ("osc1.level", 40.0f);
    r.inst.set ("osc1.pan", -25.0f);
    r.inst.set ("filter1.level", -3.0f);
    r.inst.set ("bus1.level", -9.0f);
    r.inst.set ("master.volume", -2.0f);
    r.refresh();
    check (std::string (m.strips[1].fader.state.valueText) == "40.0%"
           && std::string (m.strips[1].pan.state.valueText) == "L 25"
           && std::string (m.strips[5].fader.state.valueText).find ("-3") != std::string::npos
           && std::string (m.strips[7].fader.state.valueText).find ("-9") != std::string::npos
           && std::string (m.strips[9].fader.state.valueText).find ("-2") != std::string::npos,
           "preset A values and formatted readouts match");

    // Preset B
    r.inst.set ("osc1.level", 85.0f);
    r.inst.set ("osc1.pan", 40.0f);
    r.inst.set ("filter1.level", -12.0f);
    r.inst.set ("bus1.level", 0.0f);
    r.inst.set ("master.volume", 0.0f);
    r.refresh();
    check (std::string (m.strips[1].fader.state.valueText) == "85.0%"
           && std::string (m.strips[1].pan.state.valueText) == "R 40"
           && std::string (m.strips[5].fader.state.valueText).find ("-12") != std::string::npos
           && std::string (m.strips[7].fader.state.valueText).find ("0") != std::string::npos
           && std::string (m.strips[9].fader.state.valueText).find ("0") != std::string::npos,
           "preset B change cleanly updates all values and formatted readouts");

    // Return to Preset A
    r.inst.set ("osc1.level", 40.0f);
    r.inst.set ("osc1.pan", -25.0f);
    r.inst.set ("filter1.level", -3.0f);
    r.inst.set ("bus1.level", -9.0f);
    r.inst.set ("master.volume", -2.0f);
    r.refresh();
    check (std::string (m.strips[1].fader.state.valueText) == "40.0%"
           && std::string (m.strips[1].pan.state.valueText) == "L 25",
           "returning to preset A restores exact state");

    // 6. Overlay fallback PEEK openers and return allocation restoration on XL
    r.ui.setHostBackground (false);
    r.refresh();
    static const char* const peekNames[5] = { "FLT 1", "FLT 2", "MAIN FX", "BUS 1 FX", "BUS 2 FX" };
    for (int target = 0; target < 5; ++target)
    {
        const ui::Rect btn = m.routingBar.peekRect (target);
        r.tapAt (btn.x + btn.w / 2, btn.y + btn.h / 2);
        check (r.ui.topPage() == MixerUi::kPagePeek, std::string (peekNames[target]) + " opens PEEK over mixer");

        // Close PEEK
        const ui::Rect close = r.ui.peekCloseRect();
        r.tapAt (close.x + close.w / 2, close.y + close.h / 2);
        check (r.ui.topPage() == 0 && r.stack.top() == MixerUi::kElementPage && r.stack.depth() == 1,
               std::string (peekNames[target]) + " closes back to Mixer");
        check (std::equal (xlExpected.begin(), xlExpected.end(), r.inst.q)
               && std::equal (xlExpected.begin(), xlExpected.end(), r.ui.qTargets()),
               std::string (peekNames[target]) + " close restores complete 16-slot XL Mixer mapping");
    }

    ui::QGroup::setWideDevice (wide);
}

// ---- the FX page -----------------------------------------------------------------------------------------------------------------------------------------------
template <class Rig>
void addEffect (Rig& r, int typeRow)
{
    r.tap (r.ui.screen().chainBar.plusCard);
    r.ui.list().state.current = typeRow;
    r.ui.list().showCurrent();   // the list scrolls: the row is in its window
    const ui::Rect row = r.ui.list().rowRect (typeRow);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
}

template <int Rack>
void testBusAdd()
{
    struct BusEditor : FxUi { BusEditor() : FxUi (Rack) {} };
    demo::Rig<BusEditor> r;
    r.inst.set ("delay.on", 1);
    r.refresh();
    check (r.ui.numCards() == 0, "an empty bus hides the MAIN delay");
    addEffect (r, 4);
    check (r.ui.numCards() == 1 && r.ui.screen().chainBar.cards[0].state.rack == Rack
           && r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeEq
           && r.inst.get ("delay.rack") == 0 && r.inst.get ("delay.on") == 1,
           "ADD places EQ on BUS " + std::to_string (Rack) + " and preserves the MAIN delay");
}

/** Sets the parameter `key` of the effect in pool slot `slot` (its type `type`) to the plain value. */
void setPool (FxRig& r, int slot, int type, const char* key, float plain)
{
    const int idx = demo::Inst::idx (key);
    const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (type);
    for (int p = 0; p < d.count; ++p)
        if (d.source[p] == idx)
            r.inst.setAt (synthyboy::fxInstanceKeys (slot).p[p], synthyboy::fxInstanceNormalise (type, p, plain));
}

void testFxListBackDuringTouch()
{
    FxRig r;
    r.stack.openDetail (MixerUi::kPagePeek);
    r.refresh();
    r.tap (r.ui.screen().chainBar.plusCard);
    const ui::Rect row = r.ui.list().rowRect (0);
    TouchEvent finger;
    finger.kind = TouchEvent::Down;
    finger.x = row.x + row.w / 2;
    finger.y = row.y + row.h / 2;
    r.ui.touch (finger, r.stack, r.inst, r.now);
    const int writes = r.inst.writes;
    r.stack.back();
    r.refresh();
    finger.kind = TouchEvent::Up;
    r.ui.touch (finger, r.stack, r.inst, r.now + 30);
    r.refresh();
    check (r.inst.writes == writes && r.ui.numCards() == 0 && r.stack.top() == MixerUi::kPagePeek,
           "hardware Back during FX list row touch discards its release and preserves the parent");
}

void testFxFallbackChoiceAfterPowerOrRecall()
{
    for (bool recalled : { false, true })
    {
        FxRig r;
        r.refresh();
        addEffect (r, synthyboy::kFxTypeDistortion - 1);
        auto& editor = r.ui.screen().slotEditor;
        r.tap (editor.steppers[0]);
        check (r.ui.listOpen(), "the fallback Distortion choice list is open");
        if (! r.ui.listOpen())
            continue;
        const auto row = r.ui.list().rowRect (4);
        const int before = r.inst.writes;
        if (recalled)
        {
            ++r.inst.generation;
            r.refresh();
            check (! r.ui.listOpen() && r.stack.depth() == 1, "preset recall closes the old FX choice list");
        }
        else
            r.inst.set ("fxi01.on", 0.0f);
        r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
        check (r.inst.writes == before && ! r.ui.listOpen(), "the stale fallback choice cannot write across OFF or preset recall");
    }
}

void testFxChain()
{
    FxRig r;
    r.refresh();
    check (r.ui.numCards() == 0 && ! r.ui.screen().editorShown() && r.ui.screen().galleryShown(), "an empty chain shows the gallery and no editor");
    check (r.ui.screen().chainBar.spare[0].rect().w > 0 && r.ui.screen().chainBar.spare[0].rect().x > r.ui.screen().chainBar.plusCard.rect().x, "after the + ADD card the free places stand in the bar as quiet plus cells");
    r.tap (r.ui.screen().chainBar.plusCard);
    check (r.ui.listOpen() && r.ui.list().state.count == ui::fxl::kNumKinds - 1, "+ ADD opens the list of the effects (the 13 types and the 3 splitters: 16)");
    r.tapAt (20, 130);   // a tap beside the list closes it
    check (! r.ui.listOpen(), "a tap beside the list closes it");
    addEffect (r, 4);   // EQ
    check (r.ui.numCards() == 1 && r.ui.screen().editorShown() && r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeEq, "adding an EQ makes a card and its page");
    addEffect (r, 2);   // delay
    check (r.ui.numCards() == 2 && r.ui.selectedSlot() == 1 && r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeDelay, "the new effect is selected");
    r.tap (r.ui.screen().chainBar.cards[0]);
    check (r.ui.selectedSlot() == 0 && r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeEq, "a tap on a card selects it");
    // the power zone
    const ui::Rect pr = r.ui.screen().chainBar.cards[0].powerRect();
    check (pr.w >= 56 && pr.h >= 44, "the power zone of a card is a touch target (" + std::to_string (pr.w) + " x " + std::to_string (pr.h) + ")");
    const bool before = r.ui.screen().chainBar.cards[0].state.on;
    r.tapAt (pr.x + pr.w / 2, pr.y + pr.h / 2);
    check (r.ui.screen().chainBar.cards[0].state.on != before, "a tap on the power zone switches the effect");
    r.refresh();
    ui::FxSlotEditor& offEd = r.ui.screen().slotEditor;
    const int offWrites = r.inst.writes;
    bool offGroups = true;
    for (int g = 0; g < ui::FxSlotEditor::kMaxRows; ++g)
        offGroups = offGroups && offEd.groups[g].inactive();
    check (! offEd.rack.enabled() && ! offEd.knobs[0].enabled() && ! offEd.display.enabled() && offEd.power.enabled() && ! offEd.power.inactive() && offGroups,
           "an OFF effect keeps POWER usable while disabling its editor");
    if (offEd.numParams() > 0)
    {
        const ui::Rect k0 = offEd.knobs[0].rect();
        r.drag (k0.x + k0.w / 2, k0.y + k0.h / 2, k0.x + k0.w / 2, k0.y + k0.h / 2 + 30, true);
    }
    check (r.inst.writes == offWrites, "an OFF effect ignores knob gestures (zero writes)");
    check (std::all_of (std::begin (r.inst.q), std::end (r.inst.q), [] (int id) { return id == -1; }), "an OFF effect publishes sixteen empty Q-Link targets");
    // A preset/state refresh may alter values without changing rack topology.
    // Keep the user's selected OFF slot, and keep its Q targets empty until
    // POWER is explicitly restored.
    const int offSlot = r.ui.selectedSlot();
    r.inst.set ("eq.mid.gain", 3.0f);
    r.refresh();
    check (r.ui.selectedSlot() == offSlot && ! offEd.power.state.on
           && std::all_of (std::begin (r.inst.q), std::end (r.inst.q), [] (int id) { return id == -1; }),
           "a same-topology state refresh preserves the selected OFF slot and empty Q targets");
    // Re-enable before exercising normal rack navigation below.
    r.tapAt (offEd.power.rect().x + offEd.power.rect().w / 2, offEd.power.rect().y + offEd.power.rect().h / 2);
    check (r.ui.screen().slotEditor.rack.enabled()
           && std::any_of (std::begin (r.inst.q), std::end (r.inst.q), [] (int id) { return id >= 0; }), "turning the effect ON restores its editor and Q-Link targets");
    // MORE is not an entry point into a disabled effect, but LESS must remain
    // an escape if the effect is bypassed while its secondary page is open.
    FxRig moreRig;
    moreRig.refresh();
    addEffect (moreRig, 1); // compressor: guaranteed MORE page
    ui::FxSlotEditor& moreEd = moreRig.ui.screen().slotEditor;
    moreEd.setPage (moreEd.pageIndex() == 0 ? 1 : 0); moreRig.refresh();
    check (moreEd.isMoreView() && moreEd.tabs.state.index == 1, "MORE opens the secondary FX page before bypass");
    moreRig.tapAt (moreEd.power.rect().x + moreEd.power.rect().w / 2, moreEd.power.rect().y + moreEd.power.rect().h / 2);
    moreRig.refresh();
    check (! moreEd.power.state.on && moreEd.tabs.enabled() && moreEd.pageIndex() == 1, "OFF FX keeps LESS enabled as the escape from MORE");
    moreEd.setPage (moreEd.pageIndex() == 0 ? 1 : 0); moreRig.refresh();
    check (! moreEd.isMoreView() && moreEd.tabs.enabled() && moreEd.pageIndex() == 0, "OFF FX disables MORE after returning to the default page");
    // the rack
    r.tap (r.ui.screen().slotEditor.rack);   // the right half: BUS 1
    check (r.ui.screen().chainBar.cards[0].state.rack == 1 || r.ui.screen().chainBar.cards[1].state.rack == 1, "the RACK stepper moves the effect to BUS 1");
    // card layout: every card has room and all are in the bar
    bool cards = true;
    for (int i = 0; i < r.ui.numCards(); ++i)
        cards = cards && r.ui.screen().chainBar.rect().contains (r.ui.screen().chainBar.cards[i].rect()) && r.ui.screen().chainBar.cards[i].rect().h >= 56 && r.ui.screen().chainBar.cards[i].rect().w >= 64;
    check (cards, "the cards lie in the bar and are >= 64 x 56 px");
    // remove
    r.tap (r.ui.screen().slotEditor.remove);
    check (r.ui.numCards() == 1, "REMOVE takes the slot out of the chain");
}

void testFxPresetGenerationRetainsIdentity()
{
    FxRig r;
    r.refresh();
    addEffect (r, synthyboy::kFxTypeDelay - 1);
    addEffect (r, synthyboy::kFxTypeDelay - 1);
    auto& editor = r.ui.screen().slotEditor;
    const synthyboy::FxRackItem first { true, 0 }, second { true, 1 };
    check (r.ui.numCards() == 2 && r.ui.cardItem (0) == first && r.inst.get ("fxi01.type") == synthyboy::kFxTypeDelay
               && r.ui.selectedItem() == second && editor.currentType() == synthyboy::kFxTypeDelay,
           "the second Delay pool slot is the rendered editor");
    editor.setPage (1); r.refresh();
    ++r.inst.generation;
    r.refresh();
    check (r.ui.selectedItem() == second && editor.isMoreView(),
           "recall preserves the same powered pool identity and its open MORE view");

    // Reuse the saved slot for Chorus while a gesture still holds the
    // old rendered card. The release reconciles the live rack before update.
    auto& chain = r.ui.screen().chainBar;
    const auto from = chain.cards[1].rect(), to = chain.cards[0].rect();
    const int x = from.x + 20, y = from.y + from.h / 2;
    const int endX = to.x + 20, endY = to.y + to.h / 2;
    const int before = r.inst.writes;
    r.ui.touch ({ TouchEvent::Down, x, y }, r.stack, r.inst, r.now);
    r.ui.touch ({ TouchEvent::Move, endX, endY }, r.stack, r.inst, r.now + 15);
    r.inst.set ("fxi02.type", static_cast<float> (synthyboy::kFxTypeChorus));
    r.ui.touch ({ TouchEvent::Up, endX, endY }, r.stack, r.inst, r.now + 30);
    check (r.inst.writes == before, "releasing a card after its engine type changes cannot reorder the replacement");
    ++r.inst.generation;
    r.refresh();
    check (r.ui.selectedItem() == first && editor.currentType() == synthyboy::kFxTypeDelay && ! editor.isMoreView(),
           "the preset compares with the last rendered type, selects the first card and closes the old MORE view (slot "
           + std::to_string (r.ui.selectedItem().index) + ", type " + std::to_string (editor.currentType()) + ", page "
           + std::to_string (editor.pageIndex()) + ")");

    // A manual bypass may keep an editor open. It cannot cause recall to
    // preserve a previously bypassed second slot when the new preset powers it.
    r.tap (chain.cards[1]);
    r.tap (editor.power);
    r.refresh();
    check (r.ui.selectedItem() == second && ! editor.power.state.on &&
           std::all_of (std::begin (r.inst.q), std::end (r.inst.q), [] (int id) { return id == -1; }),
           "a manually bypassed card stays selected with empty Q targets");
    r.inst.set ("fxi02.on", 1.0f);
    ++r.inst.generation;
    r.refresh();
    check (r.ui.selectedItem() == first && editor.power.state.on,
           "the new preset does not inherit selection from a previously bypassed editor");
}

void testDistortionMarkerLayers()
{
    FxRig fx;
    MixRig mixer;
    fx.inst.set ("distortion.on", 1.0f);
    mixer.inst.set ("distortion.on", 1.0f);
    mixer.refresh();
    const auto peek = mixer.ui.screen().routingBar.peekRect (2);
    mixer.tapAt (peek.x + peek.w / 2, peek.y + peek.h / 2);
    check (mixer.ui.topPage() == MixerUi::kPagePeek, "the Mixer shows the MAIN FX editor");

    auto scene = std::make_unique<gpu::SceneData>();
    auto verify = [&](auto& rig, const std::string& name)
    {
        const auto draw = [&]
        {
            rig.refresh();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            rig.ui.draw (sb);
        };
        auto plan = [&]
        {
            gpu::CellPlan cells;
            check (gpu::planCells (*scene, cells), name + " fits the helper's cache layers");
            return cells;
        };
        auto keys = [](const gpu::CellPlan& cells)
        {
            std::vector<uint32_t> result;
            for (const auto& cell : cells.cells)
                result.push_back (cell.hash);
            return result;
        };
        draw();
        const auto silent = keys (plan());
        rig.inst.haveLive = true;
        rig.inst.liveValues.stripPeak[9] = 0.8f;
        draw();
        const auto playing = plan();
        int lines = 0, dots = 0;
        for (uint32_t i = 0; i < scene->numOps; ++i)
            if (i < playing.inCell.size() && ! playing.inCell[i])
            {
                lines += scene->ops[i].op == gpu::kOpLine;
                dots += scene->ops[i].op == gpu::kOpDisc;
            }
        check (keys (playing) == silent && lines == 2 && dots == 2,
               name + " keeps every cached cell and draws two live input marks");
        rig.inst.haveLive = false;
        draw();
        check (keys (plan()) == silent, name + " retains the curve when playback stops");
        rig.inst.set ("distortion.drive", 14.0f);
        draw();
        check (keys (plan()) != silent, name + " redraws the transfer curve after a drive edit");
    };
    verify (fx, "standalone FX");
    verify (mixer, "Mixer FX PEEK");
}

void testFxRackSnapshot()
{
    struct CountedInstance : demo::Inst
    {
        float param (int i) const noexcept override
        {
            ++reads;
            return demo::Inst::param (i);
        }
        mutable int reads = 0;
    };
    demo::Rig<FxUi, CountedInstance> r;
    r.ui.setNativeControls (false);
    r.refresh();
    r.inst.reads = 0;
    r.refresh();
    check (r.inst.reads == 3 * synthyboy::kNumFxSlots + 4 * synthyboy::kNumFxInstances,
           "an idle empty FX update only reads rack structure");

    const auto& slot = synthyboy::fxInstanceKeys (0);
    const int type = synthyboy::kFxTypeDelay;
    float defaults[synthyboy::kFxInstanceParams];
    synthyboy::fxInstanceDefaults (type, defaults);
    for (int p = 0; p < synthyboy::kFxInstanceParams; ++p)
        r.inst.setAt (slot.p[p], defaults[p]);
    r.inst.setAt (slot.p[0], defaults[0] > 0.5f ? 0.0f : 1.0f);
    const auto liveSnapshot = [&]
    {
        auto snapshot = synthyboy::ParamValues::defaults();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            snapshot.plain[i] = r.inst.param (static_cast<int> (i));
        return snapshot;
    };
    auto expected = liveSnapshot();
    std::vector<synthyboy::FxWrite> edits;
    synthyboy::fxAddModule (expected.plain, type, 0, edits);
    int writes = r.inst.writes;
    addEffect (r, type - 1);
    check (r.inst.writes - writes == static_cast<int> (edits.size()) && r.inst.param (slot.p[0]) == defaults[0],
           "ADD reads dormant slot values and writes only values that differ from the effect defaults");

    auto& editor = r.ui.screen().slotEditor;
    const int feedback = editor.controlOfKey ("delay.feedback");
    const auto& descriptor = synthyboy::fxTypeDescriptor (type);
    const auto source = std::find (descriptor.source, descriptor.source + descriptor.count, demo::Inst::idx ("delay.feedback"));
    if (feedback < 0 || source == descriptor.source + descriptor.count)
    {
        check (false, "Delay has a mapped feedback control");
        return;
    }
    const int feedbackIndex = slot.p[source - descriptor.source];
    r.inst.setAt (feedbackIndex, 0.75f);
    r.refresh();
    check (std::abs (editor.knobs[feedback].state.value - 0.75f) < 0.001f,
           "host feedback automation reaches the selected editor on the first update");

    r.inst.setAt (slot.p[synthyboy::kFxInstanceParams - 1], 0.8f);
    expected = liveSnapshot();
    edits.clear();
    synthyboy::fxRemoveSlot (expected.plain, 0, edits);
    writes = r.inst.writes;
    r.tap (editor.remove);
    bool cleared = r.inst.param (slot.type) == 0.0f;
    for (int index : slot.p)
        cleared = cleared && r.inst.param (index) == synthyboy::paramTable()[index].def;
    check (cleared && r.inst.writes - writes == static_cast<int> (edits.size()),
           "REMOVE resets the whole slot including a payload change since the last UI update");
}

void testFxGalleryEmptyState()
{
    FxRig r;
    r.refresh();
    check (r.ui.numCards() == 0 && ! r.ui.screen().editorShown() && r.ui.screen().galleryShown(), "empty chain shows the gallery and no editor");
    const ui::FxGallery& g = r.ui.screen().gallery;
    check (g.numTypes() == ui::fxl::kNumKinds - 1, "gallery has all 16 effects of the engine: the 13 types and the 3 splitters (" + std::to_string (g.numTypes()) + ")");
    check (g.rect().w >= 1200 && g.rect().h >= 400, "gallery fills the editor panel area (" + std::to_string (g.rect().w) + " x " + std::to_string (g.rect().h) + ")");

    // verify each card's dimensions and touch targets >= 56 px
    bool allCardsOk = true;
    for (int i = 0; i < g.numTypes(); ++i)
    {
        const ui::Rect c = g.cardRect (i);
        const ui::Rect pic = g.pictureRect (i);
        allCardsOk = allCardsOk && (c.w >= 56 && c.h >= 56 && g.rect().contains (c) && c.contains (pic));
    }
    check (allCardsOk, "every gallery card is >= 56 px with a picture inside and within bounds");

    // scene content in empty state
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 50, "empty FX page scene fits in buffer (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "the CPU renderer draws the empty FX page with gallery");

    // tap on a card in the gallery: card 4 (EQ)
    const ui::Rect c4 = g.cardRect (4);
    r.tapAt (c4.x + c4.w / 2, c4.y + c4.h / 2);
    check (r.ui.numCards() == 1, "tapping gallery card adds effect to chain (1 card now)");
    check (r.ui.selectedSlot() == 0, "the added effect is selected");
    check (r.ui.screen().editorShown() && ! r.ui.screen().galleryShown(), "the editor is shown and the gallery is gone");
    check (r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeEq, "the editor displays the chosen effect (EQ)");

    // removing the effect returns the empty state and the gallery
    r.tap (r.ui.screen().slotEditor.remove);
    check (r.ui.numCards() == 0 && ! r.ui.screen().editorShown() && r.ui.screen().galleryShown(), "removing the only effect returns the gallery");
}

/** The members of a cell of the page: the controls of the cell (both of a pair) and which of them is shown. */
int shownControls (ui::FxSlotEditor& ed, int p)
{
    const ui::Component* members[5] = { &ed.knobs[p], &ed.steppers[p], &ed.lists[p], &ed.toggles[p], &ed.numbers[p] };
    int n = 0;
    for (const ui::Component* c : members)
        n += isShown (ed, c);
    return n;
}

/** The kinds of effect the FX page has: a type of the pool is added by its row of the list, a splitter the same way (the list holds the 16 kinds in order). */
void addKind (FxRig& r, int kind)
{
    addEffect (r, kind - 1);
}

/** Checks every page of the open effect in its present state: one control of the kind its values ask for in every cell the table shows, nothing off the page, touch size, no overlap, one line of chips per row, the
    names and values, the display, the Q groups (a member exactly where a live control is), the closing-up rule (a hidden cell is at the end of its row, the last shown cell of a row is live). */
bool pagesOk (FxRig& r, std::string& why)
{
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    bool ok = true;
    int curPage = 0;
    auto bad = [&] (const std::string& w) { if (ok) why = w + (ed.numPages() > 1 ? (curPage == 1 ? " [MORE view]" : " [default view]") : ""); ok = false; };
    const ui::fxl::Layout& lay = ed.layoutTable();
    for (int pg = 0; pg < ed.numPages(); ++pg)
    {
        curPage = pg;
        ed.setPage (pg);
        r.refresh();
        if (ed.pageIndex() != pg)
            bad ("the view stays where it was put");
        // the default view has the display and the rows the table says; a MORE view of more rows than that gives the display up for four rows
        const bool displayHere = lay.displayH > 0 && (ed.rowsPerPage() <= 2 || ed.numPages() == 1);
        if (ed.hasDisplay() != (displayHere || ed.hasBands()) || (ed.hasDisplay() && ed.displayRect().h != lay.displayH))
            bad ("the display has the height the table says");
        int expectedRows = 0;
        const auto panels = ui::fxl::panelsOf (ed.currentType());
        for (int row = 0; row < lay.numRows; ++row)
            expectedRows += panels.rows[row] == pg;
        if (ed.rowsPerPage() != std::clamp (expectedRows, 1, ui::fxl::kMaxPageRows))
            bad ("rows per page");
        const int first = ed.firstRowOfPage(), rows = ed.rowsOnPage();
        std::vector<const ui::Component*> shownNow;
        std::vector<char> onPage (static_cast<size_t> (ui::FxSlotEditor::kMaxParams) + 1, 0);
        for (int rr = 0; rr < rows; ++rr)
        {
            int lastShown = -1, lastLive = -1, liveCount = 0, rowY = -1;
            for (int c = 0; c < 4; ++c)
            {
                const int p = ed.controlInCell (first + rr, c);
                if (p == ui::FxSlotEditor::kSel)   // the view-local BAND selector: shown, 56 px, last in its row, no Q member
                {
                    onPage[static_cast<size_t> (p)] = 1;
                    const ui::Component* comp = &ed.bandSel;
                    if (! isShown (ed, comp)) bad ("the BAND selector is shown");
                    if (comp->rect().h < 56 || ! comp->sizeOk()) bad ("the BAND selector: >= 56 px and legible");
                    for (const ui::Component* o : shownNow)
                        if (o->rect().overlaps (comp->rect())) bad ("the BAND selector: nothing overlaps");
                    shownNow.push_back (comp);
                    if (c != 3 && ed.controlInCell (first + rr, c + 1) >= 0) bad ("the BAND selector is the last cell of its row");
                    if (ed.groups[first + rr].member (c) != nullptr) bad ("the BAND selector has no Q member");
                    lastShown = lastLive = c;
                    continue;
                }
                if (p < 0)
                {
                    if (lastShown >= 0 && c < 4 && ed.controlInCell (first + rr, std::min (3, c + 1)) >= 0)
                        bad ("a hole in a row");
                    continue;
                }
                lastShown = c;
                onPage[static_cast<size_t> (p)] = 1;
                const synthyboy::ParamInfo* pi = ed.infoOf (p);
                if (pi == nullptr) { bad ("a control without a parameter"); continue; }
                if (shownControls (ed, p) != 1) bad (std::string (pi->key) + ": exactly one control of the cell is shown");
                const ui::FxSlotEditor::Kind k = ed.kindOf (p);
                const bool choice = pi->numChoices > 0;
                if (choice == (k == ui::FxSlotEditor::Kind::Knob || k == ui::FxSlotEditor::Kind::Number)) bad (std::string (pi->key) + ": a choice is never a knob, a continuous value always");
                if ((k == ui::FxSlotEditor::Kind::Number) != (! choice && pi->interval == 1.0f)) bad (std::string (pi->key) + ": a whole number is a number field");
                if (choice && pi->numChoices > 8 && k != ui::FxSlotEditor::Kind::Stepper) bad (std::string (pi->key) + ": more than 8 values is a stepper");
                if (choice && pi->numChoices <= 8 && k == ui::FxSlotEditor::Kind::List && ! ed.listFits (p, ed.lists[p].rect().w, ed.lists[p].rect().h)) bad (std::string (pi->key) + ": a list fits its cell");
                if (choice && pi->numChoices <= 8 && k == ui::FxSlotEditor::Kind::Stepper && ed.baseKind (p) == ui::FxSlotEditor::Kind::List && ed.listFits (p, ed.steppers[p].rect().w, ed.steppers[p].rect().h)) bad (std::string (pi->key) + ": a list that fits is not a stepper");
                const ui::Component* comp = ed.memberOf (p);
                if (comp->rect().h < 56 || ! comp->sizeOk()) bad (std::string (pi->key) + ": >= 56 px and legible");
                for (const ui::Component* o : shownNow)
                    if (o->rect().overlaps (comp->rect())) bad (std::string (pi->key) + ": nothing overlaps");
                shownNow.push_back (comp);
                if (! ed.rect().contains (comp->rect())) bad (std::string (pi->key) + ": inside the page");
                rowY = rowY < 0 ? comp->rect().y : rowY;
                if (comp->rect().y != rowY) bad (std::string (pi->key) + ": the chips of a row stand in one line");
                if (comp->rect().h > ed.knobArea().h / ed.rowsPerPage()) bad (std::string (pi->key) + ": the row has the standard height");
                switch (k)
                {
                    case ui::FxSlotEditor::Kind::Knob: if (ed.knobs[p].state.label[0] == '\0' || ed.knobs[p].state.valueText[0] == '\0') bad (std::string (pi->key) + ": a name and a value"); break;
                    case ui::FxSlotEditor::Kind::Stepper: if (ed.steppers[p].state.caption[0] == '\0' || ed.steppers[p].state.text[0] == '\0') bad (std::string (pi->key) + ": a name and a value"); break;
                    case ui::FxSlotEditor::Kind::List: if (ed.lists[p].state.title[0] == '\0' || ed.lists[p].state.count != pi->numChoices) bad (std::string (pi->key) + ": a name and all the values"); break;
                    case ui::FxSlotEditor::Kind::Toggle: if (ed.toggles[p].state.label[0] == '\0') bad (std::string (pi->key) + ": a name"); break;
                    case ui::FxSlotEditor::Kind::Number: if (ed.numbers[p].state.label[0] == '\0' || ed.numbers[p].state.max <= ed.numbers[p].state.min) bad (std::string (pi->key) + ": a name and a range"); break;
                }
                if (ed.isLive (p)) { lastLive = c; ++liveCount; }
                else if (ed.memberControl (first + rr, c) >= 0) bad ("a dim control is a Q member");
                if (ed.isLive (p) != ed.memberOf (p)->enabled()) bad (std::string (pi->key) + ": dim exactly where it is not live");
                // the Q group of the row has a member exactly here
                if (ed.isLive (p) != (ed.groups[first + rr].member (c) == ed.memberOf (p))) bad (std::string (pi->key) + ": a Q chip exactly on a live control");
            }
            if (lastShown != lastLive) bad ("the last cell of a row that is shown is live (what does nothing at the end of a row is hidden)");
            if (liveCount == 0) bad ("a row with nothing live is not on the page");
            if (ed.groups[first + rr].numMembers() != liveCount) bad ("the number of the Q members of a row");
        }
        // what is not on the page is not shown
        for (int p = 0; p < ed.numParams(); ++p)
            if (! onPage[static_cast<size_t> (p)] && shownControls (ed, p) != 0)
                bad (std::string ("off the page: ") + (ed.infoOf (p) ? ed.infoOf (p)->key : "?"));
    }
    return ok;
}

/** Exercise every combination of the engine and view-local choices named in the layout conditions. */
template <class Fn>
void forEveryState (FxRig& r, int kind, Fn&& fn)
{
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    std::vector<const char*> keys;
    for (int p = 0; p < ed.numParams(); ++p)
    {
        const auto* alt = ed.altOf (p);
        if (alt == nullptr)
            continue;
        for (const char* key : { alt->live.key, alt->live.key2 })
            if (key != nullptr && std::none_of (keys.begin(), keys.end(), [key] (const char* other) { return std::strcmp (key, other) == 0; }))
                keys.push_back (key);
    }

    struct Condition
    {
        const char* key;
        int control;
        int paramIndex;
        int choiceCount;
    };
    std::vector<Condition> conditions;
    const synthyboy::FxRackItem item = r.ui.cardItem (r.ui.selectedSlot());
    for (const char* key : keys)
    {
        if (std::strcmp (key, ui::fxl::kViewBand) == 0)
        {
            conditions.push_back ({ key, -1, -1, 3 });
            continue;
        }
        const int control = ed.controlOfKey (key);
        const auto* info = ed.infoOf (control);
        const int paramIndex = r.ui.paramIndexOf (item, control);
        if (info == nullptr || info->numChoices == 0 || paramIndex < 0 || static_cast<size_t> (paramIndex) >= synthyboy::paramCount())
        {
            check (false, std::string ("FX layout condition has no bound choice parameter: ") + key);
            return;
        }
        conditions.push_back ({ key, control, paramIndex, info->numChoices });
    }
    std::vector<int> at (conditions.size(), 0);
    for (;;)
    {
        std::string state;
        for (size_t i = 0; i < conditions.size(); ++i)
        {
            const auto& condition = conditions[i];
            state += " " + std::string (condition.key) + "=" + std::to_string (at[i]);
            if (condition.paramIndex < 0)
                ed.setBand (at[i]);
            else
            {
                const float plain = static_cast<float> (at[i]);
                r.inst.setAt (condition.paramIndex, item.pool ? synthyboy::fxInstanceNormalise (kind, condition.control, plain) : plain);
            }
        }
        r.refresh (2);
        fn (state);
        size_t i = 0;
        while (i < at.size() && ++at[i] >= conditions[i].choiceCount)
            at[i++] = 0;
        if (i == at.size())
            break;
    }
}

void testEveryEffectType()
{
    for (int kind = 1; kind < ui::fxl::kNumKinds; ++kind)
    {
        FxRig r;
        r.refresh();
        addKind (r, kind);
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        const ui::fxl::Layout& lay = ui::fxl::layoutOf (kind);
        const std::string name = lay.name;
        check (ed.currentType() == kind && r.ui.numCards() == 1, name + ": adding it makes its card and its page (the kind " + std::to_string (kind) + ")");

        // the table: every parameter of the type's own list has its control (the number of the control is the number of the parameter), once; the splitters and the extras of the reverb come after
        bool table = true;
        std::string why;
        if (kind < synthyboy::kNumFxInstanceTypes)
        {
            const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (kind);
            for (int p = 0; p < d.count; ++p)
                if (ed.infoOf (p) == nullptr || ed.infoOf (p) != &synthyboy::paramTable()[d.source[p]])
                {
                    table = false;
                    why = std::string (" (parameter ") + std::to_string (p) + " of the type has no control)";
                }
        }
        int cells = 0;
        for (int rr = 0; rr < lay.numRows; ++rr)
            for (int c = 0; c < 4; ++c)
                for (const ui::fxl::Alt& a : lay.row[rr].cell[c].alt)
                    cells += a.key != nullptr && ! ui::fxl::isView (a.key);   // the BAND selector of the compressor is a cell, no engine parameter
        int controls = 0;
        for (int p = 0; p < ui::FxSlotEditor::kMaxParams; ++p)
            controls += ed.infoOf (p) != nullptr;
        if (controls != cells)
        {
            table = false;
            why += " (a key of the table is not an engine parameter: " + std::to_string (controls) + " of " + std::to_string (cells) + ")";
        }
        check (table, name + ": the table has a control for every parameter of the effect, each once" + why);
        check (lay.numRows <= ui::fxl::kMaxRows && lay.rowsPerPage >= 1 && lay.rowsPerPage <= ui::fxl::kMaxPageRows, name + ": the table fits the page");

        // every state the conditions of the table can make: the pages, and no page with fewer than three live members unless the effect has fewer than three in that state
        std::string defWhy;
        r.refresh (2);
        const bool defaultOk = pagesOk (r, defWhy);
        check (defaultOk, name + ": every page of the default state (" + std::to_string (ed.numPages()) + " pages, " + std::to_string (ed.numRows()) + " rows): one control of its kind per cell, sizes, one line of chips, Q members only on live controls" + (defaultOk ? "" : " - " + defWhy));
        int states = 0;
        bool members = true, everyOk = true;
        std::string memberWhy, everyWhy;
        forEveryState (r, kind, [&] (const std::string& st)
        {
            ++states;
            int total = 0;
            for (int rr = 0; rr < ed.numRows(); ++rr)
                for (int c = 0; c < 4; ++c)
                    total += ed.memberControl (rr, c) >= 0;
            {   // the default view (the one a hand meets first) holds at least three live controls; MORE holds what is left of them
                int onPage = 0;
                for (int rr = 0; rr < ed.numDefaultRows(); ++rr)
                    for (int c = 0; c < 4; ++c)
                        onPage += ed.memberControl (rr, c) >= 0;
                if (onPage < 3 && total >= 3)
                {
                    members = false;
                    memberWhy = " (the default view has " + std::to_string (onPage) + " in the state" + st + ")";
                }
                if (onPage > 16)
                {
                    members = false;
                    memberWhy = " (the default view has " + std::to_string (onPage) + " cells in the state" + st + ")";
                }
            }
            std::string w;
            if (! pagesOk (r, w))
            {
                everyOk = false;
                everyWhy = " (" + w + " in the state" + st + ")";
            }
        });
        if (kind == synthyboy::kFxTypeCompressor)
            check (states == 3 * 4 * 3, "Compressor walks every BAND, BANDS and mode combination");
        if (kind == synthyboy::kFxTypeDelay)
            check (states == 4 * 5, "Delay walks every sync and style combination");
        check (members, name + ": in none of the " + std::to_string (states) + " states of its conditions does the default view have fewer than3 or more than16 controls" + memberWhy);
        check (everyOk, name + ": every page of every state is whole (the closing-up rule, one line of chips, sizes)" + everyWhy);

        // no text of a value is a bare number where the parameter has a unit
        bool units = true;
        for (int p = 0; p < ed.numParams(); ++p)
        {
            const synthyboy::ParamInfo* pi = ed.infoOf (p);
            if (pi == nullptr || pi->numChoices > 0 || pi->unit == synthyboy::Unit::none || ed.kindOf (p) != ui::FxSlotEditor::Kind::Knob)
                continue;
            const std::string v = ed.knobs[p].state.valueText;
            const bool ok = v.find_first_not_of ("0123456789.+-") != std::string::npos;
            if (! ok)
                std::printf ("  no unit: %s = '%s'\n", pi->key, v.c_str());
            units = units && ok;
        }
        check (units, name + ": every continuous value that has a unit is written with it");
        ed.setPage (0);
        check (ed.hasDisplay() == (lay.displayH > 0), name + ": a display exactly where the table says");
    }
}

void testFxEditing()
{
    // Test 1: Distortion (as in prompt: Distortion TYPE "Soft clip", FILTER ORDER "None")
    {
        FxRig r;
        r.refresh();
        addEffect (r, 3);   // Distortion: row 3 in type list
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        check (ed.currentType() == synthyboy::kFxTypeDistortion, "added Distortion effect");
        check (ed.isChoice (0) && ed.isChoice (3), "Distortion TYPE (control 0) and FILTER ORDER (control 3) are enumerated");
        check (std::string (ed.steppers[0].state.caption) == "TYPE" && std::string (ed.steppers[0].state.text) == "Soft clip",
               "Distortion TYPE is a stepper with caption 'TYPE' and value 'Soft clip'");
        check (std::string (ed.steppers[3].state.caption) == "FILTER ORDER" && std::string (ed.steppers[3].state.text) == "None",
               "Distortion FILTER ORDER is a stepper with caption 'FILTER ORDER' and value 'None'");
        check (isShown (ed, &ed.steppers[0]) && ! isShown (ed, &ed.knobs[0]),
               "Distortion TYPE stepper is shown, knob is hidden");
        check (isShown (ed, &ed.lists[3]) && ! isShown (ed, &ed.knobs[3]) && ! isShown (ed, &ed.steppers[3]) && ed.kindOf (3) == ui::FxSlotEditor::Kind::List,
               "Distortion FILTER ORDER (3 values) is a LED list, not a knob and not a stepper");
        check (std::string (ed.lists[3].state.title) == "FILTER ORDER" && ed.lists[3].state.count == 3 && ed.lists[3].state.index == 0 && std::string (ed.lists[3].state.names[2]) == "Post" && ed.lists[3].rect().h >= 56 * ed.lists[3].rows(),
               "the list has its title, all three values and the lit first one");
        {
            const ui::Rect row2 = ed.lists[3].rowRect (2);
            r.tapAt (row2.x + row2.w / 2, row2.y + row2.h / 2);
            r.refresh();
            const int orderIdx = synthyboy::fxInstanceKeys (r.ui.cardItem (0).index).p[3];
            check (r.inst.param (orderIdx) == synthyboy::fxInstanceNormalise (synthyboy::kFxTypeDistortion, 3, 2.0f) && ed.lists[3].state.index == 2, "a tap on 'Post' writes the parameter and lights its dot");
            check (ed.activeRow() == 1 && ed.groups[1].member (0) == &ed.lists[3] && std::string (ed.rowTitle (1)) == "FILTER", "the list is the first member of the FILTER block (the second row) and the tap made that group active");
            ed.setActiveRow (0);
            r.refresh();
        }

        // Q-Link turning steps through choices
        const int slot = r.ui.cardItem (0).index;
        const int typeParamIdx = synthyboy::fxInstanceKeys (slot).p[0];
        r.ui.turnQ (0, 1.0, r.stack, r.inst); // turn Q1 to step TYPE
        r.refresh();
        check (std::string (ed.steppers[0].state.text) == "Hard clip",
               "turning Q-Link 1 steps Distortion TYPE to 'Hard clip'");
        check (r.inst.param (typeParamIdx) == synthyboy::fxInstanceNormalise (synthyboy::kFxTypeDistortion, 0, 1.0f),
               "engine parameter reflects stepped TYPE choice");

        // Tapping stepper middle opens ListPopup with choices
        const ui::Rect st0 = ed.steppers[0].rect();
        r.tapAt (st0.x + st0.w / 2, st0.y + st0.h / 2);
        check (r.ui.listOpen() && r.stack.top() == FxUi::kPageList, "tapping stepper middle opens list popup of choices");
        check (r.ui.list().state.count == 14, "Distortion TYPE popup lists all 14 choices");
        // Select choice 4 ("Bit crush")
        const ui::Rect row4 = r.ui.list().rowRect (4);
        r.tapAt (row4.x + row4.w / 2, row4.y + row4.h / 2);
        r.refresh();
        check (! r.ui.listOpen() && r.stack.depth() == 1, "selecting choice closes list popup");
        check (std::string (ed.steppers[0].state.text) == "Bit crush", "stepper reflects selected choice 'Bit crush'");
        check (r.inst.param (typeParamIdx) == synthyboy::fxInstanceNormalise (synthyboy::kFxTypeDistortion, 0, 4.0f),
               "engine parameter reflects Bit crush");
    }

    // Test 2: EQ editing (continuous and choice parameters, display, etc.)
    {
        FxRig r;
        r.refresh();
        addEffect (r, 4);   // EQ: row 4 in type list
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        check (ed.isChoice (0) && ! ed.isChoice (1) && ed.kindOf (0) == ui::FxSlotEditor::Kind::List && ed.lists[0].state.count == 2, "EQ LOW MODE is a LED list of its two values, LOW CUTOFF is a continuous knob");
        check (std::string (ed.steppers[0].state.caption) == "LOW MODE" && std::string (ed.knobs[1].state.label) == "LOW CUTOFF",
               "the controls have the descriptor's names without the name of the effect (" + std::string (ed.knobs[1].state.label) + ")");
        check (std::string (ed.steppers[0].state.text) == "Shelf" && std::string (ed.knobs[1].state.valueText).find ("Hz") != std::string::npos,
               "a choice is written by name and a frequency with Hz (" + std::string (ed.knobs[1].state.valueText) + ")");

        // the EQ display reads the bands: a boost at 1 kHz is a bump
        check (ed.display.kind == ui::FxDisplay::Kind::Eq && ed.display.rect().w > 1000, "the EQ has its display over the whole width");
        const synthyboy::FxRackItem item = r.ui.cardItem (0);
        const synthyboy::FxTypeDescriptor& eqd = synthyboy::fxTypeDescriptor (synthyboy::kFxTypeEq);
        check (std::string (synthyboy::paramTable()[eqd.source[6]].key) == "eq.mid.gain", "parameter 6 of the EQ is eq.mid.gain");
        const int slot = item.index;
        const int gainIdx = synthyboy::fxInstanceKeys (slot).p[6];
        r.inst.setAt (gainIdx, synthyboy::fxInstanceNormalise (synthyboy::kFxTypeEq, 6, 12.0f));
        r.refresh();
        check (std::fabs (ui::FxDisplay::bandDb (1, ed.display.eq.mid, 1000.0f) - 12.0f) < 0.5f, "the display response at the centre of a +12 dB peak is +12 dB");
        check (std::fabs (ui::FxDisplay::bandDb (1, ed.display.eq.mid, 30.0f)) < 0.5f && std::fabs (ui::FxDisplay::bandDb (1, ed.display.eq.mid, 15000.0f)) < 0.5f, "far from it the band is flat");
        check (ui::FxDisplay::bandDb (0, ed.display.eq.low, 20.0f) < 0.1f, "no boost: a flat response");

        // the knob writes the parameter (the pool stores the normalised value)
        const ui::Rect k = ed.knobs[6].rect();
        r.drag (k.x + k.w / 2, k.y + k.h / 2, k.x + k.w / 2, k.y + k.h / 2 + 30, true);
        check (r.inst.param (gainIdx) < synthyboy::fxInstanceNormalise (synthyboy::kFxTypeEq, 6, 12.0f), "dragging the knob down lowers the gain");

        // the Q group is the row of the knob that was touched
        check (ed.activeRow() == 0 && r.inst.q[3] == synthyboy::fxInstanceKeys (slot).p[6] && r.inst.q[0] == synthyboy::fxInstanceKeys (slot).p[1], "the Q group is the row of the knob (LOW and MID cutoff and gain: params 1, 2, 5, 6)");
    }
}

/** The text of a value (ui_bind.h formatValue): a rate under 1 Hz keeps its decimals (the Chorus and the Reverb chorus rates read "0.40 Hz" and "0.25 Hz", not "0 Hz"), a value that rounds to zero has no sign, a pan reads like the mixer's. */
void testValueText()
{
    auto text = [] (const char* key, float v) { char b[32]; uibind::formatValue (uibind::info (uibind::indexOfKey (key)), v, b, sizeof b); return std::string (b); };
    check (text ("chorus.rate", 0.4f) == "0.40 Hz" && text ("reverb.chorus_rate", 0.25f) == "0.25 Hz", "a rate under 1 Hz keeps two decimals (" + text ("chorus.rate", 0.4f) + ", " + text ("reverb.chorus_rate", 0.25f) + ")");
    check (text ("chorus.rate", 3.2f) == "3.20 Hz", "two decimals under 10 Hz");
    check (text ("chorus.rate", 33.5f) == "33.5 Hz", "one decimal under 100 Hz (" + text ("chorus.rate", 33.5f) + ")");
    check (text ("chorus.rate", 420.0f) == "420 Hz", "none from 100 Hz (" + text ("chorus.rate", 420.0f) + ")");
    check (text ("master.volume", -0.02f) == "+0.0 dB" && text ("master.volume", -6.0f) == "-6.0 dB", "a level that rounds to zero is +0.0 dB, never -0.0 (" + text ("master.volume", -0.02f) + ")");
    check (text ("utility.pan", 0.0f) == "C" && text ("utility.pan", -50.0f) == "L 50" && text ("utility.pan", 50.0f) == "R 50", "a pan reads C / L 50 / R 50 like the mixer (" + text ("utility.pan", 0.0f) + ")");
}

/** ONE rule for what does nothing in the state of the effect (ui/fx_layout.h): a pair of controls that exclude each other (RATE / DIVISION) is ONE cell that shows the live one; a control that is not live is hidden, never
    dim (the live ones of its row close up from the left, a row with nothing live is gone and the rows below move up). */
void testClosingUpRule()
{
    // the state of the effect is written through the editor, as a finger does it (the effect lives in the pool)
    auto choose = [] (FxRig& r, const char* key, int option)
    {
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        const int p = ed.controlOfKey (key);
        if (p < 0)
            return false;
        if (ed.kindOf (p) == ui::FxSlotEditor::Kind::List)
        {
            if (ed.rowOfControl (p) >= 0)
                ed.setActiveRow (ed.rowOfControl (p));   // the page of the control
            r.refresh();
            const ui::Rect row = ed.lists[p].rowRect (option);
            r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
        }
        else
        {
            const synthyboy::FxRackItem item = r.ui.cardItem (r.ui.selectedSlot());
            r.inst.setAt (r.ui.paramIndexOf (item, p), synthyboy::fxInstanceNormalise (r.ui.screen().slotEditor.currentType(), p, static_cast<float> (option)));
        }
        r.refresh (2);
        return true;
    };
    auto shown = [] (ui::FxSlotEditor& ed, const char* key) { const int p = ed.controlOfKey (key); return p >= 0 && ed.rowOfControl (p) >= 0 && ed.isLive (p) ? 1 : (p >= 0 && ed.rowOfControl (p) >= 0 ? 2 : 0); };   // 1 live, 2 dim, 0 not on the page
    {   // RATE / DIVISION: one cell
        FxRig r;
        r.refresh();
        addEffect (r, 0);   // Chorus
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        const int rate = ed.controlOfKey ("chorus.rate"), div = ed.controlOfKey ("chorus.division");
        check (shown (ed, "chorus.rate") == 1 && shown (ed, "chorus.division") == 0 && ed.memberOf (rate)->rect() == ed.memberOf (div)->rect(), "Chorus in Free (Hz) sync: the cell shows the RATE, the DIVISION is not on the page (the same cell)");
        check (choose (r, "chorus.sync", 1), "the SYNC list of the Chorus is there");
        check (shown (ed, "chorus.rate") == 0 && shown (ed, "chorus.division") == 1 && ed.groups[0].member (0) == ed.memberOf (div), "Chorus following the tempo: the cell shows the DIVISION, which is the first member of its Q group");
        choose (r, "chorus.sync", 0);
        check (shown (ed, "chorus.rate") == 1 && ed.groups[0].member (0) == ed.memberOf (rate), "and back to the RATE");
    }
    {   // a whole row closes up: the filter of the Distortion
        FxRig r;
        r.refresh();
        addEffect (r, 3);   // Distortion
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        check (shown (ed, "distortion.filter_cutoff") == 0 && shown (ed, "distortion.filter_blend") == 0 && shown (ed, "distortion.filter_keytrack") == 0 && shown (ed, "distortion.filter_order") == 1 && shown (ed, "distortion.drive") == 1,
              "Distortion without a filter (order None): the filter values are not on the page (the end of their row is hidden), the order and the drive are live");
        check (ed.numRows() == 2, "the FILTER SHAPE row (MORE) is gone: " + std::to_string (ed.numRows()) + " rows (the row of the order has only the order)");
        check (ed.groups[1].numMembers() == 1, "its Q group has the one member");
        check (choose (r, "distortion.filter_order", 1), "the filter order of the Distortion is a list");
        check (shown (ed, "distortion.filter_cutoff") == 1 && shown (ed, "distortion.filter_keytrack") == 1 && ed.numRows() == 3 && ed.numDefaultRows() == 3 && ed.groups[1].numMembers() == 3 && ed.groups[2].numMembers() == 2, "Distortion with a filter before it: the cutoff and the resonance join the default view, the blend and the key track are the MORE row (3 rows)");
    }
    {   // the second middle band of the EQ, the end of its row
        FxRig r;
        r.refresh();
        addEffect (r, 4);   // EQ
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        check (shown (ed, "eq.mid2.mode") == 1 && shown (ed, "eq.mid2.cutoff") == 0 && shown (ed, "eq.mid2.gain") == 0 && shown (ed, "eq.mid2.resonance") == 0, "EQ with the second middle band Off: only its MODE is on the page");
        check (choose (r, "eq.mid2.mode", 1), "the MID 2 mode is a list");
        check (shown (ed, "eq.mid2.cutoff") == 1 && shown (ed, "eq.mid2.resonance") == 1, "EQ with the second middle band on: its values are live");
    }
    {   // hidden, not dim: a control that is not live is not on either view even with a live one behind it in its row
        FxRig r;
        r.refresh();
        addEffect (r, 1);   // Compressor
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        check (shown (ed, "compressor.limit_latency_comp") == 0 && shown (ed, "compressor.crossover_high") == 1, "Compressor in Compress mode: the limiter latency is not on the page (hidden, not dim), the crossovers are on the MORE view");
        const int lat = ed.controlOfKey ("compressor.limit_latency_comp");
        check (! ed.isLive (lat) && ed.groups[ed.rowOfControl (ed.controlOfKey ("compressor.mid.gain"))].numMembers() == 3, "the hidden control has no Q chip: the row of the mid gain has three members");
        const synthyboy::FxRackItem item = r.ui.cardItem (0);
        check (choose (r, "compressor.limit", 1), "the MODE list");
        check (shown (ed, "compressor.limit_latency_comp") == 1 && ed.groups[ed.rowOfControl (lat)].numMembers() == 4, "in Limit mode the latency is live, the fourth member of its row");
        // the band mode: Low band has no high band and no upper crossover; the other rows move up
        const int rowsMulti = ed.numRows();
        check (choose (r, "compressor.bands", 1), "the BANDS list");
        check (shown (ed, "compressor.high.lower_threshold") == 0 && shown (ed, "compressor.crossover_high") == 0 && shown (ed, "compressor.crossover_low") == 1 && shown (ed, "compressor.mid.gain") == 1 && ed.numRows() == rowsMulti - 1,
              "Compressor in the Low band mode: the high band row is gone (" + std::to_string (rowsMulti) + " -> " + std::to_string (ed.numRows()) + " rows), the upper crossover is hidden, the lower one is live");
        check (choose (r, "compressor.limit", 0), "back to the Compress mode");
        check (choose (r, "compressor.bands", 3), "single band");
        check (shown (ed, "compressor.low.upper_ratio") == 0 && shown (ed, "compressor.high.upper_ratio") == 0 && shown (ed, "compressor.mid.upper_ratio") == 1 && shown (ed, "compressor.low.gain") == 0 && shown (ed, "compressor.crossover_low") == 0,
              "Single band: only the middle band is live; the gains of the other bands and the crossovers are hidden");
        (void) item;
    }
    {   // Utility: the mono bass frequency is the end of its row
        FxRig r;
        r.refresh();
        addEffect (r, 10);   // Utility
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        check (shown (ed, "utility.mono_bass") == 1 && shown (ed, "utility.mono_freq") == 0, "Utility with the mono bass off: its frequency is not on the page");
        check (choose (r, "utility.mono_bass", 1), "the mono bass");
        check (shown (ed, "utility.mono_freq") == 1, "with the mono bass on the frequency is live");
    }
    {   // the FX filter: the comb offset and the formant values share a row, by the model
        FxRig r;
        r.refresh();
        addEffect (r, 5);   // FX filter
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        const int rows = ed.numRows();
        check (shown (ed, "fxfilter.comb_offset") == 0 && shown (ed, "fxfilter.formant.x") == 0 && shown (ed, "fxfilter.formant.transpose") == 0, "FX filter in a model without values of its own: the comb / formant values are not on the page (" + std::to_string (rows) + " rows)");
        check (choose (r, "fxfilter.model", 6), "the model (a stepper: more than 8 values)");
        check (shown (ed, "fxfilter.comb_offset") == 1 && shown (ed, "fxfilter.formant.x") == 0 && ed.numRows() == rows, "Comb: the COMB OFFSET is in the cell the formant X would have (the same rows)");
        check (choose (r, "fxfilter.model", 5), "Formant");
        check (shown (ed, "fxfilter.formant.x") == 1 && shown (ed, "fxfilter.formant.spread") == 1 && shown (ed, "fxfilter.formant.transpose") == 1 && shown (ed, "fxfilter.comb_offset") == 0, "Formant: the five formant values are live, the comb offset is not on the page");
    }
    {   // the reverb: the room and the feedback exist in the algorithms that have them
        FxRig r;
        r.refresh();
        addEffect (r, 8);   // Reverb
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        r.refresh();
        check (shown (ed, "reverb.room") == 0 && shown (ed, "reverb.feedback") == 0 && shown (ed, "reverb.type") == 1, "Reverb in the first algorithm: no room and no feedback on the page");
        check (choose (r, "reverb.type", 4), "the algorithm");
        check (shown (ed, "reverb.room") == 1 && shown (ed, "reverb.feedback") == 1, "Nitrous: room and feedback are live");
        check (choose (r, "reverb.type", 5), "Basin");
        check (shown (ed, "reverb.room") == 0 && shown (ed, "reverb.feedback") == 1, "Basin has the feedback, no room");
    }
}

/** The splitters are modules of the fixed chain with no type of the pool: the page of each has its controls, a display of the bands and the Q groups; they are added from the list and the gallery, switched off with their power. */
void testSplitterPages()
{
    const struct { int kind; const char* name; int controls; } kSplit[3] = { { ui::fxl::kKindSplitterLh, "Splitter L/H", 4 }, { ui::fxl::kKindSplitterLmh, "Splitter L/M/H", 6 }, { ui::fxl::kKindSplitterMs, "Splitter M/S", 3 } };
    for (const auto& sp : kSplit)
    {
        FxRig r;
        r.refresh();
        addKind (r, sp.kind);
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        const synthyboy::FxRackItem item = r.ui.cardItem (0);
        const std::string n = sp.name;
        check (r.ui.numCards() == 1 && ! item.pool && item.index == 13 + (sp.kind - ui::fxl::kKindSplitterLh) && ed.currentType() == sp.kind, n + ": adding it switches the module of the fixed chain on and selects it");
        check (r.ui.screen().chainBar.cards[0].state.type == sp.kind && std::string (r.ui.screen().chainBar.cards[0].state.name) == n, n + ": its card has its own picture and its name");
        check (ed.numParams() == sp.controls && ed.numRows() >= 1 && ed.hasBands() && ed.display.kind == ui::FxDisplay::Kind::None && ed.bands.numBands() == ui::fxl::bandsOfSplitter (sp.kind), n + ": the page has " + std::to_string (sp.controls) + " controls and the lanes of the racks of its bands");
        // every parameter of the splitter is a control that writes it, and is on a Q group
        std::string key = n == "Splitter L/H" ? "splitter_lh." : (n == "Splitter L/M/H" ? "splitter_lmh." : "splitter_ms.");
        int reached = 0, wanted = 0, q = 0;
        for (int i = 0; i < static_cast<int> (synthyboy::paramCount()); ++i)
        {
            const std::string k = synthyboy::paramTable()[i].key;
            if (k.compare (0, key.size(), key) != 0 || k == key + "on" || k == key + "rack")
                continue;
            ++wanted;
            const int p = ed.controlOfKey (k.c_str());
            reached += p >= 0 && ed.rowOfControl (p) >= 0;
            for (int rr = 0; rr < ed.numRows(); ++rr)
                for (int c = 0; c < 4; ++c)
                    q += ed.memberControl (rr, c) == p && p >= 0;
            check (p >= 0 && r.ui.paramIndexOf (item, p) == i, n + ": " + k + " has its control and it writes the engine's key");
        }
        check (reached == wanted && q == wanted, n + ": every parameter is on the page and a member of a Q group (" + std::to_string (reached) + " / " + std::to_string (wanted) + ")");
        // a toggle writes the bypass, a knob the frequency
        const int bp = ed.controlOfKey ((key + (sp.kind == ui::fxl::kKindSplitterMs ? "mid_bypass" : "low_bypass")).c_str());
        r.tap (*ed.memberOf (bp));
        check (r.inst.param (r.ui.paramIndexOf (item, bp)) > 0.5f, n + ": a tap on the bypass writes it");
        r.refresh (2);
        check (ed.bands.lane[0].tile.state.bypassed && ed.toggles[bp].state.on, n + ": the tile of the first band and the toggle follow the bypass");
        const int lvl = ed.controlOfKey ((key + "level").c_str());
        const ui::Rect k = ed.knobs[lvl].rect();
        const float before = r.inst.param (r.ui.paramIndexOf (item, lvl));
        r.drag (k.x + k.w / 2, k.y + k.h / 2, k.x + k.w / 2, k.y + k.h / 2 - 60, true);
        check (r.inst.param (r.ui.paramIndexOf (item, lvl)) != before, n + ": the level knob writes the level");
        // the power zone and the picture
        const ui::Rect pr = r.ui.screen().chainBar.cards[0].powerRect();
        r.tapAt (pr.x + pr.w / 2, pr.y + pr.h / 2);
        check (! r.ui.screen().chainBar.cards[0].state.on, n + ": the power zone switches it off");
        auto data = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        check (! sb.overflow && cv.size() == 1280u * 800u, n + ": the page is drawn");
    }
    // a splitter that is on in the engine has its card without any edit of ours
    FxRig r;
    r.inst.set ("splitter_lmh.on", 1.0f);
    r.refresh();
    check (r.ui.numCards() == 1 && r.ui.screen().chainBar.cards[0].state.type == ui::fxl::kKindSplitterLmh && r.ui.screen().slotEditor.currentType() == ui::fxl::kKindSplitterLmh, "a splitter that is on (a preset) has its card and its editor");
    r.inst.set ("splitter_lmh.on", 0.0f);
    r.refresh (2);
    check (r.ui.numCards() == 1 && ! r.ui.screen().chainBar.cards[0].state.on, "switched off in the engine the selected module keeps its card (dim), as the other modules of the fixed chain");
}


// ---- the racks of the bands of the splitters -----------------------------------------------------------------------------------------------------------------
/** Taps the row of the open + ADD list that holds the effect called `name` (false: not on the list). */
bool pickFromList (FxRig& r, const char* name)
{
    for (int i = 0; i < r.ui.list().state.count; ++i)
        if (std::string (r.ui.list().state.items[i]) == name)
        {
            r.ui.list().state.current = i;
            r.ui.list().showCurrent();
            const ui::Rect row = r.ui.list().rowRect (i);
            r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
            return true;
        }
    return false;
}

/** The walk main -> splitter -> band -> effect -> back, on the three splitters and every band: the chain of a band is the chain UI of the main chain (cards with icon, power and name; + ADD; reorder; remove; the rack
    switch; the same table pages and Q groups), and the way back is shown in the title. */
void testBandRacks()
{
    using synthyboy::FxRackItem;
    namespace fxl = ui::fxl;
    FxRig r;
    r.refresh();
    addKind (r, fxl::kKindSplitterLmh);   // the splitter L/M/H: the module 14, on, at the end of Main
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    check (r.ui.scope() == 0 && ed.hasBands() && ed.bands.numBands() == 3 && ed.bands.numCards (0) == 0 && ed.bands.numCards (1) == 0, "the page of the splitter L/M/H has three lanes, all empty");
    check (std::string (ed.bands.lane[0].tile.state.name) == "LOW" && std::string (ed.bands.lane[1].tile.state.name) == "MID" && std::string (ed.bands.lane[2].tile.state.name) == "HIGH", "the lanes are LOW, MID, HIGH");
    check (std::string (ed.bands.lane[0].tile.state.range) == "below 200 Hz" && std::string (ed.bands.lane[1].tile.state.range) == "200 Hz to 3.0 kHz" && std::string (ed.bands.lane[2].tile.state.range) == "above 3.0 kHz", "the tiles say what each band is");
    check (std::string (ed.path()) == "MAIN CHAIN", "the title of the splitter's page says where it is: " + std::string (ed.path()));
    for (int b = 0; b < 3; ++b)
    {
        const ui::Rect t = ed.bands.lane[b].tile.rect(), a = ed.bands.lane[b].add.rect();
        check (t.h >= 56 && a.h >= 56 && a.w >= 64 && ed.bands.lane[b].add.state.isPlus, "lane " + std::to_string (b) + ": the tile and + ADD are touch targets");
    }

    // + ADD of the MID lane opens the band's chain and the list of the types that are free
    r.tap (ed.bands.lane[1].add);
    check (r.ui.scope() == 6 && r.ui.listOpen() && r.ui.list().state.count == 13, "+ ADD of the MID lane opens the band (rack 6) and the list of the 13 free types (no splitters in a band)");
    check (pickFromList (r, "Delay"), "Delay is on the list");
    FxRackItem dly { false, 2 };
    check (r.inst.get ("delay.on") > 0.5f && r.inst.get ("delay.rack") == 6.0f && r.ui.numCards() == 1 && r.ui.cardItem (0) == dly, "the delay module is switched on and moved into rack 6");
    check (r.ui.screen().chainBar.backShown() && std::string (r.ui.screen().chainBar.backCard.state.name) == "SPLITTER L/M/H" && std::string (r.ui.screen().chainBar.backCard.state.tag) == "MID BAND", "the chain bar starts with the BACK card of the splitter and its band");
    check (r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeDelay && std::string (ed.path()) == "MAIN CHAIN  >  SPLITTER L/M/H  >  MID BAND", "the delay's page is the table page of the delay, the title says the place: " + std::string (ed.path()));
    check (r.ui.screen().chainBar.cards[0].state.rack == 6 && std::string (r.ui.screen().chainBar.cards[0].state.name) == "Delay", "the card is the delay of rack 6");
    check (r.ui.qTargets()[0] >= 0 && r.ui.qSlotCount() == 16 && r.inst.q[0] == r.ui.paramIndexOf (dly, ed.memberControl (0, 0)), "the Q group of a band effect is a row of the table page on Q 1 .. 4 (as in the main chain)");
    // the knobs of the effect write the module's own keys
    {
        const int fb = ed.controlOfKey ("delay.feedback");
        const ui::Rect k = ed.knobs[fb].rect();
        const float before = r.inst.get ("delay.feedback");
        r.drag (k.x + k.w / 2, k.y + k.h / 2, k.x + k.w / 2, k.y + k.h / 2 - 50, true);
        check (fb >= 0 && r.inst.get ("delay.feedback") != before, "a knob of the effect in a band writes the engine's key");
    }
    // a second effect (the list no longer offers the delay), then the order
    r.tap (r.ui.screen().chainBar.plusCard);
    check (r.ui.listOpen() && r.ui.list().state.count == 12, "the list of a band offers the types that are free (12 after the delay)");
    check (pickFromList (r, "Reverb") && r.ui.numCards() == 2 && r.ui.cardItem (1) == (FxRackItem { false, 8 }) && r.inst.get ("reverb.rack") == 6.0f, "a reverb is added behind the delay in the same band");
    // drag the reverb in front of the delay
    {
        const ui::Rect from = r.ui.screen().chainBar.cards[1].rect(), to = r.ui.screen().chainBar.cards[0].rect();
        r.drag (from.x + from.w / 2 - 20, from.y + from.h - 8, to.x + 6, from.y + from.h - 8, true);
        check (r.ui.bandItems (6).size() == 2 && r.ui.bandItems (6)[0] == (FxRackItem { false, 8 }) && r.ui.bandItems (6)[1] == dly, "a drag of a card reorders the chain of the band (the reverb first)");
        check (r.ui.cardItem (0) == (FxRackItem { false, 8 }) && r.ui.selectedItem() == (FxRackItem { false, 8 }), "the card that was moved stays selected");
    }
    // the power of a card
    {
        const ui::Rect pr = r.ui.screen().chainBar.cards[0].powerRect();
        r.tapAt (pr.x + pr.w / 2, pr.y + pr.h / 2);
        check (r.inst.get ("reverb.on") < 0.5f && r.ui.numCards() == 2 && ! r.ui.screen().chainBar.cards[0].state.on, "the power of a card bypasses the effect, and it stays in its band (dim)");
        r.tapAt (pr.x + pr.w / 2, pr.y + pr.h / 2);
        check (r.inst.get ("reverb.on") > 0.5f && r.ui.screen().chainBar.cards[0].state.on, "and switches it on again");
    }
    // the rack switch reaches every rack: MAIN, BUS 1, BUS 2 and the seven band racks
    {
        r.tap (r.ui.screen().chainBar.cards[1]);   // the delay
        check (r.ui.screen().slotEditor.rack.state.count == 10, "the rack switch of a module has ten racks");
        check (r.ui.screen().slotEditor.rack.state.index == 6, "it stands on the band the effect is in (L/M/H MID)");
        r.tap (r.ui.screen().slotEditor.rack);   // the right half: L/M/H HIGH
        check (r.inst.get ("delay.rack") == 7.0f && r.ui.scope() == 7 && r.ui.numCards() == 1 && std::string (ed.path()) == "MAIN CHAIN  >  SPLITTER L/M/H  >  HIGH BAND", "the next rack is the HIGH band: the page follows the effect (" + std::string (ed.path()) + ")");
        check (r.ui.bandItems (6).size() == 1 && r.ui.bandItems (7).size() == 1, "the MID band keeps the reverb, the HIGH band has the delay");
    }
    // the way back: the BACK card leads to the splitter's page, where the lanes show the chains
    r.tap (r.ui.screen().chainBar.backCard);
    check (r.ui.scope() == 0 && ! r.ui.screen().chainBar.backShown() && r.ui.screen().slotEditor.currentType() == fxl::kKindSplitterLmh, "BACK returns to the page of the splitter");
    check (ed.bands.numCards (0) == 0 && ed.bands.numCards (1) == 1 && ed.bands.numCards (2) == 1, "its lanes show the chains of the bands (0, 1, 1 cards)");
    check (std::string (ed.bands.lane[1].tile.state.info) == "1 effect" && std::string (ed.bands.lane[0].tile.state.info) == "no effect", "the tiles count the effects");
    check (std::string (ed.bands.lane[1].cards[0].state.name) == "Reverb" && ed.bands.lane[1].cards[0].state.type == synthyboy::kFxTypeReverb && ed.bands.lane[1].cards[0].state.on, "a lane card has the picture, the power and the name of its effect");
    // a tap on a card of a lane opens the band with that effect selected
    r.tap (ed.bands.lane[2].cards[0]);
    check (r.ui.scope() == 7 && r.ui.selectedItem() == dly && r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeDelay, "a tap on a card in a lane opens that band with the effect selected");
    r.tap (r.ui.screen().chainBar.backCard);
    // the power of a lane card, the tile of a band
    {
        const ui::Rect pr = ed.bands.lane[2].cards[0].powerRect();
        r.tapAt (pr.x + pr.w / 2, pr.y + pr.h / 2);
        check (r.inst.get ("delay.on") < 0.5f && ! ed.bands.lane[2].cards[0].state.on && r.ui.scope() == 0, "the power of a card in a lane switches the effect (the page stays)");
        r.tapAt (pr.x + pr.w / 2, pr.y + pr.h / 2);
        check (r.inst.get ("delay.on") > 0.5f, "and back on");
    }
    r.tap (ed.bands.lane[0].tile);
    check (r.ui.scope() == 5 && r.ui.numCards() == 0 && r.ui.screen().galleryShown() && ! r.ui.screen().editorShown(), "the tile of an empty band opens it: the gallery of the free types (an empty band has no page)");
    check (r.ui.screen().gallery.numTypes() == 11 && r.ui.screen().gallery.typeAt (0) == 1, "the gallery offers the 11 free types (no delay, no reverb: they are in bands)");
    r.tap (r.ui.screen().chainBar.backCard);
    // the splitter off: the lanes are idle
    r.inst.set ("splitter_lmh.on", 0.0f);
    r.refresh (2);
    check (ed.bands.lane[1].tile.state.idle && std::string (ed.bands.lane[1].tile.state.info) == "SPLITTER OFF", "a splitter that is off says so on its lanes (its bands' effects do nothing)");
    r.inst.set ("splitter_lmh.on", 1.0f);
    r.refresh (2);

    // REMOVE takes an effect out of its band and switches it off
    r.tap (ed.bands.lane[1].cards[0]);
    check (r.ui.scope() == 6 && r.ui.screen().slotEditor.remove.rect().w >= 56, "the page of a band effect has REMOVE");
    r.tap (r.ui.screen().slotEditor.remove);
    check (r.inst.get ("reverb.on") < 0.5f && r.inst.get ("reverb.rack") == 0.0f && r.ui.numCards() == 0 && r.ui.bandItems (6).empty(), "REMOVE switches the effect off and takes it out of the band (rack Main)");
    check (r.ui.screen().galleryShown() && r.ui.screen().gallery.numTypes() == 12, "the band is empty again and offers 12 types (the reverb is free)");

    // an effect that is on in the main chain is not on the list of a band (a module sits in one place)
    r.inst.set ("chorus.on", 1.0f);
    r.refresh (2);
    r.tap (r.ui.screen().chainBar.plusCard);
    bool chorusListed = false;
    for (int i = 0; i < r.ui.list().state.count; ++i)
        chorusListed = chorusListed || std::string (r.ui.list().state.items[i]) == "Chorus";
    check (r.ui.listOpen() && ! chorusListed && r.ui.list().state.count == 11, "an effect that is on elsewhere is not offered to a band (11 types)");
    r.tapAt (20, 130);
    r.tap (r.ui.screen().chainBar.backCard);
    r.inst.set ("chorus.on", 0.0f);
    r.refresh (2);
    check (r.ui.scope() == 0 && r.ui.screen().slotEditor.currentType() == fxl::kKindSplitterLmh, "back at the splitter");
}

/** The band racks of the three splitters, all seven racks: an effect parked in a rack (a preset) has its card in the right lane, its page, and every module can be moved into every rack by the rack switch and by the lanes. */
void testEveryBandRack()
{
    namespace fxl = ui::fxl;
    const int kSplit[3] = { fxl::kKindSplitterLh, fxl::kKindSplitterLmh, fxl::kKindSplitterMs };
    static const char* const kPre[3] = { "splitter_lh", "splitter_lmh", "splitter_ms" };
    for (int si = 0; si < 3; ++si)
        for (int b = 0; b < fxl::bandsOfSplitter (kSplit[si]); ++b)
        {
            const int rack = fxl::bandRack (kSplit[si], b);
            const std::string name = std::string (fxl::layoutOf (kSplit[si]).name) + " " + fxl::bandName (kSplit[si], b);
            int k2 = 0, b2 = 0;
            check (fxl::bandOfRack (rack, k2, b2) && k2 == kSplit[si] && b2 == b && std::string (fxl::rackName (rack)).size() > 4, name + ": rack " + std::to_string (rack) + " maps back to its band");
            // a preset: every module of the 13 sits in this rack, on
            FxRig r;
            r.inst.set ((std::string (kPre[si]) + ".on").c_str(), 1.0f);
            for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
            {
                const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
                r.inst.setAt (d.onIndex, 1.0f);
                r.inst.setAt (d.rackIndex, static_cast<float> (rack));
            }
            r.refresh (2);
            // the splitter is the one card of Main (the modules are in the band, not in Main)
            check (r.ui.numCards() == 1 && r.ui.screen().slotEditor.currentType() == kSplit[si] && r.ui.screen().slotEditor.bands.numCards (b) == 13, name + ": the lane has the 13 modules as cards");
            for (int other = 0; other < r.ui.screen().slotEditor.bands.numBands(); ++other)
                if (other != b)
                    check (r.ui.screen().slotEditor.bands.numCards (other) == 0, name + ": the other lanes are empty");
            { const ui::Rect cr = r.ui.screen().slotEditor.bands.lane[b].cards[12].rect(); r.tapAt (cr.x + 8, cr.y + cr.h - 10); }   // (the power zone is the top right square)
            check (r.ui.scope() == rack && r.ui.numCards() == 13 && r.ui.screen().slotEditor.currentType() == synthyboy::kFxTypeConvolve, name + ": its card opens the band: 13 cards, the last one is the convolver");
            r.tap (r.ui.screen().chainBar.backCard);
            check (r.ui.scope() == 0 && r.ui.screen().slotEditor.currentType() == kSplit[si], name + ": back at the splitter");
        }
    // every rack is reachable by the rack switch (the ten values of the engine) for every one of the 13 modules; it writes the module's own key, and the power of its card writes its own on key
    for (int m = 0; m < 13; ++m)
        for (int target = 0; target < fxl::kNumRacks; ++target)
        {
            const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (m + 1);
            const std::string nm = std::string (d.prefix) + " -> " + fxl::rackName (target);
            FxRig r;
            r.inst.setAt (d.onIndex, 1.0f);
            r.inst.set ("splitter_lh.on", 1.0f);
            r.inst.set ("splitter_lmh.on", 1.0f);
            r.inst.set ("splitter_ms.on", 1.0f);
            r.refresh (2);
            int card = -1;
            for (int i = 0; i < r.ui.numCards(); ++i)
                if (r.ui.cardItem (i) == (synthyboy::FxRackItem { false, m }))
                    card = i;
            check (card >= 0, nm + ": the module has its card in Main");
            r.tap (r.ui.screen().chainBar.cards[card]);
            for (int guard = 0; guard < 12 && r.ui.screen().slotEditor.rack.state.index != target; ++guard)
                r.tap (r.ui.screen().slotEditor.rack);
            check (r.inst.param (d.rackIndex) == static_cast<float> (target) && r.ui.screen().slotEditor.rack.state.index == target, nm + ": the rack switch writes the rack key");
            check (r.ui.scope() == (target >= 3 ? target : 0), nm + ": the page follows the effect");
            // the power of its card (in the chain bar of its rack) writes the on key
            int c2 = -1;
            for (int i = 0; i < r.ui.numCards(); ++i)
                if (r.ui.cardItem (i) == (synthyboy::FxRackItem { false, m }))
                    c2 = i;
            check (c2 >= 0, nm + ": the card is on the page of its rack");
            const ui::Rect pr = r.ui.screen().chainBar.cards[c2].powerRect();
            r.tapAt (pr.x + pr.w / 2, pr.y + pr.h / 2);
            check (r.inst.param (d.onIndex) < 0.5f, nm + ": the power of the card writes the on key");
        }
    // the splitters and the slots of the pool stay in the buses: their rack switch has three values
    {
        FxRig r;
        r.refresh();
        addKind (r, fxl::kKindSplitterMs);
        check (r.ui.screen().slotEditor.rack.state.count == 3, "a splitter's rack switch has three racks (a splitter cannot sit in a band)");
        addEffect (r, 2);   // a delay of the pool
        check (r.ui.screen().slotEditor.rack.state.count == 3 && r.ui.cardItem (r.ui.selectedSlot()).pool, "so has a slot of the pool (its rack key has three values)");
    }
}


/** A drag inside a lane reorders the chain of that band. */
void testBandLaneDrag()
{
    namespace fxl = ui::fxl;
    FxRig r;
    r.inst.set ("splitter_lh.on", 1.0f);
    r.inst.set ("eq.on", 1.0f);
    r.inst.set ("eq.rack", 4.0f);
    r.inst.set ("phaser.on", 1.0f);
    r.inst.set ("phaser.rack", 4.0f);
    r.inst.set ("bode.on", 1.0f);
    r.inst.set ("bode.rack", 4.0f);
    r.refresh (2);
    ui::FxBandBoard& bd = r.ui.screen().slotEditor.bands;
    check (r.ui.scope() == 0 && r.ui.screen().slotEditor.currentType() == fxl::kKindSplitterLh && bd.numBands() == 2 && bd.numCards (0) == 0 && bd.numCards (1) == 3, "the HIGH lane of the L/H splitter has three cards");
    const auto before = r.ui.bandItems (4);
    const ui::Rect last = bd.lane[1].cards[2].rect(), first = bd.lane[1].cards[0].rect();
    r.drag (last.x + 10, last.y + last.h - 8, first.x + 10, first.y + first.h - 8, true);
    const auto after = r.ui.bandItems (4);
    check (before.size() == 3 && after.size() == 3 && after[0] == before[2] && after[1] == before[0] && after[2] == before[1], "a drag of the last card of a lane to the first place moves it there; the others keep their order");
    check (r.ui.scope() == 0, "a drag in a lane does not open the band");
}

/** The number field of the FX editor: VOICES and UNISON are whole numbers. */
void testNumberFields()
{
    FxRig r;
    r.refresh();
    addEffect (r, 0);   // Chorus
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    const synthyboy::FxRackItem item = r.ui.cardItem (0);
    const int v = ed.controlOfKey ("chorus.voices");
    check (v >= 0 && ed.kindOf (v) == ui::FxSlotEditor::Kind::Number && isShown (ed, &ed.numbers[v]) && ! isShown (ed, &ed.knobs[v]), "the VOICES of the Chorus is a number field, not a knob");
    check (std::string (ed.numbers[v].state.label) == "VOICES" && ed.numbers[v].state.min == 1.0 && ed.numbers[v].state.max == 4.0 && ed.numbers[v].state.step == 1.0 && ed.numbers[v].state.value == 4.0, "it counts 1 .. 4 in whole steps and shows 4");
    const ui::Rect nr = ed.numbers[v].rect();
    r.drag (nr.x + nr.w / 2, nr.y + nr.h / 2, nr.x + nr.w / 2, nr.y + nr.h / 2 + 16, true);   // down 2 steps at 8 px
    const float plain = synthyboy::fxInstancePlain (synthyboy::kFxTypeChorus, v, r.inst.param (r.ui.paramIndexOf (item, v)));
    check (plain == 2.0f && ed.numbers[v].state.value == 2.0, "a drag down by two steps writes the whole number 2 (" + std::to_string (plain) + ")");
    r.ui.screen().setActiveGroup (1);
    r.refresh();
    r.ui.turnQ (0, 1.0, r.stack, r.inst);
    check (synthyboy::fxInstancePlain (synthyboy::kFxTypeChorus, v, r.inst.param (r.ui.paramIndexOf (item, v))) == 3.0f, "a Q-Link step moves the number by one");
    check (ed.groups[1].member (0) == &ed.numbers[v], "the number field is a member of the Q group of its row");
    // the drawn box is limited, the touch zone is the whole cell
    check (nr.w > 200 && ed.numbers[v].maxBox.w == 200, "the field is drawn in the middle of its cell (the touch zone is the cell)");
    FxRig h;
    h.refresh();
    addEffect (h, 9);   // Hyper / Dimension
    ui::FxSlotEditor& he = h.ui.screen().slotEditor;
    const int u = he.controlOfKey ("hyperdim.unison");
    check (u >= 0 && he.kindOf (u) == ui::FxSlotEditor::Kind::Number && he.numbers[u].state.min == 0.0 && he.numbers[u].state.max == 7.0, "the UNISON of the Hyper / Dimension is a number field 0 .. 7");
    // only whole parameters are number fields: every continuous parameter of every effect that has interval 1 is one
    bool all = true;
    for (int kind = 1; kind < ui::fxl::kNumKinds; ++kind)
    {
        FxRig q;
        q.refresh();
        addKind (q, kind);
        for (int p = 0; p < q.ui.screen().slotEditor.numParams(); ++p)
        {
            const synthyboy::ParamInfo* pi = q.ui.screen().slotEditor.infoOf (p);
            if (pi != nullptr)
                all = all && ((q.ui.screen().slotEditor.kindOf (p) == ui::FxSlotEditor::Kind::Number) == (pi->numChoices == 0 && pi->interval == 1.0f));
        }
    }
    check (all, "a number field exactly where the parameter is a whole number");
}

/** The names of the laws and types on the screen: by what they do, no name of another product anywhere on the FX pages. */
void testChoiceNames()
{
    check (std::string (ui::shownChoice ("eq.law", 0, "Vital")) == "State variable" && std::string (ui::shownChoice ("eq.law", 1, "Serum")) == "Biquad", "the EQ laws are State variable and Biquad");
    check (std::string (ui::shownChoice ("convolve.law", 0, "Vital")) == "Relative" && std::string (ui::shownChoice ("convolve.law", 1, "Serum")) == "Seconds", "the Convolve laws are Relative and Seconds");
    check (std::string (ui::shownChoice ("osc2.unison.law", 0, "Vital")) == "Equal power" && std::string (ui::shownChoice ("osc2.unison.law", 1, "Serum")) == "Fixed gain", "the unison laws are Equal power and Fixed gain (the same function for the oscillator pages)");
    check (std::string (ui::shownChoice ("compressor.limit", 2, "Serum")) == "Peak" && std::string (ui::shownChoice ("compressor.limit", 1, "Limit")) == "Limit", "the third compressor mode is Peak");
    check (std::string (ui::shownChoice ("reverb.type", 0, "Vital")) == "Dense" && std::string (ui::shownChoice ("reverb.type", 2, "Hall")) == "Hall", "the first reverb algorithm is Dense, the others keep their names");
    check (std::string (ui::shownLabel ("eq.law")) == "BAND SHAPE" && std::string (ui::shownLabel ("convolve.law")) == "SCALING" && std::string (ui::shownLabel ("reverb.type")) == "ALGORITHM" && ui::shownLabel ("chorus.rate") == nullptr, "the labels of the laws and of the algorithm");
    // no page of an effect shows a name of another product: walk the pages of every effect in the state of every choice of the laws and of the type
    bool clean = true;
    std::string where;
    for (int kind = 1; kind < ui::fxl::kNumKinds; ++kind)
    {
        FxRig r;
        r.refresh();
        addKind (r, kind);
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        for (int p = 0; p < ed.numParams(); ++p)
        {
            const synthyboy::ParamInfo* pi = ed.infoOf (p);
            if (pi == nullptr || pi->numChoices <= 0)
                continue;
            const synthyboy::FxRackItem item = r.ui.cardItem (0);
            for (int c = 0; c < pi->numChoices; ++c)
            {
                r.inst.setAt (r.ui.paramIndexOf (item, p), item.pool ? synthyboy::fxInstanceNormalise (kind, p, static_cast<float> (c)) : static_cast<float> (c));
                r.refresh (2);
                std::string seen = std::string (ed.steppers[p].state.text) + "|" + ed.steppers[p].state.caption + "|" + ed.lists[p].state.title + "|" + ed.toggles[p].state.label;
                for (int o = 0; o < ed.lists[p].state.count; ++o)
                    seen += std::string ("|") + ed.lists[p].state.names[o];
                if (seen.find ("Vital") != std::string::npos || seen.find ("Serum") != std::string::npos)
                {
                    clean = false;
                    where = pi->key;
                }
            }
        }
    }
    check (clean, "no page of an FX editor shows 'Vital' or 'Serum'" + (clean ? std::string() : " (" + where + ")"));
}

void testFxPagesAndDisplays()
{
    FxRig r;
    r.refresh();
    addEffect (r, 1);   // compressor: 24 parameters, a display, 8 knobs a page
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    check (ed.numParams() > 12 && ed.numPages() == 3 && ed.hasDisplay() && ed.rowsPerPage() == 2, "the compressor has a display, two rows of 8 knobs in the default view and a MORE view");
    check (ed.tabs.rect().w >= 168 && ed.tabs.rect().h >= 56 && ed.pageIndex() == 0, "the MORE button is a touch target");
    ed.setPage (ed.pageIndex() == 0 ? 1 : 0); r.refresh();
    check (ed.pageIndex() == 1 && ed.isMoreView() && ed.tabs.state.index == 1 && ed.activeRow() == 2, "MORE shows the second view of the same editor and its first row is the Q group");
    ed.setPage (ed.pageIndex() == 0 ? 1 : 0); r.refresh();
    check (ed.pageIndex() == 0 && ! ed.isMoreView() && ed.tabs.state.index == 0, "and LESS goes back to the default view");
    // the transfer curve: the thresholds and the ratios
    const ui::FxDisplay::CompBand b { -40.0f, -20.0f, 0.0f, 0.5f, 0.0f };
    check (std::fabs (ui::FxDisplay::compOut (b, -30.0f) + 30.0f) < 1.0e-4f && std::fabs (ui::FxDisplay::compOut (b, 0.0f) - (-20.0f + 20.0f * 0.5f)) < 1.0e-4f, "between the thresholds the line is 1 : 1 and above the upper it is bent by the ratio");
    check (ui::FxDisplay::shape (0, 0.0f, 0.0f) == 0.0f && std::fabs (ui::FxDisplay::shape (1, 20.0f, 0.5f) - 1.0f) < 1.0e-6f, "the waveshapers pass the middle and clip");
    // the other displays have their data
    FxRig d;
    d.refresh();
    addEffect (d, 2);   // delay
    setPool (d, d.ui.cardItem (0).index, synthyboy::kFxTypeDelay, "delay.sync", 1.0f);
    setPool (d, d.ui.cardItem (0).index, synthyboy::kFxTypeDelay, "delay.division", 4.0f);   // 1/4 note at 120 bpm = 500 ms
    d.refresh();
    check (d.ui.screen().slotEditor.display.kind == ui::FxDisplay::Kind::Delay && std::fabs (d.ui.screen().slotEditor.display.delay.timeMs1 - 500.0f) < 1.0f, "the delay display turns a note value into milliseconds at the tempo (" + std::to_string (d.ui.screen().slotEditor.display.delay.timeMs1) + " ms)");
    FxRig f;
    f.refresh();
    addEffect (f, 5);   // FX filter
    f.refresh();
    const ui::FxDisplay& fd = f.ui.screen().slotEditor.display;
    check (fd.kind == ui::FxDisplay::Kind::Filter && fd.filter.db != nullptr && fd.filter.n == 256, "the FX filter display has the 256 points of the real response");
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < fd.filter.n; ++i) { lo = std::min (lo, fd.filter.db[i]); hi = std::max (hi, fd.filter.db[i]); }
    check (hi - lo > 6.0f, "the response of the filter is not flat (" + std::to_string (lo) + " .. " + std::to_string (hi) + " dB)");
}

void testFxDraw()
{
    FxRig r;
    r.refresh();
    addEffect (r, 4);
    addEffect (r, 1);
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 100, "the FX page scene fits in the buffer (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "the CPU renderer draws the FX page");
}

} // namespace

int main()
{
    std::printf ("=== SynthyBoy MPC Overlay: Mixer & FX Host Tests ===\n");
    testMixerLayout();
    testMixerBoldCut();
    testMixerValuesAndWrites();
    testMixerGroupsAndList();
    testMixerQPerStrip();
    testMixerDraw();
    testMixerPeek();
    testMixerCompleteNativeXL();
    testBusAdd<1>();
    testBusAdd<2>();
    testDistortionMarkerLayers();
    testFxRackSnapshot();
    testFxGalleryEmptyState();
    testFxChain();
    testFxPresetGenerationRetainsIdentity();
    testFxListBackDuringTouch();
    testFxFallbackChoiceAfterPowerOrRecall();
    testEveryEffectType();
    testFxEditing();
    testFxPagesAndDisplays();
    testValueText();
    testClosingUpRule();
    testSplitterPages();
    testBandRacks();
    testEveryBandRack();
    testBandLaneDrag();
    testNumberFields();
    testChoiceNames();
    testFxDraw();
    std::printf (g_fails ? "mixfx_ui_host: FAILED (%d failures)\n" : "mixfx_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
