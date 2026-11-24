// SynthyBoy - MPC overlay tests: Modulation Matrix and Macros UI (mod_ui_host.cpp): the pages of the Mod and Macros tabs.
// Tests parameter wiring for all 64 slots, the page of a slot, the grouped lists, the macros with their chips and their targets page, the live spectrum, touch targets, Q-link routing (the virtual row), and CPU rendering.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../mod_ui.h"
#include "../macro_ui.h"
#include "../gpu/cell_plan.h"
#include "../pages.h"
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
class FakeInst : public IInstanceView
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
        for (int i = 0; i < 16; ++i)
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
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    void setMacroName (int macro, const std::string& name) override   // like the plugin: the name is cleaned and lands in the content
    {
        auto next = contentPtr != nullptr ? std::make_shared<synthyboy::StateContent> (*contentPtr) : std::make_shared<synthyboy::StateContent>();
        if (macro >= 0 && macro < synthyboy::kNumMacros)
            next->macroNames[macro] = synthyboy::sanitizeMacroName (name, macro);
        contentPtr = std::move (next);
    }
    std::shared_ptr<const synthyboy::StateContent> contentPtr;

    uint64_t stateGeneration() const noexcept override { return stateGen; }
    void setUiFullOpen (bool open) noexcept override { uiFullOpen = open; }
    void setNativeModMatrix (int view, const nativeui::FxCell* fields, const int* rows) noexcept override
    {
        nativeMod.publish (view, fields);
        for (int row = 0; row < nativeui::mod::kRows; ++row)
            nativeRows[row] = rows ? rows[row] : 0;
    }
    bool takeNativeModField (int field, const nativeui::FxCell& expected, float& value) noexcept override
    {
        return nativeMod.take (field, expected, value);
    }

    nativeui::targets::Bank modTargets;
    bool targetsSupported = false;
    bool nativeModTargetsSupported() const noexcept override { return targetsSupported; }
    void publishNativeModTargets (const nativeui::targets::State& state) noexcept override
    {
        modTargets.publish (state);
        for (int slot = 0; slot < nativeui::targets::kFields; ++slot) modTargets.report (modTargets.at (slot));
    }
    void hideNativeModTargets (const void* presenter) noexcept override { modTargets.hide (presenter); }
    bool takeNativeModTarget (const nativeui::targets::State& state, int slot, float& value) noexcept override
    {
        return modTargets.take (state, slot, value);
    }

    void setNativeModNavigation (int mode, int source, int destination, const nativeui::FxCell* fields) noexcept override
    {
        nativeModNavigation.publish (mode, fields);
        nativeSource = source;
        nativeDestination = destination;
    }
    bool takeNativeModNavigation (int field, const nativeui::FxCell& expected, float& value) noexcept override
    {
        return nativeModNavigation.take (field, expected, value);
    }
    void postModNavigation (int field, float value)
    {
        const auto snapshot = nativeModNavigation.at (field);
        nativeModNavigation.report (snapshot);
        nativeModNavigation.post (snapshot, value);
    }

    void setNativeModDetailNavigation (const nativeui::FxCell* fields) noexcept override
    {
        nativeModDetailNavigation.publish (fields != nullptr ? 1 : 0, fields);
    }
    bool takeNativeModDetailNavigation (int field, const nativeui::FxCell& expected, float& value) noexcept override
    {
        return nativeModDetailNavigation.take (field, expected, value);
    }
    void postModDetailNavigation (int field, float value)
    {
        const auto snapshot = nativeModDetailNavigation.at (field);
        nativeModDetailNavigation.report (snapshot);
        nativeModDetailNavigation.post (snapshot, value);
    }

    nativeui::NativeCellBank<nativeui::modboard::kHandles> nativeModBoard;
    void setNativeModBoard (int gate, const nativeui::FxCell* fields) noexcept override
    {
        nativeModBoard.publish (gate, fields);
    }
    bool takeNativeModBoard (int field, const nativeui::FxCell& expected, float& value) noexcept override
    {
        return nativeModBoard.take (field, expected, value);
    }
    void postModBoard (int field, float value)
    {
        const auto snapshot = nativeModBoard.at (field);
        nativeModBoard.report (snapshot);
        nativeModBoard.post (snapshot, value);
    }

    static constexpr int kVirtualSlots = 10;   // QRouter::kNumVirtual
    float qVirtual[kVirtualSlots] = {}, qVirtualIn[kVirtualSlots] = {};
    bool qVirtualDue[kVirtualSlots] = {};
    const synthyboy_mpc::SpectrumRing* ring = nullptr;
    int q[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    int qCalls = 0;
    int writes = 0;
    int lastWritten = -1;
    float lastWrittenNorm = 0.0f;
    uint64_t stateGen = 0;
    bool uiFullOpen = false;
    nativeui::NativeCellBank<nativeui::mod::kFields> nativeMod;
    int nativeRows[nativeui::mod::kRows] {};
    nativeui::NativeCellBank<nativeui::mod::kNavigation> nativeModNavigation;
    nativeui::NativeCellBank<nativeui::moddetail::kHandles> nativeModDetailNavigation;
    int nativeSource = 0, nativeDestination = 0;

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
        ui.setNativeControls (false);
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

    bool tapNative (const ui::Component& c, int field, float value = 1.0f)
    {
        const auto b = c.rect();
        const int x = b.x + b.w / 2, y = b.y + b.h / 2;
        const bool claimed = ui.touch (ev (TouchEvent::Down, x, y), stack, inst, now);
        if (! claimed)
            inst.postModNavigation (field, value);
        ui.touch (ev (TouchEvent::Up, x, y), stack, inst, now + 40.0);
        refresh();
        return ! claimed;
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
    struct NativeInstance : FakeInst
    {
        int pendingMacroTarget = -1;
        bool macroTargetsPublished = false;
        bool nativeMacroTargetsSupported() const noexcept override { return true; }
        void publishNativeMacroTargets (bool on) noexcept override { macroTargetsPublished = on; }
        int takeNativeMacroTarget() noexcept override
        {
            const int target = macroTargetsPublished ? pendingMacroTarget : -1;
            pendingMacroTarget = -1;
            return target;
        }
    };
    NativeInstance inst;
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

    // every card is filled: a 90 px vertical knob with its value, the chips row under it
    bool cards = true;
    for (int i = 0; i < 8; ++i)
    {
        const ui::MacroCard& c = r.ui.view().cards[i];
        cards = cards && c.knob.rect().w >= 152 && c.knob.rect().h >= 118 && c.nativeTargetArea().h >= 56 && c.rect().contains (c.knob.rect()) && c.rect().contains (c.nativeTargetArea()) && c.sizeOk() && c.knob.state.valueText[0] != '\0';
    }
    check (cards, "all eight cards hold a vertical knob (>= 152 x 118), a value and a chips row of 56 px");
    const ui::Rect specR = r.ui.view().spectrum.rect();
    bool specClear = specR.x == 8 && specR.y == 182 && specR.w == 1264 && specR.h == 290;
    for (int i = 0; i < 8; ++i)
        specClear = specClear && ! specR.overlaps (r.ui.view().cards[i].rect()) && r.ui.view().cards[i].rect().y > specR.bottom();
    check (specClear,
           "8-in-one-row layout: spectrum occupies full-width upper 1264x290 above all 8 bottom-row cards");

    const int x0 = r.ui.view().cards[0].knob.rect().x + r.ui.view().cards[0].knob.rect().w / 2;
    const int y0 = r.ui.view().cards[0].knob.rect().y + r.ui.view().cards[0].knob.rect().h / 2;
    r.drag (x0, y0, x0, y0 - 40);
    check (r.inst.get ("macro.1") == 35.0f, "native Macro 1 drag makes no overlay write");

    check (r.ui.bank() == 0, "default bank focus is Bank 1 (1-4)");
    check (r.inst.q[0] == FakeInst::idx ("macro.1") && r.inst.q[3] == FakeInst::idx ("macro.4")
           && r.inst.q[4] == FakeInst::idx ("macro.5") && r.inst.q[7] == FakeInst::idx ("macro.8"),
           "fixed Q targets: Q 1..4 = macro.1..4, Q 5..8 = macro.5..8");
    r.tap (static_cast<int> (r.ui.view().cards[4].knob.geom().cx), static_cast<int> (r.ui.view().cards[4].knob.geom().cy));
    check (r.ui.bank() == 1, "tapping card 5 focuses Bank 2 (5-8)");
    check (r.inst.q[0] == FakeInst::idx ("macro.1") && r.inst.q[4] == FakeInst::idx ("macro.5"),
           "Bank 2 touch focus does not swap Q 1..4 and Q 5..8 target mapping");
    r.inst.set ("macro.5", 20.0f);
    r.refresh();
    r.ui.turnQ (0, 5.0, r.stack, r.inst);
    r.refresh();
    check (r.inst.get ("macro.5") > 20.0f, "turning Q1 in Bank 2 focus increases macro.5");
    // a tap on a card in Bank 1 returns focus to Bank 1
    r.tap (static_cast<int> (r.ui.view().cards[2].knob.geom().cx), static_cast<int> (r.ui.view().cards[2].knob.geom().cy));
    check (r.ui.bank() == 0, "a tap on a card of Bank 1 makes Bank 1 the focused Q group");
    // returning to the page restores Bank 1 focus and keeps fixed Q mapping
    r.tap (static_cast<int> (r.ui.view().cards[5].knob.geom().cx), static_cast<int> (r.ui.view().cards[5].knob.geom().cy));
    r.refresh();
    check (r.ui.bank() == 1 && r.inst.q[0] == FakeInst::idx ("macro.1") && r.inst.q[4] == FakeInst::idx ("macro.5"), "Bank 2 focused with fixed Q targets before leaving page");
    r.ui.forgetQGroup();   // the page is shown again (PageView::onShown)
    r.refresh();
    check (r.ui.bank() == 0 && r.inst.q[0] == FakeInst::idx ("macro.1") && r.inst.q[4] == FakeInst::idx ("macro.5"), "on arrival the page is on BANK 1 focus and fixed macro.1..8 mapping");
    // the Q frame of a bank holds its four knobs and nothing else
    ui::MacroView& v = r.ui.view();
    for (int bank = 0; bank < 2; ++bank)
    {
        v.setBank (bank);
        const int o = bank * 4;
        check (v.qgroup.rect().contains (v.cards[o].knob.rect()) && v.qgroup.rect().contains (v.cards[o + 3].knob.rect()),
               "the Q frame holds the four knobs of bank " + std::to_string (bank + 1));
        for (const bool wide : { true, false })
        {
            ui::QGroup::setWideDevice (wide);
            auto data = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*data);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            v.draw (sb);
            int markers = 0;
            for (uint32_t i = 0; i < data->numOps; ++i)
                if (data->ops[i].op == gpu::kOpText)
                    markers += gpu::textString (*data, data->ops[i]) == "Q";
            check (markers == (ui::QVisualPolicy::kHasQFrame && ! wide ? 1 : 0), "bank " + std::to_string (bank + 1) + ": only Gen 1 with four knobs draws one unnumbered Q marker");
        }
    }
    v.setBank (0);
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
    check (v.cards[0].nativeTargetArea().h >= 56 && v.cards[0].nameRect().h >= 56,
           "native card target and rename actions meet touch minimum");
    r.inst.pendingMacroTarget = 0;
    r.refresh();
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

    ui::QGroup::setWideDevice (true);
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

    check (! macr.ui.hostBackground(), "hostBackground flag is initially false");
    macr.ui.setHostBackground (true);
    check (macr.ui.hostBackground(), "setHostBackground(true) enables host background");
    auto liveData = std::make_unique<gpu::SceneData>();
    std::memset (liveData.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder lsb (*liveData);
    lsb.begin (gpu::kKindPlay, 0, 1280, 800);
    macr.ui.draw (lsb);
    check (! lsb.overflow && liveData->numOps > 0, "Live pass produces ops (" + std::to_string (liveData->numOps) + " ops)");
    check (liveData->numOps < macData->numOps, "Live pass has fewer ops than all-in-one (" + std::to_string (liveData->numOps) + " vs " + std::to_string (macData->numOps) + ")");
    std::vector<uint32_t> liveCv;
    renderScene (*liveData, liveCv);

    const auto canvas = 0xff000000u | ui::theme::kNativeCanvas;
    const auto composedDifferences = [canvas] (const std::vector<uint32_t>& first, const std::vector<uint32_t>& second) {
        int differences = 0;
        for (size_t i = 0; i < first.size(); ++i)
        {
            const uint32_t a = draw::over (canvas, first[i], 255), b = draw::over (canvas, second[i], 255);
            for (int shift = 0; shift < 24; shift += 8)
                if (std::abs (static_cast<int> ((a >> shift) & 255u) - static_cast<int> ((b >> shift) & 255u)) > 1)
                {
                    ++differences;
                    break;
                }
        }
        return differences;
    };
    check (composedDifferences (macCv, liveCv) == 0, "native macro page looks the same before and after background detection");

    {
        const layout::View* mv = nullptr;
        for (const layout::View& cand : layout::kViews)
            if (cand.kind == layout::kKindMacros)
                mv = &cand;
        check (mv != nullptr, "the layout has a Macros view");
        if (mv != nullptr)
        {
            const layout::Rect spots[3] = { mv->markers[0].rect, mv->instance, mv->markers[1].rect };
            const uint32_t ground = canvas;
            int badLive = 0;
            for (const layout::Rect& r : spots)
                for (int y = r.y - 1; y < r.y + r.h + 1; ++y)
                    for (int x = r.x - 1; x < r.x + r.w + 1; ++x)
                    {
                        const uint32_t lv = liveCv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)];
                        if (lv != ground && lv != macCv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)])   // the title "MACROS" (live) lies over the spots: its pixels are the all-in-one picture's
                            ++badLive;
                    }
            check (badLive == 0, "the live pass covers the three marker spots with opaque ground, only the title's own pixels on top (bad pixels: " + std::to_string (badLive) + ")");
        }
    }

    // The helper caches the cells of a scene in at most TWO layers and presents only the damage between two pictures (gpu/cell_plan.h, gpu-helper/main.cpp partialBoxes): a scene whose cells are separated by more than one
    // group of plain drawing ops "makes the whole scene uncached" and every picture repaints the full plane (XL 2026-11-21: painted_px 1.02 M, full_frames = pictures). The live pass of the transparent plane must plan.
    {
        static float levels[256];
        for (int i = 0; i < 256; ++i)
            levels[i] = 0.2f + 0.5f * static_cast<float> (i % 7) / 7.0f;   // a spectrum with sound: the curve, its fill and its line are plain ops of the scene
        macr.ui.view().spectrum.levels = levels;
        for (bool hostGround : { false, true })
        {
            macr.ui.setHostBackground (hostGround);
            auto d = std::make_unique<gpu::SceneData>();
            std::memset (d.get(), 0, sizeof (gpu::SceneData));
            gpu::SceneBuilder b (*d);
            b.begin (gpu::kKindPlay, 0, 1280, 800);
            macr.ui.draw (b);
            gpu::CellPlan plan;
            const bool planned = gpu::planCells (*d, plan);
            const std::string what = ! hostGround ? "all-in-one" : "live";
            check (planned, "the " + what + " scene of the Macros page makes a cell plan (else every picture repaints the whole plane)");
            if (planned)
            {
                long long covered = 0;
                for (const gpu::CellRef& c : plan.cells)
                    covered += static_cast<long long> (c.x1 - c.x0) * (c.y1 - c.y0);
                check (covered > 0, "the " + what + " scene's cells cover some area");
                check (plan.cells.size() >= 10, "the " + what + " scene has its cells (" + std::to_string (plan.cells.size()) + ")");
                // the plain ops (drawn every picture) must be few and small: the spectrum curve of one strip, the frames of the Q group
                int plain = 0;
                for (uint32_t i = 0; i < d->numOps; ++i)
                    plain += plan.inCell[i] == 0;
                check (plain < 60, "the " + what + " scene has few plain ops outside the cells (" + std::to_string (plain) + ")");
            }
        }
        // Native macro value changes do not repaint overlay cells; names remain live.
        {
            macr.ui.setHostBackground (true);
            auto planOf = [&] (gpu::CellPlan& plan) {
                auto d = std::make_unique<gpu::SceneData>();
                std::memset (d.get(), 0, sizeof (gpu::SceneData));
                gpu::SceneBuilder b (*d);
                b.begin (gpu::kKindPlay, 0, 1280, 800);
                macr.ui.draw (b);
                return gpu::planCells (*d, plan);
            };
            gpu::CellPlan before, after;
            const bool ok1 = planOf (before);
            macr.inst.writeParam (macr.ui.macroParam (3), 0.9f);
            macr.refresh();
            const bool ok2 = planOf (after);
            int changed = 0;
            if (ok1 && ok2 && before.cells.size() == after.cells.size())
                for (size_t i = 0; i < before.cells.size(); ++i)
                    changed += before.cells[i].hash != after.cells[i].hash;
            check (ok1 && ok2 && before.cells.size() == after.cells.size() && changed == 0, "native macro value change leaves overlay cells untouched (" + std::to_string (changed) + " of " + std::to_string (before.cells.size()) + ")");
        }
        macr.inst.writeParam (macr.ui.macroParam (3), 0.0f);   // (back to the state of the pictures above)
        macr.refresh();
        macr.ui.view().spectrum.levels = nullptr;
        macr.ui.setHostBackground (false);
    }

    // The spectrum must repaint in every picture in which it changes, with the background on AND off: the page decides "changed" by the hash of its scene (page_view_base.h sceneHash). The rows of a plain curve
    // (the live spectrum line) live in the pool of the scene, not in its ops: a hash over the ops alone never changed with the sound (XL 2026-11-21: 2 pictures in 30 s with the background on).
    {
        static float lvA[256], lvB[256];
        for (int i = 0; i < 256; ++i)
        {
            lvA[i] = 0.2f + 0.5f * static_cast<float> (i % 7) / 7.0f;
            lvB[i] = i == 100 ? lvA[i] + 0.1f : lvA[i];   // one point of the curve moves
        }
        for (bool hostGround : { false, true })
        {
            macr.ui.setHostBackground (hostGround);
            uint32_t hashOf[2] = {};
            int k = 0;
            for (const float* lv : { static_cast<const float*> (lvA), static_cast<const float*> (lvB) })
            {
                macr.ui.view().spectrum.levels = lv;
                auto d = std::make_unique<gpu::SceneData>();
                std::memset (d.get(), 0, sizeof (gpu::SceneData));
                gpu::SceneBuilder b (*d);
                b.begin (gpu::kKindPlay, 0, 1280, 800);
                macr.ui.draw (b);
                hashOf[k++] = page::sceneHash (*d);
            }
            check (hashOf[0] != hashOf[1], std::string ("a changing spectrum changes the scene hash (") + (! hostGround ? "all-in-one" : "background on, live pass") + ")");
        }
        macr.ui.view().spectrum.levels = nullptr;
        macr.ui.setHostBackground (false);
    }

    // Names, destinations, bank and macro values remain live over the same native card faces.
    {
        MacroRig nd;
        nd.inst.setMacroName (0, "Cutoff");
        nd.inst.setMacroName (1, "Drive + Warp");
        nd.inst.setMacroName (5, "A Very Long Macro Name Indeed");
        setSlot (nd.inst, 0, uibind::macroSource (1), 1, 40.0f, false);
        setSlot (nd.inst, 1, uibind::macroSource (6), 2, -30.0f, false);
        nd.inst.writeParam (nd.ui.macroParam (2), 0.6f);
        nd.ui.setBank (1);
        nd.refresh();
        nd.refresh();
        check (nd.ui.cardName (0) == "CUTOFF" && nd.ui.bank() == 1,
               "the non-default instance preserves its names and bank 2 focus");
        auto render = [&] (bool hostGround, std::vector<uint32_t>& out) {
            nd.ui.setHostBackground (hostGround);
            auto d = std::make_unique<gpu::SceneData>();
            std::memset (d.get(), 0, sizeof (gpu::SceneData));
            gpu::SceneBuilder b (*d);
            b.begin (gpu::kKindPlay, 0, 1280, 800);
            nd.ui.draw (b);
            check (! b.overflow, "the non-default scene fits in the buffer");
            renderScene (*d, out);
        };
        std::vector<uint32_t> ndAll, ndLive;
        render (false, ndAll);
        render (true, ndLive);
        check (composedDifferences (ndAll, ndLive) == 0, "non-default instance matches over the native canvas");
        check (ndAll != macCv, "the non-default instance really looks different (the test can fail)");
    }

    // the OFF state (no background in the skin): the page draws everything itself, the same picture as at the start
    macr.ui.setHostBackground (false);
    auto offData = std::make_unique<gpu::SceneData>();
    std::memset (offData.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder osb (*offData);
    osb.begin (gpu::kKindPlay, 0, 1280, 800);
    macr.ui.draw (osb);
    std::vector<uint32_t> offCv;
    renderScene (*offData, offCv);
    check (offCv == macCv, "with the host background off the page draws everything (same picture as the all-in-one pass)");
}

