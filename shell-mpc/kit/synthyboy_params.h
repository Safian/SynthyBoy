// MpcKit - SynthyBoy's parameter table as an IParamSource. Its own header (and not part of synthyboy_adapter.h) so that shell-mpc/overlay/curve_source.h can include it for
// IInstanceView::params () without a cycle: it needs only synthyboy/params.h, q_step_rule.h and the kit.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "synthyboy/params.h"

#include "../q_step_rule.h"
#include "param_source.h"

namespace mpckit { namespace synthyboy_adapter {

static_assert (static_cast<int> (synthyboy::Scale::stepped) == static_cast<int> (Scale::stepped), "the kit's Scale has the order of the engine's");
static_assert (static_cast<int> (synthyboy::Unit::voices) == static_cast<int> (Unit::voices), "the kit's Unit has the order of the engine's");

/** The ParamDesc of one ParamInfo (a copy of the fields: for the few places that hold a ParamInfo and not an index, e.g. uibind::formatValue (const ParamInfo&, ...) after migration step 3). */
inline ParamDesc descOf (const synthyboy::ParamInfo& i) noexcept
{
    ParamDesc d;
    d.key = i.key; d.name = i.name; d.group = i.group;
    d.min = i.min; d.max = i.max; d.def = i.def; d.skew = i.skew; d.interval = i.interval;
    d.scale = static_cast<Scale> (i.scale);
    d.unit = static_cast<Unit> (i.unit);
    d.choices = i.choices; d.numChoices = i.numChoices; d.decimals = i.decimals;
    return d;
}

/** IParamSource over synthyboy::paramTable (): ParamDesc copies are made once, on first use (the table is constant). Every conversion and text forwards to the engine's functions. */
class Params final : public IParamSource
{
public:
    static const Params& get()
    {
        static const Params p;   // built on first use; the initialisation of a function-local static is thread safe
        return p;
    }
    int count() const noexcept override { return static_cast<int> (synthyboy::paramCount()); }
    const ParamDesc& desc (int index) const noexcept override { return descs_[static_cast<size_t> (index < 0 ? 0 : (index >= count() ? count() - 1 : index))]; }
    int indexOfKey (const char* key) const noexcept override
    {
        if (key == nullptr || key[0] == '\0')
            return -1;
        const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key);
        return p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    float toNormalised (int index, float plain) const noexcept override { return synthyboy::toNormalised (info (index), plain); }
    float fromNormalised (int index, float normalised) const noexcept override { return synthyboy::fromNormalised (info (index), normalised); }
    float constrain (int index, float plain) const noexcept override { return synthyboy::constrainValue (info (index), plain); }
    void valueText (int index, float plain, char* out, size_t cap) const noexcept override { formatValue (desc (index), plain, out, cap); }   // = uibind::formatValue (ui_bind.h), same algorithm
    size_t hostText (int index, float plain, char* out, size_t cap) const noexcept override { return synthyboy::valueToText (info (index), plain, out, cap); }
    int stepCount (int index) const noexcept override { return synthyboy_mpc::stepCountOf (info (index)); }

private:
    Params()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        descs_.resize (synthyboy::paramCount());
        for (size_t i = 0; i < descs_.size(); ++i)
        {
            ParamDesc& d = descs_[i];
            d.key = t[i].key; d.name = t[i].name; d.group = t[i].group;
            d.min = t[i].min; d.max = t[i].max; d.def = t[i].def; d.skew = t[i].skew; d.interval = t[i].interval;
            d.scale = static_cast<Scale> (t[i].scale);
            d.unit = static_cast<Unit> (t[i].unit);
            d.choices = t[i].choices; d.numChoices = t[i].numChoices; d.decimals = t[i].decimals;
        }
    }
    static const synthyboy::ParamInfo& info (int index) noexcept { return synthyboy::paramTable()[index]; }
    std::vector<ParamDesc> descs_;
};

}} // namespace mpckit::synthyboy_adapter
