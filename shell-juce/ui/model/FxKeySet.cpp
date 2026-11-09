// SynthyBoy - UI view-model: the key set of a pool slot.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxKeySet.h"

#include <cstring>

namespace synthyboy::ui
{
FxKeySet::FxKeySet (int slot, int type) : slotIndex (slot), typeValue (type)
{
    const FxTypeDescriptor& d = fxTypeDescriptor (type);
    const FxInstanceKeys& keys = fxInstanceKeys (slot);
    if (d.count == 0 || keys.type < 0)
        return;

    for (int k = 0; k < d.count; ++k)
    {
        const ParamInfo& src = paramTable()[d.source[k]];
        entries.push_back ({ src.key, KeyRef { &src, keys.p[k], d.source[k], true } });
    }
    entries.push_back ({ paramTable()[d.onIndex].key, KeyRef { &paramTable()[keys.on], keys.on, -1, false } });
    entries.push_back ({ paramTable()[d.rackIndex].key, KeyRef { &paramTable()[keys.rack], keys.rack, -1, false } });
    for (const Entry& e : entries)
        mapped.push_back (e.key);
}

KeyRef FxKeySet::resolve (std::string_view key) const noexcept
{
    for (const Entry& e : entries)
        if (key == e.key)
            return e.ref;
    // a switch of the whole engine that sits on the panel of a type (the reverb's force enable) is not a parameter of the instance
    if (key == "reverb.force_enable")
        return resolveKey (key);
    return {};
}

const char* fxPanelIdOfType (int type) noexcept
{
    static const char* const ids[kNumFxInstanceTypes] = { "", "FxChorusPanel", "FxCompressorPanel", "FxDelayPanel", "FxDistortionPanel", "FxEqPanel",
                                                         "FxFilterPanel", "FxFlangerPanel", "FxPhaserPanel", "FxReverbPanel", "FxHyperDimPanel",
                                                         "FxUtilityPanel", "FxBodePanel", "FxConvolvePanel" };
    return type >= 0 && type < kNumFxInstanceTypes ? ids[type] : "";
}
} // namespace synthyboy::ui
