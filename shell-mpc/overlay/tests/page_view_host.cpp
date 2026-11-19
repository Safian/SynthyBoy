// SynthyBoy - MPC overlay tests: the page views (page_view.h, pages.h, the controller's handling of them) with the REAL layout (overlay_layout.json): every tab the plugin draws entirely has its view, its two markers
// and the instance picture, and no host widget lies under it; the controller keeps the page of the element open for the instance whose tab is on screen, closes it when another tab comes, gives a touch to the page,
// hands the readout targets to the filter pages, and the pictures of all of them fit the scene.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "fakes.h"

using namespace ovt;

namespace
{
/** A page of the KIT (mpckit::PageBase over mpckit::IInstance): one knob bound to a parameter by its key. It is the whole page; the view around it is BasicPageView<KitPage, NoHooks>. */
class KitPage : public mpckit::PageBase
{
public:
    static constexpr int kElementPage = 2;
    KitPage() : cutoff_ ("Cutoff")
    {
        bind (1, cutoff_, mpckit::Binder::Type::Knob, "filter1.cutoff");
        group ({ &cutoff_ });
        relayout();
    }
    const synthyboy_mpc::ui::Knob& knob() const noexcept { return cutoff_; }

protected:
    void layout (const synthyboy_mpc::ui::Rect& tab) override { cutoff_.place ({ 100, tab.y + 100, 300, 200 }); }

private:
    synthyboy_mpc::ui::Knob cutoff_;
};

struct Rig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    std::unique_ptr<SynthyBoyController> ctl;
    double now = 0.0;
    Rig()
    {
        ctl = std::make_unique<SynthyBoyController> (plane, frame, renderer, dir.paths(), Timing(), 10.0);
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
        const char* const names[] = { "filter1", "filter2", "lfo1", "lfo2", "lfo3", "lfo4", "env1", "env2", "env3", "env4", "osca", "oscb", "oscc", "oscsub", "oscnoise", "macros", "mixer", "fx", "mod", "master", "voice" };
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
        check (all && pageKinds && wholeTab, "the layout has the 21 page views of the tabs the plugin draws entirely (whole tab, opaque)");
        check (layout::isPageKind (layout::kKindPlay) && layout::isPageKind (layout::kKindOscUi) && ! layout::isPageKind (layout::kKindFilter), "a page kind is the Play page and the pages of the elements (the oscillators of the OSC tab included): no tab of the layout is a host page any more");
        check (layout::kNumViews <= 24, "the layout has " + std::to_string (layout::kNumViews) + " views (the detector holds 24)");
    }
    // ---- the controller opens the page of the element for the instance whose tab is on screen
    {
        Rig r;
        const int slot = r.ctl->slotOf (&r.inst);
        const int filter1 = r.viewOf ("filter1"), mixer = r.viewOf ("mixer"), osca = r.viewOf ("osca"), oscsub = r.viewOf ("oscsub"), play1 = r.viewOf ("play1"), env2 = r.viewOf ("env2");
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
        // an oscillator page of the OSC tab: the page of the oscillator UI, clean (no detail page of the mixer)
        r.frame.setView (mixer, false, 90);
        r.inst.playSel[0] = 3;   // the Play page's selector says oscillator B: the Osc A page must not follow it nor write it
        r.frame.setView (osca, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == osca && r.ctl->uiState (slot).element() == OscUi::kElementOsc && r.ctl->uiState (slot).depth() == 1 && r.inst.uiFullOpen.load(), "the Osc A tab: the full page of the oscillator UI is open, the host widgets are hidden");
        {
            const auto* pg = dynamic_cast<const pages::OscPage*> (r.ctl->currentView());
            check (pg != nullptr && pg->ui().selection() == 2 && pg->ui().ui().selection() == 2 && r.inst.playSel[0] == 3, "the Osc A page shows oscillator A (selector number 2) whatever the Play selector says (B), and does not write it");
        }
        r.frame.setView (osca, false, 90);
        r.frame.setView (oscsub, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == oscsub && r.ctl->uiState (slot).element() == OscUi::kElementOsc && r.inst.playSel[0] == 3, "the Sub tab: the same page for the sub oscillator; the Play selector is still B");
        r.frame.setView (oscsub, false, 90);
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
    // ---- the Q group: the router keeps what the LAST page set, so a page that comes back on screen sets its group again (the Q-Links must act on the page that is up)
    {
        Rig r;
        const int mixer = r.viewOf ("mixer"), filter1 = r.viewOf ("filter1");
        r.frame.setView (mixer, true, 90);
        r.run (600);
        const int mixerFirst = r.inst.qGroup[0];
        check (mixerFirst >= 0, "the Mixer page sets its Q group (the level of the strip)");
        r.frame.setView (mixer, false, 90);
        r.frame.setView (filter1, true, 90);
        r.run (600);
        const int filterFirst = r.inst.qGroup[0];
        check (filterFirst >= 0 && filterFirst != mixerFirst, "the Filter page sets its own Q group (the cutoff)");
        r.frame.setView (filter1, false, 90);
        r.frame.setView (mixer, true, 90);
        r.run (600);
        check (r.inst.qGroup[0] == mixerFirst, "back on the Mixer page: its Q group is set again, the Q-Links act on the page that is up (" + std::to_string (r.inst.qGroup[0]) + ")");
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
        SynthyBoyPageFactory::targetsOfAllPages (t);
        int filterPages = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
            filterPages += layout::kViews[i].kind == layout::kKindFilterUi ? 1 : 0;
        int expect = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const int k = layout::kViews[i].kind;
            expect += k == layout::kKindFilterUi ? 4 : (k == layout::kKindOscUi ? static_cast<int> (OscPageUi::targetsOf (layout::kViews[i].index).size()) : (k == layout::kKindPlay ? 12 : 0));
        }
        check (filterPages == 2 && static_cast<int> (t.size()) == expect && expect <= synthyboy::LiveValues::kMaxTargets, "readout targets: " + std::to_string (t.size()) + " (the filter pages 4 each, the oscillator pages A, B, C 4 each, the Play views 12 each), within the engine's " + std::to_string (synthyboy::LiveValues::kMaxTargets));
    }
    // ---- a page of the KIT over the page view (migration step 6): BasicPageView<KitPage, NoHooks> takes mpckit::IInstance, loads no macro names and has no loading bar
    {
        FakeInstance inst;
        inst.set ("filter1.cutoff", 3000.0f);
        UiStack stack;
        stack.openFull (KitPage::kElementPage);
        BasicPageView<KitPage, NoHooks> pv ("kit", 1);
        ViewInput in;
        in.instance = &inst; in.io = &inst; in.ui = &stack; in.nowMs = 1000.0;
        check (pv.update (in) && pv.valid() && pv.pageElement() == KitPage::kElementPage, "a kit page in the page view: the first update has a picture, the element is the page's");
        const int idx = inst.index ("filter1.cutoff");
        check (std::fabs (pv.ui().knob().state.value - mpckit::synthyboy_adapter::Params::get().toNormalised (idx, 3000.0f)) < 1.0e-6, "... the knob shows the parameter the key names (through the kit's parameter source)");
        check (inst.uiFullOpen.load(), "... and the page told the instance a full screen page is up (IInstance::setUiFullOpen)");
        const auto knobRect = pv.ui().knob().rect();
        const int x = knobRect.x + knobRect.w / 2, y = knobRect.y + knobRect.h / 2;
        TouchEvent d; d.kind = TouchEvent::Down; d.x = x; d.y = y;
        TouchEvent m; m.kind = TouchEvent::Move; m.x = x; m.y = y - 60;
        TouchEvent u; u.kind = TouchEvent::Up; u.x = x; u.y = y - 60;
        const float before = inst.get ("filter1.cutoff");
        const bool took = pv.onTouch (d, stack, inst, 1100.0);
        pv.onTouch (m, stack, inst, 1116.0);
        pv.onTouch (u, stack, inst, 1140.0);
        check (took && inst.get ("filter1.cutoff") > before, "... a drag of the knob writes the parameter through the instance (" + std::to_string (before) + " -> " + std::to_string (inst.get ("filter1.cutoff")) + " Hz)");
        in.nowMs = 1200.0;
        pv.update (in);
        check (std::fabs (pv.ui().knob().state.value - mpckit::synthyboy_adapter::Params::get().toNormalised (idx, inst.get ("filter1.cutoff"))) < 1.0e-6, "... and the next update shows it");
        // the SynthyBoy hooks are the default: the same view over a SynthyBoy page still loads the macro names and shows the loading bar
        static_assert (std::is_same<PageView<FilterUi>, BasicPageView<FilterUi, MacroNameHooks>>::value, "the page view of SynthyBoy has its hooks");
    }
    // ---- the view factory of SynthyBoy (migration step 10): the view behind every page of the page set, and the readout targets all of them want
    {
        CpuCurveRenderer renderer;
        SynthyBoyPageFactory factory (&renderer, 10.0);
        const mpckit::IPageSet& set = mpckit::synthyboy_adapter::PageSet::get();
        bool all = set.numPages() == layout::kNumViews, names = true, elements = true;
        for (int i = 0; i < set.numPages() && all; ++i)
        {
            const mpckit::PageDef& d = set.page (i);
            const std::unique_ptr<IView> v = factory.make (d);
            all = all && v != nullptr;
            if (v)
            {
                names = names && std::string (v->name()) == std::string (d.name);
                elements = elements && ((d.kind == layout::kKindPlay) == (v->pageElement() < 0)) && (! layout::isPageKind (d.kind) || d.kind == layout::kKindPlay || v->pageElement() >= 0);
            }
        }
        check (all && names && elements, "the page factory makes a view for every page of the set: its name is the page's, a page of an element names its element, the Play view none");
        std::vector<int> a, b;
        factory.readoutTargets (a);
        SynthyBoyPageFactory::targetsOfAllPages (b);
        check (! a.empty() && a == b, "the readout targets of the factory are the ones the controller reports (" + std::to_string (a.size()) + ")");
    }
    std::printf (g_fails ? "page_view_host: FAILED (%d)\n" : "page_view_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
