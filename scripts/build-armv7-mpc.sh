#!/usr/bin/env bash
# SynthyBoy - builds the MPC VST2 shell (shell-mpc) for Linux ARMv7 hard-float (Akai MPC Key 61) in a Debian 12 Docker container.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-armv7-mpc.sh [--out DIR] [--jobs N] [--build-dir DIR] [--skins DIR]
#
#   --out DIR        where the finished plugin folder is assembled (default: <repo>/build-armv7-mpc)
#   --jobs N         parallel compile jobs inside the container (default 2)
#   --build-dir DIR  CMake build tree on the host (default: <repo>/build-armv7-mpc/tree)
#   --skins DIR      copy this directory verbatim into Plugin Skins/ in the output folder
#
# Environment: MPC_SDK (required: path to the MPC plugin SDK), ARMV7_IMAGE (default: the pinned Debian 12 image below).
#
# Why Debian 12: its toolchain (GCC 12.2, glibc 2.36, libstdc++ 3.4.30) is the one the Kick app for the MPC is built with
# (kick-armv7-builder), so the module needs nothing from the device that the Kick binary does not need already. The source tree is
# mounted read-only; nothing is written outside the build directory and the output folder. Exit codes: 0 built, 2 usage, 3 Docker is
# not usable (the manual commands are printed), 1 build / check failed.

set -euo pipefail

IMAGE_DEFAULT="debian:12-slim@sha256:734a72b3d6142a71993b4bc580ca01d8ca687f77a2c422d96a961d3376b179bc"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="$repo_root/build-armv7-mpc"
build_dir="$repo_root/build-armv7-mpc/tree"
jobs=2
skins_dir=""
image="${ARMV7_IMAGE:-$IMAGE_DEFAULT}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out_dir="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --skins) skins_dir="$2"; shift 2 ;;
        -h|--help) sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-armv7-mpc.sh: unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

if [[ -n "$skins_dir" && ! -d "$skins_dir" ]]; then echo "build-armv7-mpc.sh: --skins: not a directory: $skins_dir" >&2; exit 2; fi
[[ "$jobs" =~ ^[0-9]+$ && "$jobs" -ge 1 ]] || { echo "build-armv7-mpc.sh: --jobs needs a positive number" >&2; exit 2; }

if [[ -z "${MPC_SDK:-}" ]]; then
    echo "build-armv7-mpc.sh: MPC_SDK environment variable is required" >&2
    exit 2
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    echo "build-armv7-mpc.sh: Docker is not installed or its daemon is not running." >&2
    cat >&2 <<'EOF'
Docker is not usable. On a Debian 12 / Ubuntu x86_64 or aarch64 Linux host run (from the repository root):

  sudo dpkg --add-architecture armhf && sudo apt-get update
  sudo apt-get install -y --no-install-recommends cmake ninja-build git ca-certificates file binutils-arm-linux-gnueabihf \
      g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf
  cmake -S . -B build-armv7-mpc/tree -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armv7-linux-gnueabihf.cmake \
      -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON \
      -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR=$PWD/mpc-plugin-builder-sdk
  cmake --build build-armv7-mpc/tree --target synthyboy_mpc --parallel 2
  # the result: build-armv7-mpc/tree/shell-mpc/synthyboy_mpc.so

The glibc / libstdc++ of the build host are what the module needs on the device: build on Debian 12 (glibc 2.36), not on a newer distribution.
EOF
    exit 3
fi

mkdir -p "$build_dir" "$out_dir"
build_dir="$(cd "$build_dir" && pwd)"
out_dir="$(cd "$out_dir" && pwd)"

echo "build-armv7-mpc.sh: image $image"
echo "build-armv7-mpc.sh: source $repo_root (read-only), build tree $build_dir, jobs $jobs"

