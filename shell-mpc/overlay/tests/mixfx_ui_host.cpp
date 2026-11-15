// SynthyBoy - MPC overlay tests: the MIXER and FX UI (mixer_ui.h, fx_ui.h):
// Layout validation, touch targets >= 56 px, hit-testing, parameter mapping in both directions,
// Q-group routing with 0 offenders, popup menus, live readout meters, and CPU reference rendering.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "../../ui/tests/ui_test.h"
#include "../fx_ui.h"
#include "../mixer_ui.h"
#include "../scene_cpu.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;
using namespace uit;

namespace
{
int g_testFails = 0;
void testCheck (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_testFails;
}

class TestInstance final : public IInstanceView
{
public:
    std::vector<float> v_;
    int writes = 0;
    int lastWritten = -1;
    float lastNorm = 0.0f;
    int qTargets[4] = { -1, -1, -1, -1 };
    int qCalls = 0;
    synthyboy::LiveValues liveValues;

    TestInstance()
    {
        v_.assign (synthyboy::paramCount(), 0.0f);
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }

    float param (int i) const noexcept override
    {
        return (i >= 0 && i < static_cast<int> (v_.size())) ? v_[i] : 0.0f;
    }

    bool live (synthyboy::LiveValues& lv) const noexcept override
    {
        lv = liveValues;
        return true;
    }

    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return 1; }

    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 4; ++i)
            qTargets[i] = (i < n) ? t[i] : -1;
        ++qCalls;
    }

    void writeParam (int i, float norm) noexcept override
    {
        if (i < 0 || i >= static_cast<int> (v_.size()))
            return;
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
        lastNorm = norm;
    }

    void set (const char* key, float plain)
    {
        const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (key);
        if (pi)
        {
            const int idx = static_cast<int> (pi - synthyboy::paramTable());
            v_[idx] = plain;
        }
    }

    float get (const char* key) const
    {
        const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (key);
        return pi ? v_[static_cast<size_t> (pi - synthyboy::paramTable())] : 0.0f;
    }
};

struct Rig
{
    TestInstance inst;
    UiStack stack;
    MixerUi mixer;
    FxUi fx;

    Rig()
    {
        mixer.setCanvas (1280, 800);
        fx.setCanvas (1280, 800);
    }

    void updateMixer() { mixer.update (inst, 0, stack, 100.0); }
    void updateFx() { fx.update (inst, 0, stack, 100.0); }

    void tapMixer (int x, int y)
    {
        TouchEvent evDown; evDown.kind = TouchEvent::Down; evDown.x = x; evDown.y = y;
        mixer.touch (evDown, stack, inst, 100.0);
        TouchEvent evUp; evUp.kind = TouchEvent::Up; evUp.x = x; evUp.y = y;
        mixer.touch (evUp, stack, inst, 120.0);
        updateMixer();
    }

    void tapFx (int x, int y)
    {
        TouchEvent evDown; evDown.kind = TouchEvent::Down; evDown.x = x; evDown.y = y;
        fx.touch (evDown, stack, inst, 100.0);
        TouchEvent evUp; evUp.kind = TouchEvent::Up; evUp.x = x; evUp.y = y;
        fx.touch (evUp, stack, inst, 120.0);
        updateFx();
    }

    void dragMixer (int x, int y1, int y2)
    {
        TouchEvent evDown; evDown.kind = TouchEvent::Down; evDown.x = x; evDown.y = y1;
        mixer.touch (evDown, stack, inst, 100.0);
        TouchEvent evMove; evMove.kind = TouchEvent::Move; evMove.x = x; evMove.y = y2;
        mixer.touch (evMove, stack, inst, 110.0);
        TouchEvent evUp; evUp.kind = TouchEvent::Up; evUp.x = x; evUp.y = y2;
        mixer.touch (evUp, stack, inst, 120.0);
        updateMixer();
    }

