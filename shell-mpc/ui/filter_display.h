// SynthyBoy - MPC UI component library: the FILTER DISPLAY and filter screens.
// FilterDisplay: frequency response curve on log-frequency axis (20 Hz - 20 kHz) with interactive cutoff/reso handle,
// dual-filter curves (F1 cyan, F2 magenta, resultant white), live voice markers; Formant mode XY-pad with vowel markers.
// (The filter page itself holds no control: overlay/filter_ui.h keeps its parameter rows and the geometry of the native cells.)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "core.h"
#include "native_apertures.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================
/** FilterDisplay: the response curve on a log frequency axis (20 Hz .. 20 kHz) with thin lines, or the Formant XY pad.
    The curves are what the filter really does (shell-mpc/overlay/curve_source.h: the engine's own filter run on an impulse); one filter: its line with a faint fill, both filters: F1 and F2 and the
    resultant (white). Interactive: the point on the curve is the cutoff (x) and the resonance (y), or the formant X / Y; while a finger drags it its values are written next to it. Touch target >= 56 px.
    NEVER STORE THE TRUTH: the display draws only `state` (the owner sets it on every update) and writes none of it. A touch reports the position of the finger as two changes, X first (`Value`, `id` = the id of the cutoff,
    `value` = the position on the log axis 0 .. 1, `hzOfNorm (value)` is the frequency) and Y second, taken with `takePending` (`id` = the id of the resonance, `value` = 0 .. 1): in the dual mode `index` of both is the filter
    whose handle the finger holds (1 or 2; the nearer handle at touch down), else 0. The owner writes the parameters; the next update shows the handle there. Private and transient: the handle held and the last position reported. */
class FilterDisplay : public Component
{
public:
    static constexpr int kMaxCurve = 256;
    static constexpr double kFMin = 20.0, kFMax = 20000.0;
    static constexpr double kDbTop = 30.0, kDbBottom = -48.0;   // the bottom of the plot is -48 dB: the curve leaves it through the frame (cut at the edge), it never runs along it

    struct State
    {
        bool dual = false;
        int activeFilter = 1; // 1 or 2
        bool isFormant = false;

        // Curve data (dB values)
        const float* curve1 = nullptr;
        int numPoints1 = 0;
        const float* curve2 = nullptr;
        int numPoints2 = 0;
        const float* curveResult = nullptr;
        int numPointsResult = 0;

        // Cutoff & resonance of active filter
        double cutoffHz = 12000.0;
        double resonancePct = 20.0;

        // Dual mode: cutoff & resonance of both filters
        double cutoffHz1 = 12000.0;
        double resonancePct1 = 20.0;
        double cutoffHz2 = 12000.0;
        double resonancePct2 = 20.0;
        bool filter1On = true;
        bool filter2On = true;

        // Modulated live values
        double liveCutoffHz = -1.0;
        double liveResoPct = -1.0;

        // Voice readouts
        int numVoices = 0;
        float voiceCutoffs[8] = {};

        // Formant parameters (0 .. 100 %)
        double formantX = 50.0;
        double formantY = 50.0;
        double liveFormantX = -1.0;
        double liveFormantY = -1.0;
    };
    State state;

    FilterDisplay() = default;

    void assignIds (int idCutoff, int idReso, int idFormantX, int idFormantY) noexcept
    {
        idCutoff_ = idCutoff;
        idReso_ = idReso;
        idFormantX_ = idFormantX;
        idFormantY_ = idFormantY;
    }

    Size minSize() const override { return { 240, 140 }; }

    double cutoffHzFor (int f) const noexcept
    {
        return state.dual ? (f == 2 ? state.cutoffHz2 : state.cutoffHz1) : state.cutoffHz;
    }
    double cutoffNorm() const noexcept
    {
        const double cut = state.dual ? (state.activeFilter == 2 ? state.cutoffHz2 : state.cutoffHz1) : state.cutoffHz;
        return std::clamp (std::log10 (std::clamp (cut, kFMin, kFMax) / kFMin) / 3.0, 0.0, 1.0);
    }
    double resonanceNorm() const noexcept
    {
        const double res = state.dual ? (state.activeFilter == 2 ? state.resonancePct2 : state.resonancePct1) : state.resonancePct;
        return std::clamp (res / 100.0, 0.0, 1.0);
    }
    double formantXNorm() const noexcept
    {
        return std::clamp (state.formantX / 100.0, 0.0, 1.0);
    }
    double formantYNorm() const noexcept
    {
        return std::clamp (state.formantY / 100.0, 0.0, 1.0);
    }

