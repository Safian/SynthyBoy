// SynthyBoy - MPC UI component library: the LFO SHAPE MODEL. The drawn (custom) shape of an LFO as the UI edits it: up to 128 points {x 0 .. 1, y 0 .. 1, power -20 .. 20 of the segment to the next
// point}, the loop point and the smooth flag, exactly the data of the engine's content state (core ContentLfoShape) and evaluated exactly as the engine evaluates it (core/shapes/src/shape.cpp,
// ShapeData::evaluate), so the editor shows what the voice plays. Pure arithmetic, no allocation, no dependency: the conversion from and to the content state is the owner's (overlay/lfo_ui.h).
// The editing operations (insert, move, remove, curve, flat and ramp cells, snap) and the undo stack live here too: the canvas component (shape_editor.h) only turns touches into these calls.
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

struct Point
{
    float x = 0.0f, y = 0.0f, power = 0.0f;
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

    bool operator== (const ShapeModel& o) const noexcept
    {
        if (n != o.n || loop != o.loop || smooth != o.smooth) return false;
        for (int i = 0; i < n; ++i)
            if (p[i].x != o.p[i].x || p[i].y != o.p[i].y || p[i].power != o.p[i].power) return false;
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
