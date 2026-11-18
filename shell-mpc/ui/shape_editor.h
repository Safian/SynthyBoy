// SynthyBoy - MPC UI component library: the SHAPE EDITOR of the LFO: ToolSelector (POINT, FLAT, RAMP UP, RAMP DOWN, SELECT, SOFT, BEND), ShapeCanvas (the drawing surface: the shape with its grid, its points, the middles of
// its segments and the marker of the start / loop position; touch edits the shape model of lfo_model.h), ShapeEditorPage (the whole page: the back button, the tools, SHAPES, MORE, UNDO, DELETE, MOD, the Q group "EDIT"
// (POINT, X, Y, CURVE of the selected point; with several points selected: their count, the centre of the selection, its height), the Q group "GRID" (GRID X, GRID Y, SNAP), the Q group "TRIGGER" (MODE, PHASE, BEND ALL), LOOP and
// SMOOTH) and PointModPage (the modulation of the time, the level or the curve of the selected points: the source board). The component never stores the truth: the model is the owner's (`ShapeCanvas::State::shape` points
// at it and the canvas edits it IN PLACE, by design: the owner writes it to the content state after every change the canvas reports), so is the undo stack (the canvas pushes the state before the first change of a gesture:
// one undo state per gesture), and so are the trigger values (the PHASE and the MODE belong to the engine's parameters: the owner hands them in with `setTrigger`).
// Gestures of the canvas (single finger). Tool POINT: a tap on empty ground adds a point there (snapped to the grid when SNAP is on) and the finger keeps it: dragging moves it; a touch on a point selects it and drags it
// (the two end points only up and down; a selected group moves together); a touch on the small handle in the middle of a segment drags its curve (the finger is where the middle of the segment goes: power -20 .. 20); a double
// tap on a point deletes it (never an end point, never the point the first tap of the pair has just added), a double tap on a curve handle resets the curve; a long press (600 ms without moving) on a point deletes it as well.
// Tools FLAT / RAMP UP / RAMP DOWN: a touch on a cell of the grid draws a flat piece (at the height of the finger, on a grid line with SNAP) or a ramp (from the lower to the upper line of the cell, up or down) over the column
// of the cell, replacing what was there; a drag across the columns draws a staircase or a saw tooth: EVERY column between two events is drawn (the height of the finger interpolated), the whole drag is one undo state.
// Tool SELECT: a drag on empty ground draws a rectangle that selects the points inside; a tap on a point adds it to the selection or removes it; a tap on empty ground clears it; dragging a selected point moves the whole selection.
// Tool SOFT (relative movement): dragging a point moves the selected points (two or more), or every point within 25 percent of the cycle, along with it, the nearer the more (drawn in rainbow colours by their share).
// Tool BEND: dragging the middle handle of a segment bends ALL curves by the same amount. The marker (a vertical line with a handle of 56 px at its top, tools POINT, SELECT, SOFT, BEND): the owner gives the position and the kind
// (START, LOOP, dimmed); dragging it reports a Marker change; in the LOOP kind it snaps to the nearest point and sets the loop point of the model to it.
// Output: Index changes { id of the canvas, index = Op }: Edited = the model changed (write it), Selected = only the selection moved, Marker = the marker moved (value = its position 0 .. 1; the model may have changed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "led_toggle.h"
#include "lfo_display.h"
#include "lfo_model.h"
#include "mod_board.h"
#include "panel.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class ToolSelector : public Component
{
public:
    enum Tool : int { Point = 0, Flat, RampUp, RampDown, Select, Soft, Bend, kTools };
    struct State { int index = 0; };
    State state;
    Rect cell (int i) const noexcept { return { rect_.x + rect_.w * i / kTools, rect_.y, rect_.w * (i + 1) / kTools - rect_.w * i / kTools, rect_.h }; }
    Size minSize() const override { return { kTools * 72, 56 }; }
    int touchTargets (Rect* out, int max) const override { int n = 0; for (int i = 0; i < kTools && n < max; ++i) out[n++] = cell (i); return n; }
    static const char* name (int t) noexcept { static const char* const n[kTools] = { "POINT", "FLAT", "RAMP+", "RAMP-", "SELECT", "SOFT", "BEND" }; return n[std::clamp (t, 0, kTools - 1)]; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        for (int i = 0; i < kTools; ++i)
        {
            const Rect c = cell (i).inset (2);
            const bool cur = i == state.index;
            p.control (c, false, cur);
            if (cur)
                p.rrect (c, T.rControl, p.c (T.flow[0], 255), 1.6f);
            const uint32_t col = p.c (cur ? T.flow[0] : T.textDim, 255);
            const float cx = static_cast<float> (c.x) + static_cast<float> (c.w) * 0.5f, cy = static_cast<float> (c.y) + static_cast<float> (c.h - 17) * 0.5f;
            p.style (1.0f, true);
            switch (i)
            {
                case Point: p.line (cx - 12.0f, cy + 5.0f, cx, cy - 5.0f, 2.0f, col); p.line (cx, cy - 5.0f, cx + 12.0f, cy + 3.0f, 2.0f, col); p.disc (cx, cy - 5.0f, 3.2f, col); break;
                case Flat: p.line (cx - 12.0f, cy, cx + 12.0f, cy, 2.0f, col); p.line (cx - 12.0f, cy - 5.0f, cx - 12.0f, cy + 5.0f, 1.0f, col); p.line (cx + 12.0f, cy - 5.0f, cx + 12.0f, cy + 5.0f, 1.0f, col); break;
                case RampUp: p.line (cx - 10.0f, cy + 6.0f, cx + 10.0f, cy - 6.0f, 2.0f, col); p.line (cx + 10.0f, cy - 6.0f, cx + 10.0f, cy + 6.0f, 1.0f, col); break;
                case RampDown: p.line (cx - 10.0f, cy - 6.0f, cx + 10.0f, cy + 6.0f, 2.0f, col); p.line (cx - 10.0f, cy - 6.0f, cx - 10.0f, cy + 6.0f, 1.0f, col); break;
                case Select:   // the rubber band with two points inside
                    p.line (cx - 12.0f, cy - 6.0f, cx + 12.0f, cy - 6.0f, 1.2f, col); p.line (cx + 12.0f, cy - 6.0f, cx + 12.0f, cy + 6.0f, 1.2f, col);
                    p.line (cx + 12.0f, cy + 6.0f, cx - 12.0f, cy + 6.0f, 1.2f, col); p.line (cx - 12.0f, cy + 6.0f, cx - 12.0f, cy - 6.0f, 1.2f, col);
                    p.disc (cx - 5.0f, cy, 2.6f, col); p.disc (cx + 5.0f, cy - 1.0f, 2.6f, col);
                    break;
                case Soft:   // a hill: the points near the top follow
                    p.line (cx - 12.0f, cy + 6.0f, cx - 5.0f, cy + 3.0f, 2.0f, col); p.line (cx - 5.0f, cy + 3.0f, cx, cy - 6.0f, 2.0f, col);
                    p.line (cx, cy - 6.0f, cx + 5.0f, cy + 3.0f, 2.0f, col); p.line (cx + 5.0f, cy + 3.0f, cx + 12.0f, cy + 6.0f, 2.0f, col);
                    p.disc (cx, cy - 6.0f, 3.0f, col);
                    break;
                default:   // two curves bent together
                    p.arc (cx - 6.0f, cy + 8.0f, 11.0f, -1.2f, -0.2f, 2.0f, col); p.arc (cx + 6.0f, cy - 8.0f, 11.0f, 1.9f, 2.9f, 2.0f, col);
                    break;
            }
            p.plain();
            p.textCentered ({ c.x, c.bottom() - 17, c.w, 16 }, name (i), T.fLabel - 3.0f, p.c (cur ? T.text : T.textDim, 255), T.fMin);
        }
    }
    int cellAt (int x, int y) const noexcept { for (int i = 0; i < kTools; ++i) if (cell (i).contains (x, y)) return i; return -1; }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); down_ = cellAt (x, y); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 && down_ >= 0 && down_ != state.index ? Change::indexed (id_, down_, down_) : Change::none(); }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        const int n = std::clamp (state.index + static_cast<int> (std::clamp<long> (d, -10, 10)), 0, kTools - 1);
        return d == 0 || n == state.index ? Change::none() : Change::indexed (id_, n, n);
    }

