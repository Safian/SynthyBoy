// SynthyBoy - UI: a look into the shape display of an LFO panel for the tests (GROUPA2 WP-17).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../Panel.h"

namespace synthyboy::ui
{
struct LfoLiveProbe
{
    bool random = false;
    float playhead = -1.0f;        // the playhead as a fraction of the plot width, -1 when the engine plays no voice with this LFO (or the LFO is a random LFO)
    juce::Rectangle<int> plot;     // the plot area in the coordinates of the panel's display
    bool bound = false;            // the display listens to the live bus (it is on screen)
};
/** False when `panel` is not an LFO panel. */
bool probeLfoPanel (Panel& panel, LfoLiveProbe& out);
} // namespace synthyboy::ui
