// SynthyBoy - core/fx-serum-b: Lightweight 16/24/32-bit PCM and IEEE float WAV reader and writer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "wav_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace synthyboy::fx
{
namespace
{
uint16_t readU16LE (const uint8_t* p) noexcept
{
    return static_cast<uint16_t> (p[0] | (static_cast<uint16_t> (p[1]) << 8));
}

uint32_t readU32LE (const uint8_t* p) noexcept
{
    return static_cast<uint32_t> (p[0])
         | (static_cast<uint32_t> (p[1]) << 8)
         | (static_cast<uint32_t> (p[2]) << 16)
         | (static_cast<uint32_t> (p[3]) << 24);
}

void writeU16LE (std::vector<uint8_t>& buf, uint16_t val)
{
    buf.push_back (static_cast<uint8_t> (val & 0xFF));
    buf.push_back (static_cast<uint8_t> ((val >> 8) & 0xFF));
}

void writeU32LE (std::vector<uint8_t>& buf, uint32_t val)
{
    buf.push_back (static_cast<uint8_t> (val & 0xFF));
    buf.push_back (static_cast<uint8_t> ((val >> 8) & 0xFF));
    buf.push_back (static_cast<uint8_t> ((val >> 16) & 0xFF));
    buf.push_back (static_cast<uint8_t> ((val >> 24) & 0xFF));
}

void writeFourCC (std::vector<uint8_t>& buf, const char* str)
{
    for (int i = 0; i < 4; ++i)
        buf.push_back (static_cast<uint8_t> (str[i]));
}
} // namespace

bool readWav (const uint8_t* data, size_t sizeBytes, WavAudio& out)
{
    out = WavAudio {};
    if (data == nullptr || sizeBytes < 44)
        return false;

    // Check RIFF and WAVE signatures
    if (std::memcmp (data, "RIFF", 4) != 0 || std::memcmp (data + 8, "WAVE", 4) != 0)
        return false;

    size_t offset = 12;
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* dataPtr = nullptr;
    size_t dataLength = 0;

    while (offset + 8 <= sizeBytes)
    {
        const char chunkId[5] = { static_cast<char> (data[offset]),
                                  static_cast<char> (data[offset + 1]),
                                  static_cast<char> (data[offset + 2]),
                                  static_cast<char> (data[offset + 3]),
                                  '\0' };
        const uint32_t chunkSize = readU32LE (data + offset + 4);
        offset += 8;

        if (offset + chunkSize > sizeBytes)
            return false;

        if (std::strcmp (chunkId, "fmt ") == 0 && chunkSize >= 16)
        {
            audioFormat = readU16LE (data + offset);
            numChannels = readU16LE (data + offset + 2);
            sampleRate = readU32LE (data + offset + 4);
            bitsPerSample = readU16LE (data + offset + 14);

            if (audioFormat == 0xFFFE && chunkSize >= 40) // WAVE_FORMAT_EXTENSIBLE
            {
                // SubFormat GUID first two bytes denote audio format (1 = PCM, 3 = IEEE float)
                const uint16_t subFormat = readU16LE (data + offset + 24);
                audioFormat = subFormat;
            }
        }
        else if (std::strcmp (chunkId, "data") == 0)
        {
            dataPtr = data + offset;
            dataLength = chunkSize;
        }

        // Chunks are padded to word boundary (even number of bytes)
        offset += (chunkSize + 1) & ~static_cast<size_t> (1);
    }

    if (dataPtr == nullptr || numChannels == 0 || sampleRate == 0)
        return false;

    const bool isFloat = (audioFormat == 3);
    const bool isPcm = (audioFormat == 1);
    if (! isFloat && ! isPcm)
        return false;

    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
        return false;

    const size_t bytesPerSample = static_cast<size_t> (bitsPerSample / 8);
    const size_t bytesPerFrame = bytesPerSample * numChannels;
    const size_t totalFrames = dataLength / bytesPerFrame;
    const size_t totalSamples = totalFrames * numChannels;

    out.channels = static_cast<int> (numChannels);
    out.sampleRate = static_cast<int> (sampleRate);
    out.bitDepth = static_cast<int> (bitsPerSample);
    out.totalFrames = static_cast<int> (totalFrames);
    out.samples.resize (totalSamples);

    const uint8_t* p = dataPtr;

    if (isFloat && bitsPerSample == 32)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            float f = 0.0f;
            std::memcpy (&f, p, sizeof (float));
            out.samples[i] = std::isnan (f) || std::isinf (f) ? 0.0f : f;
            p += 4;
        }
    }
    else if (isPcm && bitsPerSample == 16)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            const int16_t s = static_cast<int16_t> (readU16LE (p));
            out.samples[i] = static_cast<float> (s) / 32768.0f;
            p += 2;
        }
    }
    else if (isPcm && bitsPerSample == 24)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            uint32_t val = static_cast<uint32_t> (p[0])
                         | (static_cast<uint32_t> (p[1]) << 8)
                         | (static_cast<uint32_t> (p[2]) << 16);
            if (val & 0x00800000u)
                val |= 0xFF000000u; // Sign extension
            const int32_t s = static_cast<int32_t> (val);
            out.samples[i] = static_cast<float> (s) / 8388608.0f;
            p += 3;
        }
    }
    else if (isPcm && bitsPerSample == 32)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            const int32_t s = static_cast<int32_t> (readU32LE (p));
            out.samples[i] = static_cast<float> (s) / 2147483648.0f;
            p += 4;
        }
    }
    else
    {
        return false;
    }

    return true;
}

