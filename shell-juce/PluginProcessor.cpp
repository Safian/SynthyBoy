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

#include "synthyboy/midi_events.h"
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
/** The engine the shell was configured with (CMake SYNTHYBOY_SHELL_ENGINE: the Vital-derived engine whenever it is part of the
    build, the own engine otherwise or on request). */
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
    caps = engine->capabilities();
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        rawParams[i] = apvts.getRawParameterValue (table[i].key);
        engine->setParam (table[i].id, table[i].def);
        sentToEngine[i] = std::numeric_limits<float>::quiet_NaN(); // the first block sends everything
    }
}

namespace
{
/** A node of the group tree built from the " / " hierarchy of ParamInfo::group. */
struct GroupNode
{
    std::string name;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    std::vector<std::unique_ptr<GroupNode>> children;

    GroupNode* child (const std::string& childName)
    {
        for (auto& c : children)
            if (c->name == childName)
                return c.get();
        children.push_back (std::make_unique<GroupNode>());
        children.back()->name = childName;
        return children.back().get();
    }
};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SynthyBoyProcessor::createParameterLayout()
{
    using namespace juce;

    const auto* table = synthyboy::paramTable();

    // Parameters are collected per group first (the groups are the CLAP "module" path and the sections of the generic editor).
    // Every parameter stays visible: the generic editor shows the 64 modulation slots as 64 small sections.
    GroupNode root;

    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const synthyboy::ParamInfo& info = table[i];
        const ParameterID id { info.key, 1 };

        std::unique_ptr<RangedAudioParameter> parameter;
        if (info.scale == synthyboy::Scale::stepped)
        {
            StringArray choices;
            for (size_t k = 0; k < synthyboy::choiceCount (info); ++k)
                choices.add (synthyboy::choiceName (info, k));

            parameter = std::make_unique<AudioParameterChoice> (id, info.name, choices, static_cast<int> (info.def));
        }
        else
        {
            // display text comes from the core, so a hardware knob and the plugin GUI show the same string
            // (fixed precision: value -> text -> value -> text is stable, clap-validator param-conversions)
            parameter = std::make_unique<AudioParameterFloat> (
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
                    }));
        }

        // the group path "Osc 1 / Unison" -> node Osc 1 -> node Unison
        GroupNode* node = &root;
        const std::string path = info.group;
        size_t pos = 0;
        while (true)
        {
            const size_t next = path.find (" / ", pos);
            node = node->child (path.substr (pos, next == std::string::npos ? std::string::npos : next - pos));
            if (next == std::string::npos)
                break;
            pos = next + 3;
        }
        node->parameters.push_back (std::move (parameter));
    }

    // JUCE wants the groups as finished objects: convert the tree bottom up (parameters before sub groups, as listed)
    struct Builder
    {
        static std::unique_ptr<AudioProcessorParameterGroup> build (GroupNode& node, const std::string& path)
        {
            auto group = std::make_unique<AudioProcessorParameterGroup> (path, node.name, " | ");
            for (auto& p : node.parameters)
                group->addChild (std::move (p));
            for (auto& c : node.children)
                group->addChild (build (*c, path + "/" + c->name));
            return group;
        }
    };

    AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& top : root.children)
        layout.add (Builder::build (*top, top->name));
    return layout;
}

void SynthyBoyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    events.resize (static_cast<size_t> (maxEventsPerBlock));

    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const float v = rawParams[i]->load();
        engine->setParam (table[i].id, v);
        sentToEngine[i] = v;
    }

    // the quality tier of this build (CMake SYNTHYBOY_DEFAULT_TIER, high on desktop); a tier only caps, it never changes parameters
    // Live playback never waits for a helper thread of the engine; an offline bounce (isNonRealtime) is deterministic (Quality::realtime).
    auto quality = synthyboy::qualityForTier (synthyboy::kDefaultTier);
    quality.realtime = ! isNonRealtime();
    engine->prepare (sampleRate, samplesPerBlock, quality);
    setLatencySamples (engine->latencySamples());
}

