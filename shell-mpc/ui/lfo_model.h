// SynthyBoy - MPC UI component library: the LFO SHAPE MODEL. The drawn (custom) shape of an LFO as the UI edits it: up to 128 points {x 0 .. 1, y 0 .. 1, power -20 .. 20 of the segment to the next
// point}, the loop point and the smooth flag, exactly the data of the engine's content state (core ContentLfoShape) and evaluated exactly as the engine evaluates it (core/shapes/src/shape.cpp,
// ShapeData::evaluate), so the editor shows what the voice plays. Pure arithmetic, no allocation, no dependency: the conversion from and to the content state is the owner's (overlay/lfo_ui.h).
// The editing operations (insert, move, remove, curve, flat and ramp cells, snap, the selection operations: move, scale, delete, falloff; flip, shift, reset and bend of the whole shape) and the undo stack live
// here too: the canvas component (shape_editor.h) only turns touches into these calls. A point also carries the modulation of its X, its Y and its curve (a source id and an amount, as the content state's
// ContentPoint::modX / modY / modCurve), so that the editor does not lose what a preset has.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace synthyboy_mpc { namespace ui { namespace lfo {

constexpr int kMaxPoints = 128, kMinPoints = 2, kNumBuiltin = 12, kShapeCustom = 12, kNumShapes = 13;
constexpr float kMaxPower = 20.0f;
constexpr float kEps = 1.0e-6f;

/** The built-in shapes as the engine reads them (core/engine-own/env_lfo/lfo.h, lfoShape): the cycle position p 0 .. 1 -> -1 .. 1. */
inline float builtinAt (int shape, float p) noexcept
{
    constexpr float kTwoPi = 6.28318530717958647692f, kPi = 3.14159265358979323846f;
    static const float kLevels[8] = { 0.2f, 0.9f, 0.5f, 1.0f, 0.1f, 0.7f, 0.35f, 0.8f };
    switch (shape)
    {
        case 0: return -std::cos (kTwoPi * p);
        case 1: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
        case 2: return 2.0f * p - 1.0f;
        case 3: return 1.0f - 2.0f * p;
        case 4: return p < 0.5f ? 1.0f : -1.0f;
        case 5: return p < 0.25f ? 1.0f : -1.0f;
        case 6: return 2.0f * std::floor (p * 4.0f) / 3.0f - 1.0f;
        case 7: return 1.0f - 2.0f * std::floor (p * 4.0f) / 3.0f;
        case 8: return 2.0f * kLevels[std::min (static_cast<int> (p * 8.0f), 7)] - 1.0f;
        case 9: return 2.0f * p * p * p - 1.0f;
        case 10: return 2.0f * (1.0f - p) * (1.0f - p) * (1.0f - p) - 1.0f;
        case 11: return std::sin (kPi * p) * 2.0f - 1.0f;
        default: return 0.0f;
    }
}

/** The curve of a segment (core shapes powerScale): t 0 .. 1 -> 0 .. 1, convex for a positive power. */
inline float powerScale (float t, float power) noexcept
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (std::fabs (power) < 0.001f) return t;
    const float p = std::clamp (power, -25.0f, 25.0f);
    const float den = std::expm1 (p);
    if (std::fabs (den) < 1.0e-7f) return t;
    return std::clamp (std::expm1 (p * t) / den, 0.0f, 1.0f);
}

/** The modulation of one value of a point: `source` = the choice id of the modulation source (1 .. the number of sources of the engine, 0 = none), `amount` -1 .. 1 (the engine adds amount * source to the value; the curve: 1 = 10 power units). */
struct PointMod
{
    int source = 0;
    float amount = 0.0f;
    bool operator== (const PointMod& o) const noexcept { return source == o.source && amount == o.amount; }
    bool on() const noexcept { return source > 0; }
};

struct Point
{
    float x = 0.0f, y = 0.0f, power = 0.0f;
    PointMod mx, my, mc;   // the modulation of the time, of the level and of the curve of the segment after the point
    Point() = default;
    Point (float xIn, float yIn, float powerIn = 0.0f) noexcept : x (xIn), y (yIn), power (powerIn) {}
    bool operator== (const Point& o) const noexcept { return x == o.x && y == o.y && power == o.power && mx == o.mx && my == o.my && mc == o.mc; }
};

