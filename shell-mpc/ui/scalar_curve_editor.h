// SynthyBoy - MPC UI component library: the NOTE / VELOCITY CURVE EDITOR (step 5 of docs/ui-map/env-filter-lfo-plan.md, section (d)). One component for both scalar sources: the curve a key number (NOTE) or a velocity
// (VELO) is read through before it drives the modulation matrix. Same contract as every component (core.h): the owner puts the curve, the legato flag, the glide time text, the routing rows and the source in, writes
// whatever comes out; the editor keeps only the gesture and the view settings (selection, grid, snap, undo).
//  - ScalarCurve: the model. 2 .. 64 points {x 0 .. 1 ascending, y 0 .. 1, power -20 .. 20 = the bend of the segment after the point}, the first x = 0 and the last x = 1 fixed. An EMPTY curve (the identity, what the content
//    stores) is the two points (0, 0) and (1, 1): `identity()` / `isIdentity()`. The value at x is the ENGINE'S OWN: the points go through synthyboy::shapes::ShapeData (sanitize, bake 2048, evaluateTable: exactly
//    what core/engine-own/voice/own_content.h bakes and Voice::curvedSource reads), the identity reads x itself, the result is clamped to 0 .. 1. Nothing of that evaluation is copied here.
//  - ScalarCurveCanvas: the small graph (axes and labels, grid, the curve, point handles) and its touch: drag a handle, a tap on the empty graph adds a point ON the curve at that x, a vertical drag on a segment bends its
//    power, a long press on a middle point deletes it. Out: Index changes { kSelect, kEdit, kEditEnd }.
//  - ScalarCurveEditor: the screen (1100 x 500 at least; designed for the body of the Filter tab): the canvas, INIT / UNDO / DELETE POINT, the LEGATO toggle with the read-only GLIDE chip, the USED BY list with TARGETS, and
//    two Q groups: Q1 [POINT, X, Y, CURVE], Q2 [LEGATO, GRID X, GRID Y, SNAP]. `apply` turns a change of any part into an Event for the owner (overlay/scalar_curve_ui.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "composite.h"
#include "led_toggle.h"
#include "lfo_model.h"
#include "note_mask.h"
#include "native_apertures.h"
#include "qgroup.h"
#include "synthyboy/shapes/shape.h"

namespace synthyboy_mpc { namespace ui { namespace scurve {

constexpr int kMaxPoints = 64, kMinPoints = 2, kUndoDepth = 16, kTableSize = 2048;   // the caps of the content (kMaxCurvePoints), the history, the engine's table
constexpr float kMaxPower = 20.0f;      // |power| of a segment (kMaxPointPower of the content)
constexpr float kMinGap = 0.01f;        // the smallest distance a dragged point keeps from its neighbours (of the width)
constexpr int kKeys = 127;              // x = key / 127 (NOTE) and x = velocity / 127 (VELO): the engine's scale

struct Pt
{
    float x = 0.0f, y = 0.0f, power = 0.0f;
    bool operator== (const Pt& o) const noexcept { return x == o.x && y == o.y && power == o.power; }
};
struct Snapshot
{
    int n = 2;
    Pt p[kMaxPoints];
    bool operator== (const Snapshot& o) const noexcept
    {
        if (n != o.n) return false;
        for (int i = 0; i < n; ++i) if (! (p[i] == o.p[i])) return false;
        return true;
    }
};

class ScalarCurve
{
public:
    ScalarCurve() { identity(); }
    int n = 2;
    Pt p[kMaxPoints];

    void identity() noexcept { n = 2; p[0] = { 0.0f, 0.0f, 0.0f }; p[1] = { 1.0f, 1.0f, 0.0f }; bake(); }
    bool isIdentity() const noexcept { return n == 2 && p[0] == Pt { 0.0f, 0.0f, 0.0f } && p[1] == Pt { 1.0f, 1.0f, 0.0f }; }
    Snapshot snapshot() const noexcept { Snapshot s; s.n = n; for (int i = 0; i < n; ++i) s.p[i] = p[i]; return s; }
    void restore (const Snapshot& s) noexcept { n = std::clamp (s.n, kMinPoints, kMaxPoints); for (int i = 0; i < n; ++i) p[i] = s.p[i]; bake(); }

    /** Takes the points of the content (fewer than 2: the identity) the way the engine does (ShapeData::sanitize: x clamped, sorted, the first x = 0, the last x = 1), y clamped to 0 .. 1, power to +-20. */
    void set (const Pt* pts, int count) noexcept
    {
        if (pts == nullptr || count < kMinPoints)
        {
            identity();
            return;
        }
        synthyboy::shapes::ShapeData sd;
        sd.numPoints = static_cast<size_t> (std::min (count, kMaxPoints));
        for (size_t i = 0; i < sd.numPoints; ++i)
        {
            sd.points[i].x = pts[i].x;
            sd.points[i].y = pts[i].y;
            sd.points[i].curve = pts[i].power;
        }
        sd.sanitize();
        n = static_cast<int> (sd.numPoints);
        for (int i = 0; i < n; ++i)
        {
            const auto& q = sd.points[static_cast<size_t> (i)];
            p[i] = { q.x, std::isfinite (q.y) ? std::clamp (q.y, 0.0f, 1.0f) : 0.0f, std::clamp (q.curve, -kMaxPower, kMaxPower) };
        }
        bake();
    }

    /** The engine's value at x: the identity reads x, the others the baked table (Voice::curvedSource), clamped to 0 .. 1. */
    float at (float x) const noexcept
    {
        x = std::isfinite (x) ? std::clamp (x, 0.0f, 1.0f) : 0.0f;
        if (isIdentity())
            return x;
        return std::clamp (synthyboy::shapes::ShapeData::evaluateTable (table_.data(), table_.size(), x), 0.0f, 1.0f);
    }

