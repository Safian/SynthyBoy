#!/usr/bin/env bash
# SynthyBoy - builds the MPC VST2 shell (shell-mpc) for the 32-bit (ARMv7 hard-float: MPC Key 61 and the other 32-bit MPCs) and the 64-bit (AArch64: MPC XL,
# Live III) MPCs in a Debian 12 Docker container and assembles ONE plugin folder with both binaries.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-mpc.sh [--plugin DIR] [--arch arm32|arm64|all] [--out DIR] [--jobs N] [--build-dir DIR] [--skins DIR] [--skin-layout FILE] [--cmake-arg ARG]... [--bench] [--release] [--skin-only] [--no-validate] [--xpl-presets xl|k61|both|off] [--dump-tool PATH] [--dump-plugin PATH] [--chunks DIR]
#
#   --plugin DIR     build ANOTHER plugin made with the kit instead of SynthyBoy (migration step 12 of docs/ui-components/TEMPLATE.md), e.g. --plugin template-mpc: DIR (relative to the repository or
#                    absolute) holds mpc-plugin.conf, which says what to build (the CMake source folder, the target, the module, the helper, the binary names, the package manifest and the script that
#                    generates the skin); the presets, content, preset browser and the SynthyBoy skin generation are not part of it, the SDK validator is. Default: SynthyBoy, everything as below.
#   --arch A         arm32 (VST/synthyboy-arm32.so, "file32"), arm64 (VST/synthyboy-arm64.so, "file64") or all (default: both)
#   --out DIR        where the finished plugin folder is assembled (default: per-checkout temporary MPC package)
#   --jobs N         parallel compile jobs inside the container (default 2)
#   --build-dir DIR  root of the CMake trees on the host; one sub directory per architecture (default: per-checkout temporary MPC tree)
#   --skins DIR      copy this directory verbatim into Plugin Skins/ in the output folder instead of generating it
#                    (default: generated from the layout file with gen-mpc-ui.py --layout and the SDK's tools/gen_ui.py (through scripts/run-gen-ui.py),
#                    then scripts/apply-mpc-skin-extras.py)
#   --skin-layout F  the layout file of the generated skin (default: scripts/overlay-skin/mpc-ui-pages-overlay.json,
#                    the production layout, 22 tabs)
#   --cmake-arg ARG  an extra argument of the CMake configure step of the plugin module, e.g. --cmake-arg -DSYNTHYBOY_MPC_LFO_LIVE_HZ=25 (repeatable; no
#                    spaces inside an argument). A CMake tree keeps the options of earlier runs in its cache: when one tree is used for several variants
#                    (--build-dir), pass every option of the variant each time. Not applied to the --bench trees.
#   --bench          also build synthyboy_bench and synthyboy_noteon_bench for each architecture (own CMake tree with SYNTHYBOY_BUILD_TESTS=ON, libstdc++
#                    linked statically) into <out>/bench-<arch>/; they are measurement tools and not part of the plugin folder
#   --release        the public release: -DSYNTHYBOY_MPC_DEV_LOG=OFF (no developer log, no diagnostic files in /tmp; the plugin binary holds no log tag: `strings synthyboy-*.so | grep -c "mpckit\|startup"`
#                    style checks are in docs/ui-components/LOGGING.md). Without it the build is what it always was (log and files on).
#   --skin-only      run only skin generation (gen-mpc-ui.py, run-gen-ui.py, apply-mpc-skin-extras.py), artwork and SDK validation into <out>/ without C++ cross-compilation
#   --no-validate    skip the SDK validator at the end (for tests without binaries)
#   --xpl-presets P  preset browser (.xpl) generation: xl, k61, both, or off (default: xl)
#   --dump-tool P    path to native dump_mpc_chunks tool (default: auto-detected from build-native / build)
#   --dump-plugin P  path to native synthyboy_mpc library for the dump tool (default: auto-detected)
#   --chunks DIR     path to directory of pre-dumped preset chunks (bypasses dump-tool)
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
skin_layout="$repo_root/scripts/overlay-skin/mpc-ui-pages-overlay.json"
skin_only=0
with_bench=0
release=0
xpl_presets="xl"
dump_tool=""
dump_plugin=""
chunks_dir=""
XPL_PATH_MODE="${XPL_PATH_MODE:-shared}"
validate=1
plugin_dir=""   # --plugin: another plugin; empty = SynthyBoy
# what is built and how the binaries are named: SynthyBoy's (a plugin's mpc-plugin.conf overrides every one of them)
P_TARGET="synthyboy_mpc"; P_CMAKE_SOURCE=""; P_CMAKE_ARGS=""; P_MODULE="shell-mpc/synthyboy_mpc.so"; P_BINARY_BASE="synthyboy"
P_HELPER="shell-mpc/overlay/gpu-helper/synthyboy-overlay-gpu"; P_MANIFEST=""; P_SKIN_SCRIPT=""
PLUGIN_NAME="Safian - VST - SynthyBoy"
cmake_args=()
image="${MPC_BUILD_IMAGE:-${ARMV7_IMAGE:-$IMAGE_DEFAULT}}"
docker_cmd="${MPC_DOCKER:-docker}"

