#!/usr/bin/env bash
# SynthyBoy - builds the MPC VST2 shell for the 32-bit MPCs (ARMv7 hard-float, Akai MPC Key 61). Thin wrapper kept for the existing calls and documents:
# it is scripts/build-mpc.sh --arch arm32 with the old default locations.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-armv7-mpc.sh [--out DIR] [--jobs N] [--build-dir DIR] [--skins DIR]
#
#   --out DIR        where the plugin folder is assembled (default: <repo>/build-armv7-mpc)
#   --build-dir DIR  root of the CMake trees (default: <repo>/build-armv7-mpc/tree; the arm32 tree is DIR/arm32)
#   the other options and the environment (MPC_SDK, ARMV7_IMAGE) are those of scripts/build-mpc.sh, which builds both architectures (the 64-bit one is for
#   the MPC XL) and is the entry point to use for the complete package.
#
# Exit codes: those of scripts/build-mpc.sh.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "${1:-}" in
    -h|--help) sed -n '/^# Usage/,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac
exec "$repo_root/scripts/build-mpc.sh" --arch arm32 --out "$repo_root/build-armv7-mpc" --build-dir "$repo_root/build-armv7-mpc/tree" "$@"