    // ---- the editing operations: each keeps the invariants (2 .. 64 points, x ascending, ends fixed, y 0 .. 1, |power| <= 20) and returns whether anything changed
    bool movable (int i) const noexcept { return i >= 0 && i < n; }
    /** The range x of point i can take: between its neighbours with the minimum gap (the ends do not move in x). */
    void xRange (int i, float& lo, float& hi) const noexcept
    {
        if (i <= 0) { lo = hi = 0.0f; return; }
        if (i >= n - 1) { lo = hi = 1.0f; return; }
        lo = p[i - 1].x + kMinGap;
        hi = p[i + 1].x - kMinGap;
        if (lo > hi)
            lo = hi = 0.5f * (p[i - 1].x + p[i + 1].x);
    }
    bool move (int i, float x, float y) noexcept
    {
        if (! movable (i) || ! std::isfinite (x) || ! std::isfinite (y))
            return false;
        float lo, hi;
        xRange (i, lo, hi);
        const Pt old = p[i];
        p[i].x = std::clamp (x, lo, hi);
        p[i].y = std::clamp (y, 0.0f, 1.0f);
        if (p[i] == old)
            return false;
        bake();
        return true;
    }
    /** Adds a point at x on the curve (y = the value there); its segment takes the power of the segment it cuts. The index of the new point; -1 when the curve is full or x is within the minimum gap of a point. */
    int insert (float x) noexcept
    {
        if (n >= kMaxPoints || ! std::isfinite (x))
            return -1;
        x = std::clamp (x, 0.0f, 1.0f);
        int at_ = 0;
        while (at_ + 1 < n && p[at_ + 1].x <= x)
            ++at_;
        for (int i = std::max (0, at_ - 1); i <= std::min (n - 1, at_ + 1); ++i)
            if (std::fabs (p[i].x - x) < kMinGap)
                return -1;
        const float y = at (x);
        const float power = p[at_].power;
        for (int i = n; i > at_ + 1; --i)
            p[i] = p[i - 1];
        p[at_ + 1] = { x, y, power };
        ++n;
        bake();
        return at_ + 1;
    }
    /** Removes a MIDDLE point (the ends stay; never below 2 points). */
    bool remove (int i) noexcept
    {
        if (n <= kMinPoints || i <= 0 || i >= n - 1)
            return false;
        for (int k = i; k + 1 < n; ++k)
            p[k] = p[k + 1];
        --n;
        bake();
        return true;
    }
    /** The power of the segment after point `seg` (0 .. n - 2). */
    bool setPower (int seg, float power) noexcept
    {
        if (seg < 0 || seg + 1 >= n || ! std::isfinite (power))
            return false;
        const float v = std::clamp (power, -kMaxPower, kMaxPower);
        if (v == p[seg].power)
            return false;
        p[seg].power = v;
        bake();
        return true;
    }
    /** A segment a drag can bend: it spans some width and some height (a flat or a vertical segment has no curve to bend). */
    bool bendable (int seg) const noexcept { return seg >= 0 && seg + 1 < n && p[seg + 1].x - p[seg].x >= 0.02f && std::fabs (p[seg + 1].y - p[seg].y) >= 0.01f; }
    /** Bends the segment so that its middle (in x) reaches the height `mid` (0 .. 1). */
    bool bendTo (int seg, float mid) noexcept { return bendable (seg) && setPower (seg, lfo::powerForMid (p[seg].y, p[seg + 1].y, mid)); }
    /** The segment x lies in (the last one for x = 1). */
    int segmentAt (float x) const noexcept
    {
        int s = 0;
        while (s + 2 < n && p[s + 1].x <= x)
            ++s;
        return s;
    }
    /** The table the engine reads (for the tests): bake () has run after every change. */
    const float* table() const noexcept { return table_.data(); }
    size_t tableSize() const noexcept { return table_.size(); }
    bool operator== (const ScalarCurve& o) const noexcept { return snapshot() == o.snapshot(); }

    /** Bakes the points with the engine's code (the same calls as OwnContent::compileCurve). */
    void bake() noexcept
    {
        synthyboy::shapes::ShapeData sd;
        sd.numPoints = static_cast<size_t> (n);
        for (int i = 0; i < n; ++i)
        {
            sd.points[static_cast<size_t> (i)].x = p[i].x;
            sd.points[static_cast<size_t> (i)].y = p[i].y;
            sd.points[static_cast<size_t> (i)].curve = p[i].power;
        }
        sd.sanitize();
        sd.bake (table_.data(), table_.size());
    }

private:
    std::array<float, kTableSize> table_ {};
};

/** The bounded history of the editor: the last kUndoDepth states before a change (an identical one on top is not kept twice). */
class History
{
public:
    void clear() noexcept { count_ = 0; head_ = 0; }
    void push (const Snapshot& s) noexcept
    {
        if (count_ > 0 && snap_[(head_ + kUndoDepth - 1) % kUndoDepth] == s)
            return;
        snap_[head_] = s;
        head_ = (head_ + 1) % kUndoDepth;
        count_ = std::min (count_ + 1, kUndoDepth);
    }
    bool pop (Snapshot& out) noexcept
    {
        if (count_ == 0)
            return false;
        head_ = (head_ + kUndoDepth - 1) % kUndoDepth;
        out = snap_[head_];
        --count_;
        return true;
    }
    int depth() const noexcept { return count_; }

private:
    Snapshot snap_[kUndoDepth];
    int head_ = 0, count_ = 0;
};

enum class Source : uint8_t { Velocity = 0, Note = 1 };
/** The x axis labels: the key / velocity number at each of the five places. NOTE is named by keyName (the MPC convention, key 60 = C3, key 0 = C-2: C-2, C1, C3, C5, G8), VELO by its number (0, 32, 64, 96, 127). */
inline int axisPlace (int k) noexcept { return k < 0 ? 0 : (k > 4 ? 4 : k); }   // (no std::clamp: GCC 12 warns "may be used uninitialized" about the references to temporaries)
inline int axisValue (Source s, int k) noexcept { static const int kNote[5] = { 0, 36, 60, 84, 127 }, kVelo[5] = { 0, 32, 64, 96, 127 }; return (s == Source::Note ? kNote : kVelo)[axisPlace (k)]; }
inline const char* axisLabel (Source s, int k) noexcept
{
    struct Labels
    {
        char note[5][8], velo[5][8];
        Labels() noexcept
        {
            for (int i = 0; i < 5; ++i)
            {
                keyName (axisValue (Source::Note, i), note[i], sizeof note[i]);
                std::snprintf (velo[i], sizeof velo[i], "%d", axisValue (Source::Velocity, i));
            }
        }
    };
    static const Labels L;
    return (s == Source::Note ? L.note : L.velo)[axisPlace (k)];
}

}}} // namespace synthyboy_mpc::ui::scurve

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** The small graph with its touch. The model lives here while it is edited (`model`: the owner puts the content in with `setCurve` when no finger is down); every change goes out as an Index change: index kSelect (the selected
    point changed), kEdit (the curve changed during a gesture: the owner may write it, throttled), kEditEnd (the curve changed and the gesture is over, or it was one step: write it now). */
