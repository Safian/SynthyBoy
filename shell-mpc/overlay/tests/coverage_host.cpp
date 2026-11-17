// SynthyBoy - MPC overlay tests: parameter coverage of the plugin-drawn pages that have no coverage test of their own (the Env / LFO and the Master / Voice pages have theirs: env_ui_host, lfo_ui_host,
// mastervoice_ui_host). For every module (oscillators A / B / C, Sub, Noise, Filter 1 / 2, FX with every effect type, the modulation matrix, the macros, the mixer) the engine's parameter table is compared
// with what the page has: the parameters that have a CONTROL (a control of the page shows and writes it, in some state of the page: another oscillator type, filter model, effect type, sub page) and the
// parameters that are a MEMBER OF A Q GROUP (a hardware knob can reach it). The test prints, per module, the parameters that have no control and the controls that are not Q members, and it PASSES today
// because the known gaps are listed in the tables below: a NEW gap fails it, and a gap that has been closed must be removed from the table (it fails until it is). The same lists are written to
// docs / the gap report (.hw-local/mpc-ui-plans/coverage-gaps.md) by the main session from the output of this test (COVERAGE_REPORT=file writes the numbers as text).
// How a control is found: oscillator pages and filter pages by their own control tables (OscUi::items, FilterUi::paramOf), the mixer by the values its update reads, FX / modulation / macro pages by
// changing the parameter and looking at the picture (a page that shows a value draws another picture when it changes). The Q members are what the page publishes to the instance (setQGroup) for every
// group it has in every state (all 16 slots: the active group and the next three).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fakes.h"
#include "../filter_ui.h"
#include "../fx_ui.h"
#include "../macro_ui.h"
#include "../mixer_ui.h"
#include "../mod_ui.h"
#include "../osc_ui.h"
#include "../scene_cpu.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"

using namespace ovt;
using synthyboy_mpc::QRouter;
namespace ui = synthyboy_mpc::ui;
using synthyboy::ParamInfo;

namespace
{
const ParamInfo* tbl() { return synthyboy::paramTable(); }
int count() { return static_cast<int> (synthyboy::paramCount()); }
bool startsWith (const char* s, const std::string& p) { return std::strncmp (s, p.c_str(), p.size()) == 0; }

std::set<int> withPrefix (std::initializer_list<const char*> prefixes)
{
    std::set<int> out;
    for (int i = 0; i < count(); ++i)
        for (const char* p : prefixes)
            if (startsWith (tbl()[i].key, p))
                out.insert (i);
    return out;
}

/** What one module's page is checked for. */
struct Module
{
    std::string name;
    std::string table;        // the name of the table of known gaps (the three oscillators share one)
    std::set<int> params;     // the engine's parameters of the module
    std::set<int> controls;   // a control of the page shows and writes it (some state of the page)
    std::set<int> disabled;   // a control exists but is drawn disabled in every state (nothing behind it in the engine)
    std::set<int> qMembers;   // a member of a Q group the page publishes
};

/** The key without the number of the module instance: osc1.x -> oscN.x, mod07.x -> modNN.x, fxi03.x -> fxiNN.x (the three oscillators, the 64 slots and the 16 pool slots are the same pages). */
std::string norm (const char* key)
{
    std::string k = key;
    auto digit = [&] (size_t i) { return i < k.size() && k[i] >= '0' && k[i] <= '9'; };
    if (k.compare (0, 3, "osc") == 0 && digit (3) && k.size() > 4 && k[4] == '.')
        return "oscN" + k.substr (4);
    if (k.compare (0, 3, "mod") == 0 && digit (3) && digit (4) && k.size() > 5 && k[5] == '.')
        return "modNN" + k.substr (5);
    if (k.compare (0, 3, "fxi") == 0 && digit (3) && digit (4) && k.size() > 5 && k[5] == '.')
        return "fxiNN" + k.substr (5);
    return k;
}

std::string describe (int i)
{
    const ParamInfo& p = tbl()[i];
    char buf[200];
    if (p.numChoices > 0)
        std::snprintf (buf, sizeof buf, "%s  [%s]  choice 0..%d (%d)  def %g", p.key, p.name, p.numChoices - 1, p.numChoices, static_cast<double> (p.def));
    else
        std::snprintf (buf, sizeof buf, "%s  [%s]  %g .. %g  def %g%s", p.key, p.name, static_cast<double> (p.min), static_cast<double> (p.max), static_cast<double> (p.def), p.interval > 0.0f ? "  (stepped)" : "");
    return buf;
}

/** A value of the parameter that differs from `cur` clearly (a choice: the next one; a number: a large step toward the far end). */
float alternative (int i, float cur)
{
    const ParamInfo& p = tbl()[i];
    if (p.numChoices > 0)
        return static_cast<float> ((static_cast<int> (std::lround (cur)) + 1) % p.numChoices);
    const float mid = 0.5f * (p.min + p.max);
    float v = cur < mid ? cur + 0.6f * (p.max - p.min) : cur - 0.6f * (p.max - p.min);
    if (p.interval > 0.0f)
        v = std::round (v / p.interval) * p.interval;
    return synthyboy::constrainValue (p, v);
}

/** A page object with its instance and stack, and the two things the probes need: the hash of the picture and the Q slots. */
template <class Ui>
struct Harness
{
    Ui ui;
    FakeInstance inst;
    UiStack stack;
    double now = 1000.0;
    int instance = 1;
    explicit Harness (int element = -1)
    {
        if (element >= 0)
            stack.openFull (element);
    }
    void refresh()
    {
        now += 150.0;
        page::Input in;
        in.instance = instance;
        ui.update (inst, stack, now, in);
    }
    uint32_t pictureHash()
    {
        refresh();
        refresh();
        auto d = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*d);
        sb.begin (gpu::kKindUi, 0, 1280, 800);
        ui.draw (sb);
        return page::sceneHash (*d);
    }
    void addQ (std::set<int>& out)
    {
        for (int k = 0; k < 16; ++k)
        {
            const int t = inst.qGroup[k];
            if (t >= 0 && QRouter::modeOf (t) != QRouter::kVirtual)
            {
                const int idx = QRouter::indexOf (t);
                if (idx >= 0 && idx < count())
                    out.insert (idx);
            }
        }
    }
};

