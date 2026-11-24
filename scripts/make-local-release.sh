#!/usr/bin/env bash
# SynthyBoy - builds a complete local release into dist/ without GitHub: the Kick installer flow can be tested with it.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/make-local-release.sh [--out DIR] [--jobs N] [--no-armv7] [--no-host] [--require-armv7] [--with-juce]
#                                      [--armv7-clap FILE] [--host-clap FILE] [--file-urls]
#
#   --out DIR         result directory (default: <repo>/dist)
#   --jobs N          parallel compile jobs (default 2 for the Docker build, the CPU count for the host build)
#   --no-armv7        do not build linux-armv7 (the MPC Key 61)
#   --no-host         do not build the host platform (macos-universal on macOS, linux-x86_64 / linux-aarch64 on Linux)
#   --require-armv7   Docker not usable = error (default: a loud warning with the manual commands, the rest is still built)
#   --with-juce       also build the JUCE CLAP / VST3 of the host platform (scripts/build.sh) and put them into its zip under juce/
#   --armv7-clap FILE use this already built ARMv7 hard-float synthyboy.clap instead of building it (checked by package-release.py)
#   --host-clap FILE  use this already built native synthyboy.clap of the host platform instead of building it
#   --file-urls       absolute file:// urls in plugin.json.release (default: urls relative to the manifest, so dist/ can be copied)
#
# What it does: scripts/build-armv7-clap.sh (Docker, Debian 12), the host build of the native CLAP shell (cmake target synthyboy_clap),
# scripts/package-release.py per platform, tools/contentpack/contentpack.py build-all (the packs of plugin.json), then
# scripts/fill-manifest.py --partial (only what was built is listed: Kick rejects a build without a SHA-256) and
# scripts/check-manifest.py --local on the result. Output in DIR: <id>-<version>-<platform>.zip, <packid>-<packversion>.zip,
# plugin.json.release, SHA256SUMS. Nothing is uploaded, no tag or release is created, nothing is sent to a device.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="$repo_root/dist"
jobs=""
do_armv7=1
do_host=1
require_armv7=0
with_juce=0
armv7_clap=""
host_clap=""
url_style="--relative"

