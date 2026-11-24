// SynthyBoy - wavetable-edit: WAV audio export (16-bit, 32-bit float, single-cycle, Serum clm chunk).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace synthyboy::wtedit
{
namespace
{
void writeU16(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void writeU32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void writeBytes(std::vector<uint8_t>& buf, const void* data, size_t len)
{
    const auto* p = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), p, p + len);
}

void patchU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v)
{
    buf[offset] = static_cast<uint8_t>(v & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
} // namespace

bool exportWav(const Wavetable& wt, const ExportOptions& options, std::vector<uint8_t>& outBytes)
{
    if (wt.numFrames() == 0)
        return false;

    outBytes.clear();

    const bool isSingleCycle = (options.format == ExportFormat::SingleCycle);
    const bool is16Bit = (options.format == ExportFormat::Wav16Bit);
    const uint32_t sampleRate = (options.sampleRate > 0) ? options.sampleRate : 44100;
    const uint16_t numChannels = 1;

    size_t frameStart = 0;
    size_t frameEnd = wt.numFrames();

    if (isSingleCycle)
    {
        frameStart = std::min(options.singleFrameIndex, wt.numFrames() - 1);
        frameEnd = frameStart + 1;
    }

    const size_t exportFrameCount = frameEnd - frameStart;
    const size_t totalSamples = exportFrameCount * kWaveformSize;

    const uint16_t bitsPerSample = is16Bit ? 16 : 32;
    const uint16_t audioFormat = is16Bit ? 1 : 3; // 1 = PCM, 3 = IEEE Float
    const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(totalSamples * (bitsPerSample / 8));

    // RIFF chunk header
    writeBytes(outBytes, "RIFF", 4);
    const size_t riffSizeOffset = outBytes.size();
    writeU32(outBytes, 0); // Placeholder for RIFF length
    writeBytes(outBytes, "WAVE", 4);

    // fmt subchunk
    writeBytes(outBytes, "fmt ", 4);
    writeU32(outBytes, 16); // Subchunk1Size for PCM / basic float
    writeU16(outBytes, audioFormat);
    writeU16(outBytes, numChannels);
    writeU32(outBytes, sampleRate);
    writeU32(outBytes, byteRate);
    writeU16(outBytes, blockAlign);
    writeU16(outBytes, bitsPerSample);

    // Serum compatibility 'clm ' chunk
    // Note: Serum manual (pages 285-286) documents "extra header information" without publishing
    // the binary specification of the 'clm ' chunk. We write the widely supported "<!>2048" text chunk.
    if (options.writeSerumClmChunk && !isSingleCycle)
    {
        const char clmText[] = "<!>2048 10000000 wavetable (created in SynthyBoy)";
        const uint32_t clmLen = static_cast<uint32_t>(sizeof(clmText)); // includes null terminator
        writeBytes(outBytes, "clm ", 4);
        writeU32(outBytes, clmLen);
        writeBytes(outBytes, clmText, clmLen);
        if (clmLen & 1)
            outBytes.push_back(0); // 2-byte alignment padding
    }

    // data subchunk
    writeBytes(outBytes, "data", 4);
    writeU32(outBytes, dataBytes);

    // Write samples
    if (is16Bit)
    {
        for (size_t f = frameStart; f < frameEnd; ++f)
        {
            const Frame& frame = wt.getFrame(f);
            for (size_t i = 0; i < kWaveformSize; ++i)
            {
                const float s = std::max(-1.0f, std::min(1.0f, frame[i]));
                const int16_t val = static_cast<int16_t>(s * 32767.0f);
                writeU16(outBytes, static_cast<uint16_t>(val));
            }
        }
    }
    else
    {
        for (size_t f = frameStart; f < frameEnd; ++f)
        {
            const Frame& frame = wt.getFrame(f);
            for (size_t i = 0; i < kWaveformSize; ++i)
            {
                float s = frame[i];
                if (std::isnan(s) || std::isinf(s)) s = 0.0f;
                uint32_t raw = 0;
                std::memcpy(&raw, &s, sizeof(float));
                writeU32(outBytes, raw);
            }
        }
    }

    // Pad data chunk if odd length
    if (dataBytes & 1)
        outBytes.push_back(0);

    // Patch RIFF total size: file size - 8 bytes
    const uint32_t riffSize = static_cast<uint32_t>(outBytes.size() - 8);
    patchU32(outBytes, riffSizeOffset, riffSize);

    return true;
}

} // namespace synthyboy::wtedit
