// SynthyBoy - UI view-model: the key set of a pool slot (docs/ui-design/18-FX-INSTANCES.md section 2.8).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The panels of the effects are written against the keys of instance 0 ("chorus.rate", "chorus.on", ...). A pool slot has other keys (fxiNN.pKK holds the
// normalised value of the K-th parameter of the type), so one panel class serves instance 0 and any slot through this indirection: the panel is built
// with a UiContext whose key set maps "chorus.rate" to the slot's key and keeps the ParamInfo of "chorus.rate" for everything that is shown (range, curve,
// unit, names). Pure C++ on synthyboy_core: tested headless.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ControlModel.h"
#include "synthyboy/fx_types.h"

namespace synthyboy::ui
{
class FxKeySet
{
public:
    /** The key set of pool slot `slot` (0 .. 15) for panels of `type` (kFxTypeChorus .. kFxTypeConvolve). */
    FxKeySet (int slot, int type);

    int slot() const noexcept { return slotIndex; }
    int type() const noexcept { return typeValue; }

    /** "chorus.rate" -> the key of the slot with the ParamInfo of "chorus.rate"; "chorus.on" / "chorus.rack" -> fxiNN.on / fxiNN.rack. A key that is not one of
        the type's (and not a global switch such as reverb.force_enable) is invalid. */
    KeyRef resolve (std::string_view key) const noexcept;

    /** Every key of instance 0 this set maps (the descriptor's keys, on and rack). */
    const std::vector<std::string>& keys() const noexcept { return mapped; }

private:
    struct Entry
    {
        std::string key;
        KeyRef ref;
    };

    int slotIndex, typeValue;
    std::vector<Entry> entries;
    std::vector<std::string> mapped;
};

/** The id of the panel class (PanelRegistry) that edits instance 0 of a type: "FxChorusPanel" ... ("" for none and for a type without a panel). */
const char* fxPanelIdOfType (int type) noexcept;
} // namespace synthyboy::ui
