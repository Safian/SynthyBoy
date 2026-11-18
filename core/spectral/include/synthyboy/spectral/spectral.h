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
/** The start offsets of a voice inside its hop (SpectralVoice::noteOn): voices started together take different ones, so that their frames fall into different blocks. */
constexpr int kStartSteps = 4;
constexpr int kStartStep = kHop / kStartSteps;   // 128 samples: one block of the engine

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
    // the block kernels of a frame (SpectralVoice::renderChannelFast): the angles (with a phase twist), the half length transform's output
    float angle[kBins];
    float phaseTmp[kBins];       // the phases of one start frame (SpectralVoice::startFrames)
    float olaTmp[kFftSize];      // one start frame before its part that lies in the past is dropped
    float zr[kFftSize / 2], zi[kFftSize / 2];
    // the gains of the band limit (shapeFrame): they depend on the settings only, so they are kept until the settings change
    float bandGain[kBins];
    float combGain[kBins];
    float combA = -1.0f, combV = -1.0f;
    float bandLo = -1.0f, bandHi = -1.0f;
    double bandBinHz = -1.0;
    bool bandSmooth = false;
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

/** Computes the output bin magnitudes of one frame without running a voice (for display).
    `frame`: frame index in the analysis (0 .. an.numFrames() - 1).
    `om`: output array of kBins floats.
    `scratch`: scratch workspace. */
void computeMagnitudeSpectrum (const Analysis& an, const SpectralParams& p, double frame, float* om, Scratch& scratch) noexcept;

class SpectralVoice
{
public:
    SpectralVoice() = default;
    void prepare (double hostSampleRate) noexcept;
    /** Stops the voice. Only the flags: the buffers (phases, smear, ring, finished hop) are not cleared, a note start (noteOn) clears or writes everything it reads. */
    void reset() noexcept;
    /** Tests only: reset() as it was before (every buffer of both channels cleared), and a fill of all buffers with a pattern to start notes on dirty memory. */
    void setLegacyReset (bool on) noexcept { legacyReset_ = on; }
    /** Tests only: the priming frames go through the 8 KB temporary and a scalar add loop (as before SP6). */
    void setLegacyStartOla (bool on) noexcept { legacyStartOla_ = on; }
    /** Tests only: with Phase Lock every bin is advanced and then the lock overwrites about half of them (as before SP5), instead of advancing only the bins the lock keeps. */
    void setLegacyPhaseLock (bool on) noexcept { legacyLock_ = on; }
    void scribbleBuffersForTest (float value) noexcept;
    /** Tests only: how many of the lazily built tables (transform, overlap-add window, start gains, start schedule) exist. prepare() builds all of them. */
    static int tablesBuiltForTest() noexcept;

