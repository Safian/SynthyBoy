// SynthyBoy - tests of the Vital importer: .vital fixtures synthesised in code (nothing from Vital is committed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The writer applies the forward conversions of the table (our value -> file value), i.e. the test counterpart of the importer
// (docs/ui-design/11-vital-preset-import.md Annex C.3: "the writer is test code, not shipped").
#pragma once

#include <map>
#include <string>
#include <vector>

#include "synthyboy/state.h"
#include "vital_import_internal.h"

namespace sbfx
{
/** Forward conversion of a row: our plain value -> the value in the file (frame rows use the 257 frame convention of the importer). */
double forward (const synthyboy::vitalimport::VitalRow& row, double plain);

std::string fmt (double v);
std::string b64 (const void* data, size_t size);
std::string quote (const std::string& s);

struct Fixture
{
    std::map<std::string, std::string> settings; // control -> raw JSON value
    std::vector<std::pair<std::string, std::string>> mods;
    std::map<std::string, std::string> modExtra;  // "modulations[i]" index text -> extra raw members, e.g. "line_mapping"
    std::string wavetables, lfos, sample, tuning; // raw JSON text of the value, empty = omit
    std::map<std::string, std::string> top;       // extra top level members, raw JSON values
    std::string version = "1.5.5";
    std::string name = "Fixture", author = "Tester", comments, style = "Bass";
    bool omitVersion = false;

    void set (const std::string& control, double v) { settings[control] = fmt (v); }
    void setRaw (const std::string& control, const std::string& raw) { settings[control] = raw; }
    std::string json() const;
};

/** The effect_chain_order code of the nine fx.order.N values (brute force over the decoder). */
int encodeEffectOrder (const synthyboy::ParamValues& values);

/** All 730 controls from the parameter defaults (forward converted), no modulations, 3 default wavetables omitted. */
Fixture defaultPatch();
/** Writes the controls of `values` (for the keys of the table) into the fixture. */
void writeValues (Fixture& f, const synthyboy::ParamValues& values);

// ---- wavetable object pieces ------------------------------------------------------------------------------------------
std::string waveKeyframe (int position, const std::vector<float>& wave);
std::string component (const std::string& type, const std::string& extra, const std::vector<std::string>& keyframes);
std::string wavetable (const std::string& name, const std::vector<std::string>& components, const std::string& flags = "");
std::vector<float> sineWave (int harmonic);
std::string lineObject (const std::vector<std::pair<double, double>>& points, const std::vector<double>& powers, bool smooth);
} // namespace sbfx
