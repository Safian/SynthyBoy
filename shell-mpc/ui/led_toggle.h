// SynthyBoy - MPC UI component library: LedToggle, a two-state control as ONE compact row: a small flat dot and the name. The state is the dot (lit = the accent colour, filled; unlit = a dim thin ring) and the
// brightness of the name; there is no ON / OFF word (the value text the Q proxy reports stays On / Off: that comes from the engine parameter). Same contract as every component (core.h).
// It is a ToggleIcon (same state: `on`, `label`; the owners that read and write a toggle keep working) with another drawing and another size: 56 px high, as wide as its name needs (96 px at least).
// In: on, label (23 characters). Out: an Index change 0 / 1 on a tap (made on the touch up); turn: up = on, down = off.
// Look: no box and no fill (the dot and the name on the ground of the block); the library palette and fonts only; a flat dot (no glow, no gradient halo), a thin crisp outline; the active Q member: the chip comes from the group, the name takes the Q colour (lit: full, unlit: dimmed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"

namespace synthyboy_mpc { namespace ui {

/** The geometry and colours the LED controls (LedToggle, LedList) share: one dot, one text size, one rule for the width a name needs. */
namespace led {
constexpr int kRowH = 56;        // the touch height of a row
constexpr int kMinToggleW = 96;
constexpr int kDotX = 11;        // the centre of the dot, from the left edge of the row
constexpr int kTextX = 25;       // the name starts here
constexpr int kPadR = 6;
constexpr float kDotR = 6.5f;    // the radius of the lit dot (the ring of an unlit one is a little smaller: the same outer size)
inline float nameSize() noexcept { return theme::L().fLabel + 3.0f; }
/** The width a name needs in a row (dot, gap, text, right padding). */
inline int widthFor (const char* name) noexcept { return kTextX + kPadR + 2 + static_cast<int> (std::ceil (Pen::textWidth (name, nameSize()))); }

/** The dot at (cx, cy): lit = a filled disc in the accent colour, unlit = a thin ring in the faint ink colour. Flat: no glow, no gradient. `dimA`: alpha 0 .. 255 of the whole dot (a disabled option). */
inline void dot (Pen& p, float cx, float cy, bool lit, int dimA = 255, bool qRing = false)
{
    const theme::Look& T = theme::L();
    p.style (1.0f, true);
    if (lit)
        p.disc (cx, cy, kDotR, p.c (T.flow[0], dimA));
    else
        p.arc (cx, cy, kDotR - 0.8f, 0.0f, 2.0f * kPi, 1.6f, p.c (qRing ? T.q : T.textFaint, dimA));
    p.plain();
}
}

class LedToggle : public ToggleIcon
{
public:
    LedToggle() { state.icon = Icon::Generic; }
    /** The width a toggle with this name needs (at least 96 px). */
    static int widthFor (const char* name) noexcept { return std::max (led::kMinToggleW, led::widthFor (name)); }
    static constexpr int kBandW = 100;   // the width of a toggle that sits in the slot of a band of knobs
    Size minSize() const override { return { widthFor (state.label), led::kRowH }; }
    /** The height of the dot and the name in the rectangle, as a fraction of it (0.5: the middle). A toggle that is the member of a Q group takes the whole cell of its slot (the index chip stands at the cell's corner, in line with its
        neighbours) and draws itself at the height of the dials beside it by this. */
    float centre = 0.5f;

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        const Rect b = rect_;   // no box, no fill: the dot and the name on the ground of the block; the touch rectangle is invisible
        const float cy = static_cast<float> (b.y) + static_cast<float> (b.h) * centre;
        led::dot (p, static_cast<float> (b.x + led::kDotX), cy, state.on, 255, qFrame_);
        const uint32_t col = state.on ? p.c (qFrame_ ? T.q : T.text, 255) : p.c (T.textDim, 200);   // the Q colour marks the lit name; an unlit one keeps its dim ink and a Q coloured ring
        p.textLeft ({ b.x + led::kTextX, static_cast<int> (std::round (cy)) - led::kRowH / 2, std::max (0, b.w - led::kTextX - led::kPadR), led::kRowH }, state.label, led::nameSize(), col, T.fMin);
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override { return tap_.up() != 0 ? flip() : Change::none(); }
    /** The ToggleIcon contract: a step up switches on, a step down off, nothing when it already is. */
    Change onTurn (double delta) override
    {
        if (delta > 0.0)
            return state.on ? Change::none() : flip();
        if (delta < 0.0)
            return state.on ? flip() : Change::none();
        return Change::none();
    }

private:
    Change flip() const { return Change::indexed (id_, state.on ? 0 : 1, state.on ? 0.0 : 1.0); }
    TapGesture tap_;
};

}} // namespace synthyboy_mpc::ui