# Runs as root inside the container (apt needs it); the results are handed back to the host user at the end.
docker run --rm \
    -v "$repo_root":/src:ro -v "$build_dir":/build -v "$out_dir":/out \
    -e JOBS="$jobs" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -e MPC_SDK_DIR="/mpc-sdk" \
    -v "$MPC_SDK":/mpc-sdk:ro \
    "$image" bash -euo pipefail -c '
        trap '"'"'chown -R "$HOST_UID:$HOST_GID" /build /out'"'"' EXIT
        need=()
        command -v arm-linux-gnueabihf-g++ >/dev/null || need+=(g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf)
        command -v arm-linux-gnueabihf-readelf >/dev/null || need+=(binutils-arm-linux-gnueabihf)
        command -v cmake >/dev/null || need+=(cmake)
        command -v file >/dev/null || need+=(file)
        command -v git >/dev/null || need+=(git ca-certificates)
        command -v ninja >/dev/null || command -v make >/dev/null || need+=(make)
        if [ "${#need[@]}" -gt 0 ]; then
            apt-get update -qq
            DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${need[@]}" ca-certificates
        fi
        cmake --version | head -1
        arm-linux-gnueabihf-g++ --version | head -1
        # Determine generator: Ninja if available, else Unix Makefiles
        if command -v ninja >/dev/null 2>&1; then
            GEN="Ninja"
        else
            GEN="Unix Makefiles"
        fi
        cmake -S /src -B /build -G "$GEN" -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/armv7-linux-gnueabihf.cmake \
            -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON \
            -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR="$MPC_SDK_DIR"
        cmake --build /build --target synthyboy_mpc --parallel "$JOBS"
        so=/build/shell-mpc/synthyboy_mpc.so
        file "$so"
        arm-linux-gnueabihf-readelf -A "$so" | grep -E "Tag_CPU_arch:|Tag_ABI_VFP_args|Tag_FP_arch"
        # Check that the .so needs only libc, libm, libdl, libpthread, ld-linux
        needed="$(arm-linux-gnueabihf-readelf -d "$so" | grep NEEDED || true)"
        echo "$needed"
        bad="$(echo "$needed" | grep -Ev "\\[(libc|libm|libdl|libpthread|ld-linux-armhf)\\.so\\.[0-9]+\\]" || true)"
        if [ -n "$bad" ]; then echo "build-armv7-mpc.sh: the .so needs libraries beyond libc/libm/libdl/libpthread/ld-linux:" >&2; echo "$bad" >&2; exit 1; fi
        # Check that only VSTPluginMain is exported
        exported="$(arm-linux-gnueabihf-nm -D --defined-only "$so" | grep -w VSTPluginMain || true)"
        if [ -z "$exported" ]; then echo "build-armv7-mpc.sh: VSTPluginMain is not an exported symbol" >&2; exit 1; fi
        extra="$(arm-linux-gnueabihf-nm -D --defined-only "$so" | grep -v -w VSTPluginMain | grep -v "^$" || true)"
        if [ -n "$extra" ]; then echo "build-armv7-mpc.sh: unexpected exported symbols:" >&2; echo "$extra" >&2; exit 1; fi
        echo "build-armv7-mpc.sh: ARMv7 build OK"
    '

# Assemble the plugin folder on the host
PLUGIN_NAME="Safian - VST - SynthyBoy"
plugin_folder="$out_dir/$PLUGIN_NAME"
mkdir -p "$plugin_folder/VST"
cp "$build_dir/shell-mpc/synthyboy_mpc.so" "$plugin_folder/VST/synthyboy-arm32.so"

# Create Content/ folder (empty if nothing to put there)
mkdir -p "$plugin_folder/Content"

# Copy Plugin Skins/ if --skins DIR was given
if [[ -n "$skins_dir" ]]; then
    rm -rf "$plugin_folder/Plugin Skins"
    cp -R "$skins_dir" "$plugin_folder/Plugin Skins"
fi

# Generate plugin.json and version.xml
python3 "$repo_root/scripts/gen-mpc-manifest.py" --plugin-dir "$plugin_folder"

# The SDK's own validator on the finished folder. Its schema lists file64 as required, while docs/plugin-folder-format.md of the SDK says
# a 32-bit MPC uses only file32; this package has no 64-bit binary, so exactly that one finding is accepted. Any other error fails the build.
echo "build-armv7-mpc.sh: running SDK validator on $plugin_folder"
report="$(python3 "$MPC_SDK/tools/validate.py" "$plugin_folder" 2>&1 || true)"
echo "$report"
other="$(echo "$report" | grep '^ERROR' | grep -v 'plugin.json is missing required keys: file64$' || true)"
if [[ -n "$other" ]] || ! echo "$report" | grep -q '^OK'; then echo "build-armv7-mpc.sh: SDK validator failed" >&2; exit 1; fi
if echo "$report" | grep -q '^ERROR'; then echo "build-armv7-mpc.sh: note: the validator's file64 finding is expected (32-bit only package)"; fi

echo "build-armv7-mpc.sh: done $plugin_folder"
