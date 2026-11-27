// SynthyBoy - MPC UI component library: the BASE components (Knob, NumberField, Stepper, UpDownStepper, ToggleIcon, MenuButton, RouteButton). Each is a black box with the contract of
// core.h: a state struct and a rectangle in, a drawing and a change { id, value } out. What is behind it (the geometry, the gesture state) is private; the tests drive the interface.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "content_name.h"
#include "core.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
// Knob and NumberField are pure ControlModel types defined in model.h (no overlay draw or touch).
using Knob = KnobModel;
using NumberField = NumberFieldModel;

// ===============================================================================================================================================================
/** 3. Stepper (prev / next). In: the list size, the current index, the text of the current item (<= 12 characters), dim (the item is drawn dim: "OFF"), wrap. Out: Index changes (the new
    index). Gestures: a tap on the left half = previous, on the right half = next (made on the touch up, from the half the touch went down in), turn (delta) steps (rounded). At an end
    without wrap nothing happens; with wrap the list goes round. */
class Stepper : public Component
{
public:
    struct State { int count = 0, index = 0; char text[48] = {}; bool dim = false, wrap = false; char caption[16] = {}; };   // caption: the name of the value (MODE, STACK ...), small at the top; the item below it
    State state;

    Size minSize() const override { return { std::min (kMinW, minWidth), 56 }; }
    bool contentName = false;   // the text is the name of a piece of content (a file): too long for its box it loses its middle, not its end (content_name.h); the layout lint accepts that
    int minWidth = kMinW;   // a stepper in a grid of cells with short choices (SAME NOTE) may be narrower than the usual 208
    void setCaption (const char* s) { setText (state.caption, sizeof state.caption, s); }
    static constexpr int kMinW = 208;   // two 28 px zones, a text of about 14 characters at 22 pixels and the frame
    void setItem (const char* text) { setText (state.text, sizeof state.text, text); }

    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        if (max > n) out[n++] = { rect_.x, rect_.y, rect_.w / 2, rect_.h };
        if (max > n) out[n++] = { rect_.x + rect_.w / 2, rect_.y, rect_.w - rect_.w / 2, rect_.h };
        return n;
    }

protected:
    static constexpr int kZone = 28;
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (2);
        p.control (b, qFrame_);
        const bool canPrev = state.count > 1 && (state.wrap || state.index > 0), canNext = state.count > 1 && (state.wrap || state.index < state.count - 1);
        const float cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f;
        p.style (1.0f, true);
        p.chevron (static_cast<float> (b.x + kZone / 2), cy, 12.0f, 0, 2.0f, p.c (T.textDim, canPrev ? 255 : 60));   // one colour for both arrows: the colour of a label (the gradient means position and value only)
        p.chevron (static_cast<float> (b.right() - kZone / 2), cy, 12.0f, 1, 2.0f, p.c (T.textDim, canNext ? 255 : 60));
        p.plain();
        char fitted[96];
        float drawSize = T.fValue;   // the size the text is drawn at: the one the middle cut was made for (a text drawn larger would be cut again by the pen, at its end)
        auto shown = [&] (const Rect& zone) -> const char*
        {
            drawSize = T.fValue;
            if (! contentName)
                return state.text;
            const float size = Pen::fitSize (state.text, static_cast<float> (zone.w), static_cast<float> (zone.h), T.fValue, T.fMin);
            if (size <= 0.0f)
                return state.text;
            contentname::middleFit (state.text, size, static_cast<float> (zone.w), fitted, static_cast<int> (sizeof fitted));   // a name too long for its box loses its middle, never its end
            drawSize = size;
            return fitted;
        };
        if (state.caption[0] != '\0')
        {
            const Rect zone { b.x + kZone, b.y + 19, std::max (0, b.w - 2 * kZone), std::max (0, b.h - 22) };
            p.textCentered ({ b.x + kZone, b.y + 3, std::max (0, b.w - 2 * kZone), 17 }, state.caption, T.fLabel - 2.0f, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
            { const char* t = shown (zone); p.textCentered (zone, t, drawSize, p.c (T.text, state.dim ? 90 : 255), T.fMin); }
            return;
        }
        const Rect zone { b.x + kZone, b.y + 2, std::max (0, b.w - 2 * kZone), std::max (0, b.h - 4) };
        { const char* t = shown (zone); p.textCentered (zone, t, drawSize, p.c (T.text, state.dim ? 90 : 255), T.fMin); }
    }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        prevHalf_ = x < rect_.x + rect_.w / 2;
        centre_ = listable && x >= rect_.x + rect_.w / 4 && x < rect_.right() - rect_.w / 4;
        return Change::none();
    }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override
    {
        if (tap_.up() == 0)
            return Change::none();
        return centre_ ? Change::request (id_) : step (prevHalf_ ? -1 : 1);
    }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        return d == 0 ? Change::none() : step (static_cast<int> (std::clamp<long> (d, -100000, 100000)));
    }

private:
    Change step (int d) const
    {
        if (state.count <= 1)
            return Change::none();
        int n = state.index + d;
        if (state.wrap)
            n = ((n % state.count) + state.count) % state.count;
        else
            n = std::clamp (n, 0, state.count - 1);
        return n == state.index ? Change::none() : Change::indexed (id_, n, n);
    }
    TapGesture tap_;
    bool prevHalf_ = false, centre_ = false;

public:
    /** A tap on the middle half (the name) is a Request change (the owner opens the list of all choices, ListPopup); the outer quarters still step. */
    bool listable = false;
};

// ===============================================================================================================================================================
/** 4. UpDownStepper. In: an integer, min, max. Out: Index changes (index = the new integer, clamped). Gestures: a tap on the upper half = +1, on the lower half = -1 (on the touch up,
    from the half the touch went down in). Compact mode uses fixed down/up targets on the right. Turn (delta) adds delta (rounded). */
class UpDownStepper : public Component
{
public:
    struct State { int value = 0, min = 0, max = 0; char caption[12] = {}; };   // caption: the name of the value, small above the number
    State state;
    bool compact = false;

