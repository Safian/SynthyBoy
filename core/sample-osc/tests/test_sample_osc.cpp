// SynthyBoy - core/sample-osc tests: pitch accuracy, loop continuity, symmetry, aliasing, and WAV fuzzing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/sample_types.h"
#include "synthyboy/sampleosc/sample_voice.h"
#include "synthyboy/sampleosc/wav_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "test_dsp.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy::sampleosc;

namespace
{
constexpr double kPi = 3.14159265358979323846;

/** Helper to generate a mono WAV file in memory. */
std::vector<uint8_t> createTestWav (int formatTag, int bitsPerSample, int sampleRate, int numChannels,
                                   const std::vector<float>& channel0, const std::vector<float>& channel1,
                                   int rootNote = -1, float fineTune = 0.0f,
                                   int64_t loopStart = -1, int64_t loopEnd = -1)
{
    const int bytesPerSample = bitsPerSample / 8;
    const int numFrames = static_cast<int> (channel0.size());
    const uint32_t dataBytes = static_cast<uint32_t> (numFrames * numChannels * bytesPerSample);

    std::vector<uint8_t> out;
    out.reserve (128 + dataBytes);

    // RIFF header placeholder
    out.insert (out.end(), { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' });

    // fmt chunk
    out.insert (out.end(), { 'f', 'm', 't', ' ' });
    const uint32_t fmtSize = 16;
    out.push_back (static_cast<uint8_t> (fmtSize & 0xFF));
    out.push_back (static_cast<uint8_t> ((fmtSize >> 8) & 0xFF));
    out.push_back (0);
    out.push_back (0);

    // formatTag
    out.push_back (static_cast<uint8_t> (formatTag & 0xFF));
    out.push_back (static_cast<uint8_t> ((formatTag >> 8) & 0xFF));
    // channels
    out.push_back (static_cast<uint8_t> (numChannels & 0xFF));
    out.push_back (0);
    // sampleRate
    out.push_back (static_cast<uint8_t> (sampleRate & 0xFF));
    out.push_back (static_cast<uint8_t> ((sampleRate >> 8) & 0xFF));
    out.push_back (static_cast<uint8_t> ((sampleRate >> 16) & 0xFF));
    out.push_back (static_cast<uint8_t> ((sampleRate >> 24) & 0xFF));
    // byteRate
    const uint32_t byteRate = sampleRate * numChannels * bytesPerSample;
    out.push_back (static_cast<uint8_t> (byteRate & 0xFF));
    out.push_back (static_cast<uint8_t> ((byteRate >> 8) & 0xFF));
    out.push_back (static_cast<uint8_t> ((byteRate >> 16) & 0xFF));
    out.push_back (static_cast<uint8_t> ((byteRate >> 24) & 0xFF));
    // blockAlign
    const uint16_t blockAlign = static_cast<uint16_t> (numChannels * bytesPerSample);
    out.push_back (static_cast<uint8_t> (blockAlign & 0xFF));
    out.push_back (static_cast<uint8_t> ((blockAlign >> 8) & 0xFF));
    // bitsPerSample
    out.push_back (static_cast<uint8_t> (bitsPerSample & 0xFF));
    out.push_back (0);

    // Optional inst chunk
    if (rootNote >= 0)
    {
        out.insert (out.end(), { 'i', 'n', 's', 't' });
        const uint32_t instSize = 7;
        out.push_back (static_cast<uint8_t> (instSize & 0xFF));
        out.push_back (0);
        out.push_back (0);
        out.push_back (0);
        out.push_back (static_cast<uint8_t> (rootNote));
        out.push_back (static_cast<uint8_t> (static_cast<int8_t> (fineTune)));
        out.push_back (0); // gain
        out.push_back (0); // low note
        out.push_back (127); // high note
        out.push_back (1); // low vel
        out.push_back (127); // high vel
        out.push_back (0); // pad byte for odd size
    }

    // Optional smpl chunk
    if (loopStart >= 0 && loopEnd >= 0)
    {
        out.insert (out.end(), { 's', 'm', 'p', 'l' });
        const uint32_t smplSize = 36 + 24;
        out.push_back (static_cast<uint8_t> (smplSize & 0xFF));
        out.push_back (static_cast<uint8_t> ((smplSize >> 8) & 0xFF));
        out.push_back (0);
        out.push_back (0);
        // 9 uint32 fields
        for (int i = 0; i < 9; ++i)
        {
            uint32_t val = 0;
            if (i == 3 && rootNote >= 0) val = static_cast<uint32_t> (rootNote);
            if (i == 7) val = 1; // 1 loop
            out.push_back (static_cast<uint8_t> (val & 0xFF));
            out.push_back (static_cast<uint8_t> ((val >> 8) & 0xFF));
            out.push_back (static_cast<uint8_t> ((val >> 16) & 0xFF));
            out.push_back (static_cast<uint8_t> ((val >> 24) & 0xFF));
        }
        // loop record: cue(4), type(4), start(4), end(4), fraction(4), playCount(4)
        for (int i = 0; i < 6; ++i)
        {
            uint32_t val = 0;
            if (i == 1) val = 0; // forward
            if (i == 2) val = static_cast<uint32_t> (loopStart);
            if (i == 3) val = static_cast<uint32_t> (loopEnd);
            out.push_back (static_cast<uint8_t> (val & 0xFF));
            out.push_back (static_cast<uint8_t> ((val >> 8) & 0xFF));
            out.push_back (static_cast<uint8_t> ((val >> 16) & 0xFF));
            out.push_back (static_cast<uint8_t> ((val >> 24) & 0xFF));
        }
    }

    // data chunk
    out.insert (out.end(), { 'd', 'a', 't', 'a' });
    out.push_back (static_cast<uint8_t> (dataBytes & 0xFF));
    out.push_back (static_cast<uint8_t> ((dataBytes >> 8) & 0xFF));
    out.push_back (static_cast<uint8_t> ((dataBytes >> 16) & 0xFF));
    out.push_back (static_cast<uint8_t> ((dataBytes >> 24) & 0xFF));

    for (int i = 0; i < numFrames; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float s = (ch == 0) ? channel0[static_cast<size_t> (i)] : channel1[static_cast<size_t> (i)];
            if (formatTag == 1)
            {
                if (bitsPerSample == 16)
                {
                    const int16_t v = static_cast<int16_t> (std::max (-32768.0f, std::min (32767.0f, s * 32767.0f)));
                    out.push_back (static_cast<uint8_t> (v & 0xFF));
                    out.push_back (static_cast<uint8_t> ((v >> 8) & 0xFF));
                }
                else if (bitsPerSample == 24)
                {
                    const int32_t v = static_cast<int32_t> (std::max (-8388608.0f, std::min (8388607.0f, s * 8388607.0f)));
                    out.push_back (static_cast<uint8_t> (v & 0xFF));
                    out.push_back (static_cast<uint8_t> ((v >> 8) & 0xFF));
                    out.push_back (static_cast<uint8_t> ((v >> 16) & 0xFF));
                }
                else if (bitsPerSample == 32)
                {
                    const int32_t v = static_cast<int32_t> (std::max (-2147483648.0, std::min (2147483647.0, static_cast<double> (s) * 2147483647.0)));
                    out.push_back (static_cast<uint8_t> (v & 0xFF));
                    out.push_back (static_cast<uint8_t> ((v >> 8) & 0xFF));
                    out.push_back (static_cast<uint8_t> ((v >> 16) & 0xFF));
                    out.push_back (static_cast<uint8_t> ((v >> 24) & 0xFF));
                }
            }
            else if (formatTag == 3)
            {
                uint8_t fb[4];
                std::memcpy (fb, &s, 4);
                out.insert (out.end(), fb, fb + 4);
            }
        }
    }

    // Update RIFF total size
    const uint32_t totalRiff = static_cast<uint32_t> (out.size() - 8);
    out[4] = static_cast<uint8_t> (totalRiff & 0xFF);
    out[5] = static_cast<uint8_t> ((totalRiff >> 8) & 0xFF);
    out[6] = static_cast<uint8_t> ((totalRiff >> 16) & 0xFF);
    out[7] = static_cast<uint8_t> ((totalRiff >> 24) & 0xFF);

    return out;
}
} // namespace

TEST (sampleosc_wav_filename_note_parsing)
{
    CHECK_EQ (parseRootNoteFromFilename ("Morning Bass F2.wav"), 53);
    CHECK_EQ (parseRootNoteFromFilename ("Ashes Piano Eb3.flac"), 63);
    CHECK_EQ (parseRootNoteFromFilename ("Ashes Piano D#3.wav"), 63);
    CHECK_EQ (parseRootNoteFromFilename ("Kick_C#4.aif"), 73);
    CHECK_EQ (parseRootNoteFromFilename ("synth C3.wav"), 60);
    CHECK_EQ (parseRootNoteFromFilename ("sub_A-1.wav"), 21);
    CHECK_EQ (parseRootNoteFromFilename ("sample_no_note.wav"), -1);
    CHECK_EQ (parseRootNoteFromFilename ("sample123.wav"), -1);
    CHECK_EQ (parseRootNoteFromFilename (""), -1);
    CHECK_EQ (parseRootNoteFromFilename (nullptr), -1);
}

TEST (sampleosc_wav_reader_formats)
{
    // Test 16-bit PCM, 24-bit PCM, 32-bit PCM, and 32-bit float
    std::vector<float> sine (1000);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = static_cast<float> (std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / 44100.0));

