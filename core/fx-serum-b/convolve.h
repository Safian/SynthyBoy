// SynthyBoy - core/fx-serum-b: Serum FX 'Convolve' (Partitioned FFT convolution reverb/IR processor).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "fft.h"

namespace synthyboy::fx
{
/** Zero-latency uniform partitioned FFT convolution reverb matching the Serum 2 Convolve FX module.
    Controls per Serum manual p.164-165:
      - IMPULSE: loaded via loadIr(data, frames, channels, irRate)
      - SIZE: Time stretch / contract factor (0.25 .. 4.0)
      - TONE: Spectral tilt filter (-1.0 .. +1.0)
      - phi MIN: Minimum phase conversion via cepstrum
      - PRE-DLY: Pre-delay in seconds or beat division
      - BPM: Pre-delay tempo sync flag
      - ATTACK: Impulse attack / fade-in time
      - DECAY: Impulse decay / shortening
      - DAMP: High-frequency damping over time
      - IR GAIN: Impulse gain in dB (-40 .. +24 dB)
      - MIX: Wet/dry mix (0.0 .. 1.0)
      - LEVEL: Output trim in dB
    Partitions: Uniform partitions of size 128 (head FIR) + size 256 radix-2 FFT (tail).
    True zero latency: latency() == 0.
    Audio thread real-time safe: no allocations, locks, or system calls in process().
    IR length cap: 524,288 frames (~10.9 seconds at 48 kHz). */
class Convolve
{
public:
    static constexpr int kPartitionSize = 128;
    static constexpr int kFftSize = 256;
    static constexpr int kMaxIrFrames = 524288;
    static constexpr double kMaxIrSeconds = 10.0;

    struct Params
    {
        float size = 1.0f;         // 0.25 .. 4.0 (stretch/contract)
        float tone = 0.0f;         // -1.0 .. +1.0 (spectral tilt)
        bool minimumPhase = false; // phi MIN (cepstral min-phase conversion)
        float preDelay = 0.0f;     // seconds (0.0 .. 1.0)
        bool bpm = false;          // pre-delay tempo sync
        double tempoBpm = 120.0;
        double preDelayBeats = 0.25;
        float attack = 0.0f;       // seconds (fade-in)
        float decay = 0.0f;        // 0.0 = full decay, >0 shortens IR
        float damp = 0.0f;         // 0.0 .. 1.0 HF damping
        float irGain = 0.0f;       // dB (-40 .. +24)
        float mix = 1.0f;          // 0.0 (dry) .. 1.0 (wet)
        float level = 0.0f;        // output level in dB
        double hostRate = 0.0;     // the rate of the host (the engine runs the effects oversampled): the Serum law scales the impulse by hostRate / sampleRate, the level of Serum, which convolves at the host rate
        bool serumLaw = false;     // convolve.law = Serum (13-new-keys.md 4.49): the impulse is normalised to the peak 1 before IR GAIN, `decay` is the T60 of an exponential envelope in seconds, `size` reaches 0.1 .. 10
    };

    Params params;

    Convolve();
    ~Convolve() = default;

    /** Allocates real-time FFT and history buffers for the given sample rate and max block size. (non-RT) */
    void prepare (double sampleRate, int maxBlock);

    /** Resets the internal state of all filter partitions and delay buffers. (RT safe) */
    void reset() noexcept;

    /** Processes stereo in-place audio blocks. Block size n can be 1 to maxBlock. (RT safe) */
    void process (float* l, float* r, int n) noexcept;

    /** Reports processing latency in frames (0 for zero-latency partitioned convolution). */
    int latency() const noexcept { return 0; }

    /** Loads and partitions an impulse response. Channels: 1 (mono) or 2 (interleaved stereo). (non-RT) */
    bool loadIr (const float* data, int frames, int channels, double irRate);

private:
    double m_sampleRate = 48000.0;
    int m_maxBlock = 512;

    Radix2FFT m_fft;

    // Stored raw IR
    std::vector<float> m_rawIr;
    int m_rawFrames = 0;
    int m_rawChannels = 1;
    double m_rawIrRate = 48000.0;

    // Active partitioned IR
    // Head: first kPartitionSize samples (time domain FIR)
    std::vector<float> m_headL;
    std::vector<float> m_headR;

    // Tail: frequency domain spectra of size kFftSize
    std::vector<std::vector<Complex>> m_tailSpectraL;
    std::vector<std::vector<Complex>> m_tailSpectraR;
    int m_numTailPartitions = 0;

    // Real-time state
    // Head history circular buffer (size kPartitionSize * 2 for seamless linear access)
    std::vector<float> m_headHistoryL;
    std::vector<float> m_headHistoryR;
    int m_headHistPos = 0;

    // Input block accumulator for tail FFT (size kPartitionSize)
    std::vector<float> m_blockInL;
    std::vector<float> m_blockInR;
    int m_blockPos = 0;

    // Tail FFT spectrum delay line (FIFO of length m_numTailPartitions)
    std::vector<std::vector<Complex>> m_tailFftHistL;
    std::vector<std::vector<Complex>> m_tailFftHistR;
    int m_tailFftHistHead = 0;

    // Tail time-domain output blocks and overlap buffer (size kPartitionSize)
    std::vector<float> m_currentTailBlockL;
    std::vector<float> m_currentTailBlockR;
    std::vector<float> m_overlapL;
    std::vector<float> m_overlapR;

    // Pre-allocated scratch buffers for FFT in process() (size kFftSize)
    std::vector<Complex> m_scratchComplexIn;
    std::vector<Complex> m_scratchComplexSum;

    // Pre-delay buffer
    std::vector<float> m_preDelayBufL;
    std::vector<float> m_preDelayBufR;
    int m_preDelayCapacity = 0;
    int m_preDelayWritePos = 0;

    // Real-time 1-pole tilt filter states
    float m_toneStateL = 0.0f;
    float m_toneStateR = 0.0f;

    void rebuildPartitions();
    void computeTailFftBlock() noexcept;
    static void convertToMinimumPhase (std::vector<float>& ir, const Radix2FFT& fft);
};
} // namespace synthyboy::fx
