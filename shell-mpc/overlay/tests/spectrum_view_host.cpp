// SynthyBoy - MPC overlay tests: the spectrum view (SpectrumView in views.h) with a fake instance whose ring a "audio thread" fills: the view has no data without a ring, a held tone puts the curve
// at the right place, silence stops the pictures, the CPU picture and the GPU scene (reference interpreter) are the same picture and the picture is opaque and drawn the way the layout says,
// the whole controller shows the view on the Macros tab markers, draws about 30 pictures a second while sound plays and none at rest.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <string>
#include <vector>

#include "fakes.h"
#include "fixtures/test_layout.h"
#include "scene_reference.h"

using namespace ovt;
using synthyboy_mpc::SpectrumRing;
using synthyboy_mpc::kSpectrumCeilingDb;
using synthyboy_mpc::kSpectrumFloorDb;

namespace
{
constexpr double kRate = 48000.0, kPi = 3.14159265358979323846;   // FakeInstance::sampleRate

struct Tone
{
    double hz = 1000.0, amp = 0.1;
    size_t t = 0;
    void push (SpectrumRing& ring, int n)
    {
        std::vector<float> l (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i, ++t)
            l[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * kPi * hz * static_cast<double> (t) / kRate));
        ring.push (l.data(), l.data(), n);
    }
};

int viewIndex (const char* name)
{
    for (int i = 0; i < testlayout::kNumViews; ++i)
        if (std::string (testlayout::kViews[i].name) == name)
            return i;
    return -1;
}

int pointOf (double hz) { return static_cast<int> (std::lround (std::log (hz / 20.0) / std::log (1000.0) * 255.0)); }

void compare (const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int& maxDiff, size_t& differing)
{
    maxDiff = 0;
    differing = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        int m = 0;
        for (int s = 0; s < 32; s += 8)
            m = std::max (m, std::abs (static_cast<int> ((a[i] >> s) & 255u) - static_cast<int> ((b[i] >> s) & 255u)));
        maxDiff = std::max (maxDiff, m);
        differing += m > 2;
    }
}
} // namespace

