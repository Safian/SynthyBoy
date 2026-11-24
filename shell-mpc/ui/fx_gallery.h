// SynthyBoy - MPC UI component library: FxGallery, the gallery of the effect types. It fills the page while the chain has no effect: a card per type the engine has, the name of the type and a small picture of
// what it does to the sound (the transfer curve of the compressor and of the distortion, the frequency response of the EQ, the filter, the flanger and the phaser, the echoes of the delay, the decaying
// tail of the reverb, the voices of the chorus and of the hyper dimension, the shifted lines of the frequency shifter, the impulse response of the convolver, the gain line of the utility). One tap on a card
// asks the owner to add that effect. The gallery knows nothing of the chain: the owner hands it the types (`setTypes`: the engine's type numbers and names) and gets the type number of the tapped card back.
// Same contract as every component (core.h): a state in (the types), a rectangle, a change out (`Index`: `index` = the type number of the card, `value` = the same as a double).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Layout: a one line headline, then the cards in two rows (the last row is centred), each card at least 56 pixels high and wide where the rectangle allows. The pictures are made of the scene's curves and
// lines in the colours of the look (no glow, nothing lit); the models behind the EQ, the compressor and the distortion are the ones of the effect page's displays (FxDisplay), so the picture is the
// shape the effect really has.
#pragma once

#include <cctype>
#include <cmath>

#include "core.h"
#include "fx_display.h"
#include "synthyboy/fx_types.h"

namespace synthyboy_mpc { namespace ui {

class FxGallery : public Component
{
public:
    static constexpr int kMaxTypes = 16, kHeadH = 44, kPad = 12, kGap = 10, kMaxCardH = 220;

    FxGallery() = default;

    /** The types the gallery offers: the engine's type numbers (synthyboy::FxInstanceType, 1 ..) and their names (shown in capitals). */
    void setTypes (const int* types, const char* const* names, int n)
    {
        n_ = std::clamp (n, 0, kMaxTypes);
        for (int i = 0; i < n_; ++i)
        {
            cards_[i].type = types[i];
            setText (cards_[i].name, sizeof cards_[i].name, names[i]);
            for (char* c = cards_[i].name; *c != '\0'; ++c)
                *c = static_cast<char> (std::toupper (static_cast<unsigned char> (*c)));
        }
        layout();
    }
    int numTypes() const noexcept { return n_; }
    void setNativeCards (bool on) noexcept
    {
        if (nativeCards_ != on)
        {
            nativeCards_ = on;
            layout();
        }
    }
    /** The line over the cards (the place the effect goes to: "TAP AN EFFECT TO ADD IT TO THE CHAIN" by default), and the note the page shows when there is no type to offer. */
    void setHeadline (const char* t) { setText (headline_, sizeof headline_, t != nullptr ? t : ""); }
    void setNote (const char* t) { setText (note_, sizeof note_, t != nullptr ? t : ""); }
    /** Does the gallery have something to show: cards, or a note. */
    bool hasContent() const noexcept { return n_ > 0 || note_[0] != '\0'; }
    int typeAt (int i) const noexcept { return i >= 0 && i < n_ ? cards_[i].type : 0; }
    const char* nameAt (int i) const noexcept { return i >= 0 && i < n_ ? cards_[i].name : ""; }
    Rect cardRect (int i) const noexcept { return i >= 0 && i < n_ ? cards_[i].r : Rect {}; }
    Rect cardRectOfType (int type) const noexcept
    {
        for (int i = 0; i < n_; ++i)
            if (cards_[i].type == type)
                return cards_[i].r;
        return {};
    }
    /** The rectangle of the picture of card i (inside the card, over its name). */
    Rect pictureRect (int i) const noexcept
    {
        const Rect r = cardRect (i);
        return { r.x + 14, r.y + 14, std::max (0, r.w - 28), std::max (0, r.h - 14 - 50) };
    }
    int numRows() const noexcept { return n_ > 0 ? 2 : 0; }

