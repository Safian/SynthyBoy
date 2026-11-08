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
#include "synthyboy/content_state.h"
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

    /** Live output meter peaks (linear amplitude, e.g. 0..1 or higher if clipping) from the audio callback. */
    virtual float outputPeakL() const noexcept { return 0.0f; }
    virtual float outputPeakR() const noexcept { return 0.0f; }

    /** The `content` section of the patch (custom LFO shapes, ...; not parameters). A copy, message thread. */
    virtual StateContent content() const { return {}; }
    /** Replaces the content, hands it to the engine and marks the host state dirty. Returns false (nothing changed) if the content
        fails validateContent () or the host keeps no content. Message thread. */
    virtual bool setContent (const StateContent& /*newContent*/) { return false; }

    /** "Revert to saved" (design 07): the parameters and content go back to what the host last loaded (setStateInformation, or the defaults of a
        new instance). False when the host keeps no such snapshot. Message thread. */
    virtual bool revertToSaved() { return false; }

    /** Sounding voices (released ones that still fade out included) after the last audio block: IEngine::activeVoices (). 0 without an engine. */
    virtual int activeVoiceCount() const noexcept { return 0; }
    /** Load of the audio callback: the time process () needed for the last blocks as a fraction of the time the blocks last (smoothed;
        0 = idle, 1 = at the limit). 0 when the host does not measure. */
    virtual float cpuLoad() const noexcept { return 0.0f; }
    /** Copies the latest `maxSamples` (up to kScopeSamples) mono samples of the master output, oldest first, into `dest`; returns how many were
        copied (0 before the first block). Message thread; lock free on the audio side. */
    virtual int copyScope (float* /*dest*/, int /*maxSamples*/) const { return 0; }
    static constexpr int kScopeSamples = 4096;

    /** Injects a MIDI message from the UI (pitch bend, mod wheel, CC 64 sustain, etc.) into the audio callback. */
    virtual void injectMidiMessage (const juce::MidiMessage& /*msg*/) {}
};
} // namespace synthyboy::ui
