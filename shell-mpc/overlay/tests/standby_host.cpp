// SynthyBoy - MPC overlay tests: the host's standby dim (standby.h). A fake framebuffer and a fake backlight directory: awake -> dim -> awake with every ordering of the two signals (the host's picture and the
// backlight) inside the hold window, the Q-Link pop-up that is up while the host dims, a host page (nothing to keep), a page that really goes away while dimmed, no backlight node, the first touch while
// dimmed, and the drawn picture = the awake picture times the factor. Never a frame in which our picture is missing at a dim or a wake.
// The XL (gen 2) only blurs its picture while dimmed (no backlight change, no darkening): the blurred-picture evidence (standby.h blurredPicture / measureFactor) is tested against the values measured
// on the XL (the section "the XL's blur"), against synthetic pictures made by an independent supersampled blur of every view's markers, and end to end through the controller (dim, touch, wake, page change).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <set>
#include <sys/stat.h>

#include "fakes.h"
#include "fixtures/legacy_layout.h"
#include "../scene_cpu.h"
#include "../standby.h"

using namespace ovt;

namespace
{
constexpr double kCover = 0.55;   // the share of a marker's contrast that is left after the host's 8 px blur (6 px square: about half in the middle)

// ---- the XL's blur: pictures made by blurring the awake markers (independent of the model in standby.h: 8 x 8 supersampling of the awake picture under the box the XL measured) ----
struct Src { int x, y, w, h; uint32_t rgb; };

std::vector<Src> viewSources (int view, int slot = 0)
{
    const auto& v = legacy::kViews[view];
    std::vector<Src> s;
    for (int m = 0; m < 2; ++m)
        s.push_back ({ v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h, v.markers[m].rgb });
    s.push_back ({ v.instance.x, v.instance.y, v.instance.w, v.instance.h, static_cast<uint32_t> (layout::kInstanceSlotRgb[slot]) });
    return s;
}

/** The host's picture around the sources: the awake picture (background 1e1d1e, the sources as solid squares) blurred by a box `width` px wide (0: not blurred) centred 0.25 px to the left / above the pixel
    centre, times `f`, painted into the fake framebuffer (12 px around every source). */
void paintPicture (FakeFrame& fr, const std::vector<Src>& srcs, double f, double width, int rot = 90)
{
    const int pad = 12;
    const int sub = 4;   // sub-samples per pixel and axis: the box edges (width 7.5, shift -0.25) lie on this grid
    auto awakeAt = [&] (double x, double y, double out[3])
    {
        uint32_t c = 0x1e1d1eu;
        for (const Src& s : srcs)
            if (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h)
                c = s.rgb;
        for (int k = 0; k < 3; ++k)
            out[k] = static_cast<double> ((c >> (16 - 8 * k)) & 255u);
    };
    std::set<std::pair<int, int>> done;
    for (const Src& s : srcs)
        for (int y = s.y - pad; y < s.y + s.h + pad; ++y)
            for (int x = s.x - pad; x < s.x + s.w + pad; ++x)
            {
                if (! done.insert ({ x, y }).second)
                    continue;
                double acc[3] = { 0, 0, 0 };
                if (width <= 0.0)
                    awakeAt (x + 0.5, y + 0.5, acc);
                else
                {
                    const int n = static_cast<int> (std::lround (width * sub));
                    const double x0 = x + 0.5 - 0.25 - width / 2.0, y0 = y + 0.5 - 0.25 - width / 2.0;
                    for (int j = 0; j < n; ++j)
                        for (int i = 0; i < n; ++i)
                        {
                            double c[3];
                            awakeAt (x0 + (i + 0.5) / sub, y0 + (j + 0.5) / sub, c);
                            for (int k = 0; k < 3; ++k) acc[k] += c[k];
                        }
                    for (double& a : acc) a /= static_cast<double> (n * n);
                }
                uint32_t rgb = 0;
                for (int k = 0; k < 3; ++k)
                    rgb |= static_cast<uint32_t> (std::floor (f * acc[k] + 0.5)) << (16 - 8 * k);
                int px, py;
                uiPointToPanel (rot, x, y, fr.panelW, fr.panelH, px, py);
                fr.setPixel (px, py, rgb);
            }
}

/** A PageDetector over the legacy table (the 17 views, their instance rectangles). */
struct DetRig
{
    FakeFrame frame;
    std::vector<MarkerSpec> marks;
    std::vector<Rect> insts;
    std::unique_ptr<PageDetector> det;
    DetRig()
    {
        frame.views = legacy::kViews;
        for (int v = 0; v < legacy::kNumViews; ++v)
        {
            for (int m = 0; m < 2; ++m)
                marks.push_back ({ { legacy::kViews[v].markers[m].rect.x, legacy::kViews[v].markers[m].rect.y, legacy::kViews[v].markers[m].rect.w, legacy::kViews[v].markers[m].rect.h }, legacy::kViews[v].markers[m].rgb });
            insts.push_back ({ legacy::kViews[v].instance.x, legacy::kViews[v].instance.y, legacy::kViews[v].instance.w, legacy::kViews[v].instance.h });
        }
        det = std::make_unique<PageDetector> (frame, marks.data(), legacy::kNumViews, insts.data());
    }
};

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
    // ---- the XL's blur: the model against what the XL's two grabs measured (play page, 1280 x 800 UI orientation, host plane) ----
    {
        // the dimmed host picture, sampled: row y = 137, x = 30 .. 54 step 2 (left marker at x 33 .. 38, instance picture at 45 .. 50), and x = 1236 .. 1254 step 2 (right marker at 1241 .. 1246)
        const uint32_t left[13] = { 0x25201fu, 0x382622u, 0x4b2d26u, 0x543027u, 0x482b25u, 0x342522u, 0x231e2fu, 0x1b1a57u, 0x191882u, 0x181795u, 0x19197au, 0x1b1b4fu, 0x1d1c2bu };
        const uint32_t right[10] = { 0x1e1d1eu, 0x202524u, 0x273733u, 0x2d4b44u, 0x30534bu, 0x2c4741u, 0x253330u, 0x202323u, 0x1e1d1eu, 0x1e1d1eu };
        const auto src = viewSources (play1);
        int worst = 0;
        auto compare = [&] (int x, uint32_t got)
        {
            for (int c = 0; c < 3; ++c)
            {
                double want = static_cast<double> ((0x1e1d1eu >> (16 - 8 * c)) & 255u);
                const double bg = want;
                for (const Src& q : src)
                    want += standby::detail::boxCover (x, q.x, q.w) * standby::detail::boxCover (137, q.y, q.h) * (static_cast<double> ((q.rgb >> (16 - 8 * c)) & 255u) - bg);
                const double g = static_cast<double> ((got >> (16 - 8 * c)) & 255u);
                const double tol = standby::kBlurTolerance + standby::kBlurRelTolerance * std::fabs (want - bg);
                worst = std::max (worst, static_cast<int> (std::ceil (std::fabs (g - want) - tol)));
            }
        };
        for (int i = 0; i < 13; ++i) compare (30 + 2 * i, left[i]);
        for (int i = 0; i < 10; ++i) compare (1236 + 2 * i, right[i]);
        check (worst <= 0, "the blur model (box 7.5 px) reproduces the 23 measured samples of the XL's dimmed picture within the tolerance (largest excess " + std::to_string (worst) + " levels)");
        // the measured centres: marker 1 awake (114,58,44) -> dimmed (84,48,39), marker 2 (20,20,216) -> (24,23,149), right (58,114,100) -> (48,83,75); the background (30,29,30) does not change
        const double cover = standby::detail::boxCover (36, 33, 6) * standby::detail::boxCover (137, 134, 6);
        check (std::fabs ((84.0 - 30.0) / (114.0 - 30.0) - cover) < 0.03 && std::fabs ((149.0 - 30.0) / (216.0 - 30.0) - cover) < 0.03 && std::fabs ((83.0 - 29.0) / (114.0 - 29.0) - cover) < 0.03,
               "the three measured centres keep " + std::to_string (cover) + " of their contrast (0.64 measured on all three channels of the three markers)");
    }
    // ---- the XL's blur: the picture-alone look, every view, the Key 61's half brightness, and what must be refused ----
    {
        std::unique_ptr<DetRig> d = std::make_unique<DetRig>();
        auto fresh = [&] { d = std::make_unique<DetRig>(); };
        auto look = [&] (int view, bool panel = false) { return d->det->blurLook (view, 90, 0, panel); };
        int recognised = 0, factorOk = 0;
        for (int v = 0; v < legacy::kNumViews; ++v)
        {
            fresh();
            paintPicture (d->frame, viewSources (v), 1.0, 7.5);
            const auto r = look (v);
            recognised += r.ok ? 1 : 0;
            factorOk += r.measured && r.factor == 1.0 ? 1 : 0;
            if (! r.ok)
                std::printf ("      (view %s not recognised)\n", legacy::kViews[v].name);
        }
        check (recognised == legacy::kNumViews && factorOk == legacy::kNumViews, "blur only (factor 1.0): the blurred markers of every one of the " + std::to_string (legacy::kNumViews) + " views are recognised, the factor measured as 1 (" + std::to_string (recognised) + " / " + std::to_string (factorOk) + ")");
        int half = 0, halfFactor = 0;
        for (int v = 0; v < legacy::kNumViews; ++v)
        {
            fresh();
            paintPicture (d->frame, viewSources (v), 0.5, 7.5);
            const auto r = look (v);
            half += r.ok ? 1 : 0;
            if (! r.ok)
                std::printf ("      (half: view %s not recognised, measured %d factor %.3f)\n", legacy::kViews[v].name, r.measured ? 1 : 0, r.factor);
            halfFactor += r.measured && r.factor == standby::kDimFactor ? 1 : 0;
        }
        check (half == legacy::kNumViews && halfFactor == legacy::kNumViews, "half brightness and blurred (the Key 61): the same code recognises every view and measures the factor 0.5 (" + std::to_string (half) + " / " + std::to_string (halfFactor) + ")");
        // the box a little narrower and wider than the fit: still recognised
        int widths = 0;
        for (double w : { 7.25, 7.4 })
        {
            fresh();
            paintPicture (d->frame, viewSources (play1), 1.0, w);
            widths += look (play1).ok ? 1 : 0;
        }
        check (widths == 2, "a box of 7.25 or 7.4 px instead of 7.5 (the fit's own uncertainty) is recognised too (" + std::to_string (widths) + " / 2)");
        // the sharp (awake) markers are not a blurred look: the contrast at the centre is 1.0, not 0.64
        fresh();
        paintPicture (d->frame, viewSources (play1), 1.0, 0.0);
        check (! look (play1).ok && look (play1).measured, "the awake (sharp) markers are no blurred look");
        // another view's markers, blurred, are not the held view's (every view whose marker colours differ by 30 levels or more)
        int refused = 0, pairs = 0;
        for (int a = 0; a < legacy::kNumViews; ++a)
        {
            fresh();
            paintPicture (d->frame, viewSources (a), 1.0, 7.5);
            for (int b = 0; b < legacy::kNumViews; ++b)
            {
                int far = 0;
                for (int m = 0; m < 2; ++m)
                    for (int c = 0; c < 3; ++c)
                        far = std::max (far, std::abs (static_cast<int> ((legacy::kViews[a].markers[m].rgb >> (8 * c)) & 255u) - static_cast<int> ((legacy::kViews[b].markers[m].rgb >> (8 * c)) & 255u)));
                if (a == b || far < 30)
                    continue;
                ++pairs;
                refused += look (b).ok ? 0 : 1;
            }
        }
        check (pairs > 100 && refused == pairs, "the blurred markers of another view (colours 30+ levels apart) are not accepted as the held view (" + std::to_string (refused) + " of " + std::to_string (pairs) + " pairs refused)");
        // the same view, but another instance's picture (the held slot is 0, the host drew slot 5)
        fresh();
        paintPicture (d->frame, viewSources (play1, 5), 1.0, 7.5);
        check (! d->det->blurLook (play1, 90, 0, false).ok && d->det->blurLook (play1, 90, 5, false).ok, "the blurred picture of another instance's page (slot 5) is not the held slot 0's; slot 5 itself is");
        // a screen with no markers: plain background, at 1.0 and 0.5, and the fake's default grey
        for (double f : { 1.0, 0.5 })
        {
            fresh();
            std::vector<Src> none;
            for (const Src& q : viewSources (play1)) none.push_back ({ q.x, q.y, q.w, q.h, 0x1e1d1eu });
            paintPicture (d->frame, none, f, 7.5);
            check (! look (play1).ok, "a screen with no markers (plain background at " + std::to_string (f).substr (0, 3) + "): not accepted");
        }
        fresh();
        check (! look (play1).ok && ! look (play1).measured, "a framebuffer of another colour altogether: not accepted, no factor");
        // a host pop-up over the page (here: a black box with white text where the left marker was) is no look
        fresh();
        paintPicture (d->frame, viewSources (play1), 1.0, 7.5);
        for (int y = 128; y < 146; ++y)
            for (int x = 28; x < 56; ++x)
            {
                int px, py;
                uiPointToPanel (90, x, y, d->frame.panelW, d->frame.panelH, px, py);
                d->frame.setPixel (px, py, (x + y) % 5 == 0 ? 0xffffffu : 0x000000u);
            }
        check (! look (play1).ok, "a host pop-up (black with white text) over the markers is not a blurred look");
        // the Q-Link pop-up over the right marker: allowed only where the caller says the right marker may be under it
        fresh();
        auto srcs = viewSources (play1);
        std::vector<Src> leftOnly = { srcs[0], srcs[2] };
        paintPicture (d->frame, leftOnly, 1.0, 7.5);
        for (int y = 122; y < 152; ++y)
            for (int x = 1226; x < 1260; ++x)
            {
                int px, py;
                uiPointToPanel (90, x, y, d->frame.panelW, d->frame.panelH, px, py);
                d->frame.setPixel (px, py, 0x1e1d1eu);
            }
        check (! look (play1, false).ok && look (play1, true).ok, "the right marker under the Q-Link pop-up (its fill instead): refused without the pop-up flag, accepted with it");
        // the pop-up flag does not make a missing left marker acceptable
        fresh();
        std::vector<Src> rightOnly = { srcs[1] };
        paintPicture (d->frame, rightOnly, 1.0, 7.5);
        std::vector<Src> bgLeft = { { srcs[0].x, srcs[0].y, srcs[0].w, srcs[0].h, 0x1e1d1eu }, { srcs[2].x, srcs[2].y, srcs[2].w, srcs[2].h, 0x1e1d1eu } };
        paintPicture (d->frame, bgLeft, 1.0, 7.5);
        check (! look (play1, true).ok, "with the pop-up flag: the left marker must still be there");
    }
    // ---- the tracker with the picture-alone evidence ----
    {
        using standby::Tracker;
        // XL: the backlight never changes (3, 3), the blurred markers decide, wake = the awake markers
        Tracker t;
        double now = 0.0;
        int hidden = 0;
        for (int i = 0; i < 300; ++i) { now += 10.0; t.update (now, true, false, true, true, 3, false); }
        for (int i = 0; i < 200; ++i)
        {
            now += 10.0;
            const standby::Verdict v = t.update (now, false, false, true, true, 3, true);
            if (! v.hold || ! v.dimmed) ++hidden;
        }
        check (hidden == 0 && t.dimmed() && t.dims() == 1 && std::string (t.evidence()) == "picture" && t.hiddenTicks() == 0, "tracker, XL: the blurred markers alone dim it at once, backlight unchanged, evidence picture, 0 hidden");
        {
            // XL with a backlight that reads 4 awake and 3 dimmed (one level, ratio 0.75): that is no backlight drop, the evidence stays the picture
            Tracker x;
            double tt = 0.0;
            for (int i = 0; i < 300; ++i) { tt += 10.0; x.update (tt, true, false, true, true, 4, false); }
            for (int i = 0; i < 50; ++i) { tt += 10.0; x.update (tt, false, false, true, true, 3, true); }
            check (x.dimmed() && std::string (x.evidence()) == "picture" && ! x.backlightLow(), "tracker, XL: a one level step of the backlight (4 -> 3) is not backlight evidence: evidence picture");
            // a real drop (227 -> 106) with the dimmed markers is still backlight+picture
            Tracker k2;
            tt = 0.0;
            for (int i = 0; i < 300; ++i) { tt += 10.0; k2.update (tt, true, false, true, true, 227, false); }
            for (int i = 0; i < 50; ++i) { tt += 10.0; k2.update (tt, false, true, true, true, 106, false); }
            check (k2.dimmed() && std::string (k2.evidence()) == "backlight+picture", "tracker, Key 61: the drop of 121 levels is still backlight evidence");
        }
        // half-way frames (neither sharp nor blurred) while waking: held, then awake
        int gone = 0;
        for (int i = 0; i < 20; ++i) { now += 10.0; if (! t.update (now, false, false, true, true, 3, false).hold) ++gone; }
        now += 10.0;
        t.update (now, true, false, true, true, 3, false);
        check (gone == 0 && ! t.dimmed() && t.wakes() == 1 && std::string (t.evidence()) == "none", "tracker, XL: 200 ms of half-way markers are held, then the sharp markers wake it (evidence none again)");
        // half-way frames that last longer than the bound: the page is gone, the host left it
        for (int i = 0; i < 100; ++i) { now += 10.0; t.update (now, false, false, true, true, 3, true); }
        int held = 0;
        for (int i = 0; i < 60; ++i) { now += 10.0; if (t.update (now, false, false, true, true, 3, false).hold) ++held; }
        check (! t.dimmed() && held > 0 && held <= static_cast<int> (standby::kHoldMs / 10.0) + 2, "tracker, XL: neither markers for longer than the hold: the page goes (held " + std::to_string (held) + " ticks)");
        // Key 61: the backlight and the picture: evidence backlight+picture; the picture alone first (blur look before the backlight follows) is picture, then backlight+picture
        Tracker k;
        now = 0.0;
        for (int i = 0; i < 300; ++i) { now += 10.0; k.update (now, true, false, true, true, 227, false); }
        for (int i = 0; i < 5; ++i) { now += 10.0; k.update (now, false, true, true, true, 106, false); }
        check (k.dimmed() && std::string (k.evidence()) == "backlight+picture", "tracker, Key 61: backlight drop + dimmed markers: evidence backlight+picture");
        // an unrelated screen: no markers, no look: never dimmed
        Tracker u;
        now = 0.0;
        for (int i = 0; i < 300; ++i) { now += 10.0; u.update (now, true, false, true, true, 3, false); }
        for (int i = 0; i < 100; ++i) { now += 10.0; u.update (now, false, false, true, true, 3, false); }
        check (! u.dimmed() && u.dims() == 0, "tracker: the page changed to an unrelated screen: no dim state");
    }
    // ---- the controller on the XL: backlight 3 -> 3, the picture blurred but not darkened ----
    {
        auto paint = [] (Rig& r, int view, double f, double width) { paintPicture (r.frame, viewSources (view), f, width); };
        Rig r (true, 3);
        paint (r, filter1, 1.0, 0.0);
        r.run (2200);
        r.watching = true;
        r.watchView = filter1;
        r.missing = 0;
        const int disables = r.plane.countOf ("disable");
        const std::vector<uint32_t> awake = r.plane.shownCopy;
        check (r.on() && r.ctl->currentIndex() == filter1 && ! awake.empty(), "(XL: the page is on screen awake, backlight 3)");
        paint (r, filter1, 1.0, 7.5);   // the host blurs its picture: the backlight stays 3
        r.run (800);
        std::string st = r.status();
        check (r.missing == 0 && r.plane.countOf ("disable") == disables && line (st, "dimmed") == "yes" && line (st, "dim_evidence") == "picture" && line (st, "dim_transitions") == "1"
                   && line (st, "dim_hidden_frames") == "0" && line (st, "backlight_value") == "3" && std::stod (line (st, "dim_factor")) == 1.0,
               "XL: the blur alone dims it: our picture never missing, the plane never switched off, evidence " + line (st, "dim_evidence") + ", factor " + line (st, "dim_factor") + ", hidden frames " + line (st, "dim_hidden_frames"));
        check (r.plane.shownCopy == awake, "XL, dimmed: our picture is the awake picture, bit for bit (not darkened, sharp)");
        // half-way markers (a blur of 3 px: neither sharp nor the dim): held
        paint (r, filter1, 1.0, 3.0);
        r.run (150);
        check (r.missing == 0 && r.on() && line (r.status(), "dimmed") == "yes", "XL: markers half-way between blurred and sharp for 150 ms: our picture stays, no frame missing");
        // wake
        paint (r, filter1, 1.0, 0.0);
        r.run (500);
        st = r.status();
        check (r.missing == 0 && line (st, "dimmed") == "no" && line (st, "wake_transitions") == "1" && line (st, "dim_evidence") == "none" && r.plane.shownCopy == awake, "XL: the sharp markers are back: awake, 1 wake, no frame missing, the same picture");
        // dim again, then the host leaves for another page (another view's markers, blurred): the page goes within the bound
        paint (r, filter1, 1.0, 7.5);
        r.run (500);
        check (line (r.status(), "dimmed") == "yes", "(XL: dimmed again)");
        {
            std::vector<Src> gone;   // filter1's markers are gone (the host's background where they were), play1's are there, blurred
            for (const Src& q : viewSources (filter1)) gone.push_back ({ q.x, q.y, q.w, q.h, 0x1e1d1eu });
            paintPicture (r.frame, gone, 1.0, 7.5);
        }
        paint (r, 14, 1.0, 7.5);   // play1's markers blurred, not filter1's
        r.watching = false;
        r.run (700);
        check (! r.on() && line (r.status(), "dimmed") == "no" && line (r.status(), "dim_hidden_frames") == "0", "XL: while dimmed the host shows another page (its blurred markers): ours goes, not held");
    }
    {
        // a host page that is blurred and has none of our markers: nothing of ours appears
        Rig r (true, 3);
        r.frame.setAll (false, 90);
        std::vector<Src> none;
        for (const Src& q : viewSources (filter1)) none.push_back ({ q.x, q.y, q.w, q.h, 0x1e1d1eu });
        paintPicture (r.frame, none, 1.0, 7.5);
        r.run (1500);
        check (! r.on() && line (r.status(), "dimmed") == "no" && line (r.status(), "dim_transitions") == "0", "XL: a blurred host page without our markers: nothing of ours appears, no dim state");
    }
    // ---- the Key 61 path stays: backlight + picture, the half brightness, the evidence says so ----
    {
        Rig r;
        startAwake (r, filter1);
        paintPicture (r.frame, viewSources (filter1), 0.5, 7.5);   // the real blurred half-brightness picture, backlight following
        r.setBl (106);
        r.run (800);
        const std::string st = r.status();
        check (line (st, "dimmed") == "yes" && line (st, "dim_evidence") == "backlight+picture" && std::fabs (std::stod (line (st, "dim_factor")) - 0.5) < 1e-9 && line (st, "dim_hidden_frames") == "0",
               "Key 61 (blurred half brightness picture + backlight drop): dimmed, evidence " + line (st, "dim_evidence") + ", factor " + line (st, "dim_factor"));
        Rig r2;
        startAwake (r2, filter1);
        dimView (r2, filter1);   // the older fixture: only the marker pixels at half brightness, no measurable background
        r2.setBl (106);
        r2.run (800);
        check (line (r2.status(), "dimmed") == "yes" && line (r2.status(), "dim_evidence") == "backlight+picture" && line (r2.status(), "dim_factor") == "0.5", "Key 61 fixture: evidence backlight+picture, factor 0.5 (the darkening is the same as before)");
    }
    // ---- the first touch while dimmed on the XL (no backlight signal) ----
    {
        Rig r (true, 3);
        r.inst.playSel[0].store (2);
        paintPicture (r.frame, viewSources (play1), 1.0, 0.0);
        r.run (2200);
        auto* pv = static_cast<PlayView*> (const_cast<IView*> (r.ctl->viewByName ("play1")));
        check (pv != nullptr && pv->oscUi() != nullptr && r.ctl->currentIndex() == play1, "(XL: the Play page is on screen)");
        const synthyboy_mpc::ui::Rect fb = pv->oscUi()->compact().full.rect();
        const int x = fb.x + fb.w / 2, y = fb.y + fb.h / 2;
        const int slot = r.ctl->slotOf (&r.inst);
        auto touchAt = [&] (TouchEvent::Kind k) { TouchEvent e; e.kind = k; e.x = x; e.y = y; e.timeUs = 1; r.touch.queue.push_back (e); };
        paintPicture (r.frame, viewSources (play1), 1.0, 7.5);
        r.run (800);
        check (line (r.status(), "dimmed") == "yes" && line (r.status(), "dim_evidence") == "picture" && r.ctl->currentIndex() == play1, "(XL: dimmed on the Play page by the blur alone)");
        const unsigned long taken = r.ctl->touchTaken();
        touchAt (TouchEvent::Down);
        r.run (40);
        paintPicture (r.frame, viewSources (play1), 1.0, 0.0);   // the host wakes
        r.run (300);
        touchAt (TouchEvent::Move);
        r.run (40);
        touchAt (TouchEvent::Up);
        r.run (1400);
        check (line (r.status(), "dimmed") == "no" && r.ctl->touchTaken() == taken && r.ctl->uiState (slot).isMain() && ! r.inst.uiFullOpen.load() && std::stoi (line (r.status(), "dim_touch_swallowed")) == 3,
               "XL: the first touch (no backlight change anywhere) woke the host and changed nothing of ours (swallowed " + line (r.status(), "dim_touch_swallowed") + ")");
        touchAt (TouchEvent::Down);
        r.run (60);
        touchAt (TouchEvent::Up);
        r.run (300);
        check (r.ctl->touchTaken() > taken && r.ctl->uiState (slot).level() == UiStack::Level::full, "XL: the next tap opens the full screen: the controls work again");
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
