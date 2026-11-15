// SynthyBoy - MPC overlay tests: Master and Voice UI (master_ui.h, voice_ui.h).
// Parameter mapping of all 33 master and 62 voice parameters, bidirectional value conversions,
// touch interactions, drag drawing, Q-Link groups, ListPopup, and scene rendering.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "../master_ui.h"
#include "../voice_ui.h"
#include "../scene_cpu.h"
#include "../touch_decoder.h"

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
        return (i >= 0 && i < static_cast<int> (synthyboy::kMaxParams)) ? v_[i] : 0.0f;
    }

    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        out = liveVal;
        return true;
    }

    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return 1; }

    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 4; ++i)
            q[i] = (i < n) ? t[i] : -1;
        ++qCalls;
    }

    void writeParam (int i, float norm) noexcept override
    {
        if (i < 0 || i >= static_cast<int> (synthyboy::paramCount()))
            return;
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }

    void setPlaySelection (int, int) noexcept override {}
    int playLastSelector() const noexcept override { return 0; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }

    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }

    synthyboy::LiveValues liveVal;
    int q[4] = { -1, -1, -1, -1 };
    int qCalls = 0;
    int writes = 0;
    int lastWritten = -1;
    bool fullOpen = false;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y)
{
    TouchEvent e;
    e.kind = k;
    e.x = x;
    e.y = y;
    return e;
}

// -------------------------------------------------------------------------------------------------------------------------------
// MASTER UI TESTS
// -------------------------------------------------------------------------------------------------------------------------------

void testMasterParameterMapping()
{
    MasterUi ui;
    FakeInst inst;
    UiStack stack;
    ui.update (inst, 1, stack, 1000.0);

    // Verify all 33 master parameters
    static const char* const kExpectedKeys[] = {
        "master.volume", "master.polyphony", "master.bend_range", "master.transpose",
        "master.tune", "master.legato", "master.portamento.time", "master.portamento.slope",
        "master.portamento.always", "master.portamento.scaled", "master.scale.key",
        "master.scale.type", "master.scale.mode", "master.tuning.a4", "master.tuning.scale",
        "master.tuning.mts_esp", "master.tuning.mts_note_on_only", "master.mpe.on",
        "master.mpe.bend_range", "voice.phase_legato", "master.voice.same_note",
        "master.voice.steal_priority", "master.poly_legato", "master.bend_down_range",
        "master.bend_down_link", "master.stereo_spread", "master.stereo_routing",
        "master.voice_override", "master.mpe.xyz_macro", "master.mpe.y_modwheel",
        "master.mpe.bend_to_x", "master.mpe.y_bipolar"
    };

    int unbound = 0;
    for (const char* key : kExpectedKeys)
    {
        const int p = FakeInst::idx (key);
        if (p < 0)
        {
            ++unbound;
            std::printf ("Missing engine param: %s\n", key);
        }
    }
    check (unbound == 0, "master: all required master parameters exist in engine table");

    // Check that every item in MasterUi has a valid parameter or is inert
    int unmapped = 0;
    for (const MasterUi::Item& it : ui.items())
    {
        if (it.type != MasterUi::T::Inert && it.param < 0)
        {
            ++unmapped;
            std::printf ("Unmapped item: %s (id %d)\n", it.key.c_str(), it.id);
        }
    }
    check (unmapped == 0, "master: every control in MasterUi is bound to a valid engine parameter");
}

