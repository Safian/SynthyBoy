// SynthyBoy - MPC UI component library: the FILTER DISPLAY and filter screens.
// FilterDisplay: frequency response curve on log-frequency axis (20 Hz - 20 kHz) with interactive cutoff/reso handle,
// dual-filter curves (F1 cyan, F2 magenta, resultant white), live voice markers; Formant mode XY-pad with vowel markers.
// FilterRoutePanel: route popup (destination, bus 1/2 sends, level, pan).
// FilterFullScreen: the page of a filter (the whole tab).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "composite.h"
#include "led_list.h"
#include "led_toggle.h"
#include "list.h"
#include "panel.h"
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================
/** FilterDisplay: the response curve on a log frequency axis (20 Hz .. 20 kHz) with thin lines, or the Formant XY pad.
    The curves are what the filter really does (shell-mpc/overlay/curve_source.h: the engine's own filter run on an impulse); one filter: its line with a faint fill, both filters: F1 and F2 and the
    resultant (white). Interactive: the point on the curve is the cutoff (x) and the resonance (y), or the formant X / Y; while a finger drags it its values are written next to it. Touch target >= 56 px. */
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
    Rect plot() const noexcept
    {
        const Rect in = rect_.inset (8);
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

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen pen (sb, enabled_);
        Cell cell (sb, rect_);
        pen.block (rect_, false);
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
                drawCurvePolyline (sb, in, state.curve1, state.numPoints1, filterColour (1), 0, 1.0f);
            if (state.curve2 && state.numPoints2 >= 2)
                drawCurvePolyline (sb, in, state.curve2, state.numPoints2, filterColour (2), 0, 1.0f);
            if (state.curveResult && state.numPointsResult >= 2)
                drawCurvePolyline (sb, in, state.curveResult, state.numPointsResult, T.text, 0, 1.8f);
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
            if (cData && cLen >= 2)
                drawCurvePolyline (sb, in, cData, cLen, filterColour (state.activeFilter), 34, 1.8f);
            std::snprintf (buf, sizeof buf, "FILTER %d", state.activeFilter);
            p.textLeft ({ in.x + 8, in.y + 3, 120, 16 }, buf, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
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
    static void drawCurvePolyline (gpu::SceneBuilder& sb, const Rect& in, const float* dbData, int nPoints, uint32_t col, int fillAlpha, float strokeWidth)
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
        dragging_ = true;
        if (state.dual && ! state.isFormant)
        {
            float hx1 = 0.0f, hy1 = 0.0f, hx2 = 0.0f, hy2 = 0.0f;
            handlePointFor (1, hx1, hy1);
            handlePointFor (2, hx2, hy2);
            const float dx1 = static_cast<float> (x) - hx1, dy1 = static_cast<float> (y) - hy1;
            const float dx2 = static_cast<float> (x) - hx2, dy2 = static_cast<float> (y) - hy2;
            const float distSq1 = dx1 * dx1 + dy1 * dy1;
            const float distSq2 = dx2 * dx2 + dy2 * dy2;
            state.activeFilter = distSq2 < distSq1 ? 2 : 1;
        }
        return updateTouch (x, y);
    }

    Change onMove (int x, int y) override
    {
        return updateTouch (x, y);
    }

    Change onUp (int /*x*/, int /*y*/) override
    {
        dragging_ = false;
        return Change::none();
    }

    Change updateTouch (int x, int y)
    {
        const Rect in = plot();
        if (in.w <= 0 || in.h <= 0)
            return Change::none();

        const double normX = std::clamp (static_cast<double> (x - in.x) / static_cast<double> (in.w), 0.0, 1.0);
        const double normY = std::clamp (static_cast<double> (in.bottom() - y) / static_cast<double> (in.h), 0.0, 1.0);

        if (state.isFormant)
        {
            state.formantX = normX * 100.0;
            state.formantY = normY * 100.0;
            return Change::normalised (idFormantX_, normX);
        }
        else
        {
            const double newCut = kFMin * std::pow (10.0, normX * 3.0);
            const double newRes = normY * 100.0;
            if (state.dual)
            {
                if (state.activeFilter == 2)
                {
                    state.cutoffHz2 = newCut;
                    state.resonancePct2 = newRes;
                }
                else
                {
                    state.cutoffHz1 = newCut;
                    state.resonancePct1 = newRes;
                }
            }
            state.cutoffHz = newCut;
            state.resonancePct = newRes;
            return Change::normalised (idCutoff_, normX);
        }
    }

private:
    int idCutoff_ = 0, idReso_ = 0;
    int idFormantX_ = 0, idFormantY_ = 0;
    bool dragging_ = false;
};

