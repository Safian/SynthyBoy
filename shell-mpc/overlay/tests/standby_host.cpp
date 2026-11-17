// SynthyBoy - MPC overlay tests: the host's standby dim (standby.h). A fake framebuffer and a fake backlight directory: awake -> dim -> awake with every ordering of the two signals (the host's picture and the
// backlight) inside the hold window, the Q-Link pop-up that is up while the host dims, a host page (nothing to keep), a page that really goes away while dimmed, no backlight node, the first touch while
// dimmed, and the drawn picture = the awake picture times the factor. Never a frame in which our picture is missing at a dim or a wake.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <sys/stat.h>

#include "fakes.h"
#include "fixtures/legacy_layout.h"
#include "../scene_cpu.h"
#include "../standby.h"

using namespace ovt;

namespace
{
constexpr double kCover = 0.55;   // the share of a marker's contrast that is left after the host's 8 px blur (6 px square: about half in the middle)

struct FakeTouch final : ITouchInput
{
    std::vector<TouchEvent> queue;
    void configure (int, int, int) override {}
    int poll (const std::function<void (const TouchEvent&)>& h) override { for (const TouchEvent& e : queue) h (e); const int n = static_cast<int> (queue.size()); queue.clear(); return n; }
};

struct Rig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    FakeTouch touch;
    std::unique_ptr<Controller> ctl;
    double now = 0.0;
    int missing = 0;       // ticks at which the page had been shown and was not (a frame without our picture)
    bool watching = false;
    int watchView = -1;

    explicit Rig (bool withBacklight = true, int bl = 227)
    {
        frame.views = legacy::kViews;
        ctl = std::make_unique<Controller> (plane, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, &touch, legacy::kViews, legacy::kNumViews);
        inst.set ("filter1.cutoff", 1000.0f);
        inst.set ("filter1.resonance", 30.0f);
        ctl->attach (&inst);
        if (withBacklight)
            setBl (bl);
    }
    void setBl (int v)
    {
        ::mkdir ((dir.path + "/backlight").c_str(), 0755);
        ::mkdir ((dir.path + "/backlight/mipi-backlight").c_str(), 0755);
        dir.write ("backlight/mipi-backlight/brightness", std::to_string (v) + "\n");
    }
    void run (double ms, double step = 10.0)
    {
        const double end = now + ms;
        while (now < end)
        {
            now += step;
            ctl->tick (now);
            if (watching && ! (ctl->currentIndex() == watchView && plane.planeOn))
                ++missing;
        }
    }
    bool on() const { return plane.planeOn; }
    std::string status() { return dir.read ("status.txt"); }
};

std::string line (const std::string& text, const std::string& key)
{
    const size_t p = text.find (key + ": ");
    if (p == std::string::npos) return "";
    const size_t e = text.find ('\n', p);
    return text.substr (p + key.size() + 2, e - p - key.size() - 2);
}

uint32_t dimmed (uint32_t rgb, double t, double f = 0.5)
{
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8)
    {
        const double bg = static_cast<double> ((0x1e1d1eu >> sh) & 255u), m = static_cast<double> ((rgb >> sh) & 255u);
        out |= static_cast<uint32_t> (std::floor (f * (bg + t * (m - bg)) + 0.5)) << sh;
    }
    return out;
}

/** The host's dimmed picture of a page: both markers at the dim factor and the blur's coverage. */
void dimView (Rig& r, int view, double t = kCover)
{
    for (int m = 0; m < 2; ++m)
        r.frame.setViewMarker (view, m, true, 90, dimmed (legacy::kViews[view].markers[m].rgb, t));
    r.frame.setViewInstanceRgb (view, 0x0f0e0fu, true, 90);
}
/** A host page at half brightness: nothing of ours (the markers' places are the dimmed background). */
void dimHostPage (Rig& r, int view)
{
    for (int m = 0; m < 2; ++m)
        r.frame.setViewMarker (view, m, true, 90, 0x0f0e0fu);
    r.frame.setViewInstanceRgb (view, 0x0f0e0fu, true, 90);
}
void awakeView (Rig& r, int view) { r.frame.setView (view, true, 90); }

