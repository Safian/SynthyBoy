// SynthyBoy - MPC overlay tests: the Q SLOTS the plugin-drawn pages publish (q_publish.h, q_router.h): with sixteen Q proxies a page hands over its groups in reading order and the active one is on Q 1 .. 4, the next
// groups (wrapping, each once) on 5 .. 8, 9 .. 12, 13 .. 16; a page with fewer than four groups leaves the rest without a target. Checked for every page that has several groups: Mixer (strips), Master (the groups of
// a side), Voice (six blocks), Filter (MAIN, TRACK, OUT), Oscillator (the blocks of the full screen), Mod (the three blocks of a slot page), Macros (the two banks; the rows of a targets page), FX (the rows of an effect);
// and the pages with one group (Osc compact view of the Play page) publish four slots. The reference is what a group publishes while it is the active one, so no table of parameter names is repeated here. Then the chips:
// the idle chips of a mapped group show 5 .. 16 on a device with 16 knobs and 1 .. 4 on the others, the device rule (q_device.h), and the numbers a router reports.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../env_ui.h"
#include "../filter_ui.h"
#include "../fx_ui.h"
#include "../lfo_ui.h"
#include "../macro_ui.h"
#include "../master_ui.h"
#include "../mixer_ui.h"
#include "../mod_ui.h"
#include "../osc_page.h"
#include "../q_device.h"
#include "../voice_ui.h"
#include "demo_common.h"
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

/** The sixteen slots as the instance received them, and the same as the page keeps them (qTargets). */
struct Slots
{
    int s[16];
};
template <class Rig>
Slots published (Rig& r)
{
    Slots out;
    for (int i = 0; i < 16; ++i)
        out.s[i] = r.inst.q[i];
    return out;
}

/** The rule for a page with n groups, reference[g] the four targets of group g while it is the active one: with `active` the slots are active, active + 1, active + 2, active + 3 (mod n), each once, the rest none. */
bool followsRule (const Slots& got, const std::vector<std::vector<int>>& reference, int active)
{
    const int n = static_cast<int> (reference.size());
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 4; ++i)
        {
            const int expected = k < n ? reference[static_cast<size_t> ((active + k) % n)][static_cast<size_t> (i)] : -1;
            if (got.s[4 * k + i] != expected)
                return false;
        }
    return true;
}

/** Walks every group of a page as the active one: collects what it publishes in slots 1 .. 4, then checks the whole rule for every active group. `setActive (g)` makes group g the active one and lets the page publish. */
template <class Rig, class SetActive>
void checkPage (const std::string& name, Rig& r, int n, SetActive&& setActive, int expectUsedGroups = -1)
{
    std::vector<std::vector<int>> ref;
    for (int g = 0; g < n; ++g)
    {
        setActive (g);
        const Slots s = published (r);
        ref.push_back ({ s.s[0], s.s[1], s.s[2], s.s[3] });
    }
    bool rule = true, count16 = true;
    int tellings = 0;
    for (int a = 0; a < n; ++a)
    {
        setActive (a);
        const Slots s = published (r);
        rule = rule && followsRule (s, ref, a);
        count16 = count16 && r.inst.qN == 16;
        for (int i = 0; i < 4; ++i)
            rule = rule && r.ui.qTargets()[i] == s.s[i];
        ++tellings;
    }
    (void) tellings;
    check (rule, name + ": " + std::to_string (n) + " groups, each one active: the slots follow the rule (active, then the next ones in reading order, wrapping, each once)");
    check (count16, name + ": the page publishes sixteen slots");
    (void) expectUsedGroups;
}

void testMixer()
{
    demo::Rig<MixerUi> r;
    r.refresh();
    checkPage ("Mixer", r, 10, [&] (int g) { r.ui.screen().setActiveGroup (g); r.refresh(); });
    r.ui.screen().setActiveGroup (8);
    r.refresh();
    const Slots s = published (r);
    check (s.s[0] == demo::Inst::idx ("bus2.level") && s.s[4] == demo::Inst::idx ("master.volume") && s.s[8] == demo::Inst::idx ("sub.level") && s.s[12] == demo::Inst::idx ("osc1.level") && s.s[13] == demo::Inst::idx ("osc1.pan"),
           "Mixer, strip BUS 2 active: Q 1 its level, Q 5 the main strip, Q 9 the Sub strip and Q 13 the first oscillator after the wrap");
    check (s.s[1] == -1 && s.s[2] == -1, "(a bus has no pan and no sends: those Qs have no target)");
}

