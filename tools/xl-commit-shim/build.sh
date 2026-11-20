#!/bin/bash
# SynthyBoy - MPC XL atomic commit measurement shim cross-compilation script.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds xl-commit-shim.so for aarch64 inside the Debian 12 Docker container
# (kick-armv7-builder:latest) using /usr/bin/aarch64-linux-gnu-gcc.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${MPC_BUILD_IMAGE:-kick-armv7-builder:latest}"
OUT="$HERE/xl-commit-shim.so"

# Ensure we hold a build slot if buildslot.sh is present and slot not already acquired
BUILDSLOT="/Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh"
if [ -z "${BUILDSLOT_ACQUIRED:-}" ] && [ -x "$BUILDSLOT" ]; then
    export BUILDSLOT_ACQUIRED=1
    exec bash "$BUILDSLOT" "$0" "$@"
fi

echo "Building aarch64 xl-commit-shim.so using container $IMAGE..."

docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "$HERE":/work \
  -w /work \
  "$IMAGE" \
  /usr/bin/aarch64-linux-gnu-gcc \
    -O2 -fPIC -shared -Wall -Wextra -pthread -ldl \
    /work/xl-commit-shim.c \
    -o /work/xl-commit-shim.so

if [ ! -f "$OUT" ]; then
    echo "Error: Failed to build $OUT" >&2
    exit 1
fi

echo "Build successful: $OUT"
file "$OUT"
