// SynthyBoy - MPC UI component library: the MIXER CHANNEL STRIP components (Fader, SendBar, MixerStrip).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"
#include "composite.h"
#include "core.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** Fader: Vertical level fader with integrated peak meter beside the track.
    In: label, normalised value 0 .. 1, valueText in dB, defaultValue, peak level 0 .. 1, peakHold level 0 .. 1.
    Out: Value changes (new value 0 .. 1) on vertical drag; double-tap resets to default. turn(delta) adds delta. */
class Fader : public Component
{
public:
    struct State
    {
        char label[16] = {};
        char valueText[16] = {};
        double value = 0.75, modValue = 0.75, defaultValue = 0.75;
        float peak = 0.0f, peakHold = 0.0f;
        bool hasMeter = true;
    };
    State state;
    static constexpr double kDragPx = 200.0;

    Fader() = default;
    explicit Fader (const char* label) { setText (state.label, sizeof state.label, label); }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return { 56, 120 }; }

    struct Geom
    {
        int trackX = 0, trackTop = 0, trackBottom = 0, trackH = 0;
        int meterX = 0, meterW = 0;
        int thumbW = 0, thumbH = 0, thumbY = 0;
        Rect labelBox, valueBox;
    };

    Geom geom() const noexcept
    {
        Geom g {};
        const int labelH = state.label[0] != '\0' ? 16 : 0;
        const int valueH = 18;
        g.labelBox = { rect_.x + 2, rect_.y + 2, rect_.w - 4, labelH };
        g.valueBox = { rect_.x + 2, rect_.bottom() - valueH, rect_.w - 4, valueH };

        const int top = rect_.y + labelH + 8;
        const int bottom = rect_.bottom() - valueH - 8;
        g.trackTop = top;
        g.trackBottom = bottom;
        g.trackH = std::max (10, bottom - top);

        g.meterW = state.hasMeter ? 8 : 0;
        const int contentW = rect_.w - 8;
        g.trackX = rect_.x + 4 + (contentW - g.meterW) / 2;
        g.meterX = g.trackX + 18;

        g.thumbW = std::clamp (contentW - g.meterW - 4, 36, 56);
        g.thumbH = 26;

        const double clampedVal = std::clamp (state.value, 0.0, 1.0);
        g.thumbY = static_cast<int> (g.trackBottom - clampedVal * g.trackH - g.thumbH * 0.5f);
        return g;
    }

    Rect thumbRect() const noexcept
    {
        const Geom g = geom();
        return { g.trackX - g.thumbW / 2, g.thumbY, g.thumbW, g.thumbH };
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (! enabled_)
            return Change::none();
        captured_ = true;
        dragCandidate_ = true;
        downX_ = x;
        downY_ = y;
        downVal_ = state.value;
        tap_.down (x, y, nowMs);
        return Change::none();
    }

    Change touchMove (int x, int y) override
    {
        if (! captured_ || ! enabled_)
            return Change::none();
        tap_.move (x, y);
        // Vertical drag: dragging upwards increases value
        const double delta = static_cast<double> (downY_ - y) / kDragPx;
        const double newVal = std::clamp (downVal_ + delta, 0.0, 1.0);
        if (std::fabs (newVal - state.value) > 1.0e-4)
        {
            state.value = newVal;
            return Change::normalised (id_, state.value);
        }
        return Change::none();
    }

    Change touchUp (int x, int y) override
    {
        if (! captured_)
            return Change::none();
        captured_ = false;
        const int tapCode = tap_.up();
        if (tapCode == 2)
        {
            state.value = state.defaultValue;
            return Change::normalised (id_, state.value);
        }
        (void) x; (void) y;
        return Change::none();
    }

    Change turn (double delta) override
    {
        if (! enabled_)
            return Change::none();
        const double newVal = std::clamp (state.value + delta, 0.0, 1.0);
        if (std::fabs (newVal - state.value) > 1.0e-4)
        {
            state.value = newVal;
            return Change::normalised (id_, state.value);
        }
        return Change::none();
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Geom g = geom();

        // 1. Label
        if (state.label[0] != '\0')
            p.textCentered (g.labelBox, state.label, T.fSmall, p.c (T.textDim, 200));

        // 2. Value text at bottom
        if (state.valueText[0] != '\0')
            p.textCentered (g.valueBox, state.valueText, T.fSmall, p.c (T.text, 240));

        // 3. Track line
        p.line (static_cast<float> (g.trackX), static_cast<float> (g.trackTop),
                static_cast<float> (g.trackX), static_cast<float> (g.trackBottom),
                2.0f, p.c (T.track, 200));

        // Graduation tick marks
        const float tickPositions[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (float pos : tickPositions)
        {
            const float ty = static_cast<float> (g.trackBottom - pos * g.trackH);
            const float tickLen = (pos == 0.75f || pos == 0.0f) ? 6.0f : 4.0f;
            p.line (static_cast<float> (g.trackX) - tickLen - 1.0f, ty,
                    static_cast<float> (g.trackX) - 2.0f, ty,
                    1.0f, p.c (T.textDim, (pos == 0.75f ? 220 : 120)));
        }

        // 4. Integrated Peak Meter
        if (state.hasMeter)
        {
            const Rect meterSlot { g.meterX, g.trackTop, g.meterW, g.trackH };
            p.rect (meterSlot, p.c (T.panel1, 255));
            p.rrect (meterSlot, 2.0f, p.c (T.keyline, 160), 1.0f);

            const float pk = std::clamp (state.peak, 0.0f, 1.0f);
            if (pk > 0.005f)
            {
                const int fillH = static_cast<int> (pk * static_cast<float> (g.trackH));
                const int fillY = g.trackBottom - fillH;

                // Threshold splits: 0..0.7 (green), 0.7..0.88 (yellow), 0.88..1.0 (red)
                const int greenTop = g.trackBottom - static_cast<int> (std::min (pk, 0.70f) * g.trackH);
                p.rect ({ g.meterX + 1, greenTop, g.meterW - 2, g.trackBottom - greenTop }, p.c (0x5cff9d, 220));

                if (pk > 0.70f)
                {
                    const int yellowTop = g.trackBottom - static_cast<int> (std::min (pk, 0.88f) * g.trackH);
                    p.rect ({ g.meterX + 1, yellowTop, g.meterW - 2, greenTop - yellowTop }, p.c (0xffd84a, 230));
                }
                if (pk > 0.88f)
                {
                    p.rect ({ g.meterX + 1, fillY, g.meterW - 2, (g.trackBottom - static_cast<int> (0.88f * g.trackH)) - fillY }, p.c (0xff4f4f, 240));
                }
            }

            // Peak hold line
            const float pkh = std::clamp (state.peakHold, 0.0f, 1.0f);
            if (pkh > 0.01f)
            {
                const float hy = static_cast<float> (g.trackBottom) - pkh * static_cast<float> (g.trackH);
                p.line (static_cast<float> (g.meterX), hy,
                        static_cast<float> (g.meterX + g.meterW), hy,
                        1.5f, p.c (0xffffff, 250));
            }
        }

        // 5. Thumb cap
        const Rect tr = thumbRect();
        p.block (tr, true);
        p.rrect (tr, 4.0f, p.c (qFrame_ ? T.q : T.keylineHi, 255), qFrame_ ? 2.0f : 1.2f);
        // Center index line on thumb
        const float midY = static_cast<float> (tr.y + tr.h / 2);
        p.line (static_cast<float> (tr.x + 4), midY,
                static_cast<float> (tr.right() - 4), midY,
                2.0f, p.c (qFrame_ ? T.q : T.text, 240));
    }

private:
    TapGesture tap_;
    bool captured_ = false, dragCandidate_ = false;
    int downX_ = 0, downY_ = 0;
    double downVal_ = 0.0;
};

