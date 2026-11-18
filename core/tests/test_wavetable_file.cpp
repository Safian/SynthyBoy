// SynthyBoy - tests of the WAV wavetable parser (core/include/synthyboy/wavetable_file.h): the frame conventions, the `clm ` chunk, the bit depths, a single cycle of any length, the
// refusals and odd chunks, and that the engine reads a table file with the same rules. Synthetic files only (written here byte by byte).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "synthyboy/content_store.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wavetable_file.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
constexpr double kPi = 3.14159265358979323846;

void put16 (std::vector<uint8_t>& b, uint32_t v) { b.push_back (static_cast<uint8_t> (v & 255)); b.push_back (static_cast<uint8_t> ((v >> 8) & 255)); }
void put32 (std::vector<uint8_t>& b, uint32_t v) { put16 (b, v & 0xFFFF); put16 (b, v >> 16); }
void putTag (std::vector<uint8_t>& b, const char* t) { b.insert (b.end(), t, t + 4); }

struct Spec
{
    int tag = 1, channels = 1, rate = 44100, bits = 16;
    std::string clm;                 // the text of a `clm ` chunk ("" = none)
    std::string junk;                // an unknown chunk before the data ("" = none), written with an odd size when the text is odd
    bool clmAfterData = false;
    bool dataSizeTooBig = false;     // the data chunk claims more bytes than the file has
};

/** A WAV file of `n` frames of `value (i)` (the same value in every channel). */
template <class F>
std::vector<uint8_t> makeWav (size_t n, const Spec& s, F value)
{
    std::vector<uint8_t> d;
    const size_t bytesPer = static_cast<size_t> (s.bits / 8);
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < s.channels; ++c)
        {
            const double v = value (i);
            if (s.tag == 3)
            {
                const float f = static_cast<float> (v);
                uint32_t u;
                std::memcpy (&u, &f, 4);
                put32 (d, u);
            }
            else if (s.bits == 16) put16 (d, static_cast<uint16_t> (static_cast<int16_t> (std::lround (v * 32767.0))));
            else if (s.bits == 24)
            {
                const int32_t q = static_cast<int32_t> (std::lround (v * 8388607.0));
                d.push_back (static_cast<uint8_t> (q & 255)); d.push_back (static_cast<uint8_t> ((q >> 8) & 255)); d.push_back (static_cast<uint8_t> ((q >> 16) & 255));
            }
            else if (s.bits == 32) put32 (d, static_cast<uint32_t> (static_cast<int32_t> (std::lround (v * 2147483647.0))));
            else for (size_t k = 0; k < bytesPer; ++k) d.push_back (128);
        }
    std::vector<uint8_t> b;
    putTag (b, "RIFF");
    put32 (b, 0);
    putTag (b, "WAVE");
    putTag (b, "fmt ");
    put32 (b, 16);
    put16 (b, static_cast<uint32_t> (s.tag));
    put16 (b, static_cast<uint32_t> (s.channels));
    put32 (b, static_cast<uint32_t> (s.rate));
    put32 (b, static_cast<uint32_t> (s.rate * s.channels * static_cast<int> (bytesPer)));
    put16 (b, static_cast<uint32_t> (s.channels * static_cast<int> (bytesPer)));
    put16 (b, static_cast<uint32_t> (s.bits));
    auto chunk = [&b] (const char* id, const std::string& text)
    {
        putTag (b, id);
        put32 (b, static_cast<uint32_t> (text.size()));
        b.insert (b.end(), text.begin(), text.end());
        if (text.size() & 1u)
            b.push_back (0);
    };
    if (! s.clm.empty() && ! s.clmAfterData)
        chunk ("clm ", s.clm);
    if (! s.junk.empty())
        chunk ("LIST", s.junk);
    putTag (b, "data");
    put32 (b, static_cast<uint32_t> (d.size()) + (s.dataSizeTooBig ? 1000u : 0u));
    b.insert (b.end(), d.begin(), d.end());
    if (! s.clm.empty() && s.clmAfterData)
        chunk ("clm ", s.clm);
    const uint32_t riff = static_cast<uint32_t> (b.size() - 8);
    b[4] = static_cast<uint8_t> (riff & 255); b[5] = static_cast<uint8_t> ((riff >> 8) & 255); b[6] = static_cast<uint8_t> ((riff >> 16) & 255); b[7] = static_cast<uint8_t> ((riff >> 24) & 255);
    return b;
}

