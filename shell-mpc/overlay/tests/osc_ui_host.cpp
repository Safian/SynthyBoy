// SynthyBoy - MPC overlay tests: the oscillator UI (osc_ui.h): the wiring of every control to the engine parameters (both directions), the pages of the UiStack, the Q group, the modulations, the
// controls that are disabled because the engine has nothing behind them, the touch path (decoder events -> components -> parameters) and the drawing (one scene, inside the canvas).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "../osc_ui.h"
#include "../scene_cpu.h"

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

/** A plugin instance made of an array of plain values: writeParam converts like the plugin does and counts the writes (the host notification). */
class Inst final : public IInstanceView
{
public:
    Inst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }
    float param (int i) const noexcept override { return v_[i]; }
    bool live (synthyboy::LiveValues&) const noexcept override { return false; }
    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return sel; }
    void setQGroup (const int* t, int n) noexcept override { for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1; ++qCalls; }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setPlaySelection (int g, int i) noexcept override { if (g == 0) sel = i; ++selWrites; }
    int playLastSelector() const noexcept override { return lastSel; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    void contentRoots (std::vector<ContentRoot>& out) const override { out = roots; }
    void requestContent (int osc, int kind, const std::string& path) override { requests.push_back ({ osc, kind, path }); loadState = 1; }
    int contentLoad (std::string* m) const override { if (m != nullptr) *m = loadMessage; return loadState; }
    void wavetableSources (synthyboy::CatalogueSources& out) const override { out = sources; }
    void requestWavetable (int osc, const synthyboy::ContentFileRef& ref) override { wtRequests.push_back ({ osc, ref }); loadState = 1; }
    int takeWavetableSteps (int) noexcept override { const int n = wtSteps; wtSteps = 0; return n; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    struct WtRequest { int osc; synthyboy::ContentFileRef ref; };
    std::vector<WtRequest> wtRequests;
    synthyboy::CatalogueSources sources;
    std::string loadMessage;
    int wtSteps = 0;
    unsigned missingContent() const noexcept override { return missingBits; }
    unsigned missingBits = 0;   // the engine's missing_content bits
    struct Request { int osc, kind; std::string path; };
    std::vector<ContentRoot> roots;
    std::vector<Request> requests;
    int loadState = 0;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }
    int sel = 2, lastSel = 0, q[4] = { -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0;
    bool fullOpen = false;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }
constexpr int kOY = 0;   // the canvas is the screen: the rectangles of the components are in screen pixels

struct Rig
{
    OscUi ui;
    Inst inst;
    UiStack stack;
    double now = 1000.0;
    Rig()
    {
        for (const char* key : { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on" })
            inst.set (key, 1.0f);
        refresh();
    }
    void refresh (int hl = 1) { now += 50.0; ui.update (inst, hl, stack, now, 1, 1); }
    bool tap (int cx, int cy, int holdMs = 40)
    {
        const bool a = ui.touch (ev (TouchEvent::Down, cx, cy + kOY), stack, inst, now);
        now += holdMs;
        ui.tick (now, stack, inst);
        ui.touch (ev (TouchEvent::Up, cx, cy + kOY), stack, inst, now);
        now += 100.0;
        refresh();
        return a;
    }
    void tapC (const ui::ControlModel& c, int holdMs = 40) { tap (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs); }
    void drag (int x0, int y0, int x1, int y1, int steps = 10)
    {
        ui.touch (ev (TouchEvent::Down, x0, y0 + kOY), stack, inst, now);
        for (int i = 1; i <= steps; ++i)
        {
            now += 16.0;
            ui.touch (ev (TouchEvent::Move, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps + kOY), stack, inst, now);
        }
        ui.touch (ev (TouchEvent::Up, x1, y1 + kOY), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void centre (const ui::ControlModel& c, int& x, int& y) { x = c.rect().x + c.rect().w / 2; y = c.rect().y + c.rect().h / 2; }
    void openFull() { tapC (ui.compact().full); }
};

void testEveryControlHasAParameterOrIsDisabled()
{
    for (int sel = 1; sel <= 5; ++sel)
        for (int raw : { 0, 4, 2, 1, 3 })
        {
            if ((sel == 1 || sel == 5) && raw != 0)
                continue;
            Rig r;
            r.inst.sel = sel;
            if (sel >= 2 && sel <= 4)
                r.inst.set ("osc" + std::to_string (sel - 1) + ".type", static_cast<float> (raw));
            r.refresh();
            const std::string name = "instance " + std::to_string (sel) + " type " + std::to_string (raw);
            int unbound = 0;
            std::string which;
            for (const OscUi::Item& it : r.ui.items())
            {
                if (it.type == OscUi::T::Inert || it.type == OscUi::T::Source)   // a source of content: no parameter, a file
                    continue;
                if (it.param < 0 && it.c->enabled())
                {
                    ++unbound;
                    which += " " + std::to_string (it.id);
                }
            }
            check (unbound == 0, name + ": every control has an engine parameter or is disabled (enabled without one:" + which + ")");
            check (r.ui.selection() == sel, name + ": the selection follows the Play Osc selector");
        }
}

void testNoiseBasicQ1IsNoiseSample()
{
    Rig r;
    r.inst.sel = 5;
    r.refresh();
    int ct[4];
    r.ui.compactTargets (ct);   // the main state's Q group is published by the Play view (PlayView::publishQ): the oscillator's compact group is what it asks for
    check (ct[0] == Inst::idx ("noise.sample") && ct[1] == Inst::idx ("noise.filter") && ct[2] == Inst::idx ("noise.pan") && ct[3] == Inst::idx ("noise.level"),
           "noise: the Basic group is noise.sample, noise.filter, noise.pan, noise.level (not noise.type)");
}

void testBasicGroupsAndSelection()
{
    Rig r;
    r.ui.compact().setNativeInstanceSelector (true);   // Exercise the overlay-owned control.

    int ct[4];
    r.ui.compactTargets (ct);
    check (ct[0] == Inst::idx ("osc1.type") && ct[1] == Inst::idx ("osc1.wavetable") && ct[2] == Inst::idx ("osc1.pan") && ct[3] == Inst::idx ("osc1.level"),
           "A: the Basic group is type, wavetable, pan, level of osc1");;
    r.inst.sel = 1;
    r.refresh();
    r.ui.compactTargets (ct);
    check (ct[0] == Inst::idx ("sub.waveform") && ct[1] == Inst::idx ("sub.octave") && ct[2] == Inst::idx ("sub.pan") && ct[3] == Inst::idx ("sub.level"), "Sub: waveform, octave, pan, level");
    r.inst.sel = 4;
    r.refresh();
    r.ui.compactTargets (ct);
    check (ct[0] == Inst::idx ("osc3.type") && ct[2] == Inst::idx ("osc3.pan"), "C is osc3");
    // the compact view's source selector (the one selector of the three thirds: a stepper with a list) writes the Play Osc selector
    r.inst.sel = 2;
    r.refresh();
    const ui::Rect c = r.ui.compact().instances.rect();
    check (c.h >= 56 && std::string (r.ui.compact().instances.state.label) == "Osc A" && r.ui.compact().instances.state.icon == ui::MenuButton::Icon::Down, "the selector shows the current source in the owner's wording (Osc A) and has a list");
    const int writesBefore = r.inst.writes;
    r.tap (c.x + c.w * 7 / 8, c.y + c.h / 2);
    check (r.ui.popupOpen() && r.inst.sel == 2, "dropdown arrow opens the list without changing selection");
    {
        const ui::ListPopup& l = r.ui.list();
        const char* const want[5] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" };
        bool names = l.state.count == 5 && l.state.current == 1;
        const char* const powerKeys[] = { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on" };
        for (int i = 0; i < 5 && names; ++i)
        {
            const std::string label = std::string (want[i]) + (r.inst.get (powerKeys[i]) >= 0.5f ? "   ON" : "   OFF");
            names = l.state.items[i] == label && l.rowRect (i).h >= 56;
        }
        check (r.stack.isMain() && names, "a tap on the name opens the list: Sub, Osc A, Osc B, Osc C, Noise (rows >= 56 px), A is the current one, no page of the stack opened");
    }
    {
        const ui::Rect row = r.ui.list().rowRect (4);
        r.tap (row.x + row.w / 2, row.y + row.h / 2);
    }
    check (r.inst.sel == 5 && r.inst.writes == writesBefore, "tap on the fifth row of the list: Play Osc = Noise");
    r.refresh();
    check (r.ui.selection() == 5, "the UI follows the selector");
}

void testFullScreenOpensAndCloses()
{
    Rig r;
    check (r.stack.isMain() && ! r.inst.fullOpen, "main: no full screen");
    r.openFull();
    check (r.stack.depth() == 1 && r.inst.fullOpen, "the full-screen button opens the full screen and tells the plugin");
    check (r.ui.displayRect().w > 500, "the full screen has a display rectangle");
    check (r.ui.plotArea().empty(), "the full screen's picture has no plot area (it fills its display rectangle)");
    r.tapC (r.ui.full().close);
    check (r.stack.isMain() && r.ui.plotArea() == ui::playrows::plotRect (r.ui.displayRect()) && r.ui.plotArea().y >= ui::playrows::bandRect (r.ui.displayRect()).bottom() && ! r.ui.plotArea().overlaps (r.ui.compact().full.rect()) && ! r.ui.plotArea().overlaps (r.ui.compact().power.rect()) && (r.ui.compact().route.rect().empty() || ! r.ui.plotArea().overlaps (r.ui.compact().route.rect())),
           "the compact view: the picture's plot area is the common one, under the header band; the buttons (power, route, full screen) stand in the band");
    check (r.stack.isMain() && ! r.inst.fullOpen, "the close button is back (main again, the plugin is told)");
    // navigating away
    r.openFull();
    r.stack.closeAll();
    r.refresh();
    check (! r.inst.fullOpen, "the stack closed from outside (the page left): the plugin is told");
}

void testKnobDragWritesTheParameter()
{
    Rig r;

    r.inst.set ("osc1.level", 20.0f);
    r.refresh();
    int x, y;
    r.centre (r.ui.compact().basic.level, x, y);
    const int before = r.inst.writes;
    r.drag (x, y, x, y - 60);
    check (r.inst.get ("osc1.level") == 20.0f, "native LEVEL drag leaves the overlay value unchanged");
    check (r.inst.writes == before && ! r.ui.covers (x, y + kOY), "native LEVEL passes input through with no overlay write");
    // the other direction: the parameter changes behind the UI's back
    r.inst.set ("osc1.level", 80.0f);
    r.refresh();
    check (std::fabs (r.ui.compact().basic.level.state.value - 0.8) < 1e-6, "a parameter change from outside shows in the knob on the next frame");
}

void testPanBipolarAndPower()
{
    Rig r;

    int x, y;
    r.centre (r.ui.compact().basic.pan, x, y);
    check (std::fabs (r.ui.compact().basic.pan.state.value - 0.5) < 1e-6 && r.ui.compact().basic.pan.state.bipolar, "PAN: centre value 0.5, bipolar");
    r.drag (x, y, x, y - 40);
    check (r.inst.get ("osc1.pan") == 0.0f && ! r.ui.covers (x, y + kOY), "native PAN passes input through with no overlay write");
    r.inst.set ("osc1.on", 1.0f);
    r.refresh();
    r.tapC (r.ui.compact().power);
    check (r.inst.get ("osc1.on") == 0.0f, "the power toggle turns the oscillator off");
}

void testTypeSelectorAndKinds()
{
    Rig r;
    r.openFull();
    // five cells: wavetable, multisample, sample, granular, spectral
    auto cell = [&] (int i) { const ui::Rect q = r.ui.full().top.selector.rect(); r.tap (q.x + q.w * (2 * i + 1) / 10, q.y + q.h / 2); };
    cell (3);
    check (r.inst.get ("osc1.type") == 2.0f, "the fourth pictogram is Granular (osc1.type = 2)");
    r.refresh();
    check (r.ui.full().kind() == ui::OscKind::Granular && r.ui.full().panel.preset() == &ui::presets::kGranularFull, "the full screen switched to the granular layout");
    cell (1);
    check (r.inst.get ("osc1.type") == 4.0f, "the second pictogram is Multisample (osc1.type = 4)");
    cell (2);
    check (r.inst.get ("osc1.type") == 1.0f, "the third pictogram is Sample (osc1.type = 1)");
    r.refresh();
    check (std::string (r.ui.typeName()) == "Sample" && r.ui.full().kind() == ui::OscKind::Sample && r.ui.full().panel.preset() == &ui::presets::kSampleFull && r.ui.full().top.pos.state.label == std::string ("SCAN"), "type Sample: its own full screen (top row with SCAN, bottom band)");
    check (r.ui.full().top.selector.state.index == 2, "the Sample pictogram is highlighted");
    cell (4);
    check (r.inst.get ("osc1.type") == 3.0f, "the fifth pictogram is Spectral (osc1.type = 3)");
    r.refresh();
    check (r.ui.full().kind() == ui::OscKind::Spectral && r.ui.full().panel.preset() == &ui::presets::kSpectralFull && r.ui.full().top.selector.state.index == 4, "type Spectral: its own full screen");
}

/** The item of the MORE page (the one page of the pitch, the unison blend, the second warp ...) of a parameter key (without the oscillator prefix). */
const OscUi::Item* moreItem (Rig& r, const char* key)
{
    const int idx = Inst::idx ((std::string ("osc1.") + key).c_str());
    for (const OscUi::Item& it : r.ui.pageItems())
        if (it.param == idx && it.c != nullptr)
            return &it;
    return nullptr;
}
void openMore (Rig& r)
{
    r.openFull();
    r.tapC (r.ui.full().more);
}
/** The page button of the page that is shown (MORE) with the token of its key ("map", "region" ...); null when the page has none (or it is hidden). */
const OscUi::Item* pageButton (Rig& r, const char* token)
{
    const oscspec::Spec* sp = r.ui.specPage().spec();
    for (int i = 0; sp != nullptr && i < sp->numCtls; ++i)
        if (sp->ctls[i].kind == oscspec::K::Page && std::string (sp->ctls[i].key) == token && r.ui.specPage().visible (i))
            return r.ui.item (OscUi::kSpec + i);
    return nullptr;
}

void testTuneOctSem()
{
    Rig r;
    r.inst.set ("osc1.transpose", 19.0f);
    r.refresh();
    r.openFull();
    auto& oct = r.ui.full().top.oct;
    check (oct.state.value == 1.0, "transpose 19 = OCT 1 (the full screen's default view has the octaves)");
    r.inst.set ("osc1.transpose", -5.0f);
    r.refresh();
    check (oct.state.value == 0.0, "transpose -5 = OCT 0");
    r.inst.set ("osc1.transpose", 19.0f);
    r.refresh();
    int x, y;
    r.centre (oct, x, y);
    r.drag (x, y, x, y - 9, 3);   // up: one step (8 px per step)
    const float tr = r.inst.get ("osc1.transpose");
    check (static_cast<int> (tr) % 12 == 7 && tr >= 31.0f, "OCT up keeps the SEM remainder: transpose " + std::to_string (tr));
    check (OscUi::transposeFrom (4, 12) == 48 && OscUi::transposeFrom (-4, -12) == -48, "OCT / SEM clamp at +-48");
    check (r.inst.get ("osc1.transpose") == std::floor (r.inst.get ("osc1.transpose")), "transpose stays whole");
    // FIN is in the default view; SEM, CRS and the pitch tracking toggle are behind MORE
    r.inst.set ("osc1.tune", 12.0f); r.inst.set ("osc1.pitch_coarse", -3.5f); r.inst.set ("osc1.transpose", 19.0f); r.refresh();
    check (r.ui.full().panel.number (ui::PanelSlot::Fin) != nullptr && r.ui.full().panel.number (ui::PanelSlot::Fin)->state.value == 12.0, "FIN = osc1.tune (default view)");
    r.tapC (r.ui.full().more);
    check (r.stack.depth() == 2 && r.stack.top() == oscspec::kMore, "the MORE button of the full screen opens the MORE page");
    const OscUi::Item* sem = moreItem (r, "transpose");
    const OscUi::Item* crs = moreItem (r, "pitch_coarse");
    const OscUi::Item* trk = moreItem (r, "pitch_track");
    check (sem != nullptr && sem->type == OscUi::T::NumberSem && static_cast<ui::NumberField*> (sem->c)->state.value == 7.0 && crs != nullptr && static_cast<ui::NumberField*> (crs->c)->state.value == -3.5 && trk != nullptr,
           "MORE: SEM (the semitones of the transpose: 19 = OCT 1, SEM 7), CRS = osc1.pitch_coarse, TRACK");
    r.inst.set ("osc1.pitch_track", 1.0f);
    r.refresh();
    r.tapC (*trk->c);
    check (r.inst.get ("osc1.pitch_track") == 0.0f, "the pitch tracking toggle (MORE) writes osc1.pitch_track");
    r.tapC (r.ui.specPage().back);
    check (r.stack.depth() == 1, "BACK: the full screen");
}

void testDisabledControls()
{
    Rig r;
    openMore (r);
    const OscUi::Item* map = nullptr;
    for (const OscUi::Item& it : r.ui.pageItems())
        if (it.type == OscUi::T::Inert && it.c != nullptr && it.c->enabled() && r.ui.specPage().spec() != nullptr)
            map = &it;
    check (map != nullptr, "wavetable: the MORE page has the page buttons (MAP, OPTIONS)");
    const OscUi::Item* ph = moreItem (r, "warp.phase");
    check (ph == nullptr || ! r.ui.specPage().visible (ph->id - OscUi::kSpec), "wavetable: the PHASE of a warp that is Off is hidden on MORE (not dimmed)");
    r.inst.set ("osc1.warp.type", 3.0f);
    r.refresh();
    ph = moreItem (r, "warp.phase");
    check (ph != nullptr && ph->param == Inst::idx ("osc1.warp.phase") && r.ui.specPage().visible (ph->id - OscUi::kSpec), "wavetable: the PHASE of warp 1 (osc1.warp.phase) shows on MORE once warp 1 has a type");
    r.tapC (r.ui.specPage().back);
    const OscUi::Item* drop = r.ui.item (OscUi::kBasic + 2);
    check (drop != nullptr && drop->c->enabled() && r.ui.compact().basic.source.stepper.listable, "the table of the compact view opens the wavetable browser (its name is listable; the full screen opens first)");
    // the source stepper of multisample is content
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    const OscUi::Item* src = r.ui.item (OscUi::kBasic + 1);
    check (src != nullptr && src->c->enabled() && src->type == OscUi::T::Source && src->param < 0, "multisample: the source (content, not a parameter) shows the loaded file and opens the browser");
}

void testRoutePopup()
{
    {
        Rig c;   // the route button of the compact view opens the full screen and the popup over it
        c.tapC (c.ui.compact().route);
        check (c.stack.depth() == 2 && c.stack.top() == OscUi::kPageRoute, "the route button of the compact view opens the full screen and the route popup");
    }
    Rig r;
    r.openFull();
    check (r.ui.full().mix.touchTargets (nullptr, 0) == 0, "the full screen has no route button: the destination is a read-only indicator (the Mixer is its home)");
    r.stack.openDetail (OscUi::kPageRoute);   // (what the compact view's route button does)
    r.refresh();
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageRoute, "the route popup");
    namespace n = nativeui::oscdetail;
    const auto& state = r.ui.prepareNativeDetail (true, 0);
    check (state.frame == n::Plain && state.fields[0].target == Inst::idx ("osc1.destination"), "native routing edits OSC A");
    check (state.fields[n::slot (1, 0)].target < 0 && r.inst.q[1] < 0, "balance unavailable for Filter 1");
    r.inst.set ("osc1.destination", 5.0f);
    r.refresh();
    r.ui.prepareNativeDetail (true, 0);
    check (state.fields[n::slot (1, 0)].target == Inst::idx ("osc1.filter_blend"), "Filter blend exposes its native dial");
    check (r.inst.q[2] == Inst::idx ("osc1.bus1_send") && r.inst.q[3] == Inst::idx ("osc1.bus2_send"), "send Q-Links follow the selected source");
    r.ui.applyNativeDetail (n::kCells, 1.0f, r.inst, r.stack);
    check (r.inst.get ("osc1.destination") == 6.0f, "native stepper selects the next destination");
    r.ui.applyNativeDetail (n::kBack, 1.0f, r.inst, r.stack);
    check (r.stack.depth() == 1, "native back returns to the editor");
}

void testDetailPages()
{
    Rig r;
    r.inst.set ("osc1.unison.mode", 2.0f);
    r.refresh();
    r.openFull();
    auto* box = r.ui.full().panel.unison();
    check (box != nullptr, "wavetable full screen has the unison box");
    r.tapC (box->gear);
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageUnison, "the gear opens the unison page");
    check (std::string (r.ui.detail().panel.stepper (ui::PanelSlot::Mode)->state.text) == "Exp", "MODE shows the engine's choice label of osc1.unison.mode");
    check (r.inst.q[0] == Inst::idx ("osc1.unison.mode") && r.inst.q[1] == Inst::idx ("osc1.unison.stack") && r.inst.q[2] == Inst::idx ("osc1.unison.spread") && r.inst.q[3] == Inst::idx ("osc1.unison.range"), "the unison page's first group: mode, stack, spread (WIDTH), range");
    ui::Stepper* st = r.ui.detail().panel.stepper (ui::PanelSlot::Mode);
    r.tap (st->rect().x + st->rect().w - 10, st->rect().y + st->rect().h / 2);
    check (r.inst.get ("osc1.unison.mode") == 3.0f, "MODE steps the unison mode");
    ui::Knob* w = r.ui.detail().panel.knob (ui::PanelSlot::Width);
    int x, y;
    r.centre (*w, x, y);
    r.inst.set ("osc1.unison.spread", 10.0f);
    r.refresh();
    r.drag (x, y, x, y - 40);
    check (r.inst.get ("osc1.unison.spread") > 20.0f, "WIDTH = osc1.unison.spread, a drag changes it");
    r.tapC (r.ui.detail().back);
    check (r.stack.depth() == 1, "BACK returns to the full screen");
    // granular: the buttons of MORE open the grain window page and the unison page; LOOP and the random grain values are on MORE
    r.inst.set ("osc1.type", 2.0f);
    r.refresh();
    check (r.ui.full().panel.part (ui::PanelSlot::Dens) != nullptr && r.ui.full().panel.warp() == nullptr, "granular full screen: the GRAIN and PLAYBACK blocks");
    r.tapC (r.ui.full().more);
    const OscUi::Item* loop = moreItem (r, "gran.loop_grains");
    check (loop != nullptr && loop->type == OscUi::T::Toggle && r.ui.specPage().visible (loop->id - OscUi::kSpec), "granular: LOOP is on MORE");
    r.tapC (*loop->c);
    check (r.inst.get ("osc1.gran.loop_grains") == 1.0f, "LOOP = gran.loop_grains");
    check (moreItem (r, "gran.rand_length") != nullptr && moreItem (r, "gran.rand_pan") != nullptr && moreItem (r, "gran.rand_level") != nullptr && moreItem (r, "unison.voices") != nullptr, "granular MORE: LEN RAND, PAN RAND, LVL RAND and the unison voices");
    // the grain window button
    const OscUi::Item* gw = nullptr;
    for (int i = 0; i < oscspec::OscSpecPage::kMaxCtl; ++i)
        if (r.ui.specPage().spec() != nullptr && i < r.ui.specPage().spec()->numCtls && r.ui.specPage().spec()->ctls[i].kind == oscspec::K::Page && std::string (r.ui.specPage().spec()->ctls[i].key) == "window")
            gw = r.ui.item (OscUi::kSpec + i);
    check (gw != nullptr && r.ui.specPage().visible (gw->id - OscUi::kSpec), "granular MORE: the WINDOW button");
    r.tapC (*gw->c);
    check (r.stack.depth() == 3 && r.stack.top() == OscUi::kPageGrain, "the WINDOW button opens the grain window page");
    check (r.inst.q[0] == Inst::idx ("osc1.gran.window_amount") && r.inst.q[1] == Inst::idx ("osc1.gran.rand_window") && r.inst.q[2] == Inst::idx ("osc1.gran.window_skew") && r.inst.q[3] == Inst::idx ("osc1.gran.rand_skew"), "the grain window page's group");
    r.tapC (r.ui.detail().back);
    check (r.stack.top() == oscspec::kMore, "BACK from a page opened by MORE: MORE again");
    r.tapC (r.ui.specPage().back);
    // multisample envelope
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    r.tapC (r.ui.full().more);
    const OscUi::Item* env = nullptr;
    for (int i = 0; i < oscspec::OscSpecPage::kMaxCtl; ++i)
        if (r.ui.specPage().spec() != nullptr && i < r.ui.specPage().spec()->numCtls && r.ui.specPage().spec()->ctls[i].kind == oscspec::K::Page && std::string (r.ui.specPage().spec()->ctls[i].key) == "env")
            env = r.ui.item (OscUi::kSpec + i);
    check (env != nullptr, "multisample MORE: the ENV button");
    r.tapC (*env->c);
    check (r.stack.top() == OscUi::kPageEnvelope && r.ui.detail().panel.toggle (ui::PanelSlot::Override) != nullptr, "multisample: the envelope button opens the envelope page");
    r.inst.set ("osc1.multi.override", 0.0f);
    r.refresh();
    r.tapC (*r.ui.detail().panel.toggle (ui::PanelSlot::Override));
    check (r.inst.get ("osc1.multi.override") == 1.0f, "OVERRIDE = osc1.multi.override");
    check (r.inst.q[0] == Inst::idx ("osc1.multi.delay") && r.inst.q[3] == Inst::idx ("osc1.multi.decay"), "the envelope page's group: delay, attack, hold, decay");
}

void testNavigatingAwayClosesEverything()
{
    Rig r;
    r.openFull();
    r.stack.openDetail (OscUi::kPageRoute);
    r.refresh();
    check (r.stack.depth() == 2, "a popup is open");
    r.stack.closeAll();   // what the controller does when the Play page leaves the screen
    r.refresh();
    check (r.stack.isMain() && r.ui.topPage() == 0 && ! r.inst.fullOpen, "after closeAll: main, the compact view, the plugin is told");
}

void testHiddenModulationRoute()
{
    Rig r;
    const size_t destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("osc1.frame")));
    const auto set = [&] (int slot, int source)
    {
        const auto sp = synthyboy::modSlotParams (slot);
        r.inst.set (synthyboy::paramTable()[sp.destination].key, static_cast<float> (destination));
        r.inst.set (synthyboy::paramTable()[sp.source].key, static_cast<float> (source));
    };
    for (int i = 0; i < ui::ModInput::kMaxMods; ++i)
        set (i, i + 1);
    const int hiddenSlot = ui::ModInput::kMaxMods + 4;
    set (hiddenSlot, 9);
    r.inst.lastSel = 2;
    r.openFull();
    r.ui.update (r.inst, 1, r.stack, r.now += 50.0, 1, 3);
    int x, y;
    r.centre (r.ui.full().top.pos, x, y);
    r.ui.touch (ev (TouchEvent::Down, x, y + kOY), r.stack, r.inst, r.now);
    r.now += 700.0;
    r.ui.tick (r.now, r.stack, r.inst);
    r.ui.touch (ev (TouchEvent::Up, x, y + kOY), r.stack, r.inst, r.now);
    r.ui.update (r.inst, 1, r.stack, r.now += 50.0, 1, 3);
    const auto& mods = r.ui.modsPage();
    check (r.stack.top() == OscUi::kPageMods && mods.numRows() == ui::ModulationsPage::kPerPage
           && mods.page.state.count == 2 && mods.sourceIds[0] == 9,
           "the OSC modulation editor offers the ninth source while its route is on the second page");
    const int writes = r.inst.writes;
    r.tapC (r.ui.modsPage().add);
    const auto free = synthyboy::modSlotParams (ui::ModInput::kMaxMods);
    check (r.inst.writes == writes && r.inst.get (synthyboy::paramTable()[free.source].key) == 0.0f
           && mods.page.state.index == 1 && mods.activeRow() == 2 && mods.rows[2].source == 9,
           "ADD focuses the matching route on the second page with zero duplicate writes");
}

void testModulations()
{
    Rig r;

    // LFO 2 modulates osc1.level at 40 %, bipolar: slot 3
    r.inst.set ("mod04.source", 8.0f);   // LFO 2 = 6 + 2
    r.inst.set ("mod04.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("osc1.level")))));
    r.inst.set ("mod04.amount", 40.0f);
    r.inst.set ("mod04.bipolar", 1.0f);
    r.inst.lastSel = 2;
    r.ui.update (r.inst, 1, r.stack, r.now += 500, 1, 2);
    const ui::Knob& lv = r.ui.compact().basic.level;
    check (lv.state.numMods == 1 && lv.state.mods[0].source == 8 && std::fabs (lv.state.mods[0].depth - 0.4) < 1e-6 && lv.state.mods[0].bidirectional, "the scan of the 64 slots finds LFO 2 on LEVEL, depth 0.4, bipolar");
    check (lv.state.selectedSource == 8, "the selected source is the LFO the Play page's LFO selector shows (LFO 2 = source 8)");
    r.inst.lastSel = 1;
    r.ui.update (r.inst, 1, r.stack, r.now += 500, 3, 2);
    check (lv.state.selectedSource == 3, "the last selector touched was the envelope's: Env 3 is the selected source");
    // Native Pan/Level never expose the former overlay modulation chip input.
    const int writes = r.inst.writes;
    const ui::Rect th = lv.rect();
    r.drag (th.x + th.w / 2, th.y + th.h / 2, th.x + th.w / 2, th.y + th.h / 2 - 40);
    check (r.inst.writes == writes && r.inst.get ("mod04.amount") == 40.0f,
           "native LEVEL retains modulation state without obsolete chip gestures");
    // the long press opens the modulations page
    // (LEVEL is read-only on the full screen: the long press is tried on the position knob, modulated by LFO 2 on slot 5)
    r.inst.set ("mod05.source", 8.0f);
    r.inst.set ("mod05.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("osc1.frame")))));
    r.inst.set ("mod05.amount", 40.0f);
    r.inst.set ("mod05.bipolar", 1.0f);
    r.openFull();
    int x, y;
    r.centre (r.ui.full().top.pos, x, y);
    r.ui.touch (ev (TouchEvent::Down, x, y + kOY), r.stack, r.inst, r.now);
    r.now += 700.0;
    r.ui.tick (r.now, r.stack, r.inst);
    r.ui.touch (ev (TouchEvent::Up, x, y + kOY), r.stack, r.inst, r.now);
    r.refresh();
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageMods, "a long press on a knob opens its modulations page");
    check (r.ui.modsPage().numRows() == 1, "the page lists the one modulation");
    check (r.inst.q[0] == Inst::idx ("mod05.amount") && r.inst.q[1] == Inst::idx ("mod05.bipolar") && r.inst.q[2] == Inst::idx ("mod05.bypass"), "the page's Q group is the amount, polarity and bypass of the slot");
    // bypass toggle
    ui::ToggleIcon& by = r.ui.modsPage().rows[0].bypass;
    r.tapC (by);
    check (r.inst.get ("mod05.bypass") == 1.0f, "the bypass toggle writes mod05.bypass");
    ui::ToggleIcon& po = r.ui.modsPage().rows[0].polarity;
    r.tapC (po);
    check (r.inst.get ("mod05.bipolar") == 0.0f, "the polarity toggle writes mod05.bipolar");
    // add a source
    r.tapC (r.ui.modsPage().add);
    check (r.ui.modsPage().numRows() == 2, "the add row adds a second modulation (a free slot got the source, destination WT POS)");
    // remove the first
    r.tapC (r.ui.modsPage().rows[1].remove);   // the added one took the free slot 0 (row 0), the old modulation is row 1
    check (r.inst.get ("mod05.source") == 0.0f && r.ui.modsPage().numRows() == 1, "the remove button frees the slot");
}

void testDrawsOneSceneInsideTheCanvas()
{
    for (int depth = 0; depth < 3; ++depth)
        for (int sel = 1; sel <= 5; ++sel)
        {
            Rig r;
            r.inst.sel = sel;
            r.refresh();
            if (depth >= 1) r.openFull();
            if (depth == 2) { r.stack.openDetail (OscUi::kPageRoute); r.refresh(); }
            auto data = std::make_unique<gpu::SceneData>();
            std::memset (data.get(), 0, sizeof (gpu::SceneData));
            gpu::SceneBuilder sb (*data);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.ui.draw (sb);
            check (! sb.overflow && (depth == 2 ? data->numOps == 0 : data->numOps > 20), "draw depth " + std::to_string (depth) + " instance " + std::to_string (sel) + ": one scene (" + std::to_string (data->numOps) + " ops)");
            std::vector<uint32_t> cv;
            renderScene (*data, cv);
            check (cv.size() == 1280u * 800u, "the CPU renderer draws it");
        }
}

void testQGroupFollowsTheActiveGroup()
{
    Rig r;
    r.inst.set ("osc1.warp.type", 1.0f); r.inst.set ("osc1.warp2.type", 1.0f);
    r.refresh();
    r.openFull();
    const int calls = r.inst.qCalls;
    // tap FIN: the Unison group becomes active
    r.tapC (*r.ui.full().panel.part (ui::PanelSlot::Fin));
    check (r.inst.q[0] == Inst::idx ("osc1.unison.voices") && r.inst.q[1] == Inst::idx ("osc1.unison.detune") && r.inst.q[2] == Inst::idx ("osc1.tune") && r.inst.q[3] == -1, "a touch on FIN makes the Unison group active: voices, detune, FIN");
    check (r.inst.qCalls > calls, "the group was handed to the instance");
    const int c2 = r.inst.qCalls;
    r.refresh();
    r.refresh();
    check (r.inst.qCalls == c2, "an unchanged group is not handed over again");
    r.tapC (r.ui.full().top.pos);
    check (r.inst.q[0] == Inst::idx ("osc1.type") && r.inst.q[1] == Inst::idx ("osc1.wavetable") && r.inst.q[2] == Inst::idx ("osc1.frame") && r.inst.q[3] == QRouter::encode (Inst::idx ("osc1.transpose"), QRouter::kOct), "the Basic group: TYPE, TABLE, WT POS, OCT (the octaves of the transpose)");
    r.tapC (r.ui.full().panel.warp()->first.amount);
    check (r.inst.q[0] == Inst::idx ("osc1.warp.type") && r.inst.q[1] == Inst::idx ("osc1.warp.amount") && r.inst.q[2] == Inst::idx ("osc1.warp2.type") && r.inst.q[3] == Inst::idx ("osc1.warp2.amount"), "the Warp group: warp 1 type and amount, warp 2 type and amount");
    // hide what does not apply: a warp that is Off has no amount and no Q slot
    r.inst.set ("osc1.warp2.type", 0.0f);
    r.refresh();
    r.refresh();
    check (r.inst.q[0] == Inst::idx ("osc1.warp.type") && r.inst.q[1] == Inst::idx ("osc1.warp.amount") && r.inst.q[2] == Inst::idx ("osc1.warp2.type") && r.inst.q[3] == -1 && ! r.ui.full().warpApplies (1), "warp 2 Off: its amount is hidden and not a Q member (the group closes up)");
    // on the Env sub-tab the compact view does not own the group
    Rig e;
    const int c0 = e.inst.qCalls;
    e.inst.sel = 3;
    e.refresh (2);
    check (e.inst.qCalls == c0, "on the Env sub-tab the oscillator UI does not set the Q group in main");
}

void testSourceAndWarpLists()
{
    Rig r;
    r.openFull();
    // the source dropdown opens the wavetable browser (the category of the current table, Saw is in Basic); the stepper's name does too, its outer quarters step
    ui::MenuButton& drop = r.ui.full().top.source.dropdown;
    check (drop.enabled() && r.ui.compact().basic.source.dropdown.enabled(), "the table dropdown is enabled on the full screen and on the compact view (wavetable type)");
    r.tapC (drop);
    check (r.stack.top() == OscUi::kPageList && r.ui.list().state.count == 6 && std::string (r.ui.list().state.header) == "Basic" && r.ui.list().state.headerBack, "the dropdown opens the browser in the category of the current table: the six one cycle tables, the header is the way up");
    const ui::Rect row = r.ui.list().rowRect (r.ui.list().firstRow());
    r.tap (row.x + 30, row.y + 20);
    check (r.inst.get ("osc1.wavetable") == static_cast<float> (r.ui.list().firstRow()) && r.stack.depth() == 1, "a tap on a row writes osc1.wavetable");
    ui::Stepper& st = r.ui.full().top.source.stepper;
    const ui::Rect sr = st.rect();
    r.tap (sr.x + sr.w / 2, sr.y + sr.h / 2);
    check (r.stack.top() == OscUi::kPageList && r.ui.list().state.current == 0, "a tap on the name of the table stepper opens the browser as well, the current table (Sine, the first row) marked");
    r.tap (20, 130);
    const float w0 = r.inst.get ("osc1.wavetable");
    r.tap (sr.x + sr.w - 8, sr.y + sr.h / 2);
    check (r.inst.get ("osc1.wavetable") == w0 + 1.0f && r.stack.depth() == 1, "the right quarter still steps to the next wavetable");
    // the warp type steppers of the wavetable panel: the name opens the list
    ui::WarpPair* wp = r.ui.full().panel.warp();
    const ui::Rect wr = wp->first.type.rect();
    r.tap (wr.x + wr.w / 2, wr.y + wr.h / 2);
    check (r.stack.top() == OscUi::kPageList && r.ui.list().state.count == 99, "the name of a warp type stepper opens the list of the warp types");
    r.tap (20, 130);
    // the other types have no source list
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    check (r.ui.full().top.source.dropdown.enabled(), "multisample: the dropdown of the source opens the content browser");
    // drawing with the list open
    r.inst.set ("osc1.type", 0.0f);
    r.refresh();
    r.tapC (r.ui.full().top.source.dropdown);
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps < 640, "the full screen with the list open is one scene (" + std::to_string (data->numOps) + " ops)");
}

void testGranularAndSubAndNoise()
{
    Rig r;
    r.inst.set ("osc1.type", 2.0f);
    r.refresh();
    r.openFull();
    auto& p = r.ui.full().panel;
    r.inst.set ("osc1.gran.rand_pitch", 12.0f);
    r.refresh();
    check (p.knob (ui::PanelSlot::Pitch)->state.value > 0.2, "PITCH reads osc1.gran.rand_pitch");
    check (r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::Scan))->param == Inst::idx ("osc1.gran.position"), "SCAN = osc1.gran.position");
    check (r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::Offset))->param == Inst::idx ("osc1.gran.rand_offset") && r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::Dir))->param == Inst::idx ("osc1.gran.rand_dir"), "OFFSET and DIR = gran.rand_offset, gran.rand_dir");
    // the warp menu opens the list of the warp types; a tap on a row writes it
    r.inst.set ("osc1.warp.type", 3.0f);
    r.refresh();
    r.tapC (*p.menu());
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageList, "the WARP menu button opens the list popup");
    check (r.ui.list().state.count == 99 && r.ui.list().state.current == 3 && r.ui.list().state.items != nullptr, "the list holds the 99 warp types of the engine, the current one is 3");
    {
        const ui::Rect row = r.ui.list().rowRect (r.ui.list().firstRow() + 1);
        r.tap (row.x + 30, row.y + 20);
    }
    check (r.inst.get ("osc1.warp.type") == static_cast<float> (r.ui.list().firstRow() + 1) && r.stack.depth() == 1, "a tap on a row writes osc1.warp.type and closes the list");
    r.tapC (*p.menu());
    r.tap (20, 130);
    check (r.stack.depth() == 1, "a tap on the backdrop closes the list without a write");
    // sub
    Rig s;
    s.inst.sel = 1;
    s.refresh();
    s.openFull();
    check (s.ui.full().panel.preset() == &ui::presets::kSubFull, "sub: the proper control area preset");
    s.inst.set ("sub.phase", 50.0f);
    s.refresh();
    check (s.ui.full().panel.knob (ui::PanelSlot::SubPhase)->state.value == 0.5 && std::string (s.ui.full().panel.knob (ui::PanelSlot::SubPhase)->state.valueText) == "180\xc2\xb0", "sub phase 50 % shows as 180 degrees");
    s.inst.set ("sub.tune", 7.0f);
    s.refresh();
    check (std::string (s.ui.full().panel.knob (ui::PanelSlot::SubCrs)->state.valueText).find ("7") != std::string::npos, "sub CRS = sub.tune");
}
}   // namespace