private:
    TapGesture tap_;
    int down_ = -1;
};

// ===============================================================================================================================================================
class ShapeCanvas : public Component
{
public:
    enum Op : int { Edited = 1, Selected = 2, Marker = 3 };
    /** What the marker is (the owner knows from the trigger mode of the LFO): nothing, dimmed (the mode does not use it: a hint says so), the START position (retrigger, envelope), the LOOP position (the loop modes). */
    enum MarkerKind : int { MarkerNone = 0, MarkerDim, MarkerStart, MarkerLoop };
    struct State
    {
        lfo::ShapeModel* shape = nullptr;   // the owner's model: the canvas edits it in place and reports
        lfo::Undo* undo = nullptr;           // the owner's undo stack: the canvas pushes the state before a change
        int tool = ToolSelector::Point;
        int gridX = 8, gridY = 8;           // divisions of the cycle (columns) and of the height (rows), lfo::kMinGrid .. kMaxGrid: drawn as the grid, used by SNAP and by the cell tools
        bool snap = true;
        int selected = -1;                   // the selected point (-1 none): the one the fields show
        int selectedSeg = -1;                // the segment whose middle was touched last (-1 none)
        lfo::PointSet marks;                 // the selection when it has points of its own (the SELECT tool, a group): when it is empty the selection is the point `selected`
        float markerX = -1.0f;               // the position of the marker 0 .. 1 (< 0: none), from the owner
        int markerKind = MarkerNone;
    };
    State state;
    static constexpr int kHitR = 28, kMidR = 22, kLongPressMs = 600, kMarkerR = 28, kMarkerZone = 56, kSoftRadiusPercent = 25;