bool readWavFile (const std::string& filePath, WavAudio& out)
{
    std::ifstream file (filePath, std::ios::binary | std::ios::ate);
    if (! file.is_open())
        return false;

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return false;

    file.seekg (0, std::ios::beg);
    std::vector<uint8_t> buffer (static_cast<size_t> (size));
    if (! file.read (reinterpret_cast<char*> (buffer.data()), size))
        return false;

    return readWav (buffer.data(), buffer.size(), out);
}

bool writeWav (std::vector<uint8_t>& out,
               const float* interleavedData,
               int frames,
               int channels,
               int sampleRate,
               int bitDepth,
               bool isFloat)
{
    if (interleavedData == nullptr || frames <= 0 || channels <= 0 || sampleRate <= 0)
        return false;

    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
        return false;

    out.clear();
    const size_t bytesPerSample = static_cast<size_t> (bitDepth / 8);
    const size_t bytesPerFrame = bytesPerSample * static_cast<size_t> (channels);
    const uint32_t dataBytes = static_cast<uint32_t> (frames) * static_cast<uint32_t> (bytesPerFrame);
    const uint32_t riffChunkSize = 36u + dataBytes;

    writeFourCC (out, "RIFF");
    writeU32LE (out, riffChunkSize);
    writeFourCC (out, "WAVE");

    // "fmt " chunk
    writeFourCC (out, "fmt ");
    writeU32LE (out, 16u);
    const uint16_t formatTag = isFloat ? 3u : 1u;
    writeU16LE (out, formatTag);
    writeU16LE (out, static_cast<uint16_t> (channels));
    writeU32LE (out, static_cast<uint32_t> (sampleRate));
    const uint32_t byteRate = static_cast<uint32_t> (sampleRate) * static_cast<uint32_t> (bytesPerFrame);
    writeU32LE (out, byteRate);
    writeU16LE (out, static_cast<uint16_t> (bytesPerFrame));
    writeU16LE (out, static_cast<uint16_t> (bitDepth));

    // "data" chunk
    writeFourCC (out, "data");
    writeU32LE (out, dataBytes);

    const size_t totalSamples = static_cast<size_t> (frames * channels);
    out.reserve (out.size() + dataBytes);

    if (isFloat && bitDepth == 32)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            float f = interleavedData[i];
            uint8_t b[4];
            std::memcpy (b, &f, 4);
            out.insert (out.end(), b, b + 4);
        }
    }
    else if (! isFloat && bitDepth == 16)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            const float clamped = std::clamp (interleavedData[i], -1.0f, 1.0f);
            const int16_t s = static_cast<int16_t> (std::round (clamped * 32767.0f));
            writeU16LE (out, static_cast<uint16_t> (s));
        }
    }
    else if (! isFloat && bitDepth == 24)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            const float clamped = std::clamp (interleavedData[i], -1.0f, 1.0f);
            const int32_t s = static_cast<int32_t> (std::round (clamped * 8388607.0f));
            out.push_back (static_cast<uint8_t> (s & 0xFF));
            out.push_back (static_cast<uint8_t> ((s >> 8) & 0xFF));
            out.push_back (static_cast<uint8_t> ((s >> 16) & 0xFF));
        }
    }
    else if (! isFloat && bitDepth == 32)
    {
        for (size_t i = 0; i < totalSamples; ++i)
        {
            const float clamped = std::clamp (interleavedData[i], -1.0f, 1.0f);
            const int32_t s = static_cast<int32_t> (std::round (clamped * 2147483647.0f));
            writeU32LE (out, static_cast<uint32_t> (s));
        }
    }

    return true;
}
} // namespace synthyboy::fx
