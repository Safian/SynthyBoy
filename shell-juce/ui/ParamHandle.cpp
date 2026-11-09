// SynthyBoy - UI: parameter binding.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ParamHandle.h"

namespace synthyboy::ui
{
ParamHandle::ParamHandle (UiContext& context, const std::string& key) : ctx (context), keyText (key)
{
    ref = ctx.resolve (key);
    if (! ref.valid())
        return;

    const char* storedKey = paramTable()[static_cast<size_t> (ref.index)].key;
    param = ctx.apvts().getParameter (storedKey);
    rawValue = ctx.apvts().getRawParameterValue (storedKey);
    if (param == nullptr)
        return;

    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float)
    {
        if (onChange)
            onChange();
    });
}

bool ParamHandle::modulatable() const noexcept
{
    if (! valid() || (paramTable()[static_cast<size_t> (ref.index)].flags & kParamModulatable) == 0)
        return false;
    return ref.index == ref.sourceIndex() || (info().flags & kParamModulatable) != 0;
}

float ParamHandle::plain() const
{
    if (rawValue == nullptr)
        return valid() ? info().def : 0.0f;
    const float stored = rawValue->load();
    return ref.storedNormalised ? fromNormalised (info(), stored) : stored;
}

float ParamHandle::normalised() const
{
    return param != nullptr ? param->getValue() : 0.0f;
}

CapState ParamHandle::caps() const
{
    if (! valid())
        return {};
    CapState state = capStateForKey (ref.index, ctx.host.engineCaps());
    if (ref.sourceIndex() != ref.index) // a pool slot: the engine needs the bit of the pool and what the key of the type needs
    {
        const CapState source = capStateForKey (ref.sourceIndex(), ctx.host.engineCaps());
        if (! source.available)
        {
            state.available = false;
            state.missing |= source.missing;
            if (state.tooltip.empty())
                state.tooltip = source.tooltip;
        }
    }
    return state;
}

void ParamHandle::beginGesture()
{
    if (attachment != nullptr && ! gestureOpen)
    {
        gestureOpen = true;
        attachment->beginGesture();
    }
}

void ParamHandle::setNormalised (float n)
{
    if (attachment == nullptr)
        return;
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
}

void ParamHandle::endGesture()
{
    if (attachment != nullptr && gestureOpen)
    {
        gestureOpen = false;
        attachment->endGesture();
    }
}

void ParamHandle::setPlainComplete (float value)
{
    if (attachment == nullptr)
        return;
    const float constrained = constrainValue (info(), value);
    attachment->setValueAsCompleteGesture (ref.storedNormalised ? toNormalised (info(), constrained) : constrained);
}

void ParamHandle::resetToDefault()
{
    if (valid())
        setPlainComplete (info().def);
}
} // namespace synthyboy::ui
