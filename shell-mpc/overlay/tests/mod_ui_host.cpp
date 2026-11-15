// SynthyBoy - MPC overlay tests: Modulation Matrix and Macros UI (mod_ui_host.cpp)
// Tests parameter wiring for all 64 slots, slot details, macro mapping, touch targets, Q-link routing, and CPU rendering.
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
        ui.update (inst, stack, now);
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
        now += 50.0;
        ui.update (inst, stack, now);
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

void testMatrixViewLayoutAndTargets()
{
    ModRig r;
    check (r.ui.matrix().rect().w == 1280 && r.ui.matrix().rect().h == 628, "ModMatrixView geometry is 1280 x 628");

    bool targetsOk = true;
    if (r.ui.matrix().close.rect().h < 48) targetsOk = false;
    if (r.ui.matrix().tabAll.rect().h < 48) targetsOk = false;

    // Check row button touch targets
    const auto& row0 = r.ui.matrix().windowRows[0];
    if (row0.sourceBtn.rect().h < 56 || row0.sourceBtn.rect().w < 56) targetsOk = false;
    if (row0.destBtn.rect().h < 56 || row0.destBtn.rect().w < 56) targetsOk = false;
    if (row0.amountBar.rect().h < 48 || row0.amountBar.rect().w < 120) targetsOk = false;
    if (row0.biToggle.rect().h < 56 || row0.biToggle.rect().w < 56) targetsOk = false;
    if (row0.bypassToggle.rect().h < 56 || row0.bypassToggle.rect().w < 56) targetsOk = false;
    if (row0.detailBtn.rect().h < 56 || row0.detailBtn.rect().w < 56) targetsOk = false;

    check (targetsOk, "all matrix touch targets are >= 56 px (or standard bar heights)");
}

void testMatrixRowParameterWrites()
{
    ModRig r;
    // Set slot 0 to active
    r.inst.set ("mod01.source", 1.0f); // Env 1
    r.inst.set ("mod01.destination", 1.0f); // First modulatable
    r.inst.set ("mod01.amount", 20.0f);
    r.inst.set ("mod01.bipolar", 0.0f);
    r.inst.set ("mod01.bypass", 0.0f);
    r.refresh();

    check (r.ui.matrix().slotData (0).active, "slot 0 is active after setting source and destination");
    check (std::fabs (r.ui.matrix().slotData (0).amount - 0.2) < 0.01, "amount reflects +20%");

    // Drag amount bar to right (increase amount)
    const int x0 = r.ui.matrix().windowRows[0].amountBar.rect().x + r.ui.matrix().windowRows[0].amountBar.rect().w / 2;
    const int y0 = r.ui.matrix().windowRows[0].amountBar.rect().y + r.ui.matrix().windowRows[0].amountBar.rect().h / 2;
    r.drag (x0, y0, x0 + 40, y0);
    check (r.inst.get ("mod01.amount") > 35.0f, "dragging amount bar writes mod01.amount (" + std::to_string (r.inst.get ("mod01.amount")) + ")");

    // Tap UNI/BI toggle
    r.tapC (r.ui.matrix().windowRows[0].biToggle);
    check (r.inst.get ("mod01.bipolar") == 1.0f, "tapping UNI/BI toggle sets mod01.bipolar = 1 (BI)");
    r.tapC (r.ui.matrix().windowRows[0].biToggle);
    check (r.inst.get ("mod01.bipolar") == 0.0f, "tapping UNI/BI toggle again sets mod01.bipolar = 0 (UNI)");

    // Tap ON/OFF bypass toggle
    r.tapC (r.ui.matrix().windowRows[0].bypassToggle);
    check (r.inst.get ("mod01.bypass") == 1.0f, "tapping ON/OFF toggle sets mod01.bypass = 1 (bypassed)");
    r.tapC (r.ui.matrix().windowRows[0].bypassToggle);
    check (r.inst.get ("mod01.bypass") == 0.0f, "tapping ON/OFF toggle again sets mod01.bypass = 0 (active)");
}

