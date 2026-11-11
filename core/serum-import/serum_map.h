// SynthyBoy - core/serum-import: the mapping tables (Serum 2 parameter -> our key) and their value conversions, internal header.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every number of a table is an assumption of docs/ui-design/13-new-keys.md section 4.26 (A325 ..); the comment of a row names it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace synthyboy
{
namespace serumimport
{
/** Conversion of a Serum plain value to ours: out = f (in). */
enum class Conv : uint8_t
{
    lin,        // in * a + b
    dbFromLin,  // 20 log10 (in) * a + b, floor -120 dB
    ms,         // in * 1000 (seconds to milliseconds)
    noteHz,     // Serum filter frequency 0..1: 135 notes -> Hz (A440 = note 69): 440 * 2 ^ ((135 * in - 69) / 12)
    boolean,    // in >= 0.5 -> 1 else 0
    enumName,   // a string, looked up in the row's table
    curve50,    // envelope curve 0..100 (50 = linear) -> power (50 - in) * a
    logPercent, // 0..100 % -> value a * (b / a) ^ (in / 100)
    logPercentInv,// 0..100 % -> value b * (a / b) ^ (in / 100): the high cut (0 % = b, 100 % = a)
    logToPercent, // value -> 0..100 % on a log scale between a (0 %) and b (100 %): the compressor attack / release
    ratioPercent, // compressor ratio r >= 1 -> 100 * (1 - 1 / r)
    filterDrive,  // Serum filter drive 0 .. 100 -> filter#.drive in dB: piecewise linear through the values measured against Serum 2.1.5; the level makeup that goes with it is added to filter#.level (serum_voice.cpp)
    curveAttack,  // Serum envelope attack curve 0 .. 100 -> env#.attack_curve: piecewise linear through the values measured against Serum 2.1.5 (the sign is the opposite of the decay)
    curveDecay,   // Serum envelope decay / release curve 0 .. 100 -> env#.decay_curve / release_curve: the same, measured
    compThresh    // Serum compressor threshold 0 .. 1 -> dB: the measured ceiling of a limiter (tools/ref-compare, REFCOMPARE-REPORT.md), 0 dB up to 0.375 and falling to -42.5 dB
};

enum class Fid : uint8_t { exact, close, approx }; // exact = the same quantity; close = the same quantity, range clamped / unit assumed; approx = nearest feature

struct EnumEntry { const char* name; int value; Fid fid; };
struct EnumTable { const EnumEntry* entries; int count; int fallback; const char* area; };

/** One row: module scope (a Serum module type string), parameter name, our key (a '#' is the 1-based instance), conversion. */
struct Rule
{
    const char* scope;   // "Osc", "WT", "Sample", "Multi", "Granular", "Spectral", "Noise", "NoiseOsc", "Sub", "SubOsc", "Filter", "Env", "LFO", "Macro", "Global", "Routing"
    const char* param;   // kParamTablePos
    const char* key;     // osc#.frame
    Conv conv;
    double a, b;
    Fid fid;
    double serumDefault; // the value an absent parameter has in Serum (NaN = unknown: our default stays)
    const EnumTable* en;
    const char* note;    // stable detail id for approximations ("" = none)
};

/** The rules of a scope (null/0 for an unknown scope). */
const Rule* rulesOf (const char* scope, size_t& count) noexcept;

/** The level makeup (dB) of a Serum filter drive of `v` percent: the louder output of the Serum drive stage, measured against Serum 2.1.5, on top of the drive in dB. */
double filterDriveMakeupDb (double v) noexcept;

/** Level (dB) to add to the oscillator level for `voices` unison voices at `blend` percent: Serum 2.1.5 normalises the unison differently from our engine (the level follows the blend and
    the parity of the voice count; measured, tools/ref-compare/experiment.py). 0 for one voice. */
double unisonLevelCompDb (int voices, double blend) noexcept;

/** Value of `rule` applied to the number `v`. */
double convert (const Rule& rule, double v) noexcept;

extern const EnumTable kGrainWindowEnum, kSpawnPatternEnum, kYAxisEnum, kWarpEnum, kOscTypeEnum, kLoopEnum, kUnisonStackEnum, kDetuneModeEnum, kPhaseMemoryEnum, kSubShapeEnum, kNoiseTypeEnum, kFilterRoutingEnum,
    kPitchModeEnum, kSlicingEnum, kFxDistortionEnum, kReverbTypeEnum, kRoutingDestEnum;

/** The filter type string of a VoiceFilter / FXFilter ("MgL24") -> our (model, style); false for a name the table does not know. `fid` is the fidelity. */
bool filterTypeFromName (const std::string& name, int& model, int& style, Fid& fid) noexcept;

/** Serum modulation source id (1..59, source[0] / source[1] of a ModSlot) -> our source choice value (0 = none, -1 = no equivalent) and a short id. */
int sourceChoiceOf (int serumId, const char*& name, Fid& fid) noexcept;

/** Serum FX module type ("FXReverb") -> our pool type (kFxType*) or 0; fills the scope name of the rule table. */
int fxTypeOf (const std::string& name) noexcept;
const Rule* fxRulesOf (int fxType, size_t& count) noexcept;
} // namespace serumimport
} // namespace synthyboy
