// SynthyBoy - MPC UI component library: the BASE components (Knob, NumberField, Stepper, UpDownStepper, ToggleIcon, MenuButton, RouteButton). Each is a black box with the contract of
// core.h: a state struct and a rectangle in, a drawing and a change { id, value } out. What is behind it (the geometry, the gesture state) is private; the tests drive the interface.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** 1. Knob. In: label (<= 8 characters), value 0 .. 1, the live modulated value 0 .. 1 (a marker; equal to value when nothing modulates), bipolar (a detent mark at the centre, the arc runs
    from the centre), small (the small variant with a centre dot: the secondary RAND knob), the default value, and the modulation input of core.h: `mods` (up to 8 entries: source id, name,
    depth, polarity, bypassed) and `selectedSource`. Drawn: (1) the value pointer, (2) the live modulated value marker, (3) the depth arc of the selected source (when it is in `mods` and not
    bypassed: unidirectional from the value to value + depth, bidirectional from value - |depth| to value + |depth|, clamped; a negative depth in the darker accent shade), (4) the tab at the
    top left (coloured: the selected source is in `mods`; grey: `mods` is not empty but it is not in it; hollow: it is, but bypassed, and then no arc; absent: `mods` is empty).
    Out: Value changes (new value 0 .. 1); Depth changes { id, source id, new depth } when a drag starts on the tab (only when the selected source is in `mods`; 200 px = the whole -1 .. +1
    scale); ModsRequest on a long press (kLongPressMs without leaving the tap square, anywhere on the knob; time comes in with touchDown and tick). Gestures: a vertical drag (up = more; the
    whole range over kDragPx = 200 pixels, from the value at touch down, after the finger left the tap square), a double tap = the default value, turn (delta) adds delta. */
class Knob : public Component
{
public:
    struct State : ModInput
    {
        char label[9] = {};
        double value = 0.0, modValue = 0.0, defaultValue = 0.0;
        bool bipolar = false, small = false;
    };
    State state;
    static constexpr double kDragPx = 200.0, kLongPressMs = 600.0;
    static constexpr float kStartAngle = 0.75f * kPi, kSweep = 1.5f * kPi;   // 135 degrees (bottom left) clockwise over 270 degrees to the bottom right

    Knob() = default;
    explicit Knob (const char* label, bool small = false) { setText (state.label, sizeof state.label, label); state.small = small; }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return state.small ? Size { 72, 56 } : Size { 100, 72 }; }

    /** The geometry of the dial inside the rectangle (public so that the tests can find the markers). */
    struct Geom { float cx, cy, radius, stroke; Rect labelBox; int labelCell; };
    Geom geom() const noexcept
    {
        Geom g {};
        const int n = static_cast<int> (std::min<size_t> (std::strlen (state.label), 8));
        g.labelCell = n > 0 ? std::max (1, Pen::fitCell (n, rect_.w - 4, 14, 2)) : 0;
        const int labelH = n > 0 ? font::kGlyphH * g.labelCell + 6 : 0;
        const int dialH = rect_.h - labelH;
        float d = static_cast<float> (std::min (rect_.w - 8, dialH - 8));
        if (state.small)
            d *= 0.8f;
        d = std::min (d, 140.0f);
        d = std::max (d, 6.0f);
        g.stroke = std::max (3.0f, std::floor (d / 12.0f));
        g.radius = d * 0.5f - g.stroke * 0.5f - 2.0f;
        g.cx = static_cast<float> (rect_.x) + static_cast<float> (rect_.w) * 0.5f;
        g.cy = static_cast<float> (rect_.y) + static_cast<float> (dialH) * 0.5f;
        g.labelBox = { rect_.x + 2, rect_.bottom() - labelH + 2, rect_.w - 4, std::max (0, labelH - 4) };
        return g;
    }
    static float angleOf (double v) noexcept { return kStartAngle + static_cast<float> (std::clamp (v, 0.0, 1.0)) * kSweep; }
    /** The centre of the value marker and of the modulation marker (the pixel position the tests look at). */
    void valuePoint (float& x, float& y) const noexcept { point (state.value, 0.0f, x, y); }
    void modPoint (float& x, float& y) const noexcept { point (state.modValue, -(geom().stroke + 1.0f), x, y); }
    /** The live modulated marker is drawn when the modulated value differs from the value. */
    bool modulated() const noexcept { return std::fabs (state.modValue - state.value) > 1.0e-4; }
    TabState tab() const noexcept { return tabState (state); }
    Rect tabHit() const noexcept { return tabHitRect (rect_); }

