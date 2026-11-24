// SynthyBoy - the native standalone Filter editor's skin handles.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"

namespace synthyboy_mpc::nativeui
{
enum FilterSlot
{
    FilterPower, FilterCutoff, FilterResonance, FilterDrive, FilterMix,
    FilterKeytrack, FilterEnv1, FilterVar, FilterCombOffset, FilterBlend, FilterRouting,
    FilterFormantX, FilterFormantY, FilterFormantTranspose, FilterFormantResonance, FilterFormantSpread, FilterClean,
    FilterModel, FilterStyle,
    FilterPoint, FilterX, FilterY, FilterCurve, FilterGridX, FilterGridY, FilterSnap, FilterLegato,
    FilterAction, FilterSlots
};
enum FilterActionId
{
    FilterF1 = 1, FilterF2, FilterNote, FilterVelo, FilterBoth, FilterEditPz,
    FilterExpandFilter = 7, FilterInit = 8, FilterUndo, FilterDelete, FilterTargets, FilterExpandCurve,
    FilterLastAction = FilterExpandCurve
};
constexpr int kFilterLastViewFrame = 4;
constexpr int kFilterIndicators = 5;  // F1, F2, NOTE, VELO and the dual-response toggle
constexpr int kFilterIndicatorLastFrame = 3;
constexpr int kFilterCurveFields = FilterLegato - FilterPoint;
constexpr const char* kFilterNames[FilterSlots] = {
    "Filter Power", "Filter Cutoff", "Filter Resonance", "Filter Drive", "Filter Mix",
    "Filter Keytrack", "Filter Env 1", "Filter Var", "Filter Comb Offset", "Filter Blend", "Filter Routing",
    "Filter Formant X", "Filter Formant Y", "Filter Formant Transpose", "Filter Formant Resonance", "Filter Formant Spread", "Filter Clean",
    "Filter Model", "Filter Style", "Curve Point", "Curve X", "Curve Y", "Curve Bend", "Curve Grid X", "Curve Grid Y", "Curve Snap", "Curve Legato",
    "Filter Native Action"
};
using FilterCellBank = NativeCellBank<FilterSlots>;
}
