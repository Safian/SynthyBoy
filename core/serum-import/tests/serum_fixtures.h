// SynthyBoy - tests of the Serum 2 importer: .SerumPreset fixtures built in code (nothing from Serum is committed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A fixture is a CBOR value tree written by hand and wrapped in the container of 19-serum-preset-format.md: header JSON, size, format 2, one zstd frame made of
// raw blocks (a valid frame without an encoder).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "synthyboy/serum_import.h"

namespace sbserum
{
using Bytes = std::vector<uint8_t>;

inline void head (Bytes& b, int major, uint64_t v)
{
    const uint8_t m = static_cast<uint8_t> (major << 5);
    if (v < 24) b.push_back (static_cast<uint8_t> (m | v));
    else if (v < 256) { b.push_back (m | 24); b.push_back (static_cast<uint8_t> (v)); }
    else if (v < 65536) { b.push_back (m | 25); b.push_back (static_cast<uint8_t> (v >> 8)); b.push_back (static_cast<uint8_t> (v)); }
    else { b.push_back (m | 26); for (int i = 3; i >= 0; --i) b.push_back (static_cast<uint8_t> (v >> (8 * i))); }
}

inline Bytes num (double v)
{
    Bytes b;
    if (v == std::floor (v) && std::fabs (v) < 2e9)
    {
        if (v >= 0) head (b, 0, static_cast<uint64_t> (v));
        else head (b, 1, static_cast<uint64_t> (-1.0 - v));
    }
    else
    {
        b.push_back (0xFB);
        uint64_t u;
        std::memcpy (&u, &v, 8);
        for (int i = 7; i >= 0; --i) b.push_back (static_cast<uint8_t> (u >> (8 * i)));
    }
    return b;
}
inline Bytes text (const std::string& s) { Bytes b; head (b, 3, s.size()); b.insert (b.end(), s.begin(), s.end()); return b; }
inline Bytes boolean (bool v) { return Bytes { static_cast<uint8_t> (v ? 0xF5 : 0xF4) }; }
inline Bytes arr (const std::vector<Bytes>& items) { Bytes b; head (b, 4, items.size()); for (const Bytes& i : items) b.insert (b.end(), i.begin(), i.end()); return b; }
inline Bytes nums (const std::vector<double>& v) { std::vector<Bytes> items; for (double d : v) items.push_back (num (d)); return arr (items); }
using Map = std::vector<std::pair<std::string, Bytes>>;
inline Bytes map (const Map& m) { Bytes b; head (b, 5, m.size()); for (const auto& kv : m) { const Bytes k = text (kv.first); b.insert (b.end(), k.begin(), k.end()); b.insert (b.end(), kv.second.begin(), kv.second.end()); } return b; }
inline Bytes dflt() { return text ("default"); }
inline Bytes plain (const std::vector<std::pair<std::string, double>>& v) { Map m; for (const auto& kv : v) m.push_back ({ kv.first, num (kv.second) }); return map (m); }

/** The container: header JSON + NUL-less length + body size + format 2 + a zstd frame of raw blocks. */
inline Bytes container (const std::string& fileType, const Bytes& body, const std::string& name = "Fixture", const std::string& extraHeader = "")
{
    std::string json = "{\"fileType\":\"" + fileType + "\",\"hash\":\"0\",\"presetAuthor\":\"Tester\",\"presetDescription\":\"fixture\",\"presetName\":\"" + name
                       + "\",\"product\":\"Serum2\",\"productVersion\":\"2.0.24\",\"tags\":[\"Wavetable\",\"Poly\"],\"url\":\"https://xferrecords.com/\",\"vendor\":\"Xfer Records\",\"version\":9.0"
                       + extraHeader + "}";
    Bytes out = { 'X', 'f', 'e', 'r', 'J', 's', 'o', 'n', 0 };
    for (int i = 0; i < 8; ++i) out.push_back (static_cast<uint8_t> (static_cast<uint64_t> (json.size()) >> (8 * i)));
    out.insert (out.end(), json.begin(), json.end());
    for (int i = 0; i < 4; ++i) out.push_back (static_cast<uint8_t> (body.size() >> (8 * i)));
    out.insert (out.end(), { 2, 0, 0, 0 });
    // zstd frame: magic, single segment, 4 byte content size, raw blocks of at most 128 KiB
    out.insert (out.end(), { 0x28, 0xB5, 0x2F, 0xFD, 0xA0 });
    for (int i = 0; i < 4; ++i) out.push_back (static_cast<uint8_t> (body.size() >> (8 * i)));
    size_t pos = 0;
    do
    {
        const size_t n = std::min<size_t> (body.size() - pos, 128u * 1024u);
        const bool last = pos + n >= body.size();
        const uint32_t bh = (last ? 1u : 0u) | (0u << 1) | (static_cast<uint32_t> (n) << 3);
        out.push_back (static_cast<uint8_t> (bh));
        out.push_back (static_cast<uint8_t> (bh >> 8));
        out.push_back (static_cast<uint8_t> (bh >> 16));
        out.insert (out.end(), body.begin() + static_cast<std::ptrdiff_t> (pos), body.begin() + static_cast<std::ptrdiff_t> (pos + n));
        pos += n;
    } while (pos < body.size());
    return out;
}

/** An in-memory content folder. */
struct MemResolver final : synthyboy::SerumContentResolver
{
    std::map<std::string, Bytes> files; // "Area/relPath"
    std::vector<std::string> asked;
    bool read (const std::string& area, const std::string& rel, std::vector<uint8_t>& bytes) override
    {
        asked.push_back (area + "/" + rel);
        const auto it = files.find (area + "/" + rel);
        if (it == files.end()) return false;
        bytes = it->second;
        return true;
    }
};

inline Bytes wavFloat (const std::vector<float>& s, int rate = 44100)
{
    Bytes w = { 'R', 'I', 'F', 'F' };
    const auto le32 = [&] (uint32_t x) { for (int i = 0; i < 4; ++i) w.push_back (static_cast<uint8_t> (x >> (8 * i))); };
    const auto le16 = [&] (uint16_t x) { w.push_back (static_cast<uint8_t> (x)); w.push_back (static_cast<uint8_t> (x >> 8)); };
    le32 (static_cast<uint32_t> (36 + s.size() * 4));
    for (char c : std::string ("WAVEfmt ")) w.push_back (static_cast<uint8_t> (c));
    le32 (16); le16 (3); le16 (1); le32 (static_cast<uint32_t> (rate)); le32 (static_cast<uint32_t> (rate * 4)); le16 (4); le16 (32);
    for (char c : std::string ("data")) w.push_back (static_cast<uint8_t> (c));
    le32 (static_cast<uint32_t> (s.size() * 4));
    const uint8_t* p = reinterpret_cast<const uint8_t*> (s.data());
    w.insert (w.end(), p, p + s.size() * 4);
    return w;
}

/** A preset body: the top level members of the file. Unset modules are simply absent (the importer treats them as Serum defaults). */
struct Preset
{
    Map top;
    Preset()
    {
        top.push_back ({ "mpeEnabled", boolean (false) });
        top.push_back ({ "mpePitchBendRange", num (48) });
    }
    Preset& set (const std::string& k, const Bytes& v) { top.push_back ({ k, v }); return *this; }
    Bytes body() const { return map (top); }
    Bytes file (const std::string& name = "Fixture") const { return container ("SerumPreset", body(), name); }
};

inline synthyboy::SerumImportResult run (const Bytes& file, synthyboy::SerumContentResolver* r = nullptr, bool multisampleOsc = true)
{
    synthyboy::SerumImportOptions o;
    o.resolver = r;
    o.multisampleOsc = multisampleOsc;
    return synthyboy::importSerumFile (file.data(), file.size(), o);
}

inline float value (const synthyboy::SerumImportResult& r, const char* key)
{
    const int idx = synthyboy::findParamIndex (synthyboy::hashKey (key));
    return idx < 0 ? -12345.0f : r.values.plain[static_cast<size_t> (idx)];
}
} // namespace sbserum
