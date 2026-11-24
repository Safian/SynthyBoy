// SynthyBoy - bitmap GUI: prev / next over the presets on disk, through the core's preset code (JUCE free).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The strip of the editor has two arrows and a name. The shell that has no preset list of a host behind it (the native shell inside Kick's CLAP host) steps through what is installed:
// <user dir>/presets and every content pack below <user dir>/packs (the factory pack ships there), sorted by category, name and path. Reading and loading is core/include/synthyboy/preset.h.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "synthyboy/preset.h"
#include "synthyboy/state.h"

namespace synthyboy::bitmap
{
class PresetBrowser
{
public:
    explicit PresetBrowser (std::string userDirectory);

    /** Scans the folders again (the current preset is found again by its path). */
    void refresh();
    size_t size() const noexcept { return list.size(); }

    struct Loaded
    {
        std::string name;
        ParamValues values;
        StateContent content;
        std::vector<std::pair<int, std::string>> midiMap; // the preset's own CC map, empty when it has none
    };
    /** Loads the neighbour of the current preset (direction -1 / +1, wrapping). Starts at the first (+1) or the last (-1) entry when no preset is current. False when there is none or the file
        does not load; the current preset moves on in both cases so that a broken file does not block the arrow. */
    bool step (int direction, Loaded& out);

    /** The name shown in the strip: the last loaded preset, "Init" before any. */
    const std::string& currentName() const noexcept { return name; }
    void setCurrentName (std::string newName) { name = std::move (newName); }

private:
    std::string userDir;
    std::vector<PresetEntry> list;
    int current = -1;
    std::string currentPath;
    std::string name = "Init";
};
} // namespace synthyboy::bitmap
