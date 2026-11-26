// SynthyBoy - MPC UI component library: the FILTER DISPLAY and filter screens.
// FilterDisplay: frequency response curve on log-frequency axis (20 Hz - 20 kHz) with interactive cutoff/reso handle,
// dual-filter curves (F1 cyan, F2 magenta, resultant white), live voice markers; Formant mode XY-pad with vowel markers.
// FilterOutReadout: the read-only DEST / BUS 1 / BUS 2 / LEVEL / PAN of the OUT block (the Mixer is their home).
// FilterFullScreen: the ONE Filter page (the whole tab): F1 | F2 | NOTE | VELO.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "scalar_curve_editor.h"

#include "native_apertures.h"
#include "../native_filter_controls.h"

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
#include "used_selector.h"

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

// ===============================================================================================================================
/** Non-rendering geometry adapter for the native read-only OUT rows. */
class FilterOutReadout : public Component
{
public:
    static constexpr int kRows = 5, kNameW = 64;

    Size minSize() const override { return { 170, kRows * 28 }; }
    int touchTargets (Rect*, int) const override { return 0; }
    Change touchDown (int, int, double) override { captured_ = false; return Change::none(); }
    static const char* rowName (int i) noexcept
    {
        static const char* const n[kRows] = { "DEST", "BUS 1", "BUS 2", "LEVEL", "PAN" };
        return i >= 0 && i < kRows ? n[i] : "";
    }

    void draw (gpu::SceneBuilder&) const override {}
protected:
    void drawBody (Pen&) const override {}
};

// ===============================================================================================================================
/** Non-rendering native control/Q adapter. The MPC owns dial art and gestures. */
class FilterKnob : public Knob
{
public:
    using Knob::Knob;
    void draw (gpu::SceneBuilder&) const override {}
    Change touchDown (int, int, double) override { return Change::none(); }
    Change touchMove (int, int) override { return Change::none(); }
    Change touchUp (int, int) override { return Change::none(); }
};

// ===============================================================================================================================
/** FilterFullScreen: the ONE Filter page (the plugin draws the whole tab; filter_ui.h). From the top: the power switch of the selected filter, the VIEW selector F1 | F2 | NOTE | VELO (a UsedSelector of four fixed cells:
    the state of the page), the F1+F2 toggle (a VIEW only: the display draws both curves and the resultant, the controls edit the selected filter), the model, the style and the CLEAN switch; the display (the real
    response, or the Formant pad with its four knobs at the right); a row of blocks, ONE Q group each:
      MAIN   CUTOFF  RESO  DRIVE  MIX      TRACK   KEYTRACK  ENV 1  VAR (Formant: RESONANCE)  COMB (Comb only)      OUT   BLEND  ROUTING  + the read-only DEST / BUS 1 / BUS 2 / LEVEL / PAN      FORMANT (Formant)  X  Y  TRANSPOSE  SPREAD
    The Q group OUT is [BLEND, ROUTING, MODEL, STYLE] (frameless: MODEL and STYLE stand in the top bar). What does not apply is HIDDEN, not dimmed: VAR and the FORMANT block with the Formant model, COMB with every model but Comb.
    NOTE and VELO (view 2 and 3) hide all of it: the owner draws the scalar curve editor (scalar_curve_editor.h) in `bodyRect ()`, under the top bar that keeps the selector. A tap on a member makes its group the Q group. */
class FilterFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 4;   // MAIN, TRACK, OUT, FORMANT
    static constexpr int kRoutingW = 150;  // the ROUTING list: dot, "Series 1 to 2", padding
    static constexpr int kSplitHeaderY = 12, kSplitHeaderH = 48;
    static constexpr int kTopH = 56, kTopBarH = kQLane + kTopH, kMarkerBandH = 8, kBandH = 214, kBlockTitleH = 34, kBlockPad = 2, kBlockGap = 4, kSlotGap = 0, kBandMargin = 4;   // the top bar: the lane of the chips of MODEL and STYLE, then the 56 px row
    static constexpr int kViewF1 = 0, kViewF2 = 1, kViewNote = 2, kViewVelo = 3, kViewCellW = 72;   // the states of the page (the selector's candidate id is the state + 1)
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    MenuButton expand { "", MenuButton::Icon::Expand };
    ToggleIcon power;
    UsedSelector view;         // F1 | F2 (split) or F1 | F2 | NOTE | VELO (expanded)
    UsedSelector scalarView;   // NOTE | VELO on the right panel in 60/40 Split mode
    LedToggle both;      // F1+F2: a view only (both curves and the resultant), the controls edit the selected filter
    Stepper model, style;
    LedToggle clean;                                       // CLEAN MODE (a LED toggle: flat dot + name)
    FilterOutReadout outReadout;                           // DEST, BUS 1, BUS 2, LEVEL, PAN: read only (the Mixer is their home)
    FilterDisplay display;

    // MAIN
    FilterKnob cutoff { "CUTOFF" }, resonance { "RESO" }, drive { "DRIVE" }, mix { "MIX" };
    // TRACK
    FilterKnob keytrack { "KEYTRACK" }, var { "VAR" }, combOffset { "COMB" };
    LedToggle env1;   // a LED toggle in its slot of the block
    // OUT
    FilterKnob blend { "BLEND" };
    LedList routing;   // ROUTING: the three ways the two filters are connected, all three visible (the caption is drawn in the title strip of the block)
    // FORMANT
    FilterKnob formantX { "X" }, formantY { "Y" }, formantTrans { "TRANSPOSE" }, formantRes { "F RESO" }, formantSpread { "SPREAD" };   // formantRes sits in the TRACK block (VAR's place) while the model is Formant

    QGroup groups[kMaxGroups]; // 0: MAIN, 1: TRACK, 2: OUT, 3: FORMANT

    FilterFullScreen()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        power.state.negativeOff = true;
        env1.setLabel ("ENV 1");
        clean.setLabel ("CLEAN");
        both.setLabel ("F1+F2");
        setViewCells (true, true, false, false);
        setScalarCells (false, false);
        model.listable = true;
        style.listable = true;

        keytrack.state.bipolar = true;
        formantTrans.state.bipolar = true;
        formantSpread.state.bipolar = true;

        addKid (close);
        addKid (expand, false);
        addKid (power);
        addKid (view);
        addKid (scalarView, false);
        addKid (both);
        addKid (model);
        addKid (style);
        addKid (clean);
        addKid (outReadout);
        addKid (display);
        addKid (cutoff); addKid (resonance); addKid (drive); addKid (mix);
        addKid (keytrack); addKid (env1); addKid (var); addKid (combOffset);
        addKid (blend); addKid (routing);
        addKid (formantX, false); addKid (formantY, false); addKid (formantTrans, false); addKid (formantRes, false); addKid (formantSpread, false);

        buildGroups();
        applyVisibility();
    }

    /** Keep POWER, navigation, expand and (while any filter is ON) shared ROUTING usable when the selected filter is OFF. */
    void setModuleEnabled (bool on, bool anyFilterOn = true)
    {
        anyOn_ = anyFilterOn;
        // Keep the root enabled: POWER and page navigation must still receive touch.
        close.setEnabled (true); expand.setEnabled (true); view.setEnabled (true); scalarView.setEnabled (true);
        both.setEnabled (true); model.setEnabled (on); style.setEnabled (on); clean.setEnabled (on);
        outReadout.setEnabled (true); display.setEnabled (on); cutoff.setEnabled (on); resonance.setEnabled (on); drive.setEnabled (on); mix.setEnabled (on);
        keytrack.setEnabled (on); env1.setEnabled (on); var.setEnabled (on); combOffset.setEnabled (on); blend.setEnabled (on);
        routing.setEnabled (anyFilterOn);
        formantX.setEnabled (on); formantY.setEnabled (on); formantTrans.setEnabled (on); formantRes.setEnabled (on); formantSpread.setEnabled (on);
        power.setEnabled (true);
        setInactive (! on);
        power.setInactive (false);
        expand.setInactive (false);
        view.setInactive (false);
        scalarView.setInactive (false);
        close.setInactive (false);
        both.setInactive (false);
        routing.setInactive (! anyFilterOn);
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setInactive (i == 2 ? ! anyFilterOn : ! on);
    }

    /** The cells of the VIEW selector: F1 | F2 in Split mode, or F1 | F2 | NOTE | VELO in Expanded view. */
    void setViewCells (bool f1Used, bool f2Used, bool noteUsed, bool veloUsed)
    {
        view.clear();
        view.add (kViewF1 + 1, "F1", f1Used, true);
        view.add (kViewF2 + 1, "F2", f2Used, true);
        if (! splitMode_)
        {
            view.add (kViewNote + 1, "NOTE", noteUsed, true);
            view.add (kViewVelo + 1, "VELO", veloUsed, true);
        }
        view.setSelected (std::min (view_, splitMode_ ? kViewF2 : kViewVelo) + 1);
    }

    /** The right-hand NOTE | VELO selector in Split mode (sources 24 / 23). */
    void setScalarCells (bool noteUsed, bool veloUsed)
    {
        scalarView.clear();
        scalarView.add (1, "NOTE", noteUsed, true);
        scalarView.add (2, "VELO", veloUsed, true);
        scalarView.setSelected (scalarSel_ + 1);
    }

    void setScalarView (int s)
    {
        scalarSel_ = std::clamp (s, 0, 1);
        scalarView.setSelected (scalarSel_ + 1);
    }
    int scalarViewState() const noexcept { return scalarSel_; }

    /** Enables the 60/40 Split layout on the left 751 px column. */
    void setSplitMode (bool split)
    {
        const bool effective = split;
        if (splitMode_ == effective)
            return;
        splitMode_ = effective;
        for (FilterKnob* k : { &cutoff, &resonance, &drive, &mix, &keytrack, &var, &formantRes, &combOffset, &blend })
        {
            k->state.side = effective;
            k->state.dialMax = effective ? 60 : 0;
        }
        env1.centre = effective ? 0.5f : 0.38f;
        if (effective && isFormant_ && activeGroup_ == 3)
            setActiveGroup (0);
        applyVisibility();
        if (rect_.w > 0)
            layout();
    }
    bool splitMode() const noexcept { return splitMode_; }
    bool anyFilterOn() const noexcept { return anyOn_; }
    /** The state of the page (0 F1, 1 F2, 2 NOTE, 3 VELO): NOTE and VELO hide the whole filter part. */
    void setView (int v)
    {
        if (view_ == std::clamp (v, 0, 3))
            return;
        const bool wasCurve = curveView();
        view_ = std::clamp (v, 0, 3);
        view.setSelected (view_ + 1);
        if (wasCurve == curveView())
            return;   // F1 <-> F2: the same parts are shown (the finger on the display that picked the other filter's handle goes on)
        cap_ = nullptr;   // a finger that holds a part that is hidden now
        applyVisibility();
        if (rect_.w > 0)
            layout();
    }
    int viewState() const noexcept { return view_; }
    bool curveView() const noexcept { return view_ >= kViewNote; }
    /** What the body of NOTE and VELO is drawn in: everything below the top bar (1280 x 556 on the page). On the page of a hardware sub-tab the top bar has nothing to hold (no selector: the sub-tab names the
        state, the chain, MODEL and STYLE belong to the filter pages), so the body is the page under the strip of the three page markers (kMarkerBandH: the markers of the detector are at the top edge, no
        control may touch them). */
    Rect bodyRect() const noexcept
    {
        if (subTabPage_)
            return { rect_.x, rect_.y + kMarkerBandH, rect_.w, std::max (0, rect_.h - kMarkerBandH) };
        return { rect_.x, rect_.y + kTopBarH, rect_.w, std::max (0, rect_.h - kTopBarH) };
    }
    Rect topBarRect() const noexcept { return { rect_.x, rect_.y, rect_.w, kTopBarH }; }

    /** The page of a hardware sub-tab (Filter 1, Filter 2, Note, Velo): there is no VIEW selector and no NOTE | VELO selector, the sub-tab names the state. The top bar keeps its geometry: the place of the
        selector is the filter chain of the skin. */
    void setSubTabPage (bool on)
    {
        subTabPage_ = on;
        show (view, ! on);
        show (scalarView, false);
        if (rect_.w > 0)
            layout();
    }
    bool subTabPage() const noexcept { return subTabPage_; }

    /** The page of its own tab: there is nothing to close. */
    void setPageMode (bool page)
    {
        pageMode_ = page;
        show (close, ! page);
        if (rect_.w > 0)
            layout();
    }
    /** The Formant model: the display is the XY pad and the FORMANT block sits at its right; VAR and COMB are hidden, RESONANCE of the formants takes the place of VAR in the TRACK block. */
    void setFormantMode (bool isFormant)
    {
        isFormant_ = isFormant;
        display.state.isFormant = isFormant;
        if (isFormant)
            combMode_ = false;
        applyVisibility();
        if (! isFormant && activeGroup_ == 3)
            setActiveGroup (0);
        if (rect_.w > 0)
            layout();
    }
    /** The Comb model: COMB (the comb offset) is shown, in the fourth slot of the TRACK block. */
    void setCombMode (bool isComb)
    {
        if (combMode_ == (isComb && ! isFormant_))
            return;
        combMode_ = isComb && ! isFormant_;
        applyVisibility();
    }
    bool formantMode() const noexcept { return isFormant_; }
    bool combMode() const noexcept { return combMode_; }
    bool pageMode() const noexcept { return pageMode_; }

    Size minSize() const override { return { 1280, 628 }; }
    Rect displayRect() const noexcept { return display.rect(); }
    const Rect& graphRect() const noexcept { return graphRect_; }
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
        if (activeGroup_ < 0 || curveView())
            return Change::none();
        if (! power.state.on || inactive_)
            return (activeGroup_ == 2 && knobIndex == 1 && anyOn_) ? groups[2].turn (1, delta) : Change::none();
        return groups[activeGroup_].turn (knobIndex, delta);
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen p (sb, enabled_, inactive_);
        if (! curveView()) display.draw (sb);
        Cell cell (sb, rect_);
        drawExtra (p);
        if (curveView())
            return;
        const Rect blocks[] = { mainBlock_, trackBlock_, outBlock_, formantBlock_ };
        const int maxG = (isFormant_ && ! splitMode_) ? kMaxGroups : 3;
        for (int i = 0; i < maxG; ++i)
            if (! blocks[i].empty())
            {
                // Leave room for the rounded bank frame at the edge of the native dials.
                const Rect frame = splitMode_ ? blocks[i].inset (-1) : blocks[i];
                groups[i].draw (sb, &frame);
            }
        for (int i = 0; i < maxG; ++i)
            if (i != activeGroup_ && ! blocks[i].empty())
                groups[i].drawMapped (sb);
    }

