#!/usr/bin/env bash
# SynthyBoy - builds the MPC VST2 shell (shell-mpc) for the 32-bit (ARMv7 hard-float: MPC Key 61 and the other 32-bit MPCs) and the 64-bit (AArch64: MPC XL,
# Live III) MPCs in a Debian 12 Docker container and assembles ONE plugin folder with both binaries.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-mpc.sh [--arch arm32|arm64|all] [--out DIR] [--jobs N] [--build-dir DIR] [--skins DIR] [--bench] [--no-validate]
#
#   --arch A         arm32 (VST/synthyboy-arm32.so, "file32"), arm64 (VST/synthyboy-arm64.so, "file64") or all (default: both)
#   --out DIR        where the finished plugin folder is assembled (default: <repo>/build-mpc)
#   --jobs N         parallel compile jobs inside the container (default 2)
#   --build-dir DIR  root of the CMake trees on the host; one sub directory per architecture (default: <repo>/build-mpc/tree)
#   --skins DIR      copy this directory verbatim into Plugin Skins/ in the output folder instead of generating it
#                    (default: generated from scripts/mpc-ui-pages.json with gen-mpc-ui.py --layout and the SDK's tools/gen_ui.py,
#                    then scripts/apply-mpc-skin-extras.py)
#   --bench          also build synthyboy_bench and synthyboy_noteon_bench for each architecture (own CMake tree with SYNTHYBOY_BUILD_TESTS=ON, libstdc++
#                    linked statically) into <out>/bench-<arch>/; they are measurement tools and not part of the plugin folder
#   --no-validate    skip the SDK validator at the end (for tests without binaries)
#
# Environment: MPC_SDK (required: path to the MPC plugin SDK), MPC_BUILD_IMAGE or ARMV7_IMAGE (the Docker image; default: the pinned Debian 12 image below;
# an image that already has the cross compilers skips the apt-get step), MPC_DOCKER (the docker command, default: docker).
#
# The plugin folder is assembled from the binaries of THIS run only (VST/ is emptied first), so plugin.json never lists a binary of an older build.
# Why Debian 12: its toolchain (GCC 12.2, glibc 2.36, libstdc++ 3.4.30) is the one the Kick app for the MPC is built with (kick-armv7-builder) and it is
# below the glibc 2.39 of the MPC OS 3 firmware, which the SDK validator enforces. The source tree is mounted read-only; nothing is written outside the
# build directory and the output folder. Exit codes: 0 built, 2 usage, 3 Docker is not usable (the manual commands are printed), 1 build / check failed.

set -euo pipefail

IMAGE_DEFAULT="debian:12-slim@sha256:734a72b3d6142a71993b4bc580ca01d8ca687f77a2c422d96a961d3376b179bc"
me="build-mpc.sh"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch=all
out_dir="$repo_root/build-mpc"
build_dir="$repo_root/build-mpc/tree"
jobs=2
skins_dir=""
with_bench=0
validate=1
image="${MPC_BUILD_IMAGE:-${ARMV7_IMAGE:-$IMAGE_DEFAULT}}"
docker_cmd="${MPC_DOCKER:-docker}"

usage() { sed -n '/^# Usage/,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch|--out|--jobs|--build-dir|--skins)
            [[ $# -ge 2 ]] || { echo "$me: $1 needs a value" >&2; exit 2; }
            case "$1" in
                --arch) arch="$2" ;;
                --out) out_dir="$2" ;;
                --jobs) jobs="$2" ;;
                --build-dir) build_dir="$2" ;;
                --skins) skins_dir="$2" ;;
            esac
            shift 2 ;;
        --bench) with_bench=1; shift ;;
        --no-validate) validate=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "$me: unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

case "$arch" in
    arm32) archs=(arm32) ;;
    arm64) archs=(arm64) ;;
    all) archs=(arm32 arm64) ;;
    *) echo "$me: --arch must be arm32, arm64 or all (got: $arch)" >&2; exit 2 ;;
esac
if [[ -n "$skins_dir" && ! -d "$skins_dir" ]]; then echo "$me: --skins: not a directory: $skins_dir" >&2; exit 2; fi
[[ "$jobs" =~ ^[0-9]+$ && "$jobs" -ge 1 ]] || { echo "$me: --jobs needs a positive number" >&2; exit 2; }

if [[ -z "${MPC_SDK:-}" ]]; then
    echo "$me: MPC_SDK environment variable is required" >&2
    exit 2
fi

if ! command -v "$docker_cmd" >/dev/null 2>&1 || ! "$docker_cmd" info >/dev/null 2>&1; then
    echo "$me: Docker is not installed or its daemon is not running." >&2
    cat >&2 <<'EOF'
Docker is not usable. On a Debian 12 x86_64 or aarch64 Linux host run (from the repository root; use "arm-linux-gnueabihf" + armv7-linux-gnueabihf.cmake for
--arch arm32 and "aarch64-linux-gnu" + aarch64-linux-gnu.cmake for --arch arm64):

  sudo apt-get update && sudo apt-get install -y --no-install-recommends cmake ninja-build git ca-certificates file \
      binutils-aarch64-linux-gnu g++-aarch64-linux-gnu gcc-aarch64-linux-gnu binutils-arm-linux-gnueabihf g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf
  cmake -S . -B build-mpc/tree/arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
      -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON \
      -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR=$PWD/mpc-plugin-builder-sdk
  cmake --build build-mpc/tree/arm64 --target synthyboy_mpc --parallel 2
  # the result: build-mpc/tree/arm64/shell-mpc/synthyboy_mpc.so (strip it with aarch64-linux-gnu-strip --strip-unneeded)