/** The oscillator page object in the same shape as Harness (the oscillator UI has its own update and draw). */
struct OscHarness
{
    OscUi ui;
    FakeInstance inst;
    UiStack stack;
    double now = 1000.0;
    void refresh()
    {
        now += 150.0;
        ui.update (inst, 1, stack, now, 1, 1);
    }
    uint32_t pictureHash()
    {
        refresh();
        refresh();
        auto d = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*d);
        sb.begin (gpu::kKindUi, 0, 1280, 800);
        ui.drawPage (sb);
        ui.drawPopup (sb);
        return page::sceneHash (*d);
    }
};

/** Parameters of `params` that change the picture of the page in some state: `make` builds the page in the state (a fresh object every time). */
template <class MakeFn>
void probeControls (const std::set<int>& params, MakeFn&& make, std::set<int>& controls)
{
    for (int p : params)
    {
        if (controls.count (p))
            continue;
        auto h = make();
        const uint32_t h0 = h->pictureHash();
        const float cur = h->inst.param (p);
        h->inst.set (tbl()[p].key, alternative (p, cur));
        if (h->pictureHash() != h0)
            controls.insert (p);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The known gaps. A parameter listed here has no control (or no Q membership) today. Remove the line when the gap is closed (the test fails until you do).
struct KnownGaps
{
    std::set<std::string> noControl, notQ, disabled;   // disabled: a control exists but is drawn disabled in every state (nothing behind it in the engine)
};
std::map<std::string, KnownGaps>& known()
{
    static std::map<std::string, KnownGaps> k;
    return k;
}
} // namespace

void initKnownGaps();   // the table of the known gaps, at the end of this file

namespace
{
int g_modules = 0;
std::string g_report;

void finish (const Module& m)
{
    ++g_modules;
    std::vector<int> noControl, controlsNotQ, qNotControl;
    int covered = 0, qCount = 0;
    for (int p : m.params)
    {
        const bool c = m.controls.count (p) > 0, q = m.qMembers.count (p) > 0;
        covered += c;
        qCount += q;
        if (! c && ! m.disabled.count (p))
            noControl.push_back (p);
        else if (c && ! q)
            controlsNotQ.push_back (p);
        if (q && ! c)
            qNotControl.push_back (p);
    }
    int disabled = 0;
    for (int p : m.disabled)
        disabled += m.params.count (p) > 0;
    char head[300];
    std::snprintf (head, sizeof head, "%s: %d parameters in the engine table, %d with a control (+ %d drawn disabled), %d Q members; no control: %d, control but no Q: %d",
                   m.name.c_str(), static_cast<int> (m.params.size()), covered, disabled, qCount, static_cast<int> (noControl.size()), static_cast<int> (controlsNotQ.size()));
    std::printf ("MODULE %s\n", head);
    g_report += std::string ("MODULE ") + head + "\n";
    std::set<std::string> gotNoControl, gotNotQ;
    for (int p : noControl)
    {
        std::printf ("  NO CONTROL  %s\n", describe (p).c_str());
        g_report += "  NO CONTROL  " + describe (p) + "\n";
        gotNoControl.insert (norm (tbl()[p].key));
    }
    for (int p : controlsNotQ)
    {
        std::printf ("  NOT Q       %s\n", describe (p).c_str());
        g_report += "  NOT Q       " + describe (p) + "\n";
        gotNotQ.insert (norm (tbl()[p].key));
    }
    std::set<std::string> gotDisabled;
    for (int p : m.disabled)
        if (m.params.count (p))
        {
            std::printf ("  DISABLED    %s\n", describe (p).c_str());
            g_report += "  DISABLED    " + describe (p) + "\n";
            gotDisabled.insert (norm (tbl()[p].key));
        }
    const KnownGaps& want = known()[m.table.empty() ? m.name : m.table];
    std::string newGaps, closed;
    for (const std::string& k : gotNoControl) if (! want.noControl.count (k)) newGaps += " " + k + "(no control)";
    for (const std::string& k : gotNotQ) if (! want.notQ.count (k)) newGaps += " " + k + "(not Q)";
    for (const std::string& k : gotDisabled) if (! want.disabled.count (k)) newGaps += " " + k + "(disabled)";
    for (const std::string& k : want.disabled) if (! gotDisabled.count (k)) closed += " " + k + "(disabled)";
    for (const std::string& k : want.noControl) if (! gotNoControl.count (k)) closed += " " + k + "(no control)";
    for (const std::string& k : want.notQ) if (! gotNotQ.count (k)) closed += " " + k + "(not Q)";
    check (newGaps.empty(), m.name + ": no new gap" + (newGaps.empty() ? "" : " - NEW:" + newGaps));
    check (closed.empty(), m.name + ": every gap in the table is still open" + (closed.empty() ? "" : " - CLOSED, remove from the table at the end of coverage_host.cpp:" + closed));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
Module testOscillator (const char* name, int sel, const char* prefix)
{
    Module m;
    m.name = name;
    m.table = sel >= 2 && sel <= 4 ? "Osc" : name;
    m.params = withPrefix ({ prefix });
    const bool sub = sel == 1, noise = sel == 5;
    std::vector<int> types = sub || noise ? std::vector<int> { 0 } : std::vector<int> { 0, 1, 2, 3, 4 };
    const int pages[] = { 0, OscUi::kPageUnison, OscUi::kPageGrain, OscUi::kPageEnvelope, OscUi::kPageRoute, 16, 17, 18, 19, 20, 21, 22 };   // 16 .. 22: the spec pages (osc_spec_page.h)
    for (int type : types)
        for (int nt = 0; nt < (noise ? 2 : 1); ++nt)
            for (int page : pages)
                if (page < 16 || (! sub && ! noise))   // the detail pages 16 .. 22 belong to A, B, C
                for (int loop = 0; loop < 3 + (page == OscUi::kPageRoute ? 7 : 0); ++loop)
                {
                    FakeInstance inst;
                    UiStack stack;
                    OscUi ui;
                    ui.setPage (sel);
                    if (! sub && ! noise)
                        inst.set ((std::string ("osc") + std::to_string (sel - 1) + ".type").c_str(), static_cast<float> (type));
                    if (noise)
                        inst.set ("noise.type", static_cast<float> (nt));
                    const std::string op = sub ? "sub." : (noise ? "noise." : "osc" + std::to_string (sel - 1) + ".");
                    if (type == 2)   // the granular loop modes show other controls
                        inst.set ((op + "gran.loop_grains").c_str(), static_cast<float> (loop == 1));
                    if (loop >= 1 && ! sub && ! noise)   // the states that show the controls hidden in the default state (loop points, slices, the grain divisions, the remap, the spectral mask ...)
                    {
                        auto set = [&] (const char* k, float v) { inst.set ((op + k).c_str(), v); };
                        set ("sample.loop_mode", 1.0f); set ("sample.slice_mode", loop == 1 ? 1.0f : 2.0f); set ("gran.density_mode", loop); set ("gran.length_mode", loop);
                        set ("remap.on", 1.0f); set ("spec.flt_use", 1.0f); set ("spec.y_axis", 1.0f); set ("gran.y_axis", 1.0f); set ("pitch_track", 0.0f);
                        set ("sample.scan_lock_tempo", 1.0f);
                    }
                    if (loop >= 1)   // another variant of every state: effects on (the warp amounts, the unison and the route controls are disabled while the effect is off)
                    {
                        for (const char* k : { "warp.type", "warp2.type" })
                            if (synthyboy::findParamByKey ((op + k).c_str()))
                                inst.set ((op + k).c_str(), 1.0f);
                        if (synthyboy::findParamByKey ((op + "unison.voices").c_str()))
                            inst.set ((op + "unison.voices").c_str(), 4.0f);
                        if (synthyboy::findParamByKey ((op + "destination").c_str()))
                            inst.set ((op + "destination").c_str(), loop >= 3 ? static_cast<float> (loop - 3) : (loop == 2 ? 4.0f : 1.0f));
                        if (synthyboy::findParamByKey ((op + "gran.manual").c_str()))
                            inst.set ((op + "gran.manual").c_str(), static_cast<float> (loop == 2));
                    }
                    double now = 1000.0;
                    auto refresh = [&] { now += 150.0; ui.update (inst, 1, stack, now, 1, 1); };
                    stack.openFull (OscUi::kElementOsc);
                    refresh();
                    if (page != 0)
                    {
                        stack.openDetail (page);
                        refresh();
                    }
                    for (const OscUi::Item& it : ui.items())
                        for (int p : { it.param, it.param2 })
                            if (p >= 0 && m.params.count (p) && it.type != OscUi::T::Inert)
                            {
                                if (it.c->enabled() && ! it.alwaysOff) m.controls.insert (p);
                                else m.disabled.insert (p);
                            }
                    for (const OscUi::Item& it : ui.pageItems())
                        for (int p : { it.param, it.param2 })
                            if (p >= 0 && m.params.count (p) && it.type != OscUi::T::Inert && (page < 16 || ui.specPage().visible (it.id - OscUi::kSpec)))
                            {
                                if (it.c->enabled() && ! it.alwaysOff) m.controls.insert (p);
                                else m.disabled.insert (p);
                            }
                    // the Q groups of the state
                    auto addQ = [&]
                    {
                        for (int k = 0; k < 16; ++k)
                        {
                            const int t = inst.qGroup[k];
                            if (t >= 0 && QRouter::modeOf (t) != QRouter::kVirtual && QRouter::indexOf (t) < count())
                                m.qMembers.insert (QRouter::indexOf (t));
                        }
                    };
                    addQ();
                    if (page == 0)
                        for (int g = 0; g < ui.full().numGroups(); ++g)
                        {
                            ui.full().setActiveGroup (g);
                            refresh();
                            addQ();
                        }
                    else if (page >= 16)
                        for (int g = 0; g < ui.specPage().numGroups(); ++g)
                        {
                            ui.specPage().setActiveGroup (g);
                            refresh();
                            addQ();
                        }
                    else
                        for (int g = 0; g < ui.detail().numGroups(); ++g)
                        {
                            ui.detail().setActiveGroup (g);
                            refresh();
                            addQ();
                        }
                }
    for (int p : m.controls)
        m.disabled.erase (p);
    // a cross-check of the control tables with the picture: a parameter without a control that changes the picture of the page would be a control the table does not know (reported, not counted)
    {
        std::string seenOnly;
        std::set<int> gaps;
        for (int p : m.params)
            if (! m.controls.count (p))
                gaps.insert (p);
        std::set<int> changed;
        for (int type : types)
            for (int variant = 0; variant < 2; ++variant)
                probeControls (gaps, [&]
                {
                    auto h = std::make_unique<OscHarness>();
                    h->ui.setPage (sel);
                    const std::string op = sub ? "sub." : (noise ? "noise." : "osc" + std::to_string (sel - 1) + ".");
                    if (! sub && ! noise) h->inst.set ((op + "type").c_str(), static_cast<float> (type));
                    if (variant) for (const char* k : { "warp.type", "warp2.type" }) if (synthyboy::findParamByKey ((op + k).c_str())) h->inst.set ((op + k).c_str(), 1.0f);
                    h->stack.openFull (OscUi::kElementOsc);
                    return h;
                }, changed);
        for (int p : changed)
            seenOnly += std::string (" ") + tbl()[p].key;
        std::printf ("INFO  %s: parameters without a control in the table whose change redraws the page (display only):%s\n", name, seenOnly.empty() ? " none" : seenOnly.c_str());
    }
    return m;
}

Module testFilter (int f)
{
    Module m;
    m.name = "Filter " + std::to_string (f);
    const std::string pre = "filter" + std::to_string (f) + ".";
    m.params = withPrefix ({ pre.c_str() });
    m.params.insert (synthyboy::findParamByKey ("filter.routing") - tbl());
    // the controls: every id of the page's table that names a parameter of this filter (the ids are what the components carry)
    {
        Harness<FilterUi> h (FilterUi::kElementFilter);
        h.instance = f;
        h.refresh();
        const int ids[] = { FilterUi::kPower, FilterUi::kModel, FilterUi::kModelList, FilterUi::kStyle, FilterUi::kClean, FilterUi::kCutoff, FilterUi::kResonance, FilterUi::kDrive, FilterUi::kMix,
                            FilterUi::kRouting, FilterUi::kBlend, FilterUi::kLevel, FilterUi::kPan, FilterUi::kKeytrack, FilterUi::kEnv1, FilterUi::kVar, FilterUi::kCombOffset,
                            FilterUi::kFormantX, FilterUi::kFormantY, FilterUi::kFormantTrans, FilterUi::kFormantRes, FilterUi::kFormantSpread, FilterUi::kDisplayCutoff, FilterUi::kDisplayFormant,
                            FilterUi::kRouteDest, FilterUi::kRouteBus1, FilterUi::kRouteBus2, FilterUi::kRouteLevel, FilterUi::kRoutePan };
        for (int id : ids)
        {
            const int p = h.ui.paramOf (id, f);
            if (p >= 0 && m.params.count (p))
                m.controls.insert (p);
        }
    }
    // the Q members: every group of the main page (formant mode, comb and the other models) and the route popup
    for (int model : { 3, 5, 6 })
        for (int g = 0; g < ui::FilterFullScreen::kMaxGroups; ++g)
        {
            Harness<FilterUi> h (FilterUi::kElementFilter);
            h.instance = f;
            h.inst.set ((pre + "model").c_str(), static_cast<float> (model));
            h.refresh();
            h.ui.full().setActiveGroup (g);
            h.refresh();
            h.addQ (m.qMembers);
        }
    {
        Harness<FilterUi> h (FilterUi::kElementFilter);
        h.instance = f;
        h.stack.openDetail (FilterUi::kPageRoute);
        h.refresh();
        h.addQ (m.qMembers);
    }
    // the same table as the picture says: every parameter that changes the picture in some model must have an id (checked, not added)
    {
        std::set<int> seen;
        probeControls (m.params, [&] { auto h = std::make_unique<Harness<FilterUi>> (FilterUi::kElementFilter); h->instance = f; return h; }, seen);
        for (int model : { 5, 6, 0 })
            probeControls (m.params, [&] { auto h = std::make_unique<Harness<FilterUi>> (FilterUi::kElementFilter); h->instance = f; h->inst.set ((pre + "model").c_str(), static_cast<float> (model)); return h; }, seen);
        std::string extra;
        for (int p : seen) if (! m.controls.count (p)) extra += std::string (" ") + tbl()[p].key;
        std::printf ("INFO  Filter %d: parameters that change the picture without an id in the control table (they feed the response display only):%s\n", f, extra.empty() ? " none" : extra.c_str());
    }
    return m;
}

Module testMixer()
{
    Module m;
    m.name = "Mixer";
    m.params = withPrefix ({ "bus1.", "bus2.", "main.", "direct." });
    // the strips of sources and filters carry their own level / pan / sends / destination / on (they are parameters of the oscillator and filter modules too): the controls are what the update reads
    Harness<MixerUi> h (MixerUi::kElementMixer);
    // controls: the picture changes with the parameter
    std::set<int> strip;
    for (int i = 0; i < count(); ++i)
    {
        const char* k = tbl()[i].key;
        if (startsWith (k, "bus1.") || startsWith (k, "bus2.") || startsWith (k, "main.") || startsWith (k, "direct.") || startsWith (k, "sub.") || startsWith (k, "osc1.") || startsWith (k, "osc2.") || startsWith (k, "osc3.")
            || startsWith (k, "noise.") || startsWith (k, "filter1.") || startsWith (k, "filter2.") || startsWith (k, "master."))
            strip.insert (i);
    }
    std::set<int> found;
    probeControls (strip, [] { return std::make_unique<Harness<MixerUi>> (MixerUi::kElementMixer); }, found);
    for (int p : found)
        m.controls.insert (p);
    std::string others;
    for (int p : found) if (! m.params.count (p)) others += std::string (" ") + tbl()[p].key;
    std::printf ("INFO  Mixer strips also write (parameters of other modules; counted there):%s\n", others.c_str());
    for (int s = 0; s < ui::MixerFullScreen::kStrips; ++s)
    {
        Harness<MixerUi> hs (MixerUi::kElementMixer);
        hs.refresh();
        hs.ui.screen().setActiveGroup (s);
        hs.refresh();
        hs.addQ (m.qMembers);
        std::set<int> all = m.qMembers;
    }
    // the strips' own parameters that are Q members (reported with the oscillator and filter modules): kept for the cross-module check
    return m;
}

Module testMacros()
{
    Module m;
    m.name = "Macros";
    m.params = withPrefix ({ "macro." });
    probeControls (m.params, [] { return std::make_unique<Harness<MacroUi>> (MacroUi::kElementMacros); }, m.controls);
    for (int bank = 0; bank < 2; ++bank)
    {
        Harness<MacroUi> h (MacroUi::kElementMacros);
        h.refresh();
        (void) bank;
        h.addQ (m.qMembers);
    }
    return m;
}

Module testMod()
{
    Module m;
    m.name = "Mod matrix";
    m.params = withPrefix ({ "mod01.", "mod02.", "mod03.", "mod04.", "mod05.", "mod06.", "mod07.", "mod08.", "mod09.", "mod1", "mod2", "mod3", "mod4", "mod5", "mod6" });
    // every slot has the same sixteen parameters (modSlotParams): the controls are probed on slot 0 and slot 63 and carried over by the position inside the slot
    std::vector<std::vector<int>> slotParams (synthyboy::kNumModSlots);
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        char pre[12];
        std::snprintf (pre, sizeof pre, "mod%02d.", s + 1);
        for (int i = 0; i < count(); ++i)
            if (startsWith (tbl()[i].key, pre))
                slotParams[static_cast<size_t> (s)].push_back (i);
    }
    auto makeRows = [] (int slot)
    {
        auto h = std::make_unique<Harness<ModUi>> (ModUi::kElementPage);
        h->inst.set ((std::string ("mod") + (slot < 9 ? "0" : "") + std::to_string (slot + 1) + ".source").c_str(), 3.0f);
        h->inst.set ((std::string ("mod") + (slot < 9 ? "0" : "") + std::to_string (slot + 1) + ".destination").c_str(), 5.0f);
        return h;
    };
    std::set<int> probed;
    for (int slot : { 0, 63 })
    {
        const std::set<int> params (slotParams[static_cast<size_t> (slot)].begin(), slotParams[static_cast<size_t> (slot)].end());
        const std::string pre = std::string ("mod") + (slot < 9 ? "0" : "") + std::to_string (slot + 1) + ".";
        for (int detail = 0; detail < 2; ++detail)
            for (int variant = 0; variant < 3; ++variant)   // the detail page shows the beats only with the tempo sync on, and the auxiliary source's controls with a source
                probeControls (params, [&]
                {
                    auto h = makeRows (slot);
                    if (variant >= 1) { h->inst.set ((pre + "aux_source").c_str(), 2.0f); h->inst.set ((pre + "amount").c_str(), 50.0f); }
                    if (variant == 2) h->inst.set ((pre + "delay_sync").c_str(), 1.0f);
                    if (detail) h->ui.openSlotDetail (slot, h->stack);
                    return h;
                }, probed);
    }
    // carry over by position
    for (int pos = 0; pos < static_cast<int> (slotParams[0].size()); ++pos)
    {
        const bool c0 = probed.count (slotParams[0][static_cast<size_t> (pos)]) > 0, c63 = probed.count (slotParams[63][static_cast<size_t> (pos)]) > 0;
        if (c0 != c63)
            std::printf ("INFO  Mod matrix: slot 1 and slot 64 differ at position %d\n", pos);
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
            if (c0 && c63 && pos < static_cast<int> (slotParams[static_cast<size_t> (s)].size()))
                m.controls.insert (slotParams[static_cast<size_t> (s)][static_cast<size_t> (pos)]);
    }
    // the Q members: the rows (AMOUNT, SOURCE, TARGET) and the blocks of the detail page, with the rows of a few slots in use
    std::set<int> qProbed;
    auto qOf = [&] (bool detail, int slot)
    {
        auto h = makeRows (slot);
        h->refresh();
        if (detail)
            h->ui.openSlotDetail (slot, h->stack);
        h->refresh();
        h->addQ (qProbed);
    };
    for (int slot : { 0, 63 })
    {
        qOf (false, slot);
        qOf (true, slot);
    }
    for (int pos = 0; pos < static_cast<int> (slotParams[0].size()); ++pos)   // the same position in every slot
        if (qProbed.count (slotParams[0][static_cast<size_t> (pos)]) && qProbed.count (slotParams[63][static_cast<size_t> (pos)]))
            for (int s = 0; s < synthyboy::kNumModSlots; ++s)
                m.qMembers.insert (slotParams[static_cast<size_t> (s)][static_cast<size_t> (pos)]);
    // the matrix with 64 slots in use: every row's group (the page publishes the selected row and the next three: walk the rows)
    {
        Harness<ModUi> h (ModUi::kElementPage);
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            char key[24];
            std::snprintf (key, sizeof key, "mod%02d.source", s + 1);
            h.inst.set (key, 3.0f);
            std::snprintf (key, sizeof key, "mod%02d.destination", s + 1);
            h.inst.set (key, 5.0f);
        }
        h.refresh();
        for (int r = 0; r < 64; ++r)
        {
            h.ui.matrix().selectRow (r);
            h.refresh();
            h.addQ (m.qMembers);
        }
    }
    return m;
}

Module testFx()
{
    Module m;
    m.name = "FX";
    // the instance-0 modules of every effect type, the pool (16 slots) and the chain order
    std::set<int> inst0;
    for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
    {
        const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
        const std::string pre = std::string (d.prefix) + ".";
        for (int i = 0; i < count(); ++i)
            if (startsWith (tbl()[i].key, pre))
                m.params.insert (i);
    }
    for (const char* extra : { "fxi", "fx.order.", "splitter_" })
        for (int i = 0; i < count(); ++i)
            if (startsWith (tbl()[i].key, extra))
                m.params.insert (i);
    // modules by their type: switch only that module on (the chain has one card, the editor shows it)
    for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
    {
        const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
        auto make = [&] { auto h = std::make_unique<Harness<FxUi>> (FxUi::kElementFx); h->inst.set (tbl()[d.onIndex].key, 1.0f); return h; };
        {
            auto h = make();
            h->refresh();
            const int rows = h->ui.screen().slotEditor.numRows();
            for (int pageFirst = 0; pageFirst < std::max (1, rows); ++pageFirst)
            {
                h->ui.screen().setActiveGroup (pageFirst);
                h->refresh();
                h->addQ (m.qMembers);
            }
        }
        std::set<int> own;
        for (int i = 0; i < count(); ++i)
            if (startsWith (tbl()[i].key, std::string (d.prefix) + "."))
                own.insert (i);
        for (int row = 0; row < 8; ++row)   // the editor shows two rows of four a page: every row
            probeControls (own, [&] { auto h = make(); h->refresh(); h->ui.screen().setActiveGroup (row); return h; }, m.controls);
    }
    // the splitters have no editor (they are not in the pool): the chain card is all there is
    for (const char* pre : { "splitter_lh.", "splitter_lmh.", "splitter_ms." })
    {
        std::set<int> own;
        for (int i = 0; i < count(); ++i)
            if (startsWith (tbl()[i].key, pre))
                own.insert (i);
        const std::string onKey = std::string (pre) + "on";
        probeControls (own, [&] { auto h = std::make_unique<Harness<FxUi>> (FxUi::kElementFx); h->inst.set (onKey.c_str(), 1.0f); return h; }, m.controls);
    }
    // the pool: a slot of every type; its parameters are the generic p01 .. p24 (the first `count` of them mean something)
    for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
    {
        const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
        const synthyboy::FxInstanceKeys& k = synthyboy::fxInstanceKeys (0);
        auto make = [&] { auto h = std::make_unique<Harness<FxUi>> (FxUi::kElementFx); h->inst.set (tbl()[k.type].key, static_cast<float> (t)); h->inst.set (tbl()[k.on].key, 1.0f); return h; };
        {
            auto h = make();
            h->refresh();
            const int rows = h->ui.screen().slotEditor.numRows();
            for (int r = 0; r < std::max (1, rows); ++r)
            {
                h->ui.screen().setActiveGroup (r);
                h->refresh();
                h->addQ (m.qMembers);
            }
        }
        std::set<int> own { k.type, k.on, k.rack, k.order };
        for (int p = 0; p < d.count; ++p)
            own.insert (k.p[p]);
        for (int row = 0; row < 8; ++row)
            probeControls (own, [&] { auto h = make(); h->refresh(); h->ui.screen().setActiveGroup (row); return h; }, m.controls);
    }
    // the pool's slots 2 .. 16 are the same keys at another number: the slot-1 result carries over by position
    {
        const synthyboy::FxInstanceKeys& k0 = synthyboy::fxInstanceKeys (0);
        const int slotKeys = 4 + synthyboy::kFxInstanceParams;
        std::vector<int> pos0;
        pos0.push_back (k0.type); pos0.push_back (k0.on); pos0.push_back (k0.rack); pos0.push_back (k0.order);
        for (int p = 0; p < synthyboy::kFxInstanceParams; ++p) pos0.push_back (k0.p[p]);
        for (int s = 1; s < synthyboy::kNumFxInstances; ++s)
        {
            const synthyboy::FxInstanceKeys& ks = synthyboy::fxInstanceKeys (s);
            int pos[28] = { ks.type, ks.on, ks.rack, ks.order };
            for (int p = 0; p < synthyboy::kFxInstanceParams; ++p) pos[4 + p] = ks.p[p];
            for (int i = 0; i < slotKeys; ++i)
                if (m.controls.count (pos0[static_cast<size_t> (i)]) && pos[i] >= 0)
                    m.controls.insert (pos[i]);
                else if (m.qMembers.count (pos0[static_cast<size_t> (i)]) && pos[i] >= 0)
                    m.qMembers.insert (pos[i]);
            for (int i = 0; i < slotKeys; ++i)
                if (m.qMembers.count (pos0[static_cast<size_t> (i)]) && pos[i] >= 0)
                    m.qMembers.insert (pos[i]);
        }
    }
    // the order of the chain (fx.order.N of the 16 modules, fxiNN.order of the pool's slots) is written by the drag of a card on the chain bar (FxUi::handle kMove: synthyboy::fxMove writes the orders it has to
    // change); no single control shows one of these values, so the picture of a one-card chain does not change with them. They count as controlled by that gesture.
    for (int i = 0; i < count(); ++i)
        if (startsWith (tbl()[i].key, "fx.order.") || (startsWith (tbl()[i].key, "fxi") && std::strstr (tbl()[i].key, ".order") != nullptr))
            m.controls.insert (i);
    return m;
}
} // namespace