void testMasterReadWrite()
{
    MasterUi ui;
    FakeInst inst;
    UiStack stack;

    // Test writing volume
    inst.set ("master.volume", -12.0f);
    ui.update (inst, 1, stack, 1000.0);
    check (std::strstr (ui.screen().volume.state.valueText, "dB") != nullptr, "master: volume knob formats dB value");

    // Touch volume knob
    const ui::Rect vr = ui.screen().volume.rect();
    ui.touch (ev (TouchEvent::Down, vr.x + vr.w / 2, vr.y + vr.h / 2), stack, inst, 1010.0);
    ui.touch (ev (TouchEvent::Move, vr.x + vr.w / 2, vr.y + 10), stack, inst, 1020.0);
    ui.touch (ev (TouchEvent::Up, vr.x + vr.w / 2, vr.y + 10), stack, inst, 1030.0);
    check (inst.writes > 0 && inst.lastWritten == FakeInst::idx ("master.volume"), "master: volume knob writes to engine");

    // Test toggle legato
    const ui::Rect lr = ui.screen().legato.rect();
    const int legIdx = FakeInst::idx ("master.legato");
    inst.set ("master.legato", 0.0f);
    ui.update (inst, 1, stack, 1040.0);
    ui.touch (ev (TouchEvent::Down, lr.x + lr.w / 2, lr.y + lr.h / 2), stack, inst, 1050.0);
    ui.touch (ev (TouchEvent::Up, lr.x + lr.w / 2, lr.y + lr.h / 2), stack, inst, 1060.0);
    check (inst.lastWritten == legIdx && inst.get ("master.legato") == 1.0f, "master: legato toggle writes to engine");

    // Test mini keyboard scale check
    check (ui::MiniKeyboardDisplay::isScaleNote (1, 0, 0), "master: C is in C Major scale");
    check (! ui::MiniKeyboardDisplay::isScaleNote (1, 0, 1), "master: C# is not in C Major scale");
    check (ui::MiniKeyboardDisplay::isScaleNote (1, 0, 4), "master: E is in C Major scale");
}

void testMasterQGroups()
{
    MasterUi ui;
    FakeInst inst;
    UiStack stack;
    ui.update (inst, 1, stack, 1000.0);

    check (ui.screen().numGroups() == 6, "master: has 6 Q-Link groups");

    // Switch to Pitch group (block 1)
    const ui::Rect pr = ui.screen().blockRect (1);
    ui.touch (ev (TouchEvent::Down, pr.x + 20, pr.y + 10), stack, inst, 1010.0);
    ui.touch (ev (TouchEvent::Up, pr.x + 20, pr.y + 10), stack, inst, 1020.0);
    ui.update (inst, 1, stack, 1030.0);

    check (ui.activeQ() == 1, "master: tapping pitch block activates Pitch QGroup");
    check (inst.q[0] == FakeInst::idx ("master.tune") && inst.q[1] == FakeInst::idx ("master.transpose"),
           "master: pitch QGroup maps tune and transpose to Q1 and Q2");
}

void testMasterListPopup()
{
    MasterUi ui;
    FakeInst inst;
    UiStack stack;
    stack.openFull (MasterUi::kElementMaster);
    ui.update (inst, 1, stack, 1000.0);

    // Tap scale type stepper to open list popup
    const ui::Rect st = ui.screen().scaleType.rect();
    ui.touch (ev (TouchEvent::Down, st.x + st.w / 2, st.y + st.h / 2), stack, inst, 1010.0);
    ui.touch (ev (TouchEvent::Up, st.x + st.w / 2, st.y + st.h / 2), stack, inst, 1020.0);
    ui.update (inst, 1, stack, 1030.0);

    check (stack.top() == MasterUi::kPageList, "master: tapping scale type opens ListPopup");
    check (ui.list().state.count == 20, "master: list popup contains all 20 scale choices");

    // Tap row 1 ("Major")
    const ui::Rect r1 = ui.list().rowRect (1);
    ui.touch (ev (TouchEvent::Down, r1.x + r1.w / 2, r1.y + r1.h / 2), stack, inst, 1040.0);
    ui.touch (ev (TouchEvent::Up, r1.x + r1.w / 2, r1.y + r1.h / 2), stack, inst, 1050.0);
    ui.update (inst, 1, stack, 1060.0);

    check (stack.top() != MasterUi::kPageList, "master: selecting item closes ListPopup");
    check (inst.get ("master.scale.type") == 1.0f, "master: selecting Major writes master.scale.type = 1");
}