void testMatrixQLinkGroup()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.refresh();

    // Select row 0
    r.ui.matrix().selectRow (0);
    r.refresh();

    const synthyboy::ModSlotParams sp0 = synthyboy::modSlotParams (0);
    check (r.inst.q[0] == sp0.amount, "Q1 targets mod01.amount");
    check (r.inst.q[1] == sp0.source, "Q2 targets mod01.source");
    check (r.inst.q[2] == sp0.destination, "Q3 targets mod01.destination");

    // Turning Q1 adjusts amount
    const float prevAmt = r.inst.get ("mod01.amount");
    r.ui.turnQ (0, 5.0, r.stack, r.inst); // positive delta
    r.refresh();
    check (r.inst.get ("mod01.amount") > prevAmt, "turning Q1 increases mod01.amount");
}

void testSlotDetailOpenCloseAndDelete()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 50.0f);
    r.refresh();

    // Tap detail button '...' on window row 0
    r.tapC (r.ui.matrix().windowRows[0].detailBtn);
    check (r.stack.depth() == 2 && r.stack.top() == ModUi::kPageSlotDetail, "tapping '...' opens slot detail page on UiStack");
    check (r.ui.detail().rect().w == 680 && r.ui.detail().rect().h == 628, "detail panel geometry is 680 x 628");

    // Test detail panel parameter writes
    // Change aux output scale
    const int ax = r.ui.detail().auxOutput.rect().x + r.ui.detail().auxOutput.rect().w / 2;
    const int ay = r.ui.detail().auxOutput.rect().y + r.ui.detail().auxOutput.rect().h / 2;
    r.drag (ax, ay, ax, ay - 40);
    check (r.inst.get ("mod01.aux_output") > 110.0f, "dragging auxOutput writes mod01.aux_output");

    // Toggle aux invert
    r.tapC (r.ui.detail().auxInv);
    check (r.inst.get ("mod01.aux_inv") == 1.0f, "tapping auxInv toggles mod01.aux_inv");

    // Toggle delay sync
    r.tapC (r.ui.detail().delaySync);
    check (r.inst.get ("mod01.delay_sync") == 1.0f, "tapping delaySync toggles mod01.delay_sync");

    // Test Delete button with confirmation
    check (! r.ui.detail().isDeleteConfirmed(), "delete confirmation initially false");
    r.tapC (r.ui.detail().deleteBtn);
    check (r.ui.detail().isDeleteConfirmed(), "first tap on delete sets confirmation state to true");

    // Second tap confirms deletion
    r.tapC (r.ui.detail().deleteBtn);
    check (r.inst.get ("mod01.source") == 0.0f && r.inst.get ("mod01.destination") == 0.0f, "confirmed delete clears mod01 slot parameters");
    check (r.stack.depth() == 1, "confirmed delete returns back to matrix view");
}

void testLiveDeflection()
{
    ModRig r;
    r.inst.set ("mod01.source", 1.0f); // Env 1 (source choice 1)
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 50.0f); // +50% = 0.5 static

    // Provide live source readout
    r.inst.hasLive = true;
    r.inst.liveVals.source[0] = 0.8f; // Env 1 live value = 0.8
    r.refresh();

    const auto& e = r.ui.matrix().slotData (0);
    check (e.hasLive, "slot data has live readout");
    check (std::fabs (e.liveVal - 0.8) < 0.01, "slot data liveVal is 0.8");
    const auto& bar = r.ui.matrix().windowRows[0].amountBar;
    check (bar.state.hasLive, "amountBar in window row has live data");
    check (std::fabs (bar.state.liveAmount - 0.4) < 0.01, "amountBar live deflection is static * liveSource = 0.4 (" + std::to_string (bar.state.liveAmount) + ")");
}

