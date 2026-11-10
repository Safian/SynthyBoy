#!/usr/bin/env bash
# SynthyBoy - builds the native CLAP shell (shell-clap, no JUCE) for Linux ARMv7 hard-float (Akai MPC Key 61) in a Debian 12 Docker container.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-armv7-clap.sh [--out FILE] [--jobs N] [--build-dir DIR] [--print-manual]
#
#   --out FILE        where the finished synthyboy.clap is copied (default: <repo>/build-armv7-clap/synthyboy.clap)
#   --jobs N          parallel compile jobs inside the container (default 2: -j4 runs a 8 GB Docker VM out of memory in cc1plus,
#                     see the Kick repo's docs/hardware/key61/tools/build_arm.sh; the release workflow passes $(nproc))
#   --build-dir DIR   CMake build tree on the host (default: <repo>/build-armv7-clap/tree); kept for incremental rebuilds
#   --print-manual    only print the commands to run on a Linux host without Docker, and exit
# Environment: ARMV7_IMAGE (default: the pinned Debian 12 image below; an image that already has the cross toolchain, cmake, ninja and
#              git, e.g. the Kick repo's kick-armv7-builder, also works: missing packages are only installed when absent).
#
# Why Debian 12: its toolchain (GCC 12.2, glibc 2.36, libstdc++ 3.4.30) is the one the Kick app for the MPC is built with
# (kick-armv7-builder), so the module needs nothing from the device that the Kick binary does not need already. The source tree is
# mounted read-only; nothing is written outside the build directory and the output file. Exit codes: 0 built, 2 usage, 3 Docker is
# not usable (the manual commands are printed), 1 build / check failed.

set -euo pipefail

IMAGE_DEFAULT="debian:12-slim@sha256:734a72b3d6142a71993b4bc580ca01d8ca687f77a2c422d96a961d3376b179bc"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_file="$repo_root/build-armv7-clap/synthyboy.clap"
build_dir="$repo_root/build-armv7-clap/tree"
jobs=2
print_manual=0
image="${ARMV7_IMAGE:-$IMAGE_DEFAULT}"

manual() {
    cat <<'EOF'
Docker is not usable. On a Debian 12 / Ubuntu x86_64 or aarch64 Linux host run (from the repository root):

  sudo apt-get install -y --no-install-recommends cmake ninja-build git ca-certificates file \
      g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf
  cmake -S . -B build-armv7-clap/tree -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armv7-linux-gnueabihf.cmake \
      -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF
  cmake --build build-armv7-clap/tree --target synthyboy_clap --parallel 2
  # the result: build-armv7-clap/tree/shell-clap/synthyboy.clap
  file build-armv7-clap/tree/shell-clap/synthyboy.clap     # ELF 32-bit LSB shared object, ARM, EABI5, hard-float
  arm-linux-gnueabihf-readelf -A build-armv7-clap/tree/shell-clap/synthyboy.clap

The glibc / libstdc++ of the build host are what the module needs on the device: build on Debian 12 (glibc 2.36), not on a newer distribution.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out_file="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --print-manual) print_manual=1; shift ;;
        -h|--help) sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-armv7-clap.sh: unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

if [[ "$print_manual" == 1 ]]; then
    manual
    exit 0
fi

[[ "$jobs" =~ ^[0-9]+$ && "$jobs" -ge 1 ]] || { echo "build-armv7-clap.sh: --jobs needs a positive number" >&2; exit 2; }

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    echo "build-armv7-clap.sh: Docker is not installed or its daemon is not running." >&2
    manual >&2
    exit 3
fi

mkdir -p "$build_dir" "$(dirname "$out_file")"
build_dir="$(cd "$build_dir" && pwd)"
out_dir="$(cd "$(dirname "$out_file")" && pwd)"
out_name="$(basename "$out_file")"

echo "build-armv7-clap.sh: image $image"
echo "build-armv7-clap.sh: source $repo_root (read-only), build tree $build_dir, jobs $jobs"

# Runs as root inside the container (apt needs it); the results are handed back to the host user at the end.
docker run --rm \
    -v "$repo_root":/src:ro -v "$build_dir":/build -v "$out_dir":/out \
    -e JOBS="$jobs" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e OUT_NAME="$out_name" \
    "$image" bash -euo pipefail -c '
        trap '"'"'chown -R "$HOST_UID:$HOST_GID" /build /out'"'"' EXIT
        need=()
        command -v arm-linux-gnueabihf-g++ >/dev/null || need+=(g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf)
        command -v arm-linux-gnueabihf-readelf >/dev/null || need+=(binutils-arm-linux-gnueabihf)
        command -v cmake >/dev/null || need+=(cmake)
        command -v ninja >/dev/null || need+=(ninja-build)
        command -v git >/dev/null || need+=(git ca-certificates)
        command -v file >/dev/null || need+=(file)
        if [ "${#need[@]}" -gt 0 ]; then
            apt-get update -qq
            DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${need[@]}" ca-certificates
        fi
        cmake --version | head -1
        arm-linux-gnueabihf-g++ --version | head -1
        cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/armv7-linux-gnueabihf.cmake \
            -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF
        cmake --build /build --target synthyboy_clap --parallel "$JOBS"
        clap=/build/shell-clap/synthyboy.clap
        file "$clap"
        arm-linux-gnueabihf-readelf -A "$clap" | grep -E "Tag_CPU_arch:|Tag_ABI_VFP_args|Tag_FP_arch|Tag_Advanced_SIMD"
        arm-linux-gnueabihf-nm -D --defined-only "$clap" | grep -w clap_entry
        # the module must not need more than the glibc / libstdc++ of Debian 12 (what the Kick binary for the MPC needs already)
        arm-linux-gnueabihf-readelf -d "$clap" | grep NEEDED
        cp "$clap" "/out/$OUT_NAME"
    '

# the same checks on the host, from the file itself (no tool of the container needed)
python3 - "$out_file" <<'EOF'
import struct, sys
data = open(sys.argv[1], "rb").read(64)
ok = data[:4] == b"\x7fELF" and data[4] == 1 and data[5] == 1 and struct.unpack_from("<H", data, 18)[0] == 40 \
    and (struct.unpack_from("<I", data, 36)[0] >> 24) == 5 and struct.unpack_from("<I", data, 36)[0] & 0x400
if not ok:
    sys.exit("build-armv7-clap.sh: %s is not an ELF32 ARM EABI5 hard-float binary" % sys.argv[1])
print("build-armv7-clap.sh: OK %s" % sys.argv[1])
EOF
