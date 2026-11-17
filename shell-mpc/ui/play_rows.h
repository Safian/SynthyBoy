// SynthyBoy - MPC UI component library: the ROWS of the three thirds of the Play pages (oscillator, envelope, LFO), ONE table so that the three displays (the wave, the envelope graph, the LFO curve) have the same top and the same
// height and the controls below them start on one line (owner request, docs/MPC.md "Play page: the rows"). Every third has, top to bottom: the selector row (the one selector component, kSelectorH), the display (displayRect), and below it the
// block of the controls that starts at controlsLine (the lane of the Q index chips lies above it). The blocks below are as tall as the tallest of the thirds needs (kControlsH: the LFO's SHAPE, RATE and the two knobs).
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
/** The line the controls under the display start at (the top of the first Q member of the block): the same in the three thirds. */
inline int controlsLine (const Rect& third) noexcept { return third.bottom() - kMargin - kControlsH; }

}}} // namespace synthyboy_mpc::ui::playrows