class ScalarCurveCanvas : public Component
{
public:
    enum Kind { kSelect = 1, kEdit = 2, kEditEnd = 3 };
    static constexpr int kLeft = 52, kRight = 20, kTop = 14, kBottom = 40, kHandleR = 10, kHitR = 22;
    static constexpr double kLongMs = 600.0;
    struct State
    {
        scurve::Source source = scurve::Source::Velocity;
        int selected = 0;           // the selected point
        int gridX = 4, gridY = 4;   // divisions (lfo::kMinGrid .. kMaxGrid): drawn, used by SNAP
        bool snap = false;
        float marker = -1.0f;       // the live x (0 .. 1) of the key / velocity played last: a vertical marker on the curve, drawn by drawLive only (never by the cached draw); < 0: none
    };
    State state;
    scurve::ScalarCurve model;
    scurve::History undo;

    Size minSize() const override { return compactMode ? Size { 420, 220 } : Size { 640, 300 }; }
    bool compactMode = false;
    Rect plot() const noexcept { return { rect_.x + kLeft, rect_.y + kTop, std::max (1, rect_.w - kLeft - kRight), std::max (1, rect_.h - kTop - kBottom) }; }
    int pxOf (float x) const noexcept { const Rect r = plot(); return r.x + static_cast<int> (std::lround (x * static_cast<float> (r.w - 1))); }
    int pyOf (float y) const noexcept { const Rect r = plot(); return r.bottom() - 1 - static_cast<int> (std::lround (y * static_cast<float> (r.h - 1))); }
    float xFrom (int px) const noexcept { const Rect r = plot(); return std::clamp (static_cast<float> (px - r.x) / static_cast<float> (std::max (1, r.w - 1)), 0.0f, 1.0f); }
    float yFrom (int py) const noexcept { const Rect r = plot(); return std::clamp (static_cast<float> (r.bottom() - 1 - py) / static_cast<float> (std::max (1, r.h - 1)), 0.0f, 1.0f); }
    int gridXc() const noexcept { return std::clamp (state.gridX, lfo::kMinGrid, lfo::kMaxGrid); }
    int gridYc() const noexcept { return std::clamp (state.gridY, lfo::kMinGrid, lfo::kMaxGrid); }
    uint32_t lineColour() const noexcept { return theme::L().src[state.source == scurve::Source::Note ? 3 : 2]; }
    /** The live marker (not cached: it moves with the notes): a vertical line at the x of the key / velocity played last, from the axis to the curve, and a dot where the curve gives the value the source takes
        (the same `model.at` the engine's curve evaluates). Only the animated layer draws it: the static picture (draw) does not know it, so a new note repaints nothing else. */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (state.marker < 0.0f || ! enabled_)
            return;
        const theme::Look& T = theme::L();
        const float x = static_cast<float> (pxOf (std::clamp (state.marker, 0.0f, 1.0f))), y = static_cast<float> (pyOf (model.at (state.marker)));
        const Rect pl = plot();
        sb.style (1.0f, true);
        sb.paintOff();
        sb.line (x, static_cast<float> (pl.bottom() - 1), x, y, 2.0f, theme::rgba (T.flow[0], 200));
        sb.disc (x, y, 6.0f, theme::rgba (T.flow[0], 255));
        sb.disc (x, y, 2.8f, 0xffffffffu);
    }

    /** The owner's copy of the content: the curve and (kept in range) the selection. Not while a finger is down. */
    void setCurve (const scurve::Pt* pts, int count)
    {
        model.set (pts, count);
        state.selected = std::clamp (state.selected, 0, model.n - 1);
    }
    void select (int i) noexcept { state.selected = std::clamp (i, 0, model.n - 1); }

    // ---- the operations of the buttons and the fields: each pushes the undo state and reports whether it changed the curve
    bool edit (const scurve::Snapshot& before, bool changed)
    {
        if (changed)
            undo.push (before);
        return changed;
    }
    bool opInit()
    {
        const scurve::Snapshot b = model.snapshot();
        const bool was = model.isIdentity();
        model.identity();
        state.selected = 0;
        return edit (b, ! was);
    }
    bool opUndo()
    {
        scurve::Snapshot s;
        if (! undo.pop (s))
            return false;
        model.restore (s);
        state.selected = std::clamp (state.selected, 0, model.n - 1);
        return true;
    }
    bool opDelete()
    {
        const scurve::Snapshot b = model.snapshot();
        const int at = state.selected;
        const bool ok = edit (b, model.remove (at));
        if (ok)
            state.selected = std::clamp (at - 1, 0, model.n - 1);
        return ok;
    }
    bool opMove (int i, float x, float y) { const scurve::Snapshot b = model.snapshot(); return edit (b, model.move (i, x, y)); }
    bool opPower (int seg, float power) { const scurve::Snapshot b = model.snapshot(); return edit (b, model.setPower (seg, power)); }

