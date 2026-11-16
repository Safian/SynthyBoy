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
        // the scale beside the track: up to kMaxTicks marks (position 0 .. 1 along the track, a short text: "0", "-12", "50") the owner sets from the parameter's range and unit
        static constexpr int kMaxTicks = 6;
        int numTicks = 0;
        float tickPos[kMaxTicks] = {};
        char tickText[kMaxTicks][6] = {};
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
        int thumbW = 0, thumbH = 0, thumbY = 0, scaleX = 0, scaleW = 0;
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

        // from the left: the scale (its numbers), the track with the thumb, the level meter beside it (the thumb never covers it)
        g.meterW = state.hasMeter ? 8 : 0;
        const int contentW = rect_.w - 8;
        g.scaleW = state.numTicks > 0 ? 26 : 0;
        g.thumbW = std::clamp (contentW - g.scaleW - g.meterW - 8, 32, 56);
        g.thumbH = 26;
        const int used = g.scaleW + g.thumbW + (g.meterW > 0 ? 3 + g.meterW : 0), margin = std::max (0, (contentW - used) / 2);
        g.scaleX = rect_.x + 4 + margin;
        g.trackX = g.scaleX + g.scaleW + g.thumbW / 2;
        g.meterX = g.trackX + g.thumbW / 2 + 3;

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

        // Graduation: a tick and its number for every mark of the owner's scale (dB or %), the unity / default mark a little stronger
        for (int i = 0; i < state.numTicks; ++i)
        {
            const float ty = static_cast<float> (g.trackBottom) - std::clamp (state.tickPos[i], 0.0f, 1.0f) * static_cast<float> (g.trackH);
            const int tx1 = g.trackX - g.thumbW / 2 - 2;
            p.line (static_cast<float> (tx1 - 5), ty, static_cast<float> (tx1), ty, 1.0f, p.c (T.textDim, 170));
            p.textRight ({ g.scaleX, static_cast<int> (ty) - 7, g.scaleW - 7, 14 }, state.tickText[i], T.fSmall, p.c (T.textFaint, 255), T.fMin);
        }

        // 4. Integrated Peak Meter: 0 = -60 dBFS .. 1 = full scale; the rail has a line every 12 dB, and at rest (no signal) a dim base line, so it never stands there as an empty box
        if (state.hasMeter)
        {
            const Rect meterSlot { g.meterX, g.trackTop, g.meterW, g.trackH };
            p.rect (meterSlot, p.c (T.panel1, 255));
            p.rrect (meterSlot, 2.0f, p.c (T.keyline, 160), 1.0f);
            for (int k = 1; k <= 5; ++k)   // -48, -36, -24, -12, 0 dB
            {
                const float ly = static_cast<float> (g.trackBottom) - static_cast<float> (k) * 0.2f * static_cast<float> (g.trackH);
                p.line (static_cast<float> (g.meterX + g.meterW - 3), ly, static_cast<float> (g.meterX + g.meterW), ly, 1.0f, p.c (T.textDim, k == 5 ? 200 : 110));
            }

            const float pk = std::clamp (state.peak, 0.0f, 1.0f);
            if (pk <= 0.005f)
                p.rect ({ g.meterX + 1, g.trackBottom - 3, g.meterW - 2, 2 }, p.c (T.textFaint, 150));
            else
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
        p.rrect (tr, 4.0f, p.c (T.keylineHi, 255), 1.2f);   // (the one Q frame is the group's)
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
/** SliderBar: a horizontal slider in a cell of 56 pixels (a touch target): the name at the left and the value at the right on the top line, the track with its thumb under them. The PAN of a strip (both ways
    from the middle: `bipolar`) and the sends to the two buses (from the left).
    In: label ("PAN", "B1"), valueText (the owner formats it: "L 35", "40%"), value 0 .. 1, defaultValue, bipolar. Out: Value changes on a horizontal drag (the whole track is the whole range, relative to the
    value at the touch down), double tap = the default, turn adds. */
class SliderBar : public Component
{
public:
    struct State
    {
        char label[8] = {};
        char valueText[16] = {};
        double value = 0.0, defaultValue = 0.0;
        bool bipolar = false;
    };
    State state;

    SliderBar() = default;
    explicit SliderBar (const char* label, bool bipolar = false) { setText (state.label, sizeof state.label, label); state.bipolar = bipolar; }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return { 96, 56 }; }
    /** The track under the texts. */
    Rect track() const noexcept { return { rect_.x + 12, rect_.y + 34, std::max (0, rect_.w - 24), 14 }; }
    float xOf (double v) const noexcept { const Rect t = track(); return static_cast<float> (t.x) + static_cast<float> (std::clamp (v, 0.0, 1.0)) * static_cast<float> (std::max (1, t.w - 1)); }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (! enabled_ || ! rect_.contains (x, y))
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
        if (! tap_.move (x, y))
            return Change::none();
        const double w = static_cast<double> (std::max (1, track().w));
        return emit (downVal_ + static_cast<double> (x - downX_) / w);
    }
    Change touchUp (int, int) override
    {
        if (! captured_)
            return Change::none();
        captured_ = false;
        return tap_.up() == 2 ? emit (state.defaultValue) : Change::none();
    }
    Change turn (double delta) override { return enabled_ ? emit (state.value + delta) : Change::none(); }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (2);
        p.control (b, qFrame_);
        p.textLeft ({ b.x + 8, b.y + 3, b.w / 2 - 8, 22 }, state.label, T.fLabel, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
        p.textRight ({ b.x + b.w / 2, b.y + 3, b.w / 2 - 8, 22 }, state.valueText, T.fLabel, p.c (T.text, 255), T.fMin);
        const Rect t = track();
        const float cy = static_cast<float> (t.y) + static_cast<float> (t.h) * 0.5f, x0 = static_cast<float> (t.x), x1 = static_cast<float> (t.right());
        const float zero = state.bipolar ? 0.5f * (x0 + x1) : x0, xv = xOf (state.value);
        p.style (1.0f, true);
        p.line (x0, cy, x1, cy, 3.0f, p.c (T.track, 255));
        if (std::fabs (xv - zero) > 0.5f)
        {
            p.paintX (std::min (zero, xv), std::max (zero, xv) + 0.5f, T.flow, 255);
            p.line (std::min (zero, xv), cy, std::max (zero, xv), cy, 3.0f, 255);
            p.paintOff();
        }
        if (state.bipolar)
            p.line (zero, cy - 6.0f, zero, cy + 6.0f, 1.2f, p.c (T.textDim, 200));
        p.disc (xv, cy, 6.0f, p.c (T.text, 255));
        p.plain();
    }

