// SynthyBoy - MPC UI component library: the FX DISPLAY component (FxDisplay): the picture of what an effect does, drawn from the parameters of the selected slot with thin smooth lines:
//   EQ          the frequency response of the bands (low shelf / high pass, peak / notch, second peak / notch, high shelf / low pass) as the engine's equalizer makes it in either law (state variable or
//               cookbook biquads, with the engine's Q of the resonance; ui_fx_editor_host compares it with the engine's impulse response), with a dot at the centre of each band
//   COMPRESSOR  the transfer curve (the peak level of a steady tone in, out) of the bands (the three bands of the multiband compressor in three colours, the one that is on screen for the others): the
//               upward stage under the lower threshold, the downward stage above the upper one (Limit: the ceiling), the make-up gain and the mix, as the engine's gain computer sets them
//   DELAY       the echoes of a click in time (left above the line, right under it) by the network of the engine's style, with the offsets of the two times, the time axis in milliseconds (a tempo sync is
//               turned into milliseconds by the owner)
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
    struct EqState { EqBand low, mid, mid2, high; int law = 0; float sampleRate = 48000.0f; };   // law (eq.law): 0 state variable, 1 cookbook biquads
    struct CompBand { float lowerThr = -40.0f, upperThr = -20.0f, lowerRatio = 0.0f, upperRatio = 0.5f, gainDb = 0.0f; };   // dB, ratios -1 .. +1 and 0 .. 1 (the percent / 100)
    struct CompState { CompBand band[3]; int bands = 0; int activeBand = 1; int mode = 0; float mix = 1.0f; };   // bands: 0 multiband, 1 low only, 2 high only, 3 one band; mode (compressor.limit): 0 Compress, 1 Limit, 2 peak compressor; mix 0 .. 1
    /** style: 0 mono, 1 stereo, 2 ping pong, 3 mid ping pong, 4 tap -> delay; offsetL / offsetR the factors of the two times (delay.offset_left, delay.offset). */
    struct DelayState { float timeMs1 = 300.0f, timeMs2 = 300.0f, feedback = 0.4f; int style = 1; float offsetL = 1.0f, offsetR = 1.0f; };
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
    /** The gain in dB of one EQ band at `f` Hz, as the engine's equalizer makes it (core/engine-own/fx/fx_eq.h; ui_fx_editor_host compares the two):
          law 0, STATE VARIABLE: the trapezoidal state variable section, whose response is the analog section (wh s^2 + wb s + wl) / (s^2 + R s + 1) at s = j tan (pi f / fs) / tan (pi fc / fs), with
                 Q = 0.5 + 15.5 r^3 (R = 1 / Q, r the resonance 0 .. 1) and the output weights of the shelves and the bell from the amplitude A = 10^(gain / 20)
          law 1, COOKBOOK: the biquads of the Audio EQ Cookbook with Q = 0.4 + 1.7417 r^3 for a shelf and 0.1 + 18.9053 r^4 for the others.
        bandIndex 0 low (shelf / high pass), 1 mid and 2 mid 2 (peak / notch), 3 high (shelf / low pass). */
    static float bandDb (int bandIndex, const EqBand& b, float f, int law = 0, float fs = 48000.0f) noexcept
    {
        if (! b.used)
            return 0.0f;
        const bool low = bandIndex == 0, high = bandIndex == 3;
        const float r = std::clamp (b.resonance * 0.01f, 0.0f, 1.0f), gainDb = std::clamp (b.gainDb, -24.0f, 24.0f);
        if (law == 0)
        {
            const float q = 0.5f + 15.5f * r * r * r, R = 1.0f / q, A = std::pow (10.0f, gainDb * 0.05f);
            float wl = 1.0f, wb = 0.0f, wh = 0.0f;   // the low pass, band pass and high pass weights of the engine's design
            if ((low || high) && b.mode == 1)
            {
                wl = high ? 1.0f : 0.0f;   // high: low pass; low: high pass
                wh = high ? 0.0f : 1.0f;
            }
            else if (! low && ! high && b.mode == 1)   // notch
                wh = 1.0f;
            else
            {
                const float blend = low ? -1.0f : (high ? 1.0f : 0.0f);   // low shelf, bell, high shelf
                wl = A + (1.0f - A) * std::clamp (blend + 1.0f, 0.0f, 1.0f);
                wh = 1.0f + (A - 1.0f) * std::clamp (blend, 0.0f, 1.0f);
                wb = R * std::sqrt (A) * (1.0f + (std::sqrt (A) - 1.0f) * (1.0f - blend * blend));
            }
            const float g = std::tan (kPi * std::min (std::max (b.freq, 1.0f) / fs, 0.499f));
            const float w = std::tan (kPi * std::min (f / fs, 0.4999f)) / g;   // the frequency of the analog section
            const float nr = wl - wh * w * w, ni = wb * w, dr = 1.0f - w * w, di = R * w;
            return 10.0f * std::log10 (std::max (1.0e-12f, (nr * nr + ni * ni) / std::max (1.0e-12f, dr * dr + di * di)));
        }
        const bool shelf = (low || high) && b.mode == 0;
        const float w0 = 2.0f * kPi * std::clamp (b.freq, 10.0f, fs * 0.499f) / fs, cw = std::cos (w0), sw = std::sin (w0);
        const float q = shelf ? 0.4f + 1.7417f * r * r * r : 0.1f + 18.9053f * r * r * r * r;
        const float a = std::pow (10.0f, gainDb / 40.0f), alpha = sw / (2.0f * std::max (q, 0.05f));
        float b0, b1, b2, a0, a1, a2;
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
    /** One transfer curve of the compressor: the output level in dB for the steady input level x (dB), as the gain computer of the engine sets it (core/engine-own/fx/fx_compressor.h): the upward stage
        lifts a level under the lower threshold by lowerRatio of the distance (a negative ratio pushes it down), the downward stage takes upperRatio of the distance above the upper threshold away (both act
        at once where the thresholds cross), mode 1 (Limit) replaces the downward stage with a ceiling at the upper threshold, then the make-up gain, then the dry / wet mix (linear, like the engine). */
    static float compOut (const CompBand& b, float x, int mode = 0, float mix = 1.0f) noexcept
    {
        const float lowerThr = std::clamp (b.lowerThr, -120.0f, 12.0f), upperThr = std::clamp (b.upperThr, -120.0f, 12.0f);
        float y = x + std::clamp (b.lowerRatio, -1.0f, 1.0f) * std::max (lowerThr - x, 0.0f);
        if (mode == 1)
            y = std::min (y, upperThr);
        else
            y -= std::clamp (b.upperRatio, 0.0f, 1.0f) * std::max (x - upperThr, 0.0f);
        y += b.gainDb;
        mix = std::clamp (mix, 0.0f, 1.0f);
        if (mix >= 1.0f)
            return y;
        const float amp = std::pow (10.0f, x / 20.0f) * (1.0f - mix) + std::pow (10.0f, y / 20.0f) * mix;
        return 20.0f * std::log10 (std::max (amp, 1.0e-9f));
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
        // The response curves use raw SceneBuilder strokes below, so carry the
        // component's inactive look into those paths through the Pen as well.
        Pen p (sb, enabled_ || inactive_, inactive_);
        Cell cell (sb, rect_);
        const theme::Look& T = theme::L();
        p.block (rect_, false);
        p.textLeft ({ rect_.x + 12, rect_.y + 3, 260, 16 }, title, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        const Rect plot = plotRect();
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

    /** Distortion's input-level marks move independently of its transfer curve. Draw after the page's cache cells. */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        const Rect plot = plotRect();
        if (kind != Kind::Distortion || ! enabled_ || dist.inLevel <= 0.0f || plot.w < 40 || plot.h < 30)
            return;
        Pen p (sb, true, inactive_);
        const uint32_t accent = theme::L().flow[3];
        const float level = std::clamp (dist.inLevel, 0.0f, 1.0f);
        for (float sample : { -level, level })
        {
            const float x = distortionX (plot, sample);
            const float y = distortionY (plot, distortionOutput (sample));
            p.line (x, distortionY (plot, 0.0f), x, y, 1.0f, p.c (accent, 140));
            p.disc (x, y, 4.0f, p.c (accent, 255));
        }
    }

protected:
    void drawBody (Pen&) const override {}

private:
    Rect plotRect() const noexcept
    {
        return { rect_.x + 44, rect_.y + 22, std::max (0, rect_.w - 44 - 16), std::max (0, rect_.h - 22 - 24) };
    }
    static float distortionX (const Rect& p, float value) noexcept
    {
        const float half = static_cast<float> (p.w) * 0.5f;
        return static_cast<float> (p.x) + half + half * value;
    }
    static float distortionY (const Rect& p, float value) noexcept
    {
        const float half = static_cast<float> (p.h) * 0.5f;
        return static_cast<float> (p.y) + half - (half - 2.0f) * value;
    }
    float distortionOutput (float sample) const noexcept
    {
        return std::clamp ((1.0f - dist.mix) * sample + dist.mix * shape (dist.type, dist.driveDb, sample), -1.0f, 1.0f);
    }

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
        p.textRight ({ plot.right() - 50, plot.bottom() + 3, 50, 16 }, plot.w < 800 ? "20k" : "20 kHz", T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);   // the short name beside the 10k of a narrow plot
    }
    /** A horizontal line of a level scale with its label at the left of the plot. */
    void levelLine (Pen& p, const Rect& plot, float y, const char* label, bool strong) const
    {
        const theme::Look& T = theme::L();
        p.line (static_cast<float> (plot.x), y, static_cast<float> (plot.right()), y, 1.0f, p.c (T.grid, strong ? 255 : 150));
        p.textRight ({ plot.x - 42, static_cast<int> (y) - 8, 38, 16 }, label, T.fSmall + 1.0f, p.c (T.textFaint, 255), T.fMin);
    }
    /** A curve of n rows over the plot's width: the line, with a fill to `fillTo` (0: none). */
    static void curveLine (Pen& p, gpu::SceneBuilder& sb, const Rect& plot, const float* rows, int n, uint32_t col, float width, int fillAlpha, float fillTo)
    {
        sb.curve (rows, n, plot.x, plot.w);
        if (fillAlpha > 0)
            sb.fill (fillTo, p.c (col, fillAlpha));
        sb.stroke (width, p.c (col, 255));
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
                db += bandDb (b, *bands[b], f, eq.law, eq.sampleRate);
            rows[i] = yOf (db);
        }
        curveLine (p, sb, plot, rows, kPoints, T.flow[0], 0.8f, 30, yOf (0.0f));
        for (int b = 0; b < 4; ++b)   // the centre of every band that is on: at its gain where the mode has one (shelf, peak), else on the curve (high pass, notch, low pass do not read the gain)
            if (bands[b]->used)
            {
                const float hz = std::clamp (bands[b]->freq, 20.0f, 20000.0f), x = xOfHz (hz, plot);
                float db = bands[b]->gainDb;
                if (bands[b]->mode != 0)
                {
                    db = 0.0f;
                    for (int o = 0; o < 4; ++o)
                        db += bandDb (o, *bands[o], hz, eq.law, eq.sampleRate);
                }
                p.disc (x, yOf (db), 4.0f, p.c (T.flow[b == 3 ? 3 : (b == 0 ? 1 : 2)], 255));
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
                rows[i] = yOf (compOut (comp.band[b], lo + (hi - lo) * static_cast<float> (i) / static_cast<float> (kPoints - 1), comp.mode, comp.mix));
            sb.curve (rows, kPoints, sq.x, sq.w);
            sb.stroke (b == comp.activeBand ? 1.0f : 0.8f, p.c (kCol[b], 255));
            p.disc (xOf (comp.band[b].upperThr), yOf (compOut (comp.band[b], comp.band[b].upperThr, comp.mode, comp.mix)), 3.5f, p.c (kCol[b], 255));
            p.disc (xOf (comp.band[b].lowerThr), yOf (compOut (comp.band[b], comp.band[b].lowerThr, comp.mode, comp.mix)), 3.5f, p.c (kCol[b], 255));
        }
    }

    void drawCompressor (Pen& p, gpu::SceneBuilder& sb, const Rect& plot) const
    {
        // a wide strip (the stacked page) has one plot per band; a block that is not much wider than high (the side page) one square plot with the bands in their colours and their numbers beside it
        if (comp.bands == 0 && plot.w >= 3 * 150 && plot.w >= 2 * plot.h)
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
                if (comp.mode == 1)
                    std::snprintf (t, sizeof t, "%+.0f%% / CEIL", static_cast<double> (comp.band[b].lowerRatio * 100.0f));
                else
                    std::snprintf (t, sizeof t, "%+.0f / %.0f%%", static_cast<double> (comp.band[b].lowerRatio * 100.0f), static_cast<double> (comp.band[b].upperRatio * 100.0f));
                p.textLeft ({ tx + 260, y, 110, 26 }, t, T.fLabel, p.c (T.text, 255), T.fMin);
                ++row;
            }
        }
        else if (sq.x - plot.x >= 100)
        {
            // the side page: every band's name and its numbers in the room at the left of the square (thresholds lower / upper; ratios lower / upper, or the ceiling of the limiter)
            const int tx = plot.x - 36, tw = sq.x - tx - 10;
            int y = plot.y + 26;
            for (int b = 0; b < 3; ++b)
            {
                if (! shown[b])
                    continue;
                char t[48];
                p.textLeft ({ tx, y, tw, 20 }, kName[b], T.fLabel, p.c (kCol[b], 255), T.fMin);
                std::snprintf (t, sizeof t, "%.0f / %.0f dB", static_cast<double> (comp.band[b].lowerThr), static_cast<double> (comp.band[b].upperThr));
                p.textLeft ({ tx, y + 20, tw, 18 }, t, T.fSmall + 1.0f, p.c (T.text, 255), T.fMin);
                if (comp.mode == 1)
                    std::snprintf (t, sizeof t, "%+.0f%%  CEILING", static_cast<double> (comp.band[b].lowerRatio * 100.0f));
                else
                    std::snprintf (t, sizeof t, "%+.0f / %.0f%%", static_cast<double> (comp.band[b].lowerRatio * 100.0f), static_cast<double> (comp.band[b].upperRatio * 100.0f));
                p.textLeft ({ tx, y + 38, tw, 18 }, t, T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
                y += 66;
            }
        }
        for (int b = 0; b < 3; ++b)
        {
            if (! shown[b])
                continue;
            float rows[kPoints];
            for (int i = 0; i < kPoints; ++i)
                rows[i] = yOf (compOut (comp.band[b], lo + (hi - lo) * static_cast<float> (i) / static_cast<float> (kPoints - 1), comp.mode, comp.mix));
            sb.curve (rows, kPoints, sq.x, sq.w);
            sb.stroke (b == comp.activeBand ? 1.0f : 0.7f, p.c (kCol[b], 255));
            p.disc (xOf (comp.band[b].upperThr), yOf (compOut (comp.band[b], comp.band[b].upperThr, comp.mode, comp.mix)), 3.5f, p.c (kCol[b], 255));
            p.disc (xOf (comp.band[b].lowerThr), yOf (compOut (comp.band[b], comp.band[b].lowerThr, comp.mode, comp.mix)), 3.5f, p.c (kCol[b], 255));
            if (comp.bands == 0 && sq.x - plot.x < 100)
            {
                p.line (static_cast<float> (lx), static_cast<float> (plot.y + 8), static_cast<float> (lx + 18), static_cast<float> (plot.y + 8), 2.0f, p.c (kCol[b], 255));
                p.textLeft ({ lx + 24, plot.y, 50, 16 }, kName[b], T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
                lx += 80;
            }
        }
    }

public:
    /** One echo of the delay: when (ms), on which side (0 left, 1 right) and how strong (the signed amplitude for a click of 1 on both inputs). */
    struct Echo { float ms = 0.0f; int side = 0; float amp = 0.0f; };
    static constexpr int kMaxEchoes = 48;
    /** The two delay times of the engine (ms): the left / right line of core/engine-own/fx/fx_effects.h Delay (the offsets scale the times, Mono reads the left time on both sides, Mid ping pong adds the
        right time to the left line), clamped like the engine (3 samples at 48 kHz .. 4 s). */
    static void delayLines (const DelayState& d, float& left, float& right) noexcept
    {
        left = d.timeMs1 * d.offsetL;
        right = (d.style == 0 ? d.timeMs1 : d.timeMs2) * d.offsetR;
        if (d.style == 3)
            left += right;
        left = std::clamp (left, 0.0625f, 4000.0f);
        right = std::clamp (right, 0.0625f, 4000.0f);
    }
    /** The echoes of a click on both inputs, up to `spanMs`, by the network of the engine's style (the loop filter and the limiter are left out): Mono / Stereo feed each line back into itself; Ping pong
        writes the right input and the right line into the left line, the left input and the fed back left line into the right line; Mid ping pong writes the mono sum into the right line and the right line
        into the left one; Tap -> delay is a tap of the mono sum (the left line) feeding a delay that repeats itself (the right line), both lines on both outputs. Echoes at the same time on the same side
        add up. Returns the number of echoes (at most kMaxEchoes, in time order; echoes under 2 % of the click are left out). */
    static int delayEchoes (const DelayState& d, float spanMs, Echo* out) noexcept
    {
        float tl = 0.0f, tr = 0.0f;
        delayLines (d, tl, tr);
        const float fb = std::clamp (d.feedback, -1.0f, 1.0f), len[2] = { tl, tr };
        // a pending read of a line: (time it comes out, line, amplitude); a small queue in time order
        struct Ev { float t; int line; float a; };
        Ev q[kMaxEchoes * 2];
        int nq = 0, n = 0;
        auto push = [&] (float t, int line, float a)
        {
            if (std::fabs (a) < 0.02f || t > spanMs || nq >= kMaxEchoes * 2)
                return;
            q[nq++] = { t, line, a };
        };
        auto emit = [&] (float t, int side, float a)
        {
            for (int i = 0; i < n; ++i)
                if (out[i].side == side && std::fabs (out[i].ms - t) < 0.01f)
                {
                    out[i].amp += a;
                    return;
                }
            if (n < kMaxEchoes)
                out[n++] = { t, side, a };
        };
        const float mono = 1.41421356f;   // 0.7071 (left + right) of a click of 1 on both sides
        switch (d.style)
        {
            case 2: push (len[0], 0, 1.0f); push (len[1], 1, 1.0f); break;   // ping pong: the right input into the left line, the left input into the right line
            case 3: push (len[1], 1, mono); break;                           // mid ping pong: the mono sum into the right line
            case 4: push (len[0], 0, mono); break;                           // tap -> delay: the mono sum into the tap
            default: push (len[0], 0, 1.0f); push (len[1], 1, 1.0f); break;  // mono, stereo
        }
        for (int guard = 0; nq > 0 && guard < 4 * kMaxEchoes; ++guard)
        {
            int k = 0;   // the earliest pending read
            for (int i = 1; i < nq; ++i)
                if (q[i].t < q[k].t)
                    k = i;
            const Ev e = q[k];
            q[k] = q[--nq];
            if (d.style == 4)
            {
                emit (e.t, 0, e.a);   // both lines are on both outputs
                emit (e.t, 1, e.a);
                push (e.t + len[1], 1, e.line == 0 ? e.a : fb * e.a);   // the tap feeds the delay, the delay feeds itself
                continue;
            }
            emit (e.t, e.line, e.a);
            if (d.style == 2 || d.style == 3)
                push (e.t + len[1 - e.line], 1 - e.line, e.line == 0 ? fb * e.a : e.a);   // the left line goes back through the feedback into the right line, the right line straight into the left one
            else
                push (e.t + len[e.line], e.line, fb * e.a);
        }
        return n;
    }

private:
    void drawDelay (Pen& p, gpu::SceneBuilder&, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        float t1 = 0.0f, t2 = 0.0f;
        delayLines (delay, t1, t2);
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
        // the dry click at 0 (on both sides), then the echoes of the engine's network (delayEchoes): left up, right down, the height against the click (a sum above it fills the lane)
        p.line (xOf (0.0f), midY - half, xOf (0.0f), midY + half, 2.0f, p.c (T.textDim, 255));
        Echo echoes[kMaxEchoes];
        const int n = delayEchoes (delay, span, echoes);
        float scale = 1.0f;
        for (int i = 0; i < n; ++i)
            scale = std::max (scale, std::fabs (echoes[i].amp));
        for (int i = 0; i < n; ++i)
        {
            const Echo& e = echoes[i];
            if (e.ms >= span || std::fabs (e.amp) < 0.02f)
                continue;
            const float sign = e.side == 0 ? -1.0f : 1.0f;
            const uint32_t col = e.side == 0 ? T.flow[0] : T.flow[3];
            const float x = xOf (e.ms), h = half * std::clamp (std::fabs (e.amp) / scale, 0.08f, 1.0f);
            p.line (x, midY, x, midY + sign * h, 2.0f, p.c (col, 255));
            p.disc (x, midY + sign * h, 3.0f, p.c (col, e.amp < 0.0f ? 150 : 255));   // an echo of a negative feedback comes back inverted: a dimmer dot
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
            curveLine (p, sb, plot, rows, n, T.flow[0], 0.8f, 34, static_cast<float> (plot.bottom()));
        }
        const float cx = xOfHz (filter.cutoffHz, plot);
        p.line (cx, static_cast<float> (plot.y), cx, static_cast<float> (plot.bottom()), 1.0f, p.c (T.flow[1], 200));
    }

    /** The transfer curve of the waveshaper over the whole plot (input -1 .. +1 across, output -1 .. +1 up): a grid every half, the scale on both axes, the identity as the reference, the curve with the drive and
        the mix, and where the signal is just now (the present peak level at the input and its mirror: the two ends of the swing) as a dot on the curve. */
    void drawDistortion (Pen& p, gpu::SceneBuilder& sb, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        auto xOf = [&] (float v) { return distortionX (plot, v); };
        auto yOf = [&] (float v) { return distortionY (plot, v); };
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
        // the identity (output = input) as a dashed, faint reference
        {
            const float x0 = xOf (-1.0f), y0 = yOf (-1.0f), x1 = xOf (1.0f), y1 = yOf (1.0f);
            const float len = std::sqrt ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
            const float dash = 10.0f, gap = 8.0f;
            for (float at = 0.0f; at < len; at += dash + gap)
            {
                const float a = at / len, b = std::min (at + dash, len) / len;
                p.line (x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * b, y0 + (y1 - y0) * b, 1.0f, p.c (T.textFaint, 170));
            }
        }
        float rows[kPoints], diagonal[kPoints];
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = -1.0f + 2.0f * static_cast<float> (i) / static_cast<float> (kPoints - 1);
            rows[i] = yOf (distortionOutput (x));
            diagonal[i] = yOf (x);
        }
        // the area between the curve and the identity: the difference shows even where the curve is nearly straight
        sb.band (diagonal, rows, kPoints, plot.x, plot.w, p.c (T.flow[0], 46));
        sb.curve (rows, kPoints, plot.x, plot.w);
        sb.stroke (1.6f, p.c (T.flow[0], 255));
    }
};

}} // namespace synthyboy_mpc::ui
