// SynthyBoy - UI: APVTS-backed modulation matrix access and the assign controller.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "UiContext.h"

namespace synthyboy::ui
{
ApvtsModMatrix::ApvtsModMatrix (juce::AudioProcessorValueTreeState& apvts)
{
    const auto* table = paramTable();
    const size_t n = paramCount();
    parameters.resize (n, nullptr);
    raw.resize (n, nullptr);
    inGesture.assign (n, 0);
    for (size_t i = 0; i < n; ++i)
    {
        parameters[i] = apvts.getParameter (table[i].key);
        raw[i] = apvts.getRawParameterValue (table[i].key);
    }
}

juce::RangedAudioParameter* ApvtsModMatrix::parameterAt (int index) const
{
    return index >= 0 && static_cast<size_t> (index) < parameters.size() ? parameters[static_cast<size_t> (index)] : nullptr;
}

float ApvtsModMatrix::get (int paramIndex) const
{
    if (paramIndex < 0 || static_cast<size_t> (paramIndex) >= raw.size() || raw[static_cast<size_t> (paramIndex)] == nullptr)
        return 0.0f;
    return raw[static_cast<size_t> (paramIndex)]->load();
}

void ApvtsModMatrix::set (int paramIndex, float plain)
{
    auto* p = parameterAt (paramIndex);
    if (p == nullptr)
        return;
    const bool wrap = inGesture[static_cast<size_t> (paramIndex)] == 0;
    if (wrap)
        p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    if (wrap)
        p->endChangeGesture();
}

void ApvtsModMatrix::beginGesture (int paramIndex)
{
    if (auto* p = parameterAt (paramIndex); p != nullptr && inGesture[static_cast<size_t> (paramIndex)] == 0)
    {
        inGesture[static_cast<size_t> (paramIndex)] = 1;
        p->beginChangeGesture();
    }
}

void ApvtsModMatrix::endGesture (int paramIndex)
{
    if (auto* p = parameterAt (paramIndex); p != nullptr && inGesture[static_cast<size_t> (paramIndex)] != 0)
    {
        inGesture[static_cast<size_t> (paramIndex)] = 0;
        p->endChangeGesture();
    }
}

// ---- ModAssignController ----------------------------------------------------------------------------------------------------------------

ModAssignController::ModAssignController (UiHost& h)
    : host (h), access (h.parameters()), assignFlow (access), current (ModSnapshot::build (access))
{
}

AssignFlow::Phase ModAssignController::tapSource (int sourceChoice)
{
    // a source that needs engine bits this engine lacks is refused (its chip is drawn dimmed)
    const int sourceKey = findParamIndex (hashKey ("mod01.source"));
    if (sourceChoice > 0 && ! capStateForValue (sourceKey, static_cast<float> (sourceChoice), host.engineCaps()).available)
    {
        setMessage (juce::String (modSourceTable()[sourceChoice - 1].name) + ": " + kNotAvailableText);
        return assignFlow.phase();
    }

    const auto phase = assignFlow.tapSource (sourceChoice);
    if (phase == AssignFlow::Phase::armed && current.full())
        setMessage ("Mod Matrix full (64/64 slots used): only existing assignments can be edited");
    else if (phase == AssignFlow::Phase::armed)
        setMessage ("Drag a purple-ringed control up or down to set the depth");
    else
        setMessage ({});
    sendChangeMessage();
    return phase;
}

void ModAssignController::exitAssignMode()
{
    assignFlow.cancel();
    setMessage ({});
    sendChangeMessage();
}

void ModAssignController::refresh()
{
    auto next = ModSnapshot::build (access);
    if (! (next == current))
    {
        current = std::move (next);
        sendChangeMessage();
    }
}

void ModAssignController::setMessage (const juce::String& m)
{
    statusMessage = m;
}
} // namespace synthyboy::ui
