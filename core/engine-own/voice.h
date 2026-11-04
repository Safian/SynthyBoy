// SynthyBoy - own engine: ADSR and voice (port of the spike voice, without JUCE).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>

namespace synthyboy
{
namespace own
{
enum class Waveform
{
    sine = 0,
    saw  = 1
};

constexpr double kTwoPi = 6.283185307179586476925286766559;

/** Flushes values that would become denormal in a recursion. */
inline float flushDenormal (float v) noexcept
{
    return std::fabs (v) < 1.0e-18f ? 0.0f : v;
}

/** Linear attack / decay / release (same behaviour as the spike's juce::ADSR). */
class Adsr
{
public:
    void configure (double sampleRate, float attack, float decay, float sustainLevel, float release) noexcept
    {
        sr = sampleRate;
        sustain = sustainLevel;
        releaseSeconds = release;
        attackRate = attack > 0.0f ? static_cast<float> (1.0 / (attack * sampleRate)) : -1.0f;
        decayRate = decay > 0.0f ? static_cast<float> ((1.0f - sustainLevel) / (decay * sampleRate)) : -1.0f;
        reset();
    }

    void reset() noexcept
    {
        value = 0.0f;
        state = State::idle;
    }

    void noteOn() noexcept
    {
        if (attackRate > 0.0f)
        {
            state = State::attack;
        }
        else if (decayRate > 0.0f)
        {
            value = 1.0f;
            state = State::decay;
        }
        else
        {
            value = sustain;
            state = State::sustain;
        }
    }

    /** Starts the release from the current value; a no-op while idle or already releasing. */
    void noteOff() noexcept
    {
        if (state == State::idle || state == State::release)
            return;
        if (releaseSeconds > 0.0f)
        {
            releaseRate = static_cast<float> (value / (releaseSeconds * sr));
            state = State::release;
        }
        else
        {
            reset();
        }
    }

    bool isActive() const noexcept { return state != State::idle; }
    bool isReleasing() const noexcept { return state == State::release; }

    float next() noexcept
    {
        switch (state)
        {
            case State::idle:
                return 0.0f;
            case State::attack:
                value += attackRate;
                if (value >= 1.0f)
                {
                    value = 1.0f;
                    state = decayRate > 0.0f ? State::decay : State::sustain;
                }
                break;
            case State::decay:
                value -= decayRate;
                if (value <= sustain)
                {
                    value = sustain;
                    state = State::sustain;
                }
                break;
            case State::sustain:
                value = sustain;
                break;
            case State::release:
                value -= releaseRate;
                if (value <= 0.0f)
                    reset();
                break;
        }
        return value;
    }

private:
    enum class State { idle, attack, decay, sustain, release };

    State state = State::idle;
    double sr = 44100.0;
    float value = 0.0f, sustain = 0.7f, releaseSeconds = 0.25f;
    float attackRate = 0.0f, decayRate = 0.0f, releaseRate = 0.0f;
};

/**
 * One polyphonic voice: sine or band-limited (PolyBLEP) saw oscillator, ADSR (5 ms / 150 ms / 0.7 / 250 ms),
 * 2 cascaded one-pole low-pass filters (12 dB/oct). Output is mono; the engine copies it to both channels.
 */
class Voice
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        adsr.configure (newSampleRate, 0.005f, 0.15f, 0.7f, 0.25f);
        kill();
    }

    void start (int newKey, int newChannel, int newNoteId, float velocity, float bendSemitones, uint32_t newAge) noexcept
    {
        key = newKey;
        channel = newChannel;
        noteId = newNoteId;
        age = newAge;
        baseFrequency = 440.0 * std::pow (2.0, static_cast<double> (newKey - 69) / 12.0);
        level = velocity * 0.3f; // headroom for 8 voices
        phase = 0.0;
        filterState1 = 0.0f;
        filterState2 = 0.0f;
        setBend (bendSemitones);
        adsr.reset();
        adsr.noteOn();
        active = true;
    }

    /** Note off with tail. */
    void release() noexcept
    {
        if (active)
            adsr.noteOff();
    }

    void kill() noexcept
    {
        adsr.reset();
        active = false;
    }

    void setBend (float semitones) noexcept
    {
        increment = baseFrequency * std::pow (2.0, static_cast<double> (semitones) / 12.0) / sampleRate;
    }

    bool isActive() const noexcept { return active; }
    bool isHeld() const noexcept { return active && ! adsr.isReleasing(); }
    int getKey() const noexcept { return key; }
    int getChannel() const noexcept { return channel; }
    int getNoteId() const noexcept { return noteId; }
    uint32_t getAge() const noexcept { return age; }

    /** Adds numFrames of this voice to out. filterCoeff is the one-pole coefficient. */
    void render (float* out, int numFrames, Waveform waveform, float filterCoeff) noexcept
    {
        if (! active)
            return;

        for (int i = 0; i < numFrames; ++i)
        {
            float osc;
            if (waveform == Waveform::sine)
                osc = static_cast<float> (std::sin (kTwoPi * phase));
            else
                osc = static_cast<float> (2.0 * phase - 1.0 - polyBlep (phase, increment));

            float sample = osc * adsr.next() * level;

            filterState1 = flushDenormal (filterState1 + filterCoeff * (sample - filterState1));
            filterState2 = flushDenormal (filterState2 + filterCoeff * (filterState1 - filterState2));
            out[i] += filterState2;

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;

            if (! adsr.isActive())
            {
                active = false;
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

    Adsr adsr;
    bool active = false;
    int key = 0, channel = 0, noteId = -1;
    uint32_t age = 0;
    double sampleRate = 44100.0;
    double baseFrequency = 440.0;
    double increment = 0.0;
    double phase = 0.0;
    float level = 0.0f;
    float filterState1 = 0.0f, filterState2 = 0.0f;
};
} // namespace own
} // namespace synthyboy