void testStandaloneModNativeControls()
{
    namespace n = nativeui::mod;
    ModRig r;
    r.ui.setNativeControls (true);
    auto& m = r.ui.matrix();
    auto covers = [&] (const ui::Component& control)
    {
        const auto b = control.rect();
        return r.ui.covers (b.x + b.w / 2, b.y + b.h / 2);
    };
    auto post = [&] (int field, float value)
    {
        const auto snap = r.inst.nativeMod.at (field);
        r.inst.nativeMod.report (snap);
        r.inst.nativeMod.post (snap, value);
    };
    auto centrePixel = [&] (const ui::Component& control)
    {
        auto data = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.ui.draw (sb);
        std::vector<uint32_t> pixels;
        renderScene (*data, pixels);
        const auto b = control.rect();
        return pixels[(b.y + b.h / 2) * 1280 + b.x + b.w / 2];
    };
    auto& first = m.windowRows[0];
    setSlot (r.inst, 6, 1, 1, 0.0f, false);
    r.refresh();
    check (r.inst.nativeMod.layout() == n::Matrix && r.inst.nativeRows[0] == 7 && r.inst.nativeRows[1] == 0,
           "one active route binds visible row one to slot 7 and hides empty rows");
    check (! covers (first.amountBar) && ! covers (first.biToggle) && ! covers (first.bypassToggle)
           && covers (first.number) && ! covers (first.sourceBtn) && ! covers (first.destBtn)
           && ! covers (first.detailBtn) && ! covers (m.tabAll) && ! covers (m.add),
           "matrix passes all six row controls and mode/ADD hit targets to the host while retaining the row number");
    check (centrePixel (first.amountBar) == 0 && centrePixel (first.detailBtn) == 0
           && centrePixel (first.sourceBtn) == 0 && centrePixel (first.destBtn) == 0,
           "the unscrolled canvas leaves Amount, MORE, Source and Target transparent");
    const auto initial = first.detailBtn.rect();
    const int more = n::navigationField (0, n::OpenMore);
    const int sourceAction = n::navigationField (0, n::OpenSource);
    const int targetAction = n::navigationField (0, n::OpenDestination);
    check (r.tapNative (first.detailBtn, more) && r.ui.openSlot() == 6 && r.inst.nativeMod.layout() == n::DetailDelay,
           "unscrolled MORE resolves the actual visible route");
    check (r.inst.nativeRows[0] == 0 && r.inst.nativeMod.at (n::Amount).cell().target < 0,
           "MORE hides all matrix widgets and drops their target bindings");
    auto& d = r.ui.detail();
    check (covers (d.back) && covers (d.deleteBtn) && covers (d.auxSource) && ! covers (d.auxOutput) && ! covers (d.power),
           "detail owns navigation and Aux picker while its scalar apertures pass to native");
    check (r.tapC (d.back) && r.inst.nativeMod.layout() == n::Matrix, "BACK restores the matrix frame");

    for (int slot = 8; slot <= 28; slot += 2)
        setSlot (r.inst, slot, slot % 4 == 0 ? 1 : 8, 1, 0.0f, false);
    r.refresh();
    auto zone = m.zoneRect();
    auto thumb = m.thumbRect();
    check (r.inst.nativeMod.layout() == n::MatrixScroll && first.detailBtn.rect().x < initial.x
           && r.ui.covers (zone.x + zone.w / 2, zone.y + 10) && ! covers (first.detailBtn),
           "scrolled geometry moves the native cells and MORE left of an overlay-owned scrollbar");
    check (centrePixel (first.amountBar) == 0 && centrePixel (first.detailBtn) == 0,
           "the scrolled canvas leaves Amount and MORE transparent");
    const auto stale = r.inst.nativeMod.at (n::Amount);
    const auto staleMore = r.inst.nativeModNavigation.at (more);
    r.inst.postModNavigation (more, 1.0f);
    post (n::Amount, 0.9f);
    const int writesBeforeScroll = r.inst.writes;
    r.drag (thumb.x + thumb.w / 2, thumb.y + thumb.h / 2, thumb.x + thumb.w / 2, zone.bottom() - 16);
    const int scrolledSlot = m.slotAtWindowIndex (0);
    check (m.firstRow() > 0 && scrolledSlot != 6 && r.inst.nativeRows[0] == scrolledSlot + 1 && r.inst.writes == writesBeforeScroll,
           "real scrollbar drag retargets row one and drops a write queued against its previous route");
    r.inst.nativeMod.post (stale, 0.95f);
    r.inst.nativeModNavigation.post (staleMore, 1.0f);
    r.refresh();
    check (r.inst.writes == writesBeforeScroll && r.ui.topPage() == 0,
           "queued and delayed events from the old row revision cannot edit or open the new route");

    const auto sp = synthyboy::modSlotParams (scrolledSlot);
    post (n::Amount, 0.75f);
    post (n::Polarity, 1.0f);
    post (n::Bypass, 1.0f);
    r.refresh();
    check (r.inst.param (sp.amount) == 50.0f && r.inst.param (sp.bipolar) == 1.0f && r.inst.param (sp.bypass) == 1.0f
           && r.inst.writes == writesBeforeScroll + 3 && r.inst.get ("mod01.amount") == 0.0f,
           "native amount, polarity and bypass each write the scrolled route once");
    check (r.tapNative (first.detailBtn, more) && r.ui.openSlot() == scrolledSlot && r.inst.nativeMod.layout() == n::DetailDelay,
           "scrolled MORE opens the same slot as the scrolled native controls");
    post (n::kDetail + n::Delay, 0.75f);
    post (n::kDetail + n::DelaySync, 1.0f);
    r.refresh();
    r.refresh();
    check (r.inst.nativeMod.layout() == n::DetailBeats && r.inst.param (sp.delay) == synthyboy::paramTable()[sp.delay].def,
           "SYNC selects the beats domain and drops any old millisecond edit");
    post (n::kDetail + n::Delay, 0.5f);
    r.refresh();
    check (r.inst.lastWritten == sp.delayBeats, "detail tempo division writes the active route's beats parameter");
    r.tapC (d.auxSource);
    check (r.ui.topPage() == ModUi::kPageList && r.inst.nativeMod.layout() == n::Hidden && covers (d.auxOutput),
           "the grouped Aux picker is reachable and hides both native views");
    r.stack.back();
    r.refresh();
    check (r.tapC (d.back) && r.inst.nativeMod.layout() == n::MatrixScroll, "closing MORE restores scroll and native rows");

    const int beforeFilter = r.inst.writes;
    check (r.tapNative (m.tabSource, n::Mode, 2.0f / n::kLastMode) && r.inst.nativeModNavigation.layout() == 2
           && ! covers (m.sourceFilter) && r.inst.nativeModNavigation.at (n::DestinationGroup).cell().target < 0,
           "BY SOURCE binds the native source-group menu and hides the target group");
    const int sourceFilter = uibind::sourceGroup (8);
    check (r.tapNative (m.sourceFilter, n::SourceGroup, static_cast<float> (sourceFilter) / n::kLastSourceGroup)
           && r.inst.nativeSource == sourceFilter && r.inst.writes == beforeFilter,
           "the source-group edit filters the matrix without changing a route");
    const int filteredSlot = m.slotAtWindowIndex (0);
    check (filteredSlot >= 0 && filteredSlot != 6 && filteredSlot != scrolledSlot && r.inst.nativeRows[0] == filteredSlot + 1,
           "the source filter projects its non-contiguous route onto the native row");
    post (n::Amount, 0.2f);
    r.refresh();
    check (r.inst.lastWritten == synthyboy::modSlotParams (filteredSlot).amount, "the filtered row sends one amount edit to its actual route");
    const int writesBeforePreset = r.inst.writes;
    const auto bar = first.amountBar.rect();
    const int bx = bar.x + bar.w / 2, by = bar.y + bar.h / 2;
    r.ui.touch (ev (TouchEvent::Down, bx, by), r.stack, r.inst, r.now);
    post (n::Amount, 0.25f);
    ++r.inst.stateGen;
    r.refresh();
    check (r.inst.nativeMod.at (n::Amount).cell().target == -1 && r.inst.writes == writesBeforePreset,
           "preset change drops a queued write and disarms a held native gesture even if its slot is unchanged");
    r.ui.touch (ev (TouchEvent::Up, bx, by), r.stack, r.inst, r.now);
    post (n::Amount, 0.65f);
    r.refresh();
    check (r.inst.writes == writesBeforePreset + 1 && r.inst.lastWritten == synthyboy::modSlotParams (filteredSlot).amount,
           "the new modulation route accepts edits again after the old gesture is released");
    const int beforeSource = r.inst.writes;
    check (r.tapNative (first.sourceBtn, sourceAction) && r.ui.topPage() == ModUi::kPageList
           && r.inst.nativeModNavigation.layout() == 0 && covers (m.tabAll),
           "the native Source hit opens the grouped overlay list and suspends matrix ownership");
    r.tap (10, 300);
    check (r.ui.topPage() == 0 && r.inst.writes == beforeSource, "cancelling the Source list performs no route write");
    check (r.tapNative (first.sourceBtn, sourceAction) && r.ui.topPage() == ModUi::kPageList,
           "the same Source cell can reopen after cancellation");
    ++r.inst.stateGen;
    r.refresh();
    check (r.ui.topPage() == 0 && r.inst.writes == beforeSource,
           "a new preset dismisses the old grouped picker without applying its choice");
    const bool hostTarget = r.tapNative (first.destBtn, targetAction);
    check (hostTarget && r.ui.topPage() == ModUi::kPageList && r.inst.nativeMod.layout() == n::Hidden,
           "the row Target tap still reaches the hierarchical selector");
    const auto group = r.ui.list().rowRect (1);   // first group after NONE
    r.tap (group.x + group.w / 2, group.y + group.h / 2);
    const auto target = r.ui.list().rowRect (1);
    r.tap (target.x + target.w / 2, target.y + target.h / 2);
    check (r.ui.topPage() == 0 && r.inst.lastWritten == synthyboy::modSlotParams (filteredSlot).destination,
           "choosing a grouped Target writes the same filtered route and restores its native frame");
}

