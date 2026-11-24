// SynthyBoy - core/fx-serum-b: Serum FX 'Bode' (Bode frequency shifter).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace synthyboy::fx
{
/** Hilbert-based single-sideband frequency shifter matching the Serum 2 Bode FX module.
    Controls per Serum manual p.161:
      - MONO INPUT: Enable to route mono input to the module.
      - SHIFT: Percentage of range (-1.0 .. +1.0).
      - Retrig: Context menu action to restart oscillator phase.
      - RANGE: Shift range in Hertz (e.g. 0 .. 5000 Hz).
      - DIR: Direction (-1.0 .. +1.0); center causes channels to go in opposite directions.
      - WIDTH: Stereo sideband spread (0.0 .. 1.0).
      - DELAY: Delay time in seconds or synced.
      - BPM: Delay tempo sync flag.
      - FEED: Feedback of delay into shifter.
      - BALANCE: Delay input mix between down and up shifted signals.
      - BLUR: Chorus and wow/flutter modulation.
      - MIX: Wet/dry mix (0.0 .. 1.0).
      - LEVEL: Output trim in dB.
    Zero allocations or locks in process(). Denormal and NaN/inf safe. */
class Bode
{
public:
    static constexpr int kHilbertTaps = 129;
    static constexpr int kHilbertLatency = (kHilbertTaps - 1) / 2; // 64 samples

    struct Params
    {
        bool monoInput = false;
        float shift = 0.0f;       // -1.0 .. +1.0
        float range = 1000.0f;    // Hz
        float dir = 0.0f;         // -1.0 (down) .. +1.0 (up), 0.0 = center
        float width = 1.0f;       // 0.0 .. 1.0
        float delay = 0.0f;       // seconds (0.0 .. 2.0)
        bool bpm = false;         // tempo sync
        double tempoBpm = 120.0;
        double delayBeats = 0.25; // in whole beats (e.g. 0.25 = 1/16)
        float feedback = 0.0f;    // -0.95 .. +0.95
        float balance = 0.0f;     // -1.0 (down) .. +1.0 (up)
        float blur = 0.0f;        // 0.0 .. 1.0
        float mix = 1.0f;         // 0.0 .. 1.0
        float level = 0.0f;       // dB
    };

    Params params;

    Bode() noexcept;
    ~Bode() = default;

    /** Allocates delay and history buffers for the given sample rate and max block size. (non-RT) */
    void prepare (double sampleRate, int maxBlock);

    /** Clears all delay and filter histories. (RT safe) */
    void reset() noexcept;

    /** Resets the carrier oscillator phase. Serum right-click "Retrig" function. (RT safe) */
    void retrig() noexcept;

    /** Processes stereo in-place audio blocks. Block size n can be 1 to maxBlock. (RT safe) */
    void process (float* l, float* r, int n) noexcept;

    /** Constant latency of the linear-phase Hilbert FIR filter in frames. */
    int latency() const noexcept { return kHilbertLatency; }

private:
    double m_sampleRate = 48000.0;
    int m_maxBlock = 512;

    // Hilbert FIR filter coefficients (Blackman-windowed anti-symmetric odd taps)
    std::array<float, kHilbertTaps> m_hilbertCoeffs {};

    // Ring buffers for Hilbert FIR
    std::vector<float> m_histL;
    std::vector<float> m_histR;
    int m_histPos = 0;

    // Delay buffer to align dry signal with Hilbert latency
    std::vector<float> m_dryDelayL;
    std::vector<float> m_dryDelayR;
    int m_dryPos = 0;

    // Feedback delay line
    std::vector<float> m_delayBufL;
    std::vector<float> m_delayBufR;
    int m_delayCapacity = 0;
    int m_delayWritePos = 0;

    // Carrier oscillator phase
    double m_carrierPhase = 0.0;

    // Blur LFO phases
    double m_blurPhaseL = 0.0;
    double m_blurPhaseR = 0.0;

    void initHilbertFilter() noexcept;
};
} // namespace synthyboy::fx
