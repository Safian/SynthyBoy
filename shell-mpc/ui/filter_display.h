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
    static constexpr double kDbTop = 30.0, kDbBottom = -36.0;

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

    double cutoffNorm() const noexcept
    {
        return std::clamp (std::log10 (std::clamp (state.cutoffHz, kFMin, kFMax) / kFMin) / 3.0, 0.0, 1.0);
    }
    double resonanceNorm() const noexcept
    {
        return std::clamp (state.resonancePct / 100.0, 0.0, 1.0);
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

    /** Center of the interactive handle on screen. */
    void handlePoint (float& hx, float& hy) const noexcept
    {
        const Rect in = plot();
        if (state.isFormant)
        {
            hx = static_cast<float> (in.x) + static_cast<float> (formantXNorm() * in.w);
            hy = static_cast<float> (in.bottom()) - static_cast<float> (formantYNorm() * in.h);
        }
        else
        {
            hx = static_cast<float> (in.x) + static_cast<float> (cutoffNorm() * in.w);
            hy = static_cast<float> (in.bottom()) - static_cast<float> (resonanceNorm() * in.h);
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
        for (const auto& d : kDecade)
        {
            const float gx = xOfHz (d.hz, in);
            p.line (gx, static_cast<float> (in.y), gx, static_cast<float> (in.bottom()), 1.0f, p.c (T.grid, 255));
            p.textCentered ({ static_cast<int> (gx) - 24, in.bottom() + 3, 48, 16 }, d.name, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textLeft ({ in.x + 2, in.bottom() + 3, 40, 16 }, "20 Hz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        p.textRight ({ in.right() - 52, in.bottom() + 3, 50, 16 }, "20 kHz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        static const double kDb[3] = { 24.0, 0.0, -24.0 };
        for (double db : kDb)
        {
            const float gy = dbToY (db, in);
            p.line (static_cast<float> (in.x), gy, static_cast<float> (in.right()), gy, 1.0f, p.c (T.grid, db == 0.0 ? 255 : 150));
            std::snprintf (buf, sizeof buf, "%+.0f", db);
            p.textRight ({ in.x - 30, static_cast<int> (gy) - 8, 26, 16 }, db == 0.0 ? "0 dB" : buf, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }

        // 2. The curves (a line of 1.5 .. 2 pixels, no glow)
        if (state.dual)
        {
            if (state.curve1 && state.numPoints1 >= 2)
                drawCurvePolyline (sb, in, state.curve1, state.numPoints1, filterColour (1), 0, 0.8f);
            if (state.curve2 && state.numPoints2 >= 2)
                drawCurvePolyline (sb, in, state.curve2, state.numPoints2, filterColour (2), 0, 0.8f);
            if (state.curveResult && state.numPointsResult >= 2)
                drawCurvePolyline (sb, in, state.curveResult, state.numPointsResult, T.text, 0, 1.0f);
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
                drawCurvePolyline (sb, in, cData, cLen, filterColour (state.activeFilter), 34, 0.8f);
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

        // 5. The handle (cutoff and resonance); while a finger drags it the values are written next to it
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
        const double margin = 8.0;
        const double h = std::max (10.0, static_cast<double> (in.h) - 2.0 * margin);
        const double pxPerDb = h / (kDbTop - kDbBottom);
        const double y = in.y + margin + (kDbTop - db) * pxPerDb;
        return static_cast<float> (std::clamp (y, static_cast<double> (in.y), static_cast<double> (in.bottom())));
    }

    static void drawCurvePolyline (gpu::SceneBuilder& sb, const Rect& in, const float* dbData, int nPoints, uint32_t col, int fillAlpha, float strokeWidth)
    {
        if (nPoints < 2 || in.w < 4)
            return;
        float rows[gpu::kMaxCurve];
        const int n = std::min (nPoints, gpu::kMaxCurve);
        for (int i = 0; i < n; ++i)
            rows[i] = dbToY (static_cast<double> (dbData[i]), in);

        sb.curve (rows, n, in.x, in.w);
        if (fillAlpha > 0)
            sb.fill (static_cast<float> (in.bottom()), colour::rgba (col, static_cast<uint32_t> (fillAlpha)));
        sb.stroke (strokeWidth, colour::rgba (col, 255));
    }

    Change onDown (int x, int y, double /*nowMs*/) override
    {
        dragging_ = true;
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
            state.cutoffHz = kFMin * std::pow (10.0, normX * 3.0);
            state.resonancePct = normY * 100.0;
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
/** FilterFullScreen: the page of a filter (the plugin draws the whole tab; filter_ui.h). From the top: the power switch, the instance selector F1 | F2 | F1+F2, the model with its list, the style, the CLEAN switch and the
    OUTPUT button (where the filter sends its signal); the display (the real response, or the Formant pad with its five knobs at the right); a row of blocks, ONE Q group each:
      MAIN   CUTOFF  RESO  DRIVE  MIX                  TRACK   KEYTRACK  ENV 1  VAR  COMB                   OUT   LEVEL  PAN  BLEND  ROUTING                  FORMANT (Formant model)  X  Y  TRANSPOSE  SPREAD (+ RESONANCE)
    A tap on a member makes its block the Q group; the four Q-Links then turn its members. Every knob shows its value. */
class FilterFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 4;   // MAIN, TRACK, OUT, FORMANT
    static constexpr int kTopH = 56, kBandH = 214, kBlockTitleH = 34, kBlockPad = 4, kBlockGap = 6, kSlotGap = 2;   // four knobs of 100 px in a block: the pads and gaps are small
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    ToggleIcon power;
    PictogramSelector instances;
    Stepper model, style;
    MenuButton modelList { "", MenuButton::Icon::Down };   // the list of the models
    ToggleIcon clean;                                      // CLEAN MODE
    RouteButton route { "M" };                             // OUTPUT: where the filter sends its signal (M main effects, D direct, - none)
    FilterDisplay display;

    // MAIN
    Knob cutoff { "CUTOFF" }, resonance { "RESO" }, drive { "DRIVE" }, mix { "MIX" };
    // TRACK
    Knob keytrack { "KEYTRACK" }, var { "VAR" }, combOffset { "COMB" };
    ToggleIcon env1;
    // OUT
    Knob level { "LEVEL" }, pan { "PAN" }, blend { "BLEND" }, routing { "ROUTING" };   // ROUTING: the three ways the two filters are connected (a knob over a choice: it steps)
    // FORMANT
    Knob formantX { "X" }, formantY { "Y" }, formantTrans { "TRANSPOSE" }, formantRes { "RESONANCE" }, formantSpread { "SPREAD" };

    QGroup groups[kMaxGroups]; // 0: MAIN, 1: TRACK, 2: OUT, 3: FORMANT

    FilterFullScreen()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        env1.state.icon = ToggleIcon::Icon::Generic;
        env1.setLabel ("ENV 1");
        env1.state.showState = true;
        clean.state.icon = ToggleIcon::Icon::Generic;
        clean.setLabel ("CLEAN");
        clean.state.showState = true;

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
    }

protected:
    void layout() override
    {
        const int m = 8, W = rect_.w;

        // 1. the top bar: CLOSE (a page has none), the power switch, F1 | F2 | F1+F2, the model and its list, the style, CLEAN, OUTPUT
        const Rect top { rect_.x + m, rect_.y + m, W - 2 * m, kTopH };
        Rect r[8];
        const int fixed[8] = { pageMode_ ? 0 : 84, 56, 3 * 84, 0, 56, 0, 96, 64 + 70 }, weight[8] = { 0, 0, 0, 5, 0, 4, 0, 0 };
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
        const int bandY = rect_.bottom() - m - kBandH;
        const int avail = W - 2 * m - 2 * kBlockGap, wBlock = avail / 3;
        mainBlock_ = { rect_.x + m, bandY, wBlock, kBandH };
        trackBlock_ = { mainBlock_.right() + kBlockGap, bandY, wBlock, kBandH };
        outBlock_ = { trackBlock_.right() + kBlockGap, bandY, rect_.right() - m - (trackBlock_.right() + kBlockGap), kBandH };
        auto inner = [&] (const Rect& b) { return Rect { b.x + kBlockPad, b.y + kBlockTitleH, std::max (0, b.w - 2 * kBlockPad), std::max (0, b.h - kBlockTitleH - kBlockPad) }; };
        auto fourSlots = [&] (const Rect& b, Component* c0, Component* c1, Component* c2, Component* c3)
        {
            const Rect in = inner (b);
            const int cw = (in.w - 3 * kSlotGap) / 4;
            Component* cs[4] = { c0, c1, c2, c3 };
            for (int i = 0; i < 4; ++i)
            {
                const int x = in.x + i * (cw + kSlotGap), w = i == 3 ? in.right() - x : cw;
                if (cs[i] == &env1)
                {
                    const int h = std::min (in.h, 96);
                    cs[i]->place ({ x, in.y + (in.h - h) / 2, w, h });
                }
                else
                    cs[i]->place ({ x, in.y, w, in.h });
            }
        };
        fourSlots (mainBlock_, &cutoff, &resonance, &drive, &mix);
        fourSlots (trackBlock_, &keytrack, &env1, &var, &combOffset);
        fourSlots (outBlock_, &level, &pan, &blend, &routing);

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
    }

    bool isFormant_ = false, pageMode_ = false;
    int activeGroup_ = 0;
    Rect mainBlock_, trackBlock_, outBlock_, formantBlock_, outCaption_;
};

}} // namespace synthyboy_mpc::ui