The glibc / libstdc++ of the build host are what the module needs on the device: build on Debian 12 (glibc 2.36), not on a newer distribution (the MPC
OS 3 firmware has glibc 2.39).
EOF
    exit 3
fi

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
for a in "${archs[@]}"; do
    mkdir -p "$build_dir/$a" "$build_dir/$a-bench"
done
build_dir="$(cd "$build_dir" && pwd)"

echo "$me: image $image, architectures: ${archs[*]}"
echo "$me: source $repo_root (read-only), build trees $build_dir/<arch>, jobs $jobs"

# ---- 1. the modules (and the benchmarks), one container per architecture ----------------------------------------------------------------------------
# Runs as root inside the container (apt needs it); the results are handed back to the host user at the end of mpc-container-build.sh.
for a in "${archs[@]}"; do
    echo "$me: === building $a ==="
    "$docker_cmd" run --rm \
        -v "$repo_root":/src:ro -v "$build_dir/$a":/build -v "$build_dir/$a-bench":/build-bench -v "$out_dir":/out -v "$MPC_SDK":/mpc-sdk:ro \
        -e ARCH="$a" -e JOBS="$jobs" -e WITH_BENCH="$with_bench" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e MPC_SDK_DIR=/mpc-sdk \
        "$image" bash /src/scripts/mpc-container-build.sh
done

# ---- 2. the plugin folder (shared by both architectures) --------------------------------------------------------------------------------------------
PLUGIN_NAME="Safian - VST - SynthyBoy"
plugin_folder="$out_dir/$PLUGIN_NAME"
rm -rf "$plugin_folder/VST"
mkdir -p "$plugin_folder/VST"
for a in "${archs[@]}"; do
    cp "$build_dir/$a/shell-mpc/synthyboy_mpc.stripped.so" "$plugin_folder/VST/synthyboy-$a.so"
done

# Create Content/ folder (empty if nothing to put there)
mkdir -p "$plugin_folder/Content"

# Copy factory presets
rm -rf "$plugin_folder/Content/presets"
cp -R "$repo_root/content/presets" "$plugin_folder/Content/presets"

# Plugin Skins/: the given directory, or generated from the page list
rm -rf "$plugin_folder/Plugin Skins"
if [[ -n "$skins_dir" ]]; then
    cp -R "$skins_dir" "$plugin_folder/Plugin Skins"
else
    ui_tmp="$(mktemp -d)"
    python3 "$repo_root/scripts/gen-mpc-ui.py" --params "$repo_root/docs/params.json" --pages "$repo_root/docs/kick-pages.json" \
        --layout "$repo_root/scripts/mpc-ui-pages.json" --out "$ui_tmp"
    python3 "$MPC_SDK/tools/gen_ui.py" --params "$ui_tmp/params-hostsim.json" --ui "$ui_tmp/ui.json" --out "$plugin_folder/Plugin Skins" --quiet
    # Graphical extras (scripts/mpc-skin-extras/*.py) on top of the generated skin; not for a given --skins directory
    python3 "$repo_root/scripts/apply-mpc-skin-extras.py" --skins "$plugin_folder/Plugin Skins" --params "$repo_root/docs/params.json"
    rm -rf "$ui_tmp"
fi

# Generate plugin.json (file32 / file64 for the binaries that exist) and version.xml
python3 "$repo_root/scripts/gen-mpc-manifest.py" --plugin-dir "$plugin_folder"

# ---- 3. the SDK's own validator on the finished folder ----------------------------------------------------------------------------------------------
# Its schema lists both file32 and file64 as required, while docs/plugin-folder-format.md of the SDK says a 32-bit MPC uses only file32 and a 64-bit one
# only file64. With both binaries built there is nothing to accept. A single-architecture build accepts exactly the one finding about the key of the
# architecture that was not built; any other error fails the build.
if [[ "$validate" == 1 ]]; then
    echo "$me: running SDK validator on $plugin_folder"
    report="$(python3 "$MPC_SDK/tools/validate.py" "$plugin_folder" 2>&1 || true)"
    echo "$report"
    expected_missing=""
    [[ "$arch" == arm32 ]] && expected_missing="file64"
    [[ "$arch" == arm64 ]] && expected_missing="file32"
    other="$(echo "$report" | grep '^ERROR' || true)"
    if [[ -n "$expected_missing" ]]; then other="$(echo "$other" | grep -v "plugin.json is missing required keys: $expected_missing\$" || true)"; fi
    if [[ -n "$other" ]] || ! echo "$report" | grep -q '^OK'; then echo "$me: SDK validator failed" >&2; exit 1; fi
    if [[ -n "$expected_missing" ]] && echo "$report" | grep -q '^ERROR'; then
        echo "$me: note: the validator's $expected_missing finding is expected (--arch $arch builds one binary only)"
    fi
fi

echo "$me: done $plugin_folder"
if [[ "$with_bench" == 1 ]]; then for a in "${archs[@]}"; do echo "$me: benchmarks: $out_dir/bench-$a/"; done; fi
