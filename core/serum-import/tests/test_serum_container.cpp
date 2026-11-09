// SynthyBoy - tests of the Serum 2 container: zstd, zlib, CBOR, UBJSON, FLAC, WAV.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Hand-built fixtures (small, committed as code) plus a gated run over every real file when tests/serum-presets exists.
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "serum_internal.h"
#include "test_runner.h"

using namespace synthyboy::serumimport;

namespace
{
std::vector<uint8_t> readAll (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::vector<uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

std::string realDir()
{
    if (const char* e = std::getenv ("SB_SERUM_PRESETS_DIR")) return e;
    std::error_code ec;
    if (std::filesystem::is_directory (SB_SERUM_PRESETS_DIR, ec)) return SB_SERUM_PRESETS_DIR;
    return SB_SERUM_PRESETS_MAIN; // a worktree: the real files live in the main checkout
}
} // namespace

TEST (zstd_raw_rle_and_empty_frames)
{
    // frame: magic, FHD = single segment, FCS 1 byte = 5; one raw block "hello" (last)
    std::vector<uint8_t> f = { 0x28, 0xB5, 0x2F, 0xFD, 0x20, 5, 0x29, 0, 0, 'h', 'e', 'l', 'l', 'o' };
    std::vector<uint8_t> out;
    CHECK (zstdDecompress (f.data(), f.size(), 1000, out) == ZstdStatus::ok);
    CHECK (std::string (out.begin(), out.end()) == "hello");
    // RLE block: 7 x 'a'
    std::vector<uint8_t> g = { 0x28, 0xB5, 0x2F, 0xFD, 0x20, 7, 0x3B, 0, 0, 'a' };
    out.clear();
    CHECK (zstdDecompress (g.data(), g.size(), 1000, out) == ZstdStatus::ok);
    CHECK (out.size() == 7 && out[0] == 'a' && out[6] == 'a');
    // the output limit
    out.clear();
    CHECK (zstdDecompress (g.data(), g.size(), 3, out) == ZstdStatus::tooLarge);
    // truncated and garbage
    out.clear();
    CHECK (zstdDecompress (f.data(), f.size() - 2, 1000, out) != ZstdStatus::ok);
    const uint8_t junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK (zstdDecompress (junk, 8, 1000, out) == ZstdStatus::malformed);
}

TEST (zstd_compressed_block_with_predefined_tables)
{
    // made by the reference encoder (zstd 1.5.7, level 19, no checksum): one compressed block, raw literals, predefined FSE tables, repeat offsets
    const uint8_t f[] = { 0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x77, 0x05, 0x02, 0x00, 0x14, 0x03, 0x61, 0x62, 0x63, 0x20, 0x74, 0x68, 0x65, 0x20, 0x71, 0x75, 0x69,
                          0x63, 0x6b, 0x20, 0x62, 0x72, 0x6f, 0x77, 0x6e, 0x20, 0x66, 0x6f, 0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f, 0x76, 0x65,
                          0x72, 0x6c, 0x61, 0x7a, 0x79, 0x20, 0x64, 0x6f, 0x67, 0x3b, 0x61, 0x67, 0x61, 0x69, 0x6e, 0x20, 0x04, 0x00, 0x56, 0xcd, 0x05, 0xaa,
                          0x70, 0x16, 0x4a, 0x70, 0x7e, 0x6e, 0x08 };
    const std::string want = "abcabcabcabcabcabcabcabc the quick brown fox jumps over the lazy dog; the quick brown fox jumps again over the lazy dog";
    std::vector<uint8_t> out;
    CHECK (zstdDecompress (f, sizeof (f), 1000, out) == ZstdStatus::ok);
    CHECK (std::string (out.begin(), out.end()) == want);
    // a flipped bit anywhere must not crash
    for (size_t k = 8; k < sizeof (f); ++k)
    {
        std::vector<uint8_t> g (f, f + sizeof (f));
        g[k] ^= 0x10;
        std::vector<uint8_t> o2;
        const ZstdStatus s = zstdDecompress (g.data(), g.size(), 1000, o2);
        (void) s; // only that it returns
    }
    // truncation at every length
    for (size_t k = 0; k + 1 < sizeof (f); ++k)
    {
        std::vector<uint8_t> o3;
        CHECK (zstdDecompress (f, k, 1000, o3) != ZstdStatus::ok);
    }
}

TEST (cbor_basic_values_and_compact_arrays)
{
    // {"a":1,"b":[1.5,2.5],"c":"hi","d":[1,"x"],"e":-3,"f":true,"g":null}
    const uint8_t d[] = { 0xA7, 0x61, 'a', 0x01, 0x61, 'b', 0x82, 0xF9, 0x3E, 0x00, 0xF9, 0x41, 0x00, 0x61, 'c', 0x62, 'h', 'i', 0x61, 'd', 0x82, 0x01, 0x61, 'x',
                          0x61, 'e', 0x22, 0x61, 'f', 0xF5, 0x61, 'g', 0xF6 };
    cbor::Doc doc;
    CHECK (cbor::parse (d, sizeof (d), 8, 1u << 20, doc) == cbor::Status::ok);
    CHECK (doc.root().type == cbor::Type::map && doc.root().count == 7);
    const int a = doc.find (0, "a"), b = doc.find (0, "b"), c = doc.find (0, "c"), dd = doc.find (0, "d"), e = doc.find (0, "e");
    CHECK (a >= 0 && doc.number (static_cast<uint32_t> (a)) == 1.0);
    CHECK (b >= 0 && doc.arraySize (static_cast<uint32_t> (b)) == 2 && doc.arrayNumber (static_cast<uint32_t> (b), 0) == 1.5 && doc.arrayNumber (static_cast<uint32_t> (b), 1) == 2.5);
    CHECK (doc.nodes[static_cast<size_t> (b)].floatArray);
    CHECK (c >= 0 && doc.text (static_cast<uint32_t> (c)) == "hi");
    CHECK (dd >= 0 && doc.arraySize (static_cast<uint32_t> (dd)) == 2 && ! doc.nodes[static_cast<size_t> (dd)].floatArray && doc.arrayNumber (static_cast<uint32_t> (dd), 0) == 1.0);
    CHECK (e >= 0 && doc.number (static_cast<uint32_t> (e)) == -3.0);
    CHECK (doc.find (0, "zz") < 0);
}

TEST (cbor_rejects_truncation_depth_and_trailing_bytes)
{
    cbor::Doc doc;
    const uint8_t cut[] = { 0xA1, 0x61, 'a' };
    CHECK (cbor::parse (cut, sizeof (cut), 8, 1u << 20, doc) == cbor::Status::malformed);
    const uint8_t extra[] = { 0x01, 0x02 };
    CHECK (cbor::parse (extra, sizeof (extra), 8, 1u << 20, doc) == cbor::Status::malformed);
    std::vector<uint8_t> deep (200, 0x81);
    deep.push_back (0x00);
    CHECK (cbor::parse (deep.data(), deep.size(), 16, 1u << 20, doc) == cbor::Status::tooDeep);
    const uint8_t hugeLen[] = { 0x9B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK (cbor::parse (hugeLen, sizeof (hugeLen), 8, 1u << 20, doc) == cbor::Status::malformed);
    const uint8_t indefinite[] = { 0x9F, 0x01, 0xFF };
    CHECK (cbor::parse (indefinite, sizeof (indefinite), 8, 1u << 20, doc) == cbor::Status::malformed);
    CHECK (cbor::parse (extra, 1, 8, 4, doc) == cbor::Status::budget);
}

TEST (ubjson_object_with_double_array)
{
    // {"curveVals":[0.5,0.5],"fileType":"XferPath","isOpen":true,"n":3}
    std::vector<uint8_t> d = { '{', 'i', 9, 'c', 'u', 'r', 'v', 'e', 'V', 'a', 'l', 's', '[' };
    const uint8_t half[8] = { 0x3F, 0xE0, 0, 0, 0, 0, 0, 0 };
    for (int k = 0; k < 2; ++k) { d.push_back ('D'); d.insert (d.end(), half, half + 8); }
    d.push_back (']');
    const char key2[] = "fileType";
    d.push_back ('i'); d.push_back (8); d.insert (d.end(), key2, key2 + 8);
    d.push_back ('S'); d.push_back ('i'); d.push_back (8); const char v2[] = "XferPath"; d.insert (d.end(), v2, v2 + 8);
    d.push_back ('i'); d.push_back (6); const char key3[] = "isOpen"; d.insert (d.end(), key3, key3 + 6); d.push_back ('T');
    d.push_back ('i'); d.push_back (1); d.push_back ('n'); d.push_back ('i'); d.push_back (3);
    d.push_back ('}');
    cbor::Doc doc;
    CHECK (parseUbjson (d.data(), d.size(), 8, 1u << 20, doc) == cbor::Status::ok);
    const int cv = doc.find (0, "curveVals"), ft = doc.find (0, "fileType"), n = doc.find (0, "n");
    CHECK (cv >= 0 && doc.arraySize (static_cast<uint32_t> (cv)) == 2 && doc.arrayNumber (static_cast<uint32_t> (cv), 1) == 0.5);
    CHECK (ft >= 0 && doc.text (static_cast<uint32_t> (ft)) == "XferPath");
    CHECK (n >= 0 && doc.number (static_cast<uint32_t> (n)) == 3.0);
    d.pop_back();
    CHECK (parseUbjson (d.data(), d.size(), 8, 1u << 20, doc) != cbor::Status::ok);
}

TEST (inflate_stored_and_fixed_blocks)
{
    // zlib, stored block "abc"
    const uint8_t stored[] = { 0x78, 0x01, 0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c', 0x02, 0x4D, 0x01, 0x27 };
    std::vector<uint8_t> out;
    CHECK (inflateZlib (stored, sizeof (stored), 100, out));
    CHECK (std::string (out.begin(), out.end()) == "abc");
    // zlib, fixed Huffman "aaaaaaaaaa" (python zlib.compress (b"a" * 10, 1))
    const uint8_t fixed[] = { 0x78, 0x01, 0x4B, 0x4C, 0x84, 0x01, 0x00, 0x14, 0xE1, 0x03, 0xCB };
    out.clear();
    CHECK (inflateZlib (fixed, sizeof (fixed), 100, out));
    CHECK (out.size() == 10 && out[9] == 'a');
    out.clear();
    CHECK (! inflateZlib (fixed, sizeof (fixed), 5, out));
    CHECK (! inflateZlib (fixed, 5, 100, out));
}

TEST (container_rejects_foreign_and_cut_files)
{
    Container c;
    const char* text = "not a serum file at all";
    CHECK (openContainer (reinterpret_cast<const uint8_t*> (text), std::strlen (text), {}, c) == ContainerStatus::notSerum);
    std::vector<uint8_t> hdr = { 'X', 'f', 'e', 'r', 'J', 's', 'o', 'n', 0, 40, 0, 0, 0, 0, 0, 0, 0, '{' };
    CHECK (openContainer (hdr.data(), hdr.size(), {}, c) == ContainerStatus::truncated);
}

TEST (wav_float_and_pcm_decode)
{
    auto le32 = [] (std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back (static_cast<uint8_t> (x >> (8 * i))); };
    auto le16 = [] (std::vector<uint8_t>& v, uint16_t x) { v.push_back (static_cast<uint8_t> (x)); v.push_back (static_cast<uint8_t> (x >> 8)); };
    std::vector<uint8_t> w = { 'R', 'I', 'F', 'F' };
    le32 (w, 36 + 8);
    for (char c : std::string ("WAVEfmt ")) w.push_back (static_cast<uint8_t> (c));
    le32 (w, 16); le16 (w, 3); le16 (w, 1); le32 (w, 44100); le32 (w, 44100 * 4); le16 (w, 4); le16 (w, 32);
    for (char c : std::string ("data")) w.push_back (static_cast<uint8_t> (c));
    le32 (w, 8);
    const float s[2] = { 0.25f, -0.5f };
    const uint8_t* sp = reinterpret_cast<const uint8_t*> (s);
    w.insert (w.end(), sp, sp + 8);
    DecodedAudio a;
    CHECK (decodeWavFloat (w.data(), w.size(), 100, a));
    CHECK (a.sampleRate == 44100 && a.channels == 1 && a.frames() == 2 && a.data[0] == 0.25f && a.data[1] == -0.5f);
    CHECK (! decodeWavFloat (w.data(), w.size(), 1, a));
    CHECK (! decodeFlac (w.data(), w.size(), 100, a));
}

TEST (aiff_pcm_and_sowt_decode)
{
    // a 16 bit stereo AIFF of two frames (44100 Hz as the 80 bit extended float 0x400E AC44 0000 0000 0000), then the same as AIFF-C 'sowt' (little endian samples)
    const auto be32 = [] (std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back (static_cast<uint8_t> (x >> (8 * i))); };
    const auto be16 = [] (std::vector<uint8_t>& v, uint16_t x) { v.push_back (static_cast<uint8_t> (x >> 8)); v.push_back (static_cast<uint8_t> (x)); };
    const auto tag = [] (std::vector<uint8_t>& v, const char* t) { for (int i = 0; i < 4; ++i) v.push_back (static_cast<uint8_t> (t[i])); };
    const auto build = [&] (bool aifc, bool little)
    {
        std::vector<uint8_t> a;
        tag (a, "FORM");
        be32 (a, 0);
        tag (a, aifc ? "AIFC" : "AIFF");
        tag (a, "COMM");
        be32 (a, aifc ? 24 : 18);
        be16 (a, 2); be32 (a, 2); be16 (a, 16);
        for (const uint8_t b : { 0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }) a.push_back (b);
        if (aifc) { tag (a, little ? "sowt" : "NONE"); be16 (a, 0); }
        tag (a, "SSND");
        be32 (a, 8 + 8);
        be32 (a, 0); be32 (a, 0);
        const int16_t samples[4] = { 16384, -16384, 8192, -32768 };
        for (const int16_t v : samples)
        {
            const uint16_t u = static_cast<uint16_t> (v);
            if (little) { a.push_back (static_cast<uint8_t> (u)); a.push_back (static_cast<uint8_t> (u >> 8)); }
            else be16 (a, u);
        }
        return a;
    };
    for (const auto& file : { build (false, false), build (true, false), build (true, true) })
    {
        DecodedAudio a;
        CHECK (decodeAudio (file.data(), file.size(), 100, a));
        CHECK (a.sampleRate == 44100 && a.channels == 2 && a.frames() == 2);
        CHECK (a.data.size() == 4 && a.data[0] == 0.5f && a.data[1] == -0.5f && a.data[2] == 0.25f && a.data[3] == -1.0f);
        CHECK (! decodeAudio (file.data(), file.size(), 1, a));
    }
    {
        // AIFF-C 'fl32': big endian 32 bit float (the Serum factory noises)
        std::vector<uint8_t> f;
        tag (f, "FORM"); be32 (f, 0); tag (f, "AIFC"); tag (f, "COMM"); be32 (f, 24);
        be16 (f, 1); be32 (f, 2); be16 (f, 32);
        for (const uint8_t b : { 0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }) f.push_back (b);
        tag (f, "fl32"); be16 (f, 0);
        tag (f, "SSND"); be32 (f, 16); be32 (f, 0); be32 (f, 0);
        be32 (f, 0x3E800000u); be32 (f, 0xBF000000u); // 0.25, -0.5
        DecodedAudio a;
        CHECK (decodeAudio (f.data(), f.size(), 100, a));
        CHECK (a.channels == 1 && a.frames() == 2 && a.data[0] == 0.25f && a.data[1] == -0.5f);
    }
    std::vector<uint8_t> bad = build (true, false);
    bad[bad.size() - 40] = 'X'; // a damaged chunk header must not crash
    DecodedAudio a;
    (void) decodeAudio (bad.data(), bad.size(), 100, a);
}

TEST (real_files_container_decodes_every_family)
{
    namespace fs = std::filesystem;
    const fs::path root = realDir();
    std::error_code ec;
    if (! fs::is_directory (root, ec))
    {
        std::printf ("  (skipped: %s does not exist)\n", root.string().c_str());
        return;
    }
    int files = 0, bad = 0;
    for (const auto& e : fs::recursive_directory_iterator (root, fs::directory_options::skip_permission_denied, ec))
    {
        if (! e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".SerumPreset" && ext != ".SerumFX" && ext != ".SerumFXRack" && ext != ".XferShape" && ext != ".XferPath" && ext != ".XferClip" && ext != ".XferArp"
            && ext != ".SerumStyle" && ext != ".XferFilterPZSingle" && ext != ".XferFilterPZMulti" && ext != ".XferArpBank" && ext != ".XferClipBank" && ext != ".SerumMIDIMap")
            continue;
        ++files;
        const auto bytes = readAll (e.path());
        Container c;
        const ContainerStatus s = openContainer (bytes.data(), bytes.size(), {}, c);
        if (s != ContainerStatus::ok)
        {
            ++bad;
            std::printf ("  FAIL %s: %s\n", e.path().string().c_str(), containerStatusText (s));
        }
    }
    std::printf ("  %d files, %d failed\n", files, bad);
    CHECK (files > 0);
    CHECK (bad == 0);
}
