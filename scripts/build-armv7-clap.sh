#!/usr/bin/env bash
# SynthyBoy - builds the native CLAP module (shell-clap, with the bitmap GUI of docs/BITMAP-GUI-EXTENSION.md) for Linux ARMv7 hard-float (Akai MPC Key 61) in a Debian 12 Docker container.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-armv7-clap.sh [--out FILE] [--jobs N] [--build-dir DIR] [--no-ui] [--run-tests] [--print-manual]
#
#   --out FILE        where the finished synthyboy.clap is copied (default: <repo>/build-armv7-clap/synthyboy.clap)
#   --jobs N          parallel compile jobs inside the container (default 2: -j4 runs a 8 GB Docker VM out of memory in cc1plus,
#                     see the Kick repo's docs/hardware/key61/tools/build_arm.sh; the release workflow passes $(nproc))
#   --build-dir DIR   CMake build tree on the host (default: <repo>/build-armv7-clap/tree); kept for incremental rebuilds
#   --no-ui           the JUCE free shell without the bitmap GUI (the module before the bitmap GUI existed)
#   --run-tests       also cross compile the bitmap GUI test hosts (tests/bitmap-gui) and run them against the ARMv7 module under qemu-user inside the
#                     container (the emulated CPU is 20 to 50 times slower than an MPC: correctness, not speed; the tests scale their waits)
#   --print-manual    only print the commands to run on a Linux host without Docker, and exit
# Environment: ARMV7_IMAGE (default: the pinned Debian 12 image below; an image that already has the cross toolchain, cmake, ninja and
#              git, e.g. the Kick repo's kick-armv7-builder, also works: missing packages are only installed when absent).
#
# Why Debian 12: its toolchain (GCC 12.2, glibc 2.36, libstdc++ 3.4.30) is the one the Kick app for the MPC is built with
# (kick-armv7-builder), so the module needs nothing from the device that the Kick binary does not need already. The source tree is
# mounted read-only; nothing is written outside the build directory and the output file. Exit codes: 0 built, 2 usage, 3 Docker is
# not usable (the manual commands are printed), 1 build / check failed.
#
# The module with the bitmap GUI links its own JUCE (the version pinned in CMakeLists.txt): it needs the JUCE sources (cloned in the container, no
# binary is downloaded; JUCE builds its helper tool for the container by itself), the development packages of
# FreeType (linked statically into the module), X11 (headers only: the module does not link libX11, JUCE loads it on demand and the GUI never
# opens a display) and ALSA (headers only, compiled out) for armhf, and checks that the module needs nothing but libc, libm, libstdc++ and libgcc_s
# (the libraries the Kick binary has) and exports only clap_entry and synthyboy_clap_get_entry.

set -euo pipefail

IMAGE_DEFAULT="debian:12-slim@sha256:734a72b3d6142a71993b4bc580ca01d8ca687f77a2c422d96a961d3376b179bc"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_file="$repo_root/build-armv7-clap/synthyboy.clap"
build_dir="$repo_root/build-armv7-clap/tree"  # the CMake build tree, plus _juce-src (the JUCE sources)
jobs=2
print_manual=0
with_ui=1
run_tests=0
image="${ARMV7_IMAGE:-$IMAGE_DEFAULT}"

