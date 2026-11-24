// SynthyBoy - tests of the zip reader and of the batch / .vitalbank import (docs/ui-design/11-vital-preset-import.md 2.6, Q5).
// The archives are built in the test (stored and deflate entries); one deflate stream was made by zlib (dynamic Huffman blocks). The real
// bank in tests/vital-presets/ (gitignored) is optional.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "fixtures.h"
#include "synthyboy/preset.h"
#include "synthyboy/vital_import.h"
#include "synthyboy/zip_reader.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
using sbfx::Fixture;

namespace
{
// ---- a deflate stream of zlib (level 9, 4444 bytes of 500 words, dynamic Huffman) ----------------------------------------------
const uint8_t kZlibStream[] = {
    85, 88, 89, 118, 28, 55, 12, 188, 74, 31, 129, 216, 184, 28, 103, 108, 143, 35, 69, 178, 199, 79, 81, 252,
    114, 252, 128, 68, 129, 28, 127, 141, 220, 221, 196, 90, 40, 20, 253, 246, 250, 254, 24, 173, 92, 247, 175, 47,
    15, 45, 122, 125, 249, 184, 253, 126, 52, 189, 190, 221, 223, 63, 111, 210, 240, 96, 8, 93, 127, 61, 222, 191,
    75, 191, 190, 190, 220, 62, 222, 95, 239, 170, 122, 253, 124, 252, 190, 255, 248, 114, 255, 104, 116, 189, 60, 62,
    239, 239, 131, 247, 163, 90, 194, 194, 52, 189, 222, 85, 181, 48, 101, 163, 92, 63, 94, 223, 238, 134, 23, 218,
    150, 111, 30, 117, 31, 38, 127, 182, 78, 91, 215, 235, 239, 127, 221, 219, 167, 53, 185, 190, 63, 254, 251, 252,
    120, 124, 82, 49, 196, 66, 248, 202, 157, 33, 42, 107, 45, 188, 84, 145, 245, 145, 149, 126, 194, 28, 118, 189,
    121, 186, 218, 234, 245, 248, 231, 235, 237, 67, 60, 184, 48, 207, 166, 105, 190, 145, 69, 96, 93, 210, 183, 180,
    235, 215, 237, 215, 173, 123, 136, 243, 124, 83, 156, 231, 161, 233, 152, 184, 156, 240, 107, 95, 223, 145, 217, 58,
    167, 94, 188, 168, 98, 183, 252, 190, 117, 94, 223, 136, 246, 235, 253, 245, 199, 173, 122, 156, 243, 91, 27, 18,
    182, 219, 206, 105, 212, 118, 189, 254, 252, 246, 122, 211, 115, 188, 50, 98, 171, 254, 23, 98, 161, 85, 214, 81,
    194, 32, 195, 142, 212, 93, 184, 202, 134, 130, 21, 244, 130, 57, 115, 36, 97, 180, 163, 180, 176, 35, 17, 144,
    199, 137, 227, 106, 35, 26, 231, 46, 87, 60, 195, 225, 50, 91, 215, 229, 212, 184, 123, 199, 214, 203, 230, 166,
    243, 161, 84, 216, 236, 232, 57, 217, 41, 92, 15, 236, 177, 23, 57, 222, 113, 248, 53, 74, 199, 92, 7, 162,
    12, 127, 202, 35, 106, 198, 186, 106, 56, 154, 173, 231, 173, 160, 249, 10, 127, 153, 139, 22, 137, 196, 213, 171,
    59, 31, 212, 0, 135, 247, 105, 254, 232, 113, 69, 20, 109, 169, 132, 10, 145, 231, 120, 123, 255, 245, 226, 80,
    140, 56, 77, 71, 34, 155, 16, 69, 143, 47, 26, 135, 81, 255, 247, 244, 65, 150, 101, 106, 104, 49, 213, 245,
    59, 219, 184, 44, 16, 69, 137, 181, 5, 190, 212, 241, 25, 121, 246, 152, 137, 233, 124, 1, 206, 58, 16, 224,
    179, 131, 72, 45, 221, 50, 149, 101, 150, 124, 160, 34, 14, 135, 55, 192, 97, 123, 92, 27, 33, 26, 147, 248,
    124, 200, 54, 37, 85, 163, 244, 166, 49, 32, 213, 48, 208, 149, 219, 202, 169, 243, 158, 137, 168, 104, 51, 116,
    139, 61, 196, 25, 251, 180, 17, 225, 100, 168, 62, 62, 235, 15, 26, 145, 190, 250, 145, 229, 184, 197, 111, 7,
    224, 102, 146, 129, 188, 14, 142, 209, 152, 111, 29, 48, 57, 179, 94, 39, 104, 167, 211, 13, 12, 53, 137, 105,
    6, 212, 75, 28, 214, 62, 54, 94, 53, 170, 215, 163, 31, 234, 92, 182, 66, 175, 180, 139, 162, 27, 220, 212,
    2, 91, 30, 254, 252, 169, 30, 212, 26, 159, 153, 249, 122, 192, 117, 229, 89, 157, 135, 162, 218, 35, 190, 167,
    136, 113, 198, 22, 179, 37, 241, 98, 122, 95, 165, 67, 54, 89, 23, 166, 150, 180, 66, 18, 137, 39, 23, 112,
    181, 51, 71, 166, 240, 27, 69, 163, 17, 241, 212, 246, 52, 107, 206, 67, 1, 201, 248, 214, 164, 34, 152, 77,
    34, 142, 168, 61, 252, 2, 120, 140, 248, 154, 90, 156, 214, 174, 73, 163, 35, 48, 47, 206, 157, 107, 4, 115,
    108, 70, 238, 133, 73, 147, 145, 140, 96, 81, 12, 240, 188, 101, 128, 230, 195, 8, 24, 158, 93, 97, 18, 184,
    2, 156, 90, 25, 0, 73, 13, 79, 243, 116, 148, 194, 231, 46, 134, 38, 203, 231, 220, 190, 236, 146, 62, 17,
    44, 71, 43, 213, 24, 132, 12, 4, 157, 245, 116, 202, 220, 11, 35, 11, 138, 222, 14, 212, 148, 188, 124, 127,
    240, 216, 24, 240, 197, 140, 146, 76, 2, 153, 241, 81, 173, 0, 235, 83, 139, 140, 99, 22, 196, 183, 85, 62,
    227, 18, 80, 21, 135, 75, 130, 76, 130, 173, 60, 197, 25, 171, 57, 55, 45, 39, 147, 117, 86, 161, 100, 68,
    117, 140, 147, 230, 140, 15, 61, 230, 54, 26, 3, 172, 209, 17, 53, 123, 125, 99, 171, 120, 28, 201, 148, 232,
    105, 119, 159, 27, 37, 53, 139, 77, 182, 25, 108, 38, 213, 188, 68, 139, 62, 9, 204, 208, 247, 228, 200, 94,
    52, 156, 237, 172, 126, 42, 135, 207, 119, 221, 58, 209, 17, 91, 5, 182, 123, 79, 158, 77, 54, 120, 174, 205,
    80, 124, 86, 5, 200, 67, 219, 198, 22, 11, 131, 15, 231, 35, 59, 234, 49, 63, 195, 82, 10, 84, 221, 139,
    89, 131, 199, 167, 118, 8, 240, 209, 49, 176, 105, 134, 65, 76, 86, 97, 49, 1, 234, 132, 185, 36, 2, 70,
    155, 189, 33, 1, 181, 39, 205, 80, 91, 172, 35, 195, 242, 27, 35, 157, 183, 130, 89, 144, 32, 77, 246, 105,
    89, 89, 145, 46, 199, 22, 213, 166, 221, 202, 170, 40, 155, 234, 222, 55, 182, 97, 82, 59, 136, 204, 219, 129,
    60, 55, 63, 148, 189, 255, 144, 39, 29, 150, 155, 13, 138, 69, 31, 198, 165, 197, 76, 12, 136, 29, 246, 44,
    33, 62, 122, 138, 29, 6, 135, 249, 154, 65, 151, 98, 45, 246, 208, 43, 140, 146, 207, 105, 95, 239, 7, 108,
    119, 217, 225, 78, 137, 182, 54, 105, 129, 249, 185, 99, 195, 88, 217, 235, 39, 27, 220, 163, 223, 147, 59, 23,
    3, 131, 239, 26, 116, 21, 217, 174, 169, 87, 6, 200, 216, 162, 205, 226, 209, 36, 250, 32, 55, 197, 44, 102,
    244, 78, 226, 203, 186, 129, 42, 59, 230, 93, 186, 37, 37, 65, 127, 85, 229, 4, 51, 22, 156, 21, 128, 31,
    132, 217, 233, 204, 109, 61, 189, 105, 71, 183, 129, 177, 248, 172, 214, 94, 115, 34, 181, 0, 17, 9, 71, 215,
    89, 145, 240, 58, 51, 36, 85, 91, 235, 150, 50, 53, 214, 86, 139, 165, 33, 130, 89, 173, 53, 233, 43, 135,
    5, 178, 168, 198, 207, 236, 250, 74, 178, 149, 208, 48, 213, 97, 179, 94, 140, 60, 168, 9, 140, 41, 47, 64,
    128, 16, 206, 180, 71, 121, 88, 132, 182, 217, 82, 235, 25, 115, 1, 165, 79, 103, 51, 13, 166, 64, 124, 123,
    66, 65, 210, 181, 127, 178, 32, 37, 249, 160, 96, 31, 54, 46, 208, 163, 49, 101, 115, 147, 45, 177, 147, 120,
    161, 138, 166, 12, 140, 90, 29, 201, 144, 100, 27, 23, 169, 252, 66, 66, 205, 214, 46, 133, 205, 65, 157, 20,
    120, 130, 238, 238, 152, 85, 226, 80, 100, 77, 160, 140, 198, 150, 187, 108, 144, 144, 53, 40, 124, 236, 172, 165,
    30, 234, 108, 45, 129, 159, 247, 32, 238, 121, 51, 217, 2, 220, 160, 95, 205, 114, 186, 227, 150, 64, 35, 120,
    96, 110, 209, 77, 129, 5, 220, 211, 33, 91, 134, 24, 66, 220, 53, 181, 179, 231, 180, 18, 160, 189, 9, 96,
    180, 188, 144, 9, 182, 120, 232, 47, 133, 22, 230, 51, 229, 149, 14, 239, 47, 176, 75, 222, 49, 90, 217, 245,
    73, 117, 112, 200, 223, 187, 187, 88, 17, 128, 179, 10, 22, 154, 187, 122, 85, 185, 101, 123, 36, 84, 234, 188,
    126, 45, 216, 159, 13, 65, 173, 99, 217, 157, 186, 50, 198, 86, 26, 174, 51, 157, 3, 186, 19, 42, 65, 200,
    245, 44, 11, 121, 186, 75, 54, 200, 245, 217, 245, 24, 86, 5, 149, 27, 149, 220, 93, 33, 50, 41, 109, 226,
    22, 50, 119, 126, 120, 77, 229, 95, 65, 242, 170, 217, 193, 185, 32, 3, 137, 146, 92, 38, 65, 132, 3, 67,
    181, 175, 98, 90, 160, 16, 38, 187, 198, 108, 164, 16, 101, 142, 17, 158, 151, 190, 152, 182, 154, 18, 18, 45,
    35, 220, 115, 176, 216, 106, 205, 203, 231, 188, 22, 174, 54, 107, 110, 226, 39, 173, 51, 21, 81, 220, 22, 202,
    214, 173, 82, 66, 71, 201, 64, 41, 57, 47, 3, 84, 246, 244, 195, 214, 232, 49, 28, 211, 29, 100, 54, 26,
    97, 249, 132, 159, 16, 207, 185, 125, 12, 90, 114, 202, 247, 85, 139, 114, 238, 231, 41, 237, 89, 158, 160, 173,
    13, 115, 30, 184, 17, 92, 93, 228, 8, 38, 105, 1, 122, 46, 156, 162, 57, 87, 69, 59, 139, 112, 222, 103,
    162, 248, 178, 217, 234, 15, 86, 101, 6, 223, 78, 133, 137, 219, 43, 17, 216, 113, 95, 227, 7, 225, 134, 162,
    79, 255, 177, 48, 25, 56, 198, 167, 97, 171, 104, 40, 73, 195, 198, 75, 154, 1, 61, 55, 78, 205, 144, 151,
    8, 175, 89, 220, 200, 32, 89, 250, 94, 229, 34, 209, 148, 185, 155, 99, 33, 243, 255,
};
constexpr size_t kZlibPlainSize = 4444;
constexpr uint32_t kZlibPlainCrc = 0x621fdd5cu;

// ---- a small deflate encoder (fixed Huffman, greedy matches) and zip writer, test side only ----------------------------------------
struct BitWriter
{
    std::vector<uint8_t> bytes;
    uint32_t acc = 0;
    int n = 0;
    void put (uint32_t value, int bits) // LSB first
    {
        for (int i = 0; i < bits; ++i)
        {
            acc |= ((value >> i) & 1u) << n;
            if (++n == 8)
            {
                bytes.push_back (static_cast<uint8_t> (acc));
                acc = 0;
                n = 0;
            }
        }
    }
    void putCode (uint32_t code, int bits) // Huffman codes go MSB first
    {
        for (int i = bits - 1; i >= 0; --i)
            put ((code >> i) & 1u, 1);
    }
    void flush()
    {
        if (n > 0)
        {
            bytes.push_back (static_cast<uint8_t> (acc));
            acc = 0;
            n = 0;
        }
    }
};

void putLiteralLength (BitWriter& w, int sym)
{
    if (sym < 144) w.putCode (0x30u + static_cast<uint32_t> (sym), 8);
    else if (sym < 256) w.putCode (0x190u + static_cast<uint32_t> (sym - 144), 9);
    else if (sym < 280) w.putCode (static_cast<uint32_t> (sym - 256), 7);
    else w.putCode (0xC0u + static_cast<uint32_t> (sym - 280), 8);
}

std::vector<uint8_t> deflateFixed (const std::string& in)
{
    static const int lenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const int lenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const int distBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const int distExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

    BitWriter w;
    w.put (1, 1); // final
    w.put (1, 2); // fixed Huffman
    std::vector<int> head (1 << 15, -1);
    const auto hash = [&] (size_t i)
    {
        return static_cast<size_t> (((static_cast<unsigned char> (in[i]) << 10) ^ (static_cast<unsigned char> (in[i + 1]) << 5) ^ static_cast<unsigned char> (in[i + 2])) & 0x7fff);
    };
    size_t i = 0;
    while (i < in.size())
    {
        int bestLen = 0, bestDist = 0;
        if (i + 3 <= in.size())
        {
            const int cand = head[hash (i)];
            if (cand >= 0 && i - static_cast<size_t> (cand) <= 32768)
            {
                int len = 0;
                while (i + static_cast<size_t> (len) < in.size() && len < 258 && in[static_cast<size_t> (cand) + static_cast<size_t> (len)] == in[i + static_cast<size_t> (len)])
                    ++len;
                if (len >= 3)
                {
                    bestLen = len;
                    bestDist = static_cast<int> (i) - cand;
                }
            }
            head[hash (i)] = static_cast<int> (i);
        }
        if (bestLen >= 3)
        {
            int lc = 28;
            while (lenBase[lc] > bestLen) --lc;
            putLiteralLength (w, 257 + lc);
            w.put (static_cast<uint32_t> (bestLen - lenBase[lc]), lenExtra[lc]);
            int dc = 29;
            while (distBase[dc] > bestDist) --dc;
            w.putCode (static_cast<uint32_t> (dc), 5);
            w.put (static_cast<uint32_t> (bestDist - distBase[dc]), distExtra[dc]);
            for (int k = 1; k < bestLen && i + static_cast<size_t> (k) + 3 <= in.size(); ++k)
                head[hash (i + static_cast<size_t> (k))] = static_cast<int> (i) + k;
            i += static_cast<size_t> (bestLen);
        }
        else
        {
            putLiteralLength (w, static_cast<unsigned char> (in[i]));
            ++i;
        }
    }
    putLiteralLength (w, 256);
    w.flush();
    return w.bytes;
}

std::vector<uint8_t> deflateStoredBlocks (const std::string& in)
{
    std::vector<uint8_t> out;
    size_t pos = 0;
    do
    {
        const size_t n = std::min<size_t> (in.size() - pos, 65535);
        const bool last = pos + n >= in.size();
        out.push_back (last ? 1 : 0);
        out.push_back (static_cast<uint8_t> (n & 0xff));
        out.push_back (static_cast<uint8_t> (n >> 8));
        out.push_back (static_cast<uint8_t> (~n & 0xff));
        out.push_back (static_cast<uint8_t> ((~n >> 8) & 0xff));
        out.insert (out.end(), in.begin() + static_cast<long> (pos), in.begin() + static_cast<long> (pos + n));
        pos += n;
    } while (pos < in.size());
    return out;
}

struct ZipFile
{
    struct Item
    {
        std::string name;
        std::vector<uint8_t> data;
        uint16_t method = 0;
        uint32_t crc = 0, plainSize = 0;
        uint16_t flags = 0;
        uint32_t offset = 0;
    };
    std::vector<Item> items;
    std::string comment;