    // 16-bit PCM
    {
        const auto wav = createTestWav (1, 16, 44100, 1, sine, sine, 69, 0.0f);
        SampleData sd;
        CHECK (sd.loadFromMemory (wav.data(), wav.size()));
        CHECK_EQ (sd.sampleRate(), 44100.0);
        CHECK_EQ (sd.numFrames(), 1000);
        CHECK_EQ (sd.rootNote(), 69);
        const auto v = sd.view();
        CHECK (v.isValid());
        CHECK_NEAR (v.getSample (0, 100), sine[100], 0.001);
    }

    // 24-bit PCM stereo
    {
        const auto wav = createTestWav (1, 24, 48000, 2, sine, sine, 60, 15.0f);
        SampleData sd;
        CHECK (sd.loadFromMemory (wav.data(), wav.size()));
        CHECK_EQ (sd.sampleRate(), 48000.0);
        CHECK_EQ (sd.numChannels(), 2);
        CHECK_EQ (sd.rootNote(), 60);
        CHECK_NEAR (sd.fineTuneCents(), 15.0f, 0.1);
        const auto v = sd.view();
        CHECK_NEAR (v.getSample (1, 250), sine[250], 0.0001);
    }

    // 32-bit PCM
    {
        const auto wav = createTestWav (1, 32, 96000, 1, sine, sine);
        SampleData sd;
        CHECK (sd.loadFromMemory (wav.data(), wav.size()));
        CHECK_EQ (sd.sampleRate(), 96000.0);
        const auto v = sd.view();
        CHECK_NEAR (v.getSample (0, 500), sine[500], 0.0001);
    }