    Size minSize() const override { return compact ? Size { 192, 48 } : Size { 72, 112 }; }
    void setCaption (const char* s) { setText (state.caption, sizeof state.caption, s); }
    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        if (compact)
        {
            if (max > n) out[n++] = compactTarget (false);
            if (max > n) out[n++] = compactTarget (true);
            return n;
        }
        if (max > n) out[n++] = { rect_.x, rect_.y, rect_.w, rect_.h / 2 };
        if (max > n) out[n++] = { rect_.x, rect_.y + rect_.h / 2, rect_.w, rect_.h - rect_.h / 2 };
        return n;
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (2);
        p.control (b, qFrame_);
        char t[16];
        std::snprintf (t, sizeof t, "%d", state.value);
        if (compact)
        {
            const Rect down = compactTarget (false);
            const Rect up = compactTarget (true);
            const int left = b.x + 6;
            const int right = down.x - 6;
            const int valueWidth = std::min (32, std::max (0, right - left));
            const float cy = static_cast<float> (b.y + b.h / 2);
            const int minimum = std::min (state.min, state.max);
            const int maximum = std::max (state.min, state.max);
            if (state.caption[0] != '\0')
                p.textLeft ({ left, b.y, std::max (0, right - left - valueWidth - 4), b.h }, state.caption, 12.0f, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
            p.textRight ({ right - valueWidth, b.y, valueWidth, b.h }, t, T.fValue - 5.0f, p.c (T.text, 255), T.fMin);
            p.style (1.0f, true);
            if (state.value > minimum)
                p.chevron (static_cast<float> (down.x + down.w / 2), cy, 11.0f, 3, 2.0f, p.c (T.textDim, 255));
            if (state.value < maximum)
                p.chevron (static_cast<float> (up.x + up.w / 2), cy, 11.0f, 2, 2.0f, p.c (T.textDim, 255));
            p.plain();
            return;
        }

        const int colW = std::max (28, b.w * 2 / 5);
        if (state.caption[0] != '\0')
        {
            p.textCentered ({ b.x + 2, b.y + 4, std::max (0, b.w - colW - 2), 17 }, state.caption, T.fLabel - 2.0f, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
            p.textCentered ({ b.x + 2, b.y + 22, std::max (0, b.w - colW - 2), std::max (0, b.h - 26) }, t, T.fValue, p.c (T.text, 255), T.fMin);
        }
        else
            p.textCentered ({ b.x + 2, b.y + 2, std::max (0, b.w - colW - 2), std::max (0, b.h - 4) }, t, T.fValue, p.c (T.text, 255), T.fMin);
        const float cx = static_cast<float> (b.right() - colW / 2);
        p.style (1.0f, true);
        p.chevron (cx, static_cast<float> (b.y + b.h / 4), 12.0f, 2, 2.0f, p.c (T.textDim, state.value < state.max ? 255 : 60));
        p.chevron (cx, static_cast<float> (b.y + b.h * 3 / 4), 12.0f, 3, 2.0f, p.c (T.textDim, state.value > state.min ? 255 : 60));
        p.plain();
        p.rect (b.right() - colW, b.y + b.h / 2, b.right() - 6, b.y + b.h / 2 + 1, p.c (T.keyline, 255));
    }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        if (compact)
        {
            touchStep_ = 0;
            if (compactTarget (false).contains (x, y) && state.value > std::min (state.min, state.max))
                touchStep_ = -1;
            else if (compactTarget (true).contains (x, y) && state.value < std::max (state.min, state.max))
                touchStep_ = 1;
        }
        else
            touchStep_ = y < rect_.y + rect_.h / 2 ? 1 : -1;
        return Change::none();
    }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 && touchStep_ != 0 ? step (touchStep_) : Change::none(); }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        return d == 0 ? Change::none() : step (static_cast<int> (std::clamp<long> (d, -100000, 100000)));
    }

private:
    Change step (int d) const
    {
        const int n = std::clamp (state.value + d, std::min (state.min, state.max), std::max (state.min, state.max));
        return n == state.value ? Change::none() : Change::indexed (id_, n, n);
    }
    TapGesture tap_;
    int touchStep_ = 0;

    Rect compactTarget (bool increase) const noexcept
    {
        const int width = std::min (56, rect_.w / 2);
        return { rect_.right() - (increase ? width : 2 * width), rect_.y, width, rect_.h };
    }
};

// ===============================================================================================================================================================
/** 5. ToggleIcon. In: the state on / off and the icon (Power, PitchTrack = the arc beside CRS, Generic). Out: an Index change 0 / 1 on a tap (made on the touch up); turn (delta > 0)
    switches on, (delta < 0) off. */
class ToggleIcon : public Component
{
public:
    enum class Icon : uint8_t { Power, PitchTrack, Generic };
    struct State
    {
        bool on = false; Icon icon = Icon::Generic; char label[24] = {}; bool showState = false;   // label (<= 8 characters, optional): under the icon; showState: " ON" / " OFF" after it
        bool negativeOff = false;   // the power button of an oscillator: drawn in the theme's negative colour (red) while it is off, in the normal active colour while on
        bool knobLike = false;   // in a row of knobs: no box; the icon where the dial is, the name on the line of the knob names, ON / OFF on the line of the values (the cell of a knob, the names in one line)
    };
    State state;