protected:
    float snapX (float x) const noexcept { return state.snap ? lfo::snapTo (x, gridXc()) : x; }
    float snapY (float y) const noexcept { return state.snap ? lfo::snapTo (y, gridYc()) : y; }
    int pointAt (int px, int py) const noexcept
    {
        int best = -1;
        long bestD = static_cast<long> (kHitR) * kHitR + 1;
        for (int i = 0; i < model.n; ++i)
        {
            const long dx = pxOf (model.p[i].x) - px, dy = pyOf (model.p[i].y) - py, d = dx * dx + dy * dy;
            if (d < bestD)
                bestD = d, best = i;
        }
        return best;
    }

    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect pl = plot();
        // the grid and the axes: GRID X / GRID Y divisions
        for (int i = 1; i < gridXc(); ++i)
        {
            const int x = pl.x + static_cast<int> (std::lround (static_cast<double> (i) * (pl.w - 1) / gridXc()));
            p.line (static_cast<float> (x), static_cast<float> (pl.y), static_cast<float> (x), static_cast<float> (pl.bottom()), T.hair, p.c (T.grid, 255));
        }
        for (int i = 1; i < gridYc(); ++i)
        {
            const int y = pl.y + static_cast<int> (std::lround (static_cast<double> (i) * (pl.h - 1) / gridYc()));
            p.line (static_cast<float> (pl.x), static_cast<float> (y), static_cast<float> (pl.right()), static_cast<float> (y), T.hair, p.c (T.grid, 255));
        }
        // the labels: the key / velocity at five places, 0 / 50 / 100 percent
        for (int k = 0; k < 5; ++k)
        {
            const float fx = static_cast<float> (scurve::axisValue (state.source, k)) / static_cast<float> (scurve::kKeys);
            const int x = pxOf (fx);
            p.line (static_cast<float> (x), static_cast<float> (pl.bottom()), static_cast<float> (x), static_cast<float> (pl.bottom() + 4), T.hair, p.c (T.textDim, 255));
            Rect box { x - 24, pl.bottom() + 5, 48, 16 };
            box.x = std::clamp (box.x, rect_.x, rect_.right() - box.w);
            p.textCentered (box, scurve::axisLabel (state.source, k), T.fSmall, p.c (T.textDim, 255), T.fMin);
        }
        static const char* const kPct[3] = { "100%", "50%", "0%" };
        for (int k = 0; k < 3; ++k)
        {
            const int y = pl.y + static_cast<int> (std::lround (static_cast<double> (k) * (pl.h - 1) / 2.0));
            p.textRight ({ rect_.x + 2, y - 8, kLeft - 8, 16 }, kPct[k], T.fSmall, p.c (T.textDim, 255), T.fMin);
        }
        p.textCentered ({ pl.x, pl.bottom() + 21, pl.w, 16 }, state.source == scurve::Source::Note ? "NOTE (KEY IN)" : "VELOCITY (IN)", T.fSmall, p.c (T.textFaint, 255), T.fMin);
        // the identity diagonal, faint, when the curve is a custom one
        p.style (1.0f, true);
        if (! model.isIdentity())
            p.line (static_cast<float> (pxOf (0.0f)), static_cast<float> (pyOf (0.0f)), static_cast<float> (pxOf (1.0f)), static_cast<float> (pyOf (1.0f)), T.hair, p.c (T.textFaint, 110));
        // Preserve fractional screen coordinates all the way to the anti-aliased stroke.
        const auto screenX = [&] (float x) { return pl.x + x * (pl.w - 1); };
        const auto screenY = [&] (float y) { return pl.bottom() - 1 - y * (pl.h - 1); };
        const uint32_t col = p.c (lineColour(), 255);
        for (int seg = 0; seg + 1 < model.n; ++seg)
        {
            const auto& from = model.p[seg];
            const auto& to = model.p[seg + 1];
            float x = screenX (from.x), y = screenY (from.y);
            const float endX = screenX (to.x);
            if (from.power != 0.0f && from.y != to.y)
            {
                for (float nextX = x + 2.0f; nextX < endX; nextX += 2.0f)
                {
                    const float norm = (nextX - pl.x) / static_cast<float> (std::max (1, pl.w - 1));
                    const float nextY = screenY (model.at (norm));
                    p.line (x, y, nextX, nextY, T.curve, col);
                    x = nextX;
                    y = nextY;
                }
            }
            p.line (x, y, endX, screenY (to.y), T.curve, col);
        }
        // the middle of every bendable segment (what a vertical drag on the segment moves), the handles
        for (int s = 0; s + 1 < model.n; ++s)
            if (model.bendable (s))
            {
                const float mx = 0.5f * (model.p[s].x + model.p[s + 1].x);
                p.arc (static_cast<float> (pxOf (mx)), static_cast<float> (pyOf (model.at (mx))), 4.0f, 0.0f, 2.0f * kPi, 1.4f, p.c (T.textDim, 160));
            }
        for (int i = 0; i < model.n; ++i)
        {
            const float cx = static_cast<float> (pxOf (model.p[i].x)), cy = static_cast<float> (pyOf (model.p[i].y));
            const bool sel = i == state.selected;
            p.disc (cx, cy, static_cast<float> (kHandleR), p.c (sel ? T.flow[0] : lineColour(), 255));
            p.disc (cx, cy, static_cast<float> (kHandleR) - 3.0f, p.c (T.bg1, 255));
            if (sel)
                p.disc (cx, cy, 4.0f, p.c (T.flow[0], 255));
        }
        p.plain();
    }

    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        downMs_ = now;
        longFired_ = pushed_ = false;
        start_ = model.snapshot();
        hit_ = pointAt (x, y);
        mode_ = Mode::Empty;
        if (hit_ >= 0)
        {
            mode_ = Mode::Drag;
            offX_ = pxOf (model.p[hit_].x) - x;
            offY_ = pyOf (model.p[hit_].y) - y;
            if (state.selected != hit_)
            {
                state.selected = hit_;
                return Change::indexed (id_, kSelect, hit_);
            }
            return Change::none();
        }
        seg_ = model.segmentAt (xFrom (x));
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        if (longFired_ || ! tap_.move (x, y))
            return Change::none();
        bool changed = false;
        if (mode_ == Mode::Drag)
            changed = model.move (hit_, snapX (xFrom (x + offX_)), snapY (yFrom (y + offY_)));
        else if (mode_ == Mode::Empty || mode_ == Mode::Bend)
        {
            if (mode_ == Mode::Empty && std::abs (y - tap_.startY()) < std::abs (x - tap_.startX()))
                return Change::none();   // a horizontal drag on the empty graph does nothing
            mode_ = Mode::Bend;
            changed = model.bendTo (seg_, yFrom (y));
        }
        if (! changed)
            return Change::none();
        if (! pushed_)
        {
            undo.push (start_);
            pushed_ = true;
        }
        return Change::indexed (id_, kEdit, 0);
    }
    Change onUp (int, int) override
    {
        const Mode m = mode_;
        mode_ = Mode::None;
        if (longFired_)
            return Change::none();
        const bool wasTap = tap_.up() != 0;
        if (m == Mode::Empty && wasTap)   // a tap on the empty graph: a point ON the curve at that x (snapped to the grid with SNAP)
        {
            const float x = snapX (xFrom (tap_.startX()));
            const scurve::Snapshot before = model.snapshot();
            const int at = model.insert (x);
            if (at < 0)
                return Change::none();
            undo.push (before);
            state.selected = at;
            return Change::indexed (id_, kEditEnd, at);
        }
        return pushed_ ? Change::indexed (id_, kEditEnd, 0) : Change::none();
    }
