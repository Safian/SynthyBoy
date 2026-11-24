// SynthyBoy - core: self-contained SHA-256 implementation (RFC 6234).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace synthyboy
{
class Sha256
{
public:
    Sha256() noexcept;

    void update (const void* data, size_t length) noexcept;
    void finalize (uint8_t hash[32]) noexcept;

    std::string hexDigest() noexcept;

    static std::string hashBufferHex (const void* data, size_t length);
    static std::string hashFileHex (const std::string& path);

private:
    uint32_t state_[8];
    uint64_t count_;
    uint8_t buffer_[64];

    void transform (const uint8_t block[64]) noexcept;
};
} // namespace synthyboy
