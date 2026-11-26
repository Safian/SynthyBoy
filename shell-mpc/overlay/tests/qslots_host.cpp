// SynthyBoy - MPC overlay tests: the Q SLOTS the plugin-drawn pages publish (q_publish.h, q_router.h): with sixteen Q proxies a page hands over its groups in reading order and the active one is on Q 1 .. 4, the next
// groups (wrapping, each once) on 5 .. 8, 9 .. 12, 13 .. 16; a page with fewer than four groups leaves the rest without a target. Checked for every page that has several groups: Mixer (strips), Master (the groups of
// a side), Voice (six blocks), Filter (MAIN, TRACK, OUT), Oscillator (the blocks of the full screen), Mod (the three blocks of a slot page), Macros (the two banks; the rows of a targets page), FX (the rows of an effect);
// and the pages with one group (Osc compact view of the Play page) publish four slots. The reference is what a group publishes while it is the active one, so no table of parameter names is repeated here. Then the chips:
// the idle chips of a mapped group show 5 .. 16 on a device with 16 knobs and 1 .. 4 on the others, the device rule (q_device.h), and the numbers a router reports.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
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
#include "../pages.h"
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

/** The key of a published slot: "" none, "virt:N" a virtual target, else the parameter key. */
std::string keyOf (int code)
{
    if (code < 0)
        return "";
    if (QRouter::modeOf (code) == QRouter::kVirtual)
        return "virt:" + std::to_string (QRouter::indexOf (code));
    return synthyboy::paramTable()[QRouter::indexOf (code)].key;
}

using Quad = std::vector<std::string>;   // the four keys of a group, "" where the slot has no target

/** Drive the same factory, stack and event entry points as the standalone tab. */
template <class Ui, class Instance = demo::Inst>
struct RoutedPage
{
    std::unique_ptr<IView> view;
    Ui& ui;
    Instance inst;
    UiStack stack;
    double now = 1000.0;

    RoutedPage (const char* name, int kind, int index = 1)
        : view (pages::make (name, kind, index)), ui (static_cast<PageView<Ui>&> (*view).ui())
    {
        stack.openFull (view->pageElement());
        view->onShown();
    }

    void refresh()
    {
        ViewInput in;
        in.io = &inst;
        in.ui = &stack;
        in.nowMs = now += 150.0;
        view->update (in);
    }

    void tap (const ui::Component& c)
    {
        const ui::Rect r = c.rect();
        TouchEvent e;
        e.x = r.x + r.w / 2;
        e.y = r.y + r.h / 2;
        e.kind = TouchEvent::Down;
        view->onTouch (e, stack, inst, now);
        e.kind = TouchEvent::Up;
        view->onTouch (e, stack, inst, now + 40.0);
        refresh();
    }
};

/** The published slot lists of a page against its groups written out by key, on BOTH devices: the XL (16 knobs, no active group: the groups of a page that fits stay on their knobs, group k on Q 4k+1 .. 4k+4) and the Key 61
    (four knobs and a bank button: the active group is on Q 1 .. 4, the next groups follow wrapping, bank n = Q 4n-3 .. 4n of the same list). `groups` are in reading order; `setActive (k)` makes group k the active one and lets the page publish.
    `groupsFit` false (more groups than four): only the Key 61 rule is checked (the XL pages them by touch). */