/** A set of point indices (the selection of the canvas): index 0 .. kMaxPoints - 1. The operations keep it in step with an insert or a removal. */
struct PointSet
{
    bool on[kMaxPoints] = {};
    bool has (int i) const noexcept { return i >= 0 && i < kMaxPoints && on[i]; }
    void add (int i) noexcept { if (i >= 0 && i < kMaxPoints) on[i] = true; }
    void remove (int i) noexcept { if (i >= 0 && i < kMaxPoints) on[i] = false; }
    void toggle (int i) noexcept { if (i >= 0 && i < kMaxPoints) on[i] = ! on[i]; }
    void clear() noexcept { for (bool& b : on) b = false; }
    int count() const noexcept { int c = 0; for (bool b : on) c += b ? 1 : 0; return c; }
    bool any() const noexcept { for (bool b : on) if (b) return true; return false; }
    int first() const noexcept { for (int i = 0; i < kMaxPoints; ++i) if (on[i]) return i; return -1; }
    int last() const noexcept { for (int i = kMaxPoints - 1; i >= 0; --i) if (on[i]) return i; return -1; }
    void selectAll (int n) noexcept { for (int i = 0; i < kMaxPoints; ++i) on[i] = i < n; }
    /** A point was inserted at `at` (the points from `at` on moved up by one). */
    void onInsert (int at) noexcept { if (at < 0 || at >= kMaxPoints) return; for (int i = kMaxPoints - 1; i > at; --i) on[i] = on[i - 1]; on[at] = false; }
    /** The point `at` was removed (the points after it moved down by one). */
    void onRemove (int at) noexcept { for (int i = std::max (0, at); i + 1 < kMaxPoints; ++i) on[i] = on[i + 1]; on[kMaxPoints - 1] = false; }
    bool operator== (const PointSet& o) const noexcept { for (int i = 0; i < kMaxPoints; ++i) if (on[i] != o.on[i]) return false; return true; }
};

/** The power that makes the middle of a segment (from y0 to y1) reach the height `mid`: the inverse of powerScale (0.5, p) = 1 / (exp (p / 2) + 1). A flat segment has no curve (0). */
inline float powerForMid (float y0, float y1, float mid) noexcept
{
    if (std::fabs (y1 - y0) < 1.0e-4f)
        return 0.0f;
    const float f = std::clamp ((mid - y0) / (y1 - y0), 0.02f, 0.98f);
    return std::clamp (2.0f * std::log ((1.0f - f) / f), -kMaxPower, kMaxPower);
}

struct ShapeModel
{
    int n = 0;
    Point p[kMaxPoints];
    int loop = -1;          // index of the loop point, -1 = none
    bool smooth = false;    // the S-curve (cosine) smoothing of every segment

    void initLinear() noexcept { n = 2; p[0] = { 0.0f, 0.0f, 0.0f }; p[1] = { 1.0f, 1.0f, 0.0f }; loop = -1; smooth = false; }

    /** The built-in shape as points: the sine and the triangle exactly, the stepped ones exactly (vertical steps), the curved ones (exp, half sine) sampled by 16 segments. The shape of a `Custom` LFO
        that has no points yet starts as a triangle. */
    static ShapeModel fromBuiltin (int shape) noexcept
    {
        ShapeModel m;
        m.loop = -1;
        auto add = [&m] (float x, float y) { if (m.n < kMaxPoints) m.p[m.n++] = { x, y, 0.0f }; };
        auto steps = [&] (const float* lv, int cells)
        {
            for (int i = 0; i < cells; ++i)
            {
                add (static_cast<float> (i) / static_cast<float> (cells), lv[i]);
                add (static_cast<float> (i + 1) / static_cast<float> (cells), lv[i]);
            }
        };
        switch (shape)
        {
            case 0: m.smooth = true; add (0.0f, 0.0f); add (0.5f, 1.0f); add (1.0f, 0.0f); break;
            case 1: default: add (0.0f, 0.0f); add (0.5f, 1.0f); add (1.0f, 0.0f); break;
            case 2: add (0.0f, 0.0f); add (1.0f, 1.0f); break;
            case 3: add (0.0f, 1.0f); add (1.0f, 0.0f); break;
            case 4: add (0.0f, 1.0f); add (0.5f, 1.0f); add (0.5f, 0.0f); add (1.0f, 0.0f); break;
            case 5: add (0.0f, 1.0f); add (0.25f, 1.0f); add (0.25f, 0.0f); add (1.0f, 0.0f); break;
            case 6: { const float lv[4] = { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f }; steps (lv, 4); break; }
            case 7: { const float lv[4] = { 1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 0.0f }; steps (lv, 4); break; }
            case 8: { const float lv[8] = { 0.2f, 0.9f, 0.5f, 1.0f, 0.1f, 0.7f, 0.35f, 0.8f }; steps (lv, 8); break; }
            case 9: case 10: case 11:
                for (int i = 0; i <= 16; ++i)
                {
                    const float t = static_cast<float> (i) / 16.0f;
                    add (t, 0.5f + 0.5f * builtinAt (shape, t));
                }
                break;
        }
        // the first and the last point of a stepped shape sit at x 0 and x 1 already; a shape with a vertical step at its first x keeps both points
        return m;
    }

