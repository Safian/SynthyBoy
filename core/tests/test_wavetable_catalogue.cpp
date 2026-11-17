// SynthyBoy - tests of the wavetable catalogue (core/include/synthyboy/wavetable_catalogue.h): built-in categories, packs with and without categories and titles, the user folders, the limits,
// the neighbours of a file, the manifest keys `category` and `title`, and preparing a reference (hash, refusals). A fake file system, no disk.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "synthyboy/content_pack.h"
#include "synthyboy/content_store.h"
#include "synthyboy/params.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wavetable_catalogue.h"
#include "synthyboy/wavetable_file.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
/** Directories and files by path; `list` gives the entries below a directory. The reads of a manifest are counted: nothing else is ever read. */
class FakeFs final : public ICatalogueFs
{
public:
    void dir (const std::string& path) { dirs_[path] = true; }
    void file (const std::string& path, uint64_t size) { files_[path] = size; }
    void text (const std::string& path, const std::string& body) { texts_[path] = body; files_[path] = body.size(); }

    bool list (const std::string& d, size_t maxEntries, std::vector<Entry>& out, bool* more) const override
    {
        out.clear();
        if (more != nullptr)
            *more = false;
        if (dirs_.find (d) == dirs_.end())
            return false;
        const std::string prefix = d + "/";
        std::map<std::string, Entry> found;
        for (const auto& p : dirs_)
            if (p.first.size() > prefix.size() && p.first.compare (0, prefix.size(), prefix) == 0 && p.first.find ('/', prefix.size()) == std::string::npos)
            {
                Entry e;
                e.name = p.first.substr (prefix.size());
                e.isDir = true;
                found[e.name] = e;
            }
        for (const auto& p : files_)
            if (p.first.size() > prefix.size() && p.first.compare (0, prefix.size(), prefix) == 0 && p.first.find ('/', prefix.size()) == std::string::npos)
            {
                Entry e;
                e.name = p.first.substr (prefix.size());
                e.size = p.second;
                found[e.name] = e;
            }
        for (const auto& p : found)
        {
            if (out.size() >= maxEntries)
            {
                if (more != nullptr)
                    *more = true;
                break;
            }
            out.push_back (p.second);
        }
        return true;
    }
    bool readSmall (const std::string& path, size_t maxBytes, std::vector<uint8_t>& out) const override
    {
        ++reads;
        const auto it = texts_.find (path);
        if (it == texts_.end() || it->second.size() > maxBytes)
            return false;
        out.assign (it->second.begin(), it->second.end());
        return true;
    }
    mutable int reads = 0;

private:
    std::map<std::string, bool> dirs_;
    std::map<std::string, uint64_t> files_;
    std::map<std::string, std::string> texts_;
};

std::string manifest (const std::string& kind, const std::string& name, const std::string& files)
{
    return "{\"schemaVersion\":1,\"id\":\"com.test.pack\",\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"license\":\"CC0-1.0\",\"attribution\":\"t\",\"kind\":\"" + kind
        + "\",\"requires_plugin\":\">=0.1.0\",\"files\":[" + files + "]}";
}
std::string sha (char c) { return std::string (64, c); }
std::string entry (const std::string& path, char shaChar, int size, const std::string& extra = "")
{
    return "{\"path\":\"" + path + "\",\"sha256\":\"" + sha (shaChar) + "\",\"size\":" + std::to_string (size) + extra + "}";
}

const WavetableCategory* find (const WavetableCatalogue& c, const std::string& name)
{
    const int i = c.findCategory (name);
    return i < 0 ? nullptr : &c.categories[static_cast<size_t> (i)];
}
} // namespace

TEST (wavetable_catalogue_builtins_are_basic_and_morph_with_their_own_names)
{
    FakeFs fs;
    const WavetableCatalogue c = buildWavetableCatalogue (fs, {});
    CHECK_EQ (c.categories.size(), static_cast<size_t> (2));
    CHECK_EQ (c.categories[0].name, std::string ("Basic"));
    CHECK_EQ (c.categories[1].name, std::string ("Morph"));
    CHECK_EQ (c.categories[0].entries.size(), static_cast<size_t> (6));
    CHECK_EQ (c.categories[1].entries.size(), static_cast<size_t> (8));
    const ParamInfo* info = findParamByKey ("osc1.wavetable");
    CHECK (info != nullptr);
    int seen[kNumWavetableChoices] = {};
    for (const auto& cat : c.categories)
        for (const auto& e : cat.entries)
        {
            CHECK (! e.isFile() && e.builtinChoice >= 0 && e.builtinChoice < kNumWavetableChoices);
            CHECK_EQ (e.title, std::string (info->choices[e.builtinChoice]));   // the existing names
            ++seen[e.builtinChoice];
        }
    for (int i = 0; i < kNumWavetableChoices; ++i)
        CHECK_EQ (seen[i], i == kWavetablePresetTable ? 0 : 1);   // every built-in table once, "Preset table" is not a table of its own
    int entry = -1;
    CHECK_EQ (c.findBuiltin (kWavetableSawDown, entry), 0);
    CHECK_EQ (c.findBuiltin (kWavetablePresetTable, entry), -1);
    CHECK_EQ (fs.reads, 0);
}

