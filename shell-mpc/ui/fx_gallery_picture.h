// SynthyBoy - MPC UI: the PICTURES of the effect types (a display, no control): a small picture of what an effect does to the sound (the transfer curve of the compressor and of the distortion, the frequency response of the
// EQ, the filter, the flanger and the phaser, the echoes of the delay, the decaying tail of the reverb, the voices of the chorus and of the hyper dimension, the shifted lines of the frequency shifter, the impulse response of
// the convolver, the gain line of the utility). The native cards of the effect gallery (the host skin) have the picture window the plugin draws into. Ported from the deleted ui::FxGallery::drawPicture
// (git show d8cfc25f~1:shell-mpc/ui/fx_gallery.h); the models behind the EQ, the compressor and the distortion are the ones of the effect page's displays (FxDisplay).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cctype>
#include <cmath>

#include "core.h"
#include "fx_display.h"
#include "synthyboy/fx_types.h"

namespace synthyboy_mpc { namespace ui { namespace fxgallery {

constexpr int kHeadH = 44, kPad = 12, kGap = 10, kMaxCardH = 220, kMaxTypes = 16;

/** The rectangle of the picture of a card of rectangle `r` (inside the card, over its name). */
inline Rect pictureRect (const Rect& r) noexcept { return { r.x + 14, r.y + 14, std::max (0, r.w - 28), std::max (0, r.h - 14 - 50) }; }

/** The rectangle of the card of effect `type` (1 .. 16) in the gallery of the rectangle `area` (two rows; the places of the types are fixed, so that a card keeps its place while others are not offered). */
inline Rect cardRectOfType (const Rect& area, int type) noexcept
{
    const int rows = 2, cols = (kMaxTypes + rows - 1) / rows;
    const int areaW = area.w - 2 * kPad, areaH = area.h - kHeadH - kPad;
    const int cw = std::max (56, (areaW - (cols - 1) * kGap) / cols);
    const int ch = std::clamp ((areaH - (rows - 1) * kGap) / rows, 56, kMaxCardH);
    const int y0 = area.y + kHeadH + std::max (0, (areaH - rows * ch - (rows - 1) * kGap) / 2);
    const int x0 = area.x + (area.w - cols * cw - (cols - 1) * kGap) / 2;
    const int t = type - 1;
    return { x0 + (t % cols) * (cw + kGap), y0 + (t / cols) * (ch + kGap), cw, ch };
}

/** The picture of an effect type inside `plot` (public: the tests draw it alone). */
inline void drawPicture (Pen& p, gpu::SceneBuilder& sb, int type, const Rect& plot)
{
    const theme::Look& T = theme::L();
    p.rrect (plot, 6.0f, p.c (T.keyline, 110), 1.0f);
    const float cy = static_cast<float> (plot.y) + static_cast<float> (plot.h) * 0.5f;
    p.line (static_cast<float> (plot.x + 6), cy, static_cast<float> (plot.right() - 6), cy, 1.0f, p.c (T.grid, 255));
    const Rect in = plot.inset (8);
    const uint32_t main = T.flow[0], second = T.flow[1];
    constexpr int kN = 64;
    float rows[kN];
    // rows from a function of x (0 .. 1) giving the height v (0 = bottom, 1 = top of the picture)
    auto yOf = [&] (float v) { return static_cast<float> (in.y) + (1.0f - std::clamp (v, 0.0f, 1.0f)) * static_cast<float> (in.h); };
    auto curve = [&] (uint32_t col, float width, int alpha, auto&& f)
    {
        for (int i = 0; i < kN; ++i)
            rows[i] = yOf (f (static_cast<float> (i) / static_cast<float> (kN - 1)));
        sb.curve (rows, kN, in.x, in.w);
        sb.stroke (width, colour::rgba (col, static_cast<uint32_t> (alpha)));
    };
    const float w = T.curveHi * 0.5f + 0.4f;   // the half width of the stroke
    auto reference = [&] { curve (T.keylineHi, 0.6f, 140, [] (float x) { return x; }); };   // the identity: what a transfer curve is compared with
    auto bar = [&] (float x, float h, uint32_t col, int alpha) { p.line (static_cast<float> (in.x) + x * static_cast<float> (in.w), yOf (0.0f), static_cast<float> (in.x) + x * static_cast<float> (in.w), yOf (h), 3.0f, p.c (col, alpha)); };
    using FD = FxDisplay;
    switch (type)
    {
        case synthyboy::kFxTypeEq:   // a boost and a cut: the biquads of the EQ page
        {
            FD::EqBand a, b;
            a.freq = 400.0f; a.gainDb = 11.0f; a.resonance = 45.0f;
            b.freq = 5000.0f; b.gainDb = -9.0f; b.resonance = 45.0f;
            curve (main, w, 255, [&] (float x) { const float f = 20.0f * std::pow (1000.0f, x); return 0.5f + (FD::bandDb (1, a, f) + FD::bandDb (1, b, f)) / 36.0f; });
            break;
        }
        case synthyboy::kFxTypeCompressor:   // the transfer curve: input level to output level, bent above the threshold
        {
            const FD::CompBand band { -36.0f, -18.0f, 0.0f, 0.7f, 0.0f };
            reference();
            curve (main, w, 255, [&] (float x) { return (FD::compOut (band, -60.0f + 60.0f * x) + 60.0f) / 60.0f; });
            break;
        }
        case synthyboy::kFxTypeDistortion:   // the transfer curve of the waveshaper
            reference();
            curve (main, w, 255, [&] (float x) { return 0.5f + 0.5f * FD::shape (0, 12.0f, 2.0f * x - 1.0f); });
            break;
        case synthyboy::kFxTypeFilter:   // a resonant low pass
            curve (main, w, 255, [&] (float x)
            {
                const float r = 0.08f * std::pow (160.0f, x), q = 2.5f, a = 1.0f - r * r, h = 1.0f / std::sqrt (a * a + (r / q) * (r / q));
                return 0.62f + 20.0f * std::log10 (h) / 60.0f;
            });
            break;
        case synthyboy::kFxTypeFlanger:   // the comb of a short delay mixed with the dry signal
            curve (main, w, 255, [&] (float x) { const float g = 0.8f; return 0.5f + 10.0f * std::log10 (1.0f + g * g + 2.0f * g * std::cos (2.0f * kPi * 5.0f * x)) / 24.0f; });
            break;
        case synthyboy::kFxTypePhaser:   // a few deep notches
            curve (main, w, 255, [&] (float x)
            {
                float d = 0.0f;
                for (float c : { 0.26f, 0.5f, 0.74f })
                    d += std::exp (-((x - c) / 0.05f) * ((x - c) / 0.05f));
                return 0.82f - 0.7f * std::min (1.0f, d);
            });
            break;
        case synthyboy::kFxTypeChorus:   // the voice and its detuned, delayed copy
            curve (second, w, 255, [] (float x) { return 0.5f + 0.28f * std::sin (2.0f * kPi * 2.0f * x - 1.0f); });
            curve (main, w, 255, [] (float x) { return 0.5f + 0.28f * std::sin (2.0f * kPi * 2.0f * x); });
            break;
        case synthyboy::kFxTypeHyperDimension:   // the fan of detuned voices
        {
            const float detune[5] = { 1.86f, 1.93f, 2.07f, 2.14f, 2.0f };
            for (int k = 0; k < 5; ++k)
                curve (k == 4 ? main : second, k == 4 ? w : w * 0.7f, k == 4 ? 255 : 170, [&] (float x) { return 0.5f + 0.2f * std::sin (2.0f * kPi * detune[k] * x); });
            break;
        }
        case synthyboy::kFxTypeDelay:   // the echoes: each one quieter than the one before
        {
            const float at[5] = { 0.10f, 0.34f, 0.56f, 0.76f, 0.92f }, h[5] = { 0.92f, 0.6f, 0.4f, 0.26f, 0.17f };
            for (int k = 0; k < 5; ++k)
                bar (at[k], h[k], k == 0 ? second : main, 255);
            break;
        }
        case synthyboy::kFxTypeReverb:   // the diffuse tail: noise that dies away
            curve (main, w * 0.7f, 255, [] (float x)
            {
                const int i = static_cast<int> (x * 63.0f + 0.5f);
                float n = std::sin (static_cast<float> (i) * 12.9898f) * 43758.5453f;
                n = (n - std::floor (n)) * 2.0f - 1.0f;
                return 0.5f + 0.46f * std::exp (-3.4f * x) * n;
            });
            break;
        case synthyboy::kFxTypeConvolve:   // the impulse response that is convolved with the signal
            curve (main, w, 255, [] (float x) { return 0.5f + 0.4f * std::exp (-4.5f * x) * std::sin (2.0f * kPi * 9.0f * x + 1.5f); });
            break;
        case synthyboy::kFxTypeUtility:   // gain: a straight line steeper than the identity, cut at the ends
            reference();
            curve (main, w, 255, [] (float x) { return 0.5f + (x - 0.5f) * 1.5f; });
            break;
        case synthyboy::kFxTypeBode:   // the lines of the spectrum and the same lines shifted up
        {
            const float at[4] = { 0.08f, 0.20f, 0.32f, 0.44f }, h[4] = { 0.85f, 0.62f, 0.46f, 0.30f };
            for (int k = 0; k < 4; ++k)
            {
                bar (at[k], h[k], second, 200);
                bar (at[k] + 0.30f, h[k], main, 255);
            }
            break;
        }
        case synthyboy::kNumFxInstanceTypes:   // splitter Low / High: the two bands side by side
        case synthyboy::kNumFxInstanceTypes + 1:   // Low / Mid / High
        {
            const int zones = type == synthyboy::kNumFxInstanceTypes ? 2 : 3;
            const int zw = (in.w - (zones - 1) * 8) / zones;
            for (int z = 0; z < zones; ++z)
            {
                const Rect r { in.x + z * (zw + 8), in.y + in.h / 4, zw, in.h / 2 };
                const uint32_t col = z == 0 ? main : (z == zones - 1 ? T.flow[3] : second);
                p.rrect (r, 5.0f, p.c (col, 70));
                p.rrect (r, 5.0f, p.c (col, 255), 1.4f);
            }
            break;
        }
        case synthyboy::kNumFxInstanceTypes + 2:   // splitter Mid / Side: two lanes
        {
            const Rect a { in.x, in.y + in.h / 8, in.w, in.h / 3 }, b { in.x, in.y + in.h * 9 / 16, in.w, in.h / 3 };
            p.rrect (a, 5.0f, p.c (main, 70));
            p.rrect (a, 5.0f, p.c (main, 255), 1.4f);
            p.rrect (b, 5.0f, p.c (T.flow[3], 70));
            p.rrect (b, 5.0f, p.c (T.flow[3], 255), 1.4f);
            break;
        }
        default:   // a type this build does not know: a flat line
            curve (main, w, 255, [] (float) { return 0.5f; });
            break;
    }
}

}}} // namespace synthyboy_mpc::ui::fxgallery
