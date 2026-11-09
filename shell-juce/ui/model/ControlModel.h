// SynthyBoy - UI view-model: control kinds, key resolution, display text, capability dimming, gesture maths, layout modes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pure C++17 on top of synthyboy_core: no JUCE here, so every decision the controls make is unit tested headless
// (shell-juce/ui/tests/test_ui_model.cpp). The JUCE Components only draw and forward events.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "synthyboy/engine_caps.h"
#include "synthyboy/params.h"

namespace synthyboy::ui
{
/** A key of the parameter table resolved once. Controls are bound by KEY, never by index (the index is derived, the key is the contract). */
struct KeyRef
{
    const ParamInfo* info = nullptr; // the parameter whose range, curve, unit and choice names apply
    int index = -1;                  // table index of the parameter that is stored, automated and modulated
    int infoIndex = -1;              // table index of `info`; -1: the same as `index`. They differ for a parameter of a pool slot (FxKeySet)
    bool storedNormalised = false;   // the stored value is the normalised value of `info` (fxiNN.pKK), not its plain value
    bool valid() const noexcept { return info != nullptr && index >= 0; }
    int sourceIndex() const noexcept { return infoIndex >= 0 ? infoIndex : index; }
};

KeyRef resolveKey (std::string_view key) noexcept;

enum class ControlKind { knob, slider, choice, toggle };

/** knob for continuous keys, toggle for two-state stepped keys (min 0, max 1), choice for the other stepped keys. */
ControlKind defaultKind (const ParamInfo& info) noexcept;

/** Value text with unit as the value bubble and the sheet show it: ParamInfo::decimals, unit label, Hz from 1000 as kHz. Stepped: the choice name. */
std::string displayText (const ParamInfo& info, float plain);
/** True for the frequency readouts that are cutoffs of a filter, a band or a crossover (Hz parameters that are no rates: the cutoffs, centres, filters of the
    effects, the splitter and crossover frequencies): the readouts the frequency units preference changes (GROUPA2 WP-02, A382). */
bool isCutoffReadout (const ParamInfo& info) noexcept;
/** A frequency in semitones from middle C (261.6256 Hz), Vital's "semitones" units: "+7.0 st", "-12.5 st". */
std::string semitoneText (float hz);
/** "7", "+7.5", "-12 st", "3 semi", "3 semitones" -> the frequency in Hz; false when the text is not a number with an optional such suffix. */
bool semitoneTextToHz (const std::string& text, float& hz);
/** displayText () with the frequency units preference: a cutoff readout is shown in semitones when `semitones`. */
std::string displayText (const ParamInfo& info, float plain, bool semitones);
/** Label for a control: the display name without the group prefix ("Master Volume" in group "Master" -> "Volume"). */
std::string shortName (const ParamInfo& info);
/** The unit label only ("dB", "Hz", ...; "" for none). */
std::string unitText (const ParamInfo& info);

/** Modulation sources as chips: choice value 1..kNumModSources. Label: "ENV1", "LFO3", "RLFO2", "M1" (macros), "MODWHEEL"... */
std::string sourceChipLabel (int sourceChoice);
/** Choice value of a source key ("macro1" -> 19), 0 if unknown. */
int sourceChoiceForKey (std::string_view key) noexcept;
/** Parameter key of macro n (1-based): "macro.1". */
std::string macroKey (int n);

// ---- engine capability dimming (docs/ui-design/13-new-keys.md rule R5) ----------------------------------------------------------

inline constexpr const char* kNotAvailableText = "not available in this engine";

struct CapState
{
    bool available = true;      // false: draw dimmed, tooltip kNotAvailableText (the value stays editable and stored)
    EngineCapsMask missing = 0; // the bits the engine does not report
    std::string tooltip;        // empty when available
};

/** A key whose required cap bits are not all set in `engineCaps` is unavailable. */
CapState capStateForKey (int paramIndex, EngineCapsMask engineCaps);
/** A single choice value (an appended entry of a version 2 list needs bits the key itself does not). */
CapState capStateForValue (int paramIndex, float plain, EngineCapsMask engineCaps);

// ---- gestures ----------------------------------------------------------------------------------------------------------------------

inline constexpr int kLongPressMs = 400;       // hold without moving
inline constexpr float kLongPressSlopPx = 6.0f;
inline constexpr int kDoubleTapMs = 320;
inline constexpr float kDoubleTapSlopPx = 24.0f;
inline constexpr float kDragPixelsForFullRange = 200.0f;
inline constexpr float kFineZoneWidthPx = 56.0f;

/** Drag, then slide the finger sideways away from the control: 3 zones (docs/ui-design/01 section 3). */
enum class FineZone { normal, fine, ultra };

FineZone zoneForOffset (float sidewaysPx, float zoneWidthPx = kFineZoneWidthPx) noexcept;
float zoneFactor (FineZone zone) noexcept; // 1, 0.25, 0.05
/** Incremental drag: dyUpPx > 0 (finger moved up) raises the normalised value; the result is clamped to 0..1. */
float applyDragDelta (float normalised, float dyUpPx, FineZone zone, float pixelsForFullRange = kDragPixelsForFullRange) noexcept;
bool isDoubleTap (int msSinceLastTap, float distancePx) noexcept;
bool isLongPress (int heldMs, float movedPx) noexcept;

// ---- layout ------------------------------------------------------------------------------------------------------------------------

/** Breakpoints of docs/ui-design/01 section 2: phone portrait < 600; tablet portrait / phone landscape 600..999; wide (tablet landscape, MPC) >= 1000. */
enum class LayoutMode { phone, tablet, wide };

LayoutMode layoutModeForWidth (int widthPx) noexcept;
int columnsFor (LayoutMode mode) noexcept; // 1, 2, 3

/** The page tabs (OSC MIX FLT ENV LFO MOD FX GLB, docs/ui-design/01 section 2.1). */
enum class Page : int { osc, mix, flt, env, lfo, mod, fx, glb };
inline constexpr int kNumPages = 8;
const char* pageLabel (Page page) noexcept;

/** Minimum touch target and gap in points at UI scale 1 (docs/ui-design/00-BRIEF.md principle 5). */
inline constexpr float kMinTouchPx = 44.0f;
inline constexpr float kMinGapPx = 8.0f;
} // namespace synthyboy::ui
