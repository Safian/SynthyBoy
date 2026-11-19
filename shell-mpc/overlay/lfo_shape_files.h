// SynthyBoy - MPC overlay: the user shapes of the LFO editor on the card. The files `<name>.lfoshape.json` in `SynthyBoy/LFO Shapes/` below each of the places the instance offers for the player's own files (the same
// roots as the user wavetables, `IInstanceView::wavetableSources` userRoots: the SD card, the USB drives, the plugin's own `Content` folder), in the format of the core (`synthyboy::serializeLfoShapeFile`:
// {"format":"synthyboy-lfo-shape","version":1,"name":..,"smooth":..,"points":[{"x","y","power"}]}), which the desktop and every other client of the core read. Two optional keys are added: "loop_point" (the point the loop
// marker sits on, as the content state stores it) and "point_mod" (the modulation of the points, an array of {"point":index,"target":"time"|"level"|"curve","source":"<the id of the mod matrix, e.g. lfo2>","amount":-1..1};
// the source is the stable text key of `synthyboy::modSourceTable`, never the number). The parser of the core skips keys it does not know (it reads the root object by key), so the files stay readable there; `version`
// stays 1 (the keys are optional: a file without them loads as it always did, a reader that does not know them ignores them). An entry with an unknown source, target or amount, or a point index outside the shape, is
// skipped on load and the rest is loaded. The keys are read by a small tokenizer (`ShapeText`) that skips strings properly, so a name that holds the text of a key cannot be mistaken for the key.
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
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#include "../safe_file.h"
#include "../ui/lfo_model.h"
#include "synthyboy/modulation.h"
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
    /** Writes the shape under `name` (made safe for a file name; at most 80 bytes), with its loop point and its point modulation. `overwrite` false refuses a name that is there. false: error (). The list is read again. */
    bool save (const std::string& name, const ui::lfo::ShapeModel& m, bool overwrite)
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
    /** The file text of a shape: the core's `serializeLfoShapeFile` with "loop_point" (when the shape has a loop point) and "point_mod" (when a point is modulated) after "smooth". */
    static std::string toText (const std::string& name, const ui::lfo::ShapeModel& m)
    {
        synthyboy::LfoShapeFile f;
        f.name = name;
        f.smooth = m.smooth;
        for (int i = 0; i < m.n; ++i)
            f.points.emplace_back (m.p[i].x, m.p[i].y, m.p[i].power);
        std::string text = synthyboy::serializeLfoShapeFile (f), extra;
        if (m.loop >= 0 && m.loop < m.n)
            extra += ",\"loop_point\":" + std::to_string (m.loop);
        std::string mods;
        for (int i = 0; i < m.n; ++i)
            for (int which = 0; which < 3; ++which)
            {
                const ui::lfo::PointMod& pm = which == 0 ? m.p[i].mx : (which == 1 ? m.p[i].my : m.p[i].mc);
                const char* id = sourceId (pm.source);
                if (! pm.on() || id == nullptr || ! std::isfinite (pm.amount))
                    continue;
                char buf[160];
                std::snprintf (buf, sizeof buf, "%s{\"point\":%d,\"target\":\"%s\",\"source\":\"%s\",\"amount\":%.9g}", mods.empty() ? "" : ",", i, kTargets[which], id, static_cast<double> (std::clamp (pm.amount, -1.0f, 1.0f)));
                mods += buf;
            }
        if (! mods.empty())
            extra += ",\"point_mod\":[" + mods + "]";
        if (! extra.empty())
        {
            const size_t at = text.find (",\"points\":[");   // the first one is the structure: a quote inside the name is escaped
            if (at != std::string::npos)
                text.insert (at, extra);
        }
        return text;
    }
    /** The shape of a file text (what `toText` writes, or any file the core reads); the loop point when "loop_point" is there and names a point, the point modulation of "point_mod" (the entries that make sense). false: not a shape file. */
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
        ShapeText::read (text, m);
        m.clean();
        out = m;
        return true;
    }