    ToggleIcon() = default;
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }
    Size minSize() const override { return state.label[0] ? Size { 72, 56 } : Size { 56, 56 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (state.knobLike)
        {
            drawKnobLike (p);
            return;
        }
        const Rect b = rect_.inset (4);
        p.control (b, qFrame_, false, state.icon == Icon::Power && state.negativeOff && ! state.on);
        char lab[32];
        std::snprintf (lab, sizeof lab, "%s%s", state.label, state.showState ? (state.on ? " ON" : " OFF") : "");
        const bool wide = state.label[0] && b.w >= 2 * b.h && Pen::textWidth (lab, T.fValue - 4.0f) + static_cast<float> (b.h) + 22.0f <= static_cast<float> (b.w);   // a wide row (the tuning column) whose label fits beside the icon: the icon at the left, the label beside it; else the icon over the label
        const int labelH = state.label[0] && ! wide ? 18 : 0;
        float cx = static_cast<float> (b.x) + static_cast<float> (b.w) * 0.5f, cy = static_cast<float> (b.y) + static_cast<float> (b.h - labelH) * 0.5f;
        const float s = static_cast<float> (std::min (wide ? b.h : b.w, b.h - labelH)) * 0.32f;
        if (wide)
        {
            const int iconW = b.h;
            cx = static_cast<float> (b.x) + static_cast<float> (iconW) * 0.5f + 8.0f;
            p.textLeft ({ b.x + iconW + 14, b.y + 2, std::max (0, b.w - iconW - 20), std::max (0, b.h - 4) }, lab, T.fValue - 4.0f, p.c (qFrame_ ? T.q : (state.on ? T.text : T.textDim), 255), T.fMin);
        }
        if (labelH > 0)
            p.textCentered ({ b.x, b.bottom() - labelH, b.w, labelH }, lab, T.fLabel - 2.0f, p.c (qFrame_ ? T.q : (state.on ? T.text : T.textDim), 255), T.fMin);
        const uint32_t col = state.negativeOff ? (state.on ? p.c (T.lfo[0], 255) : p.c (T.negative, 255)) : (state.on ? p.c (T.flow[0], 255) : p.c (T.textFaint, 255));
        p.style (1.0f, true);
        switch (state.icon)
        {
            case Icon::Power:
            {
                const float a0 = 1.5f * kPi + 0.6f;
                p.arc (cx, cy, s, a0, a0 + 2.0f * kPi - 1.2f, 2.4f, col);
                p.line (cx, cy - s - 1.0f, cx, cy - 1.0f, 2.4f, col);
                break;
            }
            case Icon::PitchTrack:
                p.arc (cx, cy + s * 0.5f, s * 1.1f, kPi, 2.0f * kPi, 2.4f, col);   // the arc over the top, from the left to the right
                p.disc (cx + s * 1.1f, cy + s * 0.5f, 2.8f, col);
                break;
            case Icon::Generic:
                if (state.on)
                    p.disc (cx, cy, s * 0.9f, col);
                else
                    p.arc (cx, cy, s, 0.0f, 2.0f * kPi, 2.4f, col);
                break;
        }
        p.plain();
    }
    /** The knob-like variant: the same cell geometry as Knob::geom (the name above the value line, the dial area above them), so the names of a row of knobs and switches stand in one line. */
    void drawKnobLike (Pen& p) const
    {
        const theme::Look& T = theme::L();
        const float labelSize = state.label[0] != '\0' ? Pen::fitSize (state.label, static_cast<float> (rect_.w - 4), 20.0f, T.fLabel, T.fMin) : 0.0f;
        const int labelH = labelSize > 0.0f ? static_cast<int> (std::ceil (labelSize * 0.89f)) + 7 : 0, valueH = 20, dialH = rect_.h - labelH - valueH;
        const float d = std::clamp (static_cast<float> (std::min (rect_.w - 8, dialH - 8)), 6.0f, 140.0f);
        const float cx = static_cast<float> (rect_.x) + static_cast<float> (rect_.w) * 0.5f, cy = static_cast<float> (rect_.y) + static_cast<float> (dialH) * 0.5f, s = d * 0.15f;
        const uint32_t col = state.negativeOff ? (state.on ? p.c (T.lfo[0], 255) : p.c (T.negative, 255)) : (state.on ? p.c (T.flow[0], 255) : p.c (T.textFaint, 255));
        p.style (1.0f, true);
        if (state.icon == Icon::Power)
        {
            const float a0 = 1.5f * kPi + 0.6f;
            p.arc (cx, cy, s * 1.5f, a0, a0 + 2.0f * kPi - 1.2f, 2.4f, col);
            p.line (cx, cy - s * 1.5f - 1.0f, cx, cy - 1.0f, 2.4f, col);
        }
        else if (state.on)
            p.disc (cx, cy, s * 1.3f, col);
        else
            p.arc (cx, cy, s * 1.4f, 0.0f, 2.0f * kPi, 2.4f, col);
        p.plain();
        if (labelSize > 0.0f)
            p.textCentered ({ rect_.x + 2, rect_.bottom() - valueH - labelH + 2, rect_.w - 4, std::max (0, labelH - 4) }, state.label, labelSize, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
        p.textCentered ({ rect_.x + 2, rect_.bottom() - valueH, rect_.w - 4, valueH }, state.on ? "ON" : "OFF", T.fLabel, p.c (state.on ? T.text : T.textDim, 255), T.fMin);
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 ? set (! state.on) : Change::none(); }
    Change onTurn (double delta) override { return delta > 0.0 ? set (true) : (delta < 0.0 ? set (false) : Change::none()); }

private:
    Change set (bool on) const { return on == state.on ? Change::none() : Change::indexed (id_, on ? 1 : 0, on ? 1.0 : 0.0); }
    TapGesture tap_;
};

// ===============================================================================================================================================================
/** 6. MenuButton. In: nothing but the picture: an icon (Down arrow, Gear) and an optional label (<= 8 characters). Out: a Request change on a tap (the owner opens whatever it wants).
    turn does nothing. */
class MenuButton : public Component
{
public:
    enum class Icon : uint8_t { Down, Gear, None, Expand };   // None: the label only (BACK, CLOSE); Expand: four corner brackets (full screen)
    struct State { char label[24] = {}; Icon icon = Icon::Down; char value[24] = {}; };   // value (optional): the current choice, under the label
    State state;

    MenuButton() = default;
    void setValue (const char* s) { setText (state.value, sizeof state.value, s); }
    explicit MenuButton (const char* label, Icon icon = Icon::Down) { setText (state.label, sizeof state.label, label); state.icon = icon; }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return { 56, 56 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (3);
        p.control (b, qFrame_);
        Rect iconBox = b;
        if (state.label[0] != '\0')
        {
            const int iw = state.icon == Icon::None ? 0 : 28;
            iconBox = { b.right() - iw, b.y, iw, b.h };
            if (state.value[0] != '\0')
            {
                const int hh = b.h / 2;
                p.textCentered ({ b.x + 2, b.y + 4, std::max (0, b.w - iw - 2), hh - 4 }, state.label, T.fLabel - 2.0f, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
                p.textCentered ({ b.x + 2, b.y + hh, std::max (0, b.w - iw - 2), b.h - hh - 4 }, state.value, T.fLabel + 2.0f, p.c (T.text, 255), T.fMin);
            }
            else
                p.textCentered ({ b.x + 2, b.y + 2, std::max (0, b.w - iw - 2), std::max (0, b.h - 4) }, state.label, T.fLabel + 2.0f, p.c (qFrame_ ? T.q : T.text, 255), T.fMin);
        }
        if (state.icon == Icon::None)
            return;
        const float cx = static_cast<float> (iconBox.x) + static_cast<float> (iconBox.w) * 0.5f, cy = static_cast<float> (iconBox.y) + static_cast<float> (iconBox.h) * 0.5f;
        const uint32_t col = p.c (T.flow[0], 255);
        p.style (1.0f, true);
        if (state.icon == Icon::Down)
            p.chevron (cx, cy, 12.0f, 3, 2.2f, p.c (T.textDim, 255));
        else if (state.icon == Icon::Expand)
            p.iconExpand (cx, cy, col);
        else
            p.iconGear (cx, cy, col);
        p.plain();
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 ? Change::request (id_) : Change::none(); }

private:
    TapGesture tap_;
};

// ===============================================================================================================================================================
/** HBar: a horizontal bar for a value that runs both ways (the depth of a modulation): the track across the rectangle's width with the zero at its middle (a tick), the bar from the zero to the
    value in the colours of the look, the handle, and the value as text at the right end (`text`, formatted by the owner; empty = the percent of the bar). In: value 0 .. 1 (0.5 = zero), modValue (the
    live marker; equal to value when nothing modulates), defaultValue, bipolar. Out: Value changes (new value 0 .. 1). Gestures: a horizontal drag (the whole track = the whole range; relative to the
    value at touch down, after the finger left the tap square), a double tap = the default, turn (delta) adds delta. The touch target is the whole rectangle. */
class HBar : public Component
{
public:
    struct State
    {
        double value = 0.5, modValue = 0.5, defaultValue = 0.5;
        bool bipolar = true;
        char text[16] = {};
    };
    State state;
    static constexpr int kTextW = 96, kPad = 10;

    HBar() = default;
    Size minSize() const override { return { 200, 56 }; }
    double wheelUnit() const noexcept override { return 1.0 / 128.0; }
    void setValueText (const char* s) { setText (state.text, sizeof state.text, s); }
    /** The track (inside the rectangle, left of the text). */
    Rect track() const noexcept { return { rect_.x + kPad, rect_.y, std::max (0, rect_.w - 2 * kPad - kTextW), rect_.h }; }
    float xOf (double v) const noexcept { const Rect t = track(); return static_cast<float> (t.x) + static_cast<float> (std::clamp (v, 0.0, 1.0)) * static_cast<float> (std::max (1, t.w - 1)); }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (3);
        p.control (b, qFrame_);
        const Rect t = track();
        const float cy = static_cast<float> (t.y) + static_cast<float> (t.h) * 0.5f, x0 = static_cast<float> (t.x) + 8.0f, x1 = static_cast<float> (t.right()) - 8.0f;
        const float zero = state.bipolar ? 0.5f * (x0 + x1) : x0, xv = x0 + static_cast<float>(std::clamp (state.value, 0.0, 1.0)) * (x1 - x0);
        p.style (1.0f, true);
        p.line (x0, cy, x1, cy, 5.0f, p.c (T.track, 255));
        if (std::fabs (xv - zero) > 0.5f)
        {
            p.paintX (std::min (zero, xv), std::max (zero, xv) + 0.5f, T.flow, 255);
            p.line (std::min (zero, xv), cy, std::max (zero, xv), cy, 5.0f, 255);
            p.paintOff();
        }
        if (state.bipolar)
            p.line (zero, cy - 11.0f, zero, cy + 11.0f, 1.5f, p.c (T.textDim, 200));
        if (std::fabs (state.modValue - state.value) > 1.0e-4)
            p.disc (x0 + static_cast<float> (std::clamp (state.modValue, 0.0, 1.0)) * (x1 - x0), cy, 3.0f, p.c (T.text, 255));
        p.disc (xv, cy, 8.0f, p.c (T.text, 255));
        p.plain();
        char buf[16];
        const char* shown = state.text;
        if (shown[0] == '\0')
        {
            std::snprintf (buf, sizeof buf, "%+d%%", static_cast<int> (std::lround ((state.value - (state.bipolar ? 0.5 : 0.0)) * (state.bipolar ? 200.0 : 100.0))));
            shown = buf;
        }
        p.textRight ({ rect_.right() - kTextW - 2, rect_.y + 6, kTextW - 12, std::max (0, rect_.h - 12) }, shown, T.fValue, p.c (T.text, 255), T.fMin);
    }
    Change onDown (int x, int, double now) override { tap_.down (x, 0, now); x0_ = x; start_ = std::clamp (state.value, 0.0, 1.0); return Change::none(); }
    Change onMove (int x, int) override
    {
        if (! tap_.move (x, 0))
            return Change::none();
        const int w = std::max (1, track().w - 16);
        return emit (start_ + static_cast<double> (x - x0_) / static_cast<double> (w));
    }
    Change onUp (int, int) override { return tap_.up() == 2 ? emit (state.defaultValue) : Change::none(); }
    Change onTurn (double delta) override { return emit (state.value + delta); }

private:
    Change emit (double v) const
    {
        v = std::clamp (v, 0.0, 1.0);
        return std::fabs (v - std::clamp (state.value, 0.0, 1.0)) < 1.0e-12 ? Change::none() : Change::normalised (id_, v);
    }
    TapGesture tap_;
    int x0_ = 0;
    double start_ = 0.5;
};

// ===============================================================================================================================================================
/** DetailButton: a "summary + details" control (routing, unison settings, the grain window). In: an icon (Gear, Bell, Route) or a short label (<= 8), and optionally a main value shown on it:
    hasValue, `text` (<= 6 characters, formatted by the owner; empty = the number is shown), `value` (0 .. 1 when !integer, else the integer min .. max), pxPerStep.
    Out: a Value (normalised) or Index (integer) change of the MAIN VALUE on a vertical drag (beyond the tap square; 200 px for the whole range, pxPerStep per step) and on turn (delta), and
    a SEPARATE Request change ("details requested") on a tap (a touch down and up that never left the tap square). The component does not know whether a popup or a whole screen opens. */
class DetailButton : public Component
{
public:
    enum class Icon : uint8_t { None, Gear, Bell, Route, Envelope };
    struct State
    {
        char label[24] = {};
        Icon icon = Icon::None;
        bool hasValue = false, integer = false;
        char text[16] = {};
        double value = 0.0, min = 0.0, max = 1.0;
        int pxPerStep = 8;
    };
    State state;

    DetailButton() = default;
    explicit DetailButton (Icon icon, const char* label = "") { state.icon = icon; setText (state.label, sizeof state.label, label); }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }
    void setValueText (const char* s) { setText (state.text, sizeof state.text, s); }

    Size minSize() const override { return state.icon != Icon::None && state.label[0] != '\0' ? Size { 116, 56 } : Size { 72, 56 }; }
    double wheelUnit() const noexcept override { return state.hasValue && ! state.integer ? 1.0 / 128.0 : 1.0; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (3);
        p.control (b, qFrame_);
        Rect rest = b.inset (2);
        if (state.icon != Icon::None)
        {
            const float cx = static_cast<float> (rest.x + 16), cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f;
            const uint32_t col = p.c (qFrame_ ? T.q : T.flow[0], 255);
            p.style (1.0f, true);
            if (state.icon == Icon::Gear) p.iconGear (cx, cy, col);
            else if (state.icon == Icon::Bell) p.iconBell (cx, cy, col);
            else if (state.icon == Icon::Envelope) p.iconEnvelope (cx, cy, col);
            else p.iconRoute (cx, cy, col);
            p.plain();
            rest = { rest.x + 34, rest.y, std::max (0, rest.w - 34), rest.h };
        }
        char t[16];
        const char* vt = state.text;
        if (state.hasValue && state.text[0] == '\0')
        {
            std::snprintf (t, sizeof t, state.integer ? "%d" : "%d%%", state.integer ? static_cast<int> (std::lround (state.value)) : static_cast<int> (std::lround (state.value * 100.0)));
            vt = t;
        }
        const bool val = state.hasValue, lab = state.label[0] != '\0';
        if (lab && val)
        {
            const int hh = rest.h / 2;
            p.textCentered ({ rest.x, rest.y, rest.w, hh }, state.label, T.fLabel - 2.0f, p.c (qFrame_ ? T.q : T.textDim, 255), T.fMin);
            p.textCentered ({ rest.x, rest.y + hh, rest.w, rest.h - hh }, vt, T.fLabel + 2.0f, p.c (T.text, 255), T.fMin);
        }
        else if (lab)
            p.textCentered (rest, state.label, T.fLabel + 2.0f, p.c (qFrame_ ? T.q : T.text, 255), T.fMin);
        else if (val)
            p.textCentered (rest, vt, T.fValue, p.c (T.text, 255), T.fMin);
    }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        start_ = state.value;
        startIndex_ = idx (state.value);
        return Change::none();
    }
    Change onMove (int, int y) override
    {
        if (! tap_.move (tap_.startX(), y) || ! state.hasValue)
            return Change::none();
        const double dy = static_cast<double> (tap_.startY() - y);
        return state.integer ? emitIndex (startIndex_ + static_cast<int> (std::lround (dy / std::max (1, state.pxPerStep)))) : emitValue (start_ + dy / 200.0);
    }
    Change onUp (int, int) override { return tap_.up() != 0 ? Change::request (id_) : Change::none(); }
    Change onTurn (double delta) override
    {
        if (! state.hasValue)
            return Change::none();
        if (state.integer)
        {
            const long d = std::lround (delta);
            return d == 0 ? Change::none() : emitIndex (idx (state.value) + static_cast<int> (std::clamp<long> (d, -100000, 100000)));
        }
        return emitValue (state.value + delta);
    }