void testSteepEdgeNoLongLine()
{
    // a saw polyline (steep flanks) in a 300 x 200 canvas: nothing is drawn far above or below the polyline's own extent (the CPU renderer used to stretch the line's reach by the slope)
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 300, 200);
    float rows[512];
    for (int i = 0; i < 512; ++i)
        rows[i] = 80.0f + 40.0f * (1.0f - 2.0f * static_cast<float> ((i * 6) % 512) / 512.0f);   // six teeth between rows 40 and 120
    sb.curve (rows, 512, 10, 280);
    sb.stroke (2.0f, 0xffffffffu);
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    int outside = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 300; ++x)
            if (cv[static_cast<size_t> (y) * 300 + x] != 0 && (y < 34 || y > 126))
                ++outside;
    check (outside == 0, "a steep polyline draws nothing outside its own extent (" + std::to_string (outside) + " pixels)");
}


/** A directory tree of the machine's sample library: a folder with WAV files and an SFZ, a hidden file, a text file, a nested folder. */
std::vector<std::string> g_libraries;   // the directories this run made (removed at the end)
std::string makeLibrary()
{
    char tmpl[] = "/tmp/sbovl-XXXXXX";
    const char* dir = ::mkdtemp (tmpl);
    const std::string root = dir != nullptr ? dir : "/tmp";
    if (dir != nullptr)
        g_libraries.push_back (root);
    auto touch = [&] (const std::string& rel) { std::ofstream (root + "/" + rel) << "x"; };
    ::mkdir ((root + "/Drums").c_str(), 0755);
    ::mkdir ((root + "/Drums/Kicks").c_str(), 0755);
    ::mkdir ((root + "/Pads").c_str(), 0755);
    touch ("Drums/kick 1.wav"); touch ("Drums/Snare.WAV"); touch ("Drums/notes.txt"); touch ("Drums/.hidden.wav"); touch ("Drums/Kicks/deep.wav");
    touch ("Pads/pad.sfz"); touch ("Pads/pad C3.wav");
    return root;
}

