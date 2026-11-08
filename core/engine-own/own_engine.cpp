// SynthyBoy - own engine: the spike's 8-voice synth behind IEngine, without JUCE.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "own_engine.h"

#include <algorithm>
#include <cmath>

namespace synthyboy
{
static_assert (std::atomic<float>::is_always_lock_free, "parameter atomics must be lock free (audio thread)");

std::unique_ptr<IEngine> createOwnEngine()
{
    return std::make_unique<own::OwnEngine>();
}

namespace own
{
OwnEngine::OwnEngine()
{
    const auto* table = paramTable();
    for (size_t i = 0; i < kMaxParams; ++i)
        params[i].store (i < paramCount() ? table[i].def : 0.0f, std::memory_order_relaxed);
}

float OwnEngine::gainFromDb (float db) noexcept
{
    return db > -60.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; // juce::Decibels::decibelsToGain (db, -60)
}

float OwnEngine::readParam (uint32_t id) const noexcept
{
    const int i = findParamIndex (id);
    return i >= 0 ? params[static_cast<size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
}

void OwnEngine::setParam (uint32_t id, float plain) noexcept
{
    const int i = findParamIndex (id);
    if (i < 0 || std::isnan (plain))
        return;
    params[static_cast<size_t> (i)].store (constrainValue (paramTable()[i], plain), std::memory_order_relaxed);
}

float OwnEngine::getParam (uint32_t id) const noexcept
{
    return readParam (id);
}

bool OwnEngine::supportsParam (uint32_t id) const noexcept
{
    // the subset this engine implements (core/engine-own/README.md)
    return id == kParamMasterVolume || id == kParamMasterBendRange || id == kParamMasterTranspose || id == kParamMasterTune
        || id == kParamOsc1Wavetable || id == kParamFilter1On || id == kParamFilter1Cutoff;
}

void OwnEngine::setQuality (const Quality& newQuality) noexcept
{
    // the own engine only uses the voice cap (it has no unison, oversampling, wavetable interpolation or effects)
    voiceCap.store (std::clamp (newQuality.maxVoices, 1, kMaxVoices), std::memory_order_relaxed);
}

void OwnEngine::prepare (double newSampleRate, int /*maxBlockFrames*/, const Quality& quality)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    const int numVoices = std::clamp (quality.maxVoices, 1, kMaxVoices);
    voiceCap.store (numVoices, std::memory_order_relaxed);
    voices.assign (static_cast<size_t> (kMaxVoices), Voice());
    sustainPending.assign (static_cast<size_t> (kMaxVoices), static_cast<uint8_t> (0));
    for (auto& v : voices)
        v.prepare (sampleRate);

    smoothedGain.reset (sampleRate, 0.02);
    prepared = true;
    reset();
}

void OwnEngine::reset()
{
    for (auto& v : voices)
        v.kill();
    for (auto& b : bendSemitones)
        b = 0.0f;
    for (auto& d : sustainDown)
        d = false;
    std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
    noteCounter = 0;
    smoothedGain.setCurrentAndTarget (gainFromDb (readParam (kParamMasterVolume)));
    activeCount.store (0, std::memory_order_relaxed);
}

void OwnEngine::noteOn (const Event& e) noexcept
{
    if (e.key < 0 || e.key > 127)
        return;
    const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);

    // hitting a key that still rings: let the old note fade out first (as the spike's juce::Synthesiser did)
    noteOff (e.key, channel);

    // the pool has kMaxVoices voices, at most voiceCap of them sound (a lowered cap releases the surplus lazily: a new note
    // takes a free voice below the cap, else steals the oldest one)
    const size_t cap = static_cast<size_t> (std::clamp (voiceCap.load (std::memory_order_relaxed), 1, kMaxVoices));
    Voice* chosen = nullptr;
    for (size_t i = 0; i < cap; ++i)
        if (! voices[i].isActive())
        {
            chosen = &voices[i];
            break;
        }

    if (chosen == nullptr) // steal the oldest voice
    {
        chosen = &voices.front();
        for (size_t i = 0; i < cap; ++i)
            if (voices[i].getAge() < chosen->getAge())
                chosen = &voices[i];
    }