    void dragVerticalFx (int x, int y1, int y2)
    {
        TouchEvent evDown; evDown.kind = TouchEvent::Down; evDown.x = x; evDown.y = y1;
        fx.touch (evDown, stack, inst, 100.0);
        TouchEvent evMove; evMove.kind = TouchEvent::Move; evMove.x = x; evMove.y = y2;
        fx.touch (evMove, stack, inst, 110.0);
        TouchEvent evUp; evUp.kind = TouchEvent::Up; evUp.x = x; evUp.y = y2;
        fx.touch (evUp, stack, inst, 120.0);
        updateFx();
    }

    void dragCardFx (int x1, int y, int x2)
    {
        TouchEvent evDown; evDown.kind = TouchEvent::Down; evDown.x = x1; evDown.y = y;
        fx.touch (evDown, stack, inst, 100.0);
        TouchEvent evMove; evMove.kind = TouchEvent::Move; evMove.x = x2; evMove.y = y;
        fx.touch (evMove, stack, inst, 110.0);
        TouchEvent evUp; evUp.kind = TouchEvent::Up; evUp.x = x2; evUp.y = y;
        fx.touch (evUp, stack, inst, 120.0);
        updateFx();
    }
};

inline ui::Component* findHit (ui::Component& root, int x, int y)
{
    if (! root.enabled() || ! root.rect().contains (x, y))
        return nullptr;
    ui::Component* leaves[256];
    const int n = root.collectLeaves (leaves, 0, 256);
    for (int i = 0; i < n; ++i)
        if (leaves[i]->enabled() && leaves[i]->rect().contains (x, y))
            return leaves[i];
    return &root;
}

// ===============================================================================================================================================================
// MIXER TESTS
// ===============================================================================================================================================================

void testMixerLayoutAndHitTesting()
{
    Rig r;
    r.updateMixer();
    ui::MixerFullScreen& screen = r.mixer.screen();

    testCheck (screen.rect().w == 1280 && screen.rect().h == 628, "mixer: canvas placed on tab region");
    testCheck (ui::MixerFullScreen::kStrips == 10, "mixer: 10 channels (SUB, A, B, C, NOISE, F1, F2, BUS 1, BUS 2, MASTER)");

    // Check all strips fit within canvas
    bool allInside = true;
    for (int s = 0; s < 10; ++s)
    {
        const ui::Rect sr = screen.strips[s].rect();
        if (! screen.rect().contains (sr))
            allInside = false;
        // Verify touch targets >= 56 px
        testCheck (sr.w >= 56, "mixer: strip " + std::to_string (s) + " touch width >= 56 px (" + std::to_string (sr.w) + " px)");
        testCheck (screen.strips[s].fader.rect().h >= 100, "mixer: strip " + std::to_string (s) + " fader touch height >= 100 px");
    }
    testCheck (allInside, "mixer: all 10 strips inside canvas bounds");

    // Hit-testing leaves
    ui::Component* leaves[256];
    const int numLeaves = screen.collectLeaves (leaves, 0, 256);
    testCheck (numLeaves >= 40, "mixer: leaf components collected (" + std::to_string (numLeaves) + " leaves)");

    bool hitAll = true;
    for (int i = 0; i < numLeaves; ++i)
    {
        const ui::Component* leaf = leaves[i];
        if (leaf->enabled() && ! leaf->rect().empty())
        {
            const int cx = leaf->rect().x + leaf->rect().w / 2;
            const int cy = leaf->rect().y + leaf->rect().h / 2;
            const ui::Component* target = findHit (screen, cx, cy);
            if (target != leaf)
                hitAll = false;
        }
    }
    testCheck (hitAll, "mixer: hit-testing reaches every enabled leaf component");
}