    Size minSize() const override { return { 400, 240 }; }
    lfodisplay::Data data() const
    {
        lfodisplay::Data d;
        d.kind = lfodisplay::Data::Kind::Shape;
        d.editing = true;
        d.gridX = std::clamp (state.gridX, lfo::kMinGrid, lfo::kMaxGrid);
        d.gridY = std::clamp (state.gridY, lfo::kMinGrid, lfo::kMaxGrid);
        return d;
    }
    Rect plot() const { return lfodisplay::plotRect (rect_, data()); }
    // the screen position of a point (x, y in 0 .. 1 of the shape) and back
    float xOf (float x) const { return lfodisplay::colOf (plot(), x); }
    float yOf (float y) const { return lfodisplay::rowOf (plot(), 2.0f * y - 1.0f); }
    float xFrom (int px) const { const Rect r = plot(); return std::clamp ((static_cast<float> (px) - static_cast<float> (r.x)) / static_cast<float> (std::max (1, r.w - 1)), 0.0f, 1.0f); }
    float yFrom (int py) const
    {
        const Rect r = plot();
        const float amp = static_cast<float> (r.h) * 0.5f * 0.9f, mid = static_cast<float> (r.y) + static_cast<float> (r.h) * 0.5f;
        return std::clamp (0.5f + 0.5f * (mid - static_cast<float> (py)) / amp, 0.0f, 1.0f);
    }
    /** The point under (px, py) within the hit radius (the nearest), -1 none; the segment whose middle handle is there, -1 none. */
    int pointAt (int px, int py) const
    {
        if (state.shape == nullptr) return -1;
        int best = -1;
        float bd = static_cast<float> (kHitR * kHitR);
        for (int i = 0; i < state.shape->n; ++i)
        {
            const float dx = xOf (state.shape->p[i].x) - static_cast<float> (px), dy = yOf (state.shape->p[i].y) - static_cast<float> (py), dd = dx * dx + dy * dy;
            if (dd <= bd) { bd = dd; best = i; }
        }
        return best;
    }
    bool midHandle (int seg, float& x, float& y) const
    {
        if (state.shape == nullptr || seg < 0 || seg + 1 >= state.shape->n) return false;
        const lfo::Point& a = state.shape->p[seg];
        const lfo::Point& b = state.shape->p[seg + 1];
        if (b.x - a.x < 0.02f || std::fabs (b.y - a.y) < 0.01f) return false;   // a vertical or a flat segment has no curve to drag
        x = xOf (0.5f * (a.x + b.x));
        y = yOf (state.shape->eval (0.5f * (a.x + b.x)));
        return true;
    }
    int segAt (int px, int py) const
    {
        if (state.shape == nullptr) return -1;
        for (int i = 0; i + 1 < state.shape->n; ++i)
        {
            float x, y;
            if (midHandle (i, x, y) && (x - static_cast<float> (px)) * (x - static_cast<float> (px)) + (y - static_cast<float> (py)) * (y - static_cast<float> (py)) <= static_cast<float> (kMidR * kMidR))
                return i;
        }
        return -1;
    }
    /** The selection as a set: the points of `marks`, or the point `selected` when `marks` has none (points beyond the shape are left out). */
    lfo::PointSet selection() const
    {
        lfo::PointSet s;
        if (state.shape == nullptr)
            return s;
        for (int i = 0; i < state.shape->n; ++i)
            if (state.marks.has (i))
                s.add (i);
        if (! s.any() && state.selected >= 0 && state.selected < state.shape->n)
            s.add (state.selected);
        return s;
    }
    /** Selects one point (-1: none) and forgets every other selected point and segment. */
    void selectOnly (int i)
    {
        state.marks.clear();
        state.selected = state.shape != nullptr && i >= 0 && i < state.shape->n ? i : -1;
        state.selectedSeg = -1;
    }
    /** Selects every point. */
    void selectAll()
    {
        state.marks.clear();
        if (state.shape == nullptr)
            return;
        state.marks.selectAll (state.shape->n);
        state.selected = state.shape->n > 0 ? 0 : -1;
        state.selectedSeg = -1;
    }
    /** The marker's handle: the zone at the top of its line that takes the touch (56 px). */
    Rect markerZone() const
    {
        const Rect pl = plot();
        const int mx = static_cast<int> (std::lround (xOf (std::clamp (state.markerX, 0.0f, 1.0f))));
        return { mx - kMarkerR, pl.y, 2 * kMarkerR, kMarkerZone };
    }
    bool markerLive() const noexcept { return state.markerX >= 0.0f && (state.markerKind == MarkerStart || state.markerKind == MarkerLoop); }
    /** The text next to the marker. */
    static const char* markerCaption (int kind) noexcept { return kind == MarkerLoop ? "LOOP" : (kind == MarkerStart ? "START" : ""); }
    static const char* markerHint (int kind) noexcept { return kind == MarkerDim ? "FREE-RUN: PHASE SHIFTS THE CYCLE" : ""; }

protected:
    void drawBody (Pen& p) const override
    {
        (void) p;
    }

public:
    void draw (gpu::SceneBuilder& sb) const override
    {
        if (state.shape == nullptr)
            return;
        const theme::Look& T = theme::L();
        Pen pen (sb, enabled_);
        lfodisplay::Data d = data();
        float y[256];
        for (int i = 0; i < 256; ++i)
            y[i] = 2.0f * state.shape->eval (static_cast<float> (i) / 255.0f) - 1.0f;
        d.y = y;
        d.n = 256;
        lfodisplay::draw (sb, rect_, d, false);   // the loop is the marker below: the display draws none here
        const Rect pl = plot();
        Cell cell (sb, rect_);
        const lfo::ShapeModel& m = *state.shape;
        // the modulation of the points: a bar from the value to the value plus the amount (time: along the axis, level: up and down)
        for (int i = 0; i < m.n; ++i)
        {
            const float px = xOf (m.p[i].x), py = yOf (m.p[i].y);
            if (m.p[i].my.on())
            {
                const float y1 = yOf (std::clamp (m.p[i].y + m.p[i].my.amount, 0.0f, 1.0f));
                pen.rect (static_cast<int> (px) - 3, static_cast<int> (std::min (py, y1)), static_cast<int> (px) + 3, static_cast<int> (std::max (py, y1)), pen.c (T.flow[1], 90));
            }
            if (m.p[i].mx.on())
            {
                const float x1 = xOf (std::clamp (m.p[i].x + m.p[i].mx.amount, 0.0f, 1.0f));
                pen.rect (static_cast<int> (std::min (px, x1)), static_cast<int> (py) - 3, static_cast<int> (std::max (px, x1)), static_cast<int> (py) + 3, pen.c (T.flow[1], 90));
            }
        }
        // the middles of the segments (the curve handles), then the points over them
        for (int i = 0; i + 1 < m.n; ++i)
        {
            float mx, my;
            if (midHandle (i, mx, my))
            {
                const bool cur = i == state.selectedSeg;
                pen.disc (mx, my, cur ? 5.0f : 3.2f, pen.c (cur ? T.text : T.textDim, cur ? 255 : 170));
                pen.disc (mx, my, cur ? 2.6f : 1.6f, pen.c (T.panel1, 255));
                if (m.p[i].mc.on())
                    pen.arc (mx, my, 8.0f, 0.0f, 2.0f * kPi, 1.2f, pen.c (T.flow[1], 220));
            }
        }
        // the marker: the line over the whole height, the handle at its top, the caption; dimmed when the mode does not use it
        if (state.markerX >= 0.0f && state.markerKind != MarkerNone)
        {
            const bool live = markerLive();
            const float mx = xOf (state.markerX);
            const uint32_t col = live ? pen.c (T.env[0], 255) : pen.c (T.textFaint, 160);
            pen.style (1.0f, true);
            pen.line (mx, static_cast<float> (pl.y), mx, static_cast<float> (pl.bottom()), live ? 1.6f : 1.0f, col);
            pen.disc (mx, static_cast<float> (pl.y) + 16.0f, 11.0f, col);
            pen.disc (mx, static_cast<float> (pl.y) + 16.0f, 5.0f, pen.c (T.panel1, 255));
            pen.plain();
            if (live)
            {
                const bool right = mx + 100.0f > static_cast<float> (pl.right());
                const Rect box { static_cast<int> (right ? mx - 100.0f : mx + 16.0f), pl.y + 4, 84, 24 };
                if (right) pen.textRight (box, markerCaption (state.markerKind), T.fLabel - 3.0f, col, T.fMin); else pen.textLeft (box, markerCaption (state.markerKind), T.fLabel - 3.0f, col, T.fMin);
            }
            else if (markerHint (state.markerKind)[0] != '\0')
                pen.textLeft ({ pl.x + 10, pl.bottom() - 30, std::min (460, pl.w - 20), 24 }, markerHint (state.markerKind), T.fLabel - 3.0f, pen.c (T.textDim, 255), T.fMin);
        }
        const lfo::PointSet sel = selection();
        const bool soft = mode_ == Mode::Soft;
        pen.style (1.0f, true);
        for (int i = 0; i < m.n; ++i)
        {
            const float px = xOf (m.p[i].x), py = yOf (m.p[i].y);
            if (px < static_cast<float> (pl.x) - 8.0f || px > static_cast<float> (pl.right()) + 8.0f)
                continue;
            const bool one = i == state.selected, inSel = sel.has (i);
            if (i == m.loop)
                pen.arc (px, py, 10.0f, 0.0f, 2.0f * kPi, 1.5f, pen.c (T.env[0], 255));
            if (soft && softW_[i] > 0.0f)   // the rainbow of the relative movement: the anchor and the points that follow, by their share
            {
                const uint32_t rc = theme::mix (T.flow[3], T.flow[0], softW_[i]);
                pen.disc (px, py, 6.5f, pen.c (rc, 255));
                pen.arc (px, py, 10.5f, 0.0f, 2.0f
                         * kPi, 1.4f, pen.c (rc, 255));
            }
            else if (inSel)
            {
                pen.disc (px, py, one ? 6.5f : 5.5f, pen.c (T.text, 255));
                pen.arc (px, py, 10.5f, 0.0f, 2.0f * kPi, one ? 1.6f : 1.2f, pen.c (T.flow[0], 255));
            }
            else
            {
                pen.disc (px, py, 4.6f, pen.c (T.lfo[1], 255));
                pen.disc (px, py, 2.6f, pen.c (T.panel1, 255));
            }
        }
        pen.plain();
        if (rubbing_)   // the rectangle of the SELECT tool
        {
            const Rect rr = rubberRect();
            pen.rect (rr, pen.c (T.flow[0], 28));
            pen.frame (rr, 1, pen.c (T.flow[0], 220));
        }
    }

