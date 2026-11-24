// SynthyBoy - core/sample-osc: robust WAV reader (16/24/32-bit PCM, float) and metadata parser.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/sampleosc/wav_reader.h"
#include "synthyboy/sampleosc/sample_types.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace synthyboy
{
namespace sampleosc
{
namespace
{
constexpr size_t kMinWavSize = 44;
constexpr size_t kMaxFileSize = 512 * 1024 * 1024;    // 512 MB safety limit
constexpr int64_t kMaxFrames = 64 * 1024 * 1024;     // 64 million frames safety limit
constexpr int kMinSampleRate = 1000;
constexpr int kMaxSampleRate = 384000;

inline uint16_t readU16LE (const uint8_t* p) noexcept
{
    return static_cast<uint16_t> (p[0] | (static_cast<uint16_t> (p[1]) << 8));
}

inline uint32_t readU32LE (const uint8_t* p) noexcept
{
    return static_cast<uint32_t> (p[0])
         | (static_cast<uint32_t> (p[1]) << 8)
         | (static_cast<uint32_t> (p[2]) << 16)
         | (static_cast<uint32_t> (p[3]) << 24);
}

inline float readFloatLE (const uint8_t* p) noexcept
{
    float v = 0.0f;
    std::memcpy (&v, p, sizeof (float));
    return sanitizeFloat (v);
}

// GUID: 00000001-0000-0010-8000-00AA00389B71 (PCM) and 00000003-0000-0010-8000-00AA00389B71 (Float)
constexpr uint8_t kSubFormatTail[14] = {
    0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};
} // namespace

int parseRootNoteFromFilename (const char* filename, float* fineTune) noexcept
{
    if (fineTune != nullptr)
        *fineTune = 0.0f;

    if (filename == nullptr || filename[0] == '\0')
        return -1;

    std::string s (filename);

    // Strip path components
    const auto lastSlash = s.find_last_of ("/\\");
    if (lastSlash != std::string::npos)
        s = s.substr (lastSlash + 1);

    // Strip common audio extension
    const auto lastDot = s.find_last_of ('.');
    if (lastDot != std::string::npos && lastDot > 0 && s.size() - lastDot <= 6)
        s = s.substr (0, lastDot);

    // Trim trailing spaces
    while (! s.empty() && std::isspace (static_cast<unsigned char> (s.back())))
        s.pop_back();

    if (s.empty())
        return -1;

    // Search backward for separator (space or underscore)
    const auto lastSep = s.find_last_of (" _");
    if (lastSep == std::string::npos || lastSep + 1 >= s.size())
        return -1;

    const std::string token = s.substr (lastSep + 1);
    if (token.size() < 2 || token.size() > 5)
        return -1;

    // First char: note letter
    const char letter = static_cast<char> (std::toupper (static_cast<unsigned char> (token[0])));
    int base = -1;
    switch (letter)
    {
        case 'C': base = 0; break;
        case 'D': base = 2; break;
        case 'E': base = 4; break;
        case 'F': base = 5; break;
        case 'G': base = 7; break;
        case 'A': base = 9; break;
        case 'B': base = 11; break;
        default:  return -1;
    }

    size_t idx = 1;
    int accidental = 0;
    if (token[idx] == '#' || token[idx] == 's' || token[idx] == 'S')
    {
        accidental = +1;
        ++idx;
    }
    else if (token[idx] == 'b' || token[idx] == 'B')
    {
        accidental = -1;
        ++idx;
    }

    if (idx >= token.size())
        return -1;

    // Parse octave integer (supports -2, -1, 0..9)
    bool negative = false;
    if (token[idx] == '-')
    {
        negative = true;
        ++idx;
    }

    if (idx >= token.size() || ! std::isdigit (static_cast<unsigned char> (token[idx])))
        return -1;

    int octave = 0;
    while (idx < token.size())
    {
        if (! std::isdigit (static_cast<unsigned char> (token[idx])))
            return -1;
        octave = octave * 10 + (token[idx] - '0');
        ++idx;
    }
    if (negative)
        octave = -octave;

    // In MIDI standard where A3 = 69 (440 Hz), C3 = 60. Octave 3 corresponds to (3 + 2) * 12 = 60.
    const int note = base + accidental + (octave + 2) * 12;
    if (note < 0 || note > 127)
        return -1;

    return note;
}

bool readWavInfo (const void* data, size_t size, WavInfo& outInfo) noexcept
{
    outInfo = WavInfo {};
    if (data == nullptr || size < kMinWavSize || size > kMaxFileSize)
        return false;

    const auto* bytes = static_cast<const uint8_t*> (data);

    // RIFF header
    if (std::memcmp (bytes, "RIFF", 4) != 0 || std::memcmp (bytes + 8, "WAVE", 4) != 0)
        return false;

    bool foundFmt = false;
    bool foundData = false;
    int formatTag = 0;
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    int64_t dataOffset = 0;
    int64_t dataSize = 0;

    size_t offset = 12;
    while (offset + 8 <= size)
    {
        const char chunkId[5] = {
            static_cast<char> (bytes[offset]),
            static_cast<char> (bytes[offset + 1]),
            static_cast<char> (bytes[offset + 2]),
            static_cast<char> (bytes[offset + 3]),
            '\0'
        };
        const uint32_t chunkSize = readU32LE (bytes + offset + 4);
        const size_t chunkDataOffset = offset + 8;

        if (chunkDataOffset > size || chunkSize > size - chunkDataOffset)
            break; // Truncated chunk

        if (std::strcmp (chunkId, "fmt ") == 0)
        {
            if (chunkSize < 16)
                return false;

            formatTag = readU16LE (bytes + chunkDataOffset);
            channels = readU16LE (bytes + chunkDataOffset + 2);
            sampleRate = static_cast<int> (readU32LE (bytes + chunkDataOffset + 4));
            bitsPerSample = readU16LE (bytes + chunkDataOffset + 14);

            if (formatTag == 0xFFFE) // WAVE_FORMAT_EXTENSIBLE
            {
                if (chunkSize >= 40)
                {
                    const uint16_t subFormat = readU16LE (bytes + chunkDataOffset + 24);
                    if (std::memcmp (bytes + chunkDataOffset + 26, kSubFormatTail, 12) == 0)
                        formatTag = subFormat;
                }
            }

            if (channels < 1 || channels > 2)
                return false;
            if (sampleRate < kMinSampleRate || sampleRate > kMaxSampleRate)
                return false;
            if (formatTag != 1 && formatTag != 3) // PCM or IEEE Float
                return false;
            if (formatTag == 1 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
                return false;
            if (formatTag == 3 && bitsPerSample != 32)
                return false;

            foundFmt = true;
        }
        else if (std::strcmp (chunkId, "data") == 0)
        {
            dataOffset = static_cast<int64_t> (chunkDataOffset);
            dataSize = static_cast<int64_t> (chunkSize);
            foundData = true;
        }
        else if (std::strcmp (chunkId, "inst") == 0)
        {
            if (chunkSize >= 7)
            {
                const uint8_t root = bytes[chunkDataOffset];
                const int8_t fine = static_cast<int8_t> (bytes[chunkDataOffset + 1]);
                if (root <= 127)
                {
                    outInfo.rootNote = static_cast<int> (root);
                    outInfo.fineTuneCents = static_cast<float> (fine);
                }
            }
        }
        else if (std::strcmp (chunkId, "smpl") == 0)
        {
            if (chunkSize >= 36)
            {
                const uint32_t unityNote = readU32LE (bytes + chunkDataOffset + 12);
                const uint32_t pitchFrac = readU32LE (bytes + chunkDataOffset + 16);
                const uint32_t numLoops = readU32LE (bytes + chunkDataOffset + 28);

                if (unityNote <= 127)
                {
                    outInfo.rootNote = static_cast<int> (unityNote);
                    outInfo.fineTuneCents = static_cast<float> (pitchFrac / 4294967296.0 * 100.0);
                }

                if (numLoops > 0 && chunkSize >= 36 + 24)
                {
                    const size_t loopOff = chunkDataOffset + 36;
                    outInfo.loopType = static_cast<int> (readU32LE (bytes + loopOff + 4));
                    outInfo.loopStart = static_cast<int64_t> (readU32LE (bytes + loopOff + 8));
                    outInfo.loopEnd = static_cast<int64_t> (readU32LE (bytes + loopOff + 12));
                    outInfo.hasSmplLoop = true;
                }
            }
        }

        const size_t paddedSize = chunkSize + (chunkSize & 1u);
        if (chunkDataOffset + paddedSize < offset)
            break; // Overflow protection
        offset = chunkDataOffset + paddedSize;
    }

    if (! foundFmt || ! foundData || dataOffset <= 0)
        return false;

    const int bytesPerFrame = channels * (bitsPerSample / 8);
    if (bytesPerFrame <= 0)
        return false;

    const int64_t numFrames = dataSize / bytesPerFrame;
    if (numFrames <= 0 || numFrames > kMaxFrames)
        return false;

    outInfo.numChannels = channels;
    outInfo.sampleRate = sampleRate;
    outInfo.bitsPerSample = bitsPerSample;
    outInfo.formatTag = formatTag;
    outInfo.numFrames = numFrames;

    return true;
}

bool readWav (const void* data, size_t size, std::vector<float>& left, std::vector<float>& right,
              WavInfo& outInfo, const char* filename)
{
    left.clear();
    right.clear();

    if (! readWavInfo (data, size, outInfo))
        return false;

    const auto* bytes = static_cast<const uint8_t*> (data);

    // Locate the 'data' chunk
    size_t dataOffset = 0;
    size_t dataSize = 0;
    size_t offset = 12;

    while (offset + 8 <= size)
    {
        const uint32_t chunkSize = readU32LE (bytes + offset + 4);
        const size_t chunkDataOffset = offset + 8;
        if (chunkDataOffset > size || chunkSize > size - chunkDataOffset)
            return false;

        if (std::memcmp (bytes + offset, "data", 4) == 0)
        {
            dataOffset = chunkDataOffset;
            dataSize = chunkSize;
            break;
        }

        const size_t paddedSize = chunkSize + (chunkSize & 1u);
        offset = chunkDataOffset + paddedSize;
    }

    if (dataOffset == 0 || dataOffset + dataSize > size)
        return false;

    const int channels = outInfo.numChannels;
    const int bits = outInfo.bitsPerSample;
    const int formatTag = outInfo.formatTag;
    const int bytesPerSample = bits / 8;
    const int bytesPerFrame = channels * bytesPerSample;
    const int64_t numFrames = static_cast<int64_t> (dataSize) / bytesPerFrame;

    if (numFrames <= 0 || numFrames > kMaxFrames)
        return false;

    left.resize (static_cast<size_t> (numFrames));
    if (channels == 2)
        right.resize (static_cast<size_t> (numFrames));

    const uint8_t* p = bytes + dataOffset;

    for (int64_t i = 0; i < numFrames; ++i)
    {
        float samples[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < channels; ++ch)
        {
            const uint8_t* sptr = p + ch * bytesPerSample;
            if (formatTag == 1) // PCM integer
            {
                if (bits == 16)
                {
                    const int16_t v = static_cast<int16_t> (readU16LE (sptr));
                    samples[ch] = static_cast<float> (v) / 32768.0f;
                }
                else if (bits == 24)
                {
                    int32_t v = static_cast<int32_t> (sptr[0] | (static_cast<uint32_t> (sptr[1]) << 8) | (static_cast<uint32_t> (sptr[2]) << 16));
                    if (v & 0x800000)
                        v |= static_cast<int32_t> (0xFF000000);
                    samples[ch] = static_cast<float> (v) / 8388608.0f;
                }
                else if (bits == 32)
                {
                    const int32_t v = static_cast<int32_t> (readU32LE (sptr));
                    samples[ch] = static_cast<float> (v) / 2147483648.0f;
                }
            }
            else if (formatTag == 3) // IEEE Float
            {
                samples[ch] = readFloatLE (sptr);
            }
            samples[ch] = sanitizeFloat (samples[ch]);
        }

        left[static_cast<size_t> (i)] = samples[0];
        if (channels == 2)
            right[static_cast<size_t> (i)] = samples[1];

        p += bytesPerFrame;
    }

    if (channels == 1)
        right = left;

    // Check filename for root note override if provided
    if (filename != nullptr && filename[0] != '\0')
    {
        float nameFine = 0.0f;
        const int noteFromName = parseRootNoteFromFilename (filename, &nameFine);
        if (noteFromName >= 0)
        {
            outInfo.rootNote = noteFromName;
            if (nameFine != 0.0f)
                outInfo.fineTuneCents = nameFine;
        }
    }

    return true;
}

bool readWavFile (const char* filePath, std::vector<float>& left, std::vector<float>& right,
                  WavInfo& outInfo)
{
    if (filePath == nullptr || filePath[0] == '\0')
        return false;

    FILE* f = std::fopen (filePath, "rb");
    if (f == nullptr)
        return false;

    std::fseek (f, 0, SEEK_END);
    const long fileLen = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);

    if (fileLen < static_cast<long> (kMinWavSize) || fileLen > static_cast<long> (kMaxFileSize))
    {
        std::fclose (f);
        return false;
    }

    std::vector<uint8_t> buffer (static_cast<size_t> (fileLen));
    const size_t bytesRead = std::fread (buffer.data(), 1, buffer.size(), f);
    std::fclose (f);

    if (bytesRead != buffer.size())
        return false;

    return readWav (buffer.data(), buffer.size(), left, right, outInfo, filePath);
}

} // namespace sampleosc
} // namespace synthyboy
