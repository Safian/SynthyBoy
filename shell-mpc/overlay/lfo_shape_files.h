// SynthyBoy - MPC overlay: the user shapes of the LFO editor on the card. The files `<name>.lfoshape.json` in `SynthyBoy/LFO Shapes/` below each of the places the instance offers for the player's own files (the same
// roots as the user wavetables, `IInstanceView::wavetableSources` userRoots: the SD card, the USB drives, the plugin's own `Content` folder), in the format of the core (`synthyboy::serializeLfoShapeFile`:
// {"format":"synthyboy-lfo-shape","version":1,"name":..,"smooth":..,"points":[{"x","y","power"}]}), which the desktop and every other client of the core read. One key is added, "loop_point" (the point the loop marker sits
// on, as the content state stores it): the parser of the core skips keys it does not know. The modulation of the points is not part of the format: a shape with modulation is stored without it (`save` says so).
// Pure logic over the file system (the listing and the small reads through synthyboy::ICatalogueFs like the wavetable catalogue, the writes through safe_file.h, the folders with mkdir): the overlay thread calls it when
// a popup opens or a button is tapped, NEVER `draw` and never the audio thread; the tests give it a temporary directory. A list is read when `refresh` is called (no index, no cache: a file copied to the card shows
// the next time the list opens).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#include "../safe_file.h"
#include "../ui/lfo_model.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wavetable_catalogue.h"

namespace synthyboy_mpc { namespace overlay {

class LfoShapeLibrary
{
public:
    struct Item { std::string name, path; };
    static constexpr const char* kFolder = "SynthyBoy/LFO Shapes";   // below a root
    static constexpr const char* kSuffix = ".lfoshape.json";
    static constexpr size_t kMaxItems = 512;                          // the first 512 files of a folder (sorted by name)
    static constexpr size_t kMaxFileBytes = 256 * 1024;               // a shape file larger than this is no shape (128 points are about 8 KB)

    /** The places that may hold SynthyBoy/LFO Shapes (the roots of the instance's user files). */
    void setRoots (std::vector<std::string> roots) { roots_ = std::move (roots); }
    const std::vector<std::string>& roots() const noexcept { return roots_; }
    /** The shapes of every root, by name (case insensitive); a name two roots have is listed once, from the first root. */
    void refresh (const synthyboy::ICatalogueFs& fs = synthyboy::StdCatalogueFs {})
    {
        items_.clear();
        for (const std::string& root : roots_)
        {
            std::vector<synthyboy::ICatalogueFs::Entry> entries;
            bool more = false;
            if (root.empty() || ! fs.list (joinPath (root, kFolder), kMaxItems, entries, &more))
                continue;
            for (const synthyboy::ICatalogueFs::Entry& e : entries)
            {
                if (e.isDir || e.name.empty() || e.name[0] == '.')
                    continue;
                const std::string stem = stemOf (e.name);
                if (stem.empty() || findIn (items_, stem) >= 0)
                    continue;
                items_.push_back ({ stem, joinPath (joinPath (root, kFolder), e.name) });
            }
        }
        std::sort (items_.begin(), items_.end(), [] (const Item& a, const Item& b) { return lower (a.name) < lower (b.name); });
    }
    const std::vector<Item>& items() const noexcept { return items_; }
    int count() const noexcept { return static_cast<int> (items_.size()); }
    /** The index of the shape with this name (as a file name: case insensitive), -1 when there is none. */
    int find (const std::string& name) const { return findIn (items_, synthyboy::sanitizeFileStem (name)); }
    /** True when a shape can be saved: some root exists (the folder is made when it is not there). */
    bool canSave() const { return ! saveDir (false).empty(); }
    /** The reason the last operation failed (the words the editor shows: short, capitals). */
    const std::string& error() const noexcept { return error_; }

