// SynthyBoy - core/fx-serum-b: Lightweight 16/24/32-bit PCM and IEEE float WAV reader and writer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace synthyboy::fx
{
struct WavAudio
{
    int channels = 0;
    int sampleRate = 0;
    int bitDepth = 0;
    int totalFrames = 0;
    std::vector<float> samples; // interleaved: [L0, R0, L1, R1, ...] or mono [S0, S1, ...]
};

/** Reads a WAV file from memory supporting 16-bit, 24-bit, 32-bit integer PCM and 32-bit IEEE float.
    Returns true on success. */
bool readWav (const uint8_t* data, size_t sizeBytes, WavAudio& out);

/** Reads a WAV file from disk. Returns true on success. */
bool readWavFile (const std::string& filePath, WavAudio& out);

/** Writes a WAV file to memory. Used for self-contained unit tests.
    bitDepth can be 16, 24, 32 (with isFloat=false) or 32 (with isFloat=true). */
bool writeWav (std::vector<uint8_t>& out,
               const float* interleavedData,
               int frames,
               int channels,
               int sampleRate,
               int bitDepth,
               bool isFloat);
} // namespace synthyboy::fx
