// SynthyBoy - UI: parameter binding.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ParamHandle.h"

namespace synthyboy::ui
{
ParamHandle::ParamHandle (UiContext& context, const std::string& key) : ctx (context), keyText (key)
{
    ref = resolveKey (key);
    if (! ref.valid())
        return;

    param = ctx.apvts().getParameter (ref.info->key);
    rawValue = ctx.apvts().getRawParameterValue (ref.info->key);
    if (param == nullptr)
        return;

    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float)
    {
        if (onChange)
            onChange();
    });
}

float ParamHandle::plain() const
{
    if (param != nullptr)
        return param->convertFrom0to1 (param->getValue());
    return rawValue != nullptr ? rawValue->load() : (valid() ? info().def : 0.0f);
}

float ParamHandle::normalised() const
{
    return param != nullptr ? param->getValue() : 0.0f;
}

CapState ParamHandle::caps() const
{
    return valid() ? capStateForKey (ref.index, ctx.host.engineCaps()) : CapState {};
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
    if (attachment != nullptr)
        attachment->setValueAsCompleteGesture (constrainValue (info(), value));
}

void ParamHandle::resetToDefault()
{
    if (valid())
        setPlainComplete (info().def);
}
} // namespace synthyboy::ui