    // 32-bit IEEE float
    {
        const auto wav = createTestWav (3, 32, 44100, 1, sine, sine, -1, 0.0f, 100, 800);
        SampleData sd;
        CHECK (sd.loadFromMemory (wav.data(), wav.size()));
        CHECK_EQ (sd.sampleRate(), 44100.0);
        CHECK (sd.hasSmplLoop());
        CHECK_EQ (sd.loopStart(), 100);
        CHECK_EQ (sd.loopEnd(), 800);
        const auto v = sd.view();
        CHECK_NEAR (v.getSample (0, 750), sine[750], 1.0e-5);
    }
}

TEST (sampleosc_wav_reader_garbage_inputs)
{
    WavInfo info;
    std::vector<float> l, r;

    // 1. Null buffer / empty
    CHECK (! readWav (nullptr, 0, l, r, info));
    CHECK (! readWav (nullptr, 100, l, r, info));

    // 2. Truncated sizes
    uint8_t tiny[16] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
    CHECK (! readWav (tiny, sizeof (tiny), l, r, info));

    // 3. Invalid magic
    uint8_t badMagic[48] = {};
    std::memcpy (badMagic, "NOT_RIFF_DATA_AT_ALL", 20);
    CHECK (! readWav (badMagic, sizeof (badMagic), l, r, info));

    // 4. Formats with invalid sample rates or channels
    std::vector<float> dummy (10, 0.0f);
    auto zeroChannels = createTestWav (1, 16, 44100, 1, dummy, dummy);
    zeroChannels[22] = 0; // numChannels = 0
    CHECK (! readWav (zeroChannels.data(), zeroChannels.size(), l, r, info));

    auto zeroRate = createTestWav (1, 16, 44100, 1, dummy, dummy);
    zeroRate[24] = 0; zeroRate[25] = 0; zeroRate[26] = 0; zeroRate[27] = 0;
    CHECK (! readWav (zeroRate.data(), zeroRate.size(), l, r, info));

    // 5. Huge fake data size exceeding file size
    auto fakeData = createTestWav (1, 16, 44100, 1, dummy, dummy);
    for (size_t i = 12; i + 8 <= fakeData.size(); ++i)
    {
        if (std::memcmp (fakeData.data() + i, "data", 4) == 0)
        {
            fakeData[i + 4] = 0xFF; fakeData[i + 5] = 0xFF; fakeData[i + 6] = 0xFF; fakeData[i + 7] = 0x7F; // 2GB
            break;
        }
    }
    CHECK (! readWav (fakeData.data(), fakeData.size(), l, r, info));

    // 6. Fuzzing with random byte streams
    sbtest::Rng rng (0x123456789ULL);
    for (int iter = 0; iter < 150; ++iter)
    {
        const size_t len = static_cast<size_t> (rng.next() % 2048 + 1);
        std::vector<uint8_t> garbage (len);
        for (size_t b = 0; b < len; ++b)
            garbage[b] = static_cast<uint8_t> (rng.next() & 0xFF);

        // Must fail cleanly without throwing or crashing
        const bool res = readWav (garbage.data(), garbage.size(), l, r, info);
        // It's expected to be false for almost all random data
        if (res)
        {
            CHECK (info.numFrames > 0);
            CHECK (info.sampleRate >= 1000);
        }
    }
}

