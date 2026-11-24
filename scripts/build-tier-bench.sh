#!/usr/bin/env bash
# SynthyBoy - cross build of the quality profile benchmark (bench/tier_bench.cpp) for the MPCs: a fully static executable per architecture that needs nothing of the
# device but the kernel (and the factory preset files it is pointed at).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-tier-bench.sh [--arch arm32|arm64|all] [--out DIR] [--build-dir DIR] [--jobs N] [--sections] [--dynamic] [--dry-run]
#
#   --arch       arm32 (ARMv7 hard-float with NEON: the MPC Key 61), arm64 (AArch64: the MPC XL) or all (default)
#   --out        output directory (default: <temporary checkout directory>/build-tier-bench/out); the binaries are <out>/tier-bench-arm32/synthyboy_tier_bench and
#                <out>/tier-bench-arm64/synthyboy_tier_bench
#   --build-dir  CMake trees (default: <temporary checkout directory>/build-tier-bench), one per architecture
#   --sections   also build the variant that times the engine by section (-DSYNTHYBOY_SECTION_TIMING=ON, a tree of its own per architecture) into
#                <out>/tier-bench-<arch>-sections/synthyboy_tier_bench: its --sections option prints where the time of a block goes. Its block times are a
#                little longer than the plain binary's (every scope reads the clock): use the plain binary for the budget numbers.
#   --dynamic    also build the variant linked like the plugin: dynamic glibc (the libm of the device through the PLT), static libstdc++ / libgcc
#                (-DSYNTHYBOY_TIER_BENCH_STATIC=OFF -DSYNTHYBOY_TIER_BENCH_STATIC_LIBCXX=ON) into <out>/tier-bench-<arch>-dyn/. Checked: it needs libm.so.6,
#                not libstdc++, and no GLIBC symbol newer than 2.39 (the device's glibc).
#   --dry-run    print one DRY-RUN line per architecture and variant and exit (no Docker, no directory is made)
# Every output directory also holds synthyboy_libm_probe and synthyboy_pmu_probe when the tree has these targets.
#
# The same container, toolchain files and compiler flags as scripts/build-mpc.sh --bench (GNU cross compilers of Debian 12, cmake/toolchains/*.cmake, Release, the own
# engine without Vital), so the DSP code is the code of the packaged plugin; only the link differs (-static). Environment: MPC_BUILD_IMAGE (or ARMV7_IMAGE) names the
# Docker image (default debian:12-slim: the script then installs the cross compilers inside the container on every run; an image that has them is faster),
# MPC_SDK is not needed (the shell is not built).

set -euo pipefail

me="build-tier-bench.sh"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scratch_root="/tmp/synthyboy-$(id -u)/$(printf '%s\n' "$repo_root" | cksum | cut -d' ' -f1)"
arch="all"
sections=0
dynamic=0
dry_run=0
out_dir=""
build_dir=""
jobs="$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) arch="$2"; shift 2 ;;
        --out) out_dir="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --sections) sections=1; shift ;;
        --dynamic) dynamic=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) sed -n '7,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "$me: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
done
case "$arch" in
    all) archs=(arm32 arm64) ;;
    arm32|arm64) archs=("$arch") ;;
    *) echo "$me: --arch must be arm32, arm64 or all" >&2; exit 2 ;;
esac
build_dir="${build_dir:-$scratch_root/build-tier-bench}"
out_dir="${out_dir:-$build_dir/out}"
image="${MPC_BUILD_IMAGE:-${ARMV7_IMAGE:-debian:12-slim}}"