    Change tick (double nowMs) override
    {
        if (! captured_ || ! enabled_ || long_ || tap_.moved() || ! pressedExisting_ || nowMs - downMs_ < kLongPressMs || state.shape == nullptr)
            return Change::none();
        long_ = true;
        if (state.selected > 0 && state.selected < state.shape->n - 1 && state.shape->n > lfo::kMinPoints)
        {
            remember();
            state.shape->remove (state.selected);
            state.marks.clear();
            state.selected = -1;
            state.selectedSeg = -1;
            return Change::indexed (id_, Edited, Edited);
        }
        return Change::none();
    }

protected:
    void remember() { if (! pushed_ && state.undo != nullptr && state.shape != nullptr) state.undo->push (*state.shape); pushed_ = true; }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        downMs_ = now;
        long_ = false;
        pushed_ = false;
        pressedExisting_ = false;
        rubbing_ = false;
        mode_ = Mode::None;
        lastCol_ = -1;
        lastKey_ = -1;
        downPoint_ = downSeg_ = toggleOnUp_ = anchor_ = -1;
        insertedNow_ = false;
        prevInsertedAtDown_ = prevInserted_;
        for (float& w : softW_) w = 0.0f;
        if (state.shape == nullptr)
            return Change::none();
        lfo::ShapeModel& m = *state.shape;
        const int tool = state.tool;
        if (tool == ToolSelector::Flat || tool == ToolSelector::RampUp || tool == ToolSelector::RampDown)
        {
            mode_ = Mode::Draw;
            state.marks.clear();
            return drawAt (x, y);
        }
        const int hit = pointAt (x, y);
        if (hit >= 0 && tool != ToolSelector::Bend)
            return downOnPoint (hit, tool);
        if (markerLive() && markerZone().contains (x, y))
        {
            mode_ = Mode::Marker;
            return markerTo (x);
        }
        const int seg = segAt (x, y);
        if (seg >= 0 && (tool == ToolSelector::Point || tool == ToolSelector::Bend))
        {
            state.selectedSeg = seg;
            state.selected = -1;
            state.marks.clear();
            downSeg_ = seg;
            base_ = m;
            mode_ = tool == ToolSelector::Bend ? Mode::CurveAll : Mode::Curve;
            return Change::indexed (id_, Selected, Selected);
        }
        if (tool == ToolSelector::Select)
        {
            mode_ = Mode::Marquee;
            rubbing_ = true;
            rubX0_ = rubX1_ = x;
            rubY0_ = rubY1_ = y;
            return Change::none();
        }
        if (tool != ToolSelector::Point)
            return Change::none();   // SOFT and BEND add nothing on empty ground
        remember();
        float fx = xFrom (x), fy = yFrom (y);
        if (state.snap) { fx = lfo::snapTo (fx, gridXc()); fy = lfo::snapTo (fy, gridYc()); }
        const int at = m.insert (fx, fy);
        if (at < 0)
        {
            pushed_ = false;
            return Change::none();
        }
        state.marks.clear();
        state.selected = at;
        state.selectedSeg = -1;
        insertedNow_ = true;
        mode_ = Mode::Drag;
        return Change::indexed (id_, Edited, Edited);
    }
    Change onMove (int x, int y) override
    {
        if (state.shape == nullptr || long_)
            return Change::none();
        const bool moved = tap_.move (x, y);
        lfo::ShapeModel& m = *state.shape;
        if (mode_ == Mode::Draw)
            return drawAt (x, y);
        if (mode_ == Mode::Marker)
            return markerTo (x);
        if (mode_ == Mode::Marquee)
        {
            rubX1_ = x;
            rubY1_ = y;
            return moved ? marquee() : Change::none();
        }
        if (! moved && mode_ != Mode::Drag)
            return Change::none();
        if (mode_ == Mode::Drag && state.selected >= 0 && state.selected < m.n)
        {
            if (! moved && ! justInserted())
                return Change::none();
            remember();
            float fx = xFrom (x), fy = yFrom (y);
            if (state.snap) { fx = lfo::snapTo (fx, gridXc()); fy = lfo::snapTo (fy, gridYc()); }
            const lfo::Point before = m.p[state.selected];
            m.move (state.selected, fx, fy);
            return before.x != m.p[state.selected].x || before.y != m.p[state.selected].y ? Change::indexed (id_, Edited, Edited) : Change::none();
        }
        if ((mode_ == Mode::DragSet || mode_ == Mode::Soft) && anchor_ >= 0 && anchor_ < base_.n && moved)
        {
            remember();
            float fx = xFrom (x), fy = yFrom (y);
            if (state.snap) { fx = lfo::snapTo (fx, gridXc()); fy = lfo::snapTo (fy, gridYc()); }
            float dx = fx - base_.p[anchor_].x, dy = fy - base_.p[anchor_].y;
            const lfo::ShapeModel prev = m;
            m = base_;   // every move is made from the start of the gesture: no drift, and the clamps give the same result going and coming back
            if (mode_ == Mode::DragSet)
                m.moveSet (selection(), dx, dy);
            else
                m.moveWeighted (softW_, dx, dy);
            return m == prev ? Change::none() : Change::indexed (id_, Edited, Edited);
        }
        if (mode_ == Mode::Curve && state.selectedSeg >= 0 && state.selectedSeg + 1 < m.n && moved)
        {
            remember();
            const float before = m.p[state.selectedSeg].power;
            m.setPower (state.selectedSeg, lfo::powerForMid (m.p[state.selectedSeg].y, m.p[state.selectedSeg + 1].y, yFrom (y)));
            return before != m.p[state.selectedSeg].power ? Change::indexed (id_, Edited, Edited) : Change::none();
        }
        if (mode_ == Mode::CurveAll && downSeg_ >= 0 && downSeg_ + 1 < base_.n && moved)
        {
            remember();
            const float target = lfo::powerForMid (base_.p[downSeg_].y, base_.p[downSeg_ + 1].y, yFrom (y));
            const lfo::ShapeModel prev = m;
            m = base_;
            m.bendAll (target - base_.p[downSeg_].power);
            return m == prev ? Change::none() : Change::indexed (id_, Edited, Edited);
        }
        return Change::none();
    }
    Change onUp (int, int) override
    {
        const Mode was = mode_;
        mode_ = Mode::None;
        const int t = tap_.up();
        pressedExisting_ = false;
        const bool wasRubbing = rubbing_;
        rubbing_ = false;
        Change out = Change::none();
        if (state.shape != nullptr && ! long_)
        {
            lfo::ShapeModel& m = *state.shape;
            if (was == Mode::Marquee && t != 0)   // a tap on empty ground with the SELECT tool: the selection is gone
            {
                selectOnly (-1);
                out = Change::indexed (id_, Selected, Selected);
            }
            else if (was == Mode::Marquee && wasRubbing)
                out = Change::indexed (id_, Selected, Selected);   // the rectangle is gone: the picture changes
            else if (toggleOnUp_ >= 0 && t != 0)   // a tap on a selected point with the SELECT tool: out of the selection
            {
                state.marks.remove (toggleOnUp_);
                state.selected = state.marks.any() ? state.marks.first() : -1;
                if (state.marks.count() == 1)
                {
                    state.selected = state.marks.first();
                    state.marks.clear();
                }
                out = Change::indexed (id_, Selected, Selected);
            }
            else if (state.tool == ToolSelector::Point && was == Mode::Drag && t == 2 && ! prevInsertedAtDown_ && downPoint_ > 0 && downPoint_ < m.n - 1 && m.n > lfo::kMinPoints)
            {
                remember();   // a double tap on a point that was there before: gone
                m.remove (downPoint_);
                state.marks.clear();
                state.selected = -1;
                state.selectedSeg = -1;
                out = Change::indexed (id_, Edited, Edited);
            }
            else if (state.tool == ToolSelector::Point && was == Mode::Curve && t == 2 && downSeg_ >= 0 && downSeg_ + 1 < m.n && m.p[downSeg_].power != 0.0f)
            {
                remember();   // a double tap on a curve handle: the curve is straight again
                m.setPower (downSeg_, 0.0f);
                out = Change::indexed (id_, Edited, Edited);
            }
        }
        prevInserted_ = t == 1 && insertedNow_;
        toggleOnUp_ = -1;
        for (float& w : softW_) w = 0.0f;
        return out;
    }

