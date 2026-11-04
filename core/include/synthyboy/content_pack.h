// SynthyBoy - core: ContentPack format, manifest parser, and verification (docs/ARCHITECTURE.md 6).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace synthyboy
{
struct ContentPackFile
{
    std::string path;
    std::string sha256;
    size_t size = 0;
};

struct ContentPackInfo
{
    int schemaVersion = 1;
    std::string id;
    std::string name;
    std::string version;
    std::string license;
    std::string attribution;
    std::string kind; // "presets" | "wavetables" | "samples"
    std::string requiresPlugin;
    std::string description;
    std::vector<ContentPackFile> files;
};

/** Parses a contentpack.json document into out. Returns true on success. */
bool parseContentPackManifest (const void* data, size_t size, ContentPackInfo& out) noexcept;

/** Verifies that all files declared in info exist in packDirectory and their SHA-256 matches.
    Appends error descriptions to errorsOut. Returns true if all files matched. */
bool verifyContentPack (const std::string& packDirectory, const ContentPackInfo& info, std::vector<std::string>& errorsOut);

/** Content registry: tracks installed content packs, missing-content flags and content resolution. */
class ContentRegistry
{
public:
    ContentRegistry();

    void addSearchPath (const std::string& path);
    void rescanPacks();

    const std::vector<ContentPackInfo>& installedPacks() const noexcept { return packs_; }

    /** Returns true if any content reference could not be resolved since last clear. */
    bool hasMissingContent() const noexcept { return missingContentFlag_; }
    void clearMissingContentFlag() noexcept { missingContentFlag_ = false; }
    void markMissingContent() noexcept { missingContentFlag_ = true; }

    /** Resolves a preset file path by relative pack path or name. Returns empty string and marks missing content if not found. */
    std::string resolvePresetPath (const std::string& relativePath);

private:
    std::vector<std::string> searchPaths_;
    std::vector<ContentPackInfo> packs_;
    bool missingContentFlag_ = false;
};
} // namespace synthyboy
