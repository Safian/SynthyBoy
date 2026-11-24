// SynthyBoy - core: waveform cycle, sample overview, magnitude spectrum and display data of the own engine's oscillators (for the oscillator displays: the MPC overlay).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ---- Oscillator display (how a display uses this) ----
//
//   OscCycle cycle;                                            // once: allocates (an oscillator with its morph buffers, sample/spectral work arrays)
//   // or sibling class: OscDisplay display;
//   cycle.setContent (content, store);                         // when the state's content changes (NOT real time: decodes user wavetables, samples, SFZ, curves)
//   OscCycle::Request req;  req.osc = 1;
//   req.framePercent = live.target[k];                         // the modulated values of the readout (readoutIndices (osc, idx)); leave unset for the knob values
//   float min[512], max[512];
//   OscCycle::Result res = cycle.computeDisplay (values, req, min, max, 512); // any thread (one at a time), no allocation, no engine involved, deterministic
//
// Displays for each oscillator type:
// - wavetable: one full cycle as rendered by WtOsc (numPoints points in outMin; if outMax is provided, it receives identical points).
// - sample / multisample: min/max waveform overview envelope reduced to the requested numPoints columns. If outMax != nullptr,
//   outMin receives numPoints minimums and outMax receives numPoints maximums. If outMax == nullptr, outMin receives 2 * numPoints
//   interleaved values [min0, max0, min1, max1, ...]. Start, end, loopStart, loopEnd positions are normalised x in [0.0, 1.0].
//   For multisample, the zone is selected for `req.note` (default 60) with timbre mapping applied.
// - granular: the sample overview as above plus normalised grain window position and size [0.0, 1.0] in `res.grainPosition` and `res.grainSize`.
// - spectral: the magnitude spectrum produced by the STFT analysis (with filter, low/high bands, warps, pitch/frequency shift) as
//   numPoints dB values [-120 dB, +60 dB] along a log frequency axis (20 Hz .. 20 kHz) in outMin.
// - noise: res.isNoise is true, res.noiseColour and res.noiseType are set; buffers are zeroed so the UI can draw a neutral noise texture.
// - off / noSample: zeros are written to output buffers, kind is Kind::off or Kind::noSample.
//
// Thread safety & lifetime rules:
// - `setContent` decodes files off the audio thread.
// - `computeDisplay` / `compute` are real-time safe (no allocation, no locks, no I/O, deterministic) and can run on any thread.
// - The caller must guarantee that `setContent` is not called concurrently with `computeDisplay` on the same instance.
#pragma once

#include <memory>
#include <optional>

#include "state.h"

namespace synthyboy
{
struct StateContent;
namespace own { struct ContentSet; }
class IContentStore;

class OscCycle
{
public:
    static constexpr int kSamples = 512;

    enum class Kind
    {
        wavetable,      // Single cycle waveform
        other,          // Legacy/fallback/unsupported: no picture (zeros)
        off,            // Oscillator is off (zeros)
        sample,         // Sample overview min/max envelope
        multisample,    // Multisample zone overview min/max envelope
        granular,       // Granular overview min/max envelope + grain window
        spectral,       // Spectral magnitude spectrum (dB)
        noise,          // Noise generator: flag + noiseColour (zeros in audio buffers)
        noSample        // Sample/multisample/granular/spectral/noise-sample type with no sample loaded
    };

    struct Request
    {
        int osc = 1;                                       // 1 .. 3: osc 1..3, 4: noise source
        int note = 60;                                     // MIDI note for multisample zone selection (default 60)
        std::optional<float> framePercent;                 // oscN.frame, plain percent 0 .. 100 (e.g. the modulated value of the readout)
        std::optional<float> warpAmountPercent[2];         // oscN.warp.amount / oscN.warp2.amount
        std::optional<float> morphAmountPercent;           // oscN.morph.amount
        std::optional<double> bpm;                         // host tempo for synced granular parameters (default 120.0)
    };

    struct Result
    {
        Kind kind = Kind::off;

