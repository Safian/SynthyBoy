// SynthyBoy - MPC overlay tests: the pages of the OSC tab (osc_page.h): one page per oscillator (A, B, C, Sub, Noise) built on the full oscillator UI, with the oscillator's display; the page names its
// oscillator itself (the Play page's selector is neither read nor written), has no CLOSE button, sets the Q groups of the full view and opens the detail pages of the oscillator UI.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "../osc_page.h"
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
    int contentLoad (std::string*) const override { return loadState; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
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

struct Rig
{
    OscPageUi page;
    Inst inst;
    UiStack stack;
    double now = 1000.0;
    int sel;
    explicit Rig (int selector) : sel (selector)
    {
        inst.sel = 3;   // the Play page's selector says oscillator B: a page of another oscillator must not follow it
        stack.openFull (OscPageUi::kElementPage);   // what the controller does for a page view
        refresh();
    }
    void refresh()
    {
        now += 50.0;
        page::Input in;
        in.instance = sel;
        page.update (inst, stack, now, in);
    }
    void tap (int cx, int cy)
    {
        page.touch (ev (TouchEvent::Down, cx, cy), stack, inst, now);
        now += 40;
        page.tick (now, stack, inst);
        page.touch (ev (TouchEvent::Up, cx, cy), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void drag (int x0, int y0, int x1, int y1)
    {
        page.touch (ev (TouchEvent::Down, x0, y0), stack, inst, now);
        for (int i = 1; i <= 10; ++i)
        {
            now += 16.0;
            page.touch (ev (TouchEvent::Move, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10), stack, inst, now);
        }
        page.touch (ev (TouchEvent::Up, x1, y1), stack, inst, now);
        now += 100.0;
        refresh();
    }
};

bool hasLeaf (ui::Composite& root, const ui::Component* c)
{
    ui::Component* leaves[256];
    const int n = root.collectLeaves (leaves, 0, 256);
    for (int i = 0; i < n; ++i)
        if (leaves[i] == c)
            return true;
    return false;
}
} // namespace

int main()
{
    {
        Rig r (2);   // Osc A
        OscUi& u = r.page.ui();
        check (u.isPage() && u.selection() == 2 && r.page.selection() == 2, "the page of Osc A shows oscillator A although the Play selector says B");
        check (u.full().isPage() && ! hasLeaf (u.full(), &u.full().close), "the full screen is a page: no CLOSE button on it");
        check (r.inst.fullOpen && r.stack.element() == OscPageUi::kElementPage && r.stack.depth() == 1, "the page is the full screen of the oscillator UI (the host widgets are hidden)");
        check (r.inst.q[0] == Inst::idx ("osc1.type") && r.inst.q[2] == Inst::idx ("osc1.pan") && r.inst.q[3] == Inst::idx ("osc1.level"), "the Basic group of osc1 is the Q group on arrival");
        check (r.page.display().valid() && r.page.display().kind() == synthyboy::OscCycle::Kind::wavetable && ! u.displayRect().empty(), "the display of the oscillator has its data and its rectangle");
        // the level knob of the page writes osc1.level and nothing of the Play selector
        const ui::Rect lr = u.full().basic.level.rect();
        const float before = r.inst.get ("osc1.level");
        r.drag (lr.x + lr.w / 2, lr.y + lr.h / 2, lr.x + lr.w / 2, lr.y + lr.h / 2 - 40);
        check (r.inst.get ("osc1.level") > before + 5.0f && r.inst.selWrites == 0 && r.inst.sel == 3, "a drag on LEVEL writes osc1.level; the Play selector is not written");
        // the unison detail page opens over it and closes back to the page
        r.stack.openDetail (OscUi::kPageUnison);
        r.refresh();
        check (r.page.topPage() == OscUi::kPageUnison && ! u.displayRect().empty(), "the unison page opens over the page (the display stays)");
        r.stack.back();
        r.refresh();
        check (r.page.topPage() == 0 && r.stack.depth() == 1 && r.stack.element() == OscPageUi::kElementPage, "back: the page again");
        // the scene of the page: the ground is the host's, the picture is one scene
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        page::ground (sb);
        r.page.draw (sb);
        check (! sb.overflow && sd.numOps > 100, "the picture of the page is one scene (" + std::to_string (sd.numOps) + " ops)");
    }
    {
        Rig r (1);   // Sub
        check (r.page.ui().selection() == 1 && r.inst.q[3] == Inst::idx ("sub.level") && r.page.display().valid(), "the Sub page: the sub oscillator, its Basic group (sub.level), its display");
        check (r.page.ui().full().instance() == ui::OscInstance::Sub, "... and the sub oscillator's full screen");
    }
    {
        Rig r (5);   // Noise
        check (r.page.ui().selection() == 5 && r.inst.q[3] == Inst::idx ("noise.level"), "the Noise page: the noise, its Basic group (noise.level)");
        check (r.page.ui().full().instance() == ui::OscInstance::Noise, "... and the noise's full screen");
    }
    {
        Rig r (4);   // Osc C: the readout targets
        const std::vector<int> t = OscPageUi::targetsOf (4);
        check (t.size() == 4 && OscPageUi::targetsOf (1).empty() && OscPageUi::targetsOf (5).empty(), "a page lists the four readout targets of its own oscillator (none for Sub and Noise)");
        check (r.page.ui().selection() == 4 && r.inst.q[3] == Inst::idx ("osc3.level"), "the Osc C page is osc3");
    }
    std::printf (g_fails ? "osc_page_host: FAILED (%d)\n" : "osc_page_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
