// SynthyBoy - shell-mpc acceptance: the plugin folder is found from the binary's path (walk up to the folder that holds plugin.json).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../plugin_paths.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

static int fails = 0;
static void check (bool ok, const char* what, const std::string& got)
{
    std::printf ("%s  %s (got '%s')\n", ok ? "PASS" : "FAIL", what, got.c_str());
    if (! ok) ++fails;
}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("sb-mpc-paths-" + std::to_string (std::rand()));
    // the real layout on the device: the drive folder is called "VST2 Plugins", the binary sits in "VST"
    const fs::path plugin = root / "VST2 Plugins" / "Safian - VST - SynthyBoy";
    fs::create_directories (plugin / "VST");
    std::ofstream (plugin / "plugin.json") << "{}";
    std::ofstream (plugin / "VST" / "synthyboy-arm32.so") << "x";

    std::string got = synthyboy_mpc::pluginFolderFor ((plugin / "VST" / "synthyboy-arm32.so").string());
    check (fs::path (got) == plugin, "binary in VST/ below a 'VST2 Plugins' drive folder", got);

    // binary directly in the plugin folder
    std::ofstream (plugin / "flat.so") << "x";
    got = synthyboy_mpc::pluginFolderFor ((plugin / "flat.so").string());
    check (fs::path (got) == plugin, "binary directly in the plugin folder", got);

    // binary two levels down
    fs::create_directories (plugin / "VST" / "arm32");
    got = synthyboy_mpc::pluginFolderFor ((plugin / "VST" / "arm32" / "deep.so").string());
    check (fs::path (got) == plugin, "binary two levels below the plugin folder", got);

    // no plugin.json anywhere above: empty result, no crash, no endless loop
    const fs::path lone = root / "build" / "shell-mpc";
    fs::create_directories (lone);
    got = synthyboy_mpc::pluginFolderFor ((lone / "synthyboy_mpc.so").string());
    check (got.empty(), "no plugin.json above the binary gives an empty string", got);
    got = synthyboy_mpc::pluginFolderFor ("");
    check (got.empty(), "empty path gives an empty string", got);

    // a path that does not exist at all, a bare file name, and the root
    got = synthyboy_mpc::pluginFolderFor ((root / "gone" / "deeper" / "x.so").string());
    check (got.empty(), "a path that does not exist gives an empty string", got);
    got = synthyboy_mpc::pluginFolderFor ("x.so");
    check (got.empty() || fs::exists (fs::path (got) / "plugin.json"), "a bare file name does not crash or loop", got);
    got = synthyboy_mpc::pluginFolderFor ("/");
    check (got.empty() || fs::exists (fs::path (got) / "plugin.json"), "the root path does not crash or loop", got);
    static_assert (noexcept (synthyboy_mpc::pluginFolderFor (std::string())), "pluginFolderFor must be noexcept");
    static_assert (noexcept (synthyboy_mpc::contentDirFor (std::string())), "contentDirFor must be noexcept");

    const std::string content = synthyboy_mpc::contentDirFor (plugin.string());
    check (fs::path (content) == plugin / "Content", "content directory is <plugin folder>/Content", content);

    fs::remove_all (root);
    std::printf ("paths_test: %s\n", fails == 0 ? "OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
