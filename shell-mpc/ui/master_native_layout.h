// Edit this file directly.
// Tab coordinates (1280 x 628). The skin (skin/TUI.json) places the native controls at the same rectangles.
#pragma once
#include "core.h"
namespace synthyboy_mpc::ui::masterlayout {
constexpr Rect kVolume { 132, 98, 150, 152 };
constexpr Rect kStereoSpread { 286, 98, 150, 152 };
constexpr Rect kStereoRouting { 440, 98, 150, 152 };
constexpr Rect kVoiceOverride { 598, 128, 150, 80 };
constexpr Rect kQualityNoSmoothing { 380, 256, 180, 56 };
constexpr Rect kQualityOversample4x { 566, 256, 186, 56 };
constexpr Rect kTune { 784, 98, 116, 152 };
constexpr Rect kTranspose { 904, 98, 116, 152 };
constexpr Rect kBendRange { 1024, 98, 116, 152 };
constexpr Rect kBendDownRange { 1144, 98, 116, 152 };
constexpr Rect kBendDownLink { 1024, 256, 236, 56 };
constexpr Rect kPolyphony { 20, 360, 176, 80 };
constexpr Rect kLegato { 204, 384, 176, 56 };
constexpr Rect kPolyLegato { 388, 384, 176, 56 };
constexpr Rect kVoicePhaseLegato { 572, 384, 180, 56 };
constexpr Rect kVelocityTrack { 20, 446, 176, 80 };
constexpr Rect kVoiceSameNote { 204, 446, 268, 80 };
constexpr Rect kVoiceStealPriority { 480, 446, 272, 80 };
constexpr Rect kShellPadPressure { 20, 556, 176, 56 };
constexpr Rect kShellPadPressureAmount { 204, 532, 176, 80 };
constexpr Rect kPortamentoTime { 1028, 360, 116, 152 };
constexpr Rect kPortamentoSlope { 1148, 360, 112, 152 };
constexpr Rect kPortamentoAlways { 1028, 556, 116, 56 };
constexpr Rect kPortamentoScaled { 1148, 556, 112, 56 };
constexpr Rect kScaleType { 20, 102, 500, 80 };
constexpr Rect kScaleKey { 528, 102, 300, 80 };
constexpr Rect kScaleMode { 836, 102, 424, 80 };
constexpr Rect kTuningA4 { 20, 240, 300, 80 };
constexpr Rect kTuningScale { 328, 240, 400, 80 };
constexpr Rect kTuningMtsEsp { 736, 264, 256, 56 };
constexpr Rect kTuningMtsNoteOnOnly { 1000, 264, 260, 56 };
constexpr Rect kMpeOn { 20, 404, 404, 56 };
constexpr Rect kMpeBendRange { 432, 380, 404, 80 };
constexpr Rect kMpeXyzMacro { 844, 380, 416, 80 };
constexpr Rect kMpeYModwheel { 20, 510, 404, 56 };
constexpr Rect kMpeBendToX { 432, 510, 404, 56 };
constexpr Rect kMpeYBipolar { 844, 510, 416, 56 };
constexpr Rect kLinkedBend { 1024, 98, 236, 152 };
constexpr Rect kGlideCurve { 784, 360, 236, 252 };
constexpr Rect kScaleInput { 704, 4, 520, 56 };
constexpr Rect kOutputClip { 20, 98, 100, 56 };
constexpr Rect kOutputMeters { 20, 158, 100, 154 };
constexpr Rect kOutputLimiter { 132, 256, 140, 56 };
constexpr Rect kOutputVoiceMeter { 388, 550, 364, 44 };
constexpr int kBlocks = 7;
/** The titled block panels in reading order: OUTPUT, PITCH, VOICES, GLIDE, SCALE, TUNING, MPE. */
constexpr Rect kBlock[kBlocks] { Rect { 8, 66, 756, 252 }, Rect { 772, 66, 500, 252 }, Rect { 8, 326, 756, 294 }, Rect { 772, 326, 500, 294 }, Rect { 8, 66, 1264, 130 }, Rect { 8, 204, 1264, 130 }, Rect { 8, 342, 1264, 278 } };
constexpr bool kBlockOnTuning[kBlocks] { false, false, false, false, true, true, true };
}
