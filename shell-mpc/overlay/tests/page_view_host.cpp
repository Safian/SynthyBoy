// SynthyBoy - MPC overlay tests: the page views (page_view.h, pages.h, the controller's handling of them) with the REAL layout (overlay_layout.json): every tab the plugin draws entirely has its view, its two markers
// and the instance picture, and no host widget lies under it; the controller keeps the page of the element open for the instance whose tab is on screen, closes it when another tab comes, gives a touch to the page,
// hands the readout targets to the filter pages, and the pictures of all of them fit the scene.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "fakes.h"
#include "preset_graph_checks.h"

using namespace ovt;

namespace
{
namespace nativeui = synthyboy_mpc::nativeui;
namespace tabview = synthyboy_mpc::tabview;
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

struct FilterInstance : FakeInstance
{
    nativeui::FilterCellBank nativeFilter;
    void setNativeFilterControls (int view, const nativeui::FxCell* cells, const float*, const int*) noexcept override { nativeFilter.publish (view, cells); }
    bool takeNativeFilterSlot (int slot, const nativeui::FxCell& expected, float& value) noexcept override { return nativeFilter.take (slot, expected, value); }
    void action (int id)
    {
        const auto binding = nativeFilter.at (nativeui::FilterAction);
        nativeFilter.report (binding);
        nativeFilter.post (binding, static_cast<float> (id) / nativeui::FilterLastAction);
    }
};
void testNativeFilterPage()
    {
        FilterInstance inst;
        UiStack stack;
        stack.openFull (FilterUi::kElementPage);
        pages::FilterPage pv ("filter1", 1);
        ViewInput in;
        in.instance = &inst; in.io = &inst; in.ui = &stack; in.nowMs = 1000.0;
        inst.setTabView (IInstanceView::kTabViewFilter, 1);   // a project saved in the state F2
        check (pv.update (in) && pv.ui().view() == 1 && pv.ui().editFilter() == 2, "the Filter page view opens in the saved state (F2)");
        auto touch = [&] (int x, int y, double t0)
        {
            TouchEvent d; d.kind = TouchEvent::Down; d.x = x; d.y = y;
            TouchEvent u; u.kind = TouchEvent::Up; u.x = x; u.y = y;
            const bool took = pv.onTouch (d, stack, inst, t0);
            pv.onTouch (u, stack, inst, t0 + 40.0);
            in.nowMs = t0 + 200.0;
            pv.update (in);
            return took;
        };
        auto tapState = [&] (int state, double t0)
        {
            const auto c = state >= 2 && pv.ui().isSplit()
                ? pv.ui().full().scalarView.cell (state - 2, 2)
                : pv.ui().full().view.cell (state, pv.ui().isSplit() ? 2 : 4);
            check (! touch (c.x + c.w / 2, c.y + c.h / 2, t0), "the native selector aperture has no overlay input fallback");
            inst.action (nativeui::FilterF1 + state);
            in.nowMs += 50; pv.update (in); in.nowMs += 50; pv.update (in);
            if (state >= 2 && pv.ui().isSplit())
            {
                inst.action (nativeui::FilterExpandCurve);
                in.nowMs += 50; pv.update (in); in.nowMs += 50; pv.update (in);
            }
            return true;
        };
        check (tapState (3, 1100.0) && pv.ui().view() == 3
               && inst.tabViewV[IInstanceView::kTabViewFilter].load() == tabview::encodeFilterSplit (1, 1, tabview::kFilterExpandedCurve)
               && pv.ui().curveView(), "native VELO selection and expand preserve F2 and open the stored scalar editor");
        check (pv.valid() && pv.ui().curveUi().which() == 0 && pv.ui().curveUi().source() == 23, "the VELO editor is the one of the source 23");
        // a drag of the canvas of the editor reaches it (the page view routes the touch by the position)
        const auto& cv = pv.ui().curveUi().screen.canvas;
        const int x = cv.pxOf (0.0f), y = cv.pyOf (0.0f);
        const auto w0 = inst.uiWrites.load();
        TouchEvent d; d.kind = TouchEvent::Down; d.x = x; d.y = y;
        TouchEvent m; m.kind = TouchEvent::Move; m.x = x + 10; m.y = y - 100;
        TouchEvent u; u.kind = TouchEvent::Up; u.x = x + 10; u.y = y - 100;
        pv.onTouch (d, stack, inst, 1400.0);
        pv.onTouch (m, stack, inst, 1420.0);
        pv.onTouch (u, stack, inst, 1440.0);
        check (inst.uiWrites.load() == w0, "a drag in the editor writes content, no engine parameter through the page view");
        check (tapState (0, 1600.0) && pv.ui().view() == 0 && ! pv.ui().curveView() && pv.ui().editFilter() == 1, "a tap on F1 through the page view is the filter page again");
    }

struct Rig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    std::unique_ptr<SynthyBoyController> ctl;
    double now = 0.0;
    explicit Rig (ITouchInput* touch = nullptr)
    {
        Timing timing;
        timing.nativeTabTouch = touch != nullptr;
        ctl = std::make_unique<SynthyBoyController> (plane, frame, renderer, dir.paths(), timing, 10.0, nullptr, touch);
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
void testHostDrawerClipsPageFurniture()
{
    Rig rig;
    for (const char* name : { "osca", "oscsub", "env1", "filter1", "mod" })
    {
        auto* view = const_cast<IView*> (rig.ctl->viewByName (name));
        check (view != nullptr, std::string (name) + " has a drawer clipping test target");
        if (!view) continue;
        UiStack stack;
        stack.openFull (view->pageElement());
        ViewInput input;
        input.io = &rig.inst;
        input.ui = &stack;
        input.nowMs = 100;
        input.panelEdgeX = page::kW / 2;
        view->update (input);
        std::vector<uint32_t> pixels (page::kW * page::kH);
        view->render (pixels.data(), page::kW, page::kH);
        bool hasPage = false, drewOverDrawer = false;
        for (int y = page::kTabY; y < page::kH; ++y)
            for (int x = 0; x < page::kW; ++x)
                if (pixels[y * page::kW + x] >> 24)
                    (x < input.panelEdgeX ? hasPage : drewOverDrawer) = true;
        check (hasPage && !drewOverDrawer, std::string (name) + " draws visible page furniture only left of the host drawer");
    }
}

void testOscNativeBackgrounds()
{
    namespace ui = synthyboy_mpc::ui;
    // The pilot-manifest executable intentionally has no oscillator furniture.
    if (backgroundOf ("osca") == nullptr)
        return;
    constexpr ui::Rect tab { 0, page::kTabY, page::kW, page::kTabH };
    ui::OscillatorFullScreen furniture;
    furniture.setNativeInstanceSelector (true);
    furniture.setNativeRouting (true);
    furniture.place (tab);
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, page::kW, page::kH);
    ui::drawOscillatorPageGround (sb, tab, tab);
    furniture.drawStaticFurniture (sb);
    std::vector<uint32_t> asset;
    renderScene (*scene, asset);

    struct Case { const char* view; const char* power; const char* type; float mode; };
    const Case cases[] {
        {"osca", "osc1.on", "osc1.type", 0}, {"oscb", "osc2.on", "osc2.type", 2}, {"oscc", "osc3.on", "osc3.type", 3},
        {"oscsub", "sub.on", nullptr, 0}, {"oscnoise", "noise.on", "noise.type", 1},
    };
    for (const auto& source : cases)
    {
        Rig r;
        const int index = r.viewOf (source.view);
        const auto* background = backgroundOf (source.view);
        check (index >= 0 && background != nullptr, std::string (source.view) + " has a release background entry");
        if (index < 0 || background == nullptr)
            continue;
        r.inst.set (source.power, 1.0f);
        if (source.type != nullptr)
            r.inst.set (source.type, source.mode);
        r.frame.setView (index, true, 90);
        const int sx = background->sentinelX, sy = page::kTabY + background->sentinelY;
        auto present = [&] (uint32_t rgb, bool on) {
            r.frame.setUiRect (sx, sy, 5, 5, rgb, on, 90);
            r.run (800);
        };
        present (0x1e1d1eu, true);
        auto* view = const_cast<pages::OscPage*> (dynamic_cast<const pages::OscPage*> (r.ctl->viewByName (source.view)));
        check (view != nullptr && r.ctl->currentIndex() == index && ! view->ui().hostBackground(),
               std::string (source.view) + " stays opaque on the host's plain tab colour");
        if (view == nullptr)
            continue;
        const auto capture = [&] {
            std::vector<uint32_t> pixels (page::kW * page::kH);
            view->render (pixels.data(), page::kW, page::kH);
            return pixels;
        };
        const auto pixelAt = [] (int x, int y) { return static_cast<size_t> (y * page::kW + x); };
        const ui::Rect selector = view->ui().selectorArea(), power = view->ui().powerArea(), routing = view->ui().routingArea();
        const ui::Rect pan = view->ui().ui().full().panArea(), level = view->ui().ui().full().levelArea();
        check (power == ui::Rect { 212, 143, 64, 56 }, std::string (source.view) + " shares the packaged POWER bounds");
        const auto hitAt = [&] (ui::Rect area) { return view->ui().covers (area.x + area.w / 2, area.y + area.h / 2); };
        const size_t outside = pixelAt (1, tab.y + tab.h / 2);
        const auto cold = capture();
        check (cold[outside] >> 24 == 255 && cold[pixelAt (power.x + 4, power.y + 8)] == 0
               && ! hitAt (selector) && ! hitAt (power) && ! hitAt (routing),
               std::string (source.view) + " draws fallback furniture around the three operable native controls");

        present (background->sentinelRgb, true);
        const auto foreground = capture();
        check (view->ui().hostBackground() && foreground[outside] >> 24 == 0,
               std::string (source.view) + " reveals the native furniture after the sentinel matches");
        check (foreground[pixelAt (selector.x + 4, selector.y + 8)] == 0
               && foreground[pixelAt (routing.x + 4, routing.y + 8)] == 0
               && foreground[pixelAt (power.x + 4, power.y + 8)] == 0
               && foreground[pixelAt (pan.x + 4, pan.y + 8)] == 0
               && foreground[pixelAt (level.x + 4, level.y + 8)] == 0
               && ! hitAt (selector) && ! hitAt (power) && ! hitAt (routing) && ! hitAt (pan) && ! hitAt (level),
               std::string (source.view) + " leaves selector, POWER, PAN, LEVEL and routing apertures transparent and clickable");

        const auto& layout = layout::kViews[index];
        check (foreground[pixelAt (sx + 2, sy + 2)] == cold[pixelAt (sx + 2, sy + 2)]
               && foreground[pixelAt (layout.markers[0].rect.x, layout.markers[0].rect.y)] == cold[pixelAt (layout.markers[0].rect.x, layout.markers[0].rect.y)]
               && foreground[pixelAt (layout.markers[1].rect.x, layout.markers[1].rect.y)] == cold[pixelAt (layout.markers[1].rect.x, layout.markers[1].rect.y)]
               && foreground[pixelAt (layout.instance.x, layout.instance.y)] == cold[pixelAt (layout.instance.x, layout.instance.y)],
               std::string (source.view) + " covers the sentinel, both marker squares and the instance strip");
        size_t different = 0;
        int maxDiff = 0;
        for (int y = tab.y; y < tab.bottom(); ++y)
            for (int x = 0; x < tab.right(); ++x)
            {
                if (selector.contains (x, y) || power.contains (x, y) || routing.contains (x, y) || pan.contains (x, y) || level.contains (x, y))
                    continue;
                const size_t pixel = pixelAt (x, y);
                const auto composed = draw::over (asset[pixel], foreground[pixel], 255);
                int error = 0;
                for (int shift = 0; shift <= 24; shift += 8)
                    error = std::max (error, std::abs (static_cast<int> ((composed >> shift) & 255u) - static_cast<int> ((cold[pixel] >> shift) & 255u)));
                maxDiff = std::max (maxDiff, error);
                different += error > 2;
            }
        check (different == 0, std::string (source.view) + " composed tab matches the opaque reference (" +
               std::to_string (different) + " pixels beyond tolerance, max channel difference " + std::to_string (maxDiff) + ")");

        if (source.type != nullptr && std::string (source.type) != "noise.type")
        {
            r.ctl->openDetail (r.ctl->slotOf (&r.inst), oscspec::kMore);
            r.run (300);
            const auto modal = capture();
            check (! view->ui().hostBackground() && modal[outside] >> 24 == 255 && modal[pixelAt (power.x + 4, power.y + 8)] >> 24 == 255
                   && hitAt (selector) && hitAt (power) && hitAt (routing),
                   std::string (source.view) + " draws its own modal ground and claims all three apertures");
            r.ctl->back (r.ctl->slotOf (&r.inst));
            r.run (300);
            check (view->ui().hostBackground(), std::string (source.view) + " restores the native root after Back");
        }
        const auto writes = r.inst.uiWrites.load();
        r.inst.set (source.power, 0.0f);
        r.run (500);
        const auto off = capture();
        const auto grey = off[outside];
        check (! view->ui().hostBackground() && grey >> 24 == 255
               && ((grey >> 16) & 255u) == ((grey >> 8) & 255u) && ((grey >> 8) & 255u) == (grey & 255u)
               && off[pixelAt (power.x + 4, power.y + 8)] == 0
               && ! hitAt (selector) && ! hitAt (power) && hitAt (routing) && r.inst.uiWrites.load() == writes,
               std::string (source.view) + " paints OFF ground in grey and preserves host selector and POWER touch");
        r.inst.set (source.power, 1.0f);
        r.run (500);
        check (view->ui().hostBackground(), std::string (source.view) + " restores the native furniture when powered on");
        present (0, false);
        check (! view->ui().hostBackground() && capture()[outside] >> 24 == 255,
               std::string (source.view) + " repaints the fallback when its sentinel disappears");
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--filter-only") == 0)
    {
        preset_graph::Preset a, b;
        preset_graph::runFilter (a, b);
        testNativeFilterPage();
        std::printf (g_fails ? "Filter page/preset: FAILED (%d)\n" : "Filter page/preset: OK\n", g_fails);
        return g_fails ? 1 : 0;
    }

    if (argc == 2 && std::string (argv[1]) == "--osc-backgrounds-only")
    {
        testOscNativeBackgrounds();
        return g_fails == 0 ? 0 : 1;
    }
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    testHostDrawerClipsPageFurniture();
    testOscNativeBackgrounds();
    preset_graph::run();
    // A host flip can take place between two framebuffer acquisitions inside one tick.
    {
        FakeFrame frame;
        const int rot = 270;
        MarkerSpec pair[] = { { { 34, 135, 5, 5 }, 0x6b3a2cu }, { { 1242, 135, 5, 5 }, 0x3a6b64u } };
        MarkerSpec ground { { 900, 130, 5, 5 }, 0x0b1025u };
        for (const auto& mark : pair)
            frame.setUiRect (mark.rect.x, mark.rect.y, 5, 5, mark.rgb, true, rot);
        frame.setUiRect (900, 130, 5, 5, ground.rgb, true, rot);
        PageDetector detector (frame, pair, 1, nullptr, nullptr, &ground);
        const Detection first = detector.check (rot);
        frame.setUiRect (900, 130, 5, 5, 0x141a38u, true, rot);
        const Detection next = detector.check (rot);
        check (first.view == 0 && first.sentinelView == 0 && next.view == 0 && next.sentinelView < 0 && frame.begins == 2,
               "each marker and native-background decision comes from one primary framebuffer acquisition");
    }
    // ---- the layout: the pages, their colours, their tabs
    {
        const char* const names[] = { "filter1", "lfo1", "env1", "osca", "oscb", "oscc", "oscsub", "oscnoise", "macros", "mixer", "fx", "mod", "master", "voice" };
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
        check (all && pageKinds && wholeTab, "the layout has the 14 page views of the tabs the plugin draws entirely (whole tab, opaque; ONE Filter tab, no filter2)");
        check (layout::isPageKind (layout::kKindPlay) && layout::isPageKind (layout::kKindOscUi) && ! layout::isPageKind (layout::kKindFilter), "a page kind is the Play page and the pages of the elements (the oscillators of the OSC tab included): no tab of the layout is a host page any more");
        check (layout::kNumViews <= 24, "the layout has " + std::to_string (layout::kNumViews) + " views (the detector holds 24)");
    }
    // ---- the host background switch: only the views of the build-time manifest (backgrounds_manifest.h, generated from the skin generator's BACKGROUND_TABS) get it; the others draw everything
    {
        int withBg = 0, withoutBg = 0;
        bool wired = true;
        Rig r;
        for (const layout::View& lv : layout::kViews)
        {
            const IView* view = r.ctl->viewByName (lv.name);
            const bool listed = hasHostBackground (lv.name);
            Rect sr;
            uint32_t srgb = 0;
            const bool hasSentinel = view && view->hostBackgroundSentinel (sr, srgb);
            wired = wired && (hasSentinel == listed);
            if (listed && hasSentinel)
            {
                const BackgroundTab* bg = backgroundOf (lv.name);
                wired = wired && sr.x == bg->sentinelX && sr.y == 110 + bg->sentinelY && sr.w == 5 && sr.h == 5 && srgb == bg->sentinelRgb;
            }
            (listed ? withBg : withoutBg)++;
        }
        check (wired && withBg == kNumBackgroundTabs && withoutBg == layout::kNumViews - withBg, "the manifest's views carry their sentinel, the others have none (" + std::to_string (withBg) + " with, " + std::to_string (withoutBg) + " without)");
        const auto* mp = dynamic_cast<const pages::MacrosPage*> (r.ctl->viewByName ("macros"));
        check (mp != nullptr && ! mp->ui().hostBackground(), "manifest views start opaque before sentinel is read");
        const char* const required[] { "macros", "filter1", "play1", "mixer", "fx", "master", "voice",
                                       "osca", "oscb", "oscc", "oscsub", "oscnoise", "mod" };
        check (std::all_of (std::begin (required), std::end (required), hasHostBackground) && ! hasHostBackground (nullptr),
               "the release manifest includes all five OSC backgrounds, Mod and the existing seven tabs");
    }
    if (hasHostBackground ("play1") && hasHostBackground ("macros"))
    {
        struct Touch final : ITouchInput
        {
            std::vector<TouchEvent> events;
            void configure (int, int, int) override {}
            int poll (const std::function<void (const TouchEvent&)>& send) override
            {
                const int count = static_cast<int> (events.size());
                for (const auto& e : events) send (e);
                events.clear();
                return count;
            }
        } touch;
        Rig r (&touch);
        const int play = r.viewOf ("play1"), macros = r.viewOf ("macros");
        auto setPage = [&] (int id, bool shown)
        {
            const auto* bg = backgroundOf (layout::kViews[id].name);
            r.frame.setView (id, shown, 270);
            r.frame.setUiRect (bg->sentinelX, 110 + bg->sentinelY, 5, 5, bg->sentinelRgb, shown, 270);
        };
        auto finger = [&] (TouchEvent::Kind kind, int x, int y)
        {
            TouchEvent event;
            event.kind = kind; event.x = x; event.y = y;
            touch.events.push_back (event);
            r.run (20);
        };
        setPage (play, true);
        r.run (220);
        for (int source : { play, macros })
        {
            const int dest = source == play ? macros : play;
            const int writes = r.inst.uiWrites.load();
            std::array<int, 16> slots;
            std::copy_n (r.inst.qGroup, slots.size(), slots.begin());
            finger (TouchEvent::Down, 107, 770);
            check (! r.plane.planeOn && r.ctl->currentIndex() == source,
                   "host tab-key down exposes the current native page without predicting a destination");
            const auto* sourceBg = backgroundOf (layout::kViews[source].name);
            r.frame.setUiRect (sourceBg->sentinelX, 110 + sourceBg->sentinelY, 5, 5, sourceBg->sentinelRgb, false, 270);
            r.run (30);
            check (! r.plane.planeOn, "withdrawn source never redraws its opaque fallback between host artwork and marker changes");
            setPage (source, true);
            finger (TouchEvent::Up, 320, 710);
            check (r.plane.planeOn && r.ctl->currentIndex() == source && r.inst.uiWrites.load() == writes &&
                   std::equal (slots.begin(), slots.end(), r.inst.qGroup), "cancelled navigation restores the source and its Q slots");
            finger (TouchEvent::Down, 107, 770);
            finger (TouchEvent::Up, 107, 770);
            r.run (300);
            check (r.plane.planeOn && r.ctl->currentIndex() == source, "an ignored host key returns to the still-confirmed source after release");
            finger (TouchEvent::Down, 107, 770);
            r.run (750);
            check (r.plane.planeOn && r.ctl->currentIndex() == source, "a lost release cannot leave a native page permanently without its live layer");
            finger (TouchEvent::Up, 107, 770);
            check (! r.plane.planeOn, "a late release can still start a real host switch");
            setPage (source, false);
            setPage (dest, true);
            r.run (60);
            check (r.plane.planeOn && r.ctl->currentIndex() == dest && r.inst.uiWrites.load() == writes,
                   "fresh destination markers restore the requested live page in either direction");
        }
        const auto* playBg = backgroundOf ("play1");
        r.frame.setUiRect (playBg->sentinelX, 110 + playBg->sentinelY, 5, 5, playBg->sentinelRgb, false, 270);
        finger (TouchEvent::Down, 107, 770);
        check (r.plane.planeOn, "a fresh disappearance of the source sentinel keeps the opaque live page visible");
        finger (TouchEvent::Up, 107, 770);
        setPage (play, true);
        r.run (120);
        r.ctl->openFull (r.ctl->activeSlot(), OscUi::kElementOsc);
        r.run (40);
        finger (TouchEvent::Down, 107, 770);
        check (r.plane.planeOn && r.ctl->uiState (r.ctl->activeSlot()).level() == UiStack::Level::full,
               "the host key does not hide an open Play detail on a guessed navigation");
        finger (TouchEvent::Up, 107, 770);
        r.ctl->closeAll (r.ctl->activeSlot());
        r.run (60);
        finger (TouchEvent::Down, 107, 770);
        setPage (play, false);
        r.run (80);
        finger (TouchEvent::Up, 107, 770);
        r.run (320);
        check (! r.plane.planeOn && r.ctl->currentIndex() < 0, "an unknown host page stays unobstructed after the navigation timeout");
        setPage (macros, true);
        r.run (200);
        setPage (macros, false);
        setPage (play, true);
        r.run (200);
        check (r.plane.planeOn && r.ctl->currentIndex() == play, "ordinary detection replaces a view even without a navigation touch");
    }
    // ---- the sentinel state machine: opaque until the host's background is read, transparent after, reset by a page change, back to opaque when the sentinel is gone (host_background.h, controller.h)
    {
        HostBackgroundGate g;
        check (! g.step (-1, false, false) && ! g.step (3, true, false) && g.step (3, true, true) && g.on(), "gate: no sentinel -> opaque; sentinel read -> transparent");
        check (! g.step (3, true, false) && ! g.on(), "gate: the sentinel is gone -> opaque again at once");
        check (g.step (3, true, true) && ! g.step (4, true, false) && g.view() == 4 && ! g.on(), "gate: a page change resets (the new page has not shown its sentinel)");
        check (! g.step (4, false, true), "gate: a view without a background is never transparent, whatever is seen");
        check (g.step (5, true, true) && ! g.step (-1, true, true), "gate: no page on screen -> opaque");
        Rig r;
        const int mac = r.viewOf ("macros"), filter1 = r.viewOf ("filter1");
        const BackgroundTab* bg = backgroundOf ("macros");
        auto macros = [&] { return dynamic_cast<const pages::MacrosPage*> (r.ctl->viewByName ("macros")); };
        auto alphaAt = [&] (int x, int y) {
            std::vector<uint32_t> cv (1280u * 800u);
            const_cast<IView*> (r.ctl->viewByName ("macros"))->render (cv.data(), 1280, 800);
            return static_cast<int> (cv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] >> 24);
        };
        check (macros() != nullptr, "the controller has the Macros view");
        if (bg == nullptr)
        {
            // the manifest is EMPTY (the default: no host background in the build): the page is always opaque and has no sentinel to wait for
            r.frame.setView (mac, true, 90);
            r.run (800);
            Rect sr;
            uint32_t srgb;
            check (r.ctl->currentIndex() == mac && ! r.ctl->currentView()->hostBackgroundSentinel (sr, srgb) && ! macros()->ui().hostBackground() && alphaAt (322, 450) == 255, "empty manifest: the Macros page draws everything (opaque), no sentinel");
            r.frame.setUiRect (900, 110 + 20, 5, 5, 0x0b1025u, true, 90);   // whatever the host shows there
            r.run (400);
            check (! macros()->ui().hostBackground() && alphaAt (322, 450) == 255, "empty manifest: a colour on the spot of the shelved sentinel changes nothing");
        }
        else
        {
        r.frame.setView (mac, true, 90);
        r.run (800);
        check (r.ctl->currentIndex() == mac && ! macros()->ui().hostBackground() && alphaAt (322, 450) == 255, "sentinel absent: the Macros page is shown OPAQUE (all-in-one pass, ground drawn)");
        r.frame.setUiRect (bg->sentinelX, 110 + bg->sentinelY, 5, 5, 0x123456u, true, 90);   // a wrong colour on the spot
        r.run (400);
        check (! macros()->ui().hostBackground() && alphaAt (322, 450) == 255, "a sentinel of the wrong colour (the host's own picture of the tab) does not switch");
        r.frame.setUiRect (bg->sentinelX, 110 + bg->sentinelY, 5, 5, bg->sentinelRgb, true, 90);
        r.run (400);
        check (macros()->ui().hostBackground() && alphaAt (322, 450) == 0, "sentinel present: the live pass alone, a transparent plane over the host's background");
        {
            auto* macView = const_cast<pages::MacrosPage*> (macros());
            const auto dial = macView->ui().view().cards[0].nativeKnobArea();
            const int dialX = dial.x + dial.w / 2, dialY = dial.y + dial.h / 2;
            TouchEvent dialTap; dialTap.kind = TouchEvent::Down; dialTap.x = dialX; dialTap.y = dialY;
            const int writes = r.inst.uiWrites.load();
            auto macStack = r.ctl->uiState (r.ctl->slotOf (&r.inst));
            check (! macView->onTouch (dialTap, macStack, r.inst, r.now) && r.inst.uiWrites.load() == writes,
                   "sentinel present: native host owns macro dial touch without overlay consumption or writes");
        }
        r.run (1200);   // the status file is written once a second
        check (r.dir.read ("status.txt").find ("host_background: on") != std::string::npos, "the status file says the host background is on");
        r.frame.setUiRect (bg->sentinelX, 110 + bg->sentinelY, 5, 5, bg->sentinelRgb, false, 90);
        r.run (400);
        check (! macros()->ui().hostBackground() && alphaAt (322, 450) == 255, "the sentinel is gone: opaque again");
        r.frame.setUiRect (bg->sentinelX, 110 + bg->sentinelY, 5, 5, bg->sentinelRgb, true, 90);
        r.run (400);
        check (macros()->ui().hostBackground(), "the sentinel is back: transparent");
        r.frame.setView (mac, false, 90);
        r.frame.setView (filter1, true, 90);
        r.run (600);
        check (r.ctl->currentIndex() == filter1 && ! macros()->ui().hostBackground(), "a page change away: the Macros page is back to opaque (state reset)");
        r.frame.setView (filter1, false, 90);
        r.frame.setView (mac, true, 90);
        r.frame.setUiRect (bg->sentinelX, 110 + bg->sentinelY, 5, 5, bg->sentinelRgb, false, 90);
        r.run (800);
        check (r.ctl->currentIndex() == mac && ! macros()->ui().hostBackground() && alphaAt (322, 450) == 255, "coming back to Macros with the host's old picture (no sentinel yet): opaque until the sentinel is read");
        }
    }
    // ---- a finger that went down on our page and whose gesture the host takes over (the left drawer: the touch starts on the handle tab, the drawer slides over the markers, the Up never reaches the hidden page):
    // the page must not keep a gesture that nobody ends, else the controller asks for a picture on every tick (57 - 60 a second at rest, XL 2026-11-21) until another touch ends it
    {
        Rig r;
        const int mac = r.viewOf ("macros");
        r.frame.setView (mac, true, 90);
        r.run (800);
        check (r.ctl->currentIndex() == mac && r.ctl->currentView() != nullptr, "drawer: the Macros page is on screen");
        TouchEvent d; d.kind = TouchEvent::Down; d.x = 5; d.y = 110 + 300;   // the handle tab at the left edge
        r.ctl->touchEvent (d, r.now);
        check (r.ctl->currentView()->fingerDown(), "drawer: the touch went down on the page");
        r.frame.setView (mac, false, 90);   // the drawer covers the markers
        r.run (300);
        check (r.ctl->currentIndex() < 0, "drawer: the page is hidden while the drawer covers its markers");
        r.frame.setView (mac, true, 90);    // the drawer is closed again (the Up was the host's)
        r.run (800);
        check (r.ctl->currentIndex() == mac && ! r.ctl->currentView()->fingerDown(), "drawer: shown again with NO finger down (the hidden page dropped the gesture)");
        r.run (500);
        const size_t calls = r.plane.callLog().size();
        r.run (1000);
        size_t pictures = 0;
        for (size_t i = calls; i < r.plane.callLog().size(); ++i)
            pictures += r.plane.callLog()[i] != "verify";   // (the plane is verified once a second: not a picture)
        check (pictures == 0, "drawer: at rest after it no picture is made (" + std::to_string (pictures) + " plane calls besides the verify in 1 s)");
    }
    // ---- the controller opens the page of the element for the instance whose tab is on screen
    {
        Rig r;
        const int slot = r.ctl->slotOf (&r.inst);
        const int filter1 = r.viewOf ("filter1"), mixer = r.viewOf ("mixer"), osca = r.viewOf ("osca"), oscsub = r.viewOf ("oscsub"), play1 = r.viewOf ("play1"), env1 = r.viewOf ("env1");
        r.frame.setView (filter1, true, 90);
        r.run (600);
        check (r.ctl->currentIndex() == filter1 && ! r.ctl->uiState (slot).isMain() && r.ctl->uiState (slot).element() == FilterUi::kElementPage, "the Filter tab: the page of the filter element is open for the instance");
        check (! r.inst.uiFullOpen.load(), "... and the Filter root retains native widgets without a background sentinel");
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
        check (r.ctl->currentIndex() == osca && r.ctl->uiState (slot).element() == OscUi::kElementOsc && r.ctl->uiState (slot).depth() == 1 && ! r.inst.uiFullOpen.load(), "the Osc A root keeps its native selector and routing visible");
        {
            const auto* pg = dynamic_cast<const pages::OscPage*> (r.ctl->currentView());
            check (pg != nullptr && pg->ui().selection() == 2 && pg->ui().ui().selection() == 2 && r.inst.playSel[0] == 3, "the Osc A page shows oscillator A (selector number 2) whatever the Play selector says (B), and does not write it");
        }
        r.frame.setView (osca, false, 90);
        r.frame.setView (oscsub, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == oscsub && r.ctl->uiState (slot).element() == OscUi::kElementOsc && r.inst.playSel[0] == 3, "the Sub tab: the same page for the sub oscillator; the Play selector is still B");
        r.frame.setView (oscsub, false, 90);
        r.frame.setView (env1, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == env1 && r.ctl->uiState (slot).element() == EnvUi::kElementPage, "the Env tab: the page of the envelope element (ONE tab for the six envelopes)");
        // the Play page keeps its own way: main, and its elements open from it
        r.frame.setView (env1, false, 90);
        r.frame.setView (play1, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == play1 && r.ctl->uiState (slot).isMain() && ! r.inst.uiFullOpen.load(), "the Play tab: main (the compact views), not a page of an element and not frame 24");
        {
            const auto* playViewConst = dynamic_cast<const PlayView*> (r.ctl->viewByName ("play1"));
            auto* playView = const_cast<PlayView*> (playViewConst);
            auto playStack = r.ctl->uiState (slot);
            TouchEvent selectorTap;
            selectorTap.kind = TouchEvent::Down;
            selectorTap.x = 160;
            selectorTap.y = play::kTabY + play::kNativeSelectorY + play::kNativeSelectorH / 2;
            const int writes = r.inst.uiWrites.load();
            check (playView != nullptr && ! playView->onTouch (selectorTap, playStack, r.inst, r.now) && r.inst.uiWrites.load() == writes,
                   "the compact Play selector row remains owned by the native slider and the overlay does not write or consume it");
            if (playView != nullptr)
            {
                std::vector<uint32_t> pixels (1280u * 800u);
                playView->render (pixels.data(), 1280, 800);
                const auto alphaAt = [&] (int x, int y) {
                    return static_cast<int> (pixels[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] >> 24);
                };
                const int selectorY = play::kTabY + play::kNativeSelectorY + play::kNativeSelectorH / 2;
                check (alphaAt (160, selectorY) == 0 && alphaAt (160, play::kTabY + play::kNativeSelectorY - 4) != 0 &&
                       alphaAt (160, play::kTabY + play::kNativeSelectorY + play::kNativeSelectorH + 20) != 0 && alphaAt (160, 600) != 0,
                       "compact Play leaves a real header hole while retaining the graph and lower controls in one render pass");
            }
        }
        // a different page view of the same kind does not keep the detail pages of the other: going Env -> LFO is another view
        const int lfo1 = r.viewOf ("lfo1");
        r.frame.setView (play1, false, 90);
        r.frame.setView (env1, true, 90);
        r.run (300);
        r.ctl->openDetail (slot, 31);
        r.frame.setView (env1, false, 90);
        r.frame.setView (lfo1, true, 90);
        r.run (500);
        check (r.ctl->uiState (slot).depth() == 1 && r.ctl->uiState (slot).element() == LfoUi::kElementPage, "Env -> LFO: the page of the new tab opens clean (no detail page of the other)");
        // the status file names the page
        r.run (1200);
        const std::string st = r.dir.read ("status.txt");
        check (st.find ("page_view: lfo1") != std::string::npos && st.find ("page_instance: 1") != std::string::npos, "the status file names the page view and its instance");
    }
    // ---- the Q group: the router keeps what the LAST page set, so a page that comes back on screen sets its group again (the Q-Links must act on the page that is up)
    {
        Rig r;
        const int mixer = r.viewOf ("mixer"), filter1 = r.viewOf ("filter1");
        r.frame.setView (mixer, true, 90);
        r.run (600);
        const int writes = r.inst.uiWrites.load();
        check (r.inst.qGroup[0] == -1 && r.inst.qGroup[1] == r.inst.index ("osc1.level") && r.inst.uiWrites.load() == writes,
               "the Mixer page sets its Q group (sub powered off: unmapped, osc1 active) with zero writes");
        r.inst.set ("sub.on", 1.0f);
        r.run (200);
        const int mixerFirst = r.inst.qGroup[0];
        check (mixerFirst == r.inst.index ("sub.level") && mixerFirst >= 0 && r.inst.uiWrites.load() == writes,
               "when sub is powered on, the Mixer page sets its Q group to exact sub.level without writes");
        r.frame.setView (mixer, false, 90);
        r.frame.setView (filter1, true, 90);
        r.run (600);
        const int filterFirst = r.inst.qGroup[0];
        check (filterFirst >= 0 && filterFirst != mixerFirst, "the Filter page sets its own Q group (the cutoff)");
        r.frame.setView (filter1, false, 90);
        r.frame.setView (mixer, true, 90);
        r.run (600);
        check (r.inst.qGroup[0] == mixerFirst && r.inst.uiWrites.load() == writes, "back on the Mixer page: its Q group is set again, the Q-Links act on the page that is up (" + std::to_string (r.inst.qGroup[0]) + ")");
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
    // ---- arriving on the Play page from a page that has the full screen up: the host still shows the blank instance frame (the instance reports it while a full screen is open) and redraws with the slot colour only
    // after the instance is told the full screen is gone and has reported (the host's redraw takes 100 to 250 ms on the XL, more than one detection period). The Play page must not blink off in between (XL, 2026-11-21: 71 to 106 ms after the picture came, a ~230 ms blank)
    {
        Rig r;
        const int slot = r.ctl->slotOf (&r.inst);
        const int fx = r.viewOf ("fx"), play1 = r.viewOf ("play1");
        r.frame.setView (fx, true, 90);
        r.run (600);
        check (r.inst.uiFullOpen.load() && r.ctl->uiState (slot).element() == FxUi::kElementPage, "(Play arrival) the FX page is up and the instance reports the full screen frame");
        r.frame.setViewInstanceRgb (fx, layout::kInstanceBlankRgb, true, 90);
        r.run (100);
        r.frame.setView (fx, false, 90);
        r.frame.setView (play1, true, 90);
        r.frame.setViewInstanceRgb (play1, layout::kInstanceBlankRgb, true, 90);   // the host's picture still carries the old report
        bool shownDrawn = false, applied = true;
        double changeAt = -1.0;
        int hiddenTicks = 0;
        for (int i = 0; i < 80; ++i)   // 800 ms
        {
            const bool want = r.inst.uiFullOpen.load();   // true: blank frame; false: the slot colour
            if (want != applied)
            {
                if (changeAt < 0.0) changeAt = r.now;
                if (r.now - changeAt >= 200.0)
                {
                    r.frame.setViewInstanceRgb (play1, want ? layout::kInstanceBlankRgb : instanceFrameRgb (slot + 1), true, 90);
                    applied = want;
                    changeAt = -1.0;
                }
            }
            else
                changeAt = -1.0;
            r.run (10);
            if (r.ctl->currentIndex() == play1)
                shownDrawn = true;
            if (shownDrawn && r.ctl->currentIndex() != play1)
                ++hiddenTicks;
        }
        check (shownDrawn && hiddenTicks == 0 && r.ctl->currentIndex() == play1 && r.ctl->state() == "visible", "(Play arrival) no blank after the Play page came: hidden ticks " + std::to_string (hiddenTicks) + ", state " + r.ctl->state());
        check (! r.inst.uiFullOpen.load() && r.dir.read ("status.txt").find ("view_hides: 0") != std::string::npos, "(Play arrival) the stack closed by the group change told the instance (the host widgets come back), no view hide counted");
    }
    // ---- the Play/OSC views share one oscillator range, and Mixer/Filter share one filter range
    {
        std::vector<int> t;
        SynthyBoyPageFactory::targetsOfAllPages (t);
        auto expected = OscPageUi::readoutTargets();
        const auto filters = pages::filterTargets();
        expected.insert (expected.end(), filters.begin(), filters.end());
        check (t == expected && t.size() == 16, "OSC and Play subscribe to the same 12 readouts once, and Mixer and Filter share four more");
    }
    // The Mixer factory requests both Filter readouts. Its modal survives a nested
    // Back, consumes a backdrop drag, and closes on a separate backdrop tap.
    {
        FakeInstance inst;
        inst.set ("filter2.on", 1.0f);   // this test opens the model selector of an enabled filter
        UiStack stack;
        stack.openFull (MixerUi::kElementPage);
        auto view = pages::make ("mixer", layout::kKindMixer, 1);
        auto& pv = static_cast<pages::MixerPage&> (*view);
        std::vector<int> indices;
        pv.targets (indices);
        check (indices == pages::filterTargets(), "the Mixer requests both filters' live cutoff and resonance in Filter page order");
        const float values[] = { 340.0f, 5.0f, 2700.0f, 31.0f };
        ViewInput in;
        in.instance = &inst; in.io = &inst; in.ui = &stack; in.targets = values; in.nowMs = 1000.0;
        auto refresh = [&] { in.nowMs += 200; pv.update (in); };
        auto touch = [&] (TouchEvent::Kind kind, int x, int y) { return pv.onTouch ({ kind, x, y }, stack, inst, in.nowMs); };
        auto tap = [&] (synthyboy_mpc::ui::Rect r)
        {
            touch (TouchEvent::Down, r.x + r.w / 2, r.y + r.h / 2);
            touch (TouchEvent::Up, r.x + r.w / 2, r.y + r.h / 2);
            refresh();
        };
        refresh();
        check (! pv.ui().hostBackground() && ! pv.ui().nativeBase(),
               "Mixer marker absent initially: host background and native base inactive");
        pv.setHostBackground (true);
        refresh();
        check (pv.ui().hostBackground() && pv.ui().nativeBase(),
               "Mixer marker present: host background and native base active");
        pv.setHostBackground (false);
        refresh();
        check (! pv.ui().hostBackground() && ! pv.ui().nativeBase(),
               "Mixer marker absent: host background and native base inactive");
        const auto writes = inst.uiWrites.load();
        tap (pv.ui().screen().routingBar.peekRect (1));
        auto* filter = static_cast<synthyboy_mpc::ui::FilterFullScreen*> (pv.ui().peekRoot());
        check (filter && filter->display.state.liveCutoffHz == values[2] && filter->display.state.liveResoPct == values[3],
               "Mixer F2 shows the modulated F2 readout");
        if (filter)
        {
            tap (filter->model.rect());
            const int* slots = pv.ui().qTargets();
            const int* end = slots + pv.ui().qSlotCount();
            check (pv.ui().topPage() == MixerUi::kPagePeek && std::find (slots, end, inst.index ("filter2.cutoff")) != end
                   && std::find (slots, end, inst.index ("filter2.model")) != end,
                   "a native model aperture never revives a GPU model list; Filter Q targets remain owned by the panel");
            const int y = MixerUi::kTabY + 2;
            touch (TouchEvent::Down, 20, y);
            touch (TouchEvent::Move, 95, y);
            touch (TouchEvent::Up, 95, y);
            refresh();
            check (pv.ui().topPage() == MixerUi::kPagePeek, "a backdrop drag leaves the panel open");
            touch (TouchEvent::Down, 20, y);
            touch (TouchEvent::Up, 20, y);
            refresh();
            check (pv.ui().topPage() == 0 && pv.ui().qTargets()[0] == -1 && pv.ui().qTargets()[1] == inst.index ("osc1.level") && inst.uiWrites.load() == writes,
                   "a backdrop tap dismisses only the panel and restores Mixer Q targets (sub powered off: unmapped, osc1 active) without writes");
            inst.set ("sub.on", 1.0f);
            refresh();
            check (pv.ui().topPage() == 0 && pv.ui().qTargets()[0] == inst.index ("sub.level") && inst.uiWrites.load() == writes,
                   "sub powered on restores exact sub.level Q target without writes");
            pv.setHostBackground (true);
            refresh();
            check (pv.ui().hostBackground() && pv.ui().nativeBase(),
                   "Mixer with panel closed and marker present: native base active");
            pv.setHostBackground (false);
            refresh();
            check (! pv.ui().hostBackground() && ! pv.ui().nativeBase(),
                   "Mixer marker absent: native base inactive");
        }
        synthyboy_mpc::ui::MixerRoutingBar bar;
        const auto min = bar.minSize();
        bar.place ({ 0, 0, min.w, min.h });
        check (! bar.peekRect (0).overlaps (bar.peekRect (1)) && ! bar.peekRect (1).overlaps (bar.peekRect (2)) && bar.peekRect (0).w >= 56,
               "at the routing bar minimum width its three touch targets remain disjoint and at least 56 px");
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
    testNativeFilterPage();

    {
        auto scene = std::make_unique<gpu::SceneData>();
        auto captionHash = [&] (const char* caption) {
            gpu::SceneBuilder builder (*scene);
            builder.text (10, 20, 16, caption, 0xffffffffu);
            return page::sceneHash (*scene);
        };
        const auto oscA = captionHash ("OSC A");
        check (captionHash ("OSC B") != oscA && captionHash ("OSC A") == oscA,
               "same-length caption changes repaint an uncached label and restoring it restores the hash");
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