manual() {
    cat <<'EOF'
Docker is not usable. On a Debian 12 / Ubuntu x86_64 or aarch64 Linux host run (from the repository root):

  sudo dpkg --add-architecture armhf && sudo apt-get update
  sudo apt-get install -y --no-install-recommends cmake ninja-build git ca-certificates file pkg-config build-essential \
      g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
      libfreetype-dev libfontconfig-dev libx11-dev libasound2-dev \
      libfreetype-dev:armhf libfontconfig-dev:armhf libx11-dev:armhf libxext-dev:armhf libxcomposite-dev:armhf libxcursor-dev:armhf \
      libxinerama-dev:armhf libxrandr-dev:armhf libxrender-dev:armhf libasound2-dev:armhf
  # JUCE (the tag of CMakeLists.txt); its configure step builds its helper tool juceaide for this machine by itself (the host libraries above)
  git clone --depth 1 --branch 8.0.17-security https://github.com/juce-framework/JUCE.git juce-src
  PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig \
  cmake -S . -B build-armv7-clap/tree -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armv7-linux-gnueabihf.cmake \
      -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BITMAP_GUI=ON -DSYNTHYBOY_BUILD_TESTS=OFF \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=$PWD/juce-src
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
        --no-ui) with_ui=0; shift ;;
        --run-tests) run_tests=1; shift ;;
        -h|--help) sed -n '2,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
    -e JOBS="$jobs" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e OUT_NAME="$out_name" -e WITH_UI="$with_ui" -e RUN_TESTS="$run_tests" \
    "$image" bash -euo pipefail -c '
        trap '"'"'chown -R "$HOST_UID:$HOST_GID" /build /out'"'"' EXIT
        need=()
        command -v arm-linux-gnueabihf-g++ >/dev/null || need+=(g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf)
        command -v arm-linux-gnueabihf-readelf >/dev/null || need+=(binutils-arm-linux-gnueabihf)
        command -v cmake >/dev/null || need+=(cmake)
        command -v ninja >/dev/null || need+=(ninja-build)
        command -v git >/dev/null || need+=(git ca-certificates)
        command -v file >/dev/null || need+=(file)
        extra_cmake=(-DSYNTHYBOY_BITMAP_GUI=OFF)
        if [ "$WITH_UI" = 1 ]; then
            # the module with the bitmap GUI: JUCE sources and host tool, armhf development packages (see the header of this script)
            dpkg --add-architecture armhf
            need+=(pkg-config build-essential libfreetype-dev libfontconfig-dev libx11-dev libasound2-dev
                   libfreetype-dev:armhf libfontconfig-dev:armhf libx11-dev:armhf libxext-dev:armhf libxcomposite-dev:armhf libxcursor-dev:armhf
                   libxinerama-dev:armhf libxrandr-dev:armhf libxrender-dev:armhf libasound2-dev:armhf)
            command -v git >/dev/null || need+=(git)
        fi
        if [ "${#need[@]}" -gt 0 ]; then
            apt-get update -qq
            DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${need[@]}" ca-certificates
        fi
        cmake --version | head -1
        arm-linux-gnueabihf-g++ --version | head -1
        if [ "$WITH_UI" = 1 ]; then
            juce_tag="$(sed -n "s/.*GIT_TAG[[:space:]]*\\(8\\.[0-9.]*-security\\).*/\\1/p" /src/CMakeLists.txt | head -1)"
            [ -n "$juce_tag" ] || { echo "build-armv7-clap.sh: no JUCE tag found in CMakeLists.txt" >&2; exit 1; }
            if [ ! -f /build/_juce-src/CMakeLists.txt ]; then
                git clone --depth 1 --branch "$juce_tag" https://github.com/juce-framework/JUCE.git /build/_juce-src
            fi
            extra_cmake=(-DSYNTHYBOY_BITMAP_GUI=ON -DFETCHCONTENT_SOURCE_DIR_JUCE=/build/_juce-src)
        fi
        export PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig
        tests=OFF; targets=(synthyboy_clap)
        if [ "$RUN_TESTS" = 1 ]; then tests=ON; targets+=(synthyboy_test_bitmap_gui synthyboy_test_bitmap_golden synthyboy_bitmap_gui_bench); fi
        cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/armv7-linux-gnueabihf.cmake \
            -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=$tests "${extra_cmake[@]}"
        cmake --build /build --target "${targets[@]}" --parallel "$JOBS"
        clap=/build/shell-clap/synthyboy.clap
        file "$clap"
        arm-linux-gnueabihf-readelf -A "$clap" | grep -E "Tag_CPU_arch:|Tag_ABI_VFP_args|Tag_FP_arch|Tag_Advanced_SIMD"
        arm-linux-gnueabihf-nm -D --defined-only "$clap" | grep -w clap_entry
        # the module must not need more than the glibc / libstdc++ of Debian 12 (what the Kick binary for the MPC needs already)
        needed="$(arm-linux-gnueabihf-readelf -d "$clap" | grep NEEDED)"
        echo "$needed"
        if [ "$WITH_UI" = 1 ]; then
            # ONLY the libraries of the Kick binary: no FreeType, fontconfig, ALSA, X11
            bad="$(echo "$needed" | grep -Ev "\\[(libc|libm|libstdc\\+\\+|libgcc_s|ld-linux-armhf)\\.so\\.[0-9]+\\]" || true)"
            if [ -n "$bad" ]; then echo "build-armv7-clap.sh: the module needs libraries beyond libc / libm / libstdc++ / libgcc_s:" >&2; echo "$bad" >&2; exit 1; fi
            NM=arm-linux-gnueabihf-nm bash /src/scripts/check-clap-exports.sh "$clap"
            echo "size: $(stat -c %s "$clap") bytes, stripped $(arm-linux-gnueabihf-strip -o /tmp/stripped.clap "$clap" && stat -c %s /tmp/stripped.clap) bytes"
        fi
        cp "$clap" "/out/$OUT_NAME"
    '

# The test hosts of tests/bitmap-gui against the ARMv7 module under qemu-user, in a SECOND container that has nothing but the ARMv7 C / C++ runtime libraries (what the MPC has): no X11, no
# FreeType, no ALSA. That is also the proof that the module loads and runs with exactly the libraries it lists as NEEDED. (A container with the armhf X11 libraries installed, like the build
# container, makes qemu-user crash inside libX11 when there is no X server: not the case on a native CPU, not on the MPC.) The emulated CPU is 20 to 50 times slower than an MPC: correctness,
# not speed; the tests scale their waits.
if [[ "$run_tests" == 1 ]]; then
    docker run --rm -v "$repo_root":/src:ro -v "$build_dir":/build:ro -e SCALE="${SYNTHYBOY_TEST_TIME_SCALE:-12}" "$image" bash -euo pipefail -c '
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends qemu-user libc6-armhf-cross libstdc++6-armhf-cross libgcc-s1-armhf-cross ca-certificates
        export SYNTHYBOY_CLAP_UNDER_TEST=/build/shell-clap/synthyboy.clap SYNTHYBOY_USER_DIR=/tmp/sbuser SYNTHYBOY_TEST_TIME_SCALE="$SCALE"
        for t in synthyboy_test_bitmap_gui synthyboy_test_bitmap_golden; do
            qemu-arm -L /usr/arm-linux-gnueabihf /build/tests/bitmap-gui/$t
        done
        echo "frame times under qemu-user (an emulated CPU, NOT an MPC):"
        SYNTHYBOY_TEST_TIME_SCALE=1 qemu-arm -L /usr/arm-linux-gnueabihf /build/tests/bitmap-gui/synthyboy_bitmap_gui_bench
    '
fi

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