usage() { sed -n '/^# Usage/,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch|--out|--jobs|--build-dir|--skins|--skin-layout|--cmake-arg|--xpl-presets|--dump-tool|--dump-plugin|--chunks|--plugin)
            [[ $# -ge 2 ]] || { echo "$me: $1 needs a value" >&2; exit 2; }
            case "$1" in
                --arch) arch="$2" ;;
                --out) out_dir="$2" ;;
                --jobs) jobs="$2" ;;
                --build-dir) build_dir="$2" ;;
                --skins) skins_dir="$2" ;;
                --skin-layout) skin_layout="$2" ;;
                --xpl-presets) xpl_presets="$2" ;;
                --dump-tool) dump_tool="$2" ;;
                --dump-plugin) dump_plugin="$2" ;;
                --chunks) chunks_dir="$2" ;;
                --plugin) plugin_dir="$2" ;;
                --cmake-arg)
                    case "$2" in
                        -D?*|-U?*|-G?*|-T?*|-A?*) ;;
                        *) echo "$me: --cmake-arg needs a CMake option (for example -DNAME=VALUE), got: $2" >&2; exit 2 ;;
                    esac
                    case "$2" in
                        *[[:space:]]*) echo "$me: --cmake-arg: no spaces inside an argument: $2" >&2; exit 2 ;;
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

if [[ "$release" == 1 ]]; then
    cmake_args+=("-DSYNTHYBOY_MPC_DEV_LOG=OFF" "-DSYNTHYBOY_MPC_DEBUG_HOOKS=OFF")
fi

