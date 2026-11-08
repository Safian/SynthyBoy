// SynthyBoy - UI: what the editor needs from its processor (and nothing more).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The UI never touches the engine or core internals: it reads and writes parameters through the APVTS, asks for the engine's capability
// bits (dimming), plays notes through a MidiKeyboardState and calls the preset callbacks. SynthyBoyProcessor implements this
// interface; the tests implement it with a stub.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <string>

#include "model/ModModel.h"
#include "synthyboy/engine_caps.h"

namespace synthyboy::ui
{
class UiHost
{
public:
    virtual ~UiHost() = default;

    /** Parameter access (message thread). Every table key is a parameter with ParameterID { key, 1 }. */
    virtual juce::AudioProcessorValueTreeState& parameters() = 0;
    /** IEngine::capabilities () of the engine behind the processor; constant for the life of the processor. */
    virtual EngineCapsMask engineCaps() const noexcept = 0;
    /** Notes played on the mini keyboard; the processor merges them into the MIDI input of the audio callback. */
    virtual juce::MidiKeyboardState& keyboardState() = 0;
    /** The two A/B snapshots live with the processor so they survive closing the editor. */
    virtual AbCompare& abCompare() = 0;

    /** Preset browsing is Kick's job; the strip only has prev / next and a name. direction is -1 or +1. */
    virtual void stepPreset (int direction) = 0;
    virtual juce::String presetName() const = 0;

    /** MIDI-learn placeholder hook (docs/ui-design/01 1.4: GAP, no CC map in core). The sheet enables its button only if canMidiLearn(). */
    virtual bool canMidiLearn() const noexcept { return false; }
    virtual void startMidiLearn (const std::string& /*key*/) {}
};
} // namespace synthyboy::ui
