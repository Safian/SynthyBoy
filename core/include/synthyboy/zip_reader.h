// SynthyBoy - core: a small, bounded zip reader (stored + deflate) with an inflate of our own.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Reads the archive from bytes the host handed over (no I/O, no globals, no threads, never throws). Made for .vitalbank (a zip of
// .vital files, docs/ui-design/11-vital-preset-import.md Q5) but generic. What it supports: the central directory, method 0 (stored)
// and 8 (deflate), CRC-32 checked on every extraction. What it refuses (ZipStatus::unsupported): encryption, zip64, other methods,
// multi-disk archives. Bomb limits: entry count, size of one entry, sum of all declared sizes; an entry is inflated into a buffer of
// exactly its declared size and the stream must end there, so the output can never exceed what the limits allowed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace synthyboy
{
enum class ZipStatus : uint8_t
{
    ok,
    notZip,        // no end-of-central-directory record found
    truncated,     // the structures point outside the data
    unsupported,   // encryption, zip64, an unknown compression method, several disks
    corrupt,       // inconsistent sizes, bad deflate stream, bad local header
    limitExceeded, // more entries / bytes than ZipLimits allow
    crcMismatch,   // inflated, but the CRC-32 is not the stored one
    badIndex
};

const char* zipStatusName (ZipStatus status) noexcept;

struct ZipLimits
{
    size_t maxEntries = 5000;                 // docs/ui-design/11 section 2.6: at most 5000 files per batch
    size_t maxEntryBytes = 32u << 20;         // one entry, uncompressed (VitalImportLimits::maxFileBytes)
    size_t maxTotalBytes = 1024u << 20;       // all entries, uncompressed, as declared
    size_t maxNameBytes = 1024;
};

struct ZipEntry
{
    std::string name;            // as stored (UTF-8 or CP437: bytes are kept verbatim)
    uint16_t method = 0;         // 0 stored, 8 deflate
    uint32_t crc32 = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localHeaderOffset = 0;
    bool directory = false;      // name ends with '/'
    bool encrypted = false;      // general purpose flag bit 0: extract () answers unsupported
    bool safePath = true;        // false: absolute, drive letter, back slash, ".." segment or a control character in the name
};

class ZipArchive
{
public:
    /** Parses the central directory of data[0..size). The data must outlive the archive. Replaces any previous archive. */
    ZipStatus open (const void* data, size_t size, const ZipLimits& limits = {}) noexcept;

    const std::vector<ZipEntry>& entries() const noexcept { return list; }
    /** Index of the first entry with this exact name, or -1. */
    int find (const std::string& name) const noexcept;

    /** Inflates (or copies) entry `index` into `out` (resized to the declared size) and verifies the CRC-32. On failure out is empty. */
    ZipStatus extract (size_t index, std::vector<uint8_t>& out) const noexcept;

private:
    const uint8_t* base = nullptr;
    size_t length = 0;
    ZipLimits lim;
    std::vector<ZipEntry> list;
};

/** True if the first bytes are a zip local file header or an empty archive's end record ("PK\3\4" / "PK\5\6"). */
bool looksLikeZip (const void* data, size_t size) noexcept;

/** CRC-32 (IEEE 802.3, the zip polynomial). */
uint32_t crc32Of (const void* data, size_t size, uint32_t seed = 0) noexcept;

/** Raw deflate (RFC 1951) of in[0..inSize) into out[0..outSize). The stream must produce exactly outSize bytes and end there
    (a longer or shorter stream is `corrupt`). Never writes outside out, never reads outside in. */
ZipStatus inflateRaw (const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize) noexcept;
} // namespace synthyboy
