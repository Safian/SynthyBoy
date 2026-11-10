// SynthyBoy - UI: APVTS-backed modulation matrix access and the assign controller.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "UiContext.h"

namespace synthyboy::ui
{
StoreModMatrix::StoreModMatrix (ParameterStore& s) : store (s), inGesture (paramCount(), 0)
{
}

float StoreModMatrix::get (int paramIndex) const
{
    return store.contains (paramIndex) ? store.plain (paramIndex) : 0.0f;
}

void StoreModMatrix::set (int paramIndex, float plain)
{
    if (! store.contains (paramIndex))
        return;
    const bool wrap = inGesture[static_cast<size_t> (paramIndex)] == 0;
    if (wrap)
        store.beginGesture (paramIndex);
    store.setPlain (paramIndex, plain);
    if (wrap)
        store.endGesture (paramIndex);
}

void StoreModMatrix::beginGesture (int paramIndex)
{
    if (store.contains (paramIndex) && inGesture[static_cast<size_t> (paramIndex)] == 0)
    {
        inGesture[static_cast<size_t> (paramIndex)] = 1;
        store.beginGesture (paramIndex);
    }
}

void StoreModMatrix::endGesture (int paramIndex)
{
    if (store.contains (paramIndex) && inGesture[static_cast<size_t> (paramIndex)] != 0)
    {
        inGesture[static_cast<size_t> (paramIndex)] = 0;
        store.endGesture (paramIndex);
    }
}

// ---- ModAssignController ----------------------------------------------------------------------------------------------------------------

ModAssignController::ModAssignController (UiHost& h)
    : host (h), access (h.parameterStore()), assignFlow (access), current (ModSnapshot::build (access))
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
    const uint32_t midiRevision = host.midiMapRevision(); // MIDI learn: a CC was learned / removed (audio thread or UI)
    if (! (next == current) || midiRevision != lastMidiRevision)
    {
        current = std::move (next);
        lastMidiRevision = midiRevision;
        sendChangeMessage();
    }
}

void ModAssignController::setMessage (const juce::String& m)
{
    statusMessage = m;
}

ParamValues readLiveValues (const UiContext& ctx)
{
    return readAllValues (ctx.store());
}

void applyValues (const UiContext& ctx, const ParamValues& values)
{
    writeAllValues (ctx.store(), values);
}
} // namespace synthyboy::ui
