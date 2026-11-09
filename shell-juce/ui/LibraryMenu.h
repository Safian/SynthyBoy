// SynthyBoy - UI: the menu of a user library category (WP-01 f): save the current item, load / favourite / delete the saved ones, load from a file.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One class serves LFO shape presets, formula presets, FX rack presets and (WP-07) voice control presets: the category decides the directory
// (UserLibrary), the spec supplies the two format functions. The operations are plain methods so that the tests call exactly what the menu items call.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "UiContext.h"

namespace synthyboy::ui
{
/** Asks for a line of text (a preset name). The shell shows a modal dialog; tests install a handler that answers at once. */
using TextPromptHandler = std::function<void (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)>;
void setTextPromptHandler (TextPromptHandler handler);
void askText (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done);

struct LibraryMenuSpec
{
    std::string category;                                              // kLibLfoShapes ...
    juce::String noun;                                                 // "shape", "formula", "FX rack"
    std::function<std::string (const juce::String& name)> makeItem;    // the text of the current item; empty = nothing to save
    std::function<bool (const juce::String& name, const std::string& json)> applyItem; // false = the item is not usable
    juce::String fileWildcards;                                        // "*.json;*.vitallfo": a file the menu may load directly ("" = no file entry)
    std::function<bool (const juce::File& file)> applyFile;            // loads a file chosen with "Load from file..."
    juce::String suggestedName;                                        // name proposed by "Save current as..."
};

class LibraryMenu : public std::enable_shared_from_this<LibraryMenu>
{
public:
    LibraryMenu (UiContext& context, LibraryMenuSpec specIn);

    /** Fills `menu` with the entries of this library, ids from `firstId`; the actions are kept until the next build. */
    void addTo (juce::PopupMenu& menu, int firstId = 1000);
    /** Runs the action of an id returned by the menu; false for an id of another menu. */
    bool run (int id);
    void show (juce::Component& target);

    // the operations behind the entries
    bool saveCurrent (const juce::String& name, bool overwrite = true);
    bool load (const juce::String& name);
    bool setFavourite (const juce::String& name, bool favourite);
    bool remove (const juce::String& name);
    std::vector<LibraryEntry> entries() const;

    /** The last status line ("Saved ...", "Could not ..."). */
    const juce::String& message() const noexcept { return lastMessage; }
    std::function<void (const juce::String&)> onMessage;

private:
    void say (const juce::String& text);

    UiContext& ctx;
    LibraryMenuSpec spec;
    std::vector<std::function<void()>> actions;
    int baseId = 0;
    juce::String lastMessage;
};
} // namespace synthyboy::ui