private:
    enum class Mode : uint8_t { None, Drag, DragSet, Soft, Curve, CurveAll, Draw, Marquee, Marker };
    bool justInserted() const noexcept { return pushed_; }
    int gridXc() const noexcept { return std::clamp (state.gridX, lfo::kMinGrid, lfo::kMaxGrid); }
    int gridYc() const noexcept { return std::clamp (state.gridY, lfo::kMinGrid, lfo::kMaxGrid); }

    /** A touch on point `hit` with the tool POINT, SELECT or SOFT. */
    Change downOnPoint (int hit, int tool)
    {
        lfo::ShapeModel& m = *state.shape;
        const lfo::PointSet sel = selection();
        const bool inGroup = sel.count() >= 2 && sel.has (hit);
        downPoint_ = hit;
        if (tool == ToolSelector::Select)
        {
            if (inGroup)
                toggleOnUp_ = hit;   // a tap takes it out of the selection; a drag moves the whole selection
            else
            {
                if (! state.marks.any() && state.selected >= 0 && state.selected < m.n && state.selected != hit)
                    state.marks.add (state.selected);   // the point that was the selection joins the others
                state.marks.add (hit);
                if (state.marks.count() == 1)
                    state.marks.clear();
            }
            state.selected = hit;
            state.selectedSeg = -1;
            beginSet (hit, Mode::DragSet);
            return Change::indexed (id_, Selected, Selected);
        }
        if (! inGroup)
        {
            state.marks.clear();   // the touch makes this point the only one selected
            state.selected = hit;
        }
        else
            state.selected = hit;
        state.selectedSeg = -1;
        if (tool == ToolSelector::Soft)
        {
            beginSet (hit, Mode::Soft);
            m.falloffWeights (hit, inGroup ? &sel : nullptr, static_cast<float> (kSoftRadiusPercent) * 0.01f, softW_);
            return Change::indexed (id_, Selected, Selected);
        }
        if (inGroup)
            beginSet (hit, Mode::DragSet);
        else
        {
            mode_ = Mode::Drag;
            pressedExisting_ = hit > 0 && hit < m.n - 1;
        }
        return Change::indexed (id_, Selected, Selected);
    }
    void beginSet (int anchor, Mode mode)
    {
        anchor_ = anchor;
        base_ = *state.shape;
        mode_ = mode;
    }
    /** The rectangle of the rubber band, normalised, and the points inside it become the selection. */
    Rect rubberRect() const noexcept
    {
        const int x0 = std::min (rubX0_, rubX1_), x1 = std::max (rubX0_, rubX1_), y0 = std::min (rubY0_, rubY1_), y1 = std::max (rubY0_, rubY1_);
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    Change marquee()
    {
        lfo::ShapeModel& m = *state.shape;
        const Rect rr = rubberRect();
        lfo::PointSet now;
        for (int i = 0; i < m.n; ++i)
            if (rr.contains (static_cast<int> (xOf (m.p[i].x)), static_cast<int> (yOf (m.p[i].y))))
                now.add (i);
        state.marks = now;
        state.selected = now.any() ? now.first() : -1;
        state.selectedSeg = -1;
        if (now.count() == 1)
        {
            state.marks.clear();
            state.selected = now.first();
        }
        return Change::indexed (id_, Selected, Selected);
    }
    /** The marker follows the finger: the loop marker sticks to the nearest point (and the model's loop point becomes it: one undo state for the drag), the start marker to a point within 14 pixels, else to the grid
        with SNAP. Reports the position (the owner writes the PHASE). */
    Change markerTo (int px)
    {
        lfo::ShapeModel& m = *state.shape;
        float fx = xFrom (px), dist = 1.0f;
        const int nearest = m.nearestByX (fx, &dist);
        int idx = -1;
        if (nearest >= 0 && (state.markerKind == MarkerLoop || dist * static_cast<float> (std::max (1, plot().w - 1)) <= 14.0f))
        {
            idx = nearest;
            fx = m.p[nearest].x;
        }
        else if (state.snap)
            fx = lfo::snapTo (fx, gridXc());
        bool modelChanged = false;
        if (state.markerKind == MarkerLoop && m.loop != idx)
        {
            remember();
            m.loop = idx;
            modelChanged = true;
        }
        const bool movedMarker = std::fabs (fx - state.markerX) > 1.0e-6f;
        state.markerX = fx;
        return movedMarker || modelChanged ? Change::indexed (id_, Marker, fx) : Change::none();
    }
    /** The columns of the tool from the last one drawn to the one under the finger (all of them, whatever the finger skipped: its height interpolated), each with the flat piece / ramp of the tool. */
    Change drawAt (int x, int y)
    {
        const float fy = yFrom (y);
        const int gx = gridXc(), col = lfo::cellOf (xFrom (x), gx);
        bool changed = false;
        if (lastCol_ < 0 || lastCol_ == col)
            changed = drawColumn (col, fy, false);
        else
        {
            const int dir = col > lastCol_ ? 1 : -1;
            for (int c = lastCol_ + dir;; c += dir)
            {
                const float t = static_cast<float> (c - lastCol_) / static_cast<float> (col - lastCol_);
                changed = drawColumn (c, lastFy_ + (fy - lastFy_) * t, true) || changed;
                if (c == col)
                    break;
            }
        }
        lastCol_ = col;
        lastFy_ = fy;
        return changed ? Change::indexed (id_, Edited, Edited) : Change::none();
    }
    /** The flat piece / ramp of the tool in column `col` at the finger height `fy` (SNAP: on a grid line). `force`: even when it is the same as the last one. true when the shape changed. */
    bool drawColumn (int col, float fy, bool force)
    {
        lfo::ShapeModel& m = *state.shape;
        const int gx = gridXc(), gy = gridYc();
        const int row = lfo::cellOf (fy, gy);
        const float xa = static_cast<float> (col) / static_cast<float> (gx), xb = static_cast<float> (col + 1) / static_cast<float> (gx);
        float ya, yb;
        int key;
        if (state.tool == ToolSelector::Flat)
        {
            ya = yb = state.snap ? lfo::snapTo (fy, gy) : std::clamp (fy, 0.0f, 1.0f);
            key = col * 4096 + static_cast<int> (std::lround (ya * 1000.0f));
        }
        else if (state.tool == ToolSelector::RampUp)
        {
            ya = static_cast<float> (row) / static_cast<float> (gy);
            yb = static_cast<float> (row + 1) / static_cast<float> (gy);
            key = col * 4096 + row;
        }
        else
        {
            ya = static_cast<float> (row + 1) / static_cast<float> (gy);
            yb = static_cast<float> (row) / static_cast<float> (gy);
            key = col * 4096 + row;
        }
        if (! force && key == lastKey_)
            return false;
        lastKey_ = key;
        const lfo::ShapeModel before = m;
        if (! m.replaceRange (xa, xb, ya, yb) || m == before)
            return false;
        if (! pushed_ && state.undo != nullptr)
            state.undo->push (before);
        pushed_ = true;
        state.selected = -1;
        state.selectedSeg = -1;
        return true;
    }
    TapGesture tap_;
    double downMs_ = 0.0;
    bool long_ = false, pushed_ = false, pressedExisting_ = false, rubbing_ = false, insertedNow_ = false, prevInserted_ = false, prevInsertedAtDown_ = false;
    Mode mode_ = Mode::None;
    int lastCol_ = -1, lastKey_ = -1, downPoint_ = -1, downSeg_ = -1, toggleOnUp_ = -1, anchor_ = -1, rubX0_ = 0, rubY0_ = 0, rubX1_ = 0, rubY1_ = 0;
    float lastFy_ = 0.0f;
    lfo::ShapeModel base_;                        // the model at the start of a group / soft / bend gesture (every move is computed from it)
    float softW_[lfo::kMaxPoints] = {};           // the weights of a soft drag (drawn as the rainbow)
};