protected:
    void layoutSplit()
    {
        const int m = 8, gapCol = 12;
        const int lx = rect_.x + m, lw = (rect_.w - 2 * m - gapCol) * 60 / 100;
        const int rx = lx + lw + gapCol, rw = rect_.right() - m - rx;

        // Leave the host detection markers (y=4..10) above the compact header.
        const Rect topLeft { lx, rect_.y + kSplitHeaderY, lw, kSplitHeaderH };
        Rect r[4];
        const int fixed[4] = { 116, 0, 0, LedToggle::widthFor ("CLEAN") }, weight[4] = { 0, 5, 4, 0 };
        layoutRow (topLeft, fixed, weight, 4, 6, r);
        close.place ({});
        both.place ({});
        view.place (r[0]);
        model.place (r[1]);
        style.place (r[2]);
        clean.place (r[3]);
        scalarView.place ({ rx, topLeft.y, std::max (116, rw - 130 - 6), kSplitHeaderH });

        // MAIN and TRACK own four dials per row; OUT has BLEND above the compact routing choice.
        const int rowHeight = 84, rowGap = 4, outWidth = 208, groupGap = 8;
        const int controlsY = rect_.bottom() - m - 2 * rowHeight - rowGap;
        const int rowWidth = lw - outWidth - groupGap;
        mainBlock_ = { lx, controlsY, rowWidth, rowHeight };
        trackBlock_ = { lx, controlsY + rowHeight + rowGap, rowWidth, rowHeight };
        outBlock_ = { lx + rowWidth + groupGap, controlsY, outWidth, 2 * rowHeight + rowGap };

        const int graphY = topLeft.bottom() + 4;
        graphRect_ = { lx, graphY, lw, controlsY - 6 - graphY };
        power.place ({ lx + 8, graphY + 4, 56, 56 });
        expand.place ({ graphRect_.right() - 64, graphY + 4, 56, 56 });
        display.placeGraph (graphRect_, power.rect(), expand.rect());

        const int gap = 4, pad = 4, slotWidth = (rowWidth - 2 * pad - 3 * gap) / 4;
        auto slot = [&] (Rect row, int column)
        {
            const int x = row.x + pad + column * (slotWidth + gap);
            return Rect { x, row.y + 20, column == 3 ? row.right() - pad - x : slotWidth, 60 };
        };
        Component* main[] = { &cutoff, &resonance, &drive, &mix };
        Component* track[] = { &keytrack, &env1, &var, &combOffset };
        for (int i = 0; i < 4; ++i)
        {
            main[i]->place (slot (mainBlock_, i));
            track[i]->place (slot (trackBlock_, i));
        }
        formantRes.place (var.rect());
        blend.place ({ outBlock_.x + pad, mainBlock_.y + 20, outWidth - 2 * pad, 60 });
        routing.place ({ outBlock_.x + pad, trackBlock_.y + 24, outWidth - 2 * pad, 48 });
        outReadout.place ({});
        formantBlock_ = {};
        for (Component* c : { static_cast<Component*> (&formantX), static_cast<Component*> (&formantY), static_cast<Component*> (&formantTrans), static_cast<Component*> (&formantSpread) })
            c->place ({});
    }

    void layout() override
    {
        if (splitMode_ && ! curveView())
        {
            layoutSplit();
            return;
        }
        const int m = 8, W = rect_.w;
        scalarView.place ({});

        const bool isExpCurve = curveView();
        const Rect top { rect_.x + m, rect_.y + kQLane, W - 2 * m - (isExpCurve ? 62 : 0), kTopH };
        Rect r[7];
        const int fixed[7] = { pageMode_ ? 0 : 84, isExpCurve ? 0 : 56, 4 * kViewCellW, LedToggle::widthFor ("F1+F2"), 0, 0, LedToggle::widthFor ("CLEAN") }, weight[7] = { 0, 0, 0, 0, 5, 4, 0 };
        layoutRow (top, fixed, weight, 7, kGap, r);
        close.place (r[0]);
        view.place (r[2]);
        both.place (r[3]);
        model.place (r[4]);
        style.place (r[5]);
        clean.place (r[6]);
        if (isExpCurve)
        {
            const int graphRight = rect_.right() - 2 * m - ScalarCurveEditor::kSideW;
            expand.place ({ graphRight - 8 - 56, top.bottom() + m + 4, 56, 56 });
            power.place ({});
            graphRect_ = {};
            display.place ({});
            mainBlock_ = {}; trackBlock_ = {}; outBlock_ = {}; formantBlock_ = {};
            return;
        }

        // 2. the bottom band: three blocks, one Q group each (a slot is a knob or a switch of the same size)
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
                const int w = i == 3 ? in.right() - x - (fixedW[3] > 0 ? 6 : 0) : (fixedW[i] > 0 ? fixedW[i] : cw);
                cs[i]->place ({ x, in.y, w, in.h });
                x += w + kSlotGap;
            }
        };
        const int noFixed[4] = { 0, 0, 0, 0 }, trackFixed[4] = { 0, togW, 0, 0 };
        fourSlots (mainBlock_, &cutoff, &resonance, &drive, &mix, noFixed);
        fourSlots (trackBlock_, &keytrack, &env1, isFormant_ ? static_cast<Component*> (&formantRes) : static_cast<Component*> (&var), &combOffset, trackFixed);
        {
            const Rect third = (isFormant_ ? formantRes : var).rect();   // VAR and the formant RESONANCE share the third slot (only one of them is shown)
            var.place (third);
            formantRes.place (third);
        }
        {
            const Rect in = inner (outBlock_);
            blend.place ({ in.x, in.y, unit, in.h });
            routing.place ({ blend.rect().right() + kSlotGap, in.y, listW, in.h });
            const int rx = routing.rect().right() + 8;
            outReadout.place ({ rx, in.y + 6, std::max (0, in.right() - 6 - rx), std::max (0, in.h - 12) });
        }
        env1.centre = 0.38f;   // ENV 1 is the whole slot (its index chip stands in line with the knobs), the dot and the name at the height of the dials

        // 3. the display row: the display, and in the Formant model the XY pad with its block at the right
        const int midY = top.bottom() + 4, midH = std::max (60, bandY - m - midY);
        const int fw = isFormant_ ? std::min (440, (W - 2 * m) * 36 / 100) : 0;
        const int gw = isFormant_ ? (W - 2 * m - fw - kBlockGap) : (W - 2 * m);
        graphRect_ = { rect_.x + m, midY, gw, midH };
        power.place ({ graphRect_.x + 8, graphRect_.y + 4, 56, 56 });
        expand.place ({ graphRect_.right() - 8 - 56, graphRect_.y + 4, 56, 56 });
        display.placeGraph (graphRect_, power.rect(), expand.rect());
        if (isFormant_)
        {
            formantBlock_ = { rect_.right() - m - fw, midY, fw, midH };
            const Rect in = inner (formantBlock_);
            const int cw = (in.w - kGap) / 2, ch = (in.h - kQLane - kGap) / 2;   // X, Y / TRANSPOSE, SPREAD in a 2 x 2 grid (the Q group)
            formantX.place ({ in.x, in.y, cw, ch });
            formantY.place ({ in.x + cw + kGap, in.y, in.w - cw - kGap, ch });
            formantTrans.place ({ in.x, in.y + ch + kGap + kQLane, cw, ch });
            formantSpread.place ({ in.x + cw + kGap, in.y + ch + kGap + kQLane, in.w - cw - kGap, ch });
        }
        else
        {
            formantBlock_ = {};
        }
    }

    /** Which parts are shown: NOTE / VELO hide the filter; hidden, not dimmed, what does not apply (VAR with the Formant model, COMB without the Comb model, the FORMANT block and its RESONANCE without the Formant model). */