private:
    int idx (double v) const noexcept { return std::clamp (static_cast<int> (std::lround (v - state.min)), 0, std::max (0, static_cast<int> (std::lround (state.max - state.min)))); }
    Change emitValue (double v) const
    {
        v = std::clamp (v, 0.0, 1.0);
        return std::fabs (v - std::clamp (state.value, 0.0, 1.0)) < 1.0e-12 ? Change::none() : Change::normalised (id_, v);
    }
    Change emitIndex (int i) const
    {
        i = std::clamp (i, 0, std::max (0, static_cast<int> (std::lround (state.max - state.min))));
        return i == idx (state.value) && std::fabs (state.value - (state.min + i)) < 1.0e-9 ? Change::none() : Change::indexed (id_, i, state.min + i);
    }
    TapGesture tap_;
    double start_ = 0.0;
    int startIndex_ = 0;
};

// ===============================================================================================================================================================
/** 7. RouteButton. In: the target text (<= 3 characters: "M", "F1", "F2", "D", "-") and two indicator segments on / off (their meaning is the owner's: it is an open question of the
    specification, nothing is hard coded). Out: a Request change on a tap ("route panel requested"). It is a DetailButton without a main value (the gesture is the DetailButton's); turn and
    drag do nothing. Opens the routing controls. */