private:
    Change emit (double v)
    {
        v = std::clamp (v, 0.0, 1.0);
        if (std::fabs (v - std::clamp (state.value, 0.0, 1.0)) < 1.0e-12)
            return Change::none();
        return Change::normalised (id_, v);
    }
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
    static constexpr int kCellH = 56;
    char title[16] = {};
    ToggleIcon power;                                    // the name of the strip and its switch in one cell
    MenuButton destButton { "MAIN", MenuButton::Icon::Down };
    SliderBar pan { "PAN", true };
    SliderBar send1 { "B1" };
    SliderBar send2 { "B2" };
    Fader fader { "LEVEL" };

    MixerStrip()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        addKid (power);
        addKid (destButton);
        addKid (pan);
        addKid (send1);
        addKid (send2);
        addKid (fader);
    }

    Size minSize() const override { return { 96, 360 }; }

    /** Which of the four Q members the strip has: a member whose parameter does not exist in the engine (the buses and the master have a level only) is left out, not drawn dim: it is not drawn, takes no touch,
        and is no member of the strip's Q group (MixerFullScreen::setStripMembers). The strip keeps its height and the others keep their places (the layout does not depend on it). */
    void setPresent (bool level, bool panPresent, bool send1Present, bool send2Present)
    {
        show (fader, level);
        show (pan, panPresent);
        show (send1, send1Present);
        show (send2, send2Present);
    }
    bool hasLevel() const noexcept { return shownKid (fader); }
    bool hasPan() const noexcept { return shownKid (pan); }
    bool hasSend1() const noexcept { return shownKid (send1); }
    bool hasSend2() const noexcept { return shownKid (send2); }

    void setTitle (const char* t)
    {
        setText (title, sizeof title, t);
        power.setLabel (t);
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
        const int pad = 4, innerW = rect_.w - 2 * pad, gap = 4;
        int y = rect_.y + pad;
        power.place ({ rect_.x + pad, y, innerW, kCellH });
        y += kCellH + gap;
        destButton.place ({ rect_.x + pad, y, innerW, kCellH });
        y += kCellH + gap;
        // the Q group of the strip lies under the destination, top down in the order of its numbers: LEVEL (the fader) 1, PAN 2, B1 3, B2 4; a lane of kQLane over every member for its index chip
        y += kQLane;
        const int bottomY = rect_.bottom() - pad - 3 * kCellH - 3 * kQLane;
        fader.place ({ rect_.x + pad, y, innerW, std::max (96, bottomY - y) });
        y = std::max (y + 96, bottomY) + kQLane;
        pan.place ({ rect_.x + pad, y, innerW, kCellH });
        y += kCellH + kQLane;
        send1.place ({ rect_.x + pad, y, innerW, kCellH });
        y += kCellH + kQLane;
        send2.place ({ rect_.x + pad, y, innerW, kCellH });
    }

    void drawUnder (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_, false);
        p.rrect (rect_, T.rBlock, p.c (T.keyline, 160), 1.0f);
    }

private:
    bool shownKid (const Component& c) const noexcept
    {
        for (int i = 0; i < numChildren(); ++i)
            if (child (i) == &c)
                return childShown (i);
        return false;
    }
};

}} // namespace synthyboy_mpc::ui
