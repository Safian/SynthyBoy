// SynthyBoy - core: the wavetable catalogue (see wavetable_catalogue.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_catalogue.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>

#include "sha256.h"
#include "synthyboy/content_pack.h"
#include "synthyboy/content_store.h"
#include "synthyboy/params.h"
#include "synthyboy/wavetable_file.h"

namespace synthyboy
{
namespace
{
std::string lower (std::string s)
{
    for (char& c : s)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

bool hasWavExtension (const std::string& name)
{
    const size_t dot = name.rfind ('.');
    return dot != std::string::npos && dot > 0 && lower (name.substr (dot)) == ".wav";
}

std::string stemOf (const std::string& fileName)
{
    const size_t slash = fileName.rfind ('/');
    std::string s = slash == std::string::npos ? fileName : fileName.substr (slash + 1);
    const size_t dot = s.rfind ('.');
    if (dot != std::string::npos && dot > 0)
        s.erase (dot);
    return s;
}

bool skipName (const std::string& name)
{
    return name.empty() || name[0] == '.' || name == "lost+found" || name == "System Volume Information";
}

/** The text of a state reference may not hold a drive, a back slash or a control character and is at most kMaxContentStringBytes long (content_state.h): a file that would make the state refuse
    its whole content section is refused here with a reason. */
std::string storableRefusal (const std::string& pack, const std::string& file, const std::string& title)
{
    auto bad = [] (const std::string& s)
    {
        for (const char c : s)
            if (c == ':' || c == '\\' || static_cast<unsigned char> (c) < 0x20)
                return true;
        return false;
    };
    if (file.size() > kMaxContentStringBytes || pack.size() > kMaxContentStringBytes || title.size() > kMaxContentStringBytes)
        return "the name is too long";
    if (bad (file) || bad (pack) || bad (title))
        return "the name has a character that cannot be kept (: or \\)";
    return {};
}

std::string sizeRefusal (uint64_t size) { return wavetableSizeRefusal (size); }

/** Collects the categories by the lower case of their name; keeps the first spelling. */
struct Builder
{
    WavetableCatalogue cat;
    std::map<std::string, size_t> index;
    size_t files = 0;

    WavetableCategory* category (const std::string& name)
    {
        const std::string key = lower (name);
        const auto it = index.find (key);
        if (it != index.end())
            return &cat.categories[it->second];
        if (cat.categories.size() >= kMaxCatalogueCategories)
        {
            cat.truncated = true;
            return nullptr;
        }
        index[key] = cat.categories.size();
        cat.categories.push_back ({ name, {} });
        return &cat.categories.back();
    }
    bool add (const std::string& categoryName, WavetableEntry e)
    {
        if (files >= kMaxCatalogueEntries)
        {
            cat.truncated = true;
            return false;
        }
        WavetableCategory* c = category (categoryName);
        if (c == nullptr)
            return false;
        c->entries.push_back (std::move (e));
        ++files;
        return true;
    }
};

void addBuiltins (Builder& b)
{
    const ParamInfo* info = findParamByKey ("osc1.wavetable");
    auto name = [&] (int choice) -> std::string
    {
        if (info != nullptr && info->choices != nullptr && choice >= 0 && choice < static_cast<int> (info->numChoices))
            return info->choices[choice];
        return "Table " + std::to_string (choice);
    };
    static const int kBasic[] = { kWavetableSine, kWavetableSaw, 2, 3, 4, kWavetableSawDown };   // Sine, Saw, Square, Triangle, Pulse 25%, Saw Down: the one cycle tables
    for (const int c : kBasic)
    {
        WavetableEntry e;
        e.builtinChoice = c;
        e.title = name (c);
        b.category (kBasicCategory)->entries.push_back (std::move (e));
    }
    for (int c = 5; c < kWavetablePresetTable; ++c)   // the eight morphing tables
    {
        WavetableEntry e;
        e.builtinChoice = c;
        e.title = name (c);
        b.category (kMorphCategory)->entries.push_back (std::move (e));
    }
}

void addPacks (const ICatalogueFs& fs, const CatalogueSources& sources, Builder& b)
{
    size_t packsRead = 0;
    for (const std::string& dir : sources.packDirs)
    {
        std::vector<ICatalogueFs::Entry> subs;
        bool more = false;
        if (dir.empty() || ! fs.list (dir, kMaxFolderEntries, subs, &more))
            continue;
        std::sort (subs.begin(), subs.end(), [] (const ICatalogueFs::Entry& x, const ICatalogueFs::Entry& y) { return lower (x.name) < lower (y.name); });
        for (const auto& sub : subs)
        {
            if (! sub.isDir || skipName (sub.name))
                continue;
            if (packsRead >= kMaxCataloguePacks)
            {
                b.cat.truncated = true;
                return;
            }
            std::vector<uint8_t> manifest;
            ContentPackInfo info;
            if (! fs.readSmall (dir + "/" + sub.name + "/contentpack.json", kMaxManifestFileBytes, manifest) || ! parseContentPackManifest (manifest.data(), manifest.size(), info) || info.kind != "wavetables")
                continue;
            ++packsRead;
            size_t n = 0;
            for (const ContentPackFile& f : info.files)
            {
                if (! hasWavExtension (f.path))
                    continue;
                if (++n > kMaxPackFiles)
                {
                    b.cat.truncated = true;
                    break;
                }
                WavetableEntry e;
                e.source = WavetableEntry::Source::pack;
                e.title = ! f.title.empty() ? f.title : stemOf (f.path);
                e.ref.pack = sub.name;   // the name of the pack's folder: that is where the store looks (it is the pack id of a pack made by the tool)
                e.ref.file = f.path;
                e.ref.sha256 = f.sha256;
                e.ref.name = e.title;
                e.size = f.size;
                e.refusal = f.size > 0 ? sizeRefusal (f.size) : std::string();
                if (e.refusal.empty())
                    e.refusal = storableRefusal (e.ref.pack, e.ref.file, e.ref.name);
                std::string category = f.category;
                if (category.empty())
                {
                    const size_t slash = f.path.find ('/');
                    category = slash != std::string::npos ? f.path.substr (0, slash) : (! info.name.empty() ? info.name : std::string ("Other"));
                }
                if (! b.add (category, std::move (e)))
                    break;
            }
        }
    }
}

void addUser (const ICatalogueFs& fs, const CatalogueSources& sources, Builder& b)
{
    std::map<std::string, bool> seen;   // pack-relative path (lower case): the first root that has a file wins, the store reads the same one
    auto addFile = [&] (const std::string& category, const std::string& relative, const ICatalogueFs::Entry& entry)
    {
        if (! seen.emplace (lower (relative), true).second)
            return true;
        WavetableEntry e;
        e.source = WavetableEntry::Source::user;
        e.title = stemOf (entry.name);
        e.ref.pack = kUserWavetablePack;
        e.ref.file = relative;
        e.ref.name = e.title;
        e.size = entry.size;
        e.refusal = sizeRefusal (entry.size);
        if (e.refusal.empty())
            e.refusal = storableRefusal (e.ref.pack, e.ref.file, e.ref.name);
        return b.add (category, std::move (e));
    };
    for (const std::string& root : sources.userRoots)
    {
        if (root.empty())
            continue;
        const std::string base = root + "/" + kUserWavetablePack;
        std::vector<ICatalogueFs::Entry> top;
        bool more = false;
        if (! fs.list (base, kMaxFolderEntries, top, &more))
            continue;
        if (more)
            b.cat.truncated = true;
        std::sort (top.begin(), top.end(), [] (const ICatalogueFs::Entry& x, const ICatalogueFs::Entry& y) { return lower (x.name) < lower (y.name); });
        size_t folders = 0;
        for (const auto& t : top)
        {
            if (skipName (t.name))
                continue;
            if (! t.isDir)
            {
                if (hasWavExtension (t.name) && ! addFile (kUserWavetableCategory, t.name, t))
                    return;
                continue;
            }
            if (++folders > kMaxUserFolders)
            {
                b.cat.truncated = true;
                continue;
            }
            std::vector<ICatalogueFs::Entry> inner;
            bool innerMore = false;
            if (! fs.list (base + "/" + t.name, kMaxFolderEntries, inner, &innerMore))
                continue;
            if (innerMore)
                b.cat.truncated = true;
            std::sort (inner.begin(), inner.end(), [] (const ICatalogueFs::Entry& x, const ICatalogueFs::Entry& y) { return lower (x.name) < lower (y.name); });
            for (const auto& f : inner)
                if (! f.isDir && ! skipName (f.name) && hasWavExtension (f.name) && ! addFile (t.name, t.name + "/" + f.name, f))
                    return;
        }
    }
}

bool lessCategory (const WavetableCategory& a, const WavetableCategory& b)
{
    auto rank = [] (const WavetableCategory& c)
    {
        const std::string n = lower (c.name);
        if (n == lower (kBasicCategory)) return 0;
        if (n == lower (kMorphCategory)) return 1;
        if (n == lower (kUserWavetableCategory)) return 3;
        return 2;
    };
    const int ra = rank (a), rb = rank (b);
    return ra != rb ? ra < rb : lower (a.name) < lower (b.name);
}
} // namespace

// ---- the file system -------------------------------------------------------------------------------------------------------------------

bool StdCatalogueFs::list (const std::string& dir, size_t maxEntries, std::vector<Entry>& out, bool* more) const
{
    namespace fs = std::filesystem;
    out.clear();
    if (more != nullptr)
        *more = false;
    try
    {
        std::error_code ec;
        fs::directory_iterator it (fs::path (dir), fs::directory_options::skip_permission_denied, ec);
        if (ec)
            return false;
        for (const fs::directory_iterator end; it != end; it.increment (ec))
        {
            if (ec)
                break;
            if (out.size() >= maxEntries)
            {
                if (more != nullptr)
                    *more = true;
                break;
            }
            std::error_code e2;
            const fs::file_status st = it->status (e2);   // follows a symbolic link
            if (e2)
                continue;
            Entry e;
            e.name = it->path().filename().string();
            e.isDir = fs::is_directory (st);
            if (! e.isDir)
            {
                if (! fs::is_regular_file (st))
                    continue;
                const std::uintmax_t size = it->file_size (e2);
                e.size = e2 ? 0 : static_cast<uint64_t> (size);
            }
            out.push_back (std::move (e));
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool StdCatalogueFs::readSmall (const std::string& path, size_t maxBytes, std::vector<uint8_t>& out) const
{
    out.clear();
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;
    uint8_t buffer[4096];
    size_t n;
    bool ok = true;
    try
    {
        while ((n = std::fread (buffer, 1, sizeof buffer, f)) > 0)
        {
            out.insert (out.end(), buffer, buffer + n);
            if (out.size() > maxBytes)
            {
                ok = false;
                break;
            }
        }
    }
    catch (...)
    {
        ok = false;
    }
    std::fclose (f);
    if (! ok)
        out.clear();
    return ok;
}

// ---- the catalogue ---------------------------------------------------------------------------------------------------------------------

size_t WavetableCatalogue::entryCount() const noexcept
{
    size_t n = 0;
    for (const auto& c : categories)
        n += c.entries.size();
    return n;
}

int WavetableCatalogue::findFile (const std::string& pack, const std::string& file, int& entry) const noexcept
{
    for (size_t c = 0; c < categories.size(); ++c)
        for (size_t i = 0; i < categories[c].entries.size(); ++i)
        {
            const WavetableEntry& e = categories[c].entries[i];
            if (e.isFile() && e.ref.pack == pack && e.ref.file == file)
            {
                entry = static_cast<int> (i);
                return static_cast<int> (c);
            }
        }
    entry = -1;
    return -1;
}

int WavetableCatalogue::findBuiltin (int choice, int& entry) const noexcept
{
    for (size_t c = 0; c < categories.size(); ++c)
        for (size_t i = 0; i < categories[c].entries.size(); ++i)
            if (! categories[c].entries[i].isFile() && categories[c].entries[i].builtinChoice == choice)
            {
                entry = static_cast<int> (i);
                return static_cast<int> (c);
            }
    entry = -1;
    return -1;
}

int WavetableCatalogue::findCategory (const std::string& name) const noexcept
{
    const std::string key = lower (name);
    for (size_t c = 0; c < categories.size(); ++c)
        if (lower (categories[c].name) == key)
            return static_cast<int> (c);
    return -1;
}

WavetableCatalogue buildWavetableCatalogue (const ICatalogueFs& fs, const CatalogueSources& sources)
{
    Builder b;
    try
    {
        addBuiltins (b);
        addPacks (fs, sources, b);
        addUser (fs, sources, b);
        // the built-in tables keep their order at the head of their category, the files follow sorted by title
        for (WavetableCategory& c : b.cat.categories)
            std::stable_sort (c.entries.begin(), c.entries.end(), [] (const WavetableEntry& x, const WavetableEntry& y)
            {
                if (x.isFile() != y.isFile())
                    return ! x.isFile();
                if (! x.isFile())
                    return false;   // the built-ins: the order they were added in
                const std::string tx = lower (x.title), ty = lower (y.title);
                return tx != ty ? tx < ty : x.ref.file < y.ref.file;
            });
        std::stable_sort (b.cat.categories.begin(), b.cat.categories.end(), lessCategory);
    }
    catch (...)
    {
        b.cat.truncated = true;
    }
    return std::move (b.cat);
}

bool neighbourFile (const WavetableCatalogue& catalogue, int category, int entry, int dir, int& outEntry) noexcept
{
    if (category < 0 || category >= static_cast<int> (catalogue.categories.size()) || dir == 0)
        return false;
    const auto& list = catalogue.categories[static_cast<size_t> (category)].entries;
    if (entry < 0 || entry >= static_cast<int> (list.size()) || ! list[static_cast<size_t> (entry)].isFile())
        return false;
    const int step = dir > 0 ? 1 : -1;
    for (int i = entry + step; i >= 0 && i < static_cast<int> (list.size()); i += step)
        if (list[static_cast<size_t> (i)].isFile() && list[static_cast<size_t> (i)].refusal.empty())
        {
            outEntry = i;
            return true;
        }
    return false;
}

bool neighbourEntry (const WavetableCatalogue& catalogue, int category, int entry, int dir, int& outCategory, int& outEntry) noexcept
{
    const int categories = static_cast<int> (catalogue.categories.size());
    if (category < 0 || category >= categories || dir == 0)
        return false;
    if (entry < 0 || entry >= static_cast<int> (catalogue.categories[static_cast<size_t> (category)].entries.size()))
        return false;
    const int step = dir > 0 ? 1 : -1;
    for (int c = category; c >= 0 && c < categories; c += step)
    {
        const auto& list = catalogue.categories[static_cast<size_t> (c)].entries;
        const int count = static_cast<int> (list.size());
        for (int i = c == category ? entry + step : (step > 0 ? 0 : count - 1); i >= 0 && i < count; i += step)
        {
            const WavetableEntry& e = list[static_cast<size_t> (i)];
            if (! e.isFile() || e.refusal.empty())
            {
                outCategory = c;
                outEntry = i;
                return true;
            }
        }
    }
    return false;
}

bool filePosition (const WavetableCatalogue& catalogue, int category, int entry, int& index, int& count) noexcept
{
    if (category < 0 || category >= static_cast<int> (catalogue.categories.size()))
        return false;
    const auto& list = catalogue.categories[static_cast<size_t> (category)].entries;
    if (entry < 0 || entry >= static_cast<int> (list.size()) || ! list[static_cast<size_t> (entry)].isFile())
        return false;
    index = -1;
    count = 0;
    for (int i = 0; i < static_cast<int> (list.size()); ++i)
        if (list[static_cast<size_t> (i)].isFile() && (list[static_cast<size_t> (i)].refusal.empty() || i == entry))
        {
            if (i == entry)
                index = count;
            ++count;
        }
    return index >= 0;
}

bool prepareWavetableRef (const IContentStore& store, ContentFileRef& ref, int& frames, std::string& reason)
{
    frames = 0;
    try
    {
        std::vector<uint8_t> bytes;
        bool tooLarge = false;
        if (! store.readFileBounded (ref, bytes, kMaxWavetableFileBytes, &tooLarge))
        {
            reason = tooLarge ? wavetableSizeRefusal (kMaxWavetableFileBytes + 1) : (ref.sha256.empty() ? std::string ("cannot read the file") : std::string ("cannot read the file, or it is not the one its pack lists"));
            return false;
        }
        const WavetableParse parsed = parseWavetableWav (bytes.data(), bytes.size());
        if (! parsed.ok)
        {
            reason = parsed.reason;
            return false;
        }
        ref.sha256 = Sha256::hashBufferHex (bytes.data(), bytes.size());
        frames = parsed.data.frames;
        return true;
    }
    catch (...)
    {
        reason = "the file could not be read";
        return false;
    }
}
} // namespace synthyboy
