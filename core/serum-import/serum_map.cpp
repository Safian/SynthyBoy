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

// Gain staging against the real Serum 2.1.5 (tools/ref-compare/experiment.py, REFCOMPARE-REPORT.md): in Serum the file values of the master, oscillator, sub and noise volume are linear
// gains and the chain is wave * level * master; the velocity of a note does not scale it (the importer sets master.velocity_track to 0). Our engine renders the same chain 2.98 dB
// (oscillators with a Serum table, whatever the table and the position; the noise samples: the same) and -0.67 dB (sub) quieter than that, so the master carries the +2.98 dB and the sub
// level is trimmed by the difference (its percent knob has the headroom; the master range ends at +6 dB, a Serum master of up to 1 is +2.98 dB at most).
constexpr double kMasterTrimDb = 2.98;
constexpr double kSubTrim = 0.6570;    // 10 ^ ((-0.67 - 2.98) / 20)
constexpr double kLevelOutDb = 12.0412;   // 40 log10 (2): the filter output level of Serum is (2 v) ^ 2
constexpr double kLevelKnobDb = 6.0206;   // 20 log10 (2): the direct / bus level knobs of Serum are a linear gain times 2 (default 0.5 = unity), measured against Serum 2.1.5
constexpr double kNoiseTrim = 1.057;   // the noise samples (10 samples, constant gap) measured through the same chain: +0.48 dB
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
      { "kPD_OSC", 47, X }, { "kPD_OSC2", 48, X }, { "kSelfPD", 49, X }, { "kAM_OSC", 50, X }, { "kAM_OSC2", 51, X }, { "kRM_OSC", 9, C }, { "kRM_OSC2", 10, C },
      // GROUPB WP-06: the modes from the noise, the sub and the two filters (entries 52 .. 75, four per family: NOISE, SUB, FILT1, FILT2)
      { "kFM_NOISE", 52, C }, { "kFM_SUB", 53, C }, { "kFM_FILT1", 54, C }, { "kFM_FILT2", 55, C }, { "kFMX_NOISE", 56, C }, { "kFMX_SUB", 57, C }, { "kFMX_FILT1", 58, C }, { "kFMX_FILT2", 59, C },
      { "kFMP_NOISE", 60, C }, { "kFMP_SUB", 61, C }, { "kFMP_FILT1", 62, C }, { "kFMP_FILT2", 63, C }, { "kPD_NOISE", 64, X }, { "kPD_SUB", 65, X }, { "kPD_FILT1", 66, X }, { "kPD_FILT2", 67, X },
      { "kAM_NOISE", 68, X }, { "kAM_SUB", 69, X }, { "kAM_FILT1", 70, X }, { "kAM_FILT2", 71, X }, { "kRM_NOISE", 72, C }, { "kRM_SUB", 73, C }, { "kRM_FILT1", 74, C }, { "kRM_FILT2", 75, C },
      // GROUPC WP-12 (A419 .. A426): the spectral warps of the spectral oscillator (entries 76 .. 98). The names are Serum's, the algorithms are ours (the manual does not describe them): close.
      { "kSpectralComb", 76, C }, { "kAddharmonics", 77, C }, { "kAddsubharmonics", 78, C }, { "kDetune", 79, C }, { "kShepardNarrow", 80, C }, { "kShepardFilter", 81, C },
      { "kPeakOctaveDown", 82, C }, { "kPeakOctaveUp", 83, C }, { "kPeakHarmDown", 84, C }, { "kPeakHarmUp", 85, C }, { "kGate", 86, C }, { "kSpectralPitchShift", 87, C },
      { "kSpectralPhaseTwist", 88, C }, { "kSpread", 89, C }, { "kSpectralShift", 90, C }, { "kSmear", 91, C }, { "kMirror", 92, C },
      { "kMask_OSC", 93, C }, { "kMask_OSC2", 94, C }, { "kMask_NOISE", 95, C }, { "kVocode_OSC", 96, C }, { "kVocode_OSC2", 97, C }, { "kVocode_NOISE", 98, C });