    /** Reads shape `index` (its points, SMOOTH and the loop point). false: the file is gone or is no shape file (error ()). */
    bool load (int index, ui::lfo::ShapeModel& out, const synthyboy::ICatalogueFs& fs = synthyboy::StdCatalogueFs {})
    {
        error_.clear();
        if (index < 0 || index >= count())
            return fail ("NO SUCH SHAPE");
        std::vector<uint8_t> bytes;
        if (! fs.readSmall (items_[static_cast<size_t> (index)].path, kMaxFileBytes, bytes))
            return fail ("CANNOT READ THE FILE");
        if (! fromText (std::string (bytes.begin(), bytes.end()), out))
            return fail ("NOT A SHAPE FILE");
        return true;
    }
    /** Writes the shape under `name` (made safe for a file name; at most 80 bytes). `overwrite` false refuses a name that is there. false: error (). `modulationLost` (when not null) says the shape had a point
        modulation that the format cannot hold. The list is read again. */
    bool save (const std::string& name, const ui::lfo::ShapeModel& m, bool overwrite, bool* modulationLost = nullptr)
    {
        error_.clear();
        const std::string shown = trim (name);
        if (shown.empty())
            return fail ("THE NAME IS EMPTY");
        const int at = find (shown);
        if (at >= 0 && ! overwrite)
            return fail ("THAT NAME IS TAKEN");
        std::string path;
        if (at >= 0)
            path = items_[static_cast<size_t> (at)].path;
        else
        {
            const std::string dir = saveDir (true);
            if (dir.empty())
                return fail ("NO CARD OR DRIVE FOUND");
            path = joinPath (dir, synthyboy::sanitizeFileStem (shown) + kSuffix);
        }
        if (modulationLost != nullptr)
            *modulationLost = hasModulation (m);
        const std::string text = toText (shown, m);
        if (! safefile::writeReplacing (path.c_str(), text.data(), text.size()))
            return fail ("CANNOT WRITE THE FILE");
        refresh();
        return true;
    }
    /** Renames shape `index` (the file and the name in it). A name that another shape has is refused. */
    bool rename (int index, const std::string& newName, const synthyboy::ICatalogueFs& fs = synthyboy::StdCatalogueFs {})
    {
        error_.clear();
        if (index < 0 || index >= count())
            return fail ("NO SUCH SHAPE");
        const std::string shown = trim (newName);
        if (shown.empty())
            return fail ("THE NAME IS EMPTY");
        const int other = find (shown);
        if (other >= 0 && other != index)
            return fail ("THAT NAME IS TAKEN");
        ui::lfo::ShapeModel m;
        if (! load (index, m, fs))
            return false;
        const Item old = items_[static_cast<size_t> (index)];
        const size_t slash = old.path.rfind ('/');
        const std::string dir = slash != std::string::npos ? old.path.substr (0, slash) : std::string (".");
        const std::string path = slash != std::string::npos ? joinPath (dir, synthyboy::sanitizeFileStem (shown) + kSuffix) : synthyboy::sanitizeFileStem (shown) + kSuffix;
        const std::string text = toText (shown, m);
        if (! safefile::writeReplacing (path.c_str(), text.data(), text.size()))
            return fail ("CANNOT WRITE THE FILE");
        // the old file goes unless it is the file just written: on a file system that does not tell letter cases apart (FAT / exFAT cards, the Mac) a rename that only changes the case names the SAME file
        if (path != old.path && ! sameFile (path, old.path) && std::remove (old.path.c_str()) != 0)
            return fail ("CANNOT REMOVE THE OLD FILE");
        refresh (fs);
        return true;
    }
    /** Both names are the same file on disk (the same device and inode). */
    static bool sameFile (const std::string& a, const std::string& b)
    {
        struct stat sa, sb;
        return ::stat (a.c_str(), &sa) == 0 && ::stat (b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
    }
    /** Deletes the file of shape `index`. */
    bool remove (int index)
    {
        error_.clear();
        if (index < 0 || index >= count())
            return fail ("NO SUCH SHAPE");
        if (std::remove (items_[static_cast<size_t> (index)].path.c_str()) != 0)
            return fail ("CANNOT DELETE THE FILE");
        refresh();
        return true;
    }

    // ---- the text of a shape file
    static bool hasModulation (const ui::lfo::ShapeModel& m) noexcept
    {
        for (int i = 0; i < m.n; ++i)
            if (m.p[i].mx.on() || m.p[i].my.on() || m.p[i].mc.on())
                return true;
        return false;
    }
    /** The file text of a shape: the core's `serializeLfoShapeFile` with "loop_point" after "smooth" when the shape has a loop point. */
    static std::string toText (const std::string& name, const ui::lfo::ShapeModel& m)
    {
        synthyboy::LfoShapeFile f;
        f.name = name;
        f.smooth = m.smooth;
        for (int i = 0; i < m.n; ++i)
            f.points.emplace_back (m.p[i].x, m.p[i].y, m.p[i].power);
        std::string text = synthyboy::serializeLfoShapeFile (f);
        if (m.loop >= 0 && m.loop < m.n)
        {
            const size_t at = text.find (",\"points\":[");   // the first one is the structure: a quote inside the name is escaped
            if (at != std::string::npos)
                text.insert (at, ",\"loop_point\":" + std::to_string (m.loop));
        }
        return text;
    }
    /** The shape of a file text (what `toText` writes, or any file the core reads); the loop point when "loop_point" is there and names a point. false: not a shape file. */
    static bool fromText (const std::string& text, ui::lfo::ShapeModel& out)
    {
        synthyboy::LfoShapeFile f;
        if (! synthyboy::parseLfoShapeFile (text, f) || f.points.size() < 2 || f.points.size() > static_cast<size_t> (ui::lfo::kMaxPoints))
            return false;
        ui::lfo::ShapeModel m;
        m.n = static_cast<int> (f.points.size());
        for (int i = 0; i < m.n; ++i)
            m.p[i] = ui::lfo::Point (f.points[static_cast<size_t> (i)].x, f.points[static_cast<size_t> (i)].y, f.points[static_cast<size_t> (i)].power);
        m.smooth = f.smooth;
        m.loop = -1;
        const size_t at = text.find ("\"loop_point\":");
        if (at != std::string::npos)
        {
            const long v = std::strtol (text.c_str() + at + std::strlen ("\"loop_point\":"), nullptr, 10);
            if (v >= 0 && v < m.n)
                m.loop = static_cast<int> (v);
        }
        m.clean();
        out = m;
        return true;
    }

private:
    static std::string joinPath (const std::string& a, const std::string& b) { return a.empty() || a.back() == '/' ? a + b : a + "/" + b; }
    static std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }
    static std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace (static_cast<unsigned char> (s[a]))) ++a;
        while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1]))) --b;
        return s.substr (a, b - a);
    }
    /** "Name.lfoshape.json" or "Name.json" -> "Name"; "" for any other file. */
    static std::string stemOf (const std::string& file)
    {
        const size_t sl = std::strlen (kSuffix);
        if (file.size() > sl && file.compare (file.size() - sl, sl, kSuffix) == 0)
            return file.substr (0, file.size() - sl);
        if (file.size() > 5 && file.compare (file.size() - 5, 5, ".json") == 0)
            return file.substr (0, file.size() - 5);
        return {};
    }
    static int findIn (const std::vector<Item>& items, const std::string& stem)
    {
        const std::string l = lower (stem);
        for (size_t i = 0; i < items.size(); ++i)
            if (lower (items[i].name) == l)
                return static_cast<int> (i);
        return -1;
    }
    bool fail (const char* why)
    {
        error_ = why;
        return false;
    }
    static bool isDir (const std::string& p)
    {
        struct stat st;
        return ::stat (p.c_str(), &st) == 0 && S_ISDIR (st.st_mode);
    }
    std::string saveDir (bool make) const { return folderFor (kFolder, make); }

public:
    /** The folder `folder` ("SynthyBoy/LFO Shapes", "SynthyBoy/Wavetables": two levels below a root) that things are written to: the first root that has it, else the first root where it can be made (`make`; the root itself
        must be there; false: only say where it would be). "" when there is no root. */
    std::string folderFor (const char* folder, bool make) const
    {
        for (const std::string& root : roots_)
            if (! root.empty() && isDir (joinPath (root, folder)))
                return joinPath (root, folder);
        for (const std::string& root : roots_)
        {
            if (root.empty() || ! isDir (root))
                continue;
            const std::string full = joinPath (root, folder);
            if (! make)
                return full;
            const size_t cut = std::string (folder).find ('/');
            const std::string first = cut == std::string::npos ? full : joinPath (root, std::string (folder).substr (0, cut));
            if ((isDir (first) || ::mkdir (first.c_str(), 0755) == 0) && (isDir (full) || ::mkdir (full.c_str(), 0755) == 0))
                return full;
        }
        return {};
    }

private:
    std::vector<std::string> roots_;
    std::vector<Item> items_;
    std::string error_;
};

}} // namespace synthyboy_mpc::overlay
