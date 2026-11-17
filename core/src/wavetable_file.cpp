// SynthyBoy - core: the WAV wavetable file rules (see wavetable_file.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_file.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "synthyboy/sampleosc/wav_reader.h"
#include "synthyboy/wavetable_edit.h"

namespace synthyboy
{
namespace
{
constexpr int kMaxChunksScanned = 256;     // a WAV file with more chunks than this is read as far as this and no further
constexpr int kMinSampleRate = 1000, kMaxSampleRate = 384000;   // what the shared WAV reader accepts

uint16_t u16 (const uint8_t* p) noexcept { return static_cast<uint16_t> (p[0] | (p[1] << 8)); }
uint32_t u32 (const uint8_t* p) noexcept { return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8) | (static_cast<uint32_t> (p[2]) << 16) | (static_cast<uint32_t> (p[3]) << 24); }

std::string megabytes (size_t bytes)
{
    char b[48];
    std::snprintf (b, sizeof b, "%.1f MB", static_cast<double> (bytes) / (1024.0 * 1024.0));
    return b;
}

/** What the chunk scan found before the data is decoded. */
struct Header
{
    bool haveFmt = false, haveData = false;
    int tag = 0, channels = 0, rate = 0, bits = 0;
    int clmFrame = 0;          // the frame size of a `clm ` chunk, 0 = none
    bool clmBad = false;       // the chunk names a frame size outside the range
    uint32_t clmValue = 0;
};

/** Walks the chunks (bounded) and reads the fields the refusals need. Returns false when the bytes are not a RIFF / WAVE file. */
bool scanChunks (const uint8_t* b, size_t size, Header& h)
{
    if (size < 12 || std::memcmp (b, "RIFF", 4) != 0 || std::memcmp (b + 8, "WAVE", 4) != 0)
        return false;
    size_t off = 12;
    for (int n = 0; n < kMaxChunksScanned && off + 8 <= size; ++n)
    {
        const uint32_t chunkSize = u32 (b + off + 4);
        const size_t body = off + 8;
        const size_t left = size - body;
        const bool isData = std::memcmp (b + off, "data", 4) == 0;
        if (isData)
            h.haveData = true;                                  // a data chunk whose size runs past the end is still "there": the decode decides
        if (static_cast<size_t> (chunkSize) > left)
            break;                                              // truncated chunk: nothing after it can be trusted
        if (std::memcmp (b + off, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            h.haveFmt = true;
            h.tag = u16 (b + body);
            h.channels = u16 (b + body + 2);
            h.rate = static_cast<int> (std::min<uint32_t> (u32 (b + body + 4), 0x7FFFFFFFu));
            h.bits = u16 (b + body + 14);
            if (h.tag == 0xFFFE && chunkSize >= 40)
                h.tag = u16 (b + body + 24);                    // the sub format of an extensible header
        }
        else if (std::memcmp (b + off, "clm ", 4) == 0 && chunkSize >= 4 && std::memcmp (b + body, "<!>", 3) == 0)
        {
            // "<!>2048 10000000 wavetable (...)": the digits right after the three characters are the frame size
            uint32_t v = 0;
            size_t i = 3, digits = 0;
            while (i < chunkSize && b[body + i] >= '0' && b[body + i] <= '9' && digits < 6)
            {
                v = v * 10 + static_cast<uint32_t> (b[body + i] - '0');
                ++i;
                ++digits;
            }
            if (digits > 0)
            {
                h.clmValue = v;
                if (v >= static_cast<uint32_t> (kMinClmFrameSamples) && v <= static_cast<uint32_t> (kMaxClmFrameSamples))
                    h.clmFrame = static_cast<int> (v);
                else
                    h.clmBad = true;
            }
        }
        const size_t padded = static_cast<size_t> (chunkSize) + (chunkSize & 1u);
        if (padded > left)
            break;
        off = body + padded;
    }
    return true;
}
} // namespace

std::string wavetableRefusalText (const std::string& reason)
{
    return "not a wavetable file: " + reason;
}

std::string wavetableSizeRefusal (uint64_t bytes)
{
    if (bytes == 0)
        return "empty file";
    if (bytes > kMaxWavetableFileBytes)
        return "file too large (" + megabytes (static_cast<size_t> (std::min<uint64_t> (bytes, 0x7FFFFFFFFFFFull))) + ", the limit is " + megabytes (kMaxWavetableFileBytes) + ")";
    return {};
}

WavetableParse parseWavetableWav (const void* bytes, size_t size)
{
    WavetableParse out;
    auto refuse = [&] (const std::string& why) { out.ok = false; out.reason = why; out.data = WavetableData {}; return out; };
    try
    {
        if (bytes == nullptr || size == 0)
            return refuse ("empty file");
        if (const std::string big = wavetableSizeRefusal (size); ! big.empty())
            return refuse (big);
        const auto* b = static_cast<const uint8_t*> (bytes);
        Header h;
        if (! scanChunks (b, size, h))
            return refuse ("not a WAV file");
        if (! h.haveFmt)
            return refuse ("the WAV file has no format chunk");
        if (h.channels < 1 || h.channels > 2)
            return refuse (std::to_string (h.channels) + " channels (mono expected)");
        if (h.tag != 1 && h.tag != 3)
            return refuse ("unsupported WAV format (tag " + std::to_string (h.tag) + "): PCM or float expected");
        if ((h.tag == 1 && h.bits != 16 && h.bits != 24 && h.bits != 32) || (h.tag == 3 && h.bits != 32))
            return refuse (std::to_string (h.bits) + " bit " + (h.tag == 3 ? "float" : "PCM") + " is not supported (16, 24, 32 bit or 32 bit float)");
        if (h.rate < kMinSampleRate || h.rate > kMaxSampleRate)
            return refuse ("sample rate " + std::to_string (h.rate) + " Hz is out of range");
        if (! h.haveData)
            return refuse ("the WAV file has no audio data");
        if (h.clmBad)
            return refuse ("frame size " + std::to_string (h.clmValue) + " in the clm chunk is outside " + std::to_string (kMinClmFrameSamples) + " .. " + std::to_string (kMaxClmFrameSamples));

        std::vector<float> left, right;
        sampleosc::WavInfo info;
        if (! sampleosc::readWav (b, size, left, right, info, nullptr) || left.empty())
            return refuse ("damaged or truncated audio data");

        const size_t n = left.size();
        const size_t frameSize = h.clmFrame > 0 ? static_cast<size_t> (h.clmFrame) : static_cast<size_t> (kWavetableFrameSamples);
        size_t sourceFrames = n / frameSize;
        bool single = false;
        if (sourceFrames == 0)
            single = true;                                          // shorter than one frame: one cycle
        else if (h.clmFrame == 0 && n % frameSize != 0)
        {
            if (n <= static_cast<size_t> (kMaxWavetableCycleSamples))
                single = true;                                      // an AKWF style cycle of a length of its own
            else
                return refuse (std::to_string (n) + " samples is not a multiple of " + std::to_string (kWavetableFrameSamples) + " and too long for one cycle (" + std::to_string (kMaxWavetableCycleSamples) + " at most): this is a sample, not a table");
        }
        if (single && n < static_cast<size_t> (kMinWavetableCycleSamples))
            return refuse ("only " + std::to_string (n) + " samples (at least " + std::to_string (kMinWavetableCycleSamples) + " for a cycle)");

        WavetableData& d = out.data;
        d.sampleRate = info.sampleRate;
        d.bits = info.bitsPerSample;
        d.floating = info.formatTag == 3;
        d.clmChunk = h.clmFrame > 0;
        d.leftOfStereo = info.numChannels == 2;
        if (single)
        {
            d.singleCycle = true;
            d.sourceFrames = 1;
            d.sourceFrameSamples = static_cast<int> (n);
            d.frames = 1;
            d.resampled = n != static_cast<size_t> (kWavetableFrameSamples);
            d.samples.resize (static_cast<size_t> (kWavetableFrameSamples));
            wtedit::resampleBuffer (left.data(), n, d.samples.data(), static_cast<size_t> (kWavetableFrameSamples));
        }
        else
        {
            const size_t frames = std::min<size_t> (sourceFrames, static_cast<size_t> (kMaxWavetableFileFrames));
            d.sourceFrames = static_cast<int> (std::min<size_t> (sourceFrames, 0x7FFFFFFFu));
            d.sourceFrameSamples = static_cast<int> (frameSize);
            d.frames = static_cast<int> (frames);
            d.truncated = sourceFrames > frames;
            d.resampled = frameSize != static_cast<size_t> (kWavetableFrameSamples);
            d.samples.resize (frames * static_cast<size_t> (kWavetableFrameSamples));
            if (! d.resampled)
                std::copy (left.begin(), left.begin() + static_cast<std::ptrdiff_t> (d.samples.size()), d.samples.begin());
            else
                for (size_t f = 0; f < frames; ++f)
                    wtedit::resampleBuffer (left.data() + f * frameSize, frameSize, d.samples.data() + f * static_cast<size_t> (kWavetableFrameSamples), static_cast<size_t> (kWavetableFrameSamples));
        }
        out.ok = true;
        return out;
    }
    catch (...)
    {
        return refuse ("the file could not be read");
    }
}
} // namespace synthyboy