void testStandaloneModDynamicOverlayPreservation()
{
    ModRig r;
    r.ui.setNativeControls (true);
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 50.0f);
    r.inst.hasLive = true;
    r.inst.liveVals.source[0] = 0.8f;
    r.refresh();

    // 1. Dynamic live deflection is retained on amount bar
    const auto& bar = r.ui.matrix().windowRows[0].amountBar;
    check (std::fabs (bar.state.modValue - 0.7) < 0.01, "dynamic live deflection marker on amount bar is retained (0.7)");

    auto data = std::make_unique<gpu::SceneData>();
    std::memset (data.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 0, "scene draws dynamic modulation overlays on top of native background");

    // 2. Empty-state quick-add ModSourceBoard is retained as dynamic overlay
    r.inst.set ("mod01.source", 0.0f);
    r.inst.set ("mod01.destination", 0.0f);
    r.refresh();
    check (r.ui.matrix().boardShown(), "empty-state quick-add board is shown when no slots are active");
    const ui::ModSourceBoard& b = r.ui.matrix().board;
    const ui::Rect c0 = b.chipRect (0);
    check (r.ui.covers (c0.x + c0.w / 2, c0.y + c0.h / 2), "dynamic quick-add board captures touch in empty state");

    // Tap on chip opens target picker
    r.tap (c0.x + c0.w / 2, c0.y + c0.h / 2);
    check (r.ui.topPage() == ModUi::kPageList, "tapping dynamic source chip opens target list");

    // 3. Hierarchical ListPopup is retained as dynamic overlay
    const ui::Rect grp0 = r.ui.list().rowRect (0);
    check (r.ui.covers (grp0.x + grp0.w / 2, grp0.y + grp0.h / 2), "hierarchical ListPopup captures touch above native controls");
}

