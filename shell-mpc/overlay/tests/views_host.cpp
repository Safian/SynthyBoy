// SynthyBoy - MPC overlay tests: the views (data -> expected values and pixels with fakes) and the controller with several views (which view is on screen, the plane follows it,
// the readout targets, the counters, no redraw when nothing changes).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstring>
#include <set>
#include <sstream>

#include "fakes.h"
#include "fixtures/test_layout.h"
#include "synthyboy/engine.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/lfo_display.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/readout.h"

using namespace ovt;

namespace
{
struct Plain
{
    FakeInstance inst;
    CpuCurveRenderer renderer;
};

ViewInput input (FakeInstance& inst, const synthyboy::LiveValues* live, const float* targets, double now)
{
    ViewInput in;
    in.instance = &inst;
    in.live = live;
    in.targets = targets;
    in.nowMs = now;
    return in;
}

uint32_t px (const std::vector<uint32_t>& cv, int w, int x, int y) { return cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)]; }
bool isAccent (uint32_t p) { return (p >> 24) == 255 && ((p >> 16) & 255) > 0xf0 && ((p >> 8) & 255) < 0x80; }
bool isWhite (uint32_t p) { return (p >> 24) == 255 && ((p >> 16) & 255) > 0x90 && ((p >> 8) & 255) > 0x90 && (p & 255) > 0x90; }   // the cursor: white at 60 % over the dark background

/** The rows of accent coloured (line) pixels of column x: first and last, -1 when none. */
void lineRows (const std::vector<uint32_t>& cv, int w, int h, int x, int& first, int& last)
{
    first = last = -1;
    for (int y = 0; y < h; ++y)
        if (isAccent (px (cv, w, x, y)))
        {
            if (first < 0) first = y;
            last = y;
        }
}

int countIf (const std::vector<uint32_t>& cv, bool (*f) (uint32_t)) { int n = 0; for (uint32_t p : cv) n += f (p); return n; }
}

