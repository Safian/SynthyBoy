// SynthyBoy - append-only native skin parameters after Q 16.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "native_choice_list.h"
#include "native_lfo_controls.h"
#include "native_env_controls.h"
#include "native_master_controls.h"
#include "native_mod_controls.h"
#include "native_osc_controls.h"
#include "native_text.h"

namespace synthyboy_mpc::nativeui
{
constexpr int kMixerStrips = 10;
constexpr int kMixerPeakBase = 0;
constexpr int kMixerHoldBase = kMixerPeakBase + kMixerStrips;
constexpr int kFxValueBase = kMixerHoldBase + kMixerStrips;   // the handles below this one are the meters (peak and hold of the ten strips)
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
constexpr int kOscAbcTitle = kOscAbcSettings + osc::kAbcTitle;
static_assert (osc::kAbcTitle + 1 <= osc::kAbcLegacySlots);
constexpr bool isOscAbcSettingOffset (int offset) noexcept { return offset >= kOscAbcSettings && offset < kOscAbcTitle; }
namespace fxheader
{
enum Field { Power, Rack, Page, Remove, kFields };
constexpr int kItems = 32;
constexpr int kHandles = 2 + kItems * kFields;
}
constexpr int kFxHeader = kOscAbcSettings + osc::kAbcLegacySlots;
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
constexpr int kPlayEnvLfoHandles = 8;
constexpr int kPlayOsc = kPlayEnvLfo + kPlayEnvLfoHandles;
constexpr int kPlayOscHandles = 31;
constexpr int kPlayLfoStandardSync = kPlayOsc + kPlayOscHandles;
constexpr int kOscDetail = kPlayLfoStandardSync + 1;
constexpr int kOscDetailHandles = 110;
constexpr int kFxBandBase = kOscDetail + kOscDetailHandles;
constexpr int kFxBandMode = kFxBandBase + fxheader::kItems;
constexpr int kPzAction = kFxBandMode + 1;
constexpr int kPzSource = kPzAction + 2;
constexpr int kPzAvailability = kPzSource + 1;
constexpr int kOscRootLoad = kPzAvailability + 1;
constexpr int kOscRootLoadView = kOscRootLoad + 3;
constexpr int kModTargets = kOscRootLoadView + 1;
constexpr int kModTargetsHandles = 35;
constexpr int kMacroTarget = kModTargets + kModTargetsHandles;
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
constexpr int kModDetailControl = kMixerControl + mixer::kHandles;
// Read-only Mixer UI presentation handles appended after the Mod detail controls.
constexpr int kMixerSoloVisual = kModDetailControl + moddetail::kHandles;
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
// The band lanes of an FX splitter (components/native/band_lane): lane after lane, kLaneHandles each (shell-mpc/native_fx_bands.h).
namespace fxbands
{
constexpr int kLanes = 3, kCards = 13;
enum Field { Tile = 0, Range = 1, Info = 2, Count = 3, Open = 4, Add = 5, AddAvail = 6, Select = 7, Power = Select + kCards, kLaneHandles = Power + kCards };
constexpr int kHandles = kLanes * kLaneHandles;
}
constexpr int kFxBandLanes = kLfoTargetsAction + 1;
// The host choices of the LFO (native_lfo_pick.h): the SOURCE of a point modulation, ADD SOURCE and ADD TARGET (GROUP and one TARGET list per group); appended after the band lanes.
constexpr int kLfoPick = kFxBandLanes + fxbands::kHandles;
constexpr int kLfoPickHandles = 44;
// Appended 2026-11-28 (ng/modosc): read-only dynamic text lines of the Mod page (native_text.h) and the twelve notes of the transpose snap (cells bound by osc_ui.h publishNativeNoteMask).
constexpr int kNativeText = kLfoPick + kLfoPickHandles;
constexpr int kNativeTextLines = nativetext::kLegacyLines;
constexpr int kNoteMask = kNativeText + kNativeTextLines;
constexpr int kNoteMaskHandles = 13;   // 12 notes (bit 0 = C .. bit 11 = B) and the gate (1 while the mask is on the page)
constexpr bool isLfoPickOffset (int offset) noexcept { return offset >= kLfoPick && offset < kNativeText; }
constexpr bool isFxBandsOffset (int offset) noexcept { return offset >= kFxBandLanes && offset < kNativeText; }
// Appended 2026-11-28 (ng/drawvmm): read-only text lines of the Macros tab (nativetext::MacroLine: the names of the eight cards, the title, the count and the empty hint of the targets page) and the gate of its sub pages
// (exactly 0 on the macro cards page, exactly 1 while the targets, the add-target list or the rename keyboard is open: the native cards and texts of the base page are gated by it).
constexpr int kMacroText = kNoteMask + kNoteMaskHandles;
constexpr int kMacroTextLines = 11;
constexpr int kMacroSubpage = kMacroText + kMacroTextLines;
constexpr bool isMacroTextOffset (int offset) noexcept { return offset >= kMacroText && offset < kMacroSubpage; }
// Appended 2026-11-28 (ng/drawlfomod): the further text lines (nativetext::kLegacyLines ..): the texts the pages used to draw themselves; the handle of line `line` is nativeTextHandle (line).
constexpr int kDrawText = kMacroSubpage + 1;
// Appended 2026-11-28 (ng/kbd): the native text keyboard (components/native/keyboard, shell-mpc/native_text_entry.h): the context (0 closed, 1..8 the name of macro 1..8, 9 the name of a new LFO shape, 10 the new
// name of a saved one; the host shows its display string, the title, while the keyboard is open), the key code the skin buttons write (0 idle .. 99, ResetOnGestureEnd) and the text shown by a Label (its display string).
constexpr int kTextEdit = kDrawText + nativetext::kDrawLines;
constexpr int kTextEditContext = kTextEdit, kTextEditKey = kTextEdit + 1, kTextEditText = kTextEdit + 2;
constexpr int kTextEditContexts = 11, kTextEditKeys = 100;
// Appended 2026-11-28 (ng/keyline): the presence gates of text lines that own a native cell (the keyline under a title): exactly 0 while the line is empty, exactly 1 while it has text. The skin gates the cell
// with IndexedEnabling/1/2/Parameter <gate>; the value of the text line itself is a changing fraction in (0, 1], which NonZero accepts for any text, and is not a gate.
constexpr int kTextGate = kTextEdit + 3;
constexpr int kTextGates = 2;   // 0: nativetext::TargetsTitle (Targets / Modulations keyline), 1: nativetext::OscDetailTitle (the oscillator detail keyline)
constexpr int textGateLine (int gate) noexcept { return gate == 0 ? nativetext::TargetsTitle : nativetext::OscDetailTitle; }
// Appended 2026-11-28 (play list): "Play List", the code of the Play page's own paged list the skin radio Buttons open (native_play_list.h: 0 none, 1 source, 2 LFO/RND, 3 noise sound, 4 LFO shape, 5 LFO division,
// 6 standard LFO, 7 oscillator selector, 8 envelope selector, 9 oscillator type, 10 route, 11 Sub wave, 12 Sub octave, 13 LFO style, 14 LFO sync); stepped integer 0 .. 14, written by the host, never stored (getParameter is always 0). Host index 2719 + offset = Parameter 3902 (the text gates end at 3901).
constexpr int kPlayList = kTextGate + kTextGates;
constexpr int kPlayListLastCode = 14;   // == playlist::kLastCode (native_play_list.h; checked in synthyboy_mpc.cpp)
static_assert (kPlayList == 3902 - 2719, "Play List is Parameter 3902 in skin/defs_play.json: the handle block before it changed");
// Appended 2026-11-28 (choice lists, native_choice_list.h): "Choice List Open" (Parameter 3903) and the 113 opener slots "Choice List 0" .. (Parameter 3904 .. 4016).
constexpr int kChoiceListOpen = kPlayList + 1;
constexpr int kChoiceList = kChoiceListOpen + 1;
static_assert (kChoiceListOpen == choicelist::kOpenParameter - 2719 && kChoiceList == choicelist::kFirstSlotParameter - 2719);
// Appended 2026-11-28 (two-level list state): exact 0/1 facts of the open native list page (row arrows 0..6, deeper level, previous/next page).
namespace liststate
{
constexpr int kRowArrow = 0;                            // 0 .. modboard::kListRows - 1 (Parameter 4017 .. 4023): 1 when row opens a deeper level, else 0
constexpr int kDeeperLevel = modboard::kListRows;       // 7 (Parameter 4024): 1 when on a deeper level (header shows the back arrow), else 0
constexpr int kHasPrev = kDeeperLevel + 1;              // 8 (Parameter 4025): 1 when there is a previous page, else 0
constexpr int kHasNext = kHasPrev + 1;                  // 9 (Parameter 4026): 1 when there is a next page, else 0
constexpr int kHandles = kHasNext + 1;                  // 10

struct State
{
    bool active = false;
    bool deeperLevel = false;
    bool hasPrev = false;
    bool hasNext = false;
    std::array<bool, modboard::kListRows> rowOpensLevel {};
};

inline void copyRowName (char* out, size_t cap, const char* text) noexcept
{
    if (out == nullptr || cap == 0)
        return;
    std::snprintf (out, cap, "%s", text != nullptr ? text : "");
    const size_t len = std::strlen (out);
    constexpr const char kArrow[] = " \xe2\x96\xb6";
    constexpr size_t kArrowLen = sizeof (kArrow) - 1;
    if (len >= kArrowLen && std::memcmp (out + len - kArrowLen, kArrow, kArrowLen) == 0)
        out[len - kArrowLen] = '\0';
}
}
constexpr int kListState = kChoiceList + choicelist::kSlots;
constexpr int kCount = kListState + liststate::kHandles;
constexpr int nativeTextHandle (int line) noexcept { return line < nativetext::kLegacyLines ? kNativeText + line : kDrawText + (line - nativetext::kLegacyLines); }
constexpr bool isDrawTextOffset (int offset) noexcept { return offset >= kDrawText && offset < kTextEdit; }
constexpr bool isTextEditOffset (int offset) noexcept { return offset >= kTextEdit && offset < kTextGate; }
constexpr bool isTextGateOffset (int offset) noexcept { return offset >= kTextGate && offset < kPlayList; }
constexpr bool isChoiceListOffset (int offset) noexcept { return offset >= kChoiceList && offset < kListState; }
constexpr bool isListStateOffset (int offset) noexcept { return offset >= kListState && offset < kCount; }
constexpr int nativeTextLine (int offset) noexcept { return offset < kNoteMask ? offset - kNativeText : nativetext::kLegacyLines + (offset - kDrawText); }
constexpr bool isNativeTextOffset (int offset) noexcept { return (offset >= kNativeText && offset < kNoteMask) || isDrawTextOffset (offset); }
constexpr bool isNoteMaskOffset (int offset) noexcept { return offset >= kNoteMask && offset < kNoteMask + kNoteMaskHandles; }
constexpr bool isFilterChainMenu (int offset) noexcept { return offset >= kFilterChainMenu && offset < kFxEditorPower; }
constexpr bool isOscDetailOffset (int offset) noexcept { return offset >= kOscDetail && offset < kOscDetail + kOscDetailHandles; }
constexpr bool isFxBandOffset (int offset) noexcept { return offset >= kFxBandBase && offset <= kFxBandMode; }
constexpr bool isPzToolbarOffset (int offset) noexcept { return offset >= kPzAction && offset <= kPzAvailability; }
constexpr bool isOscRootLoadOffset (int offset) noexcept { return offset >= kOscRootLoad && offset <= kOscRootLoadView; }
constexpr bool isModTargetsOffset (int offset) noexcept { return offset >= kModTargets && offset < kMacroTarget; }
constexpr bool isMacroTargetOffset (int offset) noexcept { return offset >= kMacroTarget && offset < kMixerControl; }
constexpr bool isMixerControlOffset (int offset) noexcept { return offset >= kMixerControl && offset < kModDetailControl; }
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

inline float meterValue (float value) noexcept
{
    return std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f;
}
}
