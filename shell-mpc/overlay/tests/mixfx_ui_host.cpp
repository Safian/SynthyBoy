// SynthyBoy - MPC overlay tests: the MIXER and FX UI (mixer_ui.h, fx_ui.h), the pages of the Mixer and FX tabs: layout and touch targets >= 56 px, the values in the parameters' own units, the parameters in both
// directions, the Q groups (one frame around the column of a strip, the row of knobs of an effect), the destination list, the meters of the engine, the chain of the effects (add, select, power, rack, remove,
// move), every effect type (names, values, no empty surface), the displays, and the CPU reference renderer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../fx_ui.h"
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
using FxRig = demo::Rig<FxUi>;

// ---- the mixer -------------------------------------------------------------------------------------------------------------------------------------------------
void testMixerLayout()
{
    MixRig r;
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();
    check (m.rect().w == 1280 && m.rect().h == 628 && r.stack.element() == MixerUi::kElementPage, "the page has the whole tab and is the element of the Mixer tab");
    bool targets = true, family = true, inside = true;
    for (int s = 0; s < 10; ++s)
    {
        const ui::MixerStrip& st = m.strips[s];
        targets = targets && st.power.rect().h >= 56 && st.destButton.rect().h >= 56 && st.pan.rect().h >= 56 && st.send1.rect().h >= 56 && st.send2.rect().h >= 56 && st.fader.rect().h >= 150 && st.sizeOk();
        family = family && st.pan.rect().w == m.strips[0].pan.rect().w && st.fader.rect().h == m.strips[0].fader.rect().h;
        inside = inside && st.rect().contains (st.pan.rect()) && st.rect().contains (st.fader.rect());
        ui::Component* leaves[16];
        ui::Component* all[64];
        const int n = m.collectLeaves (all, 0, 64);
        (void) leaves;
        inside = inside && m.groups[s].check (all, n) == 0 && st.rect().contains (m.groups[s].rect());
    }
    check (targets, "every cell of every strip is >= 56 px high, the fader >= 150 px, and the strip has its minimum size");
    check (family, "all strips are one family (the same pan and fader size)");
    check (inside, "the Q group of every strip lies in its strip and nothing else is in its frame");
    check (m.strips[0].pan.rect().w >= 96 && m.strips[0].pan.rect().h == 56, "the PAN is a 56 px slider cell, not a speck (" + std::to_string (m.strips[0].pan.rect().w) + " x " + std::to_string (m.strips[0].pan.rect().h) + ")");
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
    check (std::string (m.strips[1].pan.state.valueText) == "L 35", "the pan is L 35 (" + std::string (m.strips[1].pan.state.valueText) + ")");
    check (std::string (m.strips[1].send1.state.valueText) == "40.0%", "the send is 40.0% (" + std::string (m.strips[1].send1.state.valueText) + ")");
    check (std::string (m.strips[7].pan.state.valueText) == "-" && ! m.strips[7].pan.enabled(), "a bus has no pan: a dash, dim");

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
    r.tap (m.strips[7].power);
    check (r.inst.get ("bus1.fx_bypass") == 1.0f, "tapping BUS 1 sets the bypass of its rack (the switch is inverted)");
    // a double tap on the fader resets it
    r.tapAt (f.x + f.w / 2, f.y + f.h / 2, 30);
    r.tapAt (f.x + f.w / 2, f.y + f.h / 2, 30);
    check (r.inst.get ("osc1.level") == synthyboy::paramTable()[demo::Inst::idx ("osc1.level")].def, "a double tap resets the fader to its default");
}

