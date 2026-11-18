// SynthyBoy - core: the WAV wavetable file rules (docs/MPC.md "Wavetable browser"). One parser for every wavetable file the engine, the catalogue and the shells read: a
// user file in a folder of the player, a file of a content pack, a table the editor saved. JUCE free, no I/O (the caller hands over the bytes), defensive: every size is checked
// against what is left of the buffer before it is used, the number of chunks is bounded, nothing throws.
//
// What is accepted (and what happens to it):
//   - RIFF / WAVE, PCM 16 / 24 / 32 bit integer or IEEE float 32 bit (also in a WAVE_FORMAT_EXTENSIBLE header), 1 channel (a stereo file: the left channel is the table), any
//     sample rate (ignored: a table has no rate). Everything else is refused with a reason (8 bit, ADPCM, 64 bit float, more than 2 channels).
//   - The frame size: a `clm ` chunk of the "<!>2048 ..." kind names it (digits right after the three characters, 64 .. 8192); without one it is 2048 samples, the common
//     "wavetable WAV" convention of N frames one after the other. A frame size other than 2048 is resampled to 2048 samples per frame (cubic).
//   - A file whose length is a multiple of the frame size has length / frame size frames, at most 256 (more: the first 256 are used, `truncated` says so; the engine always did).
//   - A file with the 2048 convention whose length is NOT a multiple of 2048 and at most 4096 samples is ONE cycle of that length (an AKWF style single cycle of 600 or 1024
//     samples), resampled to one frame of 2048. With a `clm ` chunk the frame size is the chunk's and a tail shorter than a frame is dropped; a file shorter than one frame is one cycle.
//   - Refused: more than 4096 samples that are not a multiple of the frame size (a sample, not a table), fewer than 16 samples, a file above 16 MiB, damaged or truncated data,
//     a `clm ` chunk that names a frame size outside 64 .. 8192.
// A non finite sample reads as 0 (the WAV reader). The decode of a file that follows the convention (2048 per frame, no resampling) is the same float values as ever, so a preset that
// names such a file renders sample identical.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace synthyboy
{
constexpr size_t kMaxWavetableFileBytes = 16u * 1024u * 1024u;   // the largest wavetable file read at all (256 frames of 32 bit float are 2 MiB)
constexpr int kWavetableFrameSamples = 2048;                      // the engine's frame (Wavetable::kCycle)
constexpr int kMaxWavetableFileFrames = 256;                      // the engine's frame limit
constexpr int kMaxWavetableCycleSamples = 4096;                   // the longest file that is one cycle when it is not a whole number of frames
constexpr int kMinWavetableCycleSamples = 16;
constexpr int kMinClmFrameSamples = 64, kMaxClmFrameSamples = 8192;

struct WavetableData
{
    std::vector<float> samples;   // frames * 2048 samples, frame after frame
    int frames = 0;               // frames in `samples` (1 .. 256)
    int sourceFrames = 0;         // frames the file holds (before the limit of 256)
    int sourceFrameSamples = kWavetableFrameSamples;   // the frame size of the file (clm chunk, 2048, or the length of a single cycle)
    int sampleRate = 0;
    int bits = 0;                 // 16, 24 or 32
    bool floating = false;        // IEEE float
    bool singleCycle = false;     // the file is one cycle of an arbitrary length (resampled to one frame)
    bool resampled = false;       // the frames were resampled to 2048 samples
    bool truncated = false;       // more than 256 frames: the first 256 are in `samples`
    bool clmChunk = false;        // the frame size came from a `clm ` chunk
    bool leftOfStereo = false;    // a stereo file: the left channel is the table
};

struct WavetableParse
{
    bool ok = false;
    std::string reason;           // when not ok: short, lower case ("file too large (20.0 MB, the limit is 16 MB)"); the UI writes "not a wavetable file: " in front
    WavetableData data;
};

/** Parses the bytes of a WAV file as a wavetable (the rules above). Never throws; the bytes are not modified. */
WavetableParse parseWavetableWav (const void* bytes, size_t size);

/** The refusal a FILE SIZE alone gives (an empty file, above kMaxWavetableFileBytes), "" when the size is fine: the catalogue says it in the row without reading the file. */
std::string wavetableSizeRefusal (uint64_t bytes);

/** "not a wavetable file: <reason>" (the text of the browser row and of the display). */
std::string wavetableRefusalText (const std::string& reason);
} // namespace synthyboy
