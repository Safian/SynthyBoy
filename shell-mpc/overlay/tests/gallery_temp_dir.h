// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace synthyboy_mpc::overlay::gallery
{
struct GalleryTempDir
{
    std::string path = (std::filesystem::temp_directory_path() / "synthyboy_gallery_XXXXXX").string();
    GalleryTempDir()
    {
        if (! ::mkdtemp (path.data()))
            throw std::runtime_error ("Could not create the gallery temporary directory");
    }
    GalleryTempDir (const GalleryTempDir&) = delete;
    GalleryTempDir& operator= (const GalleryTempDir&) = delete;
    ~GalleryTempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all (path, ec);
    }
};
}
