// SynthyBoy - core/spectral: the spectral oscillator DSP (Serum 2 manual p.104 - 122, docs/ui-design/13-new-keys.md 4.38).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// STFT resynthesis of a sample. `Analysis` (built off the audio thread, immutable afterwards) holds the magnitude, the phase and the instantaneous frequency of
// every bin of every frame (FFT 2048, hop 512). A `SpectralVoice` reads it at a play head that moves at the scan speed, changes the spectrum (spectral filter, low /
// high band, spectral warps, pitch shift), keeps the phases of the partials coherent (phase vocoder with optional phase locking and transient reset) and
// overlap-adds the inverse transform. Real time safe: no allocation, no locks after construction. Standalone: no dependency on the engine.
#pragma once

#include <cstdint>
#include <vector>

#include "synthyboy/sampleosc/sample_types.h"

namespace synthyboy
{
namespace spectral
{
using sampleosc::LoopMode;
using sampleosc::SampleView;

constexpr int kFftSize = 2048;
constexpr int kHop = 512;
constexpr int kBins = kFftSize / 2 + 1;
constexpr int kMaxAnalysisFrames = 1536;     // per channel: about 16.4 s at 48 kHz are analysed, a longer file is cut (Analysis::truncated)
constexpr int kMaxCurvePoints = 64;

/** The spectral warp modes (the entries 76 .. 98 of the warp lists, 13-new-keys.md 4.38). Index 0 is the entry kFirstSpectralWarp (76) of the lists. */
enum class Warp : int
{
    comb, addHarmonics, addSubharmonics, detune, shepardNarrow, shepardFilter, peakOctaveDown, peakOctaveUp, peakHarmonicDown, peakHarmonicUp, gate,
    pitchShift, phaseTwist, spread, spectralShift, smear, mirror, maskFirst, maskSecond, maskNoise, vocodeFirst, vocodeSecond, vocodeNoise
};
constexpr int kNumSpectralWarps = 23;
inline bool isExternalWarp (int w) noexcept { return w >= static_cast<int> (Warp::maskFirst); }
/** For an external warp (mask / vocode): 0 first other oscillator, 1 second other oscillator, 2 noise. */
inline int externalSource (int w) noexcept { return (w - static_cast<int> (Warp::maskFirst)) % 3; }

/** FFT of kFftSize points (in place, split real / imaginary arrays); the tables are built once. */
class Fft
{
public:
    static const Fft& instance() noexcept;
    void forward (float* re, float* im) const noexcept;
    /** Inverse, scaled by 1 / kFftSize. */
    void inverse (float* re, float* im) const noexcept;

private:
    Fft();
    void run (float* re, float* im, bool inverse) const noexcept;
    float cosTable_[kFftSize / 2], sinTable_[kFftSize / 2];
    uint16_t bitReverse_[kFftSize];
};

/** The analysis of one sample, built on a non real-time thread. */
class Analysis
{
public:
    /** Analyses `sample` (kFftSize / kHop). Returns false for an invalid sample. */
    bool build (const SampleView& sample);
    bool valid() const noexcept { return frames_ > 0; }
    int numFrames() const noexcept { return frames_; }
    int channels() const noexcept { return channels_; }
    double fileRate() const noexcept { return fileRate_; }
    bool truncated() const noexcept { return truncated_; }
    int64_t fileFrames() const noexcept { return fileFrames_; }
    const float* magnitude (int ch, int frame) const noexcept { return data_.data() + offset (ch, frame); }
    const float* phase (int ch, int frame) const noexcept { return data_.data() + offset (ch, frame) + kBins; }
    /** Instantaneous frequency of every bin in radians per file sample. */
    const float* frequency (int ch, int frame) const noexcept { return data_.data() + offset (ch, frame) + 2 * kBins; }
    bool transient (int frame) const noexcept { return frame >= 0 && frame < frames_ && transient_[static_cast<size_t> (frame)] != 0; }
    /** The magnitude of every frame summed over the bins, for the display (0 .. 1 after normalisation). */
    std::vector<float> frameEnergies() const;

private:
    size_t offset (int ch, int frame) const noexcept { return (static_cast<size_t> (ch) * static_cast<size_t> (frames_) + static_cast<size_t> (frame)) * 3u * kBins; }
    std::vector<float> data_;
    std::vector<uint8_t> transient_;
    int frames_ = 0, channels_ = 0;
    int64_t fileFrames_ = 0;
    double fileRate_ = 44100.0;
    bool truncated_ = false;
};

/** The work arrays of one frame: they are not state, so the voices of an oscillator share one (the oscillator owns it; about 40 KB). */
struct Scratch
{
    float re[kFftSize], im[kFftSize];
    float mag[kBins], freq[kBins], theta[kBins];
    float om[kBins], of[kBins], oth[kBins];
    float tm[kBins], tf[kBins];
};

/** The per block settings of a voice. */
struct SpectralParams
{
    Scratch* scratch = nullptr;               // required
    double hostRate = 48000.0;
    float regionLo = 0.0f, regionHi = 1.0f;   // fractions of the file
    LoopMode loopMode = LoopMode::none;
    float loopLo = 0.0f, loopHi = 1.0f;
    bool manual = false;
    float position = 0.0f;                    // 0 .. 1 of the region (manual)
    float scanSpeed = 1.0f;                   // seconds of the file per second, negative reverses, 0 freezes
    float pitchRatio = 1.0f;                  // of the note (and the unison detune)
    float freqLoHz = 20.0f, freqHiHz = 20000.0f;
    bool bandSmooth = true;                   // fourth order edges (Butterworth) instead of a brick wall
    bool bandPostWarp = false;                // the band is applied after the warps
    bool phaseLock = false;
    bool transients = false;
    const float* filterGain = nullptr;        // kBins linear gains of the spectral filter (already mixed wet / dry), null = flat
    float freqShiftHz = 0.0f;
    int warpType[2] = { -1, -1 };             // index into Warp, < 0: none
    float warpAmount[2] = { 0.0f, 0.0f };     // 0 .. 1
    float warpVar[2] = { 0.5f, 0.5f };        // 0 .. 1 (the VAR fader)
    const float* external[3] = { nullptr, nullptr, nullptr }; // kBins magnitudes of the first / second other oscillator and the noise, for the mask and vocode warps
    float gain = 1.0f;
};

/** Builds the kBins gains of the spectral filter: `points` (x = position on the log frequency axis 0 .. 1 between 20 Hz and 20 kHz, y = 0 .. 1 with 0.5 = unity, 2 y is the
    gain) shifted along the axis by `shift` (-1 .. 1 = +- half the axis), mixed wet / dry. `binHz` = Hz of one bin. No points: flat. */
void buildFilterGains (float* gains, const float* x, const float* y, int numPoints, float shift, float mix, double binHz) noexcept;

class SpectralVoice
{
public:
    SpectralVoice() = default;
    void prepare (double hostSampleRate) noexcept;
    void reset() noexcept;