    const float velocity = std::isnan (e.value) ? 0.0f : std::clamp (e.value, 0.0f, 1.0f);
    chosen->start (e.key, channel, e.noteId, velocity, bendSemitones[channel], noteCounter++, noteOffsetSemitones);
}

void OwnEngine::noteOff (int key, int channel) noexcept
{
    for (auto& v : voices)
        if (v.isHeld() && v.getKey() == key && v.getChannel() == channel)
        {
            if (sustainDown[channel])
                sustainPending[static_cast<size_t> (&v - voices.data())] = 1; // the pedal keeps the note until it is lifted
            else
                v.release();
        }
}

void OwnEngine::setSustain (int channel, bool down) noexcept
{
    const int first = channel < 0 ? 0 : channel, last = channel < 0 ? numChannels - 1 : channel;
    for (int c = first; c <= last; ++c)
        sustainDown[c] = down;
    if (down)
        return;
    for (size_t i = 0; i < voices.size(); ++i)
        if (sustainPending[i] != 0 && (channel < 0 || voices[i].getChannel() == channel))
        {
            sustainPending[i] = 0;
            voices[i].release();
        }
}

void OwnEngine::handleEvent (const Event& e) noexcept
{
    switch (e.type)
    {
        case Event::NoteOn:
            noteOn (e);
            break;
        case Event::NoteOff:
            noteOff (e.key, std::clamp<int> (e.channel, 0, numChannels - 1));
            break;
        case Event::PitchBend:
        {
            const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
            const float bend = std::isnan (e.value) ? 0.0f : std::clamp (e.value, -1.0f, 1.0f) * bendRange;
            bendSemitones[channel] = bend;
            for (auto& v : voices)
                if (v.isActive() && v.getChannel() == channel)
                    v.setBend (bend);
            break;
        }
        case Event::AllNotesOff:
            for (auto& v : voices)
                if (e.channel < 0 || v.getChannel() == e.channel)
                    v.release();
            std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
            break;
        case Event::SustainPedal:
            setSustain (e.channel < 0 ? -1 : std::clamp<int> (e.channel, 0, numChannels - 1), e.value >= 0.5f);
            break;
        case Event::NoteExpression:
            // the only expression this engine can use: the per-note pitch (semitones on top of the channel bend); pressure and slide have
            // no destination without a modulation matrix
            if (e.expression == Event::Tuning && std::isfinite (e.value))
            {
                const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
                for (auto& v : voices)
                    if (v.isActive() && v.getChannel() == channel && (e.key < 0 || v.getKey() == e.key))
                        v.setBend (bendSemitones[channel] + std::clamp (e.value, -96.0f, 96.0f));
            }
            break;
        case Event::ModWheel:   // no modulation in this engine
        case Event::Aftertouch:
            break;
    }
}

void OwnEngine::renderVoices (float* out, int numFrames) noexcept
{
    for (auto& v : voices)
        v.render (out, numFrames, waveform, filterCoeff);
}

void OwnEngine::process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept
{
    if (left == nullptr || numFrames <= 0)
        return;

    std::fill (left, left + numFrames, 0.0f);

    if (prepared)
    {
        // parameters are read once per block
        // the sine and the saw are the first two built-in wavetables; every other entry plays the saw in this engine
        waveform = readParam (kParamOsc1Wavetable) < 0.5f ? Waveform::sine : Waveform::saw;
        const double fc = std::clamp (static_cast<double> (readParam (kParamFilter1Cutoff)), 20.0, std::max (20.0, 0.45 * sampleRate));
        filterCoeff = readParam (kParamFilter1On) < 0.5f ? 1.0f // filter off: the one-pole follows its input exactly
                                                         : static_cast<float> (1.0 - std::exp (-kTwoPi * fc / sampleRate));
        smoothedGain.setTarget (gainFromDb (readParam (kParamMasterVolume)));
        bendRange = readParam (kParamMasterBendRange);
        noteOffsetSemitones = readParam (kParamMasterTranspose) + readParam (kParamMasterTune) * 0.01f;
        if (voiceCap.load (std::memory_order_relaxed) < kMaxVoices)
        {
            // a lowered cap: voices above it are released (they fade out), they are not cut
            const size_t cap = static_cast<size_t> (std::clamp (voiceCap.load (std::memory_order_relaxed), 1, kMaxVoices));
            for (size_t i = cap; i < voices.size(); ++i)
                voices[i].release();
        }

        int pos = 0;
        for (int i = 0; i < numEvents; ++i)
        {
            const int frame = std::clamp (events[i].frame, pos, numFrames); // tolerate unsorted / out of range input
            if (frame > pos)
            {
                renderVoices (left + pos, frame - pos);
                pos = frame;
            }
            handleEvent (events[i]);
        }
        if (pos < numFrames)
            renderVoices (left + pos, numFrames - pos);

        for (int i = 0; i < numFrames; ++i)
            left[i] *= smoothedGain.next();

        int count = 0;
        for (const auto& v : voices)
            count += v.isActive() ? 1 : 0;
        activeCount.store (count, std::memory_order_relaxed);
    }

    if (right != nullptr)
        std::copy (left, left + numFrames, right);
}
} // namespace own
} // namespace synthyboy
