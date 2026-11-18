// SynthyBoy - MPC overlay tests: Modulation Matrix and Macros UI (mod_ui_host.cpp): the pages of the Mod and Macros tabs.
// Tests parameter wiring for all 64 slots, the page of a slot, the grouped lists, the macros with their chips and their targets page, the live spectrum, touch targets, Q-link routing (the virtual row), and CPU rendering.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#include "../mod_ui.h"
#include "../macro_ui.h"
#include "../scene_cpu.h"
#include "../../spectrum.h"
#include "synthyboy/modulation.h"
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

/** Fake instance implementing IInstanceView for unit testing. */
class FakeInst final : public IInstanceView
{
public:
    FakeInst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }

    float param (int i) const noexcept override
    {
        return (i >= 0 && i < static_cast<int> (synthyboy::paramCount())) ? v_[i] : 0.0f;
    }

    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        if (! hasLive) return false;
        out = liveVals;
        return true;
    }

    double sampleRate() const noexcept override { return 48000.0; }

    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 4; ++i)
            q[i] = i < n ? t[i] : -1;
        ++qCalls;
    }

    void writeParam (int i, float norm) noexcept override
    {
        if (i < 0 || i >= static_cast<int> (synthyboy::paramCount())) return;
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
        lastWrittenNorm = norm;
    }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }

    void set (const std::string& key, float v)
    {
        const int i = idx (key);
        if (i >= 0) v_[i] = v;
    }

    float get (const std::string& key) const
    {
        const int i = idx (key);
        return i >= 0 ? v_[i] : 0.0f;
    }

    void setQVirtual (int id, float v) noexcept override { if (id >= 0 && id < kVirtualSlots) qVirtual[id] = v; }
    bool takeQVirtual (int id, float& v) noexcept override
    {
        if (id < 0 || id >= kVirtualSlots || ! qVirtualDue[id])
            return false;
        qVirtualDue[id] = false;
        v = qVirtualIn[id];
        return true;
    }
    const synthyboy_mpc::SpectrumRing* spectrumRing() const noexcept override { return ring; }

    static constexpr int kVirtualSlots = 10;   // QRouter::kNumVirtual
    float qVirtual[kVirtualSlots] = {}, qVirtualIn[kVirtualSlots] = {};
    bool qVirtualDue[kVirtualSlots] = {};
    const synthyboy_mpc::SpectrumRing* ring = nullptr;
    int q[4] = { -1, -1, -1, -1 };
    int qCalls = 0;
    int writes = 0;
    int lastWritten = -1;
    float lastWrittenNorm = 0.0f;

    bool hasLive = false;
    synthyboy::LiveValues liveVals;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y)
{
    TouchEvent e; e.kind = k; e.x = x; e.y = y; return e;
}

struct ModRig
{
    ModUi ui;
    FakeInst inst;
    UiStack stack;
    double now = 1000.0;

    ModRig()
    {
        stack.openFull (ModUi::kElementModMatrix);
        refresh();
    }

    void refresh()
    {
        now += 50.0;
        page::Input in;
        in.live = inst.hasLive ? &inst.liveVals : nullptr;
        ui.update (inst, stack, now, in);
    }

    bool tap (int x, int y, int holdMs = 40)
    {
        const bool a = ui.touch (ev (TouchEvent::Down, x, y), stack, inst, now);
        now += holdMs;
        ui.tick (now, stack, inst);
        ui.touch (ev (TouchEvent::Up, x, y), stack, inst, now);
        now += 50.0;
        refresh();
        return a;
    }

    bool tapC (const ui::Component& c, int holdMs = 40)
    {
        return tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs);
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
        now += 50.0;
        refresh();
    }
};

struct MacroRig
{
    MacroUi ui;
    FakeInst inst;
    UiStack stack;
    double now = 1000.0;

    MacroRig()
    {
        stack.openFull (MacroUi::kElementMacros);
        refresh();
    }

    void refresh()
    {
        now += 120.0;
        page::Input in;
        in.live = nullptr;
        ui.update (inst, stack, now, in);
    }

    bool tap (int x, int y, int holdMs = 40)
    {
        const bool a = ui.touch (ev (TouchEvent::Down, x, y), stack, inst, now);
        now += holdMs;
        ui.tick (now, stack, inst);
        ui.touch (ev (TouchEvent::Up, x, y), stack, inst, now);
        now += 50.0;
        refresh();
        return a;
    }

    bool tapC (const ui::Component& c, int holdMs = 40)
    {
        return tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs);
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
        now += 50.0;
        refresh();
    }
};

// ===============================================================================================================================================================
// TEST SUITE

void testSlotParamsExistence()
{
    bool allValid = true;
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source < 0 || sp.destination < 0 || sp.amount < 0 || sp.bipolar < 0 || sp.stereo < 0 || sp.bypass < 0 ||
            sp.auxSource < 0 || sp.auxOutput < 0 || sp.auxInv < 0 || sp.auxPower < 0 || sp.power < 0 ||
            sp.rise < 0 || sp.fall < 0 || sp.delay < 0 || sp.delaySync < 0 || sp.delayBeats < 0)
        {
            allValid = false;
            break;
        }
    }
    check (allValid, "all 64 slots have valid parameter table indices for all 16 parameters");
}

