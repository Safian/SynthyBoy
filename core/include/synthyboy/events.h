// SynthyBoy - core: sample-accurate input events.
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
        NoteOff,     // key, channel (noteId optional), value = release (note-off) velocity 0..1; the "Lift" modulation source (0 = unknown)
        PitchBend,   // channel, value = bend -1..1 (full range = master.bend_range semitones, 2 by default)
        AllNotesOff, // channel (< 0: all channels); voices release normally
        ModWheel,    // channel, value = 0..1 (MIDI CC 1); the "Mod Wheel" modulation source
        Aftertouch,  // channel, value = 0..1 (channel pressure); the "Aftertouch" modulation source
                     //   (master.mpe.on: pressure of the notes of that member channel, the "Poly Aftertouch" source)
        SustainPedal,   // channel (< 0: all channels), value >= 0.5: pedal down (MIDI CC 64 >= 64), else up; EngineCaps kCapEventContract
        NoteExpression  // per-note expression, see `expression`: noteId / (channel, key) address the note (key < 0: every note of the
                        // channel, the MPE member channel case); value: Tuning = semitones relative to the key, Pressure and
                        // Brightness (MPE slide) = 0..1; EngineCaps kCapEventContract
    };

    /** Which expression a NoteExpression event carries (the CLAP note expressions TUNING, PRESSURE, BRIGHTNESS). */
    enum Expression : int16_t
    {
        Tuning = 0,     // per-note pitch, semitones (CLAP TUNING; master.mpe.on adds the member channel bend)
        Pressure = 1,   // per-note pressure 0..1 (CLAP PRESSURE, MIDI poly key pressure): source "Poly Aftertouch"
        Brightness = 2  // per-note slide / Y 0..1 (CLAP BRIGHTNESS, MIDI CC 74 of an MPE member channel): source "Slide"
    };

    int32_t frame = 0;   // 0 .. numFrames - 1, position inside the block
    Type type = NoteOn;
    int16_t channel = 0; // 0..15
    int16_t key = 0;     // 0..127
    Expression expression = Tuning; // NoteExpression only
    float value = 0.0f;
    int32_t noteId = -1; // host note id, -1 = none
};

/** Host transport as seen at the start of a block (IEngine::setTransport). Tempo sync of the envelopes and LFOs and the HOST
    mode of the LFOs read it; an engine that never gets one runs at 120 BPM with a stopped transport. */
struct TransportInfo
{
    double bpm = 120.0;          // beats per minute (> 0)
    double songPosBeats = 0.0;   // position of the start of the block in quarter notes
    bool playing = false;        // the transport is running (songPosBeats advances); false: LFOs in HOST mode run free
};
} // namespace synthyboy
