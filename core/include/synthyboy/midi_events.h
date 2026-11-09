// SynthyBoy - core: translation of a short MIDI channel message into an Event (shared by the CLAP and the JUCE shell).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Header only, no allocation. The mapping (docs/ui-design/13-new-keys.md 6):
//   0x9n vel > 0 -> NoteOn            0x9n vel 0, 0x8n -> NoteOff, value = release velocity / 127
//   0xEn         -> PitchBend         0xDn -> Aftertouch (channel pressure)      0xAn -> NoteExpression Pressure (key = note)
//   CC 10 -> NoteExpression Pan (X, key = -1)    CC 1 -> ModWheel    CC 64 -> SustainPedal (>= 64 down)    CC 74 -> NoteExpression Brightness (key = -1: the whole channel)
//   CC 120 / 123 -> AllNotesOff (that channel)
// Everything else is not translated (returns false).
#pragma once

#include <cstdint>

#include "events.h"

namespace synthyboy
{
/** Translates one MIDI message of 2 or 3 bytes (status, data1[, data2]) at `frame` into `out`. False if it is not one we use. */
inline bool translateMidiMessage (const uint8_t* bytes, int numBytes, int frame, Event& out) noexcept
{
    if (bytes == nullptr || numBytes < 2 || (bytes[0] & 0x80) == 0)
        return false;

    const int kind = bytes[0] & 0xF0;
    const int d1 = bytes[1] & 0x7F;
    const int d2 = numBytes >= 3 ? (bytes[2] & 0x7F) : 0;

    out = Event {};
    out.frame = frame;
    out.channel = static_cast<int16_t> (bytes[0] & 0x0F);

    switch (kind)
    {
        case 0x90:
            if (numBytes < 3)
                return false;
            out.key = static_cast<int16_t> (d1);
            if (d2 > 0)
            {
                out.type = Event::NoteOn;
                out.value = static_cast<float> (d2) / 127.0f;
            }
            else
            {
                out.type = Event::NoteOff; // note on with velocity 0: released, release velocity unknown
            }
            return true;
        case 0x80:
            if (numBytes < 3)
                return false;
            out.type = Event::NoteOff;
            out.key = static_cast<int16_t> (d1);
            out.value = static_cast<float> (d2) / 127.0f;
            return true;
        case 0xA0:
            if (numBytes < 3)
                return false;
            out.type = Event::NoteExpression;
            out.expression = Event::Pressure;
            out.key = static_cast<int16_t> (d1);
            out.value = static_cast<float> (d2) / 127.0f;
            return true;
        case 0xB0:
            if (numBytes < 3)
                return false;
            if (d1 == 1)
            {
                out.type = Event::ModWheel;
                out.value = static_cast<float> (d2) / 127.0f;
                return true;
            }
            if (d1 == 64)
            {
                out.type = Event::SustainPedal;
                out.value = d2 >= 64 ? 1.0f : 0.0f;
                return true;
            }
            if (d1 == 10)
            {
                // GROUPA1 (WP-09): CC 10 (pan) is the X axis of an MPE member channel (Serum 2 manual p.314); 0.5 = centre
                out.type = Event::NoteExpression;
                out.expression = Event::Pan;
                out.key = -1;
                out.value = static_cast<float> (d2) / 127.0f;
                return true;
            }
            if (d1 == 74)
            {
                out.type = Event::NoteExpression;
                out.expression = Event::Brightness;
                out.key = -1;
                out.value = static_cast<float> (d2) / 127.0f;
                return true;
            }
            if (d1 == 120 || d1 == 123)
            {
                out.type = Event::AllNotesOff;
                return true;
            }
            return false;
        case 0xD0:
            out.type = Event::Aftertouch;
            out.value = static_cast<float> (d1) / 127.0f;
            return true;
        case 0xE0:
            if (numBytes < 3)
                return false;
            out.type = Event::PitchBend;
            out.value = (static_cast<float> (d1 | (d2 << 7)) - 8192.0f) / 8192.0f;
            return true;
        default:
            return false;
    }
}
} // namespace synthyboy