protected:
    void point (double v, float radiusOffset, float& x, float& y) const noexcept
    {
        const Geom g = geom();
        const float a = angleOf (v);
        x = g.cx + (g.radius + radiusOffset) * std::cos (a);
        y = g.cy + (g.radius + radiusOffset) * std::sin (a);
    }

    void drawBody (Pen& p) const override
    {
        const Geom g = geom();
        const float cx = g.cx, cy = g.cy, R = g.radius, tw = g.stroke;
        const float a0 = angleOf (0.0), a1 = angleOf (1.0), av = angleOf (state.value), am = angleOf (0.5);
        p.arc (cx, cy, R, a0, a1, tw, p.c (colour::kRest, 255));
        p.arc (cx, cy, R, state.bipolar ? am : a0, av, tw, p.c (colour::kAccent, 255));
        if (state.bipolar)
            p.line (cx + (R - tw * 0.5f) * std::cos (am), cy + (R - tw * 0.5f) * std::sin (am), cx + (R + tw * 0.5f + 1.0f) * std::cos (am), cy + (R + tw * 0.5f + 1.0f) * std::sin (am), 2.0f, p.c (colour::kText, 220));
        const float rm = R - tw - 1.0f, wm = std::max (2.0f, tw * 0.6f);
        double from, to;
        if (depthArc (state, state.value, from, to))
            p.arc (cx, cy, rm, angleOf (from), angleOf (to), wm, state.selected()->depth < 0.0 ? p.c (colour::kAccentDark, 255) : p.c (colour::kText, 200));
        if (modulated())
        {
            float mx, my;
            modPoint (mx, my);
            p.disc (mx, my, std::max (2.0f, tw * 0.7f), p.c (colour::kText, 255));
        }
        p.line (cx + R * 0.25f * std::cos (av), cy + R * 0.25f * std::sin (av), cx + R * 0.6f * std::cos (av), cy + R * 0.6f * std::sin (av), 2.0f, p.c (colour::kText, 255));
        float vx, vy;
        valuePoint (vx, vy);
        p.disc (vx, vy, std::max (2.5f, tw * 0.9f), p.c (colour::kWhite, 255));
        if (state.small)
            p.disc (cx, cy, std::max (2.0f, R * 0.18f), p.c (colour::kAccentDark, 255));
        p.modTab (rect_, tab());
        if (g.labelCell > 0)
            p.textCentered (g.labelBox, state.label, g.labelCell, p.c (qFrame_ ? colour::kAccent : colour::kText, 255));
    }

    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        downMs_ = now;
        long_ = false;
        start_ = std::clamp (state.value, 0.0, 1.0);
        const ModEntry* e = state.selected();
        onTab_ = e != nullptr && tabHit().contains (x, y);
        startDepth_ = e ? e->depth : 0.0;
        return Change::none();
    }
    Change onMove (int, int y) override
    {
        if (long_ || ! tap_.move (tap_.startX(), y))   // only the vertical movement matters: a sideways slide is not a drag, a vertical one beyond the tap square is
            return Change::none();
        const double dy = static_cast<double> (tap_.startY() - y);
        if (onTab_)
        {
            const double d = std::clamp (startDepth_ + dy / kDragPx * 2.0, -1.0, 1.0);
            const ModEntry* e = state.selected();
            return e && std::fabs (d - e->depth) > 1.0e-12 ? Change::depth (id_, e->source, d) : Change::none();
        }
        return emit (start_ + dy / kDragPx);
    }
    Change onUp (int, int) override
    {
        if (long_)
        {
            tap_.reset();
            return Change::none();
        }
        return tap_.up() == 2 && ! onTab_ ? emit (state.defaultValue) : Change::none();
    }
    Change onTurn (double delta) override { return emit (state.value + delta); }

