// SynthyBoy - core/sample-osc: robust WAV reader (16/24/32-bit PCM, float) and metadata parser.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace synthyboy
{
namespace sampleosc
{
/** Metadata read from WAV chunks and file headers. */
struct WavInfo
{
    int numChannels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    int formatTag = 0;           // 1 = PCM, 3 = IEEE float, 0xFFFE = Extensible
    int64_t numFrames = 0;

    int rootNote = 60;           // Default 60 (C3) if not found in chunks or filename
    float fineTuneCents = 0.0f;

    int64_t loopStart = -1;      // -1 if no loop defined in 'smpl' chunk
    int64_t loopEnd = -1;
    int loopType = 0;            // 0 = forward, 1 = ping-pong, 2 = backward
    bool hasSmplLoop = false;
};

/**
 * Parses root note from filename suffix (e.g. "Morning Bass F2.wav", "Piano Eb3.wav", "Kick_C#4.wav").
 * The note name must be preceded by a space or underscore character.
 * Returns MIDI note number (0..127) or -1 if no valid note name found.
 */
int parseRootNoteFromFilename (const char* filename, float* fineTune = nullptr) noexcept;

/**
 * Inspects WAV header in memory without decoding sample frames.
 * Returns true if valid WAV and unsupported formats are rejected.
 */
bool readWavInfo (const void* data, size_t size, WavInfo& outInfo) noexcept;

/**
 * Decodes WAV audio into normalized floating point channel buffers (left and right).
 * If mono, right is filled identical to left.
 * If filename is provided, it is parsed for root note suffix if not specified in chunks.
 *
 * Enforces strict sanity limits (max 64M frames, max 512MB, valid sample rates).
 * Returns true on success, false on any corruption or unsupported format without throwing.
 */
bool readWav (const void* data, size_t size, std::vector<float>& left, std::vector<float>& right,
              WavInfo& outInfo, const char* filename = nullptr);

/**
 * Helper to read a WAV file from disk. Returns false if file cannot be opened or is invalid.
 */
bool readWavFile (const char* filePath, std::vector<float>& left, std::vector<float>& right,
                  WavInfo& outInfo);

} // namespace sampleosc
} // namespace synthyboy