void testModEmptyStateAndBoard()
{
    ModRig r;
    r.refresh();
    check (r.ui.matrix().activeSlots() == 0, "matrix has 0 active slots initially");
    check (r.ui.matrix().boardShown(), "empty matrix shows quick-add board (NO MODULATION YET is replaced)");
    const ui::ModSourceBoard& b = r.ui.matrix().board;
    check (b.numSources() == static_cast<int> (synthyboy::modSourceCount()), "board has all 57 engine sources (" + std::to_string (b.numSources()) + ")");
    check (b.numGroups() == 5, "board has 5 source groups");
    check (b.fits(), "all chips fit inside the available height");
    {
        // the LFOS row reads LFO 1 .. LFO 10, then RLFO 1 .. RLFO 4 (display order; the chips carry the engine's choice numbers)
        std::vector<std::string> lfos;
        for (int i = 0; i < b.numSources(); ++i)
            if (std::strncmp (b.sourceLabel (i), "LFO ", 4) == 0 || std::strncmp (b.sourceLabel (i), "RLFO ", 5) == 0)
                lfos.push_back (b.sourceLabel (i));
        std::vector<std::string> want;
        for (int k = 1; k <= 10; ++k) want.push_back ("LFO " + std::to_string (k));
        for (int k = 1; k <= 4; ++k) want.push_back ("RLFO " + std::to_string (k));
        check (lfos == want, "the LFO sources of the board read LFO 1 .. 10, then RLFO 1 .. 4");
    }

    // touch targets >= 56 px
    bool chipsOk = true;
    for (int i = 0; i < b.numSources(); ++i)
    {
        const ui::Rect cr = b.chipRect (i);
        chipsOk = chipsOk && cr.h >= 56 && cr.w >= 64 && b.rect().contains (cr);
    }
    check (chipsOk, "every chip is >= 64x56 px and lies inside the board rect");

    // + ADD stays
    check (r.ui.matrix().add.rect().h >= 56 && r.ui.matrix().add.rect().w >= 100, "+ ADD button is present in empty state");

    // scene content in empty state
    auto data = std::make_unique<gpu::SceneData>();
    std::memset (data.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 50, "empty mod page scene fits in buffer (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "CPU renderer draws empty mod page with board");

    // tap on a source chip opens the shared target picker (ModTargetsPage with list)
    const ui::Rect c0 = b.chipRect (0); // ENV 1
    r.tap (c0.x + c0.w / 2, c0.y + c0.h / 2);
    check (r.ui.topPage() == ModUi::kPageList && r.stack.top() == ModUi::kPageList, "tapping source chip opens target picker list");
    check (r.ui.targetsSource() == 1, "target picker is for ENV 1 (source 1)");

    // pick a target: group 0 -> parameter 0
    const ui::Rect grp0 = r.ui.list().rowRect (0);
    r.tap (grp0.x + grp0.w / 2, grp0.y + grp0.h / 2);
    check (r.ui.list().state.header != nullptr, "destination group opened");
    const ui::Rect dst0 = r.ui.list().rowRect (0);
    r.tap (dst0.x + dst0.w / 2, dst0.y + dst0.h / 2);

    // check row is made in engine
    check (r.inst.get ("mod01.source") == 1.0f && r.inst.get ("mod01.destination") > 0.0f, "tap on target made modulation row in engine");
    check (r.ui.topPage() == ModUi::kPageTargets, "targets page is now shown with the created row");
    check (r.ui.targetsPage().numRows() == 1, "targets page shows 1 row");

    // BACK returns to matrix and board is gone
    r.tapC (r.ui.targetsPage().back);
    check (r.ui.topPage() == 0 && r.stack.depth() == 1, "BACK returns to mod matrix");
    r.refresh();
    check (! r.ui.matrix().boardShown(), "board is gone after row is made");
    check (r.ui.matrix().visibleSlotCount() == 1, "matrix list is shown with the created row");

    // deleting the slot returns the board
    r.inst.set ("mod01.source", 0.0f);
    r.inst.set ("mod01.destination", 0.0f);
    r.refresh();
    check (r.ui.matrix().boardShown(), "board returns when all slots are empty");
}

void testMatrixViewLayoutAndTargets()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.refresh();
    check (r.ui.matrix().rect().w == 1280 && r.ui.matrix().rect().h == 628, "ModMatrixView geometry is 1280 x 628");
    check (r.stack.element() == ModUi::kElementPage, "the page is the element of the Mod tab");

    ui::ModMatrixView& m = r.ui.matrix();
    bool targetsOk = m.tabAll.rect().h >= 56 && m.tabSource.rect().h >= 56 && m.tabDest.rect().h >= 56 && m.add.rect().h >= 56 && m.add.rect().w >= 100;
    const auto& row0 = m.windowRows[0];
    targetsOk = targetsOk && row0.sourceBtn.rect().h >= 56 && row0.destBtn.rect().h >= 56 && row0.amountBar.rect().h >= 56 && row0.biToggle.rect().h >= 56 && row0.bypassToggle.rect().h >= 56 && row0.detailBtn.rect().h >= 56;
    targetsOk = targetsOk && row0.sourceBtn.rect().w >= 100 && row0.biToggle.rect().w >= 72 && row0.bypassToggle.rect().w >= 72 && row0.detailBtn.rect().w >= 56;
    check (targetsOk, "all touch targets of the matrix are >= 56 px high (a row has a lane of 16 px above them for the Q index chips)");
    check (row0.amountBar.rect().w >= 380, "the AMOUNT bar stretches over the row (" + std::to_string (row0.amountBar.rect().w) + " px)");
    check (row0.detailBtn.rect().right() >= m.rect().right() - 60 - 8, "the row uses the whole width: the last button ends at " + std::to_string (row0.detailBtn.rect().right()));
    check (std::string (row0.detailBtn.state.label) == "MORE", "the details button says MORE, not three dots");
    check (row0.number.rect().x >= m.rect().x, "the row number is inside the page (no clipped Q chip at the left edge)");
    const ui::Rect q = m.qgroup.rect();
    check (q.x >= m.rect().x + 2 && m.qgroup.active() && q.contains (row0.amountBar.rect()) && q.contains (row0.number.rect()), "the Q frame of the selected row lies inside the page and holds the AMOUNT and the row number");
    ui::Component* leaves[96];
    const int n = m.collectLeaves (leaves, 0, 96);
    check (m.qgroup.check (leaves, n) == 0, "no other control lies in the frame of the Q group of the row");
}