void testMixerGroupsAndList()
{
    MixRig r;
    r.refresh();
    ui::MixerFullScreen& m = r.ui.screen();
    check (m.activeGroup() == 0 && r.inst.q[0] == demo::Inst::idx ("sub.level") && r.inst.q[1] == demo::Inst::idx ("sub.pan"), "the first strip's group is the Q group: LEVEL, PAN, B1, B2");
    r.tap (m.strips[6].send1);
    check (m.activeGroup() == 6 && r.inst.q[0] == demo::Inst::idx ("filter2.level") && r.inst.q[3] == demo::Inst::idx ("filter2.bus2_send"), "a touch in a strip makes it the Q group");
    const ui::Rect gr = m.groups[6].rect();
    check (m.strips[6].rect().contains (gr) && gr.w >= 100, "the frame is one column around the strip's four members");
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

// ---- the FX page -----------------------------------------------------------------------------------------------------------------------------------------------
void addEffect (FxRig& r, int typeRow)
{
    r.tap (r.ui.screen().chainBar.plusCard);
    r.ui.list().state.current = typeRow;
    r.ui.list().showCurrent();   // the list scrolls: the row is in its window
    const ui::Rect row = r.ui.list().rowRect (typeRow);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
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

void testFxChain()
{
    FxRig r;
    r.refresh();
    check (r.ui.numCards() == 0 && ! r.ui.screen().editorShown(), "an empty chain shows the hint and no editor");
    r.tap (r.ui.screen().chainBar.plusCard);
    check (r.ui.listOpen() && r.ui.list().state.count == synthyboy::kNumFxInstanceTypes - 1, "+ ADD opens the list of the effect types (13)");
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

void testEveryEffectType()
{
    for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
    {
        FxRig r;
        r.refresh();
        addEffect (r, t - 1);
        ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
        const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
        bool ok = ed.currentType() == t && ed.numParams() == d.count;
        int shown = 0, withValue = 0, withName = 0, small = 0;
        // the knobs of the first page
        for (int i = 0; i < ui::FxSlotEditor::kMaxParams; ++i)
        {
            const ui::Knob& k = ed.knobs[i];
            if (k.rect().w <= 0)
                continue;
            bool visibleKnob = false;
            for (int c = 0; c < ed.numChildren(); ++c)
                visibleKnob = visibleKnob || (ed.child (c) == &k && ed.childShown (c));
            if (! visibleKnob)
                continue;
            ++shown;
            withValue += k.state.valueText[0] != '\0';
            withName += k.state.label[0] != '\0';
            small += k.rect().w < 100 || k.rect().h < 72;
        }
        ok = ok && shown == std::min (ed.numParams(), ed.rowsPerPage() * 4) && withValue == shown && withName == shown && small == 0;
        // the knobs fill the page: the lowest knob ends near the bottom of the page
        int bottom = 0;
        for (int i = 0; i < ui::FxSlotEditor::kMaxParams; ++i)
            for (int c = 0; c < ed.numChildren(); ++c)
                if (ed.child (c) == &ed.knobs[i] && ed.childShown (c))
                    bottom = std::max (bottom, ed.knobs[i].rect().bottom());
        const int rowsUsed = (std::min (ed.numParams(), ed.rowsPerPage() * 4) + 3) / 4;
        check (ok, std::string (d.name) + ": " + std::to_string (shown) + " knobs of " + std::to_string (d.count) + " on page 1 of " + std::to_string (ed.numPages()) + ", each with a name, a value and >= 100 x 72 px");
        check (rowsUsed < ed.rowsPerPage() || bottom >= ed.knobArea().bottom() - ed.knobArea().h / ed.rowsPerPage(), std::string (d.name) + ": the knobs use the page (the last row ends at " + std::to_string (bottom) + " of " + std::to_string (ed.knobArea().bottom()) + ")");
        check (ed.hasDisplay() == (ui::FxDisplay::kindOfType (t) != ui::FxDisplay::Kind::None), std::string (d.name) + ": a display exactly where the effect has a picture");
        // no text of a value is a bare number where the parameter has a unit
        bool units = true;
        for (int p = 0; p < std::min (d.count, 24); ++p)
        {
            const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (t, p);
            if (pi == nullptr || pi->numChoices > 0 || pi->unit == synthyboy::Unit::none)
                continue;
            for (int c = 0; c < ed.numChildren(); ++c)
                if (ed.child (c) == &ed.knobs[p] && ed.childShown (c))
                {
                    const std::string v = ed.knobs[p].state.valueText;
                    const bool ok = v.find_first_not_of ("0123456789.+-") != std::string::npos;
                    if (! ok)
                        std::printf ("  no unit: %s = '%s'\n", pi->key, v.c_str());
                    units = units && ok;
                }
        }
        check (units, std::string (d.name) + ": every value of a parameter that has a unit is written with it");
    }
}

void testFxEditing()
{
    FxRig r;
    r.refresh();
    addEffect (r, 4);   // EQ
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    r.refresh();
    check (std::string (ed.knobs[0].state.label) == "LOW MODE" && std::string (ed.knobs[1].state.label) == "LOW CUTOFF", "the knobs have the descriptor's names without the name of the effect (" + std::string (ed.knobs[1].state.label) + ")");
    check (std::string (ed.knobs[0].state.valueText) == "Shelf" && std::string (ed.knobs[1].state.valueText).find ("Hz") != std::string::npos, "a choice is written by name and a frequency with Hz (" + std::string (ed.knobs[1].state.valueText) + ")");
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
    check (ed.activeRow() == 1 && r.inst.q[2] == synthyboy::fxInstanceKeys (slot).p[6] && r.inst.q[0] == synthyboy::fxInstanceKeys (slot).p[4], "the Q group is the row of the knob (params 5 .. 8)");
}

void testFxPagesAndDisplays()
{
    FxRig r;
    r.refresh();
    addEffect (r, 1);   // compressor: 24 parameters, a display, 8 knobs a page
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    check (ed.numParams() > 12 && ed.numPages() > 1 && ed.hasDisplay() && ed.rowsPerPage() == 2, "the compressor has a display and pages of 8 knobs");
    check (ed.page.rect().w >= 208 && ed.page.rect().h >= 56, "the page stepper is a touch target");
    const int pages = ed.numPages();
    r.tap (ed.page);   // the right half: the next page
    check (ed.pageIndex() == 1 && ed.activeRow() == 2, "the stepper shows the next page and its first row is the Q group");
    r.tap (ed.page);
    check (ed.pageIndex() == 2 % pages || ed.pageIndex() == 2, "and the page after");
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
    testMixerValuesAndWrites();
    testMixerGroupsAndList();
    testMixerDraw();
    testFxChain();
    testEveryEffectType();
    testFxEditing();
    testFxPagesAndDisplays();
    testFxDraw();
    std::printf (g_fails ? "mixfx_ui_host: FAILED (%d failures)\n" : "mixfx_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