int main()
{
    // ---- the layout: unique marker colours, regions inside the screen --------------------------------------------------------------------------------------------
    {
        bool unique = true, inside = true;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const layout::View& a = layout::kViews[i];
            inside = inside && a.region.x >= 0 && a.region.y >= 110 && a.region.x + a.region.w <= layout::kScreenW && a.region.y + a.region.h <= layout::kScreenH;
            for (int m = 0; m < 2; ++m)
            {
                const layout::Rect& r = a.markers[m].rect;
                inside = inside && r.x >= a.region.x && r.y >= a.region.y && r.x + r.w <= a.region.x + a.region.w && r.y + r.h <= a.region.y + a.region.h;
                for (int j = 0; j < layout::kNumViews; ++j)
                    for (int n = 0; n < 2; ++n)
                    {
                        if (i == j && m == n) continue;
                        const unsigned c = a.markers[m].rgb, d = layout::kViews[j].markers[n].rgb;
                        bool near = true;
                        for (int sh = 0; sh <= 16; sh += 8)
                            near = near && std::abs (static_cast<int> ((c >> sh) & 255) - static_cast<int> ((d >> sh) & 255)) <= 2 * PageDetector::kTolerance;
                        unique = unique && ! near;
                    }
            }
        }
        check (unique, "every marker colour differs from every other by more than twice the detector's tolerance (no view can be mistaken for another)");
        bool instOk = true;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const layout::View& a = layout::kViews[i];
            const layout::Rect& r = a.instance;
            instOk = instOk && r.w >= 4 && r.h >= 4 && r.x >= a.region.x && r.y >= a.region.y && r.x + r.w <= a.region.x + a.region.w && r.y + r.h <= a.region.y + a.region.h;
            for (int m = 0; m < 2; ++m)
            {
                const layout::Rect& mr = a.markers[m].rect;
                instOk = instOk && (r.x >= mr.x + mr.w || mr.x >= r.x + r.w || r.y >= mr.y + mr.h || mr.y >= r.y + r.h);
            }
        }
        check (instOk, "every view's instance picture is inside its region, at least 4 x 4, and does not overlap a marker");
        check (inside, "every region is on the screen below the host's header and holds its two marker squares");
    }
    // ---- the readout targets: the union of the views, in the order of the layout ---------------------------------------------------------------------------------
    {
        std::vector<int> t;
        SynthyBoyPageFactory::targetsOfAllPages (t);
        int f1[2], f2[2], o[4];
        synthyboy::FilterResponse::readoutIndices (1, f1);
        synthyboy::FilterResponse::readoutIndices (2, f2);
        std::vector<int> expected;
        // Play and all three standalone OSC tabs share these slices in layout order.
        for (int osc = 1; osc <= 3; ++osc)
        {
            synthyboy::OscCycle::readoutIndices (osc, o);
            expected.insert (expected.end(), o, o + 4);
        }
        // The Filter tabs share one pair of values per filter.
        expected.insert (expected.end(), f1, f1 + 2);
        expected.insert (expected.end(), f2, f2 + 2);
        check (t == expected, "Play, OSC and Filter share sixteen readout targets in layout order");
    }
    // ---- the Filter 1 view ----------------------------------------------------------------------------------------------------------------------------------------
    {
        Plain p;
        FilterView v (p.renderer, 10.0);
        p.inst.set ("filter1.on", 1.0f);
        p.inst.set ("filter1.model", 3.0f);
        p.inst.set ("filter1.cutoff", 2500.0f);
        p.inst.set ("filter1.resonance", 60.0f);
        p.inst.set ("filter2.cutoff", 123.0f);   // filter 2 must not matter
        check (v.update (input (p.inst, nullptr, nullptr, 0.0)) && v.valid(), "filter 1 view: first update gives a curve");
        synthyboy::ParamValues pv = synthyboy::ParamValues::defaults();
        auto set = [&] (const char* k, float x) { pv.plain[synthyboy::findParamIndex (synthyboy::findParamByKey (k)->id)] = x; };
        set ("filter1.on", 1.0f); set ("filter1.model", 3.0f); set ("filter1.cutoff", 2500.0f); set ("filter1.resonance", 60.0f);
        synthyboy::FilterResponse fr (48000.0);
        synthyboy::FilterResponse::Request req;
        req.filter = 1;
        std::vector<float> ref (256);
        fr.compute (pv, req, ref.data(), 256);
        check (std::memcmp (ref.data(), v.curve()->curve(), 256 * sizeof (float)) == 0, "filter 1 view: the 256 values are FilterResponse's for filter 1 (bit for bit)");
        const uint32_t h0 = v.hash();
        p.inst.set ("filter1.cutoff", 400.0f);
        p.inst.set ("filter2.cutoff", 5000.0f);
        v.update (input (p.inst, nullptr, nullptr, 300.0));
        check (v.hash() != h0, "filter 1 view: the Filter 1 cutoff changes the curve");
        const uint32_t h1 = v.hash();
        p.inst.set ("filter2.cutoff", 50.0f);
        check (! v.update (input (p.inst, nullptr, nullptr, 700.0)) && v.hash() == h1, "filter 1 view: a Filter 2 knob changes nothing");
        float live[2] = { 800.0f, -10.0f };   // the live resonance below zero (a modulation pulled it under): the curve of 0 %, not the knob's 60 %
        v.update (input (p.inst, nullptr, live, 1100.0));
        set ("filter1.cutoff", 400.0f); set ("filter1.resonance", 0.0f);
        std::vector<float> ref0 (256);
        req.cutoffHz = 800.0f;
        fr.compute (pv, req, ref0.data(), 256);
        check (std::fabs (v.curve()->exactCurve()[100] - ref0[100]) < 1.0e-4f, "filter 1 view: a live resonance below 0 is shown as 0 % (the engine's), not as the knob");
    }
    // ---- the LFO view -------------------------------------------------------------------------------------------------------------------------------------------------
    {
        Plain p;
        LfoView v (2, 10.0);
        const int w = 600, h = 240;
        p.inst.set ("lfo2.shape", 0.0f);   // sine: starts at the bottom
        p.inst.set ("lfo2.rate", 2.0f);
        p.inst.set ("lfo2.smooth", 0.0f);
        std::vector<uint32_t> cv (static_cast<size_t> (w) * h);
        check (v.update (input (p.inst, nullptr, nullptr, 0.0)) && v.valid(), "LFO view: first update computes the shape");
        v.render (cv.data(), w, h);
        check (cv[0] == 0xff000000u + layout::kBackgroundRgb, "LFO view: opaque dark background (the skin's picture stays under it when the overlay is off)");
        int a, b;
        lineRows (cv, w, h, 0, a, b);
        const double yBottom = LfoView::valueToY (-1.0, h), yTop = LfoView::valueToY (1.0, h);
        check (a >= 0 && std::fabs (0.5 * (a + b) + 0.5 - yBottom) < 2.5, "sine at x = 0 is at the bottom (-1): row " + std::to_string (0.5 * (a + b)) + " vs " + std::to_string (yBottom));
        lineRows (cv, w, h, w / 2, a, b);
        check (a >= 0 && std::fabs (0.5 * (a + b) + 0.5 - yTop) < 2.5, "sine at half a cycle is at the top (+1)");
        lineRows (cv, w, h, w - 1, a, b);
        check (a >= 0 && std::fabs (0.5 * (a + b) + 0.5 - yBottom) < 2.5, "sine at the end of the cycle is at the bottom again");
        check (countIf (cv, isWhite) == 0, "no voice (no live values): no cursor");
        // the cursor: a held note, phase 0.25
        p.inst.hasLive = true;
        p.inst.voices = 1;
        p.inst.lfoPhase[1] = 0.25f;
        synthyboy::LiveValues lv;
        p.inst.live (lv);
        check (v.update (input (p.inst, &lv, nullptr, 50.0)), "LFO view: the cursor appears (a redraw is asked for)");
        v.render (cv.data(), w, h);
        const int cx = static_cast<int> (std::lround (0.25 * (w - 1)));
        const int whiteCol = [&] { int n = 0; for (int y = 0; y < h; ++y) n += isWhite (px (cv, w, cx, y)) || isWhite (px (cv, w, cx + 1, y)); return n; }();
        check (whiteCol > h / 2, "the cursor is a vertical line at x = 0.25 of the cycle (" + std::to_string (whiteCol) + " white pixels in the column)");
        const double yMid = LfoView::valueToY (0.0, h);   // sine at a quarter cycle crosses the middle: the dot is there
        bool dot = false;
        for (int y = static_cast<int> (yMid) - 3; y <= static_cast<int> (yMid) + 3; ++y)
            dot = dot || isWhite (px (cv, w, cx - 5, y)) || isWhite (px (cv, w, cx + 6, y));
        check (dot, "... with a dot on the curve (value 0 at a quarter of the sine)");
        // it moves; the picture is redrawn only when the cursor pixel changes
        p.inst.lfoPhase[1] = 0.25f;
        p.inst.live (lv);
        check (! v.update (input (p.inst, &lv, nullptr, 60.0)), "the cursor stands still: no redraw");
        p.inst.lfoPhase[1] = 0.5f;
        p.inst.live (lv);
        check (v.update (input (p.inst, &lv, nullptr, 70.0)) && std::fabs (v.cursor() - 0.5f) < 1.0e-6f, "the cursor moved: redraw, at 0.5");
        // no voice: the cursor goes
        p.inst.lfoPhase[1] = -1.0f;
        p.inst.live (lv);
        check (v.update (input (p.inst, &lv, nullptr, 80.0)), "the voice ended: redraw");
        v.render (cv.data(), w, h);
        check (countIf (cv, isWhite) == 0, "... without the cursor");
        // another LFO's cursor does not matter
        p.inst.lfoPhase[0] = 0.7f;
        p.inst.live (lv);
        check (! v.update (input (p.inst, &lv, nullptr, 90.0)), "the cursor of LFO 1 does not move the view of LFO 2");
        // the shape follows its parameters (and only its own)
        const uint32_t h0 = v.hash();
        p.inst.set ("lfo2.shape", 4.0f);
        v.update (input (p.inst, &lv, nullptr, 200.0));
        check (v.hash() != h0, "the shape parameter changes the picture");
        v.render (cv.data(), w, h);
        lineRows (cv, w, h, 100, a, b);
        check (a >= 0 && std::fabs (0.5 * (a + b) + 0.5 - LfoView::valueToY (1.0, h)) < 6.0, "square: the first half of the cycle is high (+1)");
        const uint32_t h1 = v.hash();
        p.inst.set ("lfo1.shape", 7.0f);
        check (! v.update (input (p.inst, &lv, nullptr, 300.0)) && v.hash() == h1, "a parameter of another LFO changes nothing");
        p.inst.set ("lfo2.phase", 90.0f);
        check (v.update (input (p.inst, &lv, nullptr, 400.0)) && v.hash() != h1, "the phase offset shifts the picture");
        // against the core: the dump values are LfoDisplay's
        std::vector<float> ref (LfoView::kPoints);
        synthyboy::ParamValues pv = synthyboy::ParamValues::defaults();
        auto set = [&] (const char* k, float x) { pv.plain[synthyboy::findParamIndex (synthyboy::findParamByKey (k)->id)] = x; };
        set ("lfo2.shape", 4.0f); set ("lfo2.rate", 2.0f); set ("lfo2.smooth", 0.0f); set ("lfo2.phase", 90.0f);
        synthyboy::LfoDisplay::Request req;
        req.lfo = 2;
        synthyboy::LfoDisplay::compute (pv, nullptr, req, ref.data(), LfoView::kPoints);
        check (std::memcmp (ref.data(), v.shape().data(), ref.size() * sizeof (float)) == 0, "the 300 values of the view are LfoDisplay's (bit for bit)");
        // a custom shape comes from the content of the state
        auto content = std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentLfoShape s;
        s.lfo = 2;
        s.points = { synthyboy::ContentPoint (0.0f, 0.0f), synthyboy::ContentPoint (0.5f, 1.0f), synthyboy::ContentPoint (1.0f, 0.0f) };
        content->lfoShapes.push_back (s);
        p.inst.set ("lfo2.shape", 12.0f);
        p.inst.set ("lfo2.phase", 0.0f);
        p.inst.setContent (content);
        v.update (input (p.inst, &lv, nullptr, 600.0));
        check (v.shape()[LfoView::kPoints / 2] > 0.9f && v.shape()[0] < -0.9f, "a custom shape from the content of the state is drawn (a triangle: low at the ends, high in the middle)");
    }
    // ---- the oscillator's display (osc_display.h: the data of the Play page's picture and of the OSC tab's pages) ------------------------------------------------------------------------------------------------------------------------------------------
    {
        Plain p;
        OscDisplay v (10.0);
        ViewCounters counters;
        const int sel = 3;   // the selector's number of oscillator B (osc2)
        auto tg = [] (float a, float b, float c, float d) { static float t[12]; std::fill (t, t + 12, -1.0f); t[4] = a; t[5] = b; t[6] = c; t[7] = d; return t; };   // the readout values of the three oscillators: B's are 4 .. 7
        auto upd = [&] (const float* t, double ms) { return v.update (p.inst, sel, t, nullptr, ms, counters); };
        p.inst.set ("osc2.on", 1.0f);
        p.inst.set ("osc2.type", 0.0f);
        p.inst.set ("osc2.wavetable", 1.0f);   // the saw
        p.inst.set ("osc2.frame", 0.0f);
        check (upd (nullptr, 0.0) && v.valid() && v.kind() == synthyboy::OscCycle::Kind::wavetable, "oscillator display: first update computes a wavetable cycle");
        check (std::fabs (v.cycle()[synthyboy::OscCycle::kSamples / 16] - v.cycle()[synthyboy::OscCycle::kSamples * 15 / 16]) > 1.0f, "the saw's cycle is a ramp: far apart at the two ends");
        // A preset may replace only its content snapshot while all normalized
        // parameters stay equal.  A budget miss must leave that identity dirty
        // so the next due update really refreshes the OscCycle.
        FakeInstance contentOscInst;
        contentOscInst.set ("osc2.on", 1.0f);
        contentOscInst.set ("osc2.type", 0.0f);
        contentOscInst.set ("osc2.wavetable", 1.0f);
        contentOscInst.set ("osc2.frame", 0.0f);
        OscDisplay contentProbe (10.0);
        ViewCounters contentCounters;
        auto contentUpd = [&] (double ms) { return contentProbe.update (contentOscInst, sel, nullptr, nullptr, ms, contentCounters); };
        check (contentUpd (0.0), "oscillator content probe computes its initial cycle");
        const unsigned long oscRecomputes = contentCounters.recomputes;
        contentOscInst.setContent (std::make_shared<synthyboy::StateContent>());
        check (! contentUpd (1.0) && contentCounters.recomputes == oscRecomputes, "oscillator content replacement can be deferred without being consumed");
        check (contentUpd (200.0) && contentCounters.recomputes > oscRecomputes, "deferred oscillator content replacement is retried at the next budget deadline");
        auto engine = synthyboy::createEngine (synthyboy::EngineKind::own);
        engine->setContent (synthyboy::StateContent {});
        contentOscInst.preparedOsc = engine->preparedOscContent();
        check (contentOscInst.preparedOsc && contentUpd (201.0) && contentCounters.recomputes == oscRecomputes + 2,
               "a prepared snapshot refreshes the same state object immediately, inside the compute budget interval");
        const uint32_t h0 = v.hash();
        // frame: a multi frame table; the knob and the live (modulated) value both move the picture
        int table = -1;   // the first built-in table whose first and last frame differ
        {
            synthyboy::OscCycle probe;
            synthyboy::ParamValues pv = synthyboy::ParamValues::defaults();
            auto set = [&] (const char* k, float x) { pv.plain[synthyboy::findParamIndex (synthyboy::findParamByKey (k)->id)] = x; };
            set ("osc1.on", 1.0f);
            std::vector<float> a (synthyboy::OscCycle::kSamples), b (a);
            for (int t = 0; t < synthyboy::kNumBuiltinWavetables && table < 0; ++t)
            {
                set ("osc1.wavetable", static_cast<float> (t));
                set ("osc1.frame", 0.0f);
                probe.compute (pv, {}, a.data());
                set ("osc1.frame", 100.0f);
                probe.compute (pv, {}, b.data());
                if (a != b && t != synthyboy::kWavetablePresetTable)
                    table = t;
            }
        }
        check (table >= 0, "a multi frame built-in table exists (" + std::to_string (table) + ")");
        const auto pixels = [] (const OscDisplay& osc)
        {
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindOsc, 0, 640, 400);
            sb.clear (0u);
            osc.draw (sb, { 0, 0, 640, 400 }, false);
            osc.draw (sb, { 0, 0, 640, 400 }, true);
            std::vector<uint32_t> raster;
            renderScene (*scene, raster);
            return raster;
        };
        const auto matchFresh = [&] (const float* t, double ms)
        {
            OscDisplay fresh (10.0);
            ViewCounters freshCounters;
            return fresh.update (p.inst, sel, t, nullptr, ms, freshCounters) && pixels (fresh) == pixels (v);
        };
        p.inst.set ("osc2.wavetable", static_cast<float> (table));
        upd (nullptr, 300.0);
        const uint32_t hFrame0 = v.hash();
        p.inst.set ("osc2.frame", 100.0f);
        upd (nullptr, 600.0);
        check (v.hash() != hFrame0, "the frame knob changes the picture (hash " + std::to_string (hFrame0) + " -> " + std::to_string (v.hash()) + ")");
        check (matchFresh (nullptr, 600.0), "changing FRAME draws the same stacked plot as a fresh display");
        p.inst.set ("osc2.frame", 0.0f);
        upd (nullptr, 900.0);
        check (v.hash() == hFrame0, "... and back to the first picture with the knob back");
        const float live[4] = { 100.0f, 0.0f, 0.0f, 0.0f };
        upd (tg (live[0], live[1], live[2], live[3]), 1100.0);
        const uint32_t hLive = v.hash();
        p.inst.set ("osc2.frame", 100.0f);
        upd (nullptr, 1500.0);
        check (hLive == v.hash(), "a modulated frame position from the readout draws the same picture as the knob at that value");
        // warp
        p.inst.set ("osc2.frame", 0.0f);
        p.inst.set ("osc2.warp.type", 4.0f);
        const float warped[4] = { 0.0f, 80.0f, 0.0f, 0.0f };
        upd (tg (warped[0], warped[1], warped[2], warped[3]), 1800.0);
        check (v.hash() != hFrame0, "a modulated warp amount from the readout changes the picture");
        check (matchFresh (tg (warped[0], warped[1], warped[2], warped[3]), 1800.0),
               "a modulated warp refreshes the stacked plot as well as the main trace");
        (void) h0;
        // the types without a single cycle: the empty frame, honestly
        p.inst.set ("osc2.type", 1.0f);
        upd (nullptr, 2100.0);
        check (v.kind() != synthyboy::OscCycle::Kind::wavetable && v.kind() != synthyboy::OscCycle::Kind::off, "a sample oscillator: no single cycle (an overview or no sample)");
        p.inst.set ("osc2.type", 0.0f);
        p.inst.set ("osc2.on", 0.0f);
        upd (nullptr, 2400.0);
        check (v.kind() == synthyboy::OscCycle::Kind::off, "an oscillator that is off: kind off");
        {
            double energy = 0.0;
            for (float x : v.cycle())
                energy += std::fabs (x);
            check (energy > 10.0, "an oscillator that is off keeps the cycle of its current table (the page draws it dim with an OFF tag, not an empty display)");
        }
        // another oscillator's parameters change nothing
        p.inst.set ("osc2.on", 1.0f);
        upd (nullptr, 2700.0);
        const uint32_t hh = v.hash();
        p.inst.set ("osc1.frame", 77.0f);
        check (! upd (nullptr, 3000.0) && v.hash() == hh, "a parameter of another oscillator changes nothing");
    }
    // ---- the controller with several views -------------------------------------------------------------------------------------------------------------------------------
    {
        TempDir dir;
        FakePlane plane;
        FakeFrame frame; frame.views = testlayout::kViews;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        SynthyBoyController ctl (plane, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &testlayout::pageSet());
        inst.numTargets = 16;
        ctl.attach (&inst);
        double now = 0.0;
        auto run = [&] (double ms) { const double end = now + ms; while (now < end) { now += 10.0; ctl.tick (now); } };
        auto until = [&] (auto cond, double maxMs) { const double s = now; while (now - s < maxMs) { now += 10.0; ctl.tick (now); if (cond()) return now - s; } return -1.0; };
        auto line = [&] (const char* key) -> std::string
        {
            const std::string t = dir.read ("status.txt");
            const size_t q = t.find (std::string (key) + ": ");
            if (q == std::string::npos) return "";
            return t.substr (q + std::strlen (key) + 2, t.find ('\n', q) - q - std::strlen (key) - 2);
        };
        auto viewIndex = [] (const char* name) { for (int i = 0; i < testlayout::kNumViews; ++i) if (std::string (testlayout::kViews[i].name) == name) return i; return -1; };
        const int lfo2 = viewIndex ("lfo2"), f1 = viewIndex ("filter1");
        check (lfo2 >= 0 && f1 >= 0, "the layout has filter1 and lfo2");
        // the LFO 2 tab is on screen
        frame.setView (lfo2, true, 90);
        check (until ([&] { return plane.planeOn; }, 500) >= 0, "the markers of LFO 2: the plane is on");
        const layout::Rect& r = testlayout::kViews[lfo2].region;
        check (plane.lastRect == uiRectToPanel (90, Rect { r.x, r.y, r.w, r.h }, 800, 1280), "the plane is the LFO 2 region");
        run (1200);
        check (line ("state") == "visible" && line ("view") == "lfo2" && ! line ("lfo_shape_hash").empty(), "the status file names the view (lfo2) and has its numbers");
        check (line ("markers_read_lfo2").find ("left=") == 0, "the status file has the marker reads of every view");
        // nothing changes: nothing is drawn
        int shown = plane.presented;
        run (3000);
        check (plane.presented == shown, "an idle LFO view: no picture in 3 s");
        // a held note: the cursor runs at about 30 pictures a second, and stops when the voice ends
        inst.hasLive = true;
        inst.voices = 1;
        const int before = plane.presented;
        for (int i = 0; i < 90; ++i) { inst.lfoPhase[1] = std::fmod (0.011f * (float) i, 1.0f); now += 33.0; ctl.tick (now); }
        const int frames = plane.presented - before;
        check (frames >= 70 && frames <= 92, "the cursor moves every 33 ms for 3 s: " + std::to_string (frames) + " pictures (about 30 a second)");
        inst.lfoPhase[1] = -1.0f; inst.voices = 0;
        run (400);
        shown = plane.presented;
        run (2000);
        check (plane.presented == shown, "the voice is gone: the cursor is gone and the pictures stop");
        // nothing on screen: off
        frame.setView (lfo2, false, 90);
        check (until ([&] { return ! plane.planeOn; }, 500) >= 0 && ctl.state() == "hidden: no view on screen", "no markers: the plane goes");
        // filter 1 and its live values (targets 0 and 1)
        frame.setView (f1, true, 90);
        inst.set ("filter1.on", 1.0f);
        inst.set ("filter1.cutoff", 900.0f);
        until ([&] { return plane.planeOn; }, 500);
        run (1200);
        check (line ("view") == "filter1" && ! line ("curve_hash").empty(), "the Filter 1 tab: the status names filter1 and has the curve hash");
        const std::string hashA = line ("curve_hash");
        inst.liveCutoff = 3000.0f; inst.liveRes = 40.0f;
        inst.hasLive = true;
        run (1300);
        check (line ("curve_hash") != hashA && std::stof (line ("live_cutoff_hz")) > 2999.0f, "the live values of the filter 1 targets move the Filter 1 curve (status: cutoff " + line ("live_cutoff_hz") + ")");
        check (line ("view_lfo2").find ("updates=") == 0 && line ("view_filter1").find ("frames=") != std::string::npos, "the status file has the counters of every view");
        // the dump is the visible view's data
        dir.touch ("dump");
        run (1500);
        check (dir.read ("curve.txt").find ("# view filter1:") == 0, "the dump switch writes the data of the visible view (filter1)");
    }
    // ---- two instances with different settings: every view type shows the data of the instance whose slot colour is on screen ---------------------------------------------------
    {
        struct Shot { std::vector<uint32_t> picture; uint32_t hash = 0; };
        auto viewIndex = [] (const char* name) { for (int i = 0; i < testlayout::kNumViews; ++i) if (std::string (testlayout::kViews[i].name) == name) return i; return -1; };
        // the setups of the two instances; the reference is a controller with ONE instance of the same settings
        auto setupA = [] (FakeInstance& i)
        {
            i.numTargets = 16;
            i.set ("lfo2.shape", 0.0f); i.set ("lfo2.rate", 2.0f);
            i.set ("osc3.on", 1.0f); i.set ("osc3.wavetable", 0.0f);
            i.set ("filter1.on", 1.0f); i.set ("filter1.cutoff", 900.0f);
        };
        auto setupB = [] (FakeInstance& i)
        {
            i.numTargets = 16;
            i.set ("lfo2.shape", 4.0f); i.set ("lfo2.rate", 5.0f); i.set ("lfo2.phase", 90.0f);
            i.set ("osc3.on", 1.0f); i.set ("osc3.wavetable", 3.0f); i.set ("osc3.warp.amount", 60.0f);
            i.set ("filter1.on", 1.0f); i.set ("filter1.cutoff", 4000.0f);
            i.hasLive = true; i.voices = 1; i.lfoPhase[1] = 0.3f;
        };
        auto shot = [&] (const char* view, auto setup)
        {
            TempDir dir; FakePlane plane; FakeFrame frame; CpuCurveRenderer renderer; FakeInstance inst; frame.views = testlayout::kViews;
            SynthyBoyController ctl (plane, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &testlayout::pageSet());
            setup (inst);
            ctl.attach (&inst);
            const int v = viewIndex (view);
            frame.setView (v, true, 90);
            double now = 0.0;
            for (int i = 0; i < 80; ++i) { now += 10.0; ctl.tick (now); }
            Shot s;
            s.picture = plane.shownCopy;
            s.hash = ctl.currentView() ? ctl.currentView()->hash() : 0u;
            return s;
        };
        TempDir dir; FakePlane plane; FakeFrame frame; CpuCurveRenderer renderer; FakeInstance a, b; frame.views = testlayout::kViews;
        SynthyBoyController ctl (plane, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &testlayout::pageSet());
        setupA (a); setupB (b);
        check (ctl.attach (&a) == 0 && ctl.attach (&b) == 1, "two instances: slots 0 and 1");
        double now = 0.0;
        auto run = [&] (double ms) { const double end = now + ms; while (now < end) { now += 10.0; ctl.tick (now); } };
        auto until = [&] (auto cond, double maxMs) { const double s0 = now; while (now - s0 < maxMs) { now += 10.0; ctl.tick (now); if (cond()) return now - s0; } return -1.0; };
        for (const char* name : { "lfo2", "filter1" })
        {
            const int v = viewIndex (name);
            const Shot refA = shot (name, setupA), refB = shot (name, setupB);
            frame.setView (v, true, 90);
            frame.setViewInstance (v, 0, true, 90);
            until ([&] { return plane.planeOn && ctl.currentIndex() == v && ctl.activeSlot() == 0; }, 600);
            run (300);
            const bool aOk = ctl.currentView() && ctl.currentView()->hash() == refA.hash && plane.shownCopy == refA.picture;
            frame.setViewInstance (v, 1, true, 90);
            const double t = until ([&] { return ctl.activeSlot() == 1 && ctl.currentView() && ctl.currentView()->hash() == refB.hash; }, 1000);
            run (300);
            const bool bOk = plane.shownCopy == refB.picture;
            check (aOk, std::string (name) + ": slot 0 colour: the data and the picture of instance 0");
            check (t >= 0 && t <= 200 && bOk, std::string (name) + ": slot 1 colour: instance 1's data and picture within 200 ms (took " + std::to_string (t) + " ms)");
            check (refA.hash != refB.hash || refA.picture != refB.picture, std::string (name) + ": (the two instances really draw differently)");
            frame.setView (v, false, 90);
            until ([&] { return ! plane.planeOn; }, 500);
        }
        // the cursor of a voice that plays on instance 1 shows only while instance 1's colour is on screen
        const int lfo2 = viewIndex ("lfo2");
        frame.setView (lfo2, true, 90);
        frame.setViewInstance (lfo2, 0, true, 90);
        until ([&] { return plane.planeOn && ctl.activeSlot() == 0; }, 600);
        run (200);
        const std::vector<uint32_t> noCursor = plane.shownCopy;
        frame.setViewInstance (lfo2, 1, true, 90);
        until ([&] { return ctl.activeSlot() == 1; }, 600);
        run (200);
        check (plane.shownCopy != noCursor, "lfo2: the playing voice of instance 1 is drawn only on instance 1's page");
        // the status file names the instance
        run (1200);
        const std::string t = dir.read ("status.txt");
        check (t.find ("instance_slot: 1") != std::string::npos && t.find ("view: lfo2") != std::string::npos && t.find ("instance_slots_alive: 0003") != std::string::npos, "the status file says view lfo2, instance_slot 1, slots alive 0003");
        ctl.detach (&a);
        ctl.detach (&b);
    }
    // ---- the Play view: its three data sources follow the selectors, one panel at a time --------------------------------------------------------------------------
    {
        FakeInstance inst;
        for (int k = 1; k <= 3; ++k)
        {
            inst.set (("osc" + std::to_string (k) + ".on").c_str(), 1.0f);
            inst.set (("osc" + std::to_string (k) + ".wavetable").c_str(), static_cast<float> (k));
            inst.set (("osc" + std::to_string (k) + ".frame").c_str(), 10.0f * static_cast<float> (k));
        }
        for (int k = 1; k <= 6; ++k)
        {
            inst.set (("env" + std::to_string (k) + ".attack").c_str(), 40.0f * static_cast<float> (k));
            inst.set (("env" + std::to_string (k) + ".sustain").c_str(), 100.0f - 12.0f * static_cast<float> (k));
        }
        for (int k = 1; k <= 10; ++k)
        {
            inst.set (("lfo" + std::to_string (k) + ".shape").c_str(), static_cast<float> (k % 8));
            inst.set (("lfo" + std::to_string (k) + ".rate").c_str(), 0.5f + static_cast<float> (k));
        }
        PlayView v (2, -1.0);
        double now = 0.0;
        auto step = [&] { ViewInput in; in.instance = &inst; in.nowMs = (now += 500.0); return v.update (in); };
        check (step() && v.valid() && v.selection (0) == 1 && v.selection (1) == 1 && v.selection (2) == 1, "the first update: all three panels have data, selection 1 / 1 / 1");
        std::set<uint32_t> osc, env, lfo;
        bool otherPanelsStill = true, changed = true;
        uint32_t h[3] = { v.panelHash (0), v.panelHash (1), v.panelHash (2) };
        osc.insert (h[0]); env.insert (h[1]); lfo.insert (h[2]);
        for (int k = 2; k <= 5; ++k) { inst.playSel[0] = k; changed = step() && changed; osc.insert (v.panelHash (0)); otherPanelsStill = otherPanelsStill && v.panelHash (1) == h[1] && v.panelHash (2) == h[2] && v.selection (0) == k; }
        for (int k = 2; k <= 6; ++k) { inst.playSel[1] = k; changed = step() && changed; env.insert (v.panelHash (1)); otherPanelsStill = otherPanelsStill && v.selection (1) == k; }
        for (int k = 2; k <= 10; ++k) { inst.playSel[2] = k; changed = step() && changed; lfo.insert (v.panelHash (2)); otherPanelsStill = otherPanelsStill && v.selection (2) == k; }
        check (changed, "every selector change makes the view ask for a new picture at once (no wait for the compute budget)");
        check (osc.size() == 5 && env.size() == 6 && lfo.size() == 10, "the data hash of a panel is different for every instance: Sub, A, B, C, Noise, 6 envelopes, 10 LFOs (" + std::to_string (osc.size()) + " / " + std::to_string (env.size()) + " / " + std::to_string (lfo.size()) + ")");
        check (otherPanelsStill, "a selector changes its own panel's selection (the other panels keep theirs)");
        // a parameter of the selected instance moves the picture, a parameter of another instance does not
        inst.playSel[1] = 3;
        step();
        const uint32_t e3 = v.panelHash (1);
        inst.set ("env4.attack", 5000.0f);
        step();
        check (v.panelHash (1) == e3, "a parameter of an instance that is not selected does not move the picture");
        inst.set ("env3.attack", 5000.0f);
        check (step() && v.panelHash (1) != e3, "a parameter of the selected instance does");
        // out of range selections are limited to what the engine has
        inst.playSel[0] = 9; inst.playSel[1] = 0; inst.playSel[2] = 99;
        step();
        check (v.selection (0) == 5 && v.selection (1) == 1 && v.selection (2) == 10, "a selection outside the instances is limited to the first / last");
        // Compact Play LFOs have the same budgeted content dependency.  Keep
        // parameters identical and change only the custom shape snapshot.
        FakeInstance contentInst;
        contentInst.set ("lfo1.shape", 12.0f);
        auto shapeA = std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentLfoShape triangle;
        triangle.lfo = 1;
        triangle.points = { synthyboy::ContentPoint (0.0f, 0.0f), synthyboy::ContentPoint (0.5f, 1.0f), synthyboy::ContentPoint (1.0f, 0.0f) };
        shapeA->lfoShapes.push_back (triangle);
        contentInst.setContent (shapeA);
        PlayView contentView (1, 10.0);
        ViewInput contentIn;
        contentIn.instance = &contentInst;
        contentIn.nowMs = 0.0;
        check (contentView.update (contentIn), "compact Play LFO computes its initial custom shape");
        const uint32_t contentHash = contentView.panelHash (2);
        auto shapeB = std::make_shared<synthyboy::StateContent> (*shapeA);
        shapeB->lfoShapes[0].points[1].y = -1.0f;
        contentInst.setContent (shapeB);
        contentIn.nowMs = 1.0;
        contentView.update (contentIn);
        check (contentView.panelHash (2) == contentHash, "compact Play LFO keeps the old shape while its deferred budget is closed");
        contentIn.nowMs = 200.0;
        check (contentView.update (contentIn) && contentView.panelHash (2) != contentHash, "compact Play LFO retries an identical-parameter content change when due");
        // the status file names the view and the three instances
        std::ostringstream o;
        v.writeStatus (o);
        const std::string st = o.str();
        check (st.find ("play_view: play1") != std::string::npos && st.find ("play_highlight: 2") != std::string::npos && st.find ("play_osc: 5") != std::string::npos && st.find ("play_env: 1") != std::string::npos
               && st.find ("play_lfo: 10") != std::string::npos && st.find ("play_env_hash:") != std::string::npos, "the status lines name the view, the highlight and the three selected instances, with a hash per panel");
        // Sub draws its waveform (analytic, kind wavetable), Noise the noise display; A, B and C read their own oscillator's parameters and live readout
        inst.set ("sub.on", 1.0f);
        inst.playSel[0] = 1; step();
        std::ostringstream o1; v.writeStatus (o1);
        check (v.selection (0) == 1 && o1.str().find ("play_osc_kind: wavetable") != std::string::npos && play::title (0, 1) == "SUB" && play::title (0, 5) == "NOISE" && play::title (0, 2) == "OSC A" && play::title (0, 4) == "OSC C", "Sub: its waveform picture; the titles are SUB, NOISE, OSC A .. OSC C");
        inst.set ("osc2.level", 33.0f);
        inst.playSel[0] = 3; step();
        const uint32_t oscB = v.panelHash (0);
        inst.set ("osc1.level", 77.0f); step();
        check (v.panelHash (0) == oscB, "instance 3 is oscillator B (osc2): a parameter of osc1 does not move its picture");
        // the live dot and cursor move the picture
        inst.playSel[1] = 2; inst.playSel[2] = 5; step();
        const uint32_t before = v.hash();
        synthyboy::LiveValues lv; inst.hasLive = true; inst.live (lv);
        lv.lfoPhase[4] = 0.5f; lv.envFineStage[1] = 2.0f; lv.envFineFraction[1] = 0.5f;
        ViewInput in; in.instance = &inst; in.live = &lv; in.nowMs = (now += 500.0);
        check (v.update (in) && v.hash() == before, "a held note: the dot and the cursor change the picture but not the data hash (that is the data only)");
        lv.lfoPhase[4] = 0.9f;
        in.nowMs = (now += 500.0);
        check (v.update (in), "the cursor moves on");
    }
    std::printf (g_fails ? "views_host: FAILED (%d)\n" : "views_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
