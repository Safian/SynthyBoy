// SynthyBoy - UI: parameter binding.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ParamHandle.h"

namespace synthyboy::ui
{
ParamHandle::ParamHandle (UiContext& context, const std::string& key) : ctx (context), keyText (key)
{
    ref = ctx.resolve (key);
    if (! ref.valid() || ! ctx.store().contains (ref.index))
        return;

    store = &ctx.store();
    storeAlive = store->lifetime();
    store->addListener (ref.index, this);
}

ParamHandle::~ParamHandle()
{
    if (store != nullptr && ! storeAlive.expired())
        store->removeListener (ref.index, this);
}

void ParamHandle::parameterChanged (int)
{
    if (onChange)
        onChange();
}

bool ParamHandle::modulatable() const noexcept
{
    if (! valid() || (paramTable()[static_cast<size_t> (ref.index)].flags & kParamModulatable) == 0)
        return false;
    return ref.index == ref.sourceIndex() || (info().flags & kParamModulatable) != 0;
}

float ParamHandle::plain() const
{
    if (store == nullptr)
        return valid() ? info().def : 0.0f;
    // the store answers with the new value already inside an onChange callback
    const float stored = store->plain (ref.index);
    return ref.storedNormalised ? fromNormalised (info(), stored) : stored;
}

float ParamHandle::normalised() const
{
    return store != nullptr ? store->normalised (ref.index) : 0.0f;
}

CapState ParamHandle::caps() const
{
    if (! valid())
        return {};
    CapState state = capStateForKey (ref.index, ctx.host.engineCaps(), ctx.host.engineCaps2());
    if (ref.sourceIndex() != ref.index) // a pool slot: the engine needs the bit of the pool and what the key of the type needs
    {
        const CapState source = capStateForKey (ref.sourceIndex(), ctx.host.engineCaps(), ctx.host.engineCaps2());
        if (! source.available)
        {
            state.available = false;
            state.missing |= source.missing;
            state.missing2 |= source.missing2;
            if (state.tooltip.empty())
                state.tooltip = source.tooltip;
        }
    }
    return state;
}

void ParamHandle::beginGesture()
{
    if (store != nullptr && ! gestureOpen)
    {
        gestureOpen = true;
        store->beginGesture (ref.index);
    }
}

void ParamHandle::setNormalised (float n)
{
    if (store != nullptr)
        store->setNormalised (ref.index, juce::jlimit (0.0f, 1.0f, n));
}

void ParamHandle::endGesture()
{
    if (store != nullptr && gestureOpen)
    {
        gestureOpen = false;
        store->endGesture (ref.index);
    }
}

void ParamHandle::setPlainComplete (float value)
{
    if (store == nullptr)
        return;
    const float constrained = constrainValue (info(), value);
    store->setPlainComplete (ref.index, ref.storedNormalised ? toNormalised (info(), constrained) : constrained);
}

void ParamHandle::resetToDefault()
{
    if (valid())
        setPlainComplete (info().def);
}
} // namespace synthyboy::ui