void testMixerParameterWiring()
{
    Rig r;
    r.inst.set ("sub.level", -6.0f);
    r.inst.set ("sub.pan", -25.0f);
    r.inst.set ("osc1.bus1_send", 45.0f);
    r.inst.set ("master.volume", -3.0f);
    r.updateMixer();

    // Verify pulled values
    const auto& strips = r.mixer.screen().strips;
    testCheck (std::string (strips[0].pan.state.valueText).find ("L25") != std::string::npos, "mixer: sub.pan pulled as L25");
    testCheck (std::string (strips[1].send1.state.valueText).find ("45%") != std::string::npos, "mixer: osc1.send1 pulled as 45%");

    // Test fader drag
    const ui::Rect faderRect = strips[0].fader.rect();
    const int oldWrites = r.inst.writes;
    r.dragMixer (faderRect.x + faderRect.w / 2, faderRect.bottom() - 20, faderRect.y + 20);
    testCheck (r.inst.writes > oldWrites, "mixer: fader drag triggers writes");
    const int subLevelIdx = static_cast<int> (synthyboy::findParamByKey ("sub.level") - synthyboy::paramTable());
    testCheck (r.inst.lastWritten == subLevelIdx, "mixer: fader drag writes sub.level parameter");

    // Test power toggle
    const ui::Rect powerRect = strips[0].power.rect();
    const int prePowerWrites = r.inst.writes;
    r.tapMixer (powerRect.x + powerRect.w / 2, powerRect.y + powerRect.h / 2);
    testCheck (r.inst.writes > prePowerWrites, "mixer: power toggle writes parameter");
    const int subOnIdx = static_cast<int> (synthyboy::findParamByKey ("sub.on") - synthyboy::paramTable());
    testCheck (r.inst.lastWritten == subOnIdx, "mixer: power toggle writes sub.on");
}

void testMixerDestinationPopup()
{
    Rig r;
    r.stack.openFull (MixerUi::kElementMixer);
    r.updateMixer();
    testCheck (! r.mixer.listOpen(), "mixer: destination popup initially closed");

    // Tap destination button on Strip 0 (SUB)
    const ui::Rect destRect = r.mixer.screen().strips[0].destButton.rect();
    r.tapMixer (destRect.x + destRect.w / 2, destRect.y + destRect.h / 2);

    testCheck (r.mixer.listOpen(), "mixer: tapping destination button opens ListPopup");
    testCheck (r.stack.top() == MixerUi::kPageList, "mixer: ListPopup opened on UiStack");

    // Tap first item in list popup using rowRect(0)
    const ui::Rect rowR = r.mixer.list().rowRect (0);
    const int oldWrites = r.inst.writes;
    r.tapMixer (rowR.x + rowR.w / 2, rowR.y + rowR.h / 2);
    testCheck (r.inst.writes > oldWrites, "mixer: selecting destination writes parameter");
    const int subDestIdx = static_cast<int> (synthyboy::findParamByKey ("sub.destination") - synthyboy::paramTable());
    testCheck (r.inst.lastWritten == subDestIdx, "mixer: destination popup wrote sub.destination");
    testCheck (! r.mixer.listOpen(), "mixer: destination popup closed after selection");
}

void testMixerQGroups()
{
    Rig r;
    r.updateMixer();
    ui::MixerFullScreen& screen = r.mixer.screen();

    ui::Component* leaves[256];
    const int numLeaves = screen.collectLeaves (leaves, 0, 256);

    // Verify each strip's Q-Group has 0 offenders
    for (int s = 0; s < 10; ++s)
    {
        screen.setActiveGroup (s);
        const ui::Component* offender = nullptr;
        const int bad = screen.groups[s].check (leaves, numLeaves, &offender);
        testCheck (bad == 0, "mixer: Q-Group for strip " + std::to_string (s) + " has 0 offenders");
    }

    // Verify setQGroup targets published to instance
    screen.setActiveGroup (0); // SUB
    r.updateMixer();
    const int subLevelIdx = static_cast<int> (synthyboy::findParamByKey ("sub.level") - synthyboy::paramTable());
    const int subPanIdx = static_cast<int> (synthyboy::findParamByKey ("sub.pan") - synthyboy::paramTable());
    testCheck (r.inst.qTargets[0] == subLevelIdx, "mixer: Q1 is sub.level");
    testCheck (r.inst.qTargets[1] == subPanIdx, "mixer: Q2 is sub.pan");
}