public:
    /** A long press (600 ms without leaving the tap square) on a middle point deletes it. */
    Change tick (double nowMs) override
    {
        if (! captured_ || ! enabled_ || longFired_ || mode_ != Mode::Drag || tap_.moved() || nowMs - downMs_ < kLongMs)
            return Change::none();
        if (hit_ <= 0 || hit_ >= model.n - 1 || model.n <= scurve::kMinPoints)
            return Change::none();
        longFired_ = true;
        const scurve::Snapshot before = model.snapshot();
        if (! model.remove (hit_))
            return Change::none();
        undo.push (before);
        state.selected = std::clamp (hit_ - 1, 0, model.n - 1);
        return Change::indexed (id_, kEditEnd, 0);
    }

private:
    enum class Mode : uint8_t { None, Empty, Drag, Bend };
    Mode mode_ = Mode::None;
    TapGesture tap_;
    scurve::Snapshot start_;
    int hit_ = -1, seg_ = 0, offX_ = 0, offY_ = 0;
    double downMs_ = 0.0;
    bool longFired_ = false, pushed_ = false;
};

// ===============================================================================================================================================================
/** What the owner has to do after a change of any part of the editor. */
struct ScalarCurveEvent
{
    enum Kind { None, Curve, Legato, Targets, Expand } kind = None;
    bool final = false;     // Curve: the gesture is over (or it was a single step): write at once, do not wait for the throttle
    int value = 0;          // Legato: 0 / 1
};

/** One slot that uses the source (the owner scans the matrix): the destination, the depth in percent, the polarity, "uses it as the AUX source", bypassed. */
struct ScalarUsedRow { char dest[32] = {}; int percent = 0; bool bipolar = false, aux = false, bypass = false; };
struct ScalarUsedBy
{
    static constexpr int kMaxRows = 8;
    int total = 0, count = 0;   // all slots that use the source; the rows given (at most kMaxRows)
    ScalarUsedRow rows[kMaxRows];
};

/** The USED BY list of the editor: the slots that use the source (destination, depth, polarity, AUX), up to four rows and "+ n more", or "NOT ROUTED". A display, no touch; drawn in the colour the graph's line has. */
class ScalarUsedByList : public Component
{
public:
    static constexpr int kRowH = 36, kShownRows = 4;
    const ScalarUsedBy* used = nullptr;
    uint32_t colour = 0;   // the dot of a row that is not bypassed (the line colour of the graph)

    Size minSize() const override { return { 200, (kShownRows + 1) * kRowH }; }
    int touchTargets (Rect*, int) const override { return 0; }
    Change touchDown (int, int, double) override { captured_ = false; return Change::none(); }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (used == nullptr || used->total <= 0)
        {
            p.textCentered ({ rect_.x, rect_.y + 12, rect_.w, kRowH * 2 }, "NOT ROUTED: tap TARGETS to add", T.fLabel, p.c (T.textDim, 255), T.fMin);
            return;
        }
        const int rows = std::min (used->count, kShownRows);
        for (int r = 0; r < rows; ++r)
        {
            const ScalarUsedRow& u = used->rows[r];
            const Rect rr { rect_.x, rect_.y + r * kRowH, rect_.w, kRowH };
            char right[24];
            std::snprintf (right, sizeof right, "%s%+d%%%s", u.aux ? "AUX " : "", u.percent, u.bipolar ? " BI" : "");
            const int rw = std::min (rr.w / 2, static_cast<int> (std::ceil (Pen::textWidth (right, T.fLabel))) + 8);
            const uint32_t ink = p.c (u.bypass ? T.textFaint : T.text, 255);
            p.disc (static_cast<float> (rr.x + 5), static_cast<float> (rr.y + rr.h / 2), 3.0f, p.c (u.bypass ? T.textFaint : colour, 255));
            p.textLeft ({ rr.x + 14, rr.y, rr.w - 14 - rw - 6, rr.h }, u.dest, T.fLabel + 1.0f, ink, T.fMin);
            p.textRight ({ rr.right() - rw, rr.y, rw, rr.h }, right, T.fLabel, p.c (T.textDim, 255), T.fMin);
        }
        if (used->total > rows)
        {
            char more[24];
            std::snprintf (more, sizeof more, "+ %d more", used->total - rows);
            const Rect rr { rect_.x, rect_.y + rows * kRowH, rect_.w, kRowH };
            p.textLeft ({ rr.x + 14, rr.y, rr.w - 14, 24 }, more, T.fLabel, p.c (T.textDim, 255), T.fMin);
        }
    }
};