void testMatrixRowParameterWrites()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 20.0f);
    r.inst.set ("mod01.bipolar", 0.0f);
    r.inst.set ("mod01.bypass", 0.0f);
    r.refresh();

    check (r.ui.matrix().slotData (0).active, "slot 0 is active after setting source and destination");
    check (std::fabs (r.ui.matrix().slotData (0).amount - 0.2) < 0.01, "amount reflects +20%");
    check (std::string (r.ui.matrix().windowRows[0].amountBar.state.text) == "+20%", "the value of the amount is written at the right end of the bar (" + std::string (r.ui.matrix().windowRows[0].amountBar.state.text) + ")");

    const ui::Rect ab = r.ui.matrix().windowRows[0].amountBar.rect();
    r.drag (ab.x + ab.w / 2, ab.y + ab.h / 2, ab.x + ab.w / 2 + 60, ab.y + ab.h / 2);
    check (r.inst.get ("mod01.amount") > 35.0f, "dragging amount bar writes mod01.amount (" + std::to_string (r.inst.get ("mod01.amount")) + ")");

    r.tapC (r.ui.matrix().windowRows[0].biToggle);
    check (r.inst.get ("mod01.bipolar") == 1.0f, "tapping UNI/BI toggle sets mod01.bipolar = 1 (BI)");
    check (std::string (r.ui.matrix().windowRows[0].biToggle.state.label) == "BI", "the polarity button says BI");
    r.tapC (r.ui.matrix().windowRows[0].biToggle);
    check (r.inst.get ("mod01.bipolar") == 0.0f, "tapping UNI/BI toggle again sets mod01.bipolar = 0 (UNI)");

    r.tapC (r.ui.matrix().windowRows[0].bypassToggle);
    check (r.inst.get ("mod01.bypass") == 1.0f && std::string (r.ui.matrix().windowRows[0].bypassToggle.state.label) == "OFF" && ! r.ui.matrix().windowRows[0].bypassToggle.state.on, "tapping ON/OFF switches the slot off (OFF, dim)");
    r.tapC (r.ui.matrix().windowRows[0].bypassToggle);
    check (r.inst.get ("mod01.bypass") == 0.0f && r.ui.matrix().windowRows[0].bypassToggle.state.on, "tapping ON/OFF again switches it on");
}

