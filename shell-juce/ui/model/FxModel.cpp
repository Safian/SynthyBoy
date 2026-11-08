// SynthyBoy - UI view-model: FX extras of block B17 (see FxModel.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxModel.h"

namespace synthyboy::ui
{
const char* curvePresetName (CurvePreset preset) noexcept
{
    switch (preset)
    {
        case CurvePreset::identity: return "Identity";
        case CurvePreset::soft:     return "Soft";
        case CurvePreset::hard:     return "Hard";
        case CurvePreset::scurve:   return "S-curve";
        case CurvePreset::fold:     return "Fold";
        case CurvePreset::steps:    return "Steps";
        case CurvePreset::kNumPresets: break;
    }
    return "";
}

std::vector<ContentPoint> curvePreset (CurvePreset preset)
{
    switch (preset)
    {
        case CurvePreset::soft:
            return { ContentPoint (0.0f, 0.0f), ContentPoint (0.25f, 0.4f), ContentPoint (0.6f, 0.82f), ContentPoint (1.0f, 0.95f) };
        case CurvePreset::hard:
            return { ContentPoint (0.0f, 0.0f), ContentPoint (0.3f, 0.9f), ContentPoint (1.0f, 1.0f) };
        case CurvePreset::scurve:
            return { ContentPoint (0.0f, 0.0f), ContentPoint (0.25f, 0.12f), ContentPoint (0.5f, 0.5f), ContentPoint (0.75f, 0.88f), ContentPoint (1.0f, 1.0f) };
        case CurvePreset::fold:
            return { ContentPoint (0.0f, 0.0f), ContentPoint (0.35f, 1.0f), ContentPoint (0.7f, 0.2f), ContentPoint (1.0f, 0.8f) };
        case CurvePreset::steps:
            return { ContentPoint (0.0f, 0.0f), ContentPoint (0.24f, 0.0f), ContentPoint (0.26f, 0.33f), ContentPoint (0.49f, 0.33f),
                     ContentPoint (0.51f, 0.66f), ContentPoint (0.74f, 0.66f), ContentPoint (0.76f, 1.0f), ContentPoint (1.0f, 1.0f) };
        case CurvePreset::identity:
        case CurvePreset::kNumPresets:
            break;
    }
    return identityCurve();
}
} // namespace synthyboy::ui