/** Frame f is a sine of f + 1 cycles over the frame of `frameSize` samples. */
std::vector<uint8_t> framesWav (int frames, int frameSize, const Spec& s)
{
    return makeWav (static_cast<size_t> (frames) * static_cast<size_t> (frameSize), s, [=] (size_t i)
    {
        const size_t f = i / static_cast<size_t> (frameSize), k = i % static_cast<size_t> (frameSize);
        return 0.5 * std::sin (2.0 * kPi * static_cast<double> (f + 1) * static_cast<double> (k) / frameSize);
    });
}
} // namespace

TEST (wavetable_file_the_convention_is_n_frames_of_2048_and_the_samples_are_those_of_the_reader)
{
    const auto wav = framesWav (5, 2048, Spec {});
    const WavetableParse p = parseWavetableWav (wav.data(), wav.size());
    CHECK (p.ok);
    CHECK_EQ (p.data.frames, 5);
    CHECK_EQ (p.data.sourceFrames, 5);
    CHECK_EQ (p.data.sourceFrameSamples, 2048);
    CHECK (! p.data.resampled && ! p.data.singleCycle && ! p.data.truncated && ! p.data.clmChunk);
    CHECK_EQ (p.data.samples.size(), static_cast<size_t> (5 * 2048));
    // 16 bit PCM is value / 32768: the engine's sample values (a preset that names such a file renders as before)
    const int16_t q = static_cast<int16_t> (std::lround (0.5 * std::sin (2.0 * kPi * 3.0 * 100.0 / 2048.0) * 32767.0));
    CHECK_EQ (p.data.samples[static_cast<size_t> (2 * 2048 + 100)], static_cast<float> (q) / 32768.0f);
}

TEST (wavetable_file_reads_16_24_32_bit_pcm_and_32_bit_float)
{
    for (int variant = 0; variant < 4; ++variant)
    {
        Spec s;
        s.bits = variant == 0 ? 16 : (variant == 1 ? 24 : 32);
        s.tag = variant == 3 ? 3 : 1;
        const auto wav = framesWav (2, 2048, s);
        const WavetableParse p = parseWavetableWav (wav.data(), wav.size());
        CHECK (p.ok);
        CHECK_EQ (p.data.bits, s.bits);
        CHECK_EQ (p.data.floating, s.tag == 3);
        CHECK_EQ (p.data.frames, 2);
        CHECK (std::fabs (p.data.samples[512] - 0.5f) < 0.001f);   // frame 0 is a sine: the quarter of the cycle is its peak
    }
}

TEST (wavetable_file_the_clm_chunk_gives_the_frame_size_and_other_sizes_are_resampled)
{
    Spec s;
    s.clm = "<!>1024 10000000 wavetable (created by test)";
    const auto wav = framesWav (6, 1024, s);
    const WavetableParse p = parseWavetableWav (wav.data(), wav.size());
    CHECK (p.ok);
    CHECK (p.data.clmChunk && p.data.resampled);
    CHECK_EQ (p.data.sourceFrameSamples, 1024);
    CHECK_EQ (p.data.frames, 6);
    CHECK_EQ (p.data.samples.size(), static_cast<size_t> (6 * 2048));
    CHECK (std::fabs (p.data.samples[512] - 0.5f) < 0.01f);   // frame 0: one sine cycle, its quarter still at the peak after the resampling

    // the same bytes without the chunk are 3 frames of 2048 (the convention): the chunk decides
    Spec none;
    const auto plain = framesWav (6, 1024, none);
    const WavetableParse q = parseWavetableWav (plain.data(), plain.size());
    CHECK (q.ok);
    CHECK_EQ (q.data.frames, 3);

    // a clm chunk of 2048 changes nothing but is reported; the chunk may stand after the data too
    Spec after;
    after.clm = "<!>2048 00000000 wavetable";
    after.clmAfterData = true;
    const auto w2 = framesWav (3, 2048, after);
    const WavetableParse r = parseWavetableWav (w2.data(), w2.size());
    CHECK (r.ok && r.data.clmChunk && ! r.data.resampled);
    CHECK_EQ (r.data.frames, 3);
}