TEST (sampleosc_pitch_accuracy_5_octaves)
{
    // Generate a 440 Hz pure sine wave at 44.1 kHz, root note 69 (A3)
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kSineLen = 44100; // 1 second
    std::vector<float> sine (kSineLen);
    for (size_t i = 0; i < kSineLen; ++i)
        sine[i] = static_cast<float> (std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / kSampleRate));

    SampleData sd;
    CHECK (sd.loadFromFrames (sine.data(), nullptr, static_cast<int64_t> (sine.size()), kSampleRate, 69));

    const auto view = sd.view();
    SampleVoice voice;
    voice.prepare (kSampleRate, 512);

    SampleOscParams params;
    params.rootKey = 69;
    params.quality = QualityTier::high;

    // Test 5 octaves of A notes:
    // A0 (33, 55 Hz), A1 (45, 110 Hz), A2 (57, 220 Hz), A3 (69, 440 Hz), A4 (81, 880 Hz), A5 (93, 1760 Hz)
    const int testNotes[6] = { 33, 45, 57, 69, 81, 93 };

    for (int note : testNotes)
    {
        voice.reset();
        voice.noteOn (note, 1.0f, &view, params);

        constexpr int kRenderFrames = 8192;
        std::vector<float> outLeft (kRenderFrames, 0.0f);
        std::vector<float> outRight (kRenderFrames, 0.0f);

        // Render in blocks
        constexpr int kBlock = 256;
        for (int start = 0; start < kRenderFrames; start += kBlock)
            voice.process (outLeft.data() + start, outRight.data() + start, kBlock);

        // Discard first 256 samples to be clear of the ~1.5ms anti-click ramp
        constexpr size_t kFftFrom = 512;
        constexpr size_t kFftLen = 4096;
        constexpr size_t kFftSize = 8192;

        const auto spectrum = sbtest::powerSpectrum (outLeft, kFftFrom, kFftLen, kFftSize, sbtest::Window::blackmanHarris);
        const double expectedFreq = 440.0 * std::pow (2.0, static_cast<double> (note - 69) / 12.0);
        const double detectedFreq = sbtest::peakFrequency (spectrum, kFftSize, kSampleRate, expectedFreq);

        const double relativeError = std::fabs (detectedFreq - expectedFreq) / expectedFreq;
        CHECK (relativeError < 0.005); // Within 0.5% (approx 8 cents over 5 octaves)
    }
}