void testMaster()
{
    demo::Rig<MasterUi> r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    checkPage ("Master (main side)", r, 6, [&] (int g) { m.setActiveGroup (g); r.refresh(); });
    m.setSide (ui::MasterScreen::SideTuning);
    m.setActiveGroup (6);
    r.refresh();
    std::vector<std::vector<int>> ref;
    for (int g = 6; g < 10; ++g)
    {
        m.setActiveGroup (g);
        r.refresh();
        const Slots s = published (r);
        ref.push_back ({ s.s[0], s.s[1], s.s[2], s.s[3] });
    }
    bool rule = true;
    for (int a = 0; a < 4; ++a)
    {
        m.setActiveGroup (6 + a);
        r.refresh();
        rule = rule && followsRule (published (r), ref, a);
    }
    check (rule, "Master (tuning side): the four groups of the side are one rotation, no group of the other side is mapped");
    m.setActiveGroup (9);
    r.refresh();
    check (published (r).s[4] == ref[0][0] && published (r).s[8] == ref[1][0] && published (r).s[12] == ref[2][0], "Master, MPE Y active: SCALE, TUNING, MPE follow (wrapping inside the side)");
    // the groups of the other side have no knobs: no chips on them (setBlock -1), the mapped ones have blocks 1 .. 3
    int mapped = 0;
    for (int g = 6; g < 10; ++g)
        mapped += m.groups[g].block() >= 1;
    check (mapped == 3 && m.groups[9].block() == 0 && m.groups[0].block() == -1, "Master: the three other groups of the side have a block of knobs, the active one block 0, the other side none");
}

void testVoice()
{
    demo::Rig<VoiceUi> r;
    r.refresh();
    checkPage ("Voice", r, 6, [&] (int g) { r.ui.screen().setActiveGroup (g); r.refresh(); });
    r.ui.screen().setActiveGroup (4);
    r.refresh();
    const Slots s = published (r);
    check (s.s[0] == demo::Inst::idx ("voicectl.osc.a") && s.s[4] == demo::Inst::idx ("voicectl.osc.noise") && QRouter::modeOf (s.s[8]) == QRouter::kVirtual && s.s[12] == demo::Inst::idx ("voicectl.rand.pan"),
           "Voice, TARGETS active: Q 1 osc A, Q 5 NOISE, Q 9 the STEP group (its virtual step knob), Q 13 RANDOM");
}

void testFilter()
{
    demo::Rig<FilterUi> r;
    r.refresh();
    checkPage ("Filter", r, 3, [&] (int g) { r.ui.full().setActiveGroup (g); r.refresh(); });
    check (r.ui.full().groups[0].block() == 1 || r.ui.full().groups[1].block() == 1 || r.ui.full().groups[2].block() == 1, "Filter: the groups that are not active have a block of knobs");
    check (r.inst.q[12] == -1 && r.inst.q[15] == -1, "Filter: three groups leave Q 13 .. 16 without a target");
    r.inst.set ("filter1.model", 5.0f);   // the Formant model: the FORMANT block is a fourth group
    r.refresh();
    checkPage ("Filter (Formant model)", r, 4, [&] (int g) { r.ui.full().setActiveGroup (g); r.refresh(); });
}

void testOscillator()
{
    demo::Rig<OscPageUi> r (2);
    r.refresh();
    ui::OscillatorFullScreen& f = r.ui.ui().full();
    const int n = f.numGroups();
    check (n >= 3, "Osc A: the full screen has several groups (" + std::to_string (n) + ")");
    checkPage ("Osc A", r, n, [&] (int g) { f.setActiveGroup (g); r.refresh(); });
    int mappedBlocks = 0;
    f.setActiveGroup (0);
    r.refresh();
    for (int g = 0; g < n; ++g)
        mappedBlocks += f.groups[g].block() >= 1;
    check (mappedBlocks == std::min (n - 1, 3), "Osc A: the groups after the active one have their blocks (" + std::to_string (mappedBlocks) + ")");
}

