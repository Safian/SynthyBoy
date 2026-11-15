// SynthyBoy - MPC UI component library: the FX DISPLAY component (FxDisplay).
// Specialized visual displays for effect types: EQ frequency curve, compressor transfer curve + live GR,
// delay beat grid, filter frequency response, distortion waveshaping curve.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "core.h"
#include "synthyboy/fx_types.h"

namespace synthyboy_mpc { namespace ui {

class FxDisplay : public Component
{
public:
    enum class Kind : uint8_t
    {
        None,
        Eq,
        Compressor,
        Delay,
        Filter,
        Distortion,
        Generic
    };

    struct EqParams
    {
        float lowFreq = 100.0f, lowGain = 0.0f, lowQ = 0.7f;
        float midFreq = 1000.0f, midGain = 0.0f, midQ = 1.0f;
        float mid2Freq = 3000.0f, mid2Gain = 0.0f, mid2Q = 1.0f;
        float highFreq = 8000.0f, highGain = 0.0f, highQ = 0.7f;
    };

    struct CompParams
    {
        float threshold = -18.0f; // dB
        float ratio = 4.0f;       // 1 .. 20
        float knee = 6.0f;        // dB
        float gainReduction = 0.0f; // dB (0 .. 20)
    };

    struct DelayParams
    {
        float timeMs1 = 250.0f, timeMs2 = 375.0f;
        float feedback = 0.4f;
        bool sync = false;
        int div1 = 5, div2 = 6;
    };

    struct FilterParams
    {
        float cutoff = 2000.0f;
        float resonance = 0.5f;
        int mode = 0; // 0 Lowpass, 1 Highpass, 2 Bandpass, 3 Notch
    };

    struct DistParams
    {
        float drive = 0.0f; // dB
        int type = 0;       // 0 Soft, 1 Hard, 2 Fold, 3 Tube
    };

    Kind kind = Kind::None;
    EqParams eq;
    CompParams comp;
    DelayParams delay;
    FilterParams filter;
    DistParams dist;
    char title[24] = {};

    FxDisplay() = default;

    Size minSize() const override { return { 200, 120 }; }

    void setKind (Kind k, const char* name = nullptr)
    {
        kind = k;
        if (name) setText (title, sizeof title, name);
    }

    void setKindFromType (int fxType)
    {
        switch (fxType)
        {
            case synthyboy::kFxTypeEq: setKind (Kind::Eq, "EQUALIZER"); break;
            case synthyboy::kFxTypeCompressor: setKind (Kind::Compressor, "COMPRESSOR"); break;
            case synthyboy::kFxTypeDelay: setKind (Kind::Delay, "DELAY"); break;
            case synthyboy::kFxTypeFilter: setKind (Kind::Filter, "FX FILTER"); break;
            case synthyboy::kFxTypeDistortion: setKind (Kind::Distortion, "DISTORTION"); break;
            default: setKind (Kind::Generic, "EFFECT"); break;
        }
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, false);
        p.rrect (rect_, 6.0f, p.c (T.keyline, 160), 1.0f);

        // Header caption
        p.text (static_cast<float> (rect_.x + 10), static_cast<float> (rect_.y + 18),
                T.fSmall, title, p.c (T.textDim, 200));

        const Rect in = rect_.inset (10);
        const int plotY = in.y + 16;
        const int plotH = in.h - 18;
        const Rect plot { in.x, plotY, in.w, plotH };

        switch (kind)
        {
            case Kind::Eq: drawEq (p, plot); break;
            case Kind::Compressor: drawCompressor (p, plot); break;
            case Kind::Delay: drawDelay (p, plot); break;
            case Kind::Filter: drawFilter (p, plot); break;
            case Kind::Distortion: drawDistortion (p, plot); break;
            default: drawGeneric (p, plot); break;
        }
    }

private:
    void drawEq (Pen& p, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float midY = static_cast<float> (plot.y + plot.h / 2);
        const float x0 = static_cast<float> (plot.x), x1 = static_cast<float> (plot.right());

        // Grid lines: 0 dB center line, 100 Hz, 1 kHz, 10 kHz
        p.line (x0, midY, x1, midY, 1.0f, p.c (T.keyline, 140));

        const float freqGrid[] = { 100.0f, 1000.0f, 10000.0f };
        for (float f : freqGrid)
        {
            const float normX = (std::log10 (f) - std::log10 (20.0f)) / (std::log10 (20000.0f) - std::log10 (20.0f));
            const float gx = x0 + normX * static_cast<float> (plot.w);
            p.line (gx, static_cast<float> (plot.y), gx, static_cast<float> (plot.bottom()), 1.0f, p.c (T.keyline, 80));
        }

        // Compute EQ curve
        constexpr int kPoints = 64;
        float prevX = x0, prevY = midY;

        for (int i = 0; i < kPoints; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (kPoints - 1);
            const float freq = 20.0f * std::pow (1000.0f, t);
            const float px = x0 + t * static_cast<float> (plot.w);

            // Combined response in dB
            float gainDb = 0.0f;
            // Low shelf
            const float lowRatio = freq / std::max (20.0f, eq.lowFreq);
            gainDb += eq.lowGain / (1.0f + lowRatio * lowRatio);
            // Mid peaking band 1
            const float midRatio = std::log2 (freq / std::max (20.0f, eq.midFreq));
            gainDb += eq.midGain * std::exp (-0.5f * (midRatio * midRatio) / (0.5f * eq.midQ));
            // Mid peaking band 2
            const float mid2Ratio = std::log2 (freq / std::max (20.0f, eq.mid2Freq));
            gainDb += eq.mid2Gain * std::exp (-0.5f * (mid2Ratio * mid2Ratio) / (0.5f * eq.mid2Q));
            // High shelf
            const float highRatio = std::max (20.0f, eq.highFreq) / freq;
            gainDb += eq.highGain / (1.0f + highRatio * highRatio);

            const float py = midY - (gainDb / 24.0f) * (static_cast<float> (plot.h) * 0.45f);
            const float clampedY = std::clamp (py, static_cast<float> (plot.y), static_cast<float> (plot.bottom()));

            if (i > 0)
                p.line (prevX, prevY, px, clampedY, 2.0f, p.c (T.flow[0], 255));
            prevX = px;
            prevY = clampedY;
        }
    }

