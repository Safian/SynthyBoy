// SynthyBoy - tests of the bitmap GUI: golden images (a compact lossless file format of our own) and the comparison with a tolerance.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// File "SBG1": "SBG1", width, height (little endian u32), then the pixels as BGRA bytes of 0xAARRGGBB words, every row stored as the byte difference to the row above (the first row against
// zero), the whole byte stream packed with PackBits (a control byte n < 128: n + 1 literal bytes follow; n >= 128: the next byte repeated 257 - n times). A golden of a 1024 x 768 GUI is
// a few hundred kilobytes, written and read without any library.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "bitmap_gui_harness.h"

namespace bgtest
{
inline bool writeGolden (const std::string& path, const Image& image)
{
    std::vector<uint8_t> delta;
    delta.reserve (image.pixels.size() * 4);
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x)
        {
            const uint32_t above = y > 0 ? image.at (x, y - 1) : 0u;
            const uint32_t now = image.at (x, y);
            for (int s = 0; s < 32; s += 8)
                delta.push_back (static_cast<uint8_t> (((now >> s) & 0xFF) - ((above >> s) & 0xFF)));
        }
    std::vector<uint8_t> out { 'S', 'B', 'G', '1' };
    for (uint32_t v : { image.width, image.height })
        for (int s = 0; s < 32; s += 8)
            out.push_back (static_cast<uint8_t> (v >> s));
    size_t i = 0;
    while (i < delta.size())
    {
        size_t run = 1;
        while (i + run < delta.size() && run < 128 && delta[i + run] == delta[i])
            ++run;
        if (run >= 3)
        {
            out.push_back (static_cast<uint8_t> (257 - run));
            out.push_back (delta[i]);
            i += run;
            continue;
        }
        size_t lit = 0;
        while (i + lit < delta.size() && lit < 128)
        {
            size_t r = 1;
            while (i + lit + r < delta.size() && r < 3 && delta[i + lit + r] == delta[i + lit])
                ++r;
            if (r >= 3)
                break;
            ++lit;
        }
        if (lit == 0)
            lit = 1;
        out.push_back (static_cast<uint8_t> (lit - 1));
        out.insert (out.end(), delta.begin() + static_cast<std::ptrdiff_t> (i), delta.begin() + static_cast<std::ptrdiff_t> (i + lit));
        i += lit;
    }
    if (FILE* f = std::fopen (path.c_str(), "wb"))
    {
        const bool ok = std::fwrite (out.data(), 1, out.size(), f) == out.size();
        std::fclose (f);
        return ok;
    }
    return false;
}

inline bool readGolden (const std::string& path, Image& image)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;
    std::vector<uint8_t> in;
    uint8_t chunk[65536];
    size_t n;
    while ((n = std::fread (chunk, 1, sizeof chunk, f)) > 0)
        in.insert (in.end(), chunk, chunk + n);
    std::fclose (f);
    if (in.size() < 12 || in[0] != 'S' || in[1] != 'B' || in[2] != 'G' || in[3] != '1')
        return false;
    const auto u32 = [&] (size_t at) { return static_cast<uint32_t> (in[at]) | (static_cast<uint32_t> (in[at + 1]) << 8) | (static_cast<uint32_t> (in[at + 2]) << 16) | (static_cast<uint32_t> (in[at + 3]) << 24); };
    image.width = u32 (4);
    image.height = u32 (8);
    const size_t total = static_cast<size_t> (image.width) * image.height * 4;
    std::vector<uint8_t> delta;
    delta.reserve (total);
    size_t i = 12;
    while (i < in.size() && delta.size() < total)
    {
        const uint8_t c = in[i++];
        if (c < 128)
        {
            const size_t lit = static_cast<size_t> (c) + 1;
            if (i + lit > in.size())
                return false;
            delta.insert (delta.end(), in.begin() + static_cast<std::ptrdiff_t> (i), in.begin() + static_cast<std::ptrdiff_t> (i + lit));
            i += lit;
        }
        else
        {
            if (i >= in.size())
                return false;
            delta.insert (delta.end(), 257u - c, in[i++]);
        }
    }
    if (delta.size() != total)
        return false;
    image.pixels.assign (static_cast<size_t> (image.width) * image.height, 0u);
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x)
        {
            const uint32_t above = y > 0 ? image.pixels[static_cast<size_t> (y - 1) * image.width + x] : 0u;
            uint32_t now = 0;
            const size_t at = (static_cast<size_t> (y) * image.width + x) * 4;
            for (int k = 0; k < 4; ++k)
                now |= static_cast<uint32_t> (static_cast<uint8_t> (delta[at + static_cast<size_t> (k)] + ((above >> (8 * k)) & 0xFF))) << (8 * k);
            image.pixels[static_cast<size_t> (y) * image.width + x] = now;
        }
    return true;
}

struct Difference
{
    bool sameSize = true;
    double fractionOver = 0.0;   // pixels with a channel that differs by more than `channelThreshold`
    double meanChannelDiff = 0.0; // 0 .. 255
    int maxChannelDiff = 0;
};

inline Difference compareImages (const Image& a, const Image& b, int channelThreshold)
{
    Difference d;
    if (a.width != b.width || a.height != b.height)
    {
        d.sameSize = false;
        d.fractionOver = 1.0;
        d.meanChannelDiff = 255.0;
        d.maxChannelDiff = 255;
        return d;
    }
    uint64_t over = 0;
    double sum = 0.0;
    for (size_t i = 0; i < a.pixels.size(); ++i)
    {
        int worst = 0;
        for (int s = 0; s < 32; s += 8)
        {
            const int diff = std::abs (static_cast<int> ((a.pixels[i] >> s) & 0xFF) - static_cast<int> ((b.pixels[i] >> s) & 0xFF));
            worst = std::max (worst, diff);
            sum += diff;
        }
        over += worst > channelThreshold;
        d.maxChannelDiff = std::max (d.maxChannelDiff, worst);
    }
    d.fractionOver = static_cast<double> (over) / static_cast<double> (a.pixels.size());
    d.meanChannelDiff = sum / (4.0 * static_cast<double> (a.pixels.size()));
    return d;
}
} // namespace bgtest