void testContentBrowser()
{
    const std::string root = makeLibrary();
    ContentBrowser b;
    b.setRoots ({ { "LIBRARY", root }, { "NOWHERE", root + "/missing" } });
    b.open (1);
    check (b.atRoots() && b.count() == 3 && b.rows()[0].text == "SOURCES" && b.rows()[1].text == "LIBRARY \xe2\x96\xb6", "browser: it starts at the list of the roots");
    check (b.tap (0) == ContentBrowser::Action::None, "browser: the title row at the roots does nothing");
    check (b.tap (1) == ContentBrowser::Action::Changed && ! b.atRoots() && b.count() == 3, "browser: a root opens its folders (Drums, Pads): " + std::to_string (b.count()));
    check (b.rows()[1].text == "Drums \xe2\x96\xb6" && b.rows()[2].text == "Pads \xe2\x96\xb6", "browser: the folders are sorted, a folder says so");
    check (b.tap (1) == ContentBrowser::Action::Changed && b.count() == 4 && b.rows()[1].text == "Kicks \xe2\x96\xb6" && b.rows()[2].text == "kick 1.wav" && b.rows()[3].text == "Snare.WAV", "browser: a sample browser lists the folders first, then the WAV files, not the text file or the hidden one");
    check (b.rows()[0].text.find ("Drums") != std::string::npos, "browser: the first row names the folder and goes up");
    check (b.tap (2) == ContentBrowser::Action::Chosen && b.chosen() == root + "/Drums/kick 1.wav", "browser: a file is chosen with its path");
    check (b.tap (0) == ContentBrowser::Action::Changed && b.count() == 3 && b.tap (0) == ContentBrowser::Action::Changed && b.atRoots(), "browser: the first row goes up, the last step is the list of the roots");
    b.open (2);
    b.tap (1); b.tap (2);   // LIBRARY, Pads
    check (b.count() == 3 && b.rows()[1].text == "pad C3.wav" && b.rows()[2].text == "pad.sfz", "browser: for an instrument the SFZ files and the WAV files are listed (" + std::to_string (b.count()) + ")");
    b.openAt (1, root + "/Drums/Kicks/deep.wav");
    check (! b.atRoots() && b.folder() == root + "/Drums/Kicks" && b.rowOf (root + "/Drums/Kicks/deep.wav") == 1, "browser: it opens in the folder of the file that was loaded last");
    check (b.tap (-1) == ContentBrowser::Action::None && b.tap (99) == ContentBrowser::Action::None, "browser: a row that is not there does nothing");
}