    /** The value at the phase 0 .. 1: ShapeData::evaluate (a vertical step at the exact phase reads the point after it, as the engine does). */
    float eval (float phase) const noexcept
    {
        const int c = std::min (n, kMaxPoints);
        if (c == 0) return 0.0f;
        phase = std::isfinite (phase) ? std::clamp (phase, 0.0f, 1.0f) : 0.0f;
        if (c == 1) return p[0].y;
        if (phase <= p[0].x)
        {
            int idx = 0;
            while (idx + 1 < c && p[idx + 1].x <= phase) ++idx;
            return p[idx].y;
        }
        if (phase >= p[c - 1].x) return p[c - 1].y;
        int low = 0, high = c - 1;
        while (low + 1 < high)
        {
            const int mid = low + (high - low) / 2;
            if (p[mid].x <= phase) low = mid; else high = mid;
        }
        while (low + 1 < c - 1 && p[low + 1].x <= phase && p[low].x == p[low + 1].x) ++low;
        const Point& a = p[low];
        const Point& b = p[low + 1];
        const float dx = b.x - a.x;
        if (dx <= 1.0e-7f) return b.y;
        float t = std::clamp ((phase - a.x) / dx, 0.0f, 1.0f);
        if (smooth) t = 0.5f * (1.0f - std::cos (3.14159265358979323846f * t));
        return a.y + powerScale (t, a.power) * (b.y - a.y);
    }