void testMatrixQLinkGroup()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod02.source", 2.0f);
    r.inst.set ("mod02.destination", 2.0f);
    r.inst.set ("mod03.source", 3.0f);
    r.inst.set ("mod03.destination", 3.0f);
    r.refresh();
    r.ui.matrix().selectRow (0);
    r.refresh();

    const synthyboy::ModSlotParams sp0 = synthyboy::modSlotParams (0);
    check (QRouter::modeOf (r.inst.q[0]) == QRouter::kVirtual && QRouter::indexOf (r.inst.q[0]) == ModUi::kVirtualRow, "Q1 is the virtual target that steps through the rows");
    check (r.inst.q[1] == sp0.source, "Q2 targets mod01.source");
    check (r.inst.q[2] == sp0.destination, "Q3 targets mod01.destination");
    check (r.inst.q[3] == sp0.amount, "Q4 targets mod01.amount");

    const float prevAmt = r.inst.get ("mod01.amount");
    r.ui.turnQ (3, 5.0, r.stack, r.inst);
    r.refresh();
    check (r.inst.get ("mod01.amount") > prevAmt, "turning Q4 increases mod01.amount");

    // turning Q1 steps the selected row
    r.ui.turnQ (0, 1.0, r.stack, r.inst);
    r.refresh();
    check (r.ui.matrix().selectedRow() == 1, "turning Q1 steps the selected row");
    r.ui.matrix().selectRow (0);
    r.refresh();

    // the host writes the virtual target: the selected row moves
    r.inst.qVirtualIn[ModUi::kVirtualRow] = 1.0f;
    r.inst.qVirtualDue[ModUi::kVirtualRow] = true;
    r.refresh();
    check (r.ui.matrix().selectedRow() == 2 && std::string (r.ui.qGroupName()) == "SLOT 03", "the first Q-Link steps the row (" + std::string (r.ui.qGroupName()) + ")");
    check (r.inst.q[3] == synthyboy::modSlotParams (2).amount, "the Q group follows the selected row");
    check (std::fabs (r.inst.qVirtual[ModUi::kVirtualRow] - 1.0f) < 1.0e-6f, "the page tells the router the row position");

    // a tap on a row selects it
    const ui::Rect nr = r.ui.matrix().windowRows[1].number.rect();
    r.tap (nr.x + nr.w / 2, nr.y + nr.h / 2);
    check (r.ui.matrix().selectedRow() == 1, "a tap on the number of a row selects it");
}

void testListsAreGrouped()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.refresh();
    r.tapC (r.ui.matrix().windowRows[0].sourceBtn);
    check (r.stack.top() == ModUi::kPageList && r.ui.list().state.count == 1 + uibind::kSourceGroups, "the SOURCE list has NONE and the " + std::to_string (uibind::kSourceGroups) + " groups of the sources");
    const ui::Rect lfos = r.ui.list().rowRect (2);   // LFOS
    r.tap (lfos.x + lfos.w / 2, lfos.y + lfos.h / 2);
    check (r.ui.list().state.header != nullptr && std::string (r.ui.list().state.header) == "LFOS" && r.ui.list().state.headerBack, "a group opens its entries with a way back in the header");
    check (r.ui.list().state.count == 14 && std::string (r.ui.list().state.items[0]) == "LFO 1", "the LFOS group lists the 10 LFOs and the 4 random LFOs");
    const ui::Rect row2 = r.ui.list().rowRect (2);
    r.tap (row2.x + row2.w / 2, row2.y + row2.h / 2);
    check (r.inst.get ("mod01.source") == 9.0f && r.stack.depth() == 1, "a tap on LFO 3 writes the source (choice 9) and closes the list");

    // the TARGET list is the grouped list of the envelope and the LFO
    r.tapC (r.ui.matrix().windowRows[0].destBtn);
    const uibind::DestIndex& d = uibind::DestIndex::get();
    check (r.ui.list().state.count == 1 + static_cast<int> (d.groups.size()), "the TARGET list has NONE and the groups of ui_bind.h DestIndex (the list of ADD TARGET), " + std::to_string (d.groups.size()) + " of them");
    const ui::Rect g1 = r.ui.list().rowRect (1);
    r.tap (g1.x + g1.w / 2, g1.y + g1.h / 2);
    check (r.ui.list().state.count == static_cast<int> (d.choices[0].size()) && std::string (r.ui.list().state.header) == d.groups[0], "the first group opens its parameters");
    const ui::Rect e1 = r.ui.list().rowRect (1);
    r.tap (e1.x + e1.w / 2, e1.y + e1.h / 2);
    check (r.inst.get ("mod01.destination") == static_cast<float> (d.choices[0][1]), "the second parameter of the group is written as the target");
    // the header is the way back, a tap beside the list closes it
    r.tapC (r.ui.matrix().windowRows[0].destBtn);
    r.tap (20, 130);
    check (r.stack.depth() == 1, "a tap beside the list closes it");
}

