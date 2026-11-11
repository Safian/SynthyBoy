// SynthyBoy - bitmap GUI: prev / next over the presets on disk (see PresetBrowser.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PresetBrowser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <tuple>

#include "synthyboy/shell_features.h"

namespace synthyboy::bitmap
{
namespace
{
std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}
} // namespace

PresetBrowser::PresetBrowser (std::string userDirectory) : userDir (std::move (userDirectory))
{
    refresh();
}

void PresetBrowser::refresh()
{
    list.clear();
    if (! userDir.empty())
    {
        const std::filesystem::path root (userDir);
        for (const auto& folder : { root / "presets", root / "packs" })
        {
            std::error_code ec;
            if (std::filesystem::is_directory (folder, ec))
            {
                auto found = scanPresetDirectory (folder.string());
                list.insert (list.end(), std::make_move_iterator (found.begin()), std::make_move_iterator (found.end()));
            }
        }
    }
    std::sort (list.begin(), list.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        const auto ka = std::make_tuple (lower (a.meta.category), lower (a.meta.name), a.path);
        const auto kb = std::make_tuple (lower (b.meta.category), lower (b.meta.name), b.path);
        return ka < kb;
    });
    current = -1;
    for (size_t i = 0; i < list.size() && ! currentPath.empty(); ++i)
        if (list[i].path == currentPath)
            current = static_cast<int> (i);
}

bool PresetBrowser::step (int direction, Loaded& out)
{
    if (list.empty())
        return false;
    const int count = static_cast<int> (list.size());
    if (current < 0)
        current = direction < 0 ? count - 1 : 0;
    else
        current = ((current + (direction < 0 ? -1 : 1)) % count + count) % count;
    const PresetEntry& entry = list[static_cast<size_t>(current)];
    currentPath = entry.path;

    std::ifstream file (entry.path, std::ios::binary);
    if (! file)
        return false;
    std::string text ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char>());
    if (text.empty() || text.size() > 8u * kMaxStateBytes)
        return false;

    auto values = ParamValues::defaults();
    StateContent content;
    PresetMeta meta;
    const LoadResult result = loadPreset (text.data(), text.size(), meta, values, content);
    if (result.status != LoadStatus::ok && result.status != LoadStatus::newer_version)
        return false;

    ShellState shell; // a preset may carry a MIDI CC map of its own
    const ShellStatus shellStatus = loadShellSection (text.data(), text.size(), shell);
    out.values = values;
    out.content = std::move (content);
    out.midiMap = shellStatus == ShellStatus::ok ? shell.midiCc : std::vector<std::pair<int, std::string>>();
    out.name = meta.name.empty() ? std::filesystem::path (entry.path).stem().string() : meta.name;
    name = out.name;
    return true;
}
} // namespace synthyboy::bitmap
