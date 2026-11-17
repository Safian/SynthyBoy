// SynthyBoy - MPC UI component library: the ENVELOPE GRAPH (EnvGraph). The picture of a DAHDSR envelope (delay, attack, hold, decay, sustain, release and the three curves) with the handles that
// edit it by touch and the running dots of the sounding voices. Same contract as every component (core.h): a plain state struct and a rectangle in, a drawing and a Change out; it knows no engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The axis (Serum 2 manual, "ruler scaling below the envelope"): the x axis is TIME. LOCKED (the default): the envelope always fills the plot, so a change of the times only changes how much time the ruler under
// the graph stands for (a continuous fit, not steps). A stage of 0 takes no width; a stage with time takes its share of the width on the one common scale, but never less than kMinStageW pixels (so the
// handles of two short neighbours stay apart); the sustain is a level, a short fixed gap marks it. UNLOCKED: the scale is the zoom the player sets by dragging along the ruler band (the window in
// time units), the envelope may run past the right edge (the handles beyond it are not offered). The ruler marks are round times (time_ruler.h: ms / s, or beats in BEATS mode), they are also the vertical grid.
// A dragged time handle follows the finger (the picture fits the plot again at every step: the value that puts the handle under the finger is found on the refitted picture); the release handle and
// everything in the unlocked state move by the distance of the finger at the scale the drag started with.
// Handles (touch targets of at least 56 x 56): the ends of delay, attack, hold, decay (two dimensions: its time and the sustain level at once) and release, and one small curve handle on the middle of
// attack, decay and release (a vertical drag bends the stage: the handle follows the finger).
// In:  State { shape (normalised times, sustain level 0 .. 1, curves -20 .. 20), defaults (a double tap on a handle), the ghost shape (envelope 1 behind another one), the dots of the voices (engine fine
//      stage 1 .. 6 and the fraction of the stage that has run), the value text of every handle (the owner formats it with the unit), the colour }.
// Out: a Value change on every move that changed something (id = the graph's id); `edit ()` says what: { handle, t, level, curve }. A double tap on a handle is the same with the default. The owner writes
//      the parameters and gives the new shape back with the next state.
#pragma once

#include "core.h"
#include "time_ruler.h"

