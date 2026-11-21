#!/bin/bash
# SynthyBoy - MPC XL atomic commit measurement shim cross-compilation script.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds xl-commit-shim.so for aarch64 inside the Debian 12 Docker container
# (debian:12-slim by default; the cross compiler is installed in the container unless the image already has it; MPC_BUILD_IMAGE overrides).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${MPC_BUILD_IMAGE:-debian:12-slim}"
OUT="$HERE/xl-commit-shim.so"

# Ensure we hold a build slot if buildslot.sh is present and slot not already acquired
BUILDSLOT="/Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh"
if [ -z "${BUILDSLOT_ACQUIRED:-}" ] && [ -x "$BUILDSLOT" ]; then
    export BUILDSLOT_ACQUIRED=1
    exec bash "$BUILDSLOT" "$0" "$@"
fi

echo "Building aarch64 xl-commit-shim.so using container $IMAGE..."

docker run --rm \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  -v "$HERE":/work \
  -w /work \
  "$IMAGE" \
  bash -c '
set -euo pipefail
if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends gcc-aarch64-linux-gnu libc6-dev-arm64-cross
fi
aarch64-linux-gnu-gcc -O2 -fPIC -shared -Wall -Wextra -pthread /work/xl-commit-shim.c -o /work/xl-commit-shim.so -ldl
chown "$HOST_UID:$HOST_GID" /work/xl-commit-shim.so
'

if [ ! -f "$OUT" ]; then
    echo "Error: Failed to build $OUT" >&2
    exit 1
fi

echo "Build successful: $OUT"
file "$OUT"
