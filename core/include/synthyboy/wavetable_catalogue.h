// SynthyBoy - core: the wavetable catalogue the browser of the oscillator shows (docs/MPC.md "Wavetable browser"). Categories, each with its tables:
//   (a) "Basic" and "Morph": the built-in tables of oscN.wavetable (their names; the entry is the choice number);
//   (b) the wavetables of every installed content pack (a folder `<pack dir>/<pack>/` with a contentpack.json of kind "wavetables"), grouped by the `category` of the manifest entry
//       (default: the first folder of the file's path, else the pack's name), shown with its `title` (default: the file name);
//   (c) the files the player copied to `<root>/SynthyBoy/Wavetables/`: one folder level is a category, a file directly in the folder is in the category "User".
// Nothing is indexed, nothing is stored: the catalogue is built when the browser opens, from directory listings (names and sizes only, a file's content is never read while listing; the
// manifests of the packs are the only files read) and is bounded (the limits below). A new file shows the next time the browser opens. The file system is behind ICatalogueFs, so
// a test gives a fake one. Pure logic; not for the audio thread.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"

namespace synthyboy
{
/** The pack id the references of the player's own files carry: `<root>/SynthyBoy/Wavetables/<file>` is the file `<file>` of the pack "SynthyBoy/Wavetables" below the root (the SD card, a USB
    drive, the plugin's Content folder): the content store finds it under whichever root holds it, so a preset moves between devices with the files. */
constexpr const char* kUserWavetablePack = "SynthyBoy/Wavetables";
constexpr const char* kUserWavetableCategory = "User";   // the category of a file directly in the user folder
constexpr const char* kBasicCategory = "Basic";          // the built-in one cycle tables
constexpr const char* kMorphCategory = "Morph";          // the built-in morphing tables

constexpr size_t kMaxCataloguePacks = 32;            // pack folders read
constexpr size_t kMaxPackFiles = 1024;               // files read from one manifest
constexpr size_t kMaxUserFolders = 64;               // category folders of one root
constexpr size_t kMaxFolderEntries = 512;            // directory entries read from one folder (the first ones in the order the file system gives them, then sorted)
constexpr size_t kMaxCatalogueCategories = 96;
constexpr size_t kMaxCatalogueEntries = 4000;        // all files in all categories
constexpr size_t kMaxManifestFileBytes = 512 * 1024;

/** The file system as the catalogue needs it. */
class ICatalogueFs
{
public:
    struct Entry
    {
        std::string name;
        bool isDir = false;
        uint64_t size = 0;   // a file's size (stat), 0 for a folder
    };
    virtual ~ICatalogueFs() = default;
    /** Lists `dir`: at most `maxEntries` entries (files and folders, no "." / ".."), names and sizes only. `more` (when not null) is set when the folder holds more. False: the folder is not there. */
    virtual bool list (const std::string& dir, size_t maxEntries, std::vector<Entry>& out, bool* more) const = 0;
    /** The whole content of a small file (a manifest); false when it is missing or larger than `maxBytes`. */
    virtual bool readSmall (const std::string& path, size_t maxBytes, std::vector<uint8_t>& out) const = 0;
};

/** The real file system (std::filesystem; never throws, symbolic links are followed). */
class StdCatalogueFs final : public ICatalogueFs
{
public:
    bool list (const std::string& dir, size_t maxEntries, std::vector<Entry>& out, bool* more) const override;
    bool readSmall (const std::string& path, size_t maxBytes, std::vector<uint8_t>& out) const override;
};

struct WavetableEntry
{
    enum class Source : uint8_t { builtin, pack, user };
    Source source = Source::builtin;
    int builtinChoice = -1;      // builtin: the value of oscN.wavetable
    std::string title;           // what the row shows (the choice name, the manifest title, the file name without its extension)
    ContentFileRef ref;          // pack / user: pack, file, sha256 (the manifest's; empty for a user file: computed when it is loaded), name = title
    uint64_t size = 0;           // bytes of the file (0: a built-in)
    std::string refusal;         // not empty: this file cannot be loaded, and why (without the "not a wavetable file: " in front)
    bool isFile() const noexcept { return source != Source::builtin; }
};

struct WavetableCategory
{
    std::string name;
    std::vector<WavetableEntry> entries;
};

struct WavetableCatalogue
{
    std::vector<WavetableCategory> categories;
    bool truncated = false;      // a limit was reached: not every file is listed

    size_t entryCount() const noexcept;
    /** The category and the entry of the file `pack` / `file` (the SHA-256 is not compared); -1 when it is not in the catalogue. */
    int findFile (const std::string& pack, const std::string& file, int& entry) const noexcept;
    /** The category and entry of a built-in table (the value of oscN.wavetable); -1 when it has none (13, "Preset table"). */
    int findBuiltin (int choice, int& entry) const noexcept;
    /** The index of the category with this name (case insensitive), -1 when there is none. */
    int findCategory (const std::string& name) const noexcept;
};

/** Where the catalogue looks. `packDirs`: folders whose sub folders are packs (`<dir>/<pack>/contentpack.json`, e.g. the plugin's Content/packs). `userRoots`: the roots that may hold SynthyBoy/Wavetables (the
    SD card, the USB drives, the plugin's Content folder). */
struct CatalogueSources
{
    std::vector<std::string> packDirs;
    std::vector<std::string> userRoots;
};

/** Builds the catalogue: Basic, Morph, then the other categories (case insensitive, merged between the packs and the user folders) alphabetically, "User" last; the tables of a category by title. */
WavetableCatalogue buildWavetableCatalogue (const ICatalogueFs& fs, const CatalogueSources& sources);

/** The file of the same category `dir` (-1 / +1) away from entry `entry` of category `category`, skipping refused files and built-in tables: false at the ends (no wrap) or when `entry` is not a file. */
bool neighbourFile (const WavetableCatalogue& catalogue, int category, int entry, int dir, int& outEntry) noexcept;
/** The position of entry `entry` among the loadable files of its category (`index` of `count`, both from 0 / 1): for the stepper. False when it is not a loadable file. */
bool filePosition (const WavetableCatalogue& catalogue, int category, int entry, int& index, int& count) noexcept;

/** Reads the file `ref` names through `store`, parses it as a wavetable (wavetable_file.h) and fills `ref.sha256` with its hash (computed now, off the audio thread) and `frames`. On a refusal: false and
    `reason` ("file too large (...)", "cannot read the file", ...; without "not a wavetable file: "). The store hands the file out bounded (kMaxWavetableFileBytes). Not real time. */
class IContentStore;
bool prepareWavetableRef (const IContentStore& store, ContentFileRef& ref, int& frames, std::string& reason);
} // namespace synthyboy
