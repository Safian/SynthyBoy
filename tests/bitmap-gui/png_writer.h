// SynthyBoy - tests of the bitmap GUI: a PNG writer without compression (stored deflate blocks), for looking at frames and for the artefacts of a failed comparison.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "bitmap_gui_harness.h"

namespace bgtest
{
inline uint32_t crc32Of (const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu)
{
    static uint32_t table[256];
    static bool ready = false;
    if (! ready)
    {
        for (uint32_t n = 0; n < 256; ++n)
        {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

inline void putBE (std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8)
        out.push_back (static_cast<uint8_t> (v >> s));
}

inline void pngChunk (std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data)
{
    putBE (png, static_cast<uint32_t> (data.size()));
    std::vector<uint8_t> body (type, type + 4);
    body.insert (body.end(), data.begin(), data.end());
    png.insert (png.end(), body.begin(), body.end());
    putBE (png, crc32Of (body.data(), body.size()) ^ 0xFFFFFFFFu);
}

/** Writes `image` (0xAARRGGBB words) as an RGB PNG. False if the file cannot be written. */
inline bool writePng (const std::string& path, const Image& image)
{
    std::vector<uint8_t> raw;
    raw.reserve ((static_cast<size_t> (image.width) * 3 + 1) * image.height);
    for (uint32_t y = 0; y < image.height; ++y)
    {
        raw.push_back (0);
        for (uint32_t x = 0; x < image.width; ++x)
        {
            const uint32_t p = image.at (x, y);
            raw.push_back (static_cast<uint8_t> (p >> 16));
            raw.push_back (static_cast<uint8_t> (p >> 8));
            raw.push_back (static_cast<uint8_t> (p));
        }
    }
    std::vector<uint8_t> z { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw)
    {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    for (size_t pos = 0; pos < raw.size(); pos += 65535)
    {
        const size_t n = std::min<size_t> (65535, raw.size() - pos);
        z.push_back (pos + n >= raw.size() ? 1 : 0);
        z.push_back (static_cast<uint8_t> (n));
        z.push_back (static_cast<uint8_t> (n >> 8));
        z.push_back (static_cast<uint8_t> (~n));
        z.push_back (static_cast<uint8_t> (~n >> 8));
        z.insert (z.end(), raw.begin() + static_cast<std::ptrdiff_t> (pos), raw.begin() + static_cast<std::ptrdiff_t> (pos + n));
    }
    putBE (z, (b << 16) | a);

    std::vector<uint8_t> png { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> header;
    putBE (header, image.width);
    putBE (header, image.height);
    header.insert (header.end(), { 8, 2, 0, 0, 0 });
    pngChunk (png, "IHDR", header);
    pngChunk (png, "IDAT", z);
    pngChunk (png, "IEND", {});
    if (FILE* f = std::fopen (path.c_str(), "wb"))
    {
        const bool ok = std::fwrite (png.data(), 1, png.size(), f) == png.size();
        std::fclose (f);
        return ok;
    }
    return false;
}
} // namespace bgtest