TEST (wavetable_file_a_single_cycle_of_any_length_is_one_resampled_frame)
{
    for (int n : { 16, 600, 1024, 2047, 2049, 4095 })
    {
        const auto wav = makeWav (static_cast<size_t> (n), Spec {}, [n] (size_t i) { return 0.5 * std::sin (2.0 * kPi * static_cast<double> (i) / n); });
        const WavetableParse p = parseWavetableWav (wav.data(), wav.size());
        CHECK (p.ok);
        CHECK (p.data.singleCycle);
        CHECK_EQ (p.data.frames, 1);
        CHECK_EQ (p.data.sourceFrameSamples, n);
        CHECK_EQ (p.data.samples.size(), static_cast<size_t> (2048));
        if (n >= 64)
            CHECK (std::fabs (p.data.samples[512] - 0.5f) < 0.02f);   // a quarter of the cycle, whatever the length was
    }
    // exactly 2048 is one frame by the convention, not a resampled cycle
    const auto exact = framesWav (1, 2048, Spec {});
    const WavetableParse e = parseWavetableWav (exact.data(), exact.size());
    CHECK (e.ok && e.data.frames == 1 && ! e.data.resampled);
}

TEST (wavetable_file_more_than_256_frames_keep_the_first_256)
{
    const auto wav = framesWav (300, 2048, Spec {});
    const WavetableParse p = parseWavetableWav (wav.data(), wav.size());
    CHECK (p.ok);
    CHECK_EQ (p.data.frames, 256);
    CHECK_EQ (p.data.sourceFrames, 300);
    CHECK (p.data.truncated);
}

TEST (wavetable_file_a_stereo_file_gives_its_left_channel)
{
    Spec s;
    s.channels = 2;
    const auto wav = framesWav (2, 2048, s);
    const WavetableParse p = parseWavetableWav (wav.data(), wav.size());
    CHECK (p.ok && p.data.leftOfStereo);
    CHECK_EQ (p.data.frames, 2);
}

