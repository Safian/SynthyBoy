// SynthyBoy - MPC UI component library: the ROWS of the three thirds of the Play pages (oscillator, envelope, LFO), ONE table so that the three displays (the wave, the envelope graph, the LFO curve) have the same top and the same
// height and the controls below them start on one line (owner request, docs/MPC.md "Play page: the rows"). Every third has, top to bottom: the selector row (the one selector component, kSelectorH), the display (displayRect), and below it the
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
constexpr int kSelectorH = 56;             // the selector row: the shared selector (a Stepper with its list) in every third; in the oscillator third it is the header row (power, selector, full screen, route)
constexpr int kSelectorGap = 8;            // between the selector row and the display
constexpr int kControlsH = 232;            // from the controls line to the bottom margin: LFO: SHAPE 56 + lane 16 + RATE 72 + lane 16 + the knob row 72 (its minimum); the oscillator: the type cells 56 + lane + the row (stepper, PAN, LEVEL); the envelope: two knob rows + lane
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
constexpr int kBandRowH = 64;              // the row of the buttons: the route button is 64 x 64 (RouteButton::minSize); the 56 px buttons (power, full screen) are centred in the row
constexpr int kBandH = kBandInset + kBandRowH + kBandInset;   // the band: the inset above the buttons, the row, and the same inset below
constexpr int kButtonW = 64, kButtonH = 56;                    // the power and the full screen button (touch targets of at least 56 px)
constexpr int kBandGap = 8;                // between two buttons and between a button and the label
constexpr int kPlotPadX = 14, kPlotPadTop = 10, kPlotPadBottom = 20;   // the plot area inside the display below the band: the margins hold the handles (8 px radius) and the time labels of the envelope graph

/** The band at the top of a display (inside its frame). */
inline Rect bandRect (const Rect& display) noexcept { return { display.x, display.y, display.w, std::min (kBandH, display.h) }; }
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

/** The line the controls under the display start at (the top of the first Q member of the block): the same in the three thirds. */
inline int controlsLine (const Rect& third) noexcept { return third.bottom() - kMargin - kControlsH; }

}}} // namespace synthyboy_mpc::ui::playrows