class RouteButton : public DetailButton
{
public:
    struct State { char target[8] = {}; bool segment[2] = { false, false }; };
    State state;
    bool compact = false;

    RouteButton() = default;
    explicit RouteButton (const char* target) { setText (state.target, sizeof state.target, target); }
    void setTarget (const char* s) { setText (state.target, sizeof state.target, s); }

    Size minSize() const override { return compact ? Size { 48, 48 } : Size { 64, 64 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (compact || rect_.h <= 48)
        {
            const Rect strip { rect_.x + 3, rect_.y + 2, rect_.w - 6, 6 };
            const int half = strip.w / 2;
            p.rrect ({ strip.x, strip.y, half - 1, strip.h }, 2.0f, p.c (state.segment[0] ? T.flow[0] : T.track, 255));
            p.rrect ({ strip.x + half + 1, strip.y, strip.w - half - 1, strip.h }, 2.0f, p.c (state.segment[1] ? T.flow[3] : T.track, 255));
            const Rect b { rect_.x + 2, rect_.y + 10, rect_.w - 4, rect_.h - 12 };
            p.control (b, qFrame_);
            p.textCentered (b.inset (1), state.target, T.fValue - 3.0f, p.c (T.text, 255), T.fMin);
            return;
        }
        const Rect strip { rect_.x + 4, rect_.y + 3, rect_.w - 8, 8 };
        const int half = strip.w / 2;
        p.rrect ({ strip.x, strip.y, half - 2, strip.h }, 3.0f, p.c (state.segment[0] ? T.flow[0] : T.track, 255));
        p.rrect ({ strip.x + half + 2, strip.y, strip.w - half - 2, strip.h }, 3.0f, p.c (state.segment[1] ? T.flow[3] : T.track, 255));
        const Rect b { rect_.x + 3, rect_.y + 14, rect_.w - 6, rect_.h - 17 };
        p.control (b, qFrame_);
        p.textCentered (b.inset (2), state.target, T.fValue, p.c (T.text, 255), T.fMin);
    }
};

// ===============================================================================================================================================================
/** The pictograms of the PictogramSelector: a DATA table (id, name, draw function); a new pictogram is a new row. Every one is drawn with scene ops (own line drawings) around (cx, cy)
    inside a box of about 28 x 24 pixels. */
namespace pictogram
{
enum Id : int { Sine = 0, Wavetable, Granular, Multisample, Noise, HalfSine, Triangle, Saw, Square, Pulse, Sample, Spectral, Count };

inline float waveAt (int kind, float t) noexcept   // t 0 .. 1 over one cycle
{
    return kind == 0 ? std::sin (2.0f * kPi * t) : (kind == 1 ? 2.0f * std::fabs (2.0f * (t - std::floor (t + 0.5f))) - 1.0f : std::sin (4.0f * kPi * t));
}
inline void polyline (Pen& p, float x0, float y0, float w, float amp, int n, int kind, float width, uint32_t col)
{
    float px = x0, py = y0 - amp * waveAt (kind, 0.0f);
    for (int i = 1; i <= n; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (n), x = x0 + w * t, y = y0 - amp * waveAt (kind, t);
        p.line (px, py, x, y, width, col);
        px = x; py = y;
    }
}
inline void drawSine (Pen& p, float cx, float cy, uint32_t col) { polyline (p, cx - 14.0f, cy, 28.0f, 9.0f, 10, 0, 2.0f, col); }
inline void drawWavetable (Pen& p, float cx, float cy, uint32_t col)
{
    for (int k = 0; k < 3; ++k)   // three stacked wave lines, the front one in front (lower)
        polyline (p, cx - 14.0f + 3.0f * static_cast<float> (2 - k), cy - 8.0f + 8.0f * static_cast<float> (k), 22.0f, 3.5f, 8, k, 2.0f, col);
}
inline void drawGranular (Pen& p, float cx, float cy, uint32_t col)
{
    float px = cx - 14.0f, py = cy + 4.0f;
    for (int i = 1; i <= 10; ++i)   // the bell curve over a row of short vertical grains
    {
        const float t = -1.0f + 0.2f * static_cast<float> (i), x = cx + 14.0f * t, y = cy + 4.0f - 14.0f * std::exp (-3.2f * t * t);
        p.line (px, py, x, y, 2.0f, col);
        px = x; py = y;
    }
    for (int i = 0; i < 7; ++i)
        p.line (cx - 12.0f + 4.0f * static_cast<float> (i), cy + 7.0f, cx - 12.0f + 4.0f * static_cast<float> (i), cy + 11.0f, 2.0f, col);
}
inline void drawMultisample (Pen& p, float cx, float cy, uint32_t col)
{
    p.frame ({ static_cast<int> (cx) - 14, static_cast<int> (cy) - 9, 28, 18 }, 2, col);   // piano keys: white key gaps and black keys
    for (int i = 1; i < 4; ++i)
        p.line (cx - 14.0f + 7.0f * static_cast<float> (i), cy - 1.0f, cx - 14.0f + 7.0f * static_cast<float> (i), cy + 8.0f, 1.0f, col);
    for (int i = 0; i < 3; ++i)
        p.rect (static_cast<int> (cx) - 10 + 7 * i, static_cast<int> (cy) - 8, static_cast<int> (cx) - 6 + 7 * i, static_cast<int> (cy) + 1, col);
}
inline void drawNoise (Pen& p, float cx, float cy, uint32_t col)
{
    static const float r[11] = { 0.1f, -0.7f, 0.5f, -0.2f, 0.9f, -0.9f, 0.3f, -0.5f, 0.8f, -0.3f, 0.2f };   // a fixed jagged line
    for (int i = 0; i < 10; ++i)
        p.line (cx - 14.0f + 2.8f * static_cast<float> (i), cy - 9.0f * r[i], cx - 14.0f + 2.8f * static_cast<float> (i + 1), cy - 9.0f * r[i + 1], 2.0f, col);
}
inline void shape (Pen& p, float cx, float cy, const float (*pts)[2], int n, uint32_t col)
{
    for (int i = 0; i + 1 < n; ++i)
        p.line (cx + pts[i][0], cy + pts[i][1], cx + pts[i + 1][0], cy + pts[i + 1][1], 2.0f, col);
}
inline void drawHalfSine (Pen& p, float cx, float cy, uint32_t col)   // the rounded / half wave: one hump, then flat
{
    float px = cx - 14.0f, py = cy + 7.0f;
    for (int i = 1; i <= 8; ++i)
    {
        const float t = static_cast<float> (i) / 8.0f, x = cx - 14.0f + 14.0f * t, y = cy + 7.0f - 14.0f * std::sin (kPi * t);
        p.line (px, py, x, y, 2.0f, col);
        px = x; py = y;
    }
    p.line (cx, cy + 7.0f, cx + 14.0f, cy + 7.0f, 2.0f, col);
}
inline void drawTriangle (Pen& p, float cx, float cy, uint32_t col) { static const float k[5][2] = { { -14, 0 }, { -7, -9 }, { 0, 0 }, { 7, 9 }, { 14, 0 } }; shape (p, cx, cy, k, 5, col); }
inline void drawSaw (Pen& p, float cx, float cy, uint32_t col) { static const float k[6][2] = { { -14, 8 }, { -1, -8 }, { -1, 8 }, { 1, 8 }, { 1, -8 }, { 14, 8 } }; shape (p, cx, cy, k, 4, col); p.line (cx + 1.0f, cy + 8.0f, cx + 14.0f, cy - 8.0f, 2.0f, col); }
inline void drawSquare (Pen& p, float cx, float cy, uint32_t col) { static const float k[6][2] = { { -14, 8 }, { -14, -8 }, { 0, -8 }, { 0, 8 }, { 14, 8 }, { 14, -8 } }; shape (p, cx, cy, k, 6, col); }
inline void drawPulse (Pen& p, float cx, float cy, uint32_t col) { static const float k[7][2] = { { -14, 8 }, { -8, 8 }, { -8, -8 }, { -2, -8 }, { -2, 8 }, { 14, 8 }, { 14, 8 } }; shape (p, cx, cy, k, 6, col); }
inline void drawSample (Pen& p, float cx, float cy, uint32_t col)   // a recorded waveform between a start and an end marker
{
    static const float h[8] = { 2.0f, 7.0f, -9.0f, 8.0f, -5.0f, 9.0f, -3.0f, 1.0f };
    for (int i = 0; i < 7; ++i)
        p.line (cx - 10.0f + 3.0f * static_cast<float> (i), cy + h[i], cx - 10.0f + 3.0f * static_cast<float> (i + 1), cy + h[i + 1], 2.0f, col);
    p.line (cx - 13.0f, cy - 10.0f, cx - 13.0f, cy + 10.0f, 2.0f, col);
    p.line (cx + 13.0f, cy - 10.0f, cx + 13.0f, cy + 10.0f, 2.0f, col);
}
inline void drawSpectral (Pen& p, float cx, float cy, uint32_t col)   // a row of spectrum bars
{
    static const float h[7] = { 6.0f, 12.0f, 18.0f, 9.0f, 14.0f, 5.0f, 8.0f };
    for (int i = 0; i < 7; ++i)
        p.line (cx - 12.0f + 4.0f * static_cast<float> (i), cy + 9.0f, cx - 12.0f + 4.0f * static_cast<float> (i), cy + 9.0f - h[i], 2.0f, col);
}
struct Def { int id; const char* name; void (*draw) (Pen&, float, float, uint32_t); };
constexpr Def kTable[] = {
    { Sine, "sine", drawSine }, { Wavetable, "wavetable", drawWavetable }, { Granular, "granular", drawGranular }, { Multisample, "multisample", drawMultisample }, { Noise, "noise", drawNoise },
    { HalfSine, "half sine", drawHalfSine }, { Triangle, "triangle", drawTriangle }, { Saw, "saw", drawSaw }, { Square, "square", drawSquare }, { Pulse, "pulse", drawPulse },
    { Sample, "sample", drawSample }, { Spectral, "spectral", drawSpectral },
};
inline const Def* find (int id) noexcept
{
    for (const Def& d : kTable)
        if (d.id == id)
            return &d;
    return nullptr;
}
}

/** 14. PictogramSelector. In: up to 10 items, each with a pictogram id (pictogram:: table, own line drawings) and a label (<= 6 characters), an enabled flag per item, and the current index.
    Draw: one cell per item in a row (a column when the rectangle is taller than wide), the pictogram with the label under it, the current one highlighted, a disabled one dim.
    Out: an Index change (the new index) on a tap of an enabled cell (made on the touch up, from the cell the touch went down in); turn (delta) steps through the ENABLED items (no wrap). */
class PictogramSelector : public Component
{
public:
    static constexpr int kMaxItems = 10, kCellW = 76, kCellH = 56;   // kCellW: the cell for the longest label of 6 characters; shorter labels need less (cellMinW)
    struct Item { int pictogram = 0; char label[16] = {}; bool enabled = true; };
    struct State { int count = 0, index = 0; Item items[kMaxItems]; };
    State state;
    int narrowestCell = 56;   // the least width of a cell (a bar of many short labels, like the instance numbers, sets it lower)