    /** method 0 stored, 8 deflate (fixed Huffman), 9 deflate made of stored blocks. */
    void add (const std::string& name, const std::string& content, uint16_t method)
    {
        Item it;
        it.name = name;
        it.method = method == 0 ? 0 : 8;
        it.crc = crc32Of (content.data(), content.size());
        it.plainSize = static_cast<uint32_t> (content.size());
        if (method == 0) it.data.assign (content.begin(), content.end());
        else if (method == 8) it.data = deflateFixed (content);
        else it.data = deflateStoredBlocks (content);
        items.push_back (std::move (it));
    }
    void addRaw (const std::string& name, std::vector<uint8_t> data, uint16_t method, uint32_t crc, uint32_t plainSize)
    {
        Item it;
        it.name = name;
        it.data = std::move (data);
        it.method = method;
        it.crc = crc;
        it.plainSize = plainSize;
        items.push_back (std::move (it));
    }

    static void p16 (std::vector<uint8_t>& o, uint32_t v) { o.push_back (static_cast<uint8_t> (v)); o.push_back (static_cast<uint8_t> (v >> 8)); }
    static void p32 (std::vector<uint8_t>& o, uint32_t v) { p16 (o, v & 0xffff); p16 (o, v >> 16); }

    std::vector<uint8_t> build()
    {
        std::vector<uint8_t> o;
        for (auto& it : items)
        {
            it.offset = static_cast<uint32_t> (o.size());
            p32 (o, 0x04034b50u); p16 (o, 20); p16 (o, it.flags); p16 (o, it.method); p16 (o, 0); p16 (o, 0x21);
            p32 (o, it.crc); p32 (o, static_cast<uint32_t> (it.data.size())); p32 (o, it.plainSize);
            p16 (o, static_cast<uint32_t> (it.name.size())); p16 (o, 0);
            o.insert (o.end(), it.name.begin(), it.name.end());
            o.insert (o.end(), it.data.begin(), it.data.end());
        }
        const uint32_t cdStart = static_cast<uint32_t> (o.size());
        for (const auto& it : items)
        {
            p32 (o, 0x02014b50u); p16 (o, 20); p16 (o, 20); p16 (o, it.flags); p16 (o, it.method); p16 (o, 0); p16 (o, 0x21);
            p32 (o, it.crc); p32 (o, static_cast<uint32_t> (it.data.size())); p32 (o, it.plainSize);
            p16 (o, static_cast<uint32_t> (it.name.size())); p16 (o, 0); p16 (o, 0); p16 (o, 0); p16 (o, 0); p32 (o, 0); p32 (o, it.offset);
            o.insert (o.end(), it.name.begin(), it.name.end());
        }
        const uint32_t cdSize = static_cast<uint32_t> (o.size()) - cdStart;
        p32 (o, 0x06054b50u); p16 (o, 0); p16 (o, 0); p16 (o, static_cast<uint32_t> (items.size())); p16 (o, static_cast<uint32_t> (items.size()));
        p32 (o, cdSize); p32 (o, cdStart); p16 (o, static_cast<uint32_t> (comment.size()));
        o.insert (o.end(), comment.begin(), comment.end());
        return o;
    }
};

std::string textOfSize (size_t n)
{
    std::string s;
    sbtest::Rng rng (99);
    static const char* words[] = { "sine", "saw", "pad", "bass", "lead", "pluck", "wobble", "bell" };
    while (s.size() < n)
        s += std::string (words[rng.u32() % 8]) + std::to_string (rng.u32() % 100) + " ";
    s.resize (n);
    return s;
}

// ---- in-memory stores ------------------------------------------------------------------------------------------------------------
struct MemContent : ContentStore
{
    std::map<std::string, ImportedWavetable> tables;
    bool hasWavetable (const std::string& sha) override { return tables.count (sha) != 0; }
    bool addWavetable (const ImportedWavetable& wt, std::string& pack, std::string& file) override
    {
        tables[wt.sha256] = wt;
        pack = "user.test";
        file = "tables/" + wt.sha256.substr (0, 12) + ".wav";
        return true;
    }
    bool addSample (const ImportedSample&, std::string& pack, std::string& file) override { pack = "user.test"; file = "samples/x.wav"; return true; }
};

struct MemPresets : PresetStore
{
    std::map<std::string, std::string> files;   // relPath -> text
    std::map<std::string, std::string> reports; // relPath -> report
    bool exists (const std::string& name, const std::string& tag) override
    {
        const auto it = files.find (name + ".sbpreset");
        if (it == files.end())
            return false;
        return tag.empty() || it->second.find (tag) != std::string::npos;
    }
    bool write (const std::string& rel, const std::string& text, const std::string& report) override
    {
        files[rel] = text;
        reports[rel] = report;
        return true;
    }
};

/** A preset of the default patch; `variant` changes one control so that two presets of the same name differ. */
std::string presetJson (const std::string& name, double variant = 0.0)
{
    Fixture f = sbfx::defaultPatch();
    f.name = name;
    if (variant != 0.0)
        f.set ("filter_1_cutoff", 40.0 + variant);
    return f.json();
}

std::string tableJson()
{
    return sbfx::wavetable ("Bank Table", { sbfx::component ("Wave Source", "\"interpolation\":0", { sbfx::waveKeyframe (0, sbfx::sineWave (1)), sbfx::waveKeyframe (255, sbfx::sineWave (3)) }) });
}

std::string lfoJson()
{
    return sbfx::lineObject ({ { 0.0, 1.0 }, { 0.5, 0.0 }, { 1.0, 1.0 } }, { 0.0, 0.0, 0.0 }, true);
}

ImportOptions options()
{
    ImportOptions o;
    o.caps.customWavetables = true;
    o.caps.customLfoShapes = true;
    o.caps.sampleOsc = true;
    return o;
}
} // namespace

