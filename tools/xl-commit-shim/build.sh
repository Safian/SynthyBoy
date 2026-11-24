#!/bin/bash
# SynthyBoy - MPC XL / Key 61 atomic commit shim cross-compilation script.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds xl-commit-shim-<arch>.so inside the Debian 12 Docker container (debian:12-slim by default;
# cross compiler installed inside the container unless already present; MPC_BUILD_IMAGE overrides).
# Usage: tools/xl-commit-shim/build.sh [aarch64|armv7|all]

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
IMAGE="${MPC_BUILD_IMAGE:-debian:12-slim}"
TARGET="${1:-aarch64}"

case "$TARGET" in
    aarch64|arm64)
        TARGETS="aarch64"
        ;;
    armv7|arm32)
        TARGETS="armv7"
        ;;
    all)
        TARGETS="aarch64 armv7"
        ;;
    *)
        echo "Usage: $0 [aarch64|armv7|all]" >&2
        exit 2
        ;;
esac

# Ensure we hold a build slot if buildslot.sh is present and slot not already acquired
BUILDSLOT="/Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh"
if [ -z "${BUILDSLOT_ACQUIRED:-}" ] && [ -x "$BUILDSLOT" ]; then
    export BUILDSLOT_ACQUIRED=1
    exec bash "$BUILDSLOT" "$0" "$@"
fi

build_one() {
    local arch="$1"
    local out="$HERE/xl-commit-shim-$arch.so"
    local pkg="" cc="" cflags=""
    if [ "$arch" = "aarch64" ]; then
        pkg="gcc-aarch64-linux-gnu libc6-dev-arm64-cross"
        cc="aarch64-linux-gnu-gcc"
        cflags="-O2 -fPIC -shared -Wall -Wextra -Werror -pthread"
    else
        pkg="gcc-arm-linux-gnueabihf libc6-dev-armhf-cross"
        cc="arm-linux-gnueabihf-gcc"
        cflags="-O2 -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -shared -Wall -Wextra -Werror -pthread"
    fi

    echo "Building $arch ($out) using container $IMAGE..."
    docker run --rm \
      -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
      -e ARCH="$arch" -e PKG="$pkg" -e CC="$cc" -e CFLAGS="$cflags" \
      -v "$REPO_ROOT":/src:ro \
      -v "$HERE":/out \
      "$IMAGE" \
      bash -c '
set -euo pipefail
if ! command -v "$CC" >/dev/null 2>&1; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $PKG
fi
$CC $CFLAGS /src/tools/xl-commit-shim/xl-commit-shim.c -o "/out/xl-commit-shim-${ARCH}.so" -ldl
chown "$HOST_UID:$HOST_GID" "/out/xl-commit-shim-${ARCH}.so"
'

    if [ ! -f "$out" ]; then
        echo "Error: Failed to build $out" >&2
        exit 1
    fi
    if [ "$arch" = "aarch64" ]; then
        ln -sf "xl-commit-shim-aarch64.so" "$HERE/xl-commit-shim.so"
    fi
    echo "Build successful: $out"
    file "$out"
}

for arch in $TARGETS; do
    build_one "$arch"
done