void testSourceBrowserInTheOscillatorUi()
{
    const std::string root = makeLibrary();
    Rig r;
    r.inst.roots = { { "LIBRARY", root } };
    r.inst.set ("osc1.type", 2.0f);   // granular
    r.refresh();
    const OscUi::Item* src = r.ui.item (OscUi::kBasic + 1);
    check (src != nullptr && src->c->enabled() && r.ui.compact().basic.source.stepper.listable && r.ui.compact().basic.source.dropdown.enabled(), "granular: the source of the compact view is enabled (a tap opens the browser)");
    check (std::string (r.ui.compact().basic.source.stepper.state.text) == "NO SAMPLE", "granular: no sample loaded: the source says NO SAMPLE");
    // from the compact view the dropdown opens the full screen and the list over it
    r.tapC (r.ui.compact().basic.source.stepper);   // the name of the source is the list / the browser (the compact row has no list button)
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageList && r.ui.list().state.count == 1 && std::string (r.ui.list().state.header) == "SOURCES" && ! r.ui.list().state.headerBack && std::string (r.ui.list().state.items[0]) == "LIBRARY \xe2\x96\xb6", "granular: the dropdown opens the full screen and the list of the sources (the header is the title, no way up at the roots)");
    // LIBRARY, then Drums, then the sample
    auto row = [&] (int i) { const ui::Rect q = r.ui.list().rowRect (i); r.tap (q.x + 30, q.y + q.h / 2); };
    row (0);
    check (r.ui.list().state.count == 2 && r.stack.top() == OscUi::kPageList && r.ui.list().state.headerBack && std::string (r.ui.list().state.header) == "LIBRARY", "granular: a tap on a root shows its folders, the popup stays, the header is the way up with the path");
    row (0);
    check (r.ui.list().state.count == 3 && r.stack.top() == OscUi::kPageList && std::string (r.ui.list().state.header) == "LIBRARY / Drums", "granular: a tap on a folder shows its files, the header says the path");
    {
        const ui::Rect h = r.ui.list().headerRect();
        r.tap (h.x + 20, h.y + h.h / 2);
        check (r.ui.list().state.count == 2 && std::string (r.ui.list().state.header) == "LIBRARY", "granular: a tap on the header (the back arrow) goes one folder up");
        row (0);
    }
    row (1);
    check (r.inst.requests.size() == 1 && r.inst.requests[0].osc == 1 && r.inst.requests[0].kind == 1 && r.inst.requests[0].path == root + "/Drums/kick 1.wav" && r.stack.depth() == 1, "granular: a tap on a file asks the instance to load it as a sample of oscillator 1 and closes the list");
    r.inst.loadState = 1;
    r.refresh();
    check (std::string (r.ui.full().top.source.stepper.state.text) == "LOADING ...", "granular: while it loads the source says LOADING ...");
    r.inst.loadState = 3;
    r.refresh();
    check (std::string (r.ui.full().top.source.stepper.state.text) == "LOAD FAILED", "granular: a failed load is said");
    r.inst.loadState = 2;
    auto content = std::make_shared<synthyboy::StateContent>();
    synthyboy::ContentSample cs;
    cs.osc = 1; cs.ref.pack = "user.samples"; cs.ref.file = "abc/kick 1.wav"; cs.ref.name = "kick 1";
    content->samples.push_back (cs);
    r.inst.contentPtr = content;
    r.refresh();
    check (std::string (r.ui.full().top.source.stepper.state.text) == "kick 1", "granular: the loaded sample's name is on the source");
    // the browser opens where the last file was
    r.tapC (r.ui.full().top.source.dropdown);
    check (r.stack.top() == OscUi::kPageList && std::string (r.ui.list().state.header).find ("Drums") != std::string::npos && r.ui.list().state.current == 1, "granular: the browser opens in the folder of the last file, on it");
    r.tap (20, 130);   // the backdrop closes
    check (r.stack.depth() == 1, "the backdrop closes the browser");
    // multisample: SFZ and WAV
    r.inst.set ("osc1.type", 4.0f);
    r.refresh();
    r.tapC (r.ui.full().top.source.dropdown);
    check (r.stack.top() == OscUi::kPageList, "multisample: the source opens the browser too");
    r.tap (20, 130);
    // no roots: nothing opens
    Rig none;
    none.inst.set ("osc1.type", 4.0f);
    none.refresh();
    none.tapC (none.ui.compact().basic.source.stepper);
    check (none.stack.depth() == 0, "an instance without roots: the browser does not open");
}


