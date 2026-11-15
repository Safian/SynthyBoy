// SynthyBoy - MPC UI component library: the SHAPE EDITOR of the LFO: ToolSelector (POINT, FLAT, RAMP UP, RAMP DOWN), ShapeCanvas (the drawing surface: the shape with its grid, its points and the middles of its
// segments; touch edits the shape model of lfo_model.h) and ShapeEditorPage (the whole page: the back button, the tools, undo, delete, snap, loop point, smooth, the four values of the selected point as the Q
// group "EDIT": X, Y, CURVE, GRID). The component never stores the truth: the model is the owner's (`ShapeCanvas::State::shape` points at it; the owner writes it to the content state after every change the
// canvas reports), and so is the undo stack.
// Gestures of the canvas (single finger). Tool POINT: a tap on empty ground adds a point there (snapped to the grid when SNAP is on) and the finger keeps it: dragging moves it; a touch on a point selects it
// and drags it (the two end points only up and down); a touch on the small handle in the middle of a segment drags its curve (the finger is where the middle of the segment goes: power -20 .. 20); a long press
// on a point deletes it (600 ms without moving). Tools FLAT / RAMP UP / RAMP DOWN: a touch on a cell of the grid draws a flat piece (at the height of the finger, on a grid line with SNAP) or a ramp (from the
// lower to the upper line of the cell, up or down) over the column of the cell, replacing what was there; a drag across the columns draws a staircase or a saw tooth.
// Output: Index changes { id of the canvas, index = Op }: Edited = the model changed (write it), Selected = only the selection moved.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "lfo_display.h"
#include "lfo_model.h"
#include "panel.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class ToolSelector : public Component
{
public:
    enum Tool : int { Point = 0, Flat, RampUp, RampDown, kTools };
    struct State { int index = 0; };
    State state;
    Rect cell (int i) const noexcept { return { rect_.x + rect_.w * i / kTools, rect_.y, rect_.w * (i + 1) / kTools - rect_.w * i / kTools, rect_.h }; }
    Size minSize() const override { return { kTools * 72, 56 }; }
    int touchTargets (Rect* out, int max) const override { int n = 0; for (int i = 0; i < kTools && n < max; ++i) out[n++] = cell (i); return n; }
    static const char* name (int t) noexcept { static const char* const n[kTools] = { "POINT", "FLAT", "RAMP+", "RAMP-" }; return n[std::clamp (t, 0, kTools - 1)]; }

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
                default: p.line (cx - 10.0f, cy - 6.0f, cx + 10.0f, cy + 6.0f, 2.0f, col); p.line (cx - 10.0f, cy - 6.0f, cx - 10.0f, cy + 6.0f, 1.0f, col); break;
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
    enum Op : int { Edited = 1, Selected = 2 };
    struct State
    {
        lfo::ShapeModel* shape = nullptr;   // the owner's model: the canvas edits it in place and reports
        lfo::Undo* undo = nullptr;           // the owner's undo stack: the canvas pushes the state before a change
        int tool = ToolSelector::Point;
        int grid = 8;
        bool snap = true;
        int selected = -1;                   // the selected point (-1 none)
        int selectedSeg = -1;                // the segment whose middle was touched last (-1 none)
    };
    State state;
    static constexpr int kHitR = 28, kMidR = 22, kLongPressMs = 600;

    Size minSize() const override { return { 400, 240 }; }
    lfodisplay::Data data() const
    {
        lfodisplay::Data d;
        d.kind = lfodisplay::Data::Kind::Shape;
        d.editing = true;
        d.grid = std::clamp (state.grid, lfo::kMinGrid, lfo::kMaxGrid);
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
        if (state.shape->loop >= 0 && state.shape->loop < state.shape->n)
            d.loopX = state.shape->p[state.shape->loop].x;
        lfodisplay::draw (sb, rect_, d, false);
        const Rect pl = plot();
        Cell cell (sb, rect_);
        // the middles of the segments (the curve handles), then the points over them
        for (int i = 0; i + 1 < state.shape->n; ++i)
        {
            float mx, my;
            if (midHandle (i, mx, my))
            {
                const bool cur = i == state.selectedSeg;
                pen.disc (mx, my, cur ? 5.0f : 3.2f, pen.c (cur ? T.text : T.textDim, cur ? 255 : 170));
                pen.disc (mx, my, cur ? 2.6f : 1.6f, pen.c (T.panel1, 255));
            }
        }
        pen.style (1.0f, true);
        for (int i = 0; i < state.shape->n; ++i)
        {
            const float px = xOf (state.shape->p[i].x), py = yOf (state.shape->p[i].y);
            if (px < static_cast<float> (pl.x) - 8.0f || px > static_cast<float> (pl.right()) + 8.0f)
                continue;
            const bool sel = i == state.selected;
            if (i == state.shape->loop)
                pen.arc (px, py, 10.0f, 0.0f, 2.0f * kPi, 1.5f, pen.c (T.q, 255));
            if (sel)
            {
                pen.disc (px, py, 6.5f, pen.c (T.text, 255));
                pen.arc (px, py, 10.5f, 0.0f, 2.0f * kPi, 1.4f, pen.c (T.flow[0], 255));
            }
            else
            {
                pen.disc (px, py, 4.6f, pen.c (T.lfo[1], 255));
                pen.disc (px, py, 2.6f, pen.c (T.panel1, 255));
            }
        }
        pen.plain();
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
        mode_ = Mode::None;
        lastCell_ = -1;
        if (state.shape == nullptr)
            return Change::none();
        lfo::ShapeModel& m = *state.shape;
        const int grid = std::clamp (state.grid, lfo::kMinGrid, lfo::kMaxGrid);
        if (state.tool == ToolSelector::Point)
        {
            const int hit = pointAt (x, y);
            if (hit >= 0)
            {
                state.selected = hit;
                state.selectedSeg = -1;
                mode_ = Mode::Drag;
                pressedExisting_ = hit > 0 && hit < m.n - 1;
                return Change::indexed (id_, Selected, Selected);
            }
            const int seg = segAt (x, y);
            if (seg >= 0)
            {
                state.selectedSeg = seg;
                state.selected = -1;
                mode_ = Mode::Curve;
                return Change::indexed (id_, Selected, Selected);
            }
            remember();
            float fx = xFrom (x), fy = yFrom (y);
            if (state.snap) { fx = lfo::snapTo (fx, grid); fy = lfo::snapTo (fy, grid); }
            const int at = m.insert (fx, fy);
            if (at < 0)
            {
                pushed_ = false;
                return Change::none();
            }
            state.selected = at;
            state.selectedSeg = -1;
            mode_ = Mode::Drag;
            return Change::indexed (id_, Edited, Edited);
        }
        mode_ = Mode::Draw;
        return drawCell (x, y);
    }
    Change onMove (int x, int y) override
    {
        if (state.shape == nullptr || long_)
            return Change::none();
        const bool moved = tap_.move (x, y);
        lfo::ShapeModel& m = *state.shape;
        const int grid = std::clamp (state.grid, lfo::kMinGrid, lfo::kMaxGrid);
        if (mode_ == Mode::Draw)
            return drawCell (x, y);
        if (! moved && mode_ != Mode::Drag)
            return Change::none();
        if (mode_ == Mode::Drag && state.selected >= 0 && state.selected < m.n)
        {
            if (! moved && ! justInserted())
                return Change::none();
            remember();
            float fx = xFrom (x), fy = yFrom (y);
            if (state.snap) { fx = lfo::snapTo (fx, grid); fy = lfo::snapTo (fy, grid); }
            const lfo::Point before = m.p[state.selected];
            m.move (state.selected, fx, fy);
            return before.x != m.p[state.selected].x || before.y != m.p[state.selected].y ? Change::indexed (id_, Edited, Edited) : Change::none();
        }
        if (mode_ == Mode::Curve && state.selectedSeg >= 0 && state.selectedSeg + 1 < m.n && moved)
        {
            remember();
            const float before = m.p[state.selectedSeg].power;
            m.setPower (state.selectedSeg, lfo::powerForMid (m.p[state.selectedSeg].y, m.p[state.selectedSeg + 1].y, yFrom (y)));
            return before != m.p[state.selectedSeg].power ? Change::indexed (id_, Edited, Edited) : Change::none();
        }
        return Change::none();
    }
    Change onUp (int, int) override
    {
        const Mode was = mode_;
        mode_ = Mode::None;
        const int t = tap_.up();
        (void) t; (void) was;
        pressedExisting_ = false;
        return Change::none();
    }

private:
    enum class Mode : uint8_t { None, Drag, Curve, Draw };
    bool justInserted() const noexcept { return pushed_; }
    /** The flat piece / ramp of the tool in the grid column under x (the height from the finger y): once per column of a gesture. */
    Change drawCell (int x, int y)
    {
        lfo::ShapeModel& m = *state.shape;
        const int grid = std::clamp (state.grid, lfo::kMinGrid, lfo::kMaxGrid);
        const float fx = xFrom (x), fy = yFrom (y);
        const int col = lfo::cellOf (fx, grid), row = lfo::cellOf (fy, grid);
        const int key = col * 1024 + row * (state.tool == ToolSelector::Flat ? 0 : 1);
        if (key == lastCell_)
            return Change::none();
        lastCell_ = key;
        const float xa = static_cast<float> (col) / static_cast<float> (grid), xb = static_cast<float> (col + 1) / static_cast<float> (grid);
        float ya, yb;
        if (state.tool == ToolSelector::Flat)
            ya = yb = state.snap ? lfo::snapTo (fy, grid) : fy;
        else if (state.tool == ToolSelector::RampUp)
        {
            ya = static_cast<float> (row) / static_cast<float> (grid);
            yb = static_cast<float> (row + 1) / static_cast<float> (grid);
        }
        else
        {
            ya = static_cast<float> (row + 1) / static_cast<float> (grid);
            yb = static_cast<float> (row) / static_cast<float> (grid);
        }
        const lfo::ShapeModel before = m;
        if (! m.replaceRange (xa, xb, ya, yb))
            return Change::none();
        if (m == before)
            return Change::none();
        if (! pushed_ && state.undo != nullptr)
            state.undo->push (before);
        pushed_ = true;
        state.selected = -1;
        state.selectedSeg = -1;
        return Change::indexed (id_, Edited, Edited);
    }
    TapGesture tap_;
    double downMs_ = 0.0;
    bool long_ = false, pushed_ = false, pressedExisting_ = false;
    Mode mode_ = Mode::None;
    int lastCell_ = -1;
};

