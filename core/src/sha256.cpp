// SynthyBoy - core: self-contained SHA-256 implementation (RFC 6234).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "sha256.h"

#include <cstdio>
#include <cstring>

namespace synthyboy
{
namespace
{
constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr (uint32_t x, uint32_t n) noexcept { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch (uint32_t x, uint32_t y, uint32_t z) noexcept { return (x & y) ^ (~x & z); }
inline uint32_t maj (uint32_t x, uint32_t y, uint32_t z) noexcept { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sigma0 (uint32_t x) noexcept { return rotr (x, 2) ^ rotr (x, 13) ^ rotr (x, 22); }
inline uint32_t sigma1 (uint32_t x) noexcept { return rotr (x, 6) ^ rotr (x, 11) ^ rotr (x, 25); }
inline uint32_t gamma0 (uint32_t x) noexcept { return rotr (x, 7) ^ rotr (x, 18) ^ (x >> 3); }
inline uint32_t gamma1 (uint32_t x) noexcept { return rotr (x, 17) ^ rotr (x, 19) ^ (x >> 10); }

std::string toHex (const uint8_t hash[32])
{
    static const char hexChars[] = "0123456789abcdef";
    std::string s;
    s.resize (64);
    for (size_t i = 0; i < 32; ++i)
    {
        s[2 * i] = hexChars[(hash[i] >> 4) & 0x0f];
        s[2 * i + 1] = hexChars[hash[i] & 0x0f];
    }
    return s;
}
} // namespace

Sha256::Sha256() noexcept
{
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    count_ = 0;
}

void Sha256::transform (const uint8_t block[64]) noexcept
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<uint32_t> (block[i * 4]) << 24)
             | (static_cast<uint32_t> (block[i * 4 + 1]) << 16)
             | (static_cast<uint32_t> (block[i * 4 + 2]) << 8)
             | (static_cast<uint32_t> (block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i)
        w[i] = gamma1 (w[i - 2]) + w[i - 7] + gamma0 (w[i - 15]) + w[i - 16];

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (int i = 0; i < 64; ++i)
    {
        const uint32_t t1 = h + sigma1 (e) + ch (e, f, g) + K[i] + w[i];
        const uint32_t t2 = sigma0 (a) + maj (a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update (const void* data, size_t length) noexcept
{
    if (data == nullptr || length == 0)
        return;

    const uint8_t* p = static_cast<const uint8_t*> (data);
    const size_t bufferIndex = static_cast<size_t> (count_ & 63);
    count_ += length;

    if (bufferIndex > 0)
    {
        const size_t left = 64 - bufferIndex;
        if (length < left)
        {
            std::memcpy (buffer_ + bufferIndex, p, length);
            return;
        }
        std::memcpy (buffer_ + bufferIndex, p, left);
        transform (buffer_);
        p += left;
        length -= left;
    }

    while (length >= 64)
    {
        transform (p);
        p += 64;
        length -= 64;
    }

    if (length > 0)
        std::memcpy (buffer_, p, length);
}

void Sha256::finalize (uint8_t hash[32]) noexcept
{
    const uint64_t totalBits = count_ * 8;
    const size_t bufferIndex = static_cast<size_t> (count_ & 63);

    buffer_[bufferIndex] = 0x80;
    const size_t padLen = (bufferIndex < 56) ? (56 - 1 - bufferIndex) : (64 + 56 - 1 - bufferIndex);
    if (padLen > 0)
        std::memset (buffer_ + bufferIndex + 1, 0, padLen);

    if (bufferIndex >= 56)
    {
        transform (buffer_);
        std::memset (buffer_, 0, 56);
    }

    for (int i = 0; i < 8; ++i)
        buffer_[56 + i] = static_cast<uint8_t> (totalBits >> (56 - 8 * i));

    transform (buffer_);

    for (int i = 0; i < 8; ++i)
    {
        hash[i * 4]     = static_cast<uint8_t> (state_[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t> (state_[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t> (state_[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t> (state_[i]);
    }
}

std::string Sha256::hexDigest() noexcept
{
    uint8_t h[32];
    finalize (h);
    return toHex (h);
}

std::string Sha256::hashBufferHex (const void* data, size_t length)
{
    Sha256 ctx;
    ctx.update (data, length);
    return ctx.hexDigest();
}

std::string Sha256::hashFileHex (const std::string& path)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return {};

    Sha256 ctx;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread (buf, 1, sizeof buf, f)) > 0)
        ctx.update (buf, n);
    std::fclose (f);
    return ctx.hexDigest();
}
} // namespace synthyboy