TEST (sampleosc_loop_continuity_no_click)
{
    // Generate a saw-like signal with an abrupt discontinuity at the loop points
    constexpr int kFrames = 2000;
    std::vector<float> saw (kFrames);
    for (int i = 0; i < kFrames; ++i)
        saw[static_cast<size_t> (i)] = static_cast<float> (i) / static_cast<float> (kFrames);

    SampleData sd;
    CHECK (sd.loadFromFrames (saw.data(), nullptr, kFrames, 44100.0, 60));
    const auto view = sd.view();

    SampleVoice voice;
    voice.prepare (44100.0, 256);

    SampleOscParams params;
    params.rootKey = 60;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.2f; // Frame 400
    params.loopEnd = 0.8f;   // Frame 1600 (without crossfade, wrap causes jump 0.8 -> 0.2, step = 0.6!)
    params.loopCrossfade = 0.1f; // 10% crossfade
    params.quality = QualityTier::high;

    voice.noteOn (60, 1.0f, &view, params);

    constexpr int kTotal = 8000;
    std::vector<float> outLeft (kTotal, 0.0f);
    constexpr int kBlock = 128;
    for (int s = 0; s < kTotal; s += kBlock)
        voice.process (outLeft.data() + s, nullptr, std::min (kBlock, kTotal - s)); // (the last block must not run past the buffer: AddressSanitizer)

    // Inspect consecutive steps after anti-click fade-in (sample 300 onwards)
    float maxStep = 0.0f;
    for (int i = 300; i < kTotal; ++i)
    {
        const float step = std::fabs (outLeft[static_cast<size_t> (i)] - outLeft[static_cast<size_t> (i - 1)]);
        if (step > maxStep)
            maxStep = step;
    }

    // Without crossfade, maxStep would be ~0.6 (click!).
    // With crossfade, the step is strictly bounded by the gentle crossfade derivative.
    CHECK (maxStep < 0.03f);
}