die() { echo "make-local-release.sh: $*" >&2; exit "${2:-1}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) [[ $# -ge 2 ]] || die "--out needs a directory" 2; out_dir="$2"; shift 2 ;;
        --jobs) [[ $# -ge 2 ]] || die "--jobs needs a number" 2; jobs="$2"; shift 2 ;;
        --no-armv7) do_armv7=0; shift ;;
        --no-host) do_host=0; shift ;;
        --require-armv7) require_armv7=1; shift ;;
        --with-juce) with_juce=1; shift ;;
        --armv7-clap) [[ $# -ge 2 ]] || die "--armv7-clap needs a file" 2; armv7_clap="$2"; shift 2 ;;
        --host-clap) [[ $# -ge 2 ]] || die "--host-clap needs a file" 2; host_clap="$2"; shift 2 ;;
        --file-urls) url_style="--file-urls"; shift ;;
        -h|--help) sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1 (see --help)" 2 ;;
    esac
done
[[ -z "$jobs" || "$jobs" =~ ^[0-9]+$ && "$jobs" -ge 1 ]] || die "--jobs needs a positive number" 2

command -v python3 >/dev/null || die "python3 not found"
py=(python3 -I)

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
# a stale zip of an earlier version or platform must not end up in the manifest or the sums
rm -f "$out_dir"/*.zip "$out_dir"/*.zip.sha256 "$out_dir/plugin.json.release" "$out_dir/SHA256SUMS"

"${py[@]}" "$repo_root/scripts/check-manifest.py"

manifest_field() { "${py[@]}" -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$repo_root/plugin.json" "$1"; }
version="$(manifest_field version)"
echo "make-local-release.sh: SynthyBoy $version -> $out_dir"

built=()
skipped=()

# --- linux-armv7 (Docker) -----------------------------------------------------------------------
if [[ "$do_armv7" == 1 ]]; then
    if [[ -z "$armv7_clap" ]]; then
        armv7_clap="$repo_root/build-armv7-clap/synthyboy.clap"
        set +e
        "$repo_root/scripts/build-armv7-clap.sh" --jobs "${jobs:-2}" --out "$armv7_clap"
        status=$?
        set -e
        if [[ "$status" == 3 ]]; then
            if [[ "$require_armv7" == 1 ]]; then die "linux-armv7: Docker is not usable (see the commands above)" 3; fi
            echo "make-local-release.sh: WARNING linux-armv7 NOT built: Docker is not usable. Run the commands printed above on a Linux host and pass the result with --armv7-clap." >&2
            armv7_clap=""
            skipped+=("linux-armv7 (no Docker)")
        elif [[ "$status" != 0 ]]; then
            die "the linux-armv7 build failed (exit $status)"
        fi
    fi
    if [[ -n "$armv7_clap" ]]; then
        "${py[@]}" "$repo_root/scripts/package-release.py" --platform linux-armv7 --native "$armv7_clap" --out "$out_dir"
        built+=(linux-armv7)
    fi
fi

# --- host platform ----------------------------------------------------------------------------------
host_platform=""
case "$(uname -s)-$(uname -m)" in
    Darwin-*)         host_platform="macos-universal" ;;
    Linux-x86_64)     host_platform="linux-x86_64" ;;
    Linux-aarch64)    host_platform="linux-aarch64" ;;
    Linux-arm64)      host_platform="linux-aarch64" ;;
esac

if [[ "$do_host" == 1 ]]; then
    [[ -n "$host_platform" ]] || die "no release platform for this host ($(uname -s) $(uname -m)); use --no-host"
    package_args=(--platform "$host_platform")
    if [[ "$with_juce" == 1 ]]; then
        # JUCE + the native shell in one tree (scripts/build.sh); extra CMake arguments can be given in CMAKE_EXTRA_ARGS (e.g. FETCHCONTENT_SOURCE_DIR_*)
        build_dir="$repo_root/build-local-juce"
        cmake_args=(-DSYNTHYBOY_BUILD_TESTS=OFF)
        [[ "$host_platform" == macos-universal ]] && cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
        # shellcheck disable=SC2206
        [[ -n "${CMAKE_EXTRA_ARGS:-}" ]] && cmake_args+=(${CMAKE_EXTRA_ARGS})
        BUILD_DIR="$build_dir" "$repo_root/scripts/build.sh" -- "${cmake_args[@]}"
        [[ -n "$host_clap" ]] || host_clap="$build_dir/shell-clap/synthyboy.clap"
        art="$build_dir/SynthyBoy_artefacts/Release"
        package_args+=(--juce-clap "$art/CLAP/SynthyBoy.clap" --juce-vst3 "$art/VST3/SynthyBoy.vst3")
    elif [[ -z "$host_clap" ]]; then
        build_dir="$repo_root/build-local"
        cmake_args=(-DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF)
        [[ "$host_platform" == macos-universal ]] && cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
        # shellcheck disable=SC2206
        [[ -n "${CMAKE_EXTRA_ARGS:-}" ]] && cmake_args+=(${CMAKE_EXTRA_ARGS})
        cmake -S "$repo_root" -B "$build_dir" "${cmake_args[@]}"
        cmake --build "$build_dir" --target synthyboy_clap --parallel "${jobs:-$( (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 4)}"
        host_clap="$build_dir/shell-clap/synthyboy.clap"
    fi
    "${py[@]}" "$repo_root/scripts/package-release.py" "${package_args[@]}" --native "$host_clap" --out "$out_dir"
    built+=("$host_platform")
fi

[[ ${#built[@]} -gt 0 ]] || die "nothing was built (platforms: none); pass --armv7-clap / --host-clap or drop --no-armv7 / --no-host"

# --- content packs, manifest ---------------------------------------------------------------------------
"${py[@]}" "$repo_root/tools/contentpack/contentpack.py" build-all --manifest "$repo_root/plugin.json" --out "$out_dir"
"${py[@]}" "$repo_root/scripts/fill-manifest.py" --assets "$out_dir" --partial "$url_style" --sums "$out_dir/SHA256SUMS"
"${py[@]}" "$repo_root/scripts/check-manifest.py" --local "$out_dir/plugin.json.release"

echo
echo "make-local-release.sh: built: ${built[*]}"
[[ ${#skipped[@]} -eq 0 ]] || echo "make-local-release.sh: NOT built: ${skipped[*]}"
echo "make-local-release.sh: $out_dir/plugin.json.release (the manifest to give to the Kick installer; its urls are $([[ $url_style == --relative ]] && echo "relative to it" || echo "file:// urls"))"
ls -l "$out_dir"