void testSlotDetailOpenCloseAndDelete()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 50.0f);
    r.refresh();

    r.tapC (r.ui.matrix().windowRows[0].detailBtn);
    check (r.stack.depth() == 2 && r.stack.top() == ModUi::kPageDetail, "tapping MORE opens the page of the slot on the UiStack");
    check (r.ui.detail().rect().w == 1280 && r.ui.detail().rect().h == 628, "the page of a slot has the whole tab");

    ui::ModSlotDetail& d = r.ui.detail();
    // every knob of the three blocks shows its value, and the blocks are filled
    bool values = true;
    for (const ui::Knob* k : { &d.auxOutput, &d.auxPower, &d.power, &d.rise, &d.fall, &d.delay })
        values = values && k->state.valueText[0] != '\0' && k->rect().w >= 100 && k->rect().h >= 100;
    check (values, "every knob of the slot page shows its value and is a big knob");
    check (std::string (d.auxOutput.state.valueText) == "100.0%", "the scale of the auxiliary source is written in percent (" + std::string (d.auxOutput.state.valueText) + ")");
    bool inside = true;
    for (int b = 0; b < 3; ++b)
        inside = inside && d.block (b).contains (d.groups[b].rect()) && d.block (b).h >= 400;
    check (inside, "each block holds its whole Q group and fills the height of the page");
    check (d.summary.sourceBtn.rect().w > 0 && d.summary.amountBar.rect().w >= 380, "the page has the slot's own row (source, target, amount, polarity, on / off)");

    const int ax = d.auxOutput.rect().x + d.auxOutput.rect().w / 2, ay = d.auxOutput.rect().y + d.auxOutput.rect().h / 2;
    r.drag (ax, ay, ax, ay - 40);
    check (r.inst.get ("mod01.aux_output") > 110.0f, "dragging auxOutput writes mod01.aux_output");
    r.tapC (r.ui.detail().auxInv);
    check (r.inst.get ("mod01.aux_inv") == 1.0f && r.ui.detail().auxInv.state.on, "tapping auxInv toggles mod01.aux_inv");
    r.tapC (r.ui.detail().delaySync);
    check (r.inst.get ("mod01.delay_sync") == 1.0f && r.ui.detail().delaySyncOn(), "tapping delaySync toggles mod01.delay_sync, the BEATS knob takes the place of DELAY");
    check (r.ui.detail().delayBeats.rect().w == r.ui.detail().delay.rect().w && r.ui.detail().groups[ui::ModSlotDetail::BlockTime].member (2) == &r.ui.detail().delayBeats, "the TIME group follows the delay (beats)");
    check (std::string (r.ui.detail().delayBeats.state.valueText).find ("beat") != std::string::npos, "the beats are written with their unit (" + std::string (r.ui.detail().delayBeats.state.valueText) + ")");

    // a tap on a member makes its block the Q group
    r.tapC (r.ui.detail().stereo);
    check (r.ui.detail().activeBlock() == ui::ModSlotDetail::BlockShape && r.inst.q[0] == synthyboy::modSlotParams (0).power && r.inst.q[1] == synthyboy::modSlotParams (0).stereo, "a tap on STEREO makes the SHAPE block the Q group");

    // the row of the page edits the slot too
    r.tapC (r.ui.detail().summary.biToggle);
    check (r.inst.get ("mod01.bipolar") == 1.0f, "the slot's own row on the page writes the polarity");

    check (! r.ui.detail().isDeleteConfirmed(), "delete confirmation initially false");
    r.tapC (r.ui.detail().deleteBtn);
    check (r.ui.detail().isDeleteConfirmed(), "first tap on delete sets confirmation state to true");
    r.tapC (r.ui.detail().deleteBtn);
    check (r.inst.get ("mod01.source") == 0.0f && r.inst.get ("mod01.destination") == 0.0f, "confirmed delete clears mod01 slot parameters");
    check (r.stack.depth() == 1, "confirmed delete returns back to matrix view");

    // + ADD makes a new slot and opens its page; BACK returns
    r.tapC (r.ui.matrix().add);
    check (r.stack.depth() == 2 && r.stack.top() == ModUi::kPageDetail && r.inst.get ("mod01.source") == 1.0f, "+ ADD fills the first free slot and opens its page");
    r.tapC (r.ui.detail().back);
    check (r.stack.depth() == 1, "BACK returns to the matrix");
}

void testLiveDeflection()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 50.0f);
    r.inst.hasLive = true;
    r.inst.liveVals.source[0] = 0.8f;
    r.refresh();

    const auto& e = r.ui.matrix().slotData (0);
    check (e.hasLive && std::fabs (e.liveVal - 0.8) < 0.01, "slot data has the live value 0.8 of its source");
    const auto& bar = r.ui.matrix().windowRows[0].amountBar;
    check (std::fabs (bar.state.modValue - 0.7) < 0.01, "the live marker of the bar is amount * live source = 0.4 (position " + std::to_string (bar.state.modValue) + ")");
}

void testFilterAndScroll()
{
    ModRig r;
    for (int s = 0; s < 12; ++s)
    {
        char key[24];
        std::snprintf (key, sizeof key, "mod%02d.source", s + 1);
        r.inst.set (key, static_cast<float> (s < 6 ? 1 + s : 7 + s));
        std::snprintf (key, sizeof key, "mod%02d.destination", s + 1);
        r.inst.set (key, static_cast<float> (1 + s));
    }
    r.refresh();
    ui::ModMatrixView& m = r.ui.matrix();
    check (m.visibleSlotCount() == 12 && m.activeSlots() == 12, "12 slots are listed");
    check (m.scrolls() && m.visibleRows() == 7, "the matrix shows 7 rows and scrolls the rest (" + std::to_string (m.visibleRows()) + ")");
    r.drag (600, m.listRect().y + 300, 600, m.listRect().y + 40);
    check (m.firstRow() > 0, "a vertical drag scrolls the list");
    // the mouse wheel over the list scrolls it a row a notch (up: the earlier rows), through the page's own wheel entry
    {
        const int f0 = m.firstRow();
        const ui::Rect lr = m.listRect();
        check (r.ui.wheel (lr.x + lr.w / 2, lr.y + 4, -1, r.stack, r.inst, r.now) && m.firstRow() == f0 + 1, "the wheel down over the list scrolls one row (" + std::to_string (f0) + " -> " + std::to_string (m.firstRow()) + ")");
        check (r.ui.wheel (lr.x + lr.w / 2, lr.y + 4, 1, r.stack, r.inst, r.now) && m.firstRow() == f0, "the wheel up scrolls back");
        r.ui.wheel (lr.x + lr.w / 2, lr.y + 4, 50, r.stack, r.inst, r.now);
        check (m.firstRow() == 0, "the wheel up past the top stops at the first row");
        check (! r.ui.wheel (5, 5, 1, r.stack, r.inst, r.now), "the wheel above the page (the host's top bar): not taken");
    }
    r.tapC (m.tabSource);
    check (m.filterMode() == ui::ModMatrixView::FilterSource, "BY SOURCE");
    r.tapC (m.sourceFilter);   // the right half of the stepper: ENVELOPES
    check (m.visibleSlotCount() == 6, "the filter ENVELOPES lists the 6 slots of envelopes (" + std::to_string (m.visibleSlotCount()) + ")");
    r.tapC (m.tabAll);
    check (m.visibleSlotCount() == 12, "ALL lists them all again");
}