void testMixerLivePeakMeters()
{
    Rig r;
    r.inst.liveValues.stripPeak[0] = 0.82f;
    r.inst.liveValues.stripPeak[9] = 0.94f;
    r.updateMixer();

    testCheck (r.mixer.screen().strips[0].fader.state.peak == 0.82f, "mixer: strip 0 live peak readout updated to 0.82");
    testCheck (r.mixer.screen().strips[9].fader.state.peak == 0.94f, "mixer: master strip live peak readout updated to 0.94");
}

// ===============================================================================================================================================================
// FX TESTS
// ===============================================================================================================================================================

void testFxLayoutAndHitTesting()
{
    Rig r;
    // Add EQ so slot editor and knobs are populated
    std::vector<synthyboy::FxWrite> writes;
    synthyboy::fxAddModule (r.inst.v_.data(), synthyboy::kFxTypeEq, 0, writes);
    for (const auto& w : writes)
        r.inst.v_[w.param] = w.value;
    r.updateFx();
    ui::FxFullScreen& screen = r.fx.screen();

    testCheck (screen.rect().w == 1280 && screen.rect().h == 628, "fx: canvas placed on tab region");
    testCheck (screen.chainBar.rect().h >= 68, "fx: chain bar height >= 68 px");
    testCheck (screen.slotEditor.rect().h >= 300, "fx: slot editor height >= 300 px");

    // Hit-testing leaves
    ui::Component* leaves[256];
    const int numLeaves = screen.collectLeaves (leaves, 0, 256);
    testCheck (numLeaves >= 20, "fx: leaves collected (" + std::to_string (numLeaves) + " leaves)");

    bool hitAll = true;
    for (int i = 0; i < numLeaves; ++i)
    {
        const ui::Component* leaf = leaves[i];
        if (leaf->enabled() && ! leaf->rect().empty())
        {
            const int cx = leaf->rect().x + leaf->rect().w / 2;
            const int cy = leaf->rect().y + leaf->rect().h / 2;
            const ui::Component* target = findHit (screen, cx, cy);
            if (target != leaf)
                hitAll = false;
        }
    }
    testCheck (hitAll, "fx: hit-testing reaches every enabled leaf component");
}

void testFxAddModuleAndParameterWiring()
{
    Rig r;
    r.stack.openFull (FxUi::kElementFx);
    r.updateFx();

    testCheck (! r.fx.listOpen(), "fx: type popup initially closed");

    // Tap "+" card to open FX type picker
    const ui::Rect plusRect = r.fx.screen().chainBar.plusCard.rect();
    r.tapFx (plusRect.x + plusRect.w / 2, plusRect.y + plusRect.h / 2);

    testCheck (r.fx.listOpen(), "fx: tapping '+' card opens type picker ListPopup");
    testCheck (r.stack.top() == FxUi::kPageList, "fx: type picker opened on UiStack");

    // Tap "EQ" (index 4 in kTypeNames: Chorus, Compressor, Delay, Distortion, EQ)
    const int oldWrites = r.inst.writes;
    const ui::Rect rowR = r.fx.list().rowRect (4);
    r.tapFx (rowR.x + rowR.w / 2, rowR.y + rowR.h / 2);

    testCheck (r.inst.writes > oldWrites, "fx: selecting EQ writes module configuration");
    testCheck (r.inst.get ("fxi01.type") == static_cast<float> (synthyboy::kFxTypeEq), "fx: fxi01.type set to EQ");
    testCheck (r.inst.get ("fxi01.on") == 1.0f, "fx: fxi01.on set to 1.0");

    r.updateFx();
    testCheck (r.fx.screen().chainBar.numActiveSlots() >= 1, "fx: chain bar has active card for EQ");
    testCheck (r.fx.screen().slotEditor.currentType() == synthyboy::kFxTypeEq, "fx: slot editor active for EQ");

    // Turn first EQ knob (Low Freq)
    const ui::Rect knob0Rect = r.fx.screen().slotEditor.knobs[0].rect();
    const int preKnobWrites = r.inst.writes;
    r.dragVerticalFx (knob0Rect.x + knob0Rect.w / 2, knob0Rect.bottom() - 10, knob0Rect.y + 10);
    testCheck (r.inst.writes > preKnobWrites, "fx: turning EQ knob writes parameter");
    const int fxi01p01Idx = synthyboy::fxInstanceKeys (0).p[0];
    testCheck (r.inst.lastWritten == fxi01p01Idx, "fx: knob writes fxi01.p01 parameter");
}

