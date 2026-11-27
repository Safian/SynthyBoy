// SynthyBoy - MPC UI component library: the parts of the LFO element that the oscillator did not need: ShapePicker (the 12 built-in shapes and the drawn one, as a grid of cells with the shape drawn in
// each), SyncSwitch (the logic of HZ / BPM / KEY with TRIP and DOT), RateControl (the rate knob or the division stepper, whichever the sync mode says). The selector of the LFO is ui::UsedSelector (used_selector.h). Everything else is built from the base components (base.h) and the composites of the oscillator.
// Same contract as every component (core.h): a state and a rectangle in, a drawing and a change { id, value } out.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "led_list.h"
#include "led_toggle.h"
#include "lfo_model.h"
#include "panel.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
namespace lfoglyph
{
/** The picture of a shape in a box of about 30 x 20 pixels around (cx, cy): the built-in shapes sampled from the function the engine reads (lfo::builtinAt), the drawn one as a scribble with a point. */
inline void draw (Pen& p, int shape, float cx, float cy, float w, float h, uint32_t col)
{
    p.style (1.0f, true);
    if (shape >= 0 && shape < lfo::kNumBuiltin)
    {
        const int n = 24;
        float px = cx - w * 0.5f, py = cy - 0.5f * h * lfo::builtinAt (shape, 0.0f);
        for (int i = 1; i <= n; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (n), x = cx - w * 0.5f + w * t, y = cy - 0.5f * h * lfo::builtinAt (shape, std::min (t, 0.9999f));
            p.line (px, py, x, y, 2.0f, col);
            px = x; py = y;
        }
    }
    else
    {
        static const float k[6][2] = { { -0.5f, 0.35f }, { -0.3f, -0.45f }, { -0.05f, 0.25f }, { 0.2f, -0.35f }, { 0.35f, 0.15f }, { 0.5f, -0.1f } };
        for (int i = 0; i + 1 < 6; ++i)
            p.line (cx + k[i][0] * w, cy + k[i][1] * h, cx + k[i + 1][0] * w, cy + k[i + 1][1] * h, 2.0f, col);
        p.disc (cx + k[3][0] * w, cy + k[3][1] * h, 3.0f, col);
    }
    p.plain();
}
inline const char* name (int shape) noexcept
{
    static const char* const n[lfo::kNumShapes] = { "SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "PULSE", "STEP UP", "STEP DN", "RANDOM", "EXP UP", "EXP DN", "HALF", "DRAWN" };
    return shape >= 0 && shape < lfo::kNumShapes ? n[shape] : "";
}
}

// ===============================================================================================================================================================
/** The pictures of the styles of a random LFO (Perlin noise, sample and hold, sine interpolation, the Lorenz attractor, the Rossler attractor) in the same box as the shapes. */
inline void drawStyleGlyph (Pen& p, int style, float cx, float cy, float w, float h, uint32_t col)
{
    p.style (1.0f, true);
    const int n = 28;
    float px = 0.0f, py = 0.0f;
    for (int i = 0; i <= n; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (n);
        float x = cx - w * 0.5f + w * t, y = cy;
        switch (style)
        {
            case 0: y = cy - h * 0.5f * (0.6f * std::sin (6.2832f * 1.3f * t + 0.5f) + 0.4f * std::sin (6.2832f * 2.9f * t + 1.7f)); break;
            case 1: { static const float lv[6] = { 0.3f, -0.7f, 0.6f, -0.2f, -0.8f, 0.5f }; y = cy - h * 0.5f * lv[std::min (5, static_cast<int> (t * 6.0f))]; break; }
            case 2: y = cy - h * 0.5f * (std::sin (6.2832f * 2.0f * t) * (0.5f + 0.5f * std::sin (6.2832f * 0.7f * t))); break;
            case 3: { const float a = 6.2832f * t; x = cx + w * 0.5f * 0.8f * std::sin (a) * std::cos (0.5f * a); y = cy - h * 0.5f * std::sin (2.0f * a) * 0.8f; break; }
            default: { const float a = 6.2832f * 2.0f * t; const float r = 0.25f + 0.75f * t; x = cx + w * 0.5f * r * std::cos (a); y = cy + h * 0.5f * r * std::sin (a); break; }
        }
        if (i > 0)
            p.line (px, py, x, y, 2.0f, col);
        px = x; py = y;
    }
    p.plain();
}
inline const char* styleName (int style) noexcept
{
    static const char* const n[5] = { "PERLIN", "S & H", "SINE INT", "LORENZ", "ROSSLER" };
    return style >= 0 && style < 5 ? n[style] : "";
}

/** ShapePicker. In: the current index, the number of columns of the grid, `styles` (false: the 13 shapes of an LFO, 12 built-in and the drawn one; true: the 5 styles of a random LFO). Draw: one cell per item (a
    grid of `cols` columns), its picture with its name under it, the current one highlighted. Out: an Index change (the new item) on a tap (on the touch up, from the cell the touch went down in); turn
    (delta) steps through the items (no wrap). */
class ShapePicker : public Component
{
public:
    struct State { int index = 0, cols = 4; bool styles = false; };
    State state;
    static constexpr int kCells = lfo::kNumShapes, kStyles = 5;

