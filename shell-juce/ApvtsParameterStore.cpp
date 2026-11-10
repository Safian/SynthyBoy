// SynthyBoy - JUCE shell: the editor's ParameterStore on the plug-in's AudioProcessorValueTreeState.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ApvtsParameterStore.h"

namespace synthyboy::ui
{
ApvtsParameterStore::ApvtsParameterStore (juce::AudioProcessorValueTreeState& apvts) : state (apvts)
{
    const auto* table = paramTable();
    const size_t count = paramCount();
    parameters.assign (count, nullptr);
    changedOffThread = std::make_unique<std::atomic<bool>[]> (count);
    int highestJuceIndex = -1;
    for (size_t i = 0; i < count; ++i)
    {
        changedOffThread[i].store (false);
        parameters[i] = state.getParameter (table[i].key);
        if (parameters[i] != nullptr)
            highestJuceIndex = juce::jmax (highestJuceIndex, parameters[i]->getParameterIndex());
    }
    tableIndexOfJuceIndex.assign (static_cast<size_t> (highestJuceIndex + 1), -1);
    for (size_t i = 0; i < count; ++i)
        if (parameters[i] != nullptr)
        {
            tableIndexOfJuceIndex[static_cast<size_t> (parameters[i]->getParameterIndex())] = static_cast<int> (i);
            parameters[i]->addListener (this);
        }
}

ApvtsParameterStore::~ApvtsParameterStore()
{
    cancelPendingUpdate();
    for (auto* p : parameters)
        if (p != nullptr)
            p->removeListener (this);
}

juce::RangedAudioParameter* ApvtsParameterStore::parameterAt (int index) const noexcept
{
    return index >= 0 && static_cast<size_t> (index) < parameters.size() ? parameters[static_cast<size_t> (index)] : nullptr;
}

int ApvtsParameterStore::tableIndexOf (int juceIndex) const noexcept
{
    return juceIndex >= 0 && static_cast<size_t> (juceIndex) < tableIndexOfJuceIndex.size() ? tableIndexOfJuceIndex[static_cast<size_t> (juceIndex)] : -1;
}

float ApvtsParameterStore::plain (int index) const
{
    // the parameter itself, not the raw atomic of the tree: inside a change callback the atomic may not have caught up yet
    auto* p = parameterAt (index);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

float ApvtsParameterStore::normalised (int index) const
{
    auto* p = parameterAt (index);
    return p != nullptr ? p->getValue() : 0.0f;
}

float ApvtsParameterStore::toNormalised (int index, float plainValue) const
{
    auto* p = parameterAt (index);
    return p != nullptr ? p->convertTo0to1 (plainValue) : 0.0f;
}

float ApvtsParameterStore::fromNormalised (int index, float normalisedValue) const
{
    auto* p = parameterAt (index);
    return p != nullptr ? p->convertFrom0to1 (normalisedValue) : 0.0f;
}

void ApvtsParameterStore::beginGesture (int index)
{
    if (auto* p = parameterAt (index))
        p->beginChangeGesture();
}

void ApvtsParameterStore::endGesture (int index)
{
    if (auto* p = parameterAt (index))
        p->endChangeGesture();
}

void ApvtsParameterStore::setNormalised (int index, float normalisedValue)
{
    if (auto* p = parameterAt (index))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalisedValue));
}

void ApvtsParameterStore::parameterValueChanged (int juceIndex, float)
{
    const int index = tableIndexOf (juceIndex);
    if (index < 0)
        return;
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        notifyChanged (index);
        return;
    }
    changedOffThread[static_cast<size_t> (index)].store (true);
    triggerAsyncUpdate();
}

void ApvtsParameterStore::parameterGestureChanged (int juceIndex, bool starting)
{
    if (const int index = tableIndexOf (juceIndex); index >= 0)
        notifyGesture (index, starting);
}

void ApvtsParameterStore::handleAsyncUpdate()
{
    for (size_t i = 0; i < parameters.size(); ++i)
        if (changedOffThread[i].exchange (false))
            notifyChanged (static_cast<int> (i));
}
} // namespace synthyboy::ui
