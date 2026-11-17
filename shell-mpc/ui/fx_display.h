// SynthyBoy - MPC UI component library: the FX DISPLAY component (FxDisplay): the picture of what an effect does, drawn from the parameters of the selected slot with thin smooth lines:
//   EQ          the frequency response of the bands (low shelf / high pass, peak / notch, second peak / notch, high shelf / low pass: the textbook biquads of the settings), with a dot at the centre of each band
//   COMPRESSOR  the transfer curve (input level to output level) of the bands (the three bands of the multiband compressor in three colours, the one that is on screen for the others): below the lower
//               threshold the lower ratio, between the thresholds 1 : 1, above the upper threshold the upper ratio, and the make-up gain of the band
//   DELAY       the echoes in time: the two taps (left above the line, right under it), the repeats with the feedback, the time axis in milliseconds (a tempo sync is turned into milliseconds by the owner)
//   FX FILTER   the response of the filter (the owner computes it with the engine's own filter, the same as the filter page), a line with a faint fill
//   DISTORTION  the transfer curve of the waveshaper (the type and the drive) over the whole plot, with the grid, the scales, the identity as a reference and the present signal level as a dot
// The other effects (and the splitters, whose page shows the racks of their bands instead: fx_band_board.h) have no display (the editor gives their knobs the room). Same contract as every component (core.h): a state in, no touch.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "synthyboy/fx_types.h"

namespace synthyboy_mpc { namespace ui {

class FxDisplay : public Component
{
public:
    enum class Kind : uint8_t { None, Eq, Compressor, Delay, Filter, Distortion };

    /** One band of the EQ: mode 0 = the band's first mode (low: shelf, mid: peak, high: shelf), 1 = the second (high pass, notch, low pass); `used` false: the band is off (the second mid band). */
    struct EqBand { bool used = true; int mode = 0; float freq = 1000.0f, gainDb = 0.0f, resonance = 30.0f; };
    struct EqState { EqBand low, mid, mid2, high; };
    struct CompBand { float lowerThr = -40.0f, upperThr = -20.0f, lowerRatio = 0.0f, upperRatio = 0.5f, gainDb = 0.0f; };   // dB, ratios -1 .. +1 and 0 .. 1 (the percent / 100)
    struct CompState { CompBand band[3]; int bands = 0; int activeBand = 1; };   // bands: 0 multiband, 1 low only, 2 high only, 3 one band
    struct DelayState { float timeMs1 = 300.0f, timeMs2 = 300.0f, feedback = 0.4f; int style = 1; };   // style: 0 mono, 1 stereo, 2 ping pong, 3 mid ping pong, 4 tap -> delay
    struct FilterState { const float* db = nullptr; int n = 0; float cutoffHz = 1000.0f; };   // dB at 20 Hz .. 20 kHz (log axis), the cutoff for the mark
    struct DistState { int type = 0; float driveDb = 0.0f; float mix = 1.0f; float inLevel = -1.0f; };   // inLevel: the present peak level of the signal at the input (0 .. 1 of full scale; < 0: not known)

    Kind kind = Kind::None;
    EqState eq;
    CompState comp;
    DelayState delay;
    FilterState filter;
    DistState dist;
    char title[24] = {};

    FxDisplay() = default;

    Size minSize() const override { return { 200, 120 }; }

    void setKind (Kind k, const char* name = nullptr)
    {
        kind = k;
        setText (title, sizeof title, name != nullptr ? name : "");
    }
    /** The kind of display an effect type has (None: no display). */
    static Kind kindOfType (int fxType) noexcept
    {
        switch (fxType)
        {
            case synthyboy::kFxTypeEq: return Kind::Eq;
            case synthyboy::kFxTypeCompressor: return Kind::Compressor;
            case synthyboy::kFxTypeDelay: return Kind::Delay;
            case synthyboy::kFxTypeFilter: return Kind::Filter;
            case synthyboy::kFxTypeDistortion: return Kind::Distortion;
            default: return Kind::None;
        }
    }
    void setKindFromType (int fxType)
    {
        switch (kindOfType (fxType))
        {
            case Kind::Eq: setKind (Kind::Eq, "EQUALIZER"); break;
            case Kind::Compressor: setKind (Kind::Compressor, "COMPRESSOR"); break;
            case Kind::Delay: setKind (Kind::Delay, "ECHOES"); break;
            case Kind::Filter: setKind (Kind::Filter, "FILTER RESPONSE"); break;
            case Kind::Distortion: setKind (Kind::Distortion, "WAVESHAPER"); break;
            default: setKind (Kind::None, ""); break;
        }
    }

