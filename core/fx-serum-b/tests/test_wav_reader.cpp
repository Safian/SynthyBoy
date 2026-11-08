// SynthyBoy - core/fx-serum-b tests: WAV reader and writer tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "wav_reader.h"

#include <cmath>
#include <vector>

#include "test_runner.h"

using namespace synthyboy::fx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeSineWave (int frames, int channels, float freq, float sr)
{
    std::vector<float> data (static_cast<size_t> (frames * channels));
    for (int i = 0; i < frames; ++i)
    {
        const float val = 0.8f * std::sin (static_cast<float> (2.0 * kPi * freq * i / sr));
        for (int ch = 0; ch < channels; ++ch)
            data[static_cast<size_t> (i * channels + ch)] = val;
    }
    return data;
}
} // namespace

TEST (wav_reader_roundtrip_16bit)
{
    const int frames = 1000;
    const int channels = 2;
    const int sr = 48000;
    const auto orig = makeSineWave (frames, channels, 440.0f, static_cast<float> (sr));

    std::vector<uint8_t> wavBytes;
    CHECK (writeWav (wavBytes, orig.data(), frames, channels, sr, 16, false));

    WavAudio loaded;
    CHECK (readWav (wavBytes.data(), wavBytes.size(), loaded));
    CHECK_EQ (loaded.channels, channels);
    CHECK_EQ (loaded.sampleRate, sr);
    CHECK_EQ (loaded.bitDepth, 16);
    CHECK_EQ (loaded.totalFrames, frames);

    // 16-bit quantization error <= 1 / 32768
    for (size_t i = 0; i < orig.size(); ++i)
        CHECK_NEAR (loaded.samples[i], orig[i], 1e-4);
}

TEST (wav_reader_roundtrip_24bit)
{
    const int frames = 800;
    const int channels = 1;
    const int sr = 44100;
    const auto orig = makeSineWave (frames, channels, 1000.0f, static_cast<float> (sr));

    std::vector<uint8_t> wavBytes;
    CHECK (writeWav (wavBytes, orig.data(), frames, channels, sr, 24, false));

    WavAudio loaded;
    CHECK (readWav (wavBytes.data(), wavBytes.size(), loaded));
    CHECK_EQ (loaded.channels, channels);
    CHECK_EQ (loaded.sampleRate, sr);
    CHECK_EQ (loaded.bitDepth, 24);
    CHECK_EQ (loaded.totalFrames, frames);

    // 24-bit quantization error <= 1 / 8388608
    for (size_t i = 0; i < orig.size(); ++i)
        CHECK_NEAR (loaded.samples[i], orig[i], 1e-6);
}

TEST (wav_reader_roundtrip_32bit_pcm)
{
    const int frames = 500;
    const int channels = 2;
    const int sr = 96000;
    const auto orig = makeSineWave (frames, channels, 880.0f, static_cast<float> (sr));

    std::vector<uint8_t> wavBytes;
    CHECK (writeWav (wavBytes, orig.data(), frames, channels, sr, 32, false));

    WavAudio loaded;
    CHECK (readWav (wavBytes.data(), wavBytes.size(), loaded));
    CHECK_EQ (loaded.channels, channels);
    CHECK_EQ (loaded.sampleRate, sr);
    CHECK_EQ (loaded.bitDepth, 32);
    CHECK_EQ (loaded.totalFrames, frames);

    for (size_t i = 0; i < orig.size(); ++i)
        CHECK_NEAR (loaded.samples[i], orig[i], 1e-7);
}

TEST (wav_reader_roundtrip_32bit_float)
{
    const int frames = 600;
    const int channels = 2;
    const int sr = 48000;
    const auto orig = makeSineWave (frames, channels, 500.0f, static_cast<float> (sr));

    std::vector<uint8_t> wavBytes;
    CHECK (writeWav (wavBytes, orig.data(), frames, channels, sr, 32, true));

    WavAudio loaded;
    CHECK (readWav (wavBytes.data(), wavBytes.size(), loaded));
    CHECK_EQ (loaded.channels, channels);
    CHECK_EQ (loaded.sampleRate, sr);
    CHECK_EQ (loaded.bitDepth, 32);
    CHECK_EQ (loaded.totalFrames, frames);

    // Float is exact
    for (size_t i = 0; i < orig.size(); ++i)
        CHECK_NEAR (loaded.samples[i], orig[i], 1e-7);
}

TEST (wav_reader_invalid_header)
{
    WavAudio loaded;
    uint8_t garbage[32] = { 0 };
    CHECK (! readWav (garbage, sizeof (garbage), loaded));
}
