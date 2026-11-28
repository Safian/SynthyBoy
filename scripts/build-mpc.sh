#!/usr/bin/env bash
# SynthyBoy - builds the MPC VST2 shell (shell-mpc) for the 32-bit (ARMv7 hard-float: MPC Key 61 and the other 32-bit MPCs) and the 64-bit (AArch64: MPC XL,
# Live III) MPCs in a Debian 12 Docker container and assembles ONE plugin folder with both binaries.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The plugin is ALWAYS built with the overlay (SYNTHYBOY_MPC_OVERLAY=ON and, for ARM, the GPU helper next to it): the overlay-only skin has no host widgets, the native pages are the base
# and the helper only adds the live curves. There is no switch to turn it off.
#
# Usage: scripts/build-mpc.sh [--arch arm32|arm64|all] [--out DIR] [--jobs N] [--build-dir DIR] [--skins DIR] [--cmake-arg ARG]... [--bench] [--release] [--skin-only] [--no-validate] [--xpl-presets xl|k61|both|off]
#
#   --arch A         arm32 (VST/synthyboy-arm32.so, "file32"), arm64 (VST/synthyboy-arm64.so, "file64") or all (default: both)
#   --out DIR        where the finished plugin folder is assembled (default: per-checkout temporary MPC package)
#   --jobs N         parallel compile jobs inside the container (default 2)
#   --build-dir DIR  root of the CMake trees on the host; one sub directory per architecture (default: per-checkout temporary MPC tree)
#   --skins DIR      copy this directory verbatim into Plugin Skins/ in the output folder (default: skin/ of the repository: the skin is edited
#                    directly there, nothing is generated)
#   --cmake-arg ARG  an extra argument of the CMake configure step of the plugin module, e.g. --cmake-arg -DSYNTHYBOY_MPC_LFO_LIVE_HZ=25 (repeatable; no
#                    spaces inside an argument). A CMake tree keeps the options of earlier runs in its cache: when one tree is used for several variants
#                    (--build-dir), pass every option of the variant each time. Not applied to the --bench trees.
#   --bench          also build synthyboy_bench and synthyboy_noteon_bench for each architecture (own CMake tree with SYNTHYBOY_BUILD_TESTS=ON, libstdc++
#                    linked statically) into <out>/bench-<arch>/; they are measurement tools and not part of the plugin folder
#   --release        the public release: -DSYNTHYBOY_MPC_DEV_LOG=OFF (no developer log, no diagnostic files in /tmp; the plugin binary holds no log tag: `strings synthyboy-*.so | grep -c "mpckit\|startup"`
#                    style checks are in docs/ui-components/LOGGING.md). Without it the build is what it always was (log and files on).
#   --skin-only      copy skin/ and run the SDK validation into <out>/ without C++ cross-compilation
#   --no-validate    skip the SDK validator at the end (for tests without binaries)
#   --xpl-presets P  preset browser (.xpl) set: xl, k61 or both copy the committed content/mpc-presets, off leaves it out (default: xl)
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
scratch="/tmp/synthyboy-$(id -u)/$(printf '%s\n' "$repo_root" | cksum | cut -d' ' -f1)/mpc"
out_dir="$scratch/package"
build_dir="$scratch/tree"
jobs=2
skins_dir=""
skin_only=0
with_bench=0
release=0
xpl_presets="xl"
validate=1
module="shell-mpc/synthyboy_mpc.so"; binary_base="synthyboy"
helper_path="shell-mpc/overlay/gpu-helper/synthyboy-overlay-gpu"
PLUGIN_NAME="Safian - VST - SynthyBoy"
cmake_args=()
image="${MPC_BUILD_IMAGE:-${ARMV7_IMAGE:-$IMAGE_DEFAULT}}"
docker_cmd="${MPC_DOCKER:-docker}"