    int count() const noexcept { return state.styles ? kStyles : kCells; }
    int rows() const noexcept { return (count() + std::max (1, state.cols) - 1) / std::max (1, state.cols); }
    /** The cell of item i (i may be beyond the last item: the free cells of the last row, for the owner's own buttons). */
    Rect cell (int i) const noexcept
    {
        const int cols = std::max (1, state.cols), r = rows(), c = i % cols, rw = i / cols;
        return { rect_.x + rect_.w * c / cols, rect_.y + rect_.h * rw / r, rect_.w * (c + 1) / cols - rect_.w * c / cols, rect_.h * (rw + 1) / r - rect_.h * rw / r };
    }
    Size minSize() const override { return { std::max (1, state.cols) * 56, rows() * 56 }; }
    int touchTargets (Rect* out, int max) const override { int n = 0; for (int i = 0; i < count() && n < max; ++i) out[n++] = cell (i); return n; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        for (int i = 0; i < count(); ++i)
        {
            const Rect c = cell (i).inset (2);
            const bool cur = i == state.index;
            p.control (c, qFrame_ && cur, cur);
            if (cur)
                p.rrect (c, T.rControl, p.c (qFrame_ ? T.q : T.flow[0], 255), 1.6f);
            const int labelH = c.h >= 44 ? 17 : 0;
            const float gw = std::min (30.0f, static_cast<float> (c.w) - 14.0f), gh = std::min (20.0f, static_cast<float> (c.h - labelH) - 10.0f);
            const float gx = static_cast<float> (c.x) + static_cast<float> (c.w) * 0.5f, gy = static_cast<float> (c.y) + static_cast<float> (c.h - labelH) * 0.5f;
            const uint32_t glyphCol = p.c (cur ? (qFrame_ ? T.q : T.flow[0]) : T.textDim, 255);
            if (state.styles)
                drawStyleGlyph (p, i, gx, gy, gw, gh, glyphCol);
            else
                lfoglyph::draw (p, i, gx, gy, gw, gh, glyphCol);
            if (labelH > 0)
                p.textCentered ({ c.x, c.bottom() - labelH - 1, c.w, labelH }, state.styles ? styleName (i) : lfoglyph::name (i), T.fLabel - 3.0f, p.c (cur ? (qFrame_ ? T.q : T.text) : T.textDim, 255), T.fMin);
        }
    }
    int cellAt (int x, int y) const noexcept { for (int i = 0; i < count(); ++i) if (cell (i).contains (x, y)) return i; return -1; }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); downCell_ = cellAt (x, y); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override
    {
        if (tap_.up() == 0 || downCell_ < 0 || downCell_ == state.index)
            return Change::none();
        return Change::indexed (id_, downCell_, downCell_);
    }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        const int n = std::clamp (state.index + static_cast<int> (std::clamp<long> (d, -100, 100)), 0, count() - 1);
        return d == 0 || n == state.index ? Change::none() : Change::indexed (id_, n, n);
    }

private:
    TapGesture tap_;
    int downCell_ = -1;
};

// ===============================================================================================================================================================
/** SyncSwitch: the sync of an LFO as the player thinks of it. The mode HZ / BPM / KEY is a LED list of three (`mode`, the Q member of the TIMING block: a knob step moves the selection); the switches TRIP and DOT
    (`trip`, `dot`, LED toggles that only work in BPM mode) are drawn by the RateControl next to the division, where they act (`RateControl::trip`, `dot`; this class only holds the logic). In: the sync value of
    the parameter (0 Free, 1 Tempo, 2 dotted, 3 triplet, 4 Key follow: `setSync`). Out: `nextSync (part, change)` turns a change of the list (part Mode) or of one of the two switches into the sync value to write
    (the mode keeps the switches, a switch excludes the other). It is no component: the screen owns the three controls and adds them as its own children. */
class SyncSwitch
{
public:
    enum Part : int { Mode = 0, Trip = 1, Dot = 2 };
    LedList mode;

    SyncSwitch()
    {
        static const char* const names[3] = { "HZ", "BPM", "KEY" };
        mode.setOptions (names, 3);
    }
    void setSync (int sync)
    {
        sync_ = sync;
        mode.state.index = static_cast<int> (lfo::syncModeOf (sync));
    }
    int sync() const noexcept { return sync_; }
    bool trip() const noexcept { return lfo::syncTrip (sync_); }
    bool dot() const noexcept { return lfo::syncDot (sync_); }
    /** The sync value after a change of the list (`Mode`) or of a switch (`Trip`, `Dot`); the current one when the change is none of the three. */
    int nextSync (int part, const Change& c) const noexcept
    {
        using M = lfo::SyncMode;
        M m = lfo::syncModeOf (sync_);
        bool tr = lfo::syncTrip (sync_), dt = lfo::syncDot (sync_);
        if (part == Mode && c.kind == ChangeKind::Index)
            m = static_cast<M> (std::clamp (c.index, 0, 2));
        else if (part == Trip && c.kind == ChangeKind::Index) { tr = c.index != 0; if (tr) dt = false; }
        else if (part == Dot && c.kind == ChangeKind::Index) { dt = c.index != 0; if (dt) tr = false; }
        return lfo::syncOf (m, tr, dt);
    }

private:
    int sync_ = 0;
};