void testStandaloneModPresetAndPopupLifecycle()
{
    ModRig r;
    r.ui.setNativeControls (true);

    // 1. Initial populated state: slot 1 is active
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 50.0f);
    r.refresh();

    check (! r.ui.matrix().boardShown(), "populated state: matrix board is not shown");
    check (! r.inst.uiFullOpen, "on matrix with active slots, nativeControls active reports uiFullOpen = false (host native controls shown)");

    // 2. Empty state: 0 active slots
    r.inst.set ("mod01.source", 0.0f);
    r.inst.set ("mod01.destination", 0.0f);
    r.refresh();

    check (r.ui.matrix().boardShown(), "empty state: quick-add board is shown");
    check (! r.inst.uiFullOpen && r.inst.nativeMod.layout() == nativeui::mod::Hidden
           && r.inst.nativeModNavigation.layout() == 1, "empty board keeps the host header while hiding all row controls");
    check (r.ui.covers (640, 300), "overlay covers entire board in empty state");

    // 3. Preset reload with state generation advance
    r.inst.stateGen = 1;
    r.inst.set ("mod01.source", 2.0f);
    r.inst.set ("mod01.destination", 3.0f);
    r.refresh();

    check (! r.ui.matrix().boardShown(), "new preset with active slot exits empty board");
    check (! r.inst.uiFullOpen, "matrix view restored after preset load reports uiFullOpen = false");
    check (r.ui.openSlot() == -1, "preset load resets openSlot when on main view");

    // 4. Opening Slot Detail: host controls hidden atomically
    r.ui.openSlotDetail (0, r.stack);
    r.refresh();
    check (r.ui.topPage() == ModUi::kPageDetail, "slot detail is open");
    check (! r.inst.uiFullOpen && r.inst.nativeMod.layout() == nativeui::mod::DetailDelay, "detail exposes its native controls on an exclusive frame");

    // 5. Opening ListPopup from detail: modal list takes touch
    r.ui.matrix().setSlotData (0, "ENV 1", "Cutoff", 0.5, false, false, 1, 0, false, 0.0);
    // Opening list from detail page
    r.stack.openDetail (ModUi::kPageList);
    r.refresh();
    check (r.ui.topPage() == ModUi::kPageList, "opening list popup from detail sets topPage to kPageList");
    check (r.inst.uiFullOpen, "ListPopup reports uiFullOpen = true");
    check (r.ui.covers (640, 300), "covers is true across entire tab while popup is open");

    // Dismiss list via back
    r.stack.back();
    r.refresh();
    check (r.ui.topPage() == ModUi::kPageDetail, "returned to detail view");
    check (! r.inst.uiFullOpen && r.inst.nativeMod.layout() == nativeui::mod::DetailDelay, "closing the picker restores only the native detail cells");

    // Dismiss detail
    r.stack.back();
    r.refresh();
    check (r.ui.topPage() == 0, "returned to matrix view");
    check (! r.inst.uiFullOpen, "matrix view restores uiFullOpen = false");
}

