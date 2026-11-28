// SynthyBoy - MPC UI component library: the ROWS of the three thirds of the Play pages (oscillator, envelope, LFO), ONE table so that the three displays (the wave, the envelope graph, the LFO curve) have the same top and the same
// height and the controls below them start on one line (owner request). Every third has, top to bottom: the selector row (the one selector component, kSelectorH), the display (displayRect), and below it the
// block of the controls that starts at controlsLine (the lane of the Q index chips lies above it). The blocks below are as tall as the tallest of the thirds needs (kControlsH: the LFO's SHAPE, RATE and the two knobs).
// The display has a HEADER BAND at its top (bandRect, kBandH): the buttons of the display stand in it (power, route, full screen: powerRect, routeRect, fullRect) with the label text between them (labelRect); the PLOT AREA (plotRect: grid,
// curve, fill, handles, cursor) is the display below the band with the same inner padding in all three thirds, so no curve ever runs under a button and the three plots have the same top, bottom and height.
// Pure constants and functions of a rectangle (the third), no dependencies but core.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core.h"

namespace synthyboy_mpc { namespace ui { namespace playrows {

constexpr int kMargin = 6;                 // the margin of a third at its sides, top and bottom
constexpr int kSelectorH = 56;             // the selector row: the shared selector (a dropdown with its list) in every third; in the oscillator third it is the header row (power, selector, full screen, route)
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
constexpr int kBandRowH = 64;              // the row of the buttons: the route button is 64 x 64 (minimum size); the 56 px buttons (power, full screen) are centred in the row
constexpr int kBandH = kBandInset + kBandRowH + kBandInset;   // the band: the inset above the buttons, the row, and the same inset below
constexpr int kButtonW = 64, kButtonH = 56;                    // the power and the full screen button (touch targets of at least 56 px)
constexpr int kBandGap = 8;                // between two buttons and between a button and the label
constexpr int kPlotPadX = 14, kPlotPadTop = 10, kPlotPadBottom = 20;   // the plot area inside the display below the band: the margins hold the handles (8 px radius) and the time labels of the envelope graph

/** The power button (oscillator only): the top left of the band. */
inline Rect powerRect (const Rect& display) noexcept { return { display.x + kBandInset, display.y + kBandInset + (kBandRowH - kButtonH) / 2, kButtonW, kButtonH }; }
/** The full screen button: the rightmost in the band, the same place in the three thirds. */
inline Rect fullRect (const Rect& display) noexcept { return { display.right() - kBandInset - kButtonW, display.y + kBandInset + (kBandRowH - kButtonH) / 2, kButtonW, kButtonH }; }
/** The route button (oscillator, not Sub): left of the full screen button. */
inline Rect routeRect (const Rect& display) noexcept { return { fullRect (display).x - kBandGap - 64, display.y + kBandInset, 64, kBandRowH }; }
/** The room of the label text of the display: between the power button (or the left margin when there is none) and the route button (or the full screen button). */
inline Rect labelRect (const Rect& display, bool withPower, bool withRoute) noexcept
{
    const int x0 = withPower ? powerRect (display).right() + kBandGap : display.x + 14;
    const int x1 = (withRoute ? routeRect (display).x : fullRect (display).x) - kBandGap;
    return { x0, display.y + kBandInset, std::max (0, x1 - x0), kBandRowH };
}
/** The plot area of a display: below the band, the same padding in the three thirds (the oscillator wave, the envelope graph, the LFO curve): the same top, bottom and height. */
inline Rect plotRect (const Rect& display) noexcept
{
    const int y = display.y + kBandH + kPlotPadTop;
    return { display.x + kPlotPadX, y, std::max (0, display.w - 2 * kPlotPadX), std::max (0, display.bottom() - kPlotPadBottom - y) };
}

// ---- Play phase 2 (docs/review/PLAY-PHASE2-PLAN-2026-11-27.md): the HEADER BAND of a third holds the buttons and the selector of the panel; the display below it has no band of its own
constexpr int kHeaderPad = 8, kHeaderGap = 8;               // header.padding / header.gap: the 40 px visual faces sit inside 56 px touch slots (8 + 40 + 8)
constexpr int kHeaderButtonSlot = 56, kHeaderRouteSlot = 72; // the touch slots of the power / full screen buttons and of the route cell
constexpr int kCompactRowH = 60, kCompactTwoRowH = 94;      // a titled dropdown / 56 px dial row and a two row dial cell (the 8 px gap lies between the two rows of the block)

/** The header band of a third: the selector row at its top. */
inline Rect headerBandRect (const Rect& third) noexcept { return { third.x + kMargin, third.y + kMargin, std::max (0, third.w - 2 * kMargin), kSelectorH }; }
/** The power button slot (oscillator only): the left end of the header band. */
inline Rect headerPowerRect (const Rect& third) noexcept { const Rect h = headerBandRect (third); return { h.x, h.y, kHeaderButtonSlot, kSelectorH }; }
/** The full screen (expand) button slot: the right end of the header band, the same place in the three thirds. */
inline Rect headerExpandRect (const Rect& third) noexcept { const Rect h = headerBandRect (third); return { h.right() - kHeaderButtonSlot, h.y, kHeaderButtonSlot, kSelectorH }; }
/** The route cell (oscillator, not Sub): left of the full screen button. */
inline Rect headerRouteRect (const Rect& third) noexcept { const Rect h = headerBandRect (third); return { h.right() - kHeaderButtonSlot - kHeaderRouteSlot, h.y, kHeaderRouteSlot, kSelectorH }; }
/** The selector (dropdown) slot between the power button (or the left pad) and the route cell (or the full screen button). */
inline Rect headerSelectorRect (const Rect& third, bool hasPower, bool hasRoute) noexcept
{
    const Rect h = headerBandRect (third);
    const int x0 = hasPower ? h.x + kHeaderButtonSlot : h.x + kHeaderPad;
    const int x1 = hasRoute ? h.right() - kHeaderButtonSlot - kHeaderRouteSlot : h.right() - kHeaderButtonSlot;
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