usage() { sed -n '/^# Usage/,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch|--out|--jobs|--build-dir|--skins|--cmake-arg|--xpl-presets)
            [[ $# -ge 2 ]] || { echo "$me: $1 needs a value" >&2; exit 2; }
            case "$1" in
                --arch) arch="$2" ;;
                --out) out_dir="$2" ;;
                --jobs) jobs="$2" ;;
                --build-dir) build_dir="$2" ;;
                --skins) skins_dir="$2" ;;
                --xpl-presets) xpl_presets="$2" ;;
                --cmake-arg)
                    case "$2" in
                        -D?*|-U?*|-G?*|-T?*|-A?*) ;;
                        *) echo "$me: --cmake-arg needs a CMake option (for example -DNAME=VALUE), got: $2" >&2; exit 2 ;;
                    esac
                    case "$2" in
                        *[[:space:]]*) echo "$me: --cmake-arg: no spaces inside an argument: $2" >&2; exit 2 ;;
                    esac
                    case "$2" in
                        -DSYNTHYBOY_MPC_OVERLAY|-DSYNTHYBOY_MPC_OVERLAY=*|-USYNTHYBOY_MPC_OVERLAY)
                            echo "$me: --cmake-arg: the plugin is always built with the overlay (SYNTHYBOY_MPC_OVERLAY=ON), $2 is refused" >&2; exit 2 ;;
                    esac
                    cmake_args+=("$2") ;;
            esac
            shift 2 ;;
        --bench) with_bench=1; shift ;;
        --release) release=1; shift ;;
        --skin-only) skin_only=1; shift ;;
        --no-validate) validate=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "$me: unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

native_skin=OFF
if [[ -z "$skins_dir" ]]; then
    native_skin=ON
fi

if [[ "$release" == 1 ]]; then
    cmake_args+=("-DSYNTHYBOY_MPC_DEV_LOG=OFF" "-DSYNTHYBOY_MPC_DEBUG_HOOKS=OFF")
fi

case "$arch" in
    arm32) archs=(arm32) ;;
    arm64) archs=(arm64) ;;
    all) archs=(arm32 arm64) ;;
    *) echo "$me: --arch must be arm32, arm64 or all (got: $arch)" >&2; exit 2 ;;
esac
case "$xpl_presets" in
    xl|k61|both|off) ;;
    *) echo "$me: --xpl-presets must be xl, k61, both or off (got: $xpl_presets)" >&2; exit 2 ;;
esac
if [[ -n "$skins_dir" && ! -d "$skins_dir" ]]; then echo "$me: --skins: not a directory: $skins_dir" >&2; exit 2; fi
[[ "$jobs" =~ ^[0-9]+$ && "$jobs" -ge 1 ]] || { echo "$me: --jobs needs a positive number" >&2; exit 2; }

if [[ -z "${MPC_SDK:-}" ]]; then
    echo "$me: MPC_SDK environment variable is required" >&2
    exit 2
fi

if [[ "$skin_only" != 1 ]]; then
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
      -DMPC_SDK_DIR=$PWD/mpc-plugin-builder-sdk
  cmake --build build-mpc/tree/arm64 --target synthyboy_mpc --parallel 2
  # the result: build-mpc/tree/arm64/shell-mpc/synthyboy_mpc.so (strip it with aarch64-linux-gnu-strip --strip-unneeded)

The glibc / libstdc++ of the build host are what the module needs on the device: build on Debian 12 (glibc 2.36), not on a newer distribution (the MPC
OS 3 firmware has glibc 2.39).
EOF
        exit 3
    fi
fi

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
if [[ "$skin_only" != 1 ]]; then
    for a in "${archs[@]}"; do
        mkdir -p "$build_dir/$a" "$build_dir/$a-bench"
    done
    build_dir="$(cd "$build_dir" && pwd)"
fi

echo "$me: image $image, architectures: ${archs[*]}"
echo "$me: source $repo_root (read-only), build trees $build_dir/<arch>, jobs $jobs"

# ---- 1. the modules (and the benchmarks), one container per architecture ----------------------------------------------------------------------------
# Runs as root inside the container (apt needs it); the results are handed back to the host user at the end of mpc-container-build.sh.
if [[ "$skin_only" != 1 ]]; then
    for a in "${archs[@]}"; do
        echo "$me: === building $a ==="
        "$docker_cmd" run --rm \
            -v "$repo_root":/src:ro -v "$build_dir/$a":/build -v "$build_dir/$a-bench":/build-bench -v "$out_dir":/out -v "$MPC_SDK":/mpc-sdk:ro \
            -e ARCH="$a" -e JOBS="$jobs" -e NATIVE_SKIN="$native_skin" -e CMAKE_EXTRA_ARGS="${cmake_args[*]:-}" -e WITH_BENCH="$with_bench" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e MPC_SDK_DIR=/mpc-sdk \
            "$image" bash /src/scripts/mpc-container-build.sh
    done
