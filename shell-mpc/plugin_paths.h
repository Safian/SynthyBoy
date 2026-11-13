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
// Terminates at the filesystem root. Never throws: the plugin runs inside the host's process, so a file system error
// (no permission, a volume that went away) or an allocation failure gives the empty result instead of an exception.
inline std::string pluginFolderFor (const std::string& binaryPath) noexcept
{
    try
    {
        if (binaryPath.empty())
            return {};

        std::filesystem::path dir = std::filesystem::path (binaryPath).parent_path();
        while (! dir.empty())
        {
            std::error_code ec;
            if (std::filesystem::is_regular_file (dir / "plugin.json", ec) && ! ec)
                return dir.string();

            std::filesystem::path parent = dir.parent_path();
            if (parent == dir)  // reached the filesystem root
                break;
            dir = std::move (parent);
        }
    }
    catch (...) {}
    return {};
}

// Returns <pluginFolder>/Content.
inline std::string contentDirFor (const std::string& pluginFolder) noexcept
{
    try { return pluginFolder + "/Content"; } catch (...) { return {}; }
}

} // namespace synthyboy_mpc

#endif // SYNTHYBOY_MPC_PLUGIN_PATHS_H