TEST (wavetable_file_refusals_say_why)
{
    // empty, not a WAV, a sample (long and not a multiple of 2048), too short, 8 bit, ADPCM, three channels, a file above the limit, a clm chunk with a silly size
    CHECK (! parseWavetableWav (nullptr, 0).ok);
    const std::vector<uint8_t> text { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'a', 'v', 'e', 't', 'a', 'b', 'l', 'e', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!' };
    const WavetableParse notWav = parseWavetableWav (text.data(), text.size());
    CHECK (! notWav.ok && notWav.reason == "not a WAV file");

    const auto sample = makeWav (44100, Spec {}, [] (size_t i) { return 0.3 * std::sin (0.05 * static_cast<double> (i)); });
    const WavetableParse longFile = parseWavetableWav (sample.data(), sample.size());
    CHECK (! longFile.ok && longFile.reason.find ("not a multiple of 2048") != std::string::npos);

    const auto tiny = makeWav (8, Spec {}, [] (size_t) { return 0.1; });
    CHECK (! parseWavetableWav (tiny.data(), tiny.size()).ok);

    Spec eight;
    eight.bits = 8;
    const auto w8 = makeWav (2048, eight, [] (size_t) { return 0.0; });
    const WavetableParse r8 = parseWavetableWav (w8.data(), w8.size());
    CHECK (! r8.ok && r8.reason.find ("8 bit") != std::string::npos);

    Spec adpcm;
    adpcm.tag = 2;
    const auto wa = makeWav (2048, adpcm, [] (size_t) { return 0.0; });
    CHECK (! parseWavetableWav (wa.data(), wa.size()).ok);

    Spec three;
    three.channels = 3;
    const auto w3 = makeWav (2048, three, [] (size_t) { return 0.0; });
    const WavetableParse r3 = parseWavetableWav (w3.data(), w3.size());
    CHECK (! r3.ok && r3.reason.find ("3 channels") != std::string::npos);

    std::vector<uint8_t> huge (kMaxWavetableFileBytes + 1, 0);
    const WavetableParse rh = parseWavetableWav (huge.data(), huge.size());
    CHECK (! rh.ok && rh.reason.find ("too large") != std::string::npos);

    Spec silly;
    silly.clm = "<!>9999 1 x";
    const auto ws = framesWav (2, 2048, silly);
    const WavetableParse rs = parseWavetableWav (ws.data(), ws.size());
    CHECK (! rs.ok && rs.reason.find ("clm chunk") != std::string::npos);

    CHECK_EQ (wavetableRefusalText ("x"), std::string ("not a wavetable file: x"));
}

TEST (wavetable_file_odd_chunks_do_not_crash_and_do_not_stop_the_read)
{
    // an unknown chunk of an odd size before the data (the pad byte), a clm chunk without "<!>", a data chunk that claims more than the file has, truncations at every length
    Spec s;
    s.junk = "abc";
    s.clm = "nothing we know";
    const auto wav = framesWav (2, 2048, s);
    const WavetableParse p = parseWavetableWav (wav.data(), wav.size());
    CHECK (p.ok);
    CHECK_EQ (p.data.frames, 2);
    CHECK (! p.data.clmChunk);

    Spec big;
    big.dataSizeTooBig = true;
    const auto lie = framesWav (2, 2048, big);
    CHECK (! parseWavetableWav (lie.data(), lie.size()).ok);   // damaged: refused, not read past the end

    for (size_t cut = 0; cut < wav.size(); cut += 97)
        (void) parseWavetableWav (wav.data(), cut);            // every prefix: no crash (the run under AddressSanitizer is the check)
    std::vector<uint8_t> noise (4096);
    for (size_t i = 0; i < noise.size(); ++i)
        noise[i] = static_cast<uint8_t> ((i * 131u + 7u) & 255u);
    std::memcpy (noise.data(), "RIFF", 4);
    std::memcpy (noise.data() + 8, "WAVE", 4);
    (void) parseWavetableWav (noise.data(), noise.size());
}

TEST (wavetable_file_the_engine_reads_a_table_file_with_the_same_rules_and_refuses_what_the_parser_refuses)
{
    // the store hands out a bounded read: a file above the limit is "too large", not read to its end
    MemoryContentStore store;
    std::vector<uint8_t> huge (kMaxWavetableFileBytes + 10, 1);
    store.add ("p", "huge.wav", huge);
    std::vector<uint8_t> bytes;
    bool tooLarge = false;
    ContentFileRef ref;
    ref.pack = "p";
    ref.file = "huge.wav";
    CHECK (! store.readFileBounded (ref, bytes, kMaxWavetableFileBytes, &tooLarge));
    CHECK (tooLarge);
    store.add ("p", "ok.wav", framesWav (2, 2048, Spec {}));
    ref.file = "ok.wav";
    CHECK (store.readFileBounded (ref, bytes, kMaxWavetableFileBytes, &tooLarge));
    CHECK (! tooLarge);
    CHECK (parseWavetableWav (bytes.data(), bytes.size()).ok);
}