        // Normalised sample / zone / grain positions [0.0 .. 1.0] relative to the whole audio file
        float start = 0.0f;
        float end = 1.0f;
        float loopStart = 0.0f;
        float loopEnd = 1.0f;
        bool loopOn = false;

        // Granular grain window position and size [0.0 .. 1.0] relative to the whole audio file
        float grainPosition = 0.0f;
        float grainSize = 0.0f;

        // Noise information
        bool isNoise = false;
        int noiseColour = 0;        // 0 .. 8 (kNoiseSamples: White, Pink, Brown, Geiger, etc.)
        int noiseType = 0;          // 0 = Generator ("Noise"), 1 = Sample ("Sample")
    };

    OscCycle();
    ~OscCycle();
    OscCycle (const OscCycle&) = delete;
    OscCycle& operator= (const OscCycle&) = delete;

    /** Decodes the content (user wavetable, remap curves, samples, multisamples) with `store` (may be null). Not real time. */
    void setPreparedContent (std::shared_ptr<const own::ContentSet> prepared);
    void setContent (const StateContent& content, const IContentStore* store);

    /** Table indices (IEngine::setReadoutTargets order) of oscN.frame, oscN.warp.amount, oscN.warp2.amount, oscN.morph.amount; -1 for an oscillator other than 1 .. 3. */
    static void readoutIndices (int osc, int out[4]) noexcept;

    /** Legacy call: writes kSamples values (one cycle). For backwards compatibility with single-cycle displays.
        Returns Kind::wavetable for wavetable, Kind::off for off, Kind::other for anything else. */
    Kind compute (const ParamValues& values, const Request& request, float* out) noexcept;

    /**
     * Extended compute for all oscillator types (wavetable, sample, multisample, granular, spectral, noise).
     * numPoints: requested number of columns / points (>= 2).
     *
     * Data layout:
     * - wavetable: outMin receives numPoints values (-1 .. 1) of one cycle. If outMax != nullptr, outMax receives the same.
     * - sample / multisample / granular:
     *     If outMax != nullptr: outMin receives numPoints min envelope values, outMax receives numPoints max envelope values.
     *     If outMax == nullptr: outMin receives 2 * numPoints interleaved values (min0, max0, min1, max1, ...).
     * - spectral: outMin receives numPoints magnitude values in dB (-120 .. +60 dB, log frequency 20 Hz .. 20 kHz).
     *             If outMax != nullptr, outMax receives the same.
     * - noise: result.isNoise is true, noiseColour and noiseType are populated. outMin / outMax are zeroed.
     * - noSample / off: zeros are written to outMin / outMax.
     *
     * Returns Result containing the kind of data, sample/loop/grain markers, and noise info.
     * Deterministic, real-time safe (no allocation, no locks, no I/O).
     */
    Result computeDisplay (const ParamValues& values, const Request& request,
                           float* outMin, float* outMax = nullptr, int numPoints = kSamples) noexcept;

    /** Overload for compute with separate min/max arrays or numPoints. */
    Result compute (const ParamValues& values, const Request& request,
                    float* outMin, float* outMax, int numPoints = kSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** Sibling class for oscillator displays (same files, style matching LfoDisplay and EnvDisplay). */
class OscDisplay
{
public:
    using Kind = OscCycle::Kind;
    using Request = OscCycle::Request;
    using Result = OscCycle::Result;
    static constexpr int kSamples = OscCycle::kSamples;

    OscDisplay();
    ~OscDisplay();
    OscDisplay (const OscDisplay&) = delete;
    OscDisplay& operator= (const OscDisplay&) = delete;

    void setContent (const StateContent& content, const IContentStore* store);
    static void readoutIndices (int osc, int out[4]) noexcept;

    Result compute (const ParamValues& values, const Request& request,
                    float* outMin, float* outMax = nullptr, int numPoints = kSamples) noexcept;

private:
    std::unique_ptr<OscCycle> cycle_;
};

} // namespace synthyboy
