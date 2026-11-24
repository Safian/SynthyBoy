// SynthyBoy - core: the user library of small JSON items (user_library.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/user_library.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "synthyboy/user_content.h"

namespace synthyboy
{
namespace fs = std::filesystem;

namespace
{
std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}
} // namespace

UserLibrary::UserLibrary (const std::string& userDir) : root_ (userDir.empty() ? std::string() : (fs::path (userDir) / "library").string()) {}

bool UserLibrary::isCategory (const std::string& category) noexcept
{
    return category == kLibLfoShapes || category == kLibFormulas || category == kLibFxRacks || category == kLibVoiceControl;
}

std::string UserLibrary::pathOf (const std::string& category, const std::string& name) const
{
    if (root_.empty() || ! isCategory (category) || name.empty())
        return {};
    return (fs::path (root_) / category / (sanitizeFileStem (name) + ".json")).string();
}

std::vector<std::string> UserLibrary::readFavourites (const std::string& category) const
{
    std::vector<std::string> names;
    if (root_.empty() || ! isCategory (category))
        return names;
    std::ifstream in (fs::path (root_) / category / "favourites.txt");
    std::string line;
    while (std::getline (in, line))
    {
        while (! line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (! line.empty())
            names.push_back (line);
    }
    return names;
}

bool UserLibrary::writeFavourites (const std::string& category, const std::vector<std::string>& names) const
{
    std::error_code ec;
    const fs::path dir = fs::path (root_) / category;
    fs::create_directories (dir, ec);
    std::ofstream out (dir / "favourites.txt", std::ios::trunc);
    for (const auto& n : names)
        out << n << '\n';
    return out.good();
}

std::vector<LibraryEntry> UserLibrary::list (const std::string& category) const
{
    std::vector<LibraryEntry> entries;
    if (root_.empty() || ! isCategory (category))
        return entries;
    const auto favourites = readFavourites (category);
    std::error_code ec;
    for (fs::directory_iterator it (fs::path (root_) / category, ec), end; ! ec && it != end && entries.size() < kMaxLibraryItems; it.increment (ec))
    {
        std::error_code e2;
        if (! it->is_regular_file (e2) || it->path().extension() != ".json")
            continue;
        LibraryEntry e;
        e.name = it->path().stem().string();
        e.favourite = std::find (favourites.begin(), favourites.end(), e.name) != favourites.end();
        entries.push_back (std::move (e));
    }
    std::sort (entries.begin(), entries.end(), [] (const LibraryEntry& a, const LibraryEntry& b)
    {
        return a.favourite != b.favourite ? a.favourite : lower (a.name) < lower (b.name);
    });
    return entries;
}

bool UserLibrary::exists (const std::string& category, const std::string& name) const
{
    const std::string path = pathOf (category, name);
    std::error_code ec;
    return ! path.empty() && fs::is_regular_file (path, ec);
}

bool UserLibrary::save (const std::string& category, const std::string& name, const std::string& json, bool overwrite)
{
    const std::string path = pathOf (category, name);
    if (path.empty() || json.empty() || json.size() > kMaxLibraryItemBytes || (! overwrite && exists (category, name)))
        return false;
    std::error_code ec;
    fs::create_directories (fs::path (path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        if (! out)
            return false;
        out.write (json.data(), static_cast<std::streamsize> (json.size()));
        out.close();
        if (! out.good())
        {
            fs::remove (tmp, ec);
            return false;
        }
    }
    fs::rename (tmp, path, ec);
    if (ec)
    {
        fs::remove (tmp, ec);
        return false;
    }
    return true;
}

bool UserLibrary::load (const std::string& category, const std::string& name, std::string& json) const
{
    const std::string path = pathOf (category, name);
    if (path.empty())
        return false;
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (text.empty() || text.size() > kMaxLibraryItemBytes)
        return false;
    json = std::move (text);
    return true;
}

bool UserLibrary::remove (const std::string& category, const std::string& name)
{
    const std::string path = pathOf (category, name);
    if (path.empty())
        return false;
    std::error_code ec;
    fs::remove (path, ec);
    setFavourite (category, name, false);
    return ! fs::exists (path, ec);
}

bool UserLibrary::isFavourite (const std::string& category, const std::string& name) const
{
    const auto f = readFavourites (category);
    return std::find (f.begin(), f.end(), sanitizeFileStem (name)) != f.end();
}

bool UserLibrary::setFavourite (const std::string& category, const std::string& name, bool favourite)
{
    if (root_.empty() || ! isCategory (category) || name.empty())
        return false;
    auto f = readFavourites (category);
    const std::string key = sanitizeFileStem (name);
    const auto it = std::find (f.begin(), f.end(), key);
    if (favourite && it == f.end())
        f.push_back (key);
    else if (! favourite && it != f.end())
        f.erase (it);
    else
        return true;
    return writeFavourites (category, f);
}
} // namespace synthyboy
