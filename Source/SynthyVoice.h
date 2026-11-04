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

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>

namespace synthyboy
{
enum class Waveform
{
    sine = 0,
    saw  = 1
};

/** The only sound: every voice can play it. */
struct SynthySound final : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

/**
 * One polyphonic voice: sine or band-limited (PolyBLEP) saw oscillator,
 * simple ADSR envelope, 2 cascaded one-pole low-pass filters (12 dB/oct).
 */
class SynthyVoice final : public juce::SynthesiserVoice
{
public:
    SynthyVoice()
    {
        adsr.setParameters ({ 0.005f, 0.15f, 0.7f, 0.25f });
    }

    /** Called once per block from the audio thread, before rendering. */
    void setParameters (Waveform newWaveform, float cutoffHz) noexcept
    {
        waveform = newWaveform;

        const auto sr = getSampleRate();
        if (sr <= 0.0)
            return;

        const auto fc = std::clamp (static_cast<double> (cutoffHz), 20.0, std::max (20.0, 0.45 * sr));
        filterCoeff   = static_cast<float> (1.0 - std::exp (-juce::MathConstants<double>::twoPi * fc / sr));
    }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SynthySound*> (sound) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        if (newRate > 0.0)
            adsr.setSampleRate (newRate);
    }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override
    {
        baseFrequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        level         = velocity * 0.3f; // headroom for 8 voices
        phase         = 0.0;
        filterState1  = 0.0f;
        filterState2  = 0.0f;
        pitchWheelMoved (currentPitchWheelPosition);
        adsr.reset();
        adsr.noteOn();
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int newPitchWheelValue) override
    {
        // +-2 semitones
        bendSemitones = (static_cast<float> (newPitchWheelValue) - 8192.0f) / 8192.0f * 2.0f;
    }

    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        const auto sr        = getSampleRate();
        const auto increment = baseFrequency * std::pow (2.0, static_cast<double> (bendSemitones) / 12.0) / sr;
        const auto numChans  = output.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            float osc;
            if (waveform == Waveform::sine)
            {
                osc = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
            }
            else
            {
                osc = static_cast<float> (2.0 * phase - 1.0 - polyBlep (phase, increment));
            }

            float sample = osc * adsr.getNextSample() * level;

            filterState1 += filterCoeff * (sample - filterState1);
            filterState2 += filterCoeff * (filterState1 - filterState2);
            sample = filterState2;

            for (int ch = 0; ch < numChans; ++ch)
                output.addSample (ch, startSample + i, sample);

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    static double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    juce::ADSR adsr;
    Waveform waveform   = Waveform::sine;
    double baseFrequency = 440.0;
    double phase         = 0.0;
    float bendSemitones  = 0.0f;
    float level          = 0.0f;
    float filterCoeff    = 1.0f;
    float filterState1   = 0.0f;
    float filterState2   = 0.0f;
};
} // namespace synthyboy