    // ---- editing. Every operation keeps the invariants of the engine's sanitize: 2 .. 128 points, x ascending, the first x = 0, the last x = 1, y 0 .. 1, |power| <= 20. They return false (and change nothing) when
    // they cannot be done.
    void clean() noexcept
    {
        if (n < kMinPoints) { initLinear(); return; }
        for (int i = 0; i < n; ++i)
        {
            p[i].x = std::clamp (p[i].x, 0.0f, 1.0f);
            p[i].y = std::clamp (p[i].y, 0.0f, 1.0f);
            p[i].power = std::clamp (p[i].power, -kMaxPower, kMaxPower);
            if (i > 0 && p[i].x < p[i - 1].x) p[i].x = p[i - 1].x;
        }
        p[0].x = 0.0f;
        p[n - 1].x = 1.0f;
        if (loop >= n) loop = -1;
    }
    /** Adds a point at (x, y) (between its neighbours by x; after points of the same x); its segment takes the power of the segment it cuts. The index of the new point, -1 when the shape is full. */
    int insert (float x, float y) noexcept
    {
        if (n >= kMaxPoints) return -1;
        x = std::clamp (x, 0.0f, 1.0f);
        y = std::clamp (y, 0.0f, 1.0f);
        int at = 1;
        while (at < n - 1 && p[at].x <= x) ++at;
        at = std::clamp (at, 1, n - 1);
        for (int i = n; i > at; --i) p[i] = p[i - 1];
        p[at] = { x, y, 0.0f };
        ++n;
        if (loop >= at) ++loop;
        return at;
    }
    /** Moves point i (the end points only vertically; the others between their neighbours). */
    void move (int i, float x, float y) noexcept
    {
        if (i < 0 || i >= n) return;
        p[i].y = std::clamp (y, 0.0f, 1.0f);
        if (i > 0 && i < n - 1)
            p[i].x = std::clamp (x, p[i - 1].x, p[i + 1].x);
    }
    bool remove (int i) noexcept
    {
        if (n <= kMinPoints || i <= 0 || i >= n - 1) return false;
        for (int k = i; k + 1 < n; ++k) p[k] = p[k + 1];
        --n;
        if (loop == i) loop = -1; else if (loop > i) --loop;
        return true;
    }
    void setPower (int seg, float power) noexcept { if (seg >= 0 && seg + 1 < n) p[seg].power = std::clamp (power, -kMaxPower, kMaxPower); }
    /** The segment that holds the phase x (the index of its first point), -1 for a vertical or an empty one. */
    int segmentAt (float x) const noexcept
    {
        for (int i = 0; i + 1 < n; ++i)
            if (x >= p[i].x && x <= p[i + 1].x && p[i + 1].x - p[i].x > kEps)
                return i;
        return -1;
    }
    /** Replaces the shape between xa and xb by the straight line (xa, ya) -> (xb, yb) (a flat piece: ya = yb; a ramp up / down: ya < yb / ya > yb): the points inside go, the value just outside the interval stays
        (a vertical step is made at the edge when the shape was at another height there). Fails when the result has more than 128 points. */
    bool replaceRange (float xa, float xb, float ya, float yb) noexcept
    {
        xa = std::clamp (xa, 0.0f, 1.0f);
        xb = std::clamp (xb, 0.0f, 1.0f);
        if (xb - xa < kEps) return false;
        ya = std::clamp (ya, 0.0f, 1.0f);
        yb = std::clamp (yb, 0.0f, 1.0f);
        // the height the shape arrives at xa with (the first point there, else the curve) and leaves xb with (the last point there, else the curve)
        float leftVal = eval (xa), rightVal = eval (xb);
        for (int i = 0; i < n; ++i)
            if (std::fabs (p[i].x - xa) <= kEps) { leftVal = p[i].y; break; }
        for (int i = n - 1; i >= 0; --i)
            if (std::fabs (p[i].x - xb) <= kEps) { rightVal = p[i].y; break; }
        const bool leftEdge = xa <= kEps, rightEdge = xb >= 1.0f - kEps;
        Point out[kMaxPoints + 8];
        int k = 0, newLoop = -1;
        for (int i = 0; i < n && ! leftEdge; ++i)
            if (p[i].x < xa - kEps)
            {
                if (i == loop) newLoop = k;
                out[k++] = p[i];
            }
        if (! leftEdge)
            out[k++] = { xa, leftVal, 0.0f };
        out[k++] = { leftEdge ? 0.0f : xa, ya, 0.0f };
        out[k++] = { rightEdge ? 1.0f : xb, yb, 0.0f };
        if (! rightEdge)
        {
            out[k++] = { xb, rightVal, 0.0f };
            for (int i = 0; i < n; ++i)
                if (p[i].x > xb + kEps)
                {
                    if (i == loop) newLoop = k;
                    out[k++] = p[i];
                }
        }
        int m = 0;   // a point equal to the one before it is no use
        for (int i = 0; i < k; ++i)
            if (m == 0 || std::fabs (out[i].x - out[m - 1].x) > kEps || std::fabs (out[i].y - out[m - 1].y) > 1.0e-5f)
                out[m++] = out[i];
        if (m < kMinPoints || m > kMaxPoints) return false;
        for (int i = 0; i < m; ++i) p[i] = out[i];
        n = m;
        loop = newLoop >= 0 && newLoop < n ? newLoop : -1;
        clean();
        return true;
    }