private:
    static constexpr const char* kTargets[3] = { "time", "level", "curve" };   // the stable ids of the three modulated values (mx, my, mc of a point)
    /** The text id of the mod matrix for a source choice (1 ..), null when there is none. */
    static const char* sourceId (int choice) noexcept
    {
        return choice >= 1 && choice <= static_cast<int> (synthyboy::modSourceCount()) ? synthyboy::modSourceTable()[choice - 1].key : nullptr;
    }
    /** The choice of a source text id, 0 when no source has it. */
    static int sourceOf (const std::string& id) noexcept
    {
        const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
        for (size_t i = 0; i < synthyboy::modSourceCount(); ++i)
            if (id == t[i].key)
                return static_cast<int> (i) + 1;
        return 0;
    }

    /** A tokenizer for the two optional keys of the root object of a shape file ("loop_point", "point_mod"). It walks the root object key by key and skips every other value as a whole (strings with their escapes, nested
        objects and arrays), so a name or any other string that contains the text of a key is never taken for the key. Anything it cannot follow ends the walk quietly: what was read stays, the rest is left at its default. */
    struct ShapeText
    {
        const char* p;
        const char* e;
        void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
        /** At a quote: the raw text up to the closing quote (backslashes kept), p after it. false: not a string or not closed. */
        bool str (std::string& out)
        {
            if (p >= e || *p != '"')
                return false;
            const char* a = ++p;
            while (p < e && *p != '"')
                p += *p == '\\' && p + 1 < e ? 2 : 1;
            if (p >= e)
                return false;
            out.assign (a, p);
            ++p;
            return true;
        }
        /** Skips one value of any kind. */
        bool skip()
        {
            ws();
            if (p >= e)
                return false;
            if (*p == '"')
            {
                std::string dummy;
                return str (dummy);
            }
            if (*p == '{' || *p == '[')
            {
                int depth = 0;
                while (p < e)
                {
                    if (*p == '"')
                    {
                        std::string dummy;
                        if (! str (dummy))
                            return false;
                        continue;
                    }
                    if (*p == '{' || *p == '[')
                        ++depth;
                    else if (*p == '}' || *p == ']')
                        --depth;
                    ++p;
                    if (depth == 0)
                        return true;
                }
                return false;
            }
            while (p < e && *p != ',' && *p != '}' && *p != ']')
                ++p;
            return true;
        }
        /** A number at p (the value of a key); false when there is none. */
        bool num (double& out)
        {
            ws();
            char* end = nullptr;
            out = std::strtod (p, &end);
            if (end == p || end > e)
                return false;
            p = end;
            return std::isfinite (out);
        }
        /** The next key of the object we are in, p behind the colon; false at the end of the object. */
        bool key (std::string& k)
        {
            ws();
            if (p < e && *p == ',')
                ++p;
            ws();
            if (! str (k))
                return false;
            ws();
            if (p >= e || *p != ':')
                return false;
            ++p;
            ws();
            return true;
        }
        /** One entry {"point":..,"target":..,"source":..,"amount":..} of "point_mod": applied to `m` when every field is valid; p behind the entry in any case (when the entry can be followed at all). */
        bool entry (ui::lfo::ShapeModel& m)
        {
            ws();
            if (p >= e || *p != '{')
                return skip();
            ++p;
            double point = -1.0, amount = 0.0;
            int which = -1, source = 0;
            bool okPoint = false, okAmount = false;
            std::string k, v;
            while (key (k))
            {
                if (k == "point" && num (point))
                    okPoint = true;
                else if (k == "amount" && num (amount))
                    okAmount = true;
                else if ((k == "target" || k == "source") && p < e && *p == '"')
                {
                    if (! str (v))
                        return false;
                    if (k == "target")
                        for (int i = 0; i < 3; ++i)
                            if (v == kTargets[i])
                                which = i;
                    if (k == "source")
                        source = sourceOf (v);
                }
                else if (! skip())
                    return false;
            }
            ws();
            if (p >= e || *p != '}')
                return false;
            ++p;
            if (okPoint && okAmount && which >= 0 && source > 0 && point >= 0.0 && point < static_cast<double> (m.n) && point == std::floor (point))
            {
                const int idx = static_cast<int> (point);
                ui::lfo::PointMod& pm = which == 0 ? m.p[idx].mx : (which == 1 ? m.p[idx].my : m.p[idx].mc);
                pm.source = source;
                pm.amount = static_cast<float> (std::clamp (amount, -1.0, 1.0));
            }
            return true;
        }
        /** Reads "loop_point" and "point_mod" of the root object into `m` (which already holds its points). */
        static void read (const std::string& text, ui::lfo::ShapeModel& m)
        {
            ShapeText t { text.data(), text.data() + text.size() };
            t.ws();
            if (t.p >= t.e || *t.p != '{')
                return;
            ++t.p;
            std::string k;
            while (t.key (k))
            {
                if (k == "loop_point")
                {
                    double v = 0.0;
                    if (! t.num (v))
                        return;
                    if (v >= 0.0 && v < static_cast<double> (m.n))
                        m.loop = static_cast<int> (v);
                }
                else if (k == "point_mod" && t.p < t.e && *t.p == '[')
                {
                    ++t.p;
                    for (;;)
                    {
                        t.ws();
                        if (t.p < t.e && *t.p == ',')
                            ++t.p;
                        t.ws();
                        if (t.p >= t.e || *t.p == ']')
                            break;
                        if (! t.entry (m))
                            return;
                    }
                    if (t.p < t.e)
                        ++t.p;
                }
                else if (! t.skip())
                    return;
            }
        }
    };

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
