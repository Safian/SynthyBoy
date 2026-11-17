// MpcKit Demo - the parameters. ONE table: the audio code, the pages, the host's parameter list and the skin all follow it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TO ADD A PARAMETER: add an id to ParamId (before kNumParams: ids are host parameter numbers, never reorder or reuse them once a project was saved), add a line to kParams in the same
// position, add the key to skin/params.json (same order) and use it from a page (bind (id, component, type, "key")) and from the audio code (demo_voice.h).
#pragma once

#include "../../shell-mpc/kit/param_source.h"

namespace demo {

enum ParamId : int { kLevel, kPan, kPitch, kCutoff, kResonance, kFilterOn, kMode, kAttack, kRelease, kNumParams };

inline const char* const kModeNames[4] = { "Low Pass", "High Pass", "Band Pass", "Notch" };
inline const char* const kOnNames[2] = { "Off", "On" };

using mpckit::ParamDesc;
using mpckit::Scale;
using mpckit::Unit;

// key, name (host, <= 31 bytes), group, min, max, default, skew, interval, scale, unit, choices, numChoices, decimals
inline const ParamDesc kParams[kNumParams] = {
    { "level",     "Level",     "Main",   -60.0f,    6.0f,   -6.0f, 1.0f, 0.0f, Scale::linear,   Unit::dB,       nullptr,    0, 1 },
    { "pan",       "Pan",       "Main",  -100.0f,  100.0f,    0.0f, 1.0f, 0.0f, Scale::linear,   Unit::percent,  nullptr,    0, 0 },
    { "pitch",     "Pitch",     "Main",   -24.0f,   24.0f,    0.0f, 1.0f, 1.0f, Scale::linear,   Unit::semitone, nullptr,    0, 0 },
    { "cutoff",    "Cutoff",    "Filter",  20.0f,20000.0f, 8000.0f, 0.3f, 0.0f, Scale::exp_skew, Unit::Hz,       nullptr,    0, 0 },
    { "resonance", "Resonance", "Filter",   0.0f,  100.0f,   10.0f, 1.0f, 0.0f, Scale::linear,   Unit::percent,  nullptr,    0, 0 },
    { "filter_on", "Filter On", "Filter",   0.0f,    1.0f,    1.0f, 1.0f, 1.0f, Scale::stepped,  Unit::none,     kOnNames,   2, 0 },
    { "mode",      "Mode",      "Filter",   0.0f,    3.0f,    0.0f, 1.0f, 1.0f, Scale::stepped,  Unit::none,     kModeNames, 4, 0 },
    // attack and release: 0 .. 10 000 ms with skew 0.3 is exactly the time axis of ui::EnvGraph (envgraph::timeFromNorm), so the normalised value IS the graph's normalised time
    { "attack",    "Attack",    "Amp",      0.0f,10000.0f,   10.0f, 0.3f, 0.0f, Scale::exp_skew, Unit::ms,       nullptr,    0, 0 },
    { "release",   "Release",   "Amp",      0.0f,10000.0f,  300.0f, 0.3f, 0.0f, Scale::exp_skew, Unit::ms,       nullptr,    0, 0 },
};

/** The table as an IParamSource (one object for every instance and thread). */
inline const mpckit::ParamTable& params()
{
    static const mpckit::ParamTable table (kParams, kNumParams);
    return table;
}

/** The presets of the List page: a name and the plain value of every parameter in ParamId order. */
struct Preset { const char* name; float v[kNumParams]; };
inline const Preset kPresets[5] = {
    { "Init",     { -6.0f,   0.0f,  0.0f,  8000.0f, 10.0f, 1.0f, 0.0f,   10.0f,  300.0f } },
    { "Soft Pad", { -9.0f,   0.0f,  0.0f,  1800.0f, 25.0f, 1.0f, 0.0f, 1200.0f, 2500.0f } },
    { "Pluck",    { -6.0f,  15.0f,  0.0f,  3500.0f, 40.0f, 1.0f, 2.0f,    2.0f,  180.0f } },
    { "Low Bass", { -4.0f,   0.0f, -12.0f,  500.0f, 30.0f, 1.0f, 0.0f,    5.0f,  120.0f } },
    { "Airy",     { -10.0f, -20.0f, 12.0f, 6000.0f, 55.0f, 1.0f, 1.0f,  300.0f, 1500.0f } },
};
inline const char* const kPresetNames[5] = { "Init", "Soft Pad", "Pluck", "Low Bass", "Airy" };

} // namespace demo
