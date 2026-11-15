// SynthyBoy - MPC UI component library: the FILTER DISPLAY and filter screens.
// FilterDisplay: frequency response curve on log-frequency axis (20 Hz - 20 kHz) with interactive cutoff/reso handle,
// dual-filter curves (F1 cyan, F2 magenta, resultant white), live voice markers; Formant mode XY-pad with vowel markers.
// FilterRoutePanel: route popup (destination, bus 1/2 sends, level, pan).
// FilterCompact: Play-page 1/3 element (~410x620).
// FilterFullScreen: full screen overlay view.
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
/** FilterDisplay: the response curve on log frequency axis (20 Hz .. 20 kHz) or Formant XY pad.
    Interactive: dragging moves Cutoff / Resonance, or Formant X / Y. Touch target >= 56 px. */
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

    /** Center of the interactive handle on screen. */
    void handlePoint (float& hx, float& hy) const noexcept
    {
        const Rect in = rect_.inset (8);
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

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen pen (sb, enabled_);
        Cell cell (sb, rect_);
        pen.block (rect_, false);
        const Rect in = rect_.inset (8);
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

    void drawFilterCurve (Pen& p, gpu::SceneBuilder& sb, const Rect& in) const
    {
        const theme::Look& T = theme::L();

        // 1. Grid lines: 100 Hz, 1 kHz, 10 kHz
        const double fGrid[3] = { 100.0, 1000.0, 10000.0 };
        for (double f : fGrid)
        {
            const float gx = static_cast<float> (in.x) + static_cast<float> (std::log10 (f / kFMin) / 3.0 * in.w);
            p.line (gx, static_cast<float> (in.y), gx, static_cast<float> (in.bottom()), 1.0f, p.c (T.grid, 60));
        }
        // Zero dB horizontal line
        const float zy = dbToY (0.0, in);
        p.line (static_cast<float> (in.x), zy, static_cast<float> (in.right()), zy, 1.0f, p.c (T.grid, 90));

        // 2. Curves
        const uint32_t colCyan = 0x2ee6f0;
        const uint32_t colMagenta = 0xff2e63;
        const uint32_t colWhite = 0xffffff;

        if (state.dual)
        {
            // F1 curve (cyan)
            if (state.curve1 && state.numPoints1 >= 2)
                drawCurvePolyline (sb, in, state.curve1, state.numPoints1, colCyan, 0, 0.75f);
            // F2 curve (magenta)
            if (state.curve2 && state.numPoints2 >= 2)
                drawCurvePolyline (sb, in, state.curve2, state.numPoints2, colMagenta, 0, 0.75f);
            // Resultant curve (white)
            if (state.curveResult && state.numPointsResult >= 2)
                drawCurvePolyline (sb, in, state.curveResult, state.numPointsResult, colWhite, 0, 0.85f);
        }
        else
        {
            const float* cData = state.activeFilter == 2 ? state.curve2 : state.curve1;
            const int cLen = state.activeFilter == 2 ? state.numPoints2 : state.numPoints1;
            const uint32_t cCol = state.activeFilter == 2 ? colMagenta : colCyan;
            if (cData && cLen >= 2)
                drawCurvePolyline (sb, in, cData, cLen, cCol, 36, 1.0f);
        }

        // 3. Live voice markers (up to 8 voices)
        if (state.numVoices > 0)
        {
            for (int v = 0; v < state.numVoices && v < 8; ++v)
            {
                const float vHz = state.voiceCutoffs[v];
                if (vHz >= static_cast<float> (kFMin) && vHz <= static_cast<float> (kFMax))
                {
                    const float vx = static_cast<float> (in.x) + static_cast<float> (std::log10 (vHz / kFMin) / 3.0 * in.w);
                    p.line (vx, static_cast<float> (in.y + 4), vx, static_cast<float> (in.bottom() - 4), 1.0f, p.c (T.textDim, 70));
                }
            }
        }

        // 4. Modulated live cutoff line
        if (state.liveCutoffHz >= kFMin && state.liveCutoffHz <= kFMax)
        {
            const float lx = static_cast<float> (in.x) + static_cast<float> (std::log10 (state.liveCutoffHz / kFMin) / 3.0 * in.w);
            p.line (lx, static_cast<float> (in.y + 2), lx, static_cast<float> (in.bottom() - 2), 1.2f, p.c (T.flow[1], 180));
        }

        // 5. Interactive handle (cutoff & resonance)
        float hx = 0.0f, hy = 0.0f;
        handlePoint (hx, hy);
        const uint32_t handleCol = state.activeFilter == 2 ? colMagenta : colCyan;
        p.disc (hx, hy, 4.0f, p.c (handleCol, 255));
        p.arc (hx, hy, 8.5f, 0.0f, 2.0f * kPi, 1.6f, p.c (handleCol, 220));

        // Frequency labels at the bottom corners
        p.textLeft ({ in.x + 4, in.bottom() - 14, 50, 12 }, "20 Hz", T.fSmall, p.c (T.textDim, 160));
        p.textRight ({ in.right() - 54, in.bottom() - 14, 50, 12 }, "20 kHz", T.fSmall, p.c (T.textDim, 160));
    }

    void drawFormantPad (Pen& p, const Rect& in) const
    {
        const theme::Look& T = theme::L();

        // Crosshairs in center
        const float midX = static_cast<float> (in.x) + in.w * 0.5f;
        const float midY = static_cast<float> (in.y) + in.h * 0.5f;
        p.line (static_cast<float> (in.x), midY, static_cast<float> (in.right()), midY, 1.0f, p.c (T.grid, 80));
        p.line (midX, static_cast<float> (in.y), midX, static_cast<float> (in.bottom()), 1.0f, p.c (T.grid, 80));

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
            p.arc (vx, vy, 11.0f, 0.0f, 2.0f * kPi, 1.0f, p.c (T.textDim, 120));
            p.textCentered ({ static_cast<int> (vx) - 10, static_cast<int> (vy) - 10, 20, 20 }, v.name, T.fSmall + 2.0f, p.c (T.text, 200));
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
        const uint32_t col = state.activeFilter == 2 ? 0xff2e63 : 0x2ee6f0;
        p.disc (hx, hy, 5.0f, p.c (col, 255));
        p.arc (hx, hy, 12.0f, 0.0f, 2.0f * kPi, 1.8f, p.c (col, 220));

        // Corner labels
        p.textLeft ({ in.x + 4, in.y + 4, 60, 12 }, "FORMANT XY", T.fSmall, p.c (T.textDim, 160));
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
        return updateTouch (x, y);
    }

    Change onMove (int x, int y) override
    {
        return updateTouch (x, y);
    }

    Change onUp (int /*x*/, int /*y*/) override
    {
        return Change::none();
    }

    Change updateTouch (int x, int y)
    {
        const Rect in = rect_.inset (8);
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
/** FilterCompact: the compact view in the left third of the Play page (~410 x 620).
    Header: F1 | F2 | F1+F2 selector, Power, Model stepper, Route button.
    Display: FilterDisplay with full screen button in top-right.
    Bottom: Q-Group (CUTOFF, RESO, DRIVE, MIX) in a 2x2 grid. All touch targets >= 56 px. */
class FilterCompact : public Composite
{
public:
    PictogramSelector instances;
    ToggleIcon power;
    RouteButton route { "M" };
    Stepper model;
    MenuButton full { "", MenuButton::Icon::Expand };
    FilterDisplay display;

    // 4 Q-Knobs in 2x2 grid
    Knob cutoff { "CUTOFF" };
    Knob resonance { "RESO" };
    Knob drive { "DRIVE" };
    Knob mix { "MIX" };
    QGroup group;

    FilterCompact()
    {
        instances.setItem (0, -1, "F1");
        instances.setItem (1, -1, "F2");
        instances.setItem (2, -1, "F1+F2");
        instances.state.count = 3;
        instances.state.index = 0;
        model.listable = true;

        power.state.icon = ToggleIcon::Icon::Power;
        model.listable = true;

        addKid (instances);
        addKid (power);
        addKid (model);
        addKid (route);
        addKid (full);
        addKid (display);
        addKid (cutoff);
        addKid (resonance);
        addKid (drive);
        addKid (mix);

        group.set (0, &cutoff);
        group.set (1, &resonance);
        group.set (2, &drive);
        group.set (3, &mix);
        group.setActive (true);
    }

    Size minSize() const override { return { 410, 600 }; }
    Rect displayRect() const noexcept { return display.rect(); }
    QGroup& qGroup() noexcept { return group; }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        group.draw (sb);
        full.draw (sb);
    }

protected:
    void layout() override
    {
        const int m = 6, W = rect_.w - 2 * m;
        // 1. Instances selector (top): 52 px high
        instances.place ({ rect_.x + m, rect_.y + m, W, 52 });

        // 2. Header row: Power (56 px), Model stepper, Route (56 px)
        const int hy = rect_.y + m + 52 + 8;
        power.place ({ rect_.x + m, hy, 56, 52 });
        route.place ({ rect_.right() - m - 56, hy, 56, 52 });
        model.place ({ rect_.x + m + 56 + kGap, hy, std::max (60, W - 112 - 2 * kGap), 52 });

        // 3. Bottom 2x2 knob grid: height ~210 px
        const int knobH = 100;
        const int gridH = 2 * knobH + kGap;
        const int by = rect_.bottom() - m - gridH;
        Rect row1[2], row2[2];
        const int f2[2] = { 0, 0 }, w2[2] = { 1, 1 };
        layoutRow ({ rect_.x + m, by, W, knobH }, f2, w2, 2, kGap, row1);
        layoutRow ({ rect_.x + m, by + knobH + kGap, W, knobH }, f2, w2, 2, kGap, row2);
        cutoff.place (row1[0]);
        resonance.place (row1[1]);
        drive.place (row2[0]);
        mix.place (row2[1]);

        // 4. FilterDisplay: fills middle space
        const int dy = hy + 52 + 8;
        const int dh = std::max (40, by - 16 - dy); // 16 px margin for Q frame tabs
        display.place ({ rect_.x + m, dy, W, dh });
        full.place ({ display.rect().right() - 56 - 4, display.rect().y + 4, 56, 48 });
    }
};

// ===============================================================================================================================
/** FilterFullScreen: full screen overlay view for the filter element (1280 x 800, tab: 1280 x 628).
    Top bar: CLOSE, POWER, instances (F1|F2|F1+F2), MODEL, ROUTE.
    Upper-middle: large FilterDisplay (left) and ROUTING block (right).
    Lower-middle: TONE/FORMANT block, TRACK block, MAIN block.
    4 Q-Groups: MAIN, TRACK, OUT, FORMANT. All touch targets >= 56 px. */
class FilterFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 4;

    MenuButton close { "CLOSE", MenuButton::Icon::None };
    ToggleIcon power;
    PictogramSelector instances;
    Stepper model;
    RouteButton route { "M" };

    FilterDisplay display;

    // Routing block controls
    Stepper routing;
    Knob blend { "BLEND" };
    Knob level { "LEVEL" };
    Knob pan { "PAN" };

    // Tone block controls
    Knob drive { "DRIVE" };
    Knob var { "VAR" };
    Knob combOffset { "COMB" };

    // Formant block controls
    Knob formantX { "X" };
    Knob formantY { "Y" };
    Knob formantTrans { "TRANS" };
    Knob formantRes { "RESO" };
    Knob formantSpread { "SPREAD" };

    // Track block controls
    Knob keytrack { "KEYTRACK" };
    ToggleIcon env1;
    ToggleIcon cleanMode;

    // Main block controls (full screen)
    Knob cutoff { "CUTOFF" };
    Knob resonance { "RESO" };
    Knob mix { "MIX" };

    // QGroups
    QGroup groups[kMaxGroups]; // 0: MAIN, 1: TRACK, 2: OUT, 3: FORMANT

    FilterFullScreen()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        env1.state.icon = ToggleIcon::Icon::Generic;
        cleanMode.state.icon = ToggleIcon::Icon::Generic;

        instances.setItem (0, -1, "F1");
        instances.setItem (1, -1, "F2");
        instances.setItem (2, -1, "F1+F2");
        instances.state.count = 3;
        instances.state.index = 0;
        model.listable = true;

        pan.state.bipolar = true;
        keytrack.state.bipolar = true;
        formantTrans.state.bipolar = true;
        formantSpread.state.bipolar = true;

        addKid (close);
        addKid (power);
        addKid (instances);
        addKid (model);
        addKid (route);
        addKid (display);

        // Routing
        addKid (routing);
        addKid (blend);
        addKid (level);
        addKid (pan);

        // Tone
        addKid (drive);
        addKid (var);
        addKid (combOffset);

        // Formant
        addKid (formantX, false);
        addKid (formantY, false);
        addKid (formantTrans, false);
        addKid (formantRes, false);
        addKid (formantSpread, false);

        // Track
        addKid (keytrack);
        addKid (env1);
        addKid (cleanMode);

        // Main
        addKid (cutoff);
        addKid (resonance);
        addKid (mix);

        setFormantMode (false);
        buildGroups();
    }

    void setFormantMode (bool isFormant)
    {
        isFormant_ = isFormant;
        display.state.isFormant = isFormant;

        show (drive, ! isFormant);
        show (var, ! isFormant);
        show (combOffset, ! isFormant);

        show (formantX, isFormant);
        show (formantY, isFormant);
        show (formantTrans, isFormant);
        show (formantRes, isFormant);
        show (formantSpread, isFormant);

        layout();
        buildGroups();
    }
    bool formantMode() const noexcept { return isFormant_; }

    Size minSize() const override { return { 1280, 800 }; }
    Rect displayRect() const noexcept { return display.rect(); }

    int numGroups() const noexcept { return kMaxGroups; }
    const char* groupName (int i) const noexcept
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
        {
            for (int i = 0; i < kMaxGroups; ++i)
            {
                bool found = false;
                for (int s = 0; s < QGroup::kSlots; ++s)
                    if (groups[i].member (s) == cap_)
                    {
                        found = true;
                        break;
                    }
                if (found)
                {
                    setActiveGroup (i);
                    break;
                }
            }
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
        const int m = 8;
        const int W = rect_.w;

        // 1. Top bar: CLOSE, POWER, INSTANCES, MODEL, ROUTE
        const int topH = 52;
        const Rect top { rect_.x + m, rect_.y + m + 16, W - 2 * m, topH };
        Rect rTop[5];
        const int fTop[5] = { 84, 56, 220, 0, 64 };
        const int wTop[5] = { 0, 0, 0, 1, 0 };
        layoutRow (top, fTop, wTop, 5, kGap, rTop);
        close.place (rTop[0]);
        power.place (rTop[1]);
        instances.place (rTop[2]);
        model.place (rTop[3]);
        route.place (rTop[4]);

        // 2. Middle area: Display (left) and ROUTING block (right)
        const int midY = top.bottom() + m + 8;
        const int bottomH = 240;
        const int midH = std::max (60, rect_.bottom() - m - bottomH - m - midY);
        const int routingW = 320;
        const int dispW = W - 2 * m - routingW - kGap;

        display.place ({ rect_.x + m, midY, dispW, midH });

        // Routing block
        const Rect rBox { rect_.x + m + dispW + kGap, midY, routingW, midH };
        routingBlock_ = rBox;
        routing.place ({ rBox.x + 4, rBox.y + 4, rBox.w - 8, 48 });
        const int rKnobY = rBox.y + 4 + 48 + kGap;
        const int rKnobH = std::max (50, rBox.bottom() - 4 - rKnobY);
        Rect rKnobs[3];
        const int f3[3] = { 0, 0, 0 }, w3[3] = { 1, 1, 1 };
        layoutRow ({ rBox.x + 4, rKnobY, rBox.w - 8, rKnobH }, f3, w3, 3, kGap, rKnobs);
        blend.place (rKnobs[0]);
        level.place (rKnobs[1]);
        pan.place (rKnobs[2]);

        // 3. Bottom area: TONE/FORMANT (left), TRACK (middle), MAIN (right)
        const int botY = rect_.bottom() - m - bottomH;
        const int botW = W - 2 * m;

        const int toneW = isFormant_ ? 460 : 380;
        const int trackW = 280;
        const int mainW = botW - toneW - trackW - 2 * kGap;

        // Left block: Tone or Formant
        toneBlock_ = { rect_.x + m, botY, toneW, bottomH };
        if (isFormant_)
        {
            Rect fRow1[3], fRow2[2];
            const int fRowH = (bottomH - 16 - kGap) / 2;
            const int f3a[3] = { 0, 0, 0 }, w3a[3] = { 1, 1, 1 };
            const int f2a[2] = { 0, 0 }, w2a[2] = { 1, 1 };
            layoutRow ({ toneBlock_.x + 4, toneBlock_.y + 12, toneBlock_.w - 8, fRowH }, f3a, w3a, 3, kGap, fRow1);
            layoutRow ({ toneBlock_.x + 4, toneBlock_.y + 12 + fRowH + kGap, toneBlock_.w - 8, fRowH }, f2a, w2a, 2, kGap, fRow2);
            formantX.place (fRow1[0]);
            formantY.place (fRow1[1]);
            formantTrans.place (fRow1[2]);
            formantRes.place (fRow2[0]);
            formantSpread.place (fRow2[1]);
        }
        else
        {
            Rect tKnobs[3];
            const int ft3[3] = { 0, 0, 0 }, wt3[3] = { 1, 1, 1 };
            layoutRow ({ toneBlock_.x + 4, toneBlock_.y + 20, toneBlock_.w - 8, bottomH - 28 }, ft3, wt3, 3, kGap, tKnobs);
            drive.place (tKnobs[0]);
            var.place (tKnobs[1]);
            combOffset.place (tKnobs[2]);
        }

        // Middle block: Track (KEYTRACK, ENV1, CLEAN)
        trackBlock_ = { toneBlock_.right() + kGap, botY, trackW, bottomH };
        keytrack.place ({ trackBlock_.x + 8, trackBlock_.y + 20, 110, bottomH - 28 });
        const int togW = trackBlock_.w - 110 - 24;
        const int togH = 52;
        env1.place ({ trackBlock_.right() - 8 - togW, trackBlock_.y + 30, togW, togH });
        cleanMode.place ({ trackBlock_.right() - 8 - togW, trackBlock_.y + 30 + togH + kGap, togW, togH });

        // Right block: Main (CUTOFF, RESO, MIX)
        mainBlock_ = { trackBlock_.right() + kGap, botY, mainW, bottomH };
        Rect mKnobs[3];
        const int fm3[3] = { 0, 0, 0 }, wm3[3] = { 1, 1, 1 };
        layoutRow ({ mainBlock_.x + 4, mainBlock_.y + 20, mainBlock_.w - 8, bottomH - 28 }, fm3, wm3, 3, kGap, mKnobs);
        cutoff.place (mKnobs[0]);
        resonance.place (mKnobs[1]);
        mix.place (mKnobs[2]);
    }

    void buildGroups()
    {
        for (int i = 0; i < kMaxGroups; ++i)
            for (int k = 0; k < 4; ++k)
                groups[i].set (k, nullptr);

        // Group 0: MAIN (Cutoff, Reso, Drive / Formant X, Mix)
        groups[0].set (0, &cutoff);
        groups[0].set (1, &resonance);
        groups[0].set (2, isFormant_ ? static_cast<Component*> (&formantX) : static_cast<Component*> (&drive));
        groups[0].set (3, &mix);

        // Group 1: TRACK (Keytrack, Env1, Var, Comb)
        groups[1].set (0, &keytrack);
        groups[1].set (1, &env1);
        groups[1].set (2, &var);
        groups[1].set (3, &combOffset);

        // Group 2: OUT (Level, Pan, Blend, Routing)
        groups[2].set (0, &level);
        groups[2].set (1, &pan);
        groups[2].set (2, &blend);
        groups[2].set (3, &routing);

        // Group 3: FORMANT (X, Y, Transpose, Spread)
        groups[3].set (0, &formantX);
        groups[3].set (1, &formantY);
        groups[3].set (2, &formantTrans);
        groups[3].set (3, &formantSpread);

        activeGroup_ = -1;
        setActiveGroup (0);
    }

    void drawUnder (Pen& p) const override
    {
        p.block (routingBlock_);
        p.block (toneBlock_);
        p.block (trackBlock_);
        p.block (mainBlock_);
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ routingBlock_.x + 8, routingBlock_.y + 3, routingBlock_.w - 16, 16 }, "ROUTING", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        p.textLeft ({ toneBlock_.x + 8, toneBlock_.y + 3, toneBlock_.w - 16, 16 }, isFormant_ ? "FORMANT" : "TONE", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        p.textLeft ({ trackBlock_.x + 8, trackBlock_.y + 3, trackBlock_.w - 16, 16 }, "TRACK", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        p.textLeft ({ mainBlock_.x + 8, mainBlock_.y + 3, mainBlock_.w - 16, 16 }, "MAIN", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
    }

    bool isFormant_ = false;
    int activeGroup_ = 0;
    Rect routingBlock_, toneBlock_, trackBlock_, mainBlock_;
};

}} // namespace synthyboy_mpc::ui