// ===============================================================================================================================================================
/** SendBar: Horizontal mini-slider for Bus 1 and Bus 2 sends.
    In: label ("B1", "B2"), value 0 .. 1, valueText ("50%").
    Out: Value changes on horizontal drag; double tap resets to 0. */
class SendBar : public Component
{
public:
    struct State
    {
        char label[8] = {};
        char valueText[12] = {};
        double value = 0.0, defaultValue = 0.0;
    };
    State state;
    static constexpr double kDragPx = 140.0;

    SendBar() = default;
    explicit SendBar (const char* label) { setText (state.label, sizeof state.label, label); }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return { 56, 22 }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (! enabled_)
            return Change::none();
        captured_ = true;
        downX_ = x;
        downVal_ = state.value;
        tap_.down (x, y, nowMs);
        return Change::none();
    }

    Change touchMove (int x, int y) override
    {
        if (! captured_ || ! enabled_)
            return Change::none();
        tap_.move (x, y);
        const double delta = static_cast<double> (x - downX_) / kDragPx;
        const double newVal = std::clamp (downVal_ + delta, 0.0, 1.0);
        if (std::fabs (newVal - state.value) > 1.0e-4)
        {
            state.value = newVal;
            return Change::normalised (id_, state.value);
        }
        return Change::none();
    }

    Change touchUp (int x, int y) override
    {
        if (! captured_)
            return Change::none();
        captured_ = false;
        const int tapCode = tap_.up();
        if (tapCode == 2)
        {
            state.value = state.defaultValue;
            return Change::normalised (id_, state.value);
        }
        (void) x; (void) y;
        return Change::none();
    }

    Change turn (double delta) override
    {
        if (! enabled_)
            return Change::none();
        const double newVal = std::clamp (state.value + delta, 0.0, 1.0);
        if (std::fabs (newVal - state.value) > 1.0e-4)
        {
            state.value = newVal;
            return Change::normalised (id_, state.value);
        }
        return Change::none();
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, false);
        p.rrect (rect_, 3.0f, p.c (qFrame_ ? T.q : T.keyline, 200), qFrame_ ? 1.5f : 1.0f);

        // Progress fill
        const float val = static_cast<float> (std::clamp (state.value, 0.0, 1.0));
        if (val > 0.01f)
        {
            const int barW = static_cast<int> (val * static_cast<float> (rect_.w - 4));
            p.rect ({ rect_.x + 2, rect_.y + 2, barW, rect_.h - 4 }, p.c (T.flow[0], 160));
        }

        // Label on left
        p.text (static_cast<float> (rect_.x + 5), static_cast<float> (rect_.y + 15),
                T.fSmall, state.label, p.c (T.textDim, 220));

        // Value text on right
        if (state.valueText[0] != '\0')
        {
            const float vw = Pen::textWidth (state.valueText, T.fSmall);
            p.text (static_cast<float> (rect_.right() - 5) - vw, static_cast<float> (rect_.y + 15),
                    T.fSmall, state.valueText, p.c (T.text, 240));
        }
    }

