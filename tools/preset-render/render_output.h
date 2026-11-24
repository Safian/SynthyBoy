// SynthyBoy - preset-render: the files of one render (WAV, optionally exact), kept in a header so a test can run the very code that writes them.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "wav_reader.h"

namespace synthyboy
{
namespace presetrender
{
/** FNV-1a, 64 bit, over the bit patterns of the samples (left, right interleaved, little endian words): any difference in any bit of any float changes it (up to a 2^-64 collision chance). */
inline uint64_t hashFloatBits (const float* v, size_t n) noexcept
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t u;
        std::memcpy (&u, v + i, 4);
        for (int k = 0; k < 4; ++k)
        {
            h ^= (u >> (8 * k)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    return h;
}

/** `--float32` writes 32 bit float WAVs (every bit of the render), the default is 24 bit integer PCM (differences below half a 24 bit step, 6e-8 of full scale, are invisible in it);
    `--hash` writes `<stem>.hash` next to the WAV: the 64 bit hash of the float bits as 16 hex digits and the frame count, also for a render that is not finite (no WAV is written then). */
struct OutputOptions
{
    bool float32 = false;
    bool hash = false;
};

/** Writes DIR/<stem>.wav (and DIR/<stem>.hash). `writeWavFile` is false for a render that must not be written as audio (not finite). Returns true if everything asked for was written. */
inline bool writeRenderFiles (const std::filesystem::path& dir, const std::string& stem, const std::vector<float>& left, const std::vector<float>& right, int sampleRate,
                              const OutputOptions& opt, bool writeWavFile = true)
{
    if (left.empty() || left.size() != right.size())
        return false;
    std::error_code ec;
    std::filesystem::create_directories (dir, ec);
    std::vector<float> inter (left.size() * 2);
    for (size_t i = 0; i < left.size(); ++i)
    {
        inter[2 * i] = left[i];
        inter[2 * i + 1] = right[i];
    }
    bool ok = true;
    if (writeWavFile)
    {
        std::vector<uint8_t> bytes;
        if (fx::writeWav (bytes, inter.data(), static_cast<int> (left.size()), 2, sampleRate, opt.float32 ? 32 : 24, opt.float32))
        {
            std::ofstream out (dir / (stem + ".wav"), std::ios::binary);
            out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
            ok = ok && static_cast<bool> (out);
        }
        else
            ok = false;
    }
    if (opt.hash)
    {
        char line[64];
        std::snprintf (line, sizeof line, "%016llx %zu\n", static_cast<unsigned long long> (hashFloatBits (inter.data(), inter.size())), left.size());
        std::ofstream out (dir / (stem + ".hash"), std::ios::binary);
        out << line;
        ok = ok && static_cast<bool> (out);
    }
    return ok;
}
} // namespace presetrender
} // namespace synthyboy
