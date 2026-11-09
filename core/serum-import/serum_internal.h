// SynthyBoy - core: internal declarations of the Serum 2 preset importer (zstd, CBOR, FLAC, container).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// docs/ui-design/19-serum-preset-format.md describes the container; everything here is bounded, allocation-checked and never throws.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace synthyboy
{
namespace serumimport
{
// ---------------------------------------------------------------------------------------------------------------------
// zstd (RFC 8878) decoder, no dictionary (zstd_decode.cpp)
// ---------------------------------------------------------------------------------------------------------------------
enum class ZstdStatus : uint8_t { ok, malformed, tooLarge, truncated };
/** Decodes every frame of `src` (skippable frames are skipped) and appends the output to `out`. The output never exceeds `maxOut` bytes in total
    (tooLarge). A frame checksum, when present, is verified (malformed on mismatch). */
ZstdStatus zstdDecompress (const uint8_t* src, size_t size, size_t maxOut, std::vector<uint8_t>& out) noexcept;

// ---------------------------------------------------------------------------------------------------------------------
// CBOR value tree (cbor_reader.cpp)
// ---------------------------------------------------------------------------------------------------------------------
namespace cbor
{
enum class Type : uint8_t { null, boolean, integer, real, text, bytes, array, map };

/** One node of a flat tree. Children of an array/map are consecutive entries `first .. first+count-1` of `children` (map: key and value are
    two consecutive nodes). Numbers: integers in `i` (and `d`), reals in `d` (half/float/double). */
struct Node
{
    Type type = Type::null;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    uint32_t str = 0;      // text / bytes: offset into Doc::blob
    uint32_t len = 0;      // text / bytes: length
    uint32_t first = 0;    // array / map: index of the first child in Doc::kids
    uint32_t count = 0;    // array: elements; map: pairs
    bool floatArray = false; // array of numbers stored compactly (see Doc::nums)
    uint32_t numFirst = 0;   // floatArray: offset into Doc::nums
};

struct Doc
{
    std::vector<Node> nodes;
    std::vector<uint32_t> kids; // child node indices (array: elements; map: key0, value0, key1, value1, ...)
    std::vector<char> blob;     // text and byte payloads
    std::vector<double> nums;   // homogeneous numeric arrays (the sample data of the embedded tables), see Node::floatArray
    size_t errorOffset = 0;

    const Node& root() const { return nodes[0]; }
    std::string text (uint32_t node) const;
    bool isText (uint32_t node, const char* s) const;
    /** Value node of key `key` in map node `map`, or -1. */
    int find (uint32_t map, const char* key) const noexcept;
    /** Number of elements of an array node (also a compact one). */
    uint32_t arraySize (uint32_t node) const noexcept;
    /** Element k of a numeric array node as a double (0 if not a number). */
    double arrayNumber (uint32_t node, uint32_t k) const noexcept;
    /** Value of a number node (integer or real), `fallback` when it is neither. */
    double number (uint32_t node, double fallback = 0.0) const noexcept;
};

enum class Status : uint8_t { ok, malformed, tooDeep, budget };
/** Parses exactly one data item that spans all of [data, data+size). `maxWorkingBytes` bounds the tree. */
Status parse (const uint8_t* data, size_t size, int maxDepth, size_t maxWorkingBytes, Doc& out) noexcept;
} // namespace cbor

// ---------------------------------------------------------------------------------------------------------------------
// audio decoders (audio_decode.cpp): FLAC and WAV to float
// ---------------------------------------------------------------------------------------------------------------------
struct DecodedAudio
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> data; // interleaved
    size_t frames() const noexcept { return channels > 0 ? data.size() / static_cast<size_t> (channels) : 0; }
};
/** FLAC (native, any block size, 4..32 bit, up to 8 channels). False on anything it cannot decode. `maxFrames` bounds the output. */
bool decodeFlac (const uint8_t* data, size_t size, size_t maxFrames, DecodedAudio& out) noexcept;
/** RIFF/WAVE PCM 8/16/24/32 and IEEE float 32/64 (also WAVE_FORMAT_EXTENSIBLE). */
bool decodeWavFloat (const uint8_t* data, size_t size, size_t maxFrames, DecodedAudio& out) noexcept;
/** FLAC or WAV, decided by the magic. */
bool decodeAudio (const uint8_t* data, size_t size, size_t maxFrames, DecodedAudio& out) noexcept;

// ---------------------------------------------------------------------------------------------------------------------
// the container (serum_container.cpp)
// ---------------------------------------------------------------------------------------------------------------------
/** zlib (RFC 1950 / 1951) inflate; appended to `out`, at most `maxOut` bytes. */
bool inflateZlib (const uint8_t* src, size_t size, size_t maxOut, std::vector<uint8_t>& out) noexcept;
/** UBJSON (the body of the old Serum 1 era files, format code 0) into the same value tree as the CBOR reader. */
cbor::Status parseUbjson (const uint8_t* data, size_t size, int maxDepth, size_t maxWorkingBytes, cbor::Doc& out) noexcept;

enum class ContainerStatus : uint8_t { ok, notSerum, truncated, unsupportedFormat, decompressFailed, malformedBody, tooLarge, budget };
struct ContainerLimits
{
    size_t maxFileBytes = 512u << 20;   // the largest real preset is 12 MiB; embedded multisample data is not stored in the file
    size_t maxBodyBytes = 256u << 20;
    size_t maxWorkingBytes = 768u << 20;
    int maxDepth = 24;
};
struct Container
{
    std::string headerText;   // the JSON header
    std::string fileType;     // "SerumPreset", "SerumFX", "SerumFXRack", "XferShape", "XferPath", "XferClip", "XferArp", ...
    uint32_t format = 0;      // 2 = zstd + CBOR, 0 = zlib + UBJSON
    uint64_t declaredBodySize = 0;
    cbor::Doc body;
};
/** Header, decompression and value tree of a file of any of the Serum 2 families. */
ContainerStatus openContainer (const uint8_t* data, size_t size, const ContainerLimits& limits, Container& out) noexcept;
const char* containerStatusText (ContainerStatus s) noexcept;
} // namespace serumimport
} // namespace synthyboy