TEST (wavetable_catalogue_a_pack_is_grouped_by_the_manifest_category_then_the_folder_then_the_pack_name)
{
    FakeFs fs;
    fs.dir ("/c/packs");
    fs.dir ("/c/packs/com.test.pack");
    fs.text ("/c/packs/com.test.pack/contentpack.json",
             manifest ("wavetables", "Test Pack",
                       entry ("Analog/warm_saw.wav", 'a', 262188) + "," + entry ("Analog/thin_pulse.wav", 'b', 100000, ",\"title\":\"Thin Pulse 2\"") + ","
                           + entry ("flat_file.wav", 'c', 4000) + "," + entry ("Digital/x.wav", 'd', 4000, ",\"category\":\"Spectral\"") + "," + entry ("Analog/readme.txt", 'e', 10)));
    // a samples pack and a broken manifest are not wavetable packs
    fs.dir ("/c/packs/com.test.samples");
    fs.text ("/c/packs/com.test.samples/contentpack.json", manifest ("samples", "S", entry ("samples/a.wav", 'f', 100)));
    fs.dir ("/c/packs/broken");
    fs.text ("/c/packs/broken/contentpack.json", "{ not json");
    CatalogueSources src;
    src.packDirs = { "/c/packs" };
    const WavetableCatalogue c = buildWavetableCatalogue (fs, src);

    const WavetableCategory* analog = find (c, "Analog");
    CHECK (analog != nullptr);
    CHECK_EQ (analog->entries.size(), static_cast<size_t> (2));   // the .txt is not listed
    CHECK_EQ (analog->entries[0].title, std::string ("Thin Pulse 2"));   // sorted by title, "thin pulse 2" before "warm_saw"
    CHECK_EQ (analog->entries[1].title, std::string ("warm_saw"));         // the default title is the file name without its extension
    CHECK_EQ (analog->entries[1].ref.pack, std::string ("com.test.pack"));
    CHECK_EQ (analog->entries[1].ref.file, std::string ("Analog/warm_saw.wav"));
    CHECK_EQ (analog->entries[1].ref.sha256, sha ('a'));
    CHECK_EQ (analog->entries[1].ref.name, analog->entries[1].title);
    CHECK (analog->entries[1].source == WavetableEntry::Source::pack);
    CHECK (find (c, "Spectral") != nullptr && find (c, "Spectral")->entries.size() == 1);   // the manifest category wins over the folder
    CHECK (find (c, "Digital") == nullptr);
    const WavetableCategory* pack = find (c, "Test Pack");                                   // a file in the root of the pack: the pack's name
    CHECK (pack != nullptr && pack->entries.size() == 1);
    CHECK (find (c, "S") == nullptr);                                                        // the samples pack is not offered
    // the order: Basic, Morph, then alphabetical
    CHECK_EQ (c.categories[0].name, std::string ("Basic"));
    CHECK_EQ (c.categories[1].name, std::string ("Morph"));
    CHECK_EQ (c.categories[2].name, std::string ("Analog"));
    int entryIndex = -1;
    CHECK_EQ (c.findFile ("com.test.pack", "Analog/warm_saw.wav", entryIndex), 2);
    CHECK_EQ (entryIndex, 1);
}