class ScalarCurveEditor : public Composite
{
public:
    // the ids of the parts (the owner reads them back from the changes)
    enum Id { kIdCanvas = 1, kIdPoint, kIdX, kIdY, kIdCurve, kIdGridX, kIdGridY, kIdSnap, kIdLegato, kIdInit, kIdUndo, kIdDelete, kIdTargets, kIdExpand };
    static constexpr int kMaxGroups = 2, kBandH = 100, kSideW = 400, kMargin = 8, kRowH = ScalarUsedByList::kRowH, kShownRows = ScalarUsedByList::kShownRows, kMaxRows = ScalarUsedBy::kMaxRows, kTitleH = 34;

    ScalarCurveCanvas canvas;
    NumberField point { "POINT", NumberField::Format::IntPlain }, xField { "X", NumberField::Format::IntPlain }, yField { "Y", NumberField::Format::IntPlain }, curveField { "CURVE", NumberField::Format::Decimal2 };
    NumberField gridX { "GRID X", NumberField::Format::IntPlain }, gridY { "GRID Y", NumberField::Format::IntPlain };
    LedToggle snap, legato;
    MenuButton init { "INIT", MenuButton::Icon::None }, undoButton { "UNDO", MenuButton::Icon::None }, del { "DELETE POINT", MenuButton::Icon::None }, targets { "TARGETS", MenuButton::Icon::None };
    MenuButton expandBtn { "", MenuButton::Icon::Expand };
    QGroup groups[kMaxGroups];   // Q1 [POINT, X, Y, CURVE], Q2 [LEGATO, GRID X, GRID Y, SNAP]

    using UsedRow = ScalarUsedRow;
    using UsedBy = ScalarUsedBy;
    ScalarUsedByList usedList;   // the rows of USED BY (a display part of the editor)
    UsedBy used;
    char glideText[16] = {};       // "120 ms": the glide time of the legato (master.portamento.time), read only

    ScalarCurveEditor()
    {
        canvas.setId (kIdCanvas);
        point.setId (kIdPoint); xField.setId (kIdX); yField.setId (kIdY); curveField.setId (kIdCurve);
        gridX.setId (kIdGridX); gridY.setId (kIdGridY); snap.setId (kIdSnap); legato.setId (kIdLegato);
        init.setId (kIdInit); undoButton.setId (kIdUndo); del.setId (kIdDelete); targets.setId (kIdTargets);
        expandBtn.setId (kIdExpand);
        snap.setLabel ("SNAP"); legato.setLabel ("LEGATO");
        snap.centre = legato.centre = 0.36f;
        point.state.min = 1.0; point.state.step = 1.0;
        xField.state.min = 0.0; xField.state.max = scurve::kKeys; xField.state.step = 1.0;
        yField.state.min = 0.0; yField.state.max = 100.0; yField.state.step = 1.0;
        curveField.state.min = -scurve::kMaxPower; curveField.state.max = scurve::kMaxPower; curveField.state.step = 0.5; curveField.state.pxPerStep = 6;
        for (NumberField* f : { &gridX, &gridY })
        {
            f->state.min = lfo::kMinGrid; f->state.max = lfo::kMaxGrid; f->state.step = 1.0; f->state.defaultValue = 4.0; f->state.value = 4.0;
        }
        addKid (canvas);
        for (NumberField* f : { &point, &xField, &yField, &curveField, &gridX, &gridY })
            addKid (*f, false);
        addKid (snap, false); addKid (legato, false); addKid (init, false); addKid (undoButton, false); addKid (del, false); addKid (targets, false); addKid (usedList);
        addKid (expandBtn, false);
        usedList.used = &used;
        groups[0].set (0, &point); groups[0].set (1, &xField); groups[0].set (2, &yField); groups[0].set (3, &curveField);
        groups[1].set (0, &legato); groups[1].set (1, &gridX); groups[1].set (2, &gridY); groups[1].set (3, &snap);
        setActiveGroup (0);
        sync();
    }
    Size minSize() const override { return compact_ ? Size { 480, 500 } : Size { 1100, 500 }; }
    bool compact() const noexcept { return compact_; }
    void setCompact (bool compact)
    {
        if (compact_ == compact)
            return;
        compact_ = compact;
        canvas.compactMode = compact;
        show (usedList, ! compact_);
        if (rect_.w > 0)
            layout();
    }
    static const char* groupName (int i) noexcept { static const char* const n[kMaxGroups] = { "EDIT", "LEGATO AND GRID" }; return i >= 0 && i < kMaxGroups ? n[i] : ""; }
    static const char* title (scurve::Source s) noexcept { return s == scurve::Source::Note ? "NOTE" : "VELO"; }