private:
    TapGesture tap_;
    bool captured_ = false;
    int downX_ = 0;
    double downVal_ = 0.0;
};

// ===============================================================================================================================================================
/** MixerStrip: One complete channel strip of the Mixer.
    Components:
      Header: power toggle, destination button
      Q-Group column (adjacent, 0 offenders): pan knob, send1, send2, fader (with peak meter). */
class MixerStrip : public Composite
{
public:
    char title[16] = {};
    ToggleIcon power;
    MenuButton destButton { "MAIN", MenuButton::Icon::Down };
    Knob pan { "PAN", true };
    SendBar send1 { "B1" };
    SendBar send2 { "B2" };
    Fader fader { "LEVEL" };

    MixerStrip()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        pan.state.bipolar = true;
        pan.state.small = true;
        addKid (power);
        addKid (destButton);
        addKid (pan);
        addKid (send1);
        addKid (send2);
        addKid (fader);
    }

    Size minSize() const override { return { 56, 240 }; }

    void setTitle (const char* t)
    {
        setText (title, sizeof title, t);
    }

    void assignIds (int baseId)
    {
        power.setId (baseId + 0);
        destButton.setId (baseId + 1);
        pan.setId (baseId + 2);
        send1.setId (baseId + 3);
        send2.setId (baseId + 4);
        fader.setId (baseId + 5);
    }

protected:
    void layout() override
    {
        const int pad = 4;
        const int innerW = rect_.w - 2 * pad;
        int y = rect_.y + 4;

        // Row 1: Header - Title (drawn) and Power button (32x32, right-aligned)
        const int powerW = 32, powerH = 32;
        power.place ({ rect_.right() - pad - powerW, y, powerW, powerH });
        y += powerH + 4;

        // Row 2: Destination button
        const int destH = 30;
        destButton.place ({ rect_.x + pad, y, innerW, destH });
        y += destH + 6;

        // Q-Group Area starts here: Pan -> Send1 -> Send2 -> Fader
        // Row 3: Pan knob
        const int panH = 68;
        pan.place ({ rect_.x + pad, y, innerW, panH });
        y += panH + 4;

        // Row 4: Send 1 bar
        const int sendH = 24;
        send1.place ({ rect_.x + pad, y, innerW, sendH });
        y += sendH + 4;

        // Row 5: Send 2 bar
        send2.place ({ rect_.x + pad, y, innerW, sendH });
        y += sendH + 6;

        // Row 6: Fader with integrated peak meter
        const int faderH = std::max (60, rect_.bottom() - y - 4);
        fader.place ({ rect_.x + pad, y, innerW, faderH });
    }

    void drawUnder (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, false);
        p.rrect (rect_, 6.0f, p.c (T.keyline, 160), 1.0f);

        // Strip Title at top left
        p.text (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + 24),
                T.fItem, title, p.c (T.text, 255));
    }
};

}} // namespace synthyboy_mpc::ui