// ===============================================================================================================================================================
/** ShapeEditorPage: the full page of the shape editor (opened from the LFO full screen for every shape; the owner has made the shape a drawn one first). Parts: `back`, the tools, SHAPES (the list of the shapes: the owner
    opens it), MORE (the list of the actions), UNDO, DELETE, MOD (the modulation of the selected points), the canvas (the grid is drawn on it); at the right three Q groups: EDIT = [POINT (which point is selected, or how many), X
    (its time), Y (its level), CURVE of the segment after it] (several points selected: [N SEL, the centre of the selection in time, in level, SCALE = its height]); GRID = [GRID X (columns), GRID Y (rows), SNAP]; TRIGGER = [MODE
    (the trigger of the LFO), PHASE (the start / loop position: the marker of the canvas), BEND ALL (the mean curve)]; under them LOOP (the selected point is the loop point) and SMOOTH (the S curve of every segment). Out: the
    changes of the parts with the ids the owner gave; the owner writes the model after every canvas change. */
class ShapeEditorPage : public Composite
{
public:
    static constexpr int kGroups = 3;
    MenuButton back { "BACK", MenuButton::Icon::None };
    ToolSelector tools;
    MenuButton shapes { "SHAPES", MenuButton::Icon::None }, more { "MORE", MenuButton::Icon::None }, undoButton { "UNDO", MenuButton::Icon::None }, del { "DELETE", MenuButton::Icon::None }, modButton { "MOD", MenuButton::Icon::None };
    LedToggle snap, loop, smooth;
    ShapeCanvas canvas;
    NumberField fpoint { "POINT", NumberField::Format::IntPlain }, fx { "X", NumberField::Format::Decimal2 }, fy { "Y", NumberField::Format::Decimal2 }, fcurve { "CURVE", NumberField::Format::IntSigned };
    NumberField fgx { "GRID X", NumberField::Format::IntPlain }, fgy { "GRID Y", NumberField::Format::IntPlain };
    NumberField fphase { "PHASE", NumberField::Format::Degrees }, fbend { "BEND ALL", NumberField::Format::IntSigned };
    Stepper mode;
    QGroup groups[kGroups];   // 0 EDIT, 1 GRID, 2 TRIGGER
    QGroup& group = groups[0];
    lfo::ShapeModel model;
    lfo::Undo undo;
    static constexpr int kBarH = 72, kColW = 300;

