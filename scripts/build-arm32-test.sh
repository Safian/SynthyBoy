#!/bin/bash
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds ONE test / bench target of the repository for 32 bit ARM (armv7 hard float, static) in a Debian 12 container and copies the binary out.
# For the MPC Key 61 and the other 32 bit devices: copy the result to the device and run it there (never build arm64 for a Key 61 test).
#
# Usage: scripts/build-arm32-test.sh <cmake target> [--out DIR] [--jobs N] [--build-dir DIR] [--src DIR] [--cmake-arg ARG]...
#   target     a CMake target name of the test tree, e.g. synthyboy_test_own_memory
#   --out      where the binary is copied (default: ./arm32-out)
#   --jobs     parallel build jobs (default 4: the Safian build rule; run this script through
#              bash /Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh scripts/build-arm32-test.sh <target> ...)
#   --build-dir  CMake build tree kept between runs (default: <out>/build-tree)
#   --src      the source tree (default: the repository root of this script; use a git worktree of the commit to measure)
#   --cmake-arg  an extra CMake argument (repeatable)
# Environment: ARMV7_IMAGE (the Docker image; default debian:12-slim: the cross compilers are installed inside the container on every run;
#              an image that already has g++-arm-linux-gnueabihf, cmake and make skips the apt-get step), MPC_DOCKER (docker command).
# The flags: own engine, no Vital, no plugin, no MPC shell, tests ON, static link, toolchain cmake/toolchains/armv7-linux-gnueabihf.cmake.
set -euo pipefail
me=$(basename "$0")
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
target="${1:-}"
[[ -n "$target" && "$target" != -* ]] || { echo "usage: $me <cmake target> [--out DIR] [--jobs N] [--build-dir DIR] [--src DIR] [--cmake-arg ARG]..." >&2; exit 2; }
shift
out="$PWD/arm32-out"; jobs=4; build_dir=""; src="$here"; extra=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --src) src="$2"; shift 2 ;;
        --cmake-arg) extra+=("$2"); shift 2 ;;
        *) echo "$me: unknown argument $1" >&2; exit 2 ;;
    esac
done
[[ -f "$src/cmake/toolchains/armv7-linux-gnueabihf.cmake" ]] || { echo "$me: $src has no cmake/toolchains/armv7-linux-gnueabihf.cmake" >&2; exit 2; }
mkdir -p "$out"; out=$(cd "$out" && pwd)
[[ -n "$build_dir" ]] || build_dir="$out/build-tree"
mkdir -p "$build_dir"; build_dir=$(cd "$build_dir" && pwd); src=$(cd "$src" && pwd)
docker_cmd="${MPC_DOCKER:-docker}"
image="${ARMV7_IMAGE:-debian:12-slim}"
if ! command -v "$docker_cmd" >/dev/null 2>&1 || ! "$docker_cmd" info >/dev/null 2>&1; then
    echo "$me: Docker is not usable" >&2; exit 3
fi
extra_args=""
for a in "${extra[@]:-}"; do [[ -n "$a" ]] && extra_args+=" $(printf '%q' "$a")"; done
"$docker_cmd" run --rm -v "$src:/src:ro" -v "$build_dir:/build" -v "$out:/out" -e TARGET="$target" -e JOBS="$jobs" "$image" bash -c '
set -euo pipefail
if ! command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf cmake make file git ca-certificates
fi
cmake -S /src -B /build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/armv7-linux-gnueabihf.cmake \
    -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=ON -DSYNTHYBOY_BUILD_MPC_SHELL=OFF \
    -DCMAKE_EXE_LINKER_FLAGS=-static '"$extra_args"'
cmake --build /build --target "$TARGET" --parallel "$JOBS"
found=$(find /build -name "$TARGET" -type f | head -n 1)
[[ -n "$found" ]] || { echo "target binary $TARGET not found in the build tree" >&2; exit 1; }
cp "$found" /out/
file "/out/$TARGET"
'
echo "$me: /out/$target -> $out/$target"