int main()
{
    const int vi = viewIndex ("spectrum");
    check (vi >= 0 && testlayout::kViews[vi].kind == layout::kKindSpectrum && testlayout::kViews[vi].opaque, "the layout has the opaque spectrum view");
    const layout::View& lv = testlayout::kViews[vi];
    const int w = lv.region.w, h = lv.region.h;
    check (w == 1208 && h == 136 && lv.region.x == 36 && lv.region.y == layout::kTabOriginY + 480, "its region is the row of the 32 meters of the Macros tab (36, 590, 1208 x 136 in screen coordinates)");

    // ---- no ring: no data, the plane stays off -------------------------------------------------------------------------------------------------------------------
    {
        FakeInstance inst;
        SpectrumView v;
        ViewInput in;
        in.instance = &inst;
        in.nowMs = 0.0;
        check (! v.update (in) && ! v.valid(), "an instance without a ring (a build without the spectrum): no data");
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        check (! v.describe (sb, vi, w, h), "... and no scene");
    }

    // ---- a held tone ---------------------------------------------------------------------------------------------------------------------------------------------
    FakeInstance inst;
    inst.hasRing = true;
    Tone tone;
    SpectrumView v;
    ViewInput in;
    in.instance = &inst;
    double now = 0.0;
    int changed = 0;
    for (int f = 0; f < 40; ++f)
    {
        tone.push (inst.ring, 1600);   // 33.3 ms at 48 kHz
        now += 33.3;
        in.nowMs = now;
        changed += v.update (in);
    }
    check (v.valid() && changed >= 5, "a held tone: the view has data and says changed while the curve moves (" + std::to_string (changed) + " of 40)");
    {
        int peak = 0;
        for (int i = 1; i < SpectrumCurve::kPoints; ++i)
            if (v.curve().level()[i] > v.curve().level()[peak])
                peak = i;
        const double db = kSpectrumFloorDb + v.curve().level()[peak] * (kSpectrumCeilingDb - kSpectrumFloorDb);
        check (std::abs (peak - pointOf (1000.0)) <= 1 && std::fabs (db + 20.0) <= 1.5, "the 1 kHz tone of amplitude 0.1: peak at point " + std::to_string (peak) + " (" + std::to_string (pointOf (1000.0)) + "), " + std::to_string (db) + " dB (-20)");
    }
    // the CPU picture and the GPU scene are the same picture
    {
        std::vector<uint32_t> cpu (static_cast<size_t> (w) * static_cast<size_t> (h)), ref;
        v.render (cpu.data(), w, h);
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        check (v.describe (sb, vi, w, h) && ! sb.overflow, "the view describes itself as a scene");
        renderSceneReference (sd, ref);
        int maxDiff;
        size_t differing;
        compare (cpu, ref, maxDiff, differing);
        check (maxDiff <= 3 && differing == 0, "the scene is the CPU picture (largest channel difference " + std::to_string (maxDiff) + ", pixels differing by more than 2: " + std::to_string (differing) + ")");
        check (sd.kind == gpu::kKindSpectrum && sd.w == w && sd.h == h && sd.numPoints == 256 && sd.viewIndex == static_cast<uint32_t> (vi), "scene header: kind spectrum, the canvas, 256 points");
        int fills = 0, strokes = 0, rects = 0;
        for (uint32_t k = 0; k < sd.numOps; ++k)
        {
            fills += sd.ops[k].op == gpu::kOpFillGradient;
            strokes += sd.ops[k].op == gpu::kOpStroke;
            rects += sd.ops[k].op == gpu::kOpRect;
        }
        check (fills == 1 && strokes == 1 && rects == 3 && sd.ops[0].op == gpu::kOpClear, "the scene: clear, three grid lines, the gradient fill, the line");

        // pixel facts of the picture: opaque everywhere (the meters under it do not show), grid lines, the line at the peak, the fill under it, the background above it
        bool opaque = true;
        for (uint32_t p : cpu)
            opaque = opaque && (p >> 24) == 255u;
        check (opaque, "every pixel of the picture is opaque (the meters of the skin do not show through)");
        auto px = [&] (int x, int y) { return cpu[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)]; };
        const uint32_t bg = draw::opaque (layout::kBackgroundRgb);
        const int gx1 = CpuCurveRenderer::gridX (1000.0, w), gx2 = CpuCurveRenderer::gridX (100.0, w), gx3 = CpuCurveRenderer::gridX (10000.0, w);
        check (px (gx1, 4) != bg && px (gx2, 4) != bg && px (gx3, 4) != bg && px (gx1 + 6, 4) == bg, "the grid lines at 100 Hz, 1 kHz and 10 kHz are faint lines on the background, nothing else is");
        const int peakX = static_cast<int> (std::lround (255 == 0 ? 0 : (w - 1) * std::log10 (1000.0 / 20.0) / 3.0));
        int lineTop = -1, lineBottom = -1;
        for (int y = 0; y < h; ++y)
        {
            const uint32_t p = px (peakX, y);
            const int r = static_cast<int> ((p >> 16) & 255u), g = static_cast<int> ((p >> 8) & 255u);
            if (r > 230 && g < 80)     // the accent line (#ff2e63)
            {
                if (lineTop < 0) lineTop = y;
                lineBottom = y;
            }
        }
        const double expectY = SpectrumView::levelToY (v.curve().level()[pointOf (1000.0)], h);
        check (lineTop >= 0 && lineBottom - lineTop <= 4 && std::fabs ((lineTop + lineBottom) * 0.5 + 0.5 - expectY) < 4.0, "the line is 2 - 4 pixels thick and at the height of the curve (rows " + std::to_string (lineTop) + " .. " + std::to_string (lineBottom) + ", expected about " + std::to_string (expectY) + ")");
        // the fill under the curve: stronger just under the line than near the bottom (a vertical gradient), none above the line
        auto lum = [] (uint32_t p) { return static_cast<int> ((p >> 16) & 255u) + static_cast<int> ((p >> 8) & 255u) + static_cast<int> (p & 255u); };
        const int under = lineBottom + 8, bottom = h - 9;
        check (lineBottom >= 0 && under < bottom && lum (px (peakX, under)) > lum (px (peakX, bottom)) && lum (px (peakX, bottom)) > lum (bg), "the fill is a gradient: stronger just under the line than near the bottom, and both over the background");
        check (px (peakX + 3, 0) == bg, "above the curve: the plain background");
    }
    // silence: the curve falls, then no change and no analysis
    {
        std::vector<float> zeros (1600, 0.0f);
        int moved = 0;
        for (int f = 0; f < 200; ++f)
        {
            inst.ring.push (zeros.data(), zeros.data(), 1600);
            now += 33.3;
            in.nowMs = now;
            moved += v.update (in);
        }
        check (moved > 0 && v.curve().maxLevel() == 0.0f && v.curve().settled(), "silence: the curve falls to the floor and settles");
        const unsigned long ffts = v.counters().recomputes;
        int again = 0;
        for (int f = 0; f < 150; ++f)
        {
            inst.ring.push (zeros.data(), zeros.data(), 1600);
            now += 33.3;
            in.nowMs = now;
            again += v.update (in);
        }
        check (again == 0 && v.counters().recomputes == ffts, "at rest: no change reported in 150 updates and no FFT (a view that stops redrawing)");
        std::vector<uint32_t> cpu (static_cast<size_t> (w) * static_cast<size_t> (h));
        v.render (cpu.data(), w, h);
        // the curve sits at the floor: a line of accent colour in the bottom rows
        int accentRows = 0;
        for (int y = 0; y < h; ++y)
        {
            const uint32_t p = cpu[static_cast<size_t> (y) * static_cast<size_t> (w) + 300];
            accentRows += ((p >> 16) & 255u) > 230u && ((p >> 8) & 255u) < 80u;
        }
        check (accentRows >= 2 && accentRows <= 4 && ! cpu.empty(), "the curve at rest is a line near the bottom (" + std::to_string (accentRows) + " accent rows at x 300)");
        // a long gap (the tab was away): the curve restarts from the floor
        tone.push (inst.ring, 1600);
        tone.push (inst.ring, 1600);
        tone.push (inst.ring, 1600);
        tone.push (inst.ring, 1600);
        now += 5000.0;
        in.nowMs = now;
        v.update (in);
        check (v.curve().maxLevel() > 0.3f, "a tone after a long pause is drawn at once (the first update after a gap starts from the floor with the attack)");
    }
    // ---- the controller on the Macros tab markers -----------------------------------------------------------------------------------------------------------------
    {
        TempDir dir;
        FakePlane plane;
        FakeFrame frame; frame.views = testlayout::kViews;
        CpuCurveRenderer renderer;
        FakeInstance inst2;
        inst2.hasRing = true;
        SynthyBoyController ctl (plane, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &testlayout::pageSet());
        ctl.attach (&inst2);
        frame.setView (vi, true, 90);
        double t = 0.0;
        Tone tn;
        auto step = [&] (double ms, bool sound)
        {
            if (sound)
                tn.push (inst2.ring, static_cast<int> (ms * kRate / 1000.0));
            else
            {
                std::vector<float> z (static_cast<size_t> (ms * kRate / 1000.0), 0.0f);
                inst2.ring.push (z.data(), z.data(), static_cast<int> (z.size()));
            }
            t += ms;
            ctl.tick (t);
        };
        for (int i = 0; i < 12; ++i)
            step (33.3, false);
        check (plane.planeOn && ctl.state() == "visible" && ctl.currentView() != nullptr && std::string (ctl.currentView()->name()) == "spectrum", "the markers of the Macros tab put the spectrum view on screen, in silence too (the floor)");
        check (plane.lastRect == uiRectToPanel (90, { lv.region.x, lv.region.y, lv.region.w, lv.region.h }, 800, 1280), "the plane is exactly over the row of the meters");
        const int presentedAtRest = plane.presented;
        for (int i = 0; i < 90; ++i)
            step (33.3, false);
        check (plane.presented == presentedAtRest, "silence for 3 s: no picture presented");
        const int before = plane.presented;
        for (int i = 0; i < 60; ++i)
            step (33.3, true);
        const int pictures = plane.presented - before;
        check (pictures >= 40 && pictures <= 62, "a held tone for 2 s: " + std::to_string (pictures) + " pictures (about 30 a second)");
        const int mid = plane.presented;
        for (int i = 0; i < 200; ++i)
            step (33.3, false);
        const int after = plane.presented;
        for (int i = 0; i < 60; ++i)
            step (33.3, false);
        check (after > mid && plane.presented == after, "the note ends: the curve falls (pictures) and then stops (none in the next 2 s)");
        const std::string st = dir.read ("status.txt");
        check (st.find ("view: spectrum") != std::string::npos && st.find ("spectrum_ffts_total:") != std::string::npos && st.find ("markers_read_spectrum:") != std::string::npos, "the status file names the view and has its counters");
        // another tab: the markers go, the plane goes
        frame.setView (vi, false, 90);
        for (int i = 0; i < 6; ++i)
            step (33.3, false);
        check (! plane.planeOn, "the markers gone (another tab): the plane is off");
    }

    std::printf ("%s: %d failure(s)\n", g_fails == 0 ? "spectrum_view_host: OK" : "spectrum_view_host: FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
