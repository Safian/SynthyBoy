// SynthyBoy - UI: parameter store base (listeners), whole-patch helpers, the plain in-memory store.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ParameterStore.h"

#include <cmath>
#include <functional>

namespace synthyboy::ui
{
void ParameterStore::setPlainComplete (int index, float plainValue)
{
    if (! contains (index))
        return;
    beginGesture (index);
    setPlain (index, plainValue);
    endGesture (index);
}

void ParameterStore::addListener (int index, Listener* listener)
{
    perParameter[index].add (listener);
}

void ParameterStore::removeListener (int index, Listener* listener)
{
    if (auto it = perParameter.find (index); it != perParameter.end())
        it->second.remove (listener);
}

void ParameterStore::addListener (Listener* listener)
{
    everyParameter.add (listener);
}

void ParameterStore::removeListener (Listener* listener)
{
    everyParameter.remove (listener);
}

void ParameterStore::notifyChanged (int index)
{
    if (auto it = perParameter.find (index); it != perParameter.end())
        it->second.call ([index] (Listener& l) { l.parameterChanged (index); });
    everyParameter.call ([index] (Listener& l) { l.parameterChanged (index); });
}

void ParameterStore::notifyGesture (int index, bool starting)
{
    if (auto it = perParameter.find (index); it != perParameter.end())
        it->second.call ([index, starting] (Listener& l) { l.parameterGestureChanged (index, starting); });
    everyParameter.call ([index, starting] (Listener& l) { l.parameterGestureChanged (index, starting); });
}

ParamValues readAllValues (const ParameterStore& store)
{
    auto values = ParamValues::defaults();
    const size_t count = paramCount();
    for (size_t i = 0; i < count; ++i)
        if (store.contains (static_cast<int> (i)))
            values.plain[i] = store.plain (static_cast<int> (i));
    return values;
}

void writeAllValues (ParameterStore& store, const ParamValues& values)
{
    const size_t count = paramCount();
    for (size_t i = 0; i < count; ++i)
    {
        const int index = static_cast<int> (i);
        if (! store.contains (index))
            continue;
        const float target = store.toNormalised (index, values.plain[i]);
        if (std::abs (store.normalised (index) - target) > 1.0e-7f)
        {
            store.beginGesture (index);
            store.setNormalised (index, target);
            store.endGesture (index);
        }
    }
}

// ---- PlainParameterStore -----------------------------------------------------------------------------------------------------------------

PlainParameterStore::PlainParameterStore()
{
    const auto defaults = ParamValues::defaults();
    values.assign (defaults.plain, defaults.plain + paramCount());
}

float PlainParameterStore::plain (int index) const
{
    return contains (index) ? values[static_cast<size_t> (index)] : 0.0f;
}

float PlainParameterStore::toNormalised (int index, float plainValue) const
{
    return contains (index) ? synthyboy::toNormalised (paramTable()[index], plainValue) : 0.0f;
}

float PlainParameterStore::fromNormalised (int index, float normalisedValue) const
{
    return contains (index) ? synthyboy::fromNormalised (paramTable()[index], normalisedValue) : 0.0f;
}

float PlainParameterStore::normalised (int index) const
{
    return contains (index) ? toNormalised (index, values[static_cast<size_t> (index)]) : 0.0f;
}

void PlainParameterStore::beginGesture (int index)
{
    if (! contains (index))
        return;
    if (onUiGesture)
        onUiGesture (index, true);
    notifyGesture (index, true);
}

void PlainParameterStore::endGesture (int index)
{
    if (! contains (index))
        return;
    if (onUiGesture)
        onUiGesture (index, false);
    notifyGesture (index, false);
}

void PlainParameterStore::setNormalised (int index, float normalisedValue)
{
    if (! contains (index))
        return;
    const float value = fromNormalised (index, normalisedValue < 0.0f ? 0.0f : (normalisedValue > 1.0f ? 1.0f : normalisedValue));
    float& slot = values[static_cast<size_t> (index)];
    if (std::equal_to<float>() (slot, value)) // exact on purpose: only a different value is a change
        return;
    slot = value;
    if (onUiChange)
        onUiChange (index, value);
    notifyChanged (index);
}

void PlainParameterStore::setFromHost (int index, float plainValue)
{
    if (! contains (index))
        return;
    const float value = constrainValue (paramTable()[index], plainValue);
    float& slot = values[static_cast<size_t> (index)];
    if (std::equal_to<float>() (slot, value)) // exact on purpose: only a different value is a change
        return;
    slot = value;
    notifyChanged (index);
}

void PlainParameterStore::setAllFromHost (const ParamValues& all)
{
    for (size_t i = 0; i < values.size(); ++i)
        setFromHost (static_cast<int> (i), all.plain[i]);
}
} // namespace synthyboy::ui
