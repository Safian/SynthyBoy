// SynthyBoy - core/serum-import: FLAC and WAV decoders for the content a Serum 2 preset references (tables, samples, impulses, noise).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// FLAC after the format specification (RFC 9639): STREAMINFO, constant / verbatim / fixed / LPC subframes, Rice residuals, the three stereo
// decorrelation modes, wasted bits. CRCs are not verified. Output is float, interleaved, scaled to +-1.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "serum_internal.h"

namespace synthyboy::serumimport
{
namespace
{
struct Bits
{
    const uint8_t* p;
    size_t n;
    size_t pos = 0; // bit position
    bool bad = false;
    Bits (const uint8_t* d, size_t size) : p (d), n (size) {}
    uint32_t bit()
    {
        if ((pos >> 3) >= n) { bad = true; return 0; }
        const uint32_t v = (p[pos >> 3] >> (7 - (pos & 7))) & 1u;
        ++pos;
        return v;
    }
    uint64_t bits (int count)
    {
        uint64_t v = 0;
        for (int i = 0; i < count; ++i) v = (v << 1) | bit();
        return v;
    }
    int64_t sbits (int count)
    {
        if (count == 0) return 0;
        const uint64_t v = bits (count);
        const uint64_t sign = 1ull << (count - 1);
        return static_cast<int64_t> ((v ^ sign)) - static_cast<int64_t> (sign);
    }
    void align() { pos = (pos + 7) & ~size_t { 7 }; }
    uint32_t unary() // zeros before the first one
    {
        uint32_t q = 0;
        while (! bad && bit() == 0) { if (++q > (1u << 24)) { bad = true; break; } }
        return q;
    }
};

bool readResidual (Bits& br, int blockSize, int order, std::vector<int64_t>& res)
{
    const int method = static_cast<int> (br.bits (2));
    if (method > 1) return false;
    const int partOrder = static_cast<int> (br.bits (4));
    const int partitions = 1 << partOrder;
    if ((blockSize >> partOrder) < order || (blockSize & (partitions - 1)) != 0) return false;
    res.assign (static_cast<size_t> (blockSize - order), 0);
    size_t at = 0;
    for (int part = 0; part < partitions; ++part)
    {
        const int param = static_cast<int> (br.bits (method == 0 ? 4 : 5));
        const int escape = method == 0 ? 15 : 31;
        const int count = (blockSize >> partOrder) - (part == 0 ? order : 0);
        if (count < 0) return false;
        if (param == escape)
        {
            const int raw = static_cast<int> (br.bits (5));
            for (int i = 0; i < count; ++i) res[at++] = br.sbits (raw);
        }
        else
            for (int i = 0; i < count; ++i)
            {
                const uint64_t q = br.unary();
                const uint64_t v = (q << param) | br.bits (param);
                res[at++] = static_cast<int64_t> (v >> 1) ^ -static_cast<int64_t> (v & 1);
            }
        if (br.bad) return false;
    }
    return true;
}

bool readSubframe (Bits& br, int blockSize, int bps, std::vector<int64_t>& out)
{
    if (br.bit() != 0) return false;
    const int type = static_cast<int> (br.bits (6));
    int wasted = 0;
    if (br.bit()) { wasted = 1; while (br.bit() == 0 && ! br.bad) ++wasted; if (wasted > 32) return false; }
    const int sb = bps - wasted;
    if (sb <= 0 || sb > 33) return false;
    out.assign (static_cast<size_t> (blockSize), 0);
    if (type == 0)
    {
        const int64_t v = br.sbits (sb);
        for (auto& x : out) x = v;
    }
    else if (type == 1)
        for (auto& x : out) x = br.sbits (sb);
    else if (type >= 8 && type <= 12)
    {
        const int order = type - 8;
        if (order > blockSize) return false;
        for (int i = 0; i < order; ++i) out[static_cast<size_t> (i)] = br.sbits (sb);
        std::vector<int64_t> res;
        if (! readResidual (br, blockSize, order, res)) return false;
        for (int i = order; i < blockSize; ++i)
        {
            int64_t pred = 0;
            const size_t s = static_cast<size_t> (i);
            switch (order)
            {
                case 0: pred = 0; break;
                case 1: pred = out[s - 1]; break;
                case 2: pred = 2 * out[s - 1] - out[s - 2]; break;
                case 3: pred = 3 * out[s - 1] - 3 * out[s - 2] + out[s - 3]; break;
                default: pred = 4 * out[s - 1] - 6 * out[s - 2] + 4 * out[s - 3] - out[s - 4]; break;
            }
            out[s] = pred + res[static_cast<size_t> (i - order)];
        }
    }
    else if (type >= 32)
    {
        const int order = type - 31;
        if (order > blockSize) return false;
        for (int i = 0; i < order; ++i) out[static_cast<size_t> (i)] = br.sbits (sb);
        const int precision = static_cast<int> (br.bits (4)) + 1;
        if (precision == 16) return false;
        const int shift = static_cast<int> (br.sbits (5));
        if (shift < 0) return false;
        int64_t coef[32];
        for (int i = 0; i < order; ++i) coef[i] = br.sbits (precision);
        std::vector<int64_t> res;
        if (! readResidual (br, blockSize, order, res)) return false;
        for (int i = order; i < blockSize; ++i)
        {
            int64_t sum = 0;
            for (int j = 0; j < order; ++j) sum += coef[j] * out[static_cast<size_t> (i - 1 - j)];
            out[static_cast<size_t> (i)] = (sum >> shift) + res[static_cast<size_t> (i - order)];
        }
    }
    else
        return false;
    if (wasted)
        for (auto& x : out) x = static_cast<int64_t> (static_cast<uint64_t> (x) << wasted);
    return ! br.bad;
}

constexpr int kRates[16] = { 0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, 0, 0, 0, 0 };
} // namespace

bool decodeFlac (const uint8_t* d, size_t size, size_t maxFrames, DecodedAudio& out) noexcept
{
    try
    {
        out = DecodedAudio {};
        if (size < 42 || std::memcmp (d, "fLaC", 4) != 0) return false;
        size_t pos = 4;
        int infoRate = 0, infoCh = 0, infoBps = 0;
        bool last = false;
        while (! last)
        {
            if (size - pos < 4) return false;
            last = (d[pos] & 0x80) != 0;
            const int type = d[pos] & 0x7F;
            const size_t len = (static_cast<size_t> (d[pos + 1]) << 16) | (static_cast<size_t> (d[pos + 2]) << 8) | d[pos + 3];
            pos += 4;
            if (size - pos < len) return false;
            if (type == 0 && len >= 18)
            {
                const uint8_t* s = d + pos;
                infoRate = (s[10] << 12) | (s[11] << 4) | (s[12] >> 4);
                infoCh = ((s[12] >> 1) & 7) + 1;
                infoBps = (((s[12] & 1) << 4) | (s[13] >> 4)) + 1;
            }
            pos += len;
        }
        if (infoRate <= 0 || infoCh <= 0 || infoBps < 4) return false;
        out.sampleRate = infoRate;
        out.channels = infoCh;
        const double scale = 1.0 / std::ldexp (1.0, infoBps - 1);
        std::vector<std::vector<int64_t>> ch (static_cast<size_t> (infoCh));
        size_t frames = 0;
        while (pos + 6 < size)
        {
            Bits br (d + pos, size - pos);
            if (br.bits (14) != 0x3FFE) return false;
            br.bit();
            br.bit();
            const int bsCode = static_cast<int> (br.bits (4)), rateCode = static_cast<int> (br.bits (4));
            const int chCode = static_cast<int> (br.bits (4)), sizeCode = static_cast<int> (br.bits (3));
            br.bit();
            // UTF-8 coded number
            const uint32_t first = static_cast<uint32_t> (br.bits (8));
            int extra = first < 0x80 ? 0 : first < 0xC0 ? -1 : first < 0xE0 ? 1 : first < 0xF0 ? 2 : first < 0xF8 ? 3 : first < 0xFC ? 4 : first < 0xFE ? 5 : 6;
            if (extra < 0) return false;
            for (int i = 0; i < extra; ++i) br.bits (8);
            int blockSize = bsCode == 1 ? 192 : (bsCode >= 2 && bsCode <= 5) ? 576 << (bsCode - 2) : (bsCode >= 8) ? 256 << (bsCode - 8) : 0;
            if (bsCode == 6) blockSize = static_cast<int> (br.bits (8)) + 1;
            else if (bsCode == 7) blockSize = static_cast<int> (br.bits (16)) + 1;
            if (rateCode == 12) br.bits (8);
            else if (rateCode == 13 || rateCode == 14) br.bits (16);
            br.bits (8); // CRC-8
            if (blockSize <= 0 || br.bad) return false;
            int bps = infoBps;
            switch (sizeCode) { case 1: bps = 8; break; case 2: bps = 12; break; case 4: bps = 16; break; case 5: bps = 20; break; case 6: bps = 24; break; case 7: bps = 32; break; default: break; }
            (void) kRates;
            const int nch = chCode < 8 ? chCode + 1 : 2;
            if (chCode > 10 || nch != infoCh) return false;
            if (frames + static_cast<size_t> (blockSize) > maxFrames) return false;
            for (int c = 0; c < nch; ++c)
            {
                int sb = bps;
                if ((chCode == 8 && c == 1) || (chCode == 9 && c == 0) || (chCode == 10 && c == 1)) sb = bps + 1;
                if (! readSubframe (br, blockSize, sb, ch[static_cast<size_t> (c)])) return false;
            }
            br.align();
            br.bits (16); // CRC-16
            if (br.bad) return false;
            if (chCode == 8) for (int i = 0; i < blockSize; ++i) ch[1][static_cast<size_t> (i)] = ch[0][static_cast<size_t> (i)] - ch[1][static_cast<size_t> (i)];
            else if (chCode == 9) for (int i = 0; i < blockSize; ++i) ch[0][static_cast<size_t> (i)] += ch[1][static_cast<size_t> (i)];
            else if (chCode == 10)
                for (int i = 0; i < blockSize; ++i)
                {
                    const size_t s = static_cast<size_t> (i);
                    int64_t mid = ch[0][s], side = ch[1][s];
                    mid = static_cast<int64_t> (static_cast<uint64_t> (mid) << 1) | (side & 1);
                    ch[0][s] = (mid + side) >> 1;
                    ch[1][s] = (mid - side) >> 1;
                }
            const size_t base = out.data.size();
            out.data.resize (base + static_cast<size_t> (blockSize) * static_cast<size_t> (nch));
            for (int i = 0; i < blockSize; ++i)
                for (int c = 0; c < nch; ++c)
                    out.data[base + static_cast<size_t> (i) * static_cast<size_t> (nch) + static_cast<size_t> (c)] =
                        static_cast<float> (static_cast<double> (ch[static_cast<size_t> (c)][static_cast<size_t> (i)]) * scale);
            frames += static_cast<size_t> (blockSize);
            pos += br.pos >> 3;
        }
        return frames > 0;
    }
    catch (...)
    {
        return false;
    }
}

bool decodeWavFloat (const uint8_t* d, size_t size, size_t maxFrames, DecodedAudio& out) noexcept
{
    try
    {
        out = DecodedAudio {};
        if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0) return false;
        size_t pos = 12;
        int tag = 0, channels = 0, bits = 0, rate = 0;
        const uint8_t* data = nullptr;
        size_t dataLen = 0;
        while (size - pos >= 8)
        {
            const size_t len = static_cast<size_t> (d[pos + 4]) | (static_cast<size_t> (d[pos + 5]) << 8) | (static_cast<size_t> (d[pos + 6]) << 16) | (static_cast<size_t> (d[pos + 7]) << 24);
            const uint8_t* c = d + pos + 8;
            const size_t avail = std::min (len, size - pos - 8);
            if (std::memcmp (d + pos, "fmt ", 4) == 0 && avail >= 16)
            {
                tag = c[0] | (c[1] << 8);
                channels = c[2] | (c[3] << 8);
                rate = c[4] | (c[5] << 8) | (c[6] << 16) | (c[7] << 24);
                bits = c[14] | (c[15] << 8);
                if (tag == 0xFFFE && avail >= 26) tag = c[24] | (c[25] << 8);
            }
            else if (std::memcmp (d + pos, "data", 4) == 0)
            {
                data = c;
                dataLen = avail;
                break;
            }
            pos += 8 + len + (len & 1);
        }
        if (! data || channels <= 0 || channels > 8 || rate <= 0) return false;
        const int bytes = bits / 8;
        if (! ((tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (tag == 3 && (bits == 32 || bits == 64)))) return false;
        const size_t frames = dataLen / (static_cast<size_t> (bytes) * static_cast<size_t> (channels));
        if (frames == 0 || frames > maxFrames) return false;
        out.sampleRate = rate;
        out.channels = channels;
        out.data.resize (frames * static_cast<size_t> (channels));
        for (size_t i = 0; i < out.data.size(); ++i)
        {
            const uint8_t* s = data + i * static_cast<size_t> (bytes);
            float v = 0.0f;
            if (tag == 3 && bits == 32) { std::memcpy (&v, s, 4); }
            else if (tag == 3) { double dv; std::memcpy (&dv, s, 8); v = static_cast<float> (dv); }
            else if (bits == 8) v = (static_cast<int>(s[0]) - 128) / 128.0f;
            else if (bits == 16) v = static_cast<int16_t> (s[0] | (s[1] << 8)) / 32768.0f;
            else if (bits == 24) v = ((static_cast<int32_t> ((s[0] << 8) | (s[1] << 16) | (static_cast<uint32_t> (s[2]) << 24))) >> 8) / 8388608.0f;
            else v = static_cast<int32_t> (s[0] | (s[1] << 8) | (s[2] << 16) | (static_cast<uint32_t> (s[3]) << 24)) / 2147483648.0f;
            out.data[i] = std::isfinite (v) ? v : 0.0f;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// AIFF / AIFF-C with PCM samples (big endian; AIFC 'NONE', or 'sowt' = little endian): the Serum factory folders hold some .aif files (GROUPA1, WP-04)
bool decodeAiff (const uint8_t* d, size_t size, size_t maxFrames, DecodedAudio& out) noexcept
{
    try
    {
        out = DecodedAudio {};
        if (size < 12 || std::memcmp (d, "FORM", 4) != 0 || (std::memcmp (d + 8, "AIFF", 4) != 0 && std::memcmp (d + 8, "AIFC", 4) != 0)) return false;
        const bool aifc = std::memcmp (d + 8, "AIFC", 4) == 0;
        const auto be32 = [&] (size_t at) { return (static_cast<size_t> (d[at]) << 24) | (static_cast<size_t> (d[at + 1]) << 16) | (static_cast<size_t> (d[at + 2]) << 8) | static_cast<size_t> (d[at + 3]); };
        size_t pos = 12;
        int channels = 0, bits = 0;
        size_t frames = 0;
        double rate = 0.0;
        bool littleEndian = false, isFloat = false;
        const uint8_t* data = nullptr;
        size_t dataLen = 0;
        while (size - pos >= 8)
        {
            const size_t len = be32 (pos + 4);
            const uint8_t* c = d + pos + 8;
            const size_t avail = std::min (len, size - pos - 8);
            if (std::memcmp (d + pos, "COMM", 4) == 0 && avail >= 18)
            {
                channels = (c[0] << 8) | c[1];
                frames = be32 (pos + 8 + 2);
                bits = (c[6] << 8) | c[7];
                // the sample rate is an 80 bit extended float: sign / 15 bit exponent, 64 bit mantissa
                const int exponent = ((c[8] & 0x7F) << 8) | c[9];
                uint64_t mantissa = 0;
                for (int k = 0; k < 8; ++k) mantissa = (mantissa << 8) | c[10 + k];
                rate = std::ldexp (static_cast<double> (mantissa), exponent - 16383 - 63);
                if (aifc)
                {
                    if (avail < 22) return false;
                    if (std::memcmp (c + 18, "sowt", 4) == 0) littleEndian = true;
                    else if (std::memcmp (c + 18, "fl32", 4) == 0 || std::memcmp (c + 18, "FL32", 4) == 0) isFloat = true;   // 32 bit float (Serum's own files)
                    else if (std::memcmp (c + 18, "NONE", 4) != 0 && std::memcmp (c + 18, "twos", 4) != 0) return false;
                }
            }
            else if (std::memcmp (d + pos, "SSND", 4) == 0 && avail >= 8)
            {
                const size_t offset = be32 (pos + 8);
                if (8 + offset > avail) return false;
                data = c + 8 + offset;
                dataLen = avail - 8 - offset;
            }
            pos += 8 + len + (len & 1);
            if (pos > size) break;
        }
        if (! data || channels <= 0 || channels > 8 || rate < 1.0 || rate > 1.0e6) return false;
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;
        if (isFloat && bits != 32) return false;
        const size_t bytes = static_cast<size_t> (bits / 8);
        frames = std::min (frames, dataLen / (bytes * static_cast<size_t> (channels)));
        if (frames == 0 || frames > maxFrames) return false;
        out.sampleRate = static_cast<int> (std::lround (rate));
        out.channels = channels;
        out.data.resize (frames * static_cast<size_t> (channels));
        for (size_t i = 0; i < out.data.size(); ++i)
        {
            const uint8_t* s = data + i * bytes;
            uint32_t raw = 0;
            for (size_t k = 0; k < bytes; ++k)
                raw = (raw << 8) | s[littleEndian ? bytes - 1 - k : k];
            if (isFloat)
            {
                float f;
                std::memcpy (&f, &raw, 4);
                out.data[i] = std::isfinite (f) ? f : 0.0f;
                continue;
            }
            const int32_t v = static_cast<int32_t> (raw << (32 - bits)) >> (32 - bits);   // sign extended
            out.data[i] = static_cast<float> (v) / static_cast<float> (1ull << (bits - 1));
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool decodeAudio (const uint8_t* data, size_t size, size_t maxFrames, DecodedAudio& out) noexcept
{
    if (size >= 4 && std::memcmp (data, "FORM", 4) == 0) return decodeAiff (data, size, maxFrames, out);
    if (size >= 4 && std::memcmp (data, "fLaC", 4) == 0) return decodeFlac (data, size, maxFrames, out);
    return decodeWavFloat (data, size, maxFrames, out);
}
} // namespace synthyboy::serumimport
