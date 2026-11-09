// SynthyBoy - UI model: the Ratio SRC of the oscillators (GROUPB WP-05, 13-new-keys.md 4.33).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

namespace synthyboy::ui
{
/** `oscN.ratio_src` choices: 0 Base note, 1 .. 3 Osc 1 .. 3, 4 Sub. The engine takes the note of the voice for a source that is the oscillator itself, a
    switched-off oscillator, or a chain of sources that leads back to the oscillator; the UI greys those entries out so the chosen value is always the one
    that sounds. `sources` holds the three current values; `osc` is 1 .. 3. */
inline bool ratioSourceAllowed (int osc, int source, const std::array<int, 3>& sources)
{
    if (source < 0 || source > 4)
        return false;
    if (source == 0 || source == 4)
        return true;
    if (source == osc)
        return false;
    int walk = source;
    for (int hops = 0; hops < 3; ++hops)
    {
        const int next = sources[static_cast<size_t> (walk - 1)];
        if (next == 0 || next == 4)
            return true;
        if (next == osc)
            return false;
        walk = next;
    }
    return false;
}
} // namespace synthyboy::ui