ENUM (kOscTypeEnum, 0, "osc.type", { "kOsc_WT", 0, X }, { "kOsc_Sample", 1, X }, { "kOsc_MultiSample", 4, X }, { "kOsc_Granular", 2, X }, { "kOsc_Spectral", 3, X });
// GROUPC WP-11 (A412 .. A418): the granular oscillator. Only the names kWindowBlackmanHarris / ExpDec / Gaussian / Triangle / Tukey, kDensityFree / BPM, kLengthBPM, kEven /
// kExponential / kRandom and kYAxisOscVolume occur in the 656 factory presets; the others are the names in the pattern of those (close, not verified against a file).
ENUM (kGrainWindowEnum, 0, "gran.window", { "kWindowHann", 0, C }, { "kWindowWelch", 1, C }, { "kWindowGaussian", 2, X }, { "kWindowBlackmanHarris", 3, X }, { "kWindowSinc", 4, C },
      { "kWindowTukey", 5, X }, { "kWindowTriangle", 6, X }, { "kWindowTrapezoid", 7, C }, { "kWindowExpDec", 8, X }, { "kWindowExpDecRev", 9, C }, { "kWindowExpDecReverse", 9, C });
ENUM (kSpawnPatternEnum, 0, "gran.spawn", { "kTogether", 0, C }, { "kEven", 1, X }, { "kExponential", 2, X }, { "kRandom", 3, X });
ENUM (kYAxisEnum, 0, "gran.yaxis", { "kYAxisNone", 0, C }, { "kYAxisOscVolume", 1, X }, { "kYAxisWarp", 2, C }, { "kYAxisWarp2", 3, C }, { "kYAxisDensity", 4, C }, { "kYAxisGrainLength", 5, C },
      { "kYAxisWindowAmount", 6, C }, { "kYAxisWindowSkew", 7, C }, { "kYAxisRandomOffset", 8, C }, { "kYAxisRandomDir", 9, C }, { "kYAxisRandomPitch", 10, C }, { "kYAxisRandomGrainLength", 11, C },
      { "kYAxisRandomPan", 12, C }, { "kYAxisRandomGain", 13, C }, { "kYAxisRandomWindowAmount", 14, C }, { "kYAxisRandomWindowSkew", 15, C }, { "kYAxisRandomWarp", 16, C }, { "kYAxisRandomWarp2", 17, C });
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
    { "Osc", "kParamVolume", "osc#.level", Conv::lin, 100, 0, X, 0.5625, nullptr, "" },   // measured against Serum 2.1.5: the file value is the linear gain, default 0.75^2
    { "Osc", "kParamPan", "osc#.pan", Conv::lin, 2, 0, X, 0, nullptr, "" },
    { "Osc", "kParamFine", "osc#.tune", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamCoarsePit", "osc#.pitch_coarse", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Osc", "kParamUnison", "osc#.unison.voices", Conv::lin, 1, 0, X, 1, nullptr, "" },
    { "Osc", "kParamDetune", "osc#.unison.detune", Conv::detuneLaw, 1, 0, X, 0.25, nullptr, "" },   // measured against Serum 2.1.5: the outer voices are 200 * d ^ 1.25 cents (d = 0.25 by default), the voices evenly spaced
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
    // GROUPC WP-13 (A427 .. A434, 13-new-keys.md 4.39): the envelope override, the velocity tracking and the timbre of the multisample block
    { "Multi", "kParamEnvOverride", "osc#.multi.override", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamEnvDelay", "osc#.multi.delay", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamEnvAttack", "osc#.multi.attack", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamEnvHold", "osc#.multi.hold", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamEnvDecay", "osc#.multi.decay", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamEnvSustain", "osc#.multi.sustain", Conv::lin, 100, 0, C, kNaN, nullptr, "multisample_sustain_is_a_fraction_assumed" },
    { "Multi", "kParamEnvRelease", "osc#.multi.release", Conv::ms, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamVelTrack", "osc#.multi.vel_track", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamVelTrackOverride", "osc#.multi.vel_override", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "Multi", "kParamTimbreShift", "osc#.multi.timbre", Conv::lin, 100, 0, C, kNaN, nullptr, "multisample_timbre_shift_is_minus_one_to_one_assumed" },
};

// GROUPC WP-11 (A412 .. A418, 13-new-keys.md 4.37): the type specific block of a granular oscillator. Density, grain length and their modes are read in code (serum_voice.cpp).
const Rule kGranular[] = {
    { "Granular", "kParamWindowParam", "osc#.gran.window_amount", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamWindowSkew", "osc#.gran.window_skew", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamWindowShape", "osc#.gran.window_shape", Conv::enumName, 1, 0, X, kNaN, &kGrainWindowEnum, "" },
    { "Granular", "kParamUnisonTrigPattern", "osc#.gran.spawn_pattern", Conv::enumName, 1, 0, X, kNaN, &kSpawnPatternEnum, "" },
    { "Granular", "kParamYAxisAssignment", "osc#.gran.y_axis", Conv::enumName, 1, 0, X, kNaN, &kYAxisEnum, "" },
    { "Granular", "kParamRandomOffset", "osc#.gran.rand_offset", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomDir", "osc#.gran.rand_dir", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomPitch", "osc#.gran.rand_pitch", Conv::lin, 1, 0, C, kNaN, nullptr, "grain_pitch_randomisation_in_semitones_assumed" },
    { "Granular", "kParamRandomGrainLength", "osc#.gran.rand_length", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomPan", "osc#.gran.rand_pan", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomGain", "osc#.gran.rand_level", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomWindowAmount", "osc#.gran.rand_window", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomWindowSkew", "osc#.gran.rand_skew", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomWarp", "osc#.gran.rand_warp", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Granular", "kParamRandomWarp2", "osc#.gran.rand_warp2", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
};

// GROUPC WP-12 (A419 .. A426, 13-new-keys.md 4.38): the type specific block of a spectral oscillator (the flex curve, the common block's Position / Manual / Hz offset are read in code)
const Rule kSpectral[] = {
    { "Spectral", "kParamFreqLo", "osc#.spec.freq_lo", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Spectral", "kParamFreqHi", "osc#.spec.freq_hi", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Spectral", "kParamLoHiIsSmooth", "osc#.spec.lohi_smooth", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "Spectral", "kParamLoHiPostWarp", "osc#.spec.lohi_post_warp", Conv::boolean, 1, 0, C, kNaN, nullptr, "post_warp_parameter_name_assumed" },
    { "Spectral", "kParamPhaseLock", "osc#.spec.phase_lock", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "Spectral", "kParamTransients", "osc#.spec.transients", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "Spectral", "kParamSpecFltShift", "osc#.spec.cut", Conv::lin, 1, 0, C, kNaN, nullptr, "spectral_filter_shift_is_cut" },
    { "Spectral", "kParamSpecFltWetDry", "osc#.spec.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
};

const Rule kNoise[] = {
    { "Noise", "kParamVolume", "noise.level", Conv::lin, 100 * kNoiseTrim, 0, X, 0.0625, nullptr, "" },   // default 0.25^2
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
    { "Sub", "kParamVolume", "sub.level", Conv::lin, 100 * kSubTrim, 0, X, 0.5625, nullptr, "" },
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
    { "Filter", "kParamFreq", "filter#.cutoff", Conv::noteHz, 1, 2.0, X, 0.5, nullptr, "" },   // measured against Serum 2.1.5 (MG Low 12, harmonic levels of a saw): 2 semitones above the 135 note scale, default 0.5
    { "Filter", "kParamReso", "filter#.resonance", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Filter", "kParamDrive", "filter#.drive", Conv::filterDrive, 1, 0, C, 0, nullptr, "filter_drive_fitted" },
    { "Filter", "kParamVar", "filter#.var", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "Filter", "kParamWet", "filter#.mix", Conv::lin, 1, 0, X, 100, nullptr, "" },
    { "Filter", "kParamKeyTrack", "filter#.keytrack", Conv::lin, 100, 0, X, 0, nullptr, "" },
    { "Filter", "kParamLevelOut", "filter#.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },   // measured against Serum 2.1.5: the gain is (2 v) ^ 2 (default 0.5 = unity, 1 = +12.04 dB, 0.25 = -12.04 dB)
    { "Filter", "kParamStereo", "filter#.pan", Conv::lin, 2, -100, X, 50, nullptr, "" },
    { "Filter", "kParamPad", "filter#.clean_mode", Conv::boolean, 1, 0, X, 0, nullptr, "" },
    { "Filter", "kParamX", "filter#.formant.x", Conv::lin, 100, 0, C, kNaN, nullptr, "filter_xy_as_formant" },
    { "Filter", "kParamY", "filter#.formant.y", Conv::lin, 100, 0, C, kNaN, nullptr, "filter_xy_as_formant" },
};

const Rule kEnv[] = {
    { "Env", "kParamAttack", "env#.attack", Conv::ms, 1, 0, X, 0.001, nullptr, "" },   // Serum 2.1.5 default of envelope 1 (envelopes 2 .. 4: 0.5 ms, serum_voice.cpp)
    { "Env", "kParamHold", "env#.hold", Conv::ms, 1, 0, X, 0, nullptr, "" },
    { "Env", "kParamDecay", "env#.decay", Conv::ms, 1, 0, X, 1.0, nullptr, "" },
    { "Env", "kParamSustain", "env#.sustain", Conv::lin, 100, 0, C, 1, nullptr, "sustain_linear_assumed" },
    { "Env", "kParamRelease", "env#.release", Conv::ms, 1, 0, X, 0.015, nullptr, "" },
    { "Env", "kParamCurve1", "env#.attack_curve", Conv::curveAttack, 1, 0, C, 50, nullptr, "env_curve_fitted" },
    { "Env", "kParamCurve2", "env#.decay_curve", Conv::curveDecay, 1, 0, C, 66.6, nullptr, "env_curve_fitted" },
    { "Env", "kParamCurve3", "env#.release_curve", Conv::curveDecay, 1, 0, C, 66.6, nullptr, "env_curve_fitted" },
    { "Env", "kParamLegatoInverted", "env#.legato_inverted", Conv::boolean, 1, 0, X, 0, nullptr, "" },
};

const Rule kMacro[] = { { "Macro", "kParamValue", "macro.#", Conv::lin, 1, 0, X, 0, nullptr, "" } };

const Rule kGlobal[] = {
    { "Global", "kParamMasterVolume", "master.volume", Conv::dbFromLin, 1, kMasterTrimDb, X, 0.25, nullptr, "" },
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
    { "Global", "kParamDirectVol", "direct.level", Conv::dbFromLin, 1, kLevelKnobDb, X, 0.5, nullptr, "" },   // the knob is a linear gain times 2: 0.5 (the default) is unity, 1 is +6.02 dB (measured against Serum 2.1.5)
    { "Global", "kParamFXBus1Vol", "bus1.level", Conv::dbFromLin, 1, kLevelKnobDb, X, 0.5, nullptr, "" },
    { "Global", "kParamFXBus2Vol", "bus2.level", Conv::dbFromLin, 1, kLevelKnobDb, X, 0.5, nullptr, "" },
    { "Global", "kParamFXBus1Dest", "bus1.destination", Conv::lin, 1, 0, X, 0, nullptr, "" },
    { "Global", "kParamFXBus2Dest", "bus2.destination", Conv::lin, 1, 0, X, 0, nullptr, "" },
};

const Rule kRouting[] = {
    { "Routing", "kParamFXBus1Level", "osc#.bus1_send", Conv::cubicPercent, 1, 0, X, 0, nullptr, "" },
    { "Routing", "kParamFXBus2Level", "osc#.bus2_send", Conv::cubicPercent, 1, 0, X, 0, nullptr, "" },
    { "Routing", "kParamFilterBalance", "osc#.filter_blend", Conv::lin, 0.5, 50, X, -100, nullptr, "" },
};

} // namespace

const Rule* rulesOf (const char* scope, size_t& count) noexcept
{
    count = 0;
    if (! std::strcmp (scope, "Osc")) { count = sizeof (kOsc) / sizeof (Rule); return kOsc; }
    if (! std::strcmp (scope, "Warp")) { count = sizeof (kWarp) / sizeof (Rule); return kWarp; }
    if (! std::strcmp (scope, "WT")) { count = sizeof (kWt) / sizeof (Rule); return kWt; }
    if (! std::strcmp (scope, "Granular")) { count = sizeof (kGranular) / sizeof (Rule); return kGranular; }
    if (! std::strcmp (scope, "Spectral")) { count = sizeof (kSpectral) / sizeof (Rule); return kSpectral; }
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

namespace
{
double interp (const double* xs, const double* ys, int n, double v) noexcept
{
    if (v <= xs[0]) return ys[0];
    if (v >= xs[n - 1]) return ys[n - 1];
    int i = 0;
    while (i + 2 < n && v > xs[i + 1]) ++i;
    return ys[i] + (ys[i + 1] - ys[i]) * (v - xs[i]) / (xs[i + 1] - xs[i]);
}
} // namespace

double envCurveOurs (bool attack, bool modulation, double v) noexcept
{
    // the curve value of our envelope that gives the same shape as the Serum curve knob v; amplitude = the dB envelope of envelope 1 (crafted presets, env time constants), modulation = the
    // centroid trajectory of a filter swept by envelope 2 (tools/ref-compare/experiment.py); Serum 50 is not our 0 in the amplitude path, it is in the modulation path
    static const double kV[] = { 0, 10, 30, 50, 66.6, 80, 95, 100 };
    static const double kAmpAttack[] = { -7.0, -5.0, -1.5, 2.5, 7.8, 12.0, 14.0, 14.5 };
    static const double kAmpDecay[] = { 13.0, 12.0, 8.5, 2.5, -1.0, -3.0, -8.0, -9.5 };
    static const double kModAttack[] = { -11.0, -9.0, -4.5, 0.5, 4.0, 8.0, 12.0, 13.0 };
    static const double kModDecay[] = { 12.0, 9.0, 5.0, 0.0, -3.5, -9.5, -15.0, -17.0 };
    const double* k = attack ? (modulation ? kModAttack : kAmpAttack) : (modulation ? kModDecay : kAmpDecay);
    return interp (kV, k, 8, v);
}

namespace
{
struct FilterLawRow { const char* type; float db[4][7]; };
const FilterLawRow kFilterLaws[] = {
#include "serum_filter_laws.inc"
};
} // namespace

double filterResoLevelDb (const std::string& type, double freq, double reso) noexcept
{
    // the level difference (Serum - ours) of the filter type by cutoff and resonance, measured on a C3 saw (tools/ref-compare/fit_filter.py, make_filter_laws.py): bilinear between the grid points
    static const double kF[4] = { 0.35, 0.5, 0.65, 0.8 };
    static const double kR[7] = { 0, 10, 20, 35, 50, 75, 100 };
    for (const FilterLawRow& row : kFilterLaws)
    {
        if (type != row.type) continue;
        // below the grid (cutoff under 0.35) the measurement is the level of a stop band (a few harmonics, some of them at the resonance peak): the correction fades out to 0 at 0.2
        const double edge = freq >= kF[0] ? 1.0 : std::max (0.0, (freq - 0.2) / (kF[0] - 0.2));
        const double f = std::min (kF[3], std::max (kF[0], freq)), r = std::min (kR[6], std::max (kR[0], reso));
        int fi = 0, ri = 0;
        while (fi < 2 && f > kF[fi + 1]) ++fi;
        while (ri < 5 && r > kR[ri + 1]) ++ri;
        const double tf = (f - kF[fi]) / (kF[fi + 1] - kF[fi]), tr = (r - kR[ri]) / (kR[ri + 1] - kR[ri]);
        const double a = row.db[fi][ri] + (row.db[fi][ri + 1] - row.db[fi][ri]) * tr;
        const double b = row.db[fi + 1][ri] + (row.db[fi + 1][ri + 1] - row.db[fi + 1][ri]) * tr;
        return (a + (b - a) * tf) * edge;
    }
    return 0.0;
}

namespace
{
struct FilterDriveLawRow { const char* type; float db[5][4]; };
const FilterDriveLawRow kFilterDriveLaws[] = {
#include "serum_filter_drive_laws.inc"
};
} // namespace

double filterDriveLevelDb (const std::string& type, double drive, double reso) noexcept
{
    // the level the drive adds on top of filterResoLevelDb and filterDriveMakeupDb (the ladders get much louder with drive and resonance together): bilinear between the grid points
    static const double kD[5] = { 0, 25, 50, 75, 100 };
    static const double kR[4] = { 0, 30, 60, 100 };
    for (const FilterDriveLawRow& row : kFilterDriveLaws)
    {
        if (type != row.type) continue;
        const double d = std::min (kD[4], std::max (kD[0], drive)), r = std::min (kR[3], std::max (kR[0], reso));
        int di = 0, ri = 0;
        while (di < 3 && d > kD[di + 1]) ++di;
        while (ri < 2 && r > kR[ri + 1]) ++ri;
        const double td = (d - kD[di]) / (kD[di + 1] - kD[di]), tr = (r - kR[ri]) / (kR[ri + 1] - kR[ri]);
        const double a = row.db[di][ri] + (row.db[di][ri + 1] - row.db[di][ri]) * tr;
        const double b = row.db[di + 1][ri] + (row.db[di + 1][ri + 1] - row.db[di + 1][ri]) * tr;
        return a + (b - a) * td;
    }
    return 0.0;
}

double filterDriveMakeupDb (double v) noexcept
{
    static const double kV[] = { 0, 10, 25, 50, 75, 100 };
    static const double kL[] = { 0.0, 3.3, 7.7, 10.7, 11.0, 10.2 };   // the level gap that remained after the drive was matched by shape
    return interp (kV, kL, 6, v);
}

double convert (const Rule& r, double v) noexcept
{
    switch (r.conv)
    {
        case Conv::lin: return v * r.a + r.b;
        case Conv::dbFromLin: return v <= 1e-6 ? -120.0 : 20.0 * std::log10 (v) * r.a + r.b;
        case Conv::ms: return v * 1000.0;
        case Conv::filterDrive:
        {
            // measured against Serum 2.1.5 (saw through MG Low 12, harmonic shape): the drive of our filter that gives the same harmonic shape; the level goes with filterDriveMakeupDb
            static const double kV[] = { 0, 10, 25, 50, 75, 100 };
            static const double kD[] = { 0.0, 0.0, 0.0, 2.0, 6.0, 12.0 };
            return interp (kV, kD, 6, v);
        }
        case Conv::curveAttack: return envCurveOurs (true, false, v);
        case Conv::curveDecay: return envCurveOurs (false, false, v);
        case Conv::compThresh:
        {
            // measured against Serum 2.1.5: the output peak of a limiter (ratio 1e6, 0.1 ms attack) at the threshold t, saw input
            static const double kT[] = { 0.375, 0.4, 0.425, 0.45, 0.475, 0.5, 0.525, 0.55, 0.575, 0.6, 0.625, 0.65, 0.675, 0.7, 0.725, 0.75 };
            static const double kDb[] = { -0.5, -4.1, -6.0, -7.9, -10.6, -13.6, -16.6, -19.6, -22.7, -26.4, -30.3, -34.3, -38.5, -41.5, -42.0, -42.3 };
            constexpr int n = static_cast<int> (sizeof kT / sizeof kT[0]);
            if (v <= kT[0]) return 0.0;
            if (v >= kT[n - 1]) return kDb[n - 1] + (v - kT[n - 1]) * 0.0;
            int i = 0;
            while (i + 2 < n && v > kT[i + 1]) ++i;
            return kDb[i] + (kDb[i + 1] - kDb[i]) * (v - kT[i]) / (kT[i + 1] - kT[i]);
        }
        case Conv::detuneLaw: return 100.0 * std::pow (std::max (0.0, v), 1.25);
        case Conv::cubicPercent: { const double x = v / 100.0; return 100.0 * x * x * x; }
        case Conv::noteHz: return 440.0 * std::pow (2.0, (135.0 * v - 69.0 + r.b) / 12.0);   // b = offset in semitones
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
        case 20: return keyChoice ("noise");           // GROUPB WP-06 (A401): the output of the noise oscillator
        case 21: return keyChoice ("random");
        case 22: return keyChoice ("note_on_rand2");   // Rand 2: a second per-note random number
        case 23: return keyChoice ("note_on_alt1");
        case 24: return keyChoice ("note_on_alt2");
        case 34: return keyChoice ("expression_x");
        case 38: return keyChoice ("fixed");
        case 49: return keyChoice ("osc_a");
        case 50: return keyChoice ("osc_b");
        case 51: return keyChoice ("osc_c");
        case 52: return keyChoice ("sub");
        case 53: return keyChoice ("filter1");
        case 54: return keyChoice ("filter2");
        case 55: return keyChoice ("active_voices");
        case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32:
        {
            static const char* k[] = { "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8" };
            return keyChoice (k[id - 25]);
        }
        case 33: return keyChoice ("pitch_wheel");
        case 35: return keyChoice ("slide");           // Expr Y (Timbre)
        case 36: fid = Fid::approx; return keyChoice ("poly_aftertouch"); // Expr Z (pressure)
        case 37: return keyChoice ("release_velocity");   // the same output as lift (Vital's name for the note-off velocity)
        case 56: return keyChoice ("voice_mod1");      // GROUPA2 (WP-07): the sources of the voice control
        case 57: return keyChoice ("voice_mod2");
        case 58: return keyChoice ("voice_index");
        case 59: return keyChoice ("note_on_rand_discrete"); // Rand (Discrete): a number of its own for every slot
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
    { "FX", "kParamLevelOut", "chorus.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
};
const Rule kComp[] = {
    { "FX", "kParamWet", "compressor.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamAttack", "compressor.attack", Conv::logToPercent, 0.1, 1000, A, kNaN, nullptr, "compressor_time_scale_assumed" },
    { "FX", "kParamRelease", "compressor.release", Conv::logToPercent, 0.1, 1000, A, kNaN, nullptr, "compressor_time_scale_assumed" },
    { "FX", "kParamMultiband", "compressor.bands", Conv::lin, -3, 3, A, kNaN, nullptr, "compressor_band_layout" },
    { "FX", "kParamThresh", "compressor.low.upper_threshold", Conv::compThresh, 1, 0, A, kNaN, nullptr, "compressor_single_band_to_all_bands" },
    { "FX", "kParamThresh", "compressor.mid.upper_threshold", Conv::compThresh, 1, 0, A, kNaN, nullptr, "compressor_single_band_to_all_bands" },
    { "FX", "kParamThresh", "compressor.high.upper_threshold", Conv::compThresh, 1, 0, A, kNaN, nullptr, "compressor_single_band_to_all_bands" },
    { "FX", "kParamRatio", "compressor.low.upper_ratio", Conv::ratioPercent, 1, 0, A, kNaN, nullptr, "compressor_ratio_scale_assumed" },
    { "FX", "kParamRatio", "compressor.mid.upper_ratio", Conv::ratioPercent, 1, 0, A, kNaN, nullptr, "compressor_ratio_scale_assumed" },
    { "FX", "kParamRatio", "compressor.high.upper_ratio", Conv::ratioPercent, 1, 0, A, kNaN, nullptr, "compressor_ratio_scale_assumed" },
    { "FX", "kParamMakeup", "compressor.low.gain", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_makeup_to_bands" },
    { "FX", "kParamMakeup", "compressor.mid.gain", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_makeup_to_bands" },
    { "FX", "kParamMakeup", "compressor.high.gain", Conv::dbFromLin, 1, 0, A, kNaN, nullptr, "compressor_makeup_to_bands" },
    { "FX", "kParamXoverLow", "compressor.crossover_low", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamXoverHi", "compressor.crossover_high", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "compressor.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
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
    // GROUPA1 (WP-10, A351): one scalar per channel (Serum manual p.166: 1.1 = 110 %), the same unit as ours
    { "FX", "kParamOffsetL", "delay.offset_left", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamOffsetR", "delay.offset", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "delay.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
};
const Rule kDist[] = {
    { "FX", "kParamWet", "distortion.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDrive", "distortion.drive", Conv::lin, 0.3, 0, A, kNaN, nullptr, "distortion_drive_scale_assumed" },
    { "FX", "kParamMode", "distortion.type", Conv::enumName, 1, 0, X, 6, &kFxDistortionEnum, "" },
    { "FX", "kParamFreq", "distortion.filter_cutoff", Conv::noteHz, 1, 0, C, kNaN, nullptr, "filter_frequency_scale_assumed" },
    { "FX", "kParamLPHP", "distortion.filter_blend", Conv::lin, 1, 0, A, kNaN, nullptr, "distortion_lphp_as_blend" },
    { "FX", "kParamPrePost", "distortion.filter_order", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "distortion.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
};
const Rule kFxFilter[] = {
    { "FX", "kParamWet", "fxfilter.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFreq", "fxfilter.cutoff", Conv::noteHz, 1, 0, C, kNaN, nullptr, "filter_frequency_scale_assumed" },
    { "FX", "kParamReso", "fxfilter.resonance", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDrive", "fxfilter.drive", Conv::lin, 0.2, 0, C, kNaN, nullptr, "filter_drive_scale_assumed" },
    { "FX", "kParamVar", "fxfilter.var", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamStereo", "fxfilter.pan", Conv::lin, 2, -100, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "fxfilter.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
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
    { "FX", "kParamLevelOut", "flanger.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
};
const Rule kPhaser[] = {
    { "FX", "kParamWet", "phaser.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRate", "phaser.rate", Conv::lin, 1, 0, C, kNaN, nullptr, "" },
    { "FX", "kParamDepth", "phaser.depth", Conv::lin, 0.48, 0, A, kNaN, nullptr, "phaser_depth_scale_assumed" },
    { "FX", "kParamFeedback", "phaser.feedback", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamFreq", "phaser.center", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamWidth", "phaser.phase_offset", Conv::lin, 1, 0, C, kNaN, nullptr, "phaser_width_as_phase_offset" },
    { "FX", "kParamLevelOut", "phaser.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
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
    { "FX", "kParamLevelOut", "reverb.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
};
const Rule kHyper[] = {
    { "FX", "kParamWet", "hyperdim.mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRate", "hyperdim.rate", Conv::lin, 0.1, 0, A, kNaN, nullptr, "hyper_rate_scale_assumed" },
    { "FX", "kParamDetune", "hyperdim.detune", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamUnison", "hyperdim.unison", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamRetrig", "hyperdim.retrig", Conv::boolean, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDimESize", "hyperdim.dim_size", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamDimEWet", "hyperdim.dim_mix", Conv::lin, 1, 0, X, kNaN, nullptr, "" },
    { "FX", "kParamLevelOut", "hyperdim.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
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
    { "FX", "kParamLevelOut", "utility.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
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
    { "FX", "kParamLevelOut", "bode.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
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
    { "FX", "kParamLevelOut", "convolve.level", Conv::dbFromLin, 2, kLevelOutDb, X, 0.5, nullptr, "" },
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
