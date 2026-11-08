// SynthyBoy - wavetable-edit: non-UI data model, frame operations, FFT, import and export.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace synthyboy::wtedit
{
/// Standard samples per wavetable frame.
constexpr size_t kWaveformSize = 2048;

/// Maximum number of frames supported in a wavetable.
constexpr size_t kMaxFrames = 256;

/// Nyquist harmonic count (bins 1..1024, with bin 0 being DC).
constexpr size_t kNumFftBins = 1024;

//==============================================================================
/// A single single-cycle waveform frame of 2048 floating-point samples.
struct Frame
{
    std::array<float, kWaveformSize> data {};

    Frame() noexcept { data.fill(0.0f); }

    float& operator[] (size_t index) noexcept { return data[index]; }
    const float& operator[] (size_t index) const noexcept { return data[index]; }

    float* sampleData() noexcept { return data.data(); }
    const float* sampleData() const noexcept { return data.data(); }
};

//==============================================================================
/// Metadata and frame collection of a wavetable.
class Wavetable
{
public:
    Wavetable();
    explicit Wavetable(const std::string& name);

    /// Name of the wavetable.
    const std::string& name() const noexcept { return name_; }
    void setName(const std::string& name) { name_ = name; }

    /// Author metadata.
    const std::string& author() const noexcept { return author_; }
    void setAuthor(const std::string& author) { author_ = author; }

    /// Free-form comments or description.
    const std::string& comment() const noexcept { return comment_; }
    void setComment(const std::string& comment) { comment_ = comment; }

    /// Native sample rate associated with the wavetable (default 44100).
    double sampleRate() const noexcept { return sampleRate_; }
    void setSampleRate(double sr) noexcept { sampleRate_ = sr; }

    /// Current number of frames (1..256).
    size_t numFrames() const noexcept { return frames_.size(); }

    /// Access frame by 0-based index.
    const Frame& getFrame(size_t index) const;
    Frame& getFrame(size_t index);

    /// Replace frame at index. Returns false if index is out of range.
    bool setFrame(size_t index, const Frame& frame);

    /// Insert a new frame at index (0..numFrames). Returns false if table is at capacity (256).
    bool insertFrame(size_t index, const Frame& frame);

    /// Duplicate frame at index into index + 1. Returns false if table is at capacity or index invalid.
    bool duplicateFrame(size_t index);

    /// Remove frame at index. Minimum 1 frame is always preserved. Returns false on failure.
    bool removeFrame(size_t index);

    /// Relocate a frame from fromIndex to toIndex.
    bool moveFrame(size_t fromIndex, size_t toIndex);

    /// Clear all frames and reset to a single default sawtooth frame.
    void initDefault();

    /// Clear table to a single silent frame.
    void clear();

    /// Reserve space for frames.
    void reserve(size_t count) { frames_.reserve(count); }

    /// Direct frame vector access.
    const std::vector<Frame>& frames() const noexcept { return frames_; }
    std::vector<Frame>& frames() noexcept { return frames_; }

private:
    std::string name_ { "Default" };
    std::string author_ {};
    std::string comment_ {};
    double sampleRate_ { 44100.0 };
    std::vector<Frame> frames_;
};

//==============================================================================
/// Radix-2 Cooley-Tukey Fast Fourier Transform (in-place and out-of-place).
class Radix2Fft
{
public:
    /// Returns true if N is a non-zero power of 2.
    static constexpr bool isPowerOfTwo(size_t n) noexcept
    {
        return n > 0 && ((n & (n - 1)) == 0);
    }

    /// Forward FFT: converts complex time-domain (real, imag) into frequency spectrum.
    static void forward(float* real, float* imag, size_t size) noexcept;

    /// Inverse FFT: converts frequency spectrum back to time domain, dividing by size.
    static void inverse(float* real, float* imag, size_t size) noexcept;

    /// Transforms 2048 real samples into magnitudes [0..1024] and phases [0..1024].
    static void realToPolar(const float* timeSamples, float* magnitudes, float* phases, size_t size = kWaveformSize) noexcept;

    /// Synthesizes 2048 real samples from magnitudes [0..1024] and phases [0..1024].
    static void polarToReal(const float* magnitudes, const float* phases, float* timeSamples, size_t size = kWaveformSize) noexcept;
};

//==============================================================================
/// Waveform drawing tools operating across horizontal grid divisions (1..256).
enum class DrawingTool
{
    FlatLine,           ///< Flat horizontal line at current value.
    Sine,               ///< Sine wave cycle across the grid step.
    SlopeUp,            ///< Linear ramp upwards across the step.
    SlopeDown,          ///< Linear ramp downwards across the step.
    CurveUp,            ///< Quarter-sine curve rising.
    CurveDown,          ///< Quarter-sine curve falling.
    HalfSinePeak,       ///< Half-sine arch (peak).
    HalfSineValley,     ///< Inverted half-sine arch (valley).
    InterpolateLinear,  ///< Connects grid segment boundary points with a straight line.
    InterpolateCurved,  ///< Connects grid segment boundary points with an S-curve (smoothstep).
    Nudge,              ///< Shifts grid step samples vertically with clipping.
    Noise,              ///< Adds random noise perturbation to the step.
    Mirror              ///< Mirrors the frame symmetrically across the center.
};

/// Applies drawing tool to a frame on a grid with `gridSize` divisions (1..256).
void applyDrawingTool(Frame& frame, DrawingTool tool, int gridSize, int stepIndex, float param = 0.0f) noexcept;

//==============================================================================
/// Morph / frame interpolation modes.
enum class MorphMode
{
    Crossfade,                ///< Linear time-domain crossfade between neighboring keyframes.
    Spectral,                 ///< Interpolates FFT harmonic magnitudes and unwrapped phases.
    SpectralZeroFundPhase,    ///< Spectral interpolation with the fundamental phase forced to 0.
    SpectralZeroAllPhases     ///< Spectral interpolation with all harmonic phases forced to 0.
};

//==============================================================================
/// Spectral sorting criteria for reordering frames in a wavetable.
enum class SortMode
{
    PeakSpect,          ///< Sort by frequency index of the highest spectral peak bin.
    AverageSpect,       ///< Sort by spectral centroid (energy-weighted average frequency).
    PeakAmount,         ///< Sort by overall maximum spectral peak magnitude.
    NumWithSpect,       ///< Sort by count of harmonic bins with energy above threshold.
    HighestWithSpect,   ///< Sort by highest harmonic bin containing energy.
    FundamentalAmount,  ///< Sort by amplitude of the fundamental frequency bin.
    Randomize,          ///< Shuffle frame order randomly.
    Reverse             ///< Invert the order of all frames.
};

//==============================================================================
/// Non-destructive and destructive frame operations matching docs/ui-design/03-wavetable.md.
namespace ops
{
    /// Normalises the single frame to maximum absolute peak of 1.0.
    void normalise(Frame& frame) noexcept;

    /// Individually normalises each frame in the wavetable to its own peak of 1.0.
    void normaliseEach(Wavetable& wt) noexcept;

    /// Scans the entire wavetable for global peak and applies the same gain to all frames.
    void normaliseSame(Wavetable& wt) noexcept;

    /// Removes DC bias by subtracting the mean sample value from the frame.
    void removeDc(Frame& frame) noexcept;

    /// Removes DC bias from all frames in the wavetable.
    void removeDcAll(Wavetable& wt) noexcept;

    /// Inverts waveform polarity (y = -y) for a single frame.
    void flipVertical(Frame& frame) noexcept;

    /// Inverts waveform polarity for all frames in the wavetable.
    void flipVerticalAll(Wavetable& wt) noexcept;

    /// Reverses sample playback order (horizontal flip) for a single frame.
    void flipHorizontal(Frame& frame) noexcept;

    /// Reverses sample playback order for all frames in the wavetable.
    void flipHorizontalAll(Wavetable& wt) noexcept;

    /// Fades left edge in and right edge out over windowSize = max(1, 2048 / gridSize) samples.
    void fadeEdges(Frame& frame, int gridSize) noexcept;

    /// Fades left and right edges for all frames in the wavetable based on grid size.
    void fadeEdgesAll(Wavetable& wt, int gridSize) noexcept;

    /// Crossfades start and end edges together over windowSize = max(1, 2048 / gridSize) samples.
    void crossfadeEdges(Frame& frame, int gridSize) noexcept;

    /// Crossfades start and end edges together for all frames in the wavetable based on grid size.
    void crossfadeEdgesAll(Wavetable& wt, int gridSize) noexcept;

    /// Crossfades start and end edges over a micro-window of 16 samples.
    void crossfadeEdges16Samples(Frame& frame) noexcept;

    /// Crossfades start and end edges over 16 samples for all frames in the wavetable.
    void crossfadeEdges16SamplesAll(Wavetable& wt) noexcept;

    /// Circularly shifts samples so the frame begins on a positive-going zero crossing.
    void shiftToZeroCrossing(Frame& frame) noexcept;

    /// Circularly shifts each frame in the wavetable to begin on a positive zero crossing.
    void shiftToZeroCrossingAll(Wavetable& wt) noexcept;

    /// Clears the single frame to complete silence.
    void initSilence(Frame& frame) noexcept;

    /// Resets the wavetable to its default state (single saw frame).
    void initAllDefault(Wavetable& wt) noexcept;

    /// Inserts a new frame at the specified index.
    bool insertFrame(Wavetable& wt, size_t index, const Frame& frame);

    /// Removes the frame at index.
    bool removeFrame(Wavetable& wt, size_t index);

    /// Removes a range of frames from startIndex to endIndex (inclusive).
    bool removeRange(Wavetable& wt, size_t startIndex, size_t endIndex);

    /// Removes frames from index 0 up to selectedIndex (inclusive).
    bool removeBeginningToSelected(Wavetable& wt, size_t selectedIndex);

    /// Removes frames from selectedIndex through to the end of the wavetable.
    bool removeSelectedToEnd(Wavetable& wt, size_t selectedIndex);

    /// Crops wavetable to only the selected range [startIndex, endIndex].
    bool removeAllExceptSelected(Wavetable& wt, size_t startIndex, size_t endIndex);

    /// Thins the wavetable by retaining only every nth frame (step >= 1).
    void reduceTo(Wavetable& wt, size_t step);

    /// Splits every frame in half to become two frames, doubling the frame count (up to 256).
    bool resizeTablesHalf(Wavetable& wt);

    /// Groups pairs of adjacent frames into single frames, halving the total frame count.
    bool resizeTablesDouble(Wavetable& wt);

    /// Creates a pulse-width modulated sweep across 256 frames from the selected frame.
    void createPwmFromFrame(Wavetable& wt, size_t frameIndex);

    /// Reduces effective sample rate for a lo-fi sound based on grid size.
    void sampleRedux(Frame& frame, int gridSize) noexcept;

    /// Applies sample reduction to all frames in the wavetable.
    void sampleReduxAll(Wavetable& wt, int gridSize) noexcept;

    /// Low-pass filter removing upper harmonics via FFT based on grid size.
    void filterLowPass(Frame& frame, int gridSize) noexcept;

    /// Applies FFT low-pass filter to all frames in the wavetable based on grid size.
    void filterLowPassAll(Wavetable& wt, int gridSize) noexcept;

    /// Zeros the fundamental frequency bin (harmonic 1), high-passing the frame.
    void removeFundamental(Frame& frame) noexcept;

    /// Zeros the fundamental frequency bin for all frames in the wavetable.
    void removeFundamentalAll(Wavetable& wt) noexcept;

    /// Zeros the lowest N frequency bins based on grid size.
    void removeLowSpectra(Frame& frame, int gridSize) noexcept;

    /// Zeros the lowest N frequency bins for all frames in the wavetable.
    void removeLowSpectraAll(Wavetable& wt, int gridSize) noexcept;

    /// Sets harmonic phases of the lowest N frequency bins to zero.
    void removeLowPhases(Frame& frame, int gridSize) noexcept;

    /// Sets harmonic phases of the lowest N frequency bins to zero for all frames.
    void removeLowPhasesAll(Wavetable& wt, int gridSize) noexcept;

    /// Rotates the phase of the fundamental harmonic to 50% (pi radians).
    void nudgeFundamentalPhaseTo50Percent(Frame& frame) noexcept;

    /// Rotates the phase of the fundamental to 50% for all frames in the wavetable.
    void nudgeFundamentalPhaseTo50PercentAll(Wavetable& wt) noexcept;

    /// Copies FFT harmonic magnitudes from frameIndex to all frames in the wavetable.
    void setSpectraThisFrameToAll(Wavetable& wt, size_t frameIndex) noexcept;

    /// Copies FFT harmonic phases from frameIndex to all frames in the wavetable.
    void setPhasesThisFrameToAll(Wavetable& wt, size_t frameIndex) noexcept;

    /// Sets FFT harmonic magnitudes of all frames from corresponding frames of another wavetable.
    void setSpectraFromOther(Wavetable& target, const Wavetable& source) noexcept;

    /// Sets FFT harmonic phases of all frames from corresponding frames of another wavetable.
    void setPhasesFromOther(Wavetable& target, const Wavetable& source) noexcept;

    /// Subtracts FFT harmonic magnitudes of another wavetable from target (clamping to >= 0).
    void subtractSpectraFromOther(Wavetable& target, const Wavetable& source) noexcept;

    /// Blurs harmonic magnitudes across neighboring bins with a window defined by gridSize.
    void blurSpectraAdjacentBins(Frame& frame, int gridSize) noexcept;

    /// Blurs harmonic magnitudes across neighboring bins for all frames in the wavetable.
    void blurSpectraAdjacentBinsAll(Wavetable& wt, int gridSize) noexcept;

    /// Blurs harmonic phases across neighboring bins with a window defined by gridSize.
    void blurPhasesAdjacentBins(Frame& frame, int gridSize) noexcept;

    /// Blurs harmonic phases across neighboring bins for all frames in the wavetable.
    void blurPhasesAdjacentBinsAll(Wavetable& wt, int gridSize) noexcept;

    /// Blurs harmonic magnitudes for each bin across adjacent frames in time.
    void blurSpectraAdjacentFrames(Wavetable& wt, int gridSize) noexcept;

    /// Blurs harmonic phases for each bin across adjacent frames in time.
    void blurPhasesAdjacentFrames(Wavetable& wt, int gridSize) noexcept;

    /// Smoothes waveform in the time domain using a moving-average filter of radius.
    void smoothTimeDomain(Frame& frame, int radius) noexcept;

    /// Warps time axis towards or away from the center (bend transformation).
    void bendTimeDomain(Frame& frame, float amount) noexcept;

    /// Frequency modulates carrier frame using modulator frame with modulation depth.
    void fmBetweenFrames(Frame& carrier, const Frame& modulator, float depth) noexcept;

    /// Amplitude modulates carrier frame using modulator frame with modulation depth.
    void amBetweenFrames(Frame& carrier, const Frame& modulator, float depth) noexcept;

    /// Interpolates intermediate frames between keyframe indices using the specified morph mode.
    void interpolateBetweenKeyframes(Wavetable& wt, const std::vector<size_t>& keyframeIndices, MorphMode mode);

    /// Strips non-keyframe morph frames, preserving only the given keyframe indices.
    void removeMorphTables(Wavetable& wt, const std::vector<size_t>& keyframeIndices);

    /// Reorders frames in the wavetable according to the selected spectral sort mode.
    void sortFrames(Wavetable& wt, SortMode mode);

    //--- FFT Area Menu Operations ---

    /// Zeros all harmonic bins (yielding silence).
    void fftClearAll(Frame& frame) noexcept;

    /// Zeros all harmonic bins from clickedBin to Nyquist (1024).
    void fftClearHf(Frame& frame, size_t clickedBin) noexcept;

    /// Zeros all harmonic bins from fundamental (1) to clickedBin.
    void fftClearLf(Frame& frame, size_t clickedBin) noexcept;

    /// Sets harmonic bin amplitudes to 1.0 / k producing a standard saw spectrum.
    void fftGenerateSaw(Frame& frame) noexcept;

    /// Assigns random amplitudes in [0, 1] to bins 1..maxBin (default 16).
    void fftRandomizeLowBins(Frame& frame, size_t maxBin = 16, bool allowHalfOnly = false) noexcept;

    /// Assigns random amplitudes in [0, 1] to all bins 1..1024.
    void fftRandomizeAll(Frame& frame) noexcept;

    /// Randomly removes harmonics by zeroing bins with gapProbability in [0, 1].
    void fftCreateRandomSeriesGaps(Frame& frame, float gapProbability = 0.3f) noexcept;

    /// Progressively attenuates higher harmonics with a smooth roll-off curve.
    void fftProgressiveFade(Frame& frame, float fadePower = 1.5f) noexcept;

    /// Shifts all harmonic bins up one octave (bin k -> bin 2k).
    void fftShiftOctaveUp(Frame& frame) noexcept;

    /// Shifts all harmonic bins down one octave (bin 2k -> bin k).
    void fftShiftOctaveDown(Frame& frame) noexcept;

    /// Repeats harmonic bins 1..cursorBin periodically across the upper spectrum.
    void fftRepeatBinGroup(Frame& frame, size_t cursorBin) noexcept;

    /// Scales harmonic amplitudes proportionally by bin index k to emphasize highs.
    void fftScaleFreqValuesByBinIndex(Frame& frame) noexcept;

    /// Modifies a single harmonic bin's amplitude and phase.
    void fftSetHarmonic(Frame& frame, size_t binIndex, float amplitude, float phase) noexcept;
} // namespace ops

//==============================================================================
/// Vital Group / Component / Keyframe non-UI data model.
namespace vital
{
    enum class InterpolationStyle
    {
        None,
        Linear,
        Cubic,
        Spectral
    };

    enum class ComponentType
    {
        WaveSource,
        LineSource,
        FileSource,
        PhaseModifier,
        WaveWindowModifier,
        FrequencyFilterModifier,
        SlewLimitModifier,
        WaveFoldModifier,
        WaveWarpModifier
    };

    /// Base keyframe on the component timeline (positions 0..255).
    struct Keyframe
    {
        float position = 0.0f;
        Frame waveform {};

        // Modifier parameters:
        float param1 = 0.0f;
        float param2 = 0.0f;
        int style = 0;
        bool flag = false;

        Keyframe() = default;
        explicit Keyframe(float pos) noexcept : position(pos) {}
    };

    /// A wavetable component (either a signal source or a modifier).
    class Component
    {
    public:
        explicit Component(ComponentType type, const std::string& name = "");
        virtual ~Component() = default;

        ComponentType type() const noexcept { return type_; }
        const std::string& name() const noexcept { return name_; }
        void setName(const std::string& n) { name_ = n; }

        bool isEnabled() const noexcept { return enabled_; }
        void setEnabled(bool en) noexcept { enabled_ = en; }

        InterpolationStyle interpolationStyle() const noexcept { return interpStyle_; }
        void setInterpolationStyle(InterpolationStyle style) noexcept { interpStyle_ = style; }

        size_t numKeyframes() const noexcept { return keyframes_.size(); }
        const Keyframe& getKeyframe(size_t index) const { return keyframes_[index]; }
        Keyframe& getKeyframe(size_t index) { return keyframes_[index]; }

        void insertKeyframe(const Keyframe& kf);
        void removeKeyframe(size_t index);
        void clearKeyframes() { keyframes_.clear(); }

        /// Renders component output at frame position (0.0 .. 255.0).
        void process(float position, Frame& frame) const noexcept;

    private:
        ComponentType type_;
        std::string name_;
        bool enabled_ { true };
        InterpolationStyle interpStyle_ { InterpolationStyle::Linear };
        std::vector<Keyframe> keyframes_;
    };

    /// A wavetable group holding an ordered chain of sources and modifiers.
    class Group
    {
    public:
        Group();

        size_t numComponents() const noexcept { return components_.size(); }
        Component* getComponent(size_t index) { return components_[index].get(); }
        const Component* getComponent(size_t index) const { return components_[index].get(); }

        void addComponent(std::unique_ptr<Component> component);
        void removeComponent(size_t index);
        void clearComponents() { components_.clear(); }

        /// Renders a single frame at position (0.0 .. 255.0) through the component chain.
        void renderFrame(float position, Frame& outFrame) const noexcept;

        /// Renders the complete group into a Wavetable of numFrames frames.
        void renderWavetable(Wavetable& outWt, size_t numFrames = 256) const;
        
        /// Synchronizes group from a Wavetable (creates WaveSource keyframes from frames).
        void initFromWavetable(const Wavetable& wt);

    private:
        std::vector<std::unique_ptr<Component>> components_;
    };
} // namespace vital

//==============================================================================
/// Bounded undo/redo command stack preserving bit-exact wavetable states.
class UndoStack
{
public:
    explicit UndoStack(size_t maxDepth = 50);

    /// Records current state for undo. Clears redo history. Bounded to maxDepth.
    void push(const Wavetable& wt);

    /// True if an undo step is available.
    bool canUndo() const noexcept;

    /// True if a redo step is available.
    bool canRedo() const noexcept;

    /// Restores previous state bit-exactly into wt.
    bool undo(Wavetable& wt);

    /// Restores next state bit-exactly into wt.
    bool redo(Wavetable& wt);

    /// Clears undo and redo histories.
    void clear() noexcept;

    /// Returns max history depth.
    size_t maxDepth() const noexcept { return maxDepth_; }

private:
    size_t maxDepth_;
    std::vector<Wavetable> history_;
    int currentIndex_ { -1 };
};

//==============================================================================
/// Audio import modes matching Serum manual (pages 291-294) and Vital.
enum class ImportMode
{
    FixedSingleCycle,       ///< Slices first cycle or 2048 samples as a single frame.
    ByFrameSize,            ///< Slices by user-specified frame size (32..9999 samples).
    PitchDetectZeroSnap,    ///< Detects pitch and snaps cycle boundaries to zero-crossings.
    PitchDetectFollow,      ///< Tracks dynamic pitch variations cycle-by-cycle.
    PitchAverage,           ///< Slices at the average fundamental period detected across the file.
    Fft256,                 ///< Spectral FFT import with window size 256.
    Fft512,                 ///< Spectral FFT import with window size 512.
    Fft1024,                ///< Spectral FFT import with window size 1024.
    Fft2048                 ///< Spectral FFT import with window size 2048.
};

struct ImportOptions
{
    ImportMode mode { ImportMode::PitchAverage };
    size_t frameSize { 2048 };      ///< Used when mode == ByFrameSize.
    int channel { 0 };              ///< 0 = Left/Mono (Osc A/C), 1 = Right (Osc B).
    size_t maxFrames { 256 };        ///< Maximum frames to import (1..256).
    bool fadeEdges { false };       ///< Apply edge fade after import.
    bool normalize { false };       ///< Normalize frames after import.
};

/// Decodes and imports audio from in-memory WAV data (16, 24, 32-bit int, 32-bit float).
bool importAudioWav(const uint8_t* wavData, size_t dataSize, const ImportOptions& options, Wavetable& outWt, std::string* errorMsg = nullptr);

/// Imports from raw floating-point audio sample buffer.
bool importAudioSamples(const float* samples, size_t numSamples, double sampleRate, const ImportOptions& options, Wavetable& outWt);

/// Resamples buffer of arbitrary inLength to 2048 samples using cubic interpolation.
void resampleBuffer(const float* in, size_t inLength, float* out, size_t outLength = kWaveformSize) noexcept;

//==============================================================================
/// Image import: raw RGBA or Grayscale pixel buffer -> additive spectrum (Serum manual p. 300-301).
/// Width determines frame count (<= 256); height maps to harmonic bins; luminance maps to amplitude.
bool importImageRgba(const uint8_t* rgba, int width, int height, Wavetable& outWt);
bool importImageGrayscale(const uint8_t* grayscale, int width, int height, Wavetable& outWt);

//==============================================================================
/// Wavetable audio export formats.
enum class ExportFormat
{
    Wav16Bit,       ///< 16-bit PCM WAV, 44.1 kHz, 2048 samples per frame.
    Wav32BitFloat,  ///< 32-bit IEEE Float WAV, 44.1 kHz, 2048 samples per frame.
    SingleCycle     ///< Single-cycle 2048-sample WAV of a specific frame.
};

struct ExportOptions
{
    ExportFormat format { ExportFormat::Wav32BitFloat };
    size_t singleFrameIndex { 0 };      ///< Frame index when format == SingleCycle.
    uint32_t sampleRate { 44100 };
    bool writeSerumClmChunk { true };   ///< Writes Serum-compatible 'clm ' RIFF chunk with "<!>2048".
};

/// Encodes wavetable into WAV byte buffer.
bool exportWav(const Wavetable& wt, const ExportOptions& options, std::vector<uint8_t>& outBytes);

//==============================================================================
/// Real-time safe wavetable oscillator player / processor.
/// Real-time safe: no allocations/locks in process(); prepare() allocates.
struct PlayerParams
{
    float framePosition { 0.0f };   ///< 0.0 .. (numFrames - 1), smooth morphing across frames.
    float frequency { 440.0f };      ///< Frequency in Hz (20 .. 20000).
    float phaseOffset { 0.0f };      ///< Start phase offset in [0, 1].
    float gain { 1.0f };             ///< Linear amplitude gain.
};

class WavetablePlayer
{
public:
    WavetablePlayer() = default;

    /// Configures audio sample rate (22050..192000) and maximum block size (1..4096). May allocate.
    void prepare(double sampleRate, int maxBlockFrames);

    /// Snap smoothers and reset phase. Real-time safe.
    void reset() noexcept;

    /// Sets the active wavetable for playback. Real-time safe (pointer swap).
    void setWavetable(const Wavetable* wt) noexcept;

    /// Renders numSamples of audio into output buffer.
    /// Real-time safe: no allocation, no locks, denormal-safe, NaN/inf-safe.
    void process(const PlayerParams& params, float* output, int numSamples) noexcept;

private:
    double sampleRate_ { 44100.0 };
    double phase_ { 0.0 };
    const Wavetable* currentWavetable_ { nullptr };
};

} // namespace synthyboy::wtedit