void testFxQGroups()
{
    Rig r;
    // Add EQ to slot 0
    std::vector<synthyboy::FxWrite> writes;
    synthyboy::fxAddModule (r.inst.v_.data(), synthyboy::kFxTypeEq, 0, writes);
    for (const auto& w : writes)
        r.inst.v_[w.param] = w.value;
    r.updateFx();

    ui::FxFullScreen& screen = r.fx.screen();
    ui::Component* leaves[256];
    const int numLeaves = screen.collectLeaves (leaves, 0, 256);

    // Group 0: Chain bar
    screen.setActiveGroup (0);
    const ui::Component* offender0 = nullptr;
    const int bad0 = screen.groups[0].check (leaves, numLeaves, &offender0);
    testCheck (bad0 == 0, "fx: Group 0 (Chain bar) has 0 offenders");

    // Groups 1..K: Slot editor knob rows
    const int numGroups = screen.numGroups();
    for (int g = 1; g < numGroups; ++g)
    {
        screen.setActiveGroup (g);
        const ui::Component* offender = nullptr;
        const int bad = screen.groups[g].check (leaves, numLeaves, &offender);
        testCheck (bad == 0, "fx: Group " + std::to_string (g) + " (knob row) has 0 offenders");
    }
}

void testFxDisplayKinds()
{
    Rig r;
    ui::FxDisplay& disp = r.fx.screen().slotEditor.display;

    disp.setKindFromType (synthyboy::kFxTypeEq);
    testCheck (disp.kind == ui::FxDisplay::Kind::Eq, "fx display: EQ kind set correctly");

    disp.setKindFromType (synthyboy::kFxTypeCompressor);
    testCheck (disp.kind == ui::FxDisplay::Kind::Compressor, "fx display: Compressor kind set correctly");

    disp.setKindFromType (synthyboy::kFxTypeDelay);
    testCheck (disp.kind == ui::FxDisplay::Kind::Delay, "fx display: Delay kind set correctly");

    disp.setKindFromType (synthyboy::kFxTypeFilter);
    testCheck (disp.kind == ui::FxDisplay::Kind::Filter, "fx display: Filter kind set correctly");

    disp.setKindFromType (synthyboy::kFxTypeDistortion);
    testCheck (disp.kind == ui::FxDisplay::Kind::Distortion, "fx display: Distortion kind set correctly");
}

// ===============================================================================================================================================================
// CPU RENDERING & IMAGE DUMPING
// ===============================================================================================================================================================