    /** Starts the voice on `analysis` (null: silent). `startFraction` moves the play head by that share of the region (unison start / span). The overlap buffer is primed so
        that the note starts at full level. */
    void noteOn (const Analysis* analysis, const SpectralParams& p, float startFraction) noexcept;
    void releaseLoop() noexcept { released_ = true; }
    /** Renders numFrames samples into left / right (overwritten). */
    void process (const SpectralParams& p, float* left, float* right, int numFrames) noexcept;

    bool isActive() const noexcept { return active_; }
    double playhead() const noexcept { return head_; }   // fraction of the file

private:
    void synthesizeHop (const SpectralParams& p, bool advance) noexcept;
    void advanceHead (const SpectralParams& p) noexcept;
    void renderChannel (const SpectralParams& p, int ch, double frame, bool transientHit) noexcept;

    const Analysis* analysis_ = nullptr;
    double hostRate_ = 48000.0;
    bool active_ = false, released_ = false, headDone_ = false;
    double head_ = 0.0;
    int headDirection_ = 1;
    int outPos_ = kHop;          // next unread sample of the finished hop
    int ola_ = 0;                // write position of the overlap buffer (start of the hop to write)
    bool primed_ = false;
    int lastTransientFrame_ = -1, drain_ = 0;
    bool phaseInit_[2] = { false, false };

    // state per channel (the phases of the output partials, the overlap-add buffer, the smear memory)
    struct Channel
    {
        float synthPhase[kBins] = {};
        float smear[kBins] = {};
        float ring[kFftSize] = {};
        float finished[kHop] = {};
    } channel_[2];

};
} // namespace spectral
} // namespace synthyboy
