// SynthyBoy - MPC UI component library: the OSCILLATOR DISPLAY. What the picture of the oscillator in the display rectangle is made of, in the language of theme.h: thin lines of light along a
// colour gradient, a stack of the wavetable's frames behind the current one, the unison voices as hairlines around the main curve, the min / max picture of a sample, the loop range, the
// grain window, a faint spectrum under the wave. The data comes in a plain struct (Data); nothing here knows an engine. Every curve is a few hundred vertices: the whole picture stays
// inside the frame budget measured on the device (docs/MPC.md "Observed on the device", D plan trial 17).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core.h"

namespace synthyboy_mpc { namespace ui { namespace display {

constexpr int kMaxStack = 9, kMaxSpectrum = 64, kMaxPoints = 1024;

struct Data
{
    enum class Kind : uint8_t { None, Wave, MinMax, Spectrum, Message } kind = Kind::None;
    // Wave: `cycle` = n samples (-1 .. 1, one period). MinMax: `cycle` = the lower row, `cycleMax` = the upper row (same scale, +- 1 at most). Spectrum: `cycle` = dB values (-120 .. +60).
    const float* cycle = nullptr;
    const float* cycleMax = nullptr;
    int n = 0;
    // the wavetable: numStack frames of the table at the fixed positions i / (numStack - 1), `pos` = the current position 0 .. 1 (the frames near it light up, so moving the position makes the highlight run)
    const float* stack[kMaxStack] = {};
    int numStack = 0;
    float pos = 0.0f;
    // unison: the voices, the detune and the blend (0 .. 1)
    int voices = 1;
    float detune = 0.0f, blend = 0.0f;
    // sample / granular
    bool loopOn = false;
    float loopStart = 0.0f, loopEnd = 1.0f;
    bool grain = false;
    float grainPos = 0.0f, grainSize = 0.0f;
    float playhead = -1.0f;          // 0 .. 1 over the sample, < 0 = not playing / not known
    // the live read-outs of the engine (all optional, < 0 / 0 = none): the read heads of the newest voices over the file (sample, multisample, the scan of a granular cloud), the grains that sound (position
    // over the file and how far each has run), the phases of the sub oscillator's voices on the wave (0 .. 1), the read head of a sample noise
    static constexpr int kMaxHeads = 8, kMaxGrains = 32;
    float heads[kMaxHeads] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int numGrainsLive = 0;
    float liveGrainPos[kMaxGrains] = {}, liveGrainLife[kMaxGrains] = {};
    float phases[kMaxHeads] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    float noiseHead = -1.0f;
    // the faint spectrum under a wave (magnitudes 0 .. 1)
    const float* spectrum = nullptr;
    int numSpectrum = 0;
    uint32_t colour = 0x2ee6f0;      // the instance colour (0xRRGGBB)
    const char* message = "";
};

/** The four gradient stops of a line of the instance colour: a light start, the colour, a step towards the end colour of the look, the end. */
inline void stopsOf (uint32_t c, uint32_t out[4]) noexcept
{
    const theme::Look& T = theme::L();
    out[0] = theme::mix (c, 0xffffff, 0.45f);
    out[1] = c;
    out[2] = theme::mix (c, T.flow[3], 0.55f);
    out[3] = T.flow[3];
}

namespace detail
{
inline uint32_t a (uint32_t rgb, int alpha) noexcept { return (rgb << 8) | static_cast<uint32_t> (std::min (255, std::max (0, alpha))); }
inline void paintLine (gpu::SceneBuilder& sb, float x0, float x1, const uint32_t st[4], int alpha)
{
    sb.paintX (x0, x1, a (st[0], alpha), a (st[1], alpha), a (st[2], alpha), a (st[3], alpha));
}
/** One crisp line along a curve that was just added with sb.curve (the colour gradient along x, anti-aliased, no light around it). */
inline void glowLine (gpu::SceneBuilder& sb, float x0, float x1, const uint32_t st[4], float half, int alpha, bool = true)
{
    paintLine (sb, x0, x1, st, 255);
    sb.style (1.0f, false);
    sb.stroke (half, static_cast<uint32_t> (alpha));
}
}

/** Draws the picture inside `r` (the display rectangle). The ground (the block) is the caller's. */
inline void draw (gpu::SceneBuilder& sb, const Rect& r, const Data& d)
{
    namespace dt = detail;
    const theme::Look& T = theme::L();
    if (r.w < 60 || r.h < 40)
        return;
    const Rect in = r.inset (8);
    const float x0 = static_cast<float> (in.x), x1 = static_cast<float> (in.right());
    const float top = static_cast<float> (in.y) + 6.0f, bottom = static_cast<float> (in.bottom()) - 6.0f, mid = 0.5f * (top + bottom), amp = 0.5f * (bottom - top);
    uint32_t st[4];
    stopsOf (d.colour, st);
    const int ix = in.x, iw = in.w;
    const bool wave = d.kind == Data::Kind::Wave, minmax = d.kind == Data::Kind::MinMax, spec = d.kind == Data::Kind::Spectrum;
    if (d.kind == Data::Kind::None)
        return;
    Cell cell (sb, r);   // the picture of the data is one cached cell: it is drawn again only when the data changes (the live read-outs are drawn after it, drawLive)
    // ---- the block under the picture: a vertical gradient, the keyline
    sb.paintY (static_cast<float> (r.y), static_cast<float> (r.bottom()), dt::a (T.panel0, 255), dt::a (T.panel0, 255), dt::a (T.panel1, 255), dt::a (T.panel1, 255));
    sb.rrect (static_cast<float> (r.x), static_cast<float> (r.y), static_cast<float> (r.right()), static_cast<float> (r.bottom()), T.rBlock, 0xffffffffu);
    sb.paintOff();
    sb.rrect (static_cast<float> (r.x), static_cast<float> (r.y), static_cast<float> (r.right()), static_cast<float> (r.bottom()), T.rBlock, dt::a (T.keyline, 255), T.frame);
    // ---- the afterglow: the previous picture of this rectangle, faded, under the new one (the GPU's feedback; the CPU reference does not draw it)
    if (T.afterglow > 0.0f && theme::D().afterglow && (d.kind == Data::Kind::Wave || d.kind == Data::Kind::MinMax))
        sb.feedback (static_cast<float> (r.x + 1), static_cast<float> (r.y + 1), static_cast<float> (r.right() - 1), static_cast<float> (r.bottom() - 1), T.afterglow);
    // ---- the grid: a centre line and quarters, hairlines
    if (wave || minmax)
    {
        sb.line (x0, mid, x1, mid, T.hair, dt::a (T.grid, 255));
        const uint32_t gridCol = dt::a (theme::mix (T.grid, T.keyline, 0.5f), 255);
        for (int q = 1; q < 4; ++q)
        {
            const float x = x0 + (x1 - x0) * static_cast<float> (q) * 0.25f;
            sb.line (x, top, x, bottom, T.hair, gridCol);
        }
    }
    float rows[kMaxPoints], rows2[kMaxPoints];
    const int n = std::min (d.n, static_cast<int> (kMaxPoints));
    // ---- the faint spectrum along the bottom of the wave
    if (wave && theme::D().spectrum && d.spectrum != nullptr && d.numSpectrum >= 2)
    {
        const int m = std::min (d.numSpectrum, static_cast<int> (kMaxSpectrum));
        float sp[kMaxSpectrum];
        for (int i = 0; i < m; ++i)
            sp[i] = bottom - std::min (1.0f, std::max (0.0f, d.spectrum[i])) * (bottom - top) * 0.26f;
        sb.curve (sp, m, ix, iw);
        sb.fillGradient (bottom, bottom - (bottom - top) * 0.26f, bottom, dt::a (d.colour, 46), dt::a (d.colour, 0));
    }
    if (wave && n >= 2)
    {
        const bool stacked = d.numStack >= 2 && theme::D().stackFrames >= 2;
        const float scale = stacked ? 0.74f : 0.86f;
        // the rows of a cycle: clamped a little beyond +-1 (the overshoot of a band limited saw at its edge stays inside the plot, it does not stick out above the curve)
        auto rowsOf = [&] (const float* src, float* dst, float yOff, float k)
        {
            for (int i = 0; i < n; ++i)
                dst[i] = mid + yOff - std::min (1.06f, std::max (-1.06f, src[i])) * amp * scale * k;
        };
        int litFirst[kMaxStack], litX[kMaxStack], litW[kMaxStack], numLit = 0;
        float litAmount[kMaxStack];
        // the stack: the frames of the table behind each other, the far ones fainter and thinner; the ones near the position light up
        if (stacked)
        {
            const int N = std::min ({ d.numStack, static_cast<int> (kMaxStack), theme::D().stackFrames });
            const float dxs = static_cast<float> (in.w) * 0.010f, dys = static_cast<float> (in.h) * 0.032f;
            for (int i = 0; i < N; ++i)     // from the back (the first frame) to the front
            {
                if (d.stack[i] == nullptr)
                    continue;
                const float depth = static_cast<float> (N - 1 - i) / static_cast<float> (N - 1);       // 1 = the far one
                const float pct = static_cast<float> (i) / static_cast<float> (N - 1);
                const float lit = std::max (0.0f, 1.0f - std::fabs (pct - d.pos) * static_cast<float> (N - 1));
                const float total = dxs * static_cast<float> (N - 1) * 0.5f;
                const float yOff = -depth * dys * static_cast<float> (N - 1) * 0.5f - 2.0f, xOff = total - depth * total;   // the far frames start further right: all stay inside the block
                rowsOf (d.stack[i], rows, yOff, 1.0f);
                const int fx = ix + static_cast<int> (std::lround (xOff)), fw = iw - static_cast<int> (std::lround (xOff));
                const int fr = sb.curve (rows, n, fx, fw);
                if (lit > 0.05f && fr >= 0 && numLit < kMaxStack)
                {
                    litFirst[numLit] = fr; litX[numLit] = fx; litW[numLit] = fw; litAmount[numLit] = lit;
                    ++numLit;
                }
                const int alpha = static_cast<int> (40.0f + 70.0f * (1.0f - depth) + 110.0f * lit);
                const uint32_t dc = theme::mix (T.flow[0], T.flow[3], depth);                       // the far frames are the end colour of the look, the near ones the start
                const uint32_t ds[4] = { dc, dc, dc, dc }, lit4[4] = { st[0], st[1], st[2], st[3] };
                if (lit > 0.01f)
                    dt::glowLine (sb, x0, x1, lit > 0.5f ? lit4 : ds, T.curveBack * 0.5f + 0.5f * lit * T.curve, alpha, false);
                else
                {
                    dt::paintLine (sb, x0, x1, ds, 255);
                    sb.style (1.0f, false);
                    sb.stroke (T.curveBack * 0.5f, static_cast<uint32_t> (alpha));
                }
            }
            sb.paintOff();
            sb.style (1.0f, false);
        }
        // the unison voices: hairlines around the main curve, a fan that opens with the detune and is as strong as the blend. They share the x grid of the main curve (not shifted in x: a shifted copy of a
        // wave with a steep edge makes stripes at the edges of the display), so a hairline differs from the main curve in height only
        if (d.voices > 1)
        {
            const int v = std::min (std::min (d.voices, 7) - 1, theme::D().unisonHairs);
            const int alpha = static_cast<int> (30.0f + 90.0f * d.blend);
            for (int k = 1; k <= v; ++k)
            {
                const float side = (k % 2 == 1 ? 1.0f : -1.0f) * static_cast<float> ((k + 1) / 2) / static_cast<float> ((v + 1) / 2);
                const float spread = side * (0.012f + 0.07f * d.detune);
                rowsOf (d.cycle, rows, 0.0f, 1.0f + spread);
                sb.curve (rows, n, ix, iw);
                dt::paintLine (sb, x0, x1, st, 255);
                sb.style (1.0f, false);
                sb.stroke (T.curveBack * 0.45f, static_cast<uint32_t> (alpha));
            }
            sb.paintOff();
        }
        // the main curve: the area under it, the soft pass, the crisp line
        rowsOf (d.cycle, rows, 0.0f, 1.0f);
        const int mainFirst = sb.curve (rows, n, ix, iw);
        sb.fillGradient (mid, mid - amp * scale, mid, dt::a (d.colour, 70), dt::a (d.colour, 0));
        dt::glowLine (sb, x0, x1, st, T.curve * 0.5f + (stacked ? 0.15f : 0.0f), 255, false);
        sb.paintOff();
        // the light: the main curve and the lit frames drawn into the bloom layer (a quarter of the size), blurred, added over the picture inside the display: one cheap pass for all the lines
        if (T.backGlow > 0.0f && theme::D().glow && mainFirst >= 0)
        {
            sb.glowBegin (4);
            dt::paintLine (sb, x0, x1, st, 255);
            sb.style (1.0f, false);
            for (int i = 0; i < numLit; ++i)
            {
                sb.curveAt (litFirst[i], n, litX[i], litW[i]);
                sb.stroke (T.curve * 0.5f + 2.0f, static_cast<uint32_t> (120.0f * litAmount[i]));
            }
            sb.curveAt (mainFirst, n, ix, iw);
            sb.stroke (T.curve * 0.5f + 2.4f, 170);
            sb.paintOff();
            sb.glowEnd (T.backGlow, 1.4f, 0xffffffffu, r.x, r.y, r.right(), r.bottom());
        }
        sb.style (1.0f, false);
    }
    else if (minmax && n >= 2 && d.cycleMax != nullptr)
    {
        float peak = 0.05f;
        for (int i = 0; i < n; ++i)
            peak = std::max ({ peak, std::fabs (d.cycle[i]), std::fabs (d.cycleMax[i]) });
        for (int i = 0; i < n; ++i)
        {
            rows[i] = mid - d.cycleMax[i] / peak * amp * 0.92f;
            rows2[i] = mid - d.cycle[i] / peak * amp * 0.92f;
        }
        const bool granular = d.grain && d.grainSize > 0.0f;
        const int bandAlpha = granular ? 70 : 175;
        const uint32_t bs[4] = { st[1], st[1], st[2], st[3] };   // the colour itself, not its light start
        sb.paintX (x0, x1, dt::a (bs[0], bandAlpha), dt::a (bs[1], bandAlpha), dt::a (bs[2], bandAlpha), dt::a (bs[3], bandAlpha));
        sb.band (rows, rows2, n, ix, iw, 255);
        sb.paintOff();
        sb.curve (rows, n, ix, iw);
        dt::glowLine (sb, x0, x1, st, T.curveBack * 0.5f + 0.2f, granular ? 90 : 255);
        sb.curve (rows2, n, ix, iw);
        dt::glowLine (sb, x0, x1, st, T.curveBack * 0.5f + 0.2f, granular ? 90 : 255);
        sb.paintOff();
        sb.style (1.0f, false);
        if (granular)
        {
            // the grains: the part of the sample under the window lights up
            const int c0 = std::max (0, static_cast<int> (d.grainPos * static_cast<float> (n - 1))), c1 = std::min (n - 1, static_cast<int> ((d.grainPos + d.grainSize) * static_cast<float> (n - 1)));
            if (c1 - c0 >= 2)
            {
                const float wx0 = x0 + static_cast<float> (c0) / static_cast<float> (n - 1) * static_cast<float> (iw - 1);
                const int wn = c1 - c0 + 1;
                sb.paintX (wx0, x1, dt::a (bs[0], 235), dt::a (bs[1], 235), dt::a (bs[2], 235), dt::a (bs[3], 235));
                sb.band (rows + c0, rows2 + c0, wn, static_cast<int> (wx0), static_cast<int> (static_cast<float> (c1 - c0) / static_cast<float> (n - 1) * static_cast<float> (iw - 1)) + 1, 255);
                sb.paintOff();
            }
        }
        if (d.loopOn)
        {
            const float xs = x0 + d.loopStart * static_cast<float> (iw - 1), xe = x0 + d.loopEnd * static_cast<float> (iw - 1);
            sb.rect (xs, top, std::max (xs, xe), bottom, dt::a (T.q, 16));
            for (float x : { xs, xe })
            {
                sb.line (x, top, x, bottom, 1.5f, dt::a (T.q, 255));
            }
        }
        if (d.grain && d.grainSize > 0.0f)
        {
            const float gx = x0 + d.grainPos * static_cast<float> (iw - 1), gw = std::max (6.0f, d.grainSize * static_cast<float> (iw - 1));
            const Rect g { static_cast<int> (gx), static_cast<int> (top), static_cast<int> (gw), static_cast<int> (bottom - top) };
            sb.style (1.0f, false);
            sb.rrect (static_cast<float> (g.x), static_cast<float> (g.y), static_cast<float> (g.right()), static_cast<float> (g.bottom()), T.rChip, dt::a (T.flow[2], 255), 1.4f);
        }
    }
    else if (spec && n >= 2)
    {
        for (int i = 0; i < n; ++i)
            rows[i] = bottom - std::min (1.0f, std::max (0.0f, (d.cycle[i] + 120.0f) / 180.0f)) * (bottom - top);
        sb.curve (rows, n, ix, iw);
        sb.fillGradient (bottom, top, bottom, dt::a (d.colour, 80), dt::a (d.colour, 0));
        dt::glowLine (sb, x0, x1, st, T.curve * 0.5f, 255);
        sb.paintOff();
        sb.style (1.0f, false);
    }
    else if (d.kind == Data::Kind::Message && d.message[0] != '\0')
    {
        const float size = 28.0f;
        sb.text (x0 + 0.5f * (static_cast<float> (iw) - sdf::textWidth (d.message, size)), sdf::baselineForCentre (0.5f * (top + bottom), size), size, d.message, dt::a (T.textDim, 255));
    }
    sb.style (1.0f, false);
    sb.paintOff();
}

/** The live read-outs of the engine over the picture (not cached: they move): the grains that sound (where each reads and how far it has run: the brightness follows the window), a line per read head (the newest
    voice the brightest), the phases of the sub oscillator's voices as dots on the wave, the read head of a sample noise. Same rectangle and data as `draw`. */
inline void drawLive (gpu::SceneBuilder& sb, const Rect& r, const Data& d)
{
    namespace dt = detail;
    const theme::Look& T = theme::L();
    if (r.w < 60 || r.h < 40 || d.kind == Data::Kind::None)
        return;
    const Rect in = r.inset (8);
    const float x0 = static_cast<float> (in.x), x1 = static_cast<float> (in.right());
    const float top = static_cast<float> (in.y) + 6.0f, bottom = static_cast<float> (in.bottom()) - 6.0f, mid = 0.5f * (top + bottom), amp = 0.5f * (bottom - top);
    const int iw = in.w;
    const int n = std::min (d.n, static_cast<int> (kMaxPoints));
    const bool minmax = d.kind == Data::Kind::MinMax;
    sb.style (1.0f, false);
    sb.paintOff();
    if (d.kind == Data::Kind::Wave && n >= 2)
    {
        const float scale = d.numStack >= 2 && theme::D().stackFrames >= 2 ? 0.74f : 0.86f;
        for (int v = 0; v < Data::kMaxHeads; ++v)
            if (d.phases[v] >= 0.0f)
            {
                const float ph = std::min (1.0f, d.phases[v]), fi = ph * static_cast<float> (n - 1);
                const int i0 = std::min (n - 2, static_cast<int> (fi));
                const float f = fi - static_cast<float> (i0);
                const float c0 = std::min (1.06f, std::max (-1.06f, d.cycle[i0])), c1 = std::min (1.06f, std::max (-1.06f, d.cycle[i0 + 1]));
                const float py = mid - (c0 + (c1 - c0) * f) * amp * scale, pxx = x0 + ph * static_cast<float> (iw - 1);
                sb.disc (pxx, py, v == 0 ? 3.6f : 2.6f, dt::a (d.colour, v == 0 ? 255 : 160));
                sb.disc (pxx, py, v == 0 ? 1.8f : 1.2f, dt::a (0xffffff, v == 0 ? 255 : 200));
            }
    }
    if (minmax)
    {
        for (int g = 0; g < std::min (d.numGrainsLive, static_cast<int> (Data::kMaxGrains)); ++g)
        {
            const float life = std::min (1.0f, std::max (0.0f, d.liveGrainLife[g]));
            const float env = std::sin (kPi * life);
            const float gx = x0 + std::min (1.0f, std::max (0.0f, d.liveGrainPos[g])) * static_cast<float> (iw - 1), hh = (bottom - top) * (0.16f + 0.30f * env);
            sb.style (1.0f, true);
            sb.line (gx, mid - hh, gx, mid + hh, 2.0f, dt::a (T.flow[2], static_cast<int> (70.0f + 185.0f * env)));
        }
        sb.style (1.0f, false);
        for (int v = 0; v < Data::kMaxHeads; ++v)
        {
            if (d.heads[v] < 0.0f)
                continue;
            const float px = x0 + std::min (1.0f, d.heads[v]) * static_cast<float> (iw - 1);
            sb.line (px, top, px, bottom, v == 0 ? 1.6f : 1.1f, dt::a (0xffffff, v == 0 ? 255 : 150 - 12 * v));
        }
        if (d.playhead >= 0.0f)
        {
            const float px = x0 + d.playhead * static_cast<float> (iw - 1);
            sb.line (px, top, px, bottom, 1.4f, dt::a (0xffffff, 255));
        }
    }
    if (d.kind == Data::Kind::Message && d.noiseHead >= 0.0f)   // a noise whose source is a sample: its read head along a thin line
    {
        const float size = 28.0f, ly = mid + 0.5f * size + 14.0f, hx = x0 + std::min (1.0f, d.noiseHead) * static_cast<float> (iw - 1);
        sb.style (1.0f, true);
        sb.line (x0, ly, x1, ly, 1.0f, dt::a (T.grid, 255));
        sb.disc (hx, ly, 3.4f, dt::a (d.colour, 255));
    }
    sb.style (1.0f, false);
    sb.paintOff();
}

/** A module panel (the compact view's envelope and LFO): a block with the title, the curve of the module as a line of light along its two colours (ENV orange to red, LFO green to cyan) over
    a faint area, the grid as hairlines, and the running dot (envelope) or cursor (LFO). `v` = n values 0 .. 1 (1 = the top of the plot), `dot` = the position 0 .. 1 of the running point
    along the curve (< 0: no data). `plot` is the rectangle of the curve inside the block. */
struct Module
{
    Rect rect, plot;
    bool env = true, highlighted = false;
    const char* title = "";
    const float* v = nullptr;
    int n = 0;
    float dot = -1.0f;
};
inline void drawModule (gpu::SceneBuilder& sb, const Module& m)
{
    namespace dt = detail;
    const theme::Look& T = theme::L();
    Cell cell (sb, m.rect);   // the block and the curve: cached, drawn again when the curve or the title changes (the running point is drawn after it, drawModuleLive)
    const uint32_t c0 = m.env ? T.env[0] : T.lfo[0], c1 = m.env ? T.env[1] : T.lfo[1];
    const float x0 = static_cast<float> (m.rect.x), y0 = static_cast<float> (m.rect.y), x1 = static_cast<float> (m.rect.right()), y1 = static_cast<float> (m.rect.bottom());
    sb.paintY (y0, y1, dt::a (T.panel0, 255), dt::a (T.panel0, 255), dt::a (T.panel1, 255), dt::a (T.panel1, 255));
    sb.rrect (x0, y0, x1, y1, T.rBlock, 0xffffffffu);
    sb.paintOff();
    sb.rrect (x0, y0, x1, y1, T.rBlock, dt::a (m.highlighted ? c0 : T.keyline, 255), m.highlighted ? 1.8f : T.frame);
    sb.text (x0 + 16.0f, y0 + 30.0f, T.fValue, m.title, dt::a (m.highlighted ? T.text : T.textDim, 255));
    const float px = static_cast<float> (m.plot.x), pxe = static_cast<float> (m.plot.right()), pt = static_cast<float> (m.plot.y), pb = static_cast<float> (m.plot.bottom());
    const float zero = m.env ? pb : 0.5f * (pt + pb);
    sb.line (px, zero, pxe, zero, T.hair, dt::a (T.grid, 255));
    for (int q = 1; q < 4; ++q)
    {
        const float gx = px + (pxe - px) * static_cast<float> (q) * 0.25f;
        sb.line (gx, pt, gx, pb, T.hair, dt::a (T.grid, 150));
    }
    if (m.v == nullptr || m.n < 2)
        return;
    const int n = std::min (m.n, static_cast<int> (kMaxPoints));
    float rows[kMaxPoints];
    for (int i = 0; i < n; ++i)
        rows[i] = std::min (pb + 40.0f, std::max (pt - 40.0f, pb - std::min (1.2f, std::max (-0.2f, m.v[i])) * (pb - pt)));
    const int first = sb.curve (rows, n, m.plot.x, m.plot.w);
    sb.fillGradient (zero, pt, zero, dt::a (c0, 70), dt::a (c0, 0));
    const uint32_t cs[4] = { dt::a (c0, 255), dt::a (theme::mix (c0, c1, 0.33f), 255), dt::a (theme::mix (c0, c1, 0.66f), 255), dt::a (c1, 255) };
    sb.paintX (px, pxe, cs[0], cs[1], cs[2], cs[3]);
    sb.style (1.0f, false);
    sb.stroke (T.curve * 0.5f + 0.15f, 255);
    sb.paintOff();
    if (T.backGlow > 0.0f && theme::D().glow && first >= 0)
    {
        sb.glowBegin (4);
        sb.paintX (px, pxe, cs[0], cs[1], cs[2], cs[3]);
        sb.curveAt (first, n, m.plot.x, m.plot.w);
        sb.stroke (T.curve * 0.5f + 2.4f, 170);
        sb.paintOff();
        sb.glowEnd (T.backGlow, 1.4f, 0xffffffffu, m.plot.x - 6, m.plot.y - 6, m.plot.right() + 6, m.plot.bottom() + 6);
    }
}

/** The running point of the envelope / the cursor of the LFO over the panel (not cached: it moves). */
inline void drawModuleLive (gpu::SceneBuilder& sb, const Module& m)
{
    namespace dt = detail;
    const theme::Look& T = theme::L();
    if (m.v == nullptr || m.n < 2 || m.dot < 0.0f)
        return;
    const uint32_t c1 = m.env ? T.env[1] : T.lfo[1];
    const float px = static_cast<float> (m.plot.x), pxe = static_cast<float> (m.plot.right()), pt = static_cast<float> (m.plot.y), pb = static_cast<float> (m.plot.bottom());
    const int n = std::min (m.n, static_cast<int> (kMaxPoints));
    auto row = [&] (int i) { return std::min (pb + 40.0f, std::max (pt - 40.0f, pb - std::min (1.2f, std::max (-0.2f, m.v[i])) * (pb - pt))); };
    const float dx = px + std::min (1.0f, m.dot) * (pxe - px - 1.0f) + 0.5f, pos = std::min (1.0f, m.dot) * static_cast<float> (n - 1);
    const int i = std::min (static_cast<int> (pos), n - 2);
    const float dy = row (i) + (row (i + 1) - row (i)) * (pos - static_cast<float> (i));
    sb.style (1.0f, false);
    sb.paintOff();
    if (! m.env)
        sb.line (dx, pt, dx, pb, 1.2f, dt::a (0xffffff, 90));
    sb.disc (dx, dy, 3.4f, dt::a (c1, 255));
    sb.disc (dx, dy, 1.8f, 0xffffffffu);
}

}}} // namespace synthyboy_mpc::ui::display
