// SynthyBoy - append-only native skin parameters after Q 16.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

#include "native_lfo_controls.h"
#include "native_env_controls.h"
#include "native_master_controls.h"
#include "native_mod_controls.h"
#include "native_osc_controls.h"

namespace synthyboy_mpc::nativeui
{
constexpr int kMixerStrips = 10;
constexpr int kMixerPeakBase = 0;
constexpr int kMixerHoldBase = kMixerPeakBase + kMixerStrips;
constexpr int kEnvSelectorBase = kMixerHoldBase + kMixerStrips;
constexpr int kEnvSelectorCells = 6;
constexpr int kEnvSelectorLastFrame = 30;
constexpr int kEnvSelectorFrameBits = 5;
constexpr int kFxValueBase = kEnvSelectorBase + kEnvSelectorCells;
constexpr int kFxCells = 16;
constexpr int kFxKindBase = kFxValueBase + kFxCells;
constexpr int kFxLayout = kFxKindBase + kFxCells;
constexpr int kFxLayoutLast = 18;
constexpr int kFxDomainBase = kFxLayout + 1;
constexpr int kFxStyleModels = 27;
constexpr int kFxHidden = 0, kFxDial = 1, kFxToggle = 2, kFxFirstDomain = 3;
enum FxDomain
{
    kFxModel, kFxSync, kFxLongDivision, kFxBands, kFxShortDivision, kFxDelayRightDivision,
    kFxDelayStyle, kFxDistortion, kFxFilterOrder, kFxLowMode, kFxMidMode, kFxHighMode,
    kFxConvolveIr, kFxCompressorMode, kFxReverbType, kFxReverbRoom, kFxMid2Mode, kFxLaw,
    kFxVoices, kFxUnison, kFxConvolveLaw, kFxStyleBase,
};
constexpr int kFxDomains = kFxStyleBase + kFxStyleModels;
constexpr int kFxBipolar = kFxFirstDomain + kFxDomains;
constexpr int kFxKindLast = kFxBipolar;
constexpr int kLfoBase = kFxDomainBase + kFxDomains;
constexpr int kFilterBase = kLfoBase + lfo::kCount;
constexpr int kFilterValueBase = kFilterBase + 1;
constexpr int kFilterIndicatorBase = kFilterValueBase + 28;
constexpr int kOscBase = kFilterIndicatorBase + 5;
constexpr int kMasterView = kOscBase + osc::kCount;
constexpr int kModView = kMasterView + 1;
constexpr int kModRows = kModView + 1;
constexpr int kModFields = kModRows + mod::kRows;
constexpr int kEnvView = kModView + mod::kOffsets;
constexpr int kEnvFields = kEnvView + 1;
constexpr int kPlayLfoStandard = kEnvFields + env::kFields;
constexpr int kModNavigation = kPlayLfoStandard + 1;
constexpr int kVoiceSequence = kModNavigation + mod::kNavigation;
constexpr int kVoiceSequenceCount = 3;
constexpr int kOscPower = kVoiceSequence + kVoiceSequenceCount;
constexpr int kMasterOutput = kOscPower + osc::kLastSource;
constexpr int kOscSettingsView = kMasterOutput + master::kOutputFields;
constexpr int kOscSettings = kOscSettingsView + 1;
constexpr int kOscRoutingBySource = kOscSettings + osc::kSettings;
constexpr int kOscWarpLayout = kOscRoutingBySource + osc::kLastSource;
constexpr int kOscAbcSettings = kOscWarpLayout + 1;
namespace fxheader
{
enum Field { Power, Rack, Page, Remove, kFields };
constexpr int kItems = 32;
constexpr int kHandles = 2 + kItems * kFields;
}
constexpr int kFxHeader = kOscAbcSettings + osc::kAbcSources * osc::kAbcSettings;
constexpr int kLfoEditor = kFxHeader + fxheader::kHandles;
namespace lfoeditor
{
constexpr int kView = 0, kValues = 1, kAvailability = 23, kScale = 34, kHandles = 35;
}
constexpr int kFxChain = kLfoEditor + lfoeditor::kHandles;
constexpr int kFxChainHandles = 54;
constexpr int kLfoDetail = kFxChain + kFxChainHandles;
constexpr int kLfoDetailHandles = 17;
constexpr int kPlayEnvLfo = kLfoDetail + kLfoDetailHandles;
constexpr int kPlayEnvLfoHandles = 10;
constexpr int kPlayOsc = kPlayEnvLfo + kPlayEnvLfoHandles;
constexpr int kPlayOscHandles = 36;
constexpr int kPlayLfoStandardSync = kPlayOsc + kPlayOscHandles;
constexpr int kOscDetail = kPlayLfoStandardSync + 1;
constexpr int kOscDetailHandles = 110;
constexpr int kMasterPanel = kOscDetail + kOscDetailHandles;
constexpr int kFxBandBase = kMasterPanel + 1;
constexpr int kFxBandMode = kFxBandBase + fxheader::kItems;
constexpr int kPzAction = kFxBandMode + 1;
constexpr int kPzSource = kPzAction + 2;
constexpr int kPzAvailability = kPzSource + 1;
constexpr int kOscRootLoad = kPzAvailability + 1;
constexpr int kOscRootLoadView = kOscRootLoad + 3;
constexpr int kModTargets = kOscRootLoadView + 1;
constexpr int kModTargetsHandles = 35;
constexpr int kPlayExpanded = kModTargets + kModTargetsHandles;
constexpr int kPlayExpandedHandles = 4;
constexpr int kMacroTarget = kPlayExpanded + kPlayExpandedHandles;
constexpr int kMacroTargetCount = 8;
namespace mixer
{
constexpr int kSolo = 0;
constexpr int kSoloCount = 10;
constexpr int kMute = kSolo + kSoloCount;
constexpr int kMuteCount = 10;
constexpr int kHandles = kMute + kMuteCount;
constexpr int kLastVisualState = 4;
}
constexpr int kMixerControl = kMacroTarget + kMacroTargetCount;
// Preserve Mod worker's fixed slot range: 5 ModDetailControl handles + 1 reserved confirm guard.
constexpr int kModDetailReserved = kMixerControl + mixer::kHandles;
constexpr int kModDetailReservedCount = 6;
constexpr int kModDetailControl = kModDetailReserved;
// Read-only Mixer UI presentation handles appended after Mod's reserved range.
constexpr int kMixerSoloVisual = kModDetailReserved + kModDetailReservedCount;
constexpr int kMixerMuteVisual = kMixerSoloVisual + kMixerStrips;
constexpr int kModBoardControl = kMixerMuteVisual + kMixerStrips;
constexpr int kMixerStereoBase = kModBoardControl + modboard::kHandles;
constexpr int kMixerStereoFields = 6; // PeakL, PeakR, HoldL, HoldR, ClipL, ClipR (10 strips each)
constexpr int kMasterScaleInput = kMixerStereoBase + kMixerStereoFields * kMixerStrips;
// The SDK menu labels are fixed per handle. One five-choice menu per routing state
// marks that route while the host's current-choice indicator follows the edited filter.
constexpr int kFilterChainMenu = kMasterScaleInput + 1;
constexpr int kFilterChainRoutes = 3, kFilterChainLastChoice = kFilterChainRoutes - 1;   // the routing only (Parallel, F1 to F2, F2 to F1)
constexpr int kFxEditorPower = kFilterChainMenu + kFilterChainRoutes;
constexpr int kLfoTargetsAction = kFxEditorPower + 1;
// The band lanes of an FX splitter (components/native/band_lane): lane after lane, kLaneHandles each (shell-mpc/native_fx_bands.h, scripts/native_ui_params.py).
namespace fxbands
{
constexpr int kLanes = 3, kCards = 13;
enum Field { Tile = 0, Range = 1, Info = 2, Count = 3, Open = 4, Add = 5, AddAvail = 6, Select = 7, Power = Select + kCards, kLaneHandles = Power + kCards };
constexpr int kHandles = kLanes * kLaneHandles;
}
constexpr int kFxBandLanes = kLfoTargetsAction + 1;
// Appended 2026-11-28 (ng/modosc): read-only dynamic text lines of the Mod page (native_text.h) and the twelve notes of the transpose snap (cells bound by osc_ui.h publishNativeNoteMask).
constexpr int kNativeText = kFxBandLanes + fxbands::kHandles;
constexpr int kNativeTextLines = 5;
constexpr int kNoteMask = kNativeText + kNativeTextLines;
constexpr int kNoteMaskHandles = 13;   // 12 notes (bit 0 = C .. bit 11 = B) and the gate (1 while the mask is on the page)
constexpr bool isFxBandsOffset (int offset) noexcept { return offset >= kFxBandLanes && offset < kNativeText; }
constexpr bool isNativeTextOffset (int offset) noexcept { return offset >= kNativeText && offset < kNoteMask; }
constexpr int kCount = kNoteMask + kNoteMaskHandles;
constexpr bool isNoteMaskOffset (int offset) noexcept { return offset >= kNoteMask && offset < kCount; }
constexpr bool isFilterChainMenu (int offset) noexcept { return offset >= kFilterChainMenu && offset < kFxEditorPower; }
constexpr bool isOscDetailOffset (int offset) noexcept { return offset >= kOscDetail && offset < kOscDetail + kOscDetailHandles; }
constexpr bool isFxBandOffset (int offset) noexcept { return offset >= kFxBandBase && offset <= kFxBandMode; }
constexpr bool isPzToolbarOffset (int offset) noexcept { return offset >= kPzAction && offset <= kPzAvailability; }
constexpr bool isOscRootLoadOffset (int offset) noexcept { return offset >= kOscRootLoad && offset <= kOscRootLoadView; }
constexpr bool isModTargetsOffset (int offset) noexcept { return offset >= kModTargets && offset < kPlayExpanded; }
constexpr bool isPlayExpandedOffset (int offset) noexcept { return offset >= kPlayExpanded && offset < kMacroTarget; }
constexpr bool isMacroTargetOffset (int offset) noexcept { return offset >= kMacroTarget && offset < kMixerControl; }
constexpr bool isMixerControlOffset (int offset) noexcept { return offset >= kMixerControl && offset < kModDetailReserved; }
constexpr bool isModDetailReservedOffset (int offset) noexcept { return offset >= kModDetailReserved && offset < kMixerSoloVisual; }
constexpr bool isMixerSoloVisualOffset (int offset) noexcept { return offset >= kMixerSoloVisual && offset < kMixerMuteVisual; }
constexpr bool isMixerMuteVisualOffset (int offset) noexcept { return offset >= kMixerMuteVisual && offset < kModBoardControl; }
constexpr bool isModDetailControlOffset (int offset) noexcept { return offset >= kModDetailControl && offset < kMixerSoloVisual; }
constexpr bool isModBoardControlOffset (int offset) noexcept { return offset >= kModBoardControl && offset < kMixerStereoBase; }
constexpr bool isMixerStereoOffset (int offset) noexcept { return offset >= kMixerStereoBase && offset < kMasterScaleInput; }
namespace pz
{
enum Action { Back = 1, Reset, Remove, Apply };
enum Availability { None, RemoveOnly, Both, ApplyOnly };
}
static_assert (kEnvSelectorCells * kEnvSelectorFrameBits <= 32 && kEnvSelectorLastFrame < (1 << kEnvSelectorFrameBits));

inline float meterValue (float value) noexcept
{
    return std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f;
}
}