void testMacrosView()
{
    MacroRig r;
    check (r.ui.view().rect().w == 1280 && r.ui.view().rect().h == 628, "MacroView geometry is 1280 x 628");
    check (r.stack.element() == MacroUi::kElementPage, "the page is the element of the Macros tab");

    r.inst.set ("macro.1", 35.0f);
    r.inst.set ("macro.2", 70.0f);
    r.refresh();
    check (std::fabs (r.ui.view().cards[0].knob.state.value - 0.35) < 0.01, "Macro 1 card value is 35%");
    check (std::string (r.ui.view().cards[1].knob.state.valueText) == "70.0%", "Macro 2 shows its value (" + std::string (r.ui.view().cards[1].knob.state.valueText) + ")");

    // every card is filled: a side knob with its name and value, the chips row under it
    bool cards = true;
    for (int i = 0; i < 8; ++i)
    {
        const ui::MacroCard& c = r.ui.view().cards[i];
        cards = cards && c.knob.rect().w >= 172 && c.knob.rect().h >= 72 && c.chipsRect().h >= 56 && c.rect().contains (c.knob.rect()) && c.rect().contains (c.chipsRect()) && c.sizeOk() && c.knob.state.valueText[0] != '\0';
    }
    check (cards, "all eight cards hold a side knob (>= 172 x 72), a value and a chips row of 56 px");
    check (r.ui.view().spectrum.rect().y > r.ui.view().cards[7].rect().bottom() && r.ui.view().spectrum.rect().h >= 100, "the spectrum strip lies under the cards");

    const int x0 = r.ui.view().cards[0].knob.rect().x + r.ui.view().cards[0].knob.rect().w / 2;
    const int y0 = r.ui.view().cards[0].knob.rect().y + r.ui.view().cards[0].knob.rect().h / 2;
    r.drag (x0, y0, x0, y0 - 40);
    check (r.inst.get ("macro.1") > 45.0f, "dragging Macro 1 knob writes macro.1 (" + std::to_string (r.inst.get ("macro.1")) + ")");

    check (r.ui.bank() == 0, "default bank is Bank 1 (1-4)");
    check (r.inst.q[0] == FakeInst::idx ("macro.1") && r.inst.q[3] == FakeInst::idx ("macro.4"), "Bank 1 Q targets are macro.1 .. macro.4");
    r.tap (r.ui.view().bankList.rowRect (1).x + 30, r.ui.view().bankList.rowRect (1).y + 28);
    check (r.ui.bank() == 1, "active bank is now Bank 2 (5-8)");
    check (r.inst.q[0] == FakeInst::idx ("macro.5") && r.inst.q[3] == FakeInst::idx ("macro.8"), "Bank 2 Q targets are macro.5 .. macro.8");
    r.inst.set ("macro.5", 20.0f);
    r.refresh();
    r.ui.turnQ (0, 5.0, r.stack, r.inst);
    r.refresh();
    check (r.inst.get ("macro.5") > 20.0f, "turning Q1 in Bank 2 increases macro.5");
    // a tap on the knob of the first row makes it the group again
    r.tap (static_cast<int> (r.ui.view().cards[2].knob.geom().cx), static_cast<int> (r.ui.view().cards[2].knob.geom().cy));   // the dial (the name beside it is a button of its own now: a tap there opens the keyboard page)
    check (r.ui.bank() == 0, "a tap on a card of the top row makes bank 1 the Q group");
    // a bank left from an earlier visit does not survive the arrival: the page shows BANK 1 and the knobs write macro 1 .. 4 (the group and the picture cannot disagree)
    r.tap (r.ui.view().bankList.rowRect (1).x + 30, r.ui.view().bankList.rowRect (1).y + 28);
    r.refresh();
    check (r.ui.bank() == 1 && r.inst.q[0] == FakeInst::idx ("macro.5"), "the bank is set to 2 before the page is left");
    r.ui.forgetQGroup();   // the page is shown again (PageView::onShown)
    r.refresh();
    check (r.ui.bank() == 0 && r.inst.q[0] == FakeInst::idx ("macro.1") && r.inst.q[3] == FakeInst::idx ("macro.4"), "on arrival the page is on BANK 1 and the group is macro.1 .. macro.4");
    // the Q frame of a bank holds its four knobs and nothing else
    ui::Component* leaves[96];
    const int n = r.ui.view().collectLeaves (leaves, 0, 96);
    ui::MacroView& v = r.ui.view();
    check (v.qgroup.rect().contains (v.cards[0].knob.rect()) && v.qgroup.rect().contains (v.cards[3].knob.rect()), "the Q frame holds the four knobs of the bank (" + std::to_string (n) + " leaves)");
    // the other bank is the next group: its quiet chips (5 .. 8 on a device with sixteen knobs) stand on its four cards
    for (const bool wide : { true, false })
    {
        ui::QGroup::setWideDevice (wide);
        auto data = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        v.draw (sb);
        std::vector<uint32_t> cv;
        renderScene (*data, cv);
        auto drawn = [&] (int card)   // a chip over the plain gradient of a card: some row of the chip's rectangle is not one colour
        {
            const ui::Rect c = ui::QGroup::chipRect (v.cards[card].knob.rect(), wide ? 5 + (card - 4) : 1 + (card - 4));
            for (int y = c.y; y < c.bottom(); ++y)
                for (int x = c.x; x < c.right(); ++x)
                    if (cv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] != cv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (c.x)])
                        return true;
            return false;
        };
        check (drawn (4) && drawn (5) && drawn (6) && drawn (7), std::string (wide ? "16 knob device: " : "4 knob device: ") + "the cards of the other bank carry their quiet chips (" + (wide ? "5 .. 8" : "1 .. 4 of the next bank") + ")");
    }
    ui::QGroup::setWideDevice (false);
}

