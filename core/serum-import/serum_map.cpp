// SynthyBoy - core/serum-import: the mapping tables of the Serum 2 importer (docs/ui-design/13-new-keys.md 4.26, assumptions A325 ..).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Data only. Module scopes: Osc (Oscillator0..2), WT / Sample / Multi / Granular / Spectral (the type specific block of an oscillator), Noise (Oscillator3),
// NoiseOsc, Sub (Oscillator4), SubOsc, Filter (VoiceFilter0..1), Env (Env0..3), Macro, Global. The names are the ones of the binary format (19-serum-preset-format.md).
#include "serum_map.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "synthyboy/filters/filter_types.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"

namespace synthyboy::serumimport
{
namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr Fid X = Fid::exact, C = Fid::close, A = Fid::approx;
} // namespace

// ----- enumerations ----------------------------------------------------------------------------------------------------------------------
#define ENUM(name, fallback, area, ...)                                                             \
    static const EnumEntry name##Entries[] = { __VA_ARGS__ };                                       \
    const EnumTable name = { name##Entries, static_cast<int> (sizeof (name##Entries) / sizeof (EnumEntry)), fallback, area }

// warp lists (WT oscillator and the other types). The indices are the ones of osc#.warp.type (13-new-keys.md 4.22). `_OSC` is the first, `_OSC2` the second other oscillator.
ENUM (kWarpEnum, 0, "warp",
      { "kNoWarp", 0, X }, { "kSync", 1, X }, { "kQuantize", 3, X }, { "kPWM", 6, C }, { "kBendPos", 12, C }, { "kBendNeg", 13, C }, { "kBendPosNeg", 14, C },
      { "kASYMPos", 15, C }, { "kASYMNeg", 16, C }, { "kASYMPosNeg", 17, C }, { "kFlip", 18, X }, { "kDLM", 19, A },
      { "kRemap_1", 20, X }, { "kRemap_2", 21, X }, { "kRemap_3", 22, X }, { "kRemap_4", 23, X }, { "kEvenOdd", 24, X },
      { "kFilterLPF", 25, X }, { "kFilterHPF", 26, X }, { "kDistTube", 27, X }, { "kDistSoftClip", 28, X }, { "kDistHardClip", 29, X }, { "kDistDiode1", 30, X },
      { "kDistDiode2", 31, X }, { "kDistLinFold", 32, X }, { "kDistSinFold", 33, X }, { "kDistZeroSquare", 34, X }, { "kDistAsym", 35, X }, { "kDistRectify", 36, X },
      { "kDistSineShaper", 37, X }, { "kDistStompBox", 38, X }, { "kDistTapeSat", 39, X }, { "kDistSoftSat", 40, X },
      { "kFM_OSC", 41, C }, { "kFM_OSC2", 42, C }, { "kFMX_OSC", 43, C }, { "kFMX_OSC2", 44, C }, { "kFMP_OSC", 45, C }, { "kFMP_OSC2", 46, C },
      { "kPD_OSC", 47, X }, { "kPD_OSC2", 48, X }, { "kSelfPD", 49, X }, { "kAM_OSC", 50, X }, { "kAM_OSC2", 51, X }, { "kRM_OSC", 9, C }, { "kRM_OSC2", 10, C });

ENUM (kOscTypeEnum, 0, "osc.type", { "kOsc_WT", 0, X }, { "kOsc_Sample", 1, X }, { "kOsc_MultiSample", 1, A }, { "kOsc_Granular", 1, A }, { "kOsc_Spectral", 1, A });
ENUM (kLoopEnum, 0, "loop", { "kNone", 0, X }, { "kForward", 1, X }, { "kReverse", 2, X }, { "kPingPong", 3, X }, { "kTailed", 4, X });
ENUM (kUnisonStackEnum, 0, "unison.stack", { "kNoUnisonStack", 0, X }, { "kCenter12", 1, X }, { "kCenter24", 2, X }, { "kOctave1", 3, X }, { "kOctave2", 4, X },
      { "kOctave3", 4, A }, { "kOctaveFifth1", 5, X }, { "kOctaveFifth2", 6, X }, { "kOctaveFifth3", 6, A });
ENUM (kDetuneModeEnum, 0, "unison.mode", { "kDetuneLinear", 0, X }, { "kDetuneSuper", 1, X }, { "kDetuneExp", 2, X }, { "kDetuneInv", 3, X }, { "kDetuneRandom", 4, X });
ENUM (kPhaseMemoryEnum, 0, "phase.memory", { "kAll", 0, X }, { "kContiguous", 1, X }, { "kPerVoice", 2, X });
ENUM (kSubShapeEnum, 0, "sub.shape", { "kSine", 0, X }, { "kRoundRect", 1, X }, { "kTriangle", 2, X }, { "kSaw", 3, X }, { "kSquare", 4, X }, { "kPulse", 5, X });
ENUM (kNoiseTypeEnum, 0, "noise.type", { "White", 0, X }, { "Pink", 1, X }, { "Brown", 2, X }, { "Geiger", 3, X });
ENUM (kFilterRoutingEnum, 0, "routing", { "Off", 0, X });
ENUM (kPitchModeEnum, 0, "pitch.mode", { "Semitones", 0, X }, { "Harmonics", 1, C }, { "Ratio", 2, C });
ENUM (kSlicingEnum, 0, "slicing", { "Off", 0, X }, { "Auto", 1, X }, { "Manual", 2, X });
// distortion types of the FX module (our list: 0 Soft clip, 1 Hard clip, 2 Linear fold, 3 Sine fold, 4 Bit crush, 5 Down sample, 6 Tube, 7 Diode 1, 8 Diode 2, 9 Zero-Square, 10 Asym, 11 Rectify, 12 X-Shaper, 13 X-Shaper (Asym))
ENUM (kFxDistortionEnum, 6, "fx.distortion", { "kTube", 6, X }, { "kSoftClip", 0, X }, { "kHardClip", 1, X }, { "kDiode1", 7, X }, { "kDiode2", 8, X }, { "kLinFold", 2, X },
      { "kSinFold", 3, X }, { "kZeroSquare", 9, X }, { "kDownsample", 5, X }, { "kAsym", 10, X }, { "kRectify", 11, X }, { "kXShaper", 12, X }, { "kXShaperAsym", 13, X },
      { "kSineShaper", 3, A }, { "kStompBox", 6, A }, { "kTapeSat", 0, A }, { "kOverdrive", 0, A }, { "kSoftSat", 0, A });
// reverb types (our list: 0 Vital, 1 Plate, 2 Hall, 3 Vintage, 4 Nitrous, 5 Basin)
ENUM (kReverbTypeEnum, 1, "fx.reverb", { "kPlate", 1, X }, { "kHall", 2, X }, { "kVintage", 3, X }, { "kSpace", 4, C }, { "kAbyss", 5, C });
// RoutingSlot destinations -> osc#.destination (0 Filter 1, 1 Filter 2, 2 Filters 1+2, 3 Effects only, 4 Direct out, 5 Filter blend, 6 None)
ENUM (kRoutingDestEnum, 5, "routing", { "kRoutingDestFilter", 5, X }, { "kRoutingDestMaster", 3, X }, { "kRoutingDestDirect", 4, X }, { "kRoutingDestNone", 6, X });

// ----- oscillator rules ------------------------------------------------------------------------------------------------------------------
namespace
{
const Rule kOsc[] = {
    { "Osc", "kParamVolume", "osc#.level", Conv::lin, 100, 0, C, kNaN, nullptr, "level_linear_assumed" },
    { "Osc", "kParamPan", "osc#.pan", Conv::lin, 2, 0, X, 0, nullptr, "" },
    { "Osc", "kParamFine", "osc#.tune", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamCoarsePit", "osc#.pitch_coarse", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamUnison", "osc#.unison.voices", Conv::lin, 1, 0, X, 1, nullptr, "" },
    { "Osc", "kParamDetune", "osc#.unison.detune", Conv::lin, 100, 0, C, kNaN, nullptr, "unison_detune_scale_assumed" },
    { "Osc", "kParamDetuneWid", "osc#.unison.blend", Conv::lin, 1, 0, X, 75, nullptr, "" },
    { "Osc", "kParamUnisonStereo", "osc#.unison.spread", Conv::lin, 1, 0, C, 100, nullptr, "unison_stereo_sign_lost" },
    { "Osc", "kParamUnisonRange", "osc#.unison.range", Conv::lin, 1, 0, X, 2, nullptr, "" },
    { "Osc", "kParamUnisonStack", "osc#.unison.stack", Conv::enumName, 1, 0, X, kNaN, &kUnisonStackEnum, "" },
    { "Osc", "kParamDetuneMode", "osc#.unison.mode", Conv::enumName, 1, 0, X, kNaN, &kDetuneModeEnum, "" },
    { "Osc", "kParamPitchMode", "osc#.pitch_mode", Conv::enumName, 1, 0, X, kNaN, &kPitchModeEnum, "" },
    { "Osc", "kParamPitchTrack", "osc#.pitch_track", Conv::boolean, 1, 0, X, 1, nullptr, "" },
    { "Osc", "kParamPitchBendTrack", "osc#.pitch_bend_track", Conv::boolean, 1, 0, X, 1, nullptr, "" },
    { "Osc", "kParamKeyZoneMin", "osc#.map.key_low", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamKeyZoneMax", "osc#.map.key_high", Conv::lin, 1, 0, X, 127, nullptr, "" },
    { "Osc", "kParamVelocityZoneMin", "osc#.map.vel_low", Conv::lin, 1, 0, X, 1, nullptr, "" },
    { "Osc", "kParamVelocityZoneMax", "osc#.map.vel_high", Conv::lin, 1, 0, X, 127, nullptr, "" },
    { "Osc", "kParamKeyZoneWarp", "osc#.map.key_warp", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamKeyZoneFold", "osc#.map.key_fold", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamUnisonWarp", "osc#.warp.spread", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamUnisonWarp2", "osc#.warp2.spread", Conv::lin, 1, 0, X, 0, nullptr, "" },
    // the sample player parameters (they live in the common block of the oscillator in the file)
    { "Osc", "kParamStart", "osc#.sample.start", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamEnd", "osc#.sample.end", Conv::lin, 1, 0, X, 100, nullptr, "" },
    { "Osc", "kParamLoopStart", "osc#.sample.loop_start", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamLoopEnd", "osc#.sample.loop_end", Conv::lin, 1, 0, X, 100, nullptr, "" },
    { "Osc", "kParamLoopMode", "osc#.sample.loop_mode", Conv::enumName, 1, 0, X, kNaN, &kLoopEnum, "" },
    { "Osc", "kParamReverse", "osc#.sample.reverse", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamLinkLoopLength", "osc#.sample.loop_link", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamLoopEndsAtRelease", "osc#.sample.loop_exit", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamRandomStart", "osc#.random_phase", Conv::lin, 1, 0, C, 0, nullptr, "random_start_as_random_phase" },
    { "Osc", "kParamUnisonSpan", "osc#.sample.uni_span", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamSlicingEnabled", "osc#.sample.slice_mode", Conv::enumName, 1, 0, X, kNaN, &kSlicingEnum, "" },
    { "Osc", "kParamAutoSliceThreshold", "osc#.sample.slice_threshold", Conv::lin, 100, 0, X, 0.5, nullptr, "" },
    { "Osc", "kParamSlicingRootNote", "osc#.sample.slice_root", Conv::lin, 1, 0, X, 36, nullptr, "" },
    { "Osc", "kParamScanTempoLock", "osc#.sample.scan_lock_tempo", Conv::boolean, 1, 0, X, 0, nullptr, "" },
};

// The warp block is the same for every type; `Warp` is the amount 0..1, `WarpVar` the second fader (our phase), `WarpMenu` the mode.
const Rule kWarp[] = {
    { "Warp", "kParamWarp", "osc#.warp.amount", Conv::lin, 100, 0, X, kNaN, nullptr, "" },
    { "Warp", "kParamWarp2", "osc#.warp2.amount", Conv::lin, 100, 0, X, kNaN, nullptr, "" },
    { "Warp", "kParamWarpVar", "osc#.warp.phase", Conv::lin, 360, 0, C, kNaN, nullptr, "warp_var_as_phase" },
    { "Warp", "kParamWarpVar2", "osc#.warp2.phase", Conv::lin, 360, 0, C, kNaN, nullptr, "warp_var_as_phase" },
    { "Warp", "kParamWarpMenu", "osc#.warp.type", Conv::enumName, 1, 0, X, kNaN, &kWarpEnum, "" },
    { "Warp", "kParamWarpMenu2", "osc#.warp2.type", Conv::enumName, 1, 0, X, kNaN, &kWarpEnum, "" },
};

const Rule kWt[] = {
    { "WT", "kParamInitialPhase", "osc#.phase", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "WT", "kParamRandomPhase", "osc#.random_phase", Conv::lin, 1, 0, X, 100, nullptr, "" },
    { "WT", "kParamUnisonWTPos", "osc#.frame_spread", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "WT", "kParamPhaseMemory", "osc#.phase_memory", Conv::enumName, 1, 0, X, kNaN, &kPhaseMemoryEnum, "" },
};

const Rule kMulti[] = {
    { "Multi", "kParamRandomPhase", "osc#.random_phase", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
};

const Rule kNoise[] = {
    { "Noise", "kParamVolume", "noise.level", Conv::lin, 100, 0, C, kNaN, nullptr, "level_linear_assumed" },
    { "Noise", "kParamPan", "noise.pan", Conv::lin, 2, 0, X, 0, nullptr, "" },
    { "Noise", "kParamEnable", "noise.on", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Noise", "kParamPitch", "noise.pitch", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Noise", "kParamFine", "noise.fine", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "NoiseOsc", "kParamFine", "noise.fine", Conv::lin, 100, 0, C, 0, nullptr, "noise_fine_unit_assumed" },
    { "NoiseOsc", "kParamInitialPhase", "noise.phase", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "NoiseOsc", "kParamColor", "noise.filter", Conv::lin, 200, -100, A, kNaN, nullptr, "noise_color_is_the_filter_knob_assumed" }, // A348
    { "NoiseOsc", "kParamRandomPhase", "noise.random_phase", Conv::boolean, 1, 0, C, 0, nullptr, "random_phase_amount_to_switch" },
    { "NoiseOsc", "kParamOneShot", "noise.loop", Conv::lin, -1, 1, X, 0, nullptr, "" },
};

const Rule kSub[] = {
    { "Sub", "kParamVolume", "sub.level", Conv::lin, 100, 0, C, kNaN, nullptr, "level_linear_assumed" },
    { "Sub", "kParamPan", "sub.pan", Conv::lin, 2, 0, X, 0, nullptr, "" },
    { "Sub", "kParamEnable", "sub.on", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Sub", "kParamOctave", "sub.octave", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Sub", "kParamPitch", "sub.tune", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "SubOsc", "kParamShape", "sub.waveform", Conv::enumName, 1, 0, X, kNaN, &kSubShapeEnum, "" },
    // GROUPA1 (WP-04, A349): the start phase in degrees (0 .. 360) is sub.phase in percent of a cycle; the option "contiguous" is sub.phase_contiguous
    { "SubOsc", "kParamInitialPhase", "sub.phase", Conv::lin, 100.0 / 360.0, 0, X, 0, nullptr, "" },
    { "SubOsc", "kParamContiguousPhase", "sub.phase_contiguous", Conv::boolean, 1, 0, X, 0, nullptr, "" },
};

const Rule kFilter[] = {
    { "Filter", "kParamEnable", "filter#.on", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Filter", "kParamFreq", "filter#.cutoff", Conv::noteHz, 1, 0, C, kNaN, nullptr, "filter_frequency_scale_assumed" },
    { "Filter", "kParamReso", "filter#.resonance", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Filter", "kParamDrive", "filter#.drive", Conv::lin, 0.2, 0, C, kNaN, nullptr, "filter_drive_scale_assumed" },
    { "Filter", "kParamVar", "filter#.var", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Filter", "kParamWet", "filter#.mix", Conv::lin, 1, 0, X, 100, nullptr, "" },
    { "Filter", "kParamKeyTrack", "filter#.keytrack", Conv::lin, 100, 0, X, 0, nullptr, "" },
    { "Filter", "kParamLevelOut", "filter#.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
    { "Filter", "kParamStereo", "filter#.pan", Conv::lin, 2, -100, X, 50, nullptr, "" },
    { "Filter", "kParamPad", "filter#.clean_mode", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Filter", "kParamX", "filter#.formant.x", Conv::lin, 100, 0, C, kNaN, nullptr, "filter_xy_as_formant" },
    { "Filter", "kParamY", "filter#.formant.y", Conv::lin, 100, 0, C, kNaN, nullptr, "filter_xy_as_formant" },
};

const Rule kEnv[] = {
    { "Env", "kParamAttack", "env#.attack", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Env", "kParamHold", "env#.hold", Conv::ms, 1, 0, X, 0, nullptr, "" },
    { "Env", "kParamDecay", "env#.decay", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Env", "kParamSustain", "env#.sustain", Conv::lin, 100, 0, C, 1, nullptr, "sustain_linear_assumed" },
    { "Env", "kParamRelease", "env#.release", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Env", "kParamCurve1", "env#.attack_curve", Conv::curve50, 0.18, 0, C, 50, nullptr, "env_curve_scale_assumed" },
    { "Env", "kParamCurve2", "env#.decay_curve", Conv::curve50, 0.18, 0, C, 66.6, nullptr, "env_curve_scale_assumed" },
    { "Env", "kParamCurve3", "env#.release_curve", Conv::curve50, 0.18, 0, C, 66.6, nullptr, "env_curve_scale_assumed" },
    { "Env", "kParamLegatoInverted", "env#.legato_inverted", Conv::boolean, 1, 0, X, 0, nullptr, "" },
};

const Rule kMacro[] = { { "Macro", "kParamValue", "macro.#", Conv::lin, 1, 0, X, 0, nullptr, "" } };

const Rule kGlobal[] = {
    { "Global", "kParamMasterVolume", "master.volume", Conv::dbFromLin, 1, 0, C, kNaN, nullptr, "master_volume_linear_assumed" },
    { "Global", "kParamPolyCount", "master.polyphony", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Global", "kParamBendRangeUp", "master.bend_range", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Global", "kParamLegato", "master.legato", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Global", "kParamPortamentoTime", "master.portamento.time", Conv::ms, 1, 0, X, 0, nullptr, "" },
    { "Global", "kParamPortamentoCurve", "master.portamento.slope", Conv::lin, 0.08, 0, C, 0, nullptr, "portamento_curve_scale_assumed" },
    { "Global", "kParamPortaAlways", "master.portamento.always", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Global", "kParamPortaScaled", "master.portamento.scaled", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Global", "kParamTranspose", "master.transpose", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Global", "kParamMasterTuning", "master.tune", Conv::lin, 1, 0, C, 0, nullptr, "master_tuning_unit_assumed" },
    { "Global", "kParamGlobalTuning", "master.tuning.a4", Conv::lin, 1, 0, X, 440, nullptr, "" },
    { "Global", "kParamDirectVol", "direct.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
    { "Global", "kParamFXBus1Vol", "bus1.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
    { "Global", "kParamFXBus2Vol", "bus2.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
    { "Global", "kParamFXBus1Dest", "bus1.destination", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Global", "kParamFXBus2Dest", "bus2.destination", Conv::lin, 1, 0, X, 0, nullptr, "" },
};

const Rule kRouting[] = {
    { "Routing", "kParamFXBus1Level", "osc#.bus1_send", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Routing", "kParamFXBus2Level", "osc#.bus2_send", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Routing", "kParamFilterBalance", "osc#.filter_blend", Conv::lin, 0.5, 50, X, -100, nullptr, "" },
};

} // namespace

const Rule* rulesOf (const char* scope, size_t& count) noexcept
{
    count = 0;
    if (! std::strcmp (scope, "Osc")) { count = sizeof (kOsc) / sizeof (Rule); return kOsc; }
    if (! std::strcmp (scope, "Warp")) { count = sizeof (kWarp) / sizeof (Rule); return kWarp; }
    if (! std::strcmp (scope, "WT")) { count = sizeof (kWt) / sizeof (Rule); return kWt; }
    if (! std::strcmp (scope, "Multi")) { count = sizeof (kMulti) / sizeof (Rule); return kMulti; }
    if (! std::strcmp (scope, "Noise") || ! std::strcmp (scope, "NoiseOsc")) { count = sizeof (kNoise) / sizeof (Rule); return kNoise; }
    if (! std::strcmp (scope, "Sub") || ! std::strcmp (scope, "SubOsc")) { count = sizeof (kSub) / sizeof (Rule); return kSub; }
    if (! std::strcmp (scope, "Filter")) { count = sizeof (kFilter) / sizeof (Rule); return kFilter; }
    if (! std::strcmp (scope, "Env")) { count = sizeof (kEnv) / sizeof (Rule); return kEnv; }
    if (! std::strcmp (scope, "Macro")) { count = sizeof (kMacro) / sizeof (Rule); return kMacro; }
    if (! std::strcmp (scope, "Global")) { count = sizeof (kGlobal) / sizeof (Rule); return kGlobal; }
    if (! std::strcmp (scope, "Routing")) { count = sizeof (kRouting) / sizeof (Rule); return kRouting; }
    return nullptr;
}

double convert (const Rule& r, double v) noexcept
{
    switch (r.conv)
    {
        case Conv::lin: return v * r.a + r.b;
        case Conv::dbFromLin: return v <= 1e-6 ? -120.0 : 20.0 * std::log10 (v) * r.a + r.b;
        case Conv::ms: return v * 1000.0;
        case Conv::noteHz: return 440.0 * std::pow (2.0, (135.0 * v - 69.0) / 12.0);
        case Conv::boolean: return v >= 0.5 ? 1.0 : 0.0;
        case Conv::curve50: return (50.0 - v) * r.a;
        case Conv::logPercent: return r.a * std::pow (r.b / r.a, v / 100.0);
        case Conv::logPercentInv: return r.b * std::pow (r.a / r.b, v / 100.0);
        case Conv::logToPercent: return v <= r.a ? 0.0 : 100.0 * std::log (v / r.a) / std::log (r.b / r.a);
        case Conv::ratioPercent: return v <= 1.0 ? 0.0 : 100.0 * (1.0 - 1.0 / v);
        case Conv::enumName: return 0.0;
    }
    return v;
}

// ----- filter types ----------------------------------------------------------------------------------------------------------------------
namespace
{
struct FilterName { const char* name; int type; Fid fid; }; // type = index of filters::FilterType (core/filters-serum)
// FilterType order: Normal 0..17 (MGLow6 MGLow12 MGLow18 MGLow24 Low6..24 High6..24 Band12 Band24 Peak12 Peak24 Notch12 Notch24), Multi 18..33 (LH LB LP LN HB HP HN BP BN PP PN NN LBH LPH LNH BPN),
// Flanges 34..42 (CmbL FlgL PhsL CmbH FlgH PhsH CmbHL FlgHL PhsHL), Misc 43..68 (LowEQ6 LowEQ12 BandEQ6 BandEQ12 HighEQ6 HighEQ12 RingMod RingModX2 SampHold SampHold- Combs Allpasses Reverb
// FrenchLP GermanLP AddBass FormantI II III Bandreject DistComb1LP 1BP 2LP 2BP ScreamLP ScreamBP), New 69..79 (NewWsp DJMixerXfer Diffusor MGLadder AcidLadder EMSLadder MGDirty PZSVF Comb2 ExpMM ExpBPF)
const FilterName kFilterNames[] = {
    { "MgL6", 0, X }, { "MgL12", 1, X }, { "MgL18", 2, X }, { "MgL24", 3, X }, { "L6", 4, X }, { "L12", 5, X }, { "L18", 6, X }, { "L24", 7, X },
    { "H6", 8, X }, { "H12", 9, X }, { "H18", 10, X }, { "H24", 11, X }, { "B12", 12, X }, { "B24", 13, X }, { "P12", 14, X }, { "P24", 15, X }, { "N12", 16, X }, { "N24", 17, X },
    { "LH6", 18, A }, { "LH12", 18, X }, { "LB12", 19, X }, { "LP12", 20, X }, { "LN12", 21, X }, { "HB12", 22, X }, { "HP12", 23, X }, { "HN12", 24, X }, { "BP12", 25, X },
    { "BN12", 26, X }, { "PP12", 27, X }, { "PN12", 28, X }, { "NN12", 29, X },
    { "LBH12", 30, X }, { "LBH24", 30, A }, { "LPH12", 31, X }, { "LPH24", 31, A }, { "LNH12", 32, X }, { "LNH24", 32, A }, { "BPN12", 33, X }, { "BPN24", 33, A },
    { "CombP", 34, A }, { "CombN", 34, A }, { "CombL6P", 34, X }, { "CombL6N", 34, A }, { "CombH6P", 37, X }, { "CombH6N", 37, A }, { "CombHL6P", 40, X }, { "CombHL6N", 40, A },
    { "FlangeP", 35, A }, { "FlangeN", 35, A }, { "FlangeL6P", 35, X }, { "FlangeL6N", 35, A }, { "FlangeH6P", 38, X }, { "FlangeH6N", 38, A }, { "FlangeHL6P", 41, X }, { "FlangeHL6N", 41, A },
    { "Phase12P", 36, A }, { "Phase12N", 36, A }, { "Phase24P", 36, A }, { "Phase24N", 36, A }, { "Phase36P", 36, A }, { "Phase36N", 36, A }, { "Phase48P", 36, A }, { "Phase48N", 36, A },
    { "Phase48L6P", 36, X }, { "Phase48L6N", 36, A }, { "Phase48H6P", 39, X }, { "Phase48H6N", 39, A }, { "Phase48HL6P", 42, X }, { "Phase48HL6N", 42, A },
    { "FlangePhase12HL6P", 42, A }, { "FlangePhase12HL6N", 42, A },
    { "LEQ6", 43, X }, { "LEQ12", 44, X }, { "BEQ12", 46, X }, { "HEQ6", 47, X }, { "HEQ12", 48, X }, { "RM", 49, X }, { "RMT", 50, A }, { "SNH1", 51, X }, { "SNH2", 52, X },
    { "Combs", 53, X }, { "Allpasses", 54, X }, { "Reverb1", 55, X }, { "Scream", 68 - 1, A }, { "ZDF_A", 57, A }, { "ADD_BASS", 58, X },
    { "FormantONE", 59, X }, { "FormantTWO", 60, X }, { "FormantTWB", 61, A }, { "BandReject", 62, X },
    { "DistComb1LP", 63, X }, { "DistComb1BP", 64, X }, { "DistComb2LP", 65, X }, { "DistComb2BP", 66, X }, { "Scream3LP", 67, X }, { "Scream3BP", 68, X },
    { "Wsp", 69, X }, { "DJMixer", 70, X }, { "Diffuser", 71, X }, { "LadderMg", 72, X }, { "LadderAcid", 73, X }, { "LadderEMS", 74, X }, { "DirtyMg", 75, X }, { "PZ_SVF", 76, X },
    { "Comb2", 77, X }, { "Exp", 78, X }, { "ExpBPF", 79, X }, { "K35", 5, A },
};
} // namespace

bool filterTypeFromName (const std::string& name, int& model, int& style, Fid& fid) noexcept
{
    for (const FilterName& f : kFilterNames)
        if (name == f.name)
        {
            for (int m = filters::kFirstSerumModel; m <= filters::kLastSerumModel; ++m)
                for (int s = 0; s < filters::serumModelStyleCount (m); ++s)
                    if (filters::serumFilterTypeOf (m, s) == f.type)
                    {
                        model = m;
                        style = s;
                        fid = f.fid;
                        return true;
                    }
            return false;
        }
    return false;
}

// ----- modulation sources ----------------------------------------------------------------------------------------------------------------
// Serum 2 id (the order of the source menu: Mod Wheel, Env 1-4, LFO 1-10, Velo, Note#, Aftertouch, Poly Aftertch, Noise OSC, NoteOn Rand1, Rand2, Alt., Alt.2, Macro 1-8,
// Pitch Bend, Expr X, Y, Z, Release Velo, Fixed, LFO 1-10 Y, OSC A, B, C, SUB, Filter 1, 2, Active Voices, Voice Mod 1, 2, Voice Index, NoteOn Rand (Discrete)).
int sourceChoiceOf (int id, const char*& name, Fid& fid) noexcept
{
    fid = Fid::exact;
    name = "";
    const auto keyChoice = [&] (const char* key) -> int
    {
        name = key;
        const ModSourceInfo* t = modSourceTable();
        for (size_t i = 0; i < modSourceCount(); ++i)
            if (std::strcmp (t[i].key, key) == 0)
                return static_cast<int> (i) + 1;
        return -1;
    };
    switch (id)
    {
        case 0: name = "none"; return 0;
        case 1: return keyChoice ("mod_wheel");
        case 2: return keyChoice ("env1");
        case 3: return keyChoice ("env2");
        case 4: return keyChoice ("env3");
        case 5: return keyChoice ("env4");
        case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: // LFO 1..8
        {
            static const char* k[] = { "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "lfo7", "lfo8" };
            return keyChoice (k[id - 6]);
        }
        case 14: return keyChoice ("lfo9");
        case 15: return keyChoice ("lfo10");
        case 16: return keyChoice ("velocity");
        case 17: return keyChoice ("note");
        case 18: return keyChoice ("aftertouch");
        case 19: return keyChoice ("poly_aftertouch");
        case 21: return keyChoice ("random");
        case 22: fid = Fid::approx; return keyChoice ("random"); // Rand 2: a second per-note random number, the same source here
        case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32:
        {
            static const char* k[] = { "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8" };
            return keyChoice (k[id - 25]);
        }
        case 33: return keyChoice ("pitch_wheel");
        case 35: return keyChoice ("slide");           // Expr Y (Timbre)
        case 36: fid = Fid::approx; return keyChoice ("poly_aftertouch"); // Expr Z (pressure)
        case 37: return keyChoice ("lift");
        case 59: fid = Fid::approx; return keyChoice ("random"); // Rand (Discrete)
        default: break;
    }
    name = "";
    return -1;
}

// ----- FX --------------------------------------------------------------------------------------------------------------------------------
int fxTypeOf (const std::string& n) noexcept
{
    if (n == "FXChorus") return kFxTypeChorus;
    if (n == "FXComp") return kFxTypeCompressor;
    if (n == "FXDelay") return kFxTypeDelay;
    if (n == "FXDistortion") return kFxTypeDistortion;
    if (n == "FXEQ") return kFxTypeEq;
    if (n == "FXFilter") return kFxTypeFilter;
    if (n == "FXFlanger") return kFxTypeFlanger;
    if (n == "FXPhaser") return kFxTypePhaser;
    if (n == "FXReverb") return kFxTypeReverb;
    if (n == "FXHyperD") return kFxTypeHyperDimension;
    if (n == "FXUtils") return kFxTypeUtility;
    if (n == "FXBode") return kFxTypeBode;
    if (n == "FXConv") return kFxTypeConvolve;
    return 0;
}

namespace
{
const Rule kChorus[] = {
    { "FX", "kParamWet", "chorus.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRate", "chorus.rate", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamDelay", "chorus.delay1", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamDelay2", "chorus.delay2", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamDepth", "chorus.depth", Conv::lin, 4, 0, A, kNaN, nullptr, "chorus_depth_scale_assumed" },
    { "FX", "kParamFeedback", "chorus.feedback", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFilt", "chorus.filter_cutoff", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "chorus.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kComp[] = {
    { "FX", "kParamWet", "compressor.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamAttack", "compressor.attack", Conv::logToPercent, 0.1, 1000, A, kNaN, nullptr, "compressor_time_scale_assumed" },
    { "FX", "kParamRelease", "compressor.release", Conv::logToPercent, 0.1, 1000, A, kNaN, nullptr, "compressor_time_scale_assumed" },
    { "FX", "kParamMultiband", "compressor.bands", Conv::lin, -3, 3, A, kNaN, nullptr, "compressor_band_layout" },
    { "FX", "kParamThresh", "compressor.low.upper_threshold", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_single_band_to_all_bands" },
    { "FX", "kParamThresh", "compressor.mid.upper_threshold", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_single_band_to_all_bands" },
    { "FX", "kParamThresh", "compressor.high.upper_threshold", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_single_band_to_all_bands" },
    { "FX", "kParamRatio", "compressor.low.upper_ratio", Conv::ratioPercent, 1, 0, A, kNaN, nullptr, "compressor_ratio_scale_assumed" },
    { "FX", "kParamRatio", "compressor.mid.upper_ratio", Conv::ratioPercent, 1, 0, A, kNaN, nullptr, "compressor_ratio_scale_assumed" },
    { "FX", "kParamRatio", "compressor.high.upper_ratio", Conv::ratioPercent, 1, 0, A, kNaN, nullptr, "compressor_ratio_scale_assumed" },
    { "FX", "kParamMakeup", "compressor.low.gain", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_makeup_to_bands" },
    { "FX", "kParamMakeup", "compressor.mid.gain", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_makeup_to_bands" },
    { "FX", "kParamMakeup", "compressor.high.gain", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_makeup_to_bands" },
    { "FX", "kParamXoverLow", "compressor.crossover_low", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamXoverHi", "compressor.crossover_high", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "compressor.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kDelay[] = {
    { "FX", "kParamWet", "delay.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamTimeL", "delay.time", Conv::ms, 1, 0, C, kNaN, nullptr, "delay_tempo_sync_flattened" },
    { "FX", "kParamTimeR", "delay.time2", Conv::ms, 1, 0, C, kNaN, nullptr, "delay_tempo_sync_flattened" },
    { "FX", "kParamFeedback", "delay.feedback", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFreq", "delay.filter_cutoff", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamBW", "delay.filter_spread", Conv::lin, 12.12, 0, A, kNaN, nullptr, "delay_bandwidth_as_spread" },
    { "FX", "kParamMode", "delay.style", Conv::lin, 1, 1, A, kNaN, nullptr, "delay_mode_nearest_style" },
    { "FX", "kParamHQ", "delay.hq", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamOffsetL", "delay.offset", Conv::lin, 1, 0, A, kNaN, nullptr, "delay_offset_left_only" },
    { "FX", "kParamLevelOut", "delay.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kDist[] = {
    { "FX", "kParamWet", "distortion.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDrive", "distortion.drive", Conv::lin, 0.3, 0, A, kNaN, nullptr, "distortion_drive_scale_assumed" },
    { "FX", "kParamMode", "distortion.type", Conv::enumName, 1, 0, X, 6, &kFxDistortionEnum, "" },
    { "FX", "kParamFreq", "distortion.filter_cutoff", Conv::noteHz, 1, 0, C, kNaN, nullptr, "filter_frequency_scale_assumed" },
    { "FX", "kParamLPHP", "distortion.filter_blend", Conv::lin, 1, 0, A, kNaN, nullptr, "distortion_lphp_as_blend" },
    { "FX", "kParamPrePost", "distortion.filter_order", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "distortion.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kFxFilter[] = {
    { "FX", "kParamWet", "fxfilter.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFreq", "fxfilter.cutoff", Conv::noteHz, 1, 0, C, kNaN, nullptr, "filter_frequency_scale_assumed" },
    { "FX", "kParamReso", "fxfilter.resonance", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDrive", "fxfilter.drive", Conv::lin, 0.2, 0, C, kNaN, nullptr, "filter_drive_scale_assumed" },
    { "FX", "kParamVar", "fxfilter.var", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamStereo", "fxfilter.pan", Conv::lin, 2, -100, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "fxfilter.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
    { "FX", "kParamPad", "fxfilter.clean_mode", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamX", "fxfilter.formant.x", Conv::lin, 100, 0, C, kNaN, nullptr, "filter_xy_as_formant" },
    { "FX", "kParamY", "fxfilter.formant.y", Conv::lin, 100, 0, C, kNaN, nullptr, "filter_xy_as_formant" },
};
const Rule kFlanger[] = {
    { "FX", "kParamWet", "flanger.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRate", "flanger.rate", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamDepth", "flanger.depth", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFeedback", "flanger.feedback", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamWidth", "flanger.phase_offset", Conv::lin, 1, 0, C, kNaN, nullptr, "flanger_width_as_phase_offset" },
    { "FX", "kParamLevelOut", "flanger.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kPhaser[] = {
    { "FX", "kParamWet", "phaser.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRate", "phaser.rate", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamDepth", "phaser.depth", Conv::lin, 0.48, 0, A, kNaN, nullptr, "phaser_depth_scale_assumed" },
    { "FX", "kParamFeedback", "phaser.feedback", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFreq", "phaser.center", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamWidth", "phaser.phase_offset", Conv::lin, 1, 0, C, kNaN, nullptr, "phaser_width_as_phase_offset" },
    { "FX", "kParamLevelOut", "phaser.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kReverb[] = {
    { "FX", "kParamWet", "reverb.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamType", "reverb.type", Conv::enumName, 1, 0, X, 1, &kReverbTypeEnum, "" },
    { "FX", "kParamSize", "reverb.size", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamPreDelay", "reverb.predelay", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDelay", "reverb.predelay", Conv::lin, 1, 0, A, kNaN, nullptr, "reverb_delay_as_predelay" },
    { "FX", "kParamFeedback", "reverb.decay", Conv::logPercent, 20, 30000, A, kNaN, nullptr, "reverb_feedback_as_decay" },
    { "FX", "kParamFreq", "reverb.pre_low_cut", Conv::logPercent, 20, 12000, A, kNaN, nullptr, "reverb_cut_scale_assumed" },
    { "FX", "kParamFreqB", "reverb.pre_high_cut", Conv::logPercentInv, 20, 12000, A, kNaN, nullptr, "reverb_cut_scale_assumed" },
    { "FX", "kParamWidth", "reverb.width", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "reverb.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kHyper[] = {
    { "FX", "kParamWet", "hyperdim.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRate", "hyperdim.rate", Conv::lin, 0.1, 0, A, kNaN, nullptr, "hyper_rate_scale_assumed" },
    { "FX", "kParamDetune", "hyperdim.detune", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamUnison", "hyperdim.unison", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRetrig", "hyperdim.retrig", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDimESize", "hyperdim.dim_size", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDimEWet", "hyperdim.dim_mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "hyperdim.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kUtils[] = {
    { "FX", "kParamWet", "utility.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamWidth", "utility.width", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamBalance", "utility.pan", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamHPF", "utility.hpf", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLPF", "utility.lpf", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLFMono", "utility.mono_bass", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLFXover", "utility.mono_freq", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamPolarityL", "utility.polarity_l", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamPolarityR", "utility.polarity_r", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "utility.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kBode[] = {
    { "FX", "kParamWet", "bode.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRange", "bode.range", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamBlur", "bode.blur", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFeedback", "bode.feed", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDelayTime", "bode.delay", Conv::ms, 1, 0, C, kNaN, nullptr, "delay_tempo_sync_flattened" },
    { "FX", "kParamDelayBalance", "bode.balance", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamOutputWidth", "bode.width", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRetrig", "bode.retrig", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamMonoInput", "bode.mono_input", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "bode.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
const Rule kConv[] = {
    { "FX", "kParamWet", "convolve.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamSize", "convolve.size", Conv::lin, 0.1, 0, A, kNaN, nullptr, "convolve_size_scale_assumed" },
    { "FX", "kParamTone", "convolve.tone", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamMinPhase", "convolve.phi_min", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamPredelay", "convolve.predelay", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamAttack", "convolve.attack", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDecay", "convolve.decay", Conv::lin, 2.5, 0, A, kNaN, nullptr, "convolve_decay_scale_assumed" },
    { "FX", "kParamDamping", "convolve.damp", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamIpTrim", "convolve.ir_gain", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "convolve.level", Conv::dbFromLin, 1, 0, X, 1, nullptr, "" },
};
#define FXTAB(a) { a, sizeof (a) / sizeof (Rule) }
struct Tab { const Rule* r; size_t n; };
const Tab kFxTabs[] = { { nullptr, 0 }, FXTAB (kChorus), FXTAB (kComp), FXTAB (kDelay), FXTAB (kDist), { nullptr, 0 }, FXTAB (kFxFilter), FXTAB (kFlanger), FXTAB (kPhaser),
                        FXTAB (kReverb), FXTAB (kHyper), FXTAB (kUtils), FXTAB (kBode), FXTAB (kConv) };
} // namespace

const Rule* fxRulesOf (int fxType, size_t& count) noexcept
{
    count = 0;
    if (fxType < 1 || fxType >= kNumFxInstanceTypes) return nullptr;
    count = kFxTabs[fxType].n;
    return kFxTabs[fxType].r;
}
} // namespace synthyboy::serumimport
