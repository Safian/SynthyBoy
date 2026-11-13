// SynthyBoy - MPC shell plugin folder / content path helpers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef SYNTHYBOY_MPC_PLUGIN_PATHS_H
#define SYNTHYBOY_MPC_PLUGIN_PATHS_H

#include <algorithm>
#include <string>
#include <filesystem>

namespace synthyboy_mpc {

// Starting at the directory that holds the binary, walk upwards and return
// the first directory that contains a file named "plugin.json".
// Returns an empty string if none is found or the path is empty.
// Terminates at the filesystem root.
inline std::string pluginFolderFor (const std::string& binaryPath)
{
    if (binaryPath.empty())
        return {};

    std::filesystem::path dir (binaryPath);
    dir = dir.parent_path();

    while (true)
    {
        if (std::filesystem::is_regular_file (dir / "plugin.json"))
            return dir.string();

        auto parent = dir.parent_path();
        if (parent == dir)  // reached filesystem root
            break;
        dir = std::move (parent);
    }
    return {};
}

// Returns <pluginFolder>/Content.
inline std::string contentDirFor (const std::string& pluginFolder)
{
    return pluginFolder + "/Content";
}

} // namespace synthyboy_mpc

#endif // SYNTHYBOY_MPC_PLUGIN_PATHS_H
