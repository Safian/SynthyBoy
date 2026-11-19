// SynthyBoy - core: content store implementations.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/content_store.h"

#include <algorithm>
#include <cstdio>

#include "sha256.h"

namespace synthyboy
{
namespace
{
bool shaMatches (const ContentFileRef& ref, const std::vector<uint8_t>& bytes)
{
    if (ref.sha256.empty())
        return true;
    return Sha256::hashBufferHex (bytes.data(), bytes.size()) == ref.sha256;
}
} // namespace

void MemoryContentStore::add (const std::string& pack, const std::string& file, std::vector<uint8_t> bytes)
{
    files_[pack + "/" + file] = std::move (bytes);
}

bool MemoryContentStore::readFile (const ContentFileRef& ref, std::vector<uint8_t>& bytes) const
{
    const auto it = files_.find (ref.pack + "/" + ref.file);
    if (it == files_.end() || it->second.size() > kMaxContentFileBytes || ! shaMatches (ref, it->second))
        return false;
    bytes = it->second;
    return true;
}

bool DirectoryContentStore::readFile (const ContentFileRef& ref, std::vector<uint8_t>& bytes) const
{
    return readFileBounded (ref, bytes, kMaxContentFileBytes, nullptr);
}

bool DirectoryContentStore::readFileBounded (const ContentFileRef& ref, std::vector<uint8_t>& bytes, size_t maxBytes, bool* tooLarge) const
{
    if (tooLarge != nullptr)
        *tooLarge = false;
    if (ref.file.empty())
        return false;
    const std::string path = root_ + "/" + (ref.pack.empty() ? std::string() : ref.pack + "/") + ref.file;
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;
    bytes.clear();
    const size_t limit = std::min (maxBytes, kMaxContentFileBytes);
    uint8_t buffer[65536];
    size_t n;
    while ((n = std::fread (buffer, 1, sizeof buffer, f)) > 0)
    {
        bytes.insert (bytes.end(), buffer, buffer + n);
        if (bytes.size() > limit)
        {
            std::fclose (f);
            bytes.clear();
            if (tooLarge != nullptr)
                *tooLarge = true;
            return false;
        }
    }
    std::fclose (f);
    return shaMatches (ref, bytes);
}
} // namespace synthyboy