    ShapeEditorPage()
    {
        snap.setLabel ("SNAP");
        loop.setLabel ("LOOP");
        smooth.setLabel ("SMOOTH");
        fpoint.state.min = 1.0; fpoint.state.max = 2.0; fpoint.state.step = 1.0; fpoint.state.pxPerStep = 14;
        fx.state.min = 0.0; fx.state.max = 1.0; fx.state.step = 0.01; fx.state.pxPerStep = 2;
        fy.state.min = 0.0; fy.state.max = 1.0; fy.state.step = 0.01; fy.state.pxPerStep = 2;
        fcurve.state.min = -20.0; fcurve.state.max = 20.0; fcurve.state.step = 1.0; fcurve.state.pxPerStep = 8;
        fbend.state.min = -20.0; fbend.state.max = 20.0; fbend.state.step = 1.0; fbend.state.pxPerStep = 8;
        fphase.state.min = 0.0; fphase.state.max = 360.0; fphase.state.step = 1.0; fphase.state.pxPerStep = 2;
        for (NumberField* g : { &fgx, &fgy })
        {
            g->state.min = lfo::kMinGrid; g->state.max = lfo::kMaxGrid; g->state.step = 1.0; g->state.pxPerStep = 10; g->state.defaultValue = 8;
        }
        mode.setCaption ("MODE");
        mode.listable = true;
        mode.state.wrap = true;
        mode.minWidth = 140;
        canvas.state.shape = &model;
        canvas.state.undo = &undo;
        model.initLinear();
        addKid (back); addKid (tools); addKid (shapes); addKid (more); addKid (undoButton); addKid (del); addKid (modButton);
        addKid (snap); addKid (loop); addKid (smooth);
        addKid (canvas); addKid (fpoint); addKid (fx); addKid (fy); addKid (fcurve); addKid (fgx); addKid (fgy); addKid (mode); addKid (fphase); addKid (fbend);
        groups[0].set (0, &fpoint); groups[0].set (1, &fx); groups[0].set (2, &fy); groups[0].set (3, &fcurve);
        groups[1].set (0, &fgx); groups[1].set (1, &fgy); groups[1].set (2, &snap);
        groups[2].set (0, &mode); groups[2].set (1, &fphase); groups[2].set (2, &fbend);
        setActiveGroup (0);
    }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int i)
    {
        for (int k = 0; k < kGroups; ++k)
            groups[k].setActive (k == i);
        active_ = i >= 0 && i < kGroups ? i : -1;
    }
    Change turnQ (int knobIndex, double delta) { return active_ >= 0 ? groups[active_].turn (knobIndex, delta) : Change::none(); }

    /** What the marker is for a trigger mode (lfoN.sync_type): 0 Retrigger and 2 Envelope: the START position, 3 Sustain envelope, 4 Loop at point, 5 Loop and hold: the LOOP position, 1 Free-run: dimmed. */
    static int markerKindOf (int syncType) noexcept
    {
        switch (syncType)
        {
            case 0: case 2: return ShapeCanvas::MarkerStart;
            case 3: case 4: case 5: return ShapeCanvas::MarkerLoop;
            default: return ShapeCanvas::MarkerDim;
        }
    }
    /** The trigger values of the LFO (the owner reads the parameters): the mode with its name and number of choices, the phase in degrees. Sets the stepper, the field and the marker of the canvas. */
    void setTrigger (int syncType, const char* modeName, int modeCount, double phaseDegrees)
    {
        mode.state.count = modeCount;
        mode.state.index = syncType;
        mode.setItem (modeName);
        fphase.state.value = std::clamp (phaseDegrees, 0.0, 360.0);
        canvas.state.markerKind = markerKindOf (syncType);
        canvas.state.markerX = static_cast<float> (std::clamp (phaseDegrees, 0.0, 360.0) / 360.0);
    }
    /** The line of text over the lower left of the canvas (the result of a save, a load, a copy): "" clears it. */
    void setStatus (const char* s) { setText (status_, sizeof status_, s); }
    const char* status() const noexcept { return status_; }

    /** The values of the fields and switches from the model and the canvas state (the owner calls it after every change). */
    void sync()
    {
        const ShapeCanvas::State& cs = canvas.state;
        tools.state.index = cs.tool;
        snap.state.on = cs.snap;
        smooth.state.on = model.smooth;
        const lfo::PointSet sel = canvas.selection();
        const int cnt = sel.count();
        const bool multi = cnt >= 2;
        const int one = cs.selected >= 0 && cs.selected < model.n ? cs.selected : -1;
        const int sel1 = multi ? sel.first() : (cnt == 1 ? sel.first() : -1);
        int deletable = 0;
        for (int i = 1; i + 1 < model.n; ++i)
            deletable += sel.has (i) ? 1 : 0;
        loop.state.on = one >= 0 && model.loop == one;
        loop.setEnabled (enabled_ && one >= 0);
        del.setEnabled (enabled_ && deletable > 0);
        modButton.setEnabled (enabled_ && cnt > 0);
        undoButton.setEnabled (enabled_ && undo.depth() > 0);
        fpoint.state.max = static_cast<double> (std::max (2, model.n));
        fgx.state.value = cs.gridX;
        fgy.state.value = cs.gridY;
        float x0 = 0.0f, x1 = 0.0f, y0 = 0.0f, y1 = 0.0f;
        model.spanOf (sel, x0, x1, y0, y1);
        if (multi)
        {
            std::snprintf (pointText_, sizeof pointText_, "%d SEL", cnt);
            setText (fpoint.state.text, sizeof fpoint.state.text, pointText_);
            fpoint.state.value = one >= 0 ? one + 1 : 1;
            fpoint.state.notSet = false;
            fx.state.value = 0.5 * (static_cast<double> (x0) + static_cast<double> (x1));
            fx.state.notSet = false;
            fx.setEnabled (enabled_ && deletable > 0);   // the group moves in time as far as an inner point is in it
            fy.state.value = 0.5 * (static_cast<double> (y0) + static_cast<double> (y1));
            fy.state.notSet = false;
            fy.setEnabled (enabled_);
            fcurve.setLabel ("SCALE");
            fcurve.state.format = NumberField::Format::Decimal2;
            fcurve.state.min = 0.01; fcurve.state.max = 1.0; fcurve.state.step = 0.01; fcurve.state.pxPerStep = 2;
            fcurve.state.value = static_cast<double> (y1 - y0);
            fcurve.state.notSet = false;
            fcurve.setEnabled (enabled_ && y1 - y0 > 1.0e-4f);
        }
        else
        {
            fpoint.state.text[0] = '\0';
            fpoint.state.value = sel1 >= 0 ? sel1 + 1 : 1;
            fpoint.state.notSet = sel1 < 0;
            fx.setEnabled (enabled_ && sel1 > 0 && sel1 < model.n - 1);   // the end points keep their x
            fy.setEnabled (enabled_ && sel1 >= 0);
            fcurve.setLabel ("CURVE");
            fcurve.state.format = NumberField::Format::IntSigned;
            fcurve.state.min = -20.0; fcurve.state.max = 20.0; fcurve.state.step = 1.0; fcurve.state.pxPerStep = 8;
            fcurve.setEnabled (enabled_ && sel1 >= 0 && sel1 + 1 < model.n);
            fx.state.value = sel1 >= 0 ? model.p[sel1].x : 0.0;
            fx.state.notSet = sel1 < 0;
            fy.state.value = sel1 >= 0 ? model.p[sel1].y : 0.0;
            fy.state.notSet = sel1 < 0;
            fcurve.state.value = sel1 >= 0 && sel1 + 1 < model.n ? std::round (model.p[sel1].power) : 0.0;
            fcurve.state.notSet = ! (sel1 >= 0 && sel1 + 1 < model.n);
        }
        bool bendable = false;
        for (int i = 0; i + 1 < model.n && ! bendable; ++i)
            bendable = model.bendable (i);
        fbend.state.value = std::round (static_cast<double> (model.meanCurve()));
        fbend.setEnabled (enabled_ && bendable);
        std::snprintf (info_, sizeof info_, "%d / %d POINTS", model.n, lfo::kMaxPoints);
    }
    Size minSize() const override { return { 1000, 520 }; }
    Rect canvasRect() const noexcept { return canvas.rect(); }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        for (int i = 0; i < kGroups; ++i)
            if (groups[i].rect().contains (x, y))
            {
                setActiveGroup (i);
                break;
            }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (int i = 0; i < kGroups; ++i)
        {
            if (i == active_)
                groups[i].draw (sb);
            else
                groups[i].drawMapped (sb);
        }
    }

