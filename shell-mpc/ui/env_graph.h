// SynthyBoy - MPC UI component library: the ENVELOPE GRAPH (EnvGraph). The picture of a DAHDSR envelope (delay, attack, hold, decay, sustain, release and the three curves) with the handles that
// edit it by touch and the running dots of the sounding voices. Same contract as every component (core.h): a plain state struct and a rectangle in, a drawing and a Change out; it knows no engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The axis. The whole envelope always fills the plot ("zoom locked": the six handles are always on the screen). The width of a time stage is the NORMALISED value of its parameter (0 .. 1, the position of the
// knob: the same law as the knobs have, a skewed time) plus a fixed minimum, so a stage of 0 ms keeps its own slot (dim) and two neighbouring handles are never on top of each other; the sustain segment
// has a fixed width. It is a schematic of the shape, not a ruler: the numbers are on the handles. A dragged time handle follows the finger exactly (the picture fits the plot again at every step: the rest of the
// curve gives way, the closed form is in onMove), up to the longest time the parameter has.
// Handles (touch targets of at least 56 x 56): the ends of delay, attack, hold, decay (two dimensions: its time and the sustain level at once) and release, and one small curve handle on the middle of
// attack, decay and release (a vertical drag bends the stage: the handle follows the finger).
// In:  State { shape (normalised times, sustain level 0 .. 1, curves -20 .. 20), defaults (a double tap on a handle), the ghost shape (envelope 1 behind another one), the dots of the voices (engine fine
//      stage 1 .. 6 and the fraction of the stage that has run), the value text of every handle (the owner formats it with the unit), the colour }.
// Out: a Value change on every move that changed something (id = the graph's id); `edit ()` says what: { handle, t, level, curve }. A double tap on a handle is the same with the default. The owner writes
//      the parameters and gives the new shape back with the next state.
#pragma once

#include "core.h"

namespace synthyboy_mpc { namespace ui {

namespace envgraph
{
enum Handle : int { None = -1, Delay = 0, Attack, Hold, Decay, Release, CurveA, CurveD, CurveR, kNumHandles };

/** The envelope as the graph needs it: t = the normalised time of delay, attack, hold, decay, release (0 .. 1), the sustain level (0 .. 1) and the curves of attack, decay, release (-20 .. 20: the parameters of the engine). */
struct Shape
{
    double t[5] = { 0.0, 0.1, 0.0, 0.38, 0.3 };
    double sustain = 1.0;
    double curve[3] = { 0.0, -3.0, -3.0 };
};

constexpr double kMinStage = 1.0, kSustainWidth = 1.4;   // natural widths: a stage is kMinStage + its normalised time wide, the sustain segment kSustainWidth

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
/** The natural widths of the six stages (delay, attack, hold, decay, sustain, release). */
inline void naturalWidths (const Shape& s, double w[6]) noexcept
{
    w[0] = kMinStage + s.t[0]; w[1] = kMinStage + s.t[1]; w[2] = kMinStage + s.t[2]; w[3] = kMinStage + s.t[3]; w[4] = kSustainWidth; w[5] = kMinStage + s.t[4];
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
        char text[envgraph::kNumHandles][16] = {};           // the value of every handle as text (with the unit)
        uint32_t colour = 0;                                 // 0xRRGGBB of the curve; 0 = the envelope colours of the look
        int selected = envgraph::None;                       // the handle whose value is written on the graph (a tap selects, the drag selects)
        bool beats = false;                                  // the units of the times: ms or beats (the caption of the axis)
    };
    State state;
    /** What the last Change was. `handle` = the handle; t = the new normalised time (Delay .. Release, Decay), level = the new sustain level (Decay), curve = the new curve parameter (CurveA .. CurveR). */
    struct Edit { int handle = envgraph::None; double t = 0.0, level = 0.0, curve = 0.0; bool reset = false; };

    EnvGraph() = default;
    Size minSize() const override { return { 300, 160 }; }
    /** The drawing area of the curve inside the rectangle (the margins hold the value text and the handles at the edges). */
    Rect plot() const noexcept
    {
        const bool c = compactChrome();
        return { rect_.x + (c ? 18 : 24), rect_.y + (c ? 30 : 38), std::max (0, rect_.w - (c ? 36 : 48)), std::max (0, rect_.h - (c ? 30 : 38) - (c ? 22 : 30)) };
    }
    const Edit& edit() const noexcept { return edit_; }
    bool dragging() const noexcept { return drag_ != envgraph::None; }
    int draggedHandle() const noexcept { return drag_; }