public:
    Change tick (double nowMs) override
    {
        if (! captured_ || ! enabled_ || long_ || tap_.moved() || nowMs - downMs_ < kLongPressMs)
            return Change::none();
        long_ = true;
        return Change::modsRequest (id_);
    }

private:
    Change emit (double v) const
    {
        v = std::clamp (v, 0.0, 1.0);
        return std::fabs (v - std::clamp (state.value, 0.0, 1.0)) < 1.0e-12 ? Change::none() : Change::normalised (id_, v);
    }
    TapGesture tap_;
    double start_ = 0.0, startDepth_ = 0.0, downMs_ = 0.0;
    bool onTab_ = false, long_ = false;
};

// ===============================================================================================================================================================
/** 2. NumberField. In: a label (<= 8 characters) OR an icon (phase, rand), value, min, max, step, format, the default value, notSet (drawn as "-"), readOnly (drawn normally, no reaction;
    the frame number). Format: IntSigned "+2" / "-3" / "0", Decimal2 "-64.00", Degrees "180^" (the glyph ^ is the degree sign), IntPlain "17".
    Out: Index changes: `index` = the number of steps from min (0 .. (max - min) / step), `value` = min + index * step. Gestures: a vertical drag (up = more; pxPerStep pixels per
    step, from the value at touch down, after the finger left the tap square), turn (delta) = delta steps (rounded), a double tap = the default. */
class NumberField : public Component
{
public:
    enum class Format : uint8_t { IntSigned, Decimal2, Degrees, IntPlain };
    enum class Icon : uint8_t { None, Phase, Rand };
    struct State : ModInput
    {
        char label[9] = {};
        Icon icon = Icon::None;
        double value = 0.0, min = 0.0, max = 1.0, step = 1.0, defaultValue = 0.0;
        Format format = Format::IntSigned;
        bool notSet = false, readOnly = false;
        int pxPerStep = 8;
    };
    State state;

    NumberField() = default;
    NumberField (const char* label, Format f) { setText (state.label, sizeof state.label, label); state.format = f; }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return { 80, 56 }; }

    /** The text of the value (the format, "-" when not set). */
    static void formatValue (const State& s, char out[16])
    {
        if (s.notSet)
        {
            std::snprintf (out, 16, "-");
            return;
        }
        const double v = s.value;
        switch (s.format)
        {
            case Format::IntSigned: { const long n = std::lround (v); if (n == 0) std::snprintf (out, 16, "0"); else std::snprintf (out, 16, "%+ld", n); break; }
            case Format::Decimal2: std::snprintf (out, 16, "%.2f", v == 0.0 ? 0.0 : v); break;
            case Format::Degrees: std::snprintf (out, 16, "%ld^", std::lround (v)); break;
            case Format::IntPlain: std::snprintf (out, 16, "%ld", std::lround (v)); break;
        }
    }
    int numSteps() const noexcept { return std::max (0, static_cast<int> (std::lround ((state.max - state.min) / stepSize()))); }
    int indexOf (double v) const noexcept { return std::clamp (static_cast<int> (std::lround ((v - state.min) / stepSize())), 0, numSteps()); }

