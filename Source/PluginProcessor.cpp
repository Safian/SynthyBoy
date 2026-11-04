/*
 * SynthyBoyVST - minimal 8-voice synthesizer (VST3 + CLAP)
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

namespace
{
namespace ids
{
const juce::ParameterID gain     { "gain", 1 };
const juce::ParameterID waveform { "waveform", 1 };
const juce::ParameterID cutoff   { "cutoff", 1 };
} // namespace ids
} // namespace

SynthyBoyProcessor::SynthyBoyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SynthyBoyState", createParameterLayout())
{
    gainParam     = apvts.getRawParameterValue (ids::gain.getParamID());
    waveformParam = apvts.getRawParameterValue (ids::waveform.getParamID());
    cutoffParam   = apvts.getRawParameterValue (ids::cutoff.getParamID());

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new synthyboy::SynthyVoice());

    synth.addSound (new synthyboy::SynthySound());
}

juce::AudioProcessorValueTreeState::ParameterLayout SynthyBoyProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::gain, "Gain",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), -12.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (
        ids::waveform, "Waveform", StringArray { "Sine", "Saw" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ids::cutoff, "Cutoff",
        NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.3f), 8000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    return layout;
}

void SynthyBoyProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load(), -60.0f));
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
    buffer.clear();

    const auto waveform = waveformParam->load() < 0.5f ? synthyboy::Waveform::sine : synthyboy::Waveform::saw;
    const auto cutoff   = cutoffParam->load();

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = static_cast<synthyboy::SynthyVoice*> (synth.getVoice (i)))
            voice->setParameters (waveform, cutoff);

    synth.renderNextBlock (buffer, midiMessages, 0, numSamples);

    smoothedGain.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load(), -60.0f));

    const int numChannels = buffer.getNumChannels();
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

juce::AudioProcessorEditor* SynthyBoyProcessor::createEditor()
{
    // Generic parameter GUI - a custom editor is out of scope for this spike.
    return new juce::GenericAudioProcessorEditor (*this);
}

void SynthyBoyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SynthyBoyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// This creates new instances of the plugin (entry point used by the VST3 and CLAP wrappers).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthyBoyProcessor();
}