// ===============================================================================================================================
/** FilterRoutePanel: route popup for a filter instance (destination, bus 1/2 sends, level, pan).
    Popup modal with dimmed backdrop. Touch targets >= 56 px. */
class FilterRoutePanel : public Composite
{
public:
    Stepper destination;
    Knob bus1 { "BUS 1" }, bus2 { "BUS 2" };
    Knob level { "LEVEL" }, pan { "PAN" };
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    QGroup group;

    FilterRoutePanel()
    {
        pan.state.bipolar = true;
        pan.state.defaultValue = 0.5;
        pan.state.value = pan.state.modValue = 0.5;
        level.state.defaultValue = 0.833; // ~0 dB
        addKid (destination);
        addKid (close);
        addKid (bus1);
        addKid (bus2);
        addKid (level);
        addKid (pan);
        setPrimary (&destination);
        group.set (0, &destination);
        group.set (1, &bus1);
        group.set (2, &bus2);
        group.set (3, &level);
    }

    Size minSize() const override { return { 380, 220 }; }

    void setPopup (bool popup, const Rect& parent)
    {
        popup_ = popup;
        parent_ = parent;
        backdropTap_ = false;
    }
    bool popup() const noexcept { return popup_; }
    void setBackdropId (int id) noexcept { backdropId_ = id; }
    int backdropId() const noexcept { return backdropId_; }

    void draw (gpu::SceneBuilder& sb) const override
    {
        if (popup_)
        {
            Pen pen (sb, true);
            Cell cell (sb, parent_);
            pen.rect (parent_, colour::rgba (theme::L().bg1, 190));
            pen.block (rect_, true);
        }
        Composite::draw (sb);
        if (popup_)
            group.draw (sb);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        backdropTap_ = false;
        if (popup_ && enabled_ && ! rect_.contains (x, y) && parent_.contains (x, y))
        {
            backdropTap_ = true;
            tap_.down (x, y, nowMs);
            cap_ = nullptr;
            return Change::none();
        }
        return Composite::touchDown (x, y, nowMs);
    }
    Change touchMove (int x, int y) override
    {
        if (backdropTap_)
        {
            tap_.move (x, y);
            return Change::none();
        }
        return Composite::touchMove (x, y);
    }
    Change touchUp (int x, int y) override
    {
        if (backdropTap_)
        {
            backdropTap_ = false;
            return tap_.up() != 0 ? Change::request (backdropId_) : Change::none();
        }
        return Composite::touchUp (x, y);
    }

protected:
    void layout() override
    {
        const int m = 8;
        const int rowH = 52;
        close.place ({ rect_.right() - m - 84, rect_.y + m, 84, rowH });
        destination.place ({ rect_.x + m, rect_.y + m, rect_.w - 2 * m - 84 - kGap, rowH });

        const int ky = rect_.y + m + rowH + kGap;
        const int kh = std::max (60, rect_.bottom() - m - ky);
        Rect k[4];
        const int f4[4] = { 0, 0, 0, 0 }, w4[4] = { 1, 1, 1, 1 };
        layoutRow ({ rect_.x + m, ky, rect_.w - 2 * m, kh }, f4, w4, 4, kGap, k);
        bus1.place (k[0]);
        bus2.place (k[1]);
        level.place (k[2]);
        pan.place (k[3]);
    }
    void drawExtra (Pen& p) const override
    {
        if (popup_)
            p.rrect (rect_, theme::L().rBlock, p.c (theme::L().flow[0], 255), 1.6f);
    }