    // ---- the state the owner gives
    void setSource (scurve::Source s) { canvas.state.source = s; usedList.colour = canvas.lineColour(); sync(); }
    scurve::Source source() const noexcept { return canvas.state.source; }
    void setLegato (bool on) { legato.state.on = on; }
    bool legatoOn() const noexcept { return legato.state.on; }
    void setGlide (const char* text) { setText (glideText, sizeof glideText, text); }
    void setUsedBy (const UsedBy& u) { used = u; }
    /** The curve of the content (not while a finger is down on the canvas). */
    void setCurve (const scurve::Pt* pts, int count) { canvas.setCurve (pts, count); sync(); }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int i)
    {
        for (int k = 0; k < kMaxGroups; ++k)
            groups[k].setActive (k == i);
        active_ = i >= 0 && i < kMaxGroups ? i : -1;
    }
    Rect block (int i) const noexcept { return i >= 0 && i < kMaxGroups ? blocks_[i] : Rect {}; }
    void setOwnQFrames (bool own) noexcept { ownQFrames_ = own; }
    Rect usedRect() const noexcept { return usedRect_; }
    Rect glideRect() const noexcept { return glideRect_; }
    Rect usedRowRect (int row) const noexcept { return { usedRect_.x + 8, usedRect_.y + kTitleH - 12 + row * kRowH, std::max (0, usedRect_.w - 16), kRowH }; }
    /** Puts the model into the fields (the selected point, the grid, the buttons): after every change of the model and of the content. */
    void sync()
    {
        const scurve::ScalarCurve& m = canvas.model;
        const int sel = std::clamp (canvas.state.selected, 0, m.n - 1);
        canvas.state.selected = sel;
        point.state.max = m.n; point.state.value = sel + 1;
        xField.state.value = std::round (m.p[sel].x * scurve::kKeys);
        xField.state.readOnly = sel == 0 || sel == m.n - 1;   // the ends keep x 0 and 1
        yField.state.value = std::round (m.p[sel].y * 100.0f);
        std::snprintf (yField.state.text, sizeof yField.state.text, "%d%%", static_cast<int> (std::lround (m.p[sel].y * 100.0f)));
        curveField.state.value = m.p[sel].power;
        curveField.state.readOnly = sel >= m.n - 1;   // the last point has no segment after it
        gridX.state.value = canvas.gridXc(); gridY.state.value = canvas.gridYc();
        snap.state.on = canvas.state.snap;
    }

    /** Applies a change of one of the parts to the model and says what the owner has to do. A change of an unknown id does nothing. */
    ScalarCurveEvent apply (const Change& c)
    {
        using E = ScalarCurveEvent;
        if (! c)
            return {};
        ScalarCurveCanvas& cv = canvas;
        E e;
        switch (c.id)
        {
            case kIdCanvas:
                if (c.kind == ChangeKind::Index && c.index == ScalarCurveCanvas::kSelect) { sync(); break; }
                if (c.kind == ChangeKind::Index) { e = { E::Curve, c.index == ScalarCurveCanvas::kEditEnd, 0 }; sync(); }
                break;
            case kIdPoint:
                if (c.kind == ChangeKind::Index) { cv.select (static_cast<int> (std::lround (c.value)) - 1); sync(); }
                break;
            case kIdX:
                if (c.kind == ChangeKind::Index && cv.opMove (cv.state.selected, static_cast<float> (c.value) / static_cast<float> (scurve::kKeys), cv.model.p[cv.state.selected].y)) e = { E::Curve, true, 0 };
                sync();
                break;
            case kIdY:
                if (c.kind == ChangeKind::Index && cv.opMove (cv.state.selected, cv.model.p[cv.state.selected].x, static_cast<float> (c.value) / 100.0f)) e = { E::Curve, true, 0 };
                sync();
                break;
            case kIdCurve:
                if (c.kind == ChangeKind::Index && cv.opPower (cv.state.selected, static_cast<float> (c.value))) e = { E::Curve, true, 0 };
                sync();
                break;
            case kIdGridX: if (c.kind == ChangeKind::Index) cv.state.gridX = std::clamp (static_cast<int> (std::lround (c.value)), lfo::kMinGrid, lfo::kMaxGrid); sync(); break;
            case kIdGridY: if (c.kind == ChangeKind::Index) cv.state.gridY = std::clamp (static_cast<int> (std::lround (c.value)), lfo::kMinGrid, lfo::kMaxGrid); sync(); break;
            case kIdSnap: if (c.kind == ChangeKind::Index) cv.state.snap = c.index != 0; sync(); break;
            case kIdLegato: if (c.kind == ChangeKind::Index) e = { E::Legato, false, c.index != 0 ? 1 : 0 }; break;
            case kIdInit: if (c.kind == ChangeKind::Request && cv.opInit()) e = { E::Curve, true, 0 }; sync(); break;
            case kIdUndo: if (c.kind == ChangeKind::Request && cv.opUndo()) e = { E::Curve, true, 0 }; sync(); break;
            case kIdDelete: if (c.kind == ChangeKind::Request && cv.opDelete()) e = { E::Curve, true, 0 }; sync(); break;
            case kIdTargets: if (c.kind == ChangeKind::Request) e = { E::Targets, false, 0 }; break;
            case kIdExpand: if (c.kind == ChangeKind::Request) e = { E::Expand, false, 0 }; break;
            default: break;
        }
        return e;
    }
    /** The Q members of group g (0 .. 1) in slot order, null where a group has fewer: what the owner publishes. */
    const ControlModel* qMember (int g, int slot) const noexcept { return g >= 0 && g < kMaxGroups ? groups[g].member (slot) : nullptr; }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        const int maxG = compact_ ? 1 : kMaxGroups;
        if (ownQFrames_)
        {
            for (int i = 0; i < maxG; ++i)
                if (blocks_[i].contains (x, y) || groups[i].rect().contains (x, y))
                {
                    setActiveGroup (i);
                    break;
                }
        }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen p (sb, enabled_, inactive_);
        const std::array<Rect, 1> corner { expandBtn.rect() };
        paintAroundNativeCells (sb, graphRect_, corner, [&] { drawNativeGraphFrame (p, graphRect_); });
        canvas.draw (sb);
        if (! compact_) usedList.draw (sb);
        Cell cell (sb, rect_);
        drawExtra (p);
        if (ownQFrames_)
        {
            const int maxG = compact_ ? 1 : kMaxGroups;
            for (int i = 0; i < maxG; ++i)
            {
                if (i == active_)
                    groups[i].draw (sb);
                else
                    groups[i].drawIdle (sb);
            }
        }
    }
    /** The live marker of the graph (the animated layer): call after `draw`. */
    void drawLive (gpu::SceneBuilder& sb) const { canvas.drawLive (sb); }
    /** The live x of the key / velocity played last (0 .. 1; negative: none). */
    void setMarker (float x) noexcept { canvas.state.marker = x; }

    static constexpr int kNativeFields = 8;
    static constexpr int kNativeButtons = 4;
    std::array<const Component*, kNativeFields + kNativeButtons> nativeControls() const noexcept
    {
        return { &point, &xField, &yField, &curveField, &gridX, &gridY, &snap, &legato,
                 &init, &undoButton, &del, &targets };
    }
    std::array<Rect, kNativeFields + kNativeButtons> nativeRects() const noexcept
    {
        std::array<Rect, kNativeFields + kNativeButtons> out {};
        const auto controls = nativeControls();
        for (unsigned i = 0; i < out.size(); ++i)
            out[i] = controls[i]->rect();
        return out;
    }
    Rect nativeGlideRect() const noexcept { return ! compact_ && legato.state.on ? glideRect().inset (2) : Rect {}; }
    Rect nativeExpandRect() const noexcept { return compact_ ? expandBtn.rect() : Rect {}; }

