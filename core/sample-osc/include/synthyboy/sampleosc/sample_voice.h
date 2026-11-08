// SynthyBoy - core/sample-osc: polyphony-ready, real-time safe sample oscillator voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "synthyboy/sampleosc/sample_types.h"

namespace synthyboy
{
namespace sampleosc
{
/** Precomputed polyphase windowed-sinc tables for all quality tiers. */
struct SincTables
{
    static constexpr int kPhases = 512;
    static constexpr int kLowTaps = 4;
    static constexpr int kMedTaps = 8;
    static constexpr int kHighTaps = 16;

    float low[kPhases * kLowTaps] = {};
    float med[kPhases * kMedTaps] = {};
    float high[kPhases * kHighTaps] = {};

    static const SincTables& instance() noexcept;

private:
    void init() noexcept;
};

/**
 * Polyphony-ready, real-time safe sample oscillator voice.
 * No memory allocations or locks on audio thread (process/processAdd/noteOn/noteOff).
 */
class SampleVoice
{
public:
    SampleVoice() = default;
    ~SampleVoice() = default;

    /**
     * Prepares the voice for rendering at host sample rate.
     * Non-RT; initializes sinc tables and buffers if needed.
     */
    void prepare (double hostSampleRate, int maxBlockFrames = 512) noexcept;

    /** Resets playback state, silences audio, and stops voice. Real-time safe. */
    void reset() noexcept;

    /**
     * Starts playback of a note. Real-time safe.
     * midiKey: played note number (0..127).
     * velocity: note velocity [0.0, 1.0].
     * sample: non-owning pointer to sample view (must outlive playback).
     * params: initial parameters for the note.
     */
    void noteOn (int midiKey, float velocity, const SampleView* sample, const SampleOscParams& params) noexcept;

    /** Releases note. Triggers anti-click fade out. Real-time safe. */
    void noteOff() noexcept;

    /** Stops playback immediately with a short anti-click ramp. Real-time safe. */
    void stop() noexcept;

    /** Updates live parameters during playback. Real-time safe. */
    void setParams (const SampleOscParams& params) noexcept;

    // --- Unison hooks ---
    /** Unison detune multiplier for pitch ratio (e.g. 2^(cents / 1200.0)). */
    void setUnisonPitchRatio (float ratio) noexcept;

    /** Unison start phase offset [0.0, 1.0]. */
    void setUnisonStartOffset (float normalizedOffset) noexcept;

    /** Unison stereo pan offset added to params.pan [-1.0, +1.0]. */
    void setUnisonPan (float pan) noexcept;

    /** Unison level scaling multiplier. */
    void setUnisonLevel (float level) noexcept;

    // --- Status queries ---
    bool isActive() const noexcept { return active_; }
    bool isReleasing() const noexcept { return stopping_; }
    int getKey() const noexcept { return midiKey_; }
    double getPlayheadPosition() const noexcept { return playhead_; }
    double getPitchRatio() const noexcept { return pitchRatio_; }

    /**
     * Renders numFrames into outLeft and outRight, overwriting buffer contents.
     * If outRight is nullptr, renders mono to outLeft.
     */
    void process (float* outLeft, float* outRight, int numFrames) noexcept;

    /**
     * Renders numFrames and adds results to outLeft and outRight.
     */
    void processAdd (float* outLeft, float* outRight, int numFrames) noexcept;

private:
    void updatePitchRatio() noexcept;
    void renderFrame (float& outL, float& outR) noexcept;

    double hostSampleRate_ = 44100.0;
    const SampleView* sample_ = nullptr;
    SampleOscParams params_;

    bool active_ = false;
    bool stopping_ = false;
    int midiKey_ = 60;
    float velocity_ = 1.0f;

    double playhead_ = 0.0;
    double direction_ = 1.0;
    double pitchRatio_ = 1.0;

    // Unison per-voice modifiers
    float unisonPitchRatio_ = 1.0f;
    float unisonStartOffset_ = 0.0f;
    float unisonPan_ = 0.0f;
    float unisonLevel_ = 1.0f;

    // Anti-click envelope
    float antiClickGain_ = 0.0f;
    float antiClickStep_ = 0.0f;
};

} // namespace sampleosc
} // namespace synthyboy