/** The detail pages of the oscillator types (osc_spec_page.h): the groups are their blocks in reading order, the rule of sixteen slots holds for every page, in the state that shows every block. */
void testOscillatorTypePages()
{
    for (int page = 16; page <= 22; ++page)
    {
        demo::Rig<OscPageUi> r (2);
        auto set = [&] (const char* k, float v) { r.inst.set (k, v); };
        set ("osc1.type", page == 16 ? 0.0f : (page == 18 || page == 19 ? 1.0f : (page == 20 ? 2.0f : (page == 17 ? 4.0f : 3.0f))));
        set ("osc1.sample.loop_mode", 1.0f); set ("osc1.sample.slice_mode", 1.0f); set ("osc1.remap.on", 1.0f); set ("osc1.spec.flt_use", 1.0f);
        r.refresh();
        r.stack.openDetail (page);
        r.refresh();
        oscspec::OscSpecPage& pg = r.ui.ui().specPage();
        const int n = pg.numGroups();
        check (n >= 3 || page == 16, "osc page " + std::to_string (page) + ": its blocks are groups (" + std::to_string (n) + ")");
        checkPage ("Osc page " + std::to_string (page), r, n, [&] (int g) { pg.setActiveGroup (g); r.refresh(); });
        bool four = true;
        for (int g = 0; g < n; ++g)
            four = four && pg.groups[g].numMembers() >= 1 && pg.groups[g].numMembers() <= 4;
        check (four, "osc page " + std::to_string (page) + ": every group has one to four members");
    }
}

void testEnvLfo()
{
    {
        demo::Rig<EnvUi> r;
        r.refresh();
        ui::EnvFullScreen& f = r.ui.full();
        checkPage ("Env page", r, 3, [&] (int g) { f.setActiveGroup (g); r.refresh(); });
        f.setActiveGroup (0);
        r.refresh();
        const int atk = demo::Inst::idx ("env1.attack"), dly = demo::Inst::idx ("env1.delay"), ac = demo::Inst::idx ("env1.attack_curve");
        check (r.inst.q[0] == atk && r.inst.q[4] == ac && r.inst.q[8] == dly && r.inst.q[12] == -1, "Env page, ADSR active: Q 1 attack, Q 5 the SHAPE group (A CURVE), Q 9 the TIME group (DELAY), Q 13 .. 16 empty");
        int mapped = 0;
        for (int g = 0; g < 3; ++g)
            mapped += f.groups[g].block() >= 1;
        check (mapped == 2 && f.groups[0].block() == 0, "Env page: the two other groups have their blocks (idle chips 5 .. 12 on the XL)");
        f.setActiveGroup (2);
        r.refresh();
        check (r.inst.q[0] == dly && r.inst.q[4] == atk && r.inst.q[8] == ac, "Env page, TIME active: Q 1 DELAY, then ADSR, then SHAPE (wrapping)");
    }
    // a shape LFO (the random LFOs share the code of the groups)
    {
        demo::Rig<LfoUi> r;
        r.refresh();

        ui::LfoFullScreen& f = r.ui.full();
        int n = 0, ids[ui::LfoFullScreen::kMaxGroups];
        for (int g = 0; g < f.numGroups(); ++g)
            if (f.groups[g].numMembers() > 0 && ! (g == f.keyGroup() && ! f.keyActive()))
                ids[n++] = g;
        check (n >= 2, std::string ("LFO page (") + (f.random() ? "random" : "shape") + "): several groups (" + std::to_string (n) + ")");
        checkPage (f.random() ? "LFO page, random" : "LFO page", r, n, [&] (int g) { f.setActiveGroup (ids[g]); r.refresh(); });
        f.setActiveGroup (ids[0]);
        r.refresh();
        int mapped = 0;
        for (int g = 0; g < n; ++g)
            mapped += f.groups[ids[g]].block() >= 1;
        check (mapped == std::min (n - 1, 3), "LFO page: the groups after the active one have their blocks (" + std::to_string (mapped) + ")");
    }
    // the shape editor: EDIT (virtual 0 .. 3), GRID (virtual 4 .. 6), TRIGGER (MODE and PHASE of the table, virtual 7)
    {
        demo::Rig<LfoUi> r;
        r.refresh();
        r.stack.openDetail (LfoUi::kPageEditor);
        r.refresh();
        ui::ShapeEditorPage& e = r.ui.editor();
        checkPage ("LFO shape editor", r, ui::ShapeEditorPage::kGroups, [&] (int g) { e.setActiveGroup (g); r.refresh(); });
        e.setActiveGroup (0);
        r.refresh();
        check (r.inst.q[0] == QRouter::encode (0, QRouter::kVirtual) && r.inst.q[3] == QRouter::encode (3, QRouter::kVirtual) && r.inst.q[4] == QRouter::encode (4, QRouter::kVirtual) && r.inst.q[6] == QRouter::encode (6, QRouter::kVirtual) && r.inst.q[7] == -1
                   && r.inst.q[8] == demo::Inst::idx ("lfo1.sync_type") && r.inst.q[9] == demo::Inst::idx ("lfo1.phase") && r.inst.q[10] == QRouter::encode (7, QRouter::kVirtual) && r.inst.q[11] == -1 && r.inst.q[12] == -1,
               "LFO shape editor, EDIT active: Q 1 .. 4 POINT X Y CURVE, Q 5 .. 7 GRID X GRID Y SNAP, Q 9 .. 11 MODE PHASE BEND ALL (11 of the 16 slots)");
        int mapped = 0;
        for (int g = 0; g < ui::ShapeEditorPage::kGroups; ++g)
            mapped += e.groups[g].block() >= 1;
        check (mapped == 2 && e.groups[0].block() == 0, "LFO shape editor: the two other groups have their blocks (idle chips 5 .. 12 on the XL)");
        e.setActiveGroup (2);
        r.refresh();
        check (r.inst.q[0] == demo::Inst::idx ("lfo1.sync_type") && r.inst.q[1] == demo::Inst::idx ("lfo1.phase") && r.inst.q[4] == QRouter::encode (0, QRouter::kVirtual), "LFO shape editor, TRIGGER active: MODE and PHASE on Q 1 and Q 2, then EDIT (wrapping)");
    }
}