protected:
    double stepSize() const noexcept { return state.step > 0.0 ? state.step : 1.0; }
    Rect box() const noexcept { return rect_.inset (2); }

    void drawBody (Pen& p) const override
    {
        const Rect b = box();
        p.rect (b, p.c (colour::kPanel, 255));
        p.frame (b, 1, p.c (qFrame_ ? colour::kAccent : colour::kRest, 255));
        const Rect in = b.inset (2);
        const bool head = state.label[0] != '\0' || state.icon != Icon::None;
        Rect valueBox = in;
        if (head)
        {
            const Rect headBox { in.x, in.y + 1, in.w, 16 };
            if (state.icon == Icon::None)
                p.textCentered (headBox, state.label, 2, p.c (qFrame_ ? colour::kAccent : colour::kText, 170));
            else if (state.label[0] == '\0')
                drawIcon (p, headBox);
            else
            {
                drawIcon (p, { headBox.x, headBox.y, 20, headBox.h });   // the icon and the label side by side
                p.textCentered ({ headBox.x + 20, headBox.y, std::max (0, headBox.w - 20), headBox.h }, state.label, 2, p.c (qFrame_ ? colour::kAccent : colour::kText, 170));
            }
            valueBox = { in.x, in.y + 17, in.w, std::max (0, in.h - 17) };
        }
        char t[16];
        formatValue (state, t);
        p.textCentered (valueBox, t, 3, p.c (colour::kWhite, 255));
        // the modulation: the tab, and an underline bar instead of an arc: the depth segment of the selected source around the position of the value on the bar
        p.modTab (rect_, tabState (state));
        double from, to;
        const double span = state.max - state.min, pos = span > 0.0 ? (state.value - state.min) / span : 0.0;
        if (depthArc (state, pos, from, to))
        {
            const Rect bar { in.x + 2, in.bottom() - 4, std::max (0, in.w - 4), 3 };
            p.rect (bar, p.c (colour::kRest, 255));
            const int x0 = bar.x + static_cast<int> (std::lround (from * bar.w)), x1 = bar.x + static_cast<int> (std::lround (to * bar.w));
            p.rect (x0, bar.y, std::max (x1, x0 + 1), bar.bottom(), state.selected()->depth < 0.0 ? p.c (colour::kAccentDark, 255) : p.c (colour::kText, 220));
        }
    }
    void drawIcon (Pen& p, const Rect& head) const
    {
        const float cx = static_cast<float> (head.x) + static_cast<float> (head.w) * 0.5f, cy = static_cast<float> (head.y) + static_cast<float> (head.h) * 0.5f;
        const uint32_t col = p.c (qFrame_ ? colour::kAccent : colour::kText, 200);
        if (state.icon == Icon::Phase)
        {
            p.arc (cx, cy, 5.0f, 0.0f, 2.0f * kPi, 2.0f, col);     // a circle with the pointer of the start phase
            p.line (cx, cy, cx + 4.0f, cy - 4.0f, 2.0f, col);
        }
        else
        {
            p.frame ({ static_cast<int> (cx) - 6, static_cast<int> (cy) - 6, 12, 12 }, 2, col);   // a die
            p.disc (cx - 2.5f, cy - 2.5f, 1.2f, col);
            p.disc (cx + 2.5f, cy + 2.5f, 1.2f, col);
        }
    }

    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        downMs_ = now;
        long_ = false;
        startIndex_ = indexOf (state.value);
        const ModEntry* e = state.selected();
        onTab_ = e != nullptr && tabHitRect (rect_).contains (x, y);
        startDepth_ = e ? e->depth : 0.0;
        return Change::none();
    }
    Change onMove (int, int y) override
    {
        if (long_ || ! tap_.move (tap_.startX(), y))
            return Change::none();
        if (onTab_)
        {
            const double d = std::clamp (startDepth_ + static_cast<double> (tap_.startY() - y) / 200.0 * 2.0, -1.0, 1.0);
            const ModEntry* e = state.selected();
            return e && std::fabs (d - e->depth) > 1.0e-12 ? Change::depth (id_, e->source, d) : Change::none();
        }
        if (state.readOnly)
            return Change::none();
        return emit (startIndex_ + static_cast<int> (std::lround (static_cast<double> (tap_.startY() - y) / std::max (1, state.pxPerStep))));
    }
    Change onUp (int, int) override
    {
        if (long_)
        {
            tap_.reset();
            return Change::none();
        }
        return tap_.up() == 2 && ! state.readOnly && ! onTab_ ? emit (indexOf (state.defaultValue)) : Change::none();
    }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        return state.readOnly || d == 0 ? Change::none() : emit (indexOf (state.value) + static_cast<int> (d));
    }