    void setItem (int i, int pictogramId, const char* label, bool enabled = true)
    {
        if (i < 0 || i >= kMaxItems)
            return;
        state.items[i].pictogram = pictogramId;
        setText (state.items[i].label, sizeof state.items[i].label, label);
        state.items[i].enabled = enabled;
        state.count = std::max (state.count, i + 1);
    }
    bool vertical() const noexcept { return rect_.h > rect_.w; }
    int count() const noexcept { return std::clamp (state.count, 0, kMaxItems); }
    /** The rectangle of cell i. */
    Rect cell (int i) const noexcept
    {
        const int n = std::max (1, count());
        if (vertical())
            return { rect_.x, rect_.y + rect_.h * i / n, rect_.w, rect_.h * (i + 1) / n - rect_.h * i / n };
        return { rect_.x + rect_.w * i / n, rect_.y, rect_.w * (i + 1) / n - rect_.w * i / n, rect_.h };
    }
    /** The least width of a cell: the longest label at 2 pixels per font pixel and the frame (at least 56). */
    int cellMinW() const noexcept
    {
        int longest = 0;
        for (int i = 0; i < count(); ++i)
            longest = std::max (longest, static_cast<int> (std::strlen (state.items[i].label)));
        return std::max (narrowestCell, static_cast<int> (std::ceil (sdf::textWidth ("MMMMMMMMMMMMMMMM", theme::L().fLabel - 2.0f) * static_cast<float> (longest) / 16.0f)) + 10);
    }
    Size minSize() const override
    {
        const int n = std::max (1, count());
        return vertical() ? Size { cellMinW(), n * kCellH } : Size { n * cellMinW(), kCellH };
    }
    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        for (int i = 0; i < count() && n < max; ++i)
            if (state.items[i].enabled)
                out[n++] = cell (i);
        return n;
    }

protected:
    void drawBody (Pen& p) const override
    {
        // the text cells (no pictogram) share ONE text size: the one the longest name needs to fit its cell (SUB A B C NOISE: the same size in every tab)
        float textSize = theme::L().fValue;
        for (int i = 0; i < count(); ++i)
            if (! pictogram::find (state.items[i].pictogram))
            {
                const Rect c = cell (i).inset (2).inset (2);
                textSize = std::min (textSize, Pen::fitSize (state.items[i].label, static_cast<float> (c.w), static_cast<float> (c.h), theme::L().fValue, theme::L().fMin));
            }
        for (int i = 0; i < count(); ++i)
        {
            const Item& it = state.items[i];
            const theme::Look& T = theme::L();
            const Rect c = cell (i).inset (2);
            const bool cur = i == state.index;
            p.control (c, false, cur);
            if (cur)
                p.rrect (c, T.rControl, p.c (T.flow[0], 255), 1.6f);
            const uint32_t col = p.c (cur ? T.flow[0] : T.textDim, it.enabled ? 255 : 60);
            const pictogram::Def* d = pictogram::find (it.pictogram);
            if (! d)   // no pictogram: a text cell
            {
                p.textCentered (c.inset (2), it.label, textSize, p.c (cur ? T.text : T.textDim, it.enabled ? 255 : 60), T.fMin);
                continue;
            }
            const int labelH = it.label[0] ? 18 : 0;
            p.style (1.0f, true);
            d->draw (p, static_cast<float> (c.x) + static_cast<float> (c.w) * 0.5f, static_cast<float> (c.y) + static_cast<float> (c.h - labelH) * 0.5f, col);
            p.plain();
            p.textCentered ({ c.x, c.bottom() - labelH, c.w, labelH }, it.label, T.fLabel - 2.0f, p.c (cur ? T.text : T.textDim, it.enabled ? 255 : 60), T.fMin);
        }
    }
    int cellAt (int x, int y) const noexcept
    {
        for (int i = 0; i < count(); ++i)
            if (cell (i).contains (x, y))
                return i;
        return -1;
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); downCell_ = cellAt (x, y); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override
    {
        if (tap_.up() == 0 || downCell_ < 0 || downCell_ >= count() || ! state.items[downCell_].enabled || downCell_ == state.index)
            return Change::none();
        return Change::indexed (id_, downCell_, downCell_);
    }
    Change onTurn (double delta) override
    {
        long d = std::lround (delta);
        if (d == 0)
            return Change::none();
        const int dir = d > 0 ? 1 : -1;
        int i = std::clamp (state.index, 0, std::max (0, count() - 1)), target = state.index;
        for (d = d > 0 ? d : -d; d > 0; --d)
        {
            int j = i + dir;
            while (j >= 0 && j < count() && ! state.items[j].enabled)
                j += dir;
            if (j < 0 || j >= count())
                break;
            i = target = j;
        }
        return target == state.index || count() == 0 ? Change::none() : Change::indexed (id_, target, target);
    }

protected:
    TapGesture tap_;
    int downCell_ = -1;
};