TEST (sampleosc_ping_pong_symmetry)
{
    // Generate a known ramp: 0.0 to 1.0
    constexpr int kFrames = 1000;
    std::vector<float> ramp (kFrames);
    for (int i = 0; i < kFrames; ++i)
        ramp[static_cast<size_t> (i)] = static_cast<float> (i) / static_cast<float> (kFrames - 1);

    SampleData sd;
    CHECK (sd.loadFromFrames (ramp.data(), nullptr, kFrames, 44100.0, 60));
    const auto view = sd.view();

    SampleVoice voice;
    voice.prepare (44100.0, 256);

    SampleOscParams params;
    params.rootKey = 60;
    params.loopMode = LoopMode::pingPong;
    params.startOffset = 0.0f;
    params.endOffset = 1.0f;
    params.loopStart = 0.1f;
    params.loopEnd = 0.5f; // Frame 499.5
    params.quality = QualityTier::low;

    voice.noteOn (60, 1.0f, &view, params);

    // Render enough frames to hit loopEnd and turn around
    constexpr int kTotal = 1500;
    std::vector<float> out (kTotal, 0.0f);
    for (int s = 0; s < kTotal; s += 256)
    {
        const int n = std::min (256, kTotal - s);
        voice.process (out.data() + s, nullptr, n);
    }

    // Find the turnaround peak in the output (after anti-click ramp, around frame 500)
    int peakIdx = 300;
    for (int i = 300; i < 700; ++i)
    {
        if (out[static_cast<size_t> (i)] > out[static_cast<size_t> (peakIdx)])
            peakIdx = i;
    }

    // Symmetry test: out[peakIdx - k] should be very close to out[peakIdx + k]
    for (int k = 1; k <= 30; ++k)
    {
        const float before = out[static_cast<size_t> (peakIdx - k)];
        const float after = out[static_cast<size_t> (peakIdx + k)];
        CHECK_NEAR (before, after, 0.015f);
    }
}

TEST (sampleosc_reverse_equals_reversed_forward)
{
    // Generate an arbitrary non-symmetric waveform (chirp)
    constexpr int kFrames = 256;
    std::vector<float> chirp (kFrames);
    for (int i = 0; i < kFrames; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (kFrames);
        chirp[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * (50.0 + 300.0 * t * t) * t));
    }

    SampleData sd;
    CHECK (sd.loadFromFrames (chirp.data(), nullptr, kFrames, 44100.0, 60));
    const auto view = sd.view();

    SampleVoice voiceFwd;
    voiceFwd.prepare (44100.0, 256);

    SampleOscParams paramsFwd;
    paramsFwd.rootKey = 60;
    paramsFwd.loopMode = LoopMode::none;
    paramsFwd.reverse = false;
    paramsFwd.quality = QualityTier::medium;

    voiceFwd.noteOn (60, 1.0f, &view, paramsFwd);
    std::vector<float> fwdOut (kFrames, 0.0f);
    voiceFwd.process (fwdOut.data(), nullptr, kFrames);

    SampleVoice voiceRev;
    voiceRev.prepare (44100.0, 256);

    SampleOscParams paramsRev = paramsFwd;
    paramsRev.reverse = true;

    voiceRev.noteOn (60, 1.0f, &view, paramsRev);
    std::vector<float> revOut (kFrames, 0.0f);
    voiceRev.process (revOut.data(), nullptr, kFrames);

    // Discard the anti-click ramp region at the beginning and end (~64 samples)
    // and verify the inner body: revOut[kFrames - 1 - i] == fwdOut[i]
    constexpr int kMargin = 75;
    for (int i = kMargin; i < kFrames - kMargin; ++i)
    {
        const float fwd = fwdOut[static_cast<size_t> (i)];
        const float rev = revOut[static_cast<size_t> (kFrames - 1 - i)];
        CHECK_NEAR (fwd, rev, 0.02f);
    }
}