/** The content the sound needs and does not have: "content missing: <name>" over the display, the name from the state's content reference (title, else file name without extension, underscores as spaces). */
void testMissingContentLine()
{
    Rig r;
    r.inst.set ("osc1.type", 1.0f);   // a sample oscillator
    auto content = std::make_shared<synthyboy::StateContent>();
    synthyboy::ContentSample cs;
    cs.osc = 1; cs.ref.pack = "p"; cs.ref.file = "drums/drum_loop_120bpm.wav";
    content->samples.push_back (cs);
    r.inst.contentPtr = content;
    r.refresh();
    check (r.ui.missingText().empty(), "nothing missing: no line over the display");
    r.inst.missingBits = 1u << 0;
    r.refresh();
    check (r.ui.missingText() == "content missing: drum loop 120bpm", "the file of oscillator A is missing: 'content missing: drum loop 120bpm' (" + r.ui.missingText() + ")");
    r.inst.missingBits = 1u << 1;   // oscillator B's file: not A's
    r.refresh();
    check (r.ui.missingText().empty(), "the line is for the oscillator on screen only");
    r.inst.missingBits = 1u << 0;
    content->samples[0].ref.name = "Break Beat";   // the pack's title wins
    r.refresh();
    check (r.ui.missingText() == "content missing: Break Beat", "the pack's title is the name when it has one");
    r.inst.set ("osc1.type", 0.0f);
    r.inst.missingBits = 1u << 3;   // the user wavetable of A
    r.refresh();
    check (r.ui.missingText() == "content missing", "a missing wavetable without a reference in the state: the line says content missing");
    // a name too long for its room loses its MIDDLE once: the text is drawn at the size the cut was made for, so the pen does not cut the end of it again (one ellipsis, not at the end), in the compact view's band and on the full screen
    r.inst.set ("osc1.type", 1.0f);
    content->samples[0].ref.name = "";
    content->samples[0].ref.file = "drums/an_extremely_long_sample_file_name_that_can_not_fit_into_the_band_of_the_display_at_any_size_final_take_07.wav";
    r.inst.missingBits = 1u << 0;
    r.refresh();
    for (int state = 0; state < 2; ++state)
    {
        if (state == 1)
            r.openFull();
        auto data = std::make_unique<gpu::SceneData>();
        std::memset (data.get(), 0, sizeof (gpu::SceneData));
        gpu::SceneBuilder sb (*data);
        r.ui.drawOnDisplay (sb);
        int lines = 0, bad = 0;
        for (uint32_t k = 0; k < data->numOps; ++k)
            if (data->ops[k].op == gpu::kOpText)
            {
                const std::string t = gpu::textString (*data, data->ops[k]);
                if (t.rfind ("content missing", 0) != 0)
                    continue;
                ++lines;
                size_t n = 0, last = 0;
                for (size_t at = t.find ("\xE2\x80\xA6"); at != std::string::npos; at = t.find ("\xE2\x80\xA6", at + 3))
                {
                    ++n;
                    last = at;
                }
                if (n != 1 || last + 3 >= t.size() || t.compare (0, 15, "content missing") != 0 || t.compare (t.size() - 2, 2, "07") != 0)
                    ++bad;
            }
        check (lines == 1 && bad == 0, std::string ("the long name of the missing content loses its middle once (") + (state == 0 ? "compact view" : "full screen") + "): one ellipsis, not at the end, the end of the name stays");
    }
}

/** The type drop-down of the compact view: closed it shows the current type, a tap opens the list of the types with their pictograms (rows >= 56 px), a row chooses (the same write the cells made). */
void testTypeDropdown()
{
    Rig r;
    r.inst.set ("osc1.type", 0.0f);
    r.refresh();
    ui::TypeSelector& t = r.ui.compact().basic.selector;
    check (t.dropdown && std::string (t.state.items[t.state.index].label) == "WAVE" && t.rect().h >= 56, "the type is a drop-down in the compact view: closed it shows WAVE");
    r.tapC (t);
    {
        const ui::ListPopup& l = r.ui.list();
        const char* const want[5] = { "WAVE", "MULTI", "SAMPLE", "GRAIN", "SPEC" };
        bool ok = r.stack.isMain() && l.state.count == 5 && l.state.icons != nullptr && l.state.current == 0;
        for (int i = 0; i < 5 && ok; ++i)
            ok = std::string (l.state.items[i]) == want[i] && l.state.icons[i] >= 0 && l.rowRect (i).h >= 56;
        check (ok, "a tap opens the list of the five types with their pictograms (rows >= 56 px), no page of the stack");
    }
    {
        const ui::Rect row = r.ui.list().rowRect (3);   // GRAIN
        r.tap (row.x + row.w / 2, row.y + row.h / 2);
    }
    check (r.inst.get ("osc1.type") == 2.0f && r.stack.isMain(), "the row GRAIN writes the granular type (osc1.type = 2) and the list closes");
    r.refresh();
    check (std::string (t.state.items[t.state.index].label) == "GRAIN", "the closed drop-down shows GRAIN");
    // the sub oscillator: the six waveforms
    r.inst.sel = 1;
    r.refresh();
    ui::TypeSelector& st = r.ui.compact().basic.selector;
    r.tapC (st);
    check (r.ui.list().state.count == 6 && std::string (r.ui.list().state.items[5]) == "PULSE", "the sub oscillator's drop-down lists the six waveforms");
    r.tap (20, 130);   // the backdrop closes
    check (r.stack.isMain(), "a tap on the backdrop closes the list");
}

/** A switched-off oscillator: the inactive look and a read-only block, with POWER and navigation still reachable. */
void testInactiveLook()
{
    const uint32_t g = ui::Pen::inactiveGrey (0x2ee6f0ffu);
    check (((g >> 24) & 255u) == ((g >> 16) & 255u) && ((g >> 16) & 255u) == ((g >> 8) & 255u) && (g & 255u) == 255u && ((g >> 8) & 255u) > 0u, "the inactive look: a colour becomes a grey of its own brightness, the alpha stays");
    Rig r;
    r.inst.set ("osc1.on", 1.0f);
    r.refresh();
    check (! r.ui.compact().basic.inactive() && ! r.ui.compact().basic.pan.inactive() && ! r.ui.compact().basic.group.inactive() && r.ui.compact().power.state.on, "an oscillator that is on: the controls have the normal look");
    r.inst.set ("osc1.on", 0.0f);
    r.refresh();
    ui::OscillatorCompact& c = r.ui.compact();
    check (! c.power.state.on && c.power.state.negativeOff && ! c.power.inactive() && c.instances.enabled() && ! c.instances.inactive() && ! c.full.enabled() && c.full.inactive(), "switched off: the power button is red and instance navigation remains reachable and EXPAND is grey");
    check (c.basic.inactive() && c.basic.pan.inactive() && c.basic.level.inactive() && c.basic.selector.inactive() && c.basic.group.inactive() && ! c.basic.pan.enabled() && ! c.basic.level.enabled() && ! c.basic.selector.enabled(), "switched off: the whole Q block is grey and read-only");
    check (r.ui.full().top.inactive() && r.ui.full().panel.inactive() && r.ui.full().groups[0].inactive() && ! r.ui.full().power.inactive() && r.ui.full().close.enabled() && ! r.ui.full().close.inactive(), "switched off: the full screen's blocks and groups are grey, POWER stays coloured and CLOSE stays reachable");
    check (std::strcmp (c.basic.pan.statusString(), "OFF") == 0
        && std::strcmp (c.basic.level.statusString(), "OFF") == 0,
        "oscillator power drives the compact PAN and LEVEL readouts");
    const int w0 = r.inst.writes;
    r.tapC (c.full);
    check (r.stack.isMain() && r.inst.writes == w0, "switched off: compact EXPAND is disabled and writes nothing");
    r.drag (c.basic.pan.rect().x + c.basic.pan.rect().w / 2, c.basic.pan.rect().y + c.basic.pan.rect().h / 2,
            c.basic.pan.rect().x + c.basic.pan.rect().w / 2, c.basic.pan.rect().y - 80);
    check (r.inst.writes == w0, "switched off: dragging a disabled control writes no parameter");
    r.stack.openFull (OscUi::kElementOsc);
    r.refresh();
    const int* targets = r.ui.qTargets();
    check (std::all_of (targets, targets + 16, [] (int target) { return target == -1 || target == Inst::idx ("osc1.on"); }),
           "OFF oscillator publishes no editable sound parameters to hardware Q-Links");
    const int w1 = r.inst.writes;
    if (r.ui.full().more.rect().w > 0)
        r.tapC (r.ui.full().more);
    check (r.inst.writes == w1 && r.stack.top() == OscUi::kElementOsc, "switched off: full MORE is disabled and keeps the page");
    r.inst.set ("osc1.on", 1.0f);
    r.refresh();
    check (! c.basic.inactive() && ! c.basic.group.inactive() && c.basic.pan.enabled() && ! r.ui.full().panel.inactive(), "switched on again: the normal look and editing return");
    check (! c.basic.pan.hasStatus() && ! c.basic.level.hasStatus(),
        "oscillator power restores the normal PAN and LEVEL readouts");
}