public:
    /** A long press (600 ms without leaving the tap square, time from touchDown and tick) asks for the modulations of the field. */
    Change tick (double nowMs) override
    {
        if (! captured_ || ! enabled_ || long_ || tap_.moved() || nowMs - downMs_ < 600.0)
            return Change::none();
        long_ = true;
        return Change::modsRequest (id_);
    }

private:
    Change emit (int idx) const
    {
        idx = std::clamp (idx, 0, numSteps());
        if (! state.notSet && idx == indexOf (state.value) && std::fabs (state.value - (state.min + idx * stepSize())) < 1.0e-9)
            return Change::none();
        return Change::indexed (id_, idx, state.min + idx * stepSize());
    }
    TapGesture tap_;
    int startIndex_ = 0;
    double startDepth_ = 0.0, downMs_ = 0.0;
    bool onTab_ = false, long_ = false;
};

// ===============================================================================================================================================================
/** 3. Stepper (prev / next). In: the list size, the current index, the text of the current item (<= 12 characters), dim (the item is drawn dim: "OFF"), wrap. Out: Index changes (the new
    index). Gestures: a tap on the left half = previous, on the right half = next (made on the touch up, from the half the touch went down in), turn (delta) steps (rounded). At an end
    without wrap nothing happens; with wrap the list goes round. */
class Stepper : public Component
{
public:
    struct State { int count = 0, index = 0; char text[13] = {}; bool dim = false, wrap = false; };
    State state;

    Size minSize() const override { return { kMinW, 56 }; }
    static constexpr int kMinW = 208;   // two 28 px zones, 12 characters at 2 pixels per font pixel (142 px) and the frame
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
        const Rect b = rect_.inset (2);
        p.rect (b, p.c (colour::kPanel, 255));
        p.frame (b, 1, p.c (qFrame_ ? colour::kAccent : colour::kRest, 255));
        const bool canPrev = state.count > 1 && (state.wrap || state.index > 0), canNext = state.count > 1 && (state.wrap || state.index < state.count - 1);
        const float cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f;
        p.chevron (static_cast<float> (b.x + kZone / 2), cy, 14.0f, 0, 3.0f, p.c (colour::kText, canPrev ? 255 : 70));
        p.chevron (static_cast<float> (b.right() - kZone / 2), cy, 14.0f, 1, 3.0f, p.c (colour::kText, canNext ? 255 : 70));
        p.textCentered ({ b.x + kZone, b.y + 2, std::max (0, b.w - 2 * kZone), std::max (0, b.h - 4) }, state.text, 3, p.c (colour::kWhite, state.dim ? 90 : 255));
    }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        prevHalf_ = x < rect_.x + rect_.w / 2;
        return Change::none();
    }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 ? step (prevHalf_ ? -1 : 1) : Change::none(); }
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
    bool prevHalf_ = false;
};

// ===============================================================================================================================================================
/** 4. UpDownStepper. In: an integer, min, max. Out: Index changes (index = the new integer, clamped). Gestures: a tap on the upper half = +1, on the lower half = -1 (on the touch up,
    from the half the touch went down in), turn (delta) adds delta (rounded). */
class UpDownStepper : public Component
{
public:
    struct State { int value = 0, min = 0, max = 0; };
    State state;

    Size minSize() const override { return { 72, 112 }; }
    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        if (max > n) out[n++] = { rect_.x, rect_.y, rect_.w, rect_.h / 2 };
        if (max > n) out[n++] = { rect_.x, rect_.y + rect_.h / 2, rect_.w, rect_.h - rect_.h / 2 };
        return n;
    }