void testPageViewNativeNavigation()
{
    namespace n = nativeui::mod;
    FakeInst inst;
    UiStack stack;
    stack.openFull (ModUi::kElementModMatrix);
    ViewInput input;
    input.io = &inst;
    input.ui = &stack;
    pages::ModPage view ("mod", 1);
    ModUi geometry;
    const auto& matrix = geometry.matrix();
    auto refresh = [&] { input.nowMs += 50.0; view.update (input); };
    auto tap = [&] (const ui::Component& button, int field, float direction)
    {
        const auto b = button.rect();
        const int x = b.x + b.w / 2, y = b.y + b.h / 2;
        const bool down = view.onTouch (ev (TouchEvent::Down, x, y), stack, inst, input.nowMs);
        if (! down)
            inst.postModNavigation (field, direction);
        const bool up = view.onTouch (ev (TouchEvent::Up, x, y), stack, inst, input.nowMs + 30.0);
        refresh();
        return ! down && ! up;
    };

    refresh();
    check (inst.nativeModNavigation.layout() == 1 && inst.nativeMod.layout() == n::Hidden
           && inst.nativeModNavigation.at (n::Add).cell().target >= 0,
           "the real Mod PageView starts with native header and ADD while the empty board owns the list");
    check (tap (matrix.tabDest, n::Mode, 1.0f) && inst.nativeModNavigation.layout() == 3
           && inst.nativeModNavigation.at (n::SourceGroup).cell().target < 0
           && inst.nativeModNavigation.at (n::DestinationGroup).cell().target >= 0,
           "native BY DEST routes the PageView to the target-group menu with zero active routes");
    geometry.matrix().setFilterMode (ui::ModMatrixView::FilterDest);
    const int unmatched = ModUi::destModuleOf (1) == n::kLastDestinationGroup - 1 ? 1 : n::kLastDestinationGroup;
    check (tap (matrix.destFilter, n::DestinationGroup, static_cast<float> (unmatched) / n::kLastDestinationGroup)
           && inst.nativeDestination == unmatched
           && inst.writes == 0 && inst.nativeMod.layout() == n::Hidden,
           "a native target-group change updates the empty view without writing the preset");
    inst.postModNavigation (n::Add, 1.0f);
    view.onHidden();
    refresh();
    check (inst.writes == 0 && stack.depth() == 1, "leaving the Mod PageView discards queued ADD");
    check (tap (matrix.add, n::Add, 1.0f) && stack.top() == ModUi::kPageDetail
           && inst.nativeModNavigation.layout() == 0 && inst.nativeMod.layout() == n::DetailDelay
           && inst.get ("mod01.source") == 1.0f && inst.get ("mod01.destination") == 1.0f
           && inst.get ("mod01.amount") == 50.0f && inst.nativeMod.at (n::kDetail + n::Amount).cell().target == synthyboy::modSlotParams (0).amount,
           "native ADD opens the first free route with the same ENV 1, destination and amount defaults");
    const int writes = inst.writes;
    inst.postModNavigation (n::Add, 1.0f);
    stack.back();
    refresh();
    check (inst.nativeModNavigation.layout() == 3 && inst.nativeMod.layout() == n::Hidden
           && inst.writes == writes, "detail suppresses ADD and returns to the previous filter with no matching rows");
    check (tap (matrix.tabAll, n::Mode, 1.0f / n::kLastMode) && inst.nativeRows[0] == 1,
           "native ALL returns to the newly created route");
}

void printNativeModContract()
{
    namespace n = nativeui::mod;
    ModUi mod;
    auto& matrix = mod.matrix();
    auto rect = [] (ui::Rect r) { std::printf ("[%d, %d, %d, %d]", r.x, r.y - ModUi::kTabY, r.w, r.h); };
    auto rects = [&] (const auto& areas)
    {
        std::printf ("[");
        bool first = true;
        for (const ui::Rect& area : areas)
        {
            if (! first) std::printf (", ");
            rect (area);
            first = false;
        }
        std::printf ("]");
    };
    std::printf ("{\n  \"rows\": %d,\n", matrix.visibleRows());
    auto header = matrix.headerNavigationApertures();
    matrix.setFilterMode (ui::ModMatrixView::FilterSource);
    header[4] = matrix.headerNavigationApertures()[4];
    matrix.setFilterMode (ui::ModMatrixView::FilterDest);
    header[5] = matrix.headerNavigationApertures()[5];
    matrix.setFilterMode (ui::ModMatrixView::FilterAll);
    std::printf ("  \"header\": ");
    rects (header);
    std::printf (",\n");
    for (int scroll = 0; scroll < 2; ++scroll)
    {
        const int rows = n::kRows + scroll;
        for (int i = 0; i < rows; ++i)
            matrix.setSlotData (i, "SRC", "DEST", 0.5, false, false, 1, 0);
        matrix.refresh();
        std::printf ("  \"%s\": {\"first\": ", scroll ? "scroll" : "normal");
        rects (matrix.windowRows[0].nativeApertures());
        std::printf (", \"navigation\": ");
        rects (matrix.windowRows[0].navigationApertures());
        std::printf (", \"rowStep\": %d", matrix.windowRows[1].rect().y - matrix.windowRows[0].rect().y);
        if (scroll)
        {
            std::printf (", \"zone\": ");
            rect (matrix.zoneRect());
        }
        std::printf ("},\n");
    }
    std::printf ("  \"detail\": ");
    rects (mod.detail().nativeApertures());
    std::printf ("\n}\n");
}