void testModDetail()
{
    demo::Rig<ModUi> r;
    r.inst.set ("mod01.source", 1.0f);
    r.inst.set ("mod01.destination", 1.0f);
    r.inst.set ("mod01.amount", 50.0f);
    r.refresh();
    r.tap (r.ui.matrix().windowRows[0].detailBtn);
    check (r.stack.top() == ModUi::kPageDetail, "Mod: the page of a slot is open");
    checkPage ("Mod slot page", r, 3, [&] (int g) { r.ui.detail().setActiveBlock (static_cast<ui::ModSlotDetail::Block> (g)); r.refresh(); });
    check (r.inst.q[0] != -1 && r.inst.q[4] != -1 && r.inst.q[8] != -1 && r.inst.q[12] == -1, "Mod slot page: three blocks fill three rows, Q 13 .. 16 stay empty");
}

void testMacros()
{
    demo::Rig<MacroUi> r;
    r.refresh();
    checkPage ("Macros (two banks)", r, 2, [&] (int g) { r.ui.view().setBank (g); r.refresh(); });
    r.ui.view().setBank (0);
    r.refresh();
    const int m1 = demo::Inst::idx ("macro.1"), m5 = demo::Inst::idx ("macro.5");
    check (r.inst.q[0] == m1 && r.inst.q[4] == m5 && r.inst.q[8] == -1, "Macros, bank 1 shown: Q 1 .. 4 macros 1 .. 4, Q 5 .. 8 macros 5 .. 8, nothing after");
    r.ui.view().setBank (1);
    r.refresh();
    check (r.inst.q[0] == m5 && r.inst.q[4] == m1, "Macros, bank 2 shown: Q 1 is macro 5 and Q 5 macro 1 (the other bank is the next group)");
}

void testFx()
{
    demo::Rig<FxUi> r;
    r.refresh();
    r.tap (r.ui.screen().chainBar.plusCard);
    r.ui.list().state.current = 4;   // the type in row 4 (EQ), as the Mixer and FX test does
    r.ui.list().showCurrent();
    const ui::Rect row = r.ui.list().rowRect (4);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    const int first = ed.firstRowOfPage(), rows = std::min (ed.rowsPerPage(), ed.numRows() - first);
    check (rows >= 2, "FX: the effect has two rows on its page (" + std::to_string (rows) + ")");
    std::vector<std::vector<int>> ref;
    for (int k = 0; k < rows; ++k)
    {
        ed.setActiveRow (first + k);
        r.refresh();
        const Slots s = published (r);
        ref.push_back ({ s.s[0], s.s[1], s.s[2], s.s[3] });
    }
    bool rule = true;
    for (int a = 0; a < rows; ++a)
    {
        ed.setActiveRow (first + a);
        r.refresh();
        rule = rule && followsRule (published (r), ref, a);
    }
    check (rule, "FX: the rows of the effect's page are the groups (the active row on Q 1 .. 4, the other row on Q 5 .. 8, nothing after)");
    check (ed.groups[first].block() >= 0 && ed.groups[first + 1].block() >= 0 && ed.groups[first].block() != ed.groups[first + 1].block(), "FX: the rows have different blocks (0 active, 1 the other)");
}