TEST (zip_crc32_known_value)
{
    const char* s = "123456789";
    CHECK_EQ (crc32Of (s, 9), 0xCBF43926u);
    CHECK_EQ (crc32Of ("", 0), 0u);
}

TEST (zip_inflate_zlib_dynamic_stream)
{
    std::vector<uint8_t> out (kZlibPlainSize);
    CHECK (inflateRaw (kZlibStream, sizeof kZlibStream, out.data(), out.size()) == ZipStatus::ok);
    CHECK_EQ (crc32Of (out.data(), out.size()), kZlibPlainCrc);

    // a wrong declared size is corrupt in both directions, a cut stream too
    std::vector<uint8_t> small (kZlibPlainSize - 1), big (kZlibPlainSize + 1);
    CHECK (inflateRaw (kZlibStream, sizeof kZlibStream, small.data(), small.size()) == ZipStatus::corrupt);
    CHECK (inflateRaw (kZlibStream, sizeof kZlibStream, big.data(), big.size()) == ZipStatus::corrupt);
    CHECK (inflateRaw (kZlibStream, sizeof kZlibStream / 2, out.data(), out.size()) == ZipStatus::corrupt);
}

TEST (zip_inflate_fixed_and_stored_blocks)
{
    for (const size_t n : { size_t (0), size_t (1), size_t (300), size_t (70000) })
    {
        const std::string text = textOfSize (n);
        for (const bool fixed : { true, false })
        {
            const auto packed = fixed ? deflateFixed (text) : deflateStoredBlocks (text);
            std::vector<uint8_t> out (text.size());
            CHECK (inflateRaw (packed.data(), packed.size(), out.data(), out.size()) == ZipStatus::ok);
            CHECK (std::equal (out.begin(), out.end(), text.begin()));
        }
    }
    // overlapping match (run length): "a" x 1000
    const std::string run (1000, 'a');
    const auto packed = deflateFixed (run);
    CHECK (packed.size() < 40);
    std::vector<uint8_t> out (run.size());
    CHECK (inflateRaw (packed.data(), packed.size(), out.data(), out.size()) == ZipStatus::ok);
    CHECK (std::equal (out.begin(), out.end(), run.begin()));
    // the reserved block type 3 is refused
    const uint8_t type3[] = { 0x07 };
    uint8_t one[1];
    CHECK (inflateRaw (type3, 1, one, 1) == ZipStatus::corrupt);
}