protected:
    void layout() override
    {
        if (compact_)
        {
            layoutCompact();
            return;
        }
        const int m = kMargin;
        const int bandY = rect_.bottom() - m - kBandH;
        const Rect gr { rect_.x + m, rect_.y + m, std::max (0, rect_.w - 3 * m - kSideW), std::max (0, bandY - m - (rect_.y + m)) };
        graphRect_ = gr;
        expandBtn.place ({ gr.right() - 8 - 56, gr.y + 4, 56, 56 });
        // The Filter owner exposes the native collapse action above the graph.
        canvas.place ({ gr.x, gr.y + 64, gr.w, std::max (0, gr.h - 64) });
        // ---- the side column: the three buttons, the glide chip, the USED BY block with TARGETS at its foot
        const int sx = gr.right() + m, sw = rect_.right() - m - sx;
        const int bw = (sw - 2 * kGap) / 3;
        init.place ({ sx, gr.y, bw, 56 });
        undoButton.place ({ sx + bw + kGap, gr.y, bw, 56 });
        del.place ({ sx + 2 * (bw + kGap), gr.y, sw - 2 * (bw + kGap), 56 });
        glideRect_ = { sx, gr.y + 56 + m, sw, 40 };
        legato.setEnabled (enabled_);
        const int uy = glideRect_.bottom() + m;
        usedRect_ = { sx, uy, sw, std::max (0, gr.bottom() - uy) };
        targets.place ({ usedRect_.x + 8, usedRect_.bottom() - 8 - 56, usedRect_.w - 16, 56 });
        const int ly = usedRect_.y + kTitleH - 12;
        usedList.place ({ usedRect_.x + 8, ly, std::max (0, usedRect_.w - 16), std::max (0, targets.rect().y - 4 - ly) });
        usedList.colour = canvas.lineColour();
        // ---- the bottom band: two blocks, a Q group each
        const int bwid = (rect_.w - 3 * m) / 2;
        blocks_[0] = { rect_.x + m, bandY, bwid, kBandH };
        blocks_[1] = { rect_.x + 2 * m + bwid, bandY, rect_.w - 3 * m - bwid, kBandH };
        auto inner = [&] (int b, Component* const* c)
        {
            const Rect in { blocks_[b].x + 6, blocks_[b].y + kTitleH, blocks_[b].w - 12, blocks_[b].h - kTitleH - 6 };
            const int cw = (in.w - 3 * kGap) / 4;
            for (int i = 0; i < 4; ++i)
                c[i]->place ({ in.x + i * (cw + kGap), in.y, i == 3 ? in.w - 3 * (cw + kGap) : cw, in.h });
            groups[b].setFrameRect (blocks_[b]);
        };
        Component* edit[4] = { &point, &xField, &yField, &curveField };
        Component* opts[4] = { &legato, &gridX, &gridY, &snap };
        inner (0, edit);
        inner (1, opts);
    }
    void layoutCompact()
    {
        const int W = rect_.w, X = rect_.x;
        const int targetsW = 130;
        targets.place ({ rect_.right() - targetsW, rect_.y, targetsW, 48 });
        glideRect_ = {};
        usedRect_ = {};
        usedList.place ({});
        legato.setEnabled (enabled_);

        const int optRowH = 56, editH = 88;
        const int optY = rect_.bottom() - optRowH;
        const int editY = optY - 4 - editH;
        const int graphY = targets.rect().bottom() + 4;
        graphRect_ = { X, graphY, W, std::max (0, editY - 8 - graphY) };
        expandBtn.place ({ graphRect_.right() - 8 - 56, graphRect_.y + 4, 56, 56 });
        canvas.place ({ graphRect_.x, graphRect_.y + 64, graphRect_.w, std::max (0, graphRect_.h - 64) });

        blocks_[0] = { X, editY, W, editH };
        blocks_[1] = {};
        const int editPad = 4, editGap = 5, editInW = W - 2 * editPad;
        const int ew = (editInW - 3 * editGap) / 4;
        Component* edit[4] = { &point, &xField, &yField, &curveField };
        for (int i = 0; i < 4; ++i)
            edit[i]->place ({ X + editPad + i * (ew + editGap), editY + 28, i == 3 ? editInW - 3 * (ew + editGap) : ew, 56 });
        groups[0].setFrameRect (blocks_[0]);

        const int og = 6, ow = (W - 3 * og) / 4;
        Component* opts[4] = { &gridX, &gridY, &snap, &legato };
        for (int i = 0; i < 4; ++i)
            opts[i]->place ({ X + i * (ow + og), optY, i == 3 ? W - 3 * (ow + og) : ow, optRowH });

        init.place ({});
        undoButton.place ({});
        del.place ({});
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (compact_)
        {
            p.textLeft ({ blocks_[0].x + 10, blocks_[0].y + 3, std::max (0, blocks_[0].w - 20), 16 }, groupName (0), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
            const char* hdr = canvas.state.source == scurve::Source::Note ? "NOTE · KEY CURVE" : "VELO · VELOCITY CURVE";
            p.textLeft ({ graphRect_.x + 12, graphRect_.y + 4, std::max (0, graphRect_.w - 84), 56 }, hdr, T.fLabel, p.c (T.textDim, 255), T.fMin);
            return;
        }
        for (int i = 0; i < kMaxGroups; ++i)
            p.textLeft ({ blocks_[i].x + 10, blocks_[i].y + 3, std::max (0, blocks_[i].w - 20), 16 }, groupName (i), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        // the title of the USED BY block (the rows are the display part `usedList`)
        p.textLeft ({ usedRect_.x + 10, usedRect_.y + 6, usedRect_.w - 20, 18 }, "USED BY", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
    }
    Rect blocks_[kMaxGroups], usedRect_, glideRect_, graphRect_;
    int active_ = -1;
    bool compact_ = false, ownQFrames_ = true;
};

}} // namespace synthyboy_mpc::ui