    static constexpr int kPoints = 192;
    /** The gain in dB of one EQ band at `f` Hz: the biquads of the Audio EQ Cookbook (shelf, peak, notch, high pass, low pass) at 48 kHz; the resonance 0 .. 100 % is the Q 0.5 .. 10. */
    static float bandDb (int bandIndex, const EqBand& b, float f) noexcept
    {
        if (! b.used)
            return 0.0f;
        const float fs = 48000.0f, w0 = 2.0f * kPi * std::clamp (b.freq, 20.0f, 20000.0f) / fs, cw = std::cos (w0), sw = std::sin (w0);
        const float q = 0.5f * std::pow (20.0f, std::clamp (b.resonance, 0.0f, 100.0f) * 0.01f), a = std::pow (10.0f, b.gainDb / 40.0f), alpha = sw / (2.0f * q);
        float b0, b1, b2, a0, a1, a2;
        const bool low = bandIndex == 0, high = bandIndex == 3;
        if (low && b.mode == 0)   // low shelf
        {
            const float s = 2.0f * std::sqrt (a) * alpha;
            b0 = a * ((a + 1.0f) - (a - 1.0f) * cw + s); b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cw); b2 = a * ((a + 1.0f) - (a - 1.0f) * cw - s);
            a0 = (a + 1.0f) + (a - 1.0f) * cw + s; a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cw); a2 = (a + 1.0f) + (a - 1.0f) * cw - s;
        }
        else if (high && b.mode == 0)   // high shelf
        {
            const float s = 2.0f * std::sqrt (a) * alpha;
            b0 = a * ((a + 1.0f) + (a - 1.0f) * cw + s); b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cw); b2 = a * ((a + 1.0f) + (a - 1.0f) * cw - s);
            a0 = (a + 1.0f) - (a - 1.0f) * cw + s; a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cw); a2 = (a + 1.0f) - (a - 1.0f) * cw - s;
        }
        else if (low)   // high pass
        {
            b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = b0; a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
        }
        else if (high)   // low pass
        {
            b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw; b2 = b0; a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
        }
        else if (b.mode == 0)   // peak
        {
            b0 = 1.0f + alpha * a; b1 = -2.0f * cw; b2 = 1.0f - alpha * a; a0 = 1.0f + alpha / a; a1 = -2.0f * cw; a2 = 1.0f - alpha / a;
        }
        else   // notch
        {
            b0 = 1.0f; b1 = -2.0f * cw; b2 = 1.0f; a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
        }
        const float w = 2.0f * kPi * f / fs, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0f * w), s2 = std::sin (2.0f * w);
        const float nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0f * std::log10 (std::max (1.0e-12f, (nr * nr + ni * ni) / std::max (1.0e-12f, dr * dr + di * di)));
    }
    /** One transfer curve of the compressor: the output level in dB for the input level x (dB). */
    static float compOut (const CompBand& b, float x) noexcept
    {
        float y = x;
        if (x > b.upperThr)
            y = b.upperThr + (x - b.upperThr) * (1.0f - std::clamp (b.upperRatio, 0.0f, 1.0f));
        else if (x < b.lowerThr)
            y = b.lowerThr + (x - b.lowerThr) * (1.0f - std::clamp (b.lowerRatio, -1.0f, 1.0f));
        return y + b.gainDb;
    }
    /** The waveshaper: output for the input x (-1 .. +1) with the gain of the drive. */
    static float shape (int type, float driveDb, float x) noexcept
    {
        const float d = std::pow (10.0f, driveDb / 20.0f), v = d * x;
        switch (type)
        {
            case 1: return std::clamp (v, -1.0f, 1.0f);                                                   // hard clip
            case 2: { const float t = std::fmod (v + 1.0f, 4.0f); const float u = t < 0.0f ? t + 4.0f : t; return u < 2.0f ? u - 1.0f : 3.0f - u; }   // linear fold: a triangle
            case 3: return std::sin (v * 0.5f * kPi);                                                        // sine fold
            case 4: return std::round (std::clamp (v, -1.0f, 1.0f) * 6.0f) / 6.0f;                       // bit crush: steps
            default: return std::tanh (v);                                                                   // soft clip and the others
        }
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        if (kind == Kind::None)
            return;
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        const theme::Look& T = theme::L();
        p.block (rect_, false);
        p.textLeft ({ rect_.x + 12, rect_.y + 3, 260, 16 }, title, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        const Rect plot { rect_.x + 44, rect_.y + 22, std::max (0, rect_.w - 44 - 16), std::max (0, rect_.h - 22 - 24) };
        if (plot.w < 40 || plot.h < 30)
            return;
        switch (kind)
        {
            case Kind::Eq: drawEq (p, sb, plot); break;
            case Kind::Compressor: drawCompressor (p, sb, plot); break;
            case Kind::Delay: drawDelay (p, sb, plot); break;
            case Kind::Filter: drawFilter (p, sb, plot); break;
            case Kind::Distortion: drawDistortion (p, sb, plot); break;
            default: break;
        }
    }

protected:
    void drawBody (Pen&) const override {}

private:
    static float xOfHz (float hz, const Rect& r) noexcept { return static_cast<float> (r.x) + std::clamp (std::log10 (hz / 20.0f) / 3.0f, 0.0f, 1.0f) * static_cast<float> (r.w); }

    /** The vertical grid line with its label, and the frequency axis of a plot. */
    void freqGrid (Pen& p, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        static const struct { float hz; const char* name; } kDecade[3] = { { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } };
        for (const auto& d : kDecade)
        {
            const float x = xOfHz (d.hz, plot);
            p.line (x, static_cast<float> (plot.y), x, static_cast<float> (plot.bottom()), 1.0f, p.c (T.grid, 255));
            p.textCentered ({ static_cast<int> (x) - 24, plot.bottom() + 3, 48, 16 }, d.name, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textLeft ({ plot.x, plot.bottom() + 3, 46, 16 }, "20 Hz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        p.textRight ({ plot.right() - 50, plot.bottom() + 3, 50, 16 }, "20 kHz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
    }
    /** A horizontal line of a level scale with its label at the left of the plot. */
    void levelLine (Pen& p, const Rect& plot, float y, const char* label, bool strong) const
    {
        const theme::Look& T = theme::L();
        p.line (static_cast<float> (plot.x), y, static_cast<float> (plot.right()), y, 1.0f, p.c (T.grid, strong ? 255 : 150));
        p.textRight ({ plot.x - 42, static_cast<int> (y) - 8, 38, 16 }, label, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
    }
    /** A curve of n rows over the plot's width: the line, with a fill to `fillTo` (0: none). */
    static void curveLine (gpu::SceneBuilder& sb, const Rect& plot, const float* rows, int n, uint32_t col, float width, int fillAlpha, float fillTo)
    {
        sb.curve (rows, n, plot.x, plot.w);
        if (fillAlpha > 0)
            sb.fill (fillTo, colour::rgba (col, static_cast<uint32_t> (fillAlpha)));
        sb.stroke (width, colour::rgba (col, 255));
    }

    void drawEq (Pen& p, gpu::SceneBuilder& sb, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        freqGrid (p, plot);
        const float range = 24.0f;   // +- 24 dB
        auto yOf = [&] (float db) { return static_cast<float> (plot.y) + (0.5f - std::clamp (db, -range, range) / (2.0f * range)) * static_cast<float> (plot.h); };
        levelLine (p, plot, yOf (12.0f), "+12", false);
        levelLine (p, plot, yOf (0.0f), "0 dB", true);
        levelLine (p, plot, yOf (-12.0f), "-12", false);
        const EqBand* bands[4] = { &eq.low, &eq.mid, &eq.mid2, &eq.high };
        float rows[kPoints];
        for (int i = 0; i < kPoints; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, static_cast<float> (i) / static_cast<float> (kPoints - 1));
            float db = 0.0f;
            for (int b = 0; b < 4; ++b)
                db += bandDb (b, *bands[b], f);
            rows[i] = yOf (db);
        }
        curveLine (sb, plot, rows, kPoints, T.flow[0], 0.8f, 30, yOf (0.0f));
        for (int b = 0; b < 4; ++b)   // the centre of every band that is on
            if (bands[b]->used)
            {
                const float x = xOfHz (bands[b]->freq, plot);
                p.disc (x, yOf (bands[b]->gainDb), 4.0f, p.c (T.flow[b == 3 ? 3 : (b == 0 ? 1 : 2)], 255));
            }
    }

    /** The multiband compressor: one transfer plot per band side by side (LOW, MID, HIGH in their colours), each with its thresholds as dots on its curve and the numbers under its name, filling the block. */
    void drawCompressorBands (Pen& p, gpu::SceneBuilder& sb, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float lo = -72.0f, hi = 0.0f;
        static const char* const kName[3] = { "LOW", "MID", "HIGH" };
        const uint32_t kCol[3] = { T.flow[0], T.flow[1], T.flow[3] };
        const int gap = 30, labelH = 22, axisH = 20;
        // the plots use the room the block has: as wide as a third of it allows (the transfer curve is not a square plot: the 1 : 1 line runs from corner to corner) and as high as the block
        const int cellW = (plot.w - 2 * gap) / 3, pw = std::max (60, std::min (cellW - 8, 420)), ph = std::max (40, plot.h - labelH - axisH);
        for (int b = 0; b < 3; ++b)
        {
            const int cx = plot.x + b * (cellW + gap) + (cellW - pw) / 2;
            const Rect sq { cx, plot.y + labelH, pw, ph };
            auto xOf = [&] (float db) { return static_cast<float> (sq.x) + std::clamp ((db - lo) / (hi - lo), 0.0f, 1.0f) * static_cast<float> (sq.w); };
            auto yOf = [&] (float db) { return static_cast<float> (sq.bottom()) - std::clamp ((db - lo) / (hi - lo), 0.0f, 1.0f) * static_cast<float> (sq.h); };
            for (int k = 0; k <= 3; ++k)
            {
                const float db = lo + 18.0f * static_cast<float> (k);
                p.line (xOf (db), static_cast<float> (sq.y), xOf (db), static_cast<float> (sq.bottom()), 1.0f, p.c (T.grid, 255));
                p.line (static_cast<float> (sq.x), yOf (db), static_cast<float> (sq.right()), yOf (db), 1.0f, p.c (T.grid, 255));
                char t[8];
                std::snprintf (t, sizeof t, "%.0f", db);
                p.textCentered ({ static_cast<int> (xOf (db)) - 24, sq.bottom() + 2, 48, 16 }, t, T.fSmall, p.c (T.textFaint, 255), T.fMin);
            }
            p.line (static_cast<float> (sq.x), static_cast<float> (sq.bottom()), static_cast<float> (sq.right()), static_cast<float> (sq.y), 1.0f, p.c (T.textFaint, 120));
            char head[56];
            std::snprintf (head, sizeof head, "%s   %.0f / %.0f dB", kName[b], static_cast<double> (comp.band[b].lowerThr), static_cast<double> (comp.band[b].upperThr));
            p.textLeft ({ sq.x, plot.y + 1, std::max (0, cellW), labelH }, head, T.fLabel - 1.0f, p.c (kCol[b], 255), T.fMin);
            float rows[kPoints];
            for (int i = 0; i < kPoints; ++i)
                rows[i] = yOf (compOut (comp.band[b], lo + (hi - lo) * static_cast<float> (i) / static_cast<float> (kPoints - 1)));
            sb.curve (rows, kPoints, sq.x, sq.w);
            sb.stroke (b == comp.activeBand ? 1.0f : 0.8f, colour::rgba (kCol[b], 255));
            p.disc (xOf (comp.band[b].upperThr), yOf (compOut (comp.band[b], comp.band[b].upperThr)), 3.5f, p.c (kCol[b], 255));
            p.disc (xOf (comp.band[b].lowerThr), yOf (compOut (comp.band[b], comp.band[b].lowerThr)), 3.5f, p.c (kCol[b], 255));
        }
    }

    void drawCompressor (Pen& p, gpu::SceneBuilder& sb, const Rect& plot) const
    {
        if (comp.bands == 0 && plot.w >= 3 * 150)
        {
            drawCompressorBands (p, sb, plot);
            return;
        }
        const theme::Look& T = theme::L();
        const float lo = -72.0f, hi = 0.0f;   // the input axis; the output axis is the same range
        const Rect sq { plot.x + std::max (0, (plot.w - plot.h) / 2), plot.y, std::min (plot.w, plot.h), plot.h };
        auto xOf = [&] (float db) { return static_cast<float> (sq.x) + std::clamp ((db - lo) / (hi - lo), 0.0f, 1.0f) * static_cast<float> (sq.w); };
        auto yOf = [&] (float db) { return static_cast<float> (sq.bottom()) - std::clamp ((db - lo) / (hi - lo), 0.0f, 1.0f) * static_cast<float> (sq.h); };
        for (int k = 0; k <= 3; ++k)   // the grid every 18 dB
        {
            const float db = lo + 18.0f * static_cast<float> (k);
            p.line (xOf (db), static_cast<float> (sq.y), xOf (db), static_cast<float> (sq.bottom()), 1.0f, p.c (T.grid, 255));
            p.line (static_cast<float> (sq.x), yOf (db), static_cast<float> (sq.right()), yOf (db), 1.0f, p.c (T.grid, 255));
            char t[8];
            std::snprintf (t, sizeof t, "%.0f", db);
            p.textCentered ({ static_cast<int> (xOf (db)) - 24, sq.bottom() + 3, 48, 16 }, t, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textRight ({ sq.x - 90, sq.y, 86, 16 }, "OUT dB", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        p.textLeft ({ sq.right() + 6, sq.bottom() + 3, 60, 16 }, "IN dB", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        p.line (static_cast<float> (sq.x), static_cast<float> (sq.bottom()), static_cast<float> (sq.right()), static_cast<float> (sq.y), 1.0f, p.c (T.textFaint, 120));   // 1 : 1
        // the bands: all three of the multiband compressor, the one that is on for the others
        static const char* const kName[3] = { "LOW", "MID", "HIGH" };
        const uint32_t kCol[3] = { T.flow[0], T.flow[1], T.flow[3] };
        // the engine: Low band = two bands (the low one and the mid values for the rest), High band = two bands (the mid values below, the high one above), Single band = the mid values alone
        const bool shown[3] = { comp.bands == 0 || comp.bands == 1, true, comp.bands == 0 || comp.bands == 2 };
        int lx = plot.x + 6;
        // the numbers of the bands at the left of the curve (the room the square leaves)
        if (sq.x - plot.x > 360)
        {
            const int tx = plot.x + 8;
            p.textLeft ({ tx + 130, plot.y + 24, 120, 16 }, "THRESHOLDS", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
            p.textLeft ({ tx + 260, plot.y + 24, 100, 16 }, "RATIOS", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
            int row = 0;
            for (int b = 0; b < 3; ++b)
            {
                if (! shown[b])
                    continue;
                char t[48];
                const int y = plot.y + 44 + row * 30;
                p.textLeft ({ tx, y, 120, 26 }, kName[b], T.fLabel, p.c (kCol[b], 255), T.fMin);
                std::snprintf (t, sizeof t, "%.0f / %.0f dB", static_cast<double> (comp.band[b].lowerThr), static_cast<double> (comp.band[b].upperThr));
                p.textLeft ({ tx + 130, y, 130, 26 }, t, T.fLabel, p.c (T.text, 255), T.fMin);
                std::snprintf (t, sizeof t, "%+.0f / %.0f%%", static_cast<double> (comp.band[b].lowerRatio * 100.0f), static_cast<double> (comp.band[b].upperRatio * 100.0f));
                p.textLeft ({ tx + 260, y, 110, 26 }, t, T.fLabel, p.c (T.text, 255), T.fMin);
                ++row;
            }
        }
        for (int b = 0; b < 3; ++b)
        {
            if (! shown[b])
                continue;
            float rows[kPoints];
            for (int i = 0; i < kPoints; ++i)
                rows[i] = yOf (compOut (comp.band[b], lo + (hi - lo) * static_cast<float> (i) / static_cast<float> (kPoints - 1)));
            sb.curve (rows, kPoints, sq.x, sq.w);
            sb.stroke (b == comp.activeBand ? 1.0f : 0.7f, colour::rgba (kCol[b], 255));
            p.disc (xOf (comp.band[b].upperThr), yOf (compOut (comp.band[b], comp.band[b].upperThr)), 3.5f, p.c (kCol[b], 255));
            p.disc (xOf (comp.band[b].lowerThr), yOf (compOut (comp.band[b], comp.band[b].lowerThr)), 3.5f, p.c (kCol[b], 255));
            if (comp.bands == 0 && sq.x - plot.x <= 360)
            {
                p.line (static_cast<float> (lx), static_cast<float> (plot.y + 8), static_cast<float> (lx + 18), static_cast<float> (plot.y + 8), 2.0f, p.c (kCol[b], 255));
                p.textLeft ({ lx + 24, plot.y, 50, 16 }, kName[b], T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
                lx += 80;
            }
        }
    }

    void drawDelay (Pen& p, gpu::SceneBuilder&, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float t1 = std::max (1.0f, delay.timeMs1), t2 = std::max (1.0f, delay.style == 0 ? delay.timeMs1 : delay.timeMs2);
        const float span = std::clamp (std::max (t1, t2) * 6.0f, 600.0f, 8000.0f);
        auto xOf = [&] (float ms) { return static_cast<float> (plot.x) + ms / span * static_cast<float> (plot.w); };
        const float midY = static_cast<float> (plot.y + plot.h / 2), half = static_cast<float> (plot.h) * 0.42f;
        p.line (static_cast<float> (plot.x), midY, static_cast<float> (plot.right()), midY, 1.0f, p.c (T.grid, 255));
        // the time axis: a mark every 1 / 2 / 5 .. s, whatever fits
        const float step = span <= 1200.0f ? 200.0f : (span <= 3000.0f ? 500.0f : (span <= 6000.0f ? 1000.0f : 2000.0f));
        for (float ms = step; ms < span; ms += step)
        {
            const float x = xOf (ms);
            p.line (x, static_cast<float> (plot.y), x, static_cast<float> (plot.bottom()), 1.0f, p.c (T.grid, 150));
            char t[12];
            std::snprintf (t, sizeof t, ms >= 1000.0f ? "%.1f s" : "%.0f ms", ms >= 1000.0f ? ms / 1000.0f : ms);
            p.textCentered ({ static_cast<int> (x) - 30, plot.bottom() + 3, 60, 16 }, t, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textLeft ({ plot.x + 4, plot.y + 2, 20, 16 }, "L", T.fSmall + 1.0f, p.c (T.flow[0], 255), T.fMin);
        p.textLeft ({ plot.x + 4, plot.bottom() - 18, 20, 16 }, "R", T.fSmall + 1.0f, p.c (T.flow[3], 255), T.fMin);
        // the dry hit at 0, then the repeats: stereo = left at the time, right at the second time, each with its own feedback; ping pong = the repeats alternate; mono = both lanes together
        const float fb = std::clamp (std::fabs (delay.feedback), 0.0f, 0.98f);
        p.line (xOf (0.0f), midY - half, xOf (0.0f), midY + half, 2.0f, p.c (T.textDim, 255));
        for (int lane = 0; lane < 2; ++lane)
        {
            const float sign = lane == 0 ? -1.0f : 1.0f;   // left up, right down
            const uint32_t col = lane == 0 ? T.flow[0] : T.flow[3];
            float level = 1.0f;
            for (int k = 1; k <= 12; ++k)
            {
                float t = 0.0f;
                if (delay.style == 2 || delay.style == 3)   // ping pong: the repeats alternate between the lanes
                {
                    if ((k % 2 == 1) != (lane == 0))
                        continue;
                    t = t1 * static_cast<float> (k);
                }
                else
                    t = (lane == 0 ? t1 : t2) * static_cast<float> (k);
                level = std::pow (fb, static_cast<float> (delay.style == 2 || delay.style == 3 ? k - 1 : k));
                if (delay.style != 2 && delay.style != 3 && delay.style != 1 && lane == 1)
                    break;
                if (t >= span || level < 0.03f)
                    break;
                const float firstEcho = (delay.style == 2 || delay.style == 3) ? 1.0f : std::max (0.05f, fb);   // the first echo fills the height of the plot, the later ones are drawn against it (the decay is what the feedback says)
                const float x = xOf (t), h = half * std::clamp (level / firstEcho, 0.08f, 1.0f);
                p.line (x, midY, x, midY + sign * h, 2.0f, p.c (col, 255));
                p.disc (x, midY + sign * h, 3.0f, p.c (col, 255));
            }
        }
    }

    void drawFilter (Pen& p, gpu::SceneBuilder& sb, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        freqGrid (p, plot);
        const float top = 24.0f, bottom = -36.0f;
        auto yOf = [&] (float db) { return static_cast<float> (plot.y) + (top - std::clamp (db, bottom, top)) / (top - bottom) * static_cast<float> (plot.h); };
        levelLine (p, plot, yOf (12.0f), "+12", false);
        levelLine (p, plot, yOf (0.0f), "0 dB", true);
        levelLine (p, plot, yOf (-24.0f), "-24", false);
        if (filter.db != nullptr && filter.n >= 2)
        {
            float rows[gpu::kMaxCurve];
            const int n = std::min (filter.n, static_cast<int> (gpu::kMaxCurve));
            for (int i = 0; i < n; ++i)
                rows[i] = yOf (filter.db[i]);
            curveLine (sb, plot, rows, n, T.flow[0], 0.8f, 34, static_cast<float> (plot.bottom()));
        }
        const float cx = xOfHz (filter.cutoffHz, plot);
        p.line (cx, static_cast<float> (plot.y), cx, static_cast<float> (plot.bottom()), 1.0f, p.c (T.flow[1], 200));
    }

    /** The transfer curve of the waveshaper over the whole plot (input -1 .. +1 across, output -1 .. +1 up): a grid every half, the scale on both axes, the identity as the reference, the curve with the drive and
        the mix, and where the signal is just now (the present peak level at the input and its mirror: the two ends of the swing) as a dot on the curve. */
    void drawDistortion (Pen& p, gpu::SceneBuilder& sb, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float cx = static_cast<float> (plot.x) + static_cast<float> (plot.w) * 0.5f, cy = static_cast<float> (plot.y) + static_cast<float> (plot.h) * 0.5f;
        const float rx = static_cast<float> (plot.w) * 0.5f, ry = static_cast<float> (plot.h) * 0.5f - 2.0f;
        auto xOf = [&] (float v) { return cx + rx * v; };
        auto yOf = [&] (float v) { return cy - ry * v; };
        for (int k = -2; k <= 2; ++k)   // the grid: -1, -0.5, 0, +0.5, +1
        {
            const float v = 0.5f * static_cast<float> (k);
            const uint32_t gc = p.c (T.grid, k == 0 ? 255 : 150);
            p.line (xOf (v), static_cast<float> (plot.y), xOf (v), static_cast<float> (plot.bottom()), 1.0f, gc);
            p.line (static_cast<float> (plot.x), yOf (v), static_cast<float> (plot.right()), yOf (v), 1.0f, gc);
            const char* t = k == -2 ? "-1" : (k == 0 ? "0" : (k == 2 ? "+1" : (k < 0 ? "-.5" : "+.5")));
            p.textCentered ({ static_cast<int> (xOf (v)) - 24, plot.bottom() + 3, 48, 16 }, t, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
            p.textRight ({ plot.x - 42, static_cast<int> (yOf (v)) - 8, 38, 16 }, t, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textLeft ({ plot.x + 6, plot.y + 2, 60, 16 }, "OUT", T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
        p.textRight ({ plot.right() - 66, plot.bottom() - 18, 60, 16 }, "IN", T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
        p.line (xOf (-1.0f), yOf (-1.0f), xOf (1.0f), yOf (1.0f), 1.0f, p.c (T.textFaint, 120));   // the identity
        float rows[kPoints];
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = -1.0f + 2.0f * static_cast<float> (i) / static_cast<float> (kPoints - 1);
            const float wet = shape (dist.type, dist.driveDb, x);
            rows[i] = yOf (std::clamp ((1.0f - dist.mix) * x + dist.mix * wet, -1.0f, 1.0f));
        }
        sb.curve (rows, kPoints, plot.x, plot.w);
        sb.stroke (0.8f, colour::rgba (T.flow[0], 255));
        if (dist.inLevel > 0.0f)
        {
            const float lv = std::clamp (dist.inLevel, 0.0f, 1.0f);
            for (int sgn = -1; sgn <= 1; sgn += 2)
            {
                const float x = static_cast<float> (sgn) * lv, wet = shape (dist.type, dist.driveDb, x);
                const float y = yOf (std::clamp ((1.0f - dist.mix) * x + dist.mix * wet, -1.0f, 1.0f));
                p.line (xOf (x), yOf (0.0f), xOf (x), y, 1.0f, p.c (T.flow[3], 140));
                p.disc (xOf (x), y, 4.0f, p.c (T.flow[3], 255));
            }
        }
    }
};

}} // namespace synthyboy_mpc::ui