TEST (zip_archive_stored_and_deflate_entries)
{
    ZipFile z;
    const std::string a = textOfSize (5000), b = "hello", c = textOfSize (200);
    z.add ("dir/", "", 0);
    z.add ("dir/a.txt", a, 8);
    z.add ("b.txt", b, 0);
    z.add ("c.bin", c, 9);
    z.add ("empty", "", 0);
    z.comment = "a comment after the end record";
    const auto bytes = z.build();

    CHECK (looksLikeZip (bytes.data(), bytes.size()));
    ZipArchive zip;
    CHECK (zip.open (bytes.data(), bytes.size()) == ZipStatus::ok);
    CHECK_EQ (zip.entries().size(), static_cast<size_t> (5));
    CHECK (zip.entries()[0].directory);
    CHECK_EQ (zip.find ("b.txt"), 2);
    CHECK_EQ (zip.find ("nope"), -1);

    std::vector<uint8_t> out;
    CHECK (zip.extract (1, out) == ZipStatus::ok);
    CHECK (std::string (out.begin(), out.end()) == a);
    CHECK (zip.extract (2, out) == ZipStatus::ok);
    CHECK (std::string (out.begin(), out.end()) == b);
    CHECK (zip.extract (3, out) == ZipStatus::ok);
    CHECK (std::string (out.begin(), out.end()) == c);
    CHECK (zip.extract (4, out) == ZipStatus::ok);
    CHECK (out.empty());
    CHECK (zip.extract (99, out) == ZipStatus::badIndex);
}