protected:
    void drawBody (Pen& p) const override
    {
        const Rect b = rect_.inset (2);
        p.rect (b, p.c (colour::kPanel, 255));
        p.frame (b, 1, p.c (qFrame_ ? colour::kAccent : colour::kRest, 255));
        const int colW = std::max (28, b.w * 2 / 5);
        char t[16];
        std::snprintf (t, sizeof t, "%d", state.value);
        p.textCentered ({ b.x + 2, b.y + 2, std::max (0, b.w - colW - 2), std::max (0, b.h - 4) }, t, 3, p.c (colour::kWhite, 255));
        const float cx = static_cast<float> (b.right() - colW / 2);
        p.chevron (cx, static_cast<float> (b.y + b.h / 4), 14.0f, 2, 3.0f, p.c (colour::kText, state.value < state.max ? 255 : 70));
        p.chevron (cx, static_cast<float> (b.y + b.h * 3 / 4), 14.0f, 3, 3.0f, p.c (colour::kText, state.value > state.min ? 255 : 70));
        p.rect (b.right() - colW, b.y + b.h / 2, b.right() - 2, b.y + b.h / 2 + 1, p.c (colour::kRest, 255));
    }
    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        upper_ = y < rect_.y + rect_.h / 2;
        return Change::none();
    }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 ? step (upper_ ? 1 : -1) : Change::none(); }
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
    bool upper_ = true;
};

// ===============================================================================================================================================================
/** 5. ToggleIcon. In: the state on / off and the icon (Power, PitchTrack = the arc beside CRS, Generic). Out: an Index change 0 / 1 on a tap (made on the touch up); turn (delta > 0)
    switches on, (delta < 0) off. */
class ToggleIcon : public Component
{
public:
    enum class Icon : uint8_t { Power, PitchTrack, Generic };
    struct State { bool on = false; Icon icon = Icon::Generic; };
    State state;

    Size minSize() const override { return { 56, 56 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const Rect b = rect_.inset (4);
        p.frame (b, 1, p.c (qFrame_ ? colour::kAccent : colour::kRest, 255));
        const float cx = static_cast<float> (b.x) + static_cast<float> (b.w) * 0.5f, cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f;
        const float s = static_cast<float> (std::min (b.w, b.h)) * 0.32f;
        const uint32_t col = state.on ? p.c (colour::kAccent, 255) : p.c (colour::kText, 110);
        switch (state.icon)
        {
            case Icon::Power:
            {
                const float a0 = 1.5f * kPi + 0.6f;
                p.arc (cx, cy, s, a0, a0 + 2.0f * kPi - 1.2f, 3.0f, col);
                p.line (cx, cy - s - 1.0f, cx, cy - 1.0f, 3.0f, col);
                break;
            }
            case Icon::PitchTrack:
                p.arc (cx, cy + s * 0.5f, s * 1.1f, kPi, 2.0f * kPi, 3.0f, col);   // the arc over the top, from the left to the right
                p.disc (cx + s * 1.1f, cy + s * 0.5f, 3.0f, col);
                break;
            case Icon::Generic:
                if (state.on)
                    p.disc (cx, cy, s, col);
                else
                    p.arc (cx, cy, s, 0.0f, 2.0f * kPi, 3.0f, col);
                break;
        }
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
    enum class Icon : uint8_t { Down, Gear, None };   // None: the label only (BACK, CLOSE)
    struct State { char label[9] = {}; Icon icon = Icon::Down; };
    State state;

    MenuButton() = default;
    explicit MenuButton (const char* label, Icon icon = Icon::Down) { setText (state.label, sizeof state.label, label); state.icon = icon; }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }

    Size minSize() const override { return { 56, 56 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const Rect b = rect_.inset (3);
        p.rect (b, p.c (colour::kPanel, 255));
        p.frame (b, 1, p.c (qFrame_ ? colour::kAccent : colour::kRest, 255));
        Rect iconBox = b;
        if (state.label[0] != '\0')
        {
            const int iw = state.icon == Icon::None ? 0 : 28;
            iconBox = { b.right() - iw, b.y, iw, b.h };
            p.textCentered ({ b.x + 2, b.y + 2, std::max (0, b.w - iw - 2), std::max (0, b.h - 4) }, state.label, 2, p.c (qFrame_ ? colour::kAccent : colour::kText, 255));
        }
        if (state.icon == Icon::None)
            return;
        const float cx = static_cast<float> (iconBox.x) + static_cast<float> (iconBox.w) * 0.5f, cy = static_cast<float> (iconBox.y) + static_cast<float> (iconBox.h) * 0.5f;
        const uint32_t col = p.c (colour::kText, 255);
        if (state.icon == Icon::Down)
            p.chevron (cx, cy, 14.0f, 3, 3.0f, col);
        else
            p.iconGear (cx, cy, col);
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 ? Change::request (id_) : Change::none(); }

private:
    TapGesture tap_;
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
        char label[9] = {};
        Icon icon = Icon::None;
        bool hasValue = false, integer = false;
        char text[7] = {};
        double value = 0.0, min = 0.0, max = 1.0;
        int pxPerStep = 8;
    };
    State state;

    DetailButton() = default;
    explicit DetailButton (Icon icon, const char* label = "") { state.icon = icon; setText (state.label, sizeof state.label, label); }
    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }
    void setValueText (const char* s) { setText (state.text, sizeof state.text, s); }