void testFactoryModBackground()
{
    ModRig r;
    r.ui.setNativeControls (true);
    r.refresh();
    check (!r.ui.hostBackground(), "Mod waits for a confirmed skin before revealing the host");
    r.ui.setHostBackground (true);
    check (r.ui.hostBackground(), "Mod confirms skin and activates host background");
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (scene->numOps == 0, "Mod native board produces zero overlay ops when host background is active");
    ui::theme::setLook (2);
    check (!r.ui.hostBackground(), "Mod alternate look owns its ground");
    ui::theme::setLook (1);
}

void testStandaloneModDetailNavigationAndBindings()
{
    namespace n = nativeui::mod;
    namespace nd = nativeui::moddetail;
    ModRig mr;
    mr.inst.targetsSupported = true;
    mr.ui.setNativeControls (true);

    // 1. Populate slots up to slot 9 (index 8):
    for (int s = 0; s < 9; ++s)
    {
        char prefix[16];
        std::snprintf (prefix, sizeof prefix, "mod%02d.", s + 1);
        mr.inst.set (std::string (prefix) + "source", static_cast<float> (s + 1));
        mr.inst.set (std::string (prefix) + "destination", static_cast<float> (s + 2));
        mr.inst.set (std::string (prefix) + "amount", 50.0f);
    }
    // Specific parameters for slot 9:
    mr.inst.set ("mod09.source", 10.0f);        // LFO 2
    mr.inst.set ("mod09.destination", 25.0f);   // Filter 1 Cutoff
    mr.inst.set ("mod09.amount", 65.0f);
    mr.inst.set ("mod09.bipolar", 1.0f);
    mr.inst.set ("mod09.bypass", 0.0f);
    mr.inst.set ("mod09.aux_source", 0.0f);     // NONE
    mr.inst.set ("mod09.aux_output", 75.0f);
    mr.inst.set ("mod09.aux_inv", 0.0f);
    mr.inst.set ("mod09.aux_power", 0.0f);
    mr.inst.set ("mod09.power", 0.0f);
    mr.inst.set ("mod09.stereo", 1.0f);
    mr.inst.set ("mod09.rise", 0.2f);
    mr.inst.set ("mod09.fall", 0.4f);
    mr.inst.set ("mod09.delay", 0.3f);
    mr.inst.set ("mod09.delay_sync", 0.0f);
    mr.inst.set ("mod09.delay_beats", 2.0f);

    mr.refresh();

    // Scroll matrix so slot 9 is exposed and not in row 0
    const auto lr = mr.ui.matrix().listRect();
    mr.ui.wheel (lr.x + lr.w / 2, lr.y + 4, -3, mr.stack, mr.inst, mr.now);
    mr.refresh();

    // Open detail for slot 9:
    mr.ui.openSlotDetail (8, mr.stack);
    mr.refresh();

    check (mr.stack.top() == ModUi::kPageDetail, "slot detail page is open");
    check (mr.ui.openSlot() == 8, "open slot is slot 9 (index 8)");

    // Detail navigation must be published!
    check (mr.inst.nativeModDetailNavigation.layout() == 1, "detail navigation is published when in detail view");
    check (mr.inst.nativeModDetailNavigation.at (nd::Back).cell().target >= 0, "Detail Back handle published");
    check (mr.inst.nativeModDetailNavigation.at (nd::Delete).cell().target >= 0, "Detail Delete handle published");
    check (mr.inst.nativeModDetailNavigation.at (nd::Source).cell().target == synthyboy::modSlotParams (8).source, "Detail Source bound to slot 9 source not row 0");
    check (mr.inst.nativeModDetailNavigation.at (nd::Target).cell().target == synthyboy::modSlotParams (8).destination, "Detail Target bound to slot 9 dest not row 0");
    check (mr.inst.nativeModDetailNavigation.at (nd::AuxSource).cell().target == synthyboy::modSlotParams (8).auxSource, "Detail AuxSource bound to slot 9 auxSource");

    // Test Back: returns to Matrix, does not add a slot!
    const int slotCountBeforeBack = mr.ui.matrix().visibleSlotCount();
    mr.inst.postModDetailNavigation (nd::Back, 1.0f);
    mr.refresh();
    check (mr.stack.depth() == 1, "Detail Back returns to matrix view");
    check (mr.ui.matrix().visibleSlotCount() == slotCountBeforeBack, "Detail Back does not add a slot");
    check (mr.inst.nativeModDetailNavigation.layout() == 0, "Detail navigation cleared when leaving detail");

    // Reopen detail for slot 9
    mr.ui.openSlotDetail (8, mr.stack);
    mr.refresh();

    // Test two-step Delete confirmation:
    check (! mr.ui.detail().isDeleteConfirmed(), "delete not confirmed initially");
    mr.inst.postModDetailNavigation (nd::Delete, 1.0f);
    mr.refresh();
    check (mr.ui.detail().isDeleteConfirmed(), "first Delete tap sets confirmation state");
    check (mr.stack.top() == ModUi::kPageDetail, "first Delete tap stays on detail page");
    check (mr.inst.get ("mod09.source") == 10.0f, "slot 9 not yet deleted");

    // Second Delete tap confirms and removes exact open slot (slot 9):
    mr.inst.postModDetailNavigation (nd::Delete, 1.0f);
    mr.refresh();
    check (mr.stack.depth() == 1, "confirmed Delete returns to matrix view");
    check (mr.inst.get ("mod09.source") == 0.0f, "confirmed Delete cleared mod09.source");
    check (mr.inst.get ("mod09.destination") == 0.0f, "confirmed Delete cleared mod09.destination");
    check (mr.inst.get ("mod09.amount") == 0.0f, "confirmed Delete cleared mod09.amount");

    // Re-populate slot 9 and reopen detail to test AuxSource, Source, Target:
    mr.inst.set ("mod09.source", 10.0f);
    mr.inst.set ("mod09.destination", 25.0f);
    mr.inst.set ("mod09.amount", 65.0f);
    mr.inst.set ("mod09.delay_sync", 0.0f);
    mr.ui.openSlotDetail (8, mr.stack);
    mr.refresh();

    // Test AuxSource tap: opens Aux list for slot 9, does not change filterGroup
    const int filterModeBefore = static_cast<int> (mr.ui.matrix().filterMode());
    mr.inst.postModDetailNavigation (nd::AuxSource, 1.0f);
    mr.refresh();
    check (mr.stack.top() == ModUi::kPageList, "AuxSource tap opens list popup");
    check (mr.ui.listKind() == ModUi::ListKind::Aux, "list popup is Aux list");
    check (static_cast<int> (mr.ui.matrix().filterMode()) == filterModeBefore, "AuxSource does not change matrix filter group");
    mr.stack.back();
    mr.refresh();

    // Test Source tap: opens Source list for slot 9 (not row 0)
    mr.inst.postModDetailNavigation (nd::Source, 1.0f);
    mr.refresh();
    check (mr.stack.top() == ModUi::kPageList, "Source tap opens list popup");
    check (mr.ui.listKind() == ModUi::ListKind::Source, "list popup is Source list");
    check (mr.ui.listSlot() == 8, "Source list is for slot 9, not row 0 after scroll");
    mr.stack.back();
    mr.refresh();

    // Test Target tap: opens Dest list for slot 9 (not row 0)
    mr.inst.postModDetailNavigation (nd::Target, 1.0f);
    mr.refresh();
    check (mr.stack.top() == ModUi::kPageList, "Target tap opens list popup");
    check (mr.ui.listKind() == ModUi::ListKind::Dest, "list popup is Dest list");
    check (mr.ui.listSlot() == 8, "Target list is for slot 9, not row 0 after scroll");
    mr.stack.back();
    mr.refresh();

    // Test native writes for all detail fields:
    auto postField = [&] (int field, float norm)
    {
        const auto snap = mr.inst.nativeMod.at (field);
        mr.inst.nativeMod.report (snap);
        mr.inst.nativeMod.post (snap, norm);
        mr.refresh();
    };

    postField (n::kDetail + n::DetailAmount, 0.85f);
    check (std::abs (mr.inst.get ("mod09.amount") - 70.0f) < 2.0f, "native write Amount writes mod09.amount");

    postField (n::kDetail + n::DetailPolarity, 0.0f);
    check (mr.inst.get ("mod09.bipolar") == 0.0f, "native write Polarity writes mod09.bipolar");

    postField (n::kDetail + n::DetailBypass, 1.0f);
    check (mr.inst.get ("mod09.bypass") == 1.0f, "native write Bypass writes mod09.bypass");

    postField (n::kDetail + n::AuxScale, 0.5f);
    check (std::abs (mr.inst.get ("mod09.aux_output") - 100.0f) < 2.0f, "native write AuxScale writes mod09.aux_output");

    postField (n::kDetail + n::AuxInvert, 1.0f);
    check (mr.inst.get ("mod09.aux_inv") == 1.0f, "native write AuxInvert writes mod09.aux_inv");

    postField (n::kDetail + n::AuxCurve, 0.75f);
    check (mr.inst.get ("mod09.aux_power") > 0.0f, "native write AuxCurve writes mod09.aux_power");

    postField (n::kDetail + n::ShapeCurve, 0.25f);
    check (mr.inst.get ("mod09.power") < 0.0f, "native write ShapeCurve writes mod09.power");

    postField (n::kDetail + n::Stereo, 0.0f);
    check (mr.inst.get ("mod09.stereo") == 0.0f, "native write Stereo writes mod09.stereo");

    postField (n::kDetail + n::Rise, 0.6f);
    check (mr.inst.get ("mod09.rise") > 0.0f, "native write Rise writes mod09.rise");

    postField (n::kDetail + n::Fall, 0.7f);
    check (mr.inst.get ("mod09.fall") > 0.0f, "native write Fall writes mod09.fall");

    // Delay when sync is OFF:
    check (mr.inst.nativeMod.layout() == n::DetailDelay, "Detail view is DetailDelay when delaySync is 0");
    postField (n::kDetail + n::Delay, 0.5f);
    check (mr.inst.get ("mod09.delay") > 0.0f, "native write Delay writes mod09.delay when sync is 0");

    // Toggle DelaySync to ON:
    postField (n::kDetail + n::DelaySync, 1.0f);
    mr.refresh();
    check (mr.inst.get ("mod09.delay_sync") == 1.0f, "native write DelaySync sets mod09.delay_sync to 1");
    check (mr.inst.nativeMod.layout() == n::DetailBeats, "Detail view switches to DetailBeats when delaySync is 1");

    // Delay when sync is ON (writes delayBeats):
    postField (n::kDetail + n::Delay, 0.4f);
    check (mr.inst.get ("mod09.delay_beats") > 0.0f, "native write Delay writes mod09.delay_beats when sync is 1");

    // Zero overlay ops test:
    mr.ui.setHostBackground (true);
    mr.refresh();
    check (mr.ui.hostBackground(), "hostBackground is active in detail view");
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (sb);
    check (scene->numOps == 0, "Detail view produces EXACTLY zero overlay ops when hostBackground is active");

    // Test Matrix view produces exactly zero overlay ops when hostBackground is active:
    mr.stack.back();
    mr.refresh();
    auto matScene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder msb (*matScene);
    msb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (msb);
    check (matScene->numOps == 0, "Matrix view produces EXACTLY zero overlay ops when hostBackground is active");

    // Reopen detail for slot 9 to test Delete confirmation sequence and presentation readout:
    mr.ui.openSlotDetail (8, mr.stack);
    mr.refresh();

    // Verify initial state: not armed
    check (! mr.ui.detail().isDeleteConfirmed(), "initial confirmation state is unarmed");
    // Verify native presentation readout for Delete button / confirm handle
    check (mr.inst.nativeModDetailNavigation.at (nd::Delete).cell().source == 0, "Delete navigation cell source is 0 when unarmed");
    check (mr.inst.nativeModDetailNavigation.at (nd::Confirm).cell().source == 0, "Confirm presentation handle cell source is 0 when unarmed");

    // First press on Delete:
    mr.inst.postModDetailNavigation (nd::Delete, 1.0f);
    mr.refresh();
    check (mr.stack.top() == ModUi::kPageDetail, "first press on Delete stays in detail view");
    check (mr.ui.detail().isDeleteConfirmed(), "first press arms confirmation state");
    check (mr.inst.nativeModDetailNavigation.at (nd::Delete).cell().source == 1, "Delete navigation cell source is 1 (CONFIRM? state)");
    check (mr.inst.nativeModDetailNavigation.at (nd::Confirm).cell().source == 1, "Confirm presentation handle cell source is 1 (CONFIRM? state)");
    check (mr.inst.get ("mod09.source") == 10.0f, "first press does NOT delete slot 9");

    // Back cancels confirmation:
    mr.inst.postModDetailNavigation (nd::Back, 1.0f);
    mr.refresh();
    check (mr.stack.top() == ModUi::kElementModMatrix, "Back returns to matrix view");
    check (mr.ui.topPage() == 0, "topPage is 0 in matrix view");
    check (mr.inst.get ("mod09.source") == 10.0f, "slot 9 is NOT deleted after Back cancel");

    // Reopen detail for slot 9:
    mr.ui.openSlotDetail (8, mr.stack);
    mr.refresh();
    check (! mr.ui.detail().isDeleteConfirmed(), "reopening detail starts with unarmed confirmation");
    check (mr.inst.nativeModDetailNavigation.at (nd::Confirm).cell().source == 0, "Confirm cell is 0 on reopening");

    // Arm confirmation again:
    mr.inst.postModDetailNavigation (nd::Delete, 1.0f);
    mr.refresh();
    check (mr.ui.detail().isDeleteConfirmed(), "confirmation armed again");
    check (mr.inst.nativeModDetailNavigation.at (nd::Confirm).cell().source == 1, "Confirm cell is 1 when armed");

    // Second press on Delete confirms deletion:
    mr.inst.postModDetailNavigation (nd::Delete, 1.0f);
    mr.refresh();
    check (mr.stack.top() == ModUi::kElementModMatrix, "second press returns to matrix view");
    check (mr.ui.topPage() == 0, "topPage is 0 after confirmed delete");
    check (mr.inst.get ("mod09.source") == 0.0f, "confirmed delete cleared mod09.source");
    check (mr.inst.get ("mod09.destination") == 0.0f, "confirmed delete cleared mod09.destination");
    check (mr.inst.get ("mod09.amount") == 0.0f, "confirmed delete cleared mod09.amount");
    check (mr.inst.get ("mod08.source") == 8.0f, "slot 8 parameters remain intact");

    // Late event test: late Delete event in matrix view must NOT delete any slot!
    mr.inst.postModDetailNavigation (nd::Delete, 1.0f);
    mr.refresh();
    check (mr.inst.get ("mod08.source") == 8.0f, "late delete event in matrix view does not delete slot 8");
    check (mr.inst.get ("mod01.source") == 1.0f, "late delete event in matrix view does not delete slot 1");

    // Custom views retain function with hostBackground active:
    // 1. List popup picker:
    mr.inst.set ("mod01.source", 1.0f);
    mr.inst.set ("mod01.destination", 2.0f);
    mr.refresh();
    mr.ui.openSlotDetail (0, mr.stack);
    mr.refresh();
    mr.inst.postModDetailNavigation (nd::Source, 1.0f);
    mr.refresh();
    check (mr.ui.topPage() == ModUi::kPageList, "picker list page is active");
    auto listScene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder lsb (*listScene);
    lsb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (lsb);
    check (listScene->numOps == 0, "List picker is host native with zero overlay ops");
    mr.stack.back();
    mr.refresh();

    // 2. Targets panel in host native mode:
    mr.stack.openDetail (ModUi::kPageTargets);
    mr.refresh();
    check (mr.ui.topPage() == ModUi::kPageTargets, "targets panel is active");
    const auto tBack = mr.ui.targetsPage().back.rect();
    check (mr.ui.isNativeControl (tBack.x + tBack.w / 2, tBack.y + tBack.h / 2), "targets Back button recognized as native control");
    check (! mr.ui.covers (tBack.x + tBack.w / 2, tBack.y + tBack.h / 2), "targets Back button does not steal touch from host");
    auto targetsScene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder tsb (*targetsScene);
    tsb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (tsb);
    check (targetsScene->numOps == 0, "Targets panel produces EXACTLY zero overlay ops when hostBackground is active");
    mr.stack.back();
    mr.refresh();

    // 3. Quick-add board on empty matrix in host native mode:
    mr.stack.back();
    mr.refresh();
    check (mr.stack.top() == ModUi::kElementModMatrix, "returned to matrix element");
    for (int s = 0; s < 64; ++s)
    {
        char prefix[16];
        std::snprintf (prefix, sizeof prefix, "mod%02d.", s + 1);
        mr.inst.set (std::string (prefix) + "source", 0.0f);
        mr.inst.set (std::string (prefix) + "destination", 0.0f);
    }
    mr.refresh();
    check (mr.ui.matrix().boardShown(), "quick-add board is shown on empty matrix");
    check (mr.inst.nativeModBoard.layout() == 1, "Quick-add board gate is published when empty");
    check (mr.ui.isNativeControl (200, 200), "Quick-add board area recognized as native control");

    auto boardScene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder bsb (*boardScene);
    bsb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (bsb);
    check (boardScene->numOps == 0, "Quick-add board produces EXACTLY zero overlay ops when hostBackground is active");

    // Test tapping a source on the native board:
    mr.inst.postModBoard (nativeui::modboard::Source, 7.0f / static_cast<float> (synthyboy::modSourceCount())); // Tap LFO 1 (choice 7)
    mr.refresh();
    check (mr.ui.topPage() == ModUi::kPageList, "Tapping source on quick board opens target list");
    check (mr.ui.targetsSource() == 7, "Targets source is LFO 1 (choice 7)");
    mr.stack.back();
    mr.refresh();
    check (mr.ui.topPage() == ModUi::kPageTargets, "Back from list reveals targets page");

    mr.ui.setHostBackground (false);
    auto fbScene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder fbsb (*fbScene);
    fbsb.begin (gpu::kKindPlay, 0, 1280, 800);
    mr.ui.draw (fbsb);
    check (fbScene->numOps > 0, "Fallback mode without hostBackground produces ops on overlay");
}