TEST (zip_archive_refusals)
{
    const std::string junk (500, 'x');
    ZipArchive zip;
    CHECK (zip.open (junk.data(), junk.size()) == ZipStatus::notZip);
    CHECK (zip.open (nullptr, 0) == ZipStatus::notZip);
    CHECK (! looksLikeZip (junk.data(), junk.size()));

    ZipFile z;
    z.add ("a.txt", textOfSize (400), 8);
    z.add ("b.txt", "bbbb", 0);
    const auto bytes = z.build();

    // a truncated archive loses its end record
    CHECK (zip.open (bytes.data(), bytes.size() - 10) != ZipStatus::ok);
    CHECK (zip.open (bytes.data(), 10) == ZipStatus::notZip);

    {
        ZipFile z2;
        z2.add ("s.txt", "stored data", 0);
        auto bad = z2.build();
        bad[30 + 5 + 2] ^= 1;
        ZipArchive a;
        CHECK (a.open (bad.data(), bad.size()) == ZipStatus::ok);
        std::vector<uint8_t> out;
        CHECK (a.extract (0, out) == ZipStatus::crcMismatch);
        CHECK (out.empty());
    }
    {
        ZipFile z2;
        z2.add ("e.txt", "secret", 0);
        z2.items[0].flags = 1;
        z2.add ("m.txt", "meth", 0);
        z2.items[1].method = 12; // bzip2
        const auto b2 = z2.build();
        ZipArchive a;
        CHECK (a.open (b2.data(), b2.size()) == ZipStatus::ok);
        std::vector<uint8_t> out;
        CHECK (a.extract (0, out) == ZipStatus::unsupported);
        CHECK (a.extract (1, out) == ZipStatus::unsupported);
    }
    {
        ZipLimits lim;
        lim.maxEntries = 1;
        CHECK (zip.open (bytes.data(), bytes.size(), lim) == ZipStatus::limitExceeded);
        lim = ZipLimits {};
        lim.maxEntryBytes = 100;
        CHECK (zip.open (bytes.data(), bytes.size(), lim) == ZipStatus::ok);
        std::vector<uint8_t> out;
        CHECK (zip.extract (0, out) == ZipStatus::limitExceeded);
        CHECK (zip.extract (1, out) == ZipStatus::ok);
        lim = ZipLimits {};
        lim.maxTotalBytes = 100;
        CHECK (zip.open (bytes.data(), bytes.size(), lim) == ZipStatus::limitExceeded);
    }
    // a bomb: the entry declares 5 bytes but the deflate stream wants to write more
    {
        ZipFile z2;
        const auto packed = deflateFixed (std::string (1000, 'a'));
        z2.addRaw ("bomb.txt", packed, 8, 0, 5);
        const auto b2 = z2.build();
        ZipArchive a;
        CHECK (a.open (b2.data(), b2.size()) == ZipStatus::ok);
        std::vector<uint8_t> out;
        CHECK (a.extract (0, out) == ZipStatus::corrupt);
    }
    // unsafe names are flagged
    {
        ZipFile z2;
        for (const char* n : { "../evil.vital", "/abs.vital", "a/../../b.vital", "C:/x.vital", "back\\slash.vital", "ok/fine.vital" })
            z2.add (n, "x", 0);
        const auto b2 = z2.build();
        ZipArchive a;
        CHECK (a.open (b2.data(), b2.size()) == ZipStatus::ok);
        CHECK (! a.entries()[0].safePath && ! a.entries()[1].safePath && ! a.entries()[2].safePath && ! a.entries()[3].safePath
               && ! a.entries()[4].safePath);
        CHECK (a.entries()[5].safePath);
    }
    // every prefix of the archive and a flipped byte at every position: no crash, no read outside (run under the sanitizer jobs)
    for (size_t n = 0; n < bytes.size(); n += 3)
    {
        ZipArchive a;
        std::vector<uint8_t> out;
        if (a.open (bytes.data(), n) == ZipStatus::ok)
            for (size_t i = 0; i < a.entries().size(); ++i)
                (void) a.extract (i, out);
    }
    for (size_t pos = 0; pos < bytes.size(); pos += 5)
    {
        auto flipped = bytes;
        flipped[pos] ^= 0xa5;
        ZipArchive a;
        std::vector<uint8_t> out;
        if (a.open (flipped.data(), flipped.size()) == ZipStatus::ok)
            for (size_t i = 0; i < a.entries().size(); ++i)
                (void) a.extract (i, out);
    }
}