void setSlot (FakeInst& inst, int slot, int source, int dest, float amount, bool bypass)
{
    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
    inst.writeParam (sp.source, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp.source], static_cast<float> (source))));
    inst.writeParam (sp.destination, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp.destination], static_cast<float> (dest))));
    inst.writeParam (sp.amount, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp.amount], amount)));
    inst.writeParam (sp.bypass, bypass ? 1.0f : 0.0f);
}

void testMacroTargets()
{
    MacroRig r;
    const int m1 = uibind::macroSource (1);
    check (m1 == 19 && uibind::macroSource (5) == 29 && uibind::macroSource (8) == 32, "the sources of the macros are found in the table by their keys (19, 29 .. 32)");
    setSlot (r.inst, 0, m1, 1, 40.0f, false);
    setSlot (r.inst, 1, m1, 2, -30.0f, false);
    setSlot (r.inst, 2, m1, 3, 10.0f, true);   // switched off: no chip
    r.refresh();

    ui::MacroView& v = r.ui.view();
    check (v.cards[0].numChips == 2 && v.cards[0].extraDestCount == 0, "Macro 1 shows the 2 targets of its active slots as chips");
    check (v.cards[1].numChips == 0, "Macro 2 has no chip");
    {   // a long target name is kept whole (the card cuts it with an ellipsis where it does not fit); the bank selector is a LED list of two rows of 56 px or more
        setSlot (r.inst, 3, m1, synthyboy::modDestinationChoiceOf (static_cast<size_t> (FakeInst::idx ("osc1.unison.spread"))), 50.0f, false);
        r.refresh();
        bool whole = false;
        for (int i = 0; i < v.cards[0].numChips; ++i)
            whole = whole || std::strlen (v.cards[0].chips[i].text) > 19;
        check (whole, "a target name longer than 19 characters is not cut by the chip buffer");
        check (v.bankList.state.count == 2 && v.bankList.rowRect (0).h >= 56 && v.bankList.rowRect (1).h >= 56 && v.bankList.sizeOk(), "BANK 1 / BANK 2 are a LED list with rows of 56 px or more");
        setSlot (r.inst, 3, 0, 0, 0.0f, false);
        r.refresh();
    }

    // the chips row asks for the targets page of the macro: the same page as the envelope's and the LFO's
    r.tap (v.cards[0].rect().x + 20, v.cards[0].chipsRect().y + 10);
    check (r.stack.top() == MacroUi::kPageTargets && r.ui.targetsOf() == 0, "a tap on the chips opens the targets page of macro 1");
    check (r.ui.targetsPage().numRows() == 3, "the page lists the 3 slots of the macro (the switched off one too)");
    ui::ModTargetsPage& t = r.ui.targetsPage();
    r.drag (t.rows[0].depth.rect().x + 60, t.rows[0].depth.rect().y + 28, t.rows[0].depth.rect().x + 160, t.rows[0].depth.rect().y + 28);
    check (r.inst.get ("mod01.amount") > 50.0f, "dragging the depth of a row writes the amount (" + std::to_string (r.inst.get ("mod01.amount")) + ")");
    check (QRouter::indexOf (r.inst.q[0]) == synthyboy::modSlotParams (0).amount, "the Q group of the page is the depth, the polarity, the switch of the active row");

    // ADD TARGET: the grouped list
    r.tapC (t.add);
    check (r.stack.top() == MacroUi::kPageList && r.ui.list().state.count == static_cast<int> (uibind::DestIndex::get().groups.size()), "ADD TARGET opens the grouped list of the destinations");
    const ui::Rect g0 = r.ui.list().rowRect (0);
    r.tap (g0.x + g0.w / 2, g0.y + g0.h / 2);
    const ui::Rect p0 = r.ui.list().rowRect (3);
    r.tap (p0.x + p0.w / 2, p0.y + p0.h / 2);
    int uses = 0;
    for (int s = 0; s < 8; ++s)
        uses += static_cast<int> (std::lround (r.inst.param (synthyboy::modSlotParams (s).source))) == m1;
    check (uses == 4 && r.stack.top() == MacroUi::kPageTargets, "a parameter of a group makes a new slot for the macro (4 slots now) and returns to the page");

    // removing a row
    r.tapC (r.ui.targetsPage().rows[0].remove);
    check (r.inst.get ("mod01.source") == 0.0f, "the X of a row clears its slot");
    r.tapC (r.ui.targetsPage().back);
    check (r.stack.depth() == 1, "BACK returns to the cards");
}

