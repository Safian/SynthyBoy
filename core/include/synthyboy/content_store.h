// SynthyBoy - core: the content store seam (docs/ui-design/13-new-keys.md 5, 11 C1/C4, ARCHITECTURE 5.2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The state (`content_state.h`) only REFERENCES wavetables, samples and impulse responses (pack, file, sha256). A ContentStore turns a
// reference into bytes. It is supplied by the host layer (shell / Kick) with IEngine::setContentStore () and used on the message thread
// only, never from process (). Implementations here: MemoryContentStore (tests, embedded content) and DirectoryContentStore (content
// packs installed as directories: <root>/<pack>/<file>).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"

namespace synthyboy
{
class IContentStore
{
public:
    virtual ~IContentStore() = default;

    /** Reads the file a reference names. Returns false if it is missing, unreadable, or (when ref.sha256 is not empty) its SHA-256
        does not match. Not real-time. */
    virtual bool readFile (const ContentFileRef& ref, std::vector<uint8_t>& bytes) const = 0;
};

/** Files held in memory, keyed by "<pack>/<file>". */
class MemoryContentStore final : public IContentStore
{
public:
    void add (const std::string& pack, const std::string& file, std::vector<uint8_t> bytes);
    bool readFile (const ContentFileRef& ref, std::vector<uint8_t>& bytes) const override;

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};

/** Files below a root directory: <root>/<pack>/<file> (pack and file are relative, validated by the state loader: no "..", no ":"). */
class DirectoryContentStore final : public IContentStore
{
public:
    explicit DirectoryContentStore (std::string rootDirectory) : root_ (std::move (rootDirectory)) {}
    bool readFile (const ContentFileRef& ref, std::vector<uint8_t>& bytes) const override;

private:
    std::string root_;
};

/** Largest file a store hands out (a sample or impulse reference is a few MB; the cap keeps a corrupt reference from eating memory). */
constexpr size_t kMaxContentFileBytes = 256u * 1024u * 1024u;
} // namespace synthyboy