/** The detail pages of the types (osc_spec_page.h): how each is reached, BACK, the size of the display, every visible control writes its parameter, the hidden states, the Q groups, the empty state. */
void testSpecPages()
{
    namespace os = synthyboy_mpc::overlay::oscspec;
    struct Case { int type; const char* token; int page; const char* name; };
    // the buttons of MORE: wavetable MAP, sample REGION / SLICE, granular GRAIN, spectral FILTER / SCAN
    const Case cases[] = { { 0, "map", os::kWtMap, "wavetable MAP" }, { 1, "region", os::kSampleRegion, "sample REGION" }, { 1, "slice", os::kSampleSlice, "sample SLICE" }, { 2, "grain", os::kGrain, "granular GRAIN" },
                           { 3, "filter", os::kSpecFilter, "spectral FILTER" }, { 3, "scan", os::kSpecScan, "spectral SCAN" } };
    for (const Case& c : cases)
    {
        Rig r;
        r.inst.set ("osc1.type", static_cast<float> (c.type));
        r.refresh();
        openMore (r);
        const OscUi::Item* b = pageButton (r, c.token);
        check (b != nullptr && b->c != nullptr, std::string (c.name) + ": MORE has the button");
        if (b == nullptr)
            continue;
        r.tapC (*b->c);
        check (r.stack.top() == c.page && r.stack.depth() == 3, std::string (c.name) + ": the button opens the page (over MORE)");
        check (r.ui.displayRect().h >= 240, std::string (c.name) + ": the display is at least 240 px tall");
        // every control that is on the page has its size, none lies over another
        const oscspec::OscSpecPage& pg = r.ui.specPage();
        int shown = 0;
        bool sizes = true, overlap = false;
        for (int i = 0; i < pg.spec()->numCtls; ++i)
            if (pg.visible (i))
            {
                ++shown;
                const ui::ControlModel* ci = const_cast<oscspec::OscSpecPage&> (pg).control (i);
                sizes = sizes && ci->rect().h >= 56 && ci->rect().w >= 56;
                for (int j = i + 1; j < pg.spec()->numCtls; ++j)
                    if (pg.visible (j))
                        overlap = overlap || ci->rect().overlaps (const_cast<oscspec::OscSpecPage&> (pg).control (j)->rect());
            }
        check (shown >= 2 && sizes && ! overlap, std::string (c.name) + ": the visible controls are 56 px or more and do not overlap");
        // each visible control writes its parameter
        int wrote = 0, tried = 0;
        for (int i = 0; i < pg.spec()->numCtls; ++i)
        {
            if (! pg.visible (i))
                continue;
            const OscUi::Item* it = r.ui.item (OscUi::kSpec + i);
            if (it == nullptr || it->param < 0)
                continue;
            ++tried;
            const float before = r.inst.param (it->param);
            const ui::Rect q = it->c->rect();
            if (pg.spec()->ctls[i].kind == os::K::Stepper)
                r.tap (q.x + q.w - 10, q.y + q.h / 2);
            else if (pg.spec()->ctls[i].kind == os::K::Knob)
            {
                r.drag (q.x + q.w / 2, q.y + q.h / 2, q.x + q.w / 2, q.y + q.h / 2 - 60);
                if (r.inst.param (it->param) == before)
                    r.drag (q.x + q.w / 2, q.y + q.h / 2, q.x + q.w / 2, q.y + q.h / 2 + 60);
            }
            else if (pg.spec()->ctls[i].kind == os::K::List)
            {
                const ui::LedList* ll = static_cast<const ui::LedList*> (it->c);
                const ui::Rect q2 = ll->rowRect (ll->state.index == 0 ? 1 : 0);
                r.tap (q2.x + q2.w / 2, q2.y + q2.h / 2);
            }
            else
                r.tapC (*it->c);
            if (r.inst.param (it->param) != before)
                ++wrote;
            if (r.stack.top() != c.page)   // a stepper with a long list may have opened its popup
                r.stack.back();
            r.refresh();
        }
        check (tried > 0 && wrote >= tried - 1, std::string (c.name) + ": the controls write their parameters (" + std::to_string (wrote) + " of " + std::to_string (tried) + ")");
        // BACK returns to the full screen
        r.tapC (r.ui.specPage().back);
        check (r.stack.depth() == 2 && r.stack.top() == os::kMore, std::string (c.name) + ": BACK returns to MORE");
    }
    // MORE for every type (one page, the controls of the type shown, the others hidden) and OPTIONS behind it; their Q groups
    for (int type : { 0, 4, 1, 2, 3 })
    {
        Rig r;
        r.inst.set ("osc1.type", static_cast<float> (type));
        r.refresh();
        openMore (r);
        const int want = type == 0 ? 4 : type == 4 ? 5 : type == 1 ? 4 : type == 2 ? 3 : 4;   // the blocks with something to show: PITCH, UNISON, (WARP), (PHASE), (VELOCITY), (GRAIN)
        check (r.stack.top() == os::kMore && r.ui.specPage().numGroups() >= 3 && r.ui.specPage().numGroups() <= want + 2, "type " + std::to_string (type) + ": MORE opens its page (" + std::to_string (r.ui.specPage().numGroups()) + " Q groups)");
        check (r.ui.displayRect().empty(), "MORE has no display (the room goes to the blocks)");
        const OscUi::Item* options = pageButton (r, "options");
        check (options != nullptr, "type " + std::to_string (type) + ": MORE has the OPTIONS button");
        r.tapC (*options->c);
        check (r.stack.top() == os::kOptions && r.ui.specPage().numGroups() == 7, "type " + std::to_string (type) + ": OPTIONS opens the options page with seven blocks");
        r.tapC (*r.ui.specPage().control (0));   // ON
        check (r.inst.get ("osc1.on") == 0.0f, "OPTIONS: the power switch is a control of the page (osc1.on)");
        check (r.inst.q[0] == Inst::idx ("osc1.on") && r.inst.q[1] == -1 && r.inst.q[2] == -1, "OFF OPTIONS: only POWER remains mapped");
        r.tapC (*r.ui.specPage().control (0));
        check (r.inst.q[0] == Inst::idx ("osc1.on") && r.inst.q[1] == Inst::idx ("osc1.env1") && r.inst.q[2] == Inst::idx ("osc1.smooth_interp") && r.inst.q[3] == -1, "OPTIONS: the first block is a Q group: ON, ENV 1, SMOOTH");
    }
    // the transpose snap: twelve note cells (a piano: the black notes above), a toggle for bit 12; the mask is the Q member
    {
        Rig r;
        r.refresh();
        openMore (r);
        r.tapC (*pageButton (r, "options")->c);
        const oscspec::OscSpecPage& pg = r.ui.specPage();
        int maskIdx = -1, bitIdx = -1;
        for (int i = 0; i < pg.spec()->numCtls; ++i)
            if (std::string (pg.spec()->ctls[i].key) == "transpose_snap")
                (pg.spec()->ctls[i].kind == os::K::Mask ? maskIdx : bitIdx) = i;
        check (maskIdx >= 0 && bitIdx >= 0, "the options page has the note mask and the toggle for the transpose snap (no knob)");
        auto* mask = static_cast<ui::NoteMask*> (const_cast<oscspec::OscSpecPage&> (pg).control (maskIdx));
        bool big = true, above = true;
        for (int n = 0; n < 12; ++n)
        {
            const ui::Rect c = mask->cellRect (n);
            big = big && c.w >= 56 && c.h >= 56;
            above = above && (! ui::NoteMask::isBlack (n) || c.y < mask->cellRect (0).y);
        }
        check (big && above, "the twelve note cells are at least 56 px and the black notes lie above the white ones");
        const ui::Rect g = mask->cellRect (7);   // G
        r.tap (g.x + g.w / 2, g.y + g.h / 2);
        check (static_cast<int> (r.inst.get ("osc1.transpose_snap")) == (1 << 7), "a tap on G sets bit 7 of osc1.transpose_snap");
        const ui::Rect cs = mask->cellRect (1);  // C#
        r.tap (cs.x + cs.w / 2, cs.y + cs.h / 2);
        check (static_cast<int> (r.inst.get ("osc1.transpose_snap")) == ((1 << 7) | 2), "a tap on C# adds bit 1");
        r.tapC (*const_cast<oscspec::OscSpecPage&> (pg).control (bitIdx));
        check (static_cast<int> (r.inst.get ("osc1.transpose_snap")) == ((1 << 7) | 2 | 4096), "the SNAP NOTE toggle sets bit 12 and keeps the notes");
        r.tap (g.x + g.w / 2, g.y + g.h / 2);
        check (static_cast<int> (r.inst.get ("osc1.transpose_snap")) == (2 | 4096), "a tap on a lit note clears it and keeps bit 12");
        r.tapC (*pg.groups[5].member (0));   // the block of the mask (the sixth) becomes the active group
        check (r.inst.q[0] == Inst::idx ("osc1.transpose_snap") && r.inst.q[1] == -1, "the mask is the Q member of its block (Q 1 = the parameter); the toggle of the same parameter has no Q knob");
    }
    // hidden states: the loop points exist only while a loop mode is on, the grain divisions only in their mode
    {
        Rig r;
        r.inst.set ("osc1.type", 1.0f);
        r.refresh();
        r.openFull();
        r.stack.openDetail (os::kSampleRegion);
        r.refresh();
        auto vis = [&] (const char* key) { for (int i = 0; i < r.ui.specPage().spec()->numCtls; ++i) if (std::string (r.ui.specPage().spec()->ctls[i].key) == key) return r.ui.specPage().visible (i); return false; };
        check (! vis ("sample.loop_start") && ! vis ("sample.loop_relative") && vis ("sample.loop_mode"), "sample: one shot hides the loop points and options");
        const int groups0 = r.ui.specPage().numGroups();
        r.inst.set ("osc1.sample.loop_mode", 1.0f);
        r.refresh();
        check (vis ("sample.loop_start") && vis ("sample.loop_xfade") && r.ui.specPage().numGroups() == groups0 + 1, "sample: a loop mode shows the loop points and the LOOP OPTIONS block appears");
        r.inst.set ("osc1.sample.keytrack", 0.0f);
        r.refresh();
        check (! vis ("sample.root"), "sample: with key tracking off the root note is hidden");
        r.inst.set ("osc1.type", 2.0f);
        r.refresh();
        r.stack.back();
        r.stack.openDetail (os::kGrain);
        r.refresh();
        check (! vis ("gran.density_sync") && ! vis ("gran.density_grains") && ! vis ("gran.length_percent") && ! vis ("gran.xy_y"), "grain: free density and length have no divisions");
        r.inst.set ("osc1.gran.density_mode", 1.0f); r.inst.set ("osc1.gran.length_mode", 2.0f); r.inst.set ("osc1.gran.y_axis", 3.0f);
        r.refresh();
        check (vis ("gran.density_sync") && ! vis ("gran.density_grains") && vis ("gran.length_percent") && ! vis ("gran.length_sync") && vis ("gran.xy_y"), "grain: each mode shows its own value, the Y position follows the axis");
    }
    // the empty state: a type that plays a file shows one button that opens the browser; a loaded file takes it away
    {
        Rig r;
        r.inst.roots = { { "LIB", "/tmp" } };
        r.inst.set ("osc1.type", 1.0f);
        r.refresh();
        r.openFull();
        check (r.ui.loadButtonShown(), "sample without a file: the LOAD button is on the display");
        r.tapC (r.ui.loadButton());
        check (r.stack.top() == OscUi::kPageList, "the LOAD button opens the browser in one tap");
        r.stack.back();
        r.refresh();
        r.stack.openDetail (os::kSampleRegion);
        r.refresh();
        check (r.ui.loadButtonShown(), "the page of the type has the LOAD button too");
        auto content = std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentSample cs;
        cs.osc = 1; cs.ref.pack = "p"; cs.ref.file = "a.wav"; cs.ref.name = "a";
        content->samples.push_back (cs);
        r.inst.contentPtr = content;
        r.refresh();
        check (! r.ui.loadButtonShown(), "with a file loaded the button is gone");
        Rig w;
        w.refresh();
        w.openFull();
        check (! w.ui.loadButtonShown(), "a wavetable never shows it");
    }
}