void testNativeListSelectionAndPaging()
{
    namespace nb = nativeui::modboard;
    namespace nd = nativeui::moddetail;
    ModRig r;
    r.ui.setNativeControls (true);
    r.ui.setHostBackground (true);
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 2.0f);
    r.refresh();
    r.ui.openSlotDetail (0, r.stack);
    r.refresh();
    auto post = [&] (int field) { r.inst.postModBoard (field, 1.0f); r.refresh(); r.refresh(); };
    auto open = [&] (int field) { r.inst.postModDetailNavigation (field, 1.0f); r.refresh(); r.refresh(); };
    open (nd::Source);
    check (r.inst.nativeModBoard.layout() == 2, "Source picker publishes native list");
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (scene->numOps == 0 && ! r.ui.covers (80, 280), "native picker draws no GPU furniture and accepts host input");
    post (nb::ListRow + 2); // LFO group
    check (r.inst.nativeModBoard.at (nb::ListRow).cell().target - nb::kChoiceBias == 7, "LFO group resolves actual source ID");
    const auto stale = r.inst.nativeModBoard.at (nb::ListRow);
    r.inst.nativeModBoard.report (stale);
    post (nb::ListNext);
    check (r.inst.nativeModBoard.at (nb::ListRow).cell().target - nb::kChoiceBias != 7, "Next displays later source choices");
    r.inst.nativeModBoard.post (stale, 1.0f);
    r.refresh();
    check (r.ui.topPage() == ModUi::kPageList && r.inst.get ("mod01.source") == 1.0f, "late row from previous page cannot write source");
    post (nb::ListPrev);
    post (nb::ListRow);
    check (r.inst.get ("mod01.source") == 7.0f && r.ui.topPage() == ModUi::kPageDetail, "native source pick writes actual route and returns");
    open (nd::AuxSource);
    post (nb::ListRow + 2);
    post (nb::ListRow);
    check (r.inst.get ("mod01.aux_source") == 7.0f, "native AUX picker writes auxiliary source");
    open (nd::Target);
    const float before = r.inst.get ("mod01.destination");
    post (nb::ListBack);
    check (r.inst.get ("mod01.destination") == before && r.ui.topPage() == ModUi::kPageDetail, "native target Back cancels without writing");
    open (nd::Target);
    post (nb::ListRow + 1); // first destination group, after NONE
    const int target = r.inst.nativeModBoard.at (nb::ListRow).cell().target - nb::kChoiceBias;
    post (nb::ListRow);
    check (target > 0 && r.inst.get ("mod01.destination") == target, "native target picker writes selected destination");

    ModRig empty;
    empty.ui.setNativeControls (true);
    empty.ui.setHostBackground (true);
    empty.refresh();
    auto ep = [&] (int field) { empty.inst.postModBoard (field, 1.0f); empty.refresh(); empty.refresh(); };
    ep (nb::Group); // ALL SOURCES
    check (empty.ui.listKind() == ModUi::ListKind::BoardSource && empty.inst.nativeModBoard.layout() == 2, "ALL SOURCES opens grouped native source chooser");
    ep (nb::ListRow + uibind::sourceGroup (57)); // actual engine group of final source
    bool foundFixed = false;
    for (int page = 0; page < 12 && ! foundFixed; ++page)
    {
        for (int row = 0; row < nb::kListRows; ++row)
            if (empty.inst.nativeModBoard.at (nb::ListRow + row).cell().target - nb::kChoiceBias == 57)
            {
                ep (nb::ListRow + row);
                foundFixed = true;
                break;
            }
        if (! foundFixed) ep (nb::ListNext);
    }
    check (foundFixed && empty.ui.targetsSource() == 57 && empty.ui.listKind() == ModUi::ListKind::NewTarget,
           "full source chooser reaches final source and opens native new-target list");
    ep (nb::ListRow); // first destination group (new targets omit NONE)
    ep (nb::ListRow); // first target
    check (empty.inst.get ("mod01.source") == 57.0f && empty.inst.get ("mod01.destination") > 0.0f,
           "native new-target selection creates modulation for chosen source");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--native-lists") == 0)
    {
        testNativeListSelectionAndPaging();
        std::printf ("mod_native_lists: %s\n", g_fails ? "FAILED" : "OK");
        return g_fails ? 1 : 0;
    }
    if (argc == 2 && std::strcmp (argv[1], "--native-contract") == 0)
    {
        printNativeModContract();
        return 0;
    }
    std::printf ("=== SynthyBoy MPC Overlay: Mod Matrix & Macros Host Tests ===\n");

    testNativeListSelectionAndPaging();
    testFactoryModBackground();
    testSlotParamsExistence();
    testModEmptyStateAndBoard();
    testMatrixViewLayoutAndTargets();
    testMatrixRowParameterWrites();
    testMatrixQLinkGroup();
    testListsAreGrouped();
    testSlotDetailOpenCloseAndDelete();
    testLiveDeflection();
    testFilterAndScroll();
    testStandaloneModNativeControls();
    testPageViewNativeNavigation();
    testStandaloneModDetailNavigationAndBindings();
    testStandaloneModDynamicOverlayPreservation();
    testStandaloneModPresetAndPopupLifecycle();
    testMacrosView();
    testMacroTargets();
    testSpectrum();
    testDrawingAndCpuRendering();

    std::printf (g_fails ? "mod_ui_host: FAILED (%d failures)\n" : "mod_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