template <class Rig, class SetActive>
void expectGroups (const std::string& name, Rig& r, const std::vector<Quad>& compactGroups, SetActive&& setActive, int activeCount = -1, const std::vector<Quad>& wideGroups = {})
{
    const int n = static_cast<int> (compactGroups.size());
    const int actives = activeCount < 0 ? n : activeCount;   // a page that has no active group of its own (the Mod matrix) passes 1
    for (const bool wide : { true, false })
    {
        ui::QGroup::setWideDevice (wide);
        const auto& groups = wide && ! wideGroups.empty() ? wideGroups : compactGroups;
        std::string why;
        for (int a = 0; a < actives && why.empty(); ++a)
        {
            setActive (a);
            for (int k = 0; k < 4 && why.empty(); ++k)
                for (int i = 0; i < 4 && why.empty(); ++i)
                {
                    const int g = wide && n <= 4 ? k : (a + k) % n;   // the XL keeps a page of up to four groups where it is; more groups are paged like on the Key 61
                    const std::string want = (k < n && i < static_cast<int> (groups[static_cast<size_t> (g)].size())) ? groups[static_cast<size_t> (g)][static_cast<size_t> (i)] : "";
                    const std::string got = keyOf (r.inst.q[4 * k + i]);
                    if (got != want)
                        why = " (active " + std::to_string (a) + ", Q " + std::to_string (4 * k + i + 1) + ": got '" + got + "', wanted '" + want + "')";
                }
        }
        check (why.empty(), name + ", " + (wide ? (n <= 4 ? "XL (16 knobs, no active group)" : "XL (more than four groups: paged)") : "Key 61 (four knobs, the active group first, banks 1 .. 4)") + ": the published slot lists are the groups " + why);
    }
    ui::QGroup::setWideDevice (false);
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
    for (const char* key : { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on", "filter1.on", "filter2.on" })
        r.inst.set (key, 1.0f);
    r.refresh();
    // the LEVELS layout: [SUB, A, B, C], [NOISE, F1, F2, MAIN], [BUS 1, BUS 2]; the strips keep PAN and the sends on the touch screen only
    expectGroups ("Mixer", r, { { "sub.level", "osc1.level", "osc2.level", "osc3.level" }, { "noise.level", "filter1.level", "filter2.level", "master.volume" }, { "bus1.level", "bus2.level", "", "" } },
                  [&] (int g) { r.ui.screen().setActiveGroup (g); r.refresh(); }, -1,
                  { { "sub.level", "osc1.level", "osc2.level", "osc3.level" }, { "noise.level", "filter1.level", "filter2.level", "bus1.level" }, { "bus2.level", "master.volume" } });
    checkPage ("Mixer", r, 3, [&] (int g) { r.ui.screen().setActiveGroup (g); r.refresh(); });
    {   // the chips follow the Q slot numbers: XL block k = Q 4k+1 .. (the levels of SUB..C are Q 1 .. 4, NOISE..MAIN 5 .. 8, the buses 9 and 10); Key 61, MAIN's group active: its block 0, the buses 1, SUB..C 2 (relative to the bank)
        ui::MixerFullScreen& m = r.ui.screen();
        ui::QGroup::setWideDevice (true);
        m.selectStrip (6);   // F2 is in the second bank on both devices
        r.refresh();
        check (m.groups[0].block() == 0 && m.groups[1].block() == 1 && m.groups[2].block() == 2, "Mixer, XL: the groups stay on blocks 0, 1, 2 (chips 1 .. 4, 5 .. 8, 9 .. 10) whatever strip was touched");
        const char* order[] = { "sub.level", "osc1.level", "osc2.level", "osc3.level", "noise.level",
                                "filter1.level", "filter2.level", "bus1.level", "bus2.level", "master.volume" };
        for (int q = 0; q < 10; ++q)
            check (keyOf (r.inst.q[q]) == order[q], "Mixer XL Q" + std::to_string (q + 1) + " follows strip order");
        ui::QGroup::setWideDevice (false);
        r.refresh();
        check (m.groups[1].block() == 0 && m.groups[2].block() == 1 && m.groups[0].block() == 2, "Mixer, Key 61: the active group is block 0, the next group block 1, wrapping");
        m.selectStrip (9);
        ui::QGroup::setWideDevice (true);
        r.refresh();
        check (m.activeGroup() == 2 && ! m.groups[1].frameless() && std::string (r.ui.qGroupName()) == "LEVELS BUS2 MAIN",
               "device detection after construction moves the selected MAIN fader and its label to the XL bank");
        ui::QGroup::setWideDevice (false);
        r.refresh();
        check (m.activeGroup() == 1 && m.groups[1].frameless() && std::string (r.ui.qGroupName()) == "LEVELS NOISE F1 F2 MAIN",
               "four-knob routing restores the compact MAIN bank and its frame policy");
        m.setActiveGroup (0);
        r.refresh();
    }
    bool noPanOrSend = true;
    for (int i = 0; i < 16; ++i)
    {
        const std::string k = keyOf (r.inst.q[i]);
        noPanOrSend = noPanOrSend && k.find (".pan") == std::string::npos && k.find ("send") == std::string::npos;
    }
    check (noPanOrSend, "Mixer: no PAN and no bus send is a Q target (touch only)");
}

void testMaster()
{
    demo::Rig<MasterUi> r;
    r.refresh();
    ui::MasterScreen& m = r.ui.screen();
    // MAIN: OUTPUT, PITCH (LINK on: three members), VOICES, GLIDE; VOICE OPTS (group 3) has no members: touch only, GLIDE took its place (owner may reverse)
    static const int kMain[4] = { 0, 1, 2, 4 };
    expectGroups ("Master (main side)", r,
                  { { "master.volume", "master.stereo_spread", "master.stereo_routing", "master.voice_override" }, { "master.tune", "master.transpose", "master.bend_range", "" },
                    { "master.polyphony", "master.legato", "master.poly_legato", "voice.phase_legato" }, { "master.portamento.time", "master.portamento.slope", "master.portamento.always", "master.portamento.scaled" } },
                  [&] (int g) { m.setActiveGroup (kMain[g]); r.refresh(); });
    check (m.groups[3].numMembers() == 0 && m.groups[3].block() == -1, "Master: VOICE OPTS has no Q members and no block of knobs");
    checkPage ("Master (main side)", r, 4, [&] (int g) { m.setActiveGroup (kMain[g]); r.refresh(); });
    m.setSide (ui::MasterScreen::SideTuning);
    r.in.instance = 2;   // the hardware sub-tab Tuning
    m.setActiveGroup (5);
    r.refresh();
    std::vector<std::vector<int>> ref;
    for (int g = 5; g < 9; ++g)
    {
        m.setActiveGroup (g);
        r.refresh();
        const Slots s = published (r);
        ref.push_back ({ s.s[0], s.s[1], s.s[2], s.s[3] });
    }
    bool rule = true;
    for (int a = 0; a < 4; ++a)
    {
        m.setActiveGroup (5 + a);
        r.refresh();
        rule = rule && followsRule (published (r), ref, a);
    }
    check (rule, "Master (tuning side): the four groups of the side are one rotation, no group of the other side is mapped");
    m.setActiveGroup (8);
    r.refresh();
    check (published (r).s[4] == ref[0][0] && published (r).s[8] == ref[1][0] && published (r).s[12] == ref[2][0], "Master, MPE Y active: SCALE, TUNING, MPE follow (wrapping inside the side)");
    // the groups of the other side have no knobs: no chips on them (setBlock -1), the mapped ones have blocks 1 .. 3
    int mapped = 0;
    for (int g = 5; g < 9; ++g)
        mapped += m.groups[g].block() >= 1;
    check (mapped == 3 && m.groups[8].block() == 0 && m.groups[0].block() == -1, "Master: the three other groups of the side have a block of knobs, the active one block 0, the other side none");
}

void testVoice()
{
    demo::Rig<VoiceUi> r;
    r.refresh();
    // four groups = the four banks of the Key 61 and the 16 knobs of the XL at once; NOISE (the 17th value) is touch only
    expectGroups ("Voice", r,
                  { { "virt:0", "virt:1", "voicectl.length", "voicectl.s1.pan" }, { "voicectl.rand.pan", "voicectl.rand.detune", "voicectl.rand.cutoff", "voicectl.rand.env" },
                    { "voicectl.rand.detune10x", "voicectl.scale.env", "voicectl.scale.lfo", "voicectl.scale.lfo_mode" }, { "voicectl.osc.a", "voicectl.osc.b", "voicectl.osc.c", "voicectl.osc.sub" } },
                  [&] (int g) { r.ui.screen().setActiveGroup (g); r.refresh(); });
    checkPage ("Voice", r, 4, [&] (int g) { r.ui.screen().setActiveGroup (g); r.refresh(); });
    bool noise = false;
    for (int i = 0; i < 16; ++i)
        noise = noise || keyOf (r.inst.q[i]) == "voicectl.osc.noise";
    check (! noise, "Voice: NOISE is no Q target (touch only)");
}

void testFilter()
{
    // the hardware sub-tabs Filter (view 1), Filter 2 (2), Note (3) and Velo (4): the state of the page is the sub-tab's (no 60/40 split, no selector)
    auto activateExpanded = [&] (auto& r, int g) { r.ui.full().setActiveGroup (g); r.refresh(); };
    // 1. Filter 1 / Filter 2: MAIN, TRACK, OUT (+ FORMANT when Formant)
    for (const int f : { 1, 2 })
    {
        RoutedPage<FilterUi> r (f == 1 ? "filter1" : "filter2", layout::kKindFilterUi, f);
        r.refresh();
        const auto activate = [&] (int g) { activateExpanded (r, g); };
        const std::string pre = "filter" + std::to_string (f) + ".", st = "Filter F" + std::to_string (f);
        if (f == 1)
        {
            checkPage ("Filter Expanded", r, 3, activate);
            check (r.ui.full().groups[0].block() == 1 || r.ui.full().groups[1].block() == 1 || r.ui.full().groups[2].block() == 1, "Filter: the groups that are not active have a block of knobs");
            check (r.inst.q[12] == -1 && r.inst.q[15] == -1, "Filter: three groups leave Q 13 .. 16 without a target");
        }
        r.inst.set ("filter1.on", 0.0f);
        r.inst.set ("filter2.on", 0.0f);
        expectGroups (st + " Both OFF", r, { {} }, activate);
        r.inst.set ("filter1.on", 1.0f);
        r.inst.set ("filter2.on", 1.0f);
        r.inst.set ((pre + "model").c_str(), 3.0f);
        r.refresh();
        expectGroups (st, r, { { pre + "cutoff", pre + "resonance", pre + "drive", pre + "mix" }, { pre + "keytrack", pre + "env1", pre + "var", "" }, { pre + "blend", "filter.routing", pre + "model", pre + "style" } }, activate);
        r.inst.set ((pre + "model").c_str(), 6.0f);   // the Comb model: COMB is the fourth member of TRACK
        r.refresh();
        expectGroups (st + " (Comb model)", r, { { pre + "cutoff", pre + "resonance", pre + "drive", pre + "mix" }, { pre + "keytrack", pre + "env1", pre + "var", pre + "comb_offset" }, { pre + "blend", "filter.routing", pre + "model", pre + "style" } }, activate);
        r.inst.set ((pre + "model").c_str(), 5.0f);   // the Formant model: the FORMANT block is a fourth group, the formant RESONANCE takes the place of VAR in TRACK
        r.refresh();
        expectGroups (st + " (Formant model)", r, { { pre + "cutoff", pre + "resonance", pre + "drive", pre + "mix" }, { pre + "keytrack", pre + "env1", pre + "formant.resonance", "" }, { pre + "blend", "filter.routing", pre + "model", pre + "style" },
                                                    { pre + "formant.x", pre + "formant.y", pre + "formant.transpose", pre + "formant.spread" } }, activate);
        checkPage (st + " (Formant model)", r, 4, activate);
        r.inst.set ((pre + "model").c_str(), 3.0f);
    }
    // 2. Note and Velo: EDIT [0..3], LEGATO AND GRID [legato, 4..6]
    for (const int sub : { 3, 4 })
    {
        const std::string legato = sub == 3 ? "curve.note.legato" : "curve.velocity.legato", nm = sub == 3 ? "Filter NOTE" : "Filter VELO";
        RoutedPage<FilterUi> r (sub == 3 ? "filternote" : "filtervelo", layout::kKindFilterUi, sub);
        r.refresh();
        auto activateCurve = [&] (int g) { r.ui.curveUi().screen.setActiveGroup (g); r.refresh(); };
        expectGroups (nm, r, { { "virt:0", "virt:1", "virt:2", "virt:3" }, { legato, "virt:4", "virt:5", "virt:6" } }, activateCurve);
        checkPage (nm, r, 2, activateCurve);
        check (r.inst.q[8] == -1 && r.inst.q[12] == -1, nm + ": two groups leave Q 9 .. 16 without a target");
    }
}

void testOscillator()
{
    demo::Rig<OscPageUi> r (2);
    for (const char* k : { "osc1.warp.type", "osc1.warp2.type" })
        r.inst.set (k, 1.0f);   // a warp that is Off has no amount (hidden, no Q member): the full group below needs both on
    r.refresh();
    ui::OscillatorFullScreen& f = r.ui.ui().full();
    const int n = f.numGroups();
    check (n >= 3, "Osc A: the full screen has several groups (" + std::to_string (n) + ")");
    checkPage ("Osc A", r, n, [&] (int g) { f.setActiveGroup (g); r.refresh(); });
    // the wavetable (bold cut): Basic [TYPE, TABLE, WT POS, OCT], Unison [UNISON, DETUNE, FIN], Warp [WARP 1 TYPE, WARP 1 AMOUNT, WARP 2 TYPE, WARP 2 AMOUNT]
    check (n == 3, "Osc A (wavetable): three groups (" + std::to_string (n) + ")");
    expectGroups ("Osc A (wavetable)", r,
                  { { "osc1.type", "osc1.wavetable", "osc1.frame", "osc1.transpose" }, { "osc1.unison.voices", "osc1.unison.detune", "osc1.tune" }, { "osc1.warp.type", "osc1.warp.amount", "osc1.warp2.type", "osc1.warp2.amount" } },
                  [&] (int g) { f.setActiveGroup (g); r.refresh(); });
    int mappedBlocks = 0;
    f.setActiveGroup (0);
    r.refresh();
    for (int g = 0; g < n; ++g)
        mappedBlocks += f.groups[g].block() >= 1;
    check (mappedBlocks == std::min (n - 1, 3), "Osc A: the groups after the active one have their blocks (" + std::to_string (mappedBlocks) + ")");
    // hide what does not apply: a warp that is Off has no amount, and the Warp group closes up (both devices)
    {
        demo::Rig<OscPageUi> o (2);
        o.refresh();
        ui::OscillatorFullScreen& fo = o.ui.ui().full();
        expectGroups ("Osc A (wavetable, both warps Off)", o,
                      { { "osc1.type", "osc1.wavetable", "osc1.frame", "osc1.transpose" }, { "osc1.unison.voices", "osc1.unison.detune", "osc1.tune" }, { "osc1.warp.type", "osc1.warp2.type" } },
                      [&] (int g) { fo.setActiveGroup (g); o.refresh(); });
        o.inst.set ("osc1.warp.type", 3.0f);
        o.refresh();
        expectGroups ("Osc A (wavetable, warp 1 on, warp 2 Off)", o,
                      { { "osc1.type", "osc1.wavetable", "osc1.frame", "osc1.transpose" }, { "osc1.unison.voices", "osc1.unison.detune", "osc1.tune" }, { "osc1.warp.type", "osc1.warp.amount", "osc1.warp2.type" } },
                      [&] (int g) { fo.setActiveGroup (g); o.refresh(); });
    }
}

/** The default view of the other types (bold cut): the groups are the blocks of the view in reading order, on both devices. A content type's SOURCE is no parameter (its Q slot is empty, as before). */
void testOscillatorDefaultViews()
{
    struct V { const char* name; int type; std::vector<Quad> groups; };
    const std::vector<V> views = {
        { "Osc A (multisample)", 4, { { "osc1.type", "", "osc1.multi.timbre", "osc1.transpose" }, { "osc1.unison.voices", "osc1.unison.detune", "osc1.tune" }, { "osc1.warp.type", "osc1.warp.amount" } } },
        { "Osc A (sample)", 1, { { "osc1.type", "", "osc1.sample.scan", "osc1.transpose" }, { "osc1.unison.voices", "osc1.unison.detune", "osc1.tune" }, { "osc1.warp.type", "osc1.warp.amount" } } },
        { "Osc A (spectral)", 3, { { "osc1.type", "", "osc1.spec.position", "osc1.transpose" }, { "osc1.unison.voices", "osc1.unison.detune" }, { "osc1.warp.type", "osc1.warp.amount" } } },
        { "Osc A (granular)", 2, { { "osc1.type", "", "osc1.gran.position", "osc1.transpose" }, { "osc1.gran.density", "osc1.gran.length", "osc1.warp.type" }, { "osc1.gran.rand_offset", "osc1.gran.rand_dir", "osc1.gran.rand_pitch", "osc1.tune" } } } };
    for (const V& v : views)
    {
        demo::Rig<OscPageUi> r (2);
        r.inst.set ("osc1.type", static_cast<float> (v.type));
        r.inst.set ("osc1.warp.type", 1.0f);
        r.refresh();
        ui::OscillatorFullScreen& f = r.ui.ui().full();
        expectGroups (v.name, r, v.groups, [&] (int g) { f.setActiveGroup (g); r.refresh(); });
        checkPage (v.name, r, f.numGroups(), [&] (int g) { f.setActiveGroup (g); r.refresh(); });
    }
    {
        RoutedPage<OscPageUi> r ("oscsub", layout::kKindOscUi, playosc::kSub);
        r.inst.set ("sub.on", 0.0f);
        r.refresh();
        ui::OscillatorFullScreen& f = r.ui.ui().full();
        auto activate = [&] (int g) { f.setActiveGroup (g); r.refresh(); };
        expectGroups ("Sub OFF", r, { {} }, activate);
        r.inst.set ("sub.on", 1.0f);
        r.refresh();
        expectGroups ("Sub ON", r, { { "sub.waveform", "sub.octave", "sub.tune", "sub.phase" }, { "sub.env1", "sub.mono", "sub.phase_contiguous" } }, activate);
    }
    {
        RoutedPage<OscPageUi> r ("oscnoise", layout::kKindOscUi, playosc::kNoise);
        r.inst.set ("noise.type", 0.0f);
        r.inst.set ("noise.on", 0.0f);
        r.refresh();
        ui::OscillatorFullScreen& f = r.ui.ui().full();
        auto activate = [&] (int g) { f.setActiveGroup (g); r.refresh(); };
        expectGroups ("Noise (generated), OFF", r, { {} }, activate);
        r.inst.set ("noise.on", 1.0f);
        r.refresh();
        expectGroups ("Noise (generated), ON", r, { { "noise.type", "noise.sample", "noise.filter", "noise.stereo" }, { "noise.pitch", "noise.fine", "noise.keytrack", "noise.env1" } }, activate);
    }
}

/** The noise that plays a sample: Basic [KIND, FILTER, STEREO] (SOUND does not apply to a sample: hidden), Tune [PITCH, FINE, KEY TRACK, ENV 1], Playback [START, RANDOM, LOOP, BOUNCE]. */
void testNoiseSample()
{
    RoutedPage<OscPageUi> r ("oscnoise", layout::kKindOscUi, playosc::kNoise);
    r.inst.set ("noise.type", 1.0f);
    r.inst.set ("noise.loop", 1.0f);
    r.inst.set ("noise.on", 0.0f);
    r.refresh();
    ui::OscillatorFullScreen& f = r.ui.ui().full();
    auto activate = [&] (int g) { f.setActiveGroup (g); r.refresh(); };
    expectGroups ("Noise (sample), OFF", r, { {} }, activate);
    r.inst.set ("noise.on", 1.0f);
    r.refresh();
    check (f.numGroups() == 3, "Noise (sample): three groups (" + std::to_string (f.numGroups()) + ")");
    expectGroups ("Noise (sample)", r,
                  { { "noise.type", "noise.filter", "noise.stereo" }, { "noise.pitch", "noise.fine", "noise.keytrack", "noise.env1" }, { "noise.phase", "noise.random_phase", "noise.loop", "noise.bounce" } },
                  [&] (int g) { f.setActiveGroup (g); r.refresh(); });
    r.inst.set ("noise.loop", 0.0f);
    r.refresh();
    expectGroups ("Noise (sample, LOOP off: BOUNCE still present)", r,
                  { { "noise.type", "noise.filter", "noise.stereo" }, { "noise.pitch", "noise.fine", "noise.keytrack", "noise.env1" }, { "noise.phase", "noise.random_phase", "noise.loop", "noise.bounce" } },
                  [&] (int g) { f.setActiveGroup (g); r.refresh(); });
}

/** The detail pages of the oscillator types (osc_spec_page.h): the groups are their blocks in reading order, the rule of sixteen slots holds for every page, in the state that shows every block. */
void testOscillatorTypePages()
{
    for (int page = 16; page <= 23; ++page)
        for (int type = 0; type < (page == 17 ? 5 : 1); ++type)   // MORE (17) is one page for the five types: each type shows its own blocks
    {
        demo::Rig<OscPageUi> r (2);
        auto set = [&] (const char* k, float v) { r.inst.set (k, v); };
        set ("osc1.type", page == 17 ? static_cast<float> (type) : page == 16 ? 0.0f : (page == 18 || page == 19 ? 1.0f : (page == 20 ? 2.0f : 3.0f)));
        set ("osc1.warp.type", 1.0f); set ("osc1.warp2.type", 1.0f);
        set ("osc1.sample.loop_mode", 1.0f); set ("osc1.sample.slice_mode", 1.0f); set ("osc1.remap.on", 1.0f); set ("osc1.spec.flt_use", 1.0f);
        r.refresh();
        r.stack.openDetail (page);
        r.refresh();
        oscspec::OscSpecPage& pg = r.ui.ui().specPage();
        const int n = pg.numGroups();
        check (n >= 3 || page == 16 || (page == 17 && n >= 2), "osc page " + std::to_string (page) + " (type " + std::to_string (type) + "): its blocks are groups (" + std::to_string (n) + ")");
        checkPage ("Osc page " + std::to_string (page) + (page == 17 ? " type " + std::to_string (type) : ""), r, n, [&] (int g) { pg.setActiveGroup (g); r.refresh(); });
        bool four = true;
        for (int g = 0; g < n; ++g)
            four = four && pg.groups[g].numMembers() >= 1 && pg.groups[g].numMembers() <= 4;
        check (four, "osc page " + std::to_string (page) + ": every group has one to four members (a block of page buttons is no group)");
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
    // the Env sub-tabs (Env 1 .. Env 6: the view's instance number) and the LFO sub-tabs (LFO 1 .. 10, RND 1 .. 4: the Q-Links follow the LFO of the sub-tab)
    for (int e : { 4, 6, 1 })
    {
        demo::Rig<EnvUi> r (e);
        r.refresh();
        r.ui.full().setActiveGroup (0);
        r.refresh();
        const std::string pre = "env" + std::to_string (e) + ".";
        check (r.inst.q[0] == demo::Inst::idx (pre + "attack") && r.inst.q[3] == demo::Inst::idx (pre + "release") && r.inst.q[4] == demo::Inst::idx (pre + "attack_curve") && r.inst.q[8] == demo::Inst::idx (pre + "delay"),
               "Env sub-tab " + std::to_string (e) + ": Q 1 .. 4 ADSR, Q 5 SHAPE, Q 9 TIME of " + pre);
    }
    {
        for (int l : { 3, 10, 1 })
        {
            demo::Rig<LfoUi> r (l);   // the sub-tab LFO l (the view's instance number): no selector, no Play selection
            r.refresh();
            r.ui.full().setActiveGroup (0);
            r.refresh();
            const std::string pre = "lfo" + std::to_string (l) + ".";
            check (r.inst.q[0] == demo::Inst::idx (pre + "shape") && r.inst.q[1] == demo::Inst::idx (pre + "rate") && r.inst.q[2] == demo::Inst::idx (pre + "fade_in") && r.inst.q[3] == demo::Inst::idx (pre + "smooth_time"),
                   "LFO sub-tab " + std::to_string (l) + ": Q 1 .. 4 are the RATE group of " + pre);
        }
        demo::Rig<LfoUi> r (13);   // the sub-tab RND 3
        r.refresh();
        r.ui.full().setActiveGroup (0);
        r.refresh();
        check (r.ui.random() && r.inst.q[0] == demo::Inst::idx ("rlfo3.style") && r.inst.q[1] == demo::Inst::idx ("rlfo3.rate") && r.inst.q[2] == demo::Inst::idx ("rlfo3.sync") && r.inst.q[3] == demo::Inst::idx ("rlfo3.stereo"),
               "LFO sub-tab RND 3: Q 1 .. 4 are the RANDOM group of rlfo3");
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

/** The matrix page: G1 [ROW, SOURCE, TARGET, AMOUNT] of the selected row, G2 [UNI / BI, ON / OFF, AUX SOURCE, AUX SCALE] of the same row, G3 and G4 the next two visible rows [SOURCE, TARGET, AMOUNT, UNI / BI]. */
void testModMatrix()
{
    demo::Rig<ModUi> r;
    for (int i = 1; i <= 4; ++i)
    {
        const std::string pre = std::string ("mod0") + std::to_string (i) + ".";
        r.inst.set ((pre + "source").c_str(), static_cast<float> (i));
        r.inst.set ((pre + "destination").c_str(), static_cast<float> (i));
    }
    r.refresh();
    r.ui.matrix().selectRow (0);
    r.refresh();
    expectGroups ("Mod matrix (row 1 selected)", r,
                  { { "virt:0", "mod01.source", "mod01.destination", "mod01.amount" }, { "mod01.bipolar", "mod01.bypass", "mod01.aux_source", "mod01.aux_output" },
                    { "mod02.source", "mod02.destination", "mod02.amount", "mod02.bipolar" }, { "mod03.source", "mod03.destination", "mod03.amount", "mod03.bipolar" } },
                  [&] (int) { r.refresh(); }, 1);
    r.ui.matrix().selectRow (2);
    r.refresh();
    expectGroups ("Mod matrix (row 3 selected: one row left after it)", r,
                  { { "virt:0", "mod03.source", "mod03.destination", "mod03.amount" }, { "mod03.bipolar", "mod03.bypass", "mod03.aux_source", "mod03.aux_output" },
                    { "mod04.source", "mod04.destination", "mod04.amount", "mod04.bipolar" }, { "", "", "", "" } },
                  [&] (int) { r.refresh(); }, 1);
    // the chips of the idle groups: the polarity and the switch of the selected row, then the next two rows (the XL numbers 5 .. 16, the Key 61 the banks as 1 .. 4)
    r.ui.matrix().selectRow (0);
    r.refresh();
    ui::ModMatrixView& mv = r.ui.matrix();
    check (mv.qmore[0].member (0) == &mv.windowRows[0].biToggle && mv.qmore[0].member (1) == &mv.windowRows[0].bypassToggle && mv.qmore[0].member (2) == nullptr && mv.qmore[0].block() == 1
               && mv.qmore[1].member (2) == &mv.windowRows[1].amountBar && mv.qmore[1].member (3) == &mv.windowRows[1].biToggle && mv.qmore[2].member (0) == &mv.windowRows[2].sourceBtn && mv.qmore[2].block() == 3,
           "Mod matrix: the idle groups sit on the controls of the selected row and the next two rows, blocks 1 .. 3");
}

void testModDetail()
{
    struct ModInstance : demo::Inst
    {
        nativeui::NativeCellBank<nativeui::mod::kNavigation> navigation;
        void setNativeModNavigation (int mode, int, int, const nativeui::FxCell* fields) noexcept override
        {
            navigation.publish (mode, fields);
        }
        bool takeNativeModNavigation (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return navigation.take (field, expected, value);
        }
    };
    RoutedPage<ModUi, ModInstance> r ("mod", layout::kKindMod);
    r.inst.mod (1, 1, "filter1.cutoff", 50.0f);
    r.inst.mod (3, 2, "master.volume", 30.0f);
    r.refresh();
    const auto more = r.inst.navigation.at (nativeui::mod::navigationField (1, nativeui::mod::OpenMore));
    r.inst.navigation.report (more);
    r.inst.navigation.post (more, 1.0f);
    r.refresh();
    check (r.stack.depth() == 2 && r.stack.top() == ModUi::kPageDetail && r.ui.openSlot() == 2,
           "Mod: MORE on the second displayed row opens slot 3, its actual route");
    expectGroups ("Mod slot 3 detail", r,
                  { { "mod03.aux_source", "mod03.aux_output", "mod03.aux_inv", "mod03.aux_power" },
                    { "mod03.power", "mod03.stereo" },
                    { "mod03.rise", "mod03.fall", "mod03.delay", "mod03.delay_sync" } },
                  [&] (int g) { r.ui.detail().setActiveBlock (static_cast<ui::ModSlotDetail::Block> (g)); r.refresh(); });
    checkPage ("Mod slot page", r, 3, [&] (int g) { r.ui.detail().setActiveBlock (static_cast<ui::ModSlotDetail::Block> (g)); r.refresh(); });
    check (r.inst.q[0] != -1 && r.inst.q[4] != -1 && r.inst.q[8] != -1 && r.inst.q[12] == -1, "Mod slot page: three blocks fill three rows, Q 13 .. 16 stay empty");
    r.tap (r.ui.detail().back);
    check (r.stack.depth() == 1 && r.ui.topPage() == 0 && r.ui.matrix().selectedSlot() == 2,
           "Mod: BACK closes the detail and keeps the edited route selected");
    expectGroups ("Mod matrix after closing detail", r,
                  { { "virt:0", "mod03.source", "mod03.destination", "mod03.amount" },
                    { "mod03.bipolar", "mod03.bypass", "mod03.aux_source", "mod03.aux_output" } },
                  [&] (int) { r.refresh(); }, 1);
}

void testMacros()
{
    demo::Rig<MacroUi> r;
    r.refresh();
    const int m1 = demo::Inst::idx ("macro.1"), m5 = demo::Inst::idx ("macro.5");
    for (const bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        const std::string dev = wide ? "XL (16 knobs)" : "Key 61 (4 knobs)";
        for (int bankFocus : { 0, 1 })
        {
            r.ui.view().setBank (bankFocus);
            r.refresh();
            bool ok = r.inst.qN == 16;
            for (int k = 0; k < 8; ++k)
                ok = ok && r.inst.q[k] == demo::Inst::idx ("macro." + std::to_string (k + 1)) && r.ui.qTargets()[k] == r.inst.q[k];
            for (int k = 8; k < 16; ++k)
                ok = ok && r.inst.q[k] == -1 && r.ui.qTargets()[k] == -1;
            check (ok && r.inst.q[0] == m1 && r.inst.q[4] == m5,
                   "Macros, " + dev + " bankFocus=" + std::to_string (bankFocus) + ": fixed Q 1..4 = macro.1..4, Q 5..8 = macro.5..8, rest none");
        }
    }
    ui::QGroup::setWideDevice (false);
    r.ui.view().setBank (0);
}

void testFx()
{
    demo::Rig<FxUi> r;
    r.inst.set ("eq.on", 1.0f);
    r.refresh();
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    check (r.ui.screen().editorShown() && ed.currentType() == synthyboy::kFxTypeEq,
           "FX: standalone EQ is selected before comparing the default and MORE Q-Link groups");
    // the default view and the MORE view of the same editor: the rows of the view on screen are the groups (bold cut: 4 members at most, the live ones from the left)
    for (int view = 0; view < 2; ++view)
    {
        ed.setPage (view);
        r.refresh();
        const std::string v = view == 0 ? "the default view" : "the MORE view";
        const int first = ed.firstRowOfPage(), pageRows = ed.rowsOnPage(), rows = ed.numRows();   // ALL the rows of the effect are groups: the default view's rows, then the MORE rows (XL Q 9 .. 16)
        check (ed.pageIndex() == view && pageRows >= 2, "FX: " + v + " of the effect has two rows or more (" + std::to_string (pageRows) + ")");
        std::vector<std::vector<int>> ref;
        for (int k = 0; k < rows; ++k)
        {
            ed.setActiveRow (k);
            r.refresh();
            const Slots s = published (r);
            ref.push_back ({ s.s[0], s.s[1], s.s[2], s.s[3] });
        }
        bool rule = true;
        for (int a = 0; a < rows; ++a)
        {
            ed.setActiveRow (a);
            r.refresh();
            rule = rule && followsRule (published (r), ref, a);
        }
        check (rule, "FX: ALL the rows of the effect are the groups, the MORE rows too (the active row on Q 1 .. 4, the others on Q 5 .. 16 in reading order, nothing after), shown on " + v);
        // the XL: the rows stay where they are (up to four): the default view's rows on Q 1 .., the MORE rows after them, whatever row is active
        if (rows <= 4)
        {
            ui::QGroup::setWideDevice (true);
            bool fixed = true;
            for (int a = 0; a < rows; ++a)
            {
                ed.setActiveRow (a);
                r.refresh();
                for (int k = 0; k < rows; ++k)
                    for (int i = 0; i < 4; ++i)
                        fixed = fixed && published (r).s[4 * k + i] == ref[static_cast<size_t> (k)][static_cast<size_t> (i)];
            }
            ui::QGroup::setWideDevice (false);
            check (fixed, "FX, XL: the rows (" + std::to_string (rows) + ") stay on Q 1 .. " + std::to_string (4 * rows) + " in the order of the effect (default view first, MORE rows after) on " + v);
        }
        ed.setActiveRow (first);
        r.refresh();
        check (ed.groups[first].block() >= 0 && ed.groups[first + 1].block() >= 0 && ed.groups[first].block() != ed.groups[first + 1].block(), "FX: the rows of " + v + " have different blocks (0 active, 1 the other)");
    }
}

/** The compressor's BAND selector is view-local (no parameter, no Q slot): when it changes, the Q group of the second row publishes the selected band's THRESHOLD and RATIO. */
void testFxCompressorBand()
{
    demo::Rig<FxUi> r;
    r.inst.set ("compressor.on", 1.0f);
    r.refresh();
    ui::FxSlotEditor& ed = r.ui.screen().slotEditor;
    check (ed.currentType() == synthyboy::kFxTypeCompressor && ed.hasBandSelector(), "FX: the compressor is on the page with its BAND selector");
    const synthyboy::FxRackItem item = r.ui.cardItem (0);
    auto idxOf = [&] (const char* key) { return r.ui.paramIndexOf (item, ed.controlOfKey (key)); };
    ed.setActiveRow (1);
    for (int band = 0; band < 3; ++band)
    {
        static const char* const kThr[3] = { "compressor.low.upper_threshold", "compressor.mid.upper_threshold", "compressor.high.upper_threshold" };
        static const char* const kRat[3] = { "compressor.low.upper_ratio", "compressor.mid.upper_ratio", "compressor.high.upper_ratio" };
        ed.setBand (band);
        r.refresh();
        check (r.inst.q[0] == idxOf ("compressor.limit") && r.inst.q[1] == idxOf (kThr[band]) && r.inst.q[2] == idxOf (kRat[band]) && r.inst.q[3] < 0,
               "FX: band " + std::to_string (band) + " selected: Q 1 .. 3 are MODE, THRESHOLD and RATIO of that band, Q 4 has no target (the selector has no Q slot)");
    }
}

void testOneGroupPage()
{
    for (const bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        demo::Inst inst;
        inst.playSel[0] = playosc::kSub;
        UiStack stack;
        PlayView view (1, 10.0);
        ViewInput in;
        in.io = &inst; in.instance = &inst; in.ui = &stack; in.nowMs = 1000.0;
        for (bool on : { false, true, false })
        {
            inst.set ("sub.on", on ? 1.0f : 0.0f);
            in.nowMs += 150.0;
            view.update (in);
            const std::string label = std::string ("Play compact SUB ") + (on ? "ON" : "OFF") + (wide ? " on XL" : " on Key 61");
            const std::array<std::string, 4> want = on
                ? std::array<std::string, 4>{ "sub.waveform", "sub.octave", "sub.pan", "sub.level" }
                : std::array<std::string, 4>{};
            bool correct = inst.qN == 16;
            for (int i = 0; i < 4; ++i)
                correct = correct && keyOf (inst.q[i]) == want[i];
            check (correct && keyOf (inst.q[4]) == "env1.attack" && keyOf (inst.q[13]) == "sub.on",
                   label + ": its Basic group follows power, while ENV and power remain reachable");
        }
    }
    ui::QGroup::setWideDevice (false);
}

void testCompactPlayInactiveOscillator()
{
    constexpr int w = overlay::play::kRegionW, h = overlay::play::kRegionH;
    demo::Inst inst;
    inst.playSel[0] = playosc::kSub;
    UiStack stack;
    PlayView view (1, 10.0);
    synthyboy::LiveValues live {};
    ViewInput in;
    in.io = &inst; in.instance = &inst; in.ui = &stack; in.live = &live; in.nowMs = 1000.0;
    auto refresh = [&] { in.nowMs += 150.0; view.update (in); };
    auto render = [&]
    {
        uit::Image im { w, h, std::vector<uint32_t> (w * h) };
        view.render (im.px.data(), w, h);
        return im;
    };
    auto grey = [](uint32_t c) { return ((c >> 16) & 255u) == ((c >> 8) & 255u) && ((c >> 8) & 255u) == (c & 255u); };
    auto centre = [](const ui::Component& c) { const auto r = c.rect(); return std::pair<int, int> { r.x + r.w / 2, r.y + r.h / 2 }; };

    inst.set ("sub.on", 1.0f);
    refresh();
    const auto on = render();
    inst.set ("sub.on", 0.0f);
    refresh();
    const auto off = render();
    const auto& osc = view.oscUi()->compact();
    const ui::Rect power = osc.power.rect();
    int offColour = 0, onColour = 0, red = 0, green = 0;
    for (int y = overlay::play::kTabY + 66; y < overlay::play::kTabY + overlay::play::kTabH; ++y)
        for (int x = 0; x < 432; ++x)
        {
            if (power.contains (x, y))
            {
                const uint32_t a = off.at (x, y), b = on.at (x, y);
                const unsigned ar = (a >> 16) & 255u, ag = (a >> 8) & 255u, ab = a & 255u;
                red += ar > ag + 30;
                offColour += ! grey (a) && (ar <= ag || ar <= ab);
                green += ((b >> 8) & 255u) > ((b >> 16) & 255u) + 30;
            }
            else
            {
                offColour += ! grey (off.at (x, y));
                onColour += ! grey (on.at (x, y));
            }
        }
    check (offColour == 0 && onColour > 100 && red > 20 && green > 20,
           "compact OSC: only the red power is coloured in OFF; restoring ON restores colour and green power");
    const int selectorMid = overlay::play::kTabY + 38;
    check ((off.at (40, selectorMid) >> 24) == 255 && grey (off.at (40, selectorMid))
           && off.at (140, selectorMid) == 0,
           "compact OSC OFF: the selector gutter hides the coloured native background while the selector stays visible");
    for (const ui::Component* c : { &osc.basic.pan, &osc.basic.level })
    {
        const ui::Rect r = c->rect();
        bool transparent = true;
        for (int y = r.y + 10; y < r.bottom() - 10; ++y)
            for (int x = r.x + 10; x < r.right() - 10; ++x)
                transparent &= off.at (x, y) == 0;
        check (transparent, "compact OSC: the grey native readout remains visible through its foreground aperture");
    }
    const auto plot = osc.plotRect();
    live.subPhase[0] = 0.47f;
    refresh();
    const auto moved = render();
    bool frozen = true;
    for (int y = plot.y; y < plot.bottom(); ++y)
        for (int x = plot.x; x < plot.right(); ++x)
            frozen &= off.at (x, y) == moved.at (x, y);
    check (frozen, "compact OSC: a changed live phase does not animate the OFF plot");

    const int before = inst.writes;
    const ui::Component* const disabled[] { &osc.basic.selector, &osc.basic.oct, &osc.basic.pan, &osc.basic.level, &osc.full };
    for (auto* c : disabled)
    {
        const auto [x, y] = centre (*c);
        view.onTouch ({ TouchEvent::Down, x, y }, stack, inst, in.nowMs);
        view.onTouch ({ TouchEvent::Move, x, y - 35 }, stack, inst, in.nowMs + 10);
        view.onTick (stack, inst, in.nowMs + 600);
        view.onTouch ({ TouchEvent::Up, x, y }, stack, inst, in.nowMs + 610);
        view.onWheel (x, y, 4, stack, inst, in.nowMs + 620);
    }
    QRouter router;
    router.setGroup (inst.q, inst.qN, static_cast<int> (synthyboy::paramCount()));
    bool noTarget = true;
    for (int knob = 0; knob < 4; ++knob)
        noTarget &= router.hostWrite (knob, 0.83f, false, [](int) { return 0.0f; },
                                     [&](int index, float value) { inst.writeParam (index, value); }) == QRouter::Write::none;
    check (noTarget && inst.writes == before && stack.isMain() && ! view.oscUi()->popupOpen(),
           "compact OSC OFF: touch, long press, drag, wheel and the four Q-Link inputs write nothing and open no editor");

    const ui::Rect nativeSelector { 84, overlay::play::kTabY + 10, 338, 56 };
    check (! view.onTouch ({ TouchEvent::Down, nativeSelector.x + 80, nativeSelector.y + 30 }, stack, inst, in.nowMs),
           "compact OSC OFF: the oscillator selector hands its touch to the host");
    inst.set ("noise.on", 0.0f);
    inst.setPlaySelection (0, playosc::kNoise);
    refresh();
    const auto& noise = view.oscUi()->compact().basic;
    check (noise.pan.rect() == ui::Rect { 14, 620, 202, 108 }
           && noise.level.rect() == ui::Rect { 220, 620, 202, 108 }
           && noise.filter.rect().bottom() < noise.pan.rect().y,
           "Play Noise puts its Filter above the native PAN/LEVEL holes in both power states");
    inst.set ("osc2.on", 0.0f);
    inst.setPlaySelection (0, playosc::kB);
    refresh();
    check (view.oscUi()->selection() == playosc::kB && ! view.oscUi()->compact().power.state.on && inst.writes == before,
           "compact OSC navigates from OFF Sub through Noise to OFF Osc B with no parameter writes");
    const auto [x, y] = centre (view.oscUi()->compact().power);
    view.onTouch ({ TouchEvent::Down, x, y }, stack, inst, in.nowMs);
    view.onTouch ({ TouchEvent::Up, x, y }, stack, inst, in.nowMs + 20);
    refresh();
    check (inst.writes == before + 1 && inst.lastWritten == demo::Inst::idx ("osc2.on") && keyOf (inst.q[2]) == "osc2.pan" && keyOf (inst.q[3]) == "osc2.level",
           "compact OSC OFF: power stays reachable and restores the selected oscillator's PAN/LEVEL Q targets");
    if (const char* dir = std::getenv ("PLAY_OFF_REFERENCE_DIR"))
    {
        check (uit::writePng (std::string (dir) + "/play-sub-on-foreground.png", on)
               && uit::writePng (std::string (dir) + "/play-sub-off-foreground.png", off),
               "write reproducible Mac Play reference foregrounds");
    }
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
    // Assignment is independent of any visual markers on idle groups.
    for (const bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        demo::Rig<OscPageUi> r (2);   // Osc A, the OPTIONS page: seven groups (blocks): on the XL the first four are mapped, the fifth is not
        r.refresh();
        r.stack.openDetail (oscspec::kOptions);
        r.refresh();
        oscspec::OscSpecPage& pg = r.ui.ui().specPage();
        pg.setActiveGroup (0);
        r.refresh();
        auto opsOf = [&] (int group)
        {
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            pg.groups[group].drawIdle (sb);
            return scene->numOps;
        };
        const uint32_t block1 = opsOf (1), unmapped = opsOf (5);
        check (pg.numGroups() >= 6 && block1 == 0 && unmapped == 0,
               std::string (wide ? "16 knob device: " : "4 knob device: ") + "both idle groups stay free of Q decorations");
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
    testOscillatorDefaultViews();
    testOscillatorTypePages();
    testNoiseSample();
    testEnvLfo();
    testModMatrix();
    testModDetail();
    testMacros();
    testFx();
    testFxCompressorBand();
    testOneGroupPage();
    testCompactPlayInactiveOscillator();
    testStatusSlotCount();
    testDeviceRule();
    testChips();
    std::printf (g_fails ? "qslots_host: FAILED (%d)\n" : "qslots_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
