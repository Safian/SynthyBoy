// SynthyBoy - MPC overlay tests: the page views (page_view.h, pages.h, the controller's handling of them) with the REAL layout (overlay_layout.json): every tab the plugin draws entirely has its view, its two markers
// and the instance picture, and no host widget lies under it; the controller keeps the page of the element open for the instance whose tab is on screen, closes it when another tab comes, gives a touch to the page,
// hands the readout targets to the filter pages, and the pictures of all of them fit the scene.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "fakes.h"

using namespace ovt;

namespace
{
struct Rig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    std::unique_ptr<Controller> ctl;
    double now = 0.0;
    Rig()
    {
        ctl = std::make_unique<Controller> (plane, frame, renderer, dir.paths(), Timing(), 10.0);
        ctl->attach (&inst);
    }
    void run (double ms, double step = 10.0)
    {
        const double end = now + ms;
        while (now < end) { now += step; ctl->tick (now); }
    }
    int viewOf (const char* name) const
    {
        for (int i = 0; i < layout::kNumViews; ++i)
            if (std::string (layout::kViews[i].name) == name)
                return i;
        return -1;
    }
};
} // namespace

int main()
{
    // ---- the layout: the pages, their colours, their tabs
    {
        const char* const names[] = { "filter1", "filter2", "lfo1", "lfo2", "lfo3", "lfo4", "env1", "env2", "env3", "env4", "macros", "mixer", "fx", "mod", "master", "voice" };
        Rig r;
        bool all = true, pageKinds = true, wholeTab = true;
        for (const char* n : names)
        {
            const int v = r.viewOf (n);
            all = all && v >= 0;
            if (v >= 0)
            {
                pageKinds = pageKinds && layout::isPageKind (layout::kViews[v].kind);
                const layout::View& lv = layout::kViews[v];
                wholeTab = wholeTab && lv.region.x == 0 && lv.region.w == 1280 && lv.region.h == 628 && lv.opaque;
            }
        }
        check (all && pageKinds && wholeTab, "the layout has the 16 page views of the tabs the plugin draws entirely (whole tab, opaque)");
        check (layout::isPageKind (layout::kKindPlay) && ! layout::isPageKind (layout::kKindOsc) && ! layout::isPageKind (layout::kKindFilter), "a page kind is the Play page and the pages of the elements; the oscillator tabs keep their own pictures");
        check (layout::kNumViews <= 24, "the layout has " + std::to_string (layout::kNumViews) + " views (the detector holds 24)");
    }
    // ---- the controller opens the page of the element for the instance whose tab is on screen
    {
        Rig r;
        const int slot = r.ctl->slotOf (&r.inst);
        const int filter1 = r.viewOf ("filter1"), mixer = r.viewOf ("mixer"), osc1 = r.viewOf ("osc1"), play1 = r.viewOf ("play1"), env2 = r.viewOf ("env2");
        r.frame.setView (filter1, true, 90);
        r.run (600);
        check (r.ctl->currentIndex() == filter1 && ! r.ctl->uiState (slot).isMain() && r.ctl->uiState (slot).element() == FilterUi::kElementPage, "the Filter tab: the page of the filter element is open for the instance");
        check (r.inst.uiFullOpen.load(), "... and the host widgets of the tab are hidden (the instance reports the frame they hide at)");
        // another page: the stack is replaced by the page of that element
        r.frame.setView (filter1, false, 90);
        r.frame.setView (mixer, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == mixer && r.ctl->uiState (slot).element() == MixerUi::kElementPage && r.ctl->uiState (slot).depth() == 1, "the Mixer tab: the stack holds the mixer page only");
        r.ctl->openDetail (slot, MixerUi::kPageList);
        r.run (200);
        // the same page again after another tab
        r.frame.setView (mixer, false, 90);
        r.frame.setView (osc1, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == osc1 && r.ctl->uiState (slot).isMain() && ! r.inst.uiFullOpen.load(), "a tab of the host (Osc 1): the stack is closed and the host widgets come back");
        r.frame.setView (osc1, false, 90);
        r.frame.setView (env2, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == env2 && r.ctl->uiState (slot).element() == EnvUi::kElementPage, "the Env 2 tab: the page of the envelope element");
        // the Play page keeps its own way: main, and its elements open from it
        r.frame.setView (env2, false, 90);
        r.frame.setView (play1, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == play1 && r.ctl->uiState (slot).isMain(), "the Play tab: main (the compact views), not a page of an element");
        // a different page view of the same kind does not keep the detail pages of the other: going Env 2 -> Env 3 is another view
        const int env3 = r.viewOf ("env3");
        r.frame.setView (play1, false, 90);
        r.frame.setView (env2, true, 90);
        r.run (300);
        r.ctl->openDetail (slot, 31);
        r.frame.setView (env2, false, 90);
        r.frame.setView (env3, true, 90);
        r.run (500);
        check (r.ctl->uiState (slot).depth() == 1 && r.ctl->uiState (slot).element() == EnvUi::kElementPage, "Env 2 -> Env 3: the page of the new tab opens clean (no detail page of the other)");
        // the status file names the page
        r.run (1200);
        const std::string st = r.dir.read ("status.txt");
        check (st.find ("page_view: env3") != std::string::npos && st.find ("page_instance: 3") != std::string::npos, "the status file names the page view and its instance");
    }
    // ---- the blank instance picture while the page is up: the page is there, the instance is not told (the widgets are hidden by design): the page stays
    {
        Rig r;
        const int slot = r.ctl->slotOf (&r.inst);
        const int fx = r.viewOf ("fx");
        r.frame.setView (fx, true, 90);
        r.run (600);
        check (r.ctl->uiState (slot).element() == FxUi::kElementPage, "the FX tab: the page is open");
        r.frame.setViewInstanceRgb (fx, layout::kInstanceBlankRgb, true, 90);
        r.run (600);
        check (r.ctl->currentIndex() == fx && r.ctl->uiState (slot).element() == FxUi::kElementPage && r.ctl->state() == "visible", "the instance picture is blank (the host widgets are hidden: frame 24): the page stays up");
    }
    // ---- the readout targets: the filter pages ask for the cutoff and the resonance of both filters
    {
        std::vector<int> t;
        Controller::readoutTargets (t);
        int filterPages = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
            filterPages += layout::kViews[i].kind == layout::kKindFilterUi ? 1 : 0;
        int expect = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const int k = layout::kViews[i].kind;
            expect += k == layout::kKindFilterUi ? 4 : (k == layout::kKindOsc ? 4 : (k == layout::kKindPlay ? 12 : 0));
        }
        check (filterPages == 2 && static_cast<int> (t.size()) == expect && expect <= synthyboy::LiveValues::kMaxTargets, "readout targets: " + std::to_string (t.size()) + " (the filter pages 4 each, the oscillators 4 each, the Play views 12 each), within the engine's " + std::to_string (synthyboy::LiveValues::kMaxTargets));
    }
    std::printf (g_fails ? "page_view_host: FAILED (%d)\n" : "page_view_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
