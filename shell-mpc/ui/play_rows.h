// SynthyBoy - MPC plugin-drawn layer: the ROWS of the three thirds of the Play pages (oscillator, envelope, LFO), ONE table so that the three displays (the wave, the envelope graph, the LFO curve) have the same top and the same
// height and the controls below them start on one line (owner request). Every third has, top to bottom: the selector row (the one selector component, kSelectorH), the display (displayRect), and below it the
// block of the controls that starts at controlsLine (the lane of the Q index chips lies above it). The blocks below are as tall as the tallest of the thirds needs (kControlsH: the LFO's SHAPE, RATE and the two knobs).
// The display has a HEADER BAND at its top (bandRect, kBandH): the buttons of the display stand in it (power: powerRect); the PLOT AREA (plotRect: grid,
// curve, fill, handles, cursor) is the display below the band with the same inner padding in all three thirds, so no curve ever runs under a button and the three plots have the same top, bottom and height.
// Pure constants and functions of a rectangle (the third), no dependencies but core.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core.h"

namespace synthyboy_mpc { namespace ui { namespace playrows {

constexpr int kMargin = 6;                 // the margin of a third at its sides, top and bottom
constexpr int kSelectorH = 56;             // the selector row: the shared selector (a dropdown with its list) in every third; in the oscillator third it is the header row (power, selector, route)
constexpr int kSelectorGap = 8;            // between the selector row and the display
constexpr int kControlGap = 8;             // the gap between two rows and two columns of the controls of a third (Play phase 2: one 8 px grid)
constexpr int kControlsH = 196;            // from the controls line to the bottom margin: two rows of 94 px (dial rows, 56 px dial) and the 8 px gap, or the LFO's four 60 px rows with three 8 px gaps (the lane of the Q chips lies above the line)
constexpr int kDisplayGap = kQLane + 4;    // between the bottom of the display and the controls line: the lane of the Q index chips of the first members and a gap

/** The display rectangle of a third: the same top (below the selector row) and the same height in all three thirds of the same height. */
inline Rect displayRect (const Rect& third) noexcept
{
    const int y = third.y + kMargin + kSelectorH + kSelectorGap;
    const int bottom = third.bottom() - kMargin - kControlsH - kDisplayGap;
    return { third.x + kMargin, y, std::max (0, third.w - 2 * kMargin), std::max (0, bottom - y) };
}
// ---- the header band of a display: one height for the three thirds
constexpr int kBandInset = 4;              // the buttons' distance from the top and the sides of the display
constexpr int kBandRowH = 64;              // the row of the buttons: the route button is 64 x 64 (minimum size); the 56 px power button is centred in the row
constexpr int kBandH = kBandInset + kBandRowH + kBandInset;   // the band: the inset above the buttons, the row, and the same inset below
constexpr int kButtonW = 64, kButtonH = 56;                    // the power button (touch targets of at least 56 px)
constexpr int kPlotPadX = 14, kPlotPadTop = 10, kPlotPadBottom = 20;   // the plot area inside the display below the band: the margins hold the handles (8 px radius) and the time labels of the envelope graph

/** The power button (oscillator only): the top left of the band. */
inline Rect powerRect (const Rect& display) noexcept { return { display.x + kBandInset, display.y + kBandInset + (kBandRowH - kButtonH) / 2, kButtonW, kButtonH }; }
/** The plot area of a display: below the band, the same padding in the three thirds (the oscillator wave, the envelope graph, the LFO curve): the same top, bottom and height. */
inline Rect plotRect (const Rect& display) noexcept
{
    const int y = display.y + kBandH + kPlotPadTop;
    return { display.x + kPlotPadX, y, std::max (0, display.w - 2 * kPlotPadX), std::max (0, display.bottom() - kPlotPadBottom - y) };
}

// ---- Play phase 2 (docs/review/PLAY-PHASE2-PLAN-2026-11-27.md): the HEADER BAND of a third holds the buttons and the selector of the panel; the display below it has no band of its own
constexpr int kHeaderPad = 8, kHeaderGap = 8;               // header.padding / header.gap: the 40 px visual faces sit inside 56 px touch slots (8 + 40 + 8)
constexpr int kHeaderButtonSlot = 56, kHeaderRouteSlot = 72; // the touch slots of the power button and of the route cell
constexpr int kCompactRowH = 60, kCompactTwoRowH = 94;      // a titled dropdown / 56 px dial row and a two row dial cell (the 8 px gap lies between the two rows of the block)

/** The header band of a third: the selector row at its top. */
inline Rect headerBandRect (const Rect& third) noexcept { return { third.x + kMargin, third.y + kMargin, std::max (0, third.w - 2 * kMargin), kSelectorH }; }
/** The power button slot (oscillator only): its 40 px face (8 px inside the 56 px slot) starts at the left edge of the display below, so the slot starts kHeaderPad left of the header band. */
inline Rect headerPowerRect (const Rect& third) noexcept { const Rect h = headerBandRect (third); return { h.x - kHeaderPad, h.y, kHeaderButtonSlot, kSelectorH }; }
/** The route cell (oscillator, not Sub): the right end of the header band; its 64 px face (8 px inside the 72 px cell) ends at the right edge of the display below. The expand buttons are gone. */
inline Rect headerRouteRect (const Rect& third) noexcept { const Rect h = headerBandRect (third); return { h.right() - kHeaderRouteSlot, h.y, kHeaderRouteSlot, kSelectorH }; }
/** The selector (dropdown) slot: its left edge is the left edge of the display (no power button) or the right edge of the power slot, its right edge the right edge of the display or the left edge of the route cell. */
inline Rect headerSelectorRect (const Rect& third, bool hasPower, bool hasRoute) noexcept
{
    const Rect h = headerBandRect (third);
    const int x0 = hasPower ? headerPowerRect (third).right() : h.x;
    const int x1 = hasRoute ? headerRouteRect (third).x : h.right();
    return { x0, h.y, std::max (0, x1 - x0), kSelectorH };
}
/** The plot area of a display without a band (the Play compact views since phase 2: the buttons stand in the header band above the display). */
inline Rect compactPlotRect (const Rect& display) noexcept
{
    return { display.x + kPlotPadX, display.y + kPlotPadTop, std::max (0, display.w - 2 * kPlotPadX), std::max (0, display.h - kPlotPadTop - kPlotPadBottom) };
}

/** The line the controls under the display start at (the top of the first Q member of the block): the same in the three thirds. */
inline int controlsLine (const Rect& third) noexcept { return third.bottom() - kMargin - kControlsH; }

}}} // namespace synthyboy_mpc::ui::playrows