void startAwake (Rig& r, int view)
{
    awakeView (r, view);
    r.run (2200);
    r.watching = true;
    r.watchView = view;
    r.missing = 0;
}
} // namespace

int main()
{
    const int filter1 = 0, play1 = 14;
    // ---- the backlight node is found generically ----
    {
        TempDir d;
        standby::Backlight none (d.path + "/nothing");
        int v = 0;
        check (! none.read (0.0, v) && none.node().empty(), "no backlight directory: no node");
        ::mkdir ((d.path + "/bl").c_str(), 0755);
        ::mkdir ((d.path + "/bl/zz_nothing_here").c_str(), 0755);
        ::mkdir ((d.path + "/bl/amdgpu_bl0").c_str(), 0755);
        d.write ("bl/amdgpu_bl0/brightness", "180\n");
        standby::Backlight b (d.path + "/bl");
        check (b.read (0.0, v) && v == 180 && b.node() == "amdgpu_bl0", "any driver name: the first node with a readable brightness is taken (" + b.node() + " = " + std::to_string (v) + ")");
        d.write ("bl/amdgpu_bl0/brightness", "90\n");
        check (b.read (10.0, v) && v == 90, "the value is read afresh");
        std::remove ((d.path + "/bl/amdgpu_bl0/brightness").c_str());
        check (! b.read (20.0, v) && b.node().empty(), "the node went away: no value, no node");
    }
    // ---- the dimmed look of the markers ----
    {
        Rig r (false);
        MarkerSpec marks[2];
        for (int m = 0; m < 2; ++m)
            marks[m] = { { legacy::kViews[filter1].markers[m].rect.x, legacy::kViews[filter1].markers[m].rect.y, legacy::kViews[filter1].markers[m].rect.w, legacy::kViews[filter1].markers[m].rect.h }, legacy::kViews[filter1].markers[m].rgb };
        PageDetector det (r.frame, marks, 1);
        for (double t : { 0.25, 0.4, 0.55, 0.8, 1.0 })
        {
            dimView (r, filter1, t);
            check (det.dimLook (0, 90, 0.5, false), "the markers at half brightness with " + std::to_string (static_cast<int> (t * 100)) + " % of their contrast left by the blur: recognised");
        }
        awakeView (r, filter1);
        check (! det.dimLook (0, 90, 0.5, false), "the awake markers are no dimmed look");
        dimHostPage (r, filter1);
        check (! det.dimLook (0, 90, 0.5, false), "a host page at half brightness (the dimmed background where the markers would be): no");
        dimView (r, filter1);
        r.frame.setViewMarker (filter1, 1, true, 90, dimmed (0x3a7264, kCover));
        check (! det.dimLook (0, 90, 0.5, false), "the right marker of a view with another hue (the dimmed colour of 3a7264): no");
        r.frame.setViewMarker (filter1, 1, true, 90, dimmed (0x3a1764, kCover));
        check (det.dimLook (0, 90, 0.5, false), "(the colours of the neighbour views differ by 7 levels: at half brightness and blurred the identity is the held view's, not read from the colour)");
        for (double f : { 0.45, 0.55 })
        {
            for (int m = 0; m < 2; ++m)
                r.frame.setViewMarker (filter1, m, true, 90, dimmed (legacy::kViews[filter1].markers[m].rgb, kCover, f));
            check (det.dimLook (0, 90, 0.5, false), "a host that dims to " + std::to_string (static_cast<int> (f * 100)) + " % (not exactly half): still recognised with the factor 0.5");
        }
    }
    // ---- the state machine alone: every ordering of the two signals ----
    {
        using standby::Tracker;
        const double hold = standby::kHoldMs;
        auto run = [&] (const char* what, bool pictureFirst, double skew, bool dimming)
        {
            Tracker t;
            double now = 0.0;
            int hidden = 0;
            auto tick = [&] (bool awake, bool look, int bl)
            {
                now += 10.0;
                const standby::Verdict v = t.update (now, awake, look, true, true, bl);
                if (! awake && ! v.hold)
                    ++hidden;
            };
            for (int i = 0; i < 300; ++i) tick (true, false, 227);   // awake for 3 s
            if (dimming)
            {
                // t = 0: the first signal; t = skew: the second
                const double t0 = now;
                bool pic = false, bk = false;
                while (now - t0 < skew + 500.0)
                {
                    if (pictureFirst) { pic = now - t0 >= 0; bk = now - t0 >= skew; }
                    else { bk = now - t0 >= 0; pic = now - t0 >= skew; }
                    tick (! pic, pic, bk ? 106 : 227);
                }
                check (hidden == 0 && t.dimmed() && t.dims() == 1 && t.hiddenTicks() == 0, std::string (what) + ": dim, the picture " + (pictureFirst ? "first" : "second") + ", skew " + std::to_string (static_cast<int> (skew)) + " ms: held the whole time, dimmed, 1 transition, 0 hidden");
            }
            else
            {
                // first reach the dimmed state
                for (int i = 0; i < 100; ++i) tick (false, true, 106);
                const double t0 = now;
                bool pic = false, bk = false;   // pic: the picture is awake again
                while (now - t0 < skew + 500.0)
                {
                    if (pictureFirst) { pic = now - t0 >= 0; bk = now - t0 >= skew; }
                    else { bk = now - t0 >= 0; pic = now - t0 >= skew; }
                    tick (pic, ! pic, bk ? 227 : 106);
                }
                check (hidden == 0 && ! t.dimmed() && t.wakes() == 1 && t.hiddenTicks() == 0, std::string (what) + ": wake, the picture " + (pictureFirst ? "first" : "second") + ", skew " + std::to_string (static_cast<int> (skew)) + " ms: held the whole time, awake, 1 wake, 0 hidden");
            }
        };
        for (bool dimming : { true, false })
            for (bool pictureFirst : { true, false })
                for (double skew : { 0.0, 20.0, 50.0, 100.0, hold - 20.0 })
                    run (dimming ? "tracker" : "tracker", pictureFirst, skew, dimming);
        // the skew longer than the bound: the page goes, and the missing frames are counted
        {
            Tracker t;
            double now = 0.0;
            for (int i = 0; i < 300; ++i) { now += 10.0; t.update (now, true, false, true, true, 227); }
            int hidden = 0;
            for (int i = 0; i < 100; ++i)
            {
                now += 10.0;
                const standby::Verdict v = t.update (now, false, true, true, true, 227);   // the picture dim, the backlight never follows
                if (! v.hold) ++hidden;
            }
            check (hidden > 0 && t.hiddenTicks() > 0 && ! t.dimmed(), "the picture dim and the backlight never follows (1 s): the page goes after the bound, the frames are counted (" + std::to_string (t.hiddenTicks()) + ")");
        }
        // the backlight is low but the markers are neither: the host left the page
        {
            Tracker t;
            double now = 0.0;
            for (int i = 0; i < 300; ++i) { now += 10.0; t.update (now, true, false, true, true, 227); }
            now += 10.0;
            const standby::Verdict v = t.update (now, false, false, true, true, 106);
            check (! v.hold && t.hiddenTicks() == 0, "the backlight low and neither markers: the page is gone at once (as without the dim), not counted as a transition failure");
        }
        // no node: the dimmed markers alone decide
        {
            Tracker t;
            double now = 0.0;
            for (int i = 0; i < 100; ++i) { now += 10.0; t.update (now, true, false, true, false, 0); }
            now += 10.0;
            const standby::Verdict v = t.update (now, false, true, true, false, 0);
            check (v.hold && v.dimmed && t.dimmed() && t.dims() == 1, "no backlight node: the half brightness markers alone decide");
        }
        // the user changes the brightness while awake: the new value is the reference (the wake step must not become it)
        {
            Tracker t;
            double now = 0.0;
            for (int i = 0; i < 300; ++i) { now += 10.0; t.update (now, true, false, true, true, 227); }
            for (int i = 0; i < 300; ++i) { now += 10.0; t.update (now, true, false, true, true, 150); }
            check (t.awakeBacklight() == 150, "the brightness set by the user while awake becomes the awake reference");
            for (int i = 0; i < 20; ++i) { now += 10.0; t.update (now, false, true, true, true, 60); }
            check (t.dimmed(), "... and a drop from it (150 -> 60) is a dim");
        }
        // wake: awake picture with the backlight still low for 150 ms does not become the reference
        {
            Tracker t;
            double now = 0.0;
            for (int i = 0; i < 300; ++i) { now += 10.0; t.update (now, true, false, true, true, 227); }
            for (int i = 0; i < 100; ++i) { now += 10.0; t.update (now, false, true, true, true, 106); }
            for (int i = 0; i < 15; ++i) { now += 10.0; t.update (now, true, false, true, true, 106); }
            check (t.awakeBacklight() == 227, "the awake picture with the backlight still low (the wake order): the reference stays 227");
        }
    }
    // ---- the controller, with the fake framebuffer and backlight: every ordering inside the hold window ----
    for (bool dimming : { true, false })
        for (bool pictureFirst : { true, false })
            for (int skew : { 0, 20, 50, 100, 250 })
            {
                Rig r;
                startAwake (r, filter1);
                const std::string tag = std::string (dimming ? "dim" : "wake") + ", " + (pictureFirst ? "picture" : "backlight") + " first, " + std::to_string (skew) + " ms apart: ";
                if (! dimming)
                {
                    dimView (r, filter1);
                    r.setBl (106);
                    r.run (800);
                    if (! (r.ctl->currentIndex() == filter1 && r.on())) { check (false, tag + "(could not reach the dimmed state)"); continue; }
                    r.missing = 0;
                }
                const int disables = r.plane.countOf ("disable");
                auto picture = [&] { if (dimming) dimView (r, filter1); else awakeView (r, filter1); };
                auto backlight = [&] { r.setBl (dimming ? 106 : 227); };
                if (pictureFirst) picture(); else backlight();
                r.run (skew);
                if (pictureFirst) backlight(); else picture();
                r.run (800);
                const std::string st = r.status();
                const bool stateOk = dimming ? (line (st, "dimmed") == "yes" && line (st, "dim_transitions") == "1" && r.ctl->currentIndex() == filter1)
                                             : (line (st, "dimmed") == "no" && line (st, "wake_transitions") == "1" && line (st, "dim_transitions") == "1" && r.ctl->currentIndex() == filter1);
                check (r.missing == 0 && r.plane.countOf ("disable") == disables && stateOk && line (st, "dim_hidden_frames") == "0",
                       tag + "our picture never missing (" + std::to_string (r.missing) + " ticks), the plane never switched off, dimmed: " + line (st, "dimmed") + ", hidden frames " + line (st, "dim_hidden_frames"));
            }
    // ---- the drawn picture is the awake picture times the factor ----
    {
        Rig r;
        startAwake (r, filter1);
        const std::vector<uint32_t> awake = r.plane.shownCopy;
        dimView (r, filter1);
        r.setBl (106);
        r.run (800);
        const std::vector<uint32_t>& dim = r.plane.shownCopy;
        size_t opaque = 0, bad = 0;
        int maxOff = 0;
        for (size_t i = 0; i < awake.size() && i < dim.size(); ++i)
        {
            if ((awake[i] >> 24) != (dim[i] >> 24)) { ++bad; continue; }
            if ((awake[i] >> 24) == 0) continue;
            ++opaque;
            for (int sh = 0; sh <= 16; sh += 8)
            {
                const int a = static_cast<int> ((awake[i] >> sh) & 255u), d = static_cast<int> ((dim[i] >> sh) & 255u);
                const int off = std::abs (d - (a + 1) / 2);
                maxOff = std::max (maxOff, off);
            }
        }
        check (opaque > 10000 && bad == 0 && maxOff <= 1, "the picture on the plane while dimmed = the awake picture times 0.5 (" + std::to_string (opaque) + " opaque pixels, alpha identical, largest difference " + std::to_string (maxOff) + ")");
        check (r.plane.presented > 0 && line (r.status(), "dimmed") == "yes", "(dimmed)");
        // and back: the same picture as before the dim
        awakeView (r, filter1);
        r.run (800);
        check (r.plane.shownCopy == awake, "awake again: the picture is the awake picture, bit for bit");
        // the GPU path: a scene darkened = the CPU picture of the awake scene darkened
        gpu::SceneData a {}, b {};
        for (gpu::SceneData* s : { &a, &b })
        {
            gpu::SceneBuilder sb (*s);
            sb.begin (gpu::kKindUi, 0, 200, 100);
            sb.clear (0u);
            sb.rect (10, 10, 190, 90, 0xc83c3cffu);
            sb.gradientRect (20, 20, 120, 80, 0x3cc8c8ffu, 0x1e1e96ffu);
            sb.disc (150, 50, 20, 0xffffff80u);
            sb.rrect (30, 30, 90, 70, 8, 0x40ff40c0u);
            sb.line (5, 5, 195, 95, 3, 0xffff00ffu);
        }
        standby::dimScene (b, 0.5);
        std::vector<uint32_t> pa, pb;
        renderScene (a, pa);
        renderScene (b, pb);
        standby::dimCanvas (pa.data(), pa.size(), 0.5);
        int worst = 0;
        for (size_t i = 0; i < pa.size() && i < pb.size(); ++i)
            for (int sh = 0; sh <= 24; sh += 8)
                worst = std::max (worst, std::abs (static_cast<int> ((pa[i] >> sh) & 255u) - static_cast<int> ((pb[i] >> sh) & 255u)));
        check (! pa.empty() && pa.size() == pb.size() && worst <= 3, "a GPU scene with its colours scaled draws the awake canvas times 0.5 (largest channel difference " + std::to_string (worst) + ", gradient, alpha and rounded shapes included)");
        // a cell's hash changes with the dim so the helper draws the cells again
        gpu::SceneData c {};
        {
            gpu::SceneBuilder sb (c);
            sb.begin (gpu::kKindUi, 0, 100, 100);
            sb.cellBegin (0, 0, 50, 50);
            sb.rect (0, 0, 50, 50, 0xff0000ffu);
            sb.cellEnd();
        }
        gpu::SceneData c2 = c;
        standby::dimScene (c2, 0.5);
        uint32_t h1 = 0, h2 = 0;
        for (uint32_t i = 0; i < c.numOps; ++i) if (c.ops[i].op == gpu::kOpCellBegin) { h1 = c.ops[i].rgba; h2 = c2.ops[i].rgba; }
        check (h1 != 0 && h1 != h2, "a cell's hash is another one while dimmed (the helper draws the cells again at the dim and at the wake)");
    }
    // ---- dim on a host page: nothing to keep ----
    {
        Rig r;
        r.frame.setAll (false, 90);
        r.run (800);
        const int shown = r.plane.presented;
        dimHostPage (r, filter1);
        r.setBl (106);
        r.run (1000);
        const std::string st = r.status();
        check (! r.on() && r.plane.presented == shown && line (st, "dimmed") == "no" && line (st, "dim_transitions") == "0", "dim on a host page: nothing of ours appears, no transition counted");
        r.frame.setView (filter1, false, 90);
        r.setBl (227);
        r.run (500);
        check (! r.on(), "wake on a host page: still nothing of ours");
    }
    // ---- a page that really goes away while dimmed ----
    {
        Rig r;
        startAwake (r, filter1);
        dimView (r, filter1);
        r.setBl (106);
        r.run (1000);
        check (r.on() && line (r.status(), "dimmed") == "yes", "(dimmed on the page)");
        dimHostPage (r, filter1);   // the host shows something else, still at half brightness
        r.watching = false;
        r.run (700);
        const std::string st = r.status();
        check (! r.on() && line (st, "dimmed") == "no" && line (st, "dim_hidden_frames") == "0", "the host leaves the page while dimmed: neither markers match, the page is gone (within the bound), not a transition failure");
        awakeView (r, filter1);
        r.setBl (227);
        r.run (700);
        check (r.on() && r.ctl->currentIndex() == filter1, "the page comes back awake: shown again");
    }
    // ---- no backlight node: the dimmed markers alone ----
    {
        Rig r (false);
        startAwake (r, filter1);
        const int disables = r.plane.countOf ("disable");
        dimView (r, filter1);
        r.run (800);
        std::string st = r.status();
        check (r.missing == 0 && r.plane.countOf ("disable") == disables && line (st, "dimmed") == "yes" && line (st, "backlight_node") == "none", "no backlight node: the dimmed markers alone keep the page, dimmed (node: " + line (st, "backlight_node") + ")");
        awakeView (r, filter1);
        r.run (500);
        st = r.status();
        check (r.missing == 0 && line (st, "dimmed") == "no" && line (st, "wake_transitions") == "1", "... and the awake markers bring it back, no frame missing");
    }
    // ---- the status file ----
    {
        Rig r;
        startAwake (r, filter1);
        std::string st = r.status();
        check (line (st, "backlight_node") == "mipi-backlight" && line (st, "backlight_value") == "227" && line (st, "backlight_awake_reference") == "227" && line (st, "dimmed") == "no", "status: node mipi-backlight, value 227, reference 227, dimmed no");
        dimView (r, filter1);
        r.setBl (106);
        r.run (1200);
        st = r.status();
        check (line (st, "backlight_value") == "106" && line (st, "dimmed") == "yes" && line (st, "dim_transitions") == "1" && line (st, "wake_transitions") == "0" && line (st, "dim_hidden_frames") == "0", "status while dimmed: value 106, dimmed yes, 1 dim, 0 wakes, 0 hidden");
    }
    // ---- the Q-Link pop-up is up while the host dims ----
    {
        Rig r;
        auto band = [&] (uint32_t lineRgb, uint32_t fill, int left = 1024)
        {
            for (int y = 112; y < 740; ++y)
                for (int x = left; x < 1280; ++x)
                {
                    int px, py;
                    uiPointToPanel (90, x, y, 800, 1280, px, py);
                    r.frame.setPixel (px, py, x < left + 2 ? lineRgb : fill);
                }
        };
        awakeView (r, filter1);
        r.run (2200);
        r.watching = true;
        r.watchView = filter1;
        for (int left : { 1250, 1150, 1050, 1024 })   // the pop-up slides in
        {
            band (0x000000u, 0x1e1d1eu, left);
            r.run (40);
        }
        r.run (600);
        check (r.on() && r.ctl->keyEdge() < PanelKey::kNoKey, "(the pop-up is up: the picture is cut at " + std::to_string (r.ctl->keyEdge()) + ")");
        const int edge = r.ctl->keyEdge();
        r.missing = 0;
        dimView (r, filter1);
        band (0x000000u, 0x0f0e0fu);
        r.frame.setViewMarker (filter1, 1, true, 90, 0x0f0e0fu);   // the right marker lies under the pop-up
        r.setBl (106);
        r.run (1500);
        const std::string st = r.status();
        check (r.missing == 0 && line (st, "dimmed") == "yes" && r.ctl->keyEdge() == edge, "dim with the Q-Link Status pop-up up: the page stays, dimmed, the cut stays at " + std::to_string (r.ctl->keyEdge()) + " (was " + std::to_string (edge) + "), no frame missing");
        // the host wakes: awake picture with the pop-up still there
        awakeView (r, filter1);
        band (0x000000u, 0x1e1d1eu);
        r.setBl (227);
        r.run (800);
        check (r.missing == 0 && line (r.status(), "dimmed") == "no" && r.ctl->keyEdge() < PanelKey::kNoKey, "wake with the pop-up still up: the page stays, awake, still cut, no frame missing");
    }
    // ---- the first touch while dimmed does not reach a control; the next one does ----
    {
        Rig r;
        r.inst.playSel[0].store (2);
        awakeView (r, play1);
        r.run (2200);
        auto* pv = static_cast<PlayView*> (const_cast<IView*> (r.ctl->viewByName ("play1")));
        check (pv != nullptr && pv->oscUi() != nullptr && r.ctl->currentIndex() == play1, "(the Play page is on screen)");
        const synthyboy_mpc::ui::Rect fb = pv->oscUi()->compact().full.rect();
        const int x = fb.x + fb.w / 2, y = fb.y + fb.h / 2;
        const int slot = r.ctl->slotOf (&r.inst);
        auto touchAt = [&] (TouchEvent::Kind k) { TouchEvent e; e.kind = k; e.x = x; e.y = y; e.timeUs = 1; r.touch.queue.push_back (e); };
        dimView (r, play1);
        r.setBl (106);
        r.run (800);
        check (line (r.status(), "dimmed") == "yes" && r.ctl->currentIndex() == play1, "(dimmed on the Play page)");
        const unsigned long taken = r.ctl->touchTaken();
        // a tap on the full-screen button: it wakes the host and nothing else. The host's picture comes back while the finger is still down
        touchAt (TouchEvent::Down);
        r.run (40);
        awakeView (r, play1);
        r.setBl (227);
        r.run (300);                  // awake again, the finger still down
        touchAt (TouchEvent::Move);
        r.run (40);
        touchAt (TouchEvent::Up);
        r.run (300);
        check (line (r.status(), "dimmed") == "no" && r.ctl->touchTaken() == taken && r.ctl->uiState (slot).isMain() && ! r.inst.uiFullOpen.load(), "the first touch (down while dimmed, up after the wake) woke the host and changed nothing of ours");
        r.run (1100);
        check (std::stoi (line (r.status(), "dim_touch_swallowed")) == 3, "... counted: " + line (r.status(), "dim_touch_swallowed") + " events swallowed");
        touchAt (TouchEvent::Down);
        r.run (60);
        touchAt (TouchEvent::Up);
        r.run (300);
        check (r.ctl->touchTaken() > taken && r.ctl->uiState (slot).level() == UiStack::Level::full, "the next tap opens the full screen: the controls work again");
    }
    // ---- cost of the darkening, and of the blur the host also applies (decides whether a blur is worth it) ----
    {
        const int w = 1280, h = 628;
        std::vector<uint32_t> cv (static_cast<size_t> (w) * h, 0xff1e1d1eu), tmp (cv.size());
        for (size_t i = 0; i < cv.size(); i += 37) cv[i] = 0xffc83c3cu;
        using Clock = std::chrono::steady_clock;
        const int n = 20;
        auto t0 = Clock::now();
        for (int i = 0; i < n; ++i) standby::dimCanvas (cv.data(), cv.size(), 0.5);
        const double dimMs = std::chrono::duration<double, std::milli> (Clock::now() - t0).count() / n;
        // a separable box blur of 8 px (running sums), two passes: what the host does to its picture
        auto blur = [&] (std::vector<uint32_t>& px)
        {
            const int r = 4;
            for (int pass = 0; pass < 2; ++pass)
            {
                const bool horiz = pass == 0;
                const int len = horiz ? w : h, lines = horiz ? h : w;
                for (int l = 0; l < lines; ++l)
                {
                    int s[3] = { 0, 0, 0 };
                    auto at = [&] (int i) -> uint32_t { i = std::min (std::max (i, 0), len - 1); return horiz ? px[static_cast<size_t> (l) * w + i] : px[static_cast<size_t> (i) * w + l]; };
                    for (int i = -r; i < r; ++i) { const uint32_t p = at (i); s[0] += (p >> 16) & 255; s[1] += (p >> 8) & 255; s[2] += p & 255; }
                    for (int i = 0; i < len; ++i)
                    {
                        const uint32_t add = at (i + r), sub = at (i - r);
                        s[0] += ((add >> 16) & 255) - ((sub >> 16) & 255); s[1] += ((add >> 8) & 255) - ((sub >> 8) & 255); s[2] += (add & 255) - (sub & 255);
                        const uint32_t out = 0xff000000u | (static_cast<uint32_t> (s[0] / 8) << 16) | (static_cast<uint32_t> (s[1] / 8) << 8) | static_cast<uint32_t> (s[2] / 8);
                        if (horiz) tmp[static_cast<size_t> (l) * w + i] = out; else px[static_cast<size_t> (i) * w + l] = out;
                    }
                }
                if (horiz) px = tmp;
            }
        };
        t0 = Clock::now();
        for (int i = 0; i < n; ++i) blur (cv);
        const double blurMs = std::chrono::duration<double, std::milli> (Clock::now() - t0).count() / n;
        std::printf ("INFO  1280x628 canvas on this host: darken %.2f ms, separable 8 px box blur %.2f ms (per picture)\n", dimMs, blurMs);
        check (dimMs < 20.0, "the darkening of a whole canvas is cheap (" + std::to_string (dimMs) + " ms)");
    }
    std::printf (g_fails == 0 ? "standby_host: OK\n" : "standby_host: %d FAILED\n", g_fails);
    return g_fails == 0 ? 0 : 1;
}
