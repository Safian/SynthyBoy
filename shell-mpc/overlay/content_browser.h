// SynthyBoy - MPC overlay: the content browser of the oscillator UI. The list of the sources a sample, granular or multisample oscillator can load: the roots the plugin offers (the SD card of the
// MPC, the plugin's own content folder, the USB drives), the folders below them, the audio files (WAV) and the SFZ instruments, browsed one folder at a time. Pure logic over the file system
// (POSIX directory reading, no threads): the oscillator UI shows it in the list popup, the tests give it a directory tree.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

namespace synthyboy_mpc { namespace overlay {

class ContentBrowser
{
public:
    struct Root { std::string label, path; };
    /** What a row is: 0 the first row (the title / the way up), 1 a folder, 2 a file that can be loaded. */
    struct Row { std::string text, path; int kind = 0; };

    static constexpr size_t kMaxRows = 4000;                       // a folder with more entries shows the first ones (sorted)
    static constexpr const char* kSampleKind = "wav";             // kind 1: a sample file for the granular oscillator
    // kind 2 (an instrument for the multisample oscillator): an SFZ file, or a WAV (one zone over the whole keyboard)

    void setRoots (std::vector<Root> roots) { roots_ = std::move (roots); }
    const std::vector<Root>& roots() const noexcept { return roots_; }

    /** Starts at the list of the roots (kind 1 sample, 2 instrument). */
    void open (int kind)
    {
        kind_ = kind;
        folder_.clear();
        rootIdx_ = -1;
        refresh();
    }
    /** Starts in the folder of `file` when it lies below a root (the file that is loaded now), else at the roots. */
    void openAt (int kind, const std::string& file)
    {
        open (kind);
        for (size_t i = 0; i < roots_.size(); ++i)
        {
            const std::string& r = roots_[i].path;
            if (! r.empty() && file.size() > r.size() && file.compare (0, r.size(), r) == 0 && file[r.size()] == '/')
            {
                rootIdx_ = static_cast<int> (i);
                folder_ = file.substr (0, file.rfind ('/'));
                refresh();
                return;
            }
        }
    }
    int kind() const noexcept { return kind_; }
    bool atRoots() const noexcept { return rootIdx_ < 0; }
    const std::string& folder() const noexcept { return folder_; }
    const std::vector<Row>& rows() const noexcept { return rows_; }
    /** The texts of the rows as the list popup wants them (a pointer array that stays valid until the next change). */
    const char* const* items() const noexcept { return ptrs_.data(); }
    int count() const noexcept { return static_cast<int> (rows_.size()); }

    enum class Action { None, Changed, Chosen };
    /** A tap on row `i`: the first row goes up one folder (nothing at the roots), a folder opens, a file is chosen (chosen () is its path). */
    Action tap (int i)
    {
        if (i < 0 || i >= count())
            return Action::None;
        const Row& r = rows_[static_cast<size_t> (i)];
        if (r.kind == 0)
        {
            if (rootIdx_ < 0)
                return Action::None;
            if (folder_ == roots_[static_cast<size_t> (rootIdx_)].path)
            {
                rootIdx_ = -1;
                folder_.clear();
            }
            else
                folder_ = folder_.substr (0, folder_.rfind ('/'));
            refresh();
            return Action::Changed;
        }
        if (r.kind == 1)
        {
            if (rootIdx_ < 0)
                for (size_t k = 0; k < roots_.size(); ++k)
                    if (roots_[k].path == r.path)
                        rootIdx_ = static_cast<int> (k);
            folder_ = r.path;
            refresh();
            return Action::Changed;
        }
        chosen_ = r.path;
        return Action::Chosen;
    }
    const std::string& chosen() const noexcept { return chosen_; }
    /** The row of the file (or folder) with this path, -1 when it is not in the list. */
    int rowOf (const std::string& path) const noexcept
    {
        if (path.empty())
            return -1;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].path == path)
                return static_cast<int> (i);
        return -1;
    }

    /** Is a file with this name something the browser lists for kind 1 / 2? */
    static bool loadable (int kind, const std::string& name)
    {
        const size_t dot = name.rfind ('.');
        if (dot == std::string::npos)
            return false;
        std::string ext = name.substr (dot + 1);
        for (char& c : ext)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return ext == "wav" || (kind == 2 && ext == "sfz");
    }

private:
    static std::string lower (std::string s)
    {
        for (char& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }
    void refresh()
    {
        rows_.clear();
        std::string head;
        if (rootIdx_ < 0)
            head = "SOURCES";
        else
        {
            const size_t cut = folder_.rfind ('/');
            head = "\xe2\x86\x90 " + (folder_ == roots_[static_cast<size_t> (rootIdx_)].path ? roots_[static_cast<size_t> (rootIdx_)].label : (cut == std::string::npos ? folder_ : folder_.substr (cut + 1)));
        }
        rows_.push_back ({ head, folder_, 0 });
        if (rootIdx_ < 0)
        {
            for (const Root& r : roots_)
                rows_.push_back ({ r.label + " \xe2\x96\xb6", r.path, 1 });
        }
        else
        {
            std::vector<Row> dirs, files;
            if (DIR* d = ::opendir (folder_.c_str()))
            {
                while (const dirent* e = ::readdir (d))
                {
                    const std::string name = e->d_name;
                    if (name.empty() || name[0] == '.' || name == "lost+found")
                        continue;
                    const std::string path = folder_ + "/" + name;
                    struct stat st;
                    if (::stat (path.c_str(), &st) != 0)
                        continue;
                    if (S_ISDIR (st.st_mode))
                        dirs.push_back ({ name + " \xe2\x96\xb6", path, 1 });
                    else if (S_ISREG (st.st_mode) && loadable (kind_, name))
                        files.push_back ({ name, path, 2 });
                    if (dirs.size() + files.size() >= kMaxRows * 2)
                        break;
                }
                ::closedir (d);
            }
            auto byName = [] (const Row& a, const Row& b) { return lower (a.text) < lower (b.text); };
            std::sort (dirs.begin(), dirs.end(), byName);
            std::sort (files.begin(), files.end(), byName);
            for (Row& r : dirs) { if (rows_.size() >= kMaxRows) break; rows_.push_back (std::move (r)); }
            for (Row& r : files) { if (rows_.size() >= kMaxRows) break; rows_.push_back (std::move (r)); }
        }
        ptrs_.clear();
        for (const Row& r : rows_)
            ptrs_.push_back (r.text.c_str());
    }

    std::vector<Root> roots_;
    std::vector<Row> rows_;
    std::vector<const char*> ptrs_;
    std::string folder_, chosen_;
    int kind_ = 1, rootIdx_ = -1;
};

}} // namespace synthyboy_mpc::overlay