void SynthyBoyProcessor::setNonRealtime (bool isNonRealtime) noexcept
{
    juce::AudioProcessor::setNonRealtime (isNonRealtime);
    auto quality = synthyboy::qualityForTier (synthyboy::kDefaultTier);
    quality.realtime = ! isNonRealtime;
    engine->setQuality (quality);
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

    // parameters: one atomic load and one compare per parameter and block; the engine is told only about the ones that changed
    // (no allocation, no lock: rawParams are the atomics of the JUCE parameters)
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const float v = rawParams[i]->load (std::memory_order_relaxed);
        if (! std::equal_to<float>() (v, sentToEngine[i])) // exact comparison on purpose; the NaN of the first block never equals
        {
            engine->setParam (table[i].id, v);
            sentToEngine[i] = v;
        }
    }

    // notes played on the on-screen keyboard join the host's MIDI input (MidiKeyboardState takes its own lock for a few instructions)
    keyboard.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    // UI injected MIDI messages (pitch wheel, mod wheel, CC 64 sustain, etc.)
    {
        std::lock_guard<std::mutex> lock (uiMidiMutex);
        if (! uiMidiBuffer.isEmpty())
        {
            midiMessages.addEvents (uiMidiBuffer, 0, numSamples, 0);
            uiMidiBuffer.clear();
        }
    }

    // MIDI -> sample accurate events (fixed capacity, overflow is dropped and counted)
    const int capacity = static_cast<int> (events.size());
    int numEvents = 0;

    for (const auto metadata : midiMessages)
    {
        if (metadata.numBytes > 3) // only channel messages matter, and short ones do not allocate
            continue;

        const auto message = metadata.getMessage();
        synthyboy::Event e;
        // note on/off (with release velocity), pitch bend, mod wheel, CC 64 sustain pedal, CC 74 slide, channel and poly pressure,
        // all notes off: core/include/synthyboy/midi_events.h (shared with the CLAP shell)
        if (! synthyboy::translateMidiMessage (message.getRawData(), message.getRawDataSize(),
                                               std::clamp (metadata.samplePosition, 0, numSamples - 1), e))
            continue;

        if (numEvents < capacity)
            events[static_cast<size_t> (numEvents++)] = e;
        else
            droppedEvents.fetch_add (1);
    }

    {
        // host transport (tempo sync, beat units, LFO HOST mode)
        synthyboy::TransportInfo transport;
        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
            {
                if (const auto bpm = position->getBpm())
                    transport.bpm = *bpm > 0.0 ? *bpm : 120.0;
                if (const auto ppq = position->getPpqPosition())
                    transport.songPosBeats = *ppq;
                transport.playing = position->getIsPlaying();
            }
        engine->setTransport (transport);
    }

    engine->process (events.data(), numEvents, buffer.getWritePointer (0),
                     numChannels > 1 ? buffer.getWritePointer (1) : nullptr, numSamples);

    const float magL = buffer.getMagnitude (0, 0, numSamples);
    const float magR = numChannels > 1 ? buffer.getMagnitude (1, 0, numSamples) : magL;
    peakL.store (magL, std::memory_order_relaxed);
    peakR.store (magR, std::memory_order_relaxed);

    // Bode's latency (the engine delays every other path to match): tell the host when it changes
    const int latency = engine->latencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

double SynthyBoyProcessor::getTailLengthSeconds() const
{
    // the longest release of the envelopes plus the tails of the effects that are on (from the current parameter values)
    const auto value = [this] (const char* key)
    {
        const auto* info = synthyboy::findParamByKey (key);
        const auto index = info != nullptr ? synthyboy::findParamIndex (info->id) : -1;
        return index >= 0 && rawParams[static_cast<size_t> (index)] != nullptr ? static_cast<double> (rawParams[static_cast<size_t> (index)]->load()) : 0.0;
    };
    double release = 0.0;
    for (int n = 1; n <= 6; ++n)
        release = std::max (release, value (("env" + std::to_string (n) + ".release").c_str()) * 0.001);
    double effects = 0.0;
    if (value ("reverb.on") > 0.5)
        effects = std::max (effects, value ("reverb.decay") * 0.001 + value ("reverb.predelay") * 0.001);
    if (value ("delay.on") > 0.5)
        effects = std::max (effects, 4.0 + 8.0 * std::abs (value ("delay.feedback")) * 0.01 * 4.0); // repeats fade with the feedback
    return release + effects;
}

