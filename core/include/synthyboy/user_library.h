// SynthyBoy - core: the user library of small JSON items (WP-01 f, docs/ui-design/13-new-keys.md 4.25).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// LFO shapes (S11 shape presets, favourites), formula presets and own formulas (S14), FX rack presets (S10), voice control presets (WP-07):
// every category is a directory `<userDir>/library/<category>/<name>.json` with the text the caller made, plus a `favourites.txt` (one name per
// line). The library stores and lists text; the formats belong to their users. JUCE free; message thread.
#pragma once

#include <string>
#include <vector>

namespace synthyboy
{
constexpr const char* kLibLfoShapes = "lfo_shapes";
constexpr const char* kLibFormulas = "formulas";
constexpr const char* kLibFxRacks = "fx_racks";
constexpr const char* kLibVoiceControl = "voice_control";

constexpr size_t kMaxLibraryItemBytes = 256 * 1024;
constexpr size_t kMaxLibraryItems = 4096;

struct LibraryEntry
{
    std::string name; // the display name (the file stem)
    bool favourite = false;
};

class UserLibrary
{
public:
    explicit UserLibrary (const std::string& userDir);

    /** True for the four categories above (any other string is refused: it becomes a directory name). */
    static bool isCategory (const std::string& category) noexcept;

    /** The items of a category, favourites first, then by name (case insensitive). */
    std::vector<LibraryEntry> list (const std::string& category) const;
    /** Writes `<name>.json` (atomically). `overwrite` false refuses an existing name. The name is made safe for a file (sanitizeFileStem). False on
        failure or for a text above kMaxLibraryItemBytes / empty. */
    bool save (const std::string& category, const std::string& name, const std::string& json, bool overwrite = true);
    bool load (const std::string& category, const std::string& name, std::string& json) const;
    bool exists (const std::string& category, const std::string& name) const;
    bool remove (const std::string& category, const std::string& name);
    bool isFavourite (const std::string& category, const std::string& name) const;
    bool setFavourite (const std::string& category, const std::string& name, bool favourite);

    const std::string& rootDirectory() const noexcept { return root_; }

private:
    std::string pathOf (const std::string& category, const std::string& name) const;
    std::vector<std::string> readFavourites (const std::string& category) const;
    bool writeFavourites (const std::string& category, const std::vector<std::string>& names) const;

    std::string root_;
};
} // namespace synthyboy