namespace synthyboy_mpc { namespace ui {

namespace envgraph
{
enum Handle : int { None = -1, Delay = 0, Attack, Hold, Decay, Release, CurveA, CurveD, CurveR, kNumHandles };

/** The envelope as the graph needs it: t = the normalised time of delay, attack, hold, decay, release (0 .. 1), the sustain level (0 .. 1) and the curves of attack, decay, release (-20 .. 20: the parameters of the engine). */
struct Shape
{
    double t[5] = { 0.0, 0.1, 0.0, 0.38, 0.3 };
    double timeMs[5] = { 0.0, 100.0, 0.0, 400.0, 200.0 };
    double sustain = 1.0;
    double curve[3] = { 0.0, -3.0, -3.0 };
};

constexpr double kSustainWidth = 1.4;
constexpr double kMinStageW = 14.0;   // pixels: a stage with time is at least this wide, so the 13 pixel handles at its two ends never overlap

inline double maxTimeMs (int stage) noexcept
{
    return (stage == 0 || stage == 2) ? 4000.0 : 10000.0;
}
/** The longest time a stage has, in ms or in beats (the parameters env.delay / hold: 4000 ms or 8 beats, the others 10000 ms or 16 beats). */
inline double maxTime (int stage, bool beats) noexcept
{
    return beats ? ((stage == 0 || stage == 2) ? 8.0 : 16.0) : maxTimeMs (stage);
}
inline double skewOf (int stage) noexcept
{
    return (stage == 0 || stage == 2) ? 0.4 : 0.3;
}
inline double timeFromNorm (int stage, double norm, bool beats) noexcept
{
    norm = std::clamp (norm, 0.0, 1.0);
    if (norm <= 0.0) return 0.0;
    return maxTime (stage, beats) * std::pow (norm, 1.0 / skewOf (stage));
}
inline double normFromTime (int stage, double t, bool beats) noexcept
{
    if (t <= 0.0) return 0.0;
    return std::clamp (std::pow (std::min (t, maxTime (stage, beats)) / maxTime (stage, beats), skewOf (stage)), 0.0, 1.0);
}
inline double timeMsFromNorm (int stage, double norm) noexcept { return timeFromNorm (stage, norm, false); }
inline double normFromTimeMs (int stage, double ms) noexcept { return normFromTime (stage, ms, false); }
/** The time of a stage 0 .. 4 (delay, attack, hold, decay, release) in the unit of the axis: ms, or beats. */
inline double stageTime (const Shape& s, int stage, bool beats) noexcept
{
    if (stage < 0 || stage >= 5) return 0.0;
    return timeFromNorm (stage, s.t[stage], beats);
}
inline double stageTimeMs (const Shape& s, int stage) noexcept { return stageTime (s, stage, false); }

/** The curve function of the engine (own::powerShape): 0 = straight, negative = fast start, positive = slow start. */
inline double powerShape (double x, double power) noexcept
{
    if (std::fabs (power) < 1.0e-4)
        return x;
    return std::expm1 (power * x) / std::expm1 (power);
}
/** The level (0 .. 1) at the fraction f (0 .. 1) of stage 0 .. 5 (delay, attack, hold, decay, sustain, release), the way the engine's envelope runs. */
inline double levelAt (const Shape& s, int stage, double f) noexcept
{
    f = std::clamp (f, 0.0, 1.0);
    switch (stage)
    {
        case 0: return 0.0;
        case 1: return powerShape (f, -s.curve[0]);
        case 2: return 1.0;
        case 3: return 1.0 - (1.0 - s.sustain) * powerShape (f, s.curve[1]);
        case 4: return s.sustain;
        default: return s.sustain * (1.0 - powerShape (f, s.curve[2]));
    }
}
/** The curve parameter that puts the middle of a stage at the level `level` of its range (the closed form: powerShape (0.5, p) = 1 / (1 + e^(p / 2))); `q` = how far the middle is from the start of the stage, 0 .. 1. */
inline double curveForMiddle (double q) noexcept
{
    q = std::clamp (q, 1.0e-5, 1.0 - 1.0e-5);
    return std::clamp (2.0 * std::log ((1.0 - q) / q), -20.0, 20.0);
}
}

class EnvGraph : public Component
{
public:
    using Shape = envgraph::Shape;
    static constexpr int kMaxDots = 8, kHit = 56;
    struct Dot { int stage = 0; float fraction = 0.0f; };   // the engine's fine stage 1 delay, 2 attack, 3 hold, 4 decay, 5 sustain, 6 release; 0 = no dot
    struct State
    {
        Shape shape, defaults;
        bool ghost = false;
        Shape ghostShape;
        int numDots = 0;
        Dot dots[kMaxDots];                                  // newest voice first
        char text[envgraph::kNumHandles][16] = {};           // the value of every handle as text (with the unit); the Decay handle's text is the time and the sustain level (its bubble)
        char decayTime[16] = {}, sustainText[16] = {};       // the labels under the axis: the decay TIME alone under the decay segment, the sustain LEVEL under the sustain segment (empty: the Decay handle's text / no label)
        uint32_t colour = 0;                                 // 0xRRGGBB of the curve; 0 = the envelope colours of the look
        int selected = envgraph::None;                       // the handle whose value is written on the graph (a tap selects, the drag selects)
        bool beats = false;                                  // the units of the times: ms or beats (the ruler's labels, the axis unit)
        bool locked = true;                                  // LOCKED: the envelope fills the plot, the ruler changes; UNLOCKED: the zoom below
        double zoom = 0.0;                                   // unlocked: the time (ms or beats) the plot's width stands for; 0 = what the envelope needs now (taken when it is unlocked)
    };
    State state;
    /** What the last Change was. `handle` = the handle; t = the new normalised time (Delay .. Release, Decay), level = the new sustain level (Decay), curve = the new curve parameter (CurveA .. CurveR). */
    struct Edit { int handle = envgraph::None; double t = 0.0, level = 0.0, curve = 0.0; bool reset = false; };   // handle kLock / kZoom: the lock was toggled / the zoom was dragged (view state, no parameter)
    static constexpr int kLock = 100, kZoom = 101;

    EnvGraph() = default;
    Size minSize() const override { return { 300, 160 }; }
    /** The drawing area of the curve inside the rectangle (the margins hold the value text and the handles at the edges). */
    Rect plot() const noexcept
    {
        const bool c = compactChrome();
        return { rect_.x + (c ? 18 : 24), rect_.y + (c ? 30 : 38), std::max (0, rect_.w - (c ? 36 : 48)), std::max (0, rect_.h - (c ? 30 : 38) - (c ? 22 : 40)) };
    }
    /** The LOCK button (the full chrome only: the compact graph is always locked) and the zoom band under the plot (the ruler's row: a horizontal drag there zooms while unlocked). */
    Rect lockRect() const noexcept { return compactChrome() ? Rect {} : Rect { rect_.x + 6, rect_.y + 2, 76, 34 }; }
    Rect zoomRect() const noexcept { const Rect p = plot(); return compactChrome() || state.locked ? Rect {} : Rect { p.x, p.bottom() + 2, p.w, std::max (0, rect_.bottom() - p.bottom() - 2) }; }
    const Edit& edit() const noexcept { return edit_; }
    bool dragging() const noexcept { return drag_ != envgraph::None; }
    int draggedHandle() const noexcept { return drag_; }