juce::AudioProcessorEditor* SynthyBoyProcessor::createEditor()
{
    // SYNTHYBOY_GENERIC_UI=1: the generic parameter editor (one section per parameter group) stays reachable for debugging
    if (juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_GENERIC_UI", {}).trim() == "1")
        return new juce::GenericAudioProcessorEditor (*this);
    return new synthyboy::ui::SynthyBoyEditor (*this, *this);
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
        {
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (values.plain[i]));

            // The normalised round trip (plain -> 0..1 -> plain) drifts in the last float bits (-12 -> -11.9999981),
            // which makes the saved state differ from the loaded one. Store the exact value in the raw atomic; skipped when
            // the round trip landed somewhere genuinely different (e.g. stepped or clamped parameters).
            if (rawParams[i] != nullptr)
            {
                const float exact = values.plain[i];
                const float stored = rawParams[i]->load();
                if (std::abs (stored - exact) <= 1.0e-4f * std::max (1.0f, std::abs (exact)))
                    rawParams[i]->store (exact);
            }
        }
}

bool SynthyBoyProcessor::loadLegacyXml (const void* data, int sizeInBytes, synthyboy::ParamValues& values) const
{
    // the spike's state: <SynthyBoyState><PARAM id="gain" value="-12.0"/>...</SynthyBoyState> in JUCE's binary XML wrapper
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    for (auto* child : xml->getChildIterator())
    {
        if (! child->hasTagName ("PARAM") || ! child->hasAttribute ("value"))
            continue;

        // the three parameters of the spike (gain, waveform, cutoff) go through the migration of state format version 1
        synthyboy::migrateLegacyParam (child->getStringAttribute ("id").toRawUTF8(), child->getDoubleAttribute ("value"), values);
    }
    return true;
}

void SynthyBoyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::string text;
    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        text = synthyboy::saveState (currentValues(), patchContent);
    }
    destData.replaceAll (text.data(), text.size());
}

void SynthyBoyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto values = currentValues();
    synthyboy::StateContent loadedContent;
    const auto result = synthyboy::loadStateWithContent (data, static_cast<size_t> (sizeInBytes), values, loadedContent);

    bool apply = result.status == synthyboy::LoadStatus::ok || result.status == synthyboy::LoadStatus::newer_version;
    if (result.status == synthyboy::LoadStatus::invalid)
        apply = loadLegacyXml (data, sizeInBytes, values);

    if (! apply)
        return;

    applyValues (values);
    {
        // a loaded document is a whole patch; the legacy XML of the spike has no content (loadedContent is empty then)
        const std::lock_guard<std::mutex> lock (contentMutex);
        patchContent = std::move (loadedContent);
        if (engine != nullptr)
            engine->setContent (patchContent); // message thread: allocates, handed to the audio thread lock free
    }

    // Values changed behind the host's back: the CLAP wrapper turns "program changed" into
    // params.rescan(VALUES), VST3 into a restartComponent (clap-validator state-reproducibility).
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

synthyboy::StateContent SynthyBoyProcessor::content() const
{
    const std::lock_guard<std::mutex> lock (contentMutex);
    return patchContent;
}

bool SynthyBoyProcessor::setContent (const synthyboy::StateContent& newContent)
{
    if (! synthyboy::validateContent (newContent))
        return false;
    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        if (newContent == patchContent)
            return true;
        patchContent = newContent;
        if (engine != nullptr)
            engine->setContent (patchContent); // message thread: allocates, handed to the audio thread lock free
    }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

void SynthyBoyProcessor::injectMidiMessage (const juce::MidiMessage& msg)
{
    std::lock_guard<std::mutex> lock (uiMidiMutex);
    uiMidiBuffer.addEvent (msg, 0);
}


// This creates new instances of the plugin (entry point used by the VST3 and CLAP wrappers).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthyBoyProcessor();
}
