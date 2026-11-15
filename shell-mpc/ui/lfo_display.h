// SynthyBoy - MPC UI component library: the LFO DISPLAY. What the picture of an LFO in its display rectangle is made of, in the language of theme.h: a block with hairline grid, the shape of one cycle as one
// thin crisp line along the LFO's two colours over a faint area, the RISE and DELAY of the LFO as a faint lead-in in front of it, the loop point of an envelope type, and (the only thing that moves) one
// cursor per sounding voice: a vertical hairline and a dot on the shape at the position the voice is at. No light around lines, values or text. A RANDOM LFO has no cycle: the display shows the trace of
// its output over the last moments (the data is a ring the owner fills, oldest first) with a dot at the newest value.
// The data comes in a plain struct (Data); nothing here knows an engine. `draw` is the still part (two cached cells: the ground and the curve), `drawLive` what moves.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <optional>

#include "core.h"

namespace synthyboy_mpc { namespace ui { namespace lfodisplay {

constexpr int kMaxCursors = 8, kMaxPoints = 1024;

struct Data
{
    enum class Kind : uint8_t { None, Shape, Random } kind = Kind::None;
    // Shape: n values -1 .. 1 of the left lane over the time since the note started, in cycles (0 .. 1: the x axis of LfoDisplay::compute)
    const float* y = nullptr;
    int n = 0;
    // the lead-in in front of the cycle: the shares 0 .. 1 of a cycle's time that the DELAY and the RISE take (the owner computes them from the rate; the display only draws them, at most 30 % of the width)
    float delayShare = 0.0f, riseShare = 0.0f;
    int grid = 0;                // lines of the grid of the shape editor (2 .. 32); 0 = quarters
    float loopX = -1.0f;         // x 0 .. 1 of the loop point of an envelope type, < 0 = none
    bool envelope = false;       // the picture is the whole run of an envelope type (no repeat)
    int numCursors = 0;          // the voices: the position x 0 .. 1 over the cycle and the output y -1 .. 1 of each (newest first)
    float cx[kMaxCursors] = {}, cy[kMaxCursors] = {};
    // Random: the trace (oldest first, n values -1 .. 1) and the style name
    const float* trace = nullptr;
    int traceN = 0;
    const char* label = "";      // the name in the corner (the shape, the style)
    bool dim = false;            // the LFO does nothing (the picture stays, faint)
    bool editing = false;        // the shape editor has the same ground and curve under its handles: no lead-in, no label
};

inline uint32_t a (uint32_t rgb, int alpha) noexcept { return (rgb << 8) | static_cast<uint32_t> (std::min (255, std::max (0, alpha))); }

/** The width of the lead-in strip for a display rectangle. */
inline int leadWidth (const Rect& r, const Data& d) noexcept
{
    const float s = d.delayShare + d.riseShare;
    if (d.kind != Data::Kind::Shape || d.editing || s <= 0.0f)
        return 0;
    const int w = r.w - 16;
    return std::clamp (static_cast<int> (std::lround (static_cast<float> (w) * s / (1.0f + s))), 20, static_cast<int> (static_cast<float> (w) * 0.3f));
}
/** The rectangle of the curve inside the block: the inner area without the lead-in strip, with a margin above and below that the extremes fit in. */
inline Rect plotRect (const Rect& r, const Data& d) noexcept
{
    const Rect in = r.inset (8);
    const int lead = leadWidth (r, d);
    return { in.x + lead, in.y + 6, std::max (0, in.w - lead), std::max (0, in.h - 12) };
}
/** The row of the value v (-1 .. 1) in a plot rectangle. */
inline float rowOf (const Rect& plot, float v) noexcept
{
    const float mid = static_cast<float> (plot.y) + static_cast<float> (plot.h) * 0.5f, amp = static_cast<float> (plot.h) * 0.5f * 0.9f;
    return mid - std::clamp (v, -1.2f, 1.2f) * amp;
}
inline float colOf (const Rect& plot, float x) noexcept { return static_cast<float> (plot.x) + std::clamp (x, 0.0f, 1.0f) * static_cast<float> (std::max (1, plot.w - 1)); }

/** The level of detail (theme.h Detail): the reduced tier (the Key 61) draws no faint area under the curve, no lead-in, at most two cursors and half the points of the curve. */
inline bool rich() noexcept { return theme::detailLevel() == 1; }

/** The four stops of the line of an LFO: its two colours (the look's LFO gradient) in four equal steps. */
inline void stops (uint32_t out[4]) noexcept
{
    const theme::Look& T = theme::L();
    for (int i = 0; i < 4; ++i)
        out[i] = theme::mix (T.lfo[0], T.lfo[1], static_cast<float> (i) / 3.0f);
}

/** The block, the grid, then the lead-in, the shape or the trace. `cached`: the data is one cached cell (drawn again only when it changes); false: it changes every picture (a handle being dragged). */
inline void draw (gpu::SceneBuilder& sb, const Rect& r, const Data& d, bool cached = true)
{
    const theme::Look& T = theme::L();
    if (r.w < 60 || r.h < 40 || d.kind == Data::Kind::None)
        return;
    const Rect plot = plotRect (r, d);
    const float x0 = static_cast<float> (plot.x), x1 = static_cast<float> (plot.right() - 1), top = static_cast<float> (plot.y), bottom = static_cast<float> (plot.bottom());
    const float mid = 0.5f * (top + bottom);
    {
        Cell ground (sb, r);
        sb.paintY (static_cast<float> (r.y), static_cast<float> (r.bottom()), a (T.panel0, 255), a (T.panel0, 255), a (T.panel1, 255), a (T.panel1, 255));
        sb.rrect (static_cast<float> (r.x), static_cast<float> (r.y), static_cast<float> (r.right()), static_cast<float> (r.bottom()), T.rBlock, 0xffffffffu);
        sb.paintOff();
        sb.rrect (static_cast<float> (r.x), static_cast<float> (r.y), static_cast<float> (r.right()), static_cast<float> (r.bottom()), T.rBlock, a (T.keyline, 255), T.frame);
        const uint32_t g0 = a (T.grid, 255), g1 = a (theme::mix (T.grid, T.keyline, 0.5f), 255);
        const int div = d.grid >= 2 ? d.grid : 4;
        for (int k = 1; k < div; ++k)
        {
            const float f = static_cast<float> (k) / static_cast<float> (div);
            sb.line (x0 + (x1 - x0) * f, top, x0 + (x1 - x0) * f, bottom, T.hair, g1);
            if (d.grid >= 2)
                sb.line (x0, top + (bottom - top) * f, x1, top + (bottom - top) * f, T.hair, g1);
        }
        sb.line (x0, mid, x1, mid, T.hair, g0);
        sb.line (x0, top, x0, bottom, T.hair, g0);
        if (d.envelope && d.kind == Data::Kind::Shape)
            sb.line (x1, top, x1, bottom, T.hair, g0);
    }
    std::optional<Cell> dataCell;
    if (cached)
        dataCell.emplace (sb, r);
    uint32_t st[4];
    stops (st);
    const int alpha = d.dim ? 70 : 255;
    float rows[kMaxPoints];
    if (d.kind == Data::Kind::Shape && d.y != nullptr && d.n >= 2)
    {
        const int lead = leadWidth (r, d);
        if (lead > 0 && rich())
        {
            // the lead-in: the delay (the LFO does not move: a flat line on the middle), then the rise (the swing grows from nothing to full): the two edges of the envelope of the amplitude
            const float lx0 = static_cast<float> (r.x + 8), lx1 = x0, share = d.delayShare + d.riseShare, dEnd = lx0 + (lx1 - lx0) * (share > 0.0f ? d.delayShare / share : 0.0f);
            const uint32_t fc = a (st[1], d.dim ? 40 : 110);
            sb.style (1.0f, false);
            sb.line (lx0, mid, dEnd, mid, T.hair + 0.3f, fc);
            const float amp = 0.5f * (bottom - top) * 0.9f;
            sb.line (dEnd, mid, lx1, mid - amp, T.hair + 0.3f, fc);
            sb.line (dEnd, mid, lx1, mid + amp, T.hair + 0.3f, fc);
            sb.line (lx1, top, lx1, bottom, T.hair, a (T.keyline, 255));
        }
        const int step = rich() ? 1 : 2;   // the reduced tier: every second point of the curve (the vertical steps of a stepped shape stay: two neighbouring points make them)
        const int n = std::min ((d.n + step - 1) / step, kMaxPoints);
        for (int i = 0; i < n; ++i)
            rows[i] = std::min (bottom + 40.0f, std::max (top - 40.0f, rowOf (plot, d.y[std::min (d.n - 1, i * step)])));
        const int first = sb.curve (rows, n, plot.x, plot.w);
        if (first >= 0)
        {
            if (rich())
                sb.fillGradient (mid, top, mid, a (T.lfo[0], d.dim ? 25 : 60), a (T.lfo[0], 0));
            sb.paintX (x0, x1, a (st[0], alpha), a (st[1], alpha), a (st[2], alpha), a (st[3], alpha));
            sb.style (1.0f, false);
            sb.stroke (T.curve * 0.5f + 0.15f, 255);
            sb.paintOff();
        }
        if (d.loopX >= 0.0f)
        {
            const float lx = colOf (plot, d.loopX);
            sb.line (lx, top, lx, bottom, 1.2f, a (T.q, 255));
            sb.disc (lx, top + 4.0f, 3.0f, a (T.q, 255));
        }
    }
    else if (d.kind == Data::Kind::Random && d.trace != nullptr && d.traceN >= 2)
    {
        const int n = std::min (d.traceN, kMaxPoints);
        for (int i = 0; i < n; ++i)
            rows[i] = std::min (bottom + 40.0f, std::max (top - 40.0f, rowOf (plot, d.trace[i])));
        if (sb.curve (rows, n, plot.x, plot.w) >= 0)
        {
            if (rich())
                sb.fillGradient (mid, top, mid, a (T.lfo[0], d.dim ? 25 : 50), a (T.lfo[0], 0));
            sb.paintX (x0, x1, a (st[0], alpha), a (st[1], alpha), a (st[2], alpha), a (st[3], alpha));
            sb.style (1.0f, false);
            sb.stroke (T.curve * 0.5f + 0.15f, 255);
            sb.paintOff();
        }
    }
    sb.style (1.0f, false);
    sb.paintOff();
    if (d.label[0] != '\0' && ! d.editing)
        sb.text (static_cast<float> (r.x + 14), static_cast<float> (r.y + 24), T.fLabel, d.label, a (T.textDim, 220));
}

/** What moves: the cursors of the voices (a hairline over the plot and a dot on the shape), the dot at the newest value of a trace. */
inline void drawLive (gpu::SceneBuilder& sb, const Rect& r, const Data& d)
{
    const theme::Look& T = theme::L();
    if (r.w < 60 || r.h < 40 || d.kind == Data::Kind::None || d.dim)
        return;
    const Rect plot = plotRect (r, d);
    const float top = static_cast<float> (plot.y), bottom = static_cast<float> (plot.bottom());
    uint32_t st[4];
    stops (st);
    sb.style (1.0f, false);
    sb.paintOff();
    if (d.kind == Data::Kind::Shape)
    {
        for (int i = std::min (d.numCursors, rich() ? kMaxCursors : 2) - 1; i >= 0; --i)   // the newest last: on top
        {
            if (d.cx[i] < 0.0f)
                continue;
            const float x = colOf (plot, d.cx[i]) + 0.5f, y = rowOf (plot, d.cy[i]);
            sb.line (x, top, x, bottom, 1.0f, a (0xffffff, i == 0 ? 110 : 55));
            sb.disc (x, y, i == 0 ? 3.6f : 2.6f, a (st[3], i == 0 ? 255 : 170));
            sb.disc (x, y, i == 0 ? 1.8f : 1.2f, a (0xffffff, 255));
        }
    }
    else if (d.kind == Data::Kind::Random && d.trace != nullptr && d.traceN >= 2)
    {
        const float x = static_cast<float> (plot.right() - 1), y = rowOf (plot, d.trace[d.traceN - 1]);
        sb.disc (x, y, 3.6f, a (st[3], 255));
        sb.disc (x, y, 1.8f, a (0xffffff, 255));
    }
    (void) T;
}

}}} // namespace synthyboy_mpc::ui::lfodisplay