public:
    std::array<const Component*, nativeui::FilterPoint> nativeControls() const noexcept
    {
        return { &power, &cutoff, &resonance, &drive, &mix, &keytrack, &env1, &var, &combOffset, &blend, &routing,
                 &formantX, &formantY, &formantTrans, &formantRes, &formantSpread, &clean, &model, &style };
    }

    bool hasNativeGeometry (int slot) const noexcept
    {
        using namespace nativeui;
        if (curveView())
            return false;
        switch (slot)
        {
            case FilterVar: return ! isFormant_;
            case FilterClean: return ! isFormant_ && ! combMode_;
            case FilterCombOffset: return combMode_;
            case FilterFormantX: case FilterFormantY: case FilterFormantTranspose: case FilterFormantSpread:
                return isFormant_ && ! splitMode_;
            case FilterFormantResonance: return isFormant_;
            case FilterStyle: return true;
            default: return true;
        }
    }

    bool hasNativeControl (int slot) const noexcept
    {
        return hasNativeGeometry (slot) && (slot != nativeui::FilterStyle || style.state.count > 1)
            && (slot == nativeui::FilterPower || (slot == nativeui::FilterRouting ? anyOn_ : power.state.on));
    }

    std::array<Rect, nativeui::FilterPoint> nativeRects (bool includePassive = false) const noexcept
    {
        std::array<Rect, nativeui::FilterPoint> out {};
        const auto controls = nativeControls();
        for (int slot = 0; slot < nativeui::FilterPoint; ++slot)
            if (slot != nativeui::FilterRouting && (includePassive ? hasNativeGeometry (slot) : hasNativeControl (slot)))
                out[slot] = controls[slot]->rect();
        return out;
    }

    Rect nativeSingleStyleRect() const noexcept
    {
        return ! curveView() && power.state.on && style.state.count == 1
            ? style.rect() : Rect {};
    }
    Rect nativeOutReadoutRect() const noexcept
    {
        return ! curveView() && ! splitMode_ ? outReadout.rect() : Rect {};
    }
    Rect nativeExpandFilterRect() const noexcept
    {
        return expand.rect();
    }
    Rect nativeScalarViewRect() const noexcept
    {
        return splitMode_ && ! curveView() ? scalarView.rect() : Rect {};
    }

    void applyVisibility()
    {
        // Native controls retain their state and Q geometry, never a second
        // base draw/input owner. Only the actual response/XY graph is a child.
        for (int i = 0; i < numChildren(); ++i)
            show (*child (i), child (i) == &display && ! curveView());
        buildGroups();
    }