fi

# ---- 2. the plugin folder (shared by both architectures) --------------------------------------------------------------------------------------------
plugin_folder="$out_dir/$PLUGIN_NAME"
# The SDK first writes a stock editor. Keep its intermediate tree out of
# the published package even when generation or final validation fails.
package_stage="$(mktemp -d "$out_dir/.synthyboy-build.XXXXXX")"
published_folder="$plugin_folder"
plugin_folder="$package_stage/$PLUGIN_NAME"
cleanup_stage() {
    if [[ -d "$package_stage/previous" && ! -e "$published_folder" ]]; then
        mv "$package_stage/previous" "$published_folder" || return 1
    fi
    rm -rf "$package_stage"
}
trap cleanup_stage EXIT
trap 'exit 1' HUP INT TERM
if [[ -d "$published_folder" ]]; then
    cp -Rp "$published_folder" "$plugin_folder"
fi
mkdir -p "$plugin_folder"
if [[ "$skin_only" != 1 ]]; then
    rm -rf "$plugin_folder/VST"
    mkdir -p "$plugin_folder/VST"
    for a in "${archs[@]}"; do
        cp "$build_dir/$a/${module%.so}.stripped.so" "$plugin_folder/VST/$binary_base-$a.so"
    done

    # The GPU helper of the overlay (always built, SYNTHYBOY_MPC_OVERLAY=ON, on ARM; scripts/mpc-container-build.sh): VST/synthyboy-overlay-gpu-<arch>, next to the plugin binary
    # (the plugin looks for it there).
    for a in "${archs[@]}"; do
        helper="$build_dir/$a/$helper_path.stripped"
        if [[ -f "$helper" ]]; then
            cp "$helper" "$plugin_folder/VST/$(basename "$helper_path")-$a"
            chmod 755 "$plugin_folder/VST/$(basename "$helper_path")-$a"
        fi
    done
fi

if [[ "$skin_only" != 1 ]]; then
    # Create Content/ folder (empty if nothing to put there)
    mkdir -p "$plugin_folder/Content"

    # Copy factory presets
    rm -rf "$plugin_folder/Content/presets"
    cp -R "$repo_root/content/presets" "$plugin_folder/Content/presets"
    # (the preset index, content/presets/index.json, is a committed file: docs/MPC.md "The preset index")
fi

# The .xpl presets of the Akai host's Sounds browser (<out>/Safian - VST - SynthyBoy/Presets/): the committed set content/mpc-presets/ (written with the XL's plugin path; the Key 61
# loads it as it is), copied as it is. --xpl-presets off leaves it out.
if [[ "$skin_only" != 1 && "$xpl_presets" != "off" ]]; then
    rm -rf "$plugin_folder/Presets"
    cp -R "$repo_root/content/mpc-presets" "$plugin_folder/Presets"
fi

if [[ "$skin_only" != 1 ]]; then
    # Copy the factory content packs (wavetables, samples, instruments, impulse responses: scripts/gen-content-pack.py writes them, they are committed) to Content/packs/<pack id>/:
    # that is where the plugin's content store looks for the files a preset names (pack id + file, shell-mpc/synthyboy_mpc.cpp addPackRoot). A pack folder is named by its id.
    rm -rf "$plugin_folder/Content/packs"
    for pack_dir in factory-wavetables factory-samples; do
        pack_id="$(sed -n 's/^  "id": "\(.*\)",$/\1/p' "$repo_root/content/$pack_dir/contentpack.json")"
        mkdir -p "$plugin_folder/Content/packs"
        cp -R "$repo_root/content/$pack_dir" "$plugin_folder/Content/packs/$pack_id"
    done
fi

# Plugin Skins/: the committed skin (skin/ in the repository, hand-maintained, never generated) or the directory given with --skins
rm -rf "$plugin_folder/Plugin Skins"
cp -R "${skins_dir:-$repo_root/skin}" "$plugin_folder/Plugin Skins"

# plugin.json and version.xml: the committed files (mpc-package/ of SynthyBoy, package/ of the other plugin) are copied; plugin.json keeps file32 / file64 only for the binaries that exist
# (a --skin-only folder without any binary keeps both). Nothing is generated.
pkg_src="$repo_root/mpc-package"
python3 -I - "$pkg_src/plugin.json" "$plugin_folder" "$skin_only" <<'PYTHON'
import json
import sys
from pathlib import Path

