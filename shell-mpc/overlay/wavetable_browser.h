// SynthyBoy - MPC overlay: the wavetable browser of the oscillator UI. The list of the tables the oscillator can play, in two levels like the path style list of the content browser (content_browser.h): the categories
// (Basic, Morph, the categories of the installed packs, the folders of the player, User), and the tables of one category with the current one marked. Pure logic over a WavetableCatalogue (core/include/synthyboy/
// wavetable_catalogue.h, built when the browser opens): the oscillator UI shows it in the list popup (its header bar is the title and the way up), the tests give it a catalogue.
// A tap on a built-in table is "choose this choice of oscN.wavetable"; a tap on a file is "load this file" (the entry carries the reference); a file the catalogue knows cannot be loaded (too large, empty) says why in
// its row and a tap on it changes nothing. A file that was refused while loading is marked the same way for the rest of the session (markRefused).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/wavetable_catalogue.h"
#include "synthyboy/wavetable_file.h"

namespace synthyboy_mpc { namespace overlay {

class WavetableBrowser
{
public:
    enum class Action { None, Changed, ChoseBuiltin, ChoseFile, Refused };

    /** Opens on `catalogue`. The category of the current table is shown, with the table marked: the built-in table `currentChoice` (the value of oscN.wavetable, -1 when the oscillator plays a file) or the file
        `currentPack` / `currentFile` (empty: none). When the current table is in no category the list of the categories shows. */
    void open (synthyboy::WavetableCatalogue catalogue, int currentChoice, const std::string& currentPack, const std::string& currentFile)
    {
        cat_ = std::move (catalogue);
        curChoice_ = currentChoice;
        curPack_ = currentPack;
        curFile_ = currentFile;
        level_ = 0;
        category_ = -1;
        int entry = -1;
        int c = -1;
        if (! currentFile.empty())
            c = cat_.findFile (currentPack, currentFile, entry);
        else if (currentChoice >= 0)
            c = cat_.findBuiltin (currentChoice, entry);
        if (c >= 0)
        {
            category_ = c;
            level_ = 1;
        }
        refresh();
    }
    const synthyboy::WavetableCatalogue& catalogue() const noexcept { return cat_; }
    bool atCategories() const noexcept { return level_ == 0; }
    /** The header of the list: "WAVETABLES" over the categories, the name of the category over its tables. */
    const std::string& title() const noexcept { return title_; }
    /** The rows as the list popup wants them (a pointer array that stays valid until the next change). */
    const char* const* items() const noexcept { return ptrs_.data(); }
    int count() const noexcept { return static_cast<int> (rows_.size()); }
    const std::string& text (int row) const { return rows_[static_cast<size_t> (row)].text; }
    /** The row of the current table (level 1) or of its category (level 0); -1 when it is not in the list. */
    int currentRow() const noexcept { return current_; }

    /** A tap on row `i`: a category opens, a table is chosen (chosenChoice () / chosenEntry ()), a refused file changes nothing (Refused, refusalText ()). */
    Action tap (int i)
    {
        if (i < 0 || i >= count())
            return Action::None;
        const Row& r = rows_[static_cast<size_t> (i)];
        if (level_ == 0)
        {
            category_ = r.category;
            level_ = 1;
            refresh();
            return Action::Changed;
        }
        const synthyboy::WavetableEntry& e = cat_.categories[static_cast<size_t> (r.category)].entries[static_cast<size_t> (r.entry)];
        if (! e.isFile())
        {
            chosenChoice_ = e.builtinChoice;
            return Action::ChoseBuiltin;
        }
        if (! noteOf (e).empty())
        {
            refusal_ = synthyboy::wavetableRefusalText (noteOf (e));
            return Action::Refused;
        }
        chosenEntry_ = e;
        chosenCategory_ = r.category;
        chosenIndex_ = r.entry;
        return Action::ChoseFile;
    }
    /** The way up (the header): from the tables to the categories; nothing over the categories. */
    Action up()
    {
        if (level_ == 0)
            return Action::None;
        level_ = 0;
        refresh();
        return Action::Changed;
    }
    int chosenChoice() const noexcept { return chosenChoice_; }
    const synthyboy::WavetableEntry& chosenEntry() const noexcept { return chosenEntry_; }
    int chosenCategory() const noexcept { return chosenCategory_; }
    int chosenIndex() const noexcept { return chosenIndex_; }
    const std::string& refusalText() const noexcept { return refusal_; }

    /** A file the loader refused: its row says why from now on (the reason without the "not a wavetable file: " in front). */
    void markRefused (const std::string& pack, const std::string& file, const std::string& reason)
    {
        refused_[pack + "|" + file] = reason;
        refresh();
    }

private:
    struct Row { std::string text; int category = -1, entry = -1; };

    std::string noteOf (const synthyboy::WavetableEntry& e) const
    {
        if (! e.refusal.empty())
            return e.refusal;
        const auto it = refused_.find (e.ref.pack + "|" + e.ref.file);
        return it != refused_.end() ? it->second : std::string();
    }
    void refresh()
    {
        rows_.clear();
        current_ = -1;
        if (level_ == 0)
        {
            title_ = "WAVETABLES";
            for (size_t c = 0; c < cat_.categories.size(); ++c)
            {
                if (cat_.categories[c].entries.empty())
                    continue;
                if (static_cast<int> (c) == category_)
                    current_ = static_cast<int> (rows_.size());
                rows_.push_back ({ cat_.categories[c].name + " \xe2\x96\xb6", static_cast<int> (c), -1 });
            }
        }
        else
        {
            const synthyboy::WavetableCategory& cat = cat_.categories[static_cast<size_t> (category_)];
            title_ = cat.name;
            for (size_t i = 0; i < cat.entries.size(); ++i)
            {
                const synthyboy::WavetableEntry& e = cat.entries[i];
                std::string text = e.title;
                if (e.isFile())
                {
                    const std::string note = noteOf (e);
                    if (! note.empty())
                        text += "  (" + note + ")";
                    else if (! curFile_.empty() && e.ref.pack == curPack_ && e.ref.file == curFile_)
                        current_ = static_cast<int> (rows_.size());
                }
                else if (curFile_.empty() && curChoice_ >= 0 && e.builtinChoice == curChoice_)
                    current_ = static_cast<int> (rows_.size());
                rows_.push_back ({ std::move (text), category_, static_cast<int> (i) });
            }
        }
        ptrs_.clear();
        for (const Row& r : rows_)
            ptrs_.push_back (r.text.c_str());
    }

    synthyboy::WavetableCatalogue cat_;
    std::vector<Row> rows_;
    std::vector<const char*> ptrs_;
    std::map<std::string, std::string> refused_;
    std::string title_, curPack_, curFile_, refusal_;
    synthyboy::WavetableEntry chosenEntry_;
    int level_ = 0, category_ = -1, current_ = -1, curChoice_ = -1, chosenChoice_ = -1, chosenCategory_ = -1, chosenIndex_ = -1;
};

}} // namespace synthyboy_mpc::overlay