protected:

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
        groups[1].set (2, isFormant_ ? static_cast<Component*> (&formantRes) : static_cast<Component*> (&var));
        groups[1].set (3, combMode_ ? &combOffset : nullptr);

        groups[2].set (0, &blend);   // the OUT group: BLEND and ROUTING in the block, MODEL and STYLE in the top bar: frameless (the chips mark it)
        groups[2].set (1, &routing);
        groups[2].set (2, &model);
        groups[2].set (3, &style);
        groups[2].setFrameless (true);

        groups[3].set (0, &formantX);
        groups[3].set (1, &formantY);
        groups[3].set (2, &formantTrans);
        groups[3].set (3, &formantSpread);

        const int keep = activeGroup_;
        activeGroup_ = -1;
        setActiveGroup (keep < 0 ? 0 : keep);
    }

    void drawExtra (Pen& p) const override
    {
        if (curveView())
            return;
        const theme::Look& T = theme::L();
        if (! graphRect_.empty())
        {
            const char* hdr = ! power.state.on
                ? (view_ == kViewF2 ? "FILTER 2 · OFF (BYPASSED)" : "FILTER 1 · OFF (BYPASSED)")
                : (isFormant_ ? (view_ == kViewF2 ? "FILTER 2 · FORMANT XY" : "FILTER 1 · FORMANT XY")
                              : (view_ == kViewF2 ? "FILTER 2 · RESPONSE" : "FILTER 1 · RESPONSE"));
            p.textLeft ({ graphRect_.x + 74, graphRect_.y + 4, std::max (0, graphRect_.w - 204), 56 },
                        hdr, T.fLabel, p.c (power.state.on ? T.textDim : T.textFaint, 255), T.fMin);
        }
        const Rect blocks[kMaxGroups] = { mainBlock_, trackBlock_, outBlock_, formantBlock_ };
        for (int i = 0; i < kMaxGroups; ++i)
            if ((i != 3 || (isFormant_ && ! splitMode_)) && ! blocks[i].empty())
                p.textLeft ({ blocks[i].x + kBlockPad + 4, blocks[i].y + 3, std::max (0, blocks[i].w - 2 * kBlockPad - 4), 16 }, groupName (i), T.fLabel - 2.0f, p.c (activeGroup_ == i ? T.q : T.textDim, 255), T.fMin);
        if (! splitMode_ && ! outReadout.rect().empty())
            p.textLeft ({ outReadout.rect().x + 4, outBlock_.y + 3, std::max (0, outReadout.rect().w - 4), 16 }, "SET ON MIXER", T.fLabel - 2.0f, p.c (T.textFaint, 255), T.fMin);
    }

    bool isFormant_ = false, combMode_ = false, pageMode_ = false, subTabPage_ = false, splitMode_ = false, anyOn_ = true;
    int activeGroup_ = 0, view_ = 0, scalarSel_ = 0;
    Rect mainBlock_, trackBlock_, outBlock_, formantBlock_, graphRect_;
};

}} // namespace synthyboy_mpc::ui