int main()
{
    std::printf ("=== SynthyBoy MPC Overlay: parameter coverage of the pages without a test of their own ===\n");
    initKnownGaps();
    std::vector<Module> mods;
    mods.push_back (testOscillator ("Osc A", 2, "osc1."));
    mods.push_back (testOscillator ("Osc B", 3, "osc2."));
    mods.push_back (testOscillator ("Osc C", 4, "osc3."));
    mods.push_back (testOscillator ("Sub", 1, "sub."));
    mods.push_back (testOscillator ("Noise", 5, "noise."));
    mods.push_back (testFilter (1));
    mods.push_back (testFilter (2));
    mods.push_back (testFx());
    mods.push_back (testMod());
    mods.push_back (testMacros());
    mods.push_back (testMixer());
    for (const Module& m : mods)
        finish (m);
    // the whole table: what no page has at all. The pages with a test of their own (Env 1 - 6, LFO 1 - 10, Random LFO 1 - 4, Master, Voice, Voice control) count as covering their prefixes: their tests fail otherwise.
    {
        Module all;
        all.name = "Whole table";
        for (int i = 0; i < count(); ++i)
            all.params.insert (i);
        for (const Module& m : mods)
        {
            all.controls.insert (m.controls.begin(), m.controls.end());
            all.controls.insert (m.disabled.begin(), m.disabled.end());   // a control drawn disabled is on a page
        }
        for (int i = 0; i < count(); ++i)
        {
            const char* k = tbl()[i].key;
            for (const char* pre : { "env1.", "env2.", "env3.", "env4.", "env5.", "env6.", "lfo", "rlfo", "master.", "voice.", "voicectl." })
                if (startsWith (k, pre))
                    all.controls.insert (i);
        }
        all.qMembers = all.controls;   // (Q membership is checked per module above)
        std::printf ("TOTAL  engine table %d parameters, %d with a control on some page, %d on no page at all\n", count(), static_cast<int> (all.controls.size()), count() - static_cast<int> (all.controls.size()));
        finish (all);
    }
    std::printf ("TOTALS BY MODULE\n");
    for (const Module& m : mods)
    {
        int c = 0, q = 0;
        for (int p : m.params) { c += m.controls.count (p) > 0; q += m.qMembers.count (p) > 0; }
        int d = 0;
        for (int p : m.disabled) d += m.params.count (p) > 0;
        std::printf ("  %-12s total %4d  control %4d  disabled %2d  no control %4d  Q member %4d\n", m.name.c_str(), static_cast<int> (m.params.size()), c, d, static_cast<int> (m.params.size()) - c - d, q);
    }
    std::printf (g_fails == 0 ? "coverage_host: OK (%d modules)\n" : "coverage_host: FAILED (%d modules)\n", g_modules);
    return g_fails == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// THE KNOWN GAPS (the functionality still to build; the report with ranges and the place on the page: .hw-local/mpc-ui-plans/coverage-gaps.md). Keys are written with the number of the instance replaced:
// oscN. (the three oscillators have the same gaps), modNN. (the 64 slots), fxiNN. (the 16 pool slots). noControl: no control on the page in any state. notQ: a control, but not a member of any Q group.
// disabled: a control that is drawn disabled in every state (nothing behind it in the engine). "Whole table": the parameters on no page at all.
void initKnownGaps()
{
    // Osc: no gap left (every one of the 143 parameters has a control and is a Q member; the pages are in osc_spec_page.h, docs/MPC.md "Oscillator UI")
    known()["Osc"].noControl = {};
    known()["Osc"].notQ = {};
    known()["Osc"].disabled = {};
    // Sub notQ: 1
    known()["Sub"].notQ = {
        "sub.on"
    };
    // Noise notQ: 1
    known()["Noise"].notQ = {
        "noise.on"
    };
    // Filter 1 notQ: 5
    known()["Filter 1"].notQ = {
        "filter1.on", "filter1.model", "filter1.style", "filter1.formant.resonance", "filter1.clean_mode"
    };
    // Filter 2 notQ: 5
    known()["Filter 2"].notQ = {
        "filter2.on", "filter2.model", "filter2.style", "filter2.formant.resonance", "filter2.clean_mode"
    };
    // FX noControl: 14
    known()["FX"].noControl = {
        "reverb.force_enable", "splitter_lh.freq", "splitter_lh.low_bypass", "splitter_lh.high_bypass", "splitter_lh.level", "splitter_lmh.freq_low",
        "splitter_lmh.freq_high", "splitter_lmh.low_bypass", "splitter_lmh.mid_bypass", "splitter_lmh.high_bypass", "splitter_lmh.level", "splitter_ms.mid_bypass",
        "splitter_ms.side_bypass", "splitter_ms.level"
    };
    // FX notQ: 52
    known()["FX"].notQ = {
        "fxfilter.on", "fx.order.1", "fx.order.2", "fx.order.3", "fx.order.4", "fx.order.5", "fx.order.6", "fx.order.7", "fx.order.8", "fx.order.9", "chorus.on",
        "compressor.on", "delay.on", "distortion.on", "eq.on", "flanger.on", "phaser.on", "reverb.on", "fx.order.10", "fx.order.11", "fx.order.12", "fx.order.13",
        "fx.order.14", "fx.order.15", "fx.order.16", "chorus.rack", "compressor.rack", "delay.rack", "distortion.rack", "eq.rack", "fxfilter.rack", "flanger.rack",
        "phaser.rack", "reverb.rack", "hyperdim.rack", "utility.rack", "bode.rack", "convolve.rack", "splitter_lh.rack", "splitter_lmh.rack", "splitter_ms.rack",
        "hyperdim.on", "utility.on", "bode.on", "convolve.on", "splitter_lh.on", "splitter_lmh.on", "splitter_ms.on", "fxiNN.type", "fxiNN.on", "fxiNN.rack",
        "fxiNN.order"
    };
    // Mod matrix notQ: 3
    known()["Mod matrix"].notQ = {
        "modNN.bipolar", "modNN.bypass", "modNN.delay_beats"
    };
    // Mixer noControl: 1
    known()["Mixer"].noControl = {
        "direct.level"
    };
    // Mixer notQ: 5
    known()["Mixer"].notQ = {
        "bus1.destination", "bus2.destination", "bus1.fx_bypass", "bus2.fx_bypass", "main.fx_bypass"
    };
    // Whole table noControl: 19
    known()["Whole table"].noControl = {
        "curve.velocity.legato", "curve.note.legato", "direct.level", "reverb.force_enable", "splitter_lh.freq",
        "splitter_lh.low_bypass", "splitter_lh.high_bypass", "splitter_lh.level", "splitter_lmh.freq_low", "splitter_lmh.freq_high",
        "splitter_lmh.low_bypass", "splitter_lmh.mid_bypass", "splitter_lmh.high_bypass", "splitter_lmh.level", "splitter_ms.mid_bypass",
        "splitter_ms.side_bypass", "splitter_ms.level", "quality.oversample_4x", "quality.no_smoothing"
    };

}
