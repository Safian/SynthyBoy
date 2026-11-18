// SynthyBoy - core/sample-osc: polyphony-ready, real-time safe sample oscillator voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "synthyboy/sampleosc/sample_types.h"

#include <algorithm>
#include <limits>

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

    // --- SynthyBoy extensions (engine-vital integration) ---
    /** Continuous pitch relative to the root key in semitones (bend, transposition, tune, unison detune of the host oscillator). Once
        set it replaces the pitch derived from the played key and params.tune; NaN returns to the key based pitch. Real-time safe. */
    void setSemitoneOffset (float semitones) noexcept;

    /** Playback speed multiplier on top of the pitch (the "scan" of the oscillator): 1 = as recorded, 0 freezes the read head, negative
        plays backwards. Real-time safe. */
    void setSpeedMultiplier (double speed) noexcept;

    /** Ends the loop: the voice plays on to the end of its region (one-shot behaviour), used for "exit loop on release" and Tailed. */
    void releaseLoop() noexcept;

    /** Renders numFrames with a per-frame rate multiplier (frequency modulation of the read head); rateMod may be null (= process). */
    void processModulated (float* outLeft, float* outRight, int numFrames, const float* rateMod) noexcept;

    /** Sets the read head to a fraction (0..1) of the playback region right now (uni start / start phase of the host oscillator). */
    void seekFraction (double fraction) noexcept;

    /** Measurements only (the bench's --scalar-content): render with the per sample reference (renderFrame) instead of the block kernel. Both give the same bits; the
        kernel only does once per block what the reference does for every sample. */
    void setReferenceKernels (bool on) noexcept { reference_ = on; }
    /** Tests: false runs the block kernel with its scalar form only (the vector form, four samples at a time on ARM with NEON, gives the same bits). */
    void setVectorKernels (bool on) noexcept { vectors_ = on; }
    /** Tests only: the block kernel computes the log2 of the pitch ratio and the pan gains on every call, as before they were cached by their inputs. */
    void setLegacyCaches (bool on) noexcept { legacyCaches_ = on; }

    // --- Status queries ---
    bool isActive() const noexcept { return active_; }
    bool isReleasing() const noexcept { return stopping_; }
    int getKey() const noexcept { return midiKey_; }
    double getPlayheadPosition() const noexcept { return playhead_; }
    /** The read head as a fraction (0 .. 1) of the file; -1 when the voice has no sample or does not play (the read-out of the display). */
    float playheadFraction() const noexcept { return active_ && sample_ != nullptr && sample_->numFrames > 1 ? static_cast<float> (std::min (1.0, std::max (0.0, playhead_ / static_cast<double> (sample_->numFrames)))) : -1.0f; }
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
    void renderFrame (float& outL, float& outR, double rateMult) noexcept;
    /** The block kernel: `numFrames` samples written (add == false; outRight may be null) or added to the buffers (add == true), the same bits as numFrames calls of renderFrame. */
    void renderBlock (float* outLeft, float* outRight, int numFrames, const float* rateMod, bool add) noexcept;
    template <QualityTier Tier, bool Add>
    void renderBlockT (float* outLeft, float* outRight, int numFrames, const float* rateMod) noexcept;

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
    float semitoneOffset_ = std::numeric_limits<float>::quiet_NaN();
    double speed_ = 1.0;

    // Unison per-voice modifiers
    float unisonPitchRatio_ = 1.0f;
    float unisonStartOffset_ = 0.0f;
    float unisonPan_ = 0.0f;
    float unisonLevel_ = 1.0f;

    /** The block kernel counts frames with 32 bit integers (a 64 bit conversion of a double is a library call on 32 bit ARM): a sample of more than 2^30 frames plays through the reference. */
    bool useKernel() const noexcept { return ! reference_ && (sample_ == nullptr || sample_->numFrames <= (int64_t { 1 } << 30)); }
    bool reference_ = false;
    bool vectors_ = true;
    bool legacyCaches_ = false;
    // the block kernel's per call libm results and what they were made for (a NaN key never matches)
    double cachedLogKey_ = std::numeric_limits<double>::quiet_NaN(), cachedLog_ = 0.0;
    float cachedPanKey_ = std::numeric_limits<float>::quiet_NaN(), cachedGainL_ = 1.0f, cachedGainR_ = 0.0f;
    // the pitch ratio of the last updatePitchRatio (2^(semitones / 12) is the dear part, and the semitones rarely change from one control period to the next)
    float cachedSemitones_ = std::numeric_limits<float>::quiet_NaN();
    double cachedSpeedFromPitch_ = 1.0;

    // Anti-click envelope
    float antiClickGain_ = 0.0f;
    float antiClickStep_ = 0.0f;
};

} // namespace sampleosc
} // namespace synthyboy