void testMacrosView()
{
    MacroRig r;
    check (r.ui.view().rect().w == 1280 && r.ui.view().rect().h == 628, "MacroView geometry is 1280 x 628");

    // Test reading macro values
    r.inst.set ("macro.1", 35.0f);
    r.inst.set ("macro.2", 70.0f);
    r.refresh();

    check (std::fabs (r.ui.view().cards[0].knob.state.value - 0.35) < 0.01, "Macro 1 card value is 35%");
    check (std::fabs (r.ui.view().cards[1].knob.state.value - 0.70) < 0.01, "Macro 2 card value is 70%");

    // Drag macro 1 knob
    const int x0 = r.ui.view().cards[0].knob.rect().x + r.ui.view().cards[0].knob.rect().w / 2;
    const int y0 = r.ui.view().cards[0].knob.rect().y + r.ui.view().cards[0].knob.rect().h / 2;
    r.drag (x0, y0, x0, y0 - 40);
    check (r.inst.get ("macro.1") > 45.0f, "dragging Macro 1 knob writes macro.1 (" + std::to_string (r.inst.get ("macro.1")) + ")");

    // Test Bank 1 vs Bank 2 QGroup
    check (r.ui.bank() == 0, "default bank is Bank 1 (1-4)");
    check (r.inst.q[0] == FakeInst::idx ("macro.1") && r.inst.q[3] == FakeInst::idx ("macro.4"), "Bank 1 Q targets are macro.1 .. macro.4");

    // Switch to Bank 2
    r.tapC (r.ui.view().bank2Btn);
    check (r.ui.bank() == 1, "active bank is now Bank 2 (5-8)");
    check (r.inst.q[0] == FakeInst::idx ("macro.5") && r.inst.q[3] == FakeInst::idx ("macro.8"), "Bank 2 Q targets are macro.5 .. macro.8");

    // Turning Q1 in Bank 2 turns macro.5
    r.inst.set ("macro.5", 20.0f);
    r.refresh();
    r.ui.turnQ (0, 5.0, r.stack, r.inst);
    r.refresh();
    check (r.inst.get ("macro.5") > 20.0f, "turning Q1 in Bank 2 increases macro.5");
}

void testMacroDestinationResolution()
{
    MacroRig r;
    // Map Macro 1 (source choice 19) to two destinations in mod matrix slots 0 and 1
    const synthyboy::ModSlotParams sp0 = synthyboy::modSlotParams (0);
    const synthyboy::ModSlotParams sp1 = synthyboy::modSlotParams (1);
    const synthyboy::ModSlotParams sp2 = synthyboy::modSlotParams (2);

    r.inst.writeParam (sp0.source, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp0.source], 19.0f))); // Macro 1
    r.inst.writeParam (sp0.destination, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp0.destination], 1.0f))); // First dest
    r.inst.writeParam (sp0.bypass, 0.0f);

    r.inst.writeParam (sp1.source, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp1.source], 19.0f))); // Macro 1
    r.inst.writeParam (sp1.destination, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp1.destination], 2.0f))); // Second dest
    r.inst.writeParam (sp1.bypass, 0.0f);

    // Slot 2 has Macro 1 but is bypassed
    r.inst.writeParam (sp2.source, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp2.source], 19.0f)));
    r.inst.writeParam (sp2.destination, static_cast<float> (synthyboy::toNormalised (synthyboy::paramTable()[sp2.destination], 3.0f)));
    r.inst.writeParam (sp2.bypass, 1.0f);

    r.refresh();

    // Verify Macro 1 card has exactly 2 destination chips (bypassed slot excluded)
    check (r.ui.view().cards[0].chips.size() == 2, "Macro 1 card resolved 2 destination chips from active slots");
    check (r.ui.view().cards[1].chips.empty(), "Macro 2 card has 0 destination chips");

    // Tap Macro 1 chip row to request matrix filter
    r.tap (r.ui.view().cards[0].rect().x + 20, r.ui.view().cards[0].chipsRect().y + 10);
    check (r.ui.requestedMacro() == 0, "tapping Macro 1 card requested opening matrix filtered to macro 0");
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

    // Open detail panel and test draw
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

    // Macros view draw and render
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
    testMatrixViewLayoutAndTargets();
    testMatrixRowParameterWrites();
    testMatrixQLinkGroup();
    testSlotDetailOpenCloseAndDelete();
    testLiveDeflection();
    testMacrosView();
    testMacroDestinationResolution();
    testDrawingAndCpuRendering();

    std::printf (g_fails ? "mod_ui_host: FAILED (%d failures)\n" : "mod_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