    /** The geometry the drawing and the touch use: x[0] = the left end of the curve, x[1 .. 6] = the ends of the six stages (delay, attack, hold, decay, sustain, release), the rows of level 1 and 0; cu[k] = the
        time (units of the axis) at x[k] (the sustain has none); pxPerUnit = the plain scale (width of the window / its time; the stages that are narrower than kMinStageW are widened); window = the time the plot stands for. */
    struct Geom
    {
        double x[7];
        double cu[7];
        double window = 1000.0;
        double pxPerUnit = 1.0;
        double sustainGap = 16.0;
        double yTop = 0.0, yBot = 0.0;
        bool locked = true;
    };
    Geom geom() const noexcept { return geomFor (state.shape); }
    static double levelRow (const Geom& g, double level) noexcept { return g.yBot - (g.yBot - g.yTop) * std::clamp (level, 0.0, 1.0); }
    /** The x of time t (units) on the axis: inside a stage by interpolation, a time after the decay is in the release (the sustain gap is between). */
    static double xAtTime (const Geom& g, double t) noexcept
    {
        if (t <= 0.0) return g.x[0];
        static const int kFrom[5] = { 0, 1, 2, 3, 5 };
        for (int k = 0; k < 5; ++k)
        {
            const int a = kFrom[k], b = k == 4 ? 6 : k + 1;
            if (t <= g.cu[b] + 1.0e-9 || k == 4)
            {
                const double span = g.cu[b] - g.cu[a];
                return span > 1.0e-12 ? g.x[a] + (g.x[b] - g.x[a]) * std::clamp ((t - g.cu[a]) / span, 0.0, 1.0) : g.x[b];
            }
        }
        return g.x[6];
    }
    /** The centre of a handle (canvas pixels). */
    void handlePoint (int h, double& px, double& py) const noexcept { handlePointOf (geom(), state.shape, h, px, py); }
    /** Is the handle offered (drawn, touchable)? The curve handle of a stage needs room beside the end handles; a handle past the right edge of the plot (unlocked, zoomed in) is not offered. */
    bool handleOffered (const Geom& g, int h) const noexcept
    {
        const Rect pl = plot();
        if (h == envgraph::CurveA && g.x[2] - g.x[1] < kCurveRoom) return false;
        if (h == envgraph::CurveD && g.x[4] - g.x[3] < kCurveRoom) return false;
        if (h == envgraph::CurveR && g.x[6] - g.x[5] < kCurveRoom) return false;
        double px, py;
        handlePointOf (g, state.shape, h, px, py);
        return px <= pl.right() + 1.0;
    }
    static constexpr double kCurveRoom = 30.0;   // pixels a stage needs for its curve handle to lie clear of both end handles
    /** The handle a touch at (x, y) would take: the nearest one inside its square target (the curve handles only when no end handle is nearer); None when there is none. */
    int handleAt (int x, int y) const noexcept
    {
        const Geom g = geom();
        int best = envgraph::None;
        double bestD = 1.0e9;
        for (int h = 0; h < envgraph::kNumHandles; ++h)
        {
            if (! handleOffered (g, h)) continue;

            double px, py;
            handlePointOf (g, state.shape, h, px, py);
            if (std::fabs (px - x) > kHit * 0.5 || std::fabs (py - y) > kHit * 0.5)
                continue;
            double d = std::hypot (px - x, py - y) + (h >= envgraph::CurveA ? 3.0 : 0.0);
            if (g.x[3] <= g.x[2] + 1.0e-3)
            {
                // attack and a zero-length hold end at the same point: a touch on the joint or up to 3 pixels right of it takes the attack, further right the hold (drag right = give it time)
                if (h == envgraph::Hold && x > g.x[2] + 3.0) d -= 2.0;
                if (h == envgraph::Attack && x <= g.x[2] + 3.0) d -= 2.0;
            }
            if (d < bestD)
            {
                bestD = d;
                best = h;
            }
        }
        return best;
    }
    int touchTargets (Rect* out, int max) const override
    {
        const Geom g = geom();
        int n = 0;
        for (int h = 0; h < envgraph::kNumHandles && n < max; ++h)
        {
            if (! handleOffered (g, h)) continue;
            double px, py;
            handlePointOf (g, state.shape, h, px, py);
            out[n++] = { static_cast<int> (std::lround (px)) - kHit / 2, static_cast<int> (std::lround (py)) - kHit / 2, kHit, kHit };
        }
        return n;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen pen (sb, enabled_);
        Cell cell (sb, rect_);
        paintStatic (sb, pen);
    }
    /** The running dots (not cached: they move). */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        const theme::Look& T = theme::L();
        if (state.numDots <= 0)
            return;
        const Geom g = geom();
        sb.style (1.0f, false);
        sb.paintOff();
        const uint32_t c1 = state.colour != 0 ? state.colour : T.env[1];
        const int n = std::min (state.numDots, theme::detailLevel() == 2 ? 3 : kMaxDots);
        for (int i = n - 1; i >= 0; --i)   // the newest voice is drawn last, on top
        {
            const Dot& d = state.dots[i];
            if (d.stage < 1 || d.stage > 6)
                continue;
            const int st = d.stage - 1;
            const double f = st == 4 ? 0.5 : std::clamp (static_cast<double> (d.fraction), 0.0, 1.0);
            const double px = std::min (g.x[st] + (g.x[st + 1] - g.x[st]) * f, static_cast<double> (plot().right())), py = levelRow (g, envgraph::levelAt (state.shape, st, f));
            if (i == 0)
            {
                sb.disc (static_cast<float> (px), static_cast<float> (py), 4.6f, theme::rgba (c1, 255));
                sb.disc (static_cast<float> (px), static_cast<float> (py), 2.2f, 0xffffffffu);
            }
            else
                sb.disc (static_cast<float> (px), static_cast<float> (py), 3.0f, theme::rgba (c1, 170));
        }
    }

protected:
    void drawBody (Pen&) const override {}
    bool compactChrome() const noexcept { return rect_.w < 520 || rect_.h < 260; }