// ===============================================================================================================================================================
/** RateControl: the speed of an LFO. In the Hz mode the knob (RATE, in Hz), in the BPM mode the division stepper (1/4 note, 1/8 note ...) and, when the screen asks for them (`setTripDot`), the two LED toggles
    TRIP and DOT under it, in the KEY mode the knob drawn dim (the rate follows the key: the knob has nothing to say). One component for the compact view and the full screen; the Q group member is the whole
    control, the hardware knob turns whichever of the two is shown (`primary`). With `setShowMode (true)` (the compact view, which has no sync list of its own) the LED list HZ / BPM / KEY stands at the right
    of it: the sync mode is one touch away. */
class RateControl : public Composite
{
public:
    Knob rate { "RATE" };
    Stepper division;
    LedList modeCells;
    LedToggle trip, dot;   // in the BPM mode, under the division (full screen only)
    static constexpr int kModeW = 3 * 70;

    RateControl()
    {
        division.setCaption ("DIVISION"); division.state.wrap = false; division.listable = true;
        static const char* const names[3] = { "HZ", "BPM", "KEY" };
        modeCells.setOptions (names, 3);
        trip.setLabel ("TRIP");
        dot.setLabel ("DOT");
        addKid (rate); addKid (division, false); addKid (modeCells, false); addKid (trip, false); addKid (dot, false);
        setPrimary (&rate);
    }
    void setShowMode (bool on) { showMode_ = on; show (modeCells, on); layout(); }
    void setModeCellsVisible (bool visible) { show (modeCells, showMode_ && visible); }
    /** The full screen: TRIP and DOT under the division in the BPM mode (their state comes with `setSync`). */
    void setTripDot (bool on) { tripDot_ = on; refreshShown(); layout(); }
    void setSync (int sync)
    {
        trip.state.on = lfo::syncTrip (sync);
        dot.state.on = lfo::syncDot (sync);
    }
    void setMode (lfo::SyncMode m)
    {
        const bool changed = m != mode_;
        mode_ = m;
        show (rate, m != lfo::SyncMode::Bpm);
        show (division, m == lfo::SyncMode::Bpm);
        setPrimary (m == lfo::SyncMode::Bpm ? static_cast<ControlModel*> (&division) : &rate);
        rate.setLabel (m == lfo::SyncMode::Key ? "KEY" : "RATE");
        modeCells.state.index = static_cast<int> (m);
        refreshShown();
        if (changed)
            layout();   // the division moves to the top of the cell in the BPM mode (TRIP and DOT under it)
        syncKids();
    }
    lfo::SyncMode mode() const noexcept { return mode_; }
    Size minSize() const override { return { Stepper::kMinW + (showMode_ ? kGap + kModeW : 0), 72 }; }

protected:
    void refreshShown() { const bool on = tripDot_ && mode_ == lfo::SyncMode::Bpm; show (trip, on); show (dot, on); }
    void syncKids() override { rate.setEnabled (enabled_ && mode_ != lfo::SyncMode::Key); division.setEnabled (enabled_); modeCells.setEnabled (enabled_); trip.setEnabled (enabled_); dot.setEnabled (enabled_); }
    void layout() override
    {
        Rect main = rect_;
        if (showMode_)
        {
            const int h = std::min (rect_.h, led::kRowH);
            modeCells.place ({ rect_.right() - kModeW, rect_.y + (rect_.h - h) / 2, kModeW, h });
            main = { rect_.x, rect_.y, std::max (0, rect_.w - kModeW - kGap), rect_.h };
        }
        if (rate.state.large)
            rate.state.side = false;
        else
            rate.state.side = main.h < 108 || main.w >= 2 * main.h;
        rate.place (main);
        if (tripDot_ && mode_ == lfo::SyncMode::Bpm)
        {
            // the division with TRIP and DOT in one row under it (they act on it), the two together in the middle of the cell
            const int h = std::min (main.h, 64), bh = led::kRowH, bw = (main.w - kGap) / 2, y0 = main.y + std::max (0, (main.h - (h + 12 + bh)) / 2), by = y0 + h + 12;
            division.place ({ main.x, y0, main.w, h });
            trip.place ({ main.x, by, bw, bh });
            dot.place ({ main.x + bw + kGap, by, main.w - bw - kGap, bh });
        }
        else
        {
            const int h = std::min (main.h, 64);
            division.place ({ main.x, main.y + (main.h - h) / 2, main.w, h });
            trip.place ({});
            dot.place ({});
        }
    }
    lfo::SyncMode mode_ = lfo::SyncMode::Hz;
    bool showMode_ = false, tripDot_ = false;
};

}} // namespace synthyboy_mpc::ui