    Size minSize() const override { return state.icon != Icon::None && state.label[0] != '\0' ? Size { 116, 56 } : Size { 72, 56 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const Rect b = rect_.inset (3);
        p.rect (b, p.c (colour::kPanel, 255));
        p.frame (b, 1, p.c (qFrame_ ? colour::kAccent : colour::kRest, 255));
        Rect rest = b.inset (2);
        if (state.icon != Icon::None)
        {
            const float cx = static_cast<float> (rest.x + 16), cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f;
            const uint32_t col = p.c (qFrame_ ? colour::kAccent : colour::kText, 255);
            if (state.icon == Icon::Gear) p.iconGear (cx, cy, col);
            else if (state.icon == Icon::Bell) p.iconBell (cx, cy, col);
            else if (state.icon == Icon::Envelope) p.iconEnvelope (cx, cy, col);
            else p.iconRoute (cx, cy, col);
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
            p.textCentered ({ rest.x, rest.y, rest.w, hh }, state.label, 2, p.c (qFrame_ ? colour::kAccent : colour::kText, 170));
            p.textCentered ({ rest.x, rest.y + hh, rest.w, rest.h - hh }, vt, 2, p.c (colour::kWhite, 255));
        }
        else if (lab)
            p.textCentered (rest, state.label, 2, p.c (qFrame_ ? colour::kAccent : colour::kText, 255));
        else if (val)
            p.textCentered (rest, vt, 3, p.c (colour::kWhite, 255));
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
    drag do nothing. The popup it opens is the RoutePanel (composite.h). */
class RouteButton : public DetailButton
{
public:
    struct State { char target[4] = {}; bool segment[2] = { false, false }; };
    State state;

    RouteButton() = default;
    explicit RouteButton (const char* target) { setText (state.target, sizeof state.target, target); }
    void setTarget (const char* s) { setText (state.target, sizeof state.target, s); }

    Size minSize() const override { return { 64, 64 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const Rect strip { rect_.x + 4, rect_.y + 3, rect_.w - 8, 8 };
        const int half = strip.w / 2;
        p.rect (strip.x, strip.y, strip.x + half - 2, strip.bottom(), p.c (state.segment[0] ? colour::kAccent : colour::kRest, 255));
        p.rect (strip.x + half + 2, strip.y, strip.right(), strip.bottom(), p.c (state.segment[1] ? colour::kAccent : colour::kRest, 255));
        const Rect b { rect_.x + 3, rect_.y + 14, rect_.w - 6, rect_.h - 17 };
        p.rect (b, p.c (colour::kPanel, 255));
        p.frame (b, 1, p.c (qFrame_ ? colour::kAccent : colour::kRest, 255));
        p.textCentered (b.inset (2), state.target, 3, p.c (colour::kWhite, 255));
    }
};

// ===============================================================================================================================================================
/** The pictograms of the PictogramSelector: a DATA table (id, name, draw function); a new pictogram is a new row. Every one is drawn with scene ops (own line drawings) around (cx, cy)
    inside a box of about 28 x 24 pixels. */
namespace pictogram
{
enum Id : int { Sine = 0, Wavetable, Granular, Multisample, Noise, HalfSine, Triangle, Saw, Square, Pulse, Count };

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
struct Def { int id; const char* name; void (*draw) (Pen&, float, float, uint32_t); };
constexpr Def kTable[] = {
    { Sine, "sine", drawSine }, { Wavetable, "wavetable", drawWavetable }, { Granular, "granular", drawGranular }, { Multisample, "multisample", drawMultisample }, { Noise, "noise", drawNoise },
    { HalfSine, "half sine", drawHalfSine }, { Triangle, "triangle", drawTriangle }, { Saw, "saw", drawSaw }, { Square, "square", drawSquare }, { Pulse, "pulse", drawPulse },
};
inline const Def* find (int id) noexcept
{
    for (const Def& d : kTable)
        if (d.id == id)
            return &d;
    return nullptr;
}
}

/** 14. PictogramSelector. In: up to 8 items, each with a pictogram id (pictogram:: table, own line drawings) and a label (<= 6 characters), an enabled flag per item, and the current index.
    Draw: one cell per item in a row (a column when the rectangle is taller than wide), the pictogram with the label under it, the current one highlighted, a disabled one dim.
    Out: an Index change (the new index) on a tap of an enabled cell (made on the touch up, from the cell the touch went down in); turn (delta) steps through the ENABLED items (no wrap). */
class PictogramSelector : public Component
{
public:
    static constexpr int kMaxItems = 8, kCellW = 76, kCellH = 56;   // kCellW: the cell for the longest label of 6 characters; shorter labels need less (cellMinW)
    struct Item { int pictogram = 0; char label[7] = {}; bool enabled = true; };
    struct State { int count = 0, index = 0; Item items[kMaxItems]; };
    State state;

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
        return std::max (56, font::textWidth (longest, 2) + 6);
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
        for (int i = 0; i < count(); ++i)
        {
            const Item& it = state.items[i];
            const Rect c = cell (i).inset (2);
            const bool cur = i == state.index;
            p.rect (c, p.c (colour::kPanel, 255));
            p.frame (c, 1, p.c (cur ? colour::kAccent : colour::kRest, 255));
            const uint32_t col = p.c (cur ? colour::kAccent : colour::kText, it.enabled ? (cur ? 255 : 200) : 60);
            const pictogram::Def* d = pictogram::find (it.pictogram);
            if (! d)   // no pictogram: a text cell
            {
                p.textCentered (c.inset (2), it.label, 3, p.c (cur ? colour::kAccent : colour::kText, it.enabled ? 255 : 60));
                continue;
            }
            const int labelH = it.label[0] ? 2 * font::kGlyphH + 4 : 0;
            d->draw (p, static_cast<float> (c.x) + static_cast<float> (c.w) * 0.5f, static_cast<float> (c.y) + static_cast<float> (c.h - labelH) * 0.5f, col);
            p.textCentered ({ c.x, c.bottom() - labelH, c.w, labelH }, it.label, 2, p.c (cur ? colour::kAccent : colour::kText, it.enabled ? 255 : 60));
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

private:
    TapGesture tap_;
    int downCell_ = -1;
};

}} // namespace synthyboy_mpc::ui
