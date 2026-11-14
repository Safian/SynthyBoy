// SynthyBoy - MPC overlay tests: the views (data -> expected values and pixels with fakes) and the controller with several views (which view is on screen, the plane follows it,
// the readout targets, the counters, no redraw when nothing changes).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstring>

#include "fakes.h"
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
        check (inside, "every region is on the screen below the host's header and holds its two marker squares");
    }
    // ---- the readout targets: the union of the views, in the order of the layout ---------------------------------------------------------------------------------
    {
        std::vector<int> t;
        Controller::readoutTargets (t);
        int f1[2], f2[2], o[4];
        synthyboy::FilterResponse::readoutIndices (1, f1);
        synthyboy::FilterResponse::readoutIndices (2, f2);
        bool ok = t.size() == 2 + 2 + 3 * 4 && t[0] == f1[0] && t[1] == f1[1] && t[2] == f2[0] && t[3] == f2[1];
        for (int n = 1; n <= 3 && ok; ++n)
        {
            synthyboy::OscCycle::readoutIndices (n, o);
            for (int k = 0; k < 4; ++k)
                ok = ok && t[static_cast<size_t> (4 + (n - 1) * 4 + k)] == o[k];
        }
        check (ok, "readout targets: filter 1 (2), filter 2 (2), oscillator 1 .. 3 (4 each), as the views ask for them (" + std::to_string (t.size()) + " in all)");
    }
    // ---- the Filter 2 view ----------------------------------------------------------------------------------------------------------------------------------------
    {
        Plain p;
        FilterView v (2, p.renderer, 10.0);
        p.inst.set ("filter2.on", 1.0f);
        p.inst.set ("filter2.model", 3.0f);
        p.inst.set ("filter2.cutoff", 2500.0f);
        p.inst.set ("filter2.resonance", 60.0f);
        p.inst.set ("filter1.cutoff", 123.0f);   // filter 1 must not matter
        check (v.update (input (p.inst, nullptr, nullptr, 0.0)) && v.valid(), "filter 2 view: first update gives a curve");
        synthyboy::ParamValues pv = synthyboy::ParamValues::defaults();
        auto set = [&] (const char* k, float x) { pv.plain[synthyboy::findParamIndex (synthyboy::findParamByKey (k)->id)] = x; };
        set ("filter2.on", 1.0f); set ("filter2.model", 3.0f); set ("filter2.cutoff", 2500.0f); set ("filter2.resonance", 60.0f);
        synthyboy::FilterResponse fr (48000.0);
        synthyboy::FilterResponse::Request req;
        req.filter = 2;
        std::vector<float> ref (256);
        fr.compute (pv, req, ref.data(), 256);
        check (std::memcmp (ref.data(), v.curve()->curve(), 256 * sizeof (float)) == 0, "filter 2 view: the 256 values are FilterResponse's for filter 2 (bit for bit)");
        const uint32_t h0 = v.hash();
        p.inst.set ("filter2.cutoff", 400.0f);
        p.inst.set ("filter1.cutoff", 5000.0f);
        v.update (input (p.inst, nullptr, nullptr, 300.0));
        check (v.hash() != h0, "filter 2 view: the Filter 2 cutoff changes the curve");
        const uint32_t h1 = v.hash();
        p.inst.set ("filter1.cutoff", 50.0f);
        check (! v.update (input (p.inst, nullptr, nullptr, 700.0)) && v.hash() == h1, "filter 2 view: a Filter 1 knob changes nothing");
        float live[2] = { 800.0f, -10.0f };   // the live resonance below zero (a modulation pulled it under): the curve of 0 %, not the knob's 60 %
        v.update (input (p.inst, nullptr, live, 1100.0));
        set ("filter2.cutoff", 400.0f); set ("filter2.resonance", 0.0f);
        std::vector<float> ref0 (256);
        req.cutoffHz = 800.0f;
        fr.compute (pv, req, ref0.data(), 256);
        check (std::fabs (v.curve()->exactCurve()[100] - ref0[100]) < 1.0e-4f, "filter 2 view: a live resonance below 0 is shown as 0 % (the engine's), not as the knob");
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
    // ---- the oscillator view ------------------------------------------------------------------------------------------------------------------------------------------
    {
        Plain p;
        OscView v (2, 10.0);
        const int w = 512, h = 240;
        p.inst.set ("osc2.on", 1.0f);
        p.inst.set ("osc2.type", 0.0f);
        p.inst.set ("osc2.wavetable", 1.0f);   // the saw
        p.inst.set ("osc2.frame", 0.0f);
        std::vector<uint32_t> cv (static_cast<size_t> (w) * h);
        check (v.update (input (p.inst, nullptr, nullptr, 0.0)) && v.valid() && v.kind() == synthyboy::OscCycle::Kind::wavetable, "oscillator view: first update renders a wavetable cycle");
        v.render (cv.data(), w, h);
        check (cv[0] == 0xff000000u + layout::kBackgroundRgb, "oscillator view: opaque dark background");
        int a, b, a2, b2;
        lineRows (cv, w, h, 20, a, b);
        lineRows (cv, w, h, w - 20, a2, b2);
        check (a >= 0 && a2 >= 0 && std::fabs (0.5 * (a + b) - 0.5 * (a2 + b2)) > 100.0, "the saw's line is far apart at the two ends of the cycle (a ramp: rows " + std::to_string (0.5 * (a + b)) + " and " + std::to_string (0.5 * (a2 + b2)) + ")");
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
        p.inst.set ("osc2.wavetable", static_cast<float> (table));
        v.update (input (p.inst, nullptr, nullptr, 300.0));
        const uint32_t hFrame0 = v.hash();
        p.inst.set ("osc2.frame", 100.0f);
        v.update (input (p.inst, nullptr, nullptr, 600.0));
        check (v.hash() != hFrame0, "the frame knob changes the picture (hash " + std::to_string (hFrame0) + " -> " + std::to_string (v.hash()) + ")");
        p.inst.set ("osc2.frame", 0.0f);
        v.update (input (p.inst, nullptr, nullptr, 900.0));
        check (v.hash() == hFrame0, "... and back to the first picture with the knob back");
        const float live[4] = { 100.0f, 0.0f, 0.0f, 0.0f };
        v.update (input (p.inst, nullptr, live, 1100.0));
        const uint32_t hLive = v.hash();
        p.inst.set ("osc2.frame", 100.0f);
        v.update (input (p.inst, nullptr, nullptr, 1500.0));
        check (hLive == v.hash(), "a modulated frame position from the readout draws the same picture as the knob at that value");
        // warp
        p.inst.set ("osc2.frame", 0.0f);
        p.inst.set ("osc2.warp.type", 4.0f);
        const float warped[4] = { 0.0f, 80.0f, 0.0f, 0.0f };
        v.update (input (p.inst, nullptr, warped, 1800.0));
        check (v.hash() != hFrame0, "a modulated warp amount from the readout changes the picture");
        (void) h0;
        // the types without a single cycle: the empty frame, honestly
        p.inst.set ("osc2.type", 1.0f);
        v.update (input (p.inst, nullptr, nullptr, 2100.0));
        check (v.kind() == synthyboy::OscCycle::Kind::other, "a sample oscillator: no single cycle (kind other)");
        v.render (cv.data(), w, h);
        check (countIf (cv, isAccent) == 0, "... and nothing is drawn but the frame");
        p.inst.set ("osc2.type", 0.0f);
        p.inst.set ("osc2.on", 0.0f);
        v.update (input (p.inst, nullptr, nullptr, 2400.0));
        check (v.kind() == synthyboy::OscCycle::Kind::off, "an oscillator that is off: kind off");
        // another oscillator's parameters change nothing
        p.inst.set ("osc2.on", 1.0f);
        v.update (input (p.inst, nullptr, nullptr, 2700.0));
        const uint32_t hh = v.hash();
        p.inst.set ("osc1.frame", 77.0f);
        check (! v.update (input (p.inst, nullptr, nullptr, 3000.0)) && v.hash() == hh, "a parameter of another oscillator changes nothing");
    }
    // ---- the controller with several views -------------------------------------------------------------------------------------------------------------------------------
    {
        TempDir dir;
        FakePlane plane;
        FakeFrame frame;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        Controller ctl (plane, frame, renderer, dir.paths(), Timing(), 10.0);
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
        auto viewIndex = [] (const char* name) { for (int i = 0; i < layout::kNumViews; ++i) if (std::string (layout::kViews[i].name) == name) return i; return -1; };
        const int lfo2 = viewIndex ("lfo2"), osc3 = viewIndex ("osc3"), f2 = viewIndex ("filter2");
        check (lfo2 >= 0 && osc3 >= 0 && f2 >= 0, "the layout has filter2, lfo2 and osc3");
        inst.set ("osc3.on", 1.0f);
        // the LFO 2 tab is on screen
        frame.setView (lfo2, true, 90);
        check (until ([&] { return plane.planeOn; }, 500) >= 0, "the markers of LFO 2: the plane is on");
        const layout::Rect& r = layout::kViews[lfo2].region;
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
        // another tab: the oscillator view takes the plane over, with its own region
        frame.setView (lfo2, false, 90);
        frame.setView (osc3, true, 90);
        check (until ([&] { return plane.planeOn && plane.lastRect.w == 240 && ctl.currentIndex() == osc3; }, 500) >= 0, "the Osc 3 tab: the plane is the Osc 3 region");
        const layout::Rect& ro = layout::kViews[osc3].region;
        check (plane.lastRect == uiRectToPanel (90, Rect { ro.x, ro.y, ro.w, ro.h }, 800, 1280), "... exactly");
        run (1200);
        check (line ("view") == "osc3" && ! line ("osc_cycle_hash").empty() && line ("osc_kind") == "wavetable", "the status file names osc3 and has the cycle hash");
        // the panel buffer that is shown is the osc picture: opaque
        int alpha255 = 0;
        for (uint32_t p : plane.shownCopy) alpha255 += (p >> 24) == 255;
        check (alpha255 == static_cast<int> (plane.shownCopy.size()), "every pixel of the shown picture is opaque (the skin's own picture is hidden under it)");
        // nothing on screen: off
        frame.setView (osc3, false, 90);
        check (until ([&] { return ! plane.planeOn; }, 500) >= 0 && ctl.state() == "hidden: no view on screen", "no markers: the plane goes");
        // filter 2 and its live values (targets 2 and 3)
        frame.setView (f2, true, 90);
        inst.set ("filter2.on", 1.0f);
        inst.set ("filter2.cutoff", 900.0f);
        until ([&] { return plane.planeOn; }, 500);
        run (1200);
        check (line ("view") == "filter2" && ! line ("curve_hash").empty(), "the Filter 2 tab: the status names filter2 and has the curve hash");
        const std::string hashA = line ("curve_hash");
        inst.extra[2] = 3000.0f; inst.extra[3] = 40.0f;
        inst.hasLive = true;
        run (1300);
        check (line ("curve_hash") != hashA && std::stof (line ("live_cutoff_hz")) > 2999.0f, "the live values of the filter 2 targets move the Filter 2 curve (status: cutoff " + line ("live_cutoff_hz") + ")");
        check (line ("view_lfo2").find ("updates=") == 0 && line ("view_osc3").find ("frames=") != std::string::npos, "the status file has the counters of every view");
        // the dump is the visible view's data
        dir.touch ("dump");
        run (1500);
        check (dir.read ("curve.txt").find ("# view filter2:") == 0, "the dump switch writes the data of the visible view (filter2)");
    }
    std::printf (g_fails ? "views_host: FAILED (%d)\n" : "views_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