/** Review 2: the values under the knobs, the noise's own controls, the pictures on the detail pages, the labels. */
void testReview2()
{
    {
        Rig r;
        r.inst.sel = 2;
        r.inst.set ("osc1.unison.detune", 40.0f);
        r.refresh();
        r.openFull();
        const OscUi::Item* it = r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::Detune));
        check (it != nullptr && std::string (static_cast<ui::Knob*> (it->c)->state.valueText) != "", "every big knob says its value under its name (DETUNE: " + std::string (static_cast<ui::Knob*> (it->c)->state.valueText) + ")");
        const OscUi::Item* w = r.ui.item (OscUi::kWarp1Amount);
        check (w != nullptr && std::string (static_cast<ui::Knob*> (w->c)->state.valueText) != "" && static_cast<ui::Knob*> (w->c)->state.side, "the WARP knobs are the medium (side) size and say their value");
        const ui::Rect pos = r.ui.full().top.pos.rect();
        check (pos.h >= 72 && r.ui.full().top.pos.state.side && std::string (r.ui.full().top.pos.state.valueText) != "", "the position knob of the top row fills the band and says its value");
        r.tapC (static_cast<ui::UnisonBox*> (r.ui.full().panel.part (ui::PanelSlot::Unison))->gear);
        check (r.stack.top() == OscUi::kPageUnison && ! r.ui.displayRect().empty() && r.ui.displayRect().h >= 150, "the unison page has the picture of the oscillator above its settings");
    }
    {
        Rig r;
        r.inst.sel = 5;
        r.refresh();
        r.openFull();
        const OscUi::Item* ty = r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::NoiseType));
        check (ty != nullptr && ty->param == Inst::idx ("noise.type") && ty->c->enabled() && ty->c == &r.ui.full().top.kind, "noise: KIND (noise.type) is in the top row");
        check (r.ui.full().panel.part (ui::PanelSlot::NoiseStart) == nullptr, "noise: START (sample only) is hidden while the noise is generated");
        const OscUi::Item* stereo = r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::NoiseStereo));
        check (stereo != nullptr && stereo->param == Inst::idx ("noise.stereo") && stereo->c == r.ui.full().panel.part (ui::PanelSlot::NoiseStereo), "noise: STEREO is in the lower character group");
        const OscUi::Item* pitch = r.ui.item (OscUi::kTuneCrs);
        const OscUi::Item* fine = r.ui.item (OscUi::kTuneFin);
        const OscUi::Item* key = r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::NoiseKey));
        check (pitch != nullptr && pitch->param == Inst::idx ("noise.pitch") && fine != nullptr && fine->param == Inst::idx ("noise.fine") && key != nullptr && key->param == Inst::idx ("noise.keytrack"), "noise: the TUNE block is PITCH, FINE, KEY TRACK (and ENV 1)");
        {   // the generated noise: the display over ONE block (TUNE) of four cells in one row
            ui::OscillatorFullScreen& f = r.ui.full();
            const ui::ControlModel* cells[3] = { f.panel.part (ui::PanelSlot::NoisePitch), f.panel.part (ui::PanelSlot::NoiseFine), f.panel.part (ui::PanelSlot::NoiseKey) };
            bool oneRow = true, order = true;
            long area = 0;
            const ui::Rect blk = f.panel.blockRect (0);
            for (int i = 0; i < 3; ++i)
            {
                oneRow = oneRow && cells[i] != nullptr && cells[i]->rect().y == cells[0]->rect().y && cells[i]->rect().h == ui::KnobPanel::kOneRowH && blk.contains (cells[i]->rect());
                area += cells[i] != nullptr ? static_cast<long> (cells[i]->rect().w) * cells[i]->rect().h : 0;
                order = order && (i == 0 || (cells[i] != nullptr && cells[i - 1] != nullptr && cells[i - 1]->rect().right() <= cells[i]->rect().x));
            }
            check (oneRow && order && area * 2 >= static_cast<long> (blk.w) * blk.h, "noise (generated): PITCH, FINE, KEY TRACK are one compact tune row, at least half covered");
            check (f.displayRect().w >= 1200 && f.displayRect().bottom() < f.panel.rect().y && f.displayRect().h >= 300 && blk.h == ui::KnobPanel::kBlockTitleH + ui::KnobPanel::kOneRowH + ui::KnobPanel::kBlockPad, "noise (generated): the display has the whole width and the room the one row frees");
        }
        r.inst.set ("noise.type", 1.0f);
        r.refresh();
        const OscUi::Item* st = r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::NoiseStart));
        check (st != nullptr && st->c->enabled() && r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::NoiseLoop))->c->enabled(), "noise: with the type Sample the sample controls are live");
        const OscUi::Item* env = r.ui.item (OscUi::kPanel + static_cast<int> (ui::PanelSlot::NoiseEnv1));
        check (env != nullptr && env->param == Inst::idx ("noise.env1"), "noise: ENV 1 is on the panel");
        {   // the noise that plays a sample: the blocks TUNE and PLAYBACK as ONE row under the display, every block covered for at least half
            ui::OscillatorFullScreen& f = r.ui.full();
            const ui::Rect d = f.displayRect(), b0 = f.panel.blockRect (0), b1 = f.panel.blockRect (1);
            check (f.panel.numBlocks() == 3 && std::string (f.panel.blockTitle (0)) == "TUNE" && std::string (f.panel.blockTitle (1)) == "CHARACTER" && std::string (f.panel.blockTitle (2)) == "PLAYBACK" && b0.y == b1.y && b0.h == b1.h && b0.h == ui::KnobPanel::kBlockTitleH + ui::KnobPanel::kOneRowH + ui::KnobPanel::kBlockPad && b0.y >= d.bottom() && d.h >= 300,
                   "noise (sample): the blocks TUNE and PLAYBACK are one row of the same height under the display, the title strip, the row at its maximum height and the padding");
            for (int b = 0; b < f.panel.numBlocks(); ++b)
            {
                long area = 0;
                ui::Component* leaves[16];
                const int n = f.panel.collectLeaves (leaves, 0, 16);
                for (int i = 0; i < n; ++i)
                    if (f.panel.blockRect (b).contains (leaves[i]->rect()))
                        area += static_cast<long> (leaves[i]->rect().w) * leaves[i]->rect().h;
                check (area * 2 >= static_cast<long> (f.panel.blockRect (b).w) * f.panel.blockRect (b).h, std::string ("noise (sample): the block ") + f.panel.blockTitle (b) + " is covered by its controls for at least half its area");
            }
        }
        r.tapC (*r.ui.full().panel.part (ui::PanelSlot::NoiseKey));
        check (r.inst.get ("noise.keytrack") == 1.0f, "noise: KEY TRACK writes noise.keytrack");
    }
    {
        Rig r;
        r.inst.sel = 2;
        r.refresh();
        r.openFull();
        r.stack.openDetail (OscUi::kPageUnison);
        r.refresh();
        const ui::Stepper* mode = r.ui.detail().panel.stepper (ui::PanelSlot::Mode);
        check (mode != nullptr && std::string (mode->state.caption) == "MODE" && mode->rect().h <= 72, "unison page: the stepper has its name (MODE) and a normal height");
    }
}

// the follow-ups of the oscillator pages: the board of sources on a control without modulations, the shown names of the unison law (no product name on any page), one format of the voice step value
void testFollowUps()
{
    {
        Rig r;
        r.openFull();
        int x, y;
        r.centre (r.ui.full().top.pos, x, y);
        r.ui.touch (ev (TouchEvent::Down, x, y + kOY), r.stack, r.inst, r.now);
        r.now += 700.0;
        r.ui.tick (r.now, r.stack, r.inst);
        r.ui.touch (ev (TouchEvent::Up, x, y + kOY), r.stack, r.inst, r.now);
        r.refresh();
        check (r.stack.top() == OscUi::kPageMods && r.ui.modsPage().numRows() == 0, "the modulations page of a control nothing modulates opens");
        check (r.ui.modsPage().boardShown() && r.ui.modsPage().board.numSources() > 10, "the page of a control without modulations is the board of every source, as on the other pages");
    }
    bool clean = true;
    std::string where;
    for (int osc = 1; osc <= 3 && clean; ++osc)
        for (int law = 0; law < 2; ++law)
        {
            Rig r;
            const std::string key = "osc" + std::to_string (osc) + ".unison.law";
            r.inst.set (key.c_str(), static_cast<float> (law));
            r.refresh();
            r.openFull();
            r.stack.openDetail (oscspec::kOptions);
            r.refresh();
            const oscspec::OscSpecPage& pg = r.ui.specPage();
            std::string seen;
            for (int i = 0; pg.spec() != nullptr && i < pg.spec()->numCtls; ++i)
                if (pg.spec()->ctls[i].kind == oscspec::K::List && std::strcmp (pg.spec()->ctls[i].key, "unison.law") == 0)
                {
                    const ui::LedList* l = dynamic_cast<const ui::LedList*> (const_cast<oscspec::OscSpecPage&> (pg).control (i));
                    if (l != nullptr)
                    {
                        seen += std::string (l->state.title);
                        for (int o = 0; o < l->state.count; ++o)
                            seen += std::string ("|") + l->state.names[o];
                        check (std::string (l->state.names[0]) == "Equal power" && std::string (l->state.names[1]) == "Fixed gain" && std::string (l->state.title) == "VOICE LAW", "the unison law is Equal power / Fixed gain under VOICE LAW (" + seen + ")");
                    }
                }
            if (seen.find ("Vital") != std::string::npos || seen.find ("Serum") != std::string::npos)
            {
                clean = false;
                where = key;
            }
        }
    check (clean, "no oscillator page shows 'Vital' or 'Serum'" + (clean ? std::string() : " (" + where + ")"));
}


/** A machine with wavetable files: a pack with a category folder (Analog) and a root file, the player's folder SynthyBoy/Wavetables with a category (Bass: x, y, z, and a 17 MB file and an empty one) and a loose file. */
std::string makeWavetableLibrary (synthyboy::CatalogueSources& sources)
{
    char tmpl[] = "/tmp/sbovl-XXXXXX";
    const char* dir = ::mkdtemp (tmpl);
    const std::string root = dir != nullptr ? dir : "/tmp";
    if (dir != nullptr)
        g_libraries.push_back (root);
    auto make = [&] (const std::string& rel) { ::mkdir ((root + "/" + rel).c_str(), 0755); };
    auto touch = [&] (const std::string& rel, const char* text = "x") { std::ofstream (root + "/" + rel) << text; };
    make ("packs"); make ("packs/com.t.pack"); make ("packs/com.t.pack/Analog");
    touch ("packs/com.t.pack/Analog/warm.wav");
    const std::string sha (64, 'a');
    touch ("packs/com.t.pack/contentpack.json", ("{\"schemaVersion\":1,\"id\":\"com.t.pack\",\"name\":\"T\",\"version\":\"0.1.0\",\"license\":\"CC0-1.0\",\"attribution\":\"t\",\"kind\":\"wavetables\",\"requires_plugin\":\">=0.1.0\","
                                                  "\"files\":[{\"path\":\"Analog/warm.wav\",\"sha256\":\"" + sha + "\",\"size\":4096,\"title\":\"Warm Saw\"}]}").c_str());
    make ("SynthyBoy"); make ("SynthyBoy/Wavetables"); make ("SynthyBoy/Wavetables/Bass");
    touch ("SynthyBoy/Wavetables/Bass/x.wav"); touch ("SynthyBoy/Wavetables/Bass/y.wav"); touch ("SynthyBoy/Wavetables/Bass/z.wav");
    touch ("SynthyBoy/Wavetables/Bass/empty.wav", "");
    { std::ofstream f (root + "/SynthyBoy/Wavetables/Bass/big.wav"); f << "x"; }
    (void) ::truncate ((root + "/SynthyBoy/Wavetables/Bass/big.wav").c_str(), 17 * 1024 * 1024);   // a sparse file: 17 MB on paper, nothing on disk
    touch ("SynthyBoy/Wavetables/loose.wav");
    sources.packDirs = { root + "/packs" };
    sources.userRoots = { root };
    return root;
}