    /** Starts the voice on `analysis` (null: silent). `startFraction` moves the play head by that share of the region (unison start / span). The overlap buffer is primed so
        that the note starts at full level: four frames are synthesised at once (the three before the first output hop and the first one of the hop itself).
        `hopOffset` (0 .. kHop - 1, a multiple of kStartStep in the engine): the output starts that many samples into the first hop, so the voice's next frame is due after kHop - hopOffset
        samples instead of kHop. Voices started in the same block with different offsets spread their frames over the four blocks of a hop instead of all landing in every fourth block.
        The note is the same rendering shifted by the offset: sample for sample the output of offset 0 from sample `hopOffset` on, also while it scans (every frame has the position, the bins
        and the phases it has at offset 0, only earlier in time). The play head is therefore ahead of the true time by `hopOffset` samples of the scan, less than one hop. */
    void noteOn (const Analysis* analysis, const SpectralParams& p, float startFraction, int hopOffset = 0) noexcept;
    void releaseLoop() noexcept { released_ = true; }
    /** Measurements only (the bench's --scalar-content): synthesise the frames with the reference code (the full length transform of the mirrored spectrum, std::sin / std::cos / std::fmod per
        bin, one product per sample of the overlap add) instead of the block kernels of kernels.h. The two agree to rounding (a few 1e-7 of the largest sample, tests/test_spectral.cpp). */
    void setReferenceKernels (bool on) noexcept { reference_ = on; }
    /** The sample of the first hop (the voice's output sample, 0 .. kHop) at which frame 1 .. 3 of the priming is made for a start offset: `join[j - 1] <= offset` means in noteOn. Every frame
        is made before the squared window it would have in the sum exceeds 0.5 (and at the latest at kHop, the first regular frame), at a multiple of kStartStep. With the Smear warp all at once. */
    static void startSchedule (int offset, bool smears, int join[3]) noexcept;
    /** Tests only: startSchedule as it was (the loop of double cosines at every call). */
    static void startScheduleReference (int offset, bool smears, int join[3]) noexcept;
    /** Measurements and tests only: the start of a note as before round 10 (the four frames of the priming made at once, the first hop their plain sum) instead of the start with the deferred frames
        (section 4a of docs/engine-spec/07). From the sample kHop of the voice (the first regular frame) on the two give the same samples. */
    void setLegacyStart (bool on) noexcept { legacyStart_ = on; }
    /** Renders numFrames samples into left / right (overwritten). */
    void process (const SpectralParams& p, float* left, float* right, int numFrames) noexcept;

    bool isActive() const noexcept { return active_; }
    double playhead() const noexcept { return head_; }   // fraction of the file

private:
    void synthesizeHop (const SpectralParams& p, bool advance) noexcept;
    void advanceHead (const SpectralParams& p) noexcept;
    void renderChannel (const SpectralParams& p, int ch, double frame, bool transientHit) noexcept;
    /** `reuseShape`: the shaped bins (scratch.om / of / oth) and `twist` are those of the previous call for this channel, nothing has touched them since (the priming of a note). */
    void renderChannelFast (const SpectralParams& p, int ch, double frame, bool transientHit, bool reuseShape = false, float* twistState = nullptr) noexcept;
    void primeFast (const SpectralParams& p) noexcept;
    // the start with deferred frames: frames 1 .. 3 of the priming (frame 0 is never made), the first of them in noteOn, the others when their weight in the first hop asks for them
    void startFrames (const SpectralParams& p, int mask, bool phantom) noexcept;
    void startPhases (const SpectralParams& p, int ch, int j, float* out) noexcept;
    void refreshFirstHop (int from) noexcept;
    void endFirstHop (const SpectralParams& p) noexcept;

    const Analysis* analysis_ = nullptr;
    double hostRate_ = 48000.0;
    bool reference_ = false;
    bool active_ = false, released_ = false, headDone_ = false;
    double head_ = 0.0;
    int headDirection_ = 1;
    int outPos_ = kHop;          // next unread sample of the finished hop
    int ola_ = 0;                // write position of the overlap buffer (start of the hop to write)
    bool primed_ = false;
    bool legacyStart_ = false;
    bool legacyReset_ = false;
    bool legacyStartOla_ = false;
    bool legacyLock_ = false;
    bool startActive_ = false;   // the first hop (the output samples 0 .. kHop - startSkip_) is being read from the ring, normalised by the windows of the frames made so far
    int startMask_ = 0;          // bit j - 1: frame j (1 .. 3) of the priming has been made
    int startJoin_[3] = { -1, -1, -1 };   // the sample of the first hop at which frame 1 .. 3 is made (-1: made)
    double startFrame_ = 0.0;    // the analysis frame (position) of the priming
    int phaseFrame_[2] = { 0, 0 };   // per channel: the frame whose phases synthPhase holds (0: none yet)
    bool framePrimed_ = false;   // the frame of the first output hop was synthesised by primeFast (the first synthesizeHop only reads the hop)
    int startSkip_ = 0;          // the output starts this many samples into the first hop (noteOn's hopOffset)
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