/** TypeSelector: a PictogramSelector (the same items, ids, Index change and turn) that can be a DROP-DOWN: with `dropdown` set it is ONE box that shows the current item (its small pictogram and its name, a chevron at the right); a tap is a Request
    with the id (the owner opens the list of the items, ListPopup with `icons`), a turn steps through the items as before. Without `dropdown` it is the row of cells (as used by the oscillator's full screen). */
class TypeSelector : public PictogramSelector
{
public:
    bool dropdown = false;
    Size minSize() const override { return dropdown ? Size { 140, 56 } : PictogramSelector::minSize(); }
    int touchTargets (Rect* out, int max) const override
    {
        if (! dropdown)
            return PictogramSelector::touchTargets (out, max);
        if (max > 0)
            out[0] = rect_;
        return max > 0 ? 1 : 0;
    }

protected:
    void drawBody (Pen& p) const override
    {
        if (! dropdown)
        {
            PictogramSelector::drawBody (p);
            return;
        }
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (2);
        p.control (b, qFrame_);
        const int i = std::clamp (state.index, 0, std::max (0, count() - 1));
        if (count() == 0)
            return;
        const Item& it = state.items[i];
        int textX = b.x + 10;
        if (const pictogram::Def* d = pictogram::find (it.pictogram))
        {
            p.style (1.0f, true);
            d->draw (p, static_cast<float> (b.x) + 32.0f, static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f, p.c (T.flow[0], 255));
            p.plain();
            textX = b.x + 56;
        }
        p.textCentered ({ textX, b.y + 2, std::max (0, b.right() - 36 - textX), std::max (0, b.h - 4) }, it.label, T.fValue, p.c (qFrame_ ? T.q : T.text, 255), T.fMin);
        p.style (1.0f, true);
        p.chevron (static_cast<float> (b.right() - 18), static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f, 12.0f, 3, 2.2f, p.c (T.textDim, 255));
        p.plain();
    }
    Change onUp (int x, int y) override
    {
        if (! dropdown)
            return PictogramSelector::onUp (x, y);
        return tap_.up() != 0 ? Change::request (id_) : Change::none();
    }
};

}} // namespace synthyboy_mpc::ui