void testWavetableBrowser()
{
    synthyboy::CatalogueSources sources;
    makeWavetableLibrary (sources);
    Rig r;
    r.inst.sources = sources;
    r.openFull();
    // opening: the category of the current table (Saw: Basic), the header is the way up, the current table is marked
    r.tapC (r.ui.full().top.source.dropdown);
    check (r.stack.top() == OscUi::kPageList && std::string (r.ui.list().state.header) == "Basic" && r.ui.list().state.headerBack && r.ui.list().state.count == 6 && r.ui.list().state.current == 1, "browser: it opens in the category of the current table (Basic), Saw marked");
    // the way up: the categories (Basic, Morph, the pack's, the player's, User)
    {
        const ui::Rect h = r.ui.list().headerRect();
        r.tap (h.x + 20, h.y + h.h / 2);
    }
    check (std::string (r.ui.list().state.header) == "WAVETABLES" && ! r.ui.list().state.headerBack && r.ui.list().state.count == 5, "browser: the way up shows the five categories (" + std::to_string (r.ui.list().state.count) + ")");
    check (std::string (r.ui.list().state.items[0]) == "Basic \xe2\x96\xb6" && std::string (r.ui.list().state.items[2]) == "Analog \xe2\x96\xb6" && std::string (r.ui.list().state.items[3]) == "Bass \xe2\x96\xb6" && std::string (r.ui.list().state.items[4]) == "User \xe2\x96\xb6", "browser: Basic, Morph, then the others by name, User last");
    auto row = [&] (int i) { const ui::Rect q = r.ui.list().rowRect (i); r.tap (q.x + 30, q.y + q.h / 2); };
    row (3);   // Bass
    check (std::string (r.ui.list().state.header) == "Bass" && r.ui.list().state.count == 5 && r.stack.top() == OscUi::kPageList, "browser: a category shows its tables (big, empty, x, y, z)");
    // the two files that cannot be loaded say why; a tap on one changes nothing
    check (std::string (r.ui.list().state.items[0]).find ("too large") != std::string::npos && std::string (r.ui.list().state.items[1]).find ("empty") != std::string::npos, "browser: a file above the limit and an empty file say why in their row");
    row (0);
    check (r.inst.wtRequests.empty() && r.stack.top() == OscUi::kPageList, "browser: a tap on a refused file loads nothing and the list stays");
    // a file: the request carries the user pack, the path below it, no hash (it is computed when the file is loaded); the browser closes
    row (3);   // y
    check (r.inst.wtRequests.size() == 1 && r.inst.wtRequests[0].osc == 1 && r.inst.wtRequests[0].ref.pack == "SynthyBoy/Wavetables" && r.inst.wtRequests[0].ref.file == "Bass/y.wav" && r.inst.wtRequests[0].ref.sha256.empty() && r.stack.depth() == 1, "browser: a tap on a file asks the instance to load Bass/y.wav of the user pack and closes the list");
    // what the instance does next: the content names the file, the oscillator is on "Preset table"
    auto showFile = [&] (const std::string& file, const std::string& name)
    {
        auto c = std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentWavetable w;
        w.osc = 1; w.ref.pack = "SynthyBoy/Wavetables"; w.ref.file = file; w.ref.name = name; w.frames = 2;
        c->wavetables.push_back (w);
        r.inst.contentPtr = c;
        r.inst.set ("osc1.wavetable", 13.0f);
        r.inst.loadState = 2;
        r.refresh();
    };
    showFile ("Bass/y.wav", "y");
    ui::Stepper& st = r.ui.full().top.source.stepper;
    check (std::string (st.state.text) == "y" && st.state.count == 3 && st.state.index == 1, "table: the selector shows the title of the file; y is in the middle of x, y, z (both arrows lit)");
    const ui::Rect sr = st.rect();
    r.tap (sr.x + sr.w - 8, sr.y + sr.h / 2);   // next: z
    check (r.inst.wtRequests.size() == 2 && r.inst.wtRequests[1].ref.file == "Bass/z.wav" && r.inst.get ("osc1.wavetable") == 13.0f, "table: the right arrow loads the next FILE of the category (z), the parameter stays Preset table");
    showFile ("Bass/z.wav", "z");
    check (st.state.index == 2 && st.state.count == 3, "table: z is the last file: the next arrow is dimmed");
    const size_t n = r.inst.wtRequests.size();
    r.tap (sr.x + sr.w - 8, sr.y + sr.h / 2);
    check (r.inst.wtRequests.size() == n && r.inst.get ("osc1.wavetable") == 13.0f, "table: at the last file nothing happens (no wrap, no built-in table)");
    r.tap (sr.x + 8, sr.y + sr.h / 2);   // previous: y
    check (r.inst.wtRequests.size() == n + 1 && r.inst.wtRequests.back().ref.file == "Bass/y.wav", "table: the left arrow loads the previous file (y; the refused big and empty files are skipped at the other end)");
    showFile ("Bass/x.wav", "x");
    check (st.state.index == 0, "table: x is the first loadable file: the previous arrow is dimmed");
    r.tap (sr.x + 8, sr.y + sr.h / 2);
    check (r.inst.wtRequests.size() == n + 1, "table: at the first file nothing happens");
    // the Q knob: the plugin leaves steps (the parameter stays), the page loads the neighbour file
    r.inst.wtSteps = 1;
    r.refresh();
    check (r.inst.wtRequests.size() == n + 2 && r.inst.wtRequests.back().ref.file == "Bass/y.wav", "table: a Q knob step moves to the next file as the arrow does");
    // a built-in table from the browser: the parameter is written as always
    showFile ("Bass/y.wav", "y");
    r.tapC (r.ui.full().top.source.dropdown);
    check (std::string (r.ui.list().state.header) == "Bass" && r.ui.list().state.current >= 0 && std::string (r.ui.list().state.items[r.ui.list().state.current]) == "y", "browser: with a file table it opens in its category with the file marked");
    {
        const ui::Rect h = r.ui.list().headerRect();
        r.tap (h.x + 20, h.y + h.h / 2);
    }
    row (1);   // Morph
    row (0);   // Saw to Square
    check (r.inst.get ("osc1.wavetable") == 5.0f && r.stack.depth() == 1, "browser: a built-in table of Morph writes oscN.wavetable (Saw to Square), the list closes");
    check (r.inst.wtRequests.size() == n + 2, "browser: choosing a built-in table loads no file");
    // the built-in stepping skips "Preset table" while the oscillator has no file table; the ends do not wrap
    r.inst.contentPtr = nullptr;
    r.inst.set ("osc1.wavetable", 12.0f);
    r.refresh();
    r.tap (sr.x + sr.w - 8, sr.y + sr.h / 2);
    check (r.inst.get ("osc1.wavetable") == 14.0f, "table: from Bell Partials the next table is Saw Down (Preset table, which would play the saw, is skipped without a file)");
    r.tap (sr.x + 8, sr.y + sr.h / 2);
    check (r.inst.get ("osc1.wavetable") == 12.0f, "table: and back");
}

void testWavetableRefusedFileAndCompactView()
{
    synthyboy::CatalogueSources sources;
    makeWavetableLibrary (sources);
    Rig r;
    r.inst.sources = sources;
    // the compact view (Play page): the name of the table opens the full screen and the browser over it
    r.tapC (r.ui.compact().basic.source.stepper);
    check (r.stack.depth() == 2 && r.stack.top() == OscUi::kPageList && std::string (r.ui.list().state.header) == "Basic", "compact view: the table's name opens the full screen and the wavetable browser");
    r.tap (20, 130);
    check (r.stack.depth() == 1, "compact view: the backdrop closes the browser");
    // a pack file: the request names the pack's folder, the manifest's path and the SHA-256 of the manifest
    r.tapC (r.ui.full().top.source.dropdown);
    {
        const ui::Rect h = r.ui.list().headerRect();
        r.tap (h.x + 20, h.y + h.h / 2);
    }
    auto row = [&] (int i) { const ui::Rect q = r.ui.list().rowRect (i); r.tap (q.x + 30, q.y + q.h / 2); };
    row (2);   // Analog
    check (r.ui.list().state.count == 1 && std::string (r.ui.list().state.items[0]) == "Warm Saw", "pack: a manifest title is the name of the row");
    row (0);
    check (r.inst.wtRequests.size() == 1 && r.inst.wtRequests[0].ref.pack == "com.t.pack" && r.inst.wtRequests[0].ref.file == "Analog/warm.wav" && r.inst.wtRequests[0].ref.sha256 == std::string (64, 'a'), "pack: the request carries the pack folder, the path and the manifest's SHA-256");
    // the load is refused: the display says it, and the row of that file says why the next time
    r.inst.loadState = 3;
    r.inst.loadMessage = "not a wavetable file: damaged or truncated audio data";
    r.refresh();
    check (r.ui.missingText() == "not a wavetable file: damaged or truncated audio data", "refused: the line over the display says why");
    r.inst.loadState = 0;
    r.refresh();
    r.tapC (r.ui.full().top.source.dropdown);
    {
        const ui::Rect h = r.ui.list().headerRect();
        r.tap (h.x + 20, h.y + h.h / 2);
    }
    row (2);
    check (std::string (r.ui.list().state.items[0]).find ("damaged or truncated audio data") != std::string::npos, "refused: the row of the file says why (" + std::string (r.ui.list().state.items[0]) + ")");
    row (0);
    check (r.inst.wtRequests.size() == 1, "refused: a tap on it asks for nothing more");
}

void testNativePanLevel()
{
    Rig r;
    const ui::Rect pan = r.ui.compact().basic.pan.rect();
    const ui::Rect level = r.ui.compact().basic.level.rect();
    check (pan.x == 14 && pan.y == 620 && pan.w == 202 && pan.h == 108, "native Pan occupies compact dial rectangle");
    check (level.x == 220 && level.y == 620 && level.w == 202 && level.h == 108, "native Level occupies compact dial rectangle");
    check (! r.ui.covers (pan.x + 45, pan.y + 54), "native Pan is not covered by overlay");
    check (! r.ui.covers (level.x + 45, level.y + 54), "native Level is not covered by overlay");
    check (r.ui.covers (pan.x + 45, 400), "overlay covers the display/graph region above the dials");
}

void testStandaloneNativeRouting()
{
    Rig r;
    r.ui.setPage (2);
    r.ui.setNativeInstanceSelector (true);
    r.ui.setNativeRouting (true);
    r.stack.openFull (OscUi::kElementOsc);
    r.refresh();

    check (r.ui.selectorArea().empty(), "a page of a tab has no in-screen source strip");
    const ui::Rect sel = r.ui.full().panArea();   // the bottom native row (PAN) took the freed strip row
    const ui::Rect route = r.ui.routingArea();
    check (sel == ui::Rect ({ 864, 690, 202, 48 }) && route == ui::Rect ({ 1122, 147, 150, 48 }),
           "standalone OscUi exposes selector and routing apertures");
    check (! r.ui.covers (sel.x + 10, sel.y + 10) && ! r.ui.covers (route.x + 10, route.y + 10),
           "standalone OscUi leaves selector and ON routing apertures uncovered on base page");
    check (! r.inst.fullOpen, "standalone OscUi clears fullOpen on base page");

    r.inst.set ("osc1.on", 0.0f);
    r.refresh();
    check (! r.ui.isNativeRoutingActive() && r.ui.covers (route.x + 10, route.y + 10),
           "standalone OscUi closes routing aperture when oscillator is OFF");
    check (! r.ui.covers (sel.x + 10, sel.y + 10),
           "standalone OscUi keeps selector aperture open when oscillator is OFF");

    r.inst.set ("osc1.on", 1.0f);
    r.stack.openDetail (oscspec::kMore);
    r.refresh();
    check (r.inst.fullOpen && r.ui.covers (sel.x + 10, sel.y + 10) && r.ui.covers (route.x + 10, route.y + 10),
           "standalone OscUi covers both apertures and sets fullOpen on MORE modal");

    r.stack.back();
    r.stack.openDetail (OscUi::kPageUnison);
    r.refresh();
    check (r.inst.fullOpen && r.ui.covers (sel.x + 10, sel.y + 10) && r.ui.covers (route.x + 10, route.y + 10),
           "standalone OscUi covers both apertures and sets fullOpen on UNISON modal");
}

void exportCompactContract()
{
    const char* const fields[] { "power", "expand", "routing", "first", "second", "pan", "level" };
    OscUi ui;
    Inst inst;
    UiStack stack;
    std::puts ("{");
    for (int source : { playosc::kSub, playosc::kA, playosc::kNoise })
    {
        inst.sel = source;
        ui.update (inst, 1, stack, 0.0, 1, 1);
        const auto items = ui.compactNativeItems();
        const char* group = source == playosc::kSub ? "sub" : source == playosc::kNoise ? "noise" : "abc";
        std::printf ("  \"%s\": {\n", group);
        for (size_t field = 0; field < items.size(); ++field)
        {
            const ui::Rect r = items[field] != nullptr ? items[field]->c->rect() : ui::Rect {};
            std::printf ("    \"%s\": [%d, %d, %d, %d]%s\n", fields[field], r.x, r.empty() ? 0 : r.y - OscUi::kTabY,
                         r.w, r.h, field + 1 < items.size() ? "," : "");
        }
        std::printf ("  }%s\n", source == playosc::kNoise ? "" : ",");
    }
    std::puts ("}");
}

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--compact-contract") == 0)
    {
        exportCompactContract();
        return 0;
    }
    testNativePanLevel();
    testStandaloneNativeRouting();
    testSteepEdgeNoLongLine();
    testEveryControlHasAParameterOrIsDisabled();
    testNoiseBasicQ1IsNoiseSample();
    testBasicGroupsAndSelection();
    testFullScreenOpensAndCloses();
    testKnobDragWritesTheParameter();
    testPanBipolarAndPower();
    testTypeSelectorAndKinds();
    testTuneOctSem();
    testDisabledControls();
    testRoutePopup();
    testDetailPages();
    testNavigatingAwayClosesEverything();
    testModulations();
    testHiddenModulationRoute();
    testDrawsOneSceneInsideTheCanvas();
    testQGroupFollowsTheActiveGroup();
    testContentBrowser();
    testSourceBrowserInTheOscillatorUi();
    testMissingContentLine();
    testInactiveLook();
    testTypeDropdown();
    testGranularAndSubAndNoise();
    testSourceAndWarpLists();
    testWavetableBrowser();
    testWavetableRefusedFileAndCompactView();
    testReview2();
    testSpecPages();
    testFollowUps();
    for (const std::string& d : g_libraries)
        if (d.compare (0, 11, "/tmp/sbovl-") == 0)
            (void) std::system (("rm -rf '" + d + "'").c_str());
    std::printf (g_fails ? "osc_ui_host: FAILED (%d)\n" : "osc_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