source, folder, skin_only = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3] == "1"
manifest = json.loads(source.read_text(encoding="utf-8"))
missing = [k for k in ("file32", "file64") if k in manifest and not (folder / manifest[k]).is_file()]
present = [k for k in ("file32", "file64") if k in manifest and k not in missing]
if not present and not skin_only:
    sys.exit("build-mpc.sh: none of the binaries named in %s is in %s" % (source, folder))
if present:
    for k in missing:
        del manifest[k]
(folder / "plugin.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
(folder / "version.xml").write_bytes((source.parent / "version.xml").read_bytes())
PYTHON
skin_checks=()
if [[ "$native_skin" == ON ]]; then
    skin_checks+=(--check-display-bindings)
fi
python3 "$repo_root/scripts/validate_synthyboy_skin.py" ${skin_checks[@]+"${skin_checks[@]}"} "$plugin_folder"

# ---- 3. the SDK's own validator on the finished folder ----------------------------------------------------------------------------------------------
# Its schema lists both file32 and file64 as required, while docs/plugin-folder-format.md of the SDK says a 32-bit MPC uses only file32 and a 64-bit one
# only file64. With both binaries built there is nothing to accept. A single-architecture build accepts exactly the one finding about the key of the
# architecture that was not built; any other error fails the build.
# Second accepted finding: the number of tabs of the skin's TUI.json is the number accepted (if > 32, any other count is an error). The Key 61
# (MPC 3.10.1.12) loads such a skin (docs/MPC.md).
if [[ "$validate" == 1 ]]; then
    echo "$me: running SDK validator on $plugin_folder"
    report="$(python3 "$MPC_SDK/tools/validate.py" "$plugin_folder" 2>&1 || true)"
    echo "$report"
    expected_missing=""
    [[ "$arch" == arm32 ]] && expected_missing="file64"
    [[ "$arch" == arm64 ]] && expected_missing="file32"
    other="$(echo "$report" | grep '^ERROR' || true)"
    # the number of tabs the skin has: exactly this finding is accepted if > 32
    layout_tabs="$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["pageData"]["tabs"]))' "$plugin_folder/Plugin Skins/TUI.json" 2>/dev/null || echo 15)"
    other="$(echo "$other" | grep -v "^ERROR  TUI.json: ${layout_tabs} tabs; at most 32 are supported\$" || true)"
    if [[ "$skin_only" == 1 ]]; then
        # no binary is built: the validator's findings about the missing binaries (the keys, or the files plugin.json names) are expected
        other="$(echo "$other" | grep -v "plugin.json is missing required keys: " | grep -Ev '^ERROR  plugin.json "file(32|64)" = .* does not exist in the plugin folder$' || true)"
    elif [[ -n "$expected_missing" ]]; then
        other="$(echo "$other" | grep -v "plugin.json is missing required keys: $expected_missing\$" || true)"
    fi
    if [[ -n "$other" ]] || ! echo "$report" | grep -q '^OK'; then echo "$me: SDK validator failed" >&2; exit 1; fi
    if [[ "$skin_only" == 1 ]] && echo "$report" | grep -q '^ERROR'; then
        echo "$me: note: validator reported missing binaries, expected for --skin-only"
    elif [[ -n "$expected_missing" ]] && echo "$report" | grep -q '^ERROR'; then
        echo "$me: note: the validator's $expected_missing finding is expected (--arch $arch builds one binary only)"
    fi
fi

# Serialize the directory replacement when builds share one output root.
python3 - "$plugin_folder" "$published_folder" "$package_stage/previous" <<'PYTHON'
import fcntl
import os
from pathlib import Path
import sys

staged, published, previous = map(Path, sys.argv[1:])
directory = os.open(published.parent, os.O_RDONLY)
try:
    fcntl.flock(directory, fcntl.LOCK_EX)
    if published.exists():
        published.rename(previous)
    try:
        staged.rename(published)
    except BaseException:
        if previous.exists():
            previous.rename(published)
        raise
finally:
    os.close(directory)
PYTHON
plugin_folder="$published_folder"
echo "$me: done $plugin_folder"
if [[ "$with_bench" == 1 ]]; then for a in "${archs[@]}"; do echo "$me: benchmarks: $out_dir/bench-$a/"; done; fi