TEST (vital_batch_kinds_and_names)
{
    CHECK (vitalFileKindFromName ("a.vital") == VitalFileKind::preset);
    CHECK (vitalFileKindFromName ("A.VITAL") == VitalFileKind::preset);
    CHECK (vitalFileKindFromName ("dir.d/b.vitaltable") == VitalFileKind::wavetable);
    CHECK (vitalFileKindFromName ("c.vitallfo") == VitalFileKind::lfo);
    CHECK (vitalFileKindFromName ("c.vitalbank") == VitalFileKind::unknown);
    CHECK (vitalFileKindFromName ("noext") == VitalFileKind::unknown);
    CHECK (vitalFileKindFromName ("dir.vital/readme") == VitalFileKind::unknown);
    CHECK (isVitalBankName ("Pack.VitalBank"));
    CHECK (! isVitalBankName ("Pack.vital"));
}

TEST (vital_batch_stand_alone_table_and_lfo)
{
    MemContent content;
    MemPresets presets;
    BatchOptions batch;

    const std::string table = tableJson();
    const auto t = importVitalDocument ("t.vitaltable", "t.vitaltable", {}, table.data(), table.size(), options(), batch, content, presets);
    CHECK (t.outcome == BatchOutcome::written);
    CHECK (t.kind == VitalFileKind::wavetable);
    CHECK_EQ (content.tables.size(), static_cast<size_t> (1));
    CHECK (! t.relPath.empty());

    // the same table again is not stored twice (the store answers by its hash)
    const auto t2 = importVitalDocument ("t2.vitaltable", "t2.vitaltable", {}, table.data(), table.size(), options(), batch, content, presets);
    CHECK (t2.outcome == BatchOutcome::written);
    CHECK_EQ (content.tables.size(), static_cast<size_t> (1));

    const std::string lfo = lfoJson();
    const auto l = importVitalDocument ("shape.vitallfo", "shape.vitallfo", "Bank", lfo.data(), lfo.size(), options(), batch, content, presets);
    CHECK (l.outcome == BatchOutcome::written);
    CHECK (l.kind == VitalFileKind::lfo);
    CHECK_EQ (presets.files.size(), static_cast<size_t> (1));
    if (! presets.files.empty())
    {
        CHECK (presets.files.begin()->first.find ("Bank/") == 0);
        const std::string& shapeText = presets.files.begin()->second;
        CHECK (shapeText.find ("\"format\":\"synthyboy-lfo-shape\"") != std::string::npos);
        CHECK (shapeText.find ("\"points\":[{") != std::string::npos);
    }

    // a file that is not what its extension says fails alone
    const std::string garbage = "{\"nothing\":1}";
    CHECK (importVitalDocument ("g.vitaltable", "g.vitaltable", {}, garbage.data(), garbage.size(), options(), batch, content, presets).outcome == BatchOutcome::failed);
    CHECK (importVitalDocument ("g.vitallfo", "g.vitallfo", {}, garbage.data(), garbage.size(), options(), batch, content, presets).outcome == BatchOutcome::failed);
    CHECK (importVitalDocument ("g.txt", "g.txt", {}, garbage.data(), garbage.size(), options(), batch, content, presets).outcome == BatchOutcome::ignored);
}