// -------------------------------------------------------------------------------------------------------------------------------
// VOICE UI TESTS
// -------------------------------------------------------------------------------------------------------------------------------

void testVoiceParameterMapping()
{
    VoiceUi ui;
    FakeInst inst;
    UiStack stack;
    ui.update (inst, 1, stack, 1000.0);

    // Verify all 48 step parameters (8 steps x 6 rows)
    static const char* const kRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
    int missingSteps = 0;
    for (int s = 1; s <= 8; ++s)
    {
        for (int r = 0; r < 6; ++r)
        {
            const std::string key = "voicectl.s" + std::to_string (s) + "." + kRows[r];
            if (FakeInst::idx (key) < 0)
            {
                ++missingSteps;
                std::printf ("Missing step param: %s\n", key.c_str());
            }
        }
    }
    check (missingSteps == 0, "voice: all 48 step parameters exist in engine table");

    // Verify random, scaling, targets, and length (14 parameters)
    static const char* const kMiscKeys[] = {
        "voicectl.length", "voicectl.rand.pan", "voicectl.rand.detune", "voicectl.rand.cutoff",
        "voicectl.rand.env", "voicectl.rand.detune10x", "voicectl.scale.env", "voicectl.scale.lfo",
        "voicectl.scale.lfo_mode", "voicectl.osc.a", "voicectl.osc.b", "voicectl.osc.c",
        "voicectl.osc.sub", "voicectl.osc.noise"
    };

    int missingMisc = 0;
    for (const char* key : kMiscKeys)
    {
        if (FakeInst::idx (key) < 0)
        {
            ++missingMisc;
            std::printf ("Missing misc param: %s\n", key);
        }
    }
    check (missingMisc == 0, "voice: all 14 voicectl control parameters exist in engine table");

    // Check that every item in VoiceUi is bound to a valid engine parameter
    int unmapped = 0;
    for (const VoiceUi::Item& it : ui.items())
    {
        if (it.type != VoiceUi::T::Inert && it.param < 0)
        {
            ++unmapped;
            std::printf ("Unmapped voice item: %s (id %d)\n", it.key.c_str(), it.id);
        }
    }
    check (unmapped == 0, "voice: every control in VoiceUi is bound to a valid engine parameter");
}

void testVoiceGridReadWriteAndDrag()
{
    VoiceUi ui;
    FakeInst inst;
    UiStack stack;

    // Set step 1 pan to +50% and step 2 pan to -30%
    inst.set ("voicectl.s1.pan", 50.0f);
    inst.set ("voicectl.s2.pan", -30.0f);
    ui.update (inst, 1, stack, 1000.0);

    check (std::fabs (ui.grid().value (0, 0) - 0.5f) < 0.02f, "voice: grid step 1 pan correctly reflects engine value (+50%)");
    check (std::fabs (ui.grid().value (0, 1) - (-0.3f)) < 0.02f, "voice: grid step 2 pan correctly reflects engine value (-30%)");

    // Test row tab switching: tap tab 1 (DETUNE)
    const ui::Rect tab1 = ui.grid().tabRect (1);
    ui.touch (ev (TouchEvent::Down, tab1.x + tab1.w / 2, tab1.y + tab1.h / 2), stack, inst, 1010.0);
    ui.touch (ev (TouchEvent::Up, tab1.x + tab1.w / 2, tab1.y + tab1.h / 2), stack, inst, 1020.0);
    ui.update (inst, 1, stack, 1030.0);
    check (ui.grid().activeRow == 1, "voice: tapping row tab 1 activates DETUNE row");

    // Test dragging across step columns 0 and 1
    const ui::Rect col0 = ui.grid().columnRect (0);
    const ui::Rect col1 = ui.grid().columnRect (1);
    const int topY = col0.y + 40; // near top = high positive value

    ui.touch (ev (TouchEvent::Down, col0.x + col0.w / 2, topY), stack, inst, 1040.0);
    check (inst.writes > 0 && inst.lastWritten == FakeInst::idx ("voicectl.s1.detune"), "voice: touching col 0 writes voicectl.s1.detune");

    // Drag to col 1
    ui.touch (ev (TouchEvent::Move, col1.x + col1.w / 2, topY + 20), stack, inst, 1050.0);
    check (inst.lastWritten == FakeInst::idx ("voicectl.s2.detune"), "voice: dragging across to col 1 writes voicectl.s2.detune");

    ui.touch (ev (TouchEvent::Up, col1.x + col1.w / 2, topY + 20), stack, inst, 1060.0);
}

