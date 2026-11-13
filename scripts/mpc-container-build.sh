#!/usr/bin/env bash
# SynthyBoy - the part of the MPC build that runs inside the Debian 12 container (called by scripts/build-mpc.sh): builds the VST2 shell for one
# architecture with the GNU cross compiler, checks the module and writes a stripped copy next to it.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Environment (all set by build-mpc.sh): ARCH (arm32 | arm64), JOBS, MPC_SDK_DIR, WITH_BENCH (0 | 1), HOST_UID, HOST_GID.
# Mounts: /src (read-only source tree), /build (CMake tree of this architecture), /build-bench (bench tree), /out (output directory), /mpc-sdk.
#
# Results: /build/shell-mpc/synthyboy_mpc.so (unstripped, kept for profiling), /build/shell-mpc/synthyboy_mpc.stripped.so (the one that is packaged) and,
# with WITH_BENCH=1, /out/bench-<ARCH>/synthyboy_bench and synthyboy_noteon_bench.
#
# The checks are the same for both architectures (see check_elf): ELF class and machine, DT_NEEDED limited to libc / libm / libdl / libpthread / the dynamic
# loader, VSTPluginMain as the only exported symbol before and after strip, and the highest referenced GLIBC symbol version at most 2.39 (the glibc of the
# MPC OS 3 firmware; Debian 12 itself has 2.36).

set -euo pipefail

me="mpc-container-build.sh"
GLIBC_MAX="2.39"

case "${ARCH:-}" in
    arm32)
        TRIPLE=arm-linux-gnueabihf
        TOOLCHAIN=/src/cmake/toolchains/armv7-linux-gnueabihf.cmake
        LOADER='ld-linux-armhf'
        FILE_RE='ELF 32-bit LSB.*ARM, EABI5'
        PKGS=(g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf)
        LABEL="ARMv7"
        ;;
    arm64)
        TRIPLE=aarch64-linux-gnu
        TOOLCHAIN=/src/cmake/toolchains/aarch64-linux-gnu.cmake
        LOADER='ld-linux-aarch64'
        FILE_RE='ELF 64-bit LSB.*ARM aarch64'
        PKGS=(g++-aarch64-linux-gnu gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu)
        LABEL="AArch64"
        ;;
    *) echo "$me: ARCH must be arm32 or arm64 (got '${ARCH:-}')" >&2; exit 2 ;;
esac
: "${JOBS:?}" "${MPC_SDK_DIR:?}"
WITH_BENCH="${WITH_BENCH:-0}"

trap 'chown -R "${HOST_UID:-0}:${HOST_GID:-0}" /build /build-bench /out 2>/dev/null || true' EXIT

need=()
command -v "$TRIPLE-g++" >/dev/null || need+=("${PKGS[0]}" "${PKGS[1]}")
command -v "$TRIPLE-readelf" >/dev/null || need+=("${PKGS[2]}")
command -v cmake >/dev/null || need+=(cmake)
command -v file >/dev/null || need+=(file)
command -v git >/dev/null || need+=(git ca-certificates)
command -v ninja >/dev/null || command -v make >/dev/null || need+=(make)
if [ "${#need[@]}" -gt 0 ]; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${need[@]}" ca-certificates
fi
cmake --version | head -1
"$TRIPLE-g++" --version | head -1
if command -v ninja >/dev/null 2>&1; then GEN="Ninja"; else GEN="Unix Makefiles"; fi

fail() { echo "$me: [$ARCH] $*" >&2; exit 1; }

# check_elf FILE KIND: the checks every produced binary has to pass. KIND is "module" (exports exactly VSTPluginMain) or "tool" (an executable).
check_elf() {
    local so="$1" kind="$2" desc needed bad
    desc="$(file "$so")"
    echo "$desc"
    echo "$desc" | grep -Eq "$FILE_RE" || fail "$so is not a $LABEL ELF file"
    if [ "$ARCH" = arm32 ]; then
        "$TRIPLE-readelf" -A "$so" | grep -E "Tag_CPU_arch:|Tag_ABI_VFP_args|Tag_FP_arch"
        "$TRIPLE-readelf" -A "$so" | grep -q "Tag_ABI_VFP_args: VFP registers" || fail "$so is not a hard-float build"
    fi
    needed="$("$TRIPLE-readelf" -d "$so" | grep NEEDED || true)"
    echo "$needed"
    bad="$(echo "$needed" | grep -Ev "\\[(libc|libm|libdl|libpthread|$LOADER)\\.so\\.[0-9]+\\]" | grep -v '^$' || true)"
    [ -z "$bad" ] || fail "$so needs libraries beyond libc/libm/libdl/libpthread/$LOADER: $bad"
    local top
    top="$("$TRIPLE-objdump" -T "$so" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/^GLIBC_//' | sort -uV | tail -1 || true)"
    echo "$so: highest GLIBC symbol version ${top:-none}"
    if [ -n "$top" ] && [ "$(printf '%s\n%s\n' "$top" "$GLIBC_MAX" | sort -V | tail -1)" != "$GLIBC_MAX" ]; then
        fail "$so references GLIBC_$top, the firmware has glibc $GLIBC_MAX"
    fi
    if "$TRIPLE-readelf" -V "$so" | grep -q 'GLIBCXX_\|CXXABI_'; then fail "$so references the dynamic libstdc++ (needs -static-libstdc++)"; fi
}

# ---- the plugin module ----------------------------------------------------------------------------------------------------------------------------
cmake -S /src -B /build -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=OFF -DSYNTHYBOY_BUILD_MPC_SHELL=ON \
    -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR="$MPC_SDK_DIR"
cmake --build /build --target synthyboy_mpc --parallel "$JOBS"
so=/build/shell-mpc/synthyboy_mpc.so
check_elf "$so" module
exported="$("$TRIPLE-nm" -D --defined-only "$so" | grep -w VSTPluginMain || true)"
[ -n "$exported" ] || fail "VSTPluginMain is not an exported symbol"
extra="$("$TRIPLE-nm" -D --defined-only "$so" | grep -v -w VSTPluginMain | grep -v '^$' || true)"
[ -z "$extra" ] || fail "unexpected exported symbols: $extra"
# The package gets a stripped copy; the unstripped module stays in the build tree for profiling.
stripped=/build/shell-mpc/synthyboy_mpc.stripped.so
"$TRIPLE-strip" --strip-unneeded -o "$stripped" "$so"
left="$("$TRIPLE-nm" -D --defined-only "$stripped" | awk '{print $NF}' | tr '\n' ' ')"
[ "$left" = "VSTPluginMain " ] || fail "exports after strip: $left"
check_elf "$stripped" module
ls -l "$so" "$stripped"

# ---- the benchmarks (same toolchain, tests switched on in a tree of their own so that the module above is built exactly as the package needs it) ----
if [ "$WITH_BENCH" = 1 ]; then
    cmake -S /src -B /build-bench -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=ON -DSYNTHYBOY_BUILD_MPC_SHELL=OFF \
        -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_SHELL_ENGINE=own -DMPC_SDK_DIR="$MPC_SDK_DIR" \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
    cmake --build /build-bench --target synthyboy_bench synthyboy_noteon_bench --parallel "$JOBS"
    rm -rf "/out/bench-$ARCH"
    mkdir -p "/out/bench-$ARCH"
    for t in synthyboy_bench synthyboy_noteon_bench; do
        cp "/build-bench/bench/$t" "/out/bench-$ARCH/$t"
        check_elf "/out/bench-$ARCH/$t" tool
    done
    ls -l "/out/bench-$ARCH"
fi
echo "$me: $LABEL build OK"