    Size minSize() const override { return { 700, kHeadH + 2 * 120 + kGap + kPad }; }
    void place (const Rect& r) override { rect_ = r; layout(); }
    int touchTargets (Rect* out, int max) const override
    {
        if (nativeCards_)
            return 0;
        int k = 0;
        for (int i = 0; i < n_ && k < max; ++i)
            out[k++] = cards_[i].r;
        return k;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        if (n_ == 0 && note_[0] == '\0')
            return;
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + kPad, rect_.y + 4, std::max (0, rect_.w - 2 * kPad), kHeadH - 8 }, n_ == 0 ? note_ : headline_, T.fLabel + 2.0f, p.c (T.textDim, 255), T.fMin);
        for (int i = 0; i < n_; ++i)
        {
            const Card& c = cards_[i];
            const bool down = pressed_ == i && captured_;
            if (! nativeCards_)
                p.block (c.r, down);
            if (down)
                p.rrect (c.r, T.rControl, p.c (T.flow[0], 255), 1.6f);
            const Rect plot = pictureRect (i);
            if (plot.w >= 24 && plot.h >= 24)
                drawPicture (p, sb, c.type, plot);
            if (! nativeCards_)
                p.textCentered ({ c.r.x + 6, c.r.bottom() - 42, std::max (0, c.r.w - 12), 34 }, c.name, T.fLabel + 2.0f, p.c (T.text, 255), T.fMin);
        }
    }

    /** The picture of an effect type inside `plot` (public: the tests draw it alone). */
    static void drawPicture (Pen& p, gpu::SceneBuilder& sb, int type, const Rect& plot)
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

protected:
    void drawBody (Pen&) const override {}
    Change onDown (int x, int y, double now) override
    {
        pressed_ = -1;
        if (nativeCards_)
            return Change::none();
        for (int i = 0; i < n_; ++i)
            if (cards_[i].r.contains (x, y))
                pressed_ = i;
        tap_.down (x, y, now);
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        tap_.move (x, y);
        return Change::none();
    }
    Change onUp (int x, int y) override
    {
        const int i = pressed_;
        pressed_ = -1;
        if (i < 0 || tap_.up() == 0 || ! cards_[i].r.contains (x, y))
            return Change::none();
        return Change::indexed (id_, cards_[i].type, static_cast<double> (cards_[i].type));
    }

private:
    struct Card
    {
        int type = 0;
        char name[28] = {};
        Rect r;
    };

    void layout()
    {
        if (rect_.empty() || n_ == 0)
            return;
        const int rows = 2, cols = ((nativeCards_ ? kMaxTypes : n_) + rows - 1) / rows;
        const int areaW = rect_.w - 2 * kPad, areaH = rect_.h - kHeadH - kPad;
        const int cw = std::max (56, (areaW - (cols - 1) * kGap) / cols);
        const int ch = std::clamp ((areaH - (rows - 1) * kGap) / rows, 56, kMaxCardH);
        const int y0 = rect_.y + kHeadH + std::max (0, (areaH - rows * ch - (rows - 1) * kGap) / 2);
        if (nativeCards_)
        {
            const int x0 = rect_.x + (rect_.w - cols * cw - (cols - 1) * kGap) / 2;
            for (int i = 0; i < n_; ++i)
            {
                const int type = cards_[i].type - 1;
                cards_[i].r = { x0 + (type % cols) * (cw + kGap), y0 + (type / cols) * (ch + kGap), cw, ch };
            }
            return;
        }
        for (int r = 0; r < rows; ++r)
        {
            const int first = r * cols, count = std::min (cols, n_ - first);
            if (count <= 0)
                break;
            const int rowW = count * cw + (count - 1) * kGap;
            int x = rect_.x + kPad + (areaW - rowW) / 2;   // the last row is centred
            for (int k = 0; k < count; ++k)
            {
                cards_[first + k].r = { x, y0 + r * (ch + kGap), cw, ch };
                x += cw + kGap;
            }
        }
    }

    Card cards_[kMaxTypes];
    int n_ = 0;
    bool nativeCards_ = false;
    char headline_[96] = "TAP AN EFFECT TO ADD IT TO THE CHAIN", note_[96] = {};
    int pressed_ = -1;
    TapGesture tap_;
};

}} // namespace synthyboy_mpc::ui