TEST (vital_batch_bank_round_trip)
{
    ZipFile z;
    z.add ("Pads/", "", 0);
    z.add ("Pads/Soft Pad.vital", presetJson ("Soft Pad"), 8);
    z.add ("Lead One.vital", presetJson ("Lead One", 1.0), 0);
    z.add ("Tables/Bank Table.vitaltable", tableJson(), 8);
    z.add ("Shapes/Wobble.vitallfo", lfoJson(), 8);
    z.add ("readme.txt", "hello", 0);
    z.add ("../evil.vital", presetJson ("Evil"), 0);
    z.add ("Broken.vital", "{ not json", 0);
    const auto bytes = z.build();

    MemContent content;
    MemPresets presets;
    BatchOptions batch;
    const auto sum = importVitalBank (bytes.data(), bytes.size(), "My Bank.vitalbank", options(), batch, content, presets);
    CHECK (sum.zip == ZipStatus::ok);
    CHECK_EQ (sum.rows.size(), static_cast<size_t> (8));
    CHECK_EQ (sum.written, 4);   // two presets, a table, a shape
    CHECK_EQ (sum.failed, 1);    // Broken.vital
    CHECK_EQ (sum.ignored, 3);   // the directory, the readme, the unsafe path
    CHECK (presets.files.count ("My Bank/Soft Pad.sbpreset") == 1);
    CHECK (presets.files.count ("My Bank/Lead One.sbpreset") == 1);
    int shapeFiles = 0;
    for (const auto& f : presets.files)
        if (f.first.rfind ("My Bank/", 0) == 0 && f.first.size() > 14 && f.first.compare (f.first.size() - 14, 14, ".lfoshape.json") == 0)
            ++shapeFiles;
    CHECK_EQ (shapeFiles, 1);
    CHECK (presets.reports["My Bank/Soft Pad.sbpreset"].find ("{") != std::string::npos);
    CHECK_EQ (content.tables.size(), static_cast<size_t> (1));
    bool sawUnsafe = false;
    for (const auto& r : sum.rows)
        if (r.source.find ("evil") != std::string::npos)
            sawUnsafe = r.outcome == BatchOutcome::ignored && ! r.error.empty();
    CHECK (sawUnsafe);

    // the imported preset loads as an .sbpreset
    PresetMeta meta;
    auto values = ParamValues::defaults();
    const std::string& text = presets.files["My Bank/Soft Pad.sbpreset"];
    CHECK (loadPreset (text.data(), text.size(), meta, values).status == LoadStatus::ok);
    CHECK (meta.name == "Soft Pad");

    // a second run: everything that was written is recognised by its source
    const auto again = importVitalBank (bytes.data(), bytes.size(), "My Bank.vitalbank", options(), batch, content, presets);
    CHECK_EQ (again.skippedSame, 2);
    CHECK_EQ (presets.files.size(), static_cast<size_t> (3));

    // replace: written again
    batch.duplicate = BatchOptions::replace;
    const auto repl = importVitalBank (bytes.data(), bytes.size(), "My Bank.vitalbank", options(), batch, content, presets);
    CHECK_EQ (repl.skippedSame, 0);
    CHECK_EQ (repl.written, 4);
}