    /** The plot area inside the block: a margin at the left for the dB scale and at the bottom for the frequencies (the formant pad uses the whole inside). */
    void placeGraph (Rect graph, Rect power, Rect expand)
    {
        headerCorners_ = { power, expand };
        place (graph);
    }

    Rect plot() const noexcept
    {
        Rect in = rect_.inset (8);
        if (! headerCorners_[0].empty())
        {
            in.y += 56;
            in.h = std::max (0, in.h - 56);
        }
        return state.isFormant ? in : Rect { in.x + 30, in.y + 14, std::max (0, in.w - 30 - 6), std::max (0, in.h - 14 - 20) };
    }
    /** The x of a frequency inside the plot. */
    static float xOfHz (double hz, const Rect& pl) noexcept
    {
        return static_cast<float> (pl.x) + static_cast<float> (std::clamp (std::log10 (std::clamp (hz, kFMin, kFMax) / kFMin) / 3.0, 0.0, 1.0) * pl.w);
    }

    /** The y on a curve at a given frequency inside the plot. */
    static float yOnCurve (const float* curve, int numPoints, double hz, const Rect& in) noexcept
    {
        if (! curve || numPoints < 2)
            return static_cast<float> (in.bottom()) - static_cast<float> (0.2 * in.h);
        const double norm = std::clamp (std::log10 (std::clamp (hz, kFMin, kFMax) / kFMin) / 3.0, 0.0, 1.0);
        const double fIdx = norm * static_cast<double> (numPoints - 1);
        const int i0 = std::clamp (static_cast<int> (fIdx), 0, numPoints - 2);
        const double frac = fIdx - static_cast<double> (i0);
        const double db = static_cast<double> (curve[i0]) * (1.0 - frac) + static_cast<double> (curve[i0 + 1]) * frac;
        return dbToY (db, in);
    }

    /** Center of the interactive handle on screen. */
    void handlePoint (float& hx, float& hy) const noexcept
    {
        handlePointFor (state.activeFilter, hx, hy);
    }

    void handlePointFor (int f, float& hx, float& hy) const noexcept
    {
        const Rect in = plot();
        if (state.isFormant)
        {
            hx = static_cast<float> (in.x) + static_cast<float> (formantXNorm() * in.w);
            hy = static_cast<float> (in.bottom()) - static_cast<float> (formantYNorm() * in.h);
        }
        else
        {
            const double cut = state.dual ? (f == 2 ? state.cutoffHz2 : state.cutoffHz1) : state.cutoffHz;
            const float* cData = f == 2 ? state.curve2 : state.curve1;
            const int cLen = f == 2 ? state.numPoints2 : state.numPoints1;
            hx = xOfHz (cut, in);
            hy = yOnCurve (cData, cLen, cut, in);
        }
    }
    bool dragging() const noexcept { return dragging_; }

    /** The frequency of a position on the log axis (0 .. 1: 20 Hz .. 20 kHz): what `value` of the cutoff change means. */
    static double hzOfNorm (double norm) noexcept { return kFMin * std::pow (10.0, std::clamp (norm, 0.0, 1.0) * 3.0); }

    /** The second change of the last touch (the Y of the finger: resonance or formant Y) after a `touchDown` or `touchMove` that returned the first: false when there is none. */
    bool takePending (Change& c) noexcept
    {
        if (! hasPending_)
            return false;
        c = pending_;
        hasPending_ = false;
        return true;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        // Curve polylines are emitted directly through SceneBuilder; pass the
        // inactive look to the Pen so their chromatic strokes are greyed too.
        Pen pen (sb, enabled_ || inactive_, inactive_);
        paintAroundNativeCells (sb, rect_, headerCorners_, [&] { drawNativeGraphFrame (pen, rect_); });
        Cell cell (sb, rect_);
        const Rect in = plot();
        if (in.w < 20 || in.h < 20)
            return;

        if (state.isFormant)
            drawFormantPad (pen, in);
        else
            drawFilterCurve (pen, sb, in);
    }