    // ---- the selection (the canvas selects points with a PointSet) --------------------------------------------------------------------------------------------------------------------------------
    /** The span of the points of `s`: the smallest and the largest time and level. false when `s` holds no point of this shape. */
    bool spanOf (const PointSet& s, float& x0, float& x1, float& y0, float& y1) const noexcept
    {
        bool any = false;
        x0 = y0 = 1.0f;
        x1 = y1 = 0.0f;
        for (int i = 0; i < n; ++i)
            if (s.has (i))
            {
                any = true;
                x0 = std::min (x0, p[i].x); x1 = std::max (x1, p[i].x);
                y0 = std::min (y0, p[i].y); y1 = std::max (y1, p[i].y);
            }
        return any;
    }
    /** Removes the points of `s` that can go (never the first and the last), the highest first, so that the loop point follows. The number removed. */
    int removeSet (const PointSet& s) noexcept
    {
        int removed = 0;
        for (int i = n - 2; i >= 1; --i)
            if (s.has (i) && remove (i))
                ++removed;
        return removed;
    }
    /** Moves the points of `s` together by (dx, dy): ONE move for all of them, clamped as a whole so that the order of the points is kept (the first and the last point keep their time) and every level stays in 0 .. 1,
        so the form of the selection is kept. dx and dy come back as the move that was made. true when something moved. */
    bool moveSet (const PointSet& s, float& dx, float& dy) noexcept
    {
        float lo = -1.0f, hi = 1.0f, ylo = -1.0f, yhi = 1.0f;
        bool any = false, anyX = false;
        for (int i = 0; i < n; ++i)
        {
            if (! s.has (i))
                continue;
            any = true;
            ylo = std::max (ylo, -p[i].y);
            yhi = std::min (yhi, 1.0f - p[i].y);
            if (i > 0 && i < n - 1)
            {
                anyX = true;
                if (! s.has (i - 1) || i - 1 == 0) lo = std::max (lo, p[i - 1].x - p[i].x);          // a neighbour that stays: the first point, or one that is not selected
                if (! s.has (i + 1) || i + 1 == n - 1) hi = std::min (hi, p[i + 1].x - p[i].x);
            }
        }
        if (! any)
        {
            dx = dy = 0.0f;
            return false;
        }
        dx = anyX ? std::clamp (dx, std::min (lo, 0.0f), std::max (hi, 0.0f)) : 0.0f;
        dy = std::clamp (dy, std::min (ylo, 0.0f), std::max (yhi, 0.0f));
        for (int i = 0; i < n; ++i)
            if (s.has (i))
            {
                p[i].y = std::clamp (p[i].y + dy, 0.0f, 1.0f);
                if (i > 0 && i < n - 1)
                    p[i].x = std::clamp (p[i].x + dx, 0.0f, 1.0f);   // dx is within the bounds of the neighbours that stay, and the selected neighbours move by the same dx
            }
        return dx != 0.0f || dy != 0.0f;
    }
    /** Sets the height (the largest level minus the smallest) of the selection to `height` (0.01 .. 1), around its centre; the centre moves only as far as the range needs to stay inside 0 .. 1. false when the
        selection has no height (all its points are level) or nothing changed. */
    bool scaleSetY (const PointSet& s, float height) noexcept
    {
        float x0, x1, y0, y1;
        if (! spanOf (s, x0, x1, y0, y1) || y1 - y0 < 1.0e-4f)
            return false;
        height = std::clamp (height, 0.01f, 1.0f);
        const float f = height / (y1 - y0), c = 0.5f * (y0 + y1), nc = std::clamp (c, 0.5f * height, 1.0f - 0.5f * height);
        bool changed = false;
        for (int i = 0; i < n; ++i)
            if (s.has (i))
            {
                const float ny = std::clamp (nc + (p[i].y - c) * f, 0.0f, 1.0f);
                changed = changed || ny != p[i].y;
                p[i].y = ny;
            }
        return changed;
    }
    /** The falloff of a soft drag by the point `anchor` (weights 0 .. 1 for the n points into `w`, at least n entries). With a selection `s` of two or more points that holds the anchor: the anchor 1, the other selected points
        1 - d / (1.25 dmax) (d = their distance in time to the anchor, dmax the largest), the points that are not selected 0. Otherwise (or `s` null): every point within `radius` of the anchor's time gets 1 - d / radius.
        The number of points with a weight above zero. */
    int falloffWeights (int anchor, const PointSet* s, float radius, float* w) const noexcept
    {
        for (int i = 0; i < n; ++i)
            w[i] = 0.0f;
        if (anchor < 0 || anchor >= n)
            return 0;
        int count = 0;
        const float ax = p[anchor].x;
        if (s != nullptr && s->count() >= 2 && s->has (anchor))
        {
            float dmax = 0.0f;
            for (int i = 0; i < n; ++i)
                if (s->has (i))
                    dmax = std::max (dmax, std::fabs (p[i].x - ax));
            for (int i = 0; i < n; ++i)
                if (s->has (i))
                    w[i] = i == anchor ? 1.0f : std::clamp (1.0f - std::fabs (p[i].x - ax) / std::max (1.25f * dmax, 1.0e-4f), 0.0f, 1.0f);
        }
        else
        {
            radius = std::max (radius, 1.0e-3f);
            for (int i = 0; i < n; ++i)
                w[i] = i == anchor ? 1.0f : std::clamp (1.0f - std::fabs (p[i].x - ax) / radius, 0.0f, 1.0f);
        }
        for (int i = 0; i < n; ++i)
            count += w[i] > 0.0f ? 1 : 0;
        return count;
    }
    /** Moves every point i by (dx, dy) * w[i] (w 0 .. 1): the levels clamped to 0 .. 1, the times of the inner points repaired so that the order is kept (the first and the last point keep their time). true when a point moved. */
    bool moveWeighted (const float* w, float dx, float dy) noexcept
    {
        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            if (! (w[i] > 0.0f))
                continue;
            const float ny = std::clamp (p[i].y + dy * w[i], 0.0f, 1.0f);
            changed = changed || ny != p[i].y;
            p[i].y = ny;
            if (i > 0 && i < n - 1)
            {
                const float nx = std::clamp (p[i].x + dx * w[i], 0.0f, 1.0f);
                changed = changed || nx != p[i].x;
                p[i].x = nx;
            }
        }
        for (int i = 1; i < n - 1; ++i) p[i].x = std::max (p[i].x, p[i - 1].x);
        for (int i = n - 2; i >= 1; --i) p[i].x = std::min (p[i].x, p[i + 1].x);
        return changed;
    }

    // ---- the whole shape -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** Mirrors the shape in time (the first point becomes the last): the times become 1 - x, the curve of every segment and its modulation change sign, the loop point is the mirrored point. */
    void flipH() noexcept
    {
        Point q[kMaxPoints];
        for (int i = 0; i < n; ++i)
        {
            const Point& a = p[n - 1 - i];
            q[i] = a;
            q[i].x = 1.0f - a.x;
            q[i].mx.amount = -a.mx.amount;
            if (i + 1 < n)   // the segment from q[i] to q[i + 1] is the old one from p[n - 2 - i] to p[n - 1 - i]: its curve belongs to the old point p[n - 2 - i]
            {
                q[i].power = -p[n - 2 - i].power;
                q[i].mc = { p[n - 2 - i].mc.source, -p[n - 2 - i].mc.amount };
            }
            else
            {
                q[i].power = 0.0f;
                q[i].mc = PointMod {};
            }
        }
        for (int i = 0; i < n; ++i)
            p[i] = q[i];
        if (loop >= 0 && loop < n)
            loop = n - 1 - loop;
        clean();
    }
    /** Mirrors the levels: y becomes 1 - y (the curves keep their power: the shape mirrors as a whole), the modulation of the level changes sign. */
    void flipV() noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            p[i].y = 1.0f - p[i].y;
            p[i].my.amount = -p[i].my.amount;
        }
    }
    /** Turns the shape in time by `d` of a cycle (0 .. 1, to the right; what leaves at the right end enters at the left): the point where the shape is cut gets a point at both ends and splits its segment (the
        curves of the two pieces are those of the original: a power p becomes p t0 and p (1 - t0) exactly; with SMOOTH on the pieces take the power that gives the same middle level). The shape keeps its form: the
        jump where the end meets the start stays a vertical step. false when `d` is no shift or the result would not fit in 128 points. */
    bool shift (float d) noexcept
    {
        d = d - std::floor (d);
        if (d < kEps || d > 1.0f - kEps)
            return false;
        const float cut = 1.0f - d;
        int g0 = 0;
        while (g0 < n && p[g0].x < cut - kEps) ++g0;
        int g1 = g0;
        while (g1 < n && p[g1].x <= cut + kEps) ++g1;
        if (g0 < 1 || g1 >= n)
            return false;
        Point out[kMaxPoints + 8];
        int k = 0, newLoop = -1;
        auto put = [&] (const Point& q, int from)
        {
            if (from >= 0 && from == loop) newLoop = k;
            out[k++] = q;
        };
        if (g0 == g1)   // the cut is inside a segment: split it
        {
            const int i = g0 - 1;
            const float x0 = p[i].x, x1 = p[g0].x, t0 = std::clamp ((cut - x0) / (x1 - x0), 0.0f, 1.0f), cutVal = eval (cut);
            float pl = p[i].power * t0, pr = p[i].power * (1.0f - t0);
            if (smooth)
            {
                pl = powerForMid (p[i].y, cutVal, eval (0.5f * (x0 + cut)));
                pr = powerForMid (cutVal, p[g0].y, eval (0.5f * (cut + x1)));
            }
            Point first (0.0f, cutVal, std::clamp (pr, -kMaxPower, kMaxPower));
            first.mc = p[i].mc;
            put (first, -1);
            for (int j = g0; j < n; ++j) { Point q = p[j]; q.x = p[j].x - cut; put (q, j); }
            for (int j = 0; j <= i; ++j)
            {
                Point q = p[j];
                q.x = p[j].x + d;
                if (j == i) q.power = std::clamp (pl, -kMaxPower, kMaxPower);
                put (q, j);
            }
            put (Point (1.0f, cutVal, 0.0f), -1);
        }
        else   // the cut is at a point (or at a vertical step: the group g0 .. g1 - 1): the shape starts with its last point, ends with its first
        {
            Point first = p[g1 - 1];
            first.x = 0.0f;
            put (first, g1 - 1);
            for (int j = g1; j < n; ++j) { Point q = p[j]; q.x = p[j].x - cut; put (q, j); }
            for (int j = 0; j < g0; ++j) { Point q = p[j]; q.x = p[j].x + d; put (q, j); }
            Point last = p[g0];
            last.x = 1.0f;
            last.power = 0.0f;
            last.mc = PointMod {};
            put (last, g0 == g1 - 1 ? -1 : g0);   // no vertical step: the one point is the start of the shape (the loop point stays there), the end is only its copy
        }
        int m = 0;   // a point equal to the one before it is no use (the later one has the curve that counts)
        int map[kMaxPoints + 8];
        for (int i = 0; i < k; ++i)
        {
            if (m > 0 && std::fabs (out[i].x - out[m - 1].x) <= kEps && std::fabs (out[i].y - out[m - 1].y) <= 1.0e-5f)
            {
                out[m - 1] = out[i];
                map[i] = m - 1;
            }
            else
            {
                out[m] = out[i];
                map[i] = m++;
            }
        }
        if (m < kMinPoints || m > kMaxPoints)
            return false;
        for (int i = 0; i < m; ++i)
            p[i] = out[i];
        n = m;
        loop = newLoop >= 0 ? map[newLoop] : -1;
        clean();
        return true;
    }
    /** Sets the curve of every segment to straight. true when a curve was not. */
    bool resetCurves() noexcept
    {
        bool changed = false;
        for (int i = 0; i + 1 < n; ++i)
        {
            changed = changed || p[i].power != 0.0f;
            p[i].power = 0.0f;
        }
        return changed;
    }
    /** True for a segment that has a curve to bend (a vertical or a flat segment has none: the canvas offers no handle). */
    bool bendable (int seg) const noexcept { return seg >= 0 && seg + 1 < n && p[seg + 1].x - p[seg].x >= 0.02f && std::fabs (p[seg + 1].y - p[seg].y) >= 0.01f; }
    /** The mean curve (power) of the segments that have a curve to bend, 0 when there is none. */
    float meanCurve() const noexcept
    {
        float sum = 0.0f;
        int c = 0;
        for (int i = 0; i + 1 < n; ++i)
            if (bendable (i)) { sum += p[i].power; ++c; }
        return c > 0 ? sum / static_cast<float> (c) : 0.0f;
    }
    /** Adds `delta` to the curve of every segment that has one to bend (clamped to +-20). true when a curve changed. */
    bool bendAll (float delta) noexcept
    {
        bool changed = false;
        for (int i = 0; i + 1 < n; ++i)
            if (bendable (i))
            {
                const float np = std::clamp (p[i].power + delta, -kMaxPower, kMaxPower);
                changed = changed || np != p[i].power;
                p[i].power = np;
            }
        return changed;
    }
    /** The index of the point whose time is nearest to `x`, and its distance (-1 / 1 for no point). */
    int nearestByX (float x, float* distance = nullptr) const noexcept
    {
        int best = -1;
        float bd = 2.0f;
        for (int i = 0; i < n; ++i)
            if (std::fabs (p[i].x - x) < bd) { bd = std::fabs (p[i].x - x); best = i; }
        if (distance != nullptr) *distance = bd;
        return best;
    }
    /** Sets the modulation of one value of every point of `s`: `which` 0 = the time, 1 = the level, 2 = the curve; source 0 removes it. */
    void setMod (const PointSet& s, int which, int source, float amount) noexcept
    {
        for (int i = 0; i < n; ++i)
            if (s.has (i))
            {
                PointMod& m = which == 0 ? p[i].mx : (which == 1 ? p[i].my : p[i].mc);
                m.source = std::max (0, source);
                m.amount = source > 0 ? std::clamp (amount, -1.0f, 1.0f) : 0.0f;
            }
    }

    bool operator== (const ShapeModel& o) const noexcept
    {
        if (n != o.n || loop != o.loop || smooth != o.smooth) return false;
        for (int i = 0; i < n; ++i)
            if (! (p[i] == o.p[i])) return false;
        return true;
    }
    bool operator!= (const ShapeModel& o) const noexcept { return ! (*this == o); }
};