variants=("")
[[ "$sections" == 1 ]] && variants+=("-sections")
[[ "$dynamic" == 1 ]] && variants+=("-dyn")
if [[ "$dry_run" == 1 ]]; then
    for a in "${archs[@]}"; do
        for variant in "${variants[@]}"; do
            v_static=ON; v_libcxx=OFF; v_sections=OFF
            [[ "$variant" == "-sections" ]] && v_sections=ON
            [[ "$variant" == "-dyn" ]] && { v_static=OFF; v_libcxx=ON; }
            echo "DRY-RUN arch=$a variant=$variant static=$v_static libcxx_static=$v_libcxx section_timing=$v_sections targets=synthyboy_tier_bench,synthyboy_libm_probe,synthyboy_pmu_probe out=$out_dir/tier-bench-$a$variant"
        done
    done
    exit 0
fi
command -v docker >/dev/null || { echo "$me: docker not found" >&2; exit 1; }
mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"

for a in "${archs[@]}"; do
  for variant in "${variants[@]}"; do
    mkdir -p "$build_dir/$a$variant"
    tree="$(cd "$build_dir/$a$variant" && pwd)"
    echo "$me: === $a$variant (image $image) ==="
    docker run --rm -v "$repo_root":/src:ro -v "$tree":/build -v "$out_dir":/out -e ARCH="$a" -e VARIANT="$variant" -e JOBS="$jobs" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$image" bash -c '
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
        case "$VARIANT" in
            -dyn) STATIC=OFF; LIBCXX=ON; SECTIONS=OFF ;;
            -sections) STATIC=ON; LIBCXX=OFF; SECTIONS=ON ;;
            *) STATIC=ON; LIBCXX=OFF; SECTIONS=OFF ;;
        esac
        cmake -S /src -B /build -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
            -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=ON -DSYNTHYBOY_BUILD_MPC_SHELL=OFF \
            -DSYNTHYBOY_TIER_BENCH_STATIC=$STATIC -DSYNTHYBOY_TIER_BENCH_STATIC_LIBCXX=$LIBCXX -DSYNTHYBOY_SECTION_TIMING=$SECTIONS
        # the probes only when the tree has them (the script works before and after they exist)
        targets="$(cmake --build /build --target help 2>/dev/null || true)"
        build_targets="synthyboy_tier_bench"
        for t in synthyboy_libm_probe synthyboy_pmu_probe; do
            if grep -q "$t" <<< "$targets"; then build_targets="$build_targets $t"; fi
        done
        for t in $build_targets; do cmake --build /build --target "$t" --parallel "$JOBS"; done
        rm -rf "/out/tier-bench-$ARCH$VARIANT"
        mkdir -p "/out/tier-bench-$ARCH$VARIANT"
        for t in $build_targets; do
            "$TRIPLE-strip" -o "/out/tier-bench-$ARCH$VARIANT/$t" "/build/bench/$t"
            desc="$(file "/out/tier-bench-$ARCH$VARIANT/$t")"
            echo "$desc"
            if [ "$STATIC" = ON ]; then
                echo "$desc" | grep -q "statically linked" || { echo "$t is not static" >&2; exit 1; }
            else
                echo "$desc" | grep -q "dynamically linked" || { echo "$t is not dynamically linked" >&2; exit 1; }
                needed="$("$TRIPLE-readelf" -d "/out/tier-bench-$ARCH$VARIANT/$t")"
                echo "$needed" | grep -q "libm.so.6" || { echo "$t does not need libm.so.6" >&2; exit 1; }
                if echo "$needed" | grep -q "libstdc++"; then echo "$t still needs libstdc++" >&2; exit 1; fi
                newest="$("$TRIPLE-objdump" -T "/out/tier-bench-$ARCH$VARIANT/$t" | grep -o "GLIBC_[0-9.]*" | sed "s/GLIBC_//" | sort -V | tail -1)"
                echo "$t: highest GLIBC symbol version $newest"
                [ "$(printf "%s\n2.39\n" "$newest" | sort -V | tail -1)" = "2.39" ] || { echo "$t needs GLIBC $newest, the device has 2.39" >&2; exit 1; }
            fi
            ls -l "/out/tier-bench-$ARCH$VARIANT/$t"
        done
    '
    echo "$me: $out_dir/tier-bench-$a$variant/synthyboy_tier_bench"
  done
done