    int touchTargets (Rect* out, int max) const override
    {
        if (max > 0)
        {
            out[0] = rect_;
            return 1;
        }
        return 0;
    }

protected:
    void drawBody (Pen&) const override {}

    /** The colour of filter f (1, 2): the two ends of the signal gradient. */
    static uint32_t filterColour (int f) noexcept { return f == 2 ? theme::L().flow[3] : theme::L().flow[0]; }
    /** The colour of the response of a filter that is switched off: the filter's own colour most of the way to the ground. */
    static uint32_t dimmed (uint32_t c) noexcept { return theme::mix (c, theme::L().panel0, 0.72f); }
    static uint32_t inactiveRgb (uint32_t c) noexcept { return Pen::inactiveGrey (colour::rgba (c, 255)) >> 8; }

    void drawFilterCurve (Pen& p, gpu::SceneBuilder& sb, const Rect& in) const
    {
        const theme::Look& T = theme::L();
        char buf[24];

        // 1. The grid: the frequency decades (100 Hz, 1 kHz, 10 kHz) and the dB lines (+24, 0, -24) with their scale
        static const struct { double hz; const char* name; } kDecade[3] = { { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } };
                for (double hz : { 50.0, 200.0, 500.0, 2000.0, 5000.0 })
        {
            const float gx = xOfHz (hz, in);
            p.line (gx, static_cast<float> (in.y), gx, static_cast<float> (in.bottom()), 1.0f, p.c (T.grid, 90));
        }
        for (const auto& d : kDecade)
        {
            const float gx = xOfHz (d.hz, in);
            p.line (gx, static_cast<float> (in.y), gx, static_cast<float> (in.bottom()), 1.0f, p.c (T.grid, 255));
            p.textCentered ({ static_cast<int> (gx) - 24, in.bottom() + 3, 48, 16 }, d.name, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textLeft ({ in.x + 2, in.bottom() + 3, 40, 16 }, "20 Hz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        p.textRight ({ in.right() - 52, in.bottom() + 3, 50, 16 }, "20 kHz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        static const double kDb[4] = { 24.0, 0.0, -24.0, -48.0 };
        for (double db : kDb)
        {
            const float gy = dbToY (db, in);
            if (db > kDbBottom)
                p.line (static_cast<float> (in.x), gy, static_cast<float> (in.right()), gy, 1.0f, p.c (T.grid, db == 0.0 ? 255 : 150));
            std::snprintf (buf, sizeof buf, "%+.0f", db);
            p.textRight ({ in.x - 34, std::min (static_cast<int> (gy) - 8, in.bottom() - 12), 30, 16 }, db == 0.0 ? "0 dB" : buf, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }

        // 2. The curves (thin crisp lines, labelled F1 / F2, resulting curve as main curve)
        if (state.dual)
        {
            if (state.curve1 && state.numPoints1 >= 2)
                drawCurvePolyline (sb, in, state.curve1, state.numPoints1, inactive_ ? inactiveRgb (filterColour (1)) : (state.filter1On ? filterColour (1) : dimmed (filterColour (1))), 0, 0.6f);
            if (state.curve2 && state.numPoints2 >= 2)
                drawCurvePolyline (sb, in, state.curve2, state.numPoints2, inactive_ ? inactiveRgb (filterColour (2)) : (state.filter2On ? filterColour (2) : dimmed (filterColour (2))), 0, 0.6f);
            if (state.curveResult && state.numPointsResult >= 2)
                drawCurvePolyline (sb, in, state.curveResult, state.numPointsResult, inactive_ ? inactiveRgb (T.text) : T.text, 0, 1.0f);
            // the legend of the three lines
            const struct { const char* name; uint32_t col; } kLegend[3] = { { "F1", filterColour (1) }, { "F2", filterColour (2) }, { "F1+F2", T.text } };
            int lx = in.x + 8;
            for (const auto& l : kLegend)
            {
                const int w = 26 + static_cast<int> (Pen::textWidth (l.name, T.fLabel - 2.0f));
                p.line (static_cast<float> (lx), static_cast<float> (in.y + 11), static_cast<float> (lx + 18), static_cast<float> (in.y + 11), 2.0f, p.c (l.col, 255));
                p.textLeft ({ lx + 24, in.y + 3, w, 16 }, l.name, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
                lx += w + 14;
            }
        }
        else
        {
            const float* cData = state.activeFilter == 2 ? state.curve2 : state.curve1;
            const int cLen = state.activeFilter == 2 ? state.numPoints2 : state.numPoints1;
            // a filter that is switched off: the response of its parameters (what switching it on will do) drawn dim, without fill, and the name says OFF; never a bright line that contradicts the knobs
            const bool on = state.activeFilter == 2 ? state.filter2On : state.filter1On;
            if (cData && cLen >= 2)
                drawCurvePolyline (sb, in, cData, cLen, inactive_ ? inactiveRgb (filterColour (state.activeFilter)) : (on ? filterColour (state.activeFilter) : dimmed (filterColour (state.activeFilter))), inactive_ ? 0 : (on ? 34 : 0), on ? 1.0f : 0.7f);   // (`stroke` takes the half width: the line is 2 px, as the curves of the LFO and the FX displays)
            if (headerCorners_[0].empty())
            {
                std::snprintf (buf, sizeof buf, on ? "FILTER %d" : "FILTER %d  OFF", state.activeFilter);
                p.textLeft ({ in.x + 8, in.y + 3, 160, 16 }, buf, T.fLabel - 2.0f, p.c (on ? T.textDim : T.textFaint, 255), T.fMin);
            }
        }

        // 3. The voices: where each sounding voice has its cutoff (up to 8, faint)
        for (int v = 0; v < state.numVoices && v < 8; ++v)
        {
            const float hz = state.voiceCutoffs[v];
            if (hz >= static_cast<float> (kFMin) && hz <= static_cast<float> (kFMax))
            {
                const float vx = xOfHz (hz, in);
                p.line (vx, static_cast<float> (in.y + 4), vx, static_cast<float> (in.bottom() - 4), 1.0f, p.c (T.textDim, 90));
            }
        }
        // 4. The modulated live cutoff of the voice that is heard
        if (state.liveCutoffHz >= kFMin && state.liveCutoffHz <= kFMax)
        {
            const float lx = xOfHz (state.liveCutoffHz, in);
            p.line (lx, static_cast<float> (in.y + 2), lx, static_cast<float> (in.bottom() - 2), 1.2f, p.c (T.flow[1], 200));
        }

        // 5. The cutoff handles on their curves
        if (state.dual)
        {
            float hx1 = 0.0f, hy1 = 0.0f, hx2 = 0.0f, hy2 = 0.0f;
            handlePointFor (1, hx1, hy1);
            handlePointFor (2, hx2, hy2);
            const uint32_t col1 = filterColour (1);
            const uint32_t col2 = filterColour (2);
            p.disc (hx1, hy1, 4.0f, p.c (col1, 255));
            p.arc (hx1, hy1, 8.5f, 0.0f, 2.0f * kPi, 1.6f, p.c (col1, 220));
            p.disc (hx2, hy2, 4.0f, p.c (col2, 255));
            p.arc (hx2, hy2, 8.5f, 0.0f, 2.0f * kPi, 1.6f, p.c (col2, 220));

            if (dragging_)
            {
                const double curCut = state.activeFilter == 2 ? state.cutoffHz2 : state.cutoffHz1;
                const double curRes = state.activeFilter == 2 ? state.resonancePct2 : state.resonancePct1;
                const float curHx = state.activeFilter == 2 ? hx2 : hx1;
                const float curHy = state.activeFilter == 2 ? hy2 : hy1;
                char hz[16], text[40];
                if (curCut >= 1000.0)
                    std::snprintf (hz, sizeof hz, "%.2f kHz", curCut / 1000.0);
                else
                    std::snprintf (hz, sizeof hz, "%.0f Hz", curCut);
                std::snprintf (text, sizeof text, "F%d: %s   RESO %.0f%%", state.activeFilter, hz, curRes);
                bubble (p, in, curHx, curHy, text);
            }
        }
        else
        {
            float hx = 0.0f, hy = 0.0f;
            handlePoint (hx, hy);
            const uint32_t handleCol = filterColour (state.activeFilter);
            p.disc (hx, hy, 4.0f, p.c (handleCol, 255));
            p.arc (hx, hy, 8.5f, 0.0f, 2.0f * kPi, 1.6f, p.c (handleCol, 220));
            if (dragging_)
            {
                char hz[16], text[40];
                if (state.cutoffHz >= 1000.0)
                    std::snprintf (hz, sizeof hz, "%.2f kHz", state.cutoffHz / 1000.0);
                else
                    std::snprintf (hz, sizeof hz, "%.0f Hz", state.cutoffHz);
                std::snprintf (text, sizeof text, "%s   RESO %.0f%%", hz, state.resonancePct);
                bubble (p, in, hx, hy, text);
            }
        }
    }

    void drawFormantPad (Pen& p, const Rect& in) const
    {
        const theme::Look& T = theme::L();

        // Crosshairs in center
        const float midX = static_cast<float> (in.x) + in.w * 0.5f;
        const float midY = static_cast<float> (in.y) + in.h * 0.5f;
        p.line (static_cast<float> (in.x), midY, static_cast<float> (in.right()), midY, 1.0f, p.c (T.grid, 255));
        p.line (midX, static_cast<float> (in.y), midX, static_cast<float> (in.bottom()), 1.0f, p.c (T.grid, 255));

        // 5 Vowel markers: I, E, A, O, U
        struct Vowel { const char* name; float x, y; };
        static const Vowel vowels[5] = {
            { "I", 0.15f, 0.82f },
            { "E", 0.35f, 0.62f },
            { "A", 0.62f, 0.85f },
            { "O", 0.72f, 0.30f },
            { "U", 0.22f, 0.20f }
        };

        for (const Vowel& v : vowels)
        {
            const float vx = static_cast<float> (in.x) + v.x * in.w;
            const float vy = static_cast<float> (in.bottom()) - v.y * in.h;
            p.arc (vx, vy, 13.0f, 0.0f, 2.0f * kPi, 1.0f, p.c (T.textDim, 120));
            p.textCentered ({ static_cast<int> (vx) - 12, static_cast<int> (vy) - 12, 24, 24 }, v.name, T.fLabel, p.c (T.text, 200));
        }

        // Live formant puck if modulated
        if (state.liveFormantX >= 0.0 && state.liveFormantY >= 0.0)
        {
            const float lx = static_cast<float> (in.x) + static_cast<float> (state.liveFormantX * 0.01 * in.w);
            const float ly = static_cast<float> (in.bottom()) - static_cast<float> (state.liveFormantY * 0.01 * in.h);
            p.disc (lx, ly, 3.0f, p.c (T.flow[1], 180));
            p.arc (lx, ly, 7.0f, 0.0f, 2.0f * kPi, 1.0f, p.c (T.flow[1], 140));
        }

        // Interactive puck handle
        float hx = 0.0f, hy = 0.0f;
        handlePoint (hx, hy);
        const uint32_t col = filterColour (state.activeFilter);
        p.disc (hx, hy, 5.0f, p.c (col, 255));
        p.arc (hx, hy, 12.0f, 0.0f, 2.0f * kPi, 1.8f, p.c (col, 220));
        if (dragging_)
        {
            char text[40];
            std::snprintf (text, sizeof text, "X %.0f%%   Y %.0f%%", state.formantX, state.formantY);
            bubble (p, in, hx, hy, text);
        }

        p.textLeft ({ in.x + 4, in.y + 2, 140, 16 }, "FORMANT  X / Y", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
    }

    /** The value of the dragged point as a small box beside it (on the side that has room). */
    void bubble (Pen& p, const Rect& in, float hx, float hy, const char* text) const
    {
        const theme::Look& T = theme::L();
        const float tw = Pen::textWidth (text, T.fValue - 4.0f);
        const int w = static_cast<int>(std::ceil (tw)) + 20, h = 30;
        int x = static_cast<int> (hx) + 18, y = static_cast<int> (hy) - h - 12;
        if (x + w > in.right())
            x = static_cast<int> (hx) - 18 - w;
        if (y < in.y)
            y = static_cast<int> (hy) + 14;
        const Rect box { std::max (in.x, x), std::min (std::max (in.y, y), in.bottom() - h), w, h };
        p.control (box, false, true);
        p.textCentered (box.inset (2), text, T.fValue - 4.0f, p.c (T.text, 255), T.fMin);
    }

    static float dbToY (double db, const Rect& in) noexcept
    {
        const double y = in.y + (kDbTop - db) * static_cast<double> (in.h) / (kDbTop - kDbBottom);
        return static_cast<float> (std::clamp (y, static_cast<double> (in.y), static_cast<double> (in.bottom())));
    }

    /** The curve cut at the frame: every stretch of the curve inside the plot (-48 .. +30 dB) is drawn as a line of its own that ends exactly on the edge it leaves through, so a deep notch or a fall below the
        floor runs out of the picture instead of lying along its bottom. The fill under a stretch goes down to the bottom of the plot. */
    static void drawCurvePolyline (gpu::SceneBuilder& sb, const Rect& in, const float* dbData, int nPoints, uint32_t col, int fillAlpha, float strokeWidth)   // the half width of the line, in pixels
    {
        if (nPoints < 2 || in.w < 4)
            return;
        const int n = std::min (nPoints, gpu::kMaxCurve);
        const double xStep = static_cast<double> (in.w) / static_cast<double> (n - 1);
        auto inside = [&] (double db) { return db >= kDbBottom && db <= kDbTop; };
        auto edgeOf = [&] (double db) { return db < kDbBottom ? kDbBottom : kDbTop; };
        int i = 0;
        while (i < n)
        {
            if (! inside (static_cast<double> (dbData[i])))
            {
                ++i;
                continue;
            }
            int j = i;   // the stretch i .. j is inside
            while (j + 1 < n && inside (static_cast<double> (dbData[j + 1])))
                ++j;
            double xa = static_cast<double> (in.x) + xStep * i, xb = static_cast<double> (in.x) + xStep * j;
            double dbA = static_cast<double> (dbData[i]), dbB = static_cast<double> (dbData[j]);
            if (i > 0)   // it came in through an edge: start where the line crosses it
            {
                const double d0 = static_cast<double> (dbData[i - 1]), d1 = dbA, e = edgeOf (d0);
                const double t = d1 != d0 ? (e - d0) / (d1 - d0) : 1.0;
                xa = static_cast<double> (in.x) + xStep * (i - 1 + std::clamp (t, 0.0, 1.0));
                dbA = e;
            }
            if (j + 1 < n)   // it leaves through an edge
            {
                const double d0 = dbB, d1 = static_cast<double> (dbData[j + 1]), e = edgeOf (d1);
                const double t = d1 != d0 ? (e - d0) / (d1 - d0) : 0.0;
                xb = static_cast<double> (in.x) + xStep * (j + std::clamp (t, 0.0, 1.0));
                dbB = e;
            }
            // the stretch as evenly spaced rows between xa and xb (a row about every 2 pixels at most)
            const int w = std::max (1, static_cast<int> (std::lround (xb - xa)));
            const int m = std::clamp (static_cast<int> (std::ceil (static_cast<double> (w) / xStep)) + 1, 2, gpu::kMaxCurve);
            float rows[gpu::kMaxCurve];
            for (int k = 0; k < m; ++k)
            {
                const double x = xa + (xb - xa) * static_cast<double> (k) / static_cast<double> (m - 1);
                const double f = std::clamp ((x - static_cast<double> (in.x)) / xStep, 0.0, static_cast<double> (n - 1));
                const int f0 = std::min (static_cast<int> (f), n - 2);
                const double u = f - static_cast<double> (f0);
                double db = static_cast<double> (dbData[f0]) * (1.0 - u) + static_cast<double> (dbData[f0 + 1]) * u;
                if (k == 0) db = dbA;
                if (k == m - 1) db = dbB;
                rows[k] = dbToY (db, in);
            }
            if (sb.curve (rows, m, static_cast<int> (std::lround (xa)), w) >= 0)
            {
                if (fillAlpha > 0)
                    sb.fill (static_cast<float> (in.bottom()), colour::rgba (col, static_cast<uint32_t> (fillAlpha)));
                sb.stroke (strokeWidth, colour::rgba (col, 255));
            }
            i = j + 1;
        }
    }

    Change onDown (int x, int y, double /*nowMs*/) override
    {
        if (! headerCorners_[0].empty() && y < headerCorners_[0].bottom())
            return Change::none();
        dragging_ = true;
        grabbed_ = state.isFormant ? 0 : state.activeFilter;
        if (state.dual && ! state.isFormant)
        {
            float hx1 = 0.0f, hy1 = 0.0f, hx2 = 0.0f, hy2 = 0.0f;
            handlePointFor (1, hx1, hy1);
            handlePointFor (2, hx2, hy2);
            const float dx1 = static_cast<float> (x) - hx1, dy1 = static_cast<float> (y) - hy1;
            const float dx2 = static_cast<float> (x) - hx2, dy2 = static_cast<float> (y) - hy2;
            const float distSq1 = dx1 * dx1 + dy1 * dy1;
            const float distSq2 = dx2 * dx2 + dy2 * dy2;
            grabbed_ = distSq2 < distSq1 ? 2 : 1;   // the handle the finger holds (gesture data; `state.activeFilter` is the owner's, it follows the change)
        }
        return updateTouch (x, y, true);
    }

    Change onMove (int x, int y) override
    {
        return dragging_ ? updateTouch (x, y, false) : Change::none();
    }

    Change onUp (int /*x*/, int /*y*/) override
    {
        dragging_ = false;
        hasPending_ = false;
        return Change::none();
    }

    /** The finger at (x, y) as changes: X (returned) and Y (`takePending`). A finger down reports both (it also names the filter whose handle it holds); a move reports only the axis that moved since the last report. */
    Change updateTouch (int x, int y, bool first)
    {
        hasPending_ = false;
        const Rect in = plot();
        if (in.w <= 0 || in.h <= 0)
            return Change::none();

        const double normX = std::clamp (static_cast<double> (x - in.x) / static_cast<double> (in.w), 0.0, 1.0);
        const double normY = std::clamp (static_cast<double> (in.bottom() - y) / static_cast<double> (in.h), 0.0, 1.0);
        const bool xMoved = first || normX != lastX_;
        const bool yMoved = first || normY != lastY_;
        if (! xMoved && ! yMoved)
            return Change::none();
        lastX_ = normX;
        lastY_ = normY;

        Change cx = Change::normalised (state.isFormant ? idFormantX_ : idCutoff_, normX);
        Change cy = Change::normalised (state.isFormant ? idFormantY_ : idReso_, normY);
        cx.index = cy.index = grabbed_;
        if (xMoved && yMoved)
        {
            pending_ = cy;
            hasPending_ = true;
            return cx;
        }
        return xMoved ? cx : cy;
    }

private:
    int idCutoff_ = 0, idReso_ = 0;
    int idFormantX_ = 0, idFormantY_ = 0;
    std::array<Rect, 2> headerCorners_ {};
    // gesture data (not the value): the finger is down, the handle it holds (1, 2, 0 = formant), the position of the last report, the second change of a touch
    bool dragging_ = false;
    int grabbed_ = 0;
    double lastX_ = 0.0, lastY_ = 0.0;
    Change pending_;
    bool hasPending_ = false;
};

}} // namespace synthyboy_mpc::ui