// ===============================================================================================================================================================
/** ShapeEditorPage: the full page of the shape editor (opened from the LFO full screen; the owner has made the shape a drawn one first). Parts: `back`; the tools; `undo`; `del` (the selected point); the
    switches `snap`, `loop` (the selected point is the loop point of the envelope types) and `smooth` (the S curve of every segment); the canvas; the four values of the selected point in the Q group `group`
    (X, Y, CURVE of the segment after it, GRID). Out: the changes of the parts with the ids the owner gave; the owner writes the model after every canvas change. */
class ShapeEditorPage : public Composite
{
public:
    MenuButton back { "BACK", MenuButton::Icon::None };
    ToolSelector tools;
    MenuButton undoButton { "UNDO", MenuButton::Icon::None }, del { "DELETE", MenuButton::Icon::None };
    ToggleIcon snap, loop, smooth;
    ShapeCanvas canvas;
    NumberField fx { "X", NumberField::Format::Decimal2 }, fy { "Y", NumberField::Format::Decimal2 }, fcurve { "CURVE", NumberField::Format::IntSigned }, fgrid { "GRID", NumberField::Format::IntPlain };
    QGroup group;
    lfo::ShapeModel model;
    lfo::Undo undo;
    static constexpr int kBarH = 72, kColW = 224;