protected:
    void layout() override
    {
        const int m = 8;
        const Rect top { rect_.x + m, rect_.y + m, rect_.w - 2 * m, kBarH - m };
        int x = top.x;
        auto put = [&] (Component& c, int w) { c.place ({ x, top.y, w, 56 }); x += w + m; };
        put (back, 96);
        put (tools, ToolSelector::kTools * 80);
        put (shapes, 100);
        put (more, 84);
        put (undoButton, 84);
        put (del, 96);
        put (modButton, 84);
        snap.place ({}); loop.place ({}); smooth.place ({});   // placed in the column below
        const int cy = rect_.y + kBarH + m;
        canvas.place ({ rect_.x + m, cy, rect_.w - 3 * m - kColW, rect_.bottom() - m - cy });
        // the column at the right: the three Q groups (the lane of the index chips above each row), then LOOP and SMOOTH
        const int colX = rect_.right() - m - kColW, H = rect_.bottom() - m - cy;
        const int titleH = 22, lane = kQLane;
        const int fieldH = std::clamp ((H - 3 * titleH - 5 * lane - 3 * m - 56) / 5, 56, 72);
        int y = cy;
        const int hw = (kColW - kGap * 2) / 2;
        titleEdit_ = { colX, y, kColW, titleH };
        y += titleH + lane;
        fpoint.place ({ colX, y, hw, fieldH }); fx.place ({ colX + kColW - hw, y, hw, fieldH });
        y += fieldH + lane;
        fy.place ({ colX, y, hw, fieldH }); fcurve.place ({ colX + kColW - hw, y, hw, fieldH });
        y += fieldH + m;
        titleGrid_ = { colX, y, kColW, titleH };
        y += titleH + lane;
        const int tw = (kColW - 2 * kGap) / 3;
        fgx.place ({ colX, y, tw, fieldH }); fgy.place ({ colX + tw + kGap, y, tw, fieldH }); snap.place ({ colX + 2 * (tw + kGap), y, kColW - 2 * (tw + kGap), fieldH });
        y += fieldH + m;
        titleTrigger_ = { colX, y, kColW, titleH };
        y += titleH + lane;
        mode.place ({ colX, y, kColW, fieldH });
        y += fieldH + lane;
        fphase.place ({ colX, y, hw, fieldH }); fbend.place ({ colX + kColW - hw, y, hw, fieldH });
        y += fieldH + m;
        loop.place ({ colX, y, hw, 56 }); smooth.place ({ colX + kColW - hw, y, hw, 56 });
        for (int i = 0; i < kGroups; ++i)
            groups[i].setFrameRect (groups[i].rect());
    }
    void drawUnder (Pen& p) const override { (void) p; }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft (titleEdit_, "SELECTED POINT", T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
        p.textLeft (titleGrid_, "GRID", T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
        p.textLeft (titleTrigger_, "TRIGGER", T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
        p.textRight ({ canvas.rect().right() - 8 - 220, canvas.rect().y + 6, 220, 18 }, info_, T.fLabel - 3.0f, p.c (T.textFaint, 255), T.fMin);
        if (status_[0] != '\0')
            p.textLeft ({ canvas.rect().x + 14, canvas.rect().y + 6, std::max (0, canvas.rect().w - 260), 22 }, status_, T.fLabel - 2.0f, p.c (T.flow[0], 255), T.fMin);
    }
    Rect titleEdit_, titleGrid_, titleTrigger_;
    int active_ = -1;
    char info_[32] = {}, status_[64] = {}, pointText_[16] = {};
};

// ===============================================================================================================================================================
/** PointModPage: the modulation of the time (TIME), the level (LEVEL) or the curve (CURVE) of the selected point(s): the page above the editor. `back`, `target` (a stepper: TIME, LEVEL, CURVE), `amount` (-100 .. 100
    percent), `remove` (the modulation of the chosen value goes), the summary line (the owner writes it: "POINT 3: LEVEL BY LFO 2, +45 PERCENT") and the board of the modulation sources (the owner fills it, `board.add`): a tap on a
    source asks the owner to give the value that source (Index change of `board`: index = the id of the source). Same contract as every component; no Q group (the amount is the field). */
class PointModPage : public Composite
{
public:
    MenuButton back { "BACK", MenuButton::Icon::None }, remove { "REMOVE", MenuButton::Icon::None };
    Stepper target;
    NumberField amount { "AMOUNT", NumberField::Format::IntSigned };
    ModSourceBoard board;

    PointModPage()
    {
        target.setCaption ("MODULATE");
        target.state.count = 3;
        target.state.wrap = true;
        target.minWidth = 140;
        target.setItem (targetName (0));
        amount.state.min = -100.0; amount.state.max = 100.0; amount.state.step = 1.0; amount.state.pxPerStep = 3; amount.state.defaultValue = 50.0;
        board.setHeadline ("TAP A SOURCE TO MODULATE THE VALUE");
        addKid (back); addKid (target); addKid (amount); addKid (remove); addKid (board);
    }
    static const char* targetName (int i) noexcept { static const char* const n[3] = { "TIME", "LEVEL", "CURVE" }; return n[std::clamp (i, 0, 2)]; }
    /** The value being edited (0 time, 1 level, 2 curve) and how it stands: the source (its name; "" = none), the amount -1 .. 1 and the number of points the edit goes to. */
    void load (int which, const char* sourceName, double amountPercent, int points, int firstPoint)
    {
        target.state.index = std::clamp (which, 0, 2);
        target.setItem (targetName (which));
        amount.state.value = std::clamp (amountPercent, -100.0, 100.0);
        const bool has = sourceName != nullptr && sourceName[0] != '\0';
        amount.setEnabled (enabled_ && has);
        remove.setEnabled (enabled_ && has);
        if (points > 1)
            std::snprintf (summary_, sizeof summary_, "%d POINTS: %s %s", points, targetName (which), has ? sourceName : "NOT MODULATED");
        else
            std::snprintf (summary_, sizeof summary_, "POINT %d: %s %s", firstPoint + 1, targetName (which), has ? sourceName : "NOT MODULATED");
    }
    Size minSize() const override { return { 1000, 520 }; }

protected:
    void layout() override
    {
        const int m = 8;
        const int y = rect_.y + m;
        back.place ({ rect_.x + m, y, 96, 56 });
        target.place ({ rect_.x + 112, y, 220, 56 });
        amount.place ({ rect_.x + 340, y, 160, 56 });
        remove.place ({ rect_.x + 508, y, 120, 56 });
        summaryBox_ = { rect_.x + 640, y, rect_.w - 640 - m, 56 };
        board.place ({ rect_.x + m, y + 56 + m, rect_.w - 2 * m, rect_.bottom() - m - (y + 56 + m) });
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft (summaryBox_, summary_, T.fLabel + 2.0f, p.c (T.text, 255), T.fMin);
    }
    Rect summaryBox_;
    char summary_[64] = {};
};

}} // namespace synthyboy_mpc::ui