    /** The geometry the drawing and the touch use: x[0] = the left end of the curve, x[1 .. 6] = the ends of the six stages (delay, attack, hold, decay, sustain, release), the rows of level 1 and 0. */
    struct Geom { double x[7]; double scale, yTop, yBot; };
    Geom geom() const noexcept { return geomFor (state.shape); }
    static double levelRow (const Geom& g, double level) noexcept { return g.yBot - (g.yBot - g.yTop) * std::clamp (level, 0.0, 1.0); }
    /** The centre of a handle (canvas pixels). */
    void handlePoint (int h, double& px, double& py) const noexcept { handlePointOf (geom(), state.shape, h, px, py); }
    /** The handle a touch at (x, y) would take: the nearest one inside its square target (the curve handles only when no end handle is nearer); None when there is none. */
    int handleAt (int x, int y) const noexcept
    {
        const Geom g = geom();
        int best = envgraph::None;
        double bestD = 1.0e9;
        for (int h = 0; h < envgraph::kNumHandles; ++h)
        {
            double px, py;
            handlePointOf (g, state.shape, h, px, py);
            if (std::fabs (px - x) > kHit * 0.5 || std::fabs (py - y) > kHit * 0.5)
                continue;
            const double d = std::hypot (px - x, py - y) + (h >= envgraph::CurveA ? 3.0 : 0.0);   // the end of a stage wins a tie with the curve handle on it
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
            const double px = g.x[st] + (g.x[st + 1] - g.x[st]) * f, py = levelRow (g, envgraph::levelAt (state.shape, st, f));
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

    Geom geomFor (const Shape& s) const noexcept
    {
        const Rect p = plot();
        double w[6], sum = 0.0;
        envgraph::naturalWidths (s, w);
        for (double v : w)
            sum += v;
        Geom g {};
        g.scale = static_cast<double> (p.w) / sum;
        g.yTop = static_cast<double> (p.y);
        g.yBot = static_cast<double> (p.bottom());
        g.x[0] = static_cast<double> (p.x);
        for (int i = 0; i < 6; ++i)
            g.x[i + 1] = g.x[i] + w[i] * g.scale;
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
            const double f = g.x[st + 1] > g.x[st] ? (x - g.x[st]) / (g.x[st + 1] - g.x[st]) : 0.0;
            rows[i] = static_cast<float> (levelRow (g, envgraph::levelAt (s, st, f)));
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
        // ---- the grid: the levels 0, 50 and 100 %, the borders of the stages as hairlines
        for (int k = 0; k < 3; ++k)
        {
            const float y = static_cast<float> (levelRow (g, 0.5 * k));
            sb.line (px0, y, px1, y, T.hair, theme::rgba (T.grid, k == 0 ? 255 : 150));
        }
        if (! reduced)
            for (int i = 1; i < 6; ++i)
                sb.line (static_cast<float> (g.x[i]), static_cast<float> (g.yTop), static_cast<float> (g.x[i]), static_cast<float> (g.yBot), T.hair, theme::rgba (T.grid, i == 4 ? 220 : 110));
        // ---- the ghost: another envelope behind this one (a thin faint line)
        constexpr int kMaxN = 1024;
        float rows[kMaxN], rowsGhost[kMaxN];
        const int n = std::min (pl.w, kMaxN);
        if (state.ghost && ! reduced)
        {
            curveRows (state.ghostShape, geomFor (state.ghostShape), rowsGhost, n);
            sb.curve (rowsGhost, n, pl.x, n);
            sb.paintOff();
            sb.style (1.0f, false);
            sb.paintX (px0, px1, theme::rgba (cs[0], 255), theme::rgba (cs[1], 255), theme::rgba (cs[2], 255), theme::rgba (cs[3], 255));
            sb.stroke (T.curveBack * 0.5f, 70);
            sb.paintOff();
        }
        // ---- the curve: a faint area under it, then one crisp line per run of stages (a stage that is 0 is dim)
        curveRows (state.shape, g, rows, n);
        const int first = sb.curve (rows, n, pl.x, n);
        if (first < 0)
            return;
        if (! reduced)
            sb.fillGradient (static_cast<float> (g.yBot), static_cast<float> (g.yTop), static_cast<float> (g.yBot), theme::rgba (c0, 56), theme::rgba (c0, 0));
        sb.style (1.0f, true);
        sb.paintX (px0, px1, theme::rgba (cs[0], 255), theme::rgba (cs[1], 255), theme::rgba (cs[2], 255), theme::rgba (cs[3], 255));
        int run0 = 0;
        bool runDim = isZero (0);
        const float half = T.curve * 0.5f + 0.15f;
        for (int st = 1; st <= 6; ++st)
        {
            if (st < 6 && isZero (st) == runDim)
                continue;
            const int a = std::clamp (static_cast<int> (std::lround (g.x[run0] - g.x[0])) - 1, 0, n - 2), b = std::clamp (static_cast<int> (std::lround (g.x[st] - g.x[0])) + 1, a + 2, n);
            sb.curveAt (first + a, b - a, pl.x + a, b - a);
            sb.stroke (half, runDim ? 110u : 255u);
            run0 = st;
            if (st < 6)
                runDim = isZero (st);
        }
        sb.paintOff();
        sb.style (1.0f, false);
        // ---- the value of every time handle under the axis (full detail, the roomy graph), the name of the unit
        if (! comp && ! reduced)
        {
            static const char* const kAxis[5] = { "DELAY", "ATTACK", "HOLD", "DECAY", "RELEASE" };
            static const int kEndOf[5] = { 1, 2, 3, 4, 6 }, kStartOf[5] = { 0, 1, 2, 3, 5 };
            for (int i = 0; i < 5; ++i)
            {
                const Rect b { static_cast<int> (std::lround (g.x[kStartOf[i]])), pl.bottom() + 4, static_cast<int> (std::lround (g.x[kEndOf[i]] - g.x[kStartOf[i]])), 22 };
                if (b.w < 24)
                    continue;
                p.textCentered ({ b.x, b.y, b.w, 12 }, kAxis[i], T.fSmall - 1.0f, p.c (T.textFaint, 255), 8.0f);
                p.textCentered ({ b.x, b.y + 12, b.w, 14 }, state.text[i], T.fSmall + 1.0f, p.c (T.textDim, 255), 8.0f);
            }
        }
        // ---- the handles
        for (int h = 0; h < envgraph::kNumHandles; ++h)
        {
            double hx, hy;
            handlePointOf (g, state.shape, h, hx, hy);
            const bool sel = h == state.selected || h == drag_;
            const bool curveH = h >= envgraph::CurveA;
            const bool dim = h <= envgraph::Release && h != envgraph::Decay && state.shape.t[h < envgraph::Decay ? h : 4] <= 0.0;
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
        return stage == 4 ? false : state.shape.t[stage < 4 ? stage : 4] <= 0.0;
    }

    // ------------------------------------------------------------------------------------------------------------------------------------------------------------------
    // the touch
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        downHandle_ = handleAt (x, y);
        drag_ = envgraph::None;
        start_ = state.shape;
        if (downHandle_ != envgraph::None)
        {
            double hy;
            handlePointOf (geomFor (start_), start_, downHandle_, downX_, hy);
        }
        if (downHandle_ != envgraph::None)
            state.selected = downHandle_;
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        if (downHandle_ == envgraph::None || ! tap_.move (x, y))
            return Change::none();
        drag_ = downHandle_;
        const Geom g0 = geomFor (start_);
        Edit e;
        e.handle = drag_;
        const double dyPx = static_cast<double> (y - tap_.startY());
        if (drag_ <= envgraph::Release)
        {
            // the handle follows the finger: its x is the finger's. With a = the natural width of the stages before this one, b = of all the others and r = the finger's place along the plot (0 .. 1), the stage's own
            // natural width w is where (a + w) / (b + w) = r, that is w = (r b - a) / (1 - r) (the picture fits the plot at every step, so the scale moves with the width).
            double w[6], a = 0.0, b = 0.0;
            envgraph::naturalWidths (start_, w);
            const int stage = drag_ == envgraph::Release ? 5 : drag_;
            for (int i = 0; i < 6; ++i)
            {
                a += i < stage ? w[i] : 0.0;
                b += i != stage ? w[i] : 0.0;
            }
            const Rect pl = plot();
            const double r = (downX_ + static_cast<double> (x - tap_.startX()) - static_cast<double> (pl.x)) / std::max (1.0, static_cast<double> (pl.w));
            const double wNew = r >= 0.999 ? envgraph::kMinStage + 1.0 : (r * b - a) / (1.0 - r);
            e.t = std::clamp (wNew - envgraph::kMinStage, 0.0, 1.0);
            if (drag_ == envgraph::Decay)
                e.level = std::clamp (start_.sustain - dyPx / std::max (1.0, g0.yBot - g0.yTop), 0.0, 1.0);
        }
        else
        {
            // a curve handle follows the finger vertically: the level of the middle of the stage at the finger's row, then the curve that puts the middle there (closed form); with too little height under the
            // stage for that (a sustain near 100 % has no decay to bend) a relative move: 20 units over half the height of the plot
            double hx, hy;
            handlePointOf (g0, start_, drag_, hx, hy);
            const double lvl = 1.0 - (std::clamp (hy + dyPx, g0.yTop, g0.yBot) - g0.yTop) / std::max (1.0, g0.yBot - g0.yTop);
            const double s = start_.sustain;
            const double range = drag_ == envgraph::CurveA ? 1.0 : (drag_ == envgraph::CurveD ? 1.0 - s : s);
            if (range < 0.12)
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
        downHandle_ = envgraph::None;
        const bool wasDrag = drag_ != envgraph::None;
        drag_ = envgraph::None;
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
    TapGesture tap_;
    Shape start_;
    Edit edit_;
    int downHandle_ = envgraph::None, drag_ = envgraph::None;
    double downX_ = 0.0;
};

}} // namespace synthyboy_mpc::ui