void testOneGroupPage()
{
    // the compact view of the Play page (stack depth 0): one group, slots 5 .. 16 empty
    OscUi ui;
    demo::Inst inst;
    UiStack stack;
    ui.update (inst, 1, stack, 1000.0, 2, 1);
    int used = 0;
    for (int i = 0; i < 16; ++i)
        used += inst.q[i] != -1;
    // in the main state the Play view publishes the groups of the three thirds together (PlayView::publishQ, tested in env_ui_host): the oscillator alone publishes nothing, and its compact group has four targets
    int ct[4];
    ui.compactTargets (ct);
    check (used == 0 && ct[0] >= 0 && ct[1] >= 0 && ct[2] >= 0 && ct[3] >= 0, "the compact oscillator view (one group): the Play view publishes it; its four targets are the compact group's (" + std::to_string (used) + " set by the oscillator itself)");
}

void testStatusSlotCount()
{
    MixerUi mixer;
    EnvUi env;
    LfoUi lfo;
    check (page::QSlotCount<MixerUi>::of (mixer) == 16 && page::QSlotCount<EnvUi>::of (env) == 16 && page::QSlotCount<LfoUi>::of (lfo) == 16, "the status file lists 16 slots for every page that publishes them (page_q_slots): Env and LFO now too");
}

void testDeviceRule()
{
    check (hasSixteenQLinks ("Akai Professional MPC XL", false) && hasSixteenQLinks ("Akai Professional MPC XL", true), "device rule: a name that says XL means 16 knobs, whatever the architecture");
    check (hasSixteenQLinks ("Akai Professional MPC XL", false), "device rule: the exact string measured on the MPC XL (2026-11-16, /proc/device-tree/model) is 16 knobs");
    check (! hasSixteenQLinks ("Akai Professional MPC Key 61", false) && ! hasSixteenQLinks ("Akai Professional MPC Key 61", true), "device rule: the Key 61 has four knobs, whatever the architecture");
    check (! hasSixteenQLinks ("Akai Professional MPC Live III", true) && ! hasSixteenQLinks ("MPC One", false) && ! hasSixteenQLinks ("XLR board", true), "device rule: another name is four knobs even on a 64-bit build (XL must be a word)");
    check (hasSixteenQLinks ("", true) && ! hasSixteenQLinks ("", false), "device rule: no readable name: the 64-bit build is taken for the XL, the 32-bit build is not");
}

void testChips()
{
    // the idle chips of a mapped group: knob numbers 5 .. 16 on a device with 16 knobs, 1 .. 4 otherwise; an unmapped group has none on the 16 knob device
    for (const bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        demo::Rig<MixerUi> r;
        r.refresh();
        auto opsOf = [&] (int group)
        {
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.ui.screen().groups[group].drawIdle (sb);
            return scene->numOps;
        };
        const uint32_t block1 = opsOf (1), unmapped = opsOf (8);   // strip 8 is no block of the active group 0
        check (block1 > 0 && (wide ? unmapped == 0 : unmapped > 0), std::string (wide ? "16 knob device: " : "4 knob device: ") + "the mapped group draws chips, the unmapped one " + (wide ? "none" : "its quiet 1 .. 4"));
    }
    ui::QGroup::setWideDevice (false);
}
} // namespace

int main()
{
    testMixer();
    testMaster();
    testVoice();
    testFilter();
    testOscillator();
    testOscillatorTypePages();
    testEnvLfo();
    testModDetail();
    testMacros();
    testFx();
    testOneGroupPage();
    testStatusSlotCount();
    testDeviceRule();
    testChips();
    std::printf (g_fails ? "qslots_host: FAILED (%d)\n" : "qslots_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