TEST (wavetable_catalogue_a_manifest_without_category_and_title_reads_as_before_and_the_keys_round_trip)
{
    const std::string old = manifest ("wavetables", "N", entry ("a.wav", 'a', 10));
    ContentPackInfo info;
    CHECK (parseContentPackManifest (old.data(), old.size(), info));
    CHECK_EQ (info.files.size(), static_cast<size_t> (1));
    CHECK (info.files[0].category.empty() && info.files[0].title.empty());
    info.files[0].category = "Vocal";
    info.files[0].title = "Ah \"Choir\"";
    const std::string text = serializeContentPackManifest (info);
    ContentPackInfo back;
    CHECK (parseContentPackManifest (text.data(), text.size(), back));
    CHECK_EQ (back.files[0].category, std::string ("Vocal"));
    CHECK_EQ (back.files[0].title, std::string ("Ah \"Choir\""));
    // a value that is not a string is skipped, a long one is cut
    const std::string odd = manifest ("wavetables", "N", entry ("a.wav", 'a', 10, ",\"category\":5,\"title\":\"" + std::string (200, 'x') + "\""));
    ContentPackInfo o;
    CHECK (parseContentPackManifest (odd.data(), odd.size(), o));
    CHECK (o.files[0].category.empty());
    CHECK_EQ (o.files[0].title.size(), static_cast<size_t> (63));
}

TEST (wavetable_catalogue_user_folders_are_categories_and_loose_files_are_user)
{
    FakeFs fs;
    for (const char* root : { "/sdcard", "/media/USB1" })
    {
        fs.dir (std::string (root) + "/SynthyBoy");
        fs.dir (std::string (root) + "/SynthyBoy/Wavetables");
    }
    fs.file ("/sdcard/SynthyBoy/Wavetables/loose.wav", 4096);
    fs.file ("/sdcard/SynthyBoy/Wavetables/notes.txt", 10);
    fs.file ("/sdcard/SynthyBoy/Wavetables/.hidden.wav", 4096);
    fs.dir ("/sdcard/SynthyBoy/Wavetables/Bass");
    fs.file ("/sdcard/SynthyBoy/Wavetables/Bass/deep one.WAV", 8000);
    fs.file ("/sdcard/SynthyBoy/Wavetables/Bass/big.wav", 20u * 1024u * 1024u);
    fs.file ("/sdcard/SynthyBoy/Wavetables/Bass/empty.wav", 0);
    fs.dir ("/sdcard/SynthyBoy/Wavetables/Bass/deeper");   // one folder level only
    fs.file ("/sdcard/SynthyBoy/Wavetables/Bass/deeper/never.wav", 100);
    fs.dir ("/media/USB1/SynthyBoy/Wavetables/bass");       // the same category on another drive, spelt differently: one category
    fs.file ("/media/USB1/SynthyBoy/Wavetables/bass/usb.wav", 4000);
    fs.file ("/media/USB1/SynthyBoy/Wavetables/loose.wav", 4096);   // the same name as on the card: the first root wins, listed once
    CatalogueSources src;
    src.userRoots = { "/sdcard", "/media/USB1", "/nowhere" };
    const WavetableCatalogue c = buildWavetableCatalogue (fs, src);

    const WavetableCategory* user = find (c, "User");
    CHECK (user != nullptr && user->entries.size() == 1);
    CHECK_EQ (user->entries[0].ref.pack, std::string (kUserWavetablePack));
    CHECK_EQ (user->entries[0].ref.file, std::string ("loose.wav"));
    CHECK (user->entries[0].ref.sha256.empty());   // the hash is made when the file is loaded
    CHECK (user->entries[0].source == WavetableEntry::Source::user);
    const WavetableCategory* bass = find (c, "bass");
    CHECK (bass != nullptr);
    CHECK_EQ (bass->entries.size(), static_cast<size_t> (4));   // deep one, big, empty, usb
    int refused = 0;
    for (const auto& e : bass->entries)
        if (! e.refusal.empty())
            ++refused;
    CHECK_EQ (refused, 2);                                   // the 20 MB file and the empty one say why they cannot be loaded
    CHECK_EQ (c.categories.back().name, std::string ("User"));   // User is last
    bool sawBig = false;
    for (const auto& e : bass->entries)
        if (e.title == "big")
        {
            sawBig = true;
            CHECK (e.refusal.find ("too large") != std::string::npos);
        }
    CHECK (sawBig);
    CHECK_EQ (c.categories.size(), static_cast<size_t> (4));   // Basic, Morph, Bass, User
    CHECK_EQ (fs.reads, 0);                                    // a user folder is listed by names, nothing is read
}

