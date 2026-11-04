/*
 * SynthyBoy - minimal 8-voice synthesizer (VST3 + CLAP): thin JUCE shell around synthyboy_core
 * Copyright (C) 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "PluginProcessor.h"

#include <algorithm>

namespace
{
/** The engine the shell was configured with (CMake SYNTHYBOY_SHELL_ENGINE; own is the default). */
std::unique_ptr<synthyboy::IEngine> createShellEngine()
{
   #if SYNTHYBOY_SHELL_ENGINE_VITAL
    if (auto vital = synthyboy::createEngine (synthyboy::EngineKind::vital))
        return vital;
   #endif
    return synthyboy::createOwnEngine();
}
} // namespace

SynthyBoyProcessor::SynthyBoyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SynthyBoyState", createParameterLayout()),
      engine (createShellEngine())
{
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        rawParams[i] = apvts.getRawParameterValue (table[i].key);
        engine->setParam (table[i].id, table[i].def);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SynthyBoyProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto* table = synthyboy::paramTable();

    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const synthyboy::ParamInfo& info = table[i];
        const ParameterID id { info.key, 1 };

        if (info.scale == synthyboy::Scale::stepped)
        {
            StringArray choices;
            for (size_t k = 0; k < synthyboy::choiceCount (info); ++k)
                choices.add (synthyboy::choiceName (info, k));

            layout.add (std::make_unique<AudioParameterChoice> (id, info.name, choices, static_cast<int> (info.def)));
        }
        else
        {
            // display text comes from the core, so a hardware knob and the plugin GUI show the same string
            // (fixed precision: value -> text -> value -> text is stable, clap-validator param-conversions)
            layout.add (std::make_unique<AudioParameterFloat> (
                id, info.name,
                NormalisableRange<float> (info.min, info.max, info.interval, info.skew), info.def,
                AudioParameterFloatAttributes().withLabel (synthyboy::unitLabel (info.unit))
                    .withStringFromValueFunction ([&info] (float v, int)
                    {
                        char text[64];
                        synthyboy::valueToText (info, v, text, sizeof text);
                        return String (text);
                    })
                    .withValueFromStringFunction ([&info] (const String& t)
                    {
                        float v = info.def;
                        synthyboy::textToValue (info, t.toRawUTF8(), v);
                        return v;
                    })));
        }
    }

    return layout;
}

void SynthyBoyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    events.resize (static_cast<size_t> (maxEventsPerBlock));

    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        engine->setParam (table[i].id, rawParams[i]->load());

    engine->prepare (sampleRate, samplesPerBlock, synthyboy::Quality {});
}

bool SynthyBoyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return layouts.inputBuses.isEmpty()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void SynthyBoyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // parameters: read every value once per block
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        engine->setParam (table[i].id, rawParams[i]->load());

    // MIDI -> sample accurate events (fixed capacity, overflow is dropped and counted)
    const int capacity = static_cast<int> (events.size());
    int numEvents = 0;

    for (const auto metadata : midiMessages)
    {
        if (metadata.numBytes > 3) // only channel messages matter, and short ones do not allocate
            continue;

        const auto message = metadata.getMessage();
        synthyboy::Event e;
        e.frame = std::clamp (metadata.samplePosition, 0, numSamples - 1);
        e.channel = static_cast<int16_t> (std::clamp (message.getChannel() - 1, 0, 15));

        if (message.isNoteOn())
        {
            e.type = synthyboy::Event::NoteOn;
            e.key = static_cast<int16_t> (message.getNoteNumber());
            e.value = message.getFloatVelocity();
        }
        else if (message.isNoteOff())
        {
            e.type = synthyboy::Event::NoteOff;
            e.key = static_cast<int16_t> (message.getNoteNumber());
        }
        else if (message.isPitchWheel())
        {
            e.type = synthyboy::Event::PitchBend;
            e.value = (static_cast<float> (message.getPitchWheelValue()) - 8192.0f) / 8192.0f;
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            e.type = synthyboy::Event::AllNotesOff;
        }
        else
        {
            continue;
        }

        if (numEvents < capacity)
            events[static_cast<size_t> (numEvents++)] = e;
        else
            droppedEvents.fetch_add (1);
    }

    engine->process (events.data(), numEvents, buffer.getWritePointer (0),
                     numChannels > 1 ? buffer.getWritePointer (1) : nullptr, numSamples);
}

juce::AudioProcessorEditor* SynthyBoyProcessor::createEditor()
{
    // Generic parameter GUI - a custom editor is out of scope for this spike.
    return new juce::GenericAudioProcessorEditor (*this);
}

synthyboy::ParamValues SynthyBoyProcessor::currentValues() const
{
    auto values = synthyboy::ParamValues::defaults();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        if (rawParams[i] != nullptr)
            values.plain[i] = rawParams[i]->load();
    return values;
}

void SynthyBoyProcessor::applyValues (const synthyboy::ParamValues& values)
{
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        if (auto* parameter = apvts.getParameter (table[i].key))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (values.plain[i]));
}

bool SynthyBoyProcessor::loadLegacyXml (const void* data, int sizeInBytes, synthyboy::ParamValues& values) const
{
    // the spike's state: <SynthyBoyState><PARAM id="gain" value="-12.0"/>...</SynthyBoyState> in JUCE's binary XML wrapper
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    for (auto* child : xml->getChildIterator())
    {
        if (! child->hasTagName ("PARAM"))
            continue;

        const auto* info = synthyboy::findParamByKey (child->getStringAttribute ("id").toRawUTF8());
        if (info == nullptr || ! child->hasAttribute ("value"))
            continue;

        const auto index = static_cast<size_t> (synthyboy::findParamIndex (info->id));
        const double v = std::clamp (child->getDoubleAttribute ("value"), static_cast<double> (info->min), static_cast<double> (info->max));
        values.plain[index] = synthyboy::constrainValue (*info, static_cast<float> (v));
    }
    return true;
}

void SynthyBoyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto text = synthyboy::saveState (currentValues());
    destData.replaceAll (text.data(), text.size());
}

void SynthyBoyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto values = currentValues();
    const auto result = synthyboy::loadState (data, static_cast<size_t> (sizeInBytes), values);

    bool apply = result.status == synthyboy::LoadStatus::ok || result.status == synthyboy::LoadStatus::newer_version;
    if (result.status == synthyboy::LoadStatus::invalid)
        apply = loadLegacyXml (data, sizeInBytes, values);

    if (! apply)
        return;

    applyValues (values);

    // Values changed behind the host's back: the CLAP wrapper turns "program changed" into
    // params.rescan(VALUES), VST3 into a restartComponent (clap-validator state-reproducibility).
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

// This creates new instances of the plugin (entry point used by the VST3 and CLAP wrappers).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthyBoyProcessor();
}