TEST (vital_batch_duplicate_policies)
{
    MemContent content;
    MemPresets presets;
    const std::string a = presetJson ("Same Name"), b = presetJson ("Same Name", 7.0);

    BatchOptions keep;
    keep.duplicate = BatchOptions::skipSame;
    CHECK (importVitalDocument ("a", "a.vital", {}, a.data(), a.size(), options(), keep, content, presets).outcome == BatchOutcome::written);
    CHECK (importVitalDocument ("b", "b.vital", {}, b.data(), b.size(), options(), keep, content, presets).outcome == BatchOutcome::written);
    CHECK (presets.files.count ("Same Name.sbpreset") == 1);
    CHECK (presets.files.count ("Same Name (2).sbpreset") == 1);

    BatchOptions ask;
    ask.duplicate = BatchOptions::ask;
    const std::string c = presetJson ("Same Name", 9.0);
    CHECK (importVitalDocument ("c", "c.vital", {}, c.data(), c.size(), options(), ask, content, presets).outcome == BatchOutcome::needsDecision);
    CHECK_EQ (presets.files.size(), static_cast<size_t> (2));

    BatchOptions repl;
    repl.duplicate = BatchOptions::replace;
    CHECK (importVitalDocument ("c", "c.vital", {}, c.data(), c.size(), options(), repl, content, presets).outcome == BatchOutcome::written);
    CHECK_EQ (presets.files.size(), static_cast<size_t> (2));
}

TEST (vital_batch_bank_refusals)
{
    MemContent content;
    MemPresets presets;
    BatchOptions batch;
    const std::string junk (300, 'x');
    const auto bad = importVitalBank (junk.data(), junk.size(), "Junk.vitalbank", options(), batch, content, presets);
    CHECK (bad.zip == ZipStatus::notZip);
    CHECK (bad.rows.empty());
    CHECK (! bad.error.empty());

    // one entry with a wrong CRC fails alone, the others import
    ZipFile z;
    z.add ("Good.vital", presetJson ("Good"), 8);
    z.add ("Bad.vital", presetJson ("Bad"), 0);
    z.items[1].crc ^= 0xffffffffu;
    const auto bytes = z.build();
    const auto sum = importVitalBank (bytes.data(), bytes.size(), "B.vitalbank", options(), batch, content, presets);
    CHECK_EQ (sum.written, 1);
    CHECK_EQ (sum.failed, 1);
    CHECK (sum.rows.size() == 2 && sum.rows[1].zip == ZipStatus::crcMismatch);

    // the entry limit applies
    ZipLimits lim;
    lim.maxEntries = 1;
    const auto limited = importVitalBank (bytes.data(), bytes.size(), "B.vitalbank", options(), batch, content, presets, lim);
    CHECK (limited.zip == ZipStatus::limitExceeded);
}

TEST (vital_batch_real_bank_optional)
{
#ifdef SB_VITAL_PRESETS_DIR
    namespace fs = std::filesystem;
    std::error_code ec;
    if (! fs::is_directory (SB_VITAL_PRESETS_DIR, ec))
    {
        std::printf ("    SKIP: no tests/vital-presets directory\n");
        return;
    }
    int banks = 0;
    for (const auto& e : fs::directory_iterator (SB_VITAL_PRESETS_DIR, ec))
    {
        if (! isVitalBankName (e.path().filename().string()))
            continue;
        std::ifstream in (e.path(), std::ios::binary);
        const std::string bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        MemContent content;
        MemPresets presets;
        BatchOptions batch;
        const auto sum = importVitalBank (bytes.data(), bytes.size(), e.path().filename().string(), options(), batch, content, presets);
        std::printf ("    %s: entries=%zu written=%d failed=%d ignored=%d\n", e.path().filename().string().c_str(), sum.rows.size(), sum.written, sum.failed, sum.ignored);
        CHECK (sum.zip == ZipStatus::ok);
        CHECK (sum.written > 0);
        CHECK_EQ (sum.failed, 0);
        ++banks;
    }
    if (banks == 0)
        std::printf ("    SKIP: no .vitalbank in tests/vital-presets\n");
#else
    std::printf ("    SKIP: SB_VITAL_PRESETS_DIR not defined\n");
#endif
}