TEST (wavetable_catalogue_is_bounded)
{
    FakeFs fs;
    fs.dir ("/r/SynthyBoy");
    fs.dir ("/r/SynthyBoy/Wavetables");
    for (int i = 0; i < 700; ++i)
        fs.file ("/r/SynthyBoy/Wavetables/f" + std::to_string (1000 + i) + ".wav", 4096);
    for (int f = 0; f < 80; ++f)
    {
        const std::string d = "/r/SynthyBoy/Wavetables/cat" + std::to_string (100 + f);
        fs.dir (d);
        fs.file (d + "/a.wav", 4096);
    }
    CatalogueSources src;
    src.userRoots = { "/r" };
    const WavetableCatalogue c = buildWavetableCatalogue (fs, src);
    CHECK (c.truncated);
    CHECK (c.entryCount() <= kMaxCatalogueEntries + 14);
    CHECK (c.categories.size() <= kMaxCatalogueCategories);
    // at most kMaxFolderEntries names of one folder are read
    size_t total = 0;
    for (const auto& cat : c.categories)
        for (const auto& e : cat.entries)
            if (e.isFile())
                ++total;
    CHECK (total <= kMaxFolderEntries + kMaxUserFolders);
}

TEST (wavetable_catalogue_neighbours_step_inside_a_category_without_wrapping_and_skip_refused_files)
{
    FakeFs fs;
    fs.dir ("/r/SynthyBoy");
    fs.dir ("/r/SynthyBoy/Wavetables");
    fs.dir ("/r/SynthyBoy/Wavetables/Pads");
    fs.file ("/r/SynthyBoy/Wavetables/Pads/a.wav", 4096);
    fs.file ("/r/SynthyBoy/Wavetables/Pads/b.wav", 0);                    // refused: stepping skips it
    fs.file ("/r/SynthyBoy/Wavetables/Pads/c.wav", 4096);
    fs.file ("/r/SynthyBoy/Wavetables/Pads/d.wav", 4096);
    CatalogueSources src;
    src.userRoots = { "/r" };
    const WavetableCatalogue c = buildWavetableCatalogue (fs, src);
    int entry = -1;
    const int cat = c.findFile (kUserWavetablePack, "Pads/a.wav", entry);
    CHECK (cat >= 0 && entry == 0);
    int next = -1, index = -1, count = 0;
    CHECK (neighbourFile (c, cat, entry, 1, next));
    CHECK_EQ (c.categories[static_cast<size_t> (cat)].entries[static_cast<size_t> (next)].title, std::string ("c"));
    CHECK (neighbourFile (c, cat, next, 1, next));
    CHECK_EQ (c.categories[static_cast<size_t> (cat)].entries[static_cast<size_t> (next)].title, std::string ("d"));
    CHECK (! neighbourFile (c, cat, next, 1, next));                      // the last file: nothing (no wrap)
    CHECK (! neighbourFile (c, cat, 0, -1, next));                        // the first: nothing
    CHECK (filePosition (c, cat, 3, index, count));
    CHECK (index == 2 && count == 3);                                     // a, c, d: the refused file does not count
    // a built-in table has no neighbours
    int b = -1;
    const int basic = c.findBuiltin (kWavetableSine, b);
    CHECK (! neighbourFile (c, basic, b, 1, next));
}

TEST (wavetable_catalogue_prepare_reads_bounded_hashes_and_refuses_with_a_reason)
{
    // a table file: 2 frames of 2048 samples, 16 bit
    std::vector<float> samples (2 * 2048);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.5f * static_cast<float> (i % 2048) / 2048.0f;
    MemoryContentStore store;
    store.add ("SynthyBoy/Wavetables", "ok.wav", encodeWav (samples.data(), nullptr, samples.size(), 44100, 16));
    store.add ("SynthyBoy/Wavetables", "Pads/text.wav", std::vector<uint8_t> (400, 'x'));
    ContentFileRef ref;
    ref.pack = "SynthyBoy/Wavetables";
    ref.file = "ok.wav";
    ref.name = "ok";
    int frames = 0;
    std::string reason;
    CHECK (prepareWavetableRef (store, ref, frames, reason));
    CHECK_EQ (frames, 2);
    CHECK_EQ (ref.sha256.size(), static_cast<size_t> (64));
    ContentFileRef again = ref;
    CHECK (prepareWavetableRef (store, again, frames, reason));   // with the hash the store checks it: still fine
    ContentFileRef wrong = ref;
    wrong.sha256 = std::string (64, '0');
    CHECK (! prepareWavetableRef (store, wrong, frames, reason));
    ContentFileRef text;
    text.pack = "SynthyBoy/Wavetables";
    text.file = "Pads/text.wav";
    CHECK (! prepareWavetableRef (store, text, frames, reason));
    CHECK_EQ (reason, std::string ("not a WAV file"));
    ContentFileRef missing;
    missing.pack = "SynthyBoy/Wavetables";
    missing.file = "nope.wav";
    CHECK (! prepareWavetableRef (store, missing, frames, reason));
    CHECK_EQ (reason, std::string ("cannot read the file"));
}