    ShapeEditorPage()
    {
        snap.state.icon = ToggleIcon::Icon::Generic; snap.setLabel ("SNAP");
        loop.state.icon = ToggleIcon::Icon::Generic; loop.setLabel ("LOOP");
        smooth.state.icon = ToggleIcon::Icon::Generic; smooth.setLabel ("SMOOTH");
        fx.state.min = 0.0; fx.state.max = 1.0; fx.state.step = 0.01; fx.state.pxPerStep = 2;
        fy.state.min = 0.0; fy.state.max = 1.0; fy.state.step = 0.01; fy.state.pxPerStep = 2;
        fcurve.state.min = -20.0; fcurve.state.max = 20.0; fcurve.state.step = 1.0; fcurve.state.pxPerStep = 8;
        fgrid.state.min = lfo::kMinGrid; fgrid.state.max = lfo::kMaxGrid; fgrid.state.step = 1.0; fgrid.state.pxPerStep = 10; fgrid.state.defaultValue = 8;
        canvas.state.shape = &model;
        canvas.state.undo = &undo;
        model.initLinear();
        addKid (back); addKid (tools); addKid (undoButton); addKid (del); addKid (snap); addKid (loop); addKid (smooth);
        addKid (canvas); addKid (fx); addKid (fy); addKid (fcurve); addKid (fgrid);
        group.set (0, &fx); group.set (1, &fy); group.set (2, &fcurve); group.set (3, &fgrid);
        group.setActive (true);
    }
    /** The values of the fields and switches from the model and the canvas state (the owner calls it after every change). */
    void sync()
    {
        const ShapeCanvas::State& cs = canvas.state;
        tools.state.index = cs.tool;
        snap.state.on = cs.snap;
        smooth.state.on = model.smooth;
        const int sel = cs.selected >= 0 && cs.selected < model.n ? cs.selected : -1;
        loop.state.on = sel >= 0 && model.loop == sel;
        loop.setEnabled (enabled_ && sel >= 0);
        del.setEnabled (enabled_ && sel > 0 && sel < model.n - 1 && model.n > lfo::kMinPoints);
        undoButton.setEnabled (enabled_ && undo.depth() > 0);
        fx.setEnabled (enabled_ && sel > 0 && sel < model.n - 1);   // the end points keep their x
        fy.setEnabled (enabled_ && sel >= 0);
        fcurve.setEnabled (enabled_ && sel >= 0 && sel + 1 < model.n);
        fx.state.value = sel >= 0 ? model.p[sel].x : 0.0;
        fx.state.notSet = sel < 0;
        fy.state.value = sel >= 0 ? model.p[sel].y : 0.0;
        fy.state.notSet = sel < 0;
        fcurve.state.value = sel >= 0 && sel + 1 < model.n ? std::round (model.p[sel].power) : 0.0;
        fcurve.state.notSet = ! (sel >= 0 && sel + 1 < model.n);
        fgrid.state.value = cs.grid;
        std::snprintf (info_, sizeof info_, "%d / %d POINTS", model.n, lfo::kMaxPoints);
    }
    Size minSize() const override { return { 1000, 520 }; }
    Rect canvasRect() const noexcept { return canvas.rect(); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        group.draw (sb);
    }

protected:
    void layout() override
    {
        const int m = 8;
        const Rect top { rect_.x + m, rect_.y + m, rect_.w - 2 * m, kBarH - m };
        Rect r[7];
        const int fixed[7] = { 100, 304, 100, 100, 72, 72, 88 }, weight[7] = { 0, 0, 0, 0, 0, 0, 0 };
        layoutRow (top, fixed, weight, 7, kGap, r);
        back.place ({ r[0].x, top.y, r[0].w, 56 });
        tools.place ({ r[1].x, top.y, r[1].w, 56 });
        undoButton.place ({ r[2].x, top.y, r[2].w, 56 });
        del.place ({ r[3].x, top.y, r[3].w, 56 });
        snap.place ({ r[4].x, top.y, r[4].w, 56 });
        loop.place ({ r[5].x, top.y, r[5].w, 56 });
        smooth.place ({ r[6].x, top.y, r[6].w, 56 });
        const int cy = rect_.y + kBarH + m;
        canvas.place ({ rect_.x + m, cy, rect_.w - 3 * m - kColW, rect_.bottom() - m - cy });
        // the four fields of the Q group in a column to the right of the canvas: the lane of the index chips above each
        const int colX = rect_.right() - m - kColW;
        const int H = rect_.bottom() - m - cy, each = std::clamp ((H - 4 * kQLane - 28) / 4, 56, 96);
        int y = cy + kQLane + 28;
        NumberField* f[4] = { &fx, &fy, &fcurve, &fgrid };
        for (int i = 0; i < 4; ++i)
        {
            f[i]->place ({ colX, y, kColW, each });
            y += each + kQLane;
        }
        column_ = { colX, cy, kColW, 28 };
    }
    void drawUnder (Pen& p) const override { (void) p; }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft (column_, "SELECTED POINT", T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
        p.textRight ({ rect_.right() - 8 - kColW, canvas.rect().y + 6, kColW, 18 }, info_, T.fLabel - 3.0f, p.c (T.textFaint, 255), T.fMin);
    }
    Rect column_;
    char info_[32] = {};
};

}} // namespace synthyboy_mpc::ui