if [[ -n "$plugin_dir" ]]; then
    [[ "$plugin_dir" = /* ]] || plugin_dir="$repo_root/$plugin_dir"
    [[ -d "$plugin_dir" && -f "$plugin_dir/mpc-plugin.conf" ]] || { echo "$me: --plugin: $plugin_dir has no mpc-plugin.conf" >&2; exit 2; }
    plugin_dir="$(cd "$plugin_dir" && pwd)"
    # mpc-plugin.conf: plain NAME=value lines (read, not sourced): TARGET, CMAKE_SOURCE, CMAKE_ARGS, MODULE, BINARY_BASE, HELPER, MANIFEST, SKIN_SCRIPT
    while IFS='=' read -r key value; do
        key="${key//[[:space:]]/}"
        case "$key" in ''|'#'*) continue ;; esac
        value="${value%%#*}"; value="${value%"${value##*[![:space:]]}"}"; value="${value#"${value%%[![:space:]]*}"}"
        case "$key" in
            TARGET) P_TARGET="$value" ;;
            CMAKE_SOURCE) P_CMAKE_SOURCE="$value" ;;
            CMAKE_ARGS) P_CMAKE_ARGS="$value" ;;
            MODULE) P_MODULE="$value" ;;
            BINARY_BASE) P_BINARY_BASE="$value" ;;
            HELPER) P_HELPER="$value" ;;
            MANIFEST) P_MANIFEST="$plugin_dir/$value" ;;
            SKIN_SCRIPT) P_SKIN_SCRIPT="$plugin_dir/$value" ;;
            *) echo "$me: --plugin: $plugin_dir/mpc-plugin.conf: unknown key $key" >&2; exit 2 ;;
        esac
    done < "$plugin_dir/mpc-plugin.conf"
    for need in TARGET:"$P_TARGET" CMAKE_SOURCE:"$P_CMAKE_SOURCE" MODULE:"$P_MODULE" BINARY_BASE:"$P_BINARY_BASE" MANIFEST:"$P_MANIFEST"; do
        [[ -n "${need#*:}" ]] || { echo "$me: --plugin: ${need%%:*} is missing in $plugin_dir/mpc-plugin.conf" >&2; exit 2; }
    done
    [[ -f "$P_MANIFEST" ]] || { echo "$me: --plugin: manifest not found: $P_MANIFEST" >&2; exit 2; }
    [[ -n "$skins_dir" || -n "$P_SKIN_SCRIPT" ]] || { echo "$me: --plugin: give --skins DIR or SKIN_SCRIPT in mpc-plugin.conf" >&2; exit 2; }
    PLUGIN_NAME="$(python3 -I "$repo_root/scripts/gen-mpc-manifest.py" --package-manifest "$P_MANIFEST" --folder-name)" || exit 2
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
if [[ -n "$dump_tool" && ! -f "$dump_tool" ]]; then
    echo "$me: --dump-tool: file not found: $dump_tool" >&2
    exit 2
fi
if [[ -n "$chunks_dir" && ! -d "$chunks_dir" ]]; then
    echo "$me: --chunks: directory not found: $chunks_dir" >&2
    exit 2
fi
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
    plugin_env=()   # another plugin: what the container builds (SynthyBoy: nothing, the script's defaults)
    if [[ -n "$plugin_dir" ]]; then
        plugin_env=(-e PLUGIN_TARGET="$P_TARGET" -e PLUGIN_CMAKE_SOURCE="$P_CMAKE_SOURCE" -e PLUGIN_CMAKE_ARGS="$P_CMAKE_ARGS" -e PLUGIN_MODULE="$P_MODULE" -e PLUGIN_HELPER="$P_HELPER")
    fi
    for a in "${archs[@]}"; do
        echo "$me: === building $a ==="
        "$docker_cmd" run --rm \
            -v "$repo_root":/src:ro -v "$build_dir/$a":/build -v "$build_dir/$a-bench":/build-bench -v "$out_dir":/out -v "$MPC_SDK":/mpc-sdk:ro \
            -e ARCH="$a" -e JOBS="$jobs" -e CMAKE_EXTRA_ARGS="${cmake_args[*]:-}" -e WITH_BENCH="$with_bench" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e MPC_SDK_DIR=/mpc-sdk \
            ${plugin_env[@]+"${plugin_env[@]}"} "$image" bash /src/scripts/mpc-container-build.sh
    done
fi

# ---- 2. the plugin folder (shared by both architectures) --------------------------------------------------------------------------------------------
plugin_folder="$out_dir/$PLUGIN_NAME"
mkdir -p "$plugin_folder"
if [[ "$skin_only" != 1 ]]; then
    rm -rf "$plugin_folder/VST"
    mkdir -p "$plugin_folder/VST"
    for a in "${archs[@]}"; do
        cp "$build_dir/$a/${P_MODULE%.so}.stripped.so" "$plugin_folder/VST/$P_BINARY_BASE-$a.so"
    done

    # The GPU helper of the overlay (built only with SYNTHYBOY_MPC_OVERLAY ON on ARM, scripts/mpc-container-build.sh): VST/synthyboy-overlay-gpu-<arch>, next to the plugin binary
    # (the plugin looks for it there). The production package has no such target, so its VST/ folder is unchanged.
    for a in "${archs[@]}"; do
        helper="$build_dir/$a/$P_HELPER.stripped"
        if [[ -f "$helper" ]]; then
            cp "$helper" "$plugin_folder/VST/$(basename "$P_HELPER")-$a"
            chmod 755 "$plugin_folder/VST/$(basename "$P_HELPER")-$a"
        fi
    done
fi

if [[ -n "$plugin_dir" ]]; then
    # another plugin (--plugin): no presets, content packs or preset browser; its skin is its own script's (or --skins)
    rm -rf "$plugin_folder/Plugin Skins"
    if [[ -n "$skins_dir" ]]; then
        cp -R "$skins_dir" "$plugin_folder/Plugin Skins"
    else
        python3 "$P_SKIN_SCRIPT" --out "$plugin_folder" --sdk "$MPC_SDK"
    fi
else
    if [[ "$skin_only" != 1 ]]; then
        # Create Content/ folder (empty if nothing to put there)
        mkdir -p "$plugin_folder/Content"

        # Copy factory presets
        rm -rf "$plugin_folder/Content/presets"
        cp -R "$repo_root/content/presets" "$plugin_folder/Content/presets"
        # The preset index (one small file the plugin reads at start instead of scanning 136 files; docs/MPC.md "The preset index")
        python3 "$repo_root/scripts/build-preset-index.py" --presets-dir "$plugin_folder/Content/presets" --categories "$repo_root/scripts/factory-preset-categories.json"
    fi

    # Generate .xpl presets for the Akai host's Sounds browser (<out>/Safian - VST - SynthyBoy/Presets/)
    # Controlled by --xpl-presets (xl|k61|both|off), default: xl.
    # XPL_PATH_MODE="shared" (default): ONE set in the package, written with the XL's plugin path; the Key 61 loads it as it is (checked on the device: the list appears and the presets load, so the
    # <PLUGIN file=...> path need not match the device). XPL_PATH_MODE="device" writes device-specific paths: XL in the package, Key 61 in Presets-k61/ (scripts/mpc-fix-xpl-paths.py rewrites the path).
    if [[ "$skin_only" != 1 && "$xpl_presets" != "off" ]]; then
        have_chunks=0
        actual_chunks=""
        chunks_tmp=""

        if [[ -n "$chunks_dir" && -d "$chunks_dir" ]]; then
            have_chunks=1
            actual_chunks="$chunks_dir"
        else
            # Locate dump_mpc_chunks tool
            if [[ -z "$dump_tool" ]]; then
                for candidate in \
                    "$build_dir/../build-native/shell-mpc/dump_mpc_chunks" \
                    "$repo_root/build-native/shell-mpc/dump_mpc_chunks" \
                    "$scratch/../build/shell-mpc/dump_mpc_chunks" \
                    "$scratch/../accept/build-mpc/shell-mpc/dump_mpc_chunks" \
                    "$repo_root/build/shell-mpc/dump_mpc_chunks" \
                    "$repo_root/build/dump_mpc_chunks"; do
                    if [[ -x "$candidate" ]]; then
                        dump_tool="$candidate"
                        break
                    fi
                done
            fi

            # Locate native synthyboy_mpc library if dump_tool was found
            if [[ -n "$dump_tool" && -x "$dump_tool" ]]; then
                if [[ -z "$dump_plugin" ]]; then
                    dump_dir="$(dirname "$dump_tool")"
                    for candidate in \
                        "$dump_dir/synthyboy_mpc.dylib" \
                        "$dump_dir/synthyboy_mpc.so" \
                        "$repo_root/build-native/shell-mpc/synthyboy_mpc.dylib" \
                        "$repo_root/build-native/shell-mpc/synthyboy_mpc.so" \
                        "$scratch/../build/shell-mpc/synthyboy_mpc.dylib" \
                        "$scratch/../accept/build-mpc/shell-mpc/synthyboy_mpc.dylib" \
                        "$repo_root/build/shell-mpc/synthyboy_mpc.dylib" \
                        "$scratch/../build/shell-mpc/synthyboy_mpc.so" \
                        "$scratch/../accept/build-mpc/shell-mpc/synthyboy_mpc.so" \
                        "$repo_root/build/shell-mpc/synthyboy_mpc.so"; do
                        if [[ -f "$candidate" ]]; then
                            dump_plugin="$candidate"
                            break
                        fi
                    done
                fi
            fi

            if [[ -n "$dump_tool" && -x "$dump_tool" && -n "$dump_plugin" && -f "$dump_plugin" ]]; then
                chunks_tmp="$(mktemp -d)"
                "$dump_tool" "$dump_plugin" "$repo_root/content/presets" "$chunks_tmp"
                have_chunks=1
                actual_chunks="$chunks_tmp"
            fi
        fi

        if [[ "$have_chunks" == 1 ]]; then
            presets_folder="$plugin_folder/Presets"
            rm -rf "$presets_folder"
            mkdir -p "$presets_folder"

            primary_dev="xl"
            if [[ "$xpl_presets" == "k61" && "$XPL_PATH_MODE" == "device" ]]; then
                primary_dev="k61"
            fi

            python3 "$repo_root/scripts/gen-xpl-presets.py" --chunks "$actual_chunks" \
                --device "$primary_dev" --out "$presets_folder"

            # Under device mode with one package serving both devices, also produce Key 61 set
            if [[ "$XPL_PATH_MODE" == "device" ]]; then
                if [[ "$xpl_presets" == "xl" || "$xpl_presets" == "both" ]]; then
                    k61_out="$out_dir/Presets-k61"
                    rm -rf "$k61_out"
                    mkdir -p "$k61_out"
                    cp -R "$presets_folder/"* "$k61_out/"
                    python3 "$repo_root/scripts/mpc-fix-xpl-paths.py" --dir "$k61_out" \
                        --path "/sdcard/VST2 Plugins/$PLUGIN_NAME/VST/synthyboy-arm32.so" --quiet
                elif [[ "$xpl_presets" == "k61" ]]; then
                    xl_out="$out_dir/Presets-xl"
                    rm -rf "$xl_out"
                    mkdir -p "$xl_out"
                    cp -R "$presets_folder/"* "$xl_out/"
                    python3 "$repo_root/scripts/mpc-fix-xpl-paths.py" --dir "$xl_out" \
                        --path "/nvme/VST2 Plugins/$PLUGIN_NAME/VST/synthyboy-arm64.so" --quiet
                fi
            fi

            if [[ -n "${chunks_tmp:-}" && -d "$chunks_tmp" ]]; then
                rm -rf "$chunks_tmp"
            fi
        else
            echo "$me: note: native dump tool (dump_mpc_chunks) or native plugin library not found; skipping XPL preset generation (pass --dump-tool <path>, build 'dump_mpc_chunks', or pass --chunks <dir>)"
        fi
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

    # Plugin Skins/: the given directory, or generated from the page list
    rm -rf "$plugin_folder/Plugin Skins"
    if [[ -n "$skins_dir" ]]; then
        cp -R "$skins_dir" "$plugin_folder/Plugin Skins"
    else
        ui_tmp="$(mktemp -d)"
        python3 "$repo_root/scripts/gen-mpc-ui.py" --params "$repo_root/docs/params.json" --pages "$repo_root/docs/kick-pages.json" \
            --layout "$skin_layout" --out "$ui_tmp"
        python3 "$repo_root/scripts/run-gen-ui.py" --sdk "$MPC_SDK" --params "$ui_tmp/params-hostsim.json" --ui "$ui_tmp/ui.json" --out "$plugin_folder/Plugin Skins" --quiet
        # Graphical extras (scripts/mpc-skin-extras/*.py) on top of the generated skin; not for a given --skins directory
        python3 "$repo_root/scripts/apply-mpc-skin-extras.py" --skins "$plugin_folder/Plugin Skins" --params "$repo_root/docs/params.json" \
            --meta "$ui_tmp/skin-meta.json"
        rm -rf "$ui_tmp"
    fi

    # Ensure browser artwork tile (Plugin Skins/browser_images/soundsmode.png) is in the package
    if [[ ! -f "$plugin_folder/Plugin Skins/browser_images/soundsmode.png" ]]; then
        mkdir -p "$plugin_folder/Plugin Skins/browser_images"
        cp "$repo_root/scripts/mpc-skin-extras/browser_images/soundsmode.png" "$plugin_folder/Plugin Skins/browser_images/soundsmode.png"
    fi
fi

# Generate plugin.json (file32 / file64 for the binaries that exist) and version.xml
manifest_args=()
if [[ "$skin_only" == 1 ]]; then
    manifest_args+=("--allow-no-binary")
fi
if [[ -n "$plugin_dir" ]]; then
    python3 -I "$repo_root/scripts/gen-mpc-manifest.py" --package-manifest "$P_MANIFEST" --plugin-dir "$plugin_folder" ${manifest_args[@]+"${manifest_args[@]}"}
else
    python3 "$repo_root/scripts/gen-mpc-manifest.py" --plugin-dir "$plugin_folder" ${manifest_args[@]+"${manifest_args[@]}"}
fi

# ---- 3. the SDK's own validator on the finished folder ----------------------------------------------------------------------------------------------
# Its schema lists both file32 and file64 as required, while docs/plugin-folder-format.md of the SDK says a 32-bit MPC uses only file32 and a 64-bit one
# only file64. With both binaries built there is nothing to accept. A single-architecture build accepts exactly the one finding about the key of the
# architecture that was not built; any other error fails the build.
# Second accepted finding: the number of pages of the --skin-layout file is the number accepted (if > 32, any other count is an error). The Key 61
# (MPC 3.10.1.12) loads such a skin (docs/MPC.md).
if [[ "$validate" == 1 ]]; then
    echo "$me: running SDK validator on $plugin_folder"
    report="$(python3 "$MPC_SDK/tools/validate.py" "$plugin_folder" 2>&1 || true)"
    echo "$report"
    expected_missing=""
    [[ "$arch" == arm32 ]] && expected_missing="file64"
    [[ "$arch" == arm64 ]] && expected_missing="file32"
    other="$(echo "$report" | grep '^ERROR' || true)"
    # the number of tabs the layout file has (15 for the overlay production layout): exactly this finding is accepted if > 32
    layout_tabs="$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["pages"]))' "$skin_layout" 2>/dev/null || echo 15)"
    [[ -z "$plugin_dir" ]] || layout_tabs=0   # another plugin: no tab limit finding is expected
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

echo "$me: done $plugin_folder"
if [[ "$with_bench" == 1 ]]; then for a in "${archs[@]}"; do echo "$me: benchmarks: $out_dir/bench-$a/"; done; fi