void testVoiceQGroupsAndLive()
{
    VoiceUi ui;
    FakeInst inst;
    UiStack stack;

    // Simulate active voice sounding
    inst.liveVal.activeVoices = 3;
    ui.update (inst, 1, stack, 1000.0);

    check (ui.grid().voiceStepMask != 0, "voice: active sounding voice sets voiceStepMask on grid");

    // Turn Q knob in Step group
    ui.screen().setActiveGroup (0);
    ui.grid().selectedStep = 0;
    ui.grid().activeRow = 0;
    const int prevWrites = inst.writes;
    ui.turnQ (0, 1.0, stack, inst);
    check (inst.writes > prevWrites && inst.lastWritten == FakeInst::idx ("voicectl.s1.pan"),
           "voice: turning Q1 in Step group writes active step parameter");

    // Switch to Random group
    const ui::Rect randBlock = ui.screen().blockRect (0);
    ui.touch (ev (TouchEvent::Down, randBlock.x + 20, randBlock.y + 10), stack, inst, 1010.0);
    ui.touch (ev (TouchEvent::Up, randBlock.x + 20, randBlock.y + 10), stack, inst, 1020.0);
    ui.update (inst, 1, stack, 1030.0);

    check (ui.activeQ() == 1, "voice: tapping Random block activates Random QGroup");
    check (inst.q[0] == FakeInst::idx ("voicectl.rand.pan"), "voice: Random QGroup routes Q1 to voicectl.rand.pan");
}

void testSceneOpsAndTouchTargets()
{
    MasterUi mUi;
    VoiceUi vUi;
    FakeInst inst;
    UiStack stack;

    mUi.update (inst, 1, stack, 1000.0);
    vUi.update (inst, 1, stack, 1000.0);

    gpu::SceneData mData, vData;
    gpu::SceneBuilder mSb (mData);
    mSb.begin (gpu::kKindPlay, 0, 1280, 800);
    mUi.draw (mSb);
    check (mData.numOps > 0 && mData.numOps < gpu::kMaxOps, "master: scene drawing fits in budget (" + std::to_string (mData.numOps) + " ops)");

    gpu::SceneBuilder vSb (vData);
    vSb.begin (gpu::kKindPlay, 0, 1280, 800);
    vUi.draw (vSb);
    check (vData.numOps > 0 && vData.numOps < gpu::kMaxOps, "voice: scene drawing fits in budget (" + std::to_string (vData.numOps) + " ops)");

    // Touch targets check: all interactive controls must have rect >= 56px in either dimension or standard chip sizes
    check (mUi.screen().close.rect().h >= 56 && mUi.screen().close.rect().w >= 56, "master: close touch target >= 56px");
    check (vUi.screen().close.rect().h >= 56 && vUi.screen().close.rect().w >= 56, "voice: close touch target >= 56px");
    check (vUi.grid().columnRect (0).w >= 56, "voice: step column width >= 56px");
}

} // namespace

int main()
{
    std::printf ("=== Running Master & Voice UI Host Tests ===\n");
    testMasterParameterMapping();
    testMasterReadWrite();
    testMasterQGroups();
    testMasterListPopup();
    testVoiceParameterMapping();
    testVoiceGridReadWriteAndDrag();
    testVoiceQGroupsAndLive();
    testSceneOpsAndTouchTargets();

    std::printf ("\nmastervoice_ui_host: %s (%d failures)\n", g_fails == 0 ? "OK" : "FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