void renderDemoImages (const std::string& outDir)
{
    mkdir ("docs-tmp", 0755);
    mkdir (outDir.c_str(), 0755);
    Rig r;

    // 1. Mixer Full Screen
    r.inst.liveValues.stripPeak[0] = 0.65f;
    r.inst.liveValues.stripPeak[1] = 0.88f;
    r.inst.liveValues.stripPeak[9] = 0.92f;
    r.updateMixer();
    {
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.mixer.draw (s.builder());
        testCheck (! s.overflow(), "mixer scene fits within gpu max ops (" + std::to_string (s.numOps()) + " ops)");
        const bool ok = writePng (outDir + "/mixer_full.png", render (s));
        testCheck (ok, "saved mixer_full.png");
    }

    // 2. Mixer with Strip 1 (Osc A) Selected
    r.mixer.screen().setActiveGroup (1);
    r.updateMixer();
    {
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.mixer.draw (s.builder());
        const bool ok = writePng (outDir + "/mixer_strip_selected.png", render (s));
        testCheck (ok, "saved mixer_strip_selected.png");
    }

    // 3. Mixer Destination Popup
    {
        const ui::Rect btnRect = r.mixer.screen().strips[0].destButton.rect();
        r.tapMixer (btnRect.x + btnRect.w / 2, btnRect.y + btnRect.h / 2);
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.mixer.draw (s.builder());
        const bool ok = writePng (outDir + "/mixer_dest_popup.png", render (s));
        testCheck (ok, "saved mixer_dest_popup.png");
        r.tapMixer (10, 10); // close
    }

    // 4. FX View with EQ Module Added
    {
        std::vector<synthyboy::FxWrite> writes;
        synthyboy::fxAddModule (r.inst.v_.data(), synthyboy::kFxTypeEq, 0, writes);
        for (const auto& w : writes)
            r.inst.v_[w.param] = w.value;
        r.updateFx();

        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.fx.draw (s.builder());
        testCheck (! s.overflow(), "fx scene fits within gpu max ops (" + std::to_string (s.numOps()) + " ops)");
        const bool ok = writePng (outDir + "/fx_chain_full.png", render (s));
        testCheck (ok, "saved fx_chain_full.png");
        const bool okEq = writePng (outDir + "/fx_editor_eq.png", render (s));
        testCheck (okEq, "saved fx_editor_eq.png");
    }

    // 5. FX Editor Compressor
    {
        r.fx.screen().slotEditor.setType (synthyboy::kFxTypeCompressor);
        r.fx.screen().slotEditor.display.comp.gainReduction = 4.5f;
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.fx.draw (s.builder());
        const bool ok = writePng (outDir + "/fx_editor_comp.png", render (s));
        testCheck (ok, "saved fx_editor_comp.png");
    }

    // 6. FX Editor Delay
    {
        r.fx.screen().slotEditor.setType (synthyboy::kFxTypeDelay);
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.fx.draw (s.builder());
        const bool ok = writePng (outDir + "/fx_editor_delay.png", render (s));
        testCheck (ok, "saved fx_editor_delay.png");
    }

    // 7. FX Editor Filter
    {
        r.fx.screen().slotEditor.setType (synthyboy::kFxTypeFilter);
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.fx.draw (s.builder());
        const bool ok = writePng (outDir + "/fx_editor_filter.png", render (s));
        testCheck (ok, "saved fx_editor_filter.png");
    }

    // 8. FX Editor Distortion
    {
        r.fx.screen().slotEditor.setType (synthyboy::kFxTypeDistortion);
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.fx.draw (s.builder());
        const bool ok = writePng (outDir + "/fx_editor_distortion.png", render (s));
        testCheck (ok, "saved fx_editor_distortion.png");
    }

    // 9. FX Add Module Popup
    {
        const ui::Rect plusRect = r.fx.screen().chainBar.plusCard.rect();
        r.tapFx (plusRect.x + plusRect.w / 2, plusRect.y + plusRect.h / 2);
        Scene s (1280, 800, theme::rgba (theme::L().bg0, 255));
        r.fx.draw (s.builder());
        const bool ok = writePng (outDir + "/fx_add_popup.png", render (s));
        testCheck (ok, "saved fx_add_popup.png");
    }
}

} // namespace

int main (int argc, char** argv)
{
    std::printf ("=== Running MIXER and FX Overlay Tests ===\n");

    testMixerLayoutAndHitTesting();
    testMixerParameterWiring();
    testMixerDestinationPopup();
    testMixerQGroups();
    testMixerLivePeakMeters();

    testFxLayoutAndHitTesting();
    testFxAddModuleAndParameterWiring();
    testFxQGroups();
    testFxDisplayKinds();

    const std::string imgDir = (argc > 1) ? argv[1] : "docs-tmp/mixfx-images";
    std::printf ("=== Rendering CPU Reference Images into %s ===\n", imgDir.c_str());
    renderDemoImages (imgDir);

    std::printf (g_testFails ? "mixfx_ui_host: FAILED (%d)\n" : "mixfx_ui_host: OK\n", g_testFails);
    return g_testFails ? 1 : 0;
}