    bool popup_ = false, backdropTap_ = false;
    Rect parent_;
    int backdropId_ = 0;
    TapGesture tap_;
};

// ===============================================================================================================================
/** FilterKnob: a knob that, when disabled, dims the dial and its value text consistently. */
class FilterKnob : public Knob
{
public:
    using Knob::Knob;

protected:
    void drawBody (Pen& p) const override
    {
        if (enabled_)
        {
            Knob::drawBody (p);
            return;
        }
        const theme::Look& T = theme::L();
        const Geom g = geom();
        const float cx = g.cx, cy = g.cy, R = g.radius, tw = g.stroke;
        const float a0 = angleOf (0.0), a1 = angleOf (1.0), av = angleOf (state.value), am = angleOf (0.5);
        p.style (1.0f, true);
        p.arc (cx, cy, R, a0, a1, g.track, p.c (T.track, 255));
        if (state.bipolar)
            p.line (cx + (R - tw * 0.5f - 2.0f) * std::cos (am), cy + (R - tw * 0.5f - 2.0f) * std::sin (am), cx + (R + tw * 0.5f + 2.0f) * std::cos (am), cy + (R + tw * 0.5f + 2.0f) * std::sin (am), 1.5f, p.c (T.textFaint, 200));
        p.line (cx + R * 0.25f * std::cos (av), cy + R * 0.25f * std::sin (av), cx + R * 0.6f * std::cos (av), cy + R * 0.6f * std::sin (av), 1.6f, p.c (T.textDim, 200));
        float vx, vy;
        valuePoint (vx, vy);
        p.disc (vx, vy, std::max (2.5f, tw * 0.9f), p.c (T.textDim, 200));
        p.plain();
        if (g.labelSize > 0.0f)
        {
            if (state.side)
                p.textLeft (g.labelBox, state.label, g.labelSize, p.c (T.textDim, 255), T.fMin);
            else
                p.textCentered (g.labelBox, state.label, g.labelSize, p.c (T.textDim, 255), T.fMin);
        }
        if (g.valueBox.h > 0)
        {
            if (state.side)
                p.textLeft (g.valueBox, state.valueText, g.valueSize, p.c (T.textDim, 255), T.fMin);
            else
                p.textCentered (g.valueBox, state.valueText, g.valueSize, p.c (T.textDim, 255), T.fMin);
        }
    }
};