/** The grid of the editor: `div` columns and rows (2 .. 32). The snap of a position to the nearest grid line, and the cell of a position. */
constexpr int kMinGrid = 2, kMaxGrid = 32;
inline float snapTo (float v, int div) noexcept { return std::round (std::clamp (v, 0.0f, 1.0f) * static_cast<float> (div)) / static_cast<float> (div); }
inline int cellOf (float v, int div) noexcept { return std::clamp (static_cast<int> (std::floor (v * static_cast<float> (div))), 0, div - 1); }

/** The grid a shape was drawn on: the coarsest of 4, 8, 12, 16, 24, 32 whose lines all the points of the shape sit on, 8 when none does (the grid is a view setting, it is not stored with the shape). */
inline int inferGrid (const ShapeModel& m) noexcept
{
    static const int kTry[6] = { 4, 8, 12, 16, 24, 32 };
    for (int g : kTry)
    {
        bool ok = true;
        for (int i = 0; i < m.n && ok; ++i)
            ok = std::fabs (m.p[i].x * static_cast<float> (g) - std::round (m.p[i].x * static_cast<float> (g))) < 1.0e-3f && std::fabs (m.p[i].y * static_cast<float> (g) - std::round (m.p[i].y * static_cast<float> (g))) < 1.0e-3f;
        if (ok) return g;
    }
    return 8;
}

