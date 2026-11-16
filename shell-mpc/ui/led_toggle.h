// SynthyBoy - MPC UI component library: LedToggle, a two-state control as ONE compact row: a small flat dot and the name. The state is the dot (lit = the accent colour, filled; unlit = a dim thin ring) and the
// brightness of the name; there is no ON / OFF word (the value text the Q proxy reports stays On / Off: that comes from the engine parameter). Same contract as every component (core.h).
// It is a ToggleIcon (same state: `on`, `label`; the owners that read and write a toggle keep working) with another drawing and another size: 56 px high, as wide as its name needs (96 px at least).
// In: on, label (15 characters). Out: an Index change 0 / 1 on a tap (made on the touch up); a Q-Link step and a wheel notch FLIP the state (either direction).
// Look: the library palette and fonts only; a flat dot (no glow, no gradient halo), a thin crisp outline; the active Q member: the chip comes from the group, the name takes the Q colour (lit: full, unlit: dimmed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"

namespace synthyboy_mpc { namespace ui {

/** The geometry and colours the LED controls (LedToggle, LedList) share: one dot, one text size, one rule for the width a name needs. */
namespace led {
constexpr int kRowH = 56;        // the touch height of a row
constexpr int kMinToggleW = 96;
constexpr int kDotX = 20;        // the centre of the dot, from the left edge of the row
constexpr int kTextX = 38;       // the name starts here
constexpr int kPadR = 12;
constexpr float kDotR = 6.5f;    // the radius of the lit dot (the ring of an unlit one is a little smaller: the same outer size)
inline float nameSize() noexcept { return theme::L().fLabel + 4.0f; }
/** The width a name needs in a row (dot, gap, text, right padding). */
inline int widthFor (const char* name) noexcept { return kTextX + kPadR + 2 + static_cast<int> (std::ceil (Pen::textWidth (name, nameSize()))); }

/** The dot at (cx, cy): lit = a filled disc in the accent colour, unlit = a thin ring in the faint ink colour. Flat: no glow, no gradient. `dimA`: alpha 0 .. 255 of the whole dot (a disabled option). */
inline void dot (Pen& p, float cx, float cy, bool lit, int dimA = 255)
{
    const theme::Look& T = theme::L();
    p.style (1.0f, true);
    if (lit)
        p.disc (cx, cy, kDotR, p.c (T.flow[0], dimA));
    else
        p.arc (cx, cy, kDotR - 0.8f, 0.0f, 2.0f * kPi, 1.6f, p.c (T.textFaint, dimA));
    p.plain();
}
}

class LedToggle : public ToggleIcon
{
public:
    LedToggle() { state.icon = Icon::Generic; }
    /** The width a toggle with this name needs (at least 96 px). */
    static int widthFor (const char* name) noexcept { return std::max (led::kMinToggleW, led::widthFor (name)); }
    Size minSize() const override { return { widthFor (state.label), led::kRowH }; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_.inset (2);
        p.control (b, qFrame_);
        const float cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f;
        led::dot (p, static_cast<float> (b.x + led::kDotX - 2), cy, state.on);
        const uint32_t col = qFrame_ ? p.c (T.q, state.on ? 255 : 150) : p.c (state.on ? T.text : T.textDim, state.on ? 255 : 200);
        p.textLeft ({ b.x + led::kTextX - 2, b.y + 2, std::max (0, b.w - led::kTextX - led::kPadR + 2), std::max (0, b.h - 4) }, state.label, led::nameSize(), col, T.fMin);
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 ? flip() : Change::none(); }
    Change onTurn (double delta) override { return delta != 0.0 ? flip() : Change::none(); }

private:
    Change flip() const { return Change::indexed (id_, state.on ? 0 : 1, state.on ? 0.0 : 1.0); }
    TapGesture tap_;
};

}} // namespace synthyboy_mpc::ui