/** FilterToggle: a toggle button that dims both the icon and label text when disabled. */
class FilterToggle : public ToggleIcon
{
public:
    using ToggleIcon::ToggleIcon;

protected:
    void drawBody (Pen& p) const override
    {
        if (enabled_)
        {
            ToggleIcon::drawBody (p);
            return;
        }
        const theme::Look& T = theme::L();
        if (state.knobLike)
        {
            const float labelSize = state.label[0] != '\0' ? Pen::fitSize (state.label, static_cast<float> (rect_.w - 4), 20.0f, T.fLabel, T.fMin) : 0.0f;
            const int labelH = labelSize > 0.0f ? static_cast<int> (std::ceil (labelSize * 0.89f)) + 7 : 0, valueH = 20, dialH = rect_.h - labelH - valueH;
            const float d = std::clamp (static_cast<float> (std::min (rect_.w - 8, dialH - 8)), 6.0f, 140.0f);
            const float cx = static_cast<float> (rect_.x) + static_cast<float> (rect_.w) * 0.5f, cy = static_cast<float> (rect_.y) + static_cast<float> (dialH) * 0.5f, s = d * 0.15f;
            p.style (1.0f, true);
            p.arc (cx, cy, s * 1.4f, 0.0f, 2.0f * kPi, 2.4f, p.c (T.textFaint, 200));
            p.plain();
            if (labelSize > 0.0f)
                p.textCentered ({ rect_.x + 2, rect_.bottom() - valueH - labelH + 2, rect_.w - 4, std::max (0, labelH - 4) }, state.label, labelSize, p.c (T.textDim, 255), T.fMin);
            p.textCentered ({ rect_.x + 2, rect_.bottom() - valueH, rect_.w - 4, valueH }, state.on ? "ON" : "OFF", T.fLabel, p.c (T.textDim, 255), T.fMin);
            return;
        }
        const Rect b = rect_.inset (4);
        p.control (b, false);
        char lab[32];
        std::snprintf (lab, sizeof lab, "%s%s", state.label, state.showState ? (state.on ? " ON" : " OFF") : "");
        const int labelH = state.label[0] ? 18 : 0;
        const float cx = static_cast<float> (b.x) + static_cast<float> (b.w) * 0.5f, cy = static_cast<float> (b.y) + static_cast<float> (b.h - labelH) * 0.5f;
        const float s = static_cast<float> (std::min (b.w, b.h - labelH)) * 0.32f;
        if (labelH > 0)
            p.textCentered ({ b.x, b.bottom() - labelH, b.w, labelH }, lab, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        p.style (1.0f, true);
        p.arc (cx, cy, s, 0.0f, 2.0f * kPi, 2.4f, p.c (T.textFaint, 200));
        p.plain();
    }
};

// ===============================================================================================================================
/** FilterFullScreen: the page of a filter (the plugin draws the whole tab; filter_ui.h). From the top: the power switch, the instance selector F1 | F2 | F1+F2, the model with its list, the style, the CLEAN switch and the
    OUTPUT button (where the filter sends its signal); the display (the real response, or the Formant pad with its five knobs at the right); a row of blocks, ONE Q group each:
      MAIN   CUTOFF  RESO  DRIVE  MIX                  TRACK   KEYTRACK  ENV 1  VAR  COMB                   OUT   LEVEL  PAN  BLEND  ROUTING                  FORMANT (Formant model)  X  Y  TRANSPOSE  SPREAD (+ RESONANCE)
    A tap on a member makes its block the Q group; the four Q-Links then turn its members. Every knob shows its value. */
class FilterFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 4;   // MAIN, TRACK, OUT, FORMANT
    static constexpr int kRoutingW = 150;  // the ROUTING list: dot, "Series 1 to 2", padding
    static constexpr int kTopH = 56, kBandH = 214, kBlockTitleH = 34, kBlockPad = 2, kBlockGap = 4, kSlotGap = 0, kBandMargin = 4;   // twelve slots of 100 px (the knobs' minimum) and the ROUTING list in one band: the pads and gaps are as small as they get
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    ToggleIcon power;
    PictogramSelector instances;
    Stepper model, style;
    MenuButton modelList { "", MenuButton::Icon::Down };   // the list of the models
    LedToggle clean;                                       // CLEAN MODE (a LED toggle: flat dot + name)
    RouteButton route { "M" };                             // OUTPUT: where the filter sends its signal (M main effects, D direct, - none)
    FilterDisplay display;

    // MAIN
    FilterKnob cutoff { "CUTOFF" }, resonance { "RESO" }, drive { "DRIVE" }, mix { "MIX" };
    // TRACK
    FilterKnob keytrack { "KEYTRACK" }, var { "VAR" }, combOffset { "COMB" };
    LedToggle env1;   // a LED toggle in its slot of the block
    // OUT
    FilterKnob level { "LEVEL" }, pan { "PAN" }, blend { "BLEND" };
    LedList routing;   // ROUTING: the three ways the two filters are connected, all three visible (the caption is drawn in the title strip of the block)
    // FORMANT
    FilterKnob formantX { "X" }, formantY { "Y" }, formantTrans { "TRANSPOSE" }, formantRes { "RESONANCE" }, formantSpread { "SPREAD" };

