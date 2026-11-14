// SynthyBoy - core: the magnitude response of filter 1 / 2 of the own engine, measured on the real filter code (for the filter display).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ---- Filter response (how a display uses this) ----
//
// 1. Subscribe to the live values (once, message thread or any thread, lock free):
//        int idx[2];
//        FilterResponse::readoutIndices (1, idx);            // table indices of filter1.cutoff and filter1.resonance (filter 2: readoutIndices (2, idx))
//        engine->setReadoutTargets (idx, 2);                 // the audio thread takes the list over at the end of its next block
//    (setReadoutTargets replaces the whole list: put every knob the display shows in one call, at most LiveValues::kMaxTargets.)
// 2. On the display timer (any thread, lock free, no allocation):
//        LiveValues live;
//        if (engine->copyReadout (live) && live.numTargets >= 2) { cutoffHz = live.target[0]; resonancePct = live.target[1]; }
//    live.target[i] is the value of idx[i] in PLAIN units (cutoff in Hz, resonance in percent 0 .. 100), as modulated by the matrix for the voice that was played
//    last. With no sounding voice the engine evaluates the slots whose sources have a value without a voice (macros, mod wheel, aftertouch, pitch wheel, the last
//    velocity and note, fixed) on the subscribed knobs, so a macro or the mod wheel moves the readout of an idle engine; slots that read a per voice source (an LFO,
//    an envelope, random ...) have nothing to say without a voice and are left out. With nothing modulating the knob it is the plain knob value. The value is NOT
//    clamped to the knob's range (the filter clamps what it runs on); resonance may be below 0 or above 100: pass it on unchanged. It does NOT contain the key
//    tracking of the filter: that is applied by compute () from `referenceNote`, as the voice does. For a slot with the stereo flag the readout holds the left lane.
// 3. Build the ParamValues snapshot the shell already holds (shell-mpc: ParamValues::defaults () filled from paramMirror) and call, on the thread that draws
//    (never needs the audio thread; it may run while the audio thread processes):
//        FilterResponse response (48000.0);                  // once: allocates
//        FilterResponse::Request req;  req.filter = 1;  req.cutoffHz = cutoffHz;  req.resonancePercent = resonancePct;   // the modulated values; leave unset for the knob values
//        bool shown = response.compute (values, req, dbOut, numPoints);
//    dbOut[i] is the gain in dB at fMin * (fMax / fMin) ^ (i / (numPoints - 1)); false (and 0 dB everywhere) when the filter is switched off.
//    A shell recomputes the curve when the snapshot or the two live values changed (a compute costs about a millisecond or less, see the test
//    own_filter_response_every_model_is_finite_and_fast).
//
// ---- What is measured ----
// The filter of the voice (own::FilterUnit, the same class and the same settings: own::fillFilterSettings, which Voice::tick uses too) runs on a one sample impulse
// of amplitude 1e-3 (the nonlinear models stay in their linear region) in blocks of one control period, as in a voice (settings re-applied every 32 samples,
// smoothers settled at the targets). The impulse response is windowed (the last quarter fades out with a squared cosine, so long ringing of high resonance and comb
// models does not leak), transformed (core/wavetable-edit Radix2Fft), divided by the (flat) input spectrum, and the magnitude is read at the requested frequencies by
// linear interpolation between bins; below the first bin (f < one bin width) the response is evaluated directly (a DFT at exactly f), so a high pass keeps its slope
// down to the lowest frequency. The length is the power of two at or above sampleRate / 3, at least 8192 and at most 65536 (16384 samples at 44.1 and 48 kHz: 2.9 Hz
// per bin; 32768 at 88.2 / 96 kHz). Frequency resolution is therefore about 3 Hz: a resonance narrower than that is shown wider. A model that adds a constant offset
// to its output (MG Dirty) is measured around that operating point: the response to silence is subtracted.
// The result is the SMALL SIGNAL response of the filter stage: mix, drive (as the filter's gain), blend, key tracking, the style and the model settings (comb,
// formant) are in; filter pan is in as the power mean of the two channels (the filter of each channel at its own cutoff); a modulation with the stereo flag shows its
// LEFT lane (the readout is the left lane). The clean mode switch scales the signal by -24 dB before and +24 dB after the filter and so has no effect on a linear
// response (it matters only where the filter saturates, which a small signal does not show); likewise drive that saturates (diode, dirty models) shows its small
// signal gain. Models that are not time invariant (ring modulator, sample and hold: no transfer function exists) show the magnitude of the response to the impulse,
// which is what the first sample of a note gives. The level of the filter (filterN.level) is only in when Request::includeLevel is set. The oscillators, the voice
// control and the effects are not.
//
// Defined behaviour for unusable input: compute () returns false and fills 0 dB when the filter is off, the filter number is not 1 or 2, numPoints < 1 (then nothing
// is written), dbOut is null, fMin or fMax is not finite or fMin <= 0 or fMax < fMin, an override is not finite (cutoff <= 0 included), or a parameter of the filter
// group in the snapshot is NaN or infinite. Snapshot values outside the knob range are clamped to it. The constructor clamps the sample rate to 22050 .. 192000 Hz
// (the range the filters themselves run in; NaN or <= 0 gives 48000); sampleRate () returns the rate that is used. numPoints == 1 gives the value at fMin.
//
// Threading and memory: the object owns its filter; it never touches an engine. All memory is allocated in the constructor; compute () does not allocate, lock or
// do I/O. One FilterResponse must not be used by two threads at the same time; the result is deterministic (the same request gives bit identical output).
#pragma once

#include <memory>
#include <optional>

#include "state.h"

namespace synthyboy
{
class FilterResponse
{
public:
    struct Request
    {
        int filter = 1;                 // 1 or 2
        std::optional<float> cutoffHz;         // set: used instead of the snapshot's filterN.cutoff (plain Hz > 0, e.g. the modulated value of the readout)
        std::optional<float> resonancePercent; // set: used instead of the snapshot's filterN.resonance (plain percent; below 0 or above 100 as the voice would run it: the filter clamps)
        int referenceNote = 60;         // key for the key tracking of the filter (60 = no shift)
        float fMin = 20.0f;             // Hz, first point
        float fMax = 20000.0f;          // Hz, last point (above Nyquist: the value at Nyquist)
        bool includeLevel = false;      // add filterN.level (dB) to the curve
    };

    /** Allocates everything (the filter, an FFT of 8192 .. 65536 samples worth of buffers: about 0.5 MB at 48 kHz) and warms up the parameter index. */
    explicit FilterResponse (double sampleRate);
    ~FilterResponse();
    FilterResponse (const FilterResponse&) = delete;
    FilterResponse& operator= (const FilterResponse&) = delete;

    /** Writes numPoints (>= 1) values in dB at logarithmically spaced frequencies from request.fMin to request.fMax into dbOut, clamped to -120 .. +60 dB.
        Returns false, and fills 0 dB, when the filter is off or the arguments are unusable (the list in the header comment). */
    bool compute (const ParamValues& values, const Request& request, float* dbOut, int numPoints) noexcept;

    /** Table indices (for IEngine::setReadoutTargets) of filterN.cutoff (out[0]) and filterN.resonance (out[1]); -1 for a filter number other than 1 or 2; null out: nothing. */
    static void readoutIndices (int filter, int out[2]) noexcept;

    double sampleRate() const noexcept;
    /** Length of the measured impulse response in samples and the width of one FFT bin in Hz. */
    int impulseLength() const noexcept;
    double binWidthHz() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace synthyboy