TEST (sampleosc_aliasing_sanity_noise_pitch_up)
{
    // White noise sample with energy spanning up to Nyquist (22.05 kHz) at 44.1 kHz
    constexpr int kNoiseFrames = 8192;
    std::vector<float> noise (kNoiseFrames);
    sbtest::Rng rng (0x987654321ULL);
    for (int i = 0; i < kNoiseFrames; ++i)
        noise[static_cast<size_t> (i)] = static_cast<float> (rng.uniform() * 2.0 - 1.0);

    SampleData sd;
    CHECK (sd.loadFromFrames (noise.data(), nullptr, kNoiseFrames, 44100.0, 60));
    CHECK (sd.numMipLevels() >= 3);

    const auto view = sd.view();
    SampleVoice voice;
    voice.prepare (44100.0, 512);

    // Pitch up by 2 octaves (pitch ratio = 4.0)
    // Note 84 is 24 semitones (2 octaves) above root 60
    SampleOscParams params;
    params.rootKey = 60;
    params.quality = QualityTier::high;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 1.0f;

    voice.noteOn (84, 1.0f, &view, params);

    constexpr int kRenderLen = 4096;
    std::vector<float> rendered (kRenderLen, 0.0f);
    for (int s = 0; s < kRenderLen; s += 256)
        voice.process (rendered.data() + s, nullptr, 256);

    // Also test pure tone that would alias without band-limiting:
    // A 14 kHz sine wave played 2 octaves up (pitch ratio 4.0)
    // 14 kHz * 4 = 56 kHz -> folds over 44.1 kHz Nyquist to 11.9 kHz!
    std::vector<float> highSine (kNoiseFrames);
    for (int i = 0; i < kNoiseFrames; ++i)
        highSine[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * 14000.0 * static_cast<double> (i) / 44100.0));

    SampleData sdSine;
    CHECK (sdSine.loadFromFrames (highSine.data(), nullptr, kNoiseFrames, 44100.0, 60));

    SampleVoice voiceTone;
    voiceTone.prepare (44100.0, 512);
    const auto sineView = sdSine.view();
    voiceTone.noteOn (84, 1.0f, &sineView, params);

    std::vector<float> toneOut (kRenderLen, 0.0f);
    for (int s = 0; s < kRenderLen; s += 256)
        voiceTone.process (toneOut.data() + s, nullptr, 256);

    // Measure RMS of the tone output (after anti-click):
    // In our band-limited sampler, the 14 kHz tone is in the stopband of mip level 2 (cutoff 5.5 kHz),
    // so it is attenuated heavily (> 40 dB).
    const double unattenuatedRms = 1.0 / std::sqrt (2.0); // ~0.707
    const double measuredRms = sbtest::rmsOf (toneOut, 512, kRenderLen);
    const double rejectionDb = sbtest::toDb (measuredRms / unattenuatedRms);

    // Rejection should be at least 40 dB down!
    CHECK (rejectionDb < -40.0);
}

TEST (sampleosc_overview_computation)
{
    constexpr int kFrames = 1000;
    std::vector<float> buf (kFrames, 0.0f);
    for (int i = 0; i < 500; ++i)
        buf[static_cast<size_t> (i)] = 0.8f;
    for (int i = 500; i < 1000; ++i)
        buf[static_cast<size_t> (i)] = -0.6f;

    SampleData sd;
    CHECK (sd.loadFromFrames (buf.data(), nullptr, kFrames, 44100.0, 60));

    const auto overview = sd.computeOverview (10);
    CHECK_EQ (static_cast<int> (overview.size()), 10);

    // First half (pixels 0..4) should have max ~0.8
    for (int i = 0; i < 5; ++i)
    {
        CHECK_NEAR (overview[static_cast<size_t> (i)].maxVal, 0.8f, 0.01f);
        CHECK_NEAR (overview[static_cast<size_t> (i)].minVal, 0.8f, 0.01f);
    }
    // Second half (pixels 5..9) should have min ~-0.6
    for (int i = 5; i < 10; ++i)
    {
        CHECK_NEAR (overview[static_cast<size_t> (i)].minVal, -0.6f, 0.01f);
        CHECK_NEAR (overview[static_cast<size_t> (i)].maxVal, -0.6f, 0.01f);
    }
}