    QGroup groups[kMaxGroups]; // 0: MAIN, 1: TRACK, 2: OUT, 3: FORMANT

    FilterFullScreen()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        env1.setLabel ("ENV 1");
        clean.setLabel ("CLEAN");

        instances.setItem (0, -1, "F1");
        instances.setItem (1, -1, "F2");
        instances.setItem (2, -1, "F1+F2");
        instances.state.count = 3;
        instances.state.index = 0;
        model.listable = true;
        style.listable = true;

        pan.state.bipolar = true;
        keytrack.state.bipolar = true;
        formantTrans.state.bipolar = true;
        formantSpread.state.bipolar = true;

        addKid (close);
        addKid (power);
        addKid (instances);
        addKid (model);
        addKid (modelList);
        addKid (style);
        addKid (clean);
        addKid (route);
        addKid (display);
        addKid (cutoff); addKid (resonance); addKid (drive); addKid (mix);
        addKid (keytrack); addKid (env1); addKid (var); addKid (combOffset);
        addKid (level); addKid (pan); addKid (blend); addKid (routing);
        addKid (formantX, false); addKid (formantY, false); addKid (formantTrans, false); addKid (formantRes, false); addKid (formantSpread, false);

        buildGroups();
        setFormantMode (false);
    }

    /** The page of its own tab: there is nothing to close. */
    void setPageMode (bool page)
    {
        pageMode_ = page;
        show (close, ! page);
        if (rect_.w > 0)
            layout();
    }
    /** The Formant model: the display is the XY pad and the FORMANT block sits at its right; VAR and COMB have no meaning there. */
    void setFormantMode (bool isFormant)
    {
        isFormant_ = isFormant;
        display.state.isFormant = isFormant;
        for (Component* c : { static_cast<Component*> (&formantX), static_cast<Component*> (&formantY), static_cast<Component*> (&formantTrans), static_cast<Component*> (&formantRes), static_cast<Component*> (&formantSpread) })
            show (*c, isFormant);
        if (! isFormant && activeGroup_ == 3)
            setActiveGroup (0);
        if (rect_.w > 0)
            layout();
    }
    bool formantMode() const noexcept { return isFormant_; }
    bool pageMode() const noexcept { return pageMode_; }

    Size minSize() const override { return { 1280, 628 }; }
    Rect displayRect() const noexcept { return display.rect(); }
    Rect block (int i) const noexcept { return i == 0 ? mainBlock_ : (i == 1 ? trackBlock_ : (i == 2 ? outBlock_ : formantBlock_)); }

    int numGroups() const noexcept { return kMaxGroups; }
    static const char* groupName (int i) noexcept
    {
        static const char* const kNames[kMaxGroups] = { "MAIN", "TRACK", "OUT", "FORMANT" };
        return i >= 0 && i < kMaxGroups ? kNames[i] : "";
    }
    int activeGroup() const noexcept { return activeGroup_; }
    void setActiveGroup (int i)
    {
        for (int k = 0; k < kMaxGroups; ++k)
            groups[k].setActive (k == i);
        activeGroup_ = i >= 0 && i < kMaxGroups ? i : -1;
    }

    Change turnQ (int knobIndex, double delta)
    {
        return activeGroup_ >= 0 ? groups[activeGroup_].turn (knobIndex, delta) : Change::none();
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
            for (int i = 0; i < kMaxGroups; ++i)
                if (groups[i].rect().contains (x, y) && (i != 3 || isFormant_))
                {
                    setActiveGroup (i);
                    break;
                }
        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].draw (sb);
        for (int i = 0; i < kMaxGroups; ++i)
            if (i != activeGroup_)
                groups[i].drawMapped (sb);
    }

