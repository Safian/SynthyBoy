// SynthyBoyVST - core: sample-accurate input events.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace synthyboy
{
/** One timed input event. A block's events must be sorted by frame (stable: equal frames keep their order). */
struct Event
{
    enum Type : int16_t
    {
        NoteOn,      // key, channel, value = velocity 0..1, noteId
        NoteOff,     // key, channel (noteId optional)
        PitchBend,   // channel, value = bend -1..1 (full range = +-2 semitones)
        AllNotesOff  // channel (< 0: all channels); voices release normally
    };

    int32_t frame = 0;   // 0 .. numFrames - 1, position inside the block
    Type type = NoteOn;
    int16_t channel = 0; // 0..15
    int16_t key = 0;     // 0..127
    float value = 0.0f;
    int32_t noteId = -1; // host note id, -1 = none
};
} // namespace synthyboy
