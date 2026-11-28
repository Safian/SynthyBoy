#!/usr/bin/env bash
# SynthyBoy - one step device build: builds the package for ONE device's architecture, saves it under .hw-local/mpc-builds/ and installs it.
# Shared by scripts/build-k61.sh (Key 61, armv7) and scripts/build-xl.sh (XL, aarch64).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/build-k61.sh [--release] [--no-install] [--name NAME] [--jobs N] [-- BUILD-MPC-ARGS...]
#        scripts/build-xl.sh  (the same options)
#
#   --release     the public release mode (build-mpc.sh --release: no developer log, no diagnostic files)
#   --no-install  build and save only; install later with scripts/k61-install.sh / xl-install.sh "<saved folder>/Safian - VST - SynthyBoy"
#   --name NAME   the folder name under .hw-local/mpc-builds/ (default: <branch>-<commit>[-dirty-<time>]-<arch>[-release])
#   --jobs N      parallel compile jobs inside the container (default: the number of CPUs of this machine)
#   -- ARGS       passed to scripts/build-mpc.sh unchanged (e.g. -- --cmake-arg -DSYNTHYBOY_MPC_SPECTRUM=OFF)
#
# Environment: MPC_SDK (default: <main checkout>/../.hw-local/mpc-knowledge/docs-official/mpc-sdk), MPC_BUILDS (default: <main checkout>/../.hw-local/mpc-builds),
# and what build-mpc.sh and install-mpc-package.sh read (MPC_BUILD_IMAGE, MPC_DOCKER, SYNTHYBOY_SKIP_SKIN_VALIDATION).
#
# Order: build (into a staging folder next to the archive) -> move to the archive and add a README line -> install. The installer keeps no backup of the
# package it replaces: the archive is that backup, so a package worth keeping is never lost by a later run. An existing archive folder is never overwritten.
# Exit codes: 0 done, 2 usage, 1 build, archive or install failed.

set -euo pipefail

me="build-${1-}.sh"
case "${1-}" in
    k61) arch=arm32; arch_label=armv7;   presets=k61; installer=k61-install.sh ;;
    xl)  arch=arm64; arch_label=aarch64; presets=xl;  installer=xl-install.sh ;;
    *) echo "Use build-k61.sh or build-xl.sh." >&2; exit 2 ;;
esac
shift

usage() { sed -n '7,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The main checkout (also when this runs from a worktree): .hw-local/ lies next to it.
common_dir="$(git -C "$repo_root" rev-parse --path-format=absolute --git-common-dir)"
safian_root="$(cd "$common_dir/../.." && pwd)"

release=0
install=1
name=""
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
extra=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --release) release=1 ;;
        --no-install) install=0 ;;
        --name) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; name="$2"; shift ;;
        --jobs) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; jobs="$2"; shift ;;
        --) shift; extra=("$@"); break ;;
        -h|--help) usage; exit 0 ;;
        *) echo "$me: unknown option $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

export MPC_SDK="${MPC_SDK:-$safian_root/.hw-local/mpc-knowledge/docs-official/mpc-sdk}"
builds="${MPC_BUILDS:-$safian_root/.hw-local/mpc-builds}"
[[ -d "$MPC_SDK" ]] || { echo "$me: no MPC SDK at $MPC_SDK (set MPC_SDK)" >&2; exit 1; }
[[ -d "$builds" ]] || { echo "$me: no build archive at $builds (set MPC_BUILDS)" >&2; exit 1; }

commit="$(git -C "$repo_root" rev-parse --short=8 HEAD)"
branch="$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)"
if [[ "$branch" == HEAD ]]; then   # a detached worktree: name it after a branch whose tip is this commit (develop first), else "detached"
    tips="$(git -C "$repo_root" for-each-ref --points-at HEAD --format='%(refname:lstrip=-1)' refs/heads refs/remotes | grep -vx HEAD || true)"
    if grep -qx develop <<< "$tips"; then branch=develop; else branch="$(head -n 1 <<< "$tips")"; fi
    branch="${branch:-detached}"
fi
dirty=""
if [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=no)" ]]; then
    dirty="-dirty-$(date +%H%M%S)"
fi
if [[ -z "$name" ]]; then
    slug="$(printf '%s' "$branch" | tr '/' '-' | tr -c 'A-Za-z0-9._-' '-')"
    name="$slug-$commit$dirty-$arch_label"
    [[ "$release" == 1 ]] && name="$name-release"
fi
[[ "$name" != */* && "$name" != .* ]] || { echo "$me: --name must be a plain folder name" >&2; exit 2; }
saved="$builds/$name"
[[ ! -e "$saved" ]] || { echo "$me: $saved exists; pass another --name (an archived package is never overwritten)" >&2; exit 1; }

stage="$(mktemp -d "$builds/.build-$name.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
build_args=(--arch "$arch" --xpl-presets "$presets" --jobs "$jobs" --out "$stage")
[[ "$release" == 1 ]] && build_args+=(--release)
echo "$me: building $branch $commit$dirty for the $arch_label device into $saved"
"$repo_root/scripts/build-mpc.sh" "${build_args[@]}" ${extra[@]+"${extra[@]}"}

package="$stage/Safian - VST - SynthyBoy"
binary="$package/VST/synthyboy-$arch.so"
[[ -s "$binary" ]] || { echo "$me: the build left no $binary" >&2; exit 1; }
if command -v md5 > /dev/null; then sum="$(md5 -q "$binary")"; else sum="$(md5sum "$binary" | cut -d' ' -f1)"; fi

mkdir "$saved"
mv "$package" "$saved/"
note="- $name: $me, branch $branch $commit$dirty, $arch_label only$([[ "$release" == 1 ]] && echo ', release mode'), $(date '+%Y-%m-%d %H:%M')."
printf '%s\n    %s md5 %s\n' "$note" "$arch" "$sum" >> "$builds/README.md"
echo "$me: saved $saved ($arch md5 $sum)"

if [[ "$install" == 1 ]]; then
    "$repo_root/scripts/$installer" "$saved/Safian - VST - SynthyBoy"
    echo "$me: installed $name"
else
    echo "$me: not installed; install with scripts/$installer \"$saved/Safian - VST - SynthyBoy\""
fi