    void drawCompressor (Pen& p, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const int grW = 16;
        const Rect curveBox { plot.x, plot.y, plot.w - grW - 8, plot.h };
        const Rect grBox { plot.right() - grW, plot.y, grW, plot.h };

        // 1. I/O Transfer Curve (-60 dB to 0 dB)
        p.rrect (curveBox, 3.0f, p.c (T.keyline, 100), 1.0f);
        const float cx0 = static_cast<float> (curveBox.x), cy0 = static_cast<float> (curveBox.bottom());
        const float cw = static_cast<float> (curveBox.w), ch = static_cast<float> (curveBox.h);

        // 1:1 Reference line
        p.line (cx0, cy0, cx0 + cw, cy0 - ch, 1.0f, p.c (T.keyline, 100));

        // Threshold marker
        const float threshNorm = std::clamp ((comp.threshold + 60.0f) / 60.0f, 0.0f, 1.0f);
        const float tx = cx0 + threshNorm * cw;
        const float ty = cy0 - threshNorm * ch;
        p.line (tx, static_cast<float> (curveBox.y), tx, static_cast<float> (curveBox.bottom()), 1.0f, p.c (T.textDim, 80));

        // Transfer line with threshold + ratio
        p.line (cx0, cy0, tx, ty, 2.0f, p.c (T.flow[0], 255));
        const float slope = 1.0f / std::max (1.0f, comp.ratio);
        const float endNormY = threshNorm + (1.0f - threshNorm) * slope;
        const float ey = cy0 - endNormY * ch;
        p.line (tx, ty, cx0 + cw, ey, 2.0f, p.c (T.flow[0], 255));
        p.disc (tx, ty, 3.5f, p.c (T.flow[1], 255));

        // 2. Gain Reduction Meter
        p.rect (grBox, p.c (T.panel1, 255));
        p.rrect (grBox, 2.0f, p.c (T.keyline, 140), 1.0f);
        const float grNorm = std::clamp (comp.gainReduction / 20.0f, 0.0f, 1.0f);
        if (grNorm > 0.02f)
        {
            const int barH = static_cast<int> (grNorm * ch);
            p.rect ({ grBox.x + 1, grBox.y + 1, grBox.w - 2, barH }, p.c (0xffd84a, 230));
        }
        p.textCentered ({ grBox.x - 4, grBox.bottom() - 14, grBox.w + 8, 14 }, "GR", T.fMin, p.c (T.textDim, 200));
    }