TEST (sampleosc_unison_and_denormal_safety)
{
    std::vector<float> frames (500, 0.5f);
    // Inject denormals and NaNs into sample
    frames[100] = 1.0e-30f;
    frames[101] = 0.0f / 0.0f; // NaN

    SampleData sd;
    CHECK (sd.loadFromFrames (frames.data(), nullptr, static_cast<int64_t> (frames.size()), 44100.0, 60));

    SampleVoice v1, v2;
    v1.prepare (44100.0, 256);
    v2.prepare (44100.0, 256);

    v1.setUnisonPitchRatio (0.99f);
    v1.setUnisonPan (-0.5f);
    v1.setUnisonStartOffset (0.1f);
    v1.setUnisonLevel (0.8f);

    v2.setUnisonPitchRatio (1.01f);
    v2.setUnisonPan (0.5f);
    v2.setUnisonStartOffset (0.2f);
    v2.setUnisonLevel (0.8f);

    SampleOscParams p;
    const auto sdView = sd.view();
    v1.noteOn (60, 1.0f, &sdView, p);
    v2.noteOn (60, 1.0f, &sdView, p);

    std::vector<float> left (512, 0.0f);
    std::vector<float> right (512, 0.0f);

    v1.processAdd (left.data(), right.data(), 512);
    v2.processAdd (left.data(), right.data(), 512);

    CHECK (sbtest::allFinite (left));
    CHECK (sbtest::allFinite (right));
}

TEST (sampleosc_samplerates_and_blocksizes)
{
    // Block size invariance: rendering in 1-sample blocks vs 64-sample blocks vs 512-sample blocks
    constexpr int kFrames = 2000;
    std::vector<float> sine (kFrames);
    for (int i = 0; i < kFrames; ++i)
        sine[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / 44100.0));

    SampleData sd;
    CHECK (sd.loadFromFrames (sine.data(), nullptr, kFrames, 44100.0, 60));
    const auto view = sd.view();

    const double testSampleRates[5] = { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
    for (double sr : testSampleRates)
    {
        SampleVoice v1, v2;
        v1.prepare (sr, 512);
        v2.prepare (sr, 512);

        SampleOscParams p;
        p.rootKey = 60;
        p.loopMode = LoopMode::forward;
        p.loopStart = 0.1f;
        p.loopEnd = 0.9f;
        p.quality = QualityTier::medium;

        v1.noteOn (64, 0.9f, &view, p);
        v2.noteOn (64, 0.9f, &view, p);

        constexpr int kTotal = 1024;
        std::vector<float> outBlock (kTotal, 0.0f);
        std::vector<float> outSingle (kTotal, 0.0f);

        // Render v1 in blocks of 64
        for (int s = 0; s < kTotal; s += 64)
            v1.process (outBlock.data() + s, nullptr, 64);

        // Render v2 in blocks of 1 (sample-by-sample)
        for (int s = 0; s < kTotal; ++s)
            v2.process (outSingle.data() + s, nullptr, 1);

        for (int i = 0; i < kTotal; ++i)
            CHECK_NEAR (outBlock[static_cast<size_t> (i)], outSingle[static_cast<size_t> (i)], 1.0e-5f);
    }

    // Large block size 4096
    SampleVoice vLarge;
    vLarge.prepare (44100.0, 4096);
    SampleOscParams p;
    vLarge.noteOn (60, 1.0f, &view, p);
    std::vector<float> out4096 (4096, 0.0f);
    vLarge.process (out4096.data(), nullptr, 4096);
    CHECK (sbtest::allFinite (out4096));
}

TEST (sampleosc_quality_tiers)
{
    constexpr int kFrames = 1000;
    std::vector<float> noise (kFrames);
    sbtest::Rng rng (42);
    for (int i = 0; i < kFrames; ++i)
        noise[static_cast<size_t> (i)] = static_cast<float> (rng.uniform() * 2.0 - 1.0);

    SampleData sd;
    CHECK (sd.loadFromFrames (noise.data(), nullptr, kFrames, 44100.0, 60));
    const auto view = sd.view();

    const QualityTier tiers[3] = { QualityTier::low, QualityTier::medium, QualityTier::high };
    for (auto tier : tiers)
    {
        SampleVoice voice;
        voice.prepare (44100.0, 256);
        SampleOscParams params;
        params.quality = tier;
        voice.noteOn (60, 1.0f, &view, params);

        std::vector<float> out (256, 0.0f);
        voice.process (out.data(), nullptr, 256);
        CHECK (sbtest::allFinite (out));
        CHECK (voice.isActive());
    }
}