    /** Widths of the five time stages for a plot of `availW` pixels: a stage of 0 has none; the others share the width in proportion to their time, none below kMinStageW (the scale is the one that makes the sum
        exactly `availW`: found by bisection, the sum is steadily growing with the scale). With `px` > 0 the scale is given (the unlocked state, the ghost). Returns the scale. */
    static double solveWidths (const double* u, double availW, double px, double* w) noexcept
    {
        int n = 0;
        double total = 0.0;
        for (int i = 0; i < 5; ++i)
            if (u[i] > 0.0) { ++n; total += u[i]; }
        if (px <= 0.0)
        {
            if (n == 0) { for (int i = 0; i < 5; ++i) w[i] = 0.0; return 0.0; }
            if (n * envgraph::kMinStageW >= availW)
            {
                for (int i = 0; i < 5; ++i) w[i] = u[i] > 0.0 ? availW / n : 0.0;
                return 0.0;
            }
            double lo = 0.0, hi = availW / std::min (total, 1.0e12) * 1.0;   // the scale at which the plain widths already fill it
            double minU = 1.0e300;
            for (int i = 0; i < 5; ++i) if (u[i] > 0.0) minU = std::min (minU, u[i]);
            hi = std::max (hi, availW / minU);
            for (int it = 0; it < 60; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                double sum = 0.0;
                for (int i = 0; i < 5; ++i) if (u[i] > 0.0) sum += std::max (envgraph::kMinStageW, u[i] * mid);
                (sum < availW ? lo : hi) = mid;
            }
            px = hi;
        }
        for (int i = 0; i < 5; ++i)
            w[i] = u[i] > 0.0 ? std::max (envgraph::kMinStageW, u[i] * px) : 0.0;
        return px;
    }
    /** `fixedPx` > 0: that scale instead of the fit (the ghost envelope on the axis of the one in front). */
    Geom geomFor (const Shape& s, double fixedPx = 0.0) const noexcept
    {
        const Rect p = plot();
        Geom g {};
        g.yTop = static_cast<double> (p.y);
        g.yBot = static_cast<double> (p.bottom());
        g.x[0] = static_cast<double> (p.x);
        g.locked = state.locked;
        g.sustainGap = compactChrome() ? 12.0 : 16.0;
        const double availW = std::max (20.0, static_cast<double> (p.w) - g.sustainGap);
        double u[5], total = 0.0;
        for (int i = 0; i < 5; ++i)
        {
            u[i] = envgraph::stageTime (s, i, state.beats);
            total += u[i];
        }
        const double rest = state.beats ? 4.0 : 1000.0;   // the time an envelope of all zeros stands for (a handle dragged out of the origin has a scale to start with)
        double px = fixedPx;
        if (px <= 0.0 && ! state.locked)
        {
            const double win = state.zoom > 0.0 ? state.zoom : std::max (total * 1.25, rest * 0.1);
            px = availW / win;
        }
        double w[5];
        double used = solveWidths (u, availW, px, w);
        if (state.locked && fixedPx <= 0.0 && total <= 0.0)
            used = availW / rest;
        g.pxPerUnit = used > 0.0 ? used : (total > 0.0 ? availW / total : availW / rest);
        g.window = state.locked && fixedPx <= 0.0 ? (total > 0.0 ? total : rest) : availW / g.pxPerUnit;
        g.x[1] = g.x[0] + w[0];
        g.x[2] = g.x[1] + w[1];
        g.x[3] = g.x[2] + w[2];
        g.x[4] = g.x[3] + w[3];
        g.x[5] = g.x[4] + g.sustainGap;
        g.x[6] = g.x[5] + w[4];
        g.cu[0] = 0.0;
        g.cu[1] = u[0]; g.cu[2] = g.cu[1] + u[1]; g.cu[3] = g.cu[2] + u[2]; g.cu[4] = g.cu[3] + u[3];
        g.cu[5] = g.cu[4]; g.cu[6] = g.cu[4] + u[4];
        return g;
    }
    static void handlePointOf (const Geom& g, const Shape& s, int h, double& px, double& py) noexcept
    {
        switch (h)
        {
            case envgraph::Delay: px = g.x[1]; py = levelRow (g, 0.0); break;
            case envgraph::Attack: px = g.x[2]; py = levelRow (g, 1.0); break;
            case envgraph::Hold: px = g.x[3]; py = levelRow (g, 1.0); break;
            case envgraph::Decay: px = g.x[4]; py = levelRow (g, s.sustain); break;
            case envgraph::Release: px = g.x[6]; py = levelRow (g, 0.0); break;
            case envgraph::CurveA: px = 0.5 * (g.x[1] + g.x[2]); py = levelRow (g, envgraph::levelAt (s, 1, 0.5)); break;
            case envgraph::CurveD: px = 0.5 * (g.x[3] + g.x[4]); py = levelRow (g, envgraph::levelAt (s, 3, 0.5)); break;
            default: px = 0.5 * (g.x[5] + g.x[6]); py = levelRow (g, envgraph::levelAt (s, 5, 0.5)); break;
        }
    }