/** The undo stack of the editor: the last kDepth states of the shape. */
class Undo
{
public:
    static constexpr int kDepth = 32;
    void clear() noexcept { count_ = 0; head_ = 0; }
    /** Remembers `m` as the state before a change (an identical one on top is not kept twice). */
    void push (const ShapeModel& m) noexcept
    {
        if (count_ > 0 && snap_[(head_ + kDepth - 1) % kDepth] == m) return;
        snap_[head_] = m;
        head_ = (head_ + 1) % kDepth;
        count_ = std::min (count_ + 1, kDepth);
    }
    bool pop (ShapeModel& out) noexcept
    {
        if (count_ == 0) return false;
        head_ = (head_ + kDepth - 1) % kDepth;
        out = snap_[head_];
        --count_;
        return true;
    }
    int depth() const noexcept { return count_; }

private:
    ShapeModel snap_[kDepth];
    int head_ = 0, count_ = 0;
};

// ---- the sync state of an LFO (lfoN.sync: 0 Free (Hz), 1 Tempo, 2 Tempo dotted, 3 Tempo triplet, 4 Key follow) as the UI shows it: a mode (HZ, BPM, KEY) and the two switches TRIP and DOT
enum class SyncMode : uint8_t { Hz, Bpm, Key };
inline SyncMode syncModeOf (int sync) noexcept { return sync <= 0 ? SyncMode::Hz : (sync >= 4 ? SyncMode::Key : SyncMode::Bpm); }
inline bool syncDot (int sync) noexcept { return sync == 2; }
inline bool syncTrip (int sync) noexcept { return sync == 3; }
/** The sync value of a mode and the two switches (only the tempo mode has them; dot and triplet exclude each other, dot wins). */
inline int syncOf (SyncMode mode, bool trip, bool dot) noexcept { return mode == SyncMode::Hz ? 0 : (mode == SyncMode::Key ? 4 : (dot ? 2 : (trip ? 3 : 1))); }

}}} // namespace synthyboy_mpc::ui::lfo