protected:
    void layout() override
    {
        const int m = 8, W = rect_.w;

        // 1. the top bar: CLOSE (a page has none), the power switch, F1 | F2 | F1+F2, the model and its list, the style, CLEAN, OUTPUT
        const Rect top { rect_.x + m, rect_.y + m, W - 2 * m, kTopH };
        Rect r[8];
        const int fixed[8] = { pageMode_ ? 0 : 84, 56, 3 * 84, 0, 56, 0, LedToggle::widthFor ("CLEAN"), 64 + 70 }, weight[8] = { 0, 0, 0, 5, 0, 4, 0, 0 };
        layoutRow (top, fixed, weight, 8, kGap, r);
        close.place (r[0]);
        power.place (r[1]);
        instances.place (r[2]);
        model.place (r[3]);
        modelList.place (r[4]);
        style.place (r[5]);
        clean.place (r[6]);
        outCaption_ = { r[7].x, r[7].y, 70, r[7].h };
        route.place ({ r[7].right() - 64, r[7].y - 4, 64, 64 });

        // 2. the bottom band: three blocks of four slots, one Q group each (a slot is a knob or a switch of the same size)
        const int bandY = rect_.bottom() - kBandMargin - kBandH;
        // the blocks are as wide as their slots need: a knob slot, the ENV 1 toggle (a LED toggle of its minimum width) and the ROUTING list (three rows of 56 px, the longest name in one column)
        const int pads = 2 * kBlockPad + 3 * kSlotGap, togW = LedToggle::kBandW, listW = kRoutingW;
        const int avail = W - 2 * kBandMargin - 2 * kBlockGap, unit = (avail - 3 * pads - togW - listW) / 10;
        const int wMain = 4 * unit + pads, wTrack = 3 * unit + togW + pads;
        mainBlock_ = { rect_.x + kBandMargin, bandY, wMain, kBandH };
        trackBlock_ = { mainBlock_.right() + kBlockGap, bandY, wTrack, kBandH };
        outBlock_ = { trackBlock_.right() + kBlockGap, bandY, rect_.right() - kBandMargin - (trackBlock_.right() + kBlockGap), kBandH };
        auto inner = [&] (const Rect& b) { return Rect { b.x + kBlockPad, b.y + kBlockTitleH, std::max (0, b.w - 2 * kBlockPad), std::max (0, b.h - kBlockTitleH - kBlockPad) }; };
        // four slots side by side; `fixedW[i]` > 0: that slot has exactly that width, the others share the rest equally
        auto fourSlots = [&] (const Rect& b, Component* c0, Component* c1, Component* c2, Component* c3, const int* fixedW)
        {
            const Rect in = inner (b);
            int fixedSum = 0, nFree = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (fixedW[i] > 0)
                    fixedSum += fixedW[i];
                else
                    ++nFree;
            }
            const int cw = nFree > 0 ? (in.w - 3 * kSlotGap - fixedSum) / nFree : 0;
            Component* cs[4] = { c0, c1, c2, c3 };
            int x = in.x;
            for (int i = 0; i < 4; ++i)
            {
                const int w = i == 3 ? in.right() - x : (fixedW[i] > 0 ? fixedW[i] : cw);
                cs[i]->place ({ x, in.y, w, in.h });
                x += w + kSlotGap;
            }
        };
        const int noFixed[4] = { 0, 0, 0, 0 }, trackFixed[4] = { 0, togW, 0, 0 }, outFixed[4] = { 0, 0, 0, listW };
        fourSlots (mainBlock_, &cutoff, &resonance, &drive, &mix, noFixed);
        fourSlots (trackBlock_, &keytrack, &env1, &var, &combOffset, trackFixed);
        fourSlots (outBlock_, &level, &pan, &blend, &routing, outFixed);
        {   // ENV 1: a 56 px LED toggle at the height of the dials of its neighbours (the cell was a knob-sized switch)
            const Rect c = env1.rect();
            env1.place ({ c.x, c.y + std::max (0, (c.h - 42 - led::kRowH) / 2), c.w, led::kRowH });
        }

        // 3. the display row: the display, and in the Formant model the XY pad with its block at the right
        const int midY = top.bottom() + m + 4, midH = std::max (60, bandY - m - midY);
        if (isFormant_)
        {
            const int fw = std::min (560, (W - 2 * m) * 45 / 100);
            display.place ({ rect_.x + m, midY, W - 2 * m - fw - kBlockGap, midH });
            formantBlock_ = { rect_.right() - m - fw, midY, fw, midH };
            const Rect in = inner (formantBlock_);
            const int areaW = in.w * 2 / 3, cw = (areaW - kGap) / 2, ch = (in.h - kQLane - kGap) / 2;   // X, Y / TRANSPOSE, SPREAD in a 2 x 2 grid (the Q group), RESONANCE in a column beside it
            formantX.place ({ in.x, in.y, cw, ch });
            formantY.place ({ in.x + cw + kGap, in.y, areaW - cw - kGap, ch });
            formantTrans.place ({ in.x, in.y + ch + kGap + kQLane, cw, ch });
            formantSpread.place ({ in.x + cw + kGap, in.y + ch + kGap + kQLane, areaW - cw - kGap, ch });
            formantRes.place ({ in.x + areaW + kGap, in.y, in.right() - (in.x + areaW + kGap), in.h });
        }
        else
        {
            display.place ({ rect_.x + m, midY, W - 2 * m, midH });
            formantBlock_ = {};
        }
    }

    void buildGroups()
    {
        for (int i = 0; i < kMaxGroups; ++i)
            for (int k = 0; k < 4; ++k)
                groups[i].set (k, nullptr);

        groups[0].set (0, &cutoff);
        groups[0].set (1, &resonance);
        groups[0].set (2, &drive);
        groups[0].set (3, &mix);

        groups[1].set (0, &keytrack);
        groups[1].set (1, &env1);
        groups[1].set (2, &var);
        groups[1].set (3, &combOffset);

        groups[2].set (0, &level);
        groups[2].set (1, &pan);
        groups[2].set (2, &blend);
        groups[2].set (3, &routing);

        groups[3].set (0, &formantX);
        groups[3].set (1, &formantY);
        groups[3].set (2, &formantTrans);
        groups[3].set (3, &formantSpread);

        activeGroup_ = -1;
        setActiveGroup (0);
    }

    void drawUnder (Pen& p) const override
    {
        p.block (mainBlock_);
        p.block (trackBlock_);
        p.block (outBlock_);
        if (isFormant_)
            p.block (formantBlock_);
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect blocks[kMaxGroups] = { mainBlock_, trackBlock_, outBlock_, formantBlock_ };
        for (int i = 0; i < kMaxGroups; ++i)
            if (i != 3 || isFormant_)
                p.textLeft ({ blocks[i].x + kBlockPad + 4, blocks[i].y + 3, std::max (0, blocks[i].w - 2 * kBlockPad - 4), 16 }, groupName (i), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        p.textRight (outCaption_, "OUTPUT", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        // the name of the LED list in the title strip of its block (the list has no title of its own: the three rows use the whole height)
        p.textLeft ({ routing.rect().x + 28, outBlock_.y + 3, std::max (0, routing.rect().w - 28), 16 }, "ROUTING", T.fLabel - 2.0f, p.c (activeGroup_ == 2 ? T.q : T.textDim, 255), T.fMin);
    }

    bool isFormant_ = false, pageMode_ = false;
    int activeGroup_ = 0;
    Rect mainBlock_, trackBlock_, outBlock_, formantBlock_, outCaption_;
};

}} // namespace synthyboy_mpc::ui
