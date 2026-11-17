#!/usr/bin/env bash
# SynthyBoy - cross build of the quality profile benchmark (bench/tier_bench.cpp) for the MPCs: a fully static executable per architecture that needs nothing of the
# device but the kernel (and the factory preset files it is pointed at).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-tier-bench.sh [--arch arm32|arm64|all] [--out DIR] [--build-dir DIR] [--jobs N]
#
#   --arch       arm32 (ARMv7 hard-float with NEON: the MPC Key 61), arm64 (AArch64: the MPC XL) or all (default)
#   --out        output directory (default: <repo>/build-tier-bench/out); the binaries are <out>/tier-bench-arm32/synthyboy_tier_bench and
#                <out>/tier-bench-arm64/synthyboy_tier_bench
#   --build-dir  CMake trees (default: <repo>/build-tier-bench), one per architecture
#
# The same container, toolchain files and compiler flags as scripts/build-mpc.sh --bench (GNU cross compilers of Debian 12, cmake/toolchains/*.cmake, Release, the own
# engine without Vital), so the DSP code is the code of the packaged plugin; only the link differs (-static). Environment: MPC_BUILD_IMAGE (or ARMV7_IMAGE) names the
# Docker image (default debian:12-slim: the script then installs the cross compilers inside the container on every run; an image that has them is faster),
# MPC_SDK is not needed (the shell is not built).

set -euo pipefail

me="build-tier-bench.sh"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch="all"
out_dir=""
build_dir=""
jobs="$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) arch="$2"; shift 2 ;;
        --out) out_dir="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        -h|--help) sed -n '7,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "$me: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
done
case "$arch" in
    all) archs=(arm32 arm64) ;;
    arm32|arm64) archs=("$arch") ;;
    *) echo "$me: --arch must be arm32, arm64 or all" >&2; exit 2 ;;
esac
build_dir="${build_dir:-$repo_root/build-tier-bench}"
out_dir="${out_dir:-$build_dir/out}"
image="${MPC_BUILD_IMAGE:-${ARMV7_IMAGE:-debian:12-slim}}"
command -v docker >/dev/null || { echo "$me: docker not found" >&2; exit 1; }
mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"

for a in "${archs[@]}"; do
    mkdir -p "$build_dir/$a"
    tree="$(cd "$build_dir/$a" && pwd)"
    echo "$me: === $a (image $image) ==="
    docker run --rm -v "$repo_root":/src:ro -v "$tree":/build -v "$out_dir":/out -e ARCH="$a" -e JOBS="$jobs" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$image" bash -c '
        set -euo pipefail
        case "$ARCH" in
            arm32) TRIPLE=arm-linux-gnueabihf; TOOLCHAIN=/src/cmake/toolchains/armv7-linux-gnueabihf.cmake; PKGS="g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf" ;;
            arm64) TRIPLE=aarch64-linux-gnu; TOOLCHAIN=/src/cmake/toolchains/aarch64-linux-gnu.cmake; PKGS="g++-aarch64-linux-gnu gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu" ;;
        esac
        trap "chown -R $HOST_UID:$HOST_GID /build /out 2>/dev/null || true" EXIT
        need=""
        command -v "$TRIPLE-g++" >/dev/null || need="$need $PKGS"
        command -v cmake >/dev/null || need="$need cmake"
        command -v file >/dev/null || need="$need file"
        command -v git >/dev/null || need="$need git ca-certificates"
        command -v ninja >/dev/null || command -v make >/dev/null || need="$need make"
        if [ -n "$need" ]; then apt-get update -qq; DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $need ca-certificates; fi
        if command -v ninja >/dev/null 2>&1; then GEN="Ninja"; else GEN="Unix Makefiles"; fi
        "$TRIPLE-g++" --version | head -1
        cmake -S /src -B /build -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
            -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=ON -DSYNTHYBOY_BUILD_MPC_SHELL=OFF -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_SHELL_ENGINE=own \
            -DSYNTHYBOY_TIER_BENCH_STATIC=ON
        cmake --build /build --target synthyboy_tier_bench --parallel "$JOBS"
        rm -rf "/out/tier-bench-$ARCH"
        mkdir -p "/out/tier-bench-$ARCH"
        "$TRIPLE-strip" -o "/out/tier-bench-$ARCH/synthyboy_tier_bench" /build/bench/synthyboy_tier_bench
        desc="$(file "/out/tier-bench-$ARCH/synthyboy_tier_bench")"
        echo "$desc"
        echo "$desc" | grep -q "statically linked" || { echo "the binary is not static" >&2; exit 1; }
        ls -l "/out/tier-bench-$ARCH/synthyboy_tier_bench"
    '
    echo "$me: $out_dir/tier-bench-$a/synthyboy_tier_bench"
done