    void drawDelay (Pen& p, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float midY = static_cast<float> (plot.y + plot.h / 2);
        const float w = static_cast<float> (plot.w);

        // Grid lines for beat divisions
        p.line (static_cast<float> (plot.x), midY, static_cast<float> (plot.right()), midY, 1.0f, p.c (T.keyline, 120));
        for (int i = 1; i <= 8; ++i)
        {
            const float gx = static_cast<float> (plot.x) + (static_cast<float> (i) / 8.0f) * w;
            p.line (gx, static_cast<float> (plot.y), gx, static_cast<float> (plot.bottom()), 1.0f, p.c (T.keyline, 60));
        }

        // Tap 1 (Left)
        const float tap1X = static_cast<float> (plot.x) + std::clamp (delay.timeMs1 / 1000.0f, 0.05f, 0.95f) * w;
        p.line (tap1X, midY, tap1X, midY - 30.0f, 2.0f, p.c (T.flow[0], 255));
        p.disc (tap1X, midY - 30.0f, 3.5f, p.c (T.flow[0], 255));
        p.text (tap1X + 4.0f, midY - 20.0f, T.fMin, "L", p.c (T.text, 240));

        // Tap 2 (Right)
        const float tap2X = static_cast<float> (plot.x) + std::clamp (delay.timeMs2 / 1000.0f, 0.05f, 0.95f) * w;
        p.line (tap2X, midY, tap2X, midY + 30.0f, 2.0f, p.c (T.flow[1], 255));
        p.disc (tap2X, midY + 30.0f, 3.5f, p.c (T.flow[1], 255));
        p.text (tap2X + 4.0f, midY + 24.0f, T.fMin, "R", p.c (T.text, 240));

        // Echo decay ghosts
        float f1 = delay.feedback;
        for (int e = 1; e <= 3; ++e)
        {
            const float echoX = tap1X + static_cast<float> (e) * 40.0f;
            if (echoX < static_cast<float> (plot.right()))
            {
                const float h = 30.0f * std::pow (f1, static_cast<float> (e));
                p.line (echoX, midY, echoX, midY - h, 1.2f, p.c (T.flow[0], 140));
            }
        }
    }

    void drawFilter (Pen& p, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float x0 = static_cast<float> (plot.x), x1 = static_cast<float> (plot.right());
        const float midY = static_cast<float> (plot.y + plot.h / 2);

        p.line (x0, midY, x1, midY, 1.0f, p.c (T.keyline, 100));

        const float normCut = (std::log10 (std::clamp (filter.cutoff, 20.0f, 20000.0f)) - std::log10 (20.0f)) / (std::log10 (20000.0f) - std::log10 (20.0f));
        const float cutX = x0 + normCut * static_cast<float> (plot.w);
        const float resY = midY - filter.resonance * (static_cast<float> (plot.h) * 0.4f);

        // Lowpass response
        p.line (x0, midY - 4.0f, cutX - 10.0f, midY - 4.0f, 2.0f, p.c (T.flow[0], 255));
        p.line (cutX - 10.0f, midY - 4.0f, cutX, resY, 2.0f, p.c (T.flow[0], 255));
        p.line (cutX, resY, cutX + 40.0f, static_cast<float> (plot.bottom()) - 4.0f, 2.0f, p.c (T.flow[0], 255));
        p.line (cutX + 40.0f, static_cast<float> (plot.bottom()) - 4.0f, x1, static_cast<float> (plot.bottom()) - 4.0f, 1.5f, p.c (T.flow[0], 180));
        p.disc (cutX, resY, 4.0f, p.c (T.flow[1], 255));
    }

    void drawDistortion (Pen& p, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float cx = static_cast<float> (plot.x + plot.w / 2);
        const float cy = static_cast<float> (plot.y + plot.h / 2);

        // Crosshairs
        p.line (static_cast<float> (plot.x), cy, static_cast<float> (plot.right()), cy, 1.0f, p.c (T.keyline, 120));
        p.line (cx, static_cast<float> (plot.y), cx, static_cast<float> (plot.bottom()), 1.0f, p.c (T.keyline, 120));

        // Waveshaping transfer curve: y = tanh (drive * x)
        constexpr int kN = 48;
        const float d = std::pow (10.0f, dist.drive / 20.0f);
        float prevX = static_cast<float> (plot.x);
        float prevY = cy;

        for (int i = 0; i < kN; ++i)
        {
            const float inX = -1.0f + 2.0f * (static_cast<float> (i) / static_cast<float> (kN - 1));
            const float outY = std::tanh (d * inX);

            const float px = cx + inX * (static_cast<float> (plot.w) * 0.45f);
            const float py = cy - outY * (static_cast<float> (plot.h) * 0.45f);

            if (i > 0)
                p.line (prevX, prevY, px, py, 2.0f, p.c (T.flow[0], 255));
            prevX = px;
            prevY = py;
        }
    }

    void drawGeneric (Pen& p, const Rect& plot) const
    {
        const theme::Look& T = theme::L();
        const float cx = static_cast<float> (plot.x + plot.w / 2);
        const float cy = static_cast<float> (plot.y + plot.h / 2);

        // LFO sine wave modulation shape
        constexpr int kN = 64;
        float prevX = static_cast<float> (plot.x), prevY = cy;
        for (int i = 0; i < kN; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (kN - 1);
            const float px = static_cast<float> (plot.x) + t * static_cast<float> (plot.w);
            const float py = cy + std::sin (t * 4.0f * kPi) * (static_cast<float> (plot.h) * 0.35f);
            if (i > 0)
                p.line (prevX, prevY, px, py, 1.8f, p.c (T.flow[0], 220));
            prevX = px;
            prevY = py;
        }
        p.disc (cx, cy, 4.0f, p.c (T.flow[1], 255));
    }
};

}} // namespace synthyboy_mpc::ui
