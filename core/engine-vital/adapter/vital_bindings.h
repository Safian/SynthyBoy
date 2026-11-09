// SynthyBoy - engine-vital adapter: the table that maps our parameters onto the controls of the Vital-derived engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This header is Vital-free (control names are strings); the table is built by buildBindings() and used by the engine at
// prepare() and by the tests (every conversion is tested on its own: core/engine-vital/tests/test_vital_bindings.cpp).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "synthyboy/params.h"

namespace synthyboy
{
/** How our plain value becomes the value of the engine's control. One converter per kind (formulas in vital_bindings.cpp). */
enum class Conv
{
    linear,       // plain * a + b
    sqrtScaled,   // sqrt (plain * a): the "quadratic" controls (level, resonance of an EQ band, detune)
    quarticMs,    // (plain / 1000) ^ (1/4): the envelope times (the engine shows seconds = control ^ 4)
    log2Ms,       // max (a, log2 (plain / 1000)): a time in ms as the log2 of seconds, a = the floor ("off")
    log2Hz,       // log2 (plain): a frequency in Hz as its log2
    log2PeriodMs, // log2 (1000 / plain): a delay time in ms as the log2 of its frequency
    noteFromHz,   // 12 * log2 (plain / 8.1757989): a frequency in Hz as a MIDI note number (the engine's cutoff scale)
    index,        // plain + b: a choice, offset by b (the list of the control starts at b)
    indexMap,     // map[plain]: a choice whose engine index differs
    volumeDb,     // dB -> the engine's master volume (shown dB = sqrt (control) - 80), -60 dB and below is silence
    quarticBeats  // (plain * 60 / bpm) ^ (1/4): an envelope time in beats at the current tempo (the engine's seconds = control ^ 4); appended after the
                  // version 2 converters: vital-import compares the numbers
};

/** What the quality tier may cap or bypass in the converted value. */
enum class Cap
{
    none,
    voices,   // min (value, Quality::maxVoices)
    unison,   // min (value, Quality::maxUnison)
    fx        // value is an on/off switch of effect number capArg: off if the tier bypasses it
};

struct Binding
{
    size_t param = 0;        // index into paramTable()
    std::string control;     // name of the engine's control
    Conv conv = Conv::linear;
    double a = 1.0, b = 0.0;
    const int* map = nullptr; // Conv::indexMap
    Cap cap = Cap::none;
    int capArg = 0;
    int gateParam = -1;       // the binding writes its control only while parameter gateParam (a choice) equals gateValue: the millisecond and
    int gateValue = 0;        // the beat keys of an envelope time share one engine control (envN.units picks which one is live)
    int frameOsc = 0;         // 1..3: the binding is the frame position (or spread) of that oscillator: the scale follows the frame count of its table
    int maxIndex = -1;        // Conv::index: the highest choice the engine implements (-1: no limit); choices appended to the list in
                              // state version 3 (a new filter model, ...) are stored but act like the last implemented one
};

/** The table: one entry per parameter that maps onto exactly one control. Parameters without an entry are the special ones
    (isSpecialParam). Built once; no engine needed. */
const std::vector<Binding>& vitalBindings();

/** Parameters the adapter handles itself instead of through one control: the oscillator wavetables, the LFO shapes, the filter
    routing, the order of the effects, the source and destination of the modulation slots, the pitch bend range; of state version 3
    the envelope units, the LFO mono / host switches, the aux keys of the slots, the curves, the scale, tuning and MPE keys and the
    oscillator mapping. */
bool isSpecialParam (size_t paramIndex);

/** The raw (not clamped) value of the control for a plain value. sampleRate limits the cutoffs of the filters. */
double convertParam (const Binding& binding, double plain, double sampleRate, double bpm = 120.0);

/** The inverse of convertParam for the continuous converters: the plain value for a raw value of the control (GROUPA2 WP-17: the live value of a modulated knob).
    NaN for a converter that has no inverse (index, indexMap). */
double invertParam (const Binding& binding, double raw, double sampleRate, double bpm = 120.0);

/** Order of the effects from the nine choices fx.order.1 .. fx.order.9 (duplicates and out-of-range choices are resolved
    deterministically: a position whose effect is already placed takes the lowest effect that is still free). */
void resolveFxOrder (const float* choices, int* order, int count = kNumLegacyFxSlots);

/** What the engine's control is: min, max and default as the engine defines them (the tests compare against these). */
struct ControlRange
{
    bool exists = false;
    double min = 0.0, max = 0.0, def = 0.0;
};
ControlRange vitalControlRange (const std::string& control);

/** The conversion of one parameter, for diagnostics and tests: control name and raw value (bound = false for special parameters). */
struct Conversion
{
    bool bound = false;
    std::string control;
    double raw = 0.0;
    ControlRange range;
};
Conversion vitalConvert (size_t paramIndex, double plain, double sampleRate = 48000.0);

/** The first binding of a parameter, nullptr if it has none (a special parameter). Valid for the life of the process. */
const Binding* vitalBindingOf (size_t paramIndex);

/** The control that a modulation destination of the matrix drives (the first binding of the parameter), "" if none. */
std::string vitalControlOf (size_t paramIndex);
} // namespace synthyboy