    // ------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the drawing
    void curveRows (const Shape& s, const Geom& g, float* rows, int n) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double x = g.x[0] + static_cast<double> (i) + 0.5;
            int st = 0;
            while (st < 5 && x >= g.x[st + 1])
                ++st;
            double f = 0.0;
            if (g.x[st + 1] > g.x[st])
                f = (x - g.x[st]) / (g.x[st + 1] - g.x[st]);
            double lvl = 0.0;
            if (x <= g.x[6])
                lvl = envgraph::levelAt (s, st, f);
            rows[i] = static_cast<float> (levelRow (g, lvl));
        }
    }
    void paintStatic (gpu::SceneBuilder& sb, Pen& p) const
    {
        const theme::Look& T = theme::L();
        const bool reduced = theme::detailLevel() == 2, comp = compactChrome();
        p.block (rect_);
        const Rect pl = plot();
        if (pl.w < 40 || pl.h < 30)
            return;
        const Geom g = geom();
        const uint32_t c0 = state.colour != 0 ? state.colour : T.env[0], c1 = state.colour != 0 ? theme::mix (state.colour, T.env[1], 0.5f) : T.env[1];
        const uint32_t cs[4] = { c0, theme::mix (c0, c1, 0.33f), theme::mix (c0, c1, 0.66f), c1 };
        const float px0 = static_cast<float> (pl.x), px1 = static_cast<float> (pl.right());

        // ---- the horizontal grid: levels 0, 50, 100 %
        for (int k = 0; k < 3; ++k)
        {
            const float y = static_cast<float> (levelRow (g, 0.5 * k));
            sb.line (px0, y, px1, y, T.hair, theme::rgba (T.grid, k == 0 ? 255 : 150));
        }

        // ---- the time ruler under the plot (round ms / s, or beats): its marks are also the vertical grid
        if (! reduced || ! comp)
        {
            const double unitScale = state.beats ? 1000.0 : 1.0;   // time_ruler.h counts in ms; beats are labelled as such below
            const double availW = g.window * g.pxPerUnit;
            const TimeRuler ruler = makeTimeRuler (g.window * unitScale, 0.0f, static_cast<float> (std::max (1.0, availW)), comp ? 56.0f : 84.0f);
            for (int i = 0; i < ruler.count; ++i)
            {
                const double t = ruler.ticks[i].ms / unitScale;
                const float gx = static_cast<float> (xAtTime (g, t));
                if (gx < px0 - 0.5f || gx > px1 + 0.5f)
                    continue;
                char lbl[24];
                if (state.beats)
                    std::snprintf (lbl, sizeof lbl, "%g b", ruler.ticks[i].ms / 1000.0);
                else
                    std::snprintf (lbl, sizeof lbl, "%s", ruler.ticks[i].label);
                if (t > 0.0 && ! reduced)
                    sb.line (gx, static_cast<float> (g.yTop), gx, static_cast<float> (g.yBot), T.hair, theme::rgba (T.grid, 110));
                if (i == 0 && ruler.count > 1)
                    std::snprintf (lbl, sizeof lbl, "0");
                const int lw = comp ? 44 : 56;
                p.textCentered ({ static_cast<int> (std::lround (gx)) - lw / 2, static_cast<int> (std::lround (g.yBot)) + 3, lw, comp ? 12 : 14 }, lbl, T.fSmall - 1.0f, p.c (T.textFaint, 200), 8.0f);
            }
        }
        if (! comp)
        {
            // the LOCK button and, unlocked, the zoom band's hint
            const Rect lk = lockRect();
            const bool lockedNow = state.locked;
            const uint32_t lc = cs[1];
            p.rrect (lk.inset (2), T.rChip, p.c (T.bg1, 255));
            p.rrect (lk.inset (2), T.rChip, p.c (lockedNow ? lc : T.keyline, 255), 1.4f);
            p.textCentered (lk.inset (2), lockedNow ? "LOCKED" : "FREE", T.fSmall, p.c (lockedNow ? T.text : T.textDim, 255), T.fMin);
            if (! lockedNow)
            {
                const Rect zr = zoomRect();
                p.textRight ({ zr.x, zr.y + zr.h - 16, zr.w, 14 }, "DRAG ALONG THE RULER TO ZOOM", T.fSmall - 1.0f, p.c (T.textFaint, 160));
            }
        }

        // ---- the ghost: another envelope behind this one (a thin faint line)
        constexpr int kMaxN = gpu::kMaxCurve;
        float rows[kMaxN], rowsGhost[kMaxN];
        const int n = std::min (pl.w, kMaxN);
        if (state.ghost && ! reduced)
        {
            curveRows (state.ghostShape, geomFor (state.ghostShape, g.pxPerUnit), rowsGhost, n);   // on the axis of the envelope in front: the same scale, the same time
            sb.curve (rowsGhost, n, pl.x, n);
            sb.paintOff();
            sb.style (1.0f, false);
            sb.paintX (px0, px1, theme::rgba (cs[0], 255), theme::rgba (cs[1], 255), theme::rgba (cs[2], 255), theme::rgba (cs[3], 255));
            sb.stroke (T.curveBack * 0.5f, 70);
            sb.paintOff();
        }

        // ---- the curve: a faint area under it, then one crisp line
        curveRows (state.shape, g, rows, n);
        const int first = sb.curve (rows, n, pl.x, n);
        if (first < 0)
            return;
        if (! reduced)
            sb.fillGradient (static_cast<float> (g.yBot), static_cast<float> (g.yTop), static_cast<float> (g.yBot), theme::rgba (c0, 56), theme::rgba (c0, 0));
        sb.style (1.0f, true);
        sb.paintX (px0, px1, theme::rgba (cs[0], 255), theme::rgba (cs[1], 255), theme::rgba (cs[2], 255), theme::rgba (cs[3], 255));
        const float half = T.curve * 0.5f + 0.15f;
        const int curveEndPx = std::clamp (static_cast<int> (std::lround (g.x[6] - g.x[0])) + 1, 2, n);
        sb.curveAt (first, curveEndPx, pl.x, curveEndPx);
        sb.stroke (half, 255u);
        sb.paintOff();
        sb.style (1.0f, false);

        // ---- stage names only as a legend that does not pretend to be the axis
        if (! comp && ! reduced)
        {
            static const char* const kLegend = "DELAY   ATTACK   HOLD   DECAY   SUSTAIN   RELEASE";
            p.textRight ({ pl.x, rect_.y + 8, pl.w, 16 }, kLegend, T.fSmall - 1.0f, p.c (T.textFaint, 180));
        }

        // ---- the handles
        for (int h = 0; h < envgraph::kNumHandles; ++h)
        {
            const bool curveH = h >= envgraph::CurveA;
            if (! handleOffered (g, h))
                continue;
            double hx, hy;
            handlePointOf (g, state.shape, h, hx, hy);
            const bool sel = h == state.selected || h == drag_;
            const bool dim = h <= envgraph::Release && h != envgraph::Decay && isZero (h < envgraph::Decay ? h : 4);
            static const int kColourOf[envgraph::kNumHandles] = { 0, 1, 1, 2, 3, 1, 2, 3 };
            const uint32_t col = cs[kColourOf[h]];
            const float r = curveH ? 4.0f : 6.5f;
            p.style (1.0f, true);
            if (sel && ! curveH)
                p.disc (static_cast<float> (hx), static_cast<float> (hy), r + 1.5f, p.c (col, 255));
            else
            {
                p.disc (static_cast<float> (hx), static_cast<float> (hy), r, p.c (T.bg1, 255));
                p.arc (static_cast<float> (hx), static_cast<float> (hy), r - 0.8f, 0.0f, 2.0f * kPi, curveH ? 1.4f : 1.8f, p.c (col, dim ? 110 : 255));
            }
            if (sel && curveH)
                p.disc (static_cast<float> (hx), static_cast<float> (hy), 2.4f, p.c (col, 255));
            p.plain();
        }

        // ---- the value of the selected / dragged handle over it
        const int sh = drag_ != envgraph::None ? drag_ : state.selected;
        if (sh != envgraph::None && state.text[sh][0] != '\0')
        {
            double hx, hy;
            handlePointOf (g, state.shape, sh, hx, hy);
            const float size = T.fValue;
            const int tw = static_cast<int> (std::ceil (Pen::textWidth (state.text[sh], size))) + 16, th = 26;
            int bx = static_cast<int> (std::lround (hx)) - tw / 2, by = static_cast<int> (std::lround (hy)) - th - 12;
            if (by < rect_.y + 2)
                by = static_cast<int>(std::lround (hy)) + 14;
            bx = std::clamp (bx, rect_.x + 2, std::max (rect_.x + 2, rect_.right() - tw - 2));
            const Rect bubble { bx, by, tw, th };
            p.rrect (bubble, T.rChip, p.c (T.bg1, 235));
            p.rrect (bubble, T.rChip, p.c (T.keylineHi, 255), 1.2f);
            p.textCentered (bubble, state.text[sh], size, p.c (T.text, 255), T.fMin);
        }
    }
    bool isZero (int stage) const noexcept   // stage 0 .. 5: a time stage whose time is 0
    {
        if (stage == 4) return false;
        const int s = stage < 4 ? stage : 4;
        return envgraph::stageTimeMs (state.shape, s) <= 0.0;
    }

    // ------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the touch
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        zone_ = Zone::None;
        downHandle_ = handleAt (x, y);
        drag_ = envgraph::None;
        start_ = state.shape;
        startGeom_ = geomFor (start_);
        if (downHandle_ == envgraph::None)
        {
            if (lockRect().contains (x, y))
                zone_ = Zone::Lock;
            else if (zoomRect().contains (x, y))
            {
                zone_ = Zone::Zoom;
                zoomStart_ = state.zoom > 0.0 ? state.zoom : startGeom_.window;
                zoomAtX_ = x;
            }
        }
        if (downHandle_ != envgraph::None)
        {
            double hy;
            handlePointOf (startGeom_, start_, downHandle_, downX_, hy);
        }
        if (downHandle_ != envgraph::None)
            state.selected = downHandle_;
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        if (zone_ == Zone::Zoom)
        {
            if (! tap_.move (x, y) && ! zoomDragging_)
                return Change::none();
            zoomDragging_ = true;
            // a drag to the right zooms in (the same time takes more room), to the left out: one e-fold per 160 pixels
            const double lo = state.beats ? 0.05 : 20.0, hi = state.beats ? 400.0 : 120000.0;
            const double z = std::clamp (zoomStart_ * std::exp (-static_cast<double> (x - zoomAtX_) / 160.0), lo, hi);
            if (std::fabs (z - state.zoom) < 1.0e-9 * z)
                return Change::none();
            state.zoom = z;
            edit_ = Edit {};
            edit_.handle = kZoom;
            edit_.level = z;
            return Change::request (id_);
        }
        if (zone_ == Zone::Lock)
        {
            tap_.move (x, y);
            return Change::none();
        }
        if (downHandle_ == envgraph::None || ! tap_.move (x, y))
            return Change::none();
        drag_ = downHandle_;
        const Geom& g0 = startGeom_;
        Edit e;
        e.handle = drag_;
        const double dyPx = static_cast<double> (y - tap_.startY());
        const double dxPx = static_cast<double> (x - tap_.startX());
        if (drag_ <= envgraph::Release)
        {
            const int stage = drag_ == envgraph::Release ? 4 : drag_;
            const double t0 = envgraph::stageTime (start_, stage, state.beats);
            double newT = std::max (0.0, t0 + dxPx / std::max (1.0e-9, g0.pxPerUnit));   // the distance of the finger at the scale the drag began with
            if (state.locked && drag_ != envgraph::Release)
            {
                // LOCKED: the picture refits at every step, so the time that puts the handle's end under the finger is looked for on the refitted picture (bisection: the end of the stage only moves to the right
                // when the time grows). When the stage cannot move its own end (it is all of the picture) the plain distance above stays.
                const double target = downX_ + dxPx, hi = envgraph::maxTime (stage, state.beats);
                const auto endX = [&] (double t)
                {
                    Shape sh = start_;
                    sh.t[stage] = envgraph::normFromTime (stage, t, state.beats);
                    return geomFor (sh).x[stage + 1];
                };
                const double x0 = endX (0.0), x1 = endX (hi);
                if (x1 - x0 > 2.0)
                {
                    if (target <= x0) newT = 0.0;
                    else if (target >= x1) newT = hi;
                    else
                    {
                        double lo = 0.0, up = hi;
                        for (int it = 0; it < 40; ++it)
                        {
                            const double mid = 0.5 * (lo + up);
                            (endX (mid) < target ? lo : up) = mid;
                        }
                        newT = 0.5 * (lo + up);
                    }
                }
            }
            e.t = envgraph::normFromTime (stage, newT, state.beats);
            if (drag_ == envgraph::Decay)
                e.level = std::clamp (start_.sustain - dyPx / std::max (1.0, g0.yBot - g0.yTop), 0.0, 1.0);
        }
        else
        {
            double hx, hy;
            handlePointOf (g0, start_, drag_, hx, hy);
            const double lvl = 1.0 - (std::clamp (hy + dyPx, g0.yTop, g0.yBot) - g0.yTop) / std::max (1.0, g0.yBot - g0.yTop);
            const double s = start_.sustain;
            const double range = drag_ == envgraph::CurveA ? 1.0 : (drag_ == envgraph::CurveD ? 1.0 - s : s);
            if (range < 0.08)
                e.curve = std::clamp (start_.curve[drag_ - envgraph::CurveA] - dyPx * 20.0 / std::max (1.0, 0.5 * (g0.yBot - g0.yTop)), -20.0, 20.0);
            else if (drag_ == envgraph::CurveA)
                e.curve = -envgraph::curveForMiddle (lvl);
            else if (drag_ == envgraph::CurveD)
                e.curve = envgraph::curveForMiddle ((1.0 - lvl) / range);
            else
                e.curve = envgraph::curveForMiddle (1.0 - lvl / range);
        }
        if (! changed (e))
            return Change::none();
        edit_ = e;
        return Change::normalised (id_, drag_ <= envgraph::Release ? e.t : e.curve);
    }
    Change onUp (int, int) override
    {
        const int h = downHandle_, kind = tap_.up();
        const Zone zone = zone_;
        zone_ = Zone::None;
        downHandle_ = envgraph::None;
        const bool wasDrag = drag_ != envgraph::None, wasZoom = zoomDragging_;
        zoomDragging_ = false;
        drag_ = envgraph::None;
        if (zone == Zone::Lock && kind != 0)
        {
            // a tap on LOCK: the next state; unlocked starts from the zoom the envelope has now, so the picture does not jump
            if (state.locked)
                state.zoom = startGeom_.window;
            state.locked = ! state.locked;
            edit_ = Edit {};
            edit_.handle = kLock;
            edit_.level = state.locked ? 1.0 : 0.0;
            return Change::request (id_);
        }
        if (zone == Zone::Zoom || wasZoom)
            return Change::none();
        if (wasDrag || h == envgraph::None || kind != 2)
            return Change::none();
        Edit e;
        e.handle = h;
        e.reset = true;
        if (h <= envgraph::Release)
        {
            e.t = state.defaults.t[h == envgraph::Release ? 4 : h];
            e.level = h == envgraph::Decay ? state.defaults.sustain : state.shape.sustain;
        }
        else
            e.curve = state.defaults.curve[h - envgraph::CurveA];
        edit_ = e;
        return Change::normalised (id_, h <= envgraph::Release ? e.t : e.curve);
    }

private:
    bool changed (const Edit& e) const noexcept
    {
        if (e.handle <= envgraph::Release)
        {
            const int ti = e.handle == envgraph::Release ? 4 : e.handle;
            if (std::fabs (e.t - state.shape.t[ti]) > 1.0e-6)
                return true;
            return e.handle == envgraph::Decay && std::fabs (e.level - state.shape.sustain) > 1.0e-6;
        }
        return std::fabs (e.curve - state.shape.curve[e.handle - envgraph::CurveA]) > 1.0e-4;
    }
    enum class Zone { None, Lock, Zoom };
    TapGesture tap_;
    Zone zone_ = Zone::None;
    Geom startGeom_ {};
    double zoomStart_ = 0.0;
    int zoomAtX_ = 0;
    bool zoomDragging_ = false;
    Shape start_;
    Edit edit_;
    int downHandle_ = envgraph::None, drag_ = envgraph::None;
    double downX_ = 0.0;
};

}} // namespace synthyboy_mpc::ui