void testSpectrum()
{
    MacroRig r;
    check (r.ui.view().spectrum.levels == nullptr, "no ring: nothing is drawn but the frame");
    static synthyboy_mpc::SpectrumRing ring;
    r.inst.ring = &ring;
    std::vector<float> buf (512);
    double ph = 0.0;
    for (int blk = 0; blk < 40; ++blk)
    {
        for (float& f : buf) { f = 0.5f * static_cast<float> (std::sin (ph)); ph += 2.0 * 3.14159265 * 1000.0 / 48000.0; }
        ring.push (buf.data(), nullptr, static_cast<int> (buf.size()));
        r.now += 40.0;
        r.refresh();
    }
    check (r.ui.view().spectrum.levels != nullptr && r.ui.spectrum() != nullptr, "with a ring the strip has the live curve");
    const float* lv = r.ui.view().spectrum.levels;
    int peak = 0;
    for (int i = 1; i < ui::SpectrumStrip::kPoints; ++i)
        if (lv[i] > lv[peak])
            peak = i;
    check (std::fabs (SpectrumCurve::pointHz (peak) - 1000.0) < 120.0 && lv[peak] > 0.3f, "the curve peaks at the 1 kHz of the output (" + std::to_string (SpectrumCurve::pointHz (peak)) + " Hz)");
}

void testDrawingAndCpuRendering()
{
    ModRig mr;
    mr.inst.set ("mod01.source", 1.0f);
    mr.inst.set ("mod01.destination", 1.0f);
    mr.inst.set ("mod01.amount", 40.0f);
    mr.refresh();

    auto data = std::make_unique<gpu::SceneData>();
    std::memset (data.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (sb);
    check (! sb.overflow && data->numOps > 20, "ModUi matrix view scene fits in buffer (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "the CPU renderer draws Mod Matrix view");

    mr.ui.openSlotDetail (0, mr.stack);
    mr.refresh();
    auto detailData = std::make_unique<gpu::SceneData>();
    std::memset (detailData.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder dsb (*detailData);
    dsb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (dsb);
    check (! dsb.overflow && detailData->numOps > 20, "ModUi slot detail scene fits in buffer (" + std::to_string (detailData->numOps) + " ops)");
    std::vector<uint32_t> detailCv;
    renderScene (*detailData, detailCv);
    check (detailCv.size() == 1280u * 800u, "the CPU renderer draws Slot Detail view");

    MacroRig macr;
    auto macData = std::make_unique<gpu::SceneData>();
    std::memset (macData.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder msb (*macData);
    msb.begin (gpu::kKindPlay, 0, 1280, 800);
    macr.ui.draw (msb);
    check (! msb.overflow && macData->numOps > 20, "MacroUi scene fits in buffer (" + std::to_string (macData->numOps) + " ops)");
    std::vector<uint32_t> macCv;
    renderScene (*macData, macCv);
    check (macCv.size() == 1280u * 800u, "the CPU renderer draws Macros view");
}

} // namespace

int main()
{
    std::printf ("=== SynthyBoy MPC Overlay: Mod Matrix & Macros Host Tests ===\n");

    testSlotParamsExistence();
    testModEmptyStateAndBoard();
    testMatrixViewLayoutAndTargets();
    testMatrixRowParameterWrites();
    testMatrixQLinkGroup();
    testListsAreGrouped();
    testSlotDetailOpenCloseAndDelete();
    testLiveDeflection();
    testFilterAndScroll();
    testMacrosView();
    testMacroTargets();
    testSpectrum();
    testDrawingAndCpuRendering();

    std::printf (g_fails ? "mod_ui_host: FAILED (%d failures)\n" : "mod_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
